//! Model-based property tests: random operation sequences are applied to the
//! store and to a simple reference model; snapshots taken along the way must
//! keep matching the model state at snapshot time (isolation), and every
//! snapshot and the final state must pass the relationship integrity check.

mod common;
use common::*;
use proptest::prelude::*;
use dino8_scene::*;
use std::collections::{BTreeMap, BTreeSet};

const N: u32 = 24;

#[derive(Debug, Clone)]
enum Op {
    Create(u32, u16),
    Delete(u32),
    SetGeom(u32, u8, u8),
    SetSem(u32, u8, u8),
    SetProp(u32, u8),
    Link(u8, u32, u32),
    Unlink(u8, u32, u32),
    Constraint(u32, u32, u8),
    Translate(u32, u8),
    Snapshot,
    Refresh,
    Rollback,
}

fn op() -> impl Strategy<Value = Op> {
    let l = 0..N;
    prop_oneof![
        4 => (l.clone(), 0u16..4).prop_map(|(a, c)| Op::Create(a, c)),
        2 => l.clone().prop_map(Op::Delete),
        3 => (l.clone(), 0u8..20, 0u8..20).prop_map(|(a, x, y)| Op::SetGeom(a, x, y)),
        3 => (l.clone(), 0u8..4, 0u8..3).prop_map(|(a, c, k)| Op::SetSem(a, c, k)),
        2 => (l.clone(), 0u8..10).prop_map(|(a, v)| Op::SetProp(a, v)),
        6 => (0u8..5, l.clone(), l.clone()).prop_map(|(k, a, b)| Op::Link(k, a, b)),
        2 => (0u8..5, l.clone(), l.clone()).prop_map(|(k, a, b)| Op::Unlink(k, a, b)),
        2 => (l.clone(), l.clone(), 0u8..3).prop_map(|(a, b, v)| Op::Constraint(a, b, v)),
        2 => (l.clone(), 0u8..9).prop_map(|(a, d)| Op::Translate(a, d)),
        2 => Just(Op::Snapshot),
        1 => Just(Op::Refresh),
        1 => Just(Op::Rollback),
    ]
}

type ModelEnt = (Option<Geometry>, Option<Semantic>, Option<u64>);

#[derive(Clone, Default, Debug)]
struct Model {
    ents: BTreeMap<u32, ModelEnt>,
    hosting: BTreeMap<u32, u32>,
    containment: BTreeMap<u32, u32>,
    connection: BTreeSet<(u32, u32)>,
    reference: BTreeSet<(u32, u32)>,
    constraint: BTreeMap<(u32, u32), (u32, f64)>,
}

fn geom(x: u8, y: u8) -> Geometry {
    Geometry { bbox: Aabb::new([x as f32, y as f32, 0.0], [x as f32 + 3.0, y as f32 + 3.0, 2.0]), handle: x as u64 * 100 + y as u64 }
}

fn semv(c: u8, k: u8) -> Semantic {
    Semantic { class: c as u16, ptype: 0, classification: k as u32, phase: 0, option: 0 }
}

impl Model {
    fn remove_ent(&mut self, l: u32) {
        self.ents.remove(&l);
        self.hosting.retain(|a, b| *a != l && *b != l);
        self.containment.retain(|a, b| *a != l && *b != l);
        self.connection.retain(|(a, b)| *a != l && *b != l);
        self.reference.retain(|(a, b)| *a != l && *b != l);
        self.constraint.retain(|(a, b), _| *a != l && *b != l);
    }
    fn apply(&mut self, op: &Op) -> Result<(), SceneError> {
        let has = |m: &Model, l: u32| m.ents.contains_key(&l);
        match *op {
            Op::Create(a, c) => {
                if has(self, a) {
                    return Err(SceneError::DuplicateUuid(uid(a)));
                }
                self.ents.insert(a, (Some(geom(c as u8, 0)), Some(semv(c as u8, 0)), None));
            }
            Op::Delete(a) => {
                if !has(self, a) {
                    return Err(SceneError::UnknownEntity(uid(a)));
                }
                let mut victims = vec![a];
                let mut i = 0;
                while i < victims.len() {
                    let v = victims[i];
                    i += 1;
                    victims.extend(self.hosting.iter().filter(|(_, h)| **h == v).map(|(c, _)| *c));
                }
                for v in victims {
                    self.remove_ent(v);
                }
            }
            Op::SetGeom(a, x, y) => {
                if let Some(e) = self.ents.get_mut(&a) {
                    e.0 = Some(geom(x, y));
                }
            }
            Op::SetSem(a, c, k) => {
                if let Some(e) = self.ents.get_mut(&a) {
                    e.1 = Some(semv(c, k));
                }
            }
            Op::SetProp(a, v) => {
                if let Some(e) = self.ents.get_mut(&a) {
                    e.2 = Some((v as f64).to_bits());
                }
            }
            Op::Translate(a, d) => {
                if let Some(e) = self.ents.get_mut(&a) {
                    if let Some(g) = e.0.as_mut() {
                        g.bbox = g.bbox.translated([d as f32, 0.0, 0.0]);
                    }
                }
            }
            Op::Link(k, a, b) => {
                if !has(self, a) {
                    return Err(SceneError::UnknownEntity(uid(a)));
                }
                if !has(self, b) {
                    return Err(SceneError::UnknownEntity(uid(b)));
                }
                if a == b {
                    return Err(SceneError::SelfRelation);
                }
                match k {
                    0 | 1 => {
                        let map = if k == 0 { &mut self.hosting } else { &mut self.containment };
                        let mut cur = b;
                        while let Some(&p) = map.get(&cur) {
                            if p == a {
                                return Err(SceneError::Cycle);
                            }
                            cur = p;
                        }
                        map.insert(a, b);
                    }
                    2 => {
                        self.connection.insert((a.min(b), a.max(b)));
                    }
                    3 => {
                        self.constraint.insert((a, b), (CT_DISTANCE, 0.0));
                    }
                    _ => {
                        self.reference.insert((a, b));
                    }
                }
            }
            Op::Unlink(k, a, b) => {
                if !has(self, a) {
                    return Err(SceneError::UnknownEntity(uid(a)));
                }
                if !has(self, b) {
                    return Err(SceneError::UnknownEntity(uid(b)));
                }
                match k {
                    0 => {
                        if self.hosting.get(&a) == Some(&b) {
                            self.hosting.remove(&a);
                        }
                    }
                    1 => {
                        if self.containment.get(&a) == Some(&b) {
                            self.containment.remove(&a);
                        }
                    }
                    2 => {
                        self.connection.remove(&(a.min(b), a.max(b)));
                    }
                    3 => {
                        self.constraint.remove(&(a, b));
                    }
                    _ => {
                        self.reference.remove(&(a, b));
                    }
                }
            }
            Op::Constraint(a, b, v) => {
                if !has(self, a) {
                    return Err(SceneError::UnknownEntity(uid(a)));
                }
                if !has(self, b) {
                    return Err(SceneError::UnknownEntity(uid(b)));
                }
                if a == b {
                    return Err(SceneError::SelfRelation);
                }
                self.constraint.insert((a, b), (CT_FIXED_OFFSET + v as u32 % 2, v as f64 * 0.5));
            }
            Op::Snapshot | Op::Refresh | Op::Rollback => {}
        }
        Ok(())
    }

    fn dump(&self) -> Dump {
        let mut d = Dump::default();
        for (l, e) in &self.ents {
            d.ents.insert(*l, (e.0, e.1, None, None, e.2));
        }
        d.hosting = self.hosting.iter().map(|(a, b)| (*a, *b)).collect();
        d.containment = self.containment.iter().map(|(a, b)| (*a, *b)).collect();
        d.connection = self.connection.clone();
        d.reference = self.reference.clone();
        d.constraint = self.constraint.iter().map(|(k, (c, v))| (*k, (*c, v.to_bits()))).collect();
        d
    }
}

fn apply_scene(s: &mut Scene, pk: PropKey, op: &Op) -> Result<(), SceneError> {
    let ix = |s: &Scene, l: u32| s.ix_of(uid(l));
    match *op {
        Op::Create(a, c) => {
            s.create(uid(a), EntityParts { geometry: Some(geom(c as u8, 0)), semantic: Some(semv(c as u8, 0)), ..Default::default() }).map(|_| ())
        }
        Op::Delete(a) => s.delete(uid(a)).map(|_| ()),
        Op::SetGeom(a, x, y) => {
            if let Some(i) = ix(s, a) {
                s.set_geometry(i, geom(x, y));
            }
            Ok(())
        }
        Op::SetSem(a, c, k) => {
            if let Some(i) = ix(s, a) {
                s.set_semantic(i, semv(c, k));
            }
            Ok(())
        }
        Op::SetProp(a, v) => {
            if let Some(i) = ix(s, a) {
                s.set_prop(i, pk, PropVal::F64(v as f64))?;
            }
            Ok(())
        }
        Op::Translate(a, d) => {
            if let Some(i) = ix(s, a) {
                s.translate(i, [d as f32, 0.0, 0.0]);
            }
            Ok(())
        }
        Op::Link(k, a, b) => s.link(RelKind::from_u8(k).unwrap(), uid(a), uid(b)),
        Op::Unlink(k, a, b) => s.unlink(RelKind::from_u8(k).unwrap(), uid(a), uid(b)).map(|_| ()),
        Op::Constraint(a, b, v) => s.add_constraint(uid(a), uid(b), CT_FIXED_OFFSET + v as u32 % 2, v as f64 * 0.5),
        Op::Snapshot | Op::Refresh | Op::Rollback => Ok(()),
    }
}

fn check_queries(s: &Scene, m: &Model, pk: PropKey) {
    // by class
    for c in 0..4u16 {
        let mut got: Vec<u32> = s.by_class(c).into_iter().map(|i| label_of(s.uuid_of(i).unwrap())).collect();
        got.sort();
        let want: Vec<u32> = m.ents.iter().filter(|(_, e)| e.1.map(|x| x.class) == Some(c)).map(|(l, _)| *l).collect();
        assert_eq!(got, want, "class {c}");
    }
    // classification
    for k in 1..3u32 {
        let mut got: Vec<u32> = s.by_classification(k).into_iter().map(|i| label_of(s.uuid_of(i).unwrap())).collect();
        got.sort();
        let want: Vec<u32> = m.ents.iter().filter(|(_, e)| e.1.map(|x| x.classification) == Some(k)).map(|(l, _)| *l).collect();
        assert_eq!(got, want, "classification {k}");
    }
    // property range
    let mut got: Vec<u32> = s.prop_range_f64(pk, 2.0, 5.0).into_iter().map(|i| label_of(s.uuid_of(i).unwrap())).collect();
    got.sort();
    let want: Vec<u32> = m
        .ents
        .iter()
        .filter(|(_, e)| e.2.map(|r| (2.0..=5.0).contains(&f64::from_bits(r))).unwrap_or(false))
        .map(|(l, _)| *l)
        .collect();
    assert_eq!(got, want);
    // spatial
    let q = Aabb::new([4.0, 4.0, -1.0], [12.0, 12.0, 3.0]);
    let mut got: Vec<u32> = s.query_aabb(&q).into_iter().map(|i| label_of(s.uuid_of(i).unwrap())).collect();
    got.sort();
    let want: Vec<u32> = m.ents.iter().filter(|(_, e)| e.0.map(|g| g.bbox.intersects(&q)).unwrap_or(false)).map(|(l, _)| *l).collect();
    assert_eq!(got, want, "aabb");
    // descendants through hosting equals model closure
    for root in 0..N {
        if let Some(ix) = s.ix_of(uid(root)) {
            let mut got: Vec<u32> = s.descendants(RelKind::Containment, ix).into_iter().map(|i| label_of(s.uuid_of(i).unwrap())).collect();
            got.sort();
            let mut want = vec![];
            let mut frontier = vec![root];
            while let Some(c) = frontier.pop() {
                for (k, p) in &m.containment {
                    if *p == c {
                        want.push(*k);
                        frontier.push(*k);
                    }
                }
            }
            want.sort();
            assert_eq!(got, want, "descendants of {root}");
        }
    }
}

fn run(ops: Vec<Op>) {
    let mut s = Scene::new(Config::default());
    let pk = s.register_prop("P", PropKind::F64);
    let mut m = Model::default();
    let mut snaps: Vec<(Scene, Dump)> = Vec::new();
    for op in &ops {
        match op {
            Op::Snapshot => snaps.push((s.clone(), m.dump())),
            Op::Refresh => s.refresh_spatial(),
            Op::Rollback => {
                if let Some((ps, pd)) = snaps.last() {
                    // restore the most recent snapshot as the current state
                    s = ps.clone();
                    let mut nm = Model::default();
                    // rebuild model from the dump
                    for (l, e) in &pd.ents {
                        nm.ents.insert(*l, (e.0, e.1, e.4));
                    }
                    nm.hosting = pd.hosting.iter().copied().collect();
                    nm.containment = pd.containment.iter().copied().collect();
                    nm.connection = pd.connection.clone();
                    nm.reference = pd.reference.clone();
                    nm.constraint = pd.constraint.iter().map(|(k, (c, v))| (*k, (*c, f64::from_bits(*v)))).collect();
                    m = nm;
                }
            }
            _ => {
                let r1 = apply_scene(&mut s, pk, op);
                let r2 = m.apply(op);
                assert_eq!(r1, r2, "result mismatch on {op:?}");
            }
        }
        assert_eq!(dump_scene(&s, N, Some(pk)), m.dump(), "state diverged after {op:?}");
    }
    s.check_integrity().expect("final integrity");
    check_queries(&s, &m, pk);
    for (i, (snap, dump)) in snaps.iter().enumerate() {
        snap.check_integrity().unwrap_or_else(|e| panic!("snapshot {i}: {e}"));
        assert_eq!(&dump_scene(snap, N, Some(pk)), dump, "snapshot {i} drifted");
    }
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 300, max_shrink_iters: 2000, .. ProptestConfig::default() })]

    #[test]
    fn snapshots_isolated_and_relationships_intact(ops in prop::collection::vec(op(), 1..160)) {
        run(ops);
    }
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 40, .. ProptestConfig::default() })]

    /// Persistent containers against std collections at larger scale.
    #[test]
    fn sorted_set_equivalent_to_btreeset(ops in prop::collection::vec((any::<bool>(), 0u64..3000), 1..4000)) {
        use dino8_scene::sorted::Sorted;
        let mut s: Sorted<u64> = Sorted::new();
        let mut m = BTreeSet::new();
        let mut snaps = vec![];
        for (i, (ins, k)) in ops.into_iter().enumerate() {
            if ins { prop_assert_eq!(s.insert(k).is_some(), !m.insert(k)); } else { prop_assert_eq!(s.remove(k).is_some(), m.remove(&k)); }
            if i % 700 == 0 { snaps.push((s.clone(), m.clone())); }
        }
        s.check().unwrap();
        prop_assert_eq!(s.iter().collect::<Vec<_>>(), m.iter().copied().collect::<Vec<_>>());
        for (a, b) in snaps {
            a.check().unwrap();
            prop_assert_eq!(a.iter().collect::<Vec<_>>(), b.iter().copied().collect::<Vec<_>>());
        }
    }
}
