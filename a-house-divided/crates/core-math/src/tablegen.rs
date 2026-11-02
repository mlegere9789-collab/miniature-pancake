//! Integer-only generators for the committed lookup tables in `tables.rs` (test builds only).
//!
//! Everything is computed in `i128` with 60 fractional bits: pi by Machin's formula, sine by its
//! Taylor series and arctangent by its Taylor series. No floating point is involved.

use crate::tables::{ATAN_TURNS, SIN_QUARTER};

const P: u32 = 60;
const ONE: i128 = 1 << P;

fn qmul(a: i128, b: i128) -> i128 {
    (a * b) >> P
}

/// `atan(1/n)` in Q60 for integer `n >= 2`.
fn atan_inv(n: i128) -> i128 {
    let n2 = n * n;
    let mut term = ONE / n;
    let (mut sum, mut k, mut sign) = (0i128, 0i128, 1i128);
    loop {
        let t = term / (2 * k + 1);
        if t == 0 {
            return sum;
        }
        sum += sign * t;
        term /= n2;
        k += 1;
        sign = -sign;
    }
}

/// pi in Q60 (Machin: pi = 16 atan(1/5) - 4 atan(1/239)).
pub(crate) fn pi_q60() -> i128 {
    16 * atan_inv(5) - 4 * atan_inv(239)
}

/// `sin(x)` in Q60 for `0 <= x <= pi/2` (Taylor series).
fn sin_q60(x: i128) -> i128 {
    let x2 = qmul(x, x);
    let (mut term, mut sum, mut n) = (x, x, 1i128);
    loop {
        term = -qmul(term, x2) / ((n + 1) * (n + 2));
        n += 2;
        if term == 0 {
            return sum;
        }
        sum += term;
    }
}

/// Regenerates `SIN_QUARTER`: `round(sin(k * (pi/2) / 1024) * 2^32)` for `k = 0..=1024`.
pub(crate) fn sin_quarter() -> Vec<i64> {
    let pi = pi_q60();
    (0..=1024i128)
        .map(|k| {
            let s = sin_q60(pi * k / 2048);
            i64::try_from((s + (1 << 27)) >> 28).expect("table value fits i64")
        })
        .collect()
}

/// Regenerates `ATAN_TURNS`: `round(atan(2^-i) / (2 pi) * 2^32)` for `i = 0..32`.
pub(crate) fn atan_turns() -> Vec<u32> {
    let pi = pi_q60();
    let tau = 2 * pi;
    (0..32u32)
        .map(|i| {
            let atan = if i == 0 { pi / 4 } else { atan_inv(1i128 << i) };
            let turns = ((atan << 32) + tau / 2) / tau;
            u32::try_from(turns).expect("table value fits u32")
        })
        .collect()
}

fn render() -> String {
    let mut out = String::new();
    out.push_str(
        "//! Committed lookup tables. Regenerate with:\n\
         //! `CORE_MATH_WRITE_TABLES=/path/tables.rs cargo test -p core-math --lib -- --ignored write_tables`\n\
         //! The unit test `tables_match_regeneration` proves they equal the integer-only generator.\n\n",
    );
    out.push_str("/// `round(sin(k * (pi/2) / 1024) * 2^32)` for `k = 0..=1024` (one quadrant of sine, Q32).\n");
    out.push_str("pub(crate) const SIN_QUARTER: [i64; 1025] = [\n");
    for v in sin_quarter() {
        out.push_str(&format!("    {v},\n"));
    }
    out.push_str("];\n\n");
    out.push_str(
        "/// `round(atan(2^-i) / (2 pi) * 2^32)` for `i = 0..32`: CORDIC angles in 2^-32 turns.\n",
    );
    out.push_str("pub(crate) const ATAN_TURNS: [u32; 32] = [\n");
    for v in atan_turns() {
        out.push_str(&format!("    {v},\n"));
    }
    out.push_str("];\n");
    out
}

#[test]
fn tables_match_regeneration() {
    assert_eq!(sin_quarter(), SIN_QUARTER.to_vec());
    assert_eq!(atan_turns(), ATAN_TURNS.to_vec());
}

#[test]
fn generator_sanity() {
    let sin = sin_quarter();
    assert_eq!(sin[0], 0);
    assert_eq!(sin[1024], 1 << 32);
    assert_eq!(sin[512], 3_037_000_500); // sin(pi/4) = 0.70710678... * 2^32, rounded
    assert!(sin.windows(2).all(|w| w[0] < w[1]));
    // pi in Q60 must agree with the committed Fx::PI to the nearest raw unit.
    assert_eq!(
        i64::try_from((pi_q60() + (1 << 27)) >> 28).unwrap(),
        crate::Fx::PI.raw()
    );
    let atan = atan_turns();
    assert_eq!(atan[0], 1 << 29);
    assert!(atan.windows(2).all(|w| w[0] >= w[1]));
}

#[test]
#[ignore = "writes the table source; run manually when regenerating"]
fn write_tables() {
    let path = std::env::var("CORE_MATH_WRITE_TABLES").expect("set CORE_MATH_WRITE_TABLES");
    std::fs::write(path, render()).expect("write tables");
}
