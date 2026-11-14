//! Identifier and hash newtypes shared by every module.
//!
//! Everything content-addressed uses BLAKE3 with a domain-separation prefix, so a hash of an
//! operation record can never collide with a hash of an object snapshot or an index node.

use std::fmt;
use std::hash::{BuildHasherDefault, Hasher};

/// Domain separators for the different content-addressed namespaces.
pub const DOM_OP: &str = "dino8/op/1";
pub const DOM_BLOB: &str = "dino8/blob/1";
pub const DOM_NODE: &str = "dino8/node/1";
pub const DOM_FRAME: &str = "dino8/frame/1";
pub const DOM_KEY: &str = "dino8/key/1";
pub const DOM_OBJ: &str = "dino8/objid/1";
pub const DOM_EMPTY: &str = "dino8/empty-state/1";

const HEX: &[u8; 16] = b"0123456789abcdef";

/// Lower-case hex encoding.
pub fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push(HEX[(b >> 4) as usize] as char);
        s.push(HEX[(b & 15) as usize] as char);
    }
    s
}

fn hex_val(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        _ => None,
    }
}

/// Strict lower-case hex decoding (upper case is rejected so the text form is canonical).
pub fn hex_decode(s: &str) -> Option<Vec<u8>> {
    let b = s.as_bytes();
    if b.len() % 2 != 0 {
        return None;
    }
    let mut out = Vec::with_capacity(b.len() / 2);
    for pair in b.chunks_exact(2) {
        out.push((hex_val(pair[0])? << 4) | hex_val(pair[1])?);
    }
    Some(out)
}

/// A 256-bit BLAKE3 digest.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Default)]
pub struct Hash(pub [u8; 32]);

impl Hash {
    pub const ZERO: Hash = Hash([0; 32]);

    /// Domain-separated digest of `data`.
    pub fn digest(domain: &str, data: &[u8]) -> Hash {
        let mut h = blake3::Hasher::new();
        h.update(domain.as_bytes());
        h.update(&[0]);
        h.update(data);
        Hash(*h.finalize().as_bytes())
    }

    pub fn to_hex(&self) -> String {
        hex_encode(&self.0)
    }

    pub fn from_hex(s: &str) -> Option<Hash> {
        let v = hex_decode(s)?;
        let a: [u8; 32] = v.try_into().ok()?;
        Some(Hash(a))
    }

    /// First eight bytes as an integer: the keys are already uniformly random.
    pub fn prefix64(&self) -> u64 {
        u64::from_le_bytes([
            self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5], self.0[6], self.0[7],
        ])
    }
}

impl std::hash::Hash for Hash {
    fn hash<H: Hasher>(&self, state: &mut H) {
        state.write_u64(self.prefix64());
    }
}

impl fmt::Display for Hash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.to_hex())
    }
}

impl fmt::Debug for Hash {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "#{}", &self.to_hex()[..12])
    }
}

/// Pass-through hasher for keys that are already uniformly distributed hashes.
#[derive(Default, Clone, Copy)]
pub struct PassHasher(u64);

impl Hasher for PassHasher {
    fn finish(&self) -> u64 {
        self.0
    }
    fn write(&mut self, bytes: &[u8]) {
        for b in bytes {
            self.0 = self.0.rotate_left(8) ^ u64::from(*b);
        }
    }
    fn write_u64(&mut self, i: u64) {
        self.0 = i;
    }
}

pub type PassBuild = BuildHasherDefault<PassHasher>;
pub type HashMap<K, V> = std::collections::HashMap<K, V, PassBuild>;

/// Identifier of an operation: the BLAKE3 digest of its canonical record.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct OpId(pub Hash);

impl OpId {
    pub fn to_hex(&self) -> String {
        self.0.to_hex()
    }
    pub fn from_hex(s: &str) -> Option<OpId> {
        Hash::from_hex(s).map(OpId)
    }
}

impl fmt::Display for OpId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0.to_hex())
    }
}

impl fmt::Debug for OpId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "op:{}", &self.0.to_hex()[..10])
    }
}

/// Stable identifier of a scene object (an entity).
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct ObjectId(pub [u8; 16]);

impl ObjectId {
    /// Deterministic id for the `n`-th object created by dot `(actor, seq)`.
    pub fn derive(actor: Actor, seq: u32, n: u32) -> ObjectId {
        let mut buf = Vec::with_capacity(24);
        buf.extend_from_slice(&actor.session.to_le_bytes());
        buf.extend_from_slice(&actor.epoch.to_le_bytes());
        buf.extend_from_slice(&seq.to_le_bytes());
        buf.extend_from_slice(&n.to_le_bytes());
        let h = Hash::digest(DOM_OBJ, &buf);
        let mut id = [0u8; 16];
        id.copy_from_slice(&h.0[..16]);
        ObjectId(id)
    }

    /// Small readable id for tests and demos.
    pub fn from_u64(n: u64) -> ObjectId {
        let mut id = [0u8; 16];
        id[8..].copy_from_slice(&n.to_be_bytes());
        ObjectId(id)
    }

    pub fn to_hex(&self) -> String {
        hex_encode(&self.0)
    }

    pub fn from_hex(s: &str) -> Option<ObjectId> {
        let v = hex_decode(s)?;
        let a: [u8; 16] = v.try_into().ok()?;
        Some(ObjectId(a))
    }

    /// Hash of the id used as the trie key (the ids are not assumed to be random).
    pub fn key_hash(&self) -> u64 {
        Hash::digest(DOM_KEY, &self.0).prefix64()
    }
}

impl fmt::Display for ObjectId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.to_hex())
    }
}

impl fmt::Debug for ObjectId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "obj:{}", &self.to_hex()[24..])
    }
}

/// A linear writer. A session starts a new actor (epoch bump) whenever its head moved by
/// anything other than appending, so the operations of one actor always form a single chain.
/// That chain property is what lets per-actor floors stand in for tombstones.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
pub struct Actor {
    pub session: u64,
    pub epoch: u32,
}

/// The unique identity of one write: `(actor, seq)`, `seq` counting from 1 along the actor chain.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
pub struct Dot {
    pub actor: Actor,
    pub seq: u32,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hex_round_trip() {
        let b = [0u8, 1, 0xab, 0xff];
        assert_eq!(hex_encode(&b), "0001abff");
        assert_eq!(hex_decode("0001abff").unwrap(), b);
        assert!(hex_decode("0001ABFF").is_none());
        assert!(hex_decode("abc").is_none());
    }

    #[test]
    fn domains_separate() {
        assert_ne!(Hash::digest(DOM_OP, b"x"), Hash::digest(DOM_BLOB, b"x"));
        assert_eq!(Hash::digest(DOM_OP, b"x"), Hash::digest(DOM_OP, b"x"));
    }

    #[test]
    fn derived_object_ids_are_deterministic_and_distinct() {
        let a = Actor { session: 7, epoch: 2 };
        assert_eq!(ObjectId::derive(a, 3, 0), ObjectId::derive(a, 3, 0));
        assert_ne!(ObjectId::derive(a, 3, 0), ObjectId::derive(a, 3, 1));
        assert_ne!(ObjectId::derive(a, 3, 0), ObjectId::derive(a, 4, 0));
    }
}
