# Spike B results: operation log retrofit (plan Rev 2, section 5.3 / ADR-3)

Crate: `dino8-core/spikes/oplog` (standalone Rust crate, about 7,900 lines of source and 1,900 of tests).
Everything below was measured on a shared 4-core cloud VM (load average 5 to 17 during runs, virtio
disk), release build, one thread unless stated. Raw output: `oplog-bench <append|big|merge|replay|jsonl>`
prints a table and one `RESULT key=value` line per measurement.

## What was built

* **Operation record** (command id, typed parameters, schema id, tool version, random seed, session,
  parent operation ids, input and output object ids, resulting state hash), BLAKE3 content addressed.
  The log is a DAG: branches, time travel and merges are all just parent pointers.
* **State** is a Merkle HAMT of object nodes in a content-addressed store, so a snapshot per operation
  costs only the changed path (structural sharing). Undo, redo and checkout swap the root hash and
  report the changed objects.
* **Dispatcher**: every command runs through one entry that records its operation first. Commands touch
  state only through a context; a replay harness re-runs the log and compares state hashes. Two
  deliberately nondeterministic commands (wall clock, process-global counter) are caught by replay.
* **Write-ahead log**: length-prefixed, tagged frames, torn-tail detection, truncation at the first bad
  byte with the discarded tail kept in a side file; periodic checkpoints; sync policies: none, group,
  per operation, background fdatasync (every 20 ms).
* **Merge**: per-property CRDTs (last-writer register with conflict item, add-wins set, RGA list,
  delete-vs-edit), element-level merge, conflicts surfaced as items resolved by ordinary operations.
  The merge operation id is content derived, so two replicas merging the same pair create the identical op.
* Canonical JSON-lines export and import (open container format candidate), shared sequencer for
  concurrent sessions (loom checked).
* The 13 commands in `commands.rs` are **stand-ins** (create, move, set property, tags, polyline
  vertices, delete, scatter, array, union ...), not the 1,070 real commands.

## Tests (all green at the commit that carries this file)

`cargo test`: **83 passed, 0 failed** = 37 library unit, 2 concurrent, 6 crash, 8 CRDT property, 2 demo,
14 engine, 5 JSON-lines, 3 merge property, 6 value property. Plus **4 loom models pass**
(`RUSTFLAGS="--cfg loom" cargo test --release --test loom_shared`). `cargo clippy --all-targets -- -D warnings` is clean.
Property tests use 96 to 400 cases each (fewer in debug builds); a mutation check on the CRDT laws caught 2 of 2
injected mutants.

Crash tests: 14 real SIGKILL runs of a writer process (5 left a torn tail, 1,628 bytes discarded in total, every run
recovered an exact prefix of the acknowledged session), injected torn appends at 7 points, a flipped byte, and a
truncation sweep at every byte offset of a log. Process-kill only: **power loss was not tested** (no way to cut power here).

During this resume one regression was found and fixed: the benchmark commit added a second random draw to the
script generator, which changed the seeded streams and made `time_travel_to_every_operation...` fail (no long jump
used a checkpoint). Root cause fixed in `src/script.rs` (scale the one roll instead of drawing again); no test or
expectation was touched. The 1M benchmark below was rerun on the fixed code.

## Gates (plan section 5.3 targets and Appendix B) and measured numbers

| Gate | Result | Measurement |
|---|---|---|
| Log append under 1 ms | PASS for the no-fsync and background-fsync policies; FAIL for fsync on every operation on this disk | see below |
| Undo or redo under 30 ms, unlimited depth | PASS on the log and state swap; depth and spill only partly shown | see below |
| Crash recovery to the last operation | PASS (process kill, torn tails, truncation at every byte) | see tests |
| Replay determinism 100% | PASS for the stand-in command set (0 mismatches in 1,000,040 ops); NOT shown for real commands | see below |
| Merge: conflicts surfaced as items, convergence | PASS on generated scenarios and the scripted demo | see below |

**Append** (dispatch + snapshot + state hash + write-ahead frame, 100,000 ops, about 771 bytes/op on disk):

| policy | wall p50 / p99 / p99.9 | thread-CPU p50 / p99 | ops over 1 ms (wall / CPU) |
|---|---|---|---|
| memory only | 35.8 / 140.9 / 2459 us | 36.2 / 112 us | 295 / 6 |
| WAL write, no fsync (survives process crash) | 25.6 / 62.5 / 4101 us | 25.9 / 60 us | 278 / 5 |
| WAL + background fdatasync every 20 ms | 26.7 / 63.3 / 4096 us | 27.0 / 58 us | 206 / 4 |
| WAL + group fsync every 32 | 29.3 / 3058 / 7142 us | 29.7 / 174 us | 3009 / 4 |
| WAL + fdatasync every operation (5,000 ops) | 3996 / 9089 / 123159 us | 154 / 296 us | 4345 / 0 |

The 0.2 to 0.3 percent of wall-clock appends over 1 ms in the first three rows are scheduler stalls on the
loaded machine (the same appends are under 1 ms in thread-CPU time, 4 to 6 per 100,000). With a background
fdatasync every 20 ms the power-loss window is at most about 20 ms of operations and append stays at about 27 us
p50. Strict power-loss durability (fdatasync per operation) costs about 4 ms per append on this VM disk, so
that mode misses the 1 ms gate here; a local NVMe with a fast flush may behave differently (not measured).
Checkpoint of a 100,000-object document: first 53.6 ms (all nodes), incremental 6.0 ms after 4,096 edits.

**Large log, 1,000,000 operations** (script of edits, 12,206 undos, 223 redos, 2,813 new branches; the document
reaches only 2,216 objects, see limits). Build 71.4 s (14,000 ops/s including parameter building).

| item | measured |
|---|---|
| log size | 770.6 MB = 771 bytes/op (op frames 735 B each, 235 checkpoints 34 MB) |
| structural sharing | 1,347,439 distinct blobs, 464.5 MB stored; one full snapshot per op would be about 0.8 TB (1,813 times more) |
| memory | rss 352 MB after build, 391 MB after navigation, peak 435 MB during recovery |
| undo, depth 1,000 and 8,503 (wall) | p50 3.8 to 4.9 us, p99 7.8 to 13 us, p99.9 up to 66 us, worst single sample 1.27 ms |
| redo, same depths (wall) | p50 3.8 to 4.7 us, p99 7.6 to 22 us, worst 188 us |
| materialize objects changed by one undo/redo | p50 0.2 to 8.3 us, worst 2.3 ms |
| 8,504 consecutive undos from the head | p50 5.6 us, p99 38.8 us, max 0.76 ms |
| time travel (checkout of an arbitrary operation, median 2,824 and max 5,399 objects change) | p50 10.8 / 12.7 / 15.3 ms for far-away jumps (<=256, 257 to 4,096, 4,097 to 65,536 levels); p99 19 to 29 ms; max 31.1 ms (first run: max 51.4 ms) |
| append on a new branch right after time travel | p50 94 us, max 261 us |
| recovery, warm page cache | 4.60 s for 1,000,040 ops (scan + index of 1,046,014 frames, state restore 2 ms) |
| recovery, cold | 29.3 s in the first run (26 MB/s from the disk after cache eviction); 4.9 s in the rerun where the eviction evidently did not take effect. Treat 4.6 s as the lower and 29 s as the upper figure |
| recovery with a 777-byte torn tail | 5.96 s, 777 bytes discarded, all 1,000,040 ops intact |
| full integrity pass (re-hash every record and blob) | 4.1 s |
| deterministic replay from empty document | 1,000,040 ops in 49.8 s (20,240 ops/s), 0 mismatches, final state hash equal |

**Merge** on a 100,000-object document (two replicas, edits on both sides):

| edits per side | merge time | objects changed | conflict items |
|---|---|---|---|
| 100 | 58.7 ms | 200 | 0 |
| 1,000 | 172.9 ms | 1,984 | 2 |
| 10,000 | 854 ms | 18,095 | 406 |
| 50,000 | 6.05 s | 63,077 | 7,633 |

Initial sync of 100,000 ops costs 3.8 to 4.5 s; sync of 100 / 1,000 / 10,000 ops costs 4 / 34 / 218 ms.
Plan Appendix B: "delta sync after a typical edit under 1 s, 500-edit change, 50 users under 1 s". The 500-edit, two-replica
point is between the 100 and 1,000 rows (about 0.1 to 0.2 s). The 50-user figure was not measured (only two
replicas and 6 in-process threads on a shared sequencer, converging).
60 generated scenarios: 5,632 operations, 155 merge operations, 319 conflict items (271 register, 48 delete-vs-edit);
merge is commutative down to the operation id, and replicas converge whatever the exchange and merge order.
Scripted two-architect demo (`oplog-demo`): 4 conflict items surfaced, resolved with ordinary operations to 0,
both replicas create the identical merge op, replay of the shared log reproduces the state hash, three design
options as branches, and undo/redo of a merge work.

**Replay and text round trip** (100,000 ops): replay 5.08 s (19,784 ops/s), 0 mismatches. Canonical JSON lines:
export 6.8 s (154 MB, 1,543 bytes/op), import 18.8 s, second export byte-identical, state equal.

## Recommendation

Adopt option B (wrap and record) as written in ADR-3, with this operation record, write-ahead frame format and
content-addressed Merkle state as the starting design for the Rust crate. The measured costs leave a wide margin
on append (tens of microseconds) and on undo/redo (microseconds), and recovery and replay scale linearly.
Default sync policy: background fdatasync (about 20 ms power-loss window), per-operation fdatasync as an opt-in.
Next work: make open fast by recovering from the newest checkpoint instead of scanning every frame (the scan
is the whole 4.6 s), and measure per-operation fdatasync on owner hardware.

## Honest limits

1. **Stand-in commands and state.** 13 synthetic commands over a map of typed properties, not the 1,070 real
   commands and not `Document`/`SceneObject`. The real undo cost also includes rebuilding SceneObjects from a
   snapshot and refreshing the viewport; that is not in these numbers (the 30 ms gate for the real app is
   untested). Replay determinism of real kernel calls (seed and tolerance capture, floating point) is untested.
2. **Small working set in the 1M run:** the script created about 2,200 objects (the cap was 100,000), so
   structural sharing and HAMT depth are better than for a 100,000-object document with random edits. The merge
   and checkpoint benchmarks do use 100,000 objects.
3. **Undo depth:** the benchmark exercises chains of up to about 8,500 undo steps on a branch and arbitrary
   time travel over 1M ops; "unlimited depth with disk spill" is not exercised (everything stayed in memory,
   391 MB rss).
4. **Time travel is not undo:** jumps that change thousands of objects take 11 to 15 ms median and
   reached 31 ms (51 ms in an earlier run) at the tail, close to or above 30 ms. Undo and redo themselves are
   far below the gate.
5. **Durability:** SIGKILL and torn-write injection only. Power loss, disk write reordering and filesystems
   that lie about flush were not tested. Per-operation fdatasync misses 1 ms on this VM disk.
6. **Memory:** about 350 to 435 MB rss for a 770 MB, 1M-op log with all index structures in memory; a real
   container would need the checkpoint-based open and an on-disk index.
7. **Shared noisy machine:** wall-clock tails (p99.9 and max) include other agents' load; thread-CPU columns
   are reported next to them for that reason. Cold-cache recovery varied from 4.9 s to 29.3 s between runs.
8. **Merge:** conflict policy is last-writer-wins with a surfaced item (registers) and add-wins (sets); no
   permissions, no multi-user server, no transport. 50-user sync not measured. Merge time grows with the
   number of changed objects (6 s for 63,000 changed objects).
9. The cost of JSON-lines import (18.8 s per 100,000 ops) is high and unoptimized.
