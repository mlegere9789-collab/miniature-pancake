//! 2D fixed-point vectors.

use crate::fx::{isqrt_u128, saturate_i64, saturate_u128};
use crate::{Angle, Fx, Vec2Fx};
use core::ops::{Add, Neg, Sub};

/// `x^2 + y^2` of the raw components (a Q64 value) as an exact `u128`.
fn sum_squares_raw(v: Vec2Fx) -> u128 {
    let (x, y) = (
        u128::from(v.x.0.unsigned_abs()),
        u128::from(v.y.0.unsigned_abs()),
    );
    // Each square is at most 2^126, so the sum cannot overflow a u128.
    x * x + y * y
}

impl Vec2Fx {
    /// The zero vector.
    pub const ZERO: Vec2Fx = Vec2Fx {
        x: Fx::ZERO,
        y: Fx::ZERO,
    };

    /// Builds a vector from its components.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let v = Vec2Fx::new(Fx::ONE, Fx::ZERO);
    /// assert_eq!(v.x, Fx::ONE);
    /// ```
    pub const fn new(x: Fx, y: Fx) -> Vec2Fx {
        Vec2Fx { x, y }
    }

    /// Multiplies both components by `k` (saturating, see [`Fx`] multiplication).
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let v = Vec2Fx::new(Fx::ONE, Fx::from_int(2)).scale(Fx::from_int(3));
    /// assert_eq!(v, Vec2Fx::new(Fx::from_int(3), Fx::from_int(6)));
    /// ```
    pub fn scale(self, k: Fx) -> Vec2Fx {
        Vec2Fx {
            x: self.x * k,
            y: self.y * k,
        }
    }

    /// Dot product, accumulated exactly in 128 bits and then shifted down once (floor) and
    /// saturated.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let a = Vec2Fx::new(Fx::from_int(1), Fx::from_int(2));
    /// let b = Vec2Fx::new(Fx::from_int(3), Fx::from_int(4));
    /// assert_eq!(a.dot(b), Fx::from_int(11));
    /// ```
    pub fn dot(self, other: Vec2Fx) -> Fx {
        let a = i128::from(self.x.0) * i128::from(other.x.0);
        let b = i128::from(self.y.0) * i128::from(other.y.0);
        Fx(saturate_i64(a.saturating_add(b) >> 32))
    }

    /// 2D cross product `self.x * other.y - self.y * other.x` (exact in 128 bits, then floor-shifted
    /// and saturated).
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let x = Vec2Fx::new(Fx::ONE, Fx::ZERO);
    /// let y = Vec2Fx::new(Fx::ZERO, Fx::ONE);
    /// assert_eq!(x.cross(y), Fx::ONE);
    /// assert_eq!(y.cross(x), -Fx::ONE);
    /// ```
    pub fn cross(self, other: Vec2Fx) -> Fx {
        let a = i128::from(self.x.0) * i128::from(other.y.0);
        let b = i128::from(self.y.0) * i128::from(other.x.0);
        Fx(saturate_i64(a.saturating_sub(b) >> 32))
    }

    /// Squared length, `(x^2 + y^2) >> 32`, saturated.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// assert_eq!(Vec2Fx::new(Fx::from_int(3), Fx::from_int(4)).length_sq(), Fx::from_int(25));
    /// ```
    pub fn length_sq(self) -> Fx {
        Fx(saturate_u128(sum_squares_raw(self) >> 32))
    }

    /// Length: the floor of the exact square root, computed in 128 bits so components up to 2^30 in
    /// magnitude cannot overflow; saturates at `MAX`.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// assert_eq!(Vec2Fx::new(Fx::from_int(3), Fx::from_int(4)).length(), Fx::from_int(5));
    /// ```
    pub fn length(self) -> Fx {
        Fx(saturate_u128(isqrt_u128(sum_squares_raw(self))))
    }

    /// Squared distance to `other` (the difference saturates per component).
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let a = Vec2Fx::new(Fx::ONE, Fx::ONE);
    /// let b = Vec2Fx::new(Fx::from_int(4), Fx::from_int(5));
    /// assert_eq!(a.distance_sq(b), Fx::from_int(25));
    /// ```
    pub fn distance_sq(self, other: Vec2Fx) -> Fx {
        (self - other).length_sq()
    }

    /// Unit vector in the same direction; the zero vector returns zero. The vector is rescaled
    /// to use the full 128-bit range first, so even tiny vectors normalise accurately.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// assert_eq!(Vec2Fx::new(Fx::from_int(5), Fx::ZERO).normalize(), Vec2Fx::new(Fx::ONE, Fx::ZERO));
    /// assert_eq!(Vec2Fx::ZERO.normalize(), Vec2Fx::ZERO);
    /// ```
    pub fn normalize(self) -> Vec2Fx {
        let (ax, ay) = (
            u128::from(self.x.0.unsigned_abs()),
            u128::from(self.y.0.unsigned_abs()),
        );
        let m = ax.max(ay);
        if m == 0 {
            return Vec2Fx::ZERO;
        }
        // Put the larger magnitude's top bit at position 61.
        let top = 127 - m.leading_zeros();
        let scale = |v: u128| {
            if top <= 61 {
                v << (61 - top)
            } else {
                v >> (top - 61)
            }
        };
        let (sx, sy) = (scale(ax), scale(ay));
        let len = isqrt_u128(sx * sx + sy * sy);
        let component = |s: u128, negative: bool| {
            // s <= len, so the quotient is at most 2^32.
            let q = i64::try_from((s << 32) / len).unwrap_or(i64::MAX);
            Fx(if negative { -q } else { q })
        };
        Vec2Fx {
            x: component(sx, self.x.0 < 0),
            y: component(sy, self.y.0 < 0),
        }
    }

    /// Rotates counter-clockwise by `angle` (uses the sine/cosine table; products are exact in
    /// 128 bits before one floor-shift and saturation).
    ///
    /// ```
    /// use core_math::{Angle, Fx, Vec2Fx};
    /// let v = Vec2Fx::new(Fx::ONE, Fx::ZERO).rotate(Angle::QUARTER);
    /// assert_eq!(v, Vec2Fx::new(Fx::ZERO, Fx::ONE));
    /// ```
    pub fn rotate(self, angle: Angle) -> Vec2Fx {
        let (s, c) = (i128::from(angle.sin().0), i128::from(angle.cos().0));
        let (x, y) = (i128::from(self.x.0), i128::from(self.y.0));
        Vec2Fx {
            x: Fx(saturate_i64((x * c - y * s) >> 32)),
            y: Fx(saturate_i64((x * s + y * c) >> 32)),
        }
    }

    /// The unit vector `(cos, sin)` of `angle`.
    ///
    /// ```
    /// use core_math::{Angle, Fx, Vec2Fx};
    /// assert_eq!(Vec2Fx::from_angle(Angle::ZERO), Vec2Fx::new(Fx::ONE, Fx::ZERO));
    /// ```
    pub fn from_angle(angle: Angle) -> Vec2Fx {
        Vec2Fx {
            x: angle.cos(),
            y: angle.sin(),
        }
    }

    /// Linear interpolation `self + (other - self) * t` per component (exact intermediate in
    /// 128 bits, saturating result). `t = 0` gives `self` and `t = ONE` gives `other`.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let a = Vec2Fx::new(Fx::ZERO, Fx::ZERO);
    /// let b = Vec2Fx::new(Fx::from_int(4), Fx::from_int(8));
    /// assert_eq!(a.lerp(b, Fx::HALF), Vec2Fx::new(Fx::from_int(2), Fx::from_int(4)));
    /// ```
    pub fn lerp(self, other: Vec2Fx, t: Fx) -> Vec2Fx {
        let lerp1 = |a: Fx, b: Fx| {
            let d = i128::from(b.0) - i128::from(a.0);
            Fx(saturate_i64(
                i128::from(a.0) + ((d * i128::from(t.0)) >> 32),
            ))
        };
        Vec2Fx {
            x: lerp1(self.x, other.x),
            y: lerp1(self.y, other.y),
        }
    }
}

impl Add for Vec2Fx {
    type Output = Vec2Fx;
    /// Component-wise saturating addition.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let v = Vec2Fx::new(Fx::ONE, Fx::ONE) + Vec2Fx::new(Fx::ONE, Fx::ZERO);
    /// assert_eq!(v, Vec2Fx::new(Fx::from_int(2), Fx::ONE));
    /// ```
    fn add(self, rhs: Vec2Fx) -> Vec2Fx {
        Vec2Fx {
            x: self.x + rhs.x,
            y: self.y + rhs.y,
        }
    }
}

impl Sub for Vec2Fx {
    type Output = Vec2Fx;
    /// Component-wise saturating subtraction.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// let v = Vec2Fx::new(Fx::ONE, Fx::ONE) - Vec2Fx::new(Fx::ONE, Fx::ZERO);
    /// assert_eq!(v, Vec2Fx::new(Fx::ZERO, Fx::ONE));
    /// ```
    fn sub(self, rhs: Vec2Fx) -> Vec2Fx {
        Vec2Fx {
            x: self.x - rhs.x,
            y: self.y - rhs.y,
        }
    }
}

impl Neg for Vec2Fx {
    type Output = Vec2Fx;
    /// Component-wise saturating negation.
    ///
    /// ```
    /// use core_math::{Fx, Vec2Fx};
    /// assert_eq!(-Vec2Fx::new(Fx::ONE, Fx::MIN), Vec2Fx::new(-Fx::ONE, Fx::MAX));
    /// ```
    fn neg(self) -> Vec2Fx {
        Vec2Fx {
            x: -self.x,
            y: -self.y,
        }
    }
}
