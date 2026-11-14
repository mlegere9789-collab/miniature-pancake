//! The command dispatcher's execution core.
//!
//! A command is a function from `(context, typed parameters)` to edits of objects. The context
//! gives it copy-on-write access to the document as it was before the operation, records every
//! object it reads (the operation's inputs) and every object it writes (the outputs), hands out
//! deterministic object ids and a seeded random number generator, and refuses anything that would
//! make the result depend on something other than the recorded record. Running the same command
//! with the same parameters, seed, clock value and base state therefore yields byte-identical
//! outputs: the property the replay harness checks.

use crate::crdt::{ElemId, Obj};
use crate::error::{Error, Result};
use crate::hamt::{self, Root};
use crate::ids::{Actor, Dot, Hash, ObjectId};
use crate::op::Output;
use crate::store::{blob_hash, envelope, Store, KIND_OBJ, KIND_RAW};
use crate::value::Value;
use std::collections::{BTreeMap, BTreeSet};

/// SplitMix64: tiny, fast, fully specified (so recorded seeds replay on any build).
#[derive(Clone, Debug)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Rng {
        Rng(seed)
    }

    pub fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Uniform in `[0, 1)` with 53 bits of precision.
    pub fn next_f64(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 / (1u64 << 53) as f64
    }

    /// Uniform in `[0, n)` (slightly biased for huge `n`; irrelevant here).
    pub fn below(&mut self, n: u64) -> u64 {
        if n == 0 {
            0
        } else {
            self.next_u64() % n
        }
    }

    pub fn fill(&mut self, buf: &mut [u8]) {
        for chunk in buf.chunks_mut(8) {
            let b = self.next_u64().to_le_bytes();
            chunk.copy_from_slice(&b[..chunk.len()]);
        }
    }
}

struct Slot {
    before: Option<Hash>,
    obj: Obj,
    dirty: bool,
    life_written: bool,
}

/// What a command sees and may change.
pub struct Ctx<'a> {
    base: &'a Root,
    store: &'a Store,
    dot: Dot,
    lamport: u64,
    wall_ms: u64,
    rng: Rng,
    objs: BTreeMap<ObjectId, Slot>,
    reads: BTreeSet<ObjectId>,
    created: u32,
    blobs: Vec<(Hash, Vec<u8>)>,
    set_touched: BTreeSet<(ObjectId, String, Value)>,
    elem_idx: u16,
}

impl<'a> Ctx<'a> {
    pub fn dot(&self) -> Dot {
        self.dot
    }

    pub fn lamport(&self) -> u64 {
        self.lamport
    }

    /// The recorded wall-clock time. Commands must use this, never the system clock.
    pub fn wall_ms(&self) -> u64 {
        self.wall_ms
    }

    pub fn rng(&mut self) -> &mut Rng {
        &mut self.rng
    }

    fn slot(&mut self, id: ObjectId) -> Result<&mut Slot> {
        if !self.objs.contains_key(&id) {
            let before = hamt::get(self.base, &id, self.store)?
                .ok_or_else(|| Error::Command(format!("no such object {id}")))?;
            let obj = self.store.get_obj(&before)?;
            self.reads.insert(id);
            self.objs.insert(id, Slot { before: Some(before), obj, dirty: false, life_written: false });
        }
        self.objs.get_mut(&id).ok_or_else(|| Error::Command("slot vanished".into()))
    }

    /// Read-only access to an object (recorded as an input).
    pub fn load(&mut self, id: ObjectId) -> Result<&Obj> {
        Ok(&self.slot(id)?.obj)
    }

    /// Mutable access to a live object. Editing re-asserts the object's existence at the end of
    /// the operation, so an edit that is concurrent with a delete keeps the object alive.
    pub fn edit(&mut self, id: ObjectId) -> Result<&mut Obj> {
        let s = self.slot(id)?;
        if !s.obj.is_alive() {
            return Err(Error::Command(format!("object {id} is deleted")));
        }
        s.dirty = true;
        Ok(&mut s.obj)
    }

    /// Create a new object with a deterministic id.
    pub fn create(&mut self) -> Result<(ObjectId, &mut Obj)> {
        let id = ObjectId::derive(self.dot.actor, self.dot.seq, self.created);
        self.created += 1;
        let mut obj = Obj::new();
        obj.set_alive(true, self.dot, self.lamport);
        self.objs.insert(id, Slot { before: None, obj, dirty: true, life_written: true });
        let s = self.objs.get_mut(&id).ok_or_else(|| Error::Command("slot vanished".into()))?;
        Ok((id, &mut s.obj))
    }

    pub fn set(&mut self, id: ObjectId, prop: &str, v: Value) -> Result<()> {
        let (dot, lam) = (self.dot, self.lamport);
        self.edit(id)?.reg_mut(prop)?.write(dot, lam, v);
        Ok(())
    }

    /// Current last-writer-wins value of a register property.
    pub fn value(&mut self, id: ObjectId, prop: &str) -> Result<Option<Value>> {
        Ok(self.slot(id)?.obj.value(prop).cloned())
    }

    fn touch_set(&mut self, id: ObjectId, prop: &str, elem: &Value) -> Result<()> {
        if !self.set_touched.insert((id, prop.to_string(), elem.clone())) {
            return Err(Error::Command(format!("set element written twice in one operation: {prop}")));
        }
        Ok(())
    }

    pub fn set_add(&mut self, id: ObjectId, prop: &str, elem: Value) -> Result<()> {
        self.touch_set(id, prop, &elem)?;
        let dot = self.dot;
        self.edit(id)?.set_mut(prop)?.add(elem, dot);
        Ok(())
    }

    pub fn set_remove(&mut self, id: ObjectId, prop: &str, elem: &Value) -> Result<()> {
        self.touch_set(id, prop, elem)?;
        self.edit(id)?.set_mut(prop)?.remove(elem);
        Ok(())
    }

    fn next_elem_id(&mut self) -> ElemId {
        let e = ElemId { lamport: self.lamport, dot: self.dot, idx: self.elem_idx };
        self.elem_idx += 1;
        e
    }

    /// Insert `v` so that it ends up at visible position `index` (clamped to the end).
    pub fn list_insert(&mut self, id: ObjectId, prop: &str, index: usize, v: Value) -> Result<()> {
        let eid = self.next_elem_id();
        let l = self.edit(id)?.list_mut(prop)?;
        let vis = l.visible_ids();
        let at = index.min(vis.len());
        let after = if at == 0 { None } else { Some(vis[at - 1]) };
        l.insert(after, eid, v);
        Ok(())
    }

    pub fn list_delete(&mut self, id: ObjectId, prop: &str, index: usize) -> Result<()> {
        let l = self.edit(id)?.list_mut(prop)?;
        let vis = l.visible_ids();
        let eid = *vis.get(index).ok_or_else(|| Error::Command(format!("{prop}[{index}] out of range")))?;
        l.delete(eid);
        Ok(())
    }

    /// Edit the element at `index` in place (its identity is kept).
    pub fn list_set(&mut self, id: ObjectId, prop: &str, index: usize, v: Value) -> Result<()> {
        let (dot, lam) = (self.dot, self.lamport);
        let l = self.edit(id)?.list_mut(prop)?;
        let vis = l.visible_ids();
        let eid = *vis.get(index).ok_or_else(|| Error::Command(format!("{prop}[{index}] out of range")))?;
        l.set(eid, dot, lam, v);
        Ok(())
    }

    pub fn delete(&mut self, id: ObjectId) -> Result<()> {
        let (dot, lam) = (self.dot, self.lamport);
        let s = self.slot(id)?;
        s.obj.set_alive(false, dot, lam);
        s.dirty = true;
        s.life_written = true;
        Ok(())
    }

    /// Explicitly keep an object (the resolution of a delete-versus-edit conflict).
    pub fn keep(&mut self, id: ObjectId) -> Result<()> {
        let (dot, lam) = (self.dot, self.lamport);
        let s = self.slot(id)?;
        s.obj.set_alive(true, dot, lam);
        s.dirty = true;
        s.life_written = true;
        Ok(())
    }

    /// Store heavy payload (for example geometry) as a content-addressed blob.
    pub fn put_blob(&mut self, payload: &[u8]) -> Hash {
        let env = envelope(KIND_RAW, payload);
        let h = blob_hash(&env);
        if !self.blobs.iter().any(|(x, _)| *x == h) {
            self.blobs.push((h, env));
        }
        h
    }

    /// Read back a raw blob (own or already stored).
    pub fn get_blob(&self, h: &Hash) -> Result<Vec<u8>> {
        if let Some((_, env)) = self.blobs.iter().find(|(x, _)| x == h) {
            return Ok(env[1..].to_vec());
        }
        self.store.get_raw(h)
    }
}

pub type CmdFn = fn(&mut Ctx<'_>, &Value) -> Result<()>;

#[derive(Clone, Copy)]
pub struct CmdSpec {
    pub id: &'static str,
    /// Schema tag recorded with the parameter blob.
    pub schema: &'static str,
    pub run: CmdFn,
}

#[derive(Clone, Default)]
pub struct Registry {
    cmds: BTreeMap<String, CmdSpec>,
}

impl Registry {
    pub fn new() -> Registry {
        Registry::default()
    }

    pub fn register(&mut self, spec: CmdSpec) {
        self.cmds.insert(spec.id.to_string(), spec);
    }

    pub fn get(&self, id: &str) -> Option<&CmdSpec> {
        self.cmds.get(id)
    }

    pub fn ids(&self) -> impl Iterator<Item = &String> {
        self.cmds.keys()
    }
}

pub struct ExecInput<'a> {
    pub command: &'a str,
    pub params: &'a Value,
    pub actor: Actor,
    pub seq: u32,
    pub lamport: u64,
    pub wall_ms: u64,
    pub seed: u64,
}

pub struct ExecResult {
    pub inputs: Vec<ObjectId>,
    pub outputs: Vec<Output>,
    /// Blob envelopes this operation introduces that the store does not hold yet.
    pub new_blobs: Vec<(Hash, Vec<u8>)>,
    pub new_root: Root,
}

/// Run a command against `base` without changing anything; the caller commits the result.
pub fn execute(reg: &Registry, base: &Root, store: &Store, inp: &ExecInput<'_>) -> Result<ExecResult> {
    let spec = reg.get(inp.command).ok_or_else(|| Error::UnknownCommand(inp.command.to_string()))?;
    let mut ctx = Ctx {
        base,
        store,
        dot: Dot { actor: inp.actor, seq: inp.seq },
        lamport: inp.lamport,
        wall_ms: inp.wall_ms,
        rng: Rng::new(inp.seed),
        objs: BTreeMap::new(),
        reads: BTreeSet::new(),
        created: 0,
        blobs: Vec::new(),
        set_touched: BTreeSet::new(),
        elem_idx: 0,
    };
    (spec.run)(&mut ctx, inp.params)?;

    let mut root = base.clone();
    let mut outputs = Vec::new();
    let mut new_blobs: Vec<(Hash, Vec<u8>)> = Vec::new();
    let objs = std::mem::take(&mut ctx.objs);
    for (id, mut slot) in objs {
        if !slot.dirty {
            continue;
        }
        if !slot.life_written {
            slot.obj.set_alive(true, ctx.dot, ctx.lamport);
        }
        let env = envelope(KIND_OBJ, &slot.obj.to_bytes());
        let after = blob_hash(&env);
        if slot.before == Some(after) {
            continue;
        }
        if !store.contains(&after) && !new_blobs.iter().any(|(h, _)| *h == after) {
            new_blobs.push((after, env));
        }
        root = hamt::insert(&root, id, after, store)?;
        outputs.push(Output { id, before: slot.before, after });
    }
    for (h, env) in std::mem::take(&mut ctx.blobs) {
        if !store.contains(&h) && !new_blobs.iter().any(|(x, _)| *x == h) {
            new_blobs.push((h, env));
        }
    }
    Ok(ExecResult { inputs: ctx.reads.iter().copied().collect(), outputs, new_blobs, new_root: root })
}

/// Join two document roots object by object. Returns the joined root, the new snapshot blobs and
/// the list of `(object, in_a, in_b, joined)` for every object that differed.
pub struct Joined {
    pub root: Root,
    pub new_blobs: Vec<(Hash, Vec<u8>)>,
    pub changed: Vec<(ObjectId, Option<Hash>, Option<Hash>, Hash)>,
}

pub fn join_roots(store: &Store, a: &Root, b: &Root) -> Result<Joined> {
    let mut diffs = Vec::new();
    hamt::diff(a, b, store, &mut |k, x, y| diffs.push((*k, x, y)))?;
    let mut root = a.clone();
    let mut new_blobs: Vec<(Hash, Vec<u8>)> = Vec::new();
    let mut changed = Vec::new();
    for (k, x, y) in diffs {
        let joined = match (x, y) {
            (Some(xa), Some(yb)) => {
                let mut oa = store.get_obj(&xa)?;
                let ob = store.get_obj(&yb)?;
                oa.merge(&ob);
                let env = envelope(KIND_OBJ, &oa.to_bytes());
                let h = blob_hash(&env);
                if !store.contains(&h) && !new_blobs.iter().any(|(x, _)| *x == h) {
                    new_blobs.push((h, env));
                }
                h
            }
            (None, Some(yb)) => yb,
            (Some(xa), None) => xa,
            (None, None) => continue,
        };
        if x != Some(joined) {
            root = hamt::insert(&root, k, joined, store)?;
        }
        changed.push((k, x, y, joined));
    }
    Ok(Joined { root, new_blobs, changed })
}
