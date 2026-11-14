//! Benchmarks against the plan's gates (section 5.3):
//!   log append under 1 ms, undo/redo under 30 ms at 1 M operations, crash recovery time,
//!   replay determinism, merge cost, text serialization throughput.
//!
//! Usage: oplog-bench <append|big|merge|replay|jsonl> [--n N] [--objects N] [--dir D] [--keep]
//!
//! Every measurement prints a human-readable row and a `RESULT key=value ...` line.

use oplog::engine::{Options, Workspace};
use oplog::exec::Rng;
use oplog::ids::{ObjectId, OpId};
use oplog::script::{Script, ScriptOptions, Step};
use oplog::value::Value;
use oplog::wal::SyncPolicy;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------

#[repr(C)]
struct Timespec {
    tv_sec: i64,
    tv_nsec: i64,
}

extern "C" {
    fn clock_gettime(clk_id: i32, tp: *mut Timespec) -> i32;
}

/// CPU time consumed by this thread (excludes time spent descheduled). The machine this runs on is
/// shared and heavily loaded, so wall-clock tails include other processes; CPU time does not.
fn thread_cpu_ns() -> u64 {
    let mut ts = Timespec { tv_sec: 0, tv_nsec: 0 };
    // SAFETY: `ts` is a valid, writable timespec and CLOCK_THREAD_CPUTIME_ID (3) is a valid clock.
    unsafe { clock_gettime(3, &mut ts) };
    ts.tv_sec as u64 * 1_000_000_000 + ts.tv_nsec as u64
}

/// Paired wall-clock and thread-CPU latencies, in nanoseconds.
#[derive(Default)]
struct Lat {
    wall: Vec<u64>,
    cpu: Vec<u64>,
}

impl Lat {
    fn time<T>(&mut self, f: impl FnOnce() -> T) -> T {
        let c0 = thread_cpu_ns();
        let t = Instant::now();
        let r = f();
        let w = t.elapsed().as_nanos() as u64;
        self.cpu.push(thread_cpu_ns() - c0);
        self.wall.push(w);
        r
    }

    fn report(&mut self, label: &str) {
        let over_wall = self.wall.iter().filter(|x| **x > 1_000_000).count();
        let over_cpu = self.cpu.iter().filter(|x| **x > 1_000_000).count();
        let mut cpu = std::mem::take(&mut self.cpu);
        let mut wall = std::mem::take(&mut self.wall);
        row(&format!("{label} [wall]"), &summarize(&mut wall));
        row(&format!("{label} [cpu]"), &summarize(&mut cpu));
        println!("    over 1 ms: wall {over_wall}, thread-cpu {over_cpu} (of {})", wall.len());
    }
}

#[derive(Debug, Clone, Copy, Default)]
struct Summary {
    n: usize,
    mean: f64,
    p50: f64,
    p90: f64,
    p99: f64,
    p999: f64,
    max: f64,
}

fn summarize(v: &mut [u64]) -> Summary {
    if v.is_empty() {
        return Summary::default();
    }
    v.sort_unstable();
    let pct = |q: f64| v[((v.len() - 1) as f64 * q) as usize] as f64 / 1000.0;
    Summary {
        n: v.len(),
        mean: v.iter().sum::<u64>() as f64 / v.len() as f64 / 1000.0,
        p50: pct(0.50),
        p90: pct(0.90),
        p99: pct(0.99),
        p999: pct(0.999),
        max: *v.last().unwrap_or(&0) as f64 / 1000.0,
    }
}

fn row(label: &str, s: &Summary) {
    println!(
        "  {label:<46} n={:>8}  mean {:>9.1} us  p50 {:>9.1}  p90 {:>9.1}  p99 {:>9.1}  p99.9 {:>9.1}  max {:>10.1} us",
        s.n, s.mean, s.p50, s.p90, s.p99, s.p999, s.max
    );
    println!(
        "RESULT metric=\"{label}\" n={} mean_us={:.1} p50_us={:.1} p90_us={:.1} p99_us={:.1} p999_us={:.1} max_us={:.1}",
        s.n, s.mean, s.p50, s.p90, s.p99, s.p999, s.max
    );
}

fn status_kb(key: &str) -> u64 {
    std::fs::read_to_string("/proc/self/status")
        .ok()
        .and_then(|s| {
            s.lines()
                .find(|l| l.starts_with(key))
                .and_then(|l| l.split_whitespace().nth(1).and_then(|n| n.parse().ok()))
        })
        .unwrap_or(0)
}

fn mem_line(tag: &str) {
    println!("  memory {tag}: rss {} MB, peak rss {} MB", status_kb("VmRSS:") / 1024, status_kb("VmHWM:") / 1024);
    println!("RESULT metric=\"memory {tag}\" rss_mb={} peak_mb={}", status_kb("VmRSS:") / 1024, status_kb("VmHWM:") / 1024);
}

fn load_avg() -> String {
    std::fs::read_to_string("/proc/loadavg").map(|s| s.split_whitespace().take(3).collect::<Vec<_>>().join(" ")).unwrap_or_default()
}

fn arg(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).cloned()
}

fn fresh_dir(base: &Path, name: &str) -> PathBuf {
    let d = base.join(name);
    let _ = std::fs::remove_dir_all(&d);
    d
}

/// Evict a file from the page cache (only that file) so recovery is measured cold.
fn evict(path: &Path) -> bool {
    let _ = std::process::Command::new("sync").status();
    std::process::Command::new("dd")
        .args(["if=/dev/null", &format!("of={}", path.display()), "oflag=nocache", "conv=notrunc,fdatasync", "count=0"])
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn create_params(i: usize, rng: &mut Rng, geom: usize) -> Value {
    Value::map([
        ("name", Value::Str(format!("o{i}"))),
        ("pos", Value::f3([rng.next_f64() * 100.0, rng.next_f64() * 100.0, rng.next_f64() * 3.0])),
        ("size", Value::f3([1.0, 1.0, 1.0])),
        ("layer", Value::Str(format!("L{}", i % 8))),
        ("color", Value::Int((i % 256) as i64)),
        ("geom_bytes", Value::Int(geom as i64)),
        ("tags", Value::List(vec![Value::str("new")])),
    ])
}

/// Create `n` objects quickly; returns their ids.
fn populate(ws: &mut Workspace, n: usize, geom: usize) -> Vec<ObjectId> {
    let mut rng = Rng::new(7);
    let mut ids = Vec::with_capacity(n);
    for i in 0..n {
        let (actor, seq) = ws.next_dot();
        ids.push(ObjectId::derive(actor, seq, 0));
        ws.run("create_box", create_params(i, &mut rng, geom)).expect("populate");
    }
    ids
}

/// A pre-built editing workload: moves, property sets, tags and vertex inserts.
fn workload(ids: &[ObjectId], n: usize, seed: u64) -> Vec<(&'static str, Value, u64)> {
    let mut rng = Rng::new(seed);
    (0..n)
        .map(|_| {
            let id = ids[rng.below(ids.len() as u64) as usize];
            let seedv = rng.next_u64();
            match rng.below(100) {
                0..=59 => ("move", Value::map([("ids", Value::List(vec![Value::Obj(id)])), ("delta", Value::f3([rng.next_f64(), rng.next_f64(), 0.0]))]), seedv),
                60..=79 => ("set_prop", Value::map([("id", Value::Obj(id)), ("prop", Value::str("color")), ("value", Value::Int(rng.below(256) as i64))]), seedv),
                80..=89 => ("tag_add", Value::map([("id", Value::Obj(id)), ("tag", Value::Str(format!("t{}", rng.below(5))))]), seedv),
                _ => ("pts_insert", Value::map([("id", Value::Obj(id)), ("index", Value::Int(rng.below(3) as i64)), ("point", Value::f3([rng.next_f64(), 0.0, 0.0]))]), seedv),
            }
        })
        .collect()
}

fn time_ops(ws: &mut Workspace, work: Vec<(&'static str, Value, u64)>) -> Lat {
    let mut lat = Lat::default();
    lat.wall.reserve(work.len());
    lat.cpu.reserve(work.len());
    for (cmd, params, seed) in work {
        lat.time(|| ws.run_with(cmd, params, seed)).expect("workload command");
    }
    lat
}

// ---------------------------------------------------------------------------------------------
// A. append latency
// ---------------------------------------------------------------------------------------------

fn bench_append(base: &Path, n: usize) {
    println!("== A. append latency (one operation = dispatch + snapshot + state hash + write-ahead frame) ==");
    println!("  load average at start: {}", load_avg());
    let objects = 5_000;
    let modes: [(&str, Option<SyncPolicy>, u32, usize); 6] = [
        ("append, memory only (no log file)", None, 0, n),
        ("append, WAL write, no fsync (process-crash safe)", Some(SyncPolicy::Never), 0, n),
        ("append, WAL + group fsync every 32", Some(SyncPolicy::EveryN(32)), 0, n),
        ("append, WAL + background fdatasync every 20 ms", Some(SyncPolicy::Background(Duration::from_millis(20))), 0, n),
        ("append, WAL + fdatasync every operation", Some(SyncPolicy::EveryAppend), 0, (n / 20).max(500)),
        ("append, WAL no fsync, checkpoint every 4096", Some(SyncPolicy::Never), 4096, n),
    ];
    for (label, sync, ckpt, count) in modes {
        let opts = Options { sync: sync.unwrap_or(SyncPolicy::Never), checkpoint_every: ckpt, ..Options::default() };
        let mut ws = match sync {
            None => Workspace::in_memory_with(1, "bench", opts),
            Some(_) => {
                let d = fresh_dir(base, "append");
                Workspace::open(&d, 1, "bench", opts).expect("open").0
            }
        };
        let ids = populate(&mut ws, objects, 256);
        let work = workload(&ids, count, 99);
        let mut lat = time_ops(&mut ws, work);
        lat.report(label);
        println!("    wal bytes/op {:.0}", ws.wal_len().map_or(0.0, |w| w as f64 / ws.len() as f64));
        drop(ws);
        let _ = std::fs::remove_dir_all(base.join("append"));
    }
    // checkpoint cost on a large document
    let d = fresh_dir(base, "ckpt");
    let (mut ws, _) = Workspace::open(&d, 1, "bench", Options { checkpoint_every: 0, ..Options::default() }).expect("open");
    let ids = populate(&mut ws, 100_000, 64);
    let t = Instant::now();
    ws.checkpoint_now().expect("checkpoint");
    let first = t.elapsed();
    let w1 = ws.wal_len().unwrap_or(0);
    let work = workload(&ids, 4096, 5);
    let _ = time_ops(&mut ws, work);
    let before = ws.wal_len().unwrap_or(0);
    let t = Instant::now();
    ws.checkpoint_now().expect("checkpoint");
    let incr = t.elapsed();
    let after = ws.wal_len().unwrap_or(0);
    println!(
        "  checkpoint of a 100,000-object document: first (all nodes) {:.1} ms; incremental after 4,096 edits {:.1} ms, {} kB written (first: {} kB)",
        first.as_secs_f64() * 1e3,
        incr.as_secs_f64() * 1e3,
        (after - before) / 1024,
        (w1.saturating_sub(0)) / 1024
    );
    println!("RESULT metric=\"checkpoint\" first_ms={:.1} incremental_ms={:.1} incremental_kb={}", first.as_secs_f64() * 1e3, incr.as_secs_f64() * 1e3, (after - before) / 1024);
    drop(ws);
    let _ = std::fs::remove_dir_all(&d);
}

// ---------------------------------------------------------------------------------------------
// B. the big log
// ---------------------------------------------------------------------------------------------

struct WalKinds {
    ops: (u64, u64),
    refs: (u64, u64),
    ckpt: (u64, u64),
}

/// Walk the frames of a log file and sum their sizes by kind (counts, bytes).
fn wal_kinds(path: &Path) -> WalKinds {
    use std::io::{BufReader, Read};
    let mut r = BufReader::with_capacity(1 << 20, std::fs::File::open(path).expect("open wal"));
    let mut hdr = [0u8; 16];
    r.read_exact(&mut hdr).expect("header");
    let mut k = WalKinds { ops: (0, 0), refs: (0, 0), ckpt: (0, 0) };
    let mut buf = Vec::new();
    loop {
        let mut l = [0u8; 4];
        if r.read_exact(&mut l).is_err() {
            break;
        }
        let len = u32::from_le_bytes(l) as usize;
        buf.resize(len + 8, 0);
        if r.read_exact(&mut buf).is_err() {
            break;
        }
        let slot = match buf[0] {
            1 => &mut k.ops,
            2 => &mut k.refs,
            _ => &mut k.ckpt,
        };
        slot.0 += 1;
        slot.1 += len as u64 + 12;
    }
    k
}

fn bench_big(base: &Path, n: usize, objects: usize, keep: bool, do_replay: bool) {
    println!("== B. large log: {n} operations over up to {objects} objects ==");
    println!("  load average at start: {}", load_avg());
    let dir = fresh_dir(base, "big");
    let opts = Options { sync: SyncPolicy::Never, checkpoint_every: 4096, ..Options::default() };
    let (mut ws, _) = Workspace::open(&dir, 1, "big", opts.clone()).expect("open");
    let mut script = Script::new(
        20260,
        ScriptOptions { navigate: true, geom_bytes: 256, max_objects: objects, nav_permille: 20 },
    );
    let t0 = Instant::now();
    let mut step_lat: Vec<u64> = Vec::with_capacity(n);
    let (mut undos, mut redos, mut branches) = (0u64, 0u64, 0u64);
    while ws.len() < n {
        let t = Instant::now();
        let st = script.step(&mut ws).expect("step");
        if let Step::Op(_) = st {
            step_lat.push(t.elapsed().as_nanos() as u64);
        }
        match st {
            Step::Undo => undos += 1,
            Step::Redo => redos += 1,
            Step::Branch => branches += 1,
            _ => {}
        }
        if ws.len() % 200_000 == 0 && step_lat.len() % 5 == 0 && ws.len() > 1 {
            println!("    ... {} operations, {:.1} s, rss {} MB", ws.len(), t0.elapsed().as_secs_f64(), status_kb("VmRSS:") / 1024);
        }
    }
    let build = t0.elapsed();
    println!(
        "  built {} operations ({} objects) in {:.1} s = {:.0} ops/s; navigation steps during the build: {undos} undo, {redos} redo, {branches} branches",
        ws.len(),
        ws.object_count(),
        build.as_secs_f64(),
        ws.len() as f64 / build.as_secs_f64()
    );
    let mut sl = step_lat.clone();
    row("script step latency incl. parameter building", &summarize(&mut sl));
    let wal = ws.wal_len().unwrap_or(0);
    let kinds = wal_kinds(&dir.join("oplog.wal"));
    println!(
        "  log file: {:.1} MB = {:.0} bytes/op; operation frames {} ({:.1} MB, {:.0} B each), refs {} ({:.2} MB), checkpoints {} ({:.1} MB)",
        wal as f64 / 1e6,
        wal as f64 / ws.len() as f64,
        kinds.ops.0,
        kinds.ops.1 as f64 / 1e6,
        kinds.ops.1 as f64 / kinds.ops.0.max(1) as f64,
        kinds.refs.0,
        kinds.refs.1 as f64 / 1e6,
        kinds.ckpt.0,
        kinds.ckpt.1 as f64 / 1e6
    );
    println!(
        "RESULT metric=\"big log\" ops={} objects={} build_s={:.1} wal_mb={:.1} bytes_per_op={:.0} op_frame_bytes={:.0} ckpt_frames={} ckpt_mb={:.1} leaves={} blobs={}",
        ws.len(),
        ws.object_count(),
        build.as_secs_f64(),
        wal as f64 / 1e6,
        wal as f64 / ws.len() as f64,
        kinds.ops.1 as f64 / kinds.ops.0.max(1) as f64,
        kinds.ckpt.0,
        kinds.ckpt.1 as f64 / 1e6,
        ws.leaves().len(),
        ws.store().len()
    );
    // sharing: what snapshot-per-operation without sharing would cost
    let mut live_bytes = 0u64;
    for (id, _) in ws.objects().expect("objects").iter().take(2000) {
        let h = ws.object_hash(*id).expect("hash").expect("present");
        live_bytes += ws.store().get(&h).map(|b| b.len() as u64).unwrap_or(0);
    }
    let avg_obj = live_bytes as f64 / 2000.0;
    let naive = avg_obj * ws.object_count() as f64 * ws.len() as f64;
    println!(
        "  structural sharing: {} distinct blobs, {:.1} MB stored; a full snapshot per operation without sharing would be about {:.1} TB ({:.0} x more)",
        ws.store().len(),
        ws.store().stored_bytes() as f64 / 1e6,
        naive / 1e12,
        naive / ws.store().stored_bytes().max(1) as f64
    );
    println!(
        "RESULT metric=\"sharing\" blobs={} stored_mb={:.1} naive_tb={:.2} ratio={:.0}",
        ws.store().len(),
        ws.store().stored_bytes() as f64 / 1e6,
        naive / 1e12,
        naive / ws.store().stored_bytes().max(1) as f64
    );
    mem_line("after build");

    // ---- undo / redo -----------------------------------------------------------------------
    println!("  -- undo / redo (gate: under 30 ms) at {} operations --", ws.len());
    let main_chain: Vec<OpId> = ws.path_to(ws.head()).expect("path");
    let materialize = |ws: &Workspace, ids: &[ObjectId]| {
        for id in ids {
            let _ = ws.object(*id).expect("object");
        }
    };
    for &depth in &[1_000usize, 100_000, 500_000, main_chain.len() - 1] {
        let depth = depth.min(main_chain.len() - 1);
        ws.checkout(main_chain[depth]).expect("checkout");
        let (mut u, mut um, mut r, mut rm) = (Lat::default(), Lat::default(), Lat::default(), Lat::default());
        for _ in 0..2000 {
            let nav = u.time(|| ws.undo()).expect("undo").expect("has parent");
            um.time(|| materialize(&ws, &nav.changed));
            let nav = r.time(|| ws.redo()).expect("redo").expect("has child");
            rm.time(|| materialize(&ws, &nav.changed));
        }
        u.report(&format!("undo at depth {depth}"));
        um.report(&format!("materialize the objects an undo changed, depth {depth}"));
        r.report(&format!("redo at depth {depth}"));
        rm.report(&format!("materialize the objects a redo changed, depth {depth}"));
    }
    // a long consecutive walk back (each step is a different operation)
    ws.checkout(*main_chain.last().expect("head")).expect("checkout head");
    let mut walk = Lat::default();
    for _ in 0..20_000 {
        if walk.time(|| ws.undo()).expect("undo").is_none() {
            break;
        }
    }
    walk.report("undo, 20,000 consecutive steps from the head");

    // ---- time travel --------------------------------------------------------------------------
    println!("  -- time travel to arbitrary operations --");
    let mut rng = Rng::new(31337);
    let ids = ws.op_ids();
    let mut buckets: [Vec<u64>; 4] = Default::default();
    let mut jump_changed: Vec<usize> = Vec::new();
    let mut at = ws.head_depth();
    for _ in 0..300 {
        let target = ids[rng.below(ids.len() as u64) as usize];
        let d = ws.depth(target).expect("depth");
        let dist = (i64::from(d) - i64::from(at)).unsigned_abs();
        let t = Instant::now();
        let nav = ws.checkout(target).expect("checkout");
        let el = t.elapsed().as_nanos() as u64;
        at = d;
        let b = match dist {
            0..=256 => 0,
            257..=4096 => 1,
            4097..=65_536 => 2,
            _ => 3,
        };
        buckets[b].push(el);
        jump_changed.push(nav.changed.len());
    }
    for (i, name) in ["<= 256 levels away (walk)", "257..4096 levels (walk or checkpoint)", "4097..65536 levels (checkpoint + diff)", "> 65536 levels (checkpoint + diff)"].iter().enumerate() {
        if !buckets[i].is_empty() {
            row(&format!("checkout, {name}"), &summarize(&mut buckets[i]));
        }
    }
    jump_changed.sort_unstable();
    println!("  objects changed per jump: median {}, max {}", jump_changed[jump_changed.len() / 2], jump_changed.last().unwrap_or(&0));
    // state at an arbitrary operation without moving the head
    let mut peek = Vec::new();
    for _ in 0..200 {
        let target = ids[rng.below(ids.len() as u64) as usize];
        let t = Instant::now();
        let _ = ws.state_hash_at(target).expect("state");
        peek.push(t.elapsed().as_nanos() as u64);
    }
    row("state hash at an arbitrary operation (no head move)", &summarize(&mut peek));
    // branching: time travel to an old operation, then edit
    let mut br = Vec::new();
    let all_ids: Vec<ObjectId> = ws.objects().expect("objects").iter().take(1000).map(|x| x.0).collect();
    let work = workload(&all_ids, 100, 8);
    for (cmd, params, seed) in work {
        let target = ids[rng.below(ids.len() as u64) as usize];
        if ws.checkout(target).is_err() {
            continue;
        }
        let t = Instant::now();
        let r = ws.run_with(cmd, params, seed);
        if r.is_ok() {
            br.push(t.elapsed().as_nanos() as u64);
        }
    }
    row("append on a new branch right after time travel", &summarize(&mut br));
    ws.checkout(*main_chain.last().expect("head")).expect("back to the head");
    mem_line("after navigation");

    // ---- recovery -------------------------------------------------------------------------------
    let (head, state, len, leaves) = (ws.head(), ws.state_hash(), ws.len(), ws.leaves().len());
    drop(ws);
    let wal_path = dir.join("oplog.wal");
    println!("  -- recovery of a {len}-operation log ({:.1} MB) --", std::fs::metadata(&wal_path).map(|m| m.len()).unwrap_or(0) as f64 / 1e6);
    let evicted = evict(&wal_path);
    for (label, cold) in [("cold (log evicted from the page cache)", true), ("warm (log in the page cache)", false)] {
        if cold && !evicted {
            println!("  (could not evict the page cache for this file; skipping the cold measurement)");
            continue;
        }
        let t = Instant::now();
        let (ws2, rep) = Workspace::open(&dir, 1, "big", opts.clone()).expect("recover");
        let total = t.elapsed();
        assert_eq!((ws2.head(), ws2.state_hash(), ws2.len(), ws2.leaves().len()), (head, state, len, leaves), "recovery must restore the exact state");
        println!(
            "  recovery {label}: total {:.2} s  (scan+index {:.2} s for {} frames, state restore {:.0} ms, {} checkpoints, discarded {} bytes)",
            total.as_secs_f64(),
            rep.scan_ms / 1e3,
            rep.frames,
            rep.state_ms,
            rep.checkpoints,
            rep.discarded_bytes
        );
        println!(
            "RESULT metric=\"recovery {}\" ops={} total_s={:.2} scan_s={:.2} state_ms={:.0} frames={} mb_per_s={:.0}",
            if cold { "cold" } else { "warm" },
            len,
            total.as_secs_f64(),
            rep.scan_ms / 1e3,
            rep.state_ms,
            rep.frames,
            std::fs::metadata(&wal_path).map(|m| m.len()).unwrap_or(0) as f64 / 1e6 / total.as_secs_f64()
        );
        if !cold {
            mem_line("after recovery");
            let t = Instant::now();
            let v = ws2.verify().expect("verify");
            println!("  full integrity pass (re-hash every record and blob, rebuild head state): {:.2} s ({} ops, {} blobs)", t.elapsed().as_secs_f64(), v.ops, v.blobs);
            println!("RESULT metric=\"full verify\" ops={} blobs={} s={:.2}", v.ops, v.blobs, t.elapsed().as_secs_f64());
            if do_replay {
                let t = Instant::now();
                let rep = ws2.replay_check(&oplog::commands::standard()).expect("replay");
                println!(
                    "  deterministic replay of all {} operations from an empty document: {:.1} s ({:.0} ops/s), mismatches: {}, final state matches: {}",
                    rep.ops,
                    t.elapsed().as_secs_f64(),
                    rep.ops_per_second(),
                    rep.mismatches.len(),
                    rep.final_root == ws2.state_hash()
                );
                println!("RESULT metric=\"replay big\" ops={} s={:.1} ops_per_s={:.0} mismatches={}", rep.ops, t.elapsed().as_secs_f64(), rep.ops_per_second(), rep.mismatches.len());
            }
        }
        drop(ws2);
    }
    // torn tail at the end of a large log
    {
        let f = std::fs::OpenOptions::new().append(true).open(&wal_path).expect("open for tear");
        use std::io::Write;
        (&f).write_all(&[0xAB; 777]).expect("write torn tail");
        drop(f);
        let t = Instant::now();
        let (ws3, rep) = Workspace::open(&dir, 1, "big", opts).expect("recover torn");
        println!("  recovery with a 777-byte torn tail: {:.2} s, discarded {} bytes, {} operations intact", t.elapsed().as_secs_f64(), rep.discarded_bytes, ws3.len());
        assert_eq!(ws3.len(), len);
    }
    if !keep {
        let _ = std::fs::remove_dir_all(&dir);
    }
}

// ---------------------------------------------------------------------------------------------
// C. merge
// ---------------------------------------------------------------------------------------------

fn bench_merge(objects: usize) {
    println!("== C. two-replica merge on a document of {objects} objects ==");
    let mut a = Workspace::in_memory(11, "merge");
    let ids = populate(&mut a, objects, 64);
    for edits in [100usize, 1_000, 10_000, 50_000] {
        let mut a2 = Workspace::in_memory(11, "merge");
        let ids2 = populate(&mut a2, objects, 64);
        assert_eq!(ids, ids2);
        let mut b = Workspace::in_memory(22, "merge");
        let t = Instant::now();
        b.pull_from(&a2).expect("pull");
        let pull = t.elapsed();
        b.checkout(a2.head()).expect("checkout");
        let wa = workload(&ids, edits, 1);
        let wb = workload(&ids, edits, 2);
        for (cmd, p, s) in wa {
            a2.run_with(cmd, p, s).expect("edit a");
        }
        for (cmd, p, s) in wb {
            b.run_with(cmd, p, s).expect("edit b");
        }
        let hb = b.head();
        let t = Instant::now();
        a2.pull_from(&b).expect("pull back");
        let sync = t.elapsed();
        let t = Instant::now();
        let out = a2.merge(hb).expect("merge");
        let merge = t.elapsed();
        println!(
            "  {edits:>6} edits per side: merge {:>8.1} ms ({} objects changed, {} conflict items); initial sync of {} ops {:.0} ms; sync of {} ops {:.0} ms",
            merge.as_secs_f64() * 1e3,
            out.changed,
            out.conflicts.len(),
            objects,
            pull.as_secs_f64() * 1e3,
            edits,
            sync.as_secs_f64() * 1e3
        );
        println!(
            "RESULT metric=\"merge\" objects={objects} edits_per_side={edits} merge_ms={:.1} changed={} conflicts={} sync_ms={:.0}",
            merge.as_secs_f64() * 1e3,
            out.changed,
            out.conflicts.len(),
            sync.as_secs_f64() * 1e3
        );
    }
}

// ---------------------------------------------------------------------------------------------
// D. replay and E. text round trip
// ---------------------------------------------------------------------------------------------

fn bench_replay(n: usize) {
    println!("== D. deterministic replay of {n} recorded operations ==");
    let mut ws = Workspace::in_memory_with(3, "replay", Options { checkpoint_every: 4096, ..Options::default() });
    let mut sc = Script::new(5, ScriptOptions { navigate: true, geom_bytes: 128, max_objects: 20_000, nav_permille: 30 });
    while ws.len() < n {
        sc.step(&mut ws).expect("step");
    }
    let t = Instant::now();
    let rep = ws.replay_check(&oplog::commands::standard()).expect("replay");
    println!(
        "  re-executed {} commands ({} merges) in {:.2} s = {:.0} ops/s; mismatches {}; final state equals recorded head state: {}",
        rep.executed,
        rep.merges,
        t.elapsed().as_secs_f64(),
        rep.ops_per_second(),
        rep.mismatches.len(),
        rep.final_root == ws.state_hash()
    );
    println!("RESULT metric=\"replay\" ops={} executed={} s={:.2} ops_per_s={:.0} mismatches={}", rep.ops, rep.executed, t.elapsed().as_secs_f64(), rep.ops_per_second(), rep.mismatches.len());
}

fn bench_jsonl(n: usize) {
    println!("== E. canonical JSON lines for {n} operations ==");
    let mut ws = Workspace::in_memory(3, "text");
    let mut sc = Script::new(6, ScriptOptions { navigate: true, geom_bytes: 128, max_objects: 20_000, nav_permille: 30 });
    while ws.len() < n {
        sc.step(&mut ws).expect("step");
    }
    let t = Instant::now();
    let text = ws.export_jsonl().expect("export");
    let ex = t.elapsed();
    let doc = oplog::export::jsonl_doc_id(&text).expect("doc");
    let mut back = Workspace::in_memory(4, &doc);
    let t = Instant::now();
    oplog::export::import_jsonl(&mut back, &text).expect("import");
    let im = t.elapsed();
    let same = back.export_jsonl().expect("re-export") == text;
    println!(
        "  export {:.2} s ({:.1} MB, {:.0} bytes/op, {} lines), import {:.2} s, second export byte-identical: {same}, state equal: {}",
        ex.as_secs_f64(),
        text.len() as f64 / 1e6,
        text.len() as f64 / ws.len() as f64,
        text.lines().count(),
        im.as_secs_f64(),
        back.state_hash() == ws.state_hash()
    );
    println!("RESULT metric=\"jsonl\" ops={} mb={:.1} export_s={:.2} import_s={:.2} identical={same}", ws.len(), text.len() as f64 / 1e6, ex.as_secs_f64(), im.as_secs_f64());
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let cmd = args.get(1).map_or("help", String::as_str);
    let n: usize = arg(&args, "--n").and_then(|s| s.parse().ok()).unwrap_or(0);
    let objects: usize = arg(&args, "--objects").and_then(|s| s.parse().ok()).unwrap_or(100_000);
    let base = PathBuf::from(arg(&args, "--dir").unwrap_or_else(|| std::env::temp_dir().join(format!("oplog-bench-{}", std::process::id())).display().to_string()));
    std::fs::create_dir_all(&base).expect("bench dir");
    let keep = args.iter().any(|a| a == "--keep");
    let t0 = Instant::now();
    match cmd {
        "append" => bench_append(&base, if n == 0 { 100_000 } else { n }),
        "big" => bench_big(&base, if n == 0 { 1_000_000 } else { n }, objects, keep, args.iter().any(|a| a == "--replay")),
        "merge" => bench_merge(if n == 0 { 100_000 } else { n }),
        "replay" => bench_replay(if n == 0 { 100_000 } else { n }),
        "jsonl" => bench_jsonl(if n == 0 { 100_000 } else { n }),
        _ => {
            eprintln!("usage: oplog-bench <append|big|merge|replay|jsonl> [--n N] [--objects N] [--dir D] [--keep] [--replay]");
            std::process::exit(2);
        }
    }
    if !keep {
        let _ = std::fs::remove_dir_all(&base);
    }
    println!("(total {:.1} s; load average at end {})", t0.elapsed().as_secs_f64(), load_avg());
}
