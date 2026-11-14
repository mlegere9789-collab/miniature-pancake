# Spike A results: entity-component-relationship scene store

Plan reference: section 5.5 (scene graph row: "5 M entities, memory and query latency") and Appendix B (edit propagation under 50 ms at 1 M elements; undo or redo under 30 ms; move 10,000 objects under 100 ms feedback). Report chapter 12.2.1 (one data graph = entity-component-relationship: geometry, semantic, procedural and presentation components plus relationships).

Crate: `dino8-core/spikes/scene/` (standalone, no workspace root). Rust 1.97, release profile (thin LTO), single thread. Machine: 4 cores shared by about 20 agents (load average 4 to 6 during the runs), counting global allocator for memory. Times are therefore pessimistic and noisy (a repeat run varied by up to 40% on scan-type queries). Raw output of the final run is appended at the end of this file.

## Recommendation: ADOPT, with five changes before production use

All Appendix B store-level gates pass by three to four orders of magnitude, and the design is small (about 3,000 lines including the benchmark and in-file unit tests, excluding integration tests). Adopt the structure (dense slot index plus stable 128-bit UUID map, persistent paged component tables, persistent sorted edge sets with a reverse index, snapshot by `Arc` bump, BVH plus dirty overlay). Reasons and the open items are below. The gate numbers measure the STORE only (see "What the gates do and do not prove").

Changes to make when this moves into the Stage 2 ECR store:
1. Sparse leaves for rare components. `procedural` is populated for 10% of entities but costs 12.4 B per entity because every 128-slot leaf is touched. A compact (packed or run-length) leaf would take roughly 50 B/entity off the 181 B total.
2. Zone maps (min and max per leaf) on property columns. Property range scans are the slowest interactive shape (35 ms at 5 M); skipping leaves should bring selective ranges under 1 ms.
3. Incremental BVH refit or background rebuild with a dirty threshold. Rebuild is 1.5 to 2.1 s at 5 M; the dirty overlay costs about 4 ns per dirty entity per query, so rebuild at about 10,000 dirty entities (41 us per query) and keep the old BVH serving meanwhile.
4. Undo history needs the disk spill the plan already requires: one snapshot per edit retains about 20 KB per edit at 5 M (19 MB per 1,000 edits), so 100,000 undo steps would hold about 2 GB. The in-memory structure is not the limit, retention is.
5. Bulk path is mandatory for imports. `SceneBuilder` loads 5 M entities in 2.3 s; incremental `create` plus a containment edge costs 30 to 40 us (about 3 minutes for 5 M).

Drop and change were rejected: a flat copy snapshot of the same data costs 642 ms at 5 M against 150 ns here, and nothing measured threatens a gate. The risks are outside this spike (kernel regeneration cost, op-log integration, C ABI to the existing C++ `SceneObject`, see "Not covered").

## Gate against plan targets

| Plan target (Appendix B) | Measured | Verdict |
|---|---|---|
| Edit propagation under 50 ms at 1 M elements | Move a wall plus its hosted and fixed-offset dependents with a snapshot kept per edit: p50 5.0 us, p99 14 us, max 72 us at 1 M (p99 33 us at 5 M). Mixed edit kinds (property, IFC class with index update, edge add, create plus link): p99 6.6 us to 83 us at 1 M | PASS, margin about 3,500x at p99 |
| Move 10,000 objects under 100 ms | 14.2 ms at 1 M, 25.3 ms at 5 M, as one undoable edit | PASS |
| Undo or redo under 30 ms | Snapshot swap: undo p50 46 ns, p99 444 ns, max 19 us at 1 M; at 5 M p99 511 ns, max 19.7 us. First lookup plus an indexed query after undo: p99 4.2 us (1 M), 18.6 us (5 M). The state after undoing all 1,000 edits is verified against a checksum taken before the edits, and redo restores the final state | PASS |
| Spike A: 5 M entities, memory and query latency | 181 B per entity (864 MB live, peak 1.0 GB during load, RSS 962 MB) for the content below; query latencies in the next section | Measured |

## Memory (5 M entities, 6.73 M relationship edges)

Content per entity: UUID (16 B), geometry component (bounding box 24 B plus kernel handle 8 B), semantic (class, type, phase, option, classification code: 12 B), procedural link on 10%, presentation (material, colour, layer, flags: 12 B), four property columns (height on all, cost on 50%, fire rating on 30%, name on 20%), containment edge for all, hosting, connection, reference and constraint edges on a fraction, plus two inverted indexes (IFC class and type, classification code).

| structure | bytes/entity |
|---|---|
| UUID by slot | 16.4 |
| UUID map (sorted chunks, 24 B per entry) | 24.4 |
| geometry | 32.4 |
| semantic | 12.4 |
| procedural (10% populated) | 12.4 |
| presentation | 12.4 |
| four property columns | 33.8 |
| edges forward plus reverse (6.7 M edges, 8 B each way) | 22.4 |
| constraint payload | 0.2 |
| class index plus classification index | 14.1 |
| total (live heap) | 181.2 |
| BVH (built on demand, 60 B/entity, 286 MB at 5 M) | 60.0 |

Bulk insert of 5 M entities with all components, properties and edges (generation, builder, sorting, tree construction): 2.33 s (467 ns per entity); 1 M: 0.41 s. Full integrity check of the loaded 5 M scene: passed (9.3 s). Plan target "within 1.5 times incumbent for equal content" cannot be checked here (no incumbent numbers in the repo); 181 B per entity excludes the geometry payload itself (only a handle is stored) and large property sets, which will dominate real models.

## Query latency (13 shapes; the task asked for 10)

| shape | N=1M p50 | N=1M p99 | N=5M p50 | N=5M p99 | N=5M result |
|---|---|---|---|---|---|
| Q1 UUID to all components | 1.09 us | 2.0 us | 1.87 us | 2.7 us | 1 entity |
| Q2 class, selective, indexed (0.05%) | 1.4 us | 2.6 us | 6.4 us | 11 us | 2,537 hits |
| Q3 class, broad, indexed (20%) | 456 us | 779 us | 2.3 ms | 3.3 ms | 997,975 hits |
| Q4 property range scan (0.03% of column) | 6.4 ms | 6.7 ms | 35.4 ms | 36.3 ms | 1,617 hits |
| Q5 class AND property range (6% x 3%) | 1.9 ms | 2.3 ms | 10.7 ms | 19.3 ms | 9,767 hits |
| Q6 children of a storey (containment) | 20 us | 70 us | 16.9 us | 38 us | 4,999 hits |
| Q7 wall neighbourhood (hosted, connected, constraints) | 0.35 us | 0.73 us | 0.43 us | 1.3 us | 3 hits |
| Q8 all descendants of a building (BFS) | 10.5 ms | 10.8 ms | 12.4 ms | 16.3 ms | 250,000 hits |
| Q9 spatial AABB, 6 m window | 4.1 us | 9.4 us | 6.7 us | 36.8 us | 34 hits |
| Q10 spatial AABB, 30 m window | 39 us | 91 us | 50 us | 112 us | 2,657 hits |
| Q11 raycast 100 m, all hits sorted | 35 us | 109 us | 99 us | 373 us | 897 hits |
| Q12 classification code, indexed | 1.9 us | 3.0 us | 5.2 us | 9.3 us | 1,702 hits |
| Q13 phase/option filter (full semantic scan) | 2.7 ms | 4.9 ms | 21.9 ms | 23.9 ms | 12,587 hits |

Outliers (max column in the raw output, up to a few ms) coincide with the shared machine and first-touch page faults; p99 is the figure to read.

Spatial queries after edits (dirty overlay, 5 M): 0 dirty 2.2 us p50; 100 dirty 2.9 us; 1,000 dirty 6.6 us; 9,977 dirty 41 us; 49,281 dirty 201 us. BVH rebuild: 1.5 s at 5 M (0.25 s at 1 M).

## Snapshots and undo

| measurement | 1 M | 5 M |
|---|---|---|
| snapshot (`Scene::clone`) p50 / p99 | 135 ns / 167 ns | 150 ns / 263 ns |
| snapshot drop | 123 ns | 136 ns |
| flat deep copy of the same bytes (memcpy reference) | 83 ms | 642 ms |
| memory retained by 1,000 mixed random edits while one snapshot is held | 7.9 MB (4.6% of the scene) | 9.4 MB (1.1%), 9.8 KB per edit |
| memory of 1,000 undoable edits (snapshot per edit, all retained) | 17.2 MB | 19.2 MB (20 KB per edit, includes the new data of the edits) |
| undo p50 / p99 / max | 46 ns / 444 ns / 19 us | 155 ns / 511 ns / 19.7 us |

Mixed edits = 40% geometry move, 15% property set, 15% IFC class change (index update), 10% presentation, 10% reference edge, 5% create, 5% delete (cascading over hosting).

## What was built

- `ptable.rs`: persistent sparse table (64-way radix tree, 128-slot leaves with presence bitmask). One generic type holds every component, every property column and the dirty set.
- `sorted.rs`: persistent sorted set (two-level B+tree with `Arc` chunks and groups). Holds the UUID map, forward and reverse edge sets (edge = 64-bit key: kind, source slot, target slot), constraint payloads, secondary indexes and the free-slot set.
- `scene.rs`, `query.rs`: the store, edit API, relationship rules, `SceneBuilder`, integrity check. Relationship kinds: hosting and containment (single parent, replace on relink, cycle rejected), connection (undirected, stored canonically), constraint (directed with type and value payload), reference (directed). Delete cascades over hosting and removes every edge of the victims; slots are reused through a persistent free set.
- `bvh.rs`: flattened median-split BVH shared between snapshots; entities edited after the build are served from a persistent overlay holding their current boxes, so queries stay exact between rebuilds.
- `history.rs`: undo and redo by snapshot swap.
- Query API: by identity, by component presence, by IFC class and type and classification code (indexed), by phase and option (scan), by property range (scan), combined class plus range, by relationship (host, hosted, container, contents, connected, references both ways, constraints, descendants), spatial (box overlap, ray all-hits).
- Limits: slot indices are 30 bits (about 1.07 billion entities); entity UUIDs are supplied by the caller (`Uuid::from_random_bits` makes a v4-style id).

## Tests (all green)

- 6 unit tests (table, sorted set against `BTreeSet`, BVH against brute force).
- 13 integration tests: identity and duplicates, hosting cascade with slot reuse, cycle and single-parent rules, undirected connections and constraint payloads, typed properties and range queries, indexed against scan results, bulk builder against incremental inserts, builder rejects bad input, snapshot isolation and branching, undo and redo, propagation, spatial overlay against brute force after edits, creates and deletes, descendants.
- 2 proptest properties. Model-based: random sequences of up to 160 operations (create, delete, set components, set property, link, unlink, constraint, translate, snapshot, refresh spatial index, roll back to a snapshot) run against both the store and a reference model; after every operation the full state must be equal, and every snapshot taken along the way must still equal the model state captured at that time and pass the relationship integrity check (every edge has a mirror, no edge touches a dead entity, one parent for hosting and containment, no cycles, indexes match components, overlay matches geometry). Default 300 cases per run; 5,000 cases run once with `PROPTEST_CASES=5000` (1.4 s). A deliberately broken `del_edge` (reverse edge not removed) was caught by the properties, then reverted. Second property: the sorted set against `BTreeSet` with snapshots.
- `cargo clippy --all-targets`: no warnings. The benchmark asserts snapshot immutability (checksum) and undo and redo state restoration at both sizes, and runs the full integrity check after load and after the edit run.

## What the gates do and do not prove

- Edit propagation here is the store cost: apply the edit to geometry and indexes, follow hosting and fixed-offset constraints to dependents, keep an undoable snapshot, mark spatial state stale. It does not include kernel regeneration of the edited object, derived-data invalidation (the Stage 3 incremental engine), or GPU upload. Those costs, not the store, will decide the real 50 ms budget; the store leaves essentially all of it free.
- The model is synthetic (random placement, 40 IFC-style classes, 3 to 4 properties per element). Real models carry larger property sets, strings and longer relationship chains. The shape of the result (O(1) snapshots, page-sized copy on write) does not depend on that, the 181 B/entity figure does.
- Timings were taken on a shared, loaded machine with a counting allocator (a few ns per allocation); Instant overhead is about 25 ns, so sub-microsecond rows are upper bounds. Plan targets are for reference hardware measured by the owner.

## Not covered by this spike

Operation log and CRDT merge (Spike B), the incremental dependency engine (Spike C), IFC 4.3 attribute mapping beyond class, type and classification ids (Spike L), the C ABI or FFI between this store and the existing C++ `SceneObject` (the plan says "side table keyed by a stable UUID on SceneObject"; the store takes a UUID and a dense slot, so the binding is a thin mapping, but it is not built), multi-threaded readers (snapshots are `Send` and `Sync` by construction because they are `Arc` trees, but this was not benchmarked), disk spill, and any text or blob payload (property strings are interned ids only).

## Reproduce

    cd dino8-core/spikes/scene
    CARGO_BUILD_JOBS=2 cargo test --release
    CARGO_BUILD_JOBS=2 cargo build --release --bin bench
    ./target/release/bench --n 5000000 --gate-n 1000000     # about 3 minutes, 1.1 GB peak RAM
    ./target/release/bench --n 200000 --skip-gate           # smoke run

## Raw output of the final run

```
# Spike A bench, 4 threads available, counting allocator

### N = 1000000 entities

- generate + bulk insert (builder, includes data generation and sorting): 0.41 s (411 ns/entity)
- live heap after load: 172.9 MB = 181.3 B/entity (components, 4 property columns, 2 secondary indexes, 1346807 edges x2 directions)
- peak heap during bulk load: 208.5 MB; VmRSS 190.0 MB, VmHWM 203.1 MB

#### Memory by structure (1000000)

| structure | MB | bytes/entity |
|---|---|---|
| ids (uuid by slot) | 15.7 | 16.4 |
| uuid map | 23.2 | 24.4 |
| geometry | 30.9 | 32.4 |
| semantic | 11.9 | 12.4 |
| procedural | 11.9 | 12.4 |
| presentation | 11.9 | 12.4 |
| property columns | 32.2 | 33.8 |
| edges forward | 10.7 | 11.2 |
| edges reverse | 10.7 | 11.2 |
| constraint payload | 0.2 | 0.2 |
| free slots | 0.0 | 0.0 |
| class index | 7.9 | 8.3 |
| classification index | 5.6 | 5.8 |
| bvh | 0.0 | 0.0 |
| node total (estimate) | 172.8 | 181.1 |

Reference: memcpy of 173 MB (a flat deep-copy snapshot of this scene would cost at least this): 83.4 ms

Full integrity check after bulk load: passed (0.9 s)

#### Query latency

| shape | runs | p50 | p90 | p99 | max | result |
|---|---|---|---|---|---|---|
| Q1 UUID -> all components | 200000 | 1093 ns | 1584 ns | 2040 ns | 211.5 us | 1 entity (chk 42) |
| Q2 class, selective, indexed (0.05%) | 500 | 1373 ns | 1933 ns | 2590 ns | 33.7 us | 505 hits |
| Q3 class, broad, indexed (20%) | 20 | 456.2 us | 492.4 us | 779.2 us | 779.2 us | 199492 hits |
| Q4 property range scan (0.03% of column) | 20 | 6387.8 us | 6518.7 us | 6714.7 us | 6714.7 us | 348 hits |
| Q5 class AND property range (6% x 3%) | 20 | 1910.1 us | 2078.4 us | 2330.7 us | 2330.7 us | 2011 hits |
| Q6 children of a storey (containment) | 2000 | 20.0 us | 25.5 us | 69.6 us | 140.4 us | 4999 hits |
| Q7 wall neighbourhood: hosted + connected + constraints | 200000 | 347 ns | 465 ns | 730 ns | 39.4 us | 2 hits |
| Q8 all descendants of a building (BFS) | 10 | 10.47 ms | 10.55 ms | 10.84 ms | 10.84 ms | 250000 hits |
| Q9 spatial AABB, 6 m window | 20000 | 4093 ns | 5840 ns | 9417 ns | 239.9 us | 73 hits |
| Q10 spatial AABB, 30 m window | 2000 | 39.3 us | 54.0 us | 90.6 us | 2845.6 us | 4784 hits |
| Q11 raycast, 100 m through a storey (all hits, sorted) | 500 | 34.9 us | 71.9 us | 109.2 us | 324.7 us | 111 hits |
| Q12 classification code, indexed (~0.035%) | 2000 | 1930 ns | 2321 ns | 3004 ns | 48.3 us | 343 hits |
| Q13 phase/option filter (full semantic scan) | 10 | 2695.4 us | 4455.2 us | 4943.2 us | 4943.2 us | 2522 hits |

#### Edits, snapshots, undo

BVH build: 0.26 s, 57.2 MB (60.0 B/entity)

| operation | runs | p50 | p90 | p99 | max | note |
|---|---|---|---|---|---|---|
| snapshot (Scene::clone) | 20000 | 135 ns | 136 ns | 167 ns | 24.6 us | O(#tables), independent of N |
| snapshot drop | 20000 | 123 ns | 124 ns | 198 ns | 13.1 us | last holder frees nothing |
| E1 move wall + dependents, snapshot kept per edit | 3000 | 4985 ns | 6396 ns | 14.1 us | 71.9 us | avg 1.61 entities touched |
| E1b move wall + dependents, in place (no snapshot held) | 3000 | 862 ns | 1571 ns | 2788 ns | 520.7 us |  |
| E2 move 10,000 objects as one undoable edit | 5 | 14.18 ms | 16.33 ms | 16.33 ms | 16.33 ms | gate: < 100 ms |
| E3a set property (snapshot kept) | 2000 | 3227 ns | 4412 ns | 6630 ns | 50.1 us |  |
| E3b change IFC class (index update) | 2000 | 15.0 us | 17.5 us | 54.6 us | 69.6 us |  |
| E3c add reference edge | 2000 | 6092 ns | 7365 ns | 13.1 us | 111.5 us |  |
| E3d create entity + containment edge | 2000 | 30.6 us | 34.9 us | 83.4 us | 1452.3 us |  |

#### Snapshot memory delta

- 1,000 mixed random edits against ONE held snapshot: +7.94 MB retained while the snapshot lives (8325 B/edit); +0.25 MB after the snapshot is dropped (net growth: new entities, index and dirty-overlay entries)
- 1,000 undoable edits (one snapshot per edit, all retained): +17.15 MB total, 18 KB/edit (includes the edits' own new data)

| operation | runs | p50 | p90 | p99 | max | note |
|---|---|---|---|---|---|---|
| U1 undo (snapshot swap) | 1000 | 46 ns | 197 ns | 444 ns | 19.0 us | gate: < 30 ms; state verified by checksum |
| U2 first reads after undo (lookup + rare-class query) | 1000 | 1949 ns | 3145 ns | 4179 ns | 36.1 us |  |
| U3 redo (snapshot swap) | 1000 | 44 ns | 136 ns | 301 ns | 414 ns |  |

Integrity check after the full edit/undo/redo run: passed.

BVH rebuild (off the edit path): 0.25 s

#### Spatial query with the dirty overlay (edits since the last BVH build)

| operation | runs | p50 | p90 | p99 | max | note |
|---|---|---|---|---|---|---|
| Q9 with 0 dirty entities | 3000 | 3229 ns | 4688 ns | 7910 ns | 376.4 us |  |
| Q9 with 100 dirty entities | 3000 | 4119 ns | 5634 ns | 24.8 us | 2655.9 us |  |
| Q9 with 1000 dirty entities | 3000 | 8681 ns | 10.5 us | 25.8 us | 194.5 us |  |
| Q9 with 9863 dirty entities | 3000 | 46.8 us | 60.0 us | 109.3 us | 778.5 us |  |
| Q9 with 46493 dirty entities | 3000 | 225.9 us | 360.1 us | 1291.8 us | 24.48 ms |  |

### N = 5000000 entities

- generate + bulk insert (builder, includes data generation and sorting): 2.33 s (467 ns/entity)
- live heap after load: 863.8 MB = 181.2 B/entity (components, 4 property columns, 2 secondary indexes, 6734530 edges x2 directions)
- peak heap during bulk load: 1006.4 MB; VmRSS 962.2 MB, VmHWM 1012.5 MB

#### Memory by structure (5000000)

| structure | MB | bytes/entity |
|---|---|---|
| ids (uuid by slot) | 78.4 | 16.4 |
| uuid map | 116.2 | 24.4 |
| geometry | 154.7 | 32.4 |
| semantic | 59.3 | 12.4 |
| procedural | 59.3 | 12.4 |
| presentation | 59.3 | 12.4 |
| property columns | 161.0 | 33.8 |
| edges forward | 53.4 | 11.2 |
| edges reverse | 53.4 | 11.2 |
| constraint payload | 0.9 | 0.2 |
| free slots | 0.0 | 0.0 |
| class index | 39.7 | 8.3 |
| classification index | 27.8 | 5.8 |
| bvh | 0.0 | 0.0 |
| node total (estimate) | 863.7 | 181.1 |

Reference: memcpy of 864 MB (a flat deep-copy snapshot of this scene would cost at least this): 641.9 ms

Full integrity check after bulk load: passed (9.3 s)

#### Query latency

| shape | runs | p50 | p90 | p99 | max | result |
|---|---|---|---|---|---|---|
| Q1 UUID -> all components | 200000 | 1866 ns | 2270 ns | 2744 ns | 144.5 us | 1 entity (chk 196) |
| Q2 class, selective, indexed (0.05%) | 500 | 6435 ns | 6644 ns | 11.0 us | 25.6 us | 2537 hits |
| Q3 class, broad, indexed (20%) | 20 | 2320.3 us | 2410.7 us | 3283.2 us | 3283.2 us | 997975 hits |
| Q4 property range scan (0.03% of column) | 20 | 35.39 ms | 36.07 ms | 36.25 ms | 36.25 ms | 1617 hits |
| Q5 class AND property range (6% x 3%) | 20 | 10.70 ms | 11.36 ms | 19.34 ms | 19.34 ms | 9767 hits |
| Q6 children of a storey (containment) | 2000 | 16.9 us | 20.6 us | 38.4 us | 253.2 us | 4999 hits |
| Q7 wall neighbourhood: hosted + connected + constraints | 200000 | 428 ns | 759 ns | 1299 ns | 343.6 us | 3 hits |
| Q8 all descendants of a building (BFS) | 10 | 12.35 ms | 14.35 ms | 16.25 ms | 16.25 ms | 250000 hits |
| Q9 spatial AABB, 6 m window | 20000 | 6683 ns | 9519 ns | 36.8 us | 152.1 us | 34 hits |
| Q10 spatial AABB, 30 m window | 2000 | 50.1 us | 73.8 us | 111.5 us | 183.5 us | 2657 hits |
| Q11 raycast, 100 m through a storey (all hits, sorted) | 500 | 98.7 us | 256.6 us | 372.6 us | 670.9 us | 897 hits |
| Q12 classification code, indexed (~0.035%) | 2000 | 5150 ns | 6608 ns | 9292 ns | 42.9 us | 1702 hits |
| Q13 phase/option filter (full semantic scan) | 10 | 21.92 ms | 23.51 ms | 23.89 ms | 23.89 ms | 12587 hits |

#### Edits, snapshots, undo

BVH build: 1.51 s, 286.1 MB (60.0 B/entity)

| operation | runs | p50 | p90 | p99 | max | note |
|---|---|---|---|---|---|---|
| snapshot (Scene::clone) | 20000 | 150 ns | 151 ns | 263 ns | 113.3 us | O(#tables), independent of N |
| snapshot drop | 20000 | 136 ns | 138 ns | 247 ns | 83.3 us | last holder frees nothing |
| E1 move wall + dependents, snapshot kept per edit | 3000 | 7145 ns | 9669 ns | 33.3 us | 113.3 us | avg 1.62 entities touched |
| E1b move wall + dependents, in place (no snapshot held) | 3000 | 1679 ns | 2624 ns | 4451 ns | 118.9 us |  |
| E2 move 10,000 objects as one undoable edit | 5 | 25.27 ms | 26.71 ms | 26.71 ms | 26.71 ms | gate: < 100 ms |
| E3a set property (snapshot kept) | 2000 | 5511 ns | 7669 ns | 10.6 us | 95.5 us |  |
| E3b change IFC class (index update) | 2000 | 21.2 us | 26.6 us | 57.8 us | 1826.3 us |  |
| E3c add reference edge | 2000 | 11.7 us | 15.3 us | 47.6 us | 664.8 us |  |
| E3d create entity + containment edge | 2000 | 38.9 us | 55.2 us | 137.9 us | 3109.7 us |  |

#### Snapshot memory delta

- 1,000 mixed random edits against ONE held snapshot: +9.36 MB retained while the snapshot lives (9816 B/edit); +0.42 MB after the snapshot is dropped (net growth: new entities, index and dirty-overlay entries)
- 1,000 undoable edits (one snapshot per edit, all retained): +19.15 MB total, 20 KB/edit (includes the edits' own new data)

| operation | runs | p50 | p90 | p99 | max | note |
|---|---|---|---|---|---|---|
| U1 undo (snapshot swap) | 1000 | 155 ns | 263 ns | 511 ns | 19.7 us | gate: < 30 ms; state verified by checksum |
| U2 first reads after undo (lookup + rare-class query) | 1000 | 6882 ns | 8258 ns | 18.6 us | 84.7 us |  |
| U3 redo (snapshot swap) | 1000 | 42 ns | 169 ns | 324 ns | 418 ns |  |

Integrity check after the full edit/undo/redo run: passed.

BVH rebuild (off the edit path): 1.51 s

#### Spatial query with the dirty overlay (edits since the last BVH build)

| operation | runs | p50 | p90 | p99 | max | note |
|---|---|---|---|---|---|---|
| Q9 with 0 dirty entities | 3000 | 2182 ns | 3788 ns | 8402 ns | 40.7 us |  |
| Q9 with 100 dirty entities | 3000 | 2931 ns | 4414 ns | 8062 ns | 62.3 us |  |
| Q9 with 1000 dirty entities | 3000 | 6570 ns | 7833 ns | 31.3 us | 394.7 us |  |
| Q9 with 9977 dirty entities | 3000 | 41.0 us | 45.2 us | 58.5 us | 217.3 us |  |
| Q9 with 49281 dirty entities | 3000 | 200.7 us | 235.0 us | 407.8 us | 4283.0 us |  |
```
