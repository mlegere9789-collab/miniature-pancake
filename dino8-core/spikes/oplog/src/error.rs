//! One error type for the whole crate.

use crate::crdt::CrdtError;
use crate::ids::{Hash, OpId};
use crate::value::CodecError;
use std::fmt;

#[derive(Debug)]
pub enum Error {
    Io(std::io::Error),
    Codec(CodecError),
    Crdt(CrdtError),
    /// Stored data failed validation (checksum, hash, structure).
    Corrupt(String),
    MissingBlob(Hash),
    UnknownOp(OpId),
    MissingParent(OpId),
    UnknownCommand(String),
    /// The command rejected its parameters.
    BadParams(String),
    /// The command failed against the current state.
    Command(String),
    /// A recorded value did not match the recomputed one (determinism or integrity failure).
    Mismatch(String),
    Invalid(String),
}

pub type Result<T> = std::result::Result<T, Error>;

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Io(e) => write!(f, "I/O error: {e}"),
            Error::Codec(e) => write!(f, "codec error: {e}"),
            Error::Crdt(e) => write!(f, "{e}"),
            Error::Corrupt(m) => write!(f, "corrupt data: {m}"),
            Error::MissingBlob(h) => write!(f, "missing blob {h}"),
            Error::UnknownOp(o) => write!(f, "unknown operation {o}"),
            Error::MissingParent(o) => write!(f, "operation parent {o} is not in the log"),
            Error::UnknownCommand(c) => write!(f, "unknown command `{c}`"),
            Error::BadParams(m) => write!(f, "bad parameters: {m}"),
            Error::Command(m) => write!(f, "command failed: {m}"),
            Error::Mismatch(m) => write!(f, "mismatch: {m}"),
            Error::Invalid(m) => write!(f, "invalid: {m}"),
        }
    }
}

impl std::error::Error for Error {}

impl From<std::io::Error> for Error {
    fn from(e: std::io::Error) -> Self {
        Error::Io(e)
    }
}

impl From<CodecError> for Error {
    fn from(e: CodecError) -> Self {
        Error::Codec(e)
    }
}

impl From<CrdtError> for Error {
    fn from(e: CrdtError) -> Self {
        Error::Crdt(e)
    }
}
