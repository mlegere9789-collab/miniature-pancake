//! Crash-test writer: appends a deterministic editing session to a log directory and reports
//! each completed step on stdout so a parent process can kill it at an arbitrary moment.
//!
//! Usage: oplog-crashwriter --dir D --seed S --count N [--navigate] [--checkpoint-every C]
//!        [--sync none|every|<n>] [--slow-us U] [--tear-after K --tear-keep B]
//!
//! Output lines: `READY`, then `ACK <operations in the log>` after every completed step, and
//! `TEAR` (followed by a sleep, waiting to be killed) when an injected torn write was made.

use oplog::engine::{Options, StepClock, Workspace};
use oplog::script::{Script, ScriptOptions};
use oplog::value::Value;
use oplog::wal::SyncPolicy;
use std::io::Write;
use std::time::Duration;

fn arg(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).cloned()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let dir = arg(&args, "--dir").expect("--dir");
    let seed: u64 = arg(&args, "--seed").and_then(|s| s.parse().ok()).unwrap_or(1);
    let count: u64 = arg(&args, "--count").and_then(|s| s.parse().ok()).unwrap_or(1_000_000);
    let navigate = args.iter().any(|a| a == "--navigate");
    let checkpoint_every: u32 = arg(&args, "--checkpoint-every").and_then(|s| s.parse().ok()).unwrap_or(0);
    let slow_us: u64 = arg(&args, "--slow-us").and_then(|s| s.parse().ok()).unwrap_or(0);
    let tear_after: Option<u64> = arg(&args, "--tear-after").and_then(|s| s.parse().ok());
    let tear_keep: usize = arg(&args, "--tear-keep").and_then(|s| s.parse().ok()).unwrap_or(20);
    let sync = match arg(&args, "--sync").as_deref() {
        None | Some("none") => SyncPolicy::Never,
        Some("every") => SyncPolicy::EveryAppend,
        Some(n) => SyncPolicy::EveryN(n.parse().unwrap_or(8)),
    };

    let opts = Options { sync, checkpoint_every, ..Options::default() };
    let (ws, _) = Workspace::open(std::path::Path::new(&dir), 7, "crash-doc", opts).expect("open");
    let mut ws = ws.with_clock(Box::new(StepClock { next: 1_700_000_000_000 + 7 * 100_000, step: 7 }));
    if slow_us > 0 {
        ws.set_slow_write(Some(Duration::from_micros(slow_us)));
    }
    let mut script = Script::new(seed, ScriptOptions { navigate, geom_bytes: 96, max_objects: 5_000, ..ScriptOptions::default() });
    let out = std::io::stdout();
    let say = |s: &str| {
        let mut o = out.lock();
        let _ = writeln!(o, "{s}");
        let _ = o.flush();
    };
    say("READY");
    let mut ops_done = 0u64;
    for _ in 0..count {
        if tear_after == Some(ops_done) {
            ws.inject_tear(tear_keep);
            let params = Value::map([
                ("name", Value::str("torn")),
                ("pos", Value::f3([0.0; 3])),
                ("size", Value::f3([1.0; 3])),
                ("layer", Value::str("T")),
                ("color", Value::Int(1)),
                ("geom_bytes", Value::Int(300)),
            ]);
            let r = ws.run("create_box", params);
            assert!(r.is_err(), "the injected tear must fail the append");
            say("TEAR");
            std::thread::sleep(Duration::from_secs(60));
            return;
        }
        let before = ws.len();
        script.step(&mut ws).expect("step");
        ops_done += (ws.len() - before) as u64;
        say(&format!("ACK {}", ws.len()));
    }
}
