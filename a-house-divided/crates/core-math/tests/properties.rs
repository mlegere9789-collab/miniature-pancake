//! Property tests (proptest). Floating point is used here only as an oracle for accuracy checks.

use core_math::{Angle, Fx, Rng, RngStreams, StreamId, Vec2Fx};
use proptest::prelude::*;

const TWO_POW_32: f64 = 4_294_967_296.0;

fn fx(raw: i64) -> Fx {
    Fx::from_raw(raw)
}

fn to_f64(v: Fx) -> f64 {
    v.raw() as f64 / TWO_POW_32
}

/// Circular distance between two angles, in turns.
fn turn_dist(a: u32, b: u32) -> f64 {
    let d = a.wrapping_sub(b);
    let d = d.min(d.wrapping_neg());
    f64::from(d) / TWO_POW_32
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 512, failure_persistence: None, ..ProptestConfig::default() })]

    #[test]
    fn add_sub_inverse_without_saturation(a in -(1i64 << 61)..(1i64 << 61), b in -(1i64 << 61)..(1i64 << 61)) {
        prop_assert_eq!((fx(a) + fx(b)) - fx(b), fx(a));
        prop_assert_eq!((fx(a) - fx(b)) + fx(b), fx(a));
        prop_assert_eq!((fx(a) + fx(b)).raw(), a + b);
    }

    #[test]
    fn add_sub_saturate_like_i64(a in any::<i64>(), b in any::<i64>()) {
        prop_assert_eq!((fx(a) + fx(b)).raw(), a.saturating_add(b));
        prop_assert_eq!((fx(a) - fx(b)).raw(), a.saturating_sub(b));
        prop_assert_eq!((-fx(a)).raw(), a.saturating_neg());
    }

    #[test]
    fn mul_commutative(a in any::<i64>(), b in any::<i64>()) {
        prop_assert_eq!(fx(a) * fx(b), fx(b) * fx(a));
    }

    #[test]
    fn mul_matches_definition(a in any::<i64>(), b in any::<i64>()) {
        let wide = (i128::from(a) * i128::from(b)) >> 32;
        let expected = wide.clamp(i128::from(i64::MIN), i128::from(i64::MAX)) as i64;
        prop_assert_eq!((fx(a) * fx(b)).raw(), expected);
    }

    #[test]
    fn mul_one_is_identity(a in any::<i64>()) {
        prop_assert_eq!(fx(a) * Fx::ONE, fx(a));
        prop_assert_eq!(Fx::ONE * fx(a), fx(a));
        prop_assert_eq!(fx(a) * Fx::ZERO, Fx::ZERO);
    }

    #[test]
    fn div_matches_definition(a in any::<i64>(), b in any::<i64>()) {
        prop_assume!(b != 0);
        let wide = (i128::from(a) << 32) / i128::from(b);
        let expected = wide.clamp(i128::from(i64::MIN), i128::from(i64::MAX)) as i64;
        prop_assert_eq!((fx(a) / fx(b)).raw(), expected);
    }

    // The spec's "within one ulp" holds when |y| <= 1.0: the truncation error of the quotient is
    // amplified by |y|, so for larger divisors the bound is |y| ulps (checked separately below).
    #[test]
    fn div_then_mul_within_one_ulp(x in -(1i64 << 40)..(1i64 << 40), ymag in (1i64 << 20)..=(1i64 << 32), neg in any::<bool>()) {
        let y = if neg { -ymag } else { ymag };
        let back = (fx(x) / fx(y)) * fx(y);
        prop_assert!((back.raw() - x).abs() <= 1, "x={x} y={y} back={}", back.raw());
    }

    #[test]
    fn div_then_mul_bounded_for_large_divisors(x in -(1i64 << 40)..(1i64 << 40), y in (1i64 << 32)..(1i64 << 40)) {
        let back = (fx(x) / fx(y)) * fx(y);
        let bound = (y >> 32) + 1;
        prop_assert!((back.raw() - x).abs() <= bound);
    }

    #[test]
    fn sqrt_is_floor_root(a in any::<i64>()) {
        let s = fx(a).sqrt().raw();
        if a <= 0 {
            prop_assert_eq!(s, 0);
        } else {
            let n = (a as u128) << 32;
            let s = s as u128;
            prop_assert!(s * s <= n);
            prop_assert!(n < (s + 1) * (s + 1));
        }
    }

    #[test]
    fn sqrt_squares_back(a in 0i64..(1i64 << 62)) {
        let s = fx(a).sqrt();
        prop_assert!((s * s).raw() <= a);
        let s1 = fx(s.raw() + 1);
        prop_assert!((s1 * s1).raw() >= a - 1 || s1.raw() > 0);
    }

    #[test]
    fn abs_min_max_clamp_consistent(a in any::<i64>(), b in any::<i64>(), c in any::<i64>()) {
        prop_assert!(fx(a).abs() >= Fx::ZERO);
        prop_assert_eq!(fx(a).min(fx(b)).raw(), a.min(b));
        prop_assert_eq!(fx(a).max(fx(b)).raw(), a.max(b));
        let (lo, hi) = (b.min(c), b.max(c));
        prop_assert_eq!(fx(a).clamp(fx(lo), fx(hi)).raw(), a.clamp(lo, hi));
        prop_assert_eq!(fx(a).signum().raw(), a.signum() << 32);
    }

    #[test]
    fn int_conversions_roundtrip(i in any::<i32>()) {
        let v = Fx::from_int(i);
        prop_assert_eq!(v.to_int_floor(), i);
        prop_assert_eq!(v.to_int_round_half_up(), i);
        prop_assert_eq!(v.raw(), i64::from(i) << 32);
    }

    #[test]
    fn int_conversions_match_definition(a in any::<i64>()) {
        prop_assert_eq!(i64::from(fx(a).to_int_floor()), a >> 32);
        let want = ((i128::from(a) + (1 << 31)) >> 32).min(i128::from(i32::MAX));
        prop_assert_eq!(i128::from(fx(a).to_int_round_half_up()), want);
    }

    #[test]
    fn sin_cos_unit_circle(raw in any::<u32>()) {
        let a = Angle::from_raw(raw);
        let (s, c) = (to_f64(a.sin()), to_f64(a.cos()));
        prop_assert!((s * s + c * c - 1.0).abs() < 2f64.powi(-18));
    }

    #[test]
    fn sin_cos_accuracy(raw in any::<u32>()) {
        let a = Angle::from_raw(raw);
        let theta = f64::from(raw) / TWO_POW_32 * std::f64::consts::TAU;
        prop_assert!((to_f64(a.sin()) - theta.sin()).abs() < 2f64.powi(-20));
        prop_assert!((to_f64(a.cos()) - theta.cos()).abs() < 2f64.powi(-20));
    }

    #[test]
    fn sin_symmetries(raw in any::<u32>()) {
        let a = Angle::from_raw(raw);
        // sin(-a) = -sin(a) up to one ulp of table rounding; cos is even.
        prop_assert!((a.sin().raw() + (-a).sin().raw()).abs() <= 1);
        prop_assert!((a.cos().raw() - (-a).cos().raw()).abs() <= 1);
        prop_assert_eq!(a.cos(), (a + Angle::QUARTER).sin());
    }

    #[test]
    fn atan2_accuracy(y in -(1i64 << 62)..(1i64 << 62), x in -(1i64 << 62)..(1i64 << 62)) {
        prop_assume!(x != 0 || y != 0);
        let got = Angle::atan2(fx(y), fx(x)).raw();
        let want = (y as f64).atan2(x as f64) / std::f64::consts::TAU;
        let want = (want.rem_euclid(1.0) * TWO_POW_32).round() as u64 as u32;
        prop_assert!(turn_dist(got, want) < 2f64.powi(-18), "y={y} x={x} got={got} want={want}");
    }

    #[test]
    fn atan2_small_magnitudes(y in -1000i64..1000, x in -1000i64..1000) {
        prop_assume!(x != 0 || y != 0);
        let got = Angle::atan2(fx(y), fx(x)).raw();
        let want = (y as f64).atan2(x as f64) / std::f64::consts::TAU;
        let want = (want.rem_euclid(1.0) * TWO_POW_32).round() as u64 as u32;
        prop_assert!(turn_dist(got, want) < 2f64.powi(-18));
    }

    #[test]
    fn atan2_inverts_from_angle(raw in any::<u32>()) {
        let a = Angle::from_raw(raw);
        let v = Vec2Fx::from_angle(a);
        let back = Angle::atan2(v.y, v.x);
        prop_assert!(turn_dist(back.raw(), raw) < 2f64.powi(-18));
    }

    #[test]
    fn angle_wraps(a in any::<u32>(), b in any::<u32>()) {
        let (x, y) = (Angle::from_raw(a), Angle::from_raw(b));
        prop_assert_eq!((x + y).raw(), a.wrapping_add(b));
        prop_assert_eq!((x - y).raw(), a.wrapping_sub(b));
        prop_assert_eq!((x + y) - y, x);
        prop_assert_eq!(x + (-x), Angle::ZERO);
        prop_assert_eq!(x.sin(), (x + Angle::from_raw(0)).sin());
    }

    #[test]
    fn degrees_wrap(d in -1_000_000i32..1_000_000, k in -1000i32..1000) {
        prop_assert_eq!(Angle::from_degrees_int(d + k * 360), Angle::from_degrees_int(d));
    }

    #[test]
    fn vec_normalize_unit_length(x in any::<i64>(), y in any::<i64>()) {
        prop_assume!(x != 0 || y != 0);
        let n = Vec2Fx::new(fx(x), fx(y)).normalize();
        let (nx, ny) = (to_f64(n.x), to_f64(n.y));
        prop_assert!(((nx * nx + ny * ny).sqrt() - 1.0).abs() < 2f64.powi(-20));
        // Direction is preserved (same signs).
        prop_assert_eq!(n.x.raw().signum(), x.signum());
        prop_assert_eq!(n.y.raw().signum(), y.signum());
    }

    #[test]
    fn vec_normalize_unit_length_small(x in -64i64..64, y in -64i64..64) {
        prop_assume!(x != 0 || y != 0);
        let n = Vec2Fx::new(fx(x), fx(y)).normalize();
        let (nx, ny) = (to_f64(n.x), to_f64(n.y));
        prop_assert!(((nx * nx + ny * ny).sqrt() - 1.0).abs() < 2f64.powi(-20));
    }

    #[test]
    fn vec_length_matches_float(x in -(1i64 << 62)..(1i64 << 62), y in -(1i64 << 62)..(1i64 << 62)) {
        let v = Vec2Fx::new(fx(x), fx(y));
        let want = ((x as f64).powi(2) + (y as f64).powi(2)).sqrt();
        let got = v.length().raw() as f64;
        prop_assert!((got - want).abs() <= want * 1e-14 + 1.0);
        prop_assert_eq!(v.length_sq(), v.dot(v));
    }

    #[test]
    fn vec_rotate_preserves_length(x in -(1i64 << 40)..(1i64 << 40), y in -(1i64 << 40)..(1i64 << 40), raw in any::<u32>()) {
        let v = Vec2Fx::new(fx(x), fx(y));
        let r = v.rotate(Angle::from_raw(raw));
        let (l0, l1) = (v.length().raw() as f64, r.length().raw() as f64);
        prop_assert!((l0 - l1).abs() <= l0 * 2f64.powi(-18) + 4.0);
    }

    #[test]
    fn vec_ops_algebra(ax in -(1i64 << 50)..(1i64 << 50), ay in -(1i64 << 50)..(1i64 << 50),
                       bx in -(1i64 << 50)..(1i64 << 50), by in -(1i64 << 50)..(1i64 << 50)) {
        let a = Vec2Fx::new(fx(ax), fx(ay));
        let b = Vec2Fx::new(fx(bx), fx(by));
        prop_assert_eq!(a + b, b + a);
        prop_assert_eq!((a + b) - b, a);
        prop_assert_eq!(a.dot(b), b.dot(a));
        // Floor rounding makes the two products differ by at most one raw unit.
        prop_assert!((a.cross(b).raw() + b.cross(a).raw()).abs() <= 1);
        prop_assert_eq!(a.distance_sq(b), b.distance_sq(a));
        prop_assert_eq!(a.lerp(b, Fx::ZERO), a);
        prop_assert_eq!(-(-a), a);
    }

    #[test]
    fn stream_independence(seed in any::<u64>(), id in any::<u32>(), other in any::<u32>()) {
        prop_assume!(id != other);
        let streams = RngStreams::new(seed);
        let mut alone = streams.stream(StreamId::new(id));
        // Derive and exhaust another stream in between; the first stream is unaffected.
        let mut noise = streams.stream(StreamId::new(other));
        for _ in 0..8 { noise.next_u64(); }
        let mut later = streams.stream(StreamId::new(id));
        for _ in 0..16 {
            prop_assert_eq!(alone.next_u64(), later.next_u64());
        }
        prop_assert_ne!(streams.stream(StreamId::new(id)).state(), streams.stream(StreamId::new(other)).state());
    }

    #[test]
    fn range_u32_in_bounds(seed in any::<u64>(), lo in any::<u32>(), span in 1u32..=u32::MAX) {
        let hi = lo.saturating_add(span);
        prop_assume!(hi > lo);
        let mut rng = Rng::from_seed(seed);
        for _ in 0..64 {
            let v = rng.range_u32(lo, hi);
            prop_assert!(v >= lo && v < hi);
        }
    }

    #[test]
    fn range_i32_in_bounds(seed in any::<u64>(), a in any::<i32>(), b in any::<i32>()) {
        let (lo, hi) = (a.min(b), a.max(b));
        prop_assume!(lo < hi);
        let mut rng = Rng::from_seed(seed);
        for _ in 0..64 {
            let v = rng.range_i32(lo, hi);
            prop_assert!(v >= lo && v < hi);
        }
    }

    #[test]
    fn shuffle_is_permutation(seed in any::<u64>(), len in 0usize..200) {
        let mut rng = Rng::from_seed(seed);
        let mut v: Vec<usize> = (0..len).collect();
        rng.shuffle(&mut v);
        let mut sorted = v.clone();
        sorted.sort_unstable();
        prop_assert_eq!(sorted, (0..len).collect::<Vec<_>>());
    }

    #[test]
    fn state_roundtrip(seed in any::<u64>(), skip in 0usize..50) {
        let mut a = Rng::from_seed(seed);
        for _ in 0..skip { a.next_u64(); }
        let mut b = Rng::from_state(a.state());
        prop_assert_eq!(a.state(), b.state());
        for _ in 0..8 { prop_assert_eq!(a.next_u64(), b.next_u64()); }
    }

    #[test]
    fn unit_fx_in_range(seed in any::<u64>()) {
        let mut rng = Rng::from_seed(seed);
        for _ in 0..64 {
            let u = rng.unit_fx();
            prop_assert!(u >= Fx::ZERO && u < Fx::ONE);
        }
    }
}

/// Chi-square-style uniformity check over 1,000,000 draws (fixed seed, so deterministic).
fn assert_uniform(lo: u32, hi: u32) {
    let buckets = (hi - lo) as usize;
    let draws = 1_000_000usize;
    let mut counts = vec![0usize; buckets];
    let mut rng = RngStreams::new(0x5EED).stream(StreamId::new(u32::from(buckets as u16)));
    for _ in 0..draws {
        let v = rng.range_u32(lo, hi);
        assert!(v >= lo && v < hi);
        counts[(v - lo) as usize] += 1;
    }
    let expected = draws as f64 / buckets as f64;
    let chi2: f64 = counts
        .iter()
        .map(|&c| (c as f64 - expected).powi(2) / expected)
        .sum();
    // Degrees of freedom = buckets - 1; accept anything within ~6 sigma of the mean.
    let dof = (buckets - 1) as f64;
    assert!(
        chi2 < dof + 6.0 * (2.0 * dof).sqrt(),
        "chi2={chi2} dof={dof}"
    );
    for &c in &counts {
        assert!(
            (c as f64 - expected).abs() < expected * 0.03,
            "bucket count {c} vs {expected}"
        );
    }
}

#[test]
fn range_u32_is_uniform() {
    assert_uniform(0, 10);
    assert_uniform(100, 107);
    assert_uniform(0, 64);
}

#[test]
fn coin_frequency() {
    let mut rng = Rng::from_seed(99);
    let n = 200_000;
    let heads = (0..n).filter(|_| rng.coin(1, 4)).count();
    let p = heads as f64 / f64::from(n);
    assert!((p - 0.25).abs() < 0.01, "p={p}");
}

#[test]
fn shuffle_changes_order_and_is_deterministic() {
    let mut a: Vec<u32> = (0..32).collect();
    let mut b = a.clone();
    Rng::from_seed(7).shuffle(&mut a);
    Rng::from_seed(7).shuffle(&mut b);
    assert_eq!(a, b);
    assert_ne!(a, (0..32).collect::<Vec<_>>());
}
