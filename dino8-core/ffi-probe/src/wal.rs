//! File-backed write-ahead frames.
//!
//! File layout: a 16-byte header (`D8LOGWAL`, version `u32`, reserved `u32`), then frames
//! `[payload_len u32][crc32 u32][payload]`, all little endian. Recovery keeps the longest valid
//! prefix of frames and truncates the rest, so a torn final write costs at most that one record.

use crate::status::{BoundaryError, D8_ERR_CORRUPT};
use std::fs::{File, OpenOptions};
use std::io::{self, BufReader, BufWriter, Read, Seek, SeekFrom, Write};
use std::path::Path;

/// Magic bytes at the start of every log file.
/// cbindgen:ignore
pub const FILE_MAGIC: [u8; 8] = *b"D8LOGWAL";
/// File format version written by this crate.
/// cbindgen:ignore
pub const FILE_VERSION: u32 = 1;
/// Length of the file header in bytes.
/// cbindgen:ignore
pub const FILE_HEADER_LEN: u64 = 16;
/// Length of the per-frame prefix (payload length and checksum) in bytes.
/// cbindgen:ignore
pub const FRAME_PREFIX_LEN: usize = 8;
const WRITE_BUFFER: usize = 64 * 1024;

/// How hard each append pushes data towards the disk.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SyncMode {
    /// Frames sit in a 64 KiB buffer; flushed when it fills, on `sync` and on close. A process
    /// crash can lose the buffered tail.
    Buffered,
    /// Every append is written to the OS (one write call). Survives a process crash, not a power
    /// failure.
    Write,
    /// Every append is written and `fdatasync`ed. Survives power failure; costs a disk flush.
    Fsync,
}

/// Outcome of scanning an existing log file.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Recovery {
    /// Records accepted from the file.
    pub records: u64,
    /// Bytes of the file that were kept (header included).
    pub valid_bytes: u64,
    /// Bytes cut from the end of the file.
    pub discarded_bytes: u64,
}

/// An open log file positioned for appending.
pub struct Wal {
    writer: BufWriter<File>,
    mode: SyncMode,
    bytes: u64,
}

const CRC_TABLE: [u32; 256] = make_crc_table();

const fn make_crc_table() -> [u32; 256] {
    let mut table = [0u32; 256];
    let mut i = 0;
    while i < 256 {
        let mut c = i as u32;
        let mut k = 0;
        while k < 8 {
            c = if c & 1 != 0 {
                0xEDB8_8320 ^ (c >> 1)
            } else {
                c >> 1
            };
            k += 1;
        }
        table[i] = c;
        i += 1;
    }
    table
}

/// CRC-32 (IEEE 802.3, the zlib polynomial) of `data`.
pub fn crc32(data: &[u8]) -> u32 {
    let mut c = !0u32;
    for &b in data {
        c = CRC_TABLE[((c ^ u32::from(b)) & 0xFF) as usize] ^ (c >> 8);
    }
    !c
}

fn file_header() -> [u8; FILE_HEADER_LEN as usize] {
    let mut h = [0u8; FILE_HEADER_LEN as usize];
    h[..8].copy_from_slice(&FILE_MAGIC);
    h[8..12].copy_from_slice(&FILE_VERSION.to_le_bytes());
    h
}

enum Fill {
    Full,
    Partial,
}

/// Reads exactly `buf.len()` bytes, or reports that the file ended first.
fn read_full<R: Read>(r: &mut R, buf: &mut [u8]) -> io::Result<Fill> {
    let mut got = 0;
    while got < buf.len() {
        let n = r.read(&mut buf[got..])?;
        if n == 0 {
            return Ok(Fill::Partial);
        }
        got += n;
    }
    Ok(Fill::Full)
}

impl Wal {
    /// Opens (creating if needed) the log file at `path`, replays its valid frames through
    /// `on_record` and positions the file for appending.
    ///
    /// `on_record` receives each payload; it returns `false` to reject the record, which ends
    /// recovery there like a corrupt frame would. Payloads outside `min_payload..=max_payload`
    /// bytes are treated as corruption.
    pub fn open<F>(
        path: &Path,
        mode: SyncMode,
        truncate: bool,
        min_payload: usize,
        max_payload: usize,
        mut on_record: F,
    ) -> Result<(Wal, Recovery), BoundaryError>
    where
        F: FnMut(&[u8]) -> bool,
    {
        let ctx = |what: &str| format!("{what} {}", path.display());
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(path)
            .map_err(|e| BoundaryError::io(&ctx("cannot open log file"), &e))?;
        let io_err = |what: &str, e: io::Error| BoundaryError::io(&ctx(what), &e);

        if truncate {
            file.set_len(0).map_err(|e| io_err("cannot truncate", e))?;
        }
        let file_len = file.metadata().map_err(|e| io_err("cannot stat", e))?.len();
        let expected = file_header();

        let mut valid = FILE_HEADER_LEN;
        let mut records = 0u64;
        let mut fresh_header = file_len == 0;
        if file_len > 0 && file_len < FILE_HEADER_LEN {
            // A torn creation: only acceptable when the bytes present are a prefix of our header.
            let mut present = vec![0u8; file_len as usize];
            (&file)
                .read_exact(&mut present)
                .map_err(|e| io_err("cannot read header of", e))?;
            if present[..] != expected[..present.len()] {
                return Err(BoundaryError::new(
                    D8_ERR_CORRUPT,
                    format!("{} is not a Dino 8 log file (short header)", path.display()),
                ));
            }
            fresh_header = true;
        }
        if fresh_header {
            let mut f = &file;
            f.seek(SeekFrom::Start(0))
                .map_err(|e| io_err("cannot seek", e))?;
            f.write_all(&expected)
                .map_err(|e| io_err("cannot write header of", e))?;
            file.set_len(FILE_HEADER_LEN)
                .map_err(|e| io_err("cannot size", e))?;
        } else {
            let mut reader = BufReader::with_capacity(1 << 20, &file);
            reader
                .seek(SeekFrom::Start(0))
                .map_err(|e| io_err("cannot seek", e))?;
            let mut head = [0u8; FILE_HEADER_LEN as usize];
            match read_full(&mut reader, &mut head)
                .map_err(|e| io_err("cannot read header of", e))?
            {
                Fill::Full => {}
                Fill::Partial => {
                    return Err(BoundaryError::new(
                        D8_ERR_CORRUPT,
                        "log file header is truncated",
                    ))
                }
            }
            if head[..8] != FILE_MAGIC {
                return Err(BoundaryError::new(
                    D8_ERR_CORRUPT,
                    format!("{} is not a Dino 8 log file (bad magic)", path.display()),
                ));
            }
            let version = u32::from_le_bytes([head[8], head[9], head[10], head[11]]);
            if version != FILE_VERSION {
                return Err(BoundaryError::new(
                    D8_ERR_CORRUPT,
                    format!("unsupported log file version {version}"),
                ));
            }
            let full_len = file.metadata().map_err(|e| io_err("cannot stat", e))?.len();
            let mut payload = Vec::new();
            loop {
                let mut prefix = [0u8; FRAME_PREFIX_LEN];
                match read_full(&mut reader, &mut prefix) {
                    Ok(Fill::Full) => {}
                    Ok(Fill::Partial) | Err(_) => break,
                }
                let len = u32::from_le_bytes([prefix[0], prefix[1], prefix[2], prefix[3]]) as usize;
                let crc = u32::from_le_bytes([prefix[4], prefix[5], prefix[6], prefix[7]]);
                if len < min_payload || len > max_payload {
                    break;
                }
                if valid + (FRAME_PREFIX_LEN + len) as u64 > full_len {
                    break;
                }
                payload.resize(len, 0);
                match read_full(&mut reader, &mut payload) {
                    Ok(Fill::Full) => {}
                    Ok(Fill::Partial) | Err(_) => break,
                }
                if crc32(&payload) != crc || !on_record(&payload) {
                    break;
                }
                valid += (FRAME_PREFIX_LEN + len) as u64;
                records += 1;
            }
        }

        let total = file.metadata().map_err(|e| io_err("cannot stat", e))?.len();
        let discarded = total.saturating_sub(valid);
        if discarded > 0 {
            file.set_len(valid)
                .map_err(|e| io_err("cannot truncate torn tail of", e))?;
        }
        let mut f = file;
        f.seek(SeekFrom::Start(valid))
            .map_err(|e| io_err("cannot seek", e))?;
        let wal = Wal {
            writer: BufWriter::with_capacity(WRITE_BUFFER, f),
            mode,
            bytes: valid,
        };
        Ok((
            wal,
            Recovery {
                records,
                valid_bytes: valid,
                discarded_bytes: discarded,
            },
        ))
    }

    /// Appends one frame holding `payload` and applies the sync mode.
    pub fn append_frame(&mut self, payload: &[u8]) -> Result<(), BoundaryError> {
        let len = u32::try_from(payload.len()).map_err(|_| {
            BoundaryError::new(crate::status::D8_ERR_LIMIT, "frame larger than 4 GiB")
        })?;
        let mut prefix = [0u8; FRAME_PREFIX_LEN];
        prefix[..4].copy_from_slice(&len.to_le_bytes());
        prefix[4..].copy_from_slice(&crc32(payload).to_le_bytes());
        let res = self
            .writer
            .write_all(&prefix)
            .and_then(|()| self.writer.write_all(payload))
            .and_then(|()| match self.mode {
                SyncMode::Buffered => Ok(()),
                SyncMode::Write => self.writer.flush(),
                SyncMode::Fsync => self
                    .writer
                    .flush()
                    .and_then(|()| self.writer.get_ref().sync_data()),
            });
        res.map_err(|e| BoundaryError::io("log write failed", &e))?;
        self.bytes += (FRAME_PREFIX_LEN + payload.len()) as u64;
        Ok(())
    }

    /// Pushes buffered frames to the OS.
    pub fn flush(&mut self) -> Result<(), BoundaryError> {
        self.writer
            .flush()
            .map_err(|e| BoundaryError::io("log flush failed", &e))
    }

    /// Flushes and forces the data to stable storage.
    pub fn sync(&mut self) -> Result<(), BoundaryError> {
        self.flush()?;
        self.writer
            .get_ref()
            .sync_data()
            .map_err(|e| BoundaryError::io("log fdatasync failed", &e))
    }

    /// Bytes in the file after the last accepted append (header included).
    pub fn bytes(&self) -> u64 {
        self.bytes
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_matches_the_standard_check_value() {
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
        assert_eq!(crc32(b""), 0);
        assert_eq!(crc32(b"a"), 0xE8B7_BE43);
    }

    #[test]
    fn header_layout_is_stable() {
        let h = file_header();
        assert_eq!(&h[..8], b"D8LOGWAL");
        assert_eq!(&h[8..12], &1u32.to_le_bytes());
        assert_eq!(&h[12..], &[0, 0, 0, 0]);
    }
}
