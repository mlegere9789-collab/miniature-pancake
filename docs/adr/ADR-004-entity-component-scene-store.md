# ADR-004: Entity-component-relationship scene store (side table keyed by a stable UUID)

| | |
|---|---|
| Status | Proposed. Spike A is complete (recommendation: adopt with five changes); the decision belongs to the owner. |
| Date | 2026-11-14 (UTC) |
| Plan | Master Plan Rev 2, 5.5 (scene graph row: "5 M entities, memory and query latency"), Appendix B (edit propagation, undo or redo, move 10,000 objects) |
| Evidence | branch `w0-spike-a` @ `b6275607`, `dino8-core/spikes/scene/RESULTS.md` |

## 1. Context

Plan 5.5: an entity store as a side table keyed by a new stable UUID on `SceneObject`, holding geometry, semantic, procedural and presentation components
plus relationships; no big-bang migration, objects gain components as features need them. Verification: 5 M entities, memory and query latency. Appendix B: edit
propagation under 50 ms at 1 M elements, move 10,000 objects under 100 ms feedback, undo or redo under 30 ms (unlimited depth with disk spill), memory within 1.5
times the incumbent for equal content.

## 2. Decision

1. Adopt the Spike A structure for the `d8-store` crate: a dense slot index plus a stable 128-bit UUID map; persistent paged component tables (64-way radix tree, 128-slot leaves with a presence
   bitmask), one generic type for every component, property column and the dirty set; persistent sorted sets (two-level B+tree with `Arc` chunks) for the UUID map, forward and reverse edge sets (edge = 64-bit
   key: kind, source slot, target slot), constraint payloads, secondary indexes and the free-slot set; snapshot by `Arc` bump; a flattened median-split BVH shared between snapshots with a
   persistent dirty overlay so queries stay exact between rebuilds; undo and redo by snapshot swap.
2. Relationship kinds as implemented: hosting and containment (single parent, replace on relink, cycle rejected), connection (undirected, canonical), constraint (directed with type and value payload), reference (directed).
   Delete cascades over hosting; slots are reused through a persistent free set. Slot indices are 30 bits (about 1.07 billion entities). UUIDs are supplied by the caller.
3. Make five changes before production use (Spike A recommendation):
   1. sparse (packed or run-length) leaves for rare components (`procedural`, populated for 10% of entities, costs 12.4 B per entity because every 128-slot leaf is touched; about 50 B per entity could come off the 181 B total);
   2. zone maps (min and max per leaf) on property columns (property range scans are the slowest interactive shape, 35 ms at 5 M);
   3. incremental BVH refit or background rebuild with a dirty threshold (rebuild is 1.5 to 2.1 s at 5 M; rebuild at about 10,000 dirty entities, 41 us per query, while the old BVH keeps serving);
   4. undo history with the disk spill the plan already requires (one snapshot per edit retains about 20 KB per edit at 5 M, so 100,000 undo steps hold about 2 GB);
   5. a mandatory bulk path for imports (incremental `create` plus a containment edge is 30 to 40 us, about 3 minutes for 5 M; `SceneBuilder` loads 5 M in 2.3 s).
4. The store is a side table: `SceneObject` gains a stable UUID; the mapping to the store is a thin binding that is NOT built (open item 1). No big-bang migration.

## 3. Measured evidence

Run conditions: standalone Rust 1.97 crate, release profile (thin LTO), single thread, counting global allocator for memory; machine with 4 cores shared by about 20 agents (load average 4 to 6 during the runs). Times are
therefore pessimistic and noisy (a repeat run varied by up to 40% on scan-type queries); sub-microsecond rows are upper bounds (the `Instant` overhead is about 25 ns). Synthetic model: random placement, 40 IFC-style classes, 3 to 4 properties per element.

Memory at 5 M entities (6.73 M relationship edges): 181.2 bytes per entity (864 MB live heap, peak 1.0 GB during load, RSS 962 MB, VmHWM 1,012 MB) for: UUID (16 B), geometry (bounding box 24 B plus kernel handle 8 B), semantic (12 B), procedural on 10%, presentation (12 B),
four property columns (height on all, cost on 50%, fire rating on 30%, name on 20%), a containment edge for all, other edges on a fraction, two inverted indexes. Breakdown in bytes per entity: UUID by slot 16.4, UUID map 24.4, geometry 32.4, semantic 12.4, procedural 12.4,
presentation 12.4, properties 33.8, edges forward plus reverse 22.4, constraint payload 0.2, indexes 14.1. The BVH adds 60 B per entity (286 MB at 5 M), built on demand. The geometry payload itself is not included (only a handle) and strings are interned ids. Bulk insert of 5 M entities: 2.33 s (467 ns per
entity); 1 M: 0.41 s. Integrity check of the loaded 5 M scene passed (9.3 s).

Query latency (13 shapes; p50 / p99; N = 1 M and 5 M):

| shape | 1 M | 5 M | 5 M result |
|---|---|---|---|
| Q1 UUID to all components | 1.09 / 2.0 us | 1.87 / 2.7 us | 1 entity |
| Q2 class, selective, indexed | 1.4 / 2.6 us | 6.4 / 11 us | 2,537 hits |
| Q3 class, broad, indexed (20%) | 456 / 779 us | 2.3 / 3.3 ms | 997,975 hits |
| Q4 property range scan | 6.4 / 6.7 ms | 35.4 / 36.3 ms | 1,617 hits |
| Q5 class AND property range | 1.9 / 2.3 ms | 10.7 / 19.3 ms | 9,767 hits |
| Q6 children of a storey | 20 / 70 us | 16.9 / 38 us | 4,999 hits |
| Q7 wall neighbourhood | 0.35 / 0.73 us | 0.43 / 1.3 us | 3 hits |
| Q8 all descendants of a building (BFS) | 10.5 / 10.8 ms | 12.4 / 16.3 ms | 250,000 hits |
| Q9 spatial AABB, 6 m window | 4.1 / 9.4 us | 6.7 / 36.8 us | 34 hits |
| Q10 spatial AABB, 30 m window | 39 / 91 us | 50 / 112 us | 2,657 hits |
| Q11 raycast 100 m, all hits sorted | 35 / 109 us | 99 / 373 us | 897 hits |
| Q12 classification code, indexed | 1.9 / 3.0 us | 5.2 / 9.3 us | 1,702 hits |
| Q13 phase/option filter (full scan) | 2.7 / 4.9 ms | 21.9 / 23.9 ms | 12,587 hits |

Spatial queries after edits (dirty overlay, 5 M): 0 dirty 2.2 us p50; 100 dirty 2.9 us; 1,000 dirty 6.6 us; 9,977 dirty 41 us; 49,281 dirty 201 us. BVH rebuild: 1.5 s at 5 M, 0.25 s at 1 M. Outliers (max column, up to a few ms) coincide with the shared machine and first-touch page faults.

Edits, snapshots, undo (1 M / 5 M):

| measurement | 1 M | 5 M |
|---|---|---|
| snapshot (`Scene::clone`) p50 / p99 | 135 / 167 ns | 150 / 263 ns |
| flat deep copy of the same bytes (reference) | 83 ms | 642 ms |
| move a wall plus its hosted and fixed-offset dependents, snapshot kept per edit: p50 / p99 / max | 5.0 us / 14 us / 72 us | 7.1 us / 33 us / 113 us |
| move 10,000 objects as one undoable edit | 14.2 ms | 25.3 ms |
| undo p50 / p99 / max | 46 ns / 444 ns / 19 us | 155 ns / 511 ns / 19.7 us |
| first lookup plus indexed query after undo, p99 | 4.2 us | 18.6 us |
| memory of 1,000 undoable edits (snapshot per edit) | 17.2 MB | 19.2 MB (20 KB per edit) |
| 1,000 mixed edits against one held snapshot | 7.9 MB (4.6% of the scene) | 9.4 MB (1.1%) |

Other edit kinds at 1 M, p99: set property 6.6 us; change IFC class (index update) 54.6 us; add reference edge 13.1 us; create entity plus containment edge 83.4 us (5 M: 137.9 us; the 30 to 40 us above is the p50). The state after undoing all 1,000 edits is verified against a checksum
taken before the edits, and redo restores the final state; the benchmark asserts snapshot immutability.

Tests (all green): 6 unit tests; 13 integration tests (identity, hosting cascade and slot reuse, cycle and single-parent rules, connections and constraints, typed properties and range queries, indexed against scan results, builder against incremental inserts, snapshot isolation and branching, undo and redo,
propagation, spatial overlay against brute force, descendants); 2 proptest properties (model-based, random sequences of up to 160 operations compared with a reference model after every operation, every snapshot checked against the state captured at that time and the relationship integrity check; 300 cases per run,
5,000 once; a deliberately broken `del_edge` was caught and reverted; sorted set against `BTreeSet`); clippy with no warnings. About 3,000 lines of code.

## 4. Gates

| Gate | Verdict | Detail |
|---|---|---|
| Spike A: 5 M entities, memory and query latency | **Measured** | 181.2 B per entity; query table above |
| Edit propagation under 50 ms at 1 M elements | **Met for the store only** | p50 5.0 us, p99 14 us, max 72 us at 1 M (p99 33 us at 5 M); margin about 3,500 times at p99. Excludes kernel regeneration of the edited object, derived-data invalidation (ADR-003) and GPU upload; those will decide the real 50 ms. |
| Move 10,000 objects under 100 ms | **Met** (store) | 14.2 ms at 1 M, 25.3 ms at 5 M, as one undoable edit |
| Undo or redo under 30 ms | **Met for the snapshot swap** (store) | p99 444 ns at 1 M, 511 ns at 5 M; first lookup after undo p99 4.2 us / 18.6 us. Real undo also rebuilds `SceneObject`s and refreshes the viewport: **not measured** |
| Undo with unlimited depth with disk spill | **Not measured** | retention measured: about 20 KB per edit in memory at 5 M; no spill exists |
| Memory within 1.5 times incumbent for equal content | **Not measurable** | no incumbent numbers in the repo; 181 B per entity excludes geometry payload and large property sets |
| Multi-threaded readers | **Not measured** | snapshots are `Send` and `Sync` by construction (`Arc` trees) |
| FFI / binding to the existing C++ `SceneObject` | **Not built** | |
| IFC 4.3 attribute mapping | **Not covered** | only class, type and classification ids |
| Text and blob payloads, disk spill | **Not covered** | property strings are interned ids |

The store-level gates pass by three to four orders of magnitude, but they measure the store only; they say nothing yet about the budget the real edit must meet (ADR-003).

## 5. Consequences

- Snapshot by `Arc` bump (150 ns at 5 M against 642 ms for a flat copy) makes per-edit undo snapshots, branching and read-while-edit cheap; retention, not structure, limits undo depth.
- Real models carry larger property sets, strings and longer relationship chains than the synthetic one; the 181 B per entity figure will rise, the shape of the result (O(1) snapshots, page-sized copy on write) should not.
- Imports must use the bulk builder; per-entity inserts are 30 to 40 us.
- Property range scans need zone maps before they are an interactive path at 5 M.
- The store is a second persistent structure beside the operation log's Merkle HAMT (ADR-002); the relationship must be decided (open item 2).

## 6. Risks

- Synthetic data; 181 B per entity is workload dependent.
- Binding to the C++ `SceneObject` (stable UUID on every object, slot allocation, deletion and reuse) is unbuilt and touches the largest legacy structure; objects that are created or mutated outside the command dispatcher (ADR-002) will diverge from the store.
- BVH rebuild stalls (1.5 to 2.1 s at 5 M) if rebuilt synchronously.
- Timings were taken on a loaded machine with a counting allocator; plan targets are for reference hardware measured by the owner.
- 30-bit slot limit (about 1.07 billion entities) is ample but is an ABI-visible constant.

## 7. Open items

1. The binding from `SceneObject` to the store: stable UUID field, creation path, deletion, slot reuse.
2. One persistent structure or two: relationship of the store's paged tables and snapshot swap to the operation log's HAMT and root-hash swap (ADR-002).
3. The five changes of section 2.3.
4. Multi-threaded reader benchmark; disk spill; blob and text payloads.
5. IFC 4.3 attribute mapping (Spike L, not delivered here).
6. Memory comparison with the incumbent on equal content, measured by the owner.
7. UUID generation policy (the store takes caller-supplied UUIDs; `Uuid::from_random_bits` makes a v4-style id).

## 8. What Wave 1 must build first

1. `d8-store` crate from the spike's `ptable.rs`, `sorted.rs`, `scene.rs`, `query.rs`, `bvh.rs`, `history.rs`, with the bulk builder as the only import path and the sparse-leaf and zone-map changes (2.3.1 and 2.3.2) done before the 5 M benchmark is re-run as a CI performance gate.
2. The `SceneObject` UUID binding and a read-mostly mirror of geometry bounding boxes and class ids into the store, with an integrity check against the legacy `Document` after every command in the test suite.
3. The FFI surface of the store under ADR-001 rules (open, snapshot, query by UUID, class, box, ray, relationships; coarse calls only), so the C++ side can use the BVH and indexed queries for selection and snapping.
4. Decision on open item 2 before Stage 4 of the operation log.
