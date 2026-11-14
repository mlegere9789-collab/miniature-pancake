//! Status codes, the caller-visible error record and the internal error type.
//!
//! Every function of the C ABI returns a [`D8Status`] and optionally fills a caller-allocated
//! [`D8Error`]. The caller owns the record; Rust only writes to it, so there is nothing to free.

use std::ffi::c_char;
use std::fmt;

/// Status code returned by every boundary function. Zero is success; every other value is an error.
pub type D8Status = i32;

/// Success.
pub const D8_OK: D8Status = 0;
/// A required pointer argument was NULL.
pub const D8_ERR_NULL_ARG: D8Status = 1;
/// An argument was invalid: unknown flag, misaligned pointer, bad UTF-8, or a size over a limit.
pub const D8_ERR_INVALID_ARG: D8Status = 2;
/// The handle is zero, was never issued, or has already been closed.
pub const D8_ERR_BAD_HANDLE: D8Status = 3;
/// The requested record does not exist.
pub const D8_ERR_NOT_FOUND: D8Status = 4;
/// A caller buffer was too small; the true sizes were written to the output record.
pub const D8_ERR_BUFFER_TOO_SMALL: D8Status = 5;
/// An undo marker was requested but no live operation exists.
pub const D8_ERR_NOTHING_TO_UNDO: D8Status = 6;
/// A file operation failed; the detail field holds the OS error number.
pub const D8_ERR_IO: D8Status = 7;
/// Stored data failed validation (bad magic, checksum or framing).
pub const D8_ERR_CORRUPT: D8Status = 8;
/// An earlier panic happened while the object was in use; the object refuses further work.
pub const D8_ERR_POISONED: D8Status = 9;
/// A panic was caught at the boundary. It did not unwind into the caller.
pub const D8_ERR_PANIC: D8Status = 10;
/// A size limit was exceeded.
pub const D8_ERR_LIMIT: D8Status = 11;
/// The log was closed by another caller while this call was in flight.
pub const D8_ERR_CLOSED: D8Status = 12;
/// An earlier write failed; the log is fail-stopped and refuses new appends.
pub const D8_ERR_FAILED: D8Status = 13;

/// A callback passed to the library returned a non-zero value; the detail field holds that value.
pub const D8_ERR_CALLBACK: D8Status = 14;

/// Version of this C ABI: major in the high 16 bits, minor in the low 16 bits.
pub const D8_ABI_VERSION: u32 = 0x0001_0000;

/// Length of the message buffer inside D8Error, including the terminating NUL.
pub const D8_ERROR_MESSAGE_LEN: usize = 240;

/// Error record filled by every boundary function. The caller allocates it (the stack is fine);
/// Rust only writes to it. On success `code` is zero and `message` is empty.
#[repr(C)]
pub struct D8Error {
    /// Same value as the returned status.
    pub code: D8Status,
    /// Reserved, always zero.
    pub reserved: u32,
    /// Code-specific detail: OS error number for IO errors, the offending sequence number for
    /// NOT_FOUND, zero otherwise.
    pub detail: i64,
    /// UTF-8 message, NUL terminated, truncated to fit.
    pub message: [c_char; D8_ERROR_MESSAGE_LEN],
}

/// Internal error type: converted into a status code and a [`D8Error`] at the boundary.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BoundaryError {
    /// Status code that the C caller will see.
    pub code: D8Status,
    /// Code-specific detail value.
    pub detail: i64,
    /// Human-readable message.
    pub message: String,
}

impl BoundaryError {
    /// Creates an error with a zero detail value.
    pub fn new(code: D8Status, message: impl Into<String>) -> Self {
        BoundaryError {
            code,
            detail: 0,
            message: message.into(),
        }
    }

    /// Sets the detail value.
    #[must_use]
    pub fn with_detail(mut self, detail: i64) -> Self {
        self.detail = detail;
        self
    }

    /// Builds an IO error from an OS error, keeping the OS error number as the detail.
    pub fn io(context: &str, e: &std::io::Error) -> Self {
        BoundaryError::new(D8_ERR_IO, format!("{context}: {e}"))
            .with_detail(i64::from(e.raw_os_error().unwrap_or(0)))
    }
}

impl fmt::Display for BoundaryError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} ({}): {}",
            status_name_str(self.code),
            self.code,
            self.message
        )
    }
}

impl std::error::Error for BoundaryError {}

/// Name of a status code as a Rust string.
pub fn status_name_str(code: D8Status) -> &'static str {
    match code {
        D8_OK => "D8_OK",
        D8_ERR_NULL_ARG => "D8_ERR_NULL_ARG",
        D8_ERR_INVALID_ARG => "D8_ERR_INVALID_ARG",
        D8_ERR_BAD_HANDLE => "D8_ERR_BAD_HANDLE",
        D8_ERR_NOT_FOUND => "D8_ERR_NOT_FOUND",
        D8_ERR_BUFFER_TOO_SMALL => "D8_ERR_BUFFER_TOO_SMALL",
        D8_ERR_NOTHING_TO_UNDO => "D8_ERR_NOTHING_TO_UNDO",
        D8_ERR_IO => "D8_ERR_IO",
        D8_ERR_CORRUPT => "D8_ERR_CORRUPT",
        D8_ERR_POISONED => "D8_ERR_POISONED",
        D8_ERR_PANIC => "D8_ERR_PANIC",
        D8_ERR_LIMIT => "D8_ERR_LIMIT",
        D8_ERR_CLOSED => "D8_ERR_CLOSED",
        D8_ERR_FAILED => "D8_ERR_FAILED",
        D8_ERR_CALLBACK => "D8_ERR_CALLBACK",
        _ => "D8_ERR_UNKNOWN",
    }
}

/// Largest prefix of `msg` that fits in `cap` bytes without splitting a UTF-8 character.
pub fn truncate_utf8(msg: &str, cap: usize) -> &str {
    if msg.len() <= cap {
        return msg;
    }
    let mut n = cap;
    while n > 0 && !msg.is_char_boundary(n) {
        n -= 1;
    }
    &msg[..n]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn truncation_never_splits_a_character() {
        let s = "aé"; // 'é' is two bytes
        assert_eq!(truncate_utf8(s, 3), "aé");
        assert_eq!(truncate_utf8(s, 2), "a");
        assert_eq!(truncate_utf8(s, 1), "a");
        assert_eq!(truncate_utf8(s, 0), "");
        assert_eq!(truncate_utf8("abc", 10), "abc");
    }

    #[test]
    fn every_code_has_a_distinct_name() {
        let mut names = Vec::new();
        for c in 0..=14 {
            names.push(status_name_str(c));
        }
        names.sort_unstable();
        names.dedup();
        assert_eq!(names.len(), 15);
        assert_eq!(status_name_str(999), "D8_ERR_UNKNOWN");
    }

    #[test]
    fn error_display_includes_name_and_message() {
        let e = BoundaryError::new(D8_ERR_NOT_FOUND, "no record 7").with_detail(7);
        let s = e.to_string();
        assert!(s.contains("D8_ERR_NOT_FOUND") && s.contains("no record 7"));
        assert_eq!(e.detail, 7);
    }
}
