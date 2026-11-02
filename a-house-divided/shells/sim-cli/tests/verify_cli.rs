//! `verify` exit codes: 0 match, 1 mismatch, 2 malformed.

mod common;

use common::{checkpoint_hash_offset, cli, fix_body_hash, process, tmp_path};
use hd_sim::scenario::{run_replay, verify_replay, ToyParams, Verdict};

fn sample_bytes() -> Vec<u8> {
    run_replay(&ToyParams::new(120, 4), 300, 100)
        .unwrap()
        .to_bytes()
}

fn verify_bytes(bytes: &[u8]) -> (i32, String, String) {
    let path = tmp_path("verify");
    std::fs::write(&path, bytes).unwrap();
    let result = process(&["verify", path.to_str().unwrap()]);
    let _ = std::fs::remove_file(path);
    result
}

#[test]
fn untouched_replay_verifies() {
    let (code, out, err) = verify_bytes(&sample_bytes());
    assert_eq!(code, 0, "stdout={out} stderr={err}");
    assert!(out.to_lowercase().contains("ok"), "{out}");
}

#[test]
fn altered_checkpoint_byte_with_recomputed_body_hash_exits_1() {
    let original = sample_bytes();
    let count = 3;
    for index in 0..count {
        for bit in [0x01u8, 0x80] {
            let mut bytes = original.clone();
            let at = checkpoint_hash_offset(bytes.len(), count, index) + 3;
            bytes[at] ^= bit;
            fix_body_hash(&mut bytes);
            let (code, out, err) = verify_bytes(&bytes);
            assert_eq!(code, 1, "index {index}: stdout={out} stderr={err}");
            let expected_tick = (index as u64 + 1) * 100;
            let text = format!("{out}{err}");
            assert!(
                text.contains(&format!("tick {expected_tick}")),
                "first differing tick {expected_tick} missing from: {text}"
            );
        }
    }
}

#[test]
fn altered_final_hash_with_recomputed_body_hash_exits_1() {
    let mut bytes = sample_bytes();
    let n = bytes.len();
    // footer: total_ticks u64, final_hash u64, body_hash u64
    bytes[n - 16] ^= 0x10;
    fix_body_hash(&mut bytes);
    let (code, out, err) = verify_bytes(&bytes);
    assert_eq!(code, 1, "stdout={out} stderr={err}");
    assert!(format!("{out}{err}").contains("tick 300"));
}

#[test]
fn corrupted_body_hash_exits_2() {
    let mut bytes = sample_bytes();
    let n = bytes.len();
    bytes[n - 1] ^= 0xff;
    let (code, _, err) = verify_bytes(&bytes);
    assert_eq!(code, 2, "{err}");
    assert!(!err.is_empty());
}

#[test]
fn damaged_checkpoint_without_recomputed_body_hash_exits_2() {
    let mut bytes = sample_bytes();
    let at = checkpoint_hash_offset(bytes.len(), 3, 1);
    bytes[at] ^= 0x01;
    let (code, _, _) = verify_bytes(&bytes);
    assert_eq!(code, 2);
}

#[test]
fn malformed_files_exit_2() {
    let good = sample_bytes();
    assert_eq!(verify_bytes(&[]).0, 2);
    assert_eq!(verify_bytes(b"not a replay at all").0, 2);
    assert_eq!(verify_bytes(&good[..good.len() / 2]).0, 2);
    let mut trailing = good.clone();
    trailing.push(0);
    assert_eq!(verify_bytes(&trailing).0, 2);
    let mut bad_magic = good;
    bad_magic[0] = b'X';
    fix_body_hash(&mut bad_magic);
    assert_eq!(verify_bytes(&bad_magic).0, 2);
}

#[test]
fn missing_file_exits_2() {
    let path = tmp_path("missing");
    let (code, _, err) = process(&["verify", path.to_str().unwrap()]);
    assert_eq!(code, 2);
    assert!(!err.is_empty());
}

#[test]
fn replay_of_another_scenario_or_without_setup_exits_2() {
    let mut w = sim_state::ReplayWriter::new("some-other-scenario", 1, 20, 0, 10);
    w.push_input(0, 0, 0, vec![0; 12]);
    let foreign = w.finish(0, 0).unwrap().to_bytes();
    assert_eq!(verify_bytes(&foreign).0, 2);

    let w = sim_state::ReplayWriter::new(hd_sim::scenario::SCENARIO_ID, 1, 20, 0, 10);
    let no_setup = w.finish(0, 0).unwrap().to_bytes();
    assert_eq!(verify_bytes(&no_setup).0, 2);

    let mut w = sim_state::ReplayWriter::new(hd_sim::scenario::SCENARIO_ID, 1, 20, 0, 10);
    w.push_input(0, 0, 0, vec![1, 2, 3]);
    let short_payload = w.finish(0, 0).unwrap().to_bytes();
    assert_eq!(verify_bytes(&short_payload).0, 2);
}

#[test]
fn checkpoint_schedule_that_disagrees_with_the_header_exits_2() {
    let replay = run_replay(&ToyParams::new(30, 2), 40, 10).unwrap();
    assert_eq!(verify_replay(&replay), Verdict::Match);
    // Same content but declared interval 7: the stored schedule no longer fits.
    let mut w = sim_state::ReplayWriter::new(replay.scenario_id(), 2, 20, 0, 7);
    for i in replay.inputs() {
        w.push_input(i.tick, i.actor, i.seq, i.payload.clone());
    }
    for c in replay.checkpoints() {
        w.push_checkpoint(c.tick, c.hash);
    }
    let odd = w.finish(40, replay.final_hash()).unwrap();
    assert!(matches!(verify_replay(&odd), Verdict::Malformed(_)));
}

#[test]
fn run_writes_a_file_that_verify_accepts_in_process() {
    let path = tmp_path("run-verify");
    let p = path.to_str().unwrap();
    let (code, out, err) = cli(&[
        "run",
        "--agents",
        "64",
        "--ticks",
        "50",
        "--seed",
        "12",
        "--checkpoint-interval",
        "10",
        "--out",
        p,
    ]);
    assert_eq!(code, 0, "{err}");
    assert!(out.contains("final_hash="), "{out}");
    let (code, _, err) = cli(&["verify", p]);
    assert_eq!(code, 0, "{err}");
    let _ = std::fs::remove_file(path);
}

#[test]
fn usage_errors_exit_2_with_a_readable_message() {
    for args in [
        vec![],
        vec!["frobnicate"],
        vec!["run", "--agents", "ten", "--ticks", "5", "--out", "x"],
        vec!["run", "--ticks", "5"],
        vec!["run", "--bogus", "1", "--out", "x"],
        vec!["verify"],
        vec![
            "hash", "--agents", "5", "--agents", "6", "--ticks", "1", "--seed", "1",
        ],
        vec!["bench", "--agents", "5", "--ticks", "0"],
        vec![
            "hash", "--agents", "99999999", "--ticks", "1", "--seed", "1",
        ],
    ] {
        let (code, _, err) = cli(&args);
        assert_eq!(code, 2, "args {args:?}");
        assert!(!err.trim().is_empty(), "args {args:?} gave no message");
    }
}

#[test]
fn equals_form_and_help_are_accepted() {
    let (code, out, _) = cli(&["hash", "--agents=20", "--ticks=5", "--seed=3"]);
    assert_eq!(code, 0);
    assert_eq!(out.trim().len(), 16);
    let (code, out, _) = cli(&["--help"]);
    assert_eq!(code, 0);
    assert!(out.contains("verify"));
}
