//! Dino 8 Spike B: an operation log with content-addressed snapshots, branching undo,
//! write-ahead durability, deterministic replay and CRDT merge.
//!
//! See `RESULTS.md` for what was measured and the recommendation.

pub mod crdt;
pub mod error;
pub mod hamt;
pub mod ids;
pub mod op;
pub mod store;
pub mod value;
pub mod wal;
