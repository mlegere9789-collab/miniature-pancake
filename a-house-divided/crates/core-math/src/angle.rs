//! Angles (2^32 per turn), table-driven sine/cosine and CORDIC arctangent.

use crate::tables::{ATAN_TURNS, SIN_QUARTER};
use crate::{Angle, Fx};
use core::ops::{Add, AddAssign, Neg, Sub, SubAssign};

/// One quadrant in raw angle units (a quarter turn).
const QUARTER_RAW: u32 = 1 << 30;

/// Sine of an angle in `0..=2^30` (first quadrant), as a Q32 integer, by linear interpolation
/// between 1025 table entries.
fn sin_first_quadrant(w: u32) -> i64 {
    let idx = (w >> 20) as usize;
    let frac = i64::from(w & 0xF_FFFF);
    if idx >= 1024 {
        return SIN_QUARTER[1024];
    }
    let (a, b) = (SIN_QUARTER[idx], SIN_QUARTER[idx + 1]);
    a + (((b - a) * frac + (1 << 19)) >> 20)
}

impl Angle {
    /// The zero angle.
    pub const ZERO: Angle = Angle(0);
    /// A quarter turn (90 degrees).
    pub const QUARTER: Angle = Angle(QUARTER_RAW);
    /// A half turn (180 degrees).
    pub const HALF: Angle = Angle(1 << 31);

    /// Builds an angle from raw units of 2^-32 turns.
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(Angle::from_raw(5).raw(), 5);
    /// ```
    pub const fn from_raw(raw: u32) -> Angle {
        Angle(raw)
    }

    /// Returns the raw units of 2^-32 turns.
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(Angle::HALF.raw(), 1 << 31);
    /// ```
    pub const fn raw(self) -> u32 {
        self.0
    }

    /// The angle `num / den` of a full turn, rounded to the nearest raw unit and reduced modulo
    /// one turn (negative or large `num` wrap). `den == 0` gives the zero angle.
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(Angle::from_turns_fraction(1, 4), Angle::QUARTER);
    /// assert_eq!(Angle::from_turns_fraction(-1, 4).raw(), 3 << 30);
    /// assert_eq!(Angle::from_turns_fraction(1, 0), Angle::ZERO);
    /// ```
    pub fn from_turns_fraction(num: i32, den: u32) -> Angle {
        if den == 0 {
            return Angle::ZERO;
        }
        let den = i128::from(den);
        let r = i128::from(num).rem_euclid(den);
        let raw = ((r << 32) + den / 2) / den;
        // `raw` can equal 2^32 after rounding, which is the zero angle; truncation wraps it.
        Angle(raw as u32)
    }

    /// An angle from whole degrees, reduced modulo 360 and rounded to the nearest raw unit.
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(Angle::from_degrees_int(90), Angle::QUARTER);
    /// assert_eq!(Angle::from_degrees_int(-90).raw(), 3 << 30);
    /// assert_eq!(Angle::from_degrees_int(360), Angle::ZERO);
    /// ```
    pub fn from_degrees_int(deg: i32) -> Angle {
        let d = i64::from(deg.rem_euclid(360));
        let raw = ((d << 32) + 180) / 360;
        Angle(raw as u32)
    }

    /// The angle in radians as an [`Fx`] in `[0, TAU)`.
    ///
    /// ```
    /// use core_math::{Angle, Fx};
    /// assert_eq!(Angle::ZERO.to_fx_radians(), Fx::ZERO);
    /// assert!((Angle::HALF.to_fx_radians().raw() - Fx::PI.raw()).abs() <= 1);
    /// ```
    pub fn to_fx_radians(self) -> Fx {
        let wide = (u128::from(self.0) * u128::from(Fx::TAU.0.unsigned_abs())) >> 32;
        Fx(crate::fx::saturate_u128(wide))
    }

    /// Sine by table lookup with linear interpolation (error below 2^-20). Exact at multiples of
    /// a quarter turn.
    ///
    /// ```
    /// use core_math::{Angle, Fx};
    /// assert_eq!(Angle::ZERO.sin(), Fx::ZERO);
    /// assert_eq!(Angle::QUARTER.sin(), Fx::ONE);
    /// ```
    pub fn sin(self) -> Fx {
        let quadrant = self.0 >> 30;
        let w = self.0 & (QUARTER_RAW - 1);
        let v = match quadrant {
            0 => sin_first_quadrant(w),
            1 => sin_first_quadrant(QUARTER_RAW - w),
            2 => -sin_first_quadrant(w),
            _ => -sin_first_quadrant(QUARTER_RAW - w),
        };
        Fx(v)
    }

    /// Cosine, defined as `sin(self + quarter turn)` (error below 2^-20).
    ///
    /// ```
    /// use core_math::{Angle, Fx};
    /// assert_eq!(Angle::ZERO.cos(), Fx::ONE);
    /// assert_eq!(Angle::QUARTER.cos(), Fx::ZERO);
    /// ```
    pub fn cos(self) -> Fx {
        (self + Angle::QUARTER).sin()
    }

    /// Four-quadrant arctangent of `y / x` as an angle in turns (0 along +x, counter-clockwise),
    /// integer-only (CORDIC, error well below 2^-18 turns). `atan2(0, 0)` is the zero angle;
    /// axis directions are exact.
    ///
    /// ```
    /// use core_math::{Angle, Fx};
    /// assert_eq!(Angle::atan2(Fx::ONE, Fx::ZERO), Angle::QUARTER);
    /// assert_eq!(Angle::atan2(Fx::ZERO, -Fx::ONE), Angle::HALF);
    /// ```
    pub fn atan2(y: Fx, x: Fx) -> Angle {
        if y.0 == 0 {
            return if x.0 >= 0 { Angle::ZERO } else { Angle::HALF };
        }
        if x.0 == 0 {
            return if y.0 > 0 {
                Angle::QUARTER
            } else {
                Angle(3 << 30)
            };
        }
        // Work in the first quadrant on magnitudes, then reflect, so atan2 is odd in y and
        // symmetric in x by construction.
        let ax = u128::from(x.0.unsigned_abs());
        let ay = u128::from(y.0.unsigned_abs());
        let first = Angle(first_quadrant_atan(ax, ay));
        match (x.0 > 0, y.0 > 0) {
            (true, true) => first,
            (false, true) => Angle::HALF - first,
            (false, false) => Angle::HALF + first,
            (true, false) => -first,
        }
    }
}

/// CORDIC vectoring mode for `ax, ay > 0`: returns `atan(ay / ax)` in raw angle units.
fn first_quadrant_atan(ax: u128, ay: u128) -> u32 {
    // Scale so the larger magnitude has its top bit at position 60 (keeps all 32 iterations
    // precise for tiny vectors and leaves headroom for the CORDIC gain).
    let top = 127 - ax.max(ay).leading_zeros();
    let scale = |v: u128| -> i128 {
        let s = if top <= 60 {
            v << (60 - top)
        } else {
            v >> (top - 60)
        };
        s as i128
    };
    let (mut x, mut y) = (scale(ax), scale(ay));
    let mut angle: i64 = 0;
    for (i, step) in ATAN_TURNS.iter().enumerate() {
        let (xs, ys) = (x >> i, y >> i);
        if y >= 0 {
            x += ys;
            y -= xs;
            angle += i64::from(*step);
        } else {
            x -= ys;
            y += xs;
            angle -= i64::from(*step);
        }
    }
    let clamped = angle.clamp(0, i64::from(QUARTER_RAW));
    // Lossless: clamped to [0, 2^30].
    clamped as u32
}

impl Add for Angle {
    type Output = Angle;
    /// Wrapping addition (modulo one turn).
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(Angle::from_raw(u32::MAX) + Angle::from_raw(1), Angle::ZERO);
    /// ```
    fn add(self, rhs: Angle) -> Angle {
        Angle(self.0.wrapping_add(rhs.0))
    }
}

impl Sub for Angle {
    type Output = Angle;
    /// Wrapping subtraction (modulo one turn).
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(Angle::ZERO - Angle::from_raw(1), Angle::from_raw(u32::MAX));
    /// ```
    fn sub(self, rhs: Angle) -> Angle {
        Angle(self.0.wrapping_sub(rhs.0))
    }
}

impl Neg for Angle {
    type Output = Angle;
    /// Wrapping negation (the angle in the opposite direction).
    ///
    /// ```
    /// use core_math::Angle;
    /// assert_eq!(-Angle::QUARTER, Angle::from_raw(3 << 30));
    /// ```
    fn neg(self) -> Angle {
        Angle(self.0.wrapping_neg())
    }
}

impl AddAssign for Angle {
    /// In-place wrapping addition.
    ///
    /// ```
    /// use core_math::Angle;
    /// let mut a = Angle::HALF;
    /// a += Angle::HALF;
    /// assert_eq!(a, Angle::ZERO);
    /// ```
    fn add_assign(&mut self, rhs: Angle) {
        *self = *self + rhs;
    }
}

impl SubAssign for Angle {
    /// In-place wrapping subtraction.
    ///
    /// ```
    /// use core_math::Angle;
    /// let mut a = Angle::ZERO;
    /// a -= Angle::QUARTER;
    /// assert_eq!(a, Angle::from_raw(3 << 30));
    /// ```
    fn sub_assign(&mut self, rhs: Angle) {
        *self = *self - rhs;
    }
}
