//! Spike R1: proves the Rust/C++ boundary of Dino 8 with an operation-log probe.
//!
//! Layout: [`status`] (codes and error record), [`log`] (the safe log core), [`handle`]
//! (generation-checked handle table), [`wal`] (file backing) and [`ffi`] (the C ABI, the only
//! module that contains unsafe code). The C header is generated from this crate by cbindgen; see
//! `scripts/gen_header.sh`.
#![deny(unsafe_op_in_unsafe_fn)]

#[forbid(unsafe_code)]
pub mod handle;
#[forbid(unsafe_code)]
pub mod log;
#[forbid(unsafe_code)]
pub mod status;
#[forbid(unsafe_code)]
mod sync;
#[forbid(unsafe_code)]
pub mod wal;

pub mod ffi;

pub use ffi::*;
pub use log::{D8LogInfo, D8ObjectId};
pub use status::*;
