//! The toy scenario: agents wander a walled arena and wear each other down when close.
//!
//! Nothing here is a game rule; it exists to exercise movement, spatial queries, named random
//! streams and despawn, all in [`Fx`] and integer arithmetic.
//!
//! # Tick order
//! 1. Every live agent, in ascending slot order, draws `(angle, magnitude)` from the `Wander`
//!    stream; its velocity is nudged by that acceleration times `1 / tick_hz`, then limited to
//!    [`MAX_SPEED_M_S`]. Its facing follows its velocity.
//! 2. Positions integrate by `vel / tick_hz` and reflect off the arena walls.
//! 3. The spatial grid is rebuilt.
//! 4. Every agent with another agent within [`NEIGHBOUR_RADIUS_M`] draws one coin from the
//!    `Skirmish` stream (in ascending slot order); winners lose one hp. This is evaluated against
//!    the grid as it stood after step 3, and damage is applied afterwards in slot order, so
//!    the result does not depend on the order of evaluation.
//! 5. Agents at zero hp despawn; the tick counter advances.
//!
//! # Replay encoding
//! The replay header carries the seed and tick rate. The replay format has no field for the
//! scenario size, so the size travels as one setup input at tick 0 (actor 0, sequence 0) whose
//! payload is `agents u32, width_m u32, height_m u32`, little-endian.

use core_math::{Angle, Fx, Rng, StreamId, Vec2Fx};
use sim_state::{
    state_hash, AgentId, AgentInit, Replay, ReplayError, ReplayWriter, RngStreams, SpatialGrid,
    WorldState,
};
use std::fmt;

/// Scenario id recorded in every replay header.
pub const SCENARIO_ID: &str = "toy-v1";
/// Default tick rate.
pub const DEFAULT_TICK_HZ: u16 = 20;
/// Default arena side in metres.
pub const DEFAULT_ARENA_M: u32 = 2000;
/// Largest accepted agent count.
pub const MAX_AGENTS: u32 = 1_000_000;
/// Smallest accepted arena side in metres.
pub const MIN_ARENA_M: u32 = 16;
/// Largest accepted arena side in metres.
pub const MAX_ARENA_M: u32 = 16_384;
/// Highest accepted tick rate.
pub const MAX_TICK_HZ: u16 = 1000;
/// Agents closer than this (metres) count as neighbours.
pub const NEIGHBOUR_RADIUS_M: i32 = 12;
/// Hit points every agent starts with.
pub const START_HP: u16 = 20;
/// Speed limit in metres per second.
pub const MAX_SPEED_M_S: i32 = 6;
/// Largest wander acceleration in metres per second squared.
pub const MAX_ACCEL_M_S2: i32 = 8;
/// Chance per tick, as `numerator / denominator`, that an agent with a neighbour loses one hp.
pub const DAMAGE_CHANCE: (u32, u32) = (1, 40);

/// Named random streams. Separate ids keep systems from perturbing each other.
const PLACEMENT: StreamId = StreamId::new(1);
const WANDER: StreamId = StreamId::new(2);
const SKIRMISH: StreamId = StreamId::new(3);

/// Size of the setup payload in bytes.
const SETUP_LEN: usize = 12;

/// Everything that determines a toy run besides the tick count.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ToyParams {
    /// Number of agents.
    pub agents: u32,
    /// Arena width in whole metres.
    pub width_m: u32,
    /// Arena height in whole metres.
    pub height_m: u32,
    /// Master seed.
    pub seed: u64,
    /// Ticks per second.
    pub tick_hz: u16,
}

impl ToyParams {
    /// Default arena and tick rate with the given agent count and seed.
    pub fn new(agents: u32, seed: u64) -> ToyParams {
        ToyParams {
            agents,
            width_m: DEFAULT_ARENA_M,
            height_m: DEFAULT_ARENA_M,
            seed,
            tick_hz: DEFAULT_TICK_HZ,
        }
    }

    fn validate(&self) -> Result<(), ScenarioError> {
        let bad = |what: String| Err(ScenarioError::InvalidParams(what));
        if self.agents > MAX_AGENTS {
            return bad(format!(
                "agents {} exceeds the limit {MAX_AGENTS}",
                self.agents
            ));
        }
        for (name, v) in [("width", self.width_m), ("height", self.height_m)] {
            if !(MIN_ARENA_M..=MAX_ARENA_M).contains(&v) {
                return bad(format!(
                    "arena {name} {v} m is outside {MIN_ARENA_M}..={MAX_ARENA_M}"
                ));
            }
        }
        if self.tick_hz == 0 || self.tick_hz > MAX_TICK_HZ {
            return bad(format!(
                "tick rate {} is outside 1..={MAX_TICK_HZ}",
                self.tick_hz
            ));
        }
        Ok(())
    }

    fn setup_payload(&self) -> Vec<u8> {
        let mut v = Vec::with_capacity(SETUP_LEN);
        v.extend_from_slice(&self.agents.to_le_bytes());
        v.extend_from_slice(&self.width_m.to_le_bytes());
        v.extend_from_slice(&self.height_m.to_le_bytes());
        v
    }
}

/// Why a scenario could not be built or a replay could not be used.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ScenarioError {
    /// A parameter is outside its accepted range.
    InvalidParams(String),
    /// The replay writer rejected the content.
    Replay(ReplayError),
}

impl fmt::Display for ScenarioError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ScenarioError::InvalidParams(m) => write!(f, "invalid parameters: {m}"),
            ScenarioError::Replay(e) => write!(f, "replay error: {e}"),
        }
    }
}

impl std::error::Error for ScenarioError {}

impl From<ReplayError> for ScenarioError {
    fn from(e: ReplayError) -> ScenarioError {
        ScenarioError::Replay(e)
    }
}

/// Smallest `c` with `c * c >= n`, at least 1.
fn ceil_sqrt(n: u32) -> u32 {
    let mut c = 1u32;
    while u64::from(c) * u64::from(c) < u64::from(n) {
        c += 1;
    }
    c
}

/// Reflects one coordinate off the walls at `0` and `max`; the velocity ends up pointing inward.
fn reflect(pos: Fx, vel: Fx, max: Fx) -> (Fx, Fx) {
    if pos < Fx::ZERO {
        ((-pos).clamp(Fx::ZERO, max), vel.abs())
    } else if pos > max {
        ((max + max - pos).clamp(Fx::ZERO, max), -vel.abs())
    } else {
        (pos, vel)
    }
}

/// A running toy scenario.
pub struct Scenario {
    world: WorldState,
    grid: SpatialGrid,
    wander: Rng,
    skirmish: Rng,
    width: Fx,
    height: Fx,
    dt: Fx,
    radius: Fx,
    max_speed: Fx,
    max_speed_sq: Fx,
    max_accel: Fx,
    ids: Vec<AgentId>,
    hits: Vec<AgentId>,
    damaged: Vec<AgentId>,
}

impl Scenario {
    /// Builds the scenario at tick 0: agents on a jittered grid, two teams in a checker pattern.
    pub fn new(params: &ToyParams) -> Result<Scenario, ScenarioError> {
        params.validate()?;
        let rng = RngStreams::new(params.seed);
        let mut world = WorldState::new(rng);
        let width = Fx::from_int(params.width_m as i32);
        let height = Fx::from_int(params.height_m as i32);

        let mut placement = rng.stream(PLACEMENT);
        let cols = ceil_sqrt(params.agents);
        let rows = params.agents.div_ceil(cols).max(1);
        let cell_w = width / Fx::from_int(cols as i32);
        let cell_h = height / Fx::from_int(rows as i32);
        for i in 0..params.agents {
            let (col, row) = (i % cols, i / cols);
            let jitter_x = placement.unit_fx();
            let jitter_y = placement.unit_fx();
            let facing = Angle::from_raw(placement.next_u32());
            let x = (Fx::from_int(col as i32) + jitter_x) * cell_w;
            let y = (Fx::from_int(row as i32) + jitter_y) * cell_h;
            world.agents.spawn(AgentInit {
                pos: Vec2Fx::new(x.clamp(Fx::ZERO, width), y.clamp(Fx::ZERO, height)),
                vel: Vec2Fx::ZERO,
                facing,
                hp: START_HP,
                flags: 0,
                team: ((col + row) & 1) as u8,
                unit: 0,
            });
        }

        let radius = Fx::from_int(NEIGHBOUR_RADIUS_M);
        let cells_x = params.width_m.div_ceil(NEIGHBOUR_RADIUS_M as u32);
        let cells_y = params.height_m.div_ceil(NEIGHBOUR_RADIUS_M as u32);
        let max_speed = Fx::from_int(MAX_SPEED_M_S);
        Ok(Scenario {
            grid: SpatialGrid::new(radius, cells_x, cells_y, Vec2Fx::ZERO),
            wander: rng.stream(WANDER),
            skirmish: rng.stream(SKIRMISH),
            width,
            height,
            dt: Fx::ONE / Fx::from_int(i32::from(params.tick_hz)),
            radius,
            max_speed,
            max_speed_sq: max_speed * max_speed,
            max_accel: Fx::from_int(MAX_ACCEL_M_S2),
            ids: Vec::new(),
            hits: Vec::new(),
            damaged: Vec::new(),
            world,
        })
    }

    /// The authoritative state.
    pub fn world(&self) -> &WorldState {
        &self.world
    }

    /// The canonical hash of the current state.
    pub fn hash(&self) -> u64 {
        state_hash(&self.world)
    }

    /// Advances the simulation by one tick.
    pub fn step(&mut self) {
        self.ids.clear();
        self.ids.extend(self.world.agents.iter_alive());
        self.move_agents();
        self.grid.rebuild(&self.world.agents);
        self.skirmish();
        self.world.advance_tick();
    }

    fn move_agents(&mut self) {
        let agents = &mut self.world.agents;
        for &id in &self.ids {
            let Some(a) = agents.get(id) else { continue };
            let angle = Angle::from_raw(self.wander.next_u32());
            let magnitude = self.wander.unit_fx() * self.max_accel;
            let nudge = Vec2Fx::from_angle(angle).scale(magnitude).scale(self.dt);
            let mut vel = a.vel + nudge;
            if vel.length_sq() > self.max_speed_sq {
                vel = vel.normalize().scale(self.max_speed);
            }
            let moved = a.pos + vel.scale(self.dt);
            let (x, vx) = reflect(moved.x, vel.x, self.width);
            let (y, vy) = reflect(moved.y, vel.y, self.height);
            let vel = Vec2Fx::new(vx, vy);
            agents.set_pos(id, Vec2Fx::new(x, y));
            agents.set_vel(id, vel);
            if vel != Vec2Fx::ZERO {
                agents.set_facing(id, Angle::atan2(vel.y, vel.x));
            }
        }
    }

    fn skirmish(&mut self) {
        self.damaged.clear();
        let agents = &self.world.agents;
        for &id in &self.ids {
            let Some(pos) = agents.pos(id) else { continue };
            self.grid.query_radius(pos, self.radius, &mut self.hits);
            let has_neighbour = self.hits.iter().any(|other| other.index != id.index);
            if has_neighbour && self.skirmish.coin(DAMAGE_CHANCE.0, DAMAGE_CHANCE.1) {
                self.damaged.push(id);
            }
        }
        let agents = &mut self.world.agents;
        for &id in &self.damaged {
            let hp = agents.hp(id).unwrap_or(0).saturating_sub(1);
            if hp == 0 {
                agents.despawn(id);
            } else {
                agents.set_hp(id, hp);
            }
        }
    }
}

/// Runs the scenario for `ticks` ticks and records a replay with a checkpoint hash every
/// `checkpoint_interval` ticks (`0` records none).
pub fn run_replay(
    params: &ToyParams,
    ticks: u64,
    checkpoint_interval: u32,
) -> Result<Replay, ScenarioError> {
    let mut scenario = Scenario::new(params)?;
    let mut writer = ReplayWriter::new(
        SCENARIO_ID,
        params.seed,
        params.tick_hz,
        0,
        checkpoint_interval,
    );
    writer.push_input(0, 0, 0, params.setup_payload());
    for tick in 1..=ticks {
        scenario.step();
        if checkpoint_interval > 0 && tick % u64::from(checkpoint_interval) == 0 {
            writer.push_checkpoint(tick, scenario.hash());
        }
    }
    Ok(writer.finish(ticks, scenario.hash())?)
}

/// The state hash after `ticks` ticks.
pub fn final_hash(params: &ToyParams, ticks: u64) -> Result<u64, ScenarioError> {
    let mut scenario = Scenario::new(params)?;
    for _ in 0..ticks {
        scenario.step();
    }
    Ok(scenario.hash())
}

/// The outcome of re-running a replay.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Verdict {
    /// Every checkpoint and the final hash matched.
    Match,
    /// The first difference: the tick, the hash stored in the replay and the hash computed now.
    Mismatch {
        /// First tick at which the hashes differ.
        tick: u64,
        /// Hash stored in the replay.
        expected: u64,
        /// Hash computed by the re-run.
        actual: u64,
    },
    /// The replay is not a usable toy replay.
    Malformed(String),
}

/// Reads the scenario parameters back out of a replay.
pub fn params_from_replay(replay: &Replay) -> Result<ToyParams, String> {
    if replay.scenario_id() != SCENARIO_ID {
        return Err(format!(
            "unknown scenario id {:?} (expected {SCENARIO_ID:?})",
            replay.scenario_id()
        ));
    }
    let setup = match replay.inputs() {
        [one] if one.tick == 0 && one.actor == 0 && one.seq == 0 => one,
        _ => return Err("expected exactly one setup input at tick 0".to_string()),
    };
    let p: &[u8; SETUP_LEN] = setup.payload.as_slice().try_into().map_err(|_| {
        format!(
            "setup payload is {} bytes, expected {SETUP_LEN}",
            setup.payload.len()
        )
    })?;
    let word = |i: usize| u32::from_le_bytes([p[i], p[i + 1], p[i + 2], p[i + 3]]);
    let params = ToyParams {
        agents: word(0),
        width_m: word(4),
        height_m: word(8),
        seed: replay.seed(),
        tick_hz: replay.tick_hz(),
    };
    params.validate().map_err(|e| e.to_string())?;
    Ok(params)
}

/// Re-runs `replay` from its header and compares every checkpoint and the final hash.
pub fn verify_replay(replay: &Replay) -> Verdict {
    let params = match params_from_replay(replay) {
        Ok(p) => p,
        Err(m) => return Verdict::Malformed(m),
    };
    let interval = u64::from(replay.checkpoint_interval());
    let total = replay.total_ticks();
    let expected_count = total.checked_div(interval).unwrap_or(0);
    let schedule_ok = replay.checkpoints().len() as u64 == expected_count
        && replay
            .checkpoints()
            .iter()
            .zip(1u64..)
            .all(|(c, n)| c.tick == n * interval);
    if !schedule_ok {
        return Verdict::Malformed(format!(
            "checkpoints do not follow the header interval {interval} over {total} ticks"
        ));
    }
    let mut scenario = match Scenario::new(&params) {
        Ok(s) => s,
        Err(e) => return Verdict::Malformed(e.to_string()),
    };
    let mut checkpoints = replay.checkpoints().iter().peekable();
    for tick in 1..=total {
        scenario.step();
        if let Some(c) = checkpoints.next_if(|c| c.tick == tick) {
            let actual = scenario.hash();
            if actual != c.hash {
                return Verdict::Mismatch {
                    tick,
                    expected: c.hash,
                    actual,
                };
            }
        }
    }
    let actual = scenario.hash();
    if actual != replay.final_hash() {
        return Verdict::Mismatch {
            tick: total,
            expected: replay.final_hash(),
            actual,
        };
    }
    Verdict::Match
}
