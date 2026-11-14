#![allow(dead_code)]
//! Shared helpers for the integration tests.

use oplog::engine::{Options, StepClock, Workspace};
use oplog::script::{Script, ScriptOptions, Step};
use oplog::value::Value;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static COUNTER: AtomicU64 = AtomicU64::new(0);

/// A scratch directory that is removed on drop.
pub struct TmpDir(pub PathBuf);

impl TmpDir {
    pub fn new(tag: &str) -> TmpDir {
        let n = COUNTER.fetch_add(1, Ordering::SeqCst);
        let p = std::env::temp_dir().join(format!("oplog-it-{}-{n}-{tag}", std::process::id()));
        let _ = std::fs::remove_dir_all(&p);
        std::fs::create_dir_all(&p).unwrap();
        TmpDir(p)
    }

    pub fn path(&self) -> &Path {
        &self.0
    }
}

impl Drop for TmpDir {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

pub fn clock() -> Box<StepClock> {
    Box::new(StepClock { next: 1_700_000_000_000, step: 7 })
}

pub fn mem(session: u64) -> Workspace {
    Workspace::in_memory(session, "test-doc").with_clock(clock())
}

pub fn open(dir: &Path, session: u64, opts: Options) -> (Workspace, oplog::engine::RecoveryReport) {
    let (ws, rep) = Workspace::open(dir, session, "test-doc", opts).unwrap();
    (ws.with_clock(clock()), rep)
}

pub fn opts(checkpoint_every: u32) -> Options {
    Options { checkpoint_every, ..Options::default() }
}

/// Run `n` script steps; returns every committed operation id with the state hash right after it.
pub fn drive(ws: &mut Workspace, script: &mut Script, n: usize) -> Vec<(oplog::ids::OpId, oplog::ids::Hash)> {
    let mut out = Vec::new();
    for _ in 0..n {
        if let Step::Op(id) = script.step(ws).unwrap() {
            out.push((id, ws.state_hash()));
        }
    }
    out
}

pub fn script(seed: u64, navigate: bool) -> Script {
    Script::new(seed, ScriptOptions { navigate, geom_bytes: 64, max_objects: 10_000, ..ScriptOptions::default() })
}

pub fn v3(a: f64, b: f64, c: f64) -> Value {
    Value::f3([a, b, c])
}

pub fn create_params(name: &str, pos: [f64; 3]) -> Value {
    Value::map([
        ("name", Value::str(name)),
        ("pos", Value::f3(pos)),
        ("size", Value::f3([1.0, 1.0, 1.0])),
        ("layer", Value::str("A-WALL")),
        ("color", Value::Int(7)),
        ("geom_bytes", Value::Int(0)),
        ("tags", Value::List(vec![])),
    ])
}
