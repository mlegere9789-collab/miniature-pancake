//! A concurrent, append-only, causally ordered log shared by many appender threads.
//!
//! This is the sequencer a sync server (or any process with several writer threads) puts in front
//! of the single-writer [`Workspace`](crate::Workspace)s: appenders race to add entries, the
//! sequencer fixes one total order that extends the causal order (an entry is only accepted once
//! all its parents are present), duplicates are rejected idempotently, and a named head can be
//! advanced with compare-and-swap so two writers that extended the same head cannot both win.
//!
//! The synchronization is deliberately small (one mutex plus one atomic watermark) so that it can
//! be checked exhaustively with `loom`: build with `RUSTFLAGS="--cfg loom"`.

use std::collections::{BTreeMap, HashMap};
use std::fmt::Debug;
use std::hash::Hash;

#[cfg(loom)]
pub use loom::sync::Arc;
#[cfg(loom)]
use loom::sync::{
    atomic::{AtomicUsize, Ordering},
    Mutex, MutexGuard,
};
#[cfg(not(loom))]
pub use std::sync::Arc;
#[cfg(not(loom))]
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Mutex, MutexGuard,
};

/// Something that can live in the shared log.
pub trait Entry {
    type Id: Copy + Eq + Hash + Debug;
    fn id(&self) -> Self::Id;
    fn parents(&self) -> &[Self::Id];
}

#[derive(Debug, PartialEq, Eq)]
pub enum AppendError<Id> {
    /// An entry with this id is already in the log (at this position).
    Duplicate(usize),
    /// A parent has not been appended yet.
    MissingParent(Id),
}

struct Inner<E: Entry> {
    entries: Vec<Arc<E>>,
    index: HashMap<E::Id, usize>,
    heads: BTreeMap<String, E::Id>,
}

pub struct SharedLog<E: Entry> {
    inner: Mutex<Inner<E>>,
    /// Number of entries visible to readers; published after the entry is in place.
    committed: AtomicUsize,
}

impl<E: Entry> Default for SharedLog<E> {
    fn default() -> Self {
        Self::new()
    }
}

impl<E: Entry> SharedLog<E> {
    pub fn new() -> Self {
        SharedLog {
            inner: Mutex::new(Inner { entries: Vec::new(), index: HashMap::new(), heads: BTreeMap::new() }),
            committed: AtomicUsize::new(0),
        }
    }

    fn lock(&self) -> MutexGuard<'_, Inner<E>> {
        #[cfg(loom)]
        {
            self.inner.lock().expect("loom mutex")
        }
        #[cfg(not(loom))]
        {
            self.inner.lock().unwrap_or_else(std::sync::PoisonError::into_inner)
        }
    }

    /// Entries visible to every thread. Never decreases.
    pub fn len(&self) -> usize {
        self.committed.load(Ordering::Acquire)
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Append `e` after checking that all its parents are already present.
    pub fn append(&self, e: E) -> Result<usize, AppendError<E::Id>> {
        let mut g = self.lock();
        if let Some(&pos) = g.index.get(&e.id()) {
            return Err(AppendError::Duplicate(pos));
        }
        for p in e.parents() {
            if !g.index.contains_key(p) {
                return Err(AppendError::MissingParent(*p));
            }
        }
        let pos = g.entries.len();
        g.index.insert(e.id(), pos);
        g.entries.push(Arc::new(e));
        self.committed.store(pos + 1, Ordering::Release);
        Ok(pos)
    }

    pub fn get(&self, i: usize) -> Option<Arc<E>> {
        let g = self.lock();
        g.entries.get(i).cloned()
    }

    pub fn position(&self, id: &E::Id) -> Option<usize> {
        self.lock().index.get(id).copied()
    }

    /// A snapshot of entries `from..len()`.
    pub fn since(&self, from: usize) -> Vec<Arc<E>> {
        let g = self.lock();
        g.entries.iter().skip(from).cloned().collect()
    }

    pub fn head(&self, name: &str) -> Option<E::Id> {
        self.lock().heads.get(name).copied()
    }

    /// Advance the named head from `expected` to `new` only if it still is `expected`.
    /// On failure returns the head's actual value, so the caller can rebase or merge.
    pub fn cas_head(&self, name: &str, expected: Option<E::Id>, new: E::Id) -> Result<(), Option<E::Id>> {
        let mut g = self.lock();
        let actual = g.heads.get(name).copied();
        if actual != expected || !g.index.contains_key(&new) {
            return Err(actual);
        }
        g.heads.insert(name.to_string(), new);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Debug)]
    struct E(u32, Vec<u32>);

    impl Entry for E {
        type Id = u32;
        fn id(&self) -> u32 {
            self.0
        }
        fn parents(&self) -> &[u32] {
            &self.1
        }
    }

    #[test]
    fn causal_order_duplicates_and_cas() {
        let log = SharedLog::new();
        assert_eq!(log.append(E(1, vec![])), Ok(0));
        assert_eq!(log.append(E(2, vec![1])), Ok(1));
        assert_eq!(log.append(E(2, vec![1])), Err(AppendError::Duplicate(1)));
        assert_eq!(log.append(E(9, vec![7])), Err(AppendError::MissingParent(7)));
        assert_eq!(log.len(), 2);
        assert_eq!(log.cas_head("main", None, 1), Ok(()));
        assert_eq!(log.cas_head("main", None, 2), Err(Some(1)));
        assert_eq!(log.cas_head("main", Some(1), 2), Ok(()));
        assert_eq!(log.cas_head("main", Some(2), 77), Err(Some(2)), "unknown entries cannot become the head");
        assert_eq!(log.since(1).len(), 1);
        assert_eq!(log.position(&2), Some(1));
    }
}
