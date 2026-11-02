//! Edge cases: every operation at MIN, MAX, ZERO, -1 and division by zero, with exact values.

use core_math::{Angle, Fx, Rng, RngStreams, StreamId, Vec2Fx};

const ONE: Fx = Fx::ONE;
const ZERO: Fx = Fx::ZERO;
const MAX: Fx = Fx::MAX;
const MIN: Fx = Fx::MIN;
const NEG_ONE_RAW: Fx = Fx::from_raw(-1);

fn r(raw: i64) -> Fx {
    Fx::from_raw(raw)
}

#[test]
fn constants() {
    assert_eq!(Fx::FRAC_BITS, 32);
    assert_eq!(ONE.raw(), 1i64 << 32);
    assert_eq!(Fx::HALF.raw(), 1i64 << 31);
    assert_eq!(Fx::EPSILON.raw(), 1);
    assert_eq!(MAX.raw(), i64::MAX);
    assert_eq!(MIN.raw(), i64::MIN);
    // round(pi * 2^32) and round(2 * pi * 2^32), computed with big-integer arithmetic.
    assert_eq!(Fx::PI.raw(), 13_493_037_705);
    assert_eq!(Fx::TAU.raw(), 26_986_075_409);
}

#[test]
fn add_saturates() {
    assert_eq!(MAX + ONE, MAX);
    assert_eq!(MAX + MAX, MAX);
    assert_eq!(MIN + (-ONE), MIN);
    assert_eq!(MIN + MIN, MIN);
    assert_eq!(MAX + MIN, NEG_ONE_RAW);
    assert_eq!(ZERO + ZERO, ZERO);
    assert_eq!(NEG_ONE_RAW + r(1), ZERO);
    assert_eq!(MAX + ZERO, MAX);
    assert_eq!(MIN + ZERO, MIN);
}

#[test]
fn sub_saturates() {
    assert_eq!(MIN - ONE, MIN);
    assert_eq!(MAX - (-ONE), MAX);
    assert_eq!(MAX - MAX, ZERO);
    assert_eq!(MIN - MIN, ZERO);
    assert_eq!(ZERO - MIN, MAX);
    assert_eq!(ZERO - MAX, r(i64::MIN + 1));
    assert_eq!(MAX - MIN, MAX);
    assert_eq!(MIN - MAX, MIN);
    assert_eq!(NEG_ONE_RAW - NEG_ONE_RAW, ZERO);
}

#[test]
fn neg_saturates() {
    assert_eq!(-MIN, MAX);
    assert_eq!(-MAX, r(i64::MIN + 1));
    assert_eq!(-ZERO, ZERO);
    assert_eq!(-NEG_ONE_RAW, r(1));
    assert_eq!(-ONE, r(-(1i64 << 32)));
}

#[test]
fn mul_exact() {
    assert_eq!(MAX * MAX, MAX);
    assert_eq!(MIN * MIN, MAX);
    assert_eq!(MIN * MAX, MIN);
    assert_eq!(MAX * MIN, MIN);
    assert_eq!(MIN * ONE, MIN);
    assert_eq!(MIN * (-ONE), MAX);
    assert_eq!(MAX * ONE, MAX);
    assert_eq!(MAX * ZERO, ZERO);
    assert_eq!(MIN * ZERO, ZERO);
    assert_eq!(ONE * ONE, ONE);
    assert_eq!(NEG_ONE_RAW * NEG_ONE_RAW, ZERO);
    // Floor (arithmetic shift): -1 raw * 1 raw = -2^-64, which floors to -1 raw.
    assert_eq!(NEG_ONE_RAW * r(1), NEG_ONE_RAW);
    assert_eq!(r(1) * r(1), ZERO);
    assert_eq!(NEG_ONE_RAW * ONE, NEG_ONE_RAW);
    assert_eq!(Fx::HALF * Fx::HALF, r(1i64 << 30));
    assert_eq!(Fx::from_int(3) * Fx::from_int(-4), Fx::from_int(-12));
}

#[test]
fn div_exact() {
    assert_eq!(ONE / ZERO, MAX);
    assert_eq!(MAX / ZERO, MAX);
    assert_eq!(r(1) / ZERO, MAX);
    assert_eq!(-ONE / ZERO, MIN);
    assert_eq!(MIN / ZERO, MIN);
    assert_eq!(NEG_ONE_RAW / ZERO, MIN);
    assert_eq!(ZERO / ZERO, ZERO);
    assert_eq!(MIN / (-ONE), MAX);
    assert_eq!(MAX / ONE, MAX);
    assert_eq!(MIN / ONE, MIN);
    assert_eq!(MAX / Fx::HALF, MAX);
    assert_eq!(MIN / Fx::HALF, MIN);
    // 2^64 / (2^63 - 1) truncates to 2; 2^64 / -2^63 is exactly -2.
    assert_eq!(ONE / MAX, r(2));
    assert_eq!(ONE / MIN, r(-2));
    assert_eq!(NEG_ONE_RAW / ONE, NEG_ONE_RAW);
    assert_eq!(ONE / (-ONE), -ONE);
    // Truncation toward zero, not floor.
    assert_eq!(r(1) / Fx::from_int(2), ZERO);
    assert_eq!(NEG_ONE_RAW / Fx::from_int(2), ZERO);
    assert_eq!(ZERO / MIN, ZERO);
    assert_eq!(Fx::from_int(7) / Fx::from_int(2), r(7 * (1i64 << 31)));
}

#[test]
fn abs_min_max_clamp_signum() {
    assert_eq!(MIN.abs(), MAX);
    assert_eq!(MAX.abs(), MAX);
    assert_eq!((-ONE).abs(), ONE);
    assert_eq!(ZERO.abs(), ZERO);
    assert_eq!(NEG_ONE_RAW.abs(), r(1));

    assert_eq!(MIN.min(MAX), MIN);
    assert_eq!(MIN.max(MAX), MAX);
    assert_eq!(ZERO.min(NEG_ONE_RAW), NEG_ONE_RAW);
    assert_eq!(ZERO.max(NEG_ONE_RAW), ZERO);

    assert_eq!(MAX.clamp(-ONE, ONE), ONE);
    assert_eq!(MIN.clamp(-ONE, ONE), -ONE);
    assert_eq!(ZERO.clamp(-ONE, ONE), ZERO);
    assert_eq!(ONE.clamp(ONE, ONE), ONE);
    // Inverted bounds never panic: the lower bound is applied first, then the upper.
    assert_eq!(ZERO.clamp(ONE, -ONE), -ONE);

    assert_eq!(MIN.signum(), -ONE);
    assert_eq!(MAX.signum(), ONE);
    assert_eq!(ZERO.signum(), ZERO);
    assert_eq!(NEG_ONE_RAW.signum(), -ONE);
    assert_eq!(r(1).signum(), ONE);
}

#[test]
fn int_conversions() {
    assert_eq!(Fx::from_int(0), ZERO);
    assert_eq!(Fx::from_int(1), ONE);
    assert_eq!(Fx::from_int(-1), -ONE);
    assert_eq!(Fx::from_int(i32::MAX).raw(), (i64::from(i32::MAX)) << 32);
    assert_eq!(Fx::from_int(i32::MIN).raw(), i64::MIN);

    assert_eq!(MAX.to_int_floor(), i32::MAX);
    assert_eq!(MIN.to_int_floor(), i32::MIN);
    assert_eq!(NEG_ONE_RAW.to_int_floor(), -1);
    assert_eq!(r(1).to_int_floor(), 0);
    assert_eq!((ONE - r(1)).to_int_floor(), 0);
    assert_eq!((-ONE).to_int_floor(), -1);
    assert_eq!((-ONE - r(1)).to_int_floor(), -2);
    assert_eq!(Fx::from_int(5).to_int_floor(), 5);

    assert_eq!(Fx::HALF.to_int_round_half_up(), 1);
    assert_eq!((Fx::HALF - r(1)).to_int_round_half_up(), 0);
    assert_eq!((-Fx::HALF).to_int_round_half_up(), 0);
    assert_eq!((-Fx::HALF - r(1)).to_int_round_half_up(), -1);
    assert_eq!((ONE + Fx::HALF).to_int_round_half_up(), 2);
    assert_eq!((Fx::from_int(2) + Fx::HALF).to_int_round_half_up(), 3);
    assert_eq!((-ONE - Fx::HALF).to_int_round_half_up(), -1);
    assert_eq!((Fx::from_int(-2) - Fx::HALF).to_int_round_half_up(), -2);
    assert_eq!(ZERO.to_int_round_half_up(), 0);
    assert_eq!(MAX.to_int_round_half_up(), i32::MAX);
    assert_eq!(MIN.to_int_round_half_up(), i32::MIN);
}

#[test]
fn sqrt_exact() {
    assert_eq!(ZERO.sqrt(), ZERO);
    assert_eq!(ONE.sqrt(), ONE);
    assert_eq!(Fx::from_int(4).sqrt(), Fx::from_int(2));
    assert_eq!(Fx::from_int(2).sqrt().raw(), 6_074_000_999);
    assert_eq!(r(1).sqrt().raw(), 65_536);
    assert_eq!(MAX.sqrt().raw(), 199_032_864_766_430);
    assert_eq!(MIN.sqrt(), ZERO);
    assert_eq!((-ONE).sqrt(), ZERO);
    assert_eq!(NEG_ONE_RAW.sqrt(), ZERO);
    assert_eq!(Fx::from_int(1 << 20).sqrt(), Fx::from_int(1 << 10));
}

#[test]
fn assign_ops_match_binary_ops() {
    let mut a = MAX;
    a += ONE;
    assert_eq!(a, MAX);
    a -= MAX;
    assert_eq!(a, ZERO);
    a = Fx::from_int(3);
    a *= Fx::from_int(5);
    assert_eq!(a, Fx::from_int(15));
    a /= Fx::from_int(3);
    assert_eq!(a, Fx::from_int(5));
}

#[test]
fn vec2_edge_cases() {
    let z = Vec2Fx::ZERO;
    assert_eq!(z, Vec2Fx::new(ZERO, ZERO));
    assert_eq!(z.normalize(), z);
    assert_eq!(z.length(), ZERO);
    assert_eq!(z.length_sq(), ZERO);

    let three_four = Vec2Fx::new(Fx::from_int(3), Fx::from_int(4));
    assert_eq!(three_four.length(), Fx::from_int(5));
    assert_eq!(three_four.length_sq(), Fx::from_int(25));
    assert_eq!(three_four.distance_sq(z), Fx::from_int(25));
    assert_eq!(z.distance_sq(three_four), Fx::from_int(25));

    let big = Vec2Fx::new(MAX, MAX);
    assert_eq!(big.length(), MAX);
    assert_eq!(big.length_sq(), MAX);
    let small = Vec2Fx::new(r(1), r(1));
    assert_eq!(small.length_sq(), ZERO);

    // Components of magnitude 2^30 (raw 2^62) must not overflow: floor(2^62 * sqrt(2)).
    let p = Fx::from_int(1 << 30);
    assert_eq!(Vec2Fx::new(p, p).length().raw(), 6_521_908_912_666_391_106);
    assert_eq!(Vec2Fx::new(-p, p).length().raw(), 6_521_908_912_666_391_106);

    // Axis-aligned normalisation is exact, including at the extremes.
    assert_eq!(Vec2Fx::new(MAX, ZERO).normalize(), Vec2Fx::new(ONE, ZERO));
    assert_eq!(Vec2Fx::new(ZERO, MIN).normalize(), Vec2Fx::new(ZERO, -ONE));
    assert_eq!(Vec2Fx::new(r(1), ZERO).normalize(), Vec2Fx::new(ONE, ZERO));
    assert_eq!(
        Vec2Fx::new(ZERO, r(-1)).normalize(),
        Vec2Fx::new(ZERO, -ONE)
    );

    // Dot / cross saturate.
    assert_eq!(Vec2Fx::new(MAX, ZERO).dot(Vec2Fx::new(MAX, ZERO)), MAX);
    assert_eq!(Vec2Fx::new(MIN, MIN).dot(Vec2Fx::new(MIN, MIN)), MAX);
    assert_eq!(
        Vec2Fx::new(MIN, MAX).dot(Vec2Fx::new(MAX, MAX)),
        r(-(1i64 << 31))
    );
    assert_eq!(Vec2Fx::new(MAX, ZERO).cross(Vec2Fx::new(ZERO, MAX)), MAX);
    assert_eq!(Vec2Fx::new(ZERO, MAX).cross(Vec2Fx::new(MAX, ZERO)), MIN);
    assert_eq!(three_four.cross(three_four), ZERO);
    assert_eq!(three_four.dot(three_four), Fx::from_int(25));
    assert_eq!(Vec2Fx::new(ONE, ZERO).cross(Vec2Fx::new(ZERO, ONE)), ONE);

    // Operators saturate component-wise.
    assert_eq!(
        Vec2Fx::new(MAX, MIN) + Vec2Fx::new(ONE, -ONE),
        Vec2Fx::new(MAX, MIN)
    );
    assert_eq!(
        Vec2Fx::new(MAX, MIN) - Vec2Fx::new(-ONE, ONE),
        Vec2Fx::new(MAX, MIN)
    );
    assert_eq!(-Vec2Fx::new(MIN, ZERO), Vec2Fx::new(MAX, ZERO));
    assert_eq!(
        three_four.scale(Fx::from_int(2)),
        Vec2Fx::new(Fx::from_int(6), Fx::from_int(8))
    );
    assert_eq!(three_four.scale(ZERO), z);
    assert_eq!(
        Vec2Fx::new(MAX, MIN).scale(Fx::from_int(2)),
        Vec2Fx::new(MAX, MIN)
    );

    // lerp endpoints and midpoint.
    let a = Vec2Fx::new(Fx::from_int(-2), Fx::from_int(10));
    let b = Vec2Fx::new(Fx::from_int(6), Fx::from_int(-10));
    assert_eq!(a.lerp(b, ZERO), a);
    assert_eq!(a.lerp(b, ONE), b);
    assert_eq!(a.lerp(b, Fx::HALF), Vec2Fx::new(Fx::from_int(2), ZERO));
    // lerp between extreme points does not panic and saturates.
    let extreme = Vec2Fx::new(MIN, MAX).lerp(Vec2Fx::new(MAX, MIN), ONE);
    assert_eq!(extreme, Vec2Fx::new(MAX, MIN));
}

#[test]
fn angle_construction_and_wrap() {
    assert_eq!(Angle::from_degrees_int(0).raw(), 0);
    assert_eq!(Angle::from_degrees_int(90).raw(), 1 << 30);
    assert_eq!(Angle::from_degrees_int(180).raw(), 1 << 31);
    assert_eq!(Angle::from_degrees_int(270).raw(), 3 << 30);
    assert_eq!(Angle::from_degrees_int(-90).raw(), 3 << 30);
    assert_eq!(Angle::from_degrees_int(360).raw(), 0);
    assert_eq!(Angle::from_degrees_int(450).raw(), 1 << 30);
    assert_eq!(Angle::from_degrees_int(45).raw(), 1 << 29);
    // i32::MIN must not panic; -2147483648 mod 360 = 232, i.e. 232/360 of a turn.
    assert_eq!(
        Angle::from_degrees_int(i32::MIN),
        Angle::from_degrees_int(232)
    );
    let _ = Angle::from_degrees_int(i32::MAX);

    assert_eq!(Angle::from_turns_fraction(1, 4).raw(), 1 << 30);
    assert_eq!(Angle::from_turns_fraction(1, 2).raw(), 1 << 31);
    assert_eq!(Angle::from_turns_fraction(-1, 4).raw(), 3 << 30);
    assert_eq!(Angle::from_turns_fraction(5, 4).raw(), 1 << 30);
    assert_eq!(Angle::from_turns_fraction(1, 0).raw(), 0);
    assert_eq!(Angle::from_turns_fraction(0, 7).raw(), 0);
    assert_eq!(Angle::from_turns_fraction(i32::MIN, 1).raw(), 0);

    let max = Angle::from_raw(u32::MAX);
    assert_eq!((max + Angle::from_raw(1)).raw(), 0);
    assert_eq!((Angle::from_raw(0) - Angle::from_raw(1)).raw(), u32::MAX);
    assert_eq!((-Angle::from_raw(1)).raw(), u32::MAX);
    assert_eq!((-Angle::from_raw(0)).raw(), 0);
    assert_eq!(Angle::QUARTER.raw(), 1 << 30);
    assert_eq!(Angle::HALF.raw(), 1 << 31);
    assert_eq!(Angle::ZERO.raw(), 0);

    assert_eq!(Angle::ZERO.to_fx_radians(), ZERO);
    assert!((Angle::HALF.to_fx_radians().raw() - Fx::PI.raw()).abs() <= 1);
    assert!(Angle::from_raw(u32::MAX).to_fx_radians() < Fx::TAU);
}

#[test]
fn sin_cos_exact_at_axes() {
    assert_eq!(Angle::ZERO.sin(), ZERO);
    assert_eq!(Angle::ZERO.cos(), ONE);
    assert_eq!(Angle::QUARTER.sin(), ONE);
    assert_eq!(Angle::QUARTER.cos(), ZERO);
    assert_eq!(Angle::HALF.sin(), ZERO);
    assert_eq!(Angle::HALF.cos(), -ONE);
    let three_q = Angle::from_raw(3 << 30);
    assert_eq!(three_q.sin(), -ONE);
    assert_eq!(three_q.cos(), ZERO);
    // Extremes of the raw range never panic and stay within [-1, 1].
    for raw in [
        0u32,
        1,
        u32::MAX,
        u32::MAX - 1,
        1 << 30,
        (1 << 30) - 1,
        (1 << 30) + 1,
    ] {
        let a = Angle::from_raw(raw);
        assert!(a.sin().abs() <= ONE && a.cos().abs() <= ONE);
    }
    assert_eq!(Vec2Fx::from_angle(Angle::QUARTER), Vec2Fx::new(ZERO, ONE));
    assert_eq!(Vec2Fx::from_angle(Angle::ZERO), Vec2Fx::new(ONE, ZERO));
    assert_eq!(
        Vec2Fx::new(ONE, ZERO).rotate(Angle::QUARTER),
        Vec2Fx::new(ZERO, ONE)
    );
    assert_eq!(
        Vec2Fx::new(ONE, ZERO).rotate(Angle::HALF),
        Vec2Fx::new(-ONE, ZERO)
    );
    assert_eq!(
        Vec2Fx::new(MAX, MIN).rotate(Angle::ZERO),
        Vec2Fx::new(MAX, MIN)
    );
}

#[test]
fn atan2_axes_exact() {
    let a = |y: i64, x: i64| Angle::atan2(r(y), r(x)).raw();
    assert_eq!(a(0, 0), 0);
    assert_eq!(a(0, 1), 0);
    assert_eq!(a(0, i64::MAX), 0);
    assert_eq!(a(1, 0), 1 << 30);
    assert_eq!(a(i64::MAX, 0), 1 << 30);
    assert_eq!(a(0, -1), 1 << 31);
    assert_eq!(a(0, i64::MIN), 1 << 31);
    assert_eq!(a(-1, 0), 3 << 30);
    assert_eq!(a(i64::MIN, 0), 3 << 30);
    // Diagonals are within a few raw units of the exact eighth-turn values.
    let near = |got: u32, want: u32| (i64::from(got) - i64::from(want)).abs() < 64;
    assert!(near(a(5, 5), 1 << 29));
    assert!(near(a(i64::MAX, i64::MAX), 1 << 29));
    assert!(near(a(i64::MAX, i64::MIN), 3 << 29));
    assert!(near(a(i64::MIN, i64::MIN), 5 << 29));
    assert!(near(a(i64::MIN, i64::MAX), 7 << 29));
}

#[test]
fn rng_edge_cases() {
    let mut rng = Rng::from_seed(42);
    assert_eq!(rng.range_u32(5, 5), 5);
    assert_eq!(rng.range_u32(9, 3), 9);
    assert_eq!(rng.range_u32(0, 1), 0);
    assert_eq!(rng.range_i32(-4, -4), -4);
    assert_eq!(rng.range_i32(7, -7), 7);
    for _ in 0..1000 {
        assert!(rng.range_u32(u32::MAX - 1, u32::MAX) == u32::MAX - 1);
        let v = rng.range_i32(i32::MIN, i32::MAX);
        assert!(v < i32::MAX);
        let w = rng.range_i32(-3, 3);
        assert!((-3..3).contains(&w));
        let u = rng.unit_fx();
        assert!(u >= ZERO && u < ONE);
    }
    assert!(!rng.coin(0, 5));
    assert!(rng.coin(5, 5));
    assert!(rng.coin(9, 5));
    assert!(!rng.coin(1, 0));
    assert!(!rng.coin(0, 0));

    let mut empty: [u8; 0] = [];
    rng.shuffle(&mut empty);
    let mut one = [7u8];
    rng.shuffle(&mut one);
    assert_eq!(one, [7]);

    // State round-trips exactly; clones evolve identically.
    let st = rng.state();
    let mut a = Rng::from_state(st);
    let mut b = rng.clone();
    for _ in 0..100 {
        let x = a.next_u64();
        assert_eq!(x, b.next_u64());
        assert_eq!(x, rng.next_u64());
    }
    assert_eq!(a.state(), b.state());
    // The all-zero state is invalid for xoshiro; it is replaced by a usable one.
    let mut z = Rng::from_state([0; 4]);
    assert!(z.next_u64() != 0 || z.next_u64() != 0);
}

#[test]
fn stream_derivation_is_pure() {
    let streams = RngStreams::new(12345);
    let copy = streams;
    let mut s1 = streams.stream(StreamId::new(1));
    let mut s1_again = streams.stream(StreamId::new(1));
    assert_eq!(streams, copy);
    assert_eq!(streams.master_seed(), 12345);
    for _ in 0..10 {
        assert_eq!(s1.next_u64(), s1_again.next_u64());
    }
    let mut s2 = streams.stream(StreamId::new(2));
    let mut s1b = streams.stream(StreamId::new(1));
    assert_ne!(s1b.next_u64(), s2.next_u64());
    assert_eq!(StreamId::new(9).get(), 9);
    // Different master seeds give different streams for the same id.
    let other = RngStreams::new(12346).stream(StreamId::new(1));
    assert_ne!(other.state(), streams.stream(StreamId::new(1)).state());
}
