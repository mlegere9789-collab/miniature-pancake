//! The content-addressed blob store.
//!
//! Blobs are immutable byte strings keyed by `BLAKE3(domain, bytes)`. Object snapshots, heavy
//! geometry and index nodes all live here, so identical content is stored once however many
//! operations, branches or checkpoints refer to it (structural sharing). In file mode the bytes
//! live inside the write-ahead log and only an offset is kept in memory.

use crate::crdt::Obj;
use crate::error::{Error, Result};
use crate::hamt::{NodeH, NodeLoader};
use crate::ids::{Hash, HashMap, DOM_BLOB};
use crate::wal::read_at;
use std::fs::File;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};

pub const KIND_RAW: u8 = 0;
pub const KIND_OBJ: u8 = 1;

const NODE_CACHE_MAX: usize = 100_000;

/// Prefix `payload` with its kind byte; the result is what gets hashed and stored.
pub fn envelope(kind: u8, payload: &[u8]) -> Vec<u8> {
    let mut v = Vec::with_capacity(payload.len() + 1);
    v.push(kind);
    v.extend_from_slice(payload);
    v
}

pub fn blob_hash(envelope: &[u8]) -> Hash {
    Hash::digest(DOM_BLOB, envelope)
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

#[derive(Clone)]
enum Loc {
    Mem(Arc<[u8]>),
    File { off: u64, len: u32 },
}

pub struct Store {
    index: Mutex<HashMap<Hash, Loc>>,
    file: Option<Arc<File>>,
    nodes: Mutex<HashMap<Hash, Arc<NodeH>>>,
    bytes: AtomicU64,
}

impl Store {
    /// In-memory store (`file = None`) or one that reads blobs back from the log file.
    pub fn new(file: Option<Arc<File>>) -> Store {
        Store {
            index: Mutex::new(HashMap::default()),
            file,
            nodes: Mutex::new(HashMap::default()),
            bytes: AtomicU64::new(0),
        }
    }

    pub fn contains(&self, h: &Hash) -> bool {
        lock(&self.index).contains_key(h)
    }

    pub fn len(&self) -> usize {
        lock(&self.index).len()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Total bytes of distinct blobs registered.
    pub fn stored_bytes(&self) -> u64 {
        self.bytes.load(Ordering::Relaxed)
    }

    /// Register bytes held in memory. Returns false if the hash was already present.
    pub fn insert_mem(&self, h: Hash, bytes: Arc<[u8]>) -> bool {
        let n = bytes.len() as u64;
        let mut ix = lock(&self.index);
        if ix.contains_key(&h) {
            return false;
        }
        ix.insert(h, Loc::Mem(bytes));
        self.bytes.fetch_add(n, Ordering::Relaxed);
        true
    }

    /// Register a blob that was just written (or found) at `off` in the log file.
    pub fn note_file(&self, h: Hash, off: u64, len: u32) -> bool {
        let mut ix = lock(&self.index);
        if ix.contains_key(&h) {
            return false;
        }
        ix.insert(h, Loc::File { off, len });
        self.bytes.fetch_add(u64::from(len), Ordering::Relaxed);
        true
    }

    pub fn get(&self, h: &Hash) -> Result<Arc<[u8]>> {
        let loc = lock(&self.index).get(h).cloned().ok_or(Error::MissingBlob(*h))?;
        match loc {
            Loc::Mem(b) => Ok(b),
            Loc::File { off, len } => {
                let f = self.file.as_ref().ok_or(Error::MissingBlob(*h))?;
                let mut buf = vec![0u8; len as usize];
                read_at(f, &mut buf, off)?;
                Ok(Arc::from(buf))
            }
        }
    }

    /// Fetch and decode an object snapshot.
    pub fn get_obj(&self, h: &Hash) -> Result<Obj> {
        let b = self.get(h)?;
        match b.split_first() {
            Some((&KIND_OBJ, rest)) => Ok(Obj::from_bytes(rest)?),
            _ => Err(Error::Corrupt(format!("blob {h} is not an object snapshot"))),
        }
    }

    /// Fetch a raw (geometry) blob's payload.
    pub fn get_raw(&self, h: &Hash) -> Result<Vec<u8>> {
        let b = self.get(h)?;
        match b.split_first() {
            Some((&KIND_RAW, rest)) => Ok(rest.to_vec()),
            _ => Err(Error::Corrupt(format!("blob {h} is not a raw blob"))),
        }
    }

    /// Verify that every registered blob hashes to its key (full integrity pass).
    pub fn verify_all(&self) -> Result<usize> {
        let keys: Vec<Hash> = lock(&self.index).keys().copied().collect();
        let mut checked = 0;
        for h in keys {
            let b = self.get(&h)?;
            // node blobs use a different domain; accept either
            if blob_hash(&b) != h && Hash::digest(crate::ids::DOM_NODE, &b) != h {
                return Err(Error::Corrupt(format!("blob {h} fails its hash")));
            }
            checked += 1;
        }
        Ok(checked)
    }
}

impl NodeLoader for Store {
    fn load_node(&self, hash: &Hash) -> Result<Arc<NodeH>> {
        if let Some(n) = lock(&self.nodes).get(hash) {
            return Ok(n.clone());
        }
        let bytes = self.get(hash)?;
        let n = NodeH::from_bytes(*hash, &bytes)?;
        let mut c = lock(&self.nodes);
        if c.len() >= NODE_CACHE_MAX {
            c.clear();
        }
        c.insert(*hash, n.clone());
        Ok(n)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn dedupes_identical_content() {
        let s = Store::new(None);
        let e = envelope(KIND_RAW, b"geometry");
        let h = blob_hash(&e);
        assert!(s.insert_mem(h, Arc::from(e.clone())));
        assert!(!s.insert_mem(h, Arc::from(e)));
        assert_eq!(s.len(), 1);
        assert_eq!(s.get_raw(&h).unwrap(), b"geometry");
        assert!(s.get_obj(&h).is_err());
        assert!(s.get(&Hash([1; 32])).is_err());
        assert_eq!(s.verify_all().unwrap(), 1);
    }
}
