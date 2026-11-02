//! SplitMix64 and xoshiro256** (both public-domain algorithms), plus stream derivation.

use crate::{Fx, RngStreams};

/// Weyl-sequence increment of SplitMix64 (the 64-bit golden ratio).
const GOLDEN: u64 = 0x9E37_79B9_7F4A_7C15;

/// The SplitMix64 output finalizer (a bijection on `u64`).
const fn mix64(mut z: u64) -> u64 {
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

/// The SplitMix64 generator, used to expand seeds into xoshiro256** state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    /// Creates a generator whose first output is derived from `seed + GOLDEN`.
    ///
    /// ```
    /// use core_math::SplitMix64;
    /// assert_eq!(SplitMix64::new(0).next_u64(), 0xE220_A839_7B1D_CDAF);
    /// ```
    pub const fn new(seed: u64) -> SplitMix64 {
        SplitMix64 { state: seed }
    }

    /// Returns the next 64-bit output.
    ///
    /// ```
    /// use core_math::SplitMix64;
    /// let mut sm = SplitMix64::new(0);
    /// assert_eq!(sm.next_u64(), 0xE220_A839_7B1D_CDAF);
    /// assert_eq!(sm.next_u64(), 0x6E78_9E6A_A1B9_65F4);
    /// ```
    pub fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(GOLDEN);
        mix64(self.state)
    }
}

/// Identifier of a named random stream (see [`RngStreams::stream`]).
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct StreamId(pub u32);

impl StreamId {
    /// Wraps a raw stream number.
    ///
    /// ```
    /// use core_math::StreamId;
    /// assert_eq!(StreamId::new(7).get(), 7);
    /// ```
    pub const fn new(id: u32) -> StreamId {
        StreamId(id)
    }

    /// Returns the raw stream number.
    ///
    /// ```
    /// use core_math::StreamId;
    /// assert_eq!(StreamId(3).get(), 3);
    /// ```
    pub const fn get(self) -> u32 {
        self.0
    }
}

impl RngStreams {
    /// Creates the stream factory for a master seed.
    ///
    /// ```
    /// use core_math::RngStreams;
    /// assert_eq!(RngStreams::new(7).master_seed(), 7);
    /// ```
    pub const fn new(master_seed: u64) -> RngStreams {
        RngStreams { master_seed }
    }

    /// Returns the master seed.
    ///
    /// ```
    /// use core_math::RngStreams;
    /// assert_eq!(RngStreams::new(u64::MAX).master_seed(), u64::MAX);
    /// ```
    pub const fn master_seed(&self) -> u64 {
        self.master_seed
    }

    /// Derives the independent generator for `id`. Pure: it never mutates `self`, and the result
    /// depends only on `(master_seed, id)`, so adding a stream never changes another stream.
    ///
    /// ```
    /// use core_math::{RngStreams, StreamId};
    /// let streams = RngStreams::new(42);
    /// let mut a = streams.stream(StreamId::new(1));
    /// let mut b = streams.stream(StreamId::new(1));
    /// assert_eq!(a.next_u64(), b.next_u64());
    /// let mut c = streams.stream(StreamId::new(2));
    /// assert_ne!(streams.stream(StreamId::new(1)).state(), c.state());
    /// ```
    pub fn stream(&self, id: StreamId) -> Rng {
        // Mixing the seed first and then folding in an injective function of the id (an odd
        // multiplier is a bijection on u64) makes distinct ids give distinct inputs to the
        // bijective finalizer, hence distinct stream seeds.
        let base = mix64(self.master_seed.wrapping_add(GOLDEN));
        let tagged = u64::from(id.0).wrapping_add(1).wrapping_mul(GOLDEN);
        Rng::from_seed(mix64(base ^ tagged))
    }
}

/// xoshiro256** generator. `Clone` copies the exact state.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Rng {
    s: [u64; 4],
}

impl Rng {
    /// Seeds the generator by taking four consecutive SplitMix64 outputs as the state.
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut r = Rng::from_seed(0);
    /// assert_eq!(r.state()[0], 0xE220_A839_7B1D_CDAF);
    /// let _ = r.next_u64();
    /// ```
    pub fn from_seed(seed: u64) -> Rng {
        let mut sm = SplitMix64::new(seed);
        Rng {
            s: [sm.next_u64(), sm.next_u64(), sm.next_u64(), sm.next_u64()],
        }
    }

    /// Restores a generator from a saved state. The all-zero state is invalid for xoshiro (it
    /// would only ever output zero), so it is replaced by the state seeded from 0.
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut a = Rng::from_seed(5);
    /// let mut b = Rng::from_state(a.state());
    /// assert_eq!(a.next_u64(), b.next_u64());
    /// ```
    pub fn from_state(state: [u64; 4]) -> Rng {
        if state == [0; 4] {
            Rng::from_seed(0)
        } else {
            Rng { s: state }
        }
    }

    /// Returns the exact generator state.
    ///
    /// ```
    /// use core_math::Rng;
    /// let r = Rng::from_seed(1);
    /// assert_eq!(Rng::from_state(r.state()), r);
    /// ```
    pub fn state(&self) -> [u64; 4] {
        self.s
    }

    /// Next 64-bit output (xoshiro256**).
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut r = Rng::from_seed(0);
    /// assert_ne!(r.next_u64(), r.next_u64());
    /// ```
    pub fn next_u64(&mut self) -> u64 {
        let s = &mut self.s;
        let result = s[1].wrapping_mul(5).rotate_left(7).wrapping_mul(9);
        let t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = s[3].rotate_left(45);
        result
    }

    /// Next 32-bit output: the high half of [`Rng::next_u64`].
    ///
    /// ```
    /// use core_math::Rng;
    /// let (mut a, mut b) = (Rng::from_seed(3), Rng::from_seed(3));
    /// assert_eq!(u64::from(a.next_u32()), b.next_u64() >> 32);
    /// ```
    pub fn next_u32(&mut self) -> u32 {
        (self.next_u64() >> 32) as u32
    }

    /// Unbiased uniform integer in the half-open range `[lo, hi)` (Lemire's method). If
    /// `hi <= lo` the range is empty and `lo` is returned without consuming randomness.
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut r = Rng::from_seed(9);
    /// let v = r.range_u32(10, 20);
    /// assert!((10..20).contains(&v));
    /// assert_eq!(r.range_u32(4, 4), 4);
    /// ```
    pub fn range_u32(&mut self, lo: u32, hi: u32) -> u32 {
        if hi <= lo {
            return lo;
        }
        lo + self.below_u32(hi - lo)
    }

    /// Unbiased uniform integer in the half-open range `[lo, hi)`. If `hi <= lo` the range is
    /// empty and `lo` is returned without consuming randomness.
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut r = Rng::from_seed(9);
    /// let v = r.range_i32(-5, 5);
    /// assert!((-5..5).contains(&v));
    /// ```
    pub fn range_i32(&mut self, lo: i32, hi: i32) -> i32 {
        if hi <= lo {
            return lo;
        }
        // hi - lo is in 1..=2^32 - 1 here, so it fits a u32.
        let span = (i64::from(hi) - i64::from(lo)) as u32;
        (i64::from(lo) + i64::from(self.below_u32(span))) as i32
    }

    /// Uniform value in `[0, ONE)` with 32 random fractional bits.
    ///
    /// ```
    /// use core_math::{Fx, Rng};
    /// let u = Rng::from_seed(2).unit_fx();
    /// assert!(u >= Fx::ZERO && u < Fx::ONE);
    /// ```
    pub fn unit_fx(&mut self) -> Fx {
        Fx::from_raw(i64::from(self.next_u32()))
    }

    /// Returns `true` with probability `num / den`. `den == 0` is always `false` and
    /// `num >= den` always `true`; in both degenerate cases no randomness is consumed.
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut r = Rng::from_seed(2);
    /// assert!(r.coin(1, 1));
    /// assert!(!r.coin(0, 3));
    /// ```
    pub fn coin(&mut self, num: u32, den: u32) -> bool {
        if den == 0 || num == 0 {
            return false;
        }
        if num >= den {
            return true;
        }
        self.below_u32(den) < num
    }

    /// Fisher-Yates shuffle of `items`.
    ///
    /// ```
    /// use core_math::Rng;
    /// let mut v = [1, 2, 3, 4, 5];
    /// Rng::from_seed(1).shuffle(&mut v);
    /// v.sort_unstable();
    /// assert_eq!(v, [1, 2, 3, 4, 5]);
    /// ```
    pub fn shuffle<T>(&mut self, items: &mut [T]) {
        for i in (1..items.len()).rev() {
            let j = self.below_u64(i as u64 + 1);
            items.swap(i, j as usize);
        }
    }

    /// Lemire's nearly-divisionless unbiased bounded integer in `[0, n)`, `n > 0`.
    fn below_u32(&mut self, n: u32) -> u32 {
        let mut m = u64::from(self.next_u32()) * u64::from(n);
        let mut low = m as u32;
        if low < n {
            let threshold = n.wrapping_neg() % n;
            while low < threshold {
                m = u64::from(self.next_u32()) * u64::from(n);
                low = m as u32;
            }
        }
        (m >> 32) as u32
    }

    /// 64-bit version of [`Rng::below_u32`], `n > 0`.
    fn below_u64(&mut self, n: u64) -> u64 {
        let mut m = u128::from(self.next_u64()) * u128::from(n);
        let mut low = m as u64;
        if low < n {
            let threshold = n.wrapping_neg() % n;
            while low < threshold {
                m = u128::from(self.next_u64()) * u128::from(n);
                low = m as u64;
            }
        }
        (m >> 64) as u64
    }
}
