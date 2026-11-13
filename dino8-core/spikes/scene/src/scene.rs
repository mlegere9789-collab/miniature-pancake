//! The entity-component-relationship store.
//!
//! Every field is a persistent structure (`Table`, `Sorted`, or an `Arc`), so
//! `Scene::clone()` is a snapshot that costs a handful of reference-count
//! bumps and shares all pages with the original until either side writes.

use std::collections::HashMap;
use std::sync::Arc;

use crate::bvh::Bvh;
use crate::ptable::Table;
use crate::sorted::Sorted;
use crate::types::*;

pub const CT_FIXED_OFFSET: u32 = 0;
pub const CT_DISTANCE: u32 = 1;

const MASK30: u64 = (1 << 30) - 1;

pub(crate) fn ek(kind: RelKind, a: Ix, b: Ix) -> u64 {
    ((kind as u64) << 60) | ((a as u64) << 30) | b as u64
}

pub(crate) fn ek_parts(k: u64) -> (RelKind, Ix, Ix) {
    (RelKind::from_u8((k >> 60) as u8).expect("valid kind"), ((k >> 30) & MASK30) as Ix, (k & MASK30) as Ix)
}

#[derive(Copy, Clone, Debug)]
pub struct Config {
    /// Maintain an inverted index (class, type) -> entities.
    pub class_index: bool,
    /// Maintain an inverted index classification code -> entities.
    pub classification_index: bool,
}

impl Default for Config {
    fn default() -> Self {
        Config { class_index: true, classification_index: true }
    }
}

#[derive(Clone, Default)]
pub struct Schema {
    pub props: Vec<(String, PropKind)>,
    pub strings: Vec<String>,
    string_ix: HashMap<String, u32>,
}

impl Schema {
    pub fn intern(&mut self, s: &str) -> u32 {
        if let Some(&i) = self.string_ix.get(s) {
            return i;
        }
        let i = self.strings.len() as u32;
        self.strings.push(s.to_string());
        self.string_ix.insert(s.to_string(), i);
        i
    }
}

#[derive(Clone, Default)]
pub(crate) struct Spatial {
    pub bvh: Option<Arc<Bvh>>,
    pub dirty: Table<()>,
    pub dirty_list: Arc<Vec<Ix>>,
}

/// Optional components supplied when an entity is created.
#[derive(Copy, Clone, Default, Debug)]
pub struct EntityParts {
    pub geometry: Option<Geometry>,
    pub semantic: Option<Semantic>,
    pub procedural: Option<Procedural>,
    pub presentation: Option<Presentation>,
}

#[derive(Clone)]
pub struct Scene {
    pub(crate) cfg: Config,
    pub(crate) ids: Table<u128>,
    pub(crate) idmap: Sorted<IdEntry>,
    pub(crate) geom: Table<Geometry>,
    pub(crate) sem: Table<Semantic>,
    pub(crate) proc_: Table<Procedural>,
    pub(crate) pres: Table<Presentation>,
    pub(crate) props: Vec<Table<u64>>,
    pub(crate) schema: Arc<Schema>,
    pub(crate) fwd: Sorted<u64>,
    pub(crate) rev: Sorted<u64>,
    pub(crate) cons: Sorted<ConstraintRec>,
    pub(crate) class_idx: Sorted<u64>,
    pub(crate) cls_idx: Sorted<u64>,
    pub(crate) next_ix: u32,
    pub(crate) free: Arc<Vec<Ix>>,
    pub(crate) alive: usize,
    pub(crate) spatial: Spatial,
}

pub(crate) fn class_key(s: &Semantic, ix: Ix) -> u64 {
    ((s.class as u64) << 48) | ((s.ptype as u64) << 32) | ix as u64
}

pub(crate) fn cls_key(s: &Semantic, ix: Ix) -> u64 {
    ((s.classification as u64) << 32) | ix as u64
}

impl Scene {
    pub fn new(cfg: Config) -> Scene {
        Scene {
            cfg,
            ids: Table::new(),
            idmap: Sorted::new(),
            geom: Table::new(),
            sem: Table::new(),
            proc_: Table::new(),
            pres: Table::new(),
            props: Vec::new(),
            schema: Arc::new(Schema::default()),
            fwd: Sorted::new(),
            rev: Sorted::new(),
            cons: Sorted::new(),
            class_idx: Sorted::new(),
            cls_idx: Sorted::new(),
            next_ix: 0,
            free: Arc::new(Vec::new()),
            alive: 0,
            spatial: Spatial::default(),
        }
    }

    pub fn config(&self) -> Config {
        self.cfg
    }

    /// Number of live entities.
    pub fn len(&self) -> usize {
        self.alive
    }

    pub fn is_empty(&self) -> bool {
        self.alive == 0
    }

    // ---------- schema ----------

    pub fn register_prop(&mut self, name: &str, kind: PropKind) -> PropKey {
        if let Some(i) = self.schema.props.iter().position(|(n, _)| n == name) {
            return PropKey(i as u16);
        }
        let s = Arc::make_mut(&mut self.schema);
        s.props.push((name.to_string(), kind));
        self.props.push(Table::new());
        PropKey((self.props.len() - 1) as u16)
    }

    pub fn intern(&mut self, s: &str) -> u32 {
        if let Some(&i) = self.schema.string_ix_get(s) {
            return i;
        }
        Arc::make_mut(&mut self.schema).intern(s)
    }

    pub fn schema(&self) -> &Schema {
        &self.schema
    }

    // ---------- identity ----------

    pub fn ix_of(&self, id: Uuid) -> Option<Ix> {
        self.idmap.get(((id.0 >> 64) as u64, id.0 as u64)).map(|e| e.ix)
    }

    pub fn uuid_of(&self, ix: Ix) -> Option<Uuid> {
        self.ids.get(ix).map(|v| Uuid(*v))
    }

    pub fn contains(&self, id: Uuid) -> bool {
        self.ix_of(id).is_some()
    }

    // ---------- create / delete ----------

    fn alloc_ix(&mut self) -> Result<Ix, SceneError> {
        if !self.free.is_empty() {
            return Ok(Arc::make_mut(&mut self.free).pop().unwrap());
        }
        if self.next_ix >= MAX_IX {
            return Err(SceneError::CapacityExceeded);
        }
        self.next_ix += 1;
        Ok(self.next_ix - 1)
    }

    pub fn create(&mut self, id: Uuid, parts: EntityParts) -> Result<Ix, SceneError> {
        if self.contains(id) {
            return Err(SceneError::DuplicateUuid(id));
        }
        let ix = self.alloc_ix()?;
        self.ids.set(ix, id.0);
        self.idmap.insert(IdEntry::new(id, ix));
        self.alive += 1;
        if let Some(g) = parts.geometry {
            self.set_geometry(ix, g);
        }
        if let Some(s) = parts.semantic {
            self.set_semantic(ix, s);
        }
        if let Some(p) = parts.procedural {
            self.proc_.set(ix, p);
        }
        if let Some(p) = parts.presentation {
            self.pres.set(ix, p);
        }
        Ok(ix)
    }

    /// Delete an entity. Entities hosted by it (transitively) are deleted
    /// too; contained, connected, referenced and constrained entities only
    /// lose the edges. Returns every deleted id.
    pub fn delete(&mut self, id: Uuid) -> Result<Vec<Uuid>, SceneError> {
        let ix = self.ix_of(id).ok_or(SceneError::UnknownEntity(id))?;
        let mut victims = vec![ix];
        let mut i = 0;
        while i < victims.len() {
            let v = victims[i];
            i += 1;
            let hosted: Vec<Ix> = self.inn(RelKind::Hosting, v).collect();
            victims.extend(hosted);
        }
        let mut out = Vec::with_capacity(victims.len());
        for v in victims {
            out.push(self.uuid_of(v).unwrap());
            self.remove_entity_raw(v);
        }
        Ok(out)
    }

    fn remove_entity_raw(&mut self, ix: Ix) {
        for kind in RelKind::ALL {
            let outs: Vec<Ix> = self.out(kind, ix).collect();
            for b in outs {
                self.del_edge(kind, ix, b);
            }
            let ins: Vec<Ix> = self.inn(kind, ix).collect();
            for a in ins {
                self.del_edge(kind, a, ix);
            }
        }
        if let Some(s) = self.sem.get(ix).copied() {
            self.unindex(&s, ix);
            self.sem.remove(ix);
        }
        self.geom.remove(ix);
        self.mark_dirty(ix);
        self.proc_.remove(ix);
        self.pres.remove(ix);
        for c in self.props.iter_mut() {
            c.remove(ix);
        }
        let id = Uuid(*self.ids.get(ix).unwrap());
        self.ids.remove(ix);
        self.idmap.remove(((id.0 >> 64) as u64, id.0 as u64));
        Arc::make_mut(&mut self.free).push(ix);
        self.alive -= 1;
    }

    // ---------- components ----------

    fn index(&mut self, s: &Semantic, ix: Ix) {
        if self.cfg.class_index {
            self.class_idx.insert(class_key(s, ix));
        }
        if self.cfg.classification_index && s.classification != 0 {
            self.cls_idx.insert(cls_key(s, ix));
        }
    }

    fn unindex(&mut self, s: &Semantic, ix: Ix) {
        if self.cfg.class_index {
            self.class_idx.remove(class_key(s, ix));
        }
        if self.cfg.classification_index && s.classification != 0 {
            self.cls_idx.remove(cls_key(s, ix));
        }
    }

    pub(crate) fn mark_dirty(&mut self, ix: Ix) {
        if self.spatial.bvh.is_some() && !self.spatial.dirty.contains(ix) {
            self.spatial.dirty.set(ix, ());
            Arc::make_mut(&mut self.spatial.dirty_list).push(ix);
        }
    }

    pub fn set_geometry(&mut self, ix: Ix, g: Geometry) {
        debug_assert!(self.ids.contains(ix));
        self.geom.set(ix, g);
        self.mark_dirty(ix);
    }

    pub fn set_semantic(&mut self, ix: Ix, s: Semantic) {
        debug_assert!(self.ids.contains(ix));
        if let Some(old) = self.sem.get(ix).copied() {
            if old == s {
                return;
            }
            self.unindex(&old, ix);
        }
        self.index(&s, ix);
        self.sem.set(ix, s);
    }

    pub fn set_procedural(&mut self, ix: Ix, p: Procedural) {
        self.proc_.set(ix, p);
    }

    pub fn clear_procedural(&mut self, ix: Ix) {
        self.proc_.remove(ix);
    }

    pub fn set_presentation(&mut self, ix: Ix, p: Presentation) {
        self.pres.set(ix, p);
    }

    pub fn set_prop(&mut self, ix: Ix, key: PropKey, v: PropVal) -> Result<(), SceneError> {
        let (_, kind) = self.schema.props.get(key.0 as usize).ok_or(SceneError::UnknownProperty)?;
        if *kind != v.kind() {
            return Err(SceneError::PropertyKind);
        }
        self.props[key.0 as usize].set(ix, v.to_raw());
        Ok(())
    }

    pub fn clear_prop(&mut self, ix: Ix, key: PropKey) {
        if let Some(c) = self.props.get_mut(key.0 as usize) {
            c.remove(ix);
        }
    }

    // ---------- relationships ----------

    pub fn out(&self, kind: RelKind, a: Ix) -> impl Iterator<Item = Ix> + '_ {
        self.fwd.range(ek(kind, a, 0), ek(kind, a, MAX_IX)).map(|k| (k & MASK30) as Ix)
    }

    pub fn inn(&self, kind: RelKind, b: Ix) -> impl Iterator<Item = Ix> + '_ {
        self.rev.range(ek(kind, b, 0), ek(kind, b, MAX_IX)).map(|k| (k & MASK30) as Ix)
    }

    fn add_edge(&mut self, kind: RelKind, a: Ix, b: Ix) {
        self.fwd.insert(ek(kind, a, b));
        self.rev.insert(ek(kind, b, a));
    }

    fn del_edge(&mut self, kind: RelKind, a: Ix, b: Ix) -> bool {
        let had = self.fwd.remove(ek(kind, a, b)).is_some();
        self.rev.remove(ek(kind, b, a));
        if kind == RelKind::Constraint {
            self.cons.remove(ek(kind, a, b));
        }
        had
    }

    pub fn parent(&self, kind: RelKind, child: Ix) -> Option<Ix> {
        debug_assert!(kind.single_parent());
        self.out(kind, child).next()
    }

    fn need(&self, id: Uuid) -> Result<Ix, SceneError> {
        self.ix_of(id).ok_or(SceneError::UnknownEntity(id))
    }

    /// Add a relationship. Hosting and containment replace the previous
    /// parent and reject cycles; connection is undirected and idempotent.
    pub fn link(&mut self, kind: RelKind, a: Uuid, b: Uuid) -> Result<(), SceneError> {
        let (ia, ib) = (self.need(a)?, self.need(b)?);
        self.link_ix(kind, ia, ib)
    }

    pub fn link_ix(&mut self, kind: RelKind, a: Ix, b: Ix) -> Result<(), SceneError> {
        if a == b {
            return Err(SceneError::SelfRelation);
        }
        match kind {
            RelKind::Hosting | RelKind::Containment => {
                let mut cur = b;
                let mut steps = 0;
                while let Some(p) = self.parent(kind, cur) {
                    if p == a {
                        return Err(SceneError::Cycle);
                    }
                    cur = p;
                    steps += 1;
                    if steps > 100_000 {
                        return Err(SceneError::Cycle);
                    }
                }
                if let Some(old) = self.parent(kind, a) {
                    if old == b {
                        return Ok(());
                    }
                    self.del_edge(kind, a, old);
                }
                self.add_edge(kind, a, b);
            }
            RelKind::Connection => self.add_edge(kind, a.min(b), a.max(b)),
            RelKind::Reference => self.add_edge(kind, a, b),
            RelKind::Constraint => return self.add_constraint_ix(a, b, CT_DISTANCE, 0.0),
        }
        Ok(())
    }

    pub fn unlink(&mut self, kind: RelKind, a: Uuid, b: Uuid) -> Result<bool, SceneError> {
        let (ia, ib) = (self.need(a)?, self.need(b)?);
        let (x, y) = if kind == RelKind::Connection { (ia.min(ib), ia.max(ib)) } else { (ia, ib) };
        Ok(self.del_edge(kind, x, y))
    }

    pub fn add_constraint(&mut self, a: Uuid, b: Uuid, ctype: u32, value: f64) -> Result<(), SceneError> {
        let (ia, ib) = (self.need(a)?, self.need(b)?);
        self.add_constraint_ix(ia, ib, ctype, value)
    }

    pub fn add_constraint_ix(&mut self, a: Ix, b: Ix, ctype: u32, value: f64) -> Result<(), SceneError> {
        if a == b {
            return Err(SceneError::SelfRelation);
        }
        self.add_edge(RelKind::Constraint, a, b);
        self.cons.insert(ConstraintRec { key: ek(RelKind::Constraint, a, b), value, ctype });
        Ok(())
    }

    // ---------- propagation (store-level) ----------

    pub fn translate(&mut self, ix: Ix, d: [f32; 3]) -> bool {
        match self.geom.get(ix).copied() {
            Some(mut g) => {
                g.bbox = g.bbox.translated(d);
                self.set_geometry(ix, g);
                true
            }
            None => false,
        }
    }

    /// Move an entity and everything that follows it: hosted entities
    /// (transitively) and targets of fixed-offset constraints. Returns the
    /// number of entities whose geometry changed.
    pub fn move_with_dependents(&mut self, ix: Ix, d: [f32; 3]) -> usize {
        let mut seen = std::collections::HashSet::new();
        let mut queue = vec![ix];
        seen.insert(ix);
        let mut n = 0;
        let mut i = 0;
        while i < queue.len() {
            let cur = queue[i];
            i += 1;
            if self.translate(cur, d) {
                n += 1;
            }
            let mut next: Vec<Ix> = self.inn(RelKind::Hosting, cur).collect();
            for b in self.out(RelKind::Constraint, cur) {
                let rec = self.cons.get(ek(RelKind::Constraint, cur, b));
                if matches!(rec, Some(r) if r.ctype == CT_FIXED_OFFSET) {
                    next.push(b);
                }
            }
            for x in next {
                if seen.insert(x) {
                    queue.push(x);
                }
            }
        }
        n
    }

    // ---------- spatial ----------

    /// Rebuild the BVH from current geometry and clear the dirty overlay.
    pub fn refresh_spatial(&mut self) {
        let mut items = Vec::with_capacity(self.geom.len());
        self.geom.scan(|ix, g| items.push((ix, g.bbox)));
        self.spatial.bvh = Some(Arc::new(Bvh::build(items.into_iter())));
        self.spatial.dirty = Table::new();
        self.spatial.dirty_list = Arc::new(Vec::new());
    }

    pub fn dirty_count(&self) -> usize {
        self.spatial.dirty_list.len()
    }

    pub fn has_spatial_index(&self) -> bool {
        self.spatial.bvh.is_some()
    }

    // ---------- memory accounting ----------

    pub fn memory_report(&self) -> Vec<(&'static str, usize)> {
        vec![
            ("ids (uuid by slot)", self.ids.heap_bytes()),
            ("uuid map", self.idmap.heap_bytes()),
            ("geometry", self.geom.heap_bytes()),
            ("semantic", self.sem.heap_bytes()),
            ("procedural", self.proc_.heap_bytes()),
            ("presentation", self.pres.heap_bytes()),
            ("property columns", self.props.iter().map(|t| t.heap_bytes()).sum()),
            ("edges forward", self.fwd.heap_bytes()),
            ("edges reverse", self.rev.heap_bytes()),
            ("constraint payload", self.cons.heap_bytes()),
            ("class index", self.class_idx.heap_bytes()),
            ("classification index", self.cls_idx.heap_bytes()),
            ("bvh", self.spatial.bvh.as_ref().map(|b| b.heap_bytes()).unwrap_or(0)),
        ]
    }

    // ---------- integrity ----------

    /// Full consistency check. O(n log n); used by tests and after bulk loads.
    pub fn check_integrity(&self) -> Result<(), String> {
        macro_rules! ensure {
            ($c:expr, $($m:tt)*) => { if !$c { return Err(format!($($m)*)); } };
        }
        self.idmap.check()?;
        self.fwd.check()?;
        self.rev.check()?;
        self.cons.check()?;
        self.class_idx.check()?;
        self.cls_idx.check()?;
        ensure!(self.idmap.len() == self.alive, "idmap {} != alive {}", self.idmap.len(), self.alive);
        ensure!(self.ids.len() == self.alive, "ids {} != alive {}", self.ids.len(), self.alive);
        // identity both ways
        let mut bad = None;
        self.ids.scan(|ix, v| {
            let id = Uuid(*v);
            if self.ix_of(id) != Some(ix) {
                bad = Some(format!("idmap does not map {id} to slot {ix}"));
            }
        });
        if let Some(b) = bad {
            return Err(b);
        }
        // components only on live slots
        let mut bad = None;
        let live = |ix: Ix| self.ids.contains(ix);
        self.geom.scan(|ix, _| bad = bad.take().or_else(|| (!live(ix)).then(|| format!("geometry on dead slot {ix}"))));
        self.sem.scan(|ix, _| bad = bad.take().or_else(|| (!live(ix)).then(|| format!("semantic on dead slot {ix}"))));
        self.proc_.scan(|ix, _| bad = bad.take().or_else(|| (!live(ix)).then(|| format!("procedural on dead slot {ix}"))));
        self.pres.scan(|ix, _| bad = bad.take().or_else(|| (!live(ix)).then(|| format!("presentation on dead slot {ix}"))));
        for (k, c) in self.props.iter().enumerate() {
            c.scan(|ix, _| bad = bad.take().or_else(|| (!live(ix)).then(|| format!("property {k} on dead slot {ix}"))));
        }
        if let Some(b) = bad {
            return Err(b);
        }
        // free list
        for &f in self.free.iter() {
            ensure!(!self.ids.contains(f), "free slot {f} is alive");
            ensure!(f < self.next_ix, "free slot {f} beyond next_ix");
        }
        ensure!(self.alive + self.free.len() == self.next_ix as usize, "slot accounting");
        // edges
        ensure!(self.fwd.len() == self.rev.len(), "fwd {} != rev {}", self.fwd.len(), self.rev.len());
        let mut prev_single: Option<(u8, Ix)> = None;
        let mut cons_edges = 0usize;
        for k in self.fwd.iter() {
            let (kind, a, b) = ek_parts(k);
            ensure!(self.ids.contains(a) && self.ids.contains(b), "edge {kind:?} {a}->{b} touches a dead slot");
            ensure!(a != b, "self edge");
            ensure!(self.rev.contains(ek(kind, b, a)), "edge {kind:?} {a}->{b} missing in reverse");
            if kind.single_parent() {
                ensure!(prev_single != Some((kind as u8, a)), "two parents for {a} in {kind:?}");
                prev_single = Some((kind as u8, a));
            }
            if kind == RelKind::Connection {
                ensure!(a < b, "connection not canonical");
            }
            if kind == RelKind::Constraint {
                cons_edges += 1;
                ensure!(self.cons.contains(k), "constraint {a}->{b} has no payload");
            }
        }
        for k in self.rev.iter() {
            let (kind, b, a) = ek_parts(k);
            ensure!(self.fwd.contains(ek(kind, a, b)), "reverse edge without forward");
        }
        ensure!(cons_edges == self.cons.len(), "constraint payload count");
        // acyclic single-parent kinds
        for kind in [RelKind::Hosting, RelKind::Containment] {
            for k in self.fwd.range(ek(kind, 0, 0), ek(kind, MAX_IX, MAX_IX)) {
                let (_, a, _) = ek_parts(k);
                let mut cur = a;
                let mut steps = 0;
                while let Some(p) = self.parent(kind, cur) {
                    cur = p;
                    steps += 1;
                    ensure!(steps < 100_000 && cur != a, "{kind:?} cycle through {a}");
                }
            }
        }
        // secondary indexes
        if self.cfg.class_index {
            ensure!(self.class_idx.len() == self.sem.len(), "class index size");
        }
        let mut with_cls = 0;
        let mut bad = None;
        self.sem.scan(|ix, s| {
            if self.cfg.class_index && !self.class_idx.contains(class_key(s, ix)) {
                bad = Some(format!("class index missing {ix}"));
            }
            if s.classification != 0 {
                with_cls += 1;
                if self.cfg.classification_index && !self.cls_idx.contains(cls_key(s, ix)) {
                    bad = Some(format!("classification index missing {ix}"));
                }
            }
        });
        if let Some(b) = bad {
            return Err(b);
        }
        if self.cfg.classification_index {
            ensure!(self.cls_idx.len() == with_cls, "classification index size");
        }
        // spatial overlay
        if let Some(b) = &self.spatial.bvh {
            let mut inbvh: HashMap<Ix, Aabb> = HashMap::new();
            for (ix, bb) in b.items() {
                inbvh.insert(ix, bb);
            }
            ensure!(self.spatial.dirty.len() == self.spatial.dirty_list.len(), "dirty accounting");
            for &ix in self.spatial.dirty_list.iter() {
                ensure!(self.spatial.dirty.contains(ix), "dirty list entry without bit");
            }
            let mut bad = None;
            self.geom.scan(|ix, g| {
                if !self.spatial.dirty.contains(ix) {
                    match inbvh.get(&ix) {
                        Some(bb) if *bb == g.bbox => {}
                        _ => bad = Some(format!("clean entity {ix} not in BVH with current box")),
                    }
                }
            });
            if let Some(b) = bad {
                return Err(b);
            }
        }
        Ok(())
    }
}

impl Schema {
    fn string_ix_get(&self, s: &str) -> Option<&u32> {
        self.string_ix.get(s)
    }
}

/// Bulk loader: collects identity, index and edge keys unsorted and builds the
/// sorted structures once. Orders of magnitude faster than per-entity `create`
/// for large imports.
pub struct SceneBuilder {
    scene: Scene,
    ids: Vec<IdEntry>,
    class_keys: Vec<u64>,
    cls_keys: Vec<u64>,
    edges: Vec<u64>,
    cons: Vec<ConstraintRec>,
}

impl SceneBuilder {
    pub fn new(cfg: Config) -> SceneBuilder {
        SceneBuilder {
            scene: Scene::new(cfg),
            ids: Vec::new(),
            class_keys: Vec::new(),
            cls_keys: Vec::new(),
            edges: Vec::new(),
            cons: Vec::new(),
        }
    }

    pub fn with_capacity(cfg: Config, n: usize) -> SceneBuilder {
        let mut b = Self::new(cfg);
        b.ids.reserve(n);
        b.class_keys.reserve(n);
        b
    }

    pub fn register_prop(&mut self, name: &str, kind: PropKind) -> PropKey {
        self.scene.register_prop(name, kind)
    }

    pub fn intern(&mut self, s: &str) -> u32 {
        self.scene.intern(s)
    }

    pub fn add(&mut self, id: Uuid, p: EntityParts) -> Result<Ix, SceneError> {
        let ix = self.scene.alloc_ix()?;
        self.scene.ids.set(ix, id.0);
        self.ids.push(IdEntry::new(id, ix));
        self.scene.alive += 1;
        if let Some(g) = p.geometry {
            self.scene.geom.set(ix, g);
        }
        if let Some(s) = p.semantic {
            if self.scene.cfg.class_index {
                self.class_keys.push(class_key(&s, ix));
            }
            if self.scene.cfg.classification_index && s.classification != 0 {
                self.cls_keys.push(cls_key(&s, ix));
            }
            self.scene.sem.set(ix, s);
        }
        if let Some(x) = p.procedural {
            self.scene.proc_.set(ix, x);
        }
        if let Some(x) = p.presentation {
            self.scene.pres.set(ix, x);
        }
        Ok(ix)
    }

    pub fn set_prop(&mut self, ix: Ix, key: PropKey, v: PropVal) -> Result<(), SceneError> {
        self.scene.set_prop(ix, key, v)
    }

    pub fn link(&mut self, kind: RelKind, a: Ix, b: Ix) {
        let (a, b) = if kind == RelKind::Connection { (a.min(b), a.max(b)) } else { (a, b) };
        self.edges.push(ek(kind, a, b));
    }

    pub fn constraint(&mut self, a: Ix, b: Ix, ctype: u32, value: f64) {
        let key = ek(RelKind::Constraint, a, b);
        self.edges.push(key);
        self.cons.push(ConstraintRec { key, value, ctype });
    }

    pub fn finish(mut self) -> Result<Scene, SceneError> {
        let mut s = self.scene;
        self.ids.sort_unstable_by_key(|e| (e.hi, e.lo));
        if let Some(w) = self.ids.windows(2).find(|w| (w[0].hi, w[0].lo) == (w[1].hi, w[1].lo)) {
            return Err(SceneError::DuplicateUuid(Uuid(((w[0].hi as u128) << 64) | w[0].lo as u128)));
        }
        s.idmap = Sorted::from_sorted(&self.ids);
        drop(self.ids);
        self.class_keys.sort_unstable();
        self.class_keys.dedup();
        s.class_idx = Sorted::from_sorted(&self.class_keys);
        self.cls_keys.sort_unstable();
        self.cls_keys.dedup();
        s.cls_idx = Sorted::from_sorted(&self.cls_keys);
        self.edges.sort_unstable();
        self.edges.dedup();
        let mut prev: Option<(u8, Ix)> = None;
        for &k in &self.edges {
            let (kind, a, b) = ek_parts(k);
            if a == b {
                return Err(SceneError::SelfRelation);
            }
            if !s.ids.contains(a) || !s.ids.contains(b) {
                return Err(SceneError::Invalid("edge endpoint is not an entity"));
            }
            if kind.single_parent() {
                if prev == Some((kind as u8, a)) {
                    return Err(SceneError::Invalid("two parents in a single-parent relation"));
                }
                prev = Some((kind as u8, a));
            }
        }
        s.fwd = Sorted::from_sorted(&self.edges);
        let mut rev: Vec<u64> = self
            .edges
            .iter()
            .map(|&k| {
                let (kind, a, b) = ek_parts(k);
                ek(kind, b, a)
            })
            .collect();
        drop(self.edges);
        rev.sort_unstable();
        s.rev = Sorted::from_sorted(&rev);
        drop(rev);
        self.cons.sort_unstable_by_key(|c| c.key);
        self.cons.dedup_by_key(|c| c.key);
        s.cons = Sorted::from_sorted(&self.cons);
        Ok(s)
    }
}
