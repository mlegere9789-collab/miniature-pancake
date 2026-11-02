//! Simulation state, spatial index, state hashing and replay format.
//!
//! See `docs/specs/SPEC-002-sim-state.md`. This crate contains no game rules; it
//! provides the deterministic data foundations that rules run on:
//!
//! * [`AgentStore`]: structure-of-arrays agent storage with deterministic slot reuse.
//! * [`SpatialGrid`]: a counting-sort spatial index with exact, totally ordered queries.
//! * [`WorldState`], [`state_hash`], [`StateHasher`]: canonical encoding and FNV-1a 64 hashing.
//! * [`Replay`], [`ReplayWriter`], [`ReplayReader`]: the binary replay format, version 1.
//!
//! There is no floating point, no `unsafe`, and no hash-based container in
//! authoritative state; every iteration order is defined by slot index.
#![forbid(unsafe_code)]
#![deny(missing_docs)]

mod grid;
mod replay;
mod store;
mod world;

pub use core_math::{Angle, Fx, RngStreams, Vec2Fx};
pub use grid::{distance_sq_raw, SpatialGrid};
pub use replay::{
    Checkpoint, InputRecord, Replay, ReplayError, ReplayReader, ReplayWriter, REPLAY_MAGIC,
    REPLAY_VERSION,
};
pub use store::{AgentId, AgentInit, AgentStore};
pub use world::{
    canonical_bytes, fnv1a64, state_hash, StateHasher, WorldState, FNV_OFFSET_BASIS, FNV_PRIME,
};
