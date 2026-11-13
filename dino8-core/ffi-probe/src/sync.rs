//! Synchronization primitives: the standard library normally, `loom` when compiled with
//! `RUSTFLAGS="--cfg loom"` so the concurrent log core can be model checked.

#[cfg(loom)]
pub use loom::sync::atomic::{AtomicU64, Ordering};
#[cfg(loom)]
pub use loom::sync::{Mutex, MutexGuard};

#[cfg(not(loom))]
pub use std::sync::atomic::{AtomicU64, Ordering};
#[cfg(not(loom))]
pub use std::sync::{Mutex, MutexGuard};
