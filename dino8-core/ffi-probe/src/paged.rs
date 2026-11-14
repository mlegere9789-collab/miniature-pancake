//! A growable sequence stored in fixed-size pages.
//!
//! Growing a `Vec` by doubling copies every element once per doubling, so one push in a million
//! pays a multi-millisecond copy (measured: 0.5 ms at 131072 elements, 2 ms at 524288). Pages are
//! allocated one at a time and never moved, so the worst-case push costs one page allocation.

/// Elements per page (a power of two).
pub const PAGE_LEN: usize = if cfg!(loom) { 64 } else { 8192 };
const _: () = assert!(PAGE_LEN.is_power_of_two());

/// Paged stack of `Copy` values with random read access.
pub struct Paged<T: Copy + Default> {
    pages: Vec<Box<[T]>>,
    len: usize,
}

impl<T: Copy + Default> Default for Paged<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: Copy + Default> Paged<T> {
    /// An empty sequence; allocates nothing.
    pub const fn new() -> Self {
        Paged {
            pages: Vec::new(),
            len: 0,
        }
    }

    /// Number of elements.
    pub fn len(&self) -> usize {
        self.len
    }

    /// True when there are no elements.
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    /// Appends `value`.
    pub fn push(&mut self, value: T) {
        if self.len == self.pages.len() * PAGE_LEN {
            self.pages
                .push(vec![T::default(); PAGE_LEN].into_boxed_slice());
        }
        self.pages[self.len / PAGE_LEN][self.len % PAGE_LEN] = value;
        self.len += 1;
    }

    /// Removes and returns the last element. The page stays allocated for reuse.
    pub fn pop(&mut self) -> Option<T> {
        let last = self.last()?;
        self.len -= 1;
        Some(last)
    }

    /// The element at `index`, if any.
    pub fn get(&self, index: usize) -> Option<T> {
        if index < self.len {
            Some(self.pages[index / PAGE_LEN][index % PAGE_LEN])
        } else {
            None
        }
    }

    /// The last element, if any.
    pub fn last(&self) -> Option<T> {
        self.len.checked_sub(1).and_then(|i| self.get(i))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn push_get_pop_across_page_boundaries() {
        let mut p: Paged<u64> = Paged::new();
        assert!(p.is_empty() && p.last().is_none() && p.pop().is_none());
        let n = PAGE_LEN * 3 + 5;
        for i in 0..n {
            p.push(i as u64 * 7);
            assert_eq!(p.len(), i + 1);
            assert_eq!(p.last(), Some(i as u64 * 7));
        }
        for i in [
            0,
            1,
            PAGE_LEN - 1,
            PAGE_LEN,
            PAGE_LEN + 1,
            2 * PAGE_LEN,
            n - 1,
        ] {
            assert_eq!(p.get(i), Some(i as u64 * 7));
        }
        assert_eq!(p.get(n), None);
        for i in (0..n).rev() {
            assert_eq!(p.pop(), Some(i as u64 * 7));
        }
        assert!(p.is_empty());
        p.push(99);
        assert_eq!(p.get(0), Some(99), "pages are reused after popping");
    }

    #[test]
    fn existing_pages_never_move_when_the_sequence_grows() {
        let mut p: Paged<u32> = Paged::new();
        p.push(1);
        let first_page = p.pages[0].as_ptr();
        for i in 0..PAGE_LEN * 20 {
            p.push(i as u32);
        }
        assert_eq!(p.pages[0].as_ptr(), first_page);
    }
}
