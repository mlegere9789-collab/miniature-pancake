# SPEC-002: sim-state (agent store, spatial index, state hash, replay format)

Crate: `crates/sim-state`, depends only on `core-math` (path) and `proptest` (dev). `#![forbid(unsafe_code)]`.
It contains no game rules. It provides the deterministic data foundations that rules will run on.

## 1. Agent store (structure of arrays)
- `AgentId { index: u32, generation: u32 }`. Slots are reused through a LIFO free list so allocation order is deterministic.
- Fields per agent: `pos: Vec2Fx`, `vel: Vec2Fx`, `facing: Angle`, `hp: u16`, `flags: u16`, `team: u8`, `unit: u32`, `alive: bool`.
- API: `spawn(AgentInit) -> AgentId`, `despawn(AgentId) -> bool`, `is_alive`, getters and setters, `len()` (alive count), `capacity()`,
  `iter_alive()` in ascending slot order, `ids_alive() -> Vec<AgentId>`. Stale ids (wrong generation) are rejected, never panic.
- No `HashMap`/`HashSet` anywhere in authoritative state. Iteration order is always defined by slot index.

## 2. Spatial grid
- `SpatialGrid::new(cell_size: Fx, width_cells: u32, height_cells: u32, origin: Vec2Fx)`.
- `rebuild(&AgentStore)` places every alive agent in a cell (clamped to the grid) using counting-sort buckets; result independent of insertion history.
- `query_radius(center: Vec2Fx, radius: Fx, out: &mut Vec<AgentId>)` returns every alive agent with `distance_sq <= radius^2`, **sorted by `(distance_sq, index)`**.
- `nearest(center, radius) -> Option<AgentId>` with the same tie-break. `cells_touched` counter exposed for tests.
- Must handle 20,000 agents; rebuild is O(n). Include a criterion-free micro benchmark as an ignored test that prints timing.

## 3. World state and hashing
- `WorldState { tick: u64, agents: AgentStore, rng: RngStreams (master seed) }` plus `advance_tick()`.
- Canonical byte encoding for hashing: little-endian, fixed-width fields, agents in ascending slot order, free-list order included, the `tick`, and the RNG master seed.
  Document the exact layout in the module docs.
- `state_hash(&WorldState) -> u64` is **FNV-1a 64** over the canonical bytes. (Not cryptographic; its job is desync detection.)
- `StateHasher` incremental API with the same result as the one-shot function.

## 4. Replay format v1 (binary, little-endian, versioned)
```
magic        4 bytes  "HDRP"
version      u16      = 1
header       scenario_id (u16 length + UTF-8), seed u64, tick_hz u16, fidelity_class u8 (0,1,2), checkpoint_interval u32
inputs       u32 count, then each: tick u64, actor u8, seq u16, payload (u32 length + bytes); sorted by (tick, actor, seq)
checkpoints  u32 count, then each: tick u64, hash u64
footer       total_ticks u64, final_hash u64, body_hash u64 (FNV-1a 64 of every preceding byte)
```
- `ReplayWriter` (builder) and `ReplayReader::parse(&[u8]) -> Result<Replay, ReplayError>`; `Replay::to_bytes()`.
- The reader rejects: bad magic, unknown version, truncated data, trailing bytes, unsorted inputs, body-hash mismatch, invalid fidelity class,
  declared lengths larger than the remaining data (no allocation bombs). It never panics on any input.
- `ReplayError` is a descriptive enum.

## 5. Tests (written first where possible; not to be weakened)
1. Golden: a fixed `WorldState` built from fixed spawns hashes to a checked-in literal. Changing any layout detail must break it.
2. Slot reuse: spawn/despawn sequences produce the same ids and iteration order every run.
3. Grid: results equal a brute-force reference (sorted the same way) on randomized worlds (proptest), including agents on cell borders and outside the grid.
4. Replay round-trip (proptest): `parse(to_bytes(r)) == r`; every single-byte mutation or truncation of a valid replay is rejected without panic
   (exhaustive for a small replay, sampled for large ones).
5. Hash independence: the hash does not depend on the order in which equivalent spawns were interleaved with despawns of other slots only when the final
   slot layout is identical; and it **does** change when any single field of any agent changes.

## 6. Done when
fmt, clippy `-D warnings`, `cargo test --workspace` pass; no `unsafe`; no floating point; docs on every public item; the byte layout documented.

## Amendment A1 (from independent review, cycle 01)
1. The replay reader additionally rejects: duplicate `(tick, actor, seq)` input keys (keys must be strictly increasing); checkpoints that are not strictly increasing
   in tick; any checkpoint tick greater than `total_ticks`; `tick_hz == 0`; `checkpoint_interval == 0` when `total_ticks > 0`. The footer body hash is verified
   before structural parsing beyond the fixed-size prefix, so corruption reports `BodyHashMismatch` before any structural error. Pre-allocation is bounded by the bytes actually remaining
   (at most one record per minimum record size), never by a declared count.
2. `SpatialGrid::new` returns `Result<SpatialGrid, GridError>`: it rejects zero dimensions, non-positive cell size, `width * height` overflow (checked) and more than 2^26 cells.
   (The earlier clamp-to-minimum behaviour is removed.) Query results are a snapshot of the last `rebuild`; this is documented on `rebuild`, `query_radius` and `nearest`,
   and `AgentStore`-aware helpers `query_radius_alive` / `nearest_alive` that also filter ids no longer alive are added.
3. Golden files and hash layout are unchanged: no golden hash may change in this amendment.
