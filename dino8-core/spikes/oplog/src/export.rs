//! Text-diffable serialization: canonical JSON lines.
//!
//! One JSON object per line, keys sorted, no insignificant whitespace, so the same log always
//! produces the same bytes and a `git diff` between two exports shows exactly the operations
//! and object snapshots that were added. Every blob is written *before* the first operation that
//! references it, in log order, so appending to the log appends to the text.
//!
//! ```text
//! {"format":"dino8-oplog/1","genesis":"..","kind":"header","tool":"0.1.0"}
//! {"data":{...object snapshot...},"hash":"..","kind":"blob","type":"obj"}
//! {"data":{"$b":"..."},"hash":"..","kind":"blob","type":"raw"}
//! {"command":"move","id":"..","kind":"op",...}
//! ...
//! {"kind":"ref","name":"option-a","target":".."}
//! {"branch":"option-a","kind":"head","target":".."}
//! ```
//!
//! Importing recomputes every hash and every operation id from the text, checks that each line
//! is in canonical form, and refuses anything that does not match what the line claims.

use crate::crdt::Obj;
use crate::engine::{hash_refs, Packet, Workspace};
use crate::error::{Error, Result};
use crate::ids::{Hash, OpId};
use crate::op::Operation;
use crate::store::{blob_hash, envelope, KIND_OBJ, KIND_RAW};
use crate::value::Value;
use std::collections::{BTreeMap, HashSet};

pub const FORMAT: &str = "dino8-oplog/1";

fn corrupt<T>(m: impl Into<String>) -> Result<T> {
    Err(Error::Corrupt(m.into()))
}

impl Workspace {
    fn blob_line(&self, h: &Hash) -> Result<String> {
        let b = self.store().get(h)?;
        let (ty, data) = match b.split_first() {
            Some((&KIND_OBJ, rest)) => ("obj", Obj::from_bytes(rest)?.to_value()),
            Some((&KIND_RAW, rest)) => ("raw", Value::Bytes(rest.to_vec())),
            _ => return corrupt(format!("blob {h} has an unknown kind")),
        };
        Ok(Value::map([
            ("kind", Value::str("blob")),
            ("hash", Value::Str(h.to_hex())),
            ("type", Value::str(ty)),
            ("data", data),
        ])
        .to_json())
    }

    /// The whole log as canonical JSON lines (operations, the blobs they introduce, refs, head).
    pub fn export_jsonl(&self) -> Result<String> {
        let mut out = String::with_capacity(self.len() * 700);
        out.push_str(
            &Value::map([
                ("kind", Value::str("header")),
                ("format", Value::str(FORMAT)),
                ("genesis", Value::Str(self.genesis().to_hex())),
                ("tool", Value::Str(self.opts.tool.to_string())),
            ])
            .to_json(),
        );
        out.push('\n');
        let mut emitted: HashSet<Hash> = HashSet::new();
        for i in 0..self.ops.len() {
            let rec = self.record(i as u32)?;
            for o in &rec.outputs {
                if emitted.insert(o.after) {
                    out.push_str(&self.blob_line(&o.after)?);
                    out.push('\n');
                    let obj = self.store().get_obj(&o.after)?;
                    let mut refs = Vec::new();
                    hash_refs(&obj.to_value(), &mut refs);
                    for g in refs {
                        if emitted.insert(g) {
                            out.push_str(&self.blob_line(&g)?);
                            out.push('\n');
                        }
                    }
                }
            }
            out.push_str(&rec.to_json_value().to_json());
            out.push('\n');
        }
        for (name, target) in self.branches() {
            out.push_str(
                &Value::map([
                    ("kind", Value::str("ref")),
                    ("name", Value::Str(name.clone())),
                    ("target", Value::Str(target.to_hex())),
                ])
                .to_json(),
            );
            out.push('\n');
        }
        out.push_str(
            &Value::map([
                ("kind", Value::str("head")),
                ("target", Value::Str(self.head().to_hex())),
                ("branch", self.current_branch().map_or(Value::Null, Value::str)),
            ])
            .to_json(),
        );
        out.push('\n');
        Ok(out)
    }
}

/// The document id recorded in the genesis operation of an export.
pub fn jsonl_doc_id(text: &str) -> Result<String> {
    for line in text.lines() {
        let v = Value::from_json(line)?;
        if v.get("kind").and_then(Value::as_str) == Some("op") {
            let op = Operation::from_json_value(&v)?;
            if op.parents.is_empty() {
                return op
                    .params
                    .get("doc")
                    .and_then(Value::as_str)
                    .map(str::to_string)
                    .map_or_else(|| corrupt("genesis lacks a document id"), Ok);
            }
        }
    }
    corrupt("no genesis operation in the export")
}

/// Rebuild a log from its text form into `ws`, which must be a fresh workspace for the same
/// document (its genesis operation must be the one in the text).
pub fn import_jsonl(ws: &mut Workspace, text: &str) -> Result<()> {
    let mut blobs: BTreeMap<Hash, Vec<u8>> = BTreeMap::new();
    let mut refs: Vec<(String, OpId)> = Vec::new();
    let mut head: Option<(OpId, Option<String>)> = None;
    let mut seen_header = false;
    for (n, line) in text.lines().enumerate() {
        let v = Value::from_json(line)?;
        if v.to_json() != line {
            return corrupt(format!("line {} is not in canonical form", n + 1));
        }
        let kind = v.get("kind").and_then(Value::as_str).unwrap_or("");
        match kind {
            "header" => {
                if n != 0 || v.get("format").and_then(Value::as_str) != Some(FORMAT) {
                    return corrupt("bad or misplaced header");
                }
                let g = v.get("genesis").and_then(Value::as_str).and_then(OpId::from_hex);
                if g != Some(ws.genesis()) {
                    return Err(Error::Mismatch("the export belongs to a different document".into()));
                }
                seen_header = true;
            }
            _ if !seen_header => return corrupt("missing header"),
            "blob" => {
                let declared = v.get("hash").and_then(Value::as_str).and_then(Hash::from_hex);
                let data = v.get("data").map_or_else(|| corrupt("blob without data"), Ok)?;
                let env = match v.get("type").and_then(Value::as_str) {
                    Some("obj") => envelope(KIND_OBJ, &Obj::from_value(data)?.to_bytes()),
                    Some("raw") => match data {
                        Value::Bytes(b) => envelope(KIND_RAW, b),
                        _ => return corrupt("raw blob data must be bytes"),
                    },
                    _ => return corrupt("unknown blob type"),
                };
                let h = blob_hash(&env);
                if declared != Some(h) {
                    return Err(Error::Mismatch(format!("blob on line {} does not hash to its name", n + 1)));
                }
                blobs.insert(h, env);
            }
            "op" => {
                let op = Operation::from_json_value(&v)?;
                let mut pk = Packet { record: op.to_bytes(), blobs: Vec::new() };
                for o in &op.outputs {
                    let env = blobs.get(&o.after).ok_or(Error::MissingBlob(o.after))?;
                    pk.blobs.push((o.after, env.clone()));
                    let obj = Obj::from_bytes(&env[1..])?;
                    let mut gs = Vec::new();
                    hash_refs(&obj.to_value(), &mut gs);
                    for g in gs {
                        let env = blobs.get(&g).ok_or(Error::MissingBlob(g))?;
                        pk.blobs.push((g, env.clone()));
                    }
                }
                ws.ingest(&[pk])?;
            }
            "ref" => {
                let name = v.get("name").and_then(Value::as_str).map(str::to_string);
                let target = v.get("target").and_then(Value::as_str).and_then(OpId::from_hex);
                match (name, target) {
                    (Some(n), Some(t)) => refs.push((n, t)),
                    _ => return corrupt("bad ref line"),
                }
            }
            "head" => {
                let target = v.get("target").and_then(Value::as_str).and_then(OpId::from_hex);
                let branch = match v.get("branch") {
                    Some(Value::Null) => None,
                    Some(Value::Str(s)) => Some(s.clone()),
                    _ => return corrupt("bad head line"),
                };
                head = target.map(|t| (t, branch));
            }
            other => return corrupt(format!("unknown line kind `{other}`")),
        }
    }
    let (target, branch) = head.map_or_else(|| corrupt("missing head line"), Ok)?;
    ws.restore_refs(&refs, target, branch.as_deref())
}
