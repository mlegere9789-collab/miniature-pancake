//! Property tests for the CRDT part: the merge of every property type is a semilattice join
//! (commutative, associative, idempotent), replicas converge, and the compact floor representation
//! agrees with an independent oracle that records every observed set explicitly.

use oplog::crdt::{ElemId, Obj, OrSet, Reg, RgaList};
use oplog::ids::{Actor, Dot, ObjectId};
use oplog::value::Value;
use proptest::prelude::*;
use std::collections::{BTreeMap, BTreeSet};

fn cases() -> ProptestConfig {
    ProptestConfig { cases: if cfg!(debug_assertions) { 96 } else { 400 }, ..ProptestConfig::default() }
}

const R: usize = 3;

fn actor(r: usize) -> Actor {
    Actor { session: r as u64 + 1, epoch: 0 }
}

/// The three semilattice laws plus absorption, for any join.
fn check_laws<T: Clone + PartialEq + std::fmt::Debug>(x: &T, y: &T, z: &T, join: impl Fn(&T, &T) -> T) {
    let xy = join(x, y);
    let yx = join(y, x);
    assert_eq!(xy, yx, "commutativity");
    assert_eq!(join(&xy, z), join(x, &join(y, z)), "associativity");
    assert_eq!(join(x, x), *x, "idempotence");
    assert_eq!(join(&xy, x), xy, "absorption");
    assert_eq!(join(&xy, &xy), xy, "idempotence of a join");
}

// ---------------------------------------------------------------------------------------------
// Register
// ---------------------------------------------------------------------------------------------

#[derive(Debug, Clone)]
enum RegAct {
    Write { r: usize, val: i64 },
    Merge { dst: usize, src: usize },
}

fn reg_acts() -> impl Strategy<Value = Vec<RegAct>> {
    prop::collection::vec(
        prop_oneof![
            3 => (0..R, 0i64..4).prop_map(|(r, val)| RegAct::Write { r, val }),
            2 => (0..R, 0..R).prop_map(|(dst, src)| RegAct::Merge { dst, src }),
        ],
        1..40,
    )
}

#[derive(Clone, Default)]
struct RegRep {
    state: Reg,
    lam: u64,
    seq: u32,
    known: BTreeSet<Dot>,
}

fn run_reg(acts: &[RegAct]) -> Vec<RegRep> {
    let mut reps = vec![RegRep::default(); R];
    // oracle: the dots each write observed (recorded explicitly, no floors)
    let mut obs: BTreeMap<Dot, BTreeSet<Dot>> = BTreeMap::new();
    let visible_model = |rep: &RegRep, obs: &BTreeMap<Dot, BTreeSet<Dot>>| -> BTreeSet<Dot> {
        let dead: BTreeSet<Dot> = rep.known.iter().flat_map(|w| obs[w].iter().copied()).collect();
        rep.known.difference(&dead).copied().collect()
    };
    for a in acts {
        match a {
            RegAct::Write { r, val } => {
                let vis = visible_model(&reps[*r], &obs);
                let rep = &mut reps[*r];
                rep.seq += 1;
                rep.lam += 1;
                let dot = Dot { actor: actor(*r), seq: rep.seq };
                rep.state.write(dot, rep.lam, Value::Int(*val));
                rep.known.insert(dot);
                obs.insert(dot, vis);
            }
            RegAct::Merge { dst, src } => {
                let s = reps[*src].clone();
                let d = &mut reps[*dst];
                d.state.merge(&s.state);
                d.known.extend(s.known.iter().copied());
                d.lam = d.lam.max(s.lam);
            }
        }
    }
    for rep in &reps {
        let imp: BTreeSet<Dot> = rep.state.visible().map(|(d, _, _)| *d).collect();
        assert_eq!(imp, visible_model(rep, &obs), "visible set must match the oracle");
    }
    reps
}

proptest! {
    #![proptest_config(cases())]

    #[test]
    fn register_laws_and_oracle(acts in reg_acts()) {
        let reps = run_reg(&acts);
        check_laws(&reps[0].state, &reps[1].state, &reps[2].state, |a, b| { let mut c = a.clone(); c.merge(b); c });
    }

    #[test]
    fn register_encoding_round_trips(acts in reg_acts()) {
        for rep in run_reg(&acts) {
            let v = rep.state.to_value();
            prop_assert_eq!(Reg::from_value(&v).unwrap(), rep.state.clone());
            // canonical bytes: equal states give equal encodings
            let again = Reg::from_value(&Value::from_bytes(&v.to_bytes()).unwrap()).unwrap();
            prop_assert_eq!(again.to_value().to_bytes(), v.to_bytes());
        }
    }

    /// Floors keep register state bounded by the number of writers, however many edits.
    #[test]
    fn register_state_is_bounded_by_actors(n in 1usize..200) {
        let mut r = Reg::new();
        for i in 0..n {
            r.write(Dot { actor: actor(0), seq: i as u32 + 1 }, i as u64 + 1, Value::Int(i as i64));
        }
        prop_assert_eq!(r.visible().count(), 1);
        prop_assert_eq!(r.floor_count(), usize::from(n > 1));
    }
}

// ---------------------------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------------------------

#[derive(Debug, Clone)]
enum SetAct {
    Add { r: usize, e: i64 },
    Remove { r: usize, e: i64 },
    Merge { dst: usize, src: usize },
}

fn set_acts() -> impl Strategy<Value = Vec<SetAct>> {
    prop::collection::vec(
        prop_oneof![
            3 => (0..R, 0i64..3).prop_map(|(r, e)| SetAct::Add { r, e }),
            2 => (0..R, 0i64..3).prop_map(|(r, e)| SetAct::Remove { r, e }),
            2 => (0..R, 0..R).prop_map(|(dst, src)| SetAct::Merge { dst, src }),
        ],
        1..50,
    )
}

#[derive(Clone, Default)]
struct SetRep {
    state: OrSet,
    seq: u32,
    /// oracle: (element, add dot) pairs known, and the dots every remove/add observed
    adds: BTreeSet<(i64, Dot)>,
    killed: BTreeSet<Dot>,
}

fn run_set(acts: &[SetAct]) -> Vec<SetRep> {
    let mut reps = vec![SetRep::default(); R];
    for a in acts {
        match a {
            SetAct::Add { r, e } => {
                let rep = &mut reps[*r];
                rep.seq += 1;
                let dot = Dot { actor: actor(*r), seq: rep.seq };
                // an add supersedes the adds of the same element it observed
                let seen: Vec<Dot> = rep.adds.iter().filter(|(el, d)| el == e && !rep.killed.contains(d)).map(|(_, d)| *d).collect();
                rep.killed.extend(seen);
                rep.adds.insert((*e, dot));
                rep.state.add(Value::Int(*e), dot);
            }
            SetAct::Remove { r, e } => {
                let rep = &mut reps[*r];
                let seen: Vec<Dot> = rep.adds.iter().filter(|(el, d)| el == e && !rep.killed.contains(d)).map(|(_, d)| *d).collect();
                rep.killed.extend(seen);
                rep.state.remove(&Value::Int(*e));
            }
            SetAct::Merge { dst, src } => {
                let s = reps[*src].clone();
                let d = &mut reps[*dst];
                d.state.merge(&s.state);
                d.adds.extend(s.adds.iter().copied());
                d.killed.extend(s.killed.iter().copied());
            }
        }
    }
    for rep in &reps {
        let model: BTreeSet<i64> = rep.adds.iter().filter(|(_, d)| !rep.killed.contains(d)).map(|(e, _)| *e).collect();
        let imp: BTreeSet<i64> = rep.state.visible().map(|v| v.as_i64().unwrap()).collect();
        assert_eq!(imp, model, "visible elements must match the oracle (add wins)");
    }
    reps
}

proptest! {
    #![proptest_config(cases())]

    #[test]
    fn set_laws_and_oracle(acts in set_acts()) {
        let reps = run_set(&acts);
        check_laws(&reps[0].state, &reps[1].state, &reps[2].state, |a, b| { let mut c = a.clone(); c.merge(b); c });
    }

    #[test]
    fn set_encoding_round_trips(acts in set_acts()) {
        for rep in run_set(&acts) {
            let v = rep.state.to_value();
            prop_assert_eq!(OrSet::from_value(&v).unwrap(), rep.state.clone());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------------------------

#[derive(Debug, Clone)]
enum ListAct {
    Insert { r: usize, pos: usize, val: i64 },
    Delete { r: usize, pos: usize },
    Edit { r: usize, pos: usize, val: i64 },
    Merge { dst: usize, src: usize },
}

fn list_acts() -> impl Strategy<Value = Vec<ListAct>> {
    prop::collection::vec(
        prop_oneof![
            4 => (0..R, 0usize..6, 0i64..4).prop_map(|(r, pos, val)| ListAct::Insert { r, pos, val }),
            1 => (0..R, 0usize..6).prop_map(|(r, pos)| ListAct::Delete { r, pos }),
            1 => (0..R, 0usize..6, 0i64..4).prop_map(|(r, pos, val)| ListAct::Edit { r, pos, val }),
            2 => (0..R, 0..R).prop_map(|(dst, src)| ListAct::Merge { dst, src }),
        ],
        1..50,
    )
}

#[derive(Clone, Default)]
struct ListRep {
    state: RgaList,
    lam: u64,
    seq: u32,
    inserted: BTreeSet<ElemId>,
    deleted: BTreeSet<ElemId>,
}

fn run_list(acts: &[ListAct]) -> Vec<ListRep> {
    let mut reps = vec![ListRep::default(); R];
    for a in acts {
        match a {
            ListAct::Insert { r, pos, val } => {
                let rep = &mut reps[*r];
                let vis = rep.state.visible_ids();
                let at = (*pos).min(vis.len());
                let after = if at == 0 { None } else { Some(vis[at - 1]) };
                rep.seq += 1;
                rep.lam += 1;
                let id = ElemId { lamport: rep.lam, dot: Dot { actor: actor(*r), seq: rep.seq }, idx: 0 };
                rep.state.insert(after, id, Value::Int(*val));
                rep.inserted.insert(id);
                // the new element must land exactly where it was asked for in the local view
                let now = rep.state.visible_ids();
                assert_eq!(now.iter().position(|x| *x == id), Some(at), "insert lands at the requested index");
            }
            ListAct::Delete { r, pos } => {
                let rep = &mut reps[*r];
                let vis = rep.state.visible_ids();
                if let Some(id) = vis.get(*pos) {
                    rep.state.delete(*id);
                    rep.deleted.insert(*id);
                }
            }
            ListAct::Edit { r, pos, val } => {
                let rep = &mut reps[*r];
                let vis = rep.state.visible_ids();
                if let Some(id) = vis.get(*pos) {
                    rep.seq += 1;
                    rep.lam += 1;
                    let dot = Dot { actor: actor(*r), seq: rep.seq };
                    assert!(rep.state.set(*id, dot, rep.lam, Value::Int(*val)));
                }
            }
            ListAct::Merge { dst, src } => {
                let s = reps[*src].clone();
                let d = &mut reps[*dst];
                let before = d.state.visible_ids();
                d.state.merge(&s.state);
                d.inserted.extend(s.inserted.iter().copied());
                d.deleted.extend(s.deleted.iter().copied());
                d.lam = d.lam.max(s.lam);
                // merging never reorders elements the replica already had
                let after = d.state.visible_ids();
                let kept: Vec<_> = before.iter().filter(|id| after.contains(id)).collect();
                let sub: Vec<_> = after.iter().filter(|id| before.contains(id)).collect();
                assert_eq!(kept, sub, "merge preserves the existing relative order");
            }
        }
    }
    for rep in &reps {
        let model: BTreeSet<ElemId> = rep.inserted.difference(&rep.deleted).copied().collect();
        let imp: BTreeSet<ElemId> = rep.state.visible_ids().into_iter().collect();
        assert_eq!(imp, model, "visible elements = inserted minus deleted");
    }
    reps
}

proptest! {
    #![proptest_config(cases())]

    #[test]
    fn list_laws_and_order(acts in list_acts()) {
        let reps = run_list(&acts);
        check_laws(&reps[0].state, &reps[1].state, &reps[2].state, |a, b| { let mut c = a.clone(); c.merge(b); c });
        // full convergence: merging in two different orders yields the same sequence
        let join = |a: &RgaList, b: &RgaList| { let mut c = a.clone(); c.merge(b); c };
        let abc = join(&join(&reps[0].state, &reps[1].state), &reps[2].state);
        let cba = join(&join(&reps[2].state, &reps[1].state), &reps[0].state);
        prop_assert_eq!(abc.values(), cba.values());
    }

    #[test]
    fn list_encoding_round_trips(acts in list_acts()) {
        for rep in run_list(&acts) {
            let v = rep.state.to_value();
            prop_assert_eq!(RgaList::from_value(&v).unwrap(), rep.state.clone());
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Whole object: registers, sets, lists and the life flag together
// ---------------------------------------------------------------------------------------------

#[derive(Debug, Clone)]
enum ObjAct {
    SetReg { r: usize, name: u8, val: i64 },
    TagAdd { r: usize, e: i64 },
    TagRemove { r: usize, e: i64 },
    PtInsert { r: usize, pos: usize, val: i64 },
    PtEdit { r: usize, pos: usize, val: i64 },
    Life { r: usize, alive: bool },
    Merge { dst: usize, src: usize },
}

fn obj_acts() -> impl Strategy<Value = Vec<ObjAct>> {
    prop::collection::vec(
        prop_oneof![
            3 => (0..R, 0u8..2, 0i64..3).prop_map(|(r, name, val)| ObjAct::SetReg { r, name, val }),
            1 => (0..R, 0i64..3).prop_map(|(r, e)| ObjAct::TagAdd { r, e }),
            1 => (0..R, 0i64..3).prop_map(|(r, e)| ObjAct::TagRemove { r, e }),
            2 => (0..R, 0usize..4, 0i64..3).prop_map(|(r, pos, val)| ObjAct::PtInsert { r, pos, val }),
            1 => (0..R, 0usize..4, 0i64..3).prop_map(|(r, pos, val)| ObjAct::PtEdit { r, pos, val }),
            1 => (0..R, any::<bool>()).prop_map(|(r, alive)| ObjAct::Life { r, alive }),
            3 => (0..R, 0..R).prop_map(|(dst, src)| ObjAct::Merge { dst, src }),
        ],
        1..60,
    )
}

#[derive(Clone, Default)]
struct ObjRep {
    obj: Obj,
    lam: u64,
    seq: u32,
}

fn run_obj(acts: &[ObjAct]) -> Vec<ObjRep> {
    let mut reps = vec![ObjRep::default(); R];
    for a in acts {
        let r = match a {
            ObjAct::Merge { dst, src } => {
                let s = reps[*src].clone();
                let d = &mut reps[*dst];
                d.obj.merge(&s.obj);
                d.lam = d.lam.max(s.lam);
                continue;
            }
            ObjAct::SetReg { r, .. }
            | ObjAct::TagAdd { r, .. }
            | ObjAct::TagRemove { r, .. }
            | ObjAct::PtInsert { r, .. }
            | ObjAct::PtEdit { r, .. }
            | ObjAct::Life { r, .. } => *r,
        };
        let rep = &mut reps[r];
        rep.seq += 1;
        rep.lam += 1;
        let dot = Dot { actor: actor(r), seq: rep.seq };
        match a {
            ObjAct::SetReg { name, val, .. } => {
                rep.obj.reg_mut(if *name == 0 { "a" } else { "b" }).unwrap().write(dot, rep.lam, Value::Int(*val));
            }
            ObjAct::TagAdd { e, .. } => rep.obj.set_mut("tags").unwrap().add(Value::Int(*e), dot),
            ObjAct::TagRemove { e, .. } => rep.obj.set_mut("tags").unwrap().remove(&Value::Int(*e)),
            ObjAct::PtInsert { pos, val, .. } => {
                let l = rep.obj.list_mut("pts").unwrap();
                let vis = l.visible_ids();
                let at = (*pos).min(vis.len());
                let after = if at == 0 { None } else { Some(vis[at - 1]) };
                l.insert(after, ElemId { lamport: rep.lam, dot, idx: 0 }, Value::Int(*val));
            }
            ObjAct::PtEdit { pos, val, .. } => {
                let l = rep.obj.list_mut("pts").unwrap();
                if let Some(id) = l.visible_ids().get(*pos) {
                    l.set(*id, dot, rep.lam, Value::Int(*val));
                }
            }
            ObjAct::Life { alive, .. } => rep.obj.set_alive(*alive, dot, rep.lam),
            ObjAct::Merge { .. } => unreachable!(),
        }
    }
    reps
}

proptest! {
    #![proptest_config(cases())]

    #[test]
    fn object_laws_and_canonical_bytes(acts in obj_acts()) {
        let reps = run_obj(&acts);
        let join = |a: &Obj, b: &Obj| { let mut c = a.clone(); c.merge(b); c };
        check_laws(&reps[0].obj, &reps[1].obj, &reps[2].obj, join);
        // equal states have equal snapshot bytes (so equal content hashes), whatever the merge order
        let abc = join(&join(&reps[0].obj, &reps[1].obj), &reps[2].obj);
        let cab = join(&join(&reps[2].obj, &reps[0].obj), &reps[1].obj);
        prop_assert_eq!(abc.to_bytes(), cab.to_bytes());
        // and conflict items are a pure function of the merged state
        let id = ObjectId::from_u64(1);
        prop_assert_eq!(abc.conflicts(id), cab.conflicts(id));
        // snapshot round trip
        for rep in &reps {
            prop_assert_eq!(Obj::from_bytes(&rep.obj.to_bytes()).unwrap(), rep.obj.clone());
        }
    }
}
