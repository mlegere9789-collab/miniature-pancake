//! World state, canonical byte encoding and FNV-1a 64 state hashing.
//!
//! # Canonical byte layout (all integers little-endian, fixed width)
//!
//! ```text
//! tick          u64
//! rng seed      u64   (RngStreams master seed)
//! slot_count    u32
//! slot_count x slot, in ascending slot order:
//!     generation u32
//!     alive      u8    (0 or 1)
//!     pos.x      i64   (raw Q32.32)
//!     pos.y      i64
//!     vel.x      i64
//!     vel.y      i64
//!     facing     u32   (raw angle)
//!     hp         u16
//!     flags      u16
//!     team       u8
//!     unit       u32        -> 50 bytes per slot
//! free_len      u32
//! free_len x u32   free-list slot indices in stack order (first = oldest,
//!                  last = next to be reused)
//! ```
//!
//! Dead slots are included (their fields are zeroed by despawn), so the
//! generation counters and the free-list order are part of the hash.
//! The hash is FNV-1a 64 over exactly these bytes.

use crate::store::AgentStore;
use core_math::RngStreams;

/// FNV-1a 64-bit offset basis.
pub const FNV_OFFSET_BASIS: u64 = 0xcbf2_9ce4_8422_2325;
/// FNV-1a 64-bit prime.
pub const FNV_PRIME: u64 = 0x0000_0100_0000_01b3;

/// FNV-1a 64 of a byte slice. Not cryptographic; used for desync detection.
pub fn fnv1a64(bytes: &[u8]) -> u64 {
    let mut h = StateHasher::new();
    h.write_bytes(bytes);
    h.finish()
}

/// The authoritative simulation state.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WorldState {
    /// Current tick number.
    pub tick: u64,
    /// All agents.
    pub agents: AgentStore,
    /// Random stream factory (master seed).
    pub rng: RngStreams,
}

impl WorldState {
    /// Creates an empty world at tick 0.
    pub fn new(rng: RngStreams) -> WorldState {
        WorldState {
            tick: 0,
            agents: AgentStore::new(),
            rng,
        }
    }

    /// Advances the tick counter by one (wrapping at `u64::MAX`).
    pub fn advance_tick(&mut self) {
        self.tick = self.tick.wrapping_add(1);
    }
}

/// Incremental FNV-1a 64 hasher with helpers for the canonical encoding.
///
/// Feeding it the same bytes in any chunking gives the same result as
/// [`fnv1a64`]; [`StateHasher::write_world`] gives [`state_hash`].
#[derive(Clone, Copy, Debug)]
pub struct StateHasher {
    state: u64,
}

impl Default for StateHasher {
    fn default() -> StateHasher {
        StateHasher::new()
    }
}

impl StateHasher {
    /// Creates a hasher at the FNV offset basis.
    pub fn new() -> StateHasher {
        StateHasher {
            state: FNV_OFFSET_BASIS,
        }
    }

    /// Feeds raw bytes.
    pub fn write_bytes(&mut self, bytes: &[u8]) {
        for &b in bytes {
            self.state = (self.state ^ u64::from(b)).wrapping_mul(FNV_PRIME);
        }
    }

    /// Feeds one byte.
    pub fn write_u8(&mut self, v: u8) {
        self.write_bytes(&[v]);
    }

    /// Feeds a little-endian `u16`.
    pub fn write_u16(&mut self, v: u16) {
        self.write_bytes(&v.to_le_bytes());
    }

    /// Feeds a little-endian `u32`.
    pub fn write_u32(&mut self, v: u32) {
        self.write_bytes(&v.to_le_bytes());
    }

    /// Feeds a little-endian `u64`.
    pub fn write_u64(&mut self, v: u64) {
        self.write_bytes(&v.to_le_bytes());
    }

    /// Feeds a little-endian `i64`.
    pub fn write_i64(&mut self, v: i64) {
        self.write_bytes(&v.to_le_bytes());
    }

    /// Feeds the canonical header: tick, RNG master seed, slot count.
    pub fn write_header(&mut self, tick: u64, master_seed: u64, slot_count: usize) {
        self.write_u64(tick);
        self.write_u64(master_seed);
        self.write_u32(slot_count as u32);
    }

    /// Feeds the canonical encoding of one slot (live or dead).
    ///
    /// # Panics
    /// Panics if `slot >= agents.slot_count()`.
    pub fn write_slot(&mut self, agents: &AgentStore, slot: usize) {
        self.write_u32(agents.generation[slot]);
        self.write_u8(u8::from(agents.alive[slot]));
        self.write_i64(agents.pos[slot].x.raw());
        self.write_i64(agents.pos[slot].y.raw());
        self.write_i64(agents.vel[slot].x.raw());
        self.write_i64(agents.vel[slot].y.raw());
        self.write_u32(agents.facing[slot].raw());
        self.write_u16(agents.hp[slot]);
        self.write_u16(agents.flags[slot]);
        self.write_u8(agents.team[slot]);
        self.write_u32(agents.unit[slot]);
    }

    /// Feeds the canonical encoding of the free list.
    pub fn write_free_list(&mut self, agents: &AgentStore) {
        self.write_u32(agents.free.len() as u32);
        for &f in &agents.free {
            self.write_u32(f);
        }
    }

    /// Feeds the complete canonical encoding of a world.
    pub fn write_world(&mut self, world: &WorldState) {
        self.write_header(
            world.tick,
            world.rng.master_seed(),
            world.agents.slot_count(),
        );
        for slot in 0..world.agents.slot_count() {
            self.write_slot(&world.agents, slot);
        }
        self.write_free_list(&world.agents);
    }

    /// The hash of everything fed so far.
    pub fn finish(&self) -> u64 {
        self.state
    }
}

/// FNV-1a 64 over the canonical encoding of `world`.
pub fn state_hash(world: &WorldState) -> u64 {
    let mut h = StateHasher::new();
    h.write_world(world);
    h.finish()
}

/// The canonical byte encoding of `world` (see the module docs), mainly for tests.
pub fn canonical_bytes(world: &WorldState) -> Vec<u8> {
    let slots = world.agents.slot_count();
    let mut out = Vec::with_capacity(20 + slots * 50 + 4 + world.agents.free.len() * 4);
    out.extend_from_slice(&world.tick.to_le_bytes());
    out.extend_from_slice(&world.rng.master_seed().to_le_bytes());
    out.extend_from_slice(&(slots as u32).to_le_bytes());
    let a = &world.agents;
    for s in 0..slots {
        out.extend_from_slice(&a.generation[s].to_le_bytes());
        out.push(u8::from(a.alive[s]));
        out.extend_from_slice(&a.pos[s].x.raw().to_le_bytes());
        out.extend_from_slice(&a.pos[s].y.raw().to_le_bytes());
        out.extend_from_slice(&a.vel[s].x.raw().to_le_bytes());
        out.extend_from_slice(&a.vel[s].y.raw().to_le_bytes());
        out.extend_from_slice(&a.facing[s].raw().to_le_bytes());
        out.extend_from_slice(&a.hp[s].to_le_bytes());
        out.extend_from_slice(&a.flags[s].to_le_bytes());
        out.push(a.team[s]);
        out.extend_from_slice(&a.unit[s].to_le_bytes());
    }
    out.extend_from_slice(&(a.free.len() as u32).to_le_bytes());
    for &f in &a.free {
        out.extend_from_slice(&f.to_le_bytes());
    }
    out
}
