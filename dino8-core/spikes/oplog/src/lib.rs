//! Dino 8 Spike B: an operation log with content-addressed snapshots, branching undo,
//! write-ahead durability, deterministic replay and CRDT merge.
//!
//! See `RESULTS.md` for what was measured and the recommendation.

pub mod ids;
pub mod value;
