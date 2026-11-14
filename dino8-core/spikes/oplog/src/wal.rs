//! The write-ahead log file: length-prefixed, checksummed frames appended in order.
//!
//! ```text
//! file  = header frame*
//! header = "D8OPLOG\0" version:u32le reserved:u32le          (16 bytes)
//! frame = len:u32le payload[len] tag[8]                     tag = BLAKE3(domain, len, payload)[..8]
//! ```
//!
//! A frame is the unit of atomicity: either all of it is present with a valid tag, or recovery
//! treats it (and everything after it) as never written. Recovery scans from the start, hands
//! every valid frame to the caller, truncates the file at the first invalid byte and keeps the
//! discarded tail in a side file so nothing is destroyed silently.

use crate::error::{Error, Result};
use crate::ids::DOM_FRAME;
use std::fs::{File, OpenOptions};
use std::io::{BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;

pub const MAGIC: [u8; 8] = *b"D8OPLOG\0";
pub const VERSION: u32 = 1;
pub const HEADER_LEN: u64 = 16;
/// Frames larger than this are treated as corruption.
pub const MAX_FRAME: u32 = 1 << 30;

/// When appended data is forced to stable storage.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SyncPolicy {
    /// `write` only: survives a process crash (SIGKILL), not a power cut or kernel crash.
    Never,
    /// `fdatasync` after every frame: survives a power cut.
    EveryAppend,
    /// `fdatasync` after every N frames (group commit): at most N-1 frames at risk on power loss.
    EveryN(u32),
}

#[derive(Debug, Default, Clone)]
pub struct ScanReport {
    pub frames: u64,
    pub valid_len: u64,
    pub file_len: u64,
    pub discarded_bytes: u64,
    pub discarded_path: Option<PathBuf>,
    pub header_repaired: bool,
}

/// Positioned read that does not touch the file cursor.
#[cfg(unix)]
pub fn read_at(f: &File, buf: &mut [u8], off: u64) -> std::io::Result<()> {
    use std::os::unix::fs::FileExt;
    f.read_exact_at(buf, off)
}

#[cfg(windows)]
pub fn read_at(f: &File, mut buf: &mut [u8], mut off: u64) -> std::io::Result<()> {
    use std::os::windows::fs::FileExt;
    while !buf.is_empty() {
        let n = f.seek_read(buf, off)?;
        if n == 0 {
            return Err(std::io::ErrorKind::UnexpectedEof.into());
        }
        buf = &mut buf[n..];
        off += n as u64;
    }
    Ok(())
}

fn frame_tag(len: [u8; 4], payload: &[u8]) -> [u8; 8] {
    let mut h = blake3::Hasher::new();
    h.update(DOM_FRAME.as_bytes());
    h.update(&len);
    h.update(payload);
    let d = h.finalize();
    let mut t = [0u8; 8];
    t.copy_from_slice(&d.as_bytes()[..8]);
    t
}

fn header_bytes() -> [u8; 16] {
    let mut h = [0u8; 16];
    h[..8].copy_from_slice(&MAGIC);
    h[8..12].copy_from_slice(&VERSION.to_le_bytes());
    h
}

/// Fill `buf` completely; `Ok(false)` if the stream ended first (a torn frame).
fn read_full(r: &mut impl Read, buf: &mut [u8]) -> std::io::Result<bool> {
    let mut got = 0;
    while got < buf.len() {
        let n = r.read(&mut buf[got..])?;
        if n == 0 {
            return Ok(false);
        }
        got += n;
    }
    Ok(true)
}

pub struct Wal {
    file: Arc<File>,
    path: PathBuf,
    len: u64,
    policy: SyncPolicy,
    unsynced: u32,
    buf: Vec<u8>,
    poisoned: bool,
    pub syncs: u64,
}

impl Wal {
    /// Open (creating if needed), recover and position for appending. `visit` is called with
    /// `(payload_offset, payload)` for every valid frame in order.
    pub fn open(
        path: &Path,
        policy: SyncPolicy,
        visit: &mut dyn FnMut(u64, &[u8]) -> Result<()>,
    ) -> Result<(Wal, ScanReport)> {
        let mut report = ScanReport::default();
        let existing = path.metadata().map(|m| m.len()).ok();
        match existing {
            None => {
                let mut f = File::create(path)?;
                f.write_all(&header_bytes())?;
                f.sync_all()?;
                report.valid_len = HEADER_LEN;
                report.file_len = HEADER_LEN;
            }
            Some(len) if len < HEADER_LEN => {
                // creation was interrupted: nothing of value can be in there
                let mut f = File::create(path)?;
                f.write_all(&header_bytes())?;
                f.sync_all()?;
                report.header_repaired = true;
                report.valid_len = HEADER_LEN;
                report.file_len = HEADER_LEN;
                report.discarded_bytes = len;
            }
            Some(file_len) => {
                report = Self::scan(path, file_len, visit)?;
            }
        }
        let file = OpenOptions::new().read(true).append(true).open(path)?;
        if report.valid_len < report.file_len {
            // keep the discarded tail for forensics, then cut it off
            let mut tail = vec![0u8; (report.file_len - report.valid_len) as usize];
            read_at(&file, &mut tail, report.valid_len)?;
            let mut side = path.as_os_str().to_owned();
            side.push(format!(".discarded-{}", report.valid_len));
            let side = PathBuf::from(side);
            std::fs::write(&side, &tail)?;
            report.discarded_path = Some(side);
            file.set_len(report.valid_len)?;
            file.sync_all()?;
        }
        Ok((
            Wal {
                file: Arc::new(file),
                path: path.to_path_buf(),
                len: report.valid_len,
                policy,
                unsynced: 0,
                buf: Vec::with_capacity(4096),
                poisoned: false,
                syncs: 0,
            },
            report,
        ))
    }

    fn scan(
        path: &Path,
        file_len: u64,
        visit: &mut dyn FnMut(u64, &[u8]) -> Result<()>,
    ) -> Result<ScanReport> {
        let mut r = BufReader::with_capacity(1 << 20, File::open(path)?);
        let mut hdr = [0u8; 16];
        if !read_full(&mut r, &mut hdr)? || hdr[..8] != MAGIC {
            return Err(Error::Corrupt(format!("{} is not an oplog file", path.display())));
        }
        let version = u32::from_le_bytes([hdr[8], hdr[9], hdr[10], hdr[11]]);
        if version != VERSION {
            return Err(Error::Corrupt(format!("unsupported oplog version {version}")));
        }
        let mut pos = HEADER_LEN;
        let mut frames = 0u64;
        let mut payload: Vec<u8> = Vec::new();
        loop {
            let mut lenb = [0u8; 4];
            if !read_full(&mut r, &mut lenb)? {
                break;
            }
            let len = u32::from_le_bytes(lenb);
            if len > MAX_FRAME || pos + 12 + u64::from(len) > file_len {
                break; // torn or corrupt length
            }
            payload.resize(len as usize, 0);
            if !read_full(&mut r, &mut payload)? {
                break;
            }
            let mut tag = [0u8; 8];
            if !read_full(&mut r, &mut tag)? || tag != frame_tag(lenb, &payload) {
                break;
            }
            visit(pos + 4, &payload)?;
            pos += 12 + u64::from(len);
            frames += 1;
        }
        Ok(ScanReport {
            frames,
            valid_len: pos,
            file_len,
            discarded_bytes: file_len - pos,
            discarded_path: None,
            header_repaired: false,
        })
    }

    fn fill_frame(&mut self, payload: &[u8]) -> Result<()> {
        let len = u32::try_from(payload.len()).map_err(|_| Error::Invalid("frame too large".into()))?;
        if len > MAX_FRAME {
            return Err(Error::Invalid("frame too large".into()));
        }
        let lenb = len.to_le_bytes();
        self.buf.clear();
        self.buf.extend_from_slice(&lenb);
        self.buf.extend_from_slice(payload);
        self.buf.extend_from_slice(&frame_tag(lenb, payload));
        Ok(())
    }

    /// Append one frame; returns the file offset of its payload.
    pub fn append(&mut self, payload: &[u8]) -> Result<u64> {
        if self.poisoned {
            return Err(Error::Invalid("log has a failed append; reopen to recover".into()));
        }
        self.fill_frame(payload)?;
        if let Err(e) = (&*self.file).write_all(&self.buf) {
            self.poisoned = true;
            return Err(e.into());
        }
        let off = self.len + 4;
        self.len += self.buf.len() as u64;
        match self.policy {
            SyncPolicy::Never => {}
            SyncPolicy::EveryAppend => self.sync()?,
            SyncPolicy::EveryN(n) => {
                self.unsynced += 1;
                if self.unsynced >= n {
                    self.sync()?;
                }
            }
        }
        Ok(off)
    }

    /// Fault-injection hook: write only the first `keep` bytes of the frame, as a crash in the
    /// middle of a write would. The log refuses further appends afterwards.
    #[doc(hidden)]
    pub fn append_partial(&mut self, payload: &[u8], keep: usize) -> Result<()> {
        self.fill_frame(payload)?;
        let keep = keep.min(self.buf.len() - 1);
        (&*self.file).write_all(&self.buf[..keep])?;
        self.poisoned = true;
        Ok(())
    }

    pub fn sync(&mut self) -> Result<()> {
        self.file.sync_data()?;
        self.unsynced = 0;
        self.syncs += 1;
        Ok(())
    }

    pub fn len(&self) -> u64 {
        self.len
    }

    pub fn is_empty(&self) -> bool {
        self.len <= HEADER_LEN
    }

    pub fn file(&self) -> Arc<File> {
        self.file.clone()
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn policy(&self) -> SyncPolicy {
        self.policy
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!("oplog-wal-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&p);
        std::fs::create_dir_all(&p).unwrap();
        p.join("t.wal")
    }

    fn collect(path: &Path) -> (Vec<Vec<u8>>, ScanReport) {
        let mut got = Vec::new();
        let (_, rep) = Wal::open(path, SyncPolicy::Never, &mut |_, p| {
            got.push(p.to_vec());
            Ok(())
        })
        .unwrap();
        (got, rep)
    }

    #[test]
    fn append_and_reopen() {
        let p = tmp("basic");
        let (mut w, rep) = Wal::open(&p, SyncPolicy::Never, &mut |_, _| Ok(())).unwrap();
        assert_eq!(rep.frames, 0);
        let o1 = w.append(b"hello").unwrap();
        let o2 = w.append(b"world!").unwrap();
        assert_eq!(o1, HEADER_LEN + 4);
        assert_eq!(o2, HEADER_LEN + 12 + 5 + 4);
        drop(w);
        let (got, rep) = collect(&p);
        assert_eq!(got, vec![b"hello".to_vec(), b"world!".to_vec()]);
        assert_eq!(rep.discarded_bytes, 0);
    }

    #[test]
    fn every_truncation_point_recovers_a_clean_prefix() {
        let p = tmp("trunc");
        let payloads: Vec<Vec<u8>> = (0..6u8).map(|i| vec![i; 7 + i as usize * 13]).collect();
        {
            let (mut w, _) = Wal::open(&p, SyncPolicy::Never, &mut |_, _| Ok(())).unwrap();
            for pl in &payloads {
                w.append(pl).unwrap();
            }
        }
        let full = std::fs::read(&p).unwrap();
        // frame end offsets
        let mut ends = vec![HEADER_LEN as usize];
        for pl in &payloads {
            ends.push(ends.last().unwrap() + 12 + pl.len());
        }
        for cut in 0..=full.len() {
            let q = p.with_file_name(format!("cut{cut}.wal"));
            std::fs::write(&q, &full[..cut]).unwrap();
            let (got, rep) = collect(&q);
            let complete = ends.iter().skip(1).take_while(|e| **e <= cut).count();
            assert_eq!(got.len(), complete, "cut at {cut}");
            assert_eq!(&got[..], &payloads[..complete]);
            let kept = if cut < HEADER_LEN as usize { HEADER_LEN as usize } else { ends[complete] };
            assert_eq!(rep.valid_len as usize, kept, "cut at {cut}");
            // the file was repaired in place and is appendable
            let (mut w, rep2) = Wal::open(&q, SyncPolicy::Never, &mut |_, _| Ok(())).unwrap();
            assert_eq!(rep2.discarded_bytes, 0, "second open is clean (cut {cut})");
            w.append(b"after").unwrap();
            let _ = std::fs::remove_file(&q);
        }
    }

    #[test]
    fn corruption_in_the_middle_drops_that_frame_and_the_rest() {
        let p = tmp("flip");
        {
            let (mut w, _) = Wal::open(&p, SyncPolicy::Never, &mut |_, _| Ok(())).unwrap();
            for i in 0..5u8 {
                w.append(&[i; 20]).unwrap();
            }
        }
        let mut data = std::fs::read(&p).unwrap();
        let frame = 12 + 20;
        data[HEADER_LEN as usize + 2 * frame + 10] ^= 0x40; // inside frame #2's payload
        std::fs::write(&p, &data).unwrap();
        let (got, rep) = collect(&p);
        assert_eq!(got.len(), 2);
        assert_eq!(rep.discarded_bytes as usize, 3 * frame);
        assert!(rep.discarded_path.as_ref().unwrap().exists());
    }

    #[test]
    fn partial_append_leaves_a_torn_tail_that_recovery_removes() {
        let p = tmp("partial");
        let (mut w, _) = Wal::open(&p, SyncPolicy::Never, &mut |_, _| Ok(())).unwrap();
        w.append(b"one").unwrap();
        w.append_partial(b"two-two-two", 9).unwrap();
        assert!(w.append(b"three").is_err());
        drop(w);
        let (got, rep) = collect(&p);
        assert_eq!(got, vec![b"one".to_vec()]);
        assert_eq!(rep.discarded_bytes, 9);
    }

    #[test]
    fn refuses_foreign_files() {
        let p = tmp("foreign");
        std::fs::write(&p, b"this is not a write-ahead log at all").unwrap();
        assert!(Wal::open(&p, SyncPolicy::Never, &mut |_, _| Ok(())).is_err());
        // and leaves the file alone
        assert_eq!(std::fs::read(&p).unwrap(), b"this is not a write-ahead log at all");
    }
}
