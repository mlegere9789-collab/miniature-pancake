//! The deterministic replay harness (plan section 5.3, Stage 3).
//!
//! Replay takes a recorded log and, using nothing but the recorded commands, typed parameters,
//! seeds, clock values and authors, re-executes every operation from an empty document into a
//! brand-new store. For each operation it checks that the recomputed inputs, outputs (object
//! snapshot hashes) and document state hash equal the recorded ones, and that the rebuilt record
//! hashes to the recorded operation id. A command that reads the system clock, a global, or any
//! other state outside the dispatcher shows up as a mismatch at exactly that operation.

use crate::engine::{build_merge, merge_op, Workspace, NONE};
use crate::error::{Error, Result};
use crate::exec::{execute, ExecInput, Registry};
use crate::hamt::{root_hash, Root};
use crate::ids::{Actor, Hash};
use crate::op::{Operation, ToolVersion};
use crate::store::Store;
use std::collections::BTreeSet;
use std::sync::Arc;
use std::time::Instant;

#[derive(Debug, Default, Clone)]
pub struct ReplayReport {
    pub ops: usize,
    /// Commands re-executed.
    pub executed: usize,
    /// Merge operations re-derived.
    pub merges: usize,
    /// One line per divergence, naming the first operation and what differed.
    pub mismatches: Vec<String>,
    pub final_root: Hash,
    pub seconds: f64,
    /// Tool versions found in the log (more than one means the log spans builds).
    pub tool_versions: Vec<ToolVersion>,
}

impl ReplayReport {
    pub fn ok(&self) -> bool {
        self.mismatches.is_empty()
    }

    pub fn ops_per_second(&self) -> f64 {
        if self.seconds > 0.0 {
            self.ops as f64 / self.seconds
        } else {
            0.0
        }
    }
}

fn describe(i: usize, rec: &Operation, got: &Operation) -> String {
    let mut parts = Vec::new();
    if rec.inputs != got.inputs {
        parts.push("inputs".to_string());
    }
    if rec.outputs != got.outputs {
        let ids: Vec<String> = rec
            .outputs
            .iter()
            .chain(got.outputs.iter())
            .filter(|o| !(rec.outputs.contains(o) && got.outputs.contains(o)))
            .map(|o| format!("{:?}", o.id))
            .collect::<BTreeSet<_>>()
            .into_iter()
            .collect();
        parts.push(format!("outputs [{}]", ids.join(", ")));
    }
    if rec.state_after != got.state_after {
        parts.push("state hash".to_string());
    }
    if rec.schema != got.schema {
        parts.push(format!("schema {} vs {}", rec.schema, got.schema));
    }
    if parts.is_empty() {
        parts.push("record".to_string());
    }
    format!("op #{i} `{}` diverged in: {}", rec.command, parts.join(", "))
}

impl Workspace {
    /// Re-execute the whole log from scratch and compare. Reads only the recorded operations.
    pub fn replay_check(&self, registry: &Registry) -> Result<ReplayReport> {
        let t0 = Instant::now();
        let n = self.ops.len();
        let store = Store::new(None);
        let mut report = ReplayReport { ops: n, ..ReplayReport::default() };
        let mut versions: BTreeSet<ToolVersion> = BTreeSet::new();

        // how many later operations still need each state (via either parent edge)
        let mut uses = vec![0u32; n];
        for m in self.ops.iter().skip(1) {
            uses[m.parent as usize] += 1;
            if m.parent2 != NONE {
                uses[m.parent2 as usize] += 1;
            }
        }
        let mut states: std::collections::HashMap<u32, Root> = std::collections::HashMap::new();
        states.insert(0, None);

        for i in 1..n {
            let rec = self.record(i as u32)?;
            versions.insert(rec.tool);
            let p0 = self.ops[i].parent;
            let base = states.get(&p0).cloned().ok_or_else(|| Error::Invalid(format!("state of {p0} missing")))?;
            let meta = |j: u32| (self.ops[j as usize].id, self.ops[j as usize].lamport, self.ops[j as usize].wall_ms);

            let (new_root, rebuilt, blobs): (Root, Operation, Vec<(Hash, Vec<u8>)>) = if rec.command == "merge"
                && self.ops[i].parent2 != NONE
            {
                let p1 = self.ops[i].parent2;
                let other = states.get(&p1).cloned().ok_or_else(|| Error::Invalid(format!("state of {p1} missing")))?;
                let b = build_merge(&store, &base, &other)?;
                let op = merge_op(meta(p0), meta(p1), &b);
                report.merges += 1;
                (b.root, op, b.new_blobs)
            } else {
                let Some(spec) = registry.get(&rec.command) else {
                    report.mismatches.push(format!("op #{i}: command `{}` is not registered", rec.command));
                    // keep the recorded state so later operations can still be checked
                    states.insert(i as u32, self.state_at(i as u32)?);
                    continue;
                };
                let inp = ExecInput {
                    command: &rec.command,
                    params: &rec.params,
                    actor: Actor { session: rec.session, epoch: rec.epoch },
                    seq: rec.seq,
                    lamport: rec.lamport,
                    wall_ms: rec.wall_ms,
                    seed: rec.seed,
                };
                let r = match execute(registry, &base, &store, &inp) {
                    Ok(r) => r,
                    Err(e) => {
                        report.mismatches.push(format!("op #{i} `{}` failed on replay: {e}", rec.command));
                        states.insert(i as u32, self.state_at(i as u32)?);
                        continue;
                    }
                };
                report.executed += 1;
                let op = Operation {
                    parents: rec.parents.clone(),
                    session: rec.session,
                    epoch: rec.epoch,
                    seq: rec.seq,
                    lamport: rec.lamport,
                    command: rec.command.clone(),
                    schema: spec.schema.to_string(),
                    params: rec.params.clone(),
                    inputs: r.inputs,
                    outputs: r.outputs,
                    tool: rec.tool,
                    seed: rec.seed,
                    wall_ms: rec.wall_ms,
                    state_after: root_hash(&r.new_root),
                };
                (r.new_root, op, r.new_blobs)
            };
            for (h, b) in blobs {
                store.insert_mem(h, Arc::from(b));
            }
            if rebuilt != rec || rebuilt.id() != self.ops[i].id {
                report.mismatches.push(describe(i, &rec, &rebuilt));
            }
            states.insert(i as u32, new_root);
            for p in [p0, self.ops[i].parent2] {
                if p != NONE {
                    uses[p as usize] -= 1;
                    if uses[p as usize] == 0 && p != self.head {
                        states.remove(&p);
                    }
                }
            }
            if uses[i] == 0 && i as u32 != self.head {
                states.remove(&(i as u32));
            }
        }
        report.final_root = states.get(&self.head).map_or_else(crate::hamt::empty_root_hash, root_hash);
        report.tool_versions = versions.into_iter().collect();
        report.seconds = t0.elapsed().as_secs_f64();
        Ok(report)
    }
}
