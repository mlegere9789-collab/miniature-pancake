//! Persistent sparse table keyed by a dense `u32` index.
//!
//! A 64-way radix tree whose leaves hold 128 slots plus a presence bitmask.
//! Cloning is one `Arc` bump; a write copies only the root-to-leaf path
//! (one leaf of 128 values plus one 64-pointer branch per level), so a
//! snapshot that is later diverged from pays only for the pages it touches.
//! Absent subtrees cost nothing.

use std::ops::ControlFlow;
use std::sync::Arc;

const LEAF_BITS: u32 = 7;
const LEAF: usize = 1 << LEAF_BITS;
const BR_BITS: u32 = 6;
const BR: usize = 1 << BR_BITS;

#[derive(Clone)]
struct Leaf<T: Copy> {
    present: [u64; 2],
    vals: Box<[T]>,
}

impl<T: Copy + Default> Leaf<T> {
    fn new() -> Self {
        Leaf { present: [0; 2], vals: vec![T::default(); LEAF].into_boxed_slice() }
    }
}

#[derive(Clone)]
enum Node<T: Copy> {
    Leaf(Leaf<T>),
    Branch(Box<[Option<Arc<Node<T>>>]>),
}

fn new_branch<T: Copy>() -> Box<[Option<Arc<Node<T>>>]> {
    vec![None; BR].into_boxed_slice()
}

pub struct Table<T: Copy + Default> {
    root: Option<Arc<Node<T>>>,
    levels: u32,
    count: usize,
}

impl<T: Copy + Default> Clone for Table<T> {
    fn clone(&self) -> Self {
        Table { root: self.root.clone(), levels: self.levels, count: self.count }
    }
}

impl<T: Copy + Default> Default for Table<T> {
    fn default() -> Self {
        Self::new()
    }
}

fn capacity(levels: u32) -> usize {
    LEAF << (BR_BITS * levels)
}

impl<T: Copy + Default> Table<T> {
    pub fn new() -> Self {
        Table { root: None, levels: 0, count: 0 }
    }

    /// Number of present entries.
    pub fn len(&self) -> usize {
        self.count
    }

    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    pub fn get(&self, i: u32) -> Option<&T> {
        let i = i as usize;
        if i >= capacity(self.levels) {
            return None;
        }
        let mut node = self.root.as_deref()?;
        let mut level = self.levels;
        loop {
            match node {
                Node::Leaf(l) => {
                    let off = i & (LEAF - 1);
                    return if (l.present[off >> 6] >> (off & 63)) & 1 == 1 { Some(&l.vals[off]) } else { None };
                }
                Node::Branch(ch) => {
                    let shift = LEAF_BITS + BR_BITS * (level - 1);
                    node = ch[(i >> shift) & (BR - 1)].as_deref()?;
                    level -= 1;
                }
            }
        }
    }

    pub fn contains(&self, i: u32) -> bool {
        self.get(i).is_some()
    }

    /// Insert or overwrite. Returns true when the slot was previously absent.
    pub fn set(&mut self, i: u32, v: T) -> bool {
        let iu = i as usize;
        while iu >= capacity(self.levels) {
            if let Some(old) = self.root.take() {
                let mut br = new_branch();
                br[0] = Some(old);
                self.root = Some(Arc::new(Node::Branch(br)));
            }
            self.levels += 1;
        }
        let added = set_rec(&mut self.root, self.levels, iu, v);
        if added {
            self.count += 1;
        }
        added
    }

    /// Remove an entry. Returns true when it was present.
    pub fn remove(&mut self, i: u32) -> bool {
        if !self.contains(i) {
            return false;
        }
        remove_rec(self.root.as_mut().unwrap(), self.levels, i as usize);
        self.count -= 1;
        true
    }

    /// Visit present entries in index order; stop early on `Break`.
    pub fn try_scan<B>(&self, mut f: impl FnMut(u32, &T) -> ControlFlow<B>) -> ControlFlow<B> {
        match &self.root {
            None => ControlFlow::Continue(()),
            Some(n) => scan_rec(n, self.levels, 0, &mut f),
        }
    }

    pub fn scan(&self, mut f: impl FnMut(u32, &T)) {
        let _ = self.try_scan::<()>(|i, v| {
            f(i, v);
            ControlFlow::Continue(())
        });
    }

    /// Approximate heap bytes (nodes only; excludes allocator overhead).
    pub fn heap_bytes(&self) -> usize {
        fn rec<T: Copy>(n: &Node<T>) -> usize {
            match n {
                Node::Leaf(_) => std::mem::size_of::<Node<T>>() + LEAF * std::mem::size_of::<T>() + 16,
                Node::Branch(ch) => {
                    std::mem::size_of::<Node<T>>()
                        + BR * std::mem::size_of::<Option<Arc<Node<T>>>>()
                        + ch.iter().flatten().map(|c| rec(c)).sum::<usize>()
                        + 16
                }
            }
        }
        self.root.as_deref().map(rec).unwrap_or(0)
    }

    /// True when both tables share the same root allocation (diagnostics).
    pub fn ptr_eq(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (Some(a), Some(b)) => Arc::ptr_eq(a, b),
            (None, None) => true,
            _ => false,
        }
    }
}

fn set_rec<T: Copy + Default>(slot: &mut Option<Arc<Node<T>>>, level: u32, i: usize, v: T) -> bool {
    if slot.is_none() {
        *slot = Some(Arc::new(if level == 0 { Node::Leaf(Leaf::new()) } else { Node::Branch(new_branch()) }));
    }
    match Arc::make_mut(slot.as_mut().unwrap()) {
        Node::Leaf(l) => {
            let off = i & (LEAF - 1);
            let (w, b) = (off >> 6, off & 63);
            let was = (l.present[w] >> b) & 1 == 1;
            l.present[w] |= 1 << b;
            l.vals[off] = v;
            !was
        }
        Node::Branch(ch) => {
            let shift = LEAF_BITS + BR_BITS * (level - 1);
            set_rec(&mut ch[(i >> shift) & (BR - 1)], level - 1, i, v)
        }
    }
}

fn remove_rec<T: Copy + Default>(node: &mut Arc<Node<T>>, level: u32, i: usize) {
    match Arc::make_mut(node) {
        Node::Leaf(l) => {
            let off = i & (LEAF - 1);
            l.present[off >> 6] &= !(1u64 << (off & 63));
            l.vals[off] = T::default();
        }
        Node::Branch(ch) => {
            let shift = LEAF_BITS + BR_BITS * (level - 1);
            remove_rec(ch[(i >> shift) & (BR - 1)].as_mut().unwrap(), level - 1, i);
        }
    }
}

fn scan_rec<T: Copy, B>(
    n: &Node<T>,
    level: u32,
    base: usize,
    f: &mut impl FnMut(u32, &T) -> ControlFlow<B>,
) -> ControlFlow<B> {
    match n {
        Node::Leaf(l) => {
            for w in 0..2 {
                let mut bits = l.present[w];
                while bits != 0 {
                    let tz = bits.trailing_zeros() as usize;
                    bits &= bits - 1;
                    let off = w * 64 + tz;
                    f((base + off) as u32, &l.vals[off])?;
                }
            }
            ControlFlow::Continue(())
        }
        Node::Branch(ch) => {
            let shift = LEAF_BITS + BR_BITS * (level - 1);
            for (k, c) in ch.iter().enumerate() {
                if let Some(c) = c {
                    scan_rec(c, level - 1, base + (k << shift), f)?;
                }
            }
            ControlFlow::Continue(())
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn set_get_remove_scan() {
        let mut t: Table<u32> = Table::new();
        for i in [0u32, 1, 127, 128, 9_000, 1_000_000, 70_000_000] {
            assert!(t.set(i, i + 1));
        }
        assert!(!t.set(1, 99));
        assert_eq!(t.get(1), Some(&99));
        assert_eq!(t.get(2), None);
        assert_eq!(t.len(), 7);
        assert!(t.remove(1_000_000));
        assert!(!t.remove(1_000_000));
        let mut seen = vec![];
        t.scan(|i, _| seen.push(i));
        assert_eq!(seen, vec![0, 1, 127, 128, 9_000, 70_000_000]);
    }

    #[test]
    fn clone_is_isolated() {
        let mut a: Table<u64> = Table::new();
        for i in 0..1000 {
            a.set(i, i as u64);
        }
        let b = a.clone();
        a.set(500, 7);
        a.remove(3);
        assert_eq!(b.get(500), Some(&500));
        assert_eq!(b.get(3), Some(&3));
        assert_eq!(a.get(500), Some(&7));
        assert_eq!(a.get(3), None);
    }

    #[test]
    fn unit_values() {
        let mut t: Table<()> = Table::new();
        t.set(5, ());
        assert!(t.contains(5) && !t.contains(6));
    }
}
