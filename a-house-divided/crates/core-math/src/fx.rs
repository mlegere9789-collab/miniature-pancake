//! Q32.32 fixed-point arithmetic.

use crate::Fx;
use core::ops::{Add, AddAssign, Div, DivAssign, Mul, MulAssign, Neg, Sub, SubAssign};

/// Saturating conversion of a wide integer to `i64`.
pub(crate) fn saturate_i64(v: i128) -> i64 {
    match i64::try_from(v) {
        Ok(x) => x,
        Err(_) if v < 0 => i64::MIN,
        Err(_) => i64::MAX,
    }
}

/// Saturating conversion of an unsigned wide integer to `i64`.
pub(crate) fn saturate_u128(v: u128) -> i64 {
    i64::try_from(v).unwrap_or(i64::MAX)
}

/// Floor of the square root of `n`, integer-only (Newton iteration).
pub(crate) fn isqrt_u128(n: u128) -> u128 {
    if n < 2 {
        return n;
    }
    let bits = 128 - n.leading_zeros();
    // 2^ceil(bits/2) is at least sqrt(n), so the iteration decreases monotonically to the floor.
    let mut x = 1u128 << bits.div_ceil(2);
    loop {
        let y = (x + n / x) >> 1;
        if y >= x {
            return x;
        }
        x = y;
    }
}

impl Fx {
    /// Number of fractional bits.
    pub const FRAC_BITS: u32 = 32;
    /// The value 0.
    pub const ZERO: Fx = Fx(0);
    /// The value 1.
    pub const ONE: Fx = Fx(1i64 << 32);
    /// The value 0.5.
    pub const HALF: Fx = Fx(1i64 << 31);
    /// The smallest positive value, 2^-32 (raw 1).
    pub const EPSILON: Fx = Fx(1);
    /// The largest representable value (raw `i64::MAX`).
    pub const MAX: Fx = Fx(i64::MAX);
    /// The smallest representable value (raw `i64::MIN`).
    pub const MIN: Fx = Fx(i64::MIN);
    /// pi, rounded to the nearest raw value (`round(pi * 2^32)`).
    pub const PI: Fx = Fx(13_493_037_705);
    /// 2 * pi, rounded to the nearest raw value (`round(2 * pi * 2^32)`).
    pub const TAU: Fx = Fx(26_986_075_409);

    /// Builds a value from its raw Q32.32 representation.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::from_raw(1 << 32), Fx::ONE);
    /// ```
    pub const fn from_raw(raw: i64) -> Fx {
        Fx(raw)
    }

    /// Returns the raw Q32.32 representation.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::ONE.raw(), 1i64 << 32);
    /// ```
    pub const fn raw(self) -> i64 {
        self.0
    }

    /// Converts an integer exactly.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::from_int(-3).raw(), -3i64 << 32);
    /// assert_eq!(Fx::from_int(i32::MIN), Fx::MIN);
    /// ```
    pub const fn from_int(v: i32) -> Fx {
        Fx((v as i64) << 32)
    }

    /// Largest integer not greater than `self` (floor).
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::from_raw(-1).to_int_floor(), -1);
    /// assert_eq!(Fx::HALF.to_int_floor(), 0);
    /// ```
    pub const fn to_int_floor(self) -> i32 {
        // The shifted value lies in [-2^31, 2^31 - 1], so the narrowing is lossless.
        (self.0 >> 32) as i32
    }

    /// Rounds to the nearest integer, ties toward positive infinity (`floor(self + 0.5)`),
    /// saturating at `i32::MAX`.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::HALF.to_int_round_half_up(), 1);
    /// assert_eq!((-Fx::HALF).to_int_round_half_up(), 0);
    /// assert_eq!(Fx::MAX.to_int_round_half_up(), i32::MAX);
    /// ```
    pub fn to_int_round_half_up(self) -> i32 {
        let v = (i128::from(self.0) + (1i128 << 31)) >> 32;
        i32::try_from(v).unwrap_or(i32::MAX)
    }

    /// Absolute value; `MIN` saturates to `MAX`.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::MIN.abs(), Fx::MAX);
    /// assert_eq!((-Fx::ONE).abs(), Fx::ONE);
    /// ```
    pub const fn abs(self) -> Fx {
        Fx(self.0.saturating_abs())
    }

    /// The smaller of two values.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::ONE.min(Fx::ZERO), Fx::ZERO);
    /// ```
    pub const fn min(self, other: Fx) -> Fx {
        if self.0 <= other.0 {
            self
        } else {
            other
        }
    }

    /// The larger of two values.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::ONE.max(Fx::ZERO), Fx::ONE);
    /// ```
    pub const fn max(self, other: Fx) -> Fx {
        if self.0 >= other.0 {
            self
        } else {
            other
        }
    }

    /// Restricts `self` to `[lo, hi]`. Never panics: if `lo > hi` the lower bound is applied
    /// first and then the upper bound, so the result is `hi`.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::from_int(5).clamp(Fx::ZERO, Fx::ONE), Fx::ONE);
    /// assert_eq!(Fx::from_int(-5).clamp(Fx::ZERO, Fx::ONE), Fx::ZERO);
    /// ```
    pub const fn clamp(self, lo: Fx, hi: Fx) -> Fx {
        self.max(lo).min(hi)
    }

    /// Returns `-ONE`, `ZERO` or `ONE` according to the sign of `self`.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::MIN.signum(), -Fx::ONE);
    /// assert_eq!(Fx::ZERO.signum(), Fx::ZERO);
    /// ```
    pub const fn signum(self) -> Fx {
        Fx(self.0.signum() << 32)
    }

    /// Floor of the square root, computed with integer Newton iteration. Negative input
    /// returns `ZERO`.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::from_int(9).sqrt(), Fx::from_int(3));
    /// assert_eq!(Fx::from_int(-9).sqrt(), Fx::ZERO);
    /// ```
    pub fn sqrt(self) -> Fx {
        if self.0 <= 0 {
            return Fx::ZERO;
        }
        // sqrt(raw / 2^32) * 2^32 = sqrt(raw * 2^32); the result is below 2^48.
        let wide = u128::from(self.0.unsigned_abs()) << 32;
        Fx(saturate_u128(isqrt_u128(wide)))
    }

    /// Converts from `f64` (round to nearest, saturating, NaN becomes zero). Visuals and tests only.
    #[cfg(feature = "float-io")]
    pub fn from_f64(v: f64) -> Fx {
        // `as` from float to int saturates and maps NaN to 0.
        Fx((v * 4_294_967_296.0).round() as i64)
    }

    /// Converts to `f64`. Visuals and tests only.
    #[cfg(feature = "float-io")]
    pub fn to_f64(self) -> f64 {
        self.0 as f64 / 4_294_967_296.0
    }
}

impl Add for Fx {
    type Output = Fx;
    /// Saturating addition.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::MAX + Fx::ONE, Fx::MAX);
    /// ```
    fn add(self, rhs: Fx) -> Fx {
        Fx(self.0.saturating_add(rhs.0))
    }
}

impl Sub for Fx {
    type Output = Fx;
    /// Saturating subtraction.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::MIN - Fx::ONE, Fx::MIN);
    /// ```
    fn sub(self, rhs: Fx) -> Fx {
        Fx(self.0.saturating_sub(rhs.0))
    }
}

impl Neg for Fx {
    type Output = Fx;
    /// Saturating negation (`-MIN` is `MAX`).
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(-Fx::MIN, Fx::MAX);
    /// ```
    fn neg(self) -> Fx {
        Fx(self.0.saturating_neg())
    }
}

impl Mul for Fx {
    type Output = Fx;
    /// `(a * b) >> 32` in 128-bit arithmetic (floor), saturated to `i64`.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::HALF * Fx::HALF, Fx::from_raw(1 << 30));
    /// assert_eq!(Fx::MAX * Fx::MAX, Fx::MAX);
    /// ```
    fn mul(self, rhs: Fx) -> Fx {
        Fx(saturate_i64((i128::from(self.0) * i128::from(rhs.0)) >> 32))
    }
}

impl Div for Fx {
    type Output = Fx;
    /// `(a << 32) / b` truncating toward zero, saturated to `i64`. Division by zero gives
    /// `MAX` for a positive dividend, `MIN` for a negative one and `ZERO` for zero.
    ///
    /// ```
    /// use core_math::Fx;
    /// assert_eq!(Fx::ONE / Fx::from_int(2), Fx::HALF);
    /// assert_eq!(Fx::ONE / Fx::ZERO, Fx::MAX);
    /// assert_eq!(-Fx::ONE / Fx::ZERO, Fx::MIN);
    /// assert_eq!(Fx::ZERO / Fx::ZERO, Fx::ZERO);
    /// ```
    fn div(self, rhs: Fx) -> Fx {
        if rhs.0 == 0 {
            return match self.0.signum() {
                1 => Fx::MAX,
                -1 => Fx::MIN,
                _ => Fx::ZERO,
            };
        }
        Fx(saturate_i64((i128::from(self.0) << 32) / i128::from(rhs.0)))
    }
}

impl AddAssign for Fx {
    /// In-place saturating addition.
    ///
    /// ```
    /// use core_math::Fx;
    /// let mut a = Fx::ONE;
    /// a += Fx::ONE;
    /// assert_eq!(a, Fx::from_int(2));
    /// ```
    fn add_assign(&mut self, rhs: Fx) {
        *self = *self + rhs;
    }
}

impl SubAssign for Fx {
    /// In-place saturating subtraction.
    ///
    /// ```
    /// use core_math::Fx;
    /// let mut a = Fx::ONE;
    /// a -= Fx::ONE;
    /// assert_eq!(a, Fx::ZERO);
    /// ```
    fn sub_assign(&mut self, rhs: Fx) {
        *self = *self - rhs;
    }
}

impl MulAssign for Fx {
    /// In-place multiplication (see [`Mul`]).
    ///
    /// ```
    /// use core_math::Fx;
    /// let mut a = Fx::from_int(3);
    /// a *= Fx::from_int(2);
    /// assert_eq!(a, Fx::from_int(6));
    /// ```
    fn mul_assign(&mut self, rhs: Fx) {
        *self = *self * rhs;
    }
}

impl DivAssign for Fx {
    /// In-place division (see [`Div`]).
    ///
    /// ```
    /// use core_math::Fx;
    /// let mut a = Fx::from_int(6);
    /// a /= Fx::from_int(3);
    /// assert_eq!(a, Fx::from_int(2));
    /// ```
    fn div_assign(&mut self, rhs: Fx) {
        *self = *self / rhs;
    }
}

#[cfg(test)]
mod tests {
    use super::isqrt_u128;

    #[test]
    fn isqrt_small_and_extreme() {
        assert_eq!(isqrt_u128(0), 0);
        assert_eq!(isqrt_u128(1), 1);
        assert_eq!(isqrt_u128(3), 1);
        assert_eq!(isqrt_u128(4), 2);
        assert_eq!(isqrt_u128(u128::MAX), u128::from(u64::MAX));
        for n in 0u128..2000 {
            let s = isqrt_u128(n);
            assert!(s * s <= n && n < (s + 1) * (s + 1));
        }
    }
}
