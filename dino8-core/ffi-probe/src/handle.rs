//! Generation-checked handle table. C code holds 64-bit ids; Rust holds the objects.
//!
//! An id is `(generation << 32) | (slot + 1)`. Zero is never valid. Removing an entry bumps the
//! slot generation, so a stale copy of an id is rejected instead of reaching a reused slot. The
//! table stores `Arc<T>`, so a call that is in flight when another thread closes the handle keeps
//! its object alive until it returns.

use std::sync::Arc;

struct Slot<T> {
    generation: u32,
    value: Option<Arc<T>>,
}

/// A table that maps opaque 64-bit ids to reference-counted objects.
pub struct HandleTable<T> {
    slots: Vec<Slot<T>>,
    free: Vec<u32>,
}

impl<T> Default for HandleTable<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> HandleTable<T> {
    /// An empty table (usable in a `static`).
    pub const fn new() -> Self {
        HandleTable {
            slots: Vec::new(),
            free: Vec::new(),
        }
    }

    /// Stores `value` and returns its id.
    pub fn insert(&mut self, value: Arc<T>) -> u64 {
        let idx = match self.free.pop() {
            Some(i) => i,
            None => {
                self.slots.push(Slot {
                    generation: 1,
                    value: None,
                });
                (self.slots.len() - 1) as u32
            }
        };
        let slot = &mut self.slots[idx as usize];
        slot.value = Some(value);
        (u64::from(slot.generation) << 32) | (u64::from(idx) + 1)
    }

    fn slot_index(&self, id: u64) -> Option<usize> {
        let low = (id & 0xFFFF_FFFF) as u32;
        let generation = (id >> 32) as u32;
        if low == 0 {
            return None;
        }
        let idx = (low - 1) as usize;
        let slot = self.slots.get(idx)?;
        if slot.generation == generation && slot.value.is_some() {
            Some(idx)
        } else {
            None
        }
    }

    /// Returns a new reference to the object, or `None` for a zero, forged or stale id.
    pub fn get(&self, id: u64) -> Option<Arc<T>> {
        let idx = self.slot_index(id)?;
        self.slots[idx].value.clone()
    }

    /// Removes the object and invalidates every copy of `id`.
    pub fn remove(&mut self, id: u64) -> Option<Arc<T>> {
        let idx = self.slot_index(id)?;
        let slot = &mut self.slots[idx];
        let value = slot.value.take();
        slot.generation = match slot.generation.wrapping_add(1) {
            0 => 1,
            g => g,
        };
        self.free.push(idx as u32);
        value
    }

    /// Number of live entries.
    pub fn len(&self) -> usize {
        self.slots.len() - self.free.len()
    }

    /// True when no entry is live.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_get_remove() {
        let mut t = HandleTable::new();
        let a = t.insert(Arc::new(10u32));
        let b = t.insert(Arc::new(20u32));
        assert_ne!(a, b);
        assert_eq!(*t.get(a).unwrap(), 10);
        assert_eq!(*t.get(b).unwrap(), 20);
        assert_eq!(t.len(), 2);
        assert_eq!(*t.remove(a).unwrap(), 10);
        assert!(t.get(a).is_none());
        assert_eq!(t.len(), 1);
    }

    #[test]
    fn zero_and_forged_ids_are_rejected() {
        let mut t = HandleTable::new();
        let a = t.insert(Arc::new(1u8));
        assert!(t.get(0).is_none());
        assert!(t.get(u64::MAX).is_none());
        assert!(t.get(a ^ (1 << 40)).is_none(), "wrong generation");
        assert!(
            t.get((a & 0xFFFF_FFFF) + 5).is_none(),
            "wrong slot, generation 0"
        );
        assert!(t.remove(0).is_none());
    }

    #[test]
    fn a_reused_slot_does_not_revive_the_old_id() {
        let mut t = HandleTable::new();
        let a = t.insert(Arc::new(1u8));
        assert!(t.remove(a).is_some());
        assert!(t.remove(a).is_none(), "double remove");
        let b = t.insert(Arc::new(2u8));
        assert_eq!(a & 0xFFFF_FFFF, b & 0xFFFF_FFFF, "slot reused");
        assert_ne!(a, b, "generation differs");
        assert!(t.get(a).is_none());
        assert_eq!(*t.get(b).unwrap(), 2);
    }

    #[test]
    fn a_closed_object_stays_alive_for_in_flight_references() {
        let mut t = HandleTable::new();
        let id = t.insert(Arc::new(String::from("alive")));
        let in_flight = t.get(id).unwrap();
        drop(t.remove(id));
        assert_eq!(in_flight.as_str(), "alive");
        assert_eq!(Arc::strong_count(&in_flight), 1);
    }
}
