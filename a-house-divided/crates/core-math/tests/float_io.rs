//! Tests for the optional `float-io` conversions (compiled only with `--features float-io`).
#![cfg(feature = "float-io")]

use core_math::Fx;

#[test]
fn from_to_f64() {
    assert_eq!(Fx::from_f64(1.0), Fx::ONE);
    assert_eq!(Fx::from_f64(-0.5).raw(), -(1i64 << 31));
    assert_eq!(Fx::from_f64(f64::NAN), Fx::ZERO);
    assert_eq!(Fx::from_f64(f64::INFINITY), Fx::MAX);
    assert_eq!(Fx::from_f64(f64::NEG_INFINITY), Fx::MIN);
    assert_eq!(Fx::from_f64(1e30), Fx::MAX);
    assert_eq!(Fx::ONE.to_f64(), 1.0);
    assert_eq!(Fx::from_int(-7).to_f64(), -7.0);
    assert_eq!(Fx::from_f64(Fx::PI.to_f64()), Fx::PI);
}
