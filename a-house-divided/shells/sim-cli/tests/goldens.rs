//! The committed golden replays are regenerated from their parameters and compared byte for byte.
//!
//! Set `HD_SIM_BLESS=1` (in a release build) to rewrite the files after an intentional change to
//! the toy scenario; the default is to assert.

mod common;

use common::cli;
use hd_sim::golden::{expected_json, GoldenEntry, GOLDENS};
use hd_sim::scenario::{run_replay, ToyParams};
use sim_state::ReplayReader;
use std::path::PathBuf;

fn golden_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../qc/golden-replays")
}

fn regenerate(spec: &hd_sim::golden::GoldenSpec) -> Vec<u8> {
    let params = ToyParams::new(spec.agents, spec.seed);
    run_replay(&params, spec.ticks, spec.checkpoint_interval)
        .unwrap()
        .to_bytes()
}

fn bless_requested() -> bool {
    std::env::var("HD_SIM_BLESS").as_deref() == Ok("1")
}

fn check_regenerates(name: &str) {
    let spec = GOLDENS.iter().find(|g| g.name == name).unwrap();
    let path = golden_dir().join(spec.file_name());
    let bytes = regenerate(spec);
    if bless_requested() {
        std::fs::write(&path, &bytes).unwrap();
    }
    let committed = std::fs::read(&path).expect("committed golden replay is present");
    assert!(
        committed == bytes,
        "{} differs from the replay regenerated from its parameters",
        spec.file_name()
    );
    let (code, _, err) = cli(&["verify", path.to_str().unwrap()]);
    assert_eq!(code, 0, "{err}");
}

#[test]
fn toy_small_regenerates_byte_for_byte() {
    check_regenerates("toy-small");
}

#[test]
fn toy_medium_regenerates_byte_for_byte() {
    check_regenerates("toy-medium");
}

/// 20,000 agents for 600 ticks is too slow for the debug suite; release builds run it, and CI
/// verifies the same file with the release binary through `tools/verify_goldens.sh`.
#[test]
#[cfg_attr(
    debug_assertions,
    ignore = "large golden: run with --release or --ignored"
)]
fn toy_large_regenerates_byte_for_byte() {
    check_regenerates("toy-large");
}

#[test]
fn golden_parameters_match_the_spec() {
    let got: Vec<(&str, u32, u64, u64, u32)> = GOLDENS
        .iter()
        .map(|g| (g.name, g.agents, g.ticks, g.seed, g.checkpoint_interval))
        .collect();
    assert_eq!(
        got,
        vec![
            ("toy-small", 100, 2000, 1, 100),
            ("toy-medium", 1000, 2000, 7, 100),
            ("toy-large", 20000, 600, 42, 100),
        ]
    );
}

#[test]
fn committed_files_agree_with_their_headers_and_expected_json() {
    let mut entries = Vec::new();
    for spec in GOLDENS {
        let bytes = std::fs::read(golden_dir().join(spec.file_name())).unwrap();
        assert!(spec.file_name().ends_with(".hdrp"));
        let replay = ReplayReader::parse(&bytes).unwrap();
        assert_eq!(replay.seed(), spec.seed);
        assert_eq!(replay.total_ticks(), spec.ticks);
        assert_eq!(replay.checkpoint_interval(), spec.checkpoint_interval);
        assert_eq!(
            replay.checkpoints().len() as u64,
            spec.ticks / u64::from(spec.checkpoint_interval)
        );
        assert_eq!(
            replay.checkpoints().last().unwrap().hash,
            replay.final_hash()
        );
        entries.push(GoldenEntry {
            spec,
            final_hash: replay.final_hash(),
        });
    }
    let rendered = expected_json(&entries);
    let path = golden_dir().join("expected.json");
    if bless_requested() {
        std::fs::write(&path, &rendered).unwrap();
    }
    let committed = std::fs::read_to_string(&path).expect("expected.json is present");
    assert_eq!(committed, rendered);
}
