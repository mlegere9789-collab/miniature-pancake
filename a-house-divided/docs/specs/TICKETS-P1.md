# TICKETS-P1: battle layer, tickets T-014 to T-024

Specs: SPEC-004 (movement and formations), SPEC-005 (weapons and fire). One ticket, one session, under about 400 changed lines (code, tests, data together). Each ticket writes its tests first, ends with the Q0 commands from `CLAUDE.md`, and appends one row to `ledger/usage.csv`. A ticket that cannot fit splits at the data/code boundary; it never drops or weakens a listed test. Order below is dependency order; a ticket starts only when everything in `Needs` is merged.

| Ticket | Title | Needs |
|---|---|---|
| T-014 | Grid segment query (SPEC-002 amendment A1) | none |
| T-015 | battle-rules crate, integer-only data loader, provenance schema | none |
| T-016 | Locomotion and terrain tables, fatigue model | T-015 |
| T-017 | Drill table, slot layout, slot assignment | T-015 |
| T-018 | BattleState, regiments, orders, drill state machine, hash | T-016, T-017 |
| T-019 | Steering, avoidance, anchor movement, movement scenarios | T-018 |
| T-020 | Ammo, weapon and damage tables | T-015 |
| T-021 | Projectile flight, range tables, Gaussian helper | T-020 |
| T-022 | Fire phase: aim, loading, misfire, fouling, ammunition, smoke source | T-019, T-021 |
| T-023 | Hit test, obstacles, cover, casualties, bursts, tick order, F0 counters | T-014, T-022 |
| T-024 | Calibration harness, bands, goldens, performance report | T-023 |

## T-014: Grid segment query
- Spec: SPEC-005 amendment A1 (adds `SpatialGrid::query_segment`; amend SPEC-002 section 2 in the same change, amending the spec first).
- Work: `crates/sim-state/src/grid.rs` method visiting only cells that touch the capsule, result sorted by `(s, index)`.
- Acceptance: **T5-07** (equals brute force, proptest, cell-border and outside-grid agents, zero-length segment, segment entirely outside the grid); existing SPEC-002 tests unchanged and green; an ignored micro-benchmark prints the time for 1,000 segments of 15 m in a 20,000-agent world.

## T-015: battle-rules crate and loader
- Spec: SPEC-004 sections 1, 2 (and the loader part of 11).
- Work: crate skeleton and workspace entry; `fx_ratio`; `DataError`; a shared integer-only TOML reader (rejects floats and unknown keys, enforces provenance); `data/schemas/rules-common.schema.json`; extend `tools/validate_data.py` to validate `data/rules/*.toml` against its schema when present; record ADR 0004 (rules data are integer-only TOML, the `toml` and `serde` dependencies are exact-pinned, loader only).
- Acceptance: **T4-02**; the loader half of **T4-01** (float, unknown-key, placeholder-without-target and non-placeholder-without-sources cases return the named `DataError`) using small in-test TOML strings.

## T-016: Locomotion, terrain, fatigue
- Spec: SPEC-004 sections 3, 4, 5.
- Work: `data/rules/locomotion.toml`, `terrain.toml`, schemas, `LocomotionTable`, `TerrainTable`, `TerrainGrid::{from_rows, class_at}`, `fatigue_speed_pm`, the integer fatigue update function.
- Acceptance: **T4-01** for these files (Rust loader and `validate_data.py`); **T4-05** (CT-2 targets within 20 percent, bounds, curve breakpoints, monotonicity); terrain clamp and blocking unit tests (positions outside the grid take the nearest edge cell).

## T-017: Drill table, slots, assignment
- Spec: SPEC-004 sections 6, 8.
- Work: `data/rules/drill.toml` and schema, `DrillTable`, `transition_ticks`, `slot_layout`, `assign_slots`, roster helper (ascending index).
- Acceptance: **T4-09** (permutation, input-order invariance by proptest, rank monotonic in `u`, 400-man line width `200 * 560 mm`); the `transition_ticks` half of **T4-07** (all six pairs by three training levels, integer ceiling); **T4-01** for `drill.toml`.

## T-018: BattleState and the drill state machine
- Spec: SPEC-004 sections 7, 11 (not the steering of section 9).
- Work: `BattleState`, `spawn_regiment`, `Regiment`, `RegimentState`, `Order` plus `encode_order`/`decode_order`, order handling and the `Forming` countdown, close-up on `dirty`, `can_fire`, `LocoExt`, `battle_hash` with the three reserved zero blocks, flag constants, stream constants.
- Acceptance: state timing half of **T4-07** (ticks in `Forming` equal `transition_ticks`; `can_fire` false in `Column` and `Forming`; pending order starts at the end of the transition); **T4-10** (close-up to 360 slots with distinct assignments); order codec round trip (proptest); a hash golden for a freshly spawned 400-man regiment recorded once, and a test that changing any listed field changes the hash.

## T-019: Steering, avoidance, anchor, movement scenarios
- Spec: SPEC-004 sections 7 (anchor), 9, 12, 13.
- Work: `step_movement` phases A and B, `desired_velocity`, terrain speed and fatigue use, catch-up gait, `scenarios::{march_single, deploy_pair, terrain_strip, march_large}`, `qc/fixtures/ref_march.py` and its committed output, `hd-sim battle --scenario NAME --ticks N --seed S` (and checkpoint replay writing with fidelity class 0), golden replays and `expected.json` rows.
- Acceptance: **T4-03**, **T4-04**, **T4-06**, **T4-08**, **T4-11**, **T4-12**, ignored **T4-13** with the measured numbers written into `docs/cycle-reports/CYCLE-02-movement.md`. If this overruns 400 lines, move the `hd-sim battle` subcommand and goldens to a follow-up ticket inserted before T-022.

## T-020: Ammo, weapon and damage tables
- Spec: SPEC-005 section 1.
- Work: `data/rules/ammo.toml`, `weapons.toml`, `damage.toml` with every row's provenance (page and tag, or placeholder with CT-n), three schemas, `AmmoTable`, `WeaponTable`, `DamageTable`, derived constants (drag `k` at load, canister pellet mass from iron density), `energy_joules`.
- Acceptance: **T5-01**, **T5-02**, **T5-03**; the cumulative-permille lookup unit tests for the damage table (no row sums above 1000).

## T-021: Flight and range tables
- Spec: SPEC-005 sections 2 (without bursts), 3 (range table, `gauss_unit`, `angle_from_urad`).
- Work: `Vec3Fx`, `Projectile`, `flight::step_projectile` (drag, gravity, wind, ground contact and ricochet), `RangeTable`, `gauss_unit`, `angle_from_urad`, `qc/fixtures/ref_flight.py` with committed output.
- Acceptance: **T5-04** (vacuum closed form), **T5-05** (independent RK4 reference, 0.5 percent), **T5-06**; unit tests of `gauss_unit` mean and standard deviation over 1,000,000 draws (mean within 0.01, deviation within 0.02 of 1).

## T-022: Fire phase
- Spec: SPEC-005 sections 3 (dispersion), 4, 8.
- Work: `FireExt` with per-soldier streams, `equip_regiment`, `set_fire_mode`, loading state machine with training multipliers, misfire and fouling, ammunition and `resupply`, aim and dispersion, projectile spawn with ascending `seq`, `SmokeGrid` source and decay, the fire and smoke parts of `battle_hash`.
- Acceptance: **T5-11**, **T5-12**, **T5-13**, **T5-14**, **T5-17**; a spawn test that checks muzzle position, launch vector magnitude and that the draw count per shot is fixed (50) for hit, misfire and out-of-range cases.

## T-023: Hit, casualties, bursts, tick order
- Spec: SPEC-005 sections 2 (bursts), 5, 6, 7.
- Work: `ObstacleLayer`, chord resolution, posture and cover, damage rolls and pierce, burst spawning with `SplitMix64` children, casualty queue and application, release of expired disables, the full `BattleState::step` order, F0 counters, casualty log hashing.
- Acceptance: **T5-08**, **T5-09**, **T5-10**, **T5-15**, **T5-16**, **T5-18**; hash test that two builds of the same scenario with projectiles resolved in reverse order (test hook) agree.

## T-024: Calibration harness and the F0 reference
- Spec: SPEC-005 sections 9, 11 (T5-19 to T5-21), 12.
- Work: `calibration::{run_cal_line, run_band_suite}`, scenarios `cal_line`, `cal_rof`, `cal_range`, `cal_canister`, `qc/calibration/bands.toml` (B1 to B11 with their tags), `qc/calibration/f0-curves.json`, `f0_large` scenario, golden replays and `expected.json` rows, the ignored performance test and `docs/cycle-reports/CYCLE-03-f0.md` with measured mean, p99 and max against H4 and H5.
- Acceptance: **T5-19**, **T5-20**, **T5-21**. Tuning is done on the placeholder data rows (dispersion, cover, damage, aim) and never on a band; a band changes only through a SPEC-005 amendment citing evidence.
