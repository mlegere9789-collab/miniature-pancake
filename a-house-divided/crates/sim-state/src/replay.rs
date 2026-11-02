//! Replay format, version 1.
//!
//! All integers are little-endian.
//!
//! ```text
//! magic        4 bytes  "HDRP"
//! version      u16      = 1
//! header       scenario_id (u16 length + UTF-8), seed u64, tick_hz u16,
//!              fidelity_class u8 (0, 1 or 2), checkpoint_interval u32
//! inputs       u32 count, then each: tick u64, actor u8, seq u16,
//!              payload (u32 length + bytes); sorted by (tick, actor, seq)
//! checkpoints  u32 count, then each: tick u64, hash u64
//! footer       total_ticks u64, final_hash u64,
//!              body_hash u64 (FNV-1a 64 of every preceding byte)
//! ```

use crate::world::fnv1a64;
use std::fmt;

/// The four magic bytes at the start of every replay.
pub const REPLAY_MAGIC: [u8; 4] = *b"HDRP";
/// The replay format version this crate reads and writes.
pub const REPLAY_VERSION: u16 = 1;

/// Smallest encoded size of one input record (empty payload).
const MIN_INPUT_BYTES: u64 = 8 + 1 + 2 + 4;
/// Encoded size of one checkpoint.
const CHECKPOINT_BYTES: u64 = 8 + 8;

/// A recorded player/agent input.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InputRecord {
    /// Tick the input applies to.
    pub tick: u64,
    /// Actor (player slot) that issued the input.
    pub actor: u8,
    /// Per-actor sequence number.
    pub seq: u16,
    /// Opaque payload bytes.
    pub payload: Vec<u8>,
}

/// A state-hash checkpoint.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Checkpoint {
    /// Tick the hash was taken at.
    pub tick: u64,
    /// `state_hash` at that tick.
    pub hash: u64,
}

/// Errors from parsing or building a replay.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ReplayError {
    /// The first four bytes are not `"HDRP"`.
    BadMagic,
    /// The version field is not [`REPLAY_VERSION`].
    UnsupportedVersion(u16),
    /// The data ended in the middle of a field.
    Truncated,
    /// A declared length or count is larger than the remaining data could hold.
    LengthExceedsData {
        /// The declared length or count.
        declared: u64,
        /// Bytes that actually remain.
        remaining: usize,
    },
    /// The scenario id is not valid UTF-8.
    InvalidUtf8,
    /// The fidelity class is not 0, 1 or 2.
    InvalidFidelityClass(u8),
    /// Input at `index` sorts before its predecessor by `(tick, actor, seq)`.
    InputsNotSorted {
        /// Index of the offending input.
        index: usize,
    },
    /// The stored body hash does not match the data.
    BodyHashMismatch {
        /// Hash stored in the file.
        expected: u64,
        /// Hash computed over the preceding bytes.
        actual: u64,
    },
    /// Bytes remain after the footer.
    TrailingBytes(usize),
    /// Writer: scenario id longer than `u16::MAX` bytes (value is its length).
    ScenarioIdTooLong(usize),
    /// Writer: a payload longer than `u32::MAX` bytes (value is its length).
    PayloadTooLong(usize),
    /// Writer: more than `u32::MAX` inputs or checkpoints (value is the count).
    TooManyRecords(usize),
}

impl fmt::Display for ReplayError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ReplayError::BadMagic => write!(f, "bad magic (expected \"HDRP\")"),
            ReplayError::UnsupportedVersion(v) => write!(f, "unsupported replay version {v}"),
            ReplayError::Truncated => write!(f, "replay data is truncated"),
            ReplayError::LengthExceedsData {
                declared,
                remaining,
            } => write!(
                f,
                "declared length {declared} exceeds the {remaining} bytes remaining"
            ),
            ReplayError::InvalidUtf8 => write!(f, "scenario id is not valid UTF-8"),
            ReplayError::InvalidFidelityClass(c) => write!(f, "invalid fidelity class {c}"),
            ReplayError::InputsNotSorted { index } => {
                write!(f, "input {index} is out of (tick, actor, seq) order")
            }
            ReplayError::BodyHashMismatch { expected, actual } => write!(
                f,
                "body hash mismatch (stored {expected:#018x}, computed {actual:#018x})"
            ),
            ReplayError::TrailingBytes(n) => write!(f, "{n} trailing bytes after footer"),
            ReplayError::ScenarioIdTooLong(n) => write!(f, "scenario id of {n} bytes is too long"),
            ReplayError::PayloadTooLong(n) => write!(f, "payload of {n} bytes is too long"),
            ReplayError::TooManyRecords(n) => write!(f, "{n} records exceed the u32 count field"),
        }
    }
}

impl std::error::Error for ReplayError {}

/// A validated replay. Construct with [`ReplayWriter`] or [`ReplayReader::parse`];
/// every value of this type can be encoded and re-parsed to an equal value.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Replay {
    scenario_id: String,
    seed: u64,
    tick_hz: u16,
    fidelity_class: u8,
    checkpoint_interval: u32,
    inputs: Vec<InputRecord>,
    checkpoints: Vec<Checkpoint>,
    total_ticks: u64,
    final_hash: u64,
}

impl Replay {
    /// Scenario identifier.
    pub fn scenario_id(&self) -> &str {
        &self.scenario_id
    }
    /// Master RNG seed.
    pub fn seed(&self) -> u64 {
        self.seed
    }
    /// Simulation ticks per second.
    pub fn tick_hz(&self) -> u16 {
        self.tick_hz
    }
    /// Fidelity class (0, 1 or 2).
    pub fn fidelity_class(&self) -> u8 {
        self.fidelity_class
    }
    /// Ticks between checkpoints.
    pub fn checkpoint_interval(&self) -> u32 {
        self.checkpoint_interval
    }
    /// Inputs, sorted by `(tick, actor, seq)`.
    pub fn inputs(&self) -> &[InputRecord] {
        &self.inputs
    }
    /// Checkpoints in recorded order.
    pub fn checkpoints(&self) -> &[Checkpoint] {
        &self.checkpoints
    }
    /// Total number of simulated ticks.
    pub fn total_ticks(&self) -> u64 {
        self.total_ticks
    }
    /// State hash after the final tick.
    pub fn final_hash(&self) -> u64 {
        self.final_hash
    }

    /// Encodes the replay in the version 1 binary format, body hash included.
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut b = Vec::new();
        b.extend_from_slice(&REPLAY_MAGIC);
        b.extend_from_slice(&REPLAY_VERSION.to_le_bytes());
        b.extend_from_slice(&(self.scenario_id.len() as u16).to_le_bytes());
        b.extend_from_slice(self.scenario_id.as_bytes());
        b.extend_from_slice(&self.seed.to_le_bytes());
        b.extend_from_slice(&self.tick_hz.to_le_bytes());
        b.push(self.fidelity_class);
        b.extend_from_slice(&self.checkpoint_interval.to_le_bytes());
        b.extend_from_slice(&(self.inputs.len() as u32).to_le_bytes());
        for i in &self.inputs {
            b.extend_from_slice(&i.tick.to_le_bytes());
            b.push(i.actor);
            b.extend_from_slice(&i.seq.to_le_bytes());
            b.extend_from_slice(&(i.payload.len() as u32).to_le_bytes());
            b.extend_from_slice(&i.payload);
        }
        b.extend_from_slice(&(self.checkpoints.len() as u32).to_le_bytes());
        for c in &self.checkpoints {
            b.extend_from_slice(&c.tick.to_le_bytes());
            b.extend_from_slice(&c.hash.to_le_bytes());
        }
        b.extend_from_slice(&self.total_ticks.to_le_bytes());
        b.extend_from_slice(&self.final_hash.to_le_bytes());
        let h = fnv1a64(&b);
        b.extend_from_slice(&h.to_le_bytes());
        b
    }
}

/// Builder for [`Replay`]. Inputs may be pushed in any order; [`ReplayWriter::finish`]
/// sorts them (stably) by `(tick, actor, seq)`.
#[derive(Clone, Debug)]
pub struct ReplayWriter {
    scenario_id: String,
    seed: u64,
    tick_hz: u16,
    fidelity_class: u8,
    checkpoint_interval: u32,
    inputs: Vec<InputRecord>,
    checkpoints: Vec<Checkpoint>,
}

impl ReplayWriter {
    /// Starts a replay with the header fields.
    pub fn new(
        scenario_id: impl Into<String>,
        seed: u64,
        tick_hz: u16,
        fidelity_class: u8,
        checkpoint_interval: u32,
    ) -> ReplayWriter {
        ReplayWriter {
            scenario_id: scenario_id.into(),
            seed,
            tick_hz,
            fidelity_class,
            checkpoint_interval,
            inputs: Vec::new(),
            checkpoints: Vec::new(),
        }
    }

    /// Records an input.
    pub fn push_input(&mut self, tick: u64, actor: u8, seq: u16, payload: Vec<u8>) -> &mut Self {
        self.inputs.push(InputRecord {
            tick,
            actor,
            seq,
            payload,
        });
        self
    }

    /// Records a checkpoint.
    pub fn push_checkpoint(&mut self, tick: u64, hash: u64) -> &mut Self {
        self.checkpoints.push(Checkpoint { tick, hash });
        self
    }

    /// Validates the fields, sorts the inputs and produces the [`Replay`].
    pub fn finish(mut self, total_ticks: u64, final_hash: u64) -> Result<Replay, ReplayError> {
        if self.fidelity_class > 2 {
            return Err(ReplayError::InvalidFidelityClass(self.fidelity_class));
        }
        if self.scenario_id.len() > usize::from(u16::MAX) {
            return Err(ReplayError::ScenarioIdTooLong(self.scenario_id.len()));
        }
        for n in [self.inputs.len(), self.checkpoints.len()] {
            if u32::try_from(n).is_err() {
                return Err(ReplayError::TooManyRecords(n));
            }
        }
        for i in &self.inputs {
            if u32::try_from(i.payload.len()).is_err() {
                return Err(ReplayError::PayloadTooLong(i.payload.len()));
            }
        }
        self.inputs.sort_by_key(|i| (i.tick, i.actor, i.seq));
        Ok(Replay {
            scenario_id: self.scenario_id,
            seed: self.seed,
            tick_hz: self.tick_hz,
            fidelity_class: self.fidelity_class,
            checkpoint_interval: self.checkpoint_interval,
            inputs: self.inputs,
            checkpoints: self.checkpoints,
            total_ticks,
            final_hash,
        })
    }
}

/// Bounds-checked little-endian reader.
struct Cursor<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Cursor<'a> {
    fn remaining(&self) -> usize {
        self.data.len() - self.pos
    }

    fn take(&mut self, n: usize) -> Result<&'a [u8], ReplayError> {
        if n > self.remaining() {
            return Err(ReplayError::Truncated);
        }
        let s = &self.data[self.pos..self.pos + n];
        self.pos += n;
        Ok(s)
    }

    /// Takes `n` bytes where `n` was declared by the file itself.
    fn take_declared(&mut self, n: u64) -> Result<&'a [u8], ReplayError> {
        if n > self.remaining() as u64 {
            return Err(ReplayError::LengthExceedsData {
                declared: n,
                remaining: self.remaining(),
            });
        }
        self.take(n as usize)
    }

    fn array<const N: usize>(&mut self) -> Result<[u8; N], ReplayError> {
        let mut a = [0u8; N];
        a.copy_from_slice(self.take(N)?);
        Ok(a)
    }

    fn u8(&mut self) -> Result<u8, ReplayError> {
        Ok(self.array::<1>()?[0])
    }
    fn u16(&mut self) -> Result<u16, ReplayError> {
        Ok(u16::from_le_bytes(self.array()?))
    }
    fn u32(&mut self) -> Result<u32, ReplayError> {
        Ok(u32::from_le_bytes(self.array()?))
    }
    fn u64(&mut self) -> Result<u64, ReplayError> {
        Ok(u64::from_le_bytes(self.array()?))
    }

    /// Reads a record count and checks the remaining data can hold that many
    /// records of at least `min_size` bytes (prevents allocation bombs).
    fn count(&mut self, min_size: u64) -> Result<usize, ReplayError> {
        let n = u64::from(self.u32()?);
        if n.saturating_mul(min_size) > self.remaining() as u64 {
            return Err(ReplayError::LengthExceedsData {
                declared: n,
                remaining: self.remaining(),
            });
        }
        Ok(n as usize)
    }
}

/// Parser for the version 1 binary replay format. Never panics on any input.
#[derive(Clone, Copy, Debug)]
pub struct ReplayReader;

impl ReplayReader {
    /// Parses and fully validates a replay.
    pub fn parse(data: &[u8]) -> Result<Replay, ReplayError> {
        let mut c = Cursor { data, pos: 0 };
        if c.array::<4>()? != REPLAY_MAGIC {
            return Err(ReplayError::BadMagic);
        }
        let version = c.u16()?;
        if version != REPLAY_VERSION {
            return Err(ReplayError::UnsupportedVersion(version));
        }

        let id_len = u64::from(c.u16()?);
        let id_bytes = c.take_declared(id_len)?;
        let scenario_id = std::str::from_utf8(id_bytes)
            .map_err(|_| ReplayError::InvalidUtf8)?
            .to_owned();
        let seed = c.u64()?;
        let tick_hz = c.u16()?;
        let fidelity_class = c.u8()?;
        if fidelity_class > 2 {
            return Err(ReplayError::InvalidFidelityClass(fidelity_class));
        }
        let checkpoint_interval = c.u32()?;

        let n_inputs = c.count(MIN_INPUT_BYTES)?;
        let mut inputs: Vec<InputRecord> = Vec::with_capacity(n_inputs);
        for index in 0..n_inputs {
            let tick = c.u64()?;
            let actor = c.u8()?;
            let seq = c.u16()?;
            let len = u64::from(c.u32()?);
            let payload = c.take_declared(len)?.to_vec();
            if let Some(prev) = inputs.last() {
                if (prev.tick, prev.actor, prev.seq) > (tick, actor, seq) {
                    return Err(ReplayError::InputsNotSorted { index });
                }
            }
            inputs.push(InputRecord {
                tick,
                actor,
                seq,
                payload,
            });
        }

        let n_cp = c.count(CHECKPOINT_BYTES)?;
        let mut checkpoints = Vec::with_capacity(n_cp);
        for _ in 0..n_cp {
            let tick = c.u64()?;
            let hash = c.u64()?;
            checkpoints.push(Checkpoint { tick, hash });
        }

        let total_ticks = c.u64()?;
        let final_hash = c.u64()?;
        let body_end = c.pos;
        let stored = c.u64()?;
        let actual = fnv1a64(&data[..body_end]);
        if stored != actual {
            return Err(ReplayError::BodyHashMismatch {
                expected: stored,
                actual,
            });
        }
        if c.remaining() != 0 {
            return Err(ReplayError::TrailingBytes(c.remaining()));
        }

        Ok(Replay {
            scenario_id,
            seed,
            tick_hz,
            fidelity_class,
            checkpoint_interval,
            inputs,
            checkpoints,
            total_ticks,
            final_hash,
        })
    }
}
