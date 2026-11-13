mod common;
use common::*;
use dino8_scene::rng::Rng;
use dino8_scene::*;

fn boxed(x: f32, y: f32, z: f32) -> Geometry {
    Geometry { bbox: Aabb::new([x, y, z], [x + 1.0, y + 1.0, z + 1.0]), handle: 7 }
}

fn sem(class: u16) -> Semantic {
    Semantic { class, ptype: 1, phase: 0, option: 0, classification: 0 }
}

fn mk(s: &mut Scene, l: u32, class: u16) -> Ix {
    s.create(uid(l), EntityParts { geometry: Some(boxed(l as f32 * 3.0, 0.0, 0.0)), semantic: Some(sem(class)), ..Default::default() })
        .unwrap()
}

#[test]
fn identity_and_duplicates() {
    let mut s = Scene::new(Config::default());
    let ix = mk(&mut s, 1, 5);
    assert_eq!(s.ix_of(uid(1)), Some(ix));
    assert_eq!(s.uuid_of(ix), Some(uid(1)));
    assert_eq!(s.create(uid(1), EntityParts::default()), Err(SceneError::DuplicateUuid(uid(1))));
    assert_eq!(s.len(), 1);
    s.check_integrity().unwrap();
}

#[test]
fn hosting_cascade_and_slot_reuse() {
    let mut s = Scene::new(Config::default());
    let wall = mk(&mut s, 0, 1);
    let door = mk(&mut s, 1, 2);
    let handle = mk(&mut s, 2, 3);
    let other = mk(&mut s, 3, 1);
    s.link_ix(RelKind::Hosting, door, wall).unwrap();
    s.link_ix(RelKind::Hosting, handle, door).unwrap();
    s.link_ix(RelKind::Containment, wall, other).unwrap();
    s.link_ix(RelKind::Connection, wall, other).unwrap();
    s.check_integrity().unwrap();
    let gone = s.delete(uid(0)).unwrap();
    assert_eq!(gone.len(), 3);
    assert!(s.contains(uid(3)) && !s.contains(uid(1)));
    assert!(s.contents_of(other).is_empty());
    assert!(s.connected(other).is_empty());
    s.check_integrity().unwrap();
    // freed slots are reused and stay consistent
    let n = mk(&mut s, 9, 4);
    assert!(n < 4);
    s.check_integrity().unwrap();
}

#[test]
fn single_parent_replace_and_cycles() {
    let mut s = Scene::new(Config::default());
    let a = mk(&mut s, 0, 1);
    let b = mk(&mut s, 1, 1);
    let c = mk(&mut s, 2, 1);
    s.link_ix(RelKind::Containment, a, b).unwrap();
    s.link_ix(RelKind::Containment, a, c).unwrap();
    assert_eq!(s.container_of(a), Some(c));
    assert!(s.contents_of(b).is_empty());
    assert_eq!(s.link_ix(RelKind::Containment, c, a), Err(SceneError::Cycle));
    assert_eq!(s.link_ix(RelKind::Hosting, a, a), Err(SceneError::SelfRelation));
    s.link_ix(RelKind::Hosting, b, a).unwrap();
    assert_eq!(s.link_ix(RelKind::Hosting, a, b), Err(SceneError::Cycle));
    s.check_integrity().unwrap();
}

#[test]
fn connection_is_undirected_and_constraints_carry_payload() {
    let mut s = Scene::new(Config::default());
    let a = mk(&mut s, 0, 1);
    let b = mk(&mut s, 1, 1);
    s.link_ix(RelKind::Connection, b, a).unwrap();
    s.link_ix(RelKind::Connection, a, b).unwrap();
    assert_eq!(s.connected(a), vec![b]);
    assert_eq!(s.connected(b), vec![a]);
    s.add_constraint_ix(a, b, CT_DISTANCE, 2.5).unwrap();
    assert_eq!(s.constraints_of(a), vec![(b, CT_DISTANCE, 2.5, true)]);
    assert_eq!(s.constraints_of(b), vec![(a, CT_DISTANCE, 2.5, false)]);
    s.add_constraint_ix(a, b, CT_DISTANCE, 4.0).unwrap();
    assert_eq!(s.constraints_of(a).len(), 1);
    assert_eq!(s.constraints_of(a)[0].2, 4.0);
    s.check_integrity().unwrap();
    s.delete(uid(1)).unwrap();
    assert!(s.constraints_of(a).is_empty());
    s.check_integrity().unwrap();
}

#[test]
fn properties_typed_and_range_queries() {
    let mut s = Scene::new(Config::default());
    let h = s.register_prop("Height", PropKind::F64);
    let fr = s.register_prop("FireRating", PropKind::I64);
    for l in 0..50 {
        let ix = mk(&mut s, l, (l % 3) as u16);
        s.set_prop(ix, h, PropVal::F64(l as f64)).unwrap();
        if l % 2 == 0 {
            s.set_prop(ix, fr, PropVal::I64(l as i64 - 10)).unwrap();
        }
    }
    let ix0 = s.ix_of(uid(0)).unwrap();
    assert_eq!(s.set_prop(ix0, h, PropVal::I64(1)), Err(SceneError::PropertyKind));
    assert_eq!(s.prop_range_f64(h, 10.0, 14.0).len(), 5);
    assert_eq!(s.prop_range_i64(fr, -10, -4).len(), 4);
    let both = s.by_class_and_prop_range(1, h, 0.0, 20.0);
    let expect = (0..=20).filter(|l| l % 3 == 1).count();
    assert_eq!(both.len(), expect);
    s.clear_prop(ix0, h);
    assert_eq!(s.prop(ix0, h), None);
}

#[test]
fn indexes_follow_semantic_edits_and_match_scan() {
    let mut idx = Scene::new(Config::default());
    let mut scan = Scene::new(Config { class_index: false, classification_index: false });
    for s in [&mut idx, &mut scan] {
        for l in 0..300 {
            let ix = s
                .create(uid(l), EntityParts { semantic: Some(Semantic { class: (l % 7) as u16, ptype: (l % 2) as u16, classification: l % 5, phase: 0, option: 0 }), ..Default::default() })
                .unwrap();
            let _ = ix;
        }
        for l in (0..300).step_by(4) {
            let ix = s.ix_of(uid(l)).unwrap();
            s.set_semantic(ix, Semantic { class: 3, ptype: 0, classification: 2, phase: 1, option: 0 });
        }
        s.delete(uid(5)).unwrap();
    }
    idx.check_integrity().unwrap();
    for c in 0..7u16 {
        let mut a = idx.by_class(c);
        let mut b = scan.by_class(c);
        a.sort();
        b.sort();
        assert_eq!(a, b, "class {c}");
        let mut a = idx.by_class_type(c, 0);
        let mut b = scan.by_class_type(c, 0);
        a.sort();
        b.sort();
        assert_eq!(a, b);
    }
    for code in 1..5u32 {
        let mut a = idx.by_classification(code);
        let mut b = scan.by_classification(code);
        a.sort();
        b.sort();
        assert_eq!(a, b, "classification {code}");
    }
}

#[test]
fn builder_matches_incremental() {
    let mut rng = Rng(42);
    let n = 400u32;
    let mut b = SceneBuilder::new(Config::default());
    let mut s = Scene::new(Config::default());
    let hb = b.register_prop("H", PropKind::F64);
    let hs = s.register_prop("H", PropKind::F64);
    let mut ixb = vec![];
    let mut ixs = vec![];
    for l in 0..n {
        let parts = EntityParts { geometry: Some(boxed(l as f32, 0.0, 0.0)), semantic: Some(sem((l % 9) as u16)), presentation: Some(Presentation { layer: l as u16, ..Default::default() }), ..Default::default() };
        ixb.push(b.add(uid(l), parts).unwrap());
        ixs.push(s.create(uid(l), parts).unwrap());
        b.set_prop(ixb[l as usize], hb, PropVal::F64(l as f64)).unwrap();
        s.set_prop(ixs[l as usize], hs, PropVal::F64(l as f64)).unwrap();
    }
    for l in 1..n {
        let host = rng.below(l as u64) as usize;
        b.link(RelKind::Hosting, ixb[l as usize], ixb[host]);
        s.link_ix(RelKind::Hosting, ixs[l as usize], ixs[host]).unwrap();
        let x = rng.below(n as u64) as usize;
        if x != l as usize {
            b.link(RelKind::Connection, ixb[l as usize], ixb[x]);
            s.link_ix(RelKind::Connection, ixs[l as usize], ixs[x]).unwrap();
            b.constraint(ixb[x], ixb[l as usize], CT_FIXED_OFFSET, 1.5);
            s.add_constraint_ix(ixs[x], ixs[l as usize], CT_FIXED_OFFSET, 1.5).unwrap();
        }
    }
    let built = b.finish().unwrap();
    built.check_integrity().unwrap();
    s.check_integrity().unwrap();
    assert_eq!(dump_scene(&built, n, Some(hb)), dump_scene(&s, n, Some(hs)));
}

#[test]
fn builder_rejects_bad_input() {
    let mut b = SceneBuilder::new(Config::default());
    let a = b.add(uid(0), EntityParts::default()).unwrap();
    let c = b.add(uid(1), EntityParts::default()).unwrap();
    let d = b.add(uid(2), EntityParts::default()).unwrap();
    b.link(RelKind::Containment, a, c);
    b.link(RelKind::Containment, a, d);
    assert!(b.finish().is_err());
    let mut b = SceneBuilder::new(Config::default());
    b.add(uid(0), EntityParts::default()).unwrap();
    b.add(uid(0), EntityParts::default()).unwrap();
    assert!(matches!(b.finish(), Err(SceneError::DuplicateUuid(_))));
}

#[test]
fn snapshot_isolation_and_branching() {
    let mut s = Scene::new(Config::default());
    let h = s.register_prop("H", PropKind::F64);
    for l in 0..500 {
        let ix = mk(&mut s, l, (l % 4) as u16);
        s.set_prop(ix, h, PropVal::F64(l as f64)).unwrap();
        if l > 0 {
            s.link_ix(RelKind::Containment, ix, s.ix_of(uid(0)).unwrap()).unwrap();
        }
    }
    s.refresh_spatial();
    let before = dump_scene(&s, 600, Some(h));
    let snap = s.clone();
    // mutate the original heavily
    for l in 0..500 {
        let ix = s.ix_of(uid(l)).unwrap();
        s.translate(ix, [100.0, 0.0, 0.0]);
        s.set_semantic(ix, sem(99));
        s.set_prop(ix, h, PropVal::F64(-1.0)).unwrap();
    }
    s.delete(uid(0)).unwrap();
    for l in 500..520 {
        mk(&mut s, l, 1);
    }
    assert_eq!(dump_scene(&snap, 600, Some(h)), before, "snapshot changed under the original");
    snap.check_integrity().unwrap();
    s.check_integrity().unwrap();
    // branch: mutate the snapshot independently
    let mut branch = snap.clone();
    branch.delete(uid(1)).unwrap();
    assert!(snap.contains(uid(1)) && !branch.contains(uid(1)));
    assert!(s.contains(uid(1)));
    assert_eq!(dump_scene(&snap, 600, Some(h)), before);
    // queries on the snapshot still see old state
    assert_eq!(snap.by_class(99).len(), 0);
    assert_eq!(s.by_class(99).len(), 499);
    let q = Aabb::new([-1.0, -1.0, -1.0], [10.0, 5.0, 5.0]);
    let mut a = snap.query_aabb(&q);
    a.sort();
    assert!(!a.is_empty());
    assert!(s.query_aabb(&q).iter().all(|i| *i != s.ix_of(uid(0)).unwrap_or(u32::MAX)));
}

#[test]
fn history_undo_redo() {
    let mut s = Scene::new(Config::default());
    for l in 0..100 {
        mk(&mut s, l, 1);
    }
    let mut h = History::new(s);
    let g0 = *h.scene().geometry(h.scene().ix_of(uid(5)).unwrap()).unwrap();
    for k in 0..10 {
        h.edit(|s| {
            let ix = s.ix_of(uid(5)).unwrap();
            s.translate(ix, [k as f32 + 1.0, 0.0, 0.0]);
        });
    }
    assert_eq!(h.undo_depth(), 10);
    let g10 = *h.scene().geometry(h.scene().ix_of(uid(5)).unwrap()).unwrap();
    assert_ne!(g10, g0);
    for _ in 0..10 {
        assert!(h.undo());
    }
    assert!(!h.undo());
    assert_eq!(*h.scene().geometry(h.scene().ix_of(uid(5)).unwrap()).unwrap(), g0);
    for _ in 0..10 {
        assert!(h.redo());
    }
    assert_eq!(*h.scene().geometry(h.scene().ix_of(uid(5)).unwrap()).unwrap(), g10);
    h.undo();
    h.edit(|s| {
        s.delete(uid(7)).unwrap();
    });
    assert!(!h.can_redo());
    h.scene().check_integrity().unwrap();
}

#[test]
fn propagation_moves_hosted_and_fixed_offset_followers() {
    let mut s = Scene::new(Config::default());
    let wall = mk(&mut s, 0, 1);
    let door = mk(&mut s, 1, 2);
    let hdl = mk(&mut s, 2, 3);
    let fol = mk(&mut s, 3, 4);
    let ind = mk(&mut s, 4, 4);
    s.link_ix(RelKind::Hosting, door, wall).unwrap();
    s.link_ix(RelKind::Hosting, hdl, door).unwrap();
    s.add_constraint_ix(wall, fol, CT_FIXED_OFFSET, 0.0).unwrap();
    s.add_constraint_ix(wall, ind, CT_DISTANCE, 5.0).unwrap();
    let g_ind = *s.geometry(ind).unwrap();
    let n = s.move_with_dependents(wall, [0.0, 10.0, 0.0]);
    assert_eq!(n, 4);
    assert_eq!(*s.geometry(ind).unwrap(), g_ind);
    assert_eq!(s.geometry(hdl).unwrap().bbox.min[1], 10.0);
    s.check_integrity().unwrap();
}

#[test]
fn spatial_overlay_matches_brute_force() {
    let mut rng = Rng(7);
    let mut s = Scene::new(Config::default());
    for l in 0..2000 {
        let p = [rng.f32() * 100.0, rng.f32() * 100.0, rng.f32() * 100.0];
        s.create(uid(l), EntityParts { geometry: Some(Geometry { bbox: Aabb::new(p, [p[0] + 2.0, p[1] + 2.0, p[2] + 2.0]), handle: l as u64 }), ..Default::default() }).unwrap();
    }
    s.refresh_spatial();
    // edit, create and delete after the build: overlay must stay exact
    for l in 0..200 {
        let ix = s.ix_of(uid(l)).unwrap();
        s.translate(ix, [rng.f32() * 50.0 - 25.0, 0.0, 0.0]);
    }
    for l in 2000..2100 {
        let p = [rng.f32() * 100.0, rng.f32() * 100.0, rng.f32() * 100.0];
        s.create(uid(l), EntityParts { geometry: Some(Geometry { bbox: Aabb::new(p, [p[0] + 2.0, p[1] + 2.0, p[2] + 2.0]), handle: 0 }), ..Default::default() }).unwrap();
    }
    for l in 300..350 {
        s.delete(uid(l)).unwrap();
    }
    s.check_integrity().unwrap();
    let bf = |s: &Scene, q: &Aabb| {
        let mut v: Vec<Ix> = s.with_component(Component::Geometry).into_iter().filter(|&i| s.geometry(i).unwrap().bbox.intersects(q)).collect();
        v.sort();
        v
    };
    for _ in 0..40 {
        let p = [rng.f32() * 100.0, rng.f32() * 100.0, rng.f32() * 100.0];
        let q = Aabb::new(p, [p[0] + 20.0, p[1] + 20.0, p[2] + 20.0]);
        let mut got = s.query_aabb(&q);
        got.sort();
        assert_eq!(got, bf(&s, &q));
    }
    let before = s.dirty_count();
    assert!(before > 0);
    s.refresh_spatial();
    assert_eq!(s.dirty_count(), 0);
    s.check_integrity().unwrap();
    let hits = s.raycast([-10.0, 50.0, 50.0], [1.0, 0.0, 0.0]);
    assert!(hits.windows(2).all(|w| w[0].1 <= w[1].1));
}

#[test]
fn descendants_walk_containment_tree() {
    let mut s = Scene::new(Config::default());
    let root = mk(&mut s, 0, 1);
    let mut prev = vec![root];
    let mut l = 1;
    let mut total = 0;
    for _depth in 0..4 {
        let mut next = vec![];
        for &p in &prev {
            for _ in 0..3 {
                let c = mk(&mut s, l, 1);
                l += 1;
                s.link_ix(RelKind::Containment, c, p).unwrap();
                next.push(c);
                total += 1;
            }
        }
        prev = next;
    }
    assert_eq!(s.descendants(RelKind::Containment, root).len(), total);
    s.check_integrity().unwrap();
}
