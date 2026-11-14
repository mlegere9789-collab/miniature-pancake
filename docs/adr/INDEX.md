# Architecture decision records: index (Wave 0 close-out)

Date: 2026-11-14 (UTC). All five records are **Proposed**: the spikes are complete and the evidence is recorded, but the decisions belong to the owner. Every number in the records is copied from the spike reports
on the branches named below; anything the spike reports as not measured is listed as not measured. All measurements were taken on a shared 4-core VM (about 20 agents, load average 4 to 27), so tails are pessimistic;
re-measurement on the owner's hardware is an open item in every record.

## 1. Decision table

| ADR | Decision (one line) | Plan reference | Evidence (branch @ commit) | Gate status in one line |
|---|---|---|---|---|
| [ADR-001](ADR-001-language-policy-and-c-rust-boundary.md) | Rust for new core systems, C++ stays; one plain C ABI (cbindgen header, status codes plus error record, generation-checked handles, panic guard, `C-unwind` callbacks with catch-all wrappers), exactly one Rust staticlib (a facade crate), Corrosion for CMake | 5.1 "ADR-1", 5.2 "ADR-2" | `w0-r1` @ `7be371bd` | Part 1 met (0.175 us mean append, p99.9 2.71 us); **part 2 (app suite) not measured**; per-append `fdatasync` 0.9 to 5.8 ms misses 1 ms on this disk (re-stated gate proposed); Miri, Windows, macOS not measured |
| [ADR-002](ADR-002-operation-log.md) | Option B, staged: wrap the command engine and record operations; Spike B's DAG log with BLAKE3 content addressing, Merkle HAMT state, WAL with torn-tail truncation, per-property CRDT merge; default sync = write-through plus background `fdatasync` every 20 ms | 5.3 "ADR-3" | `w0-spike-b` @ `7bbb7bd5` | Append met for memory, WAL and background-fdatasync (wall p99 62.5 to 140.9 us), **missed** for per-op `fdatasync` (p50 3,996 us) and at p99 for group fsync every 32; undo/redo met on the log only (end-to-end **not measured**); replay 0 mismatches on **13 stand-in commands only**; power loss, 50-user sync, disk spill **not measured** |
| [ADR-003](ADR-003-incremental-derived-data-engine.md) | Keep own incremental engine in Rust (revisions, content-hash keyed pure queries, recorded dependencies, early cutoff, restart-based parallel evaluation, priorities, cancellation); kernel reached through registered C function pointers; design rules: kernel-class nodes (5 us and up), fan-in-64 aggregates, UI roots are tiles | 5.4 "ADR-4" | `w0-spike-c` @ `e188cc98` | **Bounded**: 50 ms met for typical edits (0.05 to 0.17 ms at 1 M walls) and missed above about 80,000 touched nodes with trivial kernels; 20 times met only up to about 4% touched with kernels of 5 us or more; parallel speedup, ABI crossing cost, sanitizers **not measured**; re-stated gate proposed |
| [ADR-004](ADR-004-entity-component-scene-store.md) | Adopt Spike A's entity-component-relationship store as a side table keyed by a stable UUID on `SceneObject` (persistent paged tables, persistent sorted sets, snapshot by `Arc` bump, BVH plus dirty overlay), with five changes before production | 5.5 (scene graph row) | `w0-spike-a` @ `b6275607` | Store-level gates met by 3 to 4 orders (edit p99 14 us at 1 M; 10,000 moved in 14.2 ms; undo p99 444 ns); 181.2 B per entity at 5 M; **store only**: end-to-end edit and undo, incumbent memory comparison, binding to `SceneObject`, disk spill **not measured or built** |
| [ADR-005](ADR-005-kernel-thread-safety-contract.md) | Concurrent kernel calls allowed for tessellation, mass properties, mesh and B-rep booleans, fillet, sweep, hull; SubD and all 3dm/STEP/IGES I/O serialized; conditions: warm each published B-rep, patch openNURBS, never flip test switches, `MANIFOLD_PAR=-1`, 256 KiB worker stacks, no cancellation inside B-rep calls | 5.2 threads bullet, 5.4 fallback | `w0-kthreads` @ `e5bc8421` | Audit met; TSan run **bounded** (34 and 66 iterations, not 200; `-O1`; probe operations only); 13 race root causes found, 2 kernel ones fixed on the branch but **not on cand2**, openNURBS patch **not wired**; off-origin sphere tessellation defect open |

The plan's numbering and these records: ADR-001 covers plan ADR-1 (language policy) and ADR-2 (C/Rust boundary); ADR-002 is plan ADR-3; ADR-003 is plan ADR-4; ADR-004 is the scene-graph row of 5.5; ADR-005 is the threads bullet of 5.2 and the fallback in 5.4.
The container format name and extension (5.5, "decided in an ADR in Wave 0") and the IFC library selection (5.5, after counsel reviews licences) are NOT decided by these records and have no spike result; see section 5.

## 2. Gates the owner is asked to restate (proposals, each in its ADR)

| Plan gate | Why it cannot stand as written | Proposed wording | Where |
|---|---|---|---|
| Log append under 1 ms (5.3) | per-append `fdatasync` costs 0.9 to 5.8 ms on this VM disk (R1) and p50 3,996 us (B); no design meets 1 ms with a flush per append on this disk | write-ahead frame written (survives application crash) at p99 under 1 ms wall and under 200 us thread-CPU, background `fdatasync` every 20 ms as default (loss window at most about 20 ms); per-op `fdatasync` opt-in, reported on owner hardware, no pass/fail until measured there | ADR-001 section 4, ADR-002 section 4 |
| Undo or redo under 30 ms (5.3, Appendix B) | measured on the log and store swap only; time travel reaches 31.1 ms (51.4 ms first run) | keep 30 ms p99 for the log plus state swap AND add an end-to-end row (log, `SceneObject` rebuild, viewport refresh) measured in the app; time travel is its own row (proposal under 50 ms p99) | ADR-002 section 4 |
| Edit propagation under 50 ms at 1 M elements (5.4) and 20 times speedup on partial edits | met only up to bounded edit sizes with the engine's 0.5 to 0.7 us per node cost | 20 times for edits touching up to 4% of derived data with kernel-class queries (5 us and up); 50 ms edit-to-visible for edits whose visible closure needs at most 50 ms of serial kernel time, and for any viewport-first edit; originals kept as stretch goals for the parallel build on owner hardware. (The node-count form "80,000 nodes" in the spike holds only for trivial kernels; with a 5 us kernel about 8,800 nodes fit 50 ms: my arithmetic, ADR-003 section 4) | ADR-003 section 4 |

## 3. Conflicts between the records that Wave 1 must settle before the crates are cut

1. **Three id and hash types.** Spike B: 16-byte `ObjectId`, 32-byte BLAKE3 `Hash`; Spike A: 128-bit UUID plus 30-bit slot; Spike C: 128-bit `Hash128`; R1: 16-byte object ids in the record. Proposal: one `d8-types` crate (16-byte id, 32-byte content hash, one status namespace), with the engine's 128-bit hash derived or replaced. Owner of the decision: Performance factory with the Collaboration and cloud factory.
2. **Two persistent state structures and two snapshot-swap undo paths** (the oplog's Merkle HAMT, the store's paged tables). Stage 1 of ADR-002 does not need them unified (state stays in `Document`/`SceneObject`); the decision is due before Stage 4 (semantic deltas).
3. **Two log designs.** R1's probe log is a flat arena of 160-byte records; Spike B's is a DAG with 735-byte frames. Decision recorded here: Spike B's design is the data model, R1's rules (handles, paged containers against doubling spikes, status codes) are the surface. R1's paged sequence applies to the hot-path containers of the production log; whether Spike B's append has doubling spikes was not checked (ADR-002 open item 4).
4. **Error and status codes.** R1 uses `D8_*` (int32), Spike C uses `D8G_*`. One namespace in the facade crate.
5. **Cancellation.** The engine cancels cooperatively; the kernel cannot be cancelled mid B-rep boolean. Wave 1 must not promise sub-call cancellation.

## 4. Crate layout for `dino8-core` (proposal for the first Wave 1 commit)

```
dino8-core/
  Cargo.toml                workspace: members = crates/*; the Wave 0 spikes stay standalone (each has its own [workspace]) and are excluded
  rust-toolchain.toml       pinned stable (rustc 1.97.0 was used by every spike)
  crates/
    d8-types/               ObjectId (16 B), ContentHash, status codes, error record; no unsafe        (added by this index: not in the requested four, needed by conflict 1)
    d8-store/               ptable, sorted, scene, query, bvh, history, bulk builder                  from spike A; deps: d8-types
    d8-oplog/               op record, DAG, HAMT, content store, WAL, checkpoints, dispatcher/replay,
                            CRDT merge, JSON-lines export                                             from spike B; deps: d8-types, blake3
    d8-engine/              hash, engine, scheduler, kernel registry (callback types, no unsafe)      from spike C; deps: d8-types, rayon
    d8-ffi/                 the ONLY staticlib (+cdylib for the sync server later); the ONLY crate
                            with unsafe; handle table, panic guard, cbindgen header, d8_abi_version,
                            log, store and engine C functions; probe-only functions behind a feature  from R1; deps: all four above
  cpp/                      C++ wrapper namespace (RAII handles, catch-all callback template, status
                            adapter), boundary test program, ASan/UBSan/TSan configurations
  ffi-probe/, kernel_tsan_probe/   kept from Wave 0 until d8-ffi and the nightly TSan job replace them
```
Rules from the records: exactly one Rust staticlib per final binary (ADR-001); `#![forbid(unsafe_code)]` (or `deny`) in every crate except `d8-ffi`; `panic = "unwind"` in the release profile; hot-path containers paged, not doubling (ADR-001 F4);
CI jobs per ADR-001 section 7 (fmt, clippy `-D warnings`, test, loom, C++ ASan+UBSan and TSan, header drift) plus the nightly kernel TSan job (ADR-005).

## 5. Sequence of the first Wave 1 Rust deliverables

Wave 1 is Wed 18 Nov to Mon 30 Nov (plan 6.2). Owners are factories named in plan 6.3, as PROPOSALS: the plan has no factory for platform plumbing, so the Performance factory is proposed for the workspace, CI and engine core, and the owner is asked to confirm. "Depends on" means cannot be merged before.

| # | Deliverable | Proposed owner (factory) | Depends on | Proposed window | Done when (evidence) |
|---|---|---|---|---|---|
| W1-00 | Kernel prerequisites: merge `9555319d` and `7f75a3bd` into the kernel base; wire the openNURBS patch; fix the off-origin adaptive tessellation defect with a failing test first | Kernel geometry and robustness | none | 18 to 20 Nov | kernel CI green; TSan probe 0 reports on the patched build |
| W1-01 | Workspace skeleton: `Cargo.toml`, toolchain pin, `d8-types`, `d8-ffi` facade with the log append path, committed header and drift check, Corrosion in the app CMake, CI jobs; **full app test suite in the Corrosion-linked configuration (R1 gate part 2)** | Performance | none | 18 to 19 Nov | app suite pass count identical to the base; `ctest` passes in release and ASan+UBSan |
| W1-02 | `d8-oplog` Stage 1: record, DAG, content store, WAL with torn-tail truncation, checkpoints; append, batch append, undo marker, read, visit through `d8-ffi` | Collaboration and cloud | W1-01 | 19 to 22 Nov | spike B tests (83) and crash tests ported and green; append p99 under the re-stated gate with the background-fdatasync policy |
| W1-03 | `d8-store` port with bulk builder, sparse leaves and zone maps; `SceneObject` UUID; FFI surface for queries | BIM architecture (semantic components) with Performance (benchmarks) | W1-01 | 19 to 24 Nov | 5 M benchmark re-run as a CI gate (no regression over 3%); integrity check against `Document` after every command in the suite |
| W1-04 | `d8-engine` production core (spike C semantics, `abi.rs` moved into `d8-ffi`), serialized lane first, thread-safe lane after W1-00 | Performance | W1-01; W1-00 for the thread-safe lane | 19 to 24 Nov | 47 spike tests ported and green; loom and TSan jobs on engine and ABI module |
| W1-05 | C++ command bus (dispatcher) and the audit of commands that mutate state outside it | Scripting, API and AI (typed command surface), reviewed by Collaboration and cloud | W1-02 | 22 to 27 Nov | every command in the existing suite records one operation; audit list closed |
| W1-06 | Engine adopter 1: tessellation as a content-keyed query, invalidation-correctness test, existing tessellation tests unchanged, measured edit-to-visible with the real kernel on a 10,000-object scene | Kernel geometry and robustness (gate by Performance) | W1-04, W1-00 | 23 to 27 Nov | invalidation test and measured figure recorded against the re-stated ADR-003 gate |
| W1-07 | Checkpoint-based open for the log | Collaboration and cloud | W1-02 | 24 to 27 Nov | open time measured against the first-view gate; cold and warm figures |
| W1-08 | Stage 2 schemas for the first factories' commands and the Stage 3 replay test per command as a CI gate | Scripting, API and AI (schemas); each factory its own commands' replay tests | W1-05 | 25 to 30 Nov | plan exit criterion: replay gate passing for at least 50% of commands by 30 Nov (the spikes do not show this is feasible: replay was proven only on 13 stand-in commands) |
| W1-09 | Engine adopters 2 and 3: mass properties; section and elevation views as fan-in-64 trees | Kernel geometry and robustness; Drafting and documentation | W1-06 | 27 to 30 Nov | invalidation tests; section regeneration time against the 1 s gate |
| W1-10 | End-to-end undo benchmark in the app (log, `SceneObject` rebuild, viewport refresh) | Performance | W1-05 | 27 to 30 Nov | measured p99 against the re-stated undo row |

Order dependencies: W1-00 and W1-01 start together; W1-02, W1-03 and W1-04 run in parallel after W1-01; W1-05 after W1-02; W1-06 after W1-04 and W1-00; W1-07 after W1-02; W1-08 after W1-05; W1-09 after W1-06; W1-10 after W1-05.
Critical path: W1-01 (the app suite in the new configuration) then W1-02 then W1-05 then W1-08, because the command bus gates replay, macros, AI and co-editing (plan 6.1 item 4). Decisions needed before W1-01 closes: conflicts 1 and 4 of section 3.
The proposed dates are my sequencing of plan 6.2, not a result of any spike; the one effort figure the spikes give is that the R1 mechanism took 42 minutes of agent time while its integration into the app was not attempted.

## 6. ADRs not yet written (named by the plan, no spike result)

- Container format name and extension (plan 5.5): operation log + snapshots + content-addressed blobs + JSON schema, zstd; Spike B's canonical JSON lines is a candidate for the text form (1,543 bytes per op, 18.8 s per 100,000 ops import, unoptimized). 1 GB open under 5 s is unmeasured. Proposed owner: Interop and migration.
- IFC read/write library (plan 5.5): after counsel reviews licences. Proposed owner: Interop and migration.
- Flow runtime v2 (plan 5.5): Rust runtime behind the existing node API; depends on ADR-003 adoption order.

## 7. Evidence index

| Branch | Commit | Files read for these records |
|---|---|---|
| `w0-r1` | `7be371bd` | `dino8-core/ADR-boundary.md`, `dino8-core/ffi-probe/` |
| `w0-spike-a` | `b6275607` | `dino8-core/spikes/scene/RESULTS.md` |
| `w0-spike-b` | `7bbb7bd5` | `dino8-core/spikes/oplog/RESULTS.md` |
| `w0-spike-c` | `e188cc98` | `dino8-core/spikes/graph/RESULTS.md` |
| `w0-kthreads` | `e5bc8421` | `dino8-core/KERNEL_THREADS.md`, `dino8-core/kernel_tsan_probe/` |

These records were written from the reports and Cargo manifests on those branches. The spikes' code and benchmarks were not re-run for this job (no code changes were made).
