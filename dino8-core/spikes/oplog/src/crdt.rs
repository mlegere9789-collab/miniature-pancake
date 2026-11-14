//! Per-property-type merge rules.
//!
//! Every property of an object is one of three CRDTs, chosen by the property's declared type:
//!
//! * [`Reg`]     multi-value register. Reads resolve last-writer-wins (highest Lamport stamp, ties
//!   broken by the writer's identity), but concurrent writes are *kept* and surface as explicit
//!   conflict items until a later write supersedes all of them.
//! * [`OrSet`]   observed-remove set with add-wins semantics (a concurrent add beats a remove).
//! * [`RgaList`] ordered list (replicated growable array); each element's value is itself a
//!   [`Reg`], so editing a vertex in place never duplicates it.
//!
//! An object also carries a `life` register: a delete writes `false`, any edit re-asserts `true`
//! (so a delete concurrent with an edit resolves to "kept" and surfaces a conflict item).
//!
//! No tombstone sets: superseded writes are remembered as one *floor* per writing actor
//! (`all dots of this actor up to seq N are dead`). That is sound because the operations of one
//! actor form a single causal chain (see [`crate::ids::Actor`]), and it keeps per-register state
//! bounded by the number of distinct actors, not by the number of edits.
//!
//! The merge of every type is a join in a semilattice: commutative, associative and idempotent.

use crate::ids::{Actor, Dot, ObjectId};
use crate::value::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CrdtError {
    /// A property was used with a different type than it was created with.
    KindMismatch(String),
    /// A snapshot failed validation while decoding.
    Decode(String),
    /// A property name starting with `$` (reserved by the JSON tagging scheme).
    ReservedName(String),
}

impl fmt::Display for CrdtError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CrdtError::KindMismatch(p) => write!(f, "property `{p}` used with a different type"),
            CrdtError::Decode(m) => write!(f, "bad object snapshot: {m}"),
            CrdtError::ReservedName(p) => write!(f, "reserved property name `{p}`"),
        }
    }
}

impl std::error::Error for CrdtError {}

fn bad<T>(msg: &str) -> Result<T, CrdtError> {
    Err(CrdtError::Decode(msg.to_string()))
}

// ---------------------------------------------------------------------------------------------
// small (de)serialization helpers
// ---------------------------------------------------------------------------------------------

fn dot_to_ints(d: &Dot) -> [Value; 3] {
    [
        Value::from_u64(d.actor.session),
        Value::Int(i64::from(d.actor.epoch)),
        Value::Int(i64::from(d.seq)),
    ]
}

fn u32_of(v: &Value) -> Result<u32, CrdtError> {
    match v {
        Value::Int(i) => u32::try_from(*i).or_else(|_| bad("integer out of u32 range")),
        _ => bad("expected integer"),
    }
}

fn u64_of(v: &Value) -> Result<u64, CrdtError> {
    v.as_u64().map_or_else(|| bad("expected integer"), Ok)
}

fn dot_from_ints(l: &[Value]) -> Result<Dot, CrdtError> {
    if l.len() < 3 {
        return bad("dot needs three integers");
    }
    Ok(Dot {
        actor: Actor { session: u64_of(&l[0])?, epoch: u32_of(&l[1])? },
        seq: u32_of(&l[2])?,
    })
}

fn list_of(v: &Value) -> Result<&[Value], CrdtError> {
    v.as_list().map_or_else(|| bad("expected list"), Ok)
}

// ---------------------------------------------------------------------------------------------
// Multi-value register
// ---------------------------------------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct Reg {
    /// Live writes: dot -> (lamport, value).
    entries: BTreeMap<Dot, (u64, Value)>,
    /// Per actor: every dot with `seq <= floor` is superseded.
    floors: BTreeMap<Actor, u32>,
}

impl Reg {
    pub fn new() -> Reg {
        Reg::default()
    }

    fn raise(floors: &mut BTreeMap<Actor, u32>, d: &Dot) {
        let f = floors.entry(d.actor).or_insert(0);
        if d.seq > *f {
            *f = d.seq;
        }
    }

    /// Write `value` with the dot of the current operation. The write supersedes every entry
    /// the writer can currently see (those it observed).
    pub fn write(&mut self, dot: Dot, lamport: u64, value: Value) {
        self.entries.remove(&dot); // a second write by the same operation replaces the first
        for d in self.entries.keys() {
            Self::raise(&mut self.floors, d);
        }
        self.entries.clear();
        debug_assert!(dot.seq > self.floors.get(&dot.actor).copied().unwrap_or(0));
        self.entries.insert(dot, (lamport, value));
    }

    /// Join with another replica's state.
    pub fn merge(&mut self, other: &Reg) {
        for (a, f) in &other.floors {
            let e = self.floors.entry(*a).or_insert(0);
            if *f > *e {
                *e = *f;
            }
        }
        for (d, v) in &other.entries {
            if let Some(mine) = self.entries.get(d) {
                debug_assert_eq!(mine, v, "same dot must carry the same write");
            } else {
                self.entries.insert(*d, v.clone());
            }
        }
        let floors = &self.floors;
        self.entries
            .retain(|d, _| d.seq > floors.get(&d.actor).copied().unwrap_or(0));
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Visible (not superseded) writes.
    pub fn visible(&self) -> impl Iterator<Item = (&Dot, u64, &Value)> {
        self.entries.iter().map(|(d, (l, v))| (d, *l, v))
    }

    /// Last-writer-wins read: the visible write with the highest (lamport, dot).
    pub fn winner(&self) -> Option<&Value> {
        self.entries
            .iter()
            .max_by_key(|(d, (l, _))| (*l, **d))
            .map(|(_, (_, v))| v)
    }

    /// Distinct visible values, sorted. More than one means a conflict.
    pub fn distinct_values(&self) -> Vec<&Value> {
        let mut v: Vec<&Value> = self.entries.values().map(|(_, v)| v).collect();
        v.sort();
        v.dedup();
        v
    }

    pub fn is_conflicted(&self) -> bool {
        self.distinct_values().len() > 1
    }

    /// Number of floors (the state-size cost of superseded writes).
    pub fn floor_count(&self) -> usize {
        self.floors.len()
    }

    pub fn to_value(&self) -> Value {
        let mut m = BTreeMap::new();
        if !self.entries.is_empty() {
            let l = self
                .entries
                .iter()
                .map(|(d, (lam, v))| {
                    let [s, e, q] = dot_to_ints(d);
                    Value::List(vec![s, e, q, Value::from_u64(*lam), v.clone()])
                })
                .collect();
            m.insert("e".to_string(), Value::List(l));
        }
        if !self.floors.is_empty() {
            let l = self
                .floors
                .iter()
                .map(|(a, f)| {
                    Value::List(vec![
                        Value::from_u64(a.session),
                        Value::Int(i64::from(a.epoch)),
                        Value::Int(i64::from(*f)),
                    ])
                })
                .collect();
            m.insert("f".to_string(), Value::List(l));
        }
        Value::Map(m)
    }

    pub fn from_value(v: &Value) -> Result<Reg, CrdtError> {
        let Value::Map(m) = v else { return bad("register must be a map") };
        if m.keys().any(|k| k != "e" && k != "f") {
            return bad("unknown register field");
        }
        let mut r = Reg::new();
        if let Some(f) = m.get("f") {
            let l = list_of(f)?;
            if l.is_empty() {
                return bad("empty floor list must be omitted");
            }
            let mut prev: Option<Actor> = None;
            for item in l {
                let it = list_of(item)?;
                if it.len() != 3 {
                    return bad("floor needs three integers");
                }
                let a = Actor { session: u64_of(&it[0])?, epoch: u32_of(&it[1])? };
                let f = u32_of(&it[2])?;
                if f == 0 || prev.is_some_and(|p| p >= a) {
                    return bad("floors must be sorted and non-zero");
                }
                prev = Some(a);
                r.floors.insert(a, f);
            }
        }
        if let Some(e) = m.get("e") {
            let l = list_of(e)?;
            if l.is_empty() {
                return bad("empty entry list must be omitted");
            }
            let mut prev: Option<Dot> = None;
            for item in l {
                let it = list_of(item)?;
                if it.len() != 5 {
                    return bad("entry needs five fields");
                }
                let d = dot_from_ints(it)?;
                if prev.is_some_and(|p| p >= d) {
                    return bad("entries must be sorted");
                }
                if d.seq <= r.floors.get(&d.actor).copied().unwrap_or(0) {
                    return bad("entry below its floor");
                }
                prev = Some(d);
                r.entries.insert(d, (u64_of(&it[3])?, it[4].clone()));
            }
        }
        Ok(r)
    }
}

// ---------------------------------------------------------------------------------------------
// Observed-remove set (add wins)
// ---------------------------------------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq, Default)]
struct ElemState {
    dots: BTreeSet<Dot>,
    floors: BTreeMap<Actor, u32>,
}

impl ElemState {
    fn merge(&mut self, other: &ElemState) {
        for (a, f) in &other.floors {
            let e = self.floors.entry(*a).or_insert(0);
            if *f > *e {
                *e = *f;
            }
        }
        self.dots.extend(other.dots.iter().copied());
        let floors = &self.floors;
        self.dots
            .retain(|d| d.seq > floors.get(&d.actor).copied().unwrap_or(0));
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct OrSet {
    elems: BTreeMap<Value, ElemState>,
}

impl OrSet {
    pub fn new() -> OrSet {
        OrSet::default()
    }

    /// Add `elem`. The new dot supersedes every add of the same element the writer observed.
    pub fn add(&mut self, elem: Value, dot: Dot) {
        let e = self.elems.entry(elem).or_default();
        for d in &e.dots {
            if *d != dot {
                Reg::raise(&mut e.floors, d);
            }
        }
        e.dots.clear();
        debug_assert!(dot.seq > e.floors.get(&dot.actor).copied().unwrap_or(0));
        e.dots.insert(dot);
    }

    /// Remove `elem`: kills exactly the adds the writer has observed.
    pub fn remove(&mut self, elem: &Value) {
        if let Some(e) = self.elems.get_mut(elem) {
            for d in &e.dots {
                Reg::raise(&mut e.floors, d);
            }
            e.dots.clear();
        }
    }

    pub fn merge(&mut self, other: &OrSet) {
        for (elem, oe) in &other.elems {
            match self.elems.get_mut(elem) {
                Some(e) => e.merge(oe),
                None => {
                    self.elems.insert(elem.clone(), oe.clone());
                }
            }
        }
    }

    pub fn contains(&self, elem: &Value) -> bool {
        self.elems.get(elem).is_some_and(|e| !e.dots.is_empty())
    }

    pub fn visible(&self) -> impl Iterator<Item = &Value> {
        self.elems.iter().filter(|(_, e)| !e.dots.is_empty()).map(|(v, _)| v)
    }

    pub fn len(&self) -> usize {
        self.visible().count()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn to_value(&self) -> Value {
        Value::List(
            self.elems
                .iter()
                .map(|(elem, e)| {
                    let dots = e.dots.iter().map(|d| Value::List(dot_to_ints(d).to_vec())).collect();
                    let floors = e
                        .floors
                        .iter()
                        .map(|(a, f)| {
                            Value::List(vec![
                                Value::from_u64(a.session),
                                Value::Int(i64::from(a.epoch)),
                                Value::Int(i64::from(*f)),
                            ])
                        })
                        .collect();
                    Value::List(vec![elem.clone(), Value::List(dots), Value::List(floors)])
                })
                .collect(),
        )
    }

    pub fn from_value(v: &Value) -> Result<OrSet, CrdtError> {
        let mut s = OrSet::new();
        let mut prev: Option<&Value> = None;
        for item in list_of(v)? {
            let it = list_of(item)?;
            if it.len() != 3 {
                return bad("set element needs three fields");
            }
            if prev.is_some_and(|p| *p >= it[0]) {
                return bad("set elements must be sorted");
            }
            prev = Some(&it[0]);
            let mut e = ElemState::default();
            let mut pa: Option<Actor> = None;
            for f in list_of(&it[2])? {
                let f = list_of(f)?;
                if f.len() != 3 {
                    return bad("floor needs three integers");
                }
                let a = Actor { session: u64_of(&f[0])?, epoch: u32_of(&f[1])? };
                let n = u32_of(&f[2])?;
                if n == 0 || pa.is_some_and(|p| p >= a) {
                    return bad("floors must be sorted and non-zero");
                }
                pa = Some(a);
                e.floors.insert(a, n);
            }
            let mut pd: Option<Dot> = None;
            for d in list_of(&it[1])? {
                let d = dot_from_ints(list_of(d)?)?;
                if pd.is_some_and(|p| p >= d) {
                    return bad("dots must be sorted");
                }
                if d.seq <= e.floors.get(&d.actor).copied().unwrap_or(0) {
                    return bad("dot below its floor");
                }
                pd = Some(d);
                e.dots.insert(d);
            }
            if e.dots.is_empty() && e.floors.is_empty() {
                return bad("empty set element");
            }
            s.elems.insert(it[0].clone(), e);
        }
        Ok(s)
    }
}

// ---------------------------------------------------------------------------------------------
// Ordered list (RGA)
// ---------------------------------------------------------------------------------------------

/// Unique identity of a list element; ordered by Lamport time first so that a newer insert
/// placed after the same reference element sorts before older ones.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
pub struct ElemId {
    pub lamport: u64,
    pub dot: Dot,
    pub idx: u16,
}

impl ElemId {
    fn to_ints(self) -> Vec<Value> {
        let [s, e, q] = dot_to_ints(&self.dot);
        vec![Value::from_u64(self.lamport), s, e, q, Value::Int(i64::from(self.idx))]
    }

    fn from_ints(l: &[Value]) -> Result<ElemId, CrdtError> {
        if l.len() != 5 {
            return bad("element id needs five integers");
        }
        Ok(ElemId {
            lamport: u64_of(&l[0])?,
            dot: dot_from_ints(&l[1..4])?,
            idx: u16::try_from(u32_of(&l[4])?).or_else(|_| bad("idx out of range"))?,
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ListElem {
    pub after: Option<ElemId>,
    pub deleted: bool,
    pub value: Reg,
}

#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct RgaList {
    elems: BTreeMap<ElemId, ListElem>,
}

impl RgaList {
    pub fn new() -> RgaList {
        RgaList::default()
    }

    /// Insert `value` right after `after` (or at the head). `id` must be unique.
    pub fn insert(&mut self, after: Option<ElemId>, id: ElemId, value: Value) {
        let mut reg = Reg::new();
        reg.write(id.dot, id.lamport, value);
        self.elems.insert(id, ListElem { after, deleted: false, value: reg });
    }

    pub fn delete(&mut self, id: ElemId) -> bool {
        match self.elems.get_mut(&id) {
            Some(e) => {
                e.deleted = true;
                true
            }
            None => false,
        }
    }

    /// Write a new value into an element's register (an in-place edit).
    pub fn set(&mut self, id: ElemId, dot: Dot, lamport: u64, value: Value) -> bool {
        match self.elems.get_mut(&id) {
            Some(e) if !e.deleted => {
                e.value.write(dot, lamport, value);
                true
            }
            _ => false,
        }
    }

    pub fn merge(&mut self, other: &RgaList) {
        for (id, oe) in &other.elems {
            match self.elems.get_mut(id) {
                Some(e) => {
                    debug_assert_eq!(e.after, oe.after);
                    e.deleted |= oe.deleted;
                    e.value.merge(&oe.value);
                }
                None => {
                    self.elems.insert(*id, oe.clone());
                }
            }
        }
    }

    /// All element ids (including deleted ones) in document order.
    fn order(&self) -> Vec<ElemId> {
        let mut children: BTreeMap<Option<ElemId>, Vec<ElemId>> = BTreeMap::new();
        for (id, e) in &self.elems {
            children.entry(e.after).or_default().push(*id);
        }
        // ids come out of the BTreeMap ascending, which is the push order we need: the
        // stack pops the largest (newest) sibling first.
        let mut out = Vec::with_capacity(self.elems.len());
        let mut stack: Vec<ElemId> = children.remove(&None).unwrap_or_default();
        while let Some(id) = stack.pop() {
            out.push(id);
            if let Some(kids) = children.remove(&Some(id)) {
                stack.extend(kids);
            }
        }
        out
    }

    /// Visible element ids in order.
    pub fn visible_ids(&self) -> Vec<ElemId> {
        self.order()
            .into_iter()
            .filter(|id| self.elems.get(id).is_some_and(|e| !e.deleted))
            .collect()
    }

    /// Visible values in order (last-writer-wins per element).
    pub fn values(&self) -> Vec<Value> {
        self.visible_ids()
            .iter()
            .filter_map(|id| self.elems.get(id).and_then(|e| e.value.winner().cloned()))
            .collect()
    }

    pub fn len(&self) -> usize {
        self.elems.values().filter(|e| !e.deleted).count()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn tombstones(&self) -> usize {
        self.elems.values().filter(|e| e.deleted).count()
    }

    pub fn elem(&self, id: &ElemId) -> Option<&ListElem> {
        self.elems.get(id)
    }

    pub fn elems(&self) -> impl Iterator<Item = (&ElemId, &ListElem)> {
        self.elems.iter()
    }

    pub fn to_value(&self) -> Value {
        Value::List(
            self.elems
                .iter()
                .map(|(id, e)| {
                    Value::List(vec![
                        Value::List(id.to_ints()),
                        e.after.map_or(Value::Null, |a| Value::List(a.to_ints())),
                        Value::Bool(e.deleted),
                        e.value.to_value(),
                    ])
                })
                .collect(),
        )
    }

    pub fn from_value(v: &Value) -> Result<RgaList, CrdtError> {
        let mut l = RgaList::new();
        let mut prev: Option<ElemId> = None;
        for item in list_of(v)? {
            let it = list_of(item)?;
            if it.len() != 4 {
                return bad("list element needs four fields");
            }
            let id = ElemId::from_ints(list_of(&it[0])?)?;
            if prev.is_some_and(|p| p >= id) {
                return bad("list elements must be sorted");
            }
            prev = Some(id);
            let after = match &it[1] {
                Value::Null => None,
                other => Some(ElemId::from_ints(list_of(other)?)?),
            };
            let Value::Bool(deleted) = it[2] else { return bad("deleted flag must be a bool") };
            let value = Reg::from_value(&it[3])?;
            l.elems.insert(id, ListElem { after, deleted, value });
        }
        Ok(l)
    }
}

// ---------------------------------------------------------------------------------------------
// Property and object
// ---------------------------------------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Prop {
    Reg(Reg),
    Set(OrSet),
    List(RgaList),
}

impl Prop {
    fn kind_rank(&self) -> u8 {
        match self {
            Prop::Reg(_) => 0,
            Prop::Set(_) => 1,
            Prop::List(_) => 2,
        }
    }

    pub fn kind_name(&self) -> &'static str {
        match self {
            Prop::Reg(_) => "reg",
            Prop::Set(_) => "set",
            Prop::List(_) => "list",
        }
    }

    /// Join. If two replicas disagree on a property's type (a schema error), both sides keep
    /// the lower-ranked type, so the result is still independent of the merge order.
    pub fn merge(&mut self, other: &Prop) {
        match (&mut *self, other) {
            (Prop::Reg(a), Prop::Reg(b)) => a.merge(b),
            (Prop::Set(a), Prop::Set(b)) => a.merge(b),
            (Prop::List(a), Prop::List(b)) => a.merge(b),
            (mine, theirs) => {
                if theirs.kind_rank() < mine.kind_rank() {
                    *mine = theirs.clone();
                }
            }
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ConflictKind {
    /// Concurrent different values in a register.
    Register,
    /// Concurrent different values for one list element.
    ListElement,
    /// One session deleted the object while another edited it; the object was kept.
    DeleteVsEdit,
}

/// An explicit, resolvable conflict item.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Conflict {
    pub object: ObjectId,
    pub prop: String,
    pub elem: Option<ElemId>,
    pub kind: ConflictKind,
    /// The value the last-writer-wins read currently shows.
    pub winner: Value,
    /// The other concurrent values.
    pub others: Vec<Value>,
    /// Sessions that contributed the conflicting writes.
    pub sessions: Vec<u64>,
}

#[derive(Clone, Debug, PartialEq, Eq, Default)]
pub struct Obj {
    pub life: Reg,
    pub props: BTreeMap<String, Prop>,
}

fn check_name(name: &str) -> Result<(), CrdtError> {
    if name.starts_with('$') {
        Err(CrdtError::ReservedName(name.to_string()))
    } else {
        Ok(())
    }
}

impl Obj {
    pub fn new() -> Obj {
        Obj::default()
    }

    /// Alive unless every visible `life` write says deleted (add wins).
    pub fn is_alive(&self) -> bool {
        self.life.visible().any(|(_, _, v)| *v == Value::Bool(true))
    }

    pub fn set_alive(&mut self, alive: bool, dot: Dot, lamport: u64) {
        self.life.write(dot, lamport, Value::Bool(alive));
    }

    pub fn reg(&self, name: &str) -> Option<&Reg> {
        match self.props.get(name) {
            Some(Prop::Reg(r)) => Some(r),
            _ => None,
        }
    }

    /// Last-writer-wins value of a register property.
    pub fn value(&self, name: &str) -> Option<&Value> {
        self.reg(name).and_then(Reg::winner)
    }

    pub fn set(&self, name: &str) -> Option<&OrSet> {
        match self.props.get(name) {
            Some(Prop::Set(s)) => Some(s),
            _ => None,
        }
    }

    pub fn list(&self, name: &str) -> Option<&RgaList> {
        match self.props.get(name) {
            Some(Prop::List(l)) => Some(l),
            _ => None,
        }
    }

    pub fn reg_mut(&mut self, name: &str) -> Result<&mut Reg, CrdtError> {
        check_name(name)?;
        match self.props.entry(name.to_string()).or_insert_with(|| Prop::Reg(Reg::new())) {
            Prop::Reg(r) => Ok(r),
            _ => Err(CrdtError::KindMismatch(name.to_string())),
        }
    }

    pub fn set_mut(&mut self, name: &str) -> Result<&mut OrSet, CrdtError> {
        check_name(name)?;
        match self.props.entry(name.to_string()).or_insert_with(|| Prop::Set(OrSet::new())) {
            Prop::Set(s) => Ok(s),
            _ => Err(CrdtError::KindMismatch(name.to_string())),
        }
    }

    pub fn list_mut(&mut self, name: &str) -> Result<&mut RgaList, CrdtError> {
        check_name(name)?;
        match self.props.entry(name.to_string()).or_insert_with(|| Prop::List(RgaList::new())) {
            Prop::List(l) => Ok(l),
            _ => Err(CrdtError::KindMismatch(name.to_string())),
        }
    }

    pub fn merge(&mut self, other: &Obj) {
        self.life.merge(&other.life);
        for (name, p) in &other.props {
            match self.props.get_mut(name) {
                Some(mine) => mine.merge(p),
                None => {
                    self.props.insert(name.clone(), p.clone());
                }
            }
        }
    }

    /// Conflict items currently visible in this object.
    pub fn conflicts(&self, object: ObjectId) -> Vec<Conflict> {
        let mut out = Vec::new();
        let sessions_of = |r: &Reg| -> Vec<u64> {
            let mut s: Vec<u64> = r.visible().map(|(d, _, _)| d.actor.session).collect();
            s.sort_unstable();
            s.dedup();
            s
        };
        let has_true = self.life.visible().any(|(_, _, v)| *v == Value::Bool(true));
        let has_false = self.life.visible().any(|(_, _, v)| *v == Value::Bool(false));
        if has_true && has_false {
            out.push(Conflict {
                object,
                prop: "$life".to_string(),
                elem: None,
                kind: ConflictKind::DeleteVsEdit,
                winner: Value::Bool(true),
                others: vec![Value::Bool(false)],
                sessions: sessions_of(&self.life),
            });
        }
        if !self.is_alive() {
            return out;
        }
        for (name, p) in &self.props {
            match p {
                Prop::Reg(r) if r.is_conflicted() => {
                    let win = r.winner().cloned().unwrap_or(Value::Null);
                    out.push(Conflict {
                        object,
                        prop: name.clone(),
                        elem: None,
                        kind: ConflictKind::Register,
                        others: r.distinct_values().into_iter().filter(|v| **v != win).cloned().collect(),
                        winner: win,
                        sessions: sessions_of(r),
                    });
                }
                Prop::List(l) => {
                    for (id, e) in l.elems() {
                        if !e.deleted && e.value.is_conflicted() {
                            let win = e.value.winner().cloned().unwrap_or(Value::Null);
                            out.push(Conflict {
                                object,
                                prop: name.clone(),
                                elem: Some(*id),
                                kind: ConflictKind::ListElement,
                                others: e
                                    .value
                                    .distinct_values()
                                    .into_iter()
                                    .filter(|v| **v != win)
                                    .cloned()
                                    .collect(),
                                winner: win,
                                sessions: sessions_of(&e.value),
                            });
                        }
                    }
                }
                _ => {}
            }
        }
        out
    }

    /// The user-visible state: resolved values only, as a plain value tree.
    pub fn project(&self) -> Value {
        let mut m = BTreeMap::new();
        m.insert("alive".to_string(), Value::Bool(self.is_alive()));
        for (name, p) in &self.props {
            let v = match p {
                Prop::Reg(r) => r.winner().cloned().unwrap_or(Value::Null),
                Prop::Set(s) => Value::List(s.visible().cloned().collect()),
                Prop::List(l) => Value::List(l.values()),
            };
            m.insert(name.clone(), v);
        }
        Value::Map(m)
    }

    pub fn to_value(&self) -> Value {
        let mut m = BTreeMap::new();
        m.insert("life".to_string(), self.life.to_value());
        if !self.props.is_empty() {
            let mut pm = BTreeMap::new();
            for (name, p) in &self.props {
                let (tag, v) = match p {
                    Prop::Reg(r) => ("r", r.to_value()),
                    Prop::Set(s) => ("s", s.to_value()),
                    Prop::List(l) => ("l", l.to_value()),
                };
                pm.insert(name.clone(), Value::map([(tag, v)]));
            }
            m.insert("p".to_string(), Value::Map(pm));
        }
        Value::Map(m)
    }

    pub fn from_value(v: &Value) -> Result<Obj, CrdtError> {
        let Value::Map(m) = v else { return bad("object must be a map") };
        if m.keys().any(|k| k != "life" && k != "p") {
            return bad("unknown object field");
        }
        let life = Reg::from_value(m.get("life").map_or_else(|| bad("missing life"), Ok)?)?;
        let mut o = Obj { life, props: BTreeMap::new() };
        if let Some(p) = m.get("p") {
            let Value::Map(pm) = p else { return bad("props must be a map") };
            if pm.is_empty() {
                return bad("empty props map must be omitted");
            }
            for (name, pv) in pm {
                check_name(name)?;
                let Value::Map(one) = pv else { return bad("prop must be a tagged map") };
                if one.len() != 1 {
                    return bad("prop must have exactly one tag");
                }
                let (tag, inner) = one.iter().next().map_or_else(|| bad("empty prop"), Ok)?;
                let prop = match tag.as_str() {
                    "r" => Prop::Reg(Reg::from_value(inner)?),
                    "s" => Prop::Set(OrSet::from_value(inner)?),
                    "l" => Prop::List(RgaList::from_value(inner)?),
                    _ => return bad("unknown prop tag"),
                };
                o.props.insert(name.clone(), prop);
            }
        }
        Ok(o)
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        self.to_value().to_bytes()
    }

    pub fn from_bytes(b: &[u8]) -> Result<Obj, CrdtError> {
        let v = Value::from_bytes(b).map_err(|e| CrdtError::Decode(e.to_string()))?;
        Obj::from_value(&v)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn actor(s: u64) -> Actor {
        Actor { session: s, epoch: 0 }
    }

    fn dot(s: u64, seq: u32) -> Dot {
        Dot { actor: actor(s), seq }
    }

    #[test]
    fn register_sequential_writes_do_not_conflict() {
        let mut r = Reg::new();
        r.write(dot(1, 1), 1, Value::Int(1));
        r.write(dot(1, 2), 2, Value::Int(2));
        assert_eq!(r.winner(), Some(&Value::Int(2)));
        assert!(!r.is_conflicted());
        assert_eq!(r.floor_count(), 1);
    }

    #[test]
    fn register_concurrent_writes_conflict_and_resolve() {
        let mut base = Reg::new();
        base.write(dot(1, 1), 1, Value::Int(0));
        let (mut a, mut b) = (base.clone(), base.clone());
        a.write(dot(1, 2), 2, Value::Int(10));
        b.write(dot(2, 1), 2, Value::Int(20));
        let mut ab = a.clone();
        ab.merge(&b);
        let mut ba = b.clone();
        ba.merge(&a);
        assert_eq!(ab, ba);
        assert!(ab.is_conflicted());
        assert_eq!(ab.winner(), Some(&Value::Int(20))); // same lamport, higher session wins
        assert_eq!(ab.distinct_values().len(), 2);
        // a later write by anyone who saw both resolves it
        ab.write(dot(3, 1), 3, Value::Int(15));
        assert!(!ab.is_conflicted());
        // and a stale replica merged in cannot resurrect the old values
        ab.merge(&a);
        ab.merge(&b);
        assert_eq!(ab.winner(), Some(&Value::Int(15)));
        assert!(!ab.is_conflicted());
    }

    #[test]
    fn same_value_concurrent_writes_are_not_a_conflict() {
        let (mut a, mut b) = (Reg::new(), Reg::new());
        a.write(dot(1, 1), 1, Value::Int(7));
        b.write(dot(2, 1), 1, Value::Int(7));
        a.merge(&b);
        assert_eq!(a.visible().count(), 2);
        assert!(!a.is_conflicted());
    }

    #[test]
    fn set_add_wins_over_concurrent_remove() {
        let mut base = OrSet::new();
        base.add(Value::str("x"), dot(1, 1));
        let (mut a, mut b) = (base.clone(), base.clone());
        a.remove(&Value::str("x"));
        b.add(Value::str("x"), dot(2, 1));
        let mut ab = a.clone();
        ab.merge(&b);
        let mut ba = b.clone();
        ba.merge(&a);
        assert_eq!(ab, ba);
        assert!(ab.contains(&Value::str("x")));
        // remove after observing both wins
        ab.remove(&Value::str("x"));
        assert!(!ab.contains(&Value::str("x")));
        ab.merge(&a);
        ab.merge(&b);
        assert!(!ab.contains(&Value::str("x")));
    }

    #[test]
    fn set_remove_then_readd_is_sequential() {
        let mut s = OrSet::new();
        s.add(Value::Int(1), dot(1, 1));
        s.remove(&Value::Int(1));
        s.add(Value::Int(1), dot(1, 2));
        assert!(s.contains(&Value::Int(1)));
    }

    fn eid(lamport: u64, s: u64, seq: u32) -> ElemId {
        ElemId { lamport, dot: dot(s, seq), idx: 0 }
    }

    #[test]
    fn list_concurrent_inserts_keep_both_in_a_fixed_order() {
        let mut base = RgaList::new();
        let a0 = eid(1, 1, 1);
        base.insert(None, a0, Value::str("p0"));
        let (mut a, mut b) = (base.clone(), base.clone());
        a.insert(Some(a0), eid(2, 1, 2), Value::str("fromA"));
        b.insert(Some(a0), eid(2, 2, 1), Value::str("fromB"));
        let mut ab = a.clone();
        ab.merge(&b);
        let mut ba = b.clone();
        ba.merge(&a);
        assert_eq!(ab, ba);
        let vals: Vec<_> = ab.values().iter().map(|v| v.as_str().unwrap().to_string()).collect();
        assert_eq!(vals, ["p0", "fromB", "fromA"].map(String::from));
    }

    #[test]
    fn list_edit_in_place_keeps_identity() {
        let mut l = RgaList::new();
        let a0 = eid(1, 1, 1);
        l.insert(None, a0, Value::Int(1));
        let (mut x, mut y) = (l.clone(), l.clone());
        assert!(x.set(a0, dot(1, 2), 2, Value::Int(2)));
        assert!(y.set(a0, dot(2, 1), 2, Value::Int(3)));
        x.merge(&y);
        assert_eq!(x.len(), 1);
        assert!(x.elem(&a0).unwrap().value.is_conflicted());
    }

    #[test]
    fn object_delete_vs_edit_keeps_object_and_reports() {
        let id = ObjectId::from_u64(1);
        let mut base = Obj::new();
        base.set_alive(true, dot(1, 1), 1);
        base.reg_mut("color").unwrap().write(dot(1, 1), 1, Value::Int(1));
        let (mut a, mut b) = (base.clone(), base.clone());
        a.set_alive(false, dot(1, 2), 2);
        b.set_alive(true, dot(2, 1), 2);
        b.reg_mut("color").unwrap().write(dot(2, 1), 2, Value::Int(2));
        a.merge(&b);
        assert!(a.is_alive());
        let c = a.conflicts(id);
        assert_eq!(c.len(), 1);
        assert_eq!(c[0].kind, ConflictKind::DeleteVsEdit);
        // resolving: delete again after seeing both
        a.set_alive(false, dot(3, 1), 3);
        assert!(!a.is_alive());
        assert!(a.conflicts(id).is_empty());
    }

    #[test]
    fn object_snapshot_round_trip_is_canonical() {
        let mut o = Obj::new();
        o.set_alive(true, dot(1, 1), 1);
        o.reg_mut("pos").unwrap().write(dot(1, 1), 1, Value::f3([1.0, 2.0, 3.0]));
        o.reg_mut("pos").unwrap().write(dot(1, 2), 2, Value::f3([1.0, 2.0, 4.0]));
        o.set_mut("tags").unwrap().add(Value::str("a"), dot(1, 2));
        o.set_mut("tags").unwrap().add(Value::str("b"), dot(1, 2));
        o.set_mut("tags").unwrap().remove(&Value::str("a"));
        let l = o.list_mut("pts").unwrap();
        l.insert(None, eid(2, 1, 2), Value::f3([0.0; 3]));
        let bytes = o.to_bytes();
        let back = Obj::from_bytes(&bytes).unwrap();
        assert_eq!(back, o);
        assert_eq!(back.to_bytes(), bytes);
        let j = o.to_value().to_json();
        assert_eq!(Obj::from_value(&Value::from_json(&j).unwrap()).unwrap(), o);
    }

    #[test]
    fn kind_mismatch_is_reported_and_merge_stays_commutative() {
        let mut o = Obj::new();
        o.reg_mut("x").unwrap();
        assert!(o.set_mut("x").is_err());
        assert!(o.reg_mut("$bad").is_err());
        let mut a = Obj::new();
        a.reg_mut("x").unwrap().write(dot(1, 1), 1, Value::Int(1));
        let mut b = Obj::new();
        b.set_mut("x").unwrap().add(Value::Int(1), dot(2, 1));
        let mut ab = a.clone();
        ab.merge(&b);
        let mut ba = b.clone();
        ba.merge(&a);
        assert_eq!(ab, ba);
    }

    #[test]
    fn decoder_rejects_unnormalized_state() {
        // an entry below its own floor must be rejected
        let v = Value::map([
            (
                "e",
                Value::List(vec![Value::List(vec![
                    Value::Int(1),
                    Value::Int(0),
                    Value::Int(1),
                    Value::Int(1),
                    Value::Int(5),
                ])]),
            ),
            ("f", Value::List(vec![Value::List(vec![Value::Int(1), Value::Int(0), Value::Int(1)])])),
        ]);
        assert!(Reg::from_value(&v).is_err());
    }
}
