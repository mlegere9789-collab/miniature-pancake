//! `hd-sim`: a headless harness that runs a deliberately simple toy scenario and proves the
//! determinism machinery (SPEC-003).
//!
//! * [`scenario`]: the toy scenario, replay recording and verification. Integer and [`core_math::Fx`]
//!   arithmetic only; randomness only from named streams.
//! * [`golden`]: the parameters of the committed golden replays and the `expected.json` format.
//! * [`cli`]: the command-line front end (`run`, `verify`, `hash`, `bench`).
//! * [`bench`]: tick timing, the only place a wall clock is read.
#![forbid(unsafe_code)]
#![deny(missing_docs)]

pub mod bench;
pub mod cli;
pub mod golden;
pub mod scenario;
