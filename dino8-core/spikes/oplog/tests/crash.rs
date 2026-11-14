//! Crash-recovery tests: kill a writer process in the middle of appending and check that the
//! log recovers to the last complete operation, byte-exactly, with nothing invented.

mod common;

use common::*;
use oplog::engine::{Options, StepClock, Workspace};
use oplog::script::{Script, ScriptOptions};
use oplog::wal::SyncPolicy;
use std::io::{BufRead, BufReader};
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

const WRITER: &str = env!("CARGO_BIN_EXE_oplog-crashwriter");

struct Writer {
    child: Child,
    lines: Arc<Mutex<Vec<String>>>,
    reader: Option<JoinHandle<()>>,
}

impl Writer {
    fn spawn(dir: &Path, args: &[&str]) -> Writer {
        let mut child = Command::new(WRITER)
            .arg("--dir")
            .arg(dir)
            .args(args)
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .spawn()
            .expect("spawn crash writer");
        let out = child.stdout.take().unwrap();
        let lines = Arc::new(Mutex::new(Vec::new()));
        let sink = lines.clone();
        let reader = std::thread::spawn(move || {
            for l in BufReader::new(out).lines() {
                match l {
                    Ok(l) => sink.lock().unwrap().push(l),
                    Err(_) => break,
                }
            }
        });
        Writer { child, lines, reader: Some(reader) }
    }

    fn wait_for(&self, what: impl Fn(&[String]) -> bool) {
        let t0 = Instant::now();
        loop {
            if what(&self.lines.lock().unwrap()) {
                return;
            }
            assert!(t0.elapsed() < Duration::from_secs(60), "writer produced no progress");
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    /// SIGKILL the child and return every line it managed to print.
    fn kill(mut self) -> Vec<String> {
        let _ = self.child.kill();
        let _ = self.child.wait();
        if let Some(r) = self.reader.take() {
            r.join().unwrap();
        }
        let v = self.lines.lock().unwrap().clone();
        v
    }
}

fn last_ack(lines: &[String]) -> usize {
    lines
        .iter()
        .rev()
        .find_map(|l| l.strip_prefix("ACK ").and_then(|n| n.parse().ok()))
        .unwrap_or(1)
}

fn acks(lines: &[String]) -> usize {
    lines.iter().filter(|l| l.starts_with("ACK ")).count()
}

/// Regenerate, in memory, the same session the writer was running.
fn regenerate(seed: u64, navigate: bool, until_len: usize) -> Workspace {
    let mut ws = Workspace::in_memory(7, "crash-doc")
        .with_clock(Box::new(StepClock { next: 1_700_000_000_000 + 7 * 100_000, step: 7 }));
    let mut sc = Script::new(seed, ScriptOptions { navigate, geom_bytes: 96, max_objects: 5_000, ..ScriptOptions::default() });
    while ws.len() < until_len {
        sc.step(&mut ws).unwrap();
    }
    ws
}

#[test]
fn sigkill_during_appends_recovers_exactly_a_prefix_of_the_session() {
    let mut torn_runs = 0;
    let mut discarded_total = 0;
    for i in 0..14u64 {
        let tmp = TmpDir::new("kill");
        let seed = 1000 + i;
        let slow = if i % 2 == 0 { "150" } else { "0" };
        let ckpt = if i % 3 == 0 { "8" } else { "0" };
        let sync = if i % 5 == 0 { "every" } else { "none" };
        let w = Writer::spawn(
            tmp.path(),
            &["--seed", &seed.to_string(), "--slow-us", slow, "--checkpoint-every", ckpt, "--sync", sync],
        );
        w.wait_for(|l| acks(l) >= 25 + 3 * i as usize);
        // kill at a pseudo-random moment
        std::thread::sleep(Duration::from_micros(300 + (i * 7919) % 6000));
        let lines = w.kill();
        let acked = last_ack(&lines);

        let (mut ws, rep) = open(tmp.path(), 8, opts(0));
        let recovered = ws.len();
        assert!(
            recovered == acked || recovered == acked + 1,
            "run {i}: recovered {recovered} operations, writer acknowledged {acked}"
        );
        if rep.discarded_bytes > 0 {
            torn_runs += 1;
            discarded_total += rep.discarded_bytes;
            let side = rep.discarded_path.as_ref().expect("discarded tail is preserved");
            assert_eq!(std::fs::metadata(side).unwrap().len(), rep.discarded_bytes);
        }
        // byte-exact prefix: same operation ids, same head, same state
        let expect = regenerate(seed, false, recovered);
        assert_eq!(ws.op_ids(), expect.op_ids(), "run {i}: operation ids differ from the regenerated session");
        assert_eq!(ws.head(), expect.head());
        assert_eq!(ws.state_hash(), expect.state_hash(), "run {i}");
        ws.verify().unwrap();
        // the repaired log accepts more work, and a second reopen is clean
        let mut sc = script(seed + 5000, false);
        let _ = drive(&mut ws, &mut sc, 12);
        let (len, head, state) = (ws.len(), ws.head(), ws.state_hash());
        drop(ws);
        let (ws2, rep2) = open(tmp.path(), 8, opts(0));
        assert_eq!(rep2.discarded_bytes, 0, "run {i}: second recovery must be clean");
        assert_eq!((ws2.len(), ws2.head(), ws2.state_hash()), (len, head, state));
        ws2.verify().unwrap();
    }
    println!("sigkill runs with a torn tail: {torn_runs}/14, bytes discarded in total: {discarded_total}");
}

#[test]
fn an_injected_torn_append_is_discarded_and_everything_before_it_survives() {
    for (n, keep) in [1usize, 3, 4, 5, 20, 100, 250].into_iter().enumerate() {
        let tmp = TmpDir::new("tear");
        let after = 15 + n as u64;
        let w = Writer::spawn(
            tmp.path(),
            &["--seed", "42", "--tear-after", &after.to_string(), "--tear-keep", &keep.to_string()],
        );
        w.wait_for(|l| l.iter().any(|x| x == "TEAR"));
        let lines = w.kill();
        assert_eq!(last_ack(&lines), 1 + after as usize, "all {after} operations before the tear were acknowledged");
        let (ws, rep) = open(tmp.path(), 8, opts(0));
        assert_eq!(ws.len(), 1 + after as usize, "keep={keep}: exactly the acknowledged operations survive");
        assert_eq!(rep.discarded_bytes, keep as u64, "keep={keep}: the partial frame is cut off exactly");
        let side = rep.discarded_path.unwrap();
        assert_eq!(std::fs::metadata(side).unwrap().len(), keep as u64);
        let expect = regenerate(42, false, ws.len());
        assert_eq!(ws.op_ids(), expect.op_ids());
        assert_eq!(ws.state_hash(), expect.state_hash());
        ws.verify().unwrap();
    }
}

#[test]
fn sigkill_with_undo_redo_branches_and_checkpoints_keeps_the_log_consistent() {
    for i in 0..8u64 {
        let tmp = TmpDir::new("killnav");
        let w = Writer::spawn(
            tmp.path(),
            &["--seed", &(300 + i).to_string(), "--navigate", "--checkpoint-every", "8", "--slow-us", "100"],
        );
        w.wait_for(|l| acks(l) >= 60 + 10 * i as usize);
        std::thread::sleep(Duration::from_micros(200 + (i * 4909) % 5000));
        let lines = w.kill();
        let acked = last_ack(&lines);
        let (mut ws, _) = open(tmp.path(), 8, opts(8));
        assert!(ws.len() >= acked && ws.len() <= acked + 1, "run {i}: {} vs acked {acked}", ws.len());
        ws.verify().unwrap();
        let rep = ws.replay_check(&oplog::commands::standard()).unwrap();
        assert!(rep.ok(), "run {i}: {:?}", rep.mismatches);
        // the recovered head is a real, replayable state; keep working from it
        let mut sc = script(900 + i, true);
        drive(&mut ws, &mut sc, 30);
        ws.verify().unwrap();
    }
}

#[test]
fn truncating_the_log_at_any_byte_gives_a_valid_prefix() {
    let tmp = TmpDir::new("cuts");
    let (mut ws, _) = open(tmp.path(), 5, opts(8));
    let mut sc = script(4242, true);
    drive(&mut ws, &mut sc, 60);
    let ids = ws.op_ids();
    drop(ws);
    let wal = tmp.path().join("oplog.wal");
    let full = std::fs::read(&wal).unwrap();
    // every byte of the last 3 kB, then a sparse sweep over the rest
    let mut cuts: Vec<usize> = (full.len().saturating_sub(3000)..=full.len()).collect();
    cuts.extend((0..full.len().saturating_sub(3000)).step_by(131));
    cuts.sort_unstable();
    let mut prev_len = 0;
    for cut in cuts {
        let d = TmpDir::new("cut");
        std::fs::write(d.path().join("oplog.wal"), &full[..cut]).unwrap();
        let (ws, _) = open(d.path(), 6, opts(8));
        let got = ws.op_ids();
        assert_eq!(got[..], ids[..got.len()], "cut at {cut}: recovered operations are not a prefix");
        assert!(got.len() >= prev_len, "cut at {cut}: recovery must be monotone in the cut position");
        prev_len = got.len();
        ws.verify().unwrap();
    }
    assert_eq!(prev_len, ids.len());
}

#[test]
fn a_flipped_byte_cuts_the_log_at_that_frame_and_keeps_the_evidence() {
    let tmp = TmpDir::new("flip");
    let (mut ws, _) = open(tmp.path(), 5, opts(0));
    let mut sc = script(77, false);
    drive(&mut ws, &mut sc, 50);
    let ids = ws.op_ids();
    drop(ws);
    let wal = tmp.path().join("oplog.wal");
    let mut data = std::fs::read(&wal).unwrap();
    let at = data.len() / 2;
    data[at] ^= 0x10;
    std::fs::write(&wal, &data).unwrap();
    let (ws, rep) = open(tmp.path(), 6, opts(0));
    assert!(ws.len() < ids.len() && ws.len() > 1);
    assert_eq!(ws.op_ids()[..], ids[..ws.len()]);
    assert!(rep.discarded_bytes as usize >= data.len() / 2 - 400);
    assert!(rep.discarded_path.unwrap().exists());
    ws.verify().unwrap();
}

#[test]
fn power_loss_policy_syncs_on_every_append() {
    let tmp = TmpDir::new("sync");
    let o = Options { sync: SyncPolicy::EveryAppend, checkpoint_every: 0, ..Options::default() };
    let (mut ws, _) = open(tmp.path(), 5, o);
    let before = ws.wal_syncs();
    let mut sc = script(1, false);
    let n = drive(&mut ws, &mut sc, 10).len() as u64;
    assert_eq!(ws.wal_syncs() - before, n, "one fdatasync per committed operation");
    let o = Options { sync: SyncPolicy::EveryN(4), checkpoint_every: 0, ..Options::default() };
    let tmp2 = TmpDir::new("sync4");
    let (mut ws, _) = open(tmp2.path(), 5, o);
    let before = ws.wal_syncs();
    let n = drive(&mut ws, &mut script(1, false), 40).len() as u64;
    assert_eq!(ws.wal_syncs() - before, n / 4, "group commit: one sync per 4 operations");
}
