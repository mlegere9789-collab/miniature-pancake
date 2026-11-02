//! Structure-of-arrays agent store with a deterministic LIFO free list.

use core_math::{Angle, Vec2Fx};

/// Handle to an agent slot. The `generation` guards against stale handles:
/// it is incremented every time the slot is despawned.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct AgentId {
    /// Slot index into the store.
    pub index: u32,
    /// Generation of the slot this id was issued for.
    pub generation: u32,
}

/// Initial (or snapshot) values of every per-agent field.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AgentInit {
    /// Position.
    pub pos: Vec2Fx,
    /// Velocity.
    pub vel: Vec2Fx,
    /// Facing angle.
    pub facing: Angle,
    /// Hit points.
    pub hp: u16,
    /// Free-form flag bits (meaning belongs to game rules).
    pub flags: u16,
    /// Team number.
    pub team: u8,
    /// Unit type number.
    pub unit: u32,
}

/// Structure-of-arrays agent storage.
///
/// Slots are reused through a LIFO free list, so allocation order is a pure
/// function of the spawn/despawn call sequence. Despawning clears the slot's
/// fields to their defaults and bumps its generation. Iteration is always in
/// ascending slot order.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AgentStore {
    pub(crate) pos: Vec<Vec2Fx>,
    pub(crate) vel: Vec<Vec2Fx>,
    pub(crate) facing: Vec<Angle>,
    pub(crate) hp: Vec<u16>,
    pub(crate) flags: Vec<u16>,
    pub(crate) team: Vec<u8>,
    pub(crate) unit: Vec<u32>,
    pub(crate) alive: Vec<bool>,
    pub(crate) generation: Vec<u32>,
    /// Free slot indices; the last element is reused first.
    pub(crate) free: Vec<u32>,
    alive_count: usize,
}

macro_rules! accessors {
    ($($field:ident, $setter:ident, $ty:ty, $what:literal;)*) => {$(
        #[doc = concat!("Returns the ", $what, " of a live agent, or `None` for a stale or dead id.")]
        pub fn $field(&self, id: AgentId) -> Option<$ty> {
            self.slot(id).map(|i| self.$field[i])
        }

        #[doc = concat!("Sets the ", $what, " of a live agent; returns `false` for a stale or dead id.")]
        pub fn $setter(&mut self, id: AgentId, value: $ty) -> bool {
            match self.slot(id) {
                Some(i) => {
                    self.$field[i] = value;
                    true
                }
                None => false,
            }
        }
    )*};
}

impl AgentStore {
    /// Creates an empty store.
    pub fn new() -> AgentStore {
        AgentStore::default()
    }

    /// Resolves an id to a slot index if it refers to a live agent of the right generation.
    fn slot(&self, id: AgentId) -> Option<usize> {
        let i = id.index as usize;
        if self.alive.get(i) == Some(&true) && self.generation.get(i) == Some(&id.generation) {
            Some(i)
        } else {
            None
        }
    }

    /// Spawns an agent, reusing the most recently freed slot if any (LIFO).
    pub fn spawn(&mut self, init: AgentInit) -> AgentId {
        self.alive_count += 1;
        if let Some(index) = self.free.pop() {
            let i = index as usize;
            self.pos[i] = init.pos;
            self.vel[i] = init.vel;
            self.facing[i] = init.facing;
            self.hp[i] = init.hp;
            self.flags[i] = init.flags;
            self.team[i] = init.team;
            self.unit[i] = init.unit;
            self.alive[i] = true;
            AgentId {
                index,
                generation: self.generation[i],
            }
        } else {
            let index = self.alive.len() as u32;
            self.pos.push(init.pos);
            self.vel.push(init.vel);
            self.facing.push(init.facing);
            self.hp.push(init.hp);
            self.flags.push(init.flags);
            self.team.push(init.team);
            self.unit.push(init.unit);
            self.alive.push(true);
            self.generation.push(0);
            AgentId {
                index,
                generation: 0,
            }
        }
    }

    /// Despawns an agent. Returns `false` (and changes nothing) for a stale or dead id.
    pub fn despawn(&mut self, id: AgentId) -> bool {
        let Some(i) = self.slot(id) else {
            return false;
        };
        self.pos[i] = Vec2Fx::default();
        self.vel[i] = Vec2Fx::default();
        self.facing[i] = Angle::default();
        self.hp[i] = 0;
        self.flags[i] = 0;
        self.team[i] = 0;
        self.unit[i] = 0;
        self.alive[i] = false;
        self.generation[i] = self.generation[i].wrapping_add(1);
        self.free.push(id.index);
        self.alive_count -= 1;
        true
    }

    /// Returns whether `id` refers to a live agent.
    pub fn is_alive(&self, id: AgentId) -> bool {
        self.slot(id).is_some()
    }

    /// Number of live agents.
    pub fn len(&self) -> usize {
        self.alive_count
    }

    /// Returns `true` if no agent is alive.
    pub fn is_empty(&self) -> bool {
        self.alive_count == 0
    }

    /// Total number of slots ever allocated (live plus free).
    pub fn capacity(&self) -> usize {
        self.alive.len()
    }

    /// Alias of [`AgentStore::capacity`] used by the hashing code.
    pub fn slot_count(&self) -> usize {
        self.alive.len()
    }

    /// The free list in stack order (the last element is reused first).
    pub fn free_list(&self) -> &[u32] {
        &self.free
    }

    /// Snapshot of all fields of a live agent.
    pub fn get(&self, id: AgentId) -> Option<AgentInit> {
        self.slot(id).map(|i| AgentInit {
            pos: self.pos[i],
            vel: self.vel[i],
            facing: self.facing[i],
            hp: self.hp[i],
            flags: self.flags[i],
            team: self.team[i],
            unit: self.unit[i],
        })
    }

    /// Iterates live agents in ascending slot order.
    pub fn iter_alive(&self) -> impl Iterator<Item = AgentId> + '_ {
        self.alive
            .iter()
            .zip(self.generation.iter())
            .enumerate()
            .filter(|(_, (alive, _))| **alive)
            .map(|(i, (_, generation))| AgentId {
                index: i as u32,
                generation: *generation,
            })
    }

    /// Live agent ids in ascending slot order.
    pub fn ids_alive(&self) -> Vec<AgentId> {
        self.iter_alive().collect()
    }

    accessors! {
        pos, set_pos, Vec2Fx, "position";
        vel, set_vel, Vec2Fx, "velocity";
        facing, set_facing, Angle, "facing angle";
        hp, set_hp, u16, "hit points";
        flags, set_flags, u16, "flag bits";
        team, set_team, u8, "team";
        unit, set_unit, u32, "unit type";
    }
}
