//! Persistent sorted multiset-free ordered set (a two-level B+tree with
//! `Arc`-shared chunks). Used for the UUID map, edge sets and secondary
//! indexes. Clone is one `Arc` bump; an insert or remove copies one chunk
//! (at most 256 items), one group (at most 64 chunk pointers) and the root
//! vector (one entry per group).

use std::sync::Arc;

pub trait Keyed: Copy {
    type K: Ord + Copy;
    fn key(&self) -> Self::K;
}

impl Keyed for u64 {
    type K = u64;
    fn key(&self) -> u64 {
        *self
    }
}

const CHUNK_MAX: usize = 256;
const GROUP_MAX: usize = 64;
const BULK_FILL: usize = 192;
const BULK_GROUP: usize = 48;

struct Group<T: Keyed> {
    firsts: Vec<T::K>,
    chunks: Vec<Arc<Vec<T>>>,
}

impl<T: Keyed> Clone for Group<T> {
    fn clone(&self) -> Self {
        Group { firsts: self.firsts.clone(), chunks: self.chunks.clone() }
    }
}

struct RootEnt<T: Keyed> {
    first: T::K,
    group: Arc<Group<T>>,
}

impl<T: Keyed> Clone for RootEnt<T> {
    fn clone(&self) -> Self {
        RootEnt { first: self.first, group: self.group.clone() }
    }
}

pub struct Sorted<T: Keyed> {
    root: Arc<Vec<RootEnt<T>>>,
    len: usize,
}

impl<T: Keyed> Clone for Sorted<T> {
    fn clone(&self) -> Self {
        Sorted { root: self.root.clone(), len: self.len }
    }
}

impl<T: Keyed> Default for Sorted<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: Keyed> Sorted<T> {
    pub fn new() -> Self {
        Sorted { root: Arc::new(Vec::new()), len: 0 }
    }

    /// Build from items already sorted by key and free of duplicate keys.
    pub fn from_sorted(items: &[T]) -> Self {
        debug_assert!(items.windows(2).all(|w| w[0].key() < w[1].key()));
        let mut root = Vec::new();
        let mut chunks: Vec<Arc<Vec<T>>> = Vec::new();
        let mut firsts: Vec<T::K> = Vec::new();
        let flush = |root: &mut Vec<RootEnt<T>>, firsts: &mut Vec<T::K>, chunks: &mut Vec<Arc<Vec<T>>>| {
            if !chunks.is_empty() {
                let f = firsts[0];
                root.push(RootEnt {
                    first: f,
                    group: Arc::new(Group { firsts: std::mem::take(firsts), chunks: std::mem::take(chunks) }),
                });
            }
        };
        for c in items.chunks(BULK_FILL) {
            firsts.push(c[0].key());
            chunks.push(Arc::new(c.to_vec()));
            if chunks.len() == BULK_GROUP {
                flush(&mut root, &mut firsts, &mut chunks);
            }
        }
        flush(&mut root, &mut firsts, &mut chunks);
        Sorted { root: Arc::new(root), len: items.len() }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    fn locate(&self, k: T::K) -> Option<(usize, usize)> {
        if self.root.is_empty() {
            return None;
        }
        let r = self.root.partition_point(|e| e.first <= k).saturating_sub(1);
        let g = &self.root[r].group;
        let c = g.firsts.partition_point(|f| *f <= k).saturating_sub(1);
        Some((r, c))
    }

    pub fn get(&self, k: T::K) -> Option<&T> {
        let (r, c) = self.locate(k)?;
        let ch = &self.root[r].group.chunks[c];
        ch.binary_search_by(|t| t.key().cmp(&k)).ok().map(|p| &ch[p])
    }

    pub fn contains(&self, k: T::K) -> bool {
        self.get(k).is_some()
    }

    /// Insert; returns the replaced item when the key already existed.
    pub fn insert(&mut self, t: T) -> Option<T> {
        let k = t.key();
        let Some((r, c)) = self.locate(k) else {
            let g = Group { firsts: vec![k], chunks: vec![Arc::new(vec![t])] };
            Arc::make_mut(&mut self.root).push(RootEnt { first: k, group: Arc::new(g) });
            self.len = 1;
            return None;
        };
        // Check for an existing key first so a pure replace does not split.
        let root = Arc::make_mut(&mut self.root);
        let RootEnt { first, group } = &mut root[r];
        let g = Arc::make_mut(group);
        let chunk = Arc::make_mut(&mut g.chunks[c]);
        let mut replaced = None;
        match chunk.binary_search_by(|x| x.key().cmp(&k)) {
            Ok(p) => replaced = Some(std::mem::replace(&mut chunk[p], t)),
            Err(p) => {
                chunk.insert(p, t);
                if p == 0 {
                    g.firsts[c] = k;
                    if c == 0 {
                        *first = k;
                    }
                }
                self.len += 1;
            }
        }
        if g.chunks[c].len() > CHUNK_MAX {
            let chunk = Arc::make_mut(&mut g.chunks[c]);
            let right = chunk.split_off(chunk.len() / 2);
            g.firsts.insert(c + 1, right[0].key());
            g.chunks.insert(c + 1, Arc::new(right));
        }
        if g.chunks.len() > GROUP_MAX {
            let rc = g.chunks.split_off(g.chunks.len() / 2);
            let rf = g.firsts.split_off(g.firsts.len() / 2);
            let nf = rf[0];
            let ng = Group { firsts: rf, chunks: rc };
            root.insert(r + 1, RootEnt { first: nf, group: Arc::new(ng) });
        }
        replaced
    }

    pub fn remove(&mut self, k: T::K) -> Option<T> {
        let (r, c) = self.locate(k)?;
        let p = self.root[r].group.chunks[c].binary_search_by(|x| x.key().cmp(&k)).ok()?;
        let root = Arc::make_mut(&mut self.root);
        let RootEnt { first, group } = &mut root[r];
        let g = Arc::make_mut(group);
        let chunk = Arc::make_mut(&mut g.chunks[c]);
        let old = chunk.remove(p);
        self.len -= 1;
        if chunk.is_empty() {
            g.chunks.remove(c);
            g.firsts.remove(c);
            if g.chunks.is_empty() {
                root.remove(r);
                return Some(old);
            }
            if c == 0 {
                *first = g.firsts[0];
            }
        } else if p == 0 {
            g.firsts[c] = chunk[0].key();
            if c == 0 {
                *first = g.firsts[0];
            }
        }
        Some(old)
    }

    /// Items with `lo <= key <= hi`, in key order.
    pub fn range(&self, lo: T::K, hi: T::K) -> Iter<'_, T> {
        let Some((r, c)) = self.locate(lo) else {
            return Iter { root: &self.root, r: 0, c: 0, i: 0, hi: Some(hi), done: true };
        };
        let ch = &self.root[r].group.chunks[c];
        let i = ch.partition_point(|t| t.key() < lo);
        let mut it = Iter { root: &self.root, r, c, i, hi: Some(hi), done: false };
        it.normalize();
        it
    }

    pub fn iter(&self) -> Iter<'_, T> {
        Iter { root: &self.root, r: 0, c: 0, i: 0, hi: None, done: self.root.is_empty() }
    }

    pub fn heap_bytes(&self) -> usize {
        let mut b = self.root.capacity() * std::mem::size_of::<RootEnt<T>>() + 16;
        for e in self.root.iter() {
            b += std::mem::size_of::<Group<T>>() + 16;
            b += e.group.firsts.capacity() * std::mem::size_of::<T::K>();
            b += e.group.chunks.capacity() * 8;
            for c in &e.group.chunks {
                b += c.capacity() * std::mem::size_of::<T>() + 40;
            }
        }
        b
    }

    /// Structure check used by tests: ordering, `firsts` and `len`.
    pub fn check(&self) -> Result<(), String> {
        let mut n = 0usize;
        let mut prev: Option<T::K> = None;
        for (ri, e) in self.root.iter().enumerate() {
            if e.group.chunks.is_empty() {
                return Err(format!("empty group {ri}"));
            }
            if e.first != e.group.firsts[0] {
                return Err(format!("root.first mismatch at {ri}"));
            }
            for (ci, ch) in e.group.chunks.iter().enumerate() {
                if ch.is_empty() {
                    return Err("empty chunk".into());
                }
                if e.group.firsts[ci] != ch[0].key() {
                    return Err(format!("firsts mismatch {ri}/{ci}"));
                }
                for t in ch.iter() {
                    if let Some(p) = prev {
                        if p >= t.key() {
                            return Err("order violated".into());
                        }
                    }
                    prev = Some(t.key());
                    n += 1;
                }
            }
        }
        if n != self.len {
            return Err(format!("len {} != counted {}", self.len, n));
        }
        Ok(())
    }
}

pub struct Iter<'a, T: Keyed> {
    root: &'a [RootEnt<T>],
    r: usize,
    c: usize,
    i: usize,
    hi: Option<T::K>,
    done: bool,
}

impl<'a, T: Keyed> Iter<'a, T> {
    /// Move to the next valid (chunk, index) position if `i` ran off the chunk.
    fn normalize(&mut self) {
        loop {
            if self.r >= self.root.len() {
                self.done = true;
                return;
            }
            let g = &self.root[self.r].group;
            if self.c >= g.chunks.len() {
                self.r += 1;
                self.c = 0;
                self.i = 0;
                continue;
            }
            if self.i >= g.chunks[self.c].len() {
                self.c += 1;
                self.i = 0;
                continue;
            }
            return;
        }
    }
}

impl<'a, T: Keyed> Iterator for Iter<'a, T> {
    type Item = T;
    fn next(&mut self) -> Option<T> {
        if self.done {
            return None;
        }
        let t = self.root[self.r].group.chunks[self.c][self.i];
        if let Some(hi) = self.hi {
            if t.key() > hi {
                self.done = true;
                return None;
            }
        }
        self.i += 1;
        self.normalize();
        Some(t)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;

    #[test]
    fn matches_btreeset() {
        let mut s: Sorted<u64> = Sorted::new();
        let mut m = BTreeSet::new();
        let mut x = 0x9E3779B97F4A7C15u64;
        for step in 0..40_000u64 {
            x = x.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            let k = (x >> 40) % 5000;
            if step % 3 == 2 {
                assert_eq!(s.remove(k).is_some(), m.remove(&k));
            } else {
                assert_eq!(s.insert(k).is_some(), !m.insert(k));
            }
            if step % 5000 == 0 {
                s.check().unwrap();
            }
        }
        s.check().unwrap();
        assert_eq!(s.iter().collect::<Vec<_>>(), m.iter().copied().collect::<Vec<_>>());
        assert_eq!(s.range(100, 900).collect::<Vec<_>>(), m.range(100..=900).copied().collect::<Vec<_>>());
    }

    #[test]
    fn bulk_and_snapshot() {
        let items: Vec<u64> = (0..10_000).map(|i| i * 2).collect();
        let mut a = Sorted::from_sorted(&items);
        a.check().unwrap();
        let b = a.clone();
        for i in 0..3000 {
            a.insert(i * 2 + 1);
        }
        for i in 0..1000 {
            a.remove(i * 2);
        }
        a.check().unwrap();
        b.check().unwrap();
        assert_eq!(b.len(), 10_000);
        assert_eq!(b.iter().count(), 10_000);
        assert_eq!(a.len(), 10_000 + 3000 - 1000);
    }
}
