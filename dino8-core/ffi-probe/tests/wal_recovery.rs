//! Crash-recovery tests for the file-backed log.

use dino8_ffi_probe::log::{Log, OpInput, ReadBuffers, D8_KIND_OP, D8_KIND_UNDO};
use dino8_ffi_probe::wal::SyncMode;
use dino8_ffi_probe::*;
use std::fs::{self, OpenOptions};
use std::io::{Seek, SeekFrom, Write};
use std::path::PathBuf;

fn temp(name: &str) -> PathBuf {
    let p = PathBuf::from(env!("CARGO_TARGET_TMPDIR"))
        .join(format!("wal_{}_{name}.d8log", std::process::id()));
    let _ = fs::remove_file(&p);
    p
}

fn op(k: u32) -> (Vec<u8>, [D8ObjectId; 1]) {
    let mut id = D8ObjectId::default();
    id.bytes[..4].copy_from_slice(&k.to_le_bytes());
    (
        format!("command {k} with some parameters").into_bytes(),
        [id],
    )
}

fn append_n(log: &Log, from: u32, n: u32) {
    for k in from..from + n {
        let (params, ids) = op(k);
        log.append(&OpInput {
            command_id: k,
            tool_version: 5,
            random_seed: u64::from(k) * 3,
            params: &params,
            inputs: &ids,
            outputs: &[],
        })
        .unwrap();
    }
}

fn read_command(log: &Log, seq: u64) -> u32 {
    let mut p = vec![0u8; 256];
    let mut i = [D8ObjectId::default(); 1];
    log.read(
        seq,
        ReadBuffers {
            params: &mut p,
            inputs: &mut i,
            outputs: &mut [],
        },
    )
    .unwrap()
    .command_id
}

#[test]
fn reopening_replays_everything_including_undo_state() {
    let path = temp("reopen");
    {
        let log = Log::open_file(&path, SyncMode::Write, true).unwrap();
        append_n(&log, 1, 500);
        for _ in 0..4 {
            log.undo_marker().unwrap();
        }
        log.close().unwrap();
    }
    let log = Log::open_file(&path, SyncMode::Write, false).unwrap();
    let info = log.info().unwrap();
    assert_eq!(
        (
            info.records,
            info.live_ops,
            info.recovered_records,
            info.discarded_tail_bytes
        ),
        (504, 496, 504, 0)
    );
    assert_eq!(read_command(&log, 1), 1);
    assert_eq!(read_command(&log, 500), 500);
    // The undo stack continues where it left off: the next undo targets operation 496.
    assert_eq!(log.undo_marker().unwrap(), (505, 496));
    // New appends continue the sequence and are durable too.
    append_n(&log, 1000, 3);
    log.close().unwrap();
    let again = Log::open_file(&path, SyncMode::Write, false).unwrap();
    assert_eq!(again.len(), 508);
    assert_eq!(read_command(&again, 508), 1002);
    let _ = fs::remove_file(&path);
}

#[test]
fn a_torn_tail_costs_exactly_the_last_record() {
    // Cut the file at every byte offset inside the final frame: each cut loses that frame only.
    let path = temp("torn");
    {
        let log = Log::open_file(&path, SyncMode::Write, true).unwrap();
        append_n(&log, 1, 20);
        log.close().unwrap();
    }
    let full = fs::metadata(&path).unwrap().len();
    let (params, _) = op(20);
    let last_frame = (8 + 48 + params.len() + 16) as u64;
    for cut in 1..last_frame {
        let copy = temp("torn_copy_work");
        fs::copy(&path, &copy).unwrap();
        OpenOptions::new()
            .write(true)
            .open(&copy)
            .unwrap()
            .set_len(full - cut)
            .unwrap();
        let log = Log::open_file(&copy, SyncMode::Write, false).unwrap();
        let info = log.info().unwrap();
        assert_eq!(info.records, 19, "cut of {cut} bytes");
        assert_eq!(info.discarded_tail_bytes, last_frame - cut);
        // The file is usable again and the sequence continues at 20.
        append_n(&log, 99, 1);
        assert_eq!(log.len(), 20);
        let _ = fs::remove_file(&copy);
    }
    let _ = fs::remove_file(&path);
}

#[test]
fn a_flipped_byte_ends_recovery_at_the_damaged_record() {
    let path = temp("flip");
    {
        let log = Log::open_file(&path, SyncMode::Write, true).unwrap();
        append_n(&log, 1, 50);
        log.close().unwrap();
    }
    let (params, _) = op(1);
    let frame = (8 + 48 + params.len() + 16) as u64; // commands 1..9 have equal length
                                                     // Flip a payload byte inside record 10 (offset: header 16 + 9 frames + 8 prefix + 20).
    let offset = 16 + 9 * frame + 8 + 20;
    {
        let mut f = OpenOptions::new()
            .write(true)
            .read(true)
            .open(&path)
            .unwrap();
        f.seek(SeekFrom::Start(offset)).unwrap();
        f.write_all(&[0xA5]).unwrap();
    }
    let log = Log::open_file(&path, SyncMode::Write, false).unwrap();
    let info = log.info().unwrap();
    assert_eq!(info.records, 9, "records before the damage survive");
    assert!(info.discarded_tail_bytes > 0);
    let _ = fs::remove_file(&path);
}

#[test]
fn files_that_are_not_logs_are_refused_and_left_alone() {
    let path = temp("junk");
    fs::write(&path, b"definitely not a log file, just some text").unwrap();
    let err = Log::open_file(&path, SyncMode::Buffered, false)
        .err()
        .expect("refused");
    assert_eq!(err.code, D8_ERR_CORRUPT);
    assert_eq!(
        fs::read(&path).unwrap(),
        b"definitely not a log file, just some text"
    );

    // A newer format version is refused too.
    let mut header = Vec::from(*b"D8LOGWAL");
    header.extend_from_slice(&99u32.to_le_bytes());
    header.extend_from_slice(&[0; 4]);
    fs::write(&path, &header).unwrap();
    assert_eq!(
        Log::open_file(&path, SyncMode::Buffered, false)
            .err()
            .expect("refused")
            .code,
        D8_ERR_CORRUPT
    );

    // A torn header (crash while creating the file) is repaired, not refused.
    fs::write(&path, b"D8LOG").unwrap();
    let log = Log::open_file(&path, SyncMode::Buffered, false).unwrap();
    assert!(log.is_empty());
    let _ = fs::remove_file(&path);
}

#[test]
fn truncate_flag_starts_a_fresh_log() {
    let path = temp("trunc");
    let log = Log::open_file(&path, SyncMode::Write, true).unwrap();
    append_n(&log, 1, 10);
    log.close().unwrap();
    let fresh = Log::open_file(&path, SyncMode::Write, true).unwrap();
    assert!(fresh.is_empty());
    let _ = fs::remove_file(&path);
}

#[test]
fn buffered_mode_loses_at_most_the_unflushed_tail_and_close_flushes() {
    let path = temp("buffered");
    let log = Log::open_file(&path, SyncMode::Buffered, true).unwrap();
    append_n(&log, 1, 100);
    log.close().unwrap(); // close flushes the 64 KiB buffer
    let reopened = Log::open_file(&path, SyncMode::Buffered, false).unwrap();
    assert_eq!(reopened.len(), 100);
    let kinds: Vec<u32> = (1..=100)
        .map(|s| {
            let mut p = vec![0u8; 256];
            let mut i = [D8ObjectId::default(); 1];
            reopened
                .read(
                    s,
                    ReadBuffers {
                        params: &mut p,
                        inputs: &mut i,
                        outputs: &mut [],
                    },
                )
                .unwrap()
                .kind
        })
        .collect();
    assert!(kinds.iter().all(|&k| k == D8_KIND_OP) && D8_KIND_UNDO == 1);
    let _ = fs::remove_file(&path);
}
