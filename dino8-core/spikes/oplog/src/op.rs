//! The operation record: one node of the log DAG.
//!
//! An operation id is the BLAKE3 digest of the record's canonical binary encoding, so the log is a
//! Merkle DAG: an id commits to the parents, the command, its typed parameters, the object
//! snapshots it produced and the document state hash after it ran.

use crate::error::{Error, Result};
use crate::ids::{Hash, ObjectId, OpId, DOM_OP};
use crate::value::{put_varint, Reader, Value};
use std::fmt;

/// Version of the tool that ran the command (recorded so replay can tell builds apart).
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug, Default)]
pub struct ToolVersion {
    pub major: u16,
    pub minor: u16,
    pub patch: u16,
}

impl ToolVersion {
    pub const CURRENT: ToolVersion = ToolVersion { major: 0, minor: 1, patch: 0 };
}

impl fmt::Display for ToolVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}.{}.{}", self.major, self.minor, self.patch)
    }
}

/// One object touched by an operation: its snapshot before (absent if created) and after.
/// Undo restores `before`, redo restores `after`; both are content hashes into the blob store.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Output {
    pub id: ObjectId,
    pub before: Option<Hash>,
    pub after: Hash,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Operation {
    /// DAG parents. Empty for the genesis operation, one for an ordinary edit, two for a merge.
    /// `parents[0]` is the "first parent": `before`/`after` are relative to its state.
    pub parents: Vec<OpId>,
    /// Author session (random per launch). 0 marks system operations (genesis, merge).
    pub session: u64,
    /// Actor epoch within the session (bumped whenever the session's head jumped).
    pub epoch: u32,
    /// Position in the actor's chain, from 1. 0 for system operations.
    pub seq: u32,
    /// Logical clock: 1 + max over parents. Orders concurrent writes deterministically.
    pub lamport: u64,
    /// Command identifier, for example `"move"`.
    pub command: String,
    /// Schema tag of the parameter blob, for example `"move/1"`.
    pub schema: String,
    /// Typed parameters.
    pub params: Value,
    /// Objects the command read (sorted).
    pub inputs: Vec<ObjectId>,
    /// Objects the command wrote (sorted by id).
    pub outputs: Vec<Output>,
    pub tool: ToolVersion,
    /// Seed of the command's random number generator.
    pub seed: u64,
    /// Wall-clock time in milliseconds since the Unix epoch (audit only; never used for ordering).
    pub wall_ms: u64,
    /// Merkle root of the document after this operation.
    pub state_after: Hash,
}

impl Operation {
    /// The id this record would have.
    pub fn id(&self) -> OpId {
        OpId(Hash::digest(DOM_OP, &self.to_bytes()))
    }

    pub fn encode(&self, out: &mut Vec<u8>) {
        put_varint(out, self.parents.len() as u64);
        for p in &self.parents {
            out.extend_from_slice(&p.0 .0);
        }
        out.extend_from_slice(&self.session.to_le_bytes());
        put_varint(out, u64::from(self.epoch));
        put_varint(out, u64::from(self.seq));
        put_varint(out, self.lamport);
        for s in [&self.command, &self.schema] {
            put_varint(out, s.len() as u64);
            out.extend_from_slice(s.as_bytes());
        }
        self.params.encode(out);
        put_varint(out, self.inputs.len() as u64);
        for i in &self.inputs {
            out.extend_from_slice(&i.0);
        }
        put_varint(out, self.outputs.len() as u64);
        for o in &self.outputs {
            out.extend_from_slice(&o.id.0);
            match &o.before {
                Some(b) => {
                    out.push(1);
                    out.extend_from_slice(&b.0);
                }
                None => out.push(0),
            }
            out.extend_from_slice(&o.after.0);
        }
        put_varint(out, u64::from(self.tool.major));
        put_varint(out, u64::from(self.tool.minor));
        put_varint(out, u64::from(self.tool.patch));
        out.extend_from_slice(&self.seed.to_le_bytes());
        put_varint(out, self.wall_ms);
        out.extend_from_slice(&self.state_after.0);
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut v = Vec::with_capacity(256);
        self.encode(&mut v);
        v
    }

    pub fn decode(bytes: &[u8]) -> Result<Operation> {
        let mut r = Reader::new(bytes);
        let n = r.varint()? as usize;
        if n > 8 {
            return Err(Error::Corrupt("too many parents".into()));
        }
        let mut parents = Vec::with_capacity(n);
        for _ in 0..n {
            parents.push(OpId(Hash(r.array::<32>()?)));
        }
        let session = u64::from_le_bytes(r.array::<8>()?);
        let epoch = u32::try_from(r.varint()?).map_err(|_| Error::Corrupt("epoch".into()))?;
        let seq = u32::try_from(r.varint()?).map_err(|_| Error::Corrupt("seq".into()))?;
        let lamport = r.varint()?;
        let command = r.string()?;
        let schema = r.string()?;
        let params = r.read_value()?;
        let ni = r.varint()? as usize;
        if ni > bytes.len() / 16 + 1 {
            return Err(Error::Corrupt("too many inputs".into()));
        }
        let mut inputs = Vec::with_capacity(ni);
        for _ in 0..ni {
            inputs.push(ObjectId(r.array::<16>()?));
        }
        let no = r.varint()? as usize;
        if no > bytes.len() / 48 + 1 {
            return Err(Error::Corrupt("too many outputs".into()));
        }
        let mut outputs = Vec::with_capacity(no);
        for _ in 0..no {
            let id = ObjectId(r.array::<16>()?);
            let before = match r.u8()? {
                0 => None,
                1 => Some(Hash(r.array::<32>()?)),
                _ => return Err(Error::Corrupt("bad before flag".into())),
            };
            let after = Hash(r.array::<32>()?);
            outputs.push(Output { id, before, after });
        }
        let tool = ToolVersion {
            major: u16::try_from(r.varint()?).map_err(|_| Error::Corrupt("tool".into()))?,
            minor: u16::try_from(r.varint()?).map_err(|_| Error::Corrupt("tool".into()))?,
            patch: u16::try_from(r.varint()?).map_err(|_| Error::Corrupt("tool".into()))?,
        };
        let seed = u64::from_le_bytes(r.array::<8>()?);
        let wall_ms = r.varint()?;
        let state_after = Hash(r.array::<32>()?);
        if !r.is_empty() {
            return Err(Error::Corrupt("trailing bytes in operation".into()));
        }
        Ok(Operation {
            parents,
            session,
            epoch,
            seq,
            lamport,
            command,
            schema,
            params,
            inputs,
            outputs,
            tool,
            seed,
            wall_ms,
            state_after,
        })
    }

    // -----------------------------------------------------------------------------------------
    // Text form (one JSON object per line in the export)
    // -----------------------------------------------------------------------------------------

    /// The record as a value tree with readable keys; the id is included for the reader's benefit.
    pub fn to_json_value(&self) -> Value {
        let hex_list = |l: Vec<String>| Value::List(l.into_iter().map(Value::Str).collect());
        Value::map([
            ("kind", Value::str("op")),
            ("id", Value::Str(self.id().to_hex())),
            ("parents", hex_list(self.parents.iter().map(OpId::to_hex).collect())),
            ("session", Value::Str(format!("{:016x}", self.session))),
            ("epoch", Value::Int(i64::from(self.epoch))),
            ("seq", Value::Int(i64::from(self.seq))),
            ("lamport", Value::from_u64(self.lamport)),
            ("command", Value::Str(self.command.clone())),
            ("schema", Value::Str(self.schema.clone())),
            ("params", self.params.clone()),
            ("inputs", hex_list(self.inputs.iter().map(ObjectId::to_hex).collect())),
            (
                "outputs",
                Value::List(
                    self.outputs
                        .iter()
                        .map(|o| {
                            Value::map([
                                ("id", Value::Str(o.id.to_hex())),
                                ("before", o.before.map_or(Value::Null, |b| Value::Str(b.to_hex()))),
                                ("after", Value::Str(o.after.to_hex())),
                            ])
                        })
                        .collect(),
                ),
            ),
            ("tool", Value::Str(self.tool.to_string())),
            ("seed", Value::Str(format!("{:016x}", self.seed))),
            ("time", Value::from_u64(self.wall_ms)),
            ("state", Value::Str(self.state_after.to_hex())),
        ])
    }

    /// Parse the text form; the embedded id must match the recomputed one.
    pub fn from_json_value(v: &Value) -> Result<Operation> {
        fn field<'a>(v: &'a Value, k: &str) -> Result<&'a Value> {
            v.get(k).ok_or_else(|| Error::Corrupt(format!("op line misses `{k}`")))
        }
        fn s<'a>(v: &'a Value, k: &str) -> Result<&'a str> {
            field(v, k)?.as_str().ok_or_else(|| Error::Corrupt(format!("`{k}` must be a string")))
        }
        fn n(v: &Value, k: &str) -> Result<i64> {
            field(v, k)?.as_i64().ok_or_else(|| Error::Corrupt(format!("`{k}` must be an integer")))
        }
        fn hex<T>(text: &str, f: impl Fn(&str) -> Option<T>, what: &str) -> Result<T> {
            f(text).ok_or_else(|| Error::Corrupt(format!("bad {what} `{text}`")))
        }
        fn u64hex(text: &str) -> Result<u64> {
            u64::from_str_radix(text, 16).map_err(|_| Error::Corrupt(format!("bad hex `{text}`")))
        }
        fn list<'a>(v: &'a Value, k: &str) -> Result<&'a [Value]> {
            field(v, k)?.as_list().ok_or_else(|| Error::Corrupt(format!("`{k}` must be a list")))
        }
        let parents = list(v, "parents")?
            .iter()
            .map(|p| hex(p.as_str().unwrap_or(""), OpId::from_hex, "op id"))
            .collect::<Result<Vec<_>>>()?;
        let inputs = list(v, "inputs")?
            .iter()
            .map(|p| hex(p.as_str().unwrap_or(""), ObjectId::from_hex, "object id"))
            .collect::<Result<Vec<_>>>()?;
        let outputs = list(v, "outputs")?
            .iter()
            .map(|o| {
                Ok(Output {
                    id: hex(s(o, "id")?, ObjectId::from_hex, "object id")?,
                    before: match field(o, "before")? {
                        Value::Null => None,
                        Value::Str(t) => Some(hex(t, Hash::from_hex, "hash")?),
                        _ => return Err(Error::Corrupt("bad `before`".into())),
                    },
                    after: hex(s(o, "after")?, Hash::from_hex, "hash")?,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        let tool_s = s(v, "tool")?;
        let parts: Vec<&str> = tool_s.split('.').collect();
        if parts.len() != 3 {
            return Err(Error::Corrupt(format!("bad tool version `{tool_s}`")));
        }
        let p = |i: usize| -> Result<u16> {
            parts[i].parse().map_err(|_| Error::Corrupt(format!("bad tool version `{tool_s}`")))
        };
        let op = Operation {
            parents,
            session: u64hex(s(v, "session")?)?,
            epoch: u32::try_from(n(v, "epoch")?).map_err(|_| Error::Corrupt("epoch".into()))?,
            seq: u32::try_from(n(v, "seq")?).map_err(|_| Error::Corrupt("seq".into()))?,
            lamport: field(v, "lamport")?.as_u64().ok_or_else(|| Error::Corrupt("lamport".into()))?,
            command: s(v, "command")?.to_string(),
            schema: s(v, "schema")?.to_string(),
            params: field(v, "params")?.clone(),
            inputs,
            outputs,
            tool: ToolVersion { major: p(0)?, minor: p(1)?, patch: p(2)? },
            seed: u64hex(s(v, "seed")?)?,
            wall_ms: field(v, "time")?.as_u64().ok_or_else(|| Error::Corrupt("time".into()))?,
            state_after: hex(s(v, "state")?, Hash::from_hex, "hash")?,
        };
        let declared = hex(s(v, "id")?, OpId::from_hex, "op id")?;
        if op.id() != declared {
            return Err(Error::Mismatch(format!(
                "op line declares id {declared} but its content hashes to {}",
                op.id()
            )));
        }
        Ok(op)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Operation {
        Operation {
            parents: vec![OpId(Hash([1; 32]))],
            session: 0xdead_beef_0123_4567,
            epoch: 2,
            seq: 9,
            lamport: 77,
            command: "move".into(),
            schema: "move/1".into(),
            params: Value::map([("delta", Value::f3([1.0, 2.0, 3.5])), ("ids", Value::List(vec![Value::Obj(ObjectId([5; 16]))]))]),
            inputs: vec![ObjectId([5; 16])],
            outputs: vec![
                Output { id: ObjectId([5; 16]), before: Some(Hash([2; 32])), after: Hash([3; 32]) },
                Output { id: ObjectId([6; 16]), before: None, after: Hash([4; 32]) },
            ],
            tool: ToolVersion::CURRENT,
            seed: u64::MAX,
            wall_ms: 1_760_000_000_123,
            state_after: Hash([9; 32]),
        }
    }

    #[test]
    fn binary_round_trip_and_id_commitment() {
        let op = sample();
        let b = op.to_bytes();
        assert_eq!(Operation::decode(&b).unwrap(), op);
        let id = op.id();
        let mut other = op.clone();
        other.params = Value::Null;
        assert_ne!(other.id(), id);
        let mut other = op.clone();
        other.state_after = Hash([8; 32]);
        assert_ne!(other.id(), id, "the id commits to the resulting state hash");
        // trailing garbage and truncation are rejected
        let mut bad = b.clone();
        bad.push(0);
        assert!(Operation::decode(&bad).is_err());
        assert!(Operation::decode(&b[..b.len() - 1]).is_err());
    }

    #[test]
    fn json_round_trip_verifies_the_id() {
        let op = sample();
        let line = op.to_json_value().to_json();
        let back = Operation::from_json_value(&Value::from_json(&line).unwrap()).unwrap();
        assert_eq!(back, op);
        assert_eq!(back.to_json_value().to_json(), line);
        let tampered = line.replace("\"lamport\":77", "\"lamport\":78");
        assert!(Operation::from_json_value(&Value::from_json(&tampered).unwrap()).is_err());
    }
}
