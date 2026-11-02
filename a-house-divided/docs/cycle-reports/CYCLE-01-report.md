# Cycle 01 report (foundation and first determinism proof)

**Delivered**
- Workspace with pinned toolchain and path-filtered CI (static gates, Rust gates, determinism matrix on Linux, Windows and macOS).
- `core-math`: fixed-point Q32.32, integer-only sin/cos/atan2 tables, vector math, SplitMix64 and xoshiro256** named streams.
- `sim-state`: agent store, spatial grid, canonical state hash, replay format v1 with a hardened reader.
- `hd-sim`: toy scenario, `run`, `verify`, `hash`, `bench`; three golden replays verified in CI on all three operating systems.
- Data layer: schemas, validator (provenance, sums, ranges, regression fixtures), 5 battles and 30 commanders, none marked verified.
- Gates: forbidden-terms scan, no-process-text lint.

**Measured**
- 20,000-agent toy tick: mean about 11.8 ms, p99 about 17 ms (single thread, 4 vCPU), against a 50 ms budget at 20 Hz. See CYCLE-00-baseline.md.
- Tests: 187 pass in debug; the same suite passes in release.

**Not yet done (next cycle)**
- Rules content: formations, ballistics (F0) from weapon tables, morale, command messages.
- Renderer prototype (needs a GPU machine).
- Counsel engagement and patent landscape (human lane); the charter is a draft.
- Extraction pass 2 and second-source verification of flagged records.
- An independent reviewer pass over the three crates and their tests (tests were written by the same agent that implemented each crate).

**Usage:** see `ledger/usage.csv`.
