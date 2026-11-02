//! Deterministic math for the simulation core. See docs/specs/SPEC-001-core-math.md.
//!
//! Everything in the authoritative path is integer-only, so results are bit-identical on every
//! platform, compiler and optimisation level:
//!
//! * [`Fx`]: signed Q32.32 fixed point with saturating arithmetic and an integer square root.
//! * [`Angle`]: a full turn is 2^32; table-based [`Angle::sin`] / [`Angle::cos`] and CORDIC
//!   [`Angle::atan2`].
//! * [`Vec2Fx`]: a 2D vector of [`Fx`].
//! * [`RngStreams`] / [`Rng`]: xoshiro256** generators seeded through SplitMix64, with independent
//!   named streams derived from one master seed.
//!
//! Floating point appears only behind the off-by-default `float-io` feature
//! ([`Fx::from_f64`] / [`Fx::to_f64`]) and in tests.

#![forbid(unsafe_code)]

mod angle;
mod fx;
mod rng;
#[cfg(test)]
mod tablegen;
mod tables;
mod vec2;

pub use rng::{Rng, SplitMix64, StreamId};

/// Signed Q32.32 fixed-point number stored in an `i64` (value = raw / 2^32).
///
/// All arithmetic is saturating and independent of debug/release overflow checks; see the
/// individual operators for the exact definitions.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Fx(i64);

/// A 2D vector of fixed-point numbers.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct Vec2Fx {
    /// Horizontal component.
    pub x: Fx,
    /// Vertical component.
    pub y: Fx,
}

/// An angle where a full turn is 2^32 (wrapping arithmetic is intended).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Angle(u32);

/// Factory for independent named random streams derived from one master seed.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct RngStreams {
    master_seed: u64,
}
