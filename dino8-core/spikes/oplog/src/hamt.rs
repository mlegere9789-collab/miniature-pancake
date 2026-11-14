//! A persistent, content-addressed, history-independent hash-array-mapped trie from
//! [`ObjectId`] to the hash of the object's snapshot.
//!
//! * Persistent: every update copies only the path from the root to the touched leaf; all other
//!   nodes are shared by `Arc`, so a document version costs O(log n) new nodes.
//! * Content-addressed: every node has a BLAKE3 hash over its encoding, which includes its
//!   children's hashes. The root hash is therefore a Merkle commitment to the whole document and
//!   serves as the **state hash** used by replay verification.
//! * History-independent: the shape depends only on the set of keys (a node is a leaf exactly when
//!   its subtree holds at most [`LEAF_MAX`] entries), so two replicas holding the same objects have
//!   the same root hash whatever order the edits arrived in.
//! * Lazy: a child is either in memory or a bare hash (`Child::Ref`) that is loaded on demand, so a
//!   structural diff or a time-travel step only touches the nodes on the paths that differ.

use crate::error::{Error, Result};
use crate::ids::{Hash, ObjectId, DOM_EMPTY, DOM_NODE};
use crate::value::{put_varint, Reader};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, OnceLock};

/// A leaf holds at most this many entries; one more and it splits into a branch.
pub const LEAF_MAX: usize = 6;
/// A key hash has 16 nibbles; below that depth a leaf may grow without bound.
const MAX_DEPTH: usize = 16;

pub type Entry = (ObjectId, Hash);

#[derive(Debug)]
pub enum Node {
    Leaf(Vec<Entry>),
    Branch { bitmap: u16, kids: Vec<Child> },
}

#[derive(Debug, Clone)]
pub enum Child {
    Mem(Arc<NodeH>),
    /// Not loaded: only the hash and the subtree size are known.
    Ref { hash: Hash, count: u32 },
}

impl Child {
    pub fn hash(&self) -> Hash {
        match self {
            Child::Mem(n) => n.hash,
            Child::Ref { hash, .. } => *hash,
        }
    }

    pub fn count(&self) -> u32 {
        match self {
            Child::Mem(n) => n.count,
            Child::Ref { count, .. } => *count,
        }
    }
}

/// A node together with its cached hash and size.
#[derive(Debug)]
pub struct NodeH {
    pub hash: Hash,
    pub count: u32,
    pub node: Node,
    stored: AtomicBool,
}

/// Source of nodes that are only referenced by hash.
pub trait NodeLoader {
    fn load_node(&self, hash: &Hash) -> Result<Arc<NodeH>>;
}

/// A loader for trees that live entirely in memory.
pub struct NoLoader;

impl NodeLoader for NoLoader {
    fn load_node(&self, hash: &Hash) -> Result<Arc<NodeH>> {
        Err(Error::MissingBlob(*hash))
    }
}

pub type Root = Option<Arc<NodeH>>;

/// The state hash of an empty document.
pub fn empty_root_hash() -> Hash {
    static H: OnceLock<Hash> = OnceLock::new();
    *H.get_or_init(|| Hash::digest(DOM_EMPTY, b""))
}

pub fn root_hash(r: &Root) -> Hash {
    r.as_ref().map_or_else(empty_root_hash, |n| n.hash)
}

pub fn root_count(r: &Root) -> usize {
    r.as_ref().map_or(0, |n| n.count as usize)
}

fn encode_node(node: &Node) -> Vec<u8> {
    match node {
        Node::Leaf(es) => {
            let mut v = Vec::with_capacity(2 + es.len() * 48);
            v.push(0);
            put_varint(&mut v, es.len() as u64);
            for (k, h) in es {
                v.extend_from_slice(&k.0);
                v.extend_from_slice(&h.0);
            }
            v
        }
        Node::Branch { bitmap, kids } => {
            let mut v = Vec::with_capacity(3 + kids.len() * 36);
            v.push(1);
            v.extend_from_slice(&bitmap.to_le_bytes());
            for k in kids {
                v.extend_from_slice(&k.hash().0);
                put_varint(&mut v, u64::from(k.count()));
            }
            v
        }
    }
}

impl NodeH {
    fn new(node: Node) -> Arc<NodeH> {
        let count = match &node {
            Node::Leaf(es) => es.len() as u32,
            Node::Branch { kids, .. } => kids.iter().map(Child::count).sum(),
        };
        let hash = Hash::digest(DOM_NODE, &encode_node(&node));
        Arc::new(NodeH { hash, count, node, stored: AtomicBool::new(false) })
    }

    /// Canonical bytes of this node (what the checkpoint stores under `hash`).
    pub fn to_bytes(&self) -> Vec<u8> {
        encode_node(&self.node)
    }

    /// Decode a stored node, verifying that the bytes hash to `hash`.
    pub fn from_bytes(hash: Hash, bytes: &[u8]) -> Result<Arc<NodeH>> {
        if Hash::digest(DOM_NODE, bytes) != hash {
            return Err(Error::Corrupt(format!("node {hash} fails its hash")));
        }
        let mut r = Reader::new(bytes);
        let node = match r.u8()? {
            0 => {
                let n = r.varint()? as usize;
                if n > bytes.len() / 48 + 1 {
                    return Err(Error::Corrupt("leaf too large".into()));
                }
                let mut es = Vec::with_capacity(n);
                for _ in 0..n {
                    let k = ObjectId(r.array::<16>()?);
                    let h = Hash(r.array::<32>()?);
                    es.push((k, h));
                }
                if es.windows(2).any(|w| w[0].0 >= w[1].0) {
                    return Err(Error::Corrupt("leaf entries not sorted".into()));
                }
                Node::Leaf(es)
            }
            1 => {
                let bitmap = u16::from_le_bytes(r.array::<2>()?);
                let mut kids = Vec::with_capacity(bitmap.count_ones() as usize);
                for _ in 0..bitmap.count_ones() {
                    let hash = Hash(r.array::<32>()?);
                    let count = u32::try_from(r.varint()?).map_err(|_| Error::Corrupt("count".into()))?;
                    kids.push(Child::Ref { hash, count });
                }
                Node::Branch { bitmap, kids }
            }
            t => return Err(Error::Corrupt(format!("unknown node tag {t}"))),
        };
        if !r.is_empty() {
            return Err(Error::Corrupt("trailing bytes in node".into()));
        }
        let count = match &node {
            Node::Leaf(es) => es.len() as u32,
            Node::Branch { kids, .. } => kids.iter().map(Child::count).sum(),
        };
        Ok(Arc::new(NodeH { hash, count, node, stored: AtomicBool::new(true) }))
    }

    pub fn is_stored(&self) -> bool {
        self.stored.load(Ordering::Acquire)
    }
}

fn nibble(kh: u64, depth: usize) -> usize {
    ((kh >> (60 - 4 * depth)) & 0xf) as usize
}

fn resolve(c: &Child, ld: &dyn NodeLoader) -> Result<Arc<NodeH>> {
    match c {
        Child::Mem(n) => Ok(n.clone()),
        Child::Ref { hash, .. } => ld.load_node(hash),
    }
}

/// Load a stored root by hash (`None` for the empty tree).
pub fn load_root(hash: &Hash, ld: &dyn NodeLoader) -> Result<Root> {
    if *hash == empty_root_hash() {
        return Ok(None);
    }
    ld.load_node(hash).map(Some)
}

pub fn get(root: &Root, key: &ObjectId, ld: &dyn NodeLoader) -> Result<Option<Hash>> {
    let kh = key.key_hash();
    let Some(mut cur) = root.clone() else { return Ok(None) };
    let mut depth = 0;
    loop {
        let next = match &cur.node {
            Node::Leaf(es) => {
                return Ok(es.binary_search_by(|(k, _)| k.cmp(key)).ok().map(|i| es[i].1));
            }
            Node::Branch { bitmap, kids } => {
                let bit = 1u16 << nibble(kh, depth);
                if bitmap & bit == 0 {
                    return Ok(None);
                }
                let idx = (bitmap & (bit - 1)).count_ones() as usize;
                resolve(&kids[idx], ld)?
            }
        };
        cur = next;
        depth += 1;
    }
}

fn build(es: Vec<Entry>, depth: usize) -> Arc<NodeH> {
    debug_assert!(es.len() > LEAF_MAX && depth < MAX_DEPTH);
    let mut groups: [Vec<Entry>; 16] = Default::default();
    for e in es {
        groups[nibble(e.0.key_hash(), depth)].push(e);
    }
    let mut bitmap = 0u16;
    let mut kids = Vec::new();
    for (nib, g) in groups.into_iter().enumerate() {
        if g.is_empty() {
            continue;
        }
        bitmap |= 1 << nib;
        let child = if g.len() > LEAF_MAX && depth + 1 < MAX_DEPTH {
            build(g, depth + 1)
        } else {
            NodeH::new(Node::Leaf(g))
        };
        kids.push(Child::Mem(child));
    }
    NodeH::new(Node::Branch { bitmap, kids })
}

fn insert_rec(
    node: Option<&Arc<NodeH>>,
    depth: usize,
    key: ObjectId,
    kh: u64,
    val: Hash,
    ld: &dyn NodeLoader,
) -> Result<Arc<NodeH>> {
    let Some(n) = node else {
        return Ok(NodeH::new(Node::Leaf(vec![(key, val)])));
    };
    match &n.node {
        Node::Leaf(es) => {
            let mut es = es.clone();
            match es.binary_search_by(|(k, _)| k.cmp(&key)) {
                Ok(i) => {
                    if es[i].1 == val {
                        return Ok(n.clone());
                    }
                    es[i].1 = val;
                }
                Err(i) => es.insert(i, (key, val)),
            }
            if es.len() > LEAF_MAX && depth < MAX_DEPTH {
                Ok(build(es, depth))
            } else {
                Ok(NodeH::new(Node::Leaf(es)))
            }
        }
        Node::Branch { bitmap, kids } => {
            let bit = 1u16 << nibble(kh, depth);
            let idx = (bitmap & (bit - 1)).count_ones() as usize;
            let mut kids = kids.clone();
            let mut bitmap = *bitmap;
            if bitmap & bit != 0 {
                let child = resolve(&kids[idx], ld)?;
                let new_child = insert_rec(Some(&child), depth + 1, key, kh, val, ld)?;
                if Arc::ptr_eq(&child, &new_child) {
                    return Ok(n.clone());
                }
                kids[idx] = Child::Mem(new_child);
            } else {
                kids.insert(idx, Child::Mem(NodeH::new(Node::Leaf(vec![(key, val)]))));
                bitmap |= bit;
            }
            Ok(NodeH::new(Node::Branch { bitmap, kids }))
        }
    }
}

/// Insert or replace `key`.
pub fn insert(root: &Root, key: ObjectId, val: Hash, ld: &dyn NodeLoader) -> Result<Root> {
    let kh = key.key_hash();
    insert_rec(root.as_ref(), 0, key, kh, val, ld).map(Some)
}

pub fn collect_entries(n: &Arc<NodeH>, ld: &dyn NodeLoader, out: &mut Vec<Entry>) -> Result<()> {
    match &n.node {
        Node::Leaf(es) => out.extend(es.iter().copied()),
        Node::Branch { kids, .. } => {
            for k in kids {
                collect_entries(&resolve(k, ld)?, ld, out)?;
            }
        }
    }
    Ok(())
}

/// Every entry, sorted by key.
pub fn entries(root: &Root, ld: &dyn NodeLoader) -> Result<Vec<Entry>> {
    let mut v = Vec::with_capacity(root_count(root));
    if let Some(n) = root {
        collect_entries(n, ld, &mut v)?;
    }
    v.sort_unstable_by_key(|e| e.0);
    Ok(v)
}

/// `Ok(None)`: key absent, nothing changed. `Ok(Some(None))`: the subtree became empty.
fn remove_rec(
    n: &Arc<NodeH>,
    depth: usize,
    key: &ObjectId,
    kh: u64,
    ld: &dyn NodeLoader,
) -> Result<Option<Option<Arc<NodeH>>>> {
    match &n.node {
        Node::Leaf(es) => match es.binary_search_by(|(k, _)| k.cmp(key)) {
            Err(_) => Ok(None),
            Ok(i) => {
                if es.len() == 1 {
                    Ok(Some(None))
                } else {
                    let mut es = es.clone();
                    es.remove(i);
                    Ok(Some(Some(NodeH::new(Node::Leaf(es)))))
                }
            }
        },
        Node::Branch { bitmap, kids } => {
            let bit = 1u16 << nibble(kh, depth);
            if bitmap & bit == 0 {
                return Ok(None);
            }
            let idx = (bitmap & (bit - 1)).count_ones() as usize;
            let child = resolve(&kids[idx], ld)?;
            let Some(new_child) = remove_rec(&child, depth + 1, key, kh, ld)? else {
                return Ok(None);
            };
            if (n.count as usize - 1) <= LEAF_MAX {
                // the subtree is small enough to be a single leaf again (canonical shape)
                let mut es = Vec::new();
                collect_entries(n, ld, &mut es)?;
                es.retain(|(k, _)| k != key);
                es.sort_unstable_by_key(|e| e.0);
                return Ok(Some(if es.is_empty() { None } else { Some(NodeH::new(Node::Leaf(es))) }));
            }
            let mut kids = kids.clone();
            let mut bitmap = *bitmap;
            match new_child {
                Some(c) => kids[idx] = Child::Mem(c),
                None => {
                    kids.remove(idx);
                    bitmap &= !bit;
                }
            }
            Ok(Some(Some(NodeH::new(Node::Branch { bitmap, kids }))))
        }
    }
}

/// Remove `key` (a no-op if absent).
pub fn remove(root: &Root, key: &ObjectId, ld: &dyn NodeLoader) -> Result<Root> {
    let Some(r) = root else { return Ok(None) };
    match remove_rec(r, 0, key, key.key_hash(), ld)? {
        None => Ok(root.clone()),
        Some(new) => Ok(new),
    }
}

type DiffFn<'a> = dyn FnMut(&ObjectId, Option<Hash>, Option<Hash>) + 'a;

fn emit_all(n: &Arc<NodeH>, ld: &dyn NodeLoader, left: bool, f: &mut DiffFn<'_>) -> Result<()> {
    let mut es = Vec::new();
    collect_entries(n, ld, &mut es)?;
    es.sort_unstable_by_key(|e| e.0);
    for (k, h) in es {
        if left {
            f(&k, Some(h), None);
        } else {
            f(&k, None, Some(h));
        }
    }
    Ok(())
}

fn diff_nodes(x: &Arc<NodeH>, y: &Arc<NodeH>, ld: &dyn NodeLoader, f: &mut DiffFn<'_>) -> Result<()> {
    if x.hash == y.hash {
        return Ok(());
    }
    match (&x.node, &y.node) {
        (Node::Branch { bitmap: bx, kids: kx }, Node::Branch { bitmap: by, kids: ky }) => {
            for nib in 0..16 {
                let bit = 1u16 << nib;
                let cx = (bx & bit != 0).then(|| &kx[(bx & (bit - 1)).count_ones() as usize]);
                let cy = (by & bit != 0).then(|| &ky[(by & (bit - 1)).count_ones() as usize]);
                match (cx, cy) {
                    (None, None) => {}
                    (Some(c), None) => emit_all(&resolve(c, ld)?, ld, true, f)?,
                    (None, Some(c)) => emit_all(&resolve(c, ld)?, ld, false, f)?,
                    (Some(a), Some(b)) => {
                        if a.hash() != b.hash() {
                            diff_nodes(&resolve(a, ld)?, &resolve(b, ld)?, ld, f)?;
                        }
                    }
                }
            }
            Ok(())
        }
        _ => {
            // at least one side is a leaf: compare the flattened entries
            let mut ex = Vec::new();
            collect_entries(x, ld, &mut ex)?;
            ex.sort_unstable_by_key(|e| e.0);
            let mut ey = Vec::new();
            collect_entries(y, ld, &mut ey)?;
            ey.sort_unstable_by_key(|e| e.0);
            let (mut i, mut j) = (0, 0);
            while i < ex.len() || j < ey.len() {
                match (ex.get(i), ey.get(j)) {
                    (Some(a), Some(b)) if a.0 == b.0 => {
                        if a.1 != b.1 {
                            f(&a.0, Some(a.1), Some(b.1));
                        }
                        i += 1;
                        j += 1;
                    }
                    (Some(a), Some(b)) if a.0 < b.0 => {
                        f(&a.0, Some(a.1), None);
                        i += 1;
                    }
                    (Some(_), Some(b)) => {
                        f(&b.0, None, Some(b.1));
                        j += 1;
                    }
                    (Some(a), None) => {
                        f(&a.0, Some(a.1), None);
                        i += 1;
                    }
                    (None, Some(b)) => {
                        f(&b.0, None, Some(b.1));
                        j += 1;
                    }
                    (None, None) => break,
                }
            }
            Ok(())
        }
    }
}

/// Structural diff: calls `f(key, in_a, in_b)` for every key whose value differs. Subtrees
/// with equal hashes are skipped without being loaded, so the cost follows the size of the
/// difference, not the size of the document.
pub fn diff(a: &Root, b: &Root, ld: &dyn NodeLoader, f: &mut DiffFn<'_>) -> Result<()> {
    match (a, b) {
        (None, None) => Ok(()),
        (Some(x), None) => emit_all(x, ld, true, f),
        (None, Some(y)) => emit_all(y, ld, false, f),
        (Some(x), Some(y)) => diff_nodes(x, y, ld, f),
    }
}

/// Collect the nodes under `root` that are not yet stored, children before parents.
pub fn collect_unstored(root: &Root, out: &mut Vec<Arc<NodeH>>) {
    fn go(n: &Arc<NodeH>, out: &mut Vec<Arc<NodeH>>) {
        if n.is_stored() {
            return;
        }
        if let Node::Branch { kids, .. } = &n.node {
            for k in kids {
                if let Child::Mem(c) = k {
                    go(c, out);
                }
            }
        }
        out.push(n.clone());
    }
    if let Some(r) = root {
        go(r, out);
    }
}

/// Mark nodes as persisted (call after the bytes are durably written).
pub fn mark_stored(nodes: &[Arc<NodeH>]) {
    for n in nodes {
        n.stored.store(true, Ordering::Release);
    }
}

/// Check the structural invariants of a fully loaded tree (used by tests).
pub fn check_invariants(root: &Root, ld: &dyn NodeLoader) -> Result<()> {
    fn go(n: &Arc<NodeH>, depth: usize, prefix: &mut Vec<usize>, ld: &dyn NodeLoader) -> Result<u32> {
        match &n.node {
            Node::Leaf(es) => {
                if es.len() > LEAF_MAX && depth < MAX_DEPTH {
                    return Err(Error::Corrupt("oversized leaf".into()));
                }
                if es.is_empty() {
                    return Err(Error::Corrupt("empty leaf".into()));
                }
                for (k, _) in es {
                    let kh = k.key_hash();
                    if (0..depth).any(|d| nibble(kh, d) != prefix[d]) {
                        return Err(Error::Corrupt("leaf key outside its prefix".into()));
                    }
                }
                Ok(es.len() as u32)
            }
            Node::Branch { bitmap, kids } => {
                if bitmap.count_ones() as usize != kids.len() {
                    return Err(Error::Corrupt("bitmap/children mismatch".into()));
                }
                let mut total = 0;
                let mut nib_iter = (0..16).filter(|i| bitmap & (1 << i) != 0);
                for k in kids {
                    let nib = nib_iter.next().ok_or_else(|| Error::Corrupt("bitmap".into()))?;
                    prefix.push(nib);
                    total += go(&resolve(k, ld)?, depth + 1, prefix, ld)?;
                    prefix.pop();
                }
                if total as usize <= LEAF_MAX {
                    return Err(Error::Corrupt("branch that should be a leaf".into()));
                }
                if total != n.count {
                    return Err(Error::Corrupt("count mismatch".into()));
                }
                Ok(total)
            }
        }
    }
    if let Some(r) = root {
        go(r, 0, &mut Vec::new(), ld)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn h(n: u64) -> Hash {
        Hash::digest("test", &n.to_le_bytes())
    }

    fn build_tree(keys: &[u64]) -> Root {
        let mut r: Root = None;
        for k in keys {
            r = insert(&r, ObjectId::from_u64(*k), h(*k), &NoLoader).unwrap();
        }
        r
    }

    #[test]
    fn insert_get_remove() {
        let mut r: Root = None;
        let mut model = BTreeMap::new();
        for k in 0..500u64 {
            r = insert(&r, ObjectId::from_u64(k), h(k), &NoLoader).unwrap();
            model.insert(k, h(k));
        }
        check_invariants(&r, &NoLoader).unwrap();
        assert_eq!(root_count(&r), 500);
        for k in 0..600u64 {
            let got = get(&r, &ObjectId::from_u64(k), &NoLoader).unwrap();
            assert_eq!(got, model.get(&k).copied());
        }
        for k in (0..500u64).step_by(2) {
            r = remove(&r, &ObjectId::from_u64(k), &NoLoader).unwrap();
            model.remove(&k);
        }
        check_invariants(&r, &NoLoader).unwrap();
        assert_eq!(root_count(&r), 250);
        let es = entries(&r, &NoLoader).unwrap();
        assert_eq!(es.len(), 250);
        // removing a missing key changes nothing
        let same = remove(&r, &ObjectId::from_u64(0), &NoLoader).unwrap();
        assert_eq!(root_hash(&same), root_hash(&r));
    }

    #[test]
    fn shape_is_history_independent() {
        let keys: Vec<u64> = (0..300).collect();
        let a = build_tree(&keys);
        let mut rev = keys.clone();
        rev.reverse();
        let b = build_tree(&rev);
        assert_eq!(root_hash(&a), root_hash(&b));
        // build with extra keys then remove them: same hash as never having them
        let mut c = build_tree(&(0..400).collect::<Vec<_>>());
        for k in 300..400u64 {
            c = remove(&c, &ObjectId::from_u64(k), &NoLoader).unwrap();
        }
        assert_eq!(root_hash(&a), root_hash(&c));
        check_invariants(&c, &NoLoader).unwrap();
        // overwrite with the same value is the identity
        let d = insert(&a, ObjectId::from_u64(5), h(5), &NoLoader).unwrap();
        assert!(Arc::ptr_eq(a.as_ref().unwrap(), d.as_ref().unwrap()));
        // empty after removing everything
        let mut e = a.clone();
        for k in 0..300u64 {
            e = remove(&e, &ObjectId::from_u64(k), &NoLoader).unwrap();
        }
        assert!(e.is_none());
        assert_eq!(root_hash(&e), empty_root_hash());
    }

    #[test]
    fn structural_sharing() {
        let a = build_tree(&(0..2000).collect::<Vec<_>>());
        let b = insert(&a, ObjectId::from_u64(7), h(9999), &NoLoader).unwrap();
        // a single update creates only the nodes on one path
        let mut na = Vec::new();
        collect_unstored(&a, &mut na);
        let mut nb = Vec::new();
        collect_unstored(&b, &mut nb);
        let sa: std::collections::HashSet<Hash> = na.iter().map(|n| n.hash).collect();
        let fresh = nb.iter().filter(|n| !sa.contains(&n.hash)).count();
        assert!(fresh <= 6, "fresh nodes: {fresh}");
    }

    #[test]
    fn diff_reports_exactly_the_differences() {
        let a = build_tree(&(0..1000).collect::<Vec<_>>());
        let mut b = a.clone();
        let mut expect = BTreeMap::new();
        for k in [3u64, 77, 500, 999] {
            b = insert(&b, ObjectId::from_u64(k), h(k + 10_000), &NoLoader).unwrap();
            expect.insert(ObjectId::from_u64(k), (Some(h(k)), Some(h(k + 10_000))));
        }
        b = remove(&b, &ObjectId::from_u64(10), &NoLoader).unwrap();
        expect.insert(ObjectId::from_u64(10), (Some(h(10)), None));
        b = insert(&b, ObjectId::from_u64(5000), h(5000), &NoLoader).unwrap();
        expect.insert(ObjectId::from_u64(5000), (None, Some(h(5000))));
        let mut got = BTreeMap::new();
        diff(&a, &b, &NoLoader, &mut |k, x, y| {
            got.insert(*k, (x, y));
        })
        .unwrap();
        assert_eq!(got, expect);
        // diff against empty
        let mut n = 0;
        diff(&None, &a, &NoLoader, &mut |_, _, _| n += 1).unwrap();
        assert_eq!(n, 1000);
        let mut n = 0;
        diff(&a, &a, &NoLoader, &mut |_, _, _| n += 1).unwrap();
        assert_eq!(n, 0);
    }

    #[test]
    fn nodes_round_trip_through_bytes() {
        let a = build_tree(&(0..100).collect::<Vec<_>>());
        let root = a.as_ref().unwrap();
        let bytes = root.to_bytes();
        let back = NodeH::from_bytes(root.hash, &bytes).unwrap();
        assert_eq!(back.count, root.count);
        assert!(back.is_stored());
        assert!(NodeH::from_bytes(h(1), &bytes).is_err());
        let mut bad = bytes.clone();
        bad.push(0);
        assert!(NodeH::from_bytes(Hash::digest(DOM_NODE, &bad), &bad).is_err());
    }
}
