//! Same seed gives identical bytes; different seeds differ; run then verify always passes.

mod common;

use common::{cli, process, tmp_path};
use hd_sim::scenario::{final_hash, run_replay, verify_replay, ToyParams, Verdict};
use proptest::prelude::*;

fn params(agents: u32, seed: u64) -> ToyParams {
    ToyParams::new(agents, seed)
}

#[test]
fn same_seed_is_byte_identical_in_one_process() {
    let a = run_replay(&params(150, 5), 120, 20).unwrap().to_bytes();
    let b = run_replay(&params(150, 5), 120, 20).unwrap().to_bytes();
    assert_eq!(a, b);
}

#[test]
fn same_seed_is_byte_identical_across_two_processes() {
    let (p1, p2) = (tmp_path("proc-a"), tmp_path("proc-b"));
    for p in [&p1, &p2] {
        let (code, _, err) = process(&[
            "run",
            "--agents",
            "150",
            "--ticks",
            "120",
            "--seed",
            "5",
            "--checkpoint-interval",
            "20",
            "--out",
            p.to_str().unwrap(),
        ]);
        assert_eq!(code, 0, "run failed: {err}");
    }
    let (b1, b2) = (std::fs::read(&p1).unwrap(), std::fs::read(&p2).unwrap());
    assert_eq!(b1, b2);
    // The separate processes agree with the in-process library, too.
    assert_eq!(b1, run_replay(&params(150, 5), 120, 20).unwrap().to_bytes());
    let _ = std::fs::remove_file(p1);
    let _ = std::fs::remove_file(p2);
}

#[test]
fn different_seeds_give_different_final_hashes() {
    let hashes: Vec<u64> = (1..=8u64)
        .map(|seed| final_hash(&params(100, seed), 100).unwrap())
        .collect();
    for i in 0..hashes.len() {
        for j in i + 1..hashes.len() {
            assert_ne!(hashes[i], hashes[j], "seeds {} and {}", i + 1, j + 1);
        }
    }
}

#[test]
fn hash_command_matches_replay_final_hash() {
    let (code, out, _) = cli(&["hash", "--agents", "80", "--ticks", "60", "--seed", "9"]);
    assert_eq!(code, 0);
    let replay = run_replay(&params(80, 9), 60, 10).unwrap();
    assert_eq!(out.trim(), format!("{:016x}", replay.final_hash()));
    assert_eq!(replay.final_hash(), final_hash(&params(80, 9), 60).unwrap());
}

#[test]
fn checkpoints_land_on_the_interval() {
    let replay = run_replay(&params(40, 3), 95, 10).unwrap();
    let ticks: Vec<u64> = replay.checkpoints().iter().map(|c| c.tick).collect();
    assert_eq!(ticks, vec![10, 20, 30, 40, 50, 60, 70, 80, 90]);
    assert_eq!(replay.total_ticks(), 95);
    let none = run_replay(&params(40, 3), 95, 0).unwrap();
    assert!(none.checkpoints().is_empty());
}

#[test]
fn agents_actually_move_and_get_despawned() {
    // A dense little arena: contact is constant, so hp drains and agents despawn.
    let mut p = params(60, 11);
    p.width_m = 40;
    p.height_m = 40;
    let early = final_hash(&p, 1).unwrap();
    let late = final_hash(&p, 3000).unwrap();
    assert_ne!(early, late);
    let mut s = hd_sim::scenario::Scenario::new(&p).unwrap();
    assert_eq!(s.world().agents.len(), 60);
    for _ in 0..3000 {
        s.step();
    }
    assert!(s.world().agents.len() < 60, "no agent despawned");
    assert_eq!(s.world().tick, 3000);
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(48))]

    #[test]
    fn run_then_verify_always_passes(
        agents in 0u32..40,
        ticks in 0u64..40,
        seed in any::<u64>(),
        interval in 0u32..12,
        arena in 16u32..300,
        hz in 1u16..60,
    ) {
        let mut p = params(agents, seed);
        p.width_m = arena;
        p.height_m = arena + 7;
        p.tick_hz = hz;
        let replay = run_replay(&p, ticks, interval).unwrap();
        prop_assert_eq!(verify_replay(&replay), Verdict::Match);
        let reparsed = sim_state::ReplayReader::parse(&replay.to_bytes()).unwrap();
        prop_assert_eq!(&reparsed, &replay);
        prop_assert_eq!(verify_replay(&reparsed), Verdict::Match);
    }
}
