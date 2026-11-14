//! Baseline for the boundary measurement: the same append, called directly from Rust with no C ABI,
//! no handle lookup, no guard and no error record. Boundary overhead = C++ append cost minus this.
//!
//!     cargo run --release --example native_bench -- [ops]

use dino8_ffi_probe::log::{D8ObjectId, Log, OpInput};
use std::time::Instant;

fn id(a: u64, b: u64) -> D8ObjectId {
    let mut bytes = [0u8; 16];
    bytes[..8].copy_from_slice(&a.to_le_bytes());
    bytes[8..].copy_from_slice(&b.to_le_bytes());
    D8ObjectId { bytes }
}

fn run(n: u64) -> f64 {
    let log = Log::open_memory();
    let mut params = [0u8; 64];
    for (i, b) in params.iter_mut().enumerate() {
        *b = (i * 7 + 1) as u8;
    }
    let start = Instant::now();
    for k in 0..n {
        params[..8].copy_from_slice(&k.to_le_bytes());
        let ins = [id(k, 1), id(k, 2)];
        let outs = [id(k, 3)];
        let seq = log
            .append(&OpInput {
                command_id: (k % 1070) as u32,
                tool_version: 800,
                random_seed: 0x9E37_79B9_7F4A_7C15,
                params: &params,
                inputs: &ins,
                outputs: &outs,
            })
            .expect("append");
        std::hint::black_box(seq);
    }
    let us = start.elapsed().as_secs_f64() * 1e6;
    assert_eq!(log.len(), n);
    us
}

fn main() {
    let n: u64 = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(1_000_000);
    let mut runs: Vec<f64> = (0..5).map(|_| run(n) / n as f64).collect();
    runs.sort_by(f64::total_cmp);
    println!(
        "native Rust append (no boundary): ops={n} mean_us min={:.4} median={:.4} max={:.4} (5 runs)",
        runs[0], runs[2], runs[4]
    );
}
