//! End-to-end tests of the workspace: commands, undo/redo, branching, time travel, recovery,
//! replay determinism and synchronisation.

mod common;

use common::*;
use oplog::engine::{MergeKind, Options};
use oplog::exec::Registry;
use oplog::hamt::empty_root_hash;
use oplog::ids::{ObjectId, OpId};
use oplog::value::Value;
use oplog::{commands, Error};
use std::collections::HashMap;

fn created_id(ws: &oplog::Workspace, op: OpId) -> ObjectId {
    let o = ws.op(op).unwrap();
    o.outputs.iter().find(|x| x.before.is_none()).unwrap().id
}

#[test]
fn commands_edit_objects_through_typed_parameters() {
    let mut ws = mem(1);
    let op = ws.run("create_box", create_params("wall", [0.0, 0.0, 0.0])).unwrap();
    let id = created_id(&ws, op);
    ws.run("move", Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(1.0, 2.0, 3.0))])).unwrap();
    ws.run("tag_add", Value::map([("id", Value::Obj(id)), ("tag", Value::str("exterior"))])).unwrap();
    ws.run("tag_add", Value::map([("id", Value::Obj(id)), ("tag", Value::str("fire"))])).unwrap();
    ws.run("tag_remove", Value::map([("id", Value::Obj(id)), ("tag", Value::str("fire"))])).unwrap();
    for (i, p) in [[0.0, 0.0, 0.0], [2.0, 0.0, 0.0], [1.0, 1.0, 0.0]].iter().enumerate() {
        ws.run("pts_insert", Value::map([("id", Value::Obj(id)), ("index", Value::Int(i as i64)), ("point", Value::f3(*p))])).unwrap();
    }
    ws.run("pts_move", Value::map([("id", Value::Obj(id)), ("index", Value::Int(1)), ("point", v3(9.0, 9.0, 9.0))])).unwrap();
    let o = ws.object(id).unwrap().unwrap();
    let view = o.project();
    assert_eq!(view.get("pos").unwrap().as_f3(), Some([1.0, 2.0, 3.0]));
    assert_eq!(view.get("tags").unwrap().as_list().unwrap().len(), 1);
    let pts: Vec<[f64; 3]> = view.get("pts").unwrap().as_list().unwrap().iter().map(|v| v.as_f3().unwrap()).collect();
    assert_eq!(pts, vec![[0.0, 0.0, 0.0], [9.0, 9.0, 9.0], [1.0, 1.0, 0.0]]);
    // the operation records what it read and wrote
    let rec = ws.op(ws.head()).unwrap();
    assert_eq!(rec.command, "pts_move");
    assert_eq!(rec.inputs, vec![id]);
    assert_eq!(rec.outputs.len(), 1);
    assert!(rec.outputs[0].before.is_some());
    // delete, and editing a deleted object is refused
    ws.run("delete", Value::map([("ids", Value::List(vec![Value::Obj(id)]))])).unwrap();
    assert!(!ws.object(id).unwrap().unwrap().is_alive());
    let err = ws.run("move", Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(1.0, 0.0, 0.0))]));
    assert!(matches!(err, Err(Error::Command(_))));
    // bad parameters and unknown commands change nothing
    let before = (ws.len(), ws.state_hash());
    assert!(matches!(ws.run("move", Value::Null), Err(Error::BadParams(_))));
    assert!(matches!(ws.run("nope", Value::Null), Err(Error::UnknownCommand(_))));
    assert_eq!(before, (ws.len(), ws.state_hash()));
}

#[test]
fn undo_and_redo_restore_exact_snapshots() {
    let mut ws = mem(2);
    let mut sc = script(11, false);
    let steps = drive(&mut ws, &mut sc, 120);
    let mut hashes = vec![empty_root_hash()];
    hashes.extend(steps.iter().map(|s| s.1));
    for k in (0..steps.len()).rev() {
        let nav = ws.undo().unwrap().unwrap();
        assert_eq!(ws.state_hash(), hashes[k], "after undoing down to {k}");
        assert!(!nav.via_checkpoint);
    }
    assert!(ws.undo().unwrap().is_none(), "nothing before the genesis operation");
    for (k, h) in hashes.iter().enumerate().skip(1) {
        ws.redo().unwrap().unwrap();
        assert_eq!(ws.state_hash(), *h, "after redoing up to {k}");
    }
    assert!(ws.redo().unwrap().is_none());
    ws.verify().unwrap();
}

#[test]
fn undo_then_edit_creates_a_branch_and_loses_nothing() {
    let mut ws = mem(3);
    let a = ws.run("create_box", create_params("a", [0.0; 3])).unwrap();
    let id = created_id(&ws, a);
    let mv = |d: f64| Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(d, 0.0, 0.0))]);
    let b = ws.run("move", mv(1.0)).unwrap();
    let hb = ws.state_hash();
    ws.undo().unwrap();
    let c = ws.run("move", mv(5.0)).unwrap();
    let hc = ws.state_hash();
    assert_ne!(hb, hc);
    // the tree now has two tips under `a`
    let mut leaves = ws.leaves();
    leaves.sort();
    let mut want = vec![b, c];
    want.sort();
    assert_eq!(leaves, want);
    assert_eq!(ws.children(a).unwrap(), vec![c, b], "newest first");
    // time travel to the abandoned future restores exactly what was there
    ws.checkout(b).unwrap();
    assert_eq!(ws.state_hash(), hb);
    ws.checkout(c).unwrap();
    assert_eq!(ws.state_hash(), hc);
    // undo from c then redo comes back to c, not b
    ws.undo().unwrap();
    ws.redo().unwrap();
    assert_eq!(ws.head(), c);
    ws.verify().unwrap();
}

#[test]
fn design_options_are_branches_that_can_be_compared_and_merged() {
    let mut ws = mem(4);
    let a = ws.run("create_box", create_params("slab", [0.0; 3])).unwrap();
    let id = created_id(&ws, a);
    ws.branch("main").unwrap();
    let base = ws.head();
    // option A: red; option B: moved
    ws.branch("option-a").unwrap();
    ws.run("set_prop", Value::map([("id", Value::Obj(id)), ("prop", Value::str("color")), ("value", Value::Int(1))])).unwrap();
    let tip_a = ws.head();
    ws.switch("main").unwrap();
    assert_eq!(ws.head(), base);
    ws.branch("option-b").unwrap();
    ws.run("move", Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(0.0, 4.0, 0.0))])).unwrap();
    let tip_b = ws.head();
    assert_eq!(ws.branches().count(), 3);
    assert_eq!(ws.current_branch(), Some("option-b"));
    // compare: the two options differ in exactly one object
    let d = ws.diff_ops(tip_a, tip_b).unwrap();
    assert_eq!(d.len(), 1);
    assert_eq!(d[0].0, id);
    // switching back and forth is exact
    let hb = ws.state_hash();
    ws.switch("option-a").unwrap();
    assert_eq!(ws.object(id).unwrap().unwrap().project().get("color"), Some(&Value::Int(1)));
    ws.switch("option-b").unwrap();
    assert_eq!(ws.state_hash(), hb);
    // merge option A into option B: different properties, so no conflict, both edits present
    let out = ws.merge(tip_a).unwrap();
    assert_eq!(out.kind, MergeKind::Merged);
    assert!(out.conflicts.is_empty());
    let view = ws.object(id).unwrap().unwrap().project();
    assert_eq!(view.get("color"), Some(&Value::Int(1)));
    assert_eq!(view.get("pos").unwrap().as_f3(), Some([0.0, 4.0, 0.0]));
    // merging again is a no-op
    assert_eq!(ws.merge(tip_a).unwrap().kind, MergeKind::AlreadyMerged);
    ws.verify().unwrap();
}

#[test]
fn time_travel_to_every_operation_matches_the_recorded_state() {
    for checkpoint_every in [0u32, 5, 16] {
        let tmp = TmpDir::new("tt");
        let (mut ws, _) = open(tmp.path(), 5, opts(checkpoint_every));
        let mut sc = script(77 + u64::from(checkpoint_every), true);
        let steps = drive(&mut ws, &mut sc, 400);
        assert!(ws.leaves().len() > 1, "the script must have produced branches (undo then edit)");
        let recorded: HashMap<OpId, _> = steps.iter().cloned().collect();
        let mut jumped_via_checkpoint = 0;
        let ids: Vec<OpId> = steps.iter().map(|s| s.0).collect();
        for (n, id) in ids.iter().enumerate().filter(|(n, _)| n % 3 == 0) {
            let want = recorded[id];
            assert_eq!(ws.state_hash_at(*id).unwrap(), want, "state_hash_at #{n} (checkpoint_every={checkpoint_every})");
            let nav = ws.checkout(*id).unwrap();
            jumped_via_checkpoint += usize::from(nav.via_checkpoint);
            assert_eq!(ws.state_hash(), want, "checkout #{n}");
        }
        if checkpoint_every > 0 {
            assert!(jumped_via_checkpoint > 0, "some long jumps must use checkpoints");
        }
        ws.verify().unwrap();
    }
}

#[test]
fn clean_close_and_reopen_restores_log_state_branches_and_head() {
    let tmp = TmpDir::new("reopen");
    let (mut ws, rep) = open(tmp.path(), 10, opts(32));
    assert_eq!(rep.ops, 1);
    let mut sc = script(5, true);
    let steps = drive(&mut ws, &mut sc, 500);
    assert!(!steps.is_empty());
    let before = (ws.head(), ws.state_hash(), ws.len(), ws.current_branch().map(str::to_string));
    let branches: Vec<_> = ws.branches().map(|(n, o)| (n.clone(), *o)).collect();
    let leaves = ws.leaves();
    ws.verify().unwrap();
    drop(ws);
    let (ws2, rep2) = open(tmp.path(), 10, opts(32));
    assert_eq!(rep2.discarded_bytes, 0);
    assert_eq!(rep2.ops, before.2);
    assert_eq!((ws2.head(), ws2.state_hash(), ws2.len(), ws2.current_branch().map(str::to_string)), before);
    assert_eq!(ws2.branches().map(|(n, o)| (n.clone(), *o)).collect::<Vec<_>>(), branches);
    assert_eq!(ws2.leaves(), leaves);
    ws2.verify().unwrap();
    // every recorded state is still reachable after reopening
    let mut ws2 = ws2;
    for (n, (id, h)) in steps.iter().enumerate().filter(|(n, _)| n % 7 == 0) {
        ws2.checkout(*id).unwrap();
        assert_eq!(ws2.state_hash(), *h, "after reopen, state of #{n}");
    }
}

#[test]
fn a_reopened_session_with_the_same_id_never_reuses_a_dot() {
    let tmp = TmpDir::new("dots");
    let (mut ws, _) = open(tmp.path(), 99, opts(0));
    let op = ws.run("create_box", create_params("x", [0.0; 3])).unwrap();
    let first = ws.op(op).unwrap();
    drop(ws);
    let (mut ws, _) = open(tmp.path(), 99, opts(0));
    let id = created_id(&ws, op);
    let op2 = ws.run("move", Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(1.0, 0.0, 0.0))])).unwrap();
    let second = ws.op(op2).unwrap();
    assert_eq!(first.session, second.session);
    assert!(second.epoch > first.epoch, "a new actor epoch after reopening");
    assert_eq!(second.seq, 1);
    ws.verify().unwrap();
}

#[test]
fn replay_reproduces_the_recorded_session_exactly() {
    let tmp = TmpDir::new("replay");
    let (mut ws, _) = open(tmp.path(), 21, opts(64));
    let mut sc = script(2024, true);
    drive(&mut ws, &mut sc, 800);
    let rep = ws.replay_check(&commands::standard()).unwrap();
    assert!(rep.ok(), "{:?}", rep.mismatches);
    assert_eq!(rep.ops, ws.len());
    assert!(rep.executed > 300, "executed {}", rep.executed);
    assert_eq!(rep.final_root, ws.state_hash());
    // a log reopened from disk replays to the same hash
    drop(ws);
    let (ws2, _) = open(tmp.path(), 21, opts(64));
    let rep2 = ws2.replay_check(&commands::standard()).unwrap();
    assert!(rep2.ok());
    assert_eq!(rep2.final_root, rep.final_root);
}

#[test]
fn replay_detects_commands_that_read_state_outside_the_dispatcher() {
    let mut ws = mem(8).with_registry(commands::with_faulty());
    let op = ws.run("create_box", create_params("b", [0.0; 3])).unwrap();
    let id = created_id(&ws, op);
    ws.run("move", Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(1.0, 1.0, 1.0))])).unwrap();
    let clean = ws.replay_check(&commands::with_faulty()).unwrap();
    assert!(clean.ok());
    ws.run("bad_clock", Value::map([("id", Value::Obj(id))])).unwrap();
    ws.run("bad_global", Value::map([("id", Value::Obj(id))])).unwrap();
    let rep = ws.replay_check(&commands::with_faulty()).unwrap();
    assert!(!rep.ok());
    assert!(rep.mismatches.iter().any(|m| m.contains("bad_clock")), "{:?}", rep.mismatches);
    assert!(rep.mismatches.iter().any(|m| m.contains("bad_global")), "{:?}", rep.mismatches);
    assert!(rep.mismatches.iter().all(|m| !m.contains("`move`") && !m.contains("`create_box`")));
    // a log recorded with a command the replaying build does not know is reported, not hidden
    let rep = ws.replay_check(&Registry::new()).unwrap();
    assert!(rep.mismatches.iter().any(|m| m.contains("not registered")));
}

#[test]
fn replay_with_seeded_randomness_is_exact() {
    let mut ws = mem(9);
    for i in 0..20 {
        ws.run_with("scatter", Value::map([("count", Value::Int(25)), ("extent", Value::Float(100.0)), ("layer", Value::str("S"))]), 1000 + i).unwrap();
    }
    assert_eq!(ws.object_count(), 20 * 25);
    let rep = ws.replay_check(&commands::standard()).unwrap();
    assert!(rep.ok(), "{:?}", rep.mismatches);
    // the seed is part of the record
    let rec = ws.op(ws.head()).unwrap();
    assert_eq!(rec.seed, 1019);
}

#[test]
fn two_replicas_sync_and_merge_to_the_same_operation_and_state() {
    let mut a = mem(100);
    let op = a.run("create_box", create_params("wall", [0.0; 3])).unwrap();
    let wall = created_id(&a, op);
    let op = a.run("create_box", create_params("door", [1.0, 0.0, 0.0])).unwrap();
    let door = created_id(&a, op);
    let mut b = mem(200);
    b.pull_from(&a).unwrap();
    b.checkout(a.head()).unwrap();
    assert_eq!(a.state_hash(), b.state_hash());
    // concurrent edits
    let mv = |id, d: f64| Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", v3(d, 0.0, 0.0))]);
    a.run("move", mv(wall, 1.0)).unwrap();
    a.run("tag_add", Value::map([("id", Value::Obj(door)), ("tag", Value::str("a-tag"))])).unwrap();
    b.run("move", mv(wall, 10.0)).unwrap();
    b.run("tag_add", Value::map([("id", Value::Obj(door)), ("tag", Value::str("b-tag"))])).unwrap();
    // exchange operations
    let (ha, hb) = (a.head(), b.head());
    a.pull_from(&b).unwrap();
    b.pull_from(&a).unwrap();
    assert_eq!(a.len(), b.len());
    let ma = a.merge(hb).unwrap();
    let mb = b.merge(ha).unwrap();
    assert_eq!(ma.kind, MergeKind::Merged);
    assert_eq!(mb.kind, MergeKind::Merged);
    assert_eq!(ma.op, mb.op, "both replicas create the identical merge operation");
    assert_eq!(a.state_hash(), b.state_hash());
    assert_eq!(ma.conflicts.len(), 1, "both moved the wall");
    assert_eq!(ma.conflicts, mb.conflicts);
    let tags = a.object(door).unwrap().unwrap().project().get("tags").unwrap().as_list().unwrap().len();
    assert_eq!(tags, 2, "set union keeps both tags");
    assert_eq!(a.conflicts().unwrap().len(), 1);
    a.verify().unwrap();
    b.verify().unwrap();
    // resolving: anyone who saw both writes it again
    a.run("set_prop", Value::map([("id", Value::Obj(wall)), ("prop", Value::str("pos")), ("value", v3(5.0, 0.0, 0.0))])).unwrap();
    assert!(a.conflicts().unwrap().is_empty());
}

#[test]
fn ingest_rejects_tampered_and_orphaned_operations() {
    let mut a = mem(1);
    a.run("create_box", create_params("w", [0.0; 3])).unwrap();
    a.run("create_box", create_params("v", [1.0; 3])).unwrap();
    let mut b = mem(2);
    let have = |_: &oplog::ids::Hash| false;
    let p1 = a.export_op(1, &have).unwrap();
    let p2 = a.export_op(2, &have).unwrap();
    // child before parent
    assert!(matches!(b.ingest(std::slice::from_ref(&p2)), Err(Error::MissingParent(_))));
    // tampered blob
    let mut bad = p1.clone();
    bad.blobs[0].1[3] ^= 1;
    assert!(matches!(b.ingest(&[bad]), Err(Error::Mismatch(_))));
    // missing blob
    let mut missing = p1.clone();
    missing.blobs.clear();
    assert!(matches!(b.ingest(&[missing]), Err(Error::MissingBlob(_))));
    // the real thing works, twice (idempotent)
    let r = b.ingest(&[p1.clone(), p2.clone()]).unwrap();
    assert_eq!((r.added, r.skipped), (2, 0));
    let r = b.ingest(&[p1, p2]).unwrap();
    assert_eq!((r.added, r.skipped), (0, 2));
    b.checkout(a.head()).unwrap();
    assert_eq!(a.state_hash(), b.state_hash());
}

#[test]
fn identical_content_is_stored_once() {
    let mut ws = mem(12);
    let op = ws.run("create_box", {
        let mut p = create_params("big", [0.0; 3]);
        if let Value::Map(m) = &mut p {
            m.insert("geom_bytes".into(), Value::Int(50_000));
        }
        p
    }).unwrap();
    let src = created_id(&ws, op);
    let blobs_before = ws.store().len();
    let bytes_before = ws.store().stored_bytes();
    // 50 copies share the geometry blob; only the small object snapshots are new
    ws.run("array", Value::map([("src", Value::Obj(src)), ("n", Value::Int(50)), ("delta", v3(1.0, 0.0, 0.0))])).unwrap();
    assert_eq!(ws.object_count(), 51);
    let added_bytes = ws.store().stored_bytes() - bytes_before;
    assert!(added_bytes < 50_000, "copies must not duplicate the 50 kB geometry (added {added_bytes})");
    assert!(ws.store().len() >= blobs_before + 50);
    // undo of the whole array restores the previous document exactly
    let h = ws.state_hash();
    ws.undo().unwrap();
    assert_eq!(ws.object_count(), 1);
    ws.redo().unwrap();
    assert_eq!(ws.state_hash(), h);
}

#[test]
fn options_default_is_sane() {
    let o = Options::default();
    assert!(o.checkpoint_every >= 1024);
}
