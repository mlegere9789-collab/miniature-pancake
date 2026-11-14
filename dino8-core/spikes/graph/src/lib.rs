//! Spike C: the incremental derived-data engine.
//!
//! See `RESULTS.md` for the design, the measurements and the recommendation.
#![deny(unsafe_code)]

pub mod abi;
pub mod engine;
pub mod hash;
pub mod workload;

pub use engine::{
    CancelProbe, CancelToken, Config, Ctx, Db, EditReport, EvalError, EvalOptions, EvalReport, Explanation, Interrupt, Kind, NodeId, NodeLabel,
    Priority, Rev, Value, Why,
};
pub use hash::{ContentHash, ContentHasher, Hash128};
