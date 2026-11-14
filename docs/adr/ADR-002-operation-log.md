# ADR-002: Operation log, retrofit by wrapping the command engine (option B, staged)

| | |
|---|---|
| Status | Proposed. Spike B is complete; the decision belongs to the owner. |
| Date | 2026-11-14 (UTC) |
| Plan | Master Plan Rev 2, 5.3 ("ADR-3") and Appendix B (log append, undo or redo, delta sync) |
| Evidence | branch `w0-spike-b` @ `7bbb7bd5`, `dino8-core/spikes/oplog/RESULTS.md`; boundary rules from ADR-001 (`w0-r1`); determinism of concurrent kernel calls from ADR-005 (`w0-kthreads`) |

## 1. Context

Options in plan 5.3: (A) rewrite all 1,070 commands as pure typed operations on a new store; (B) wrap the existing command
engine and record operations first, replacing storage later; (C) leave undo as is. The plan chose B, staged: Stage 1 command
bus plus write-ahead log; Stage 2 typed schemas; Stage 3 determinism tests (replay reproduces the state hash); Stage 4
semantic deltas; Stage 5 merge; Stage 6 retire the legacy undo stack. Targets: undo or redo under 30 ms; log append under 1 ms;
crash recovery to the last operation; replay determinism for 100% of commands.

## 2. Decision

1. Adopt option B, staged, as written in plan 5.3. A would freeze feature work for weeks with every command at risk; C leaves
   co-editing, branching and crash recovery out of reach.
2. Use Spike B's design as the starting point of the production `d8-oplog` crate:
   - an operation record carries command id, typed parameters, schema id, tool version, random seed, session, parent
     operation ids, input and output object ids and the resulting state hash, content addressed with BLAKE3
     (32-byte hash, 16-byte object ids);
   - the log is a DAG (branches, time travel and merges are parent pointers);
   - state is a Merkle HAMT of object nodes in a content-addressed store, so a snapshot per operation costs only the
     changed path; undo, redo and checkout swap the root hash and report changed objects;
   - one dispatcher: commands touch state only through a context, a replay harness re-runs the log and compares state
     hashes;
   - a write-ahead log of length-prefixed tagged frames with torn-tail detection, truncation at the first bad byte (the
     discarded tail is kept in a side file) and periodic checkpoints;
   - per-property CRDT merge (last-writer register with a conflict item, add-wins set, RGA list, delete-versus-edit),
     conflicts surfaced as items and resolved by ordinary operations; the merge operation id is content derived;
   - canonical JSON-lines export and import as the candidate for the open container's text form.
3. Sync policy default: write-ahead frame written immediately (survives an application crash) plus a background
   `fdatasync` every 20 ms (power-loss window at most about 20 ms of operations). Per-operation `fdatasync` is opt-in. Group
   `fsync` every N operations is not the default (see section 3: it puts the full sync cost on every Nth operation).
4. Access from C++ only through the ADR-001 boundary rules (handles, status codes, panic guard); the dispatcher fills one
   record per command and calls the append function; macros and imports use the batch append.
5. Stage 1 keeps state in the existing `Document` / `SceneObject` structures; the Merkle HAMT is the log's own state model
   for undo, snapshot restore and replay checks. Whether the entity store (ADR-004) later becomes the HAMT's
   successor or a second structure is NOT decided here (open item 1).

## 3. Measured evidence

Run conditions: shared 4-core cloud VM, load average 5 to 17 during runs, virtio disk, release build, one thread unless
stated; wall-clock and thread-CPU columns are both reported because of the load. The 13 commands (`commands.rs`) are
stand-ins (create, move, set property, tags, polyline vertices, delete, scatter, array, union ...), not the 1,070 real commands.

Append (dispatch + snapshot + state hash + write-ahead frame; 100,000 operations; about 771 bytes per op on disk), microseconds:

| policy | wall p50 / p99 / p99.9 | thread-CPU p50 / p99 | ops over 1 ms (wall / CPU) |
|---|---|---|---|
| memory only | 35.8 / 140.9 / 2,459 | 36.2 / 112 | 295 / 6 |
| WAL write, no fsync (survives process crash) | 25.6 / 62.5 / 4,101 | 25.9 / 60 | 278 / 5 |
| WAL + background fdatasync every 20 ms | 26.7 / 63.3 / 4,096 | 27.0 / 58 | 206 / 4 |
| WAL + group fsync every 32 | 29.3 / 3,058 / 7,142 | 29.7 / 174 | 3,009 / 4 |
| WAL + fdatasync every operation (5,000 ops) | 3,996 / 9,089 / 123,159 | 154 / 296 | 4,345 / 0 |

The 0.2 to 0.3 percent of wall appends over 1 ms in the first three rows are scheduler stalls (the same appends are
under 1 ms in thread-CPU time: 4 to 6 per 100,000). Checkpoint of a 100,000-object document: first 53.6 ms (all nodes), incremental 6.0 ms
after 4,096 edits. Compare R1: its bare boundary-plus-record append is 0.175 us (ADR-001); the 25 to 36 us here is the
dispatch, snapshot and hash around it.

Large log, 1,000,000 operations (script of edits, 12,206 undos, 223 redos, 2,813 new branches; the document reaches
only 2,216 objects). Build 71.4 s (14,000 ops/s including parameter building):

| item | measured |
|---|---|
| log size | 770.6 MB = 771 bytes/op (op frames 735 B each, 235 checkpoints 34 MB) |
| structural sharing | 1,347,439 distinct blobs, 464.5 MB stored; one full snapshot per op would be about 0.8 TB (1,813 times more) |
| memory | rss 352 MB after build, 391 MB after navigation, peak 435 MB during recovery |
| undo at depth 1,000 and 8,503 (wall) | p50 3.8 to 4.9 us, p99 7.8 to 13 us, p99.9 up to 66 us, worst single sample 1.27 ms |
| redo, same depths (wall) | p50 3.8 to 4.7 us, p99 7.6 to 22 us, worst 188 us |
| time travel to an arbitrary operation (median 2,824, max 5,399 objects change) | p50 10.8 / 12.7 / 15.3 ms for far jumps (up to 256, 257 to 4,096, 4,097 to 65,536 levels); p99 19 to 29 ms; max 31.1 ms (first run: 51.4 ms) |
| append on a new branch right after time travel | p50 94 us, max 261 us |
| recovery, warm page cache | 4.60 s for 1,000,040 ops (scan + index of 1,046,014 frames; state restore 2 ms) |
| recovery, cold | 29.3 s in the first run (26 MB/s), 4.9 s in a rerun where the cache eviction evidently did not take effect; treat 4.6 s as the lower and 29 s as the upper figure |
| recovery with a 777-byte torn tail | 5.96 s, 777 bytes discarded, all 1,000,040 ops intact |
| full integrity pass (re-hash every record and blob) | 4.1 s |
| deterministic replay from empty document | 1,000,040 ops in 49.8 s (20,240 ops/s), 0 mismatches, final state hash equal |
| canonical JSON lines, 100,000 ops | export 6.8 s (154 MB, 1,543 bytes/op), import 18.8 s (unoptimized), second export byte-identical |

Merge on a 100,000-object document, two replicas:

| edits per side | merge time | objects changed | conflict items |
|---|---|---|---|
| 100 | 58.7 ms | 200 | 0 |
| 1,000 | 172.9 ms | 1,984 | 2 |
| 10,000 | 854 ms | 18,095 | 406 |
| 50,000 | 6.05 s | 63,077 | 7,633 |

Initial sync of 100,000 ops 3.8 to 4.5 s; sync of 100 / 1,000 / 10,000 ops 4 / 34 / 218 ms. 60 generated scenarios (5,632 ops,
155 merges, 319 conflict items) converge whatever the exchange and merge order; merge is commutative down to the operation id.

Tests: `cargo test` 83 passed, 0 failed (37 unit, 2 concurrent, 6 crash, 8 CRDT property, 2 demo, 14 engine, 5 JSON-lines,
3 merge property, 6 value property); 4 loom models pass; clippy `-D warnings` clean; a mutation check on the CRDT laws caught 2 of
2 injected mutants. Crash tests: 14 real SIGKILL runs of a writer (5 left a torn tail, 1,628 bytes discarded in total; every run
recovered an exact prefix of the acknowledged session), torn appends injected at 7 points, a flipped byte, truncation at every byte
offset of a log. Two deliberately nondeterministic commands (wall clock, process-global counter) are caught by the replay harness.
One regression was found and root-fixed during the spike (a second random draw changed the seeded script streams; fixed in
`src/script.rs`, no test or expectation touched).

Related measurement from ADR-005 (kernel audit, not this spike): every concurrent kernel result equalled its serial result bit
for bit in all probe runs (0 divergences), and the serial reference was reproduced exactly on a second call, for the
operations probed. This is the only evidence on kernel determinism and covers 13 probe operations, not the 1,070 commands.

## 4. Gates

| Gate | Verdict | Detail |
|---|---|---|
| Log append under 1 ms | **Met** for memory-only, WAL write and WAL + background fdatasync (wall p99 62.5 to 140.9 us; thread-CPU p99 58 to 112 us). **Missed** for fdatasync on every operation on this disk (wall p50 3,996 us, p99 9,089 us; 4,345 of 5,000 ops over 1 ms). **Missed at p99** for group fsync every 32 (wall p99 3,058 us: every 32nd op pays the sync). Wall p99.9 of 2.5 to 4.1 ms for the met policies coincides with the host stall seen in ADR-001 and is not separated here from any growth cost in the log's containers (not checked). | see re-stated gate |
| Undo or redo under 30 ms, unlimited depth | **Met on the log and state swap** (p50 3.8 to 4.9 us, p99 up to 22 us). **Not measured:** the real undo cost, which also includes rebuilding `SceneObject`s from a snapshot and refreshing the viewport. **Not exercised:** unlimited depth with disk spill (everything stayed in memory, 391 MB rss, chains of up to about 8,500 undo steps). **Time travel** (not undo) reaches 31.1 ms max, 51.4 ms in the first run. |
| Crash recovery to the last operation | **Met for process kill** (14 SIGKILL runs, torn tails, truncation sweep). **Not measured:** power loss, disk write reordering, filesystems that lie about flush. |
| Replay determinism 100% | **Met for the 13 stand-in commands** (0 mismatches in 1,000,040 ops). **Not shown** for real commands (seed and tolerance capture, floating point, kernel calls). |
| Merge: conflicts surfaced as items, convergence | Met on generated scenarios and the scripted two-architect demo. |
| Delta sync after a typical edit under 1 s (Appendix B) | The 500-edit two-replica point lies between the 100 and 1,000 edit rows (about 0.1 to 0.2 s). **50 users not measured** (only two replicas and 6 in-process threads on a shared sequencer). |
| Open 1 GB project, first view under 5 s (plan 5.5) | **Not measured with this log.** Open currently scans every frame: 4.6 s warm, up to 29 s cold for 1 M operations; checkpoint-based open is not built. |

Re-stated gate proposal (owner to approve; the plan wording is kept as the stretch goal):
- Append: "a write-ahead frame is written (survives an application crash) at p99 under 1 ms wall and under 200 us thread-CPU, measured with
  the background-fdatasync policy; power-loss durability window at most 20 ms by default; per-operation `fdatasync` is opt-in, reported on owner hardware, and
  is not a pass/fail until measured there (this VM disk costs about 4 ms per call)." Group fsync every N is not an accepted policy for the gate.
- Undo: "undo or redo of the log and state swap under 30 ms p99, AND the end-to-end undo (log, `SceneObject` rebuild, viewport refresh) under 30 ms p99 as a
  separate row measured in the app" (the second part is the part not yet measurable).
- Time travel to an arbitrary operation is a separate row with its own budget (proposal: under 50 ms p99) rather than being read as undo.

## 5. Consequences

- Positive: crash recovery, branching undo, time travel, replay checks and merge come from one structure; undo and redo cost microseconds.
- The dispatcher is the critical path of the whole plan (it gates AI, macros, co-editing and replay tests). Commands that mutate state outside
  the dispatcher must be found by the Stage 1 audit; the replay harness is the CI gate.
- Parameters need a typed schema for each of the 1,070 commands (Stage 2). The spike used opaque stand-in commands; this is the largest remaining
  piece of work (also noted in ADR-001).
- Disk cost is about 771 bytes per op; 1 M ops is 770 MB and needs an on-disk index and checkpoint-based open (all indexes were in memory: 352 to 435 MB rss).
- The JSON-lines form is large (1,543 bytes/op) and slow to import (18.8 s per 100,000 ops); a binary container with zstd (plan 5.5) is needed for the 1 GB open gate.

## 6. Risks

- Real commands may be nondeterministic in ways the stand-ins are not (kernel floating point, tolerance, seed capture); the replay gate may fail for many commands at first.
- Cold recovery varies 4.9 to 29.3 s with the disk; first-view time depends on checkpoint-based open being built.
- Wall-clock tails (p99.9, max) include other agents' load; thread-CPU columns exist for that reason; re-measure on owner hardware.
- The 1 M run reached only about 2,200 objects (cap was 100,000), so structural sharing and HAMT depth are better than for a 100,000-object document with random edits. Merge and checkpoint benchmarks do use 100,000 objects.
- Merge cost grows with the number of changed objects (6.05 s for 63,077); conflict policy is last-writer-wins with a surfaced item and add-wins sets; no permissions, no server, no transport.
- Per-operation `fdatasync` on a local NVMe may behave differently; not measured.

## 7. Open items

1. Relationship between the log's Merkle HAMT and the entity store's persistent tables (ADR-004): two persistent structures and two snapshot-swap undo paths exist; decide whether to unify before Stage 4 (semantic deltas).
2. Checkpoint-based open (recover from the newest checkpoint, on-disk index) so open is not a full scan.
3. Disk spill and unlimited undo depth, exercised with a 100,000-object working set.
4. Run the R1 timed pass (every call timed) over the append path to look for container-doubling spikes (R1 finding F4).
5. Power-loss testing and per-operation `fdatasync` on owner hardware.
6. 50-user sync and permissions (plan 5.5 collaboration row; belongs to the Collaboration and cloud factory).
7. Id and hash types: B uses a 32-byte BLAKE3 hash and 16-byte object ids; Spike C uses a 128-bit content hash; Spike A a 128-bit UUID. Align (`INDEX.md`).
8. Naming and extension of the open container (plan 5.5 says an ADR in Wave 0; not decided here).

## 8. What Wave 1 must build first

1. `d8-oplog` crate: port the operation record, DAG, BLAKE3 content store, WAL with torn-tail truncation and checkpoints from the spike; expose append, batch append, undo marker, read and visit through `d8-ffi` under ADR-001 rules.
2. The C++ command bus (Stage 1): one dispatcher through which every command runs and records an operation first; the audit that lists commands mutating state outside the dispatcher.
3. Checkpoint-based open (open item 2) before the 1 GB first-view gate is quoted.
4. Stage 2 schema generator from the command catalog (starting with the commands of the first factories) and the per-command replay test (Stage 3), wired as a CI gate on the plan's trajectory (replay gate passing for at least 50% of commands by 30 Nov).
5. An end-to-end undo benchmark inside the app (log, `SceneObject` rebuild, viewport refresh) so the 30 ms gate is measured where it matters.
