//! Spike R1: proves the Rust/C++ boundary of Dino 8 with an operation-log probe.
//!
//! Layout: `status` (codes and error record), `log` (the safe log core), `handle`
//! (generation-checked handle table), `wal` (file backing), `ffi` (the C ABI, the only
//! module that contains unsafe code).
#![deny(unsafe_op_in_unsafe_fn)]

pub mod status;
