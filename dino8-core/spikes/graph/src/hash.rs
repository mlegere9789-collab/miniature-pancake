//! Stable 128-bit content hashing.
//!
//! The engine keys memo entries and detects "value unchanged" (early cutoff) with
//! a 128-bit content hash.  The hash below is a fast, non-cryptographic
//! multiply-fold construction in the style of wyhash, run on two cross-fed lanes.
//! It is deterministic across platforms (little-endian canonicalisation of every
//! integer), which the determinism tests rely on.
//!
//! Production note (see RESULTS.md): the trait boundary here is what matters; the
//! hasher behind it can be swapped for BLAKE3 when content addresses must also be
//! collision-resistant against adversarial input (shared cache, sync server).

use std::fmt;

/// A 128-bit content hash.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Default, PartialOrd, Ord)]
pub struct Hash128 {
    pub hi: u64,
    pub lo: u64,
}

impl Hash128 {
    pub const ZERO: Hash128 = Hash128 { hi: 0, lo: 0 };

    /// Hash of a single integer.
    pub fn of_u64(x: u64) -> Hash128 {
        let mut h = ContentHasher::new();
        h.write_u64(x);
        h.finish()
    }

    /// Order-dependent combination of two hashes.
    pub fn combine(self, other: Hash128) -> Hash128 {
        let mut h = ContentHasher::new();
        h.write_u64(self.hi);
        h.write_u64(self.lo);
        h.write_u64(other.hi);
        h.write_u64(other.lo);
        h.finish()
    }

    /// Short human-readable form (first 8 hex digits).
    pub fn short(&self) -> String {
        format!("{:08x}", (self.hi >> 32) as u32)
    }
}

impl fmt::Display for Hash128 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:016x}{:016x}", self.hi, self.lo)
    }
}

impl fmt::Debug for Hash128 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "#{}", self.short())
    }
}

const K0: u64 = 0xa076_1d64_78bd_642f;
const K1: u64 = 0xe703_7ed1_a0b4_28db;
const K2: u64 = 0x8ebc_6af0_9c88_c6e3;
const K3: u64 = 0x5899_65cc_7537_4cc3;
const K4: u64 = 0x1d8e_4e27_c47d_124f;

#[inline(always)]
fn mum(a: u64, b: u64) -> u64 {
    let r = u128::from(a) * u128::from(b);
    (r as u64) ^ ((r >> 64) as u64)
}

/// Streaming hasher producing a [`Hash128`].
#[derive(Clone, Debug)]
pub struct ContentHasher {
    a: u64,
    b: u64,
    len: u64,
}

impl Default for ContentHasher {
    fn default() -> Self {
        Self::new()
    }
}

impl ContentHasher {
    pub const fn new() -> Self {
        Self { a: K0, b: K1, len: 0 }
    }

    /// A hasher whose output is separated from every other domain tag.
    pub fn with_domain(tag: u64) -> Self {
        let mut h = Self::new();
        h.write_u64(tag);
        h
    }

    #[inline]
    pub fn write_u64(&mut self, x: u64) {
        let a = mum(self.a ^ x ^ K2, self.b ^ K3);
        let b = mum(self.b.rotate_left(17) ^ x.rotate_left(32) ^ K4, self.a ^ K1);
        self.a = a;
        self.b = b;
        self.len = self.len.wrapping_add(1);
    }

    #[inline]
    pub fn write_u32(&mut self, x: u32) {
        self.write_u64(u64::from(x));
    }

    #[inline]
    pub fn write_f64(&mut self, x: f64) {
        // Canonicalise NaN so that "same value" hashes the same on every platform.
        let bits = if x.is_nan() { 0x7ff8_0000_0000_0000 } else { x.to_bits() };
        self.write_u64(bits);
    }

    #[inline]
    pub fn write_f32(&mut self, x: f32) {
        let bits = if x.is_nan() { 0x7fc0_0000 } else { x.to_bits() };
        self.write_u64(u64::from(bits));
    }

    pub fn write_bytes(&mut self, bytes: &[u8]) {
        let mut chunks = bytes.chunks_exact(8);
        for c in &mut chunks {
            let mut w = [0u8; 8];
            w.copy_from_slice(c);
            self.write_u64(u64::from_le_bytes(w));
        }
        let rem = chunks.remainder();
        let mut w = [0u8; 8];
        w[..rem.len()].copy_from_slice(rem);
        // Length-tagged tail so that "ab" and "ab\0" differ.
        self.write_u64(u64::from_le_bytes(w) ^ ((rem.len() as u64) << 56));
    }

    pub fn finish(&self) -> Hash128 {
        let a = mum(self.a ^ self.len, K2);
        let b = mum(self.b ^ self.len.rotate_left(32), K3);
        Hash128 { hi: mum(a ^ K0, b ^ K4), lo: mum(b ^ K1, a ^ K2) }
    }
}

/// Values that can be fed to the content hasher.  Two values that compare as
/// "the same result" must produce the same hash; the engine uses this to stop
/// invalidation early (early cutoff).
pub trait ContentHash {
    fn content_hash(&self, h: &mut ContentHasher);

    fn hash128(&self) -> Hash128 {
        let mut h = ContentHasher::new();
        self.content_hash(&mut h);
        h.finish()
    }
}

macro_rules! impl_int {
    ($($t:ty),*) => {$(
        impl ContentHash for $t {
            #[inline]
            fn content_hash(&self, h: &mut ContentHasher) { h.write_u64(*self as u64); }
        }
    )*};
}
impl_int!(u8, u16, u32, u64, usize, i8, i16, i32, i64, isize);

impl ContentHash for bool {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_u64(u64::from(*self));
    }
}
impl ContentHash for f64 {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_f64(*self);
    }
}
impl ContentHash for f32 {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_f32(*self);
    }
}
impl ContentHash for Hash128 {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_u64(self.hi);
        h.write_u64(self.lo);
    }
}
impl ContentHash for str {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_bytes(self.as_bytes());
    }
}
impl ContentHash for String {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_bytes(self.as_bytes());
    }
}
impl<T: ContentHash> ContentHash for [T] {
    fn content_hash(&self, h: &mut ContentHasher) {
        h.write_u64(self.len() as u64);
        for x in self {
            x.content_hash(h);
        }
    }
}
impl<T: ContentHash> ContentHash for Vec<T> {
    fn content_hash(&self, h: &mut ContentHasher) {
        self.as_slice().content_hash(h);
    }
}
impl<T: ContentHash + ?Sized> ContentHash for std::sync::Arc<T> {
    fn content_hash(&self, h: &mut ContentHasher) {
        (**self).content_hash(h);
    }
}
impl<A: ContentHash, B: ContentHash> ContentHash for (A, B) {
    fn content_hash(&self, h: &mut ContentHasher) {
        self.0.content_hash(h);
        self.1.content_hash(h);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;

    #[test]
    fn deterministic_and_input_sensitive() {
        assert_eq!(1u64.hash128(), 1u64.hash128());
        assert_ne!(1u64.hash128(), 2u64.hash128());
        assert_ne!(0u64.hash128(), Hash128::ZERO);
        assert_ne!("ab".hash128(), "ab\0".hash128());
        assert_ne!((1u64, 2u64).hash128(), (2u64, 1u64).hash128());
    }

    #[test]
    fn nan_is_canonical_and_signed_zero_is_distinct() {
        let n1 = f64::NAN;
        let n2 = -f64::NAN;
        assert_eq!(n1.hash128(), n2.hash128());
        assert_ne!(0.0f64.hash128(), (-0.0f64).hash128());
    }

    #[test]
    fn no_collisions_on_dense_keys() {
        let mut seen = HashSet::new();
        for i in 0..400_000u64 {
            assert!(seen.insert(i.hash128()), "collision at {i}");
        }
    }

    #[test]
    fn avalanche_is_close_to_half() {
        // Flip each input bit; on average about 64 of the 128 output bits move.
        let mut total = 0u64;
        let mut n = 0u64;
        for x in [0u64, 1, 0xdead_beef, 0x0123_4567_89ab_cdef, u64::MAX / 3] {
            let base = x.hash128();
            for bit in 0..64 {
                let other = (x ^ (1u64 << bit)).hash128();
                total += u64::from((base.hi ^ other.hi).count_ones() + (base.lo ^ other.lo).count_ones());
                n += 1;
            }
        }
        let mean = total as f64 / n as f64;
        assert!((56.0..72.0).contains(&mean), "mean flipped bits {mean}");
    }
}
