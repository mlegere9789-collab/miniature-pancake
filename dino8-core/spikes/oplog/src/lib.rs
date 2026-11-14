//! Dino 8 Spike B: an operation log with content-addressed snapshots, branching undo,
//! write-ahead durability, deterministic replay and CRDT merge.
//!
//! See `RESULTS.md` for what was measured and the recommendation.

pub mod commands;
pub mod crdt;
pub mod engine;
pub mod error;
pub mod exec;
pub mod hamt;
pub mod ids;
pub mod op;
pub mod replay;
pub mod script;
pub mod store;
pub mod value;
pub mod wal;

pub use engine::{Options, Workspace};
pub use error::{Error, Result};
