//! Helpers shared by the integration tests.
#![allow(dead_code)]

use hd_sim::cli::run_cli;
use sim_state::fnv1a64;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU32, Ordering};

static COUNTER: AtomicU32 = AtomicU32::new(0);

/// A unique scratch path in the system temp directory.
pub fn tmp_path(tag: &str) -> PathBuf {
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    std::env::temp_dir().join(format!(
        "hd-sim-test-{}-{}-{}.hdrp",
        std::process::id(),
        tag,
        n
    ))
}

/// Runs the CLI in-process; returns `(exit code, stdout, stderr)`.
pub fn cli(args: &[&str]) -> (i32, String, String) {
    let args: Vec<String> = args.iter().map(|s| (*s).to_string()).collect();
    let mut out = Vec::new();
    let mut err = Vec::new();
    let code = run_cli(&args, &mut out, &mut err);
    (
        code,
        String::from_utf8(out).expect("stdout is utf-8"),
        String::from_utf8(err).expect("stderr is utf-8"),
    )
}

/// Runs the compiled `hd-sim` binary as a separate process; returns `(exit code, stdout, stderr)`.
pub fn process(args: &[&str]) -> (i32, String, String) {
    let output = Command::new(env!("CARGO_BIN_EXE_hd-sim"))
        .args(args)
        .output()
        .expect("spawn hd-sim");
    (
        output.status.code().expect("exit code"),
        String::from_utf8_lossy(&output.stdout).into_owned(),
        String::from_utf8_lossy(&output.stderr).into_owned(),
    )
}

/// Recomputes the trailing body hash of a replay held in memory.
pub fn fix_body_hash(bytes: &mut [u8]) {
    let n = bytes.len();
    let h = fnv1a64(&bytes[..n - 8]);
    bytes[n - 8..].copy_from_slice(&h.to_le_bytes());
}

/// Byte offset of the hash field of checkpoint `index` in a replay that has `count` checkpoints.
/// The checkpoints sit directly before the 24-byte footer, 16 bytes each (tick, hash).
pub fn checkpoint_hash_offset(len: usize, count: usize, index: usize) -> usize {
    len - 24 - 16 * (count - index) + 8
}
