//! Property tests at the operation-log level: replicas that edit concurrently, exchange
//! operations in arbitrary patterns and merge in arbitrary orders always converge.

mod common;

use common::*;
use oplog::engine::MergeKind;
use oplog::ids::{ObjectId, OpId};
use oplog::script::Script;
use oplog::Workspace;
use proptest::prelude::*;
use std::collections::BTreeMap;

fn pull(reps: &mut [Workspace], dst: usize, src: usize) {
    if dst == src {
        return;
    }
    let (a, b) = if dst < src {
        let (l, r) = reps.split_at_mut(src);
        (&mut l[dst], &r[0])
    } else {
        let (l, r) = reps.split_at_mut(dst);
        (&mut r[0], &l[src])
    };
    a.pull_from(b).unwrap();
}

fn mix(x: u64) -> u64 {
    let mut z = x.wrapping_add(0x9E37_79B9_7F4A_7C15);
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

struct Outcome {
    reps: Vec<Workspace>,
    heads: Vec<OpId>,
}

fn run_scenario(seed: u64, nrep: usize, rounds: usize, per_round: usize, nav: bool) -> Outcome {
    let mut reps: Vec<Workspace> = (0..nrep).map(|r| mem(500 + r as u64)).collect();
    let mut sc0 = script(seed, false);
    drive(&mut reps[0], &mut sc0, 40);
    let base_ids: Vec<ObjectId> = reps[0].objects().unwrap().into_iter().map(|(id, _)| id).collect();
    for r in 1..nrep {
        pull(&mut reps, r, 0);
        let h = reps[0].head();
        reps[r].checkout(h).unwrap();
    }
    let mut scripts: Vec<Script> = (0..nrep)
        .map(|r| {
            let mut s = script(seed + 1 + r as u64, nav);
            s.adopt(base_ids.iter().copied());
            s
        })
        .collect();
    for round in 0..rounds {
        for r in 0..nrep {
            drive(&mut reps[r], &mut scripts[r], per_round);
        }
        // a pseudo-random pairwise exchange, sometimes followed by a merge
        let m = mix(seed ^ round as u64);
        let (a, b) = ((m % nrep as u64) as usize, ((m >> 8) % nrep as u64) as usize);
        pull(&mut reps, a, b);
        if (m >> 16) % 2 == 0 && a != b {
            let h = reps[b].head();
            if reps[a].contains(&h) {
                reps[a].merge(h).unwrap();
            }
        }
    }
    let heads: Vec<OpId> = reps.iter().map(Workspace::head).collect();
    // full exchange (two passes so everything propagates), then every replica merges every head
    // in its own rotated order
    for _ in 0..2 {
        for d in 0..nrep {
            for s in 0..nrep {
                pull(&mut reps, d, s);
            }
        }
    }
    for (r, ws) in reps.iter_mut().enumerate() {
        for k in 0..nrep {
            let h = heads[(r + k) % nrep];
            if h != ws.head() {
                ws.merge(h).unwrap();
            }
        }
    }
    Outcome { reps, heads }
}

proptest! {
    #![proptest_config(ProptestConfig { cases: if cfg!(debug_assertions) { 40 } else { 160 }, ..ProptestConfig::default() })]

    #[test]
    fn replicas_converge_whatever_the_exchange_and_merge_order(
        seed in 0u64..1_000_000,
        nrep in 2usize..5,
        rounds in 1usize..4,
        per_round in 3usize..14,
        nav in any::<bool>(),
    ) {
        let out = run_scenario(seed, nrep, rounds, per_round, nav);
        let reps = &out.reps;
        // 1. convergence: identical document state and identical conflict items everywhere
        let h0 = reps[0].state_hash();
        let c0 = reps[0].conflicts().unwrap();
        for ws in reps.iter() {
            prop_assert_eq!(ws.state_hash(), h0);
            prop_assert_eq!(ws.conflicts().unwrap(), c0.clone());
            ws.verify().unwrap();
        }
        // 2. idempotence: merging again changes nothing
        let mut again = run_scenario(seed, nrep, rounds, per_round, nav);
        for ws in again.reps.iter_mut() {
            let before = (ws.head(), ws.state_hash());
            for h in out.heads.iter() {
                let o = ws.merge(*h).unwrap();
                prop_assert_eq!(o.kind, MergeKind::AlreadyMerged);
            }
            prop_assert_eq!((ws.head(), ws.state_hash()), before);
        }
        // 3. oracle: the merged state equals the plain per-object join of every replica's head
        let mut joined: BTreeMap<ObjectId, oplog::crdt::Obj> = BTreeMap::new();
        for h in out.heads.iter() {
            for (id, obj) in reps[0].objects_at(*h).unwrap() {
                joined.entry(id).and_modify(|o| o.merge(&obj)).or_insert(obj);
            }
        }
        let merged: BTreeMap<ObjectId, Vec<u8>> = reps[0].objects().unwrap().into_iter().map(|(i, o)| (i, o.to_bytes())).collect();
        let oracle: BTreeMap<ObjectId, Vec<u8>> = joined.into_iter().map(|(i, o)| (i, o.to_bytes())).collect();
        prop_assert_eq!(merged, oracle);
        // 4. the merged logs replay: merge operations are re-derived exactly
        for ws in reps.iter() {
            let rep = ws.replay_check(&oplog::commands::standard()).unwrap();
            prop_assert!(rep.ok(), "{:?}", rep.mismatches);
        }
    }

    /// Two replicas that merge each other's heads create the *identical* merge operation.
    #[test]
    fn pairwise_merge_is_commutative_down_to_the_operation_id(
        seed in 0u64..1_000_000,
        per_round in 4usize..25,
    ) {
        let mut reps: Vec<Workspace> = (0..2).map(|r| mem(900 + r as u64)).collect();
        let mut sc0 = script(seed, false);
        drive(&mut reps[0], &mut sc0, 30);
        let base: Vec<ObjectId> = reps[0].objects().unwrap().into_iter().map(|(i, _)| i).collect();
        pull(&mut reps, 1, 0);
        let h = reps[0].head();
        reps[1].checkout(h).unwrap();
        for (r, ws) in reps.iter_mut().enumerate() {
            let mut s = script(seed + 7 + r as u64, false);
            s.adopt(base.iter().copied());
            drive(ws, &mut s, per_round);
        }
        let (ha, hb) = (reps[0].head(), reps[1].head());
        pull(&mut reps, 0, 1);
        pull(&mut reps, 1, 0);
        let oa = reps[0].merge(hb).unwrap();
        let ob = reps[1].merge(ha).unwrap();
        prop_assert_eq!(oa.kind, ob.kind);
        prop_assert_eq!(oa.op, ob.op);
        prop_assert_eq!(reps[0].state_hash(), reps[1].state_hash());
        prop_assert_eq!(oa.conflicts, ob.conflicts);
        if let Some(op) = oa.op {
            let rec = reps[0].op(op).unwrap();
            prop_assert_eq!(rec.parents.len(), 2);
            prop_assert!(rec.parents[0] < rec.parents[1], "parents are sorted by id");
            prop_assert_eq!(rec.session, 0);
        }
    }
}

/// The property tests above are only meaningful if the scenarios really contain concurrent
/// edits, merge operations and conflicts. Count them over a fixed set of seeds.
#[test]
fn the_generated_scenarios_exercise_merges_and_register_and_delete_conflicts() {
    use oplog::crdt::ConflictKind;
    let (mut merges, mut conflicts, mut reg, mut list, mut del, mut ops) = (0, 0, 0, 0, 0, 0);
    for seed in 0..60u64 {
        let out = run_scenario(seed * 7919 + 3, 2 + (seed % 3) as usize, 1 + (seed % 3) as usize, 6 + (seed % 7) as usize, seed % 2 == 0);
        let ws = &out.reps[0];
        ops += ws.len();
        for i in ws.op_ids() {
            if ws.op(i).unwrap().command == "merge" {
                merges += 1;
            }
        }
        for c in ws.conflicts().unwrap() {
            conflicts += 1;
            match c.kind {
                ConflictKind::Register => reg += 1,
                ConflictKind::ListElement => list += 1,
                ConflictKind::DeleteVsEdit => del += 1,
            }
        }
    }
    println!("60 scenarios: {ops} operations, {merges} merge operations, {conflicts} conflict items (register {reg}, list element {list}, delete-vs-edit {del})");
    assert!(merges >= 60, "merge operations: {merges}");
    assert!(reg > 0 && del > 0, "register conflicts {reg}, delete-vs-edit {del}");
}
