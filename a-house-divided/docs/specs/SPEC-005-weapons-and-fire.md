# SPEC-005: battle-rules II, weapons, flight, hits, casualties and the F0 rule

Crate: `crates/battle-rules` (created by SPEC-004; adds modules `weapons`, `flight`, `aim`, `fire`, `hit`, `damage`, `smoke`, `calibration`). Same rules, data units and provenance as SPEC-004 sections 1 and 2.
Source key: **CWV pNNN** = "The American Civil War: Research Volume and Game Design", PDF page NNN, with the tag the page gives. 1 yd = 914.4 mm, 1 ft = 304.8 mm, 1 lb = 453.592 g. Invented numbers are `placeholder` with a calibration target CT-n (section 12).
**Amendment A1 to SPEC-002:** `SpatialGrid::query_segment(p0, p1, half_width, out: &mut Vec<AgentId>)` returns every alive agent within `half_width` of segment `p0..p1`, sorted by `(s, index)`, `s` being the raw `Fx` projection of `pos - p0` on the segment clamped to `[0, |p1-p0|]`; it equals a brute-force reference.

## 1. Tables (`data/rules/ammo.toml`, `weapons.toml`, `damage.toml`; matching `*.schema.json`)
Ammo row: `id, kind (ball|conical|shot|shell|case|canister), mass_g, diameter_mm, cd_pm (drag coefficient x1000), muzzle_mm_s`, plus `restitution_pm` (shot) or `burst {count, pellet_mass_g, pellet_diameter_mm, pellet_cd_pm, shape (cone|sphere), spread_sigma_urad, pellet_speed_pm, fuse_sigma_substeps, dud_ppm}`.

| id | kind | mass_g | dia_mm | cd_pm | muzzle_mm_s | basis |
|---|---|---:|---:|---:|---:|---|
| ball_69 | ball | 27 | 17 | 470 | 300000 | calibre .69 (CWV p459, V S21); rest placeholder CT-1 |
| minie_58 | conical | 32 | 15 | 300 | 290000 | calibre .58 (CWV p459, V S21); rest placeholder CT-1 |
| carbine_52 | conical | 29 | 13 | 300 | 250000 | placeholder CT-1 |
| revolver_44 | ball | 9 | 11 | 400 | 230000 | placeholder CT-1 |
| shot_12 | shot | 5443 | 117 | 470 | 452600 | 1485 ft/s (CWV p459, V S19); 12-pounder (CWV p030); diameter, Cd placeholder CT-2 |
| shell_12 | shell | 5579 | 117 | 470 | 400000 | 12.3 lb (CWV p459, V S19); velocity placeholder CT-2 |
| case_12 | case | 5400 | 117 | 470 | 400000 | placeholder CT-2 |
| canister_12 | canister | 5400 | 117 | 470 | 400000 | 27 iron balls of 1.5 in (CWV p459, V S20); rest placeholder CT-2 |

Bursts (placeholder CT-2 except counts and size): canister 27 pellets of 38 mm, mass derived at load as iron 7.2 g/cm3 times sphere volume (208 g), cone, `spread_sigma_urad 60000`, `pellet_speed_pm 850`, fuse 0; case 76 pellets of 17 mm, cone 25000, `fuse_sigma_substeps 15`, `dud_ppm 30000`; shell 40 fragments of 60 g, sphere, 150 m/s, same fuse and dud. Shot `restitution_pm 400`.

Weapon row: `id, class (smoothbore|rifled|carbine|revolver|field_gun), ammo[], magazine, load_steps[{id,ticks}], cycle_ticks, misfire_restart_step, sigma0_urad, sigma_slope_urad_per_100m, aim_scale_pm, powder_mg, misfire_ppm, misfire_fouling_ppm, fouling_per_shot, ram_penalty_pm, fire_drain_ppm, effective_mm, extended_mm, reach_mm, rounds`.

| id | steps (ticks, Trained) | sum | sigma0 / slope | effective / extended mm | basis |
|---|---|---:|---|---|---|
| smoothbore_69 | handle 120, seat 60, ram 140, prime 100, ready 60 | 480 | 6000 / 4000 | 45720 / 182880 | 50 and 200 yd (CWV p459, V S21); rest placeholder CT-3 |
| rifle_58 | handle 120, seat 60, ram 160, prime 100, ready 60 | 500 | 700 / 250 | 274320 / 804672 | 300 yd and half a mile (CWV p459, V S21); steps CT-4 |
| carbine_52 | open 40, insert 60, close 30, prime 20 | 150 | 1000 / 300 | 183000 / 500000 | placeholder CT-3, CT-4; breechloaders rare (CWV p510 4.2) |
| revolver_44 | magazine 6, cycle 20; reload 300, 600, 300 | 1200 | 5000 / 3000 | 25000 / 80000 | placeholder CT-3, CT-4 |
| napoleon_12 | sponge 200, load 300, ram 200, prime 200, lay 300 | 1200 | 1500 / 400 | 320040 / 1536192 | 1 round/min (CWV p453, p459, V S19); canister effective 350 yd, shot max 1680 yd (CWV p459, V S19); splits CT-4 |

Common placeholders (CT-3): `aim_scale_pm` 1000 (revolver 1500, gun 150); `powder_mg` 3900 (revolver 1000; gun 907000, the "2.0 lb charge", CWV p459, V S19, partial); `misfire_ppm` 20000 (gun 10000); `misfire_fouling_ppm` 30000; `fouling_per_shot` 12; `ram_penalty_pm` 500; `fire_drain_ppm` 2000; `reach_mm = 2 * extended_mm`; `rounds` 60 (carbine 40, revolver 18; gun shot 20, shell 20, case 10, canister 6). Load speed by training `load_pm`: raw 1200, trained 1000, veteran 840; step ticks `ceil(ticks * load_pm / 1000)`. Rate of fire: trained infantry 2 to 3 rounds/min (CWV p453, `[Memory/Estimate]`), 2.5 planning figure (CWV p459); the table gives 2.08 raw, 2.5 trained, 2.98 veteran.
Postures (`damage.toml`, placeholder CT-5), width / height / muzzle height / `cover_pm` in mm and permille: standing 500/1700/1450/500, kneeling 500/1200/1000/800, prone 500/450/300/1000; a gun (`F_GUN`) 1400/1300. Hit height fraction: legs below 500 permille, torso below 850, head above. Energy bands (J): B0 below 150, B1 150-499, B2 500-1499, B3 1500 and up. Outcome permille `killed/wounded/disabled`, remainder no effect. Derivation: lethality rises with energy and with torso or head hits, legs are rarely fatal, spent balls mostly bruise (placeholder CT-6):

| band | legs | torso | head |
|---|---|---|---|
| B0 | 0/50/200 | 20/150/300 | 150/350/300 |
| B1 | 10/500/300 | 150/600/200 | 500/350/100 |
| B2 | 50/800/100 | 300/650/40 | 800/180/10 |
| B3 | 300/650/40 | 650/330/10 | 950/50/0 |

`disabled_ticks = 600 + roll % 1801`. `absorb_j` per body hit: ball, conical, pellet 600; shot 40000. Constants: `air_density_g_m3 = 1225`, `g = fx_ratio(980665, 100000)`.

## 2. Flight (`flight`)
`Projectile { seq u64, owner u32, unit u32, team u8, ammo u8, pos Vec2Fx, z Fx, vel Vec2Fx, vz Fx, travelled_mm u32, bounces u8, hits u8, fuse i32 (substeps, -1 none), seed u64 }`; `Vec3Fx` (add, scale, dot, length_sq, length) lives in this crate. The pool is a `Vec` in ascending `seq`.
Per ammo at load, in `i128`: `k = rho * cd * A / (2 m)`, `A = pi d^2 / 4`.
`DT = Fx::ONE / Fx::from_int(200)` (raw 21474836), 10 sub-steps per tick. Each sub-step (semi-implicit Euler): `rel = v - w` (`w` the scenario wind, m/s), `v += (-k |rel| rel - (0,0,g)) * DT`, `p += v * DT`.
Ground (`z <= 0`): `shot` above 30 m/s with under 3 bounces: `z = 0`, `vz = -vz * restitution_pm * ground_pm / 10^6`, horizontal speed times 0.8, and flight for this tick stops (`ground_pm` 1000 dry, 500 wet; wet ground reduces ricochet, CWV p459 design). Other kinds end. A projectile also ends at `travelled_mm >= reach_mm` or speed below 40 m/s.
Bursts: when `fuse` reaches 0, or a shell or case strikes the ground, the parent ends and `count` children are appended (they fly from the next tick). Cone: parent direction rotated by `N(0, spread_sigma)` in azimuth and elevation, speed `pellet_speed_pm` of the parent. Sphere: azimuth uniform over a turn, elevation uniform within 45 degrees, fixed speed. Child draws come from `SplitMix64::new(parent.seed)`. A `dud_ppm` roll makes the round an inert shot. Fuse error, airburst cones and canister density falling with range follow CWV p459 (design).
`gauss_unit(&mut Rng) -> Fx` is the sum of twelve `unit_fx()` minus 6.

## 3. Aiming and dispersion (`aim`)
`RangeTable` per ammo: entries every 25 m to `reach_mm`, built at load by 24-step bisection of elevation in `[0, 15 deg]` with the section 2 integrator (no wind) so the projectile first reaches horizontal range `r` at aim height 1200 mm; stores `(Angle, time_of_flight_substeps)`, interpolated linearly.
Per shot the soldier's stream is read in this **fixed order whatever the outcome**: one `u32` misfire roll, `gauss` for range, azimuth, elevation, fuse, one `u64` projectile seed. Range estimate `r_est = max(0, r * (1000 + range_sigma_pm * g) / 1000)` with `range_sigma_pm` raw 250, trained 150, veteran 100 (placeholder CT-7; range estimation is hard, CWV p030 1.4). Elevation `table(r_est) + aim_high_urad` (raw 4000, trained 2000, veteran 1000, placeholder CT-7; aiming too high, CWV p459, V S21). Fuse (shell, case) `tof(r_est) + fuse_sigma_substeps * g`; canister bursts at the muzzle.
`sigma_b = sigma0 + slope * r / 100000` (urad, r in mm); `sigma_s = aim_sigma * aim_scale_pm/1000 * (1000 + 600 * fatigue/10^6)/1000 * (1000 + 4 * smoke_at_shooter)/1000`, `aim_sigma` raw 12000, trained 6000, veteran 4000 (placeholder CT-7); `sigma = sqrt(sigma_b^2 + sigma_s^2)` by `Fx::sqrt`. Azimuth and elevation errors are `sigma * gauss`, converted by `angle_from_urad(u)` = raw `u * 2^32 / 6283185`.

## 4. Loading, firing, fouling, ammunition (`fire`)
`FireExt` per slot: `rounds u16, ammo_sel u8, step u8, left u16, fouling u16, mag u8, disabled_until u64, rng [u64;4]`. Stream `STREAM_SOLDIER_BASE (0x4000_0000) + slot index` is derived at spawn with `RngStreams::stream`; state hashed. Initial `left` gets `range_u32(0, step ticks)` from the stream so regiments do not fire in lockstep.
Each tick a soldier with rounds does `left -= 1`; at 0 it advances a step (the `ram` step is multiplied by `(1000 + ram_penalty_pm * fouling/1000)/1000`); after the last step it is **Ready**. Ready fires when the regiment `can_fire`, its `FireMode` is `AtWill { target: RegimentId }`, the soldier is not down, and the target is within `extended_mm`. Target: `target_roster[j % len]`, `j` the shooter's roster index (spread fire; tactical targeting is out of scope). A shot costs a round, adds `fouling_per_shot` (cap 1000) and `fire_drain_ppm` fatigue, and restarts loading (revolver: `mag -= 1`, wait `cycle_ticks`, reload when empty). Misfire when `roll % 10^6 < (misfire_ppm + misfire_fouling_ppm * fouling/1000) * wet_pm/1000` (`wet_pm` scenario value, 1000 dry; wet powder, CWV p510 4.2): round spent, no projectile, loading restarts at `misfire_restart_step`. Muzzle at `pos + forward * 600 mm`, height by posture. `resupply(regiment, n)` adds rounds. `set_fire_mode` has order payload tag 5.

## 5. Hit test and line of fire (`hit`)
Each tick a projectile flies 10 sub-steps giving a chord `p0..p1`, `z0..z1`, cut at the first ground or burst event. Everything reads the **frozen start-of-tick snapshot**. Events on the chord, `s` its fraction:
1. **Obstacles** (`ObstacleLayer`, `Vec<Obstacle {a, b: Vec2Fx, height_mm: u16}>`): 2D `i128` cross-product intersection; stops the projectile if `z(s) <= height`. Walls and earthworks are the research's prepared positions (CWV p065, p511 4.4); heights are scenario data.
2. **Soldiers**: `query_segment(p0, p1, 700 mm)`; skip the owner, down soldiers (`F_DISABLED`, `F_WOUNDED`) and own-unit soldiers while `travelled_mm < 2500` (placeholder CT-5); hit if horizontal distance to the chord is within half-width and `0 <= z(s) <= height`.
Earliest event wins, obstacle first on ties. A soldier hit rolls cover from the projectile's `SplitMix64(seed)` (draw index = `hits`): absorbed if `roll % 1000 < terrain.cover_pm * posture.cover_pm / 1000`. Otherwise `E = mass_g * speed^2 / 2000` J, band, region, outcome roll `% 10^6` against cumulative permille times 1000, and a further draw for `disabled_ticks`; queue `(target index, severity, disabled_ticks, seq)`. If `E - absorb_j >= 150` J and `hits < 8` the projectile continues with `speed * sqrt(E'/E)`; else it ends.

## 6. Casualties and simultaneous shots (`damage`)
Projectiles never change agents in flight, so resolution is a pure function of the snapshot and independent of projectile order. At tick end, queued hits are grouped by target ascending; per target the outcome is the most severe (Killed, Wounded, Disabled), ties to the lowest `seq`, `disabled_ticks` the maximum. Killed: log then `despawn`. Wounded: `F_WOUNDED`, `hp = 25`. Disabled: `F_DISABLED`, `hp = 60`, `disabled_until = tick + n` (released at tick start). Casualties leave the roster (SPEC-004 close-up). `Casualty {tick, target_index, unit, team, kind, shooter_unit, ammo}` is appended in ascending target index (persistent, CWV p512 4.9). A soldier hit on tick t still fires on tick t.

## 7. Tick order and the F0 exact rule
`BattleState::step`: (1) movement (SPEC-004); (2) rebuild grid, release expired disables; (3) fire phase, ascending shooter slot, spawning ascending `seq` and adding smoke; (4) flight and hit phase, ascending `seq`, children join the tail; (5) apply casualties; (6) smoke decay, roster updates, `advance_tick`.
**F0 (ADR 0002):** in a class-0 scenario every shot that satisfies section 4 is a pooled projectile integrated by section 2 for its whole life and tested against individual soldiers by section 5; no hit chance is drawn per unit or per volley; random draws come only from the soldier stream (fire) and the projectile seed (impact), in counts independent of distance, visibility, load or machine speed; nothing is culled, merged or skipped for load. If the budget in section 12 cannot be met, the scenario shrinks, never the rule. Counters `shots_fired`, `misfires`, `projectiles_spawned`, `unit_level_rolls` (must stay 0) are exposed.

## 8. Smoke source (`smoke`)
`SmokeGrid {cell_mm, width, height, origin, density: Vec<u32>}`. Each shot adds `powder_mg * SMOKE_UNITS_PER_G / 1000` (integer division; misfire a quarter) to the muzzle cell (`SMOKE_UNITS_PER_G = 1`, placeholder CT-8; smoke as a density field, CWV p511 4.3). Each tick `d -= ceil(d * 5000 / 10^6)` (placeholder CT-8). Wind transport and rendering are out of scope.

## 9. Calibration harness (`calibration`, bands in `qc/calibration/bands.toml`)
`cal_line(weapon, training, range_mm, wall)`: two regiments of 200 (100 files, 2 ranks), `Steady(Line)`, facing, `range_mm` apart, both `AtWill`, 60 rounds each, 2400 ticks, seeds 1 to 8; `wall` adds a 1000 mm obstacle in front of side B. `cal_rof`: one regiment firing at a distant wall. `cal_range`: single-shot range checks. `cal_canister(range_mm)`: one gun at a 100-man standing line, 200 rounds per range.
Metrics: casualties (killed, wounded, disabled) per 100 men; killed share; rounds per soldier per minute; range. A band holds if the mean over seeds 1 to 8 is inside it; min and max are printed. Placeholder bands change only by spec amendment citing calibration evidence, before the first playable-battle milestone, then freeze.

| band | quantity | at | low | high | tag and source |
|---|---|---|---:|---:|---|
| B1 | rifle casualties/100 | 46/91/137/183/274/366 m | 20/15/10/6/2/0.5 | 60/50/40/30/15/8 | unverified placeholder CT-9; most firefights under 150 yd (CWV p030, partial) |
| B2 | smoothbore casualties/100 | 46/91/137/183 m | 8/2/0/0 | 40/15/5/2 | unverified placeholder CT-9 |
| B3 | rifle / smoothbore | 183 m | 3 | none | partial: "much higher lethality" (CWV p510 4.2) |
| B4 | casualties non-increasing in range | B1, B2 ranges | n/a | n/a | partial, same page |
| B5 | smoothbore rounds/soldier/min | trained/raw/veteran | 2.3/1.9/2.8 | 2.7/2.2/3.2 | partial: 2 to 3 (CWV p453, Memory/Estimate) |
| B6 | gun rounds/min | trained | 0.9 | 1.1 | partial: nominal 1 (CWV p459, V S19) |
| B7 | range to first ground contact | shot 5 deg / shell 3.75 deg / case 3.75 deg | 1382/1070/934 m | 1690/1308/1142 m | partial: 1680 (alt. 1619) / 1300 / 1135 yd, +-10 percent (CWV p459, V S19) |
| B8 | canister hits per round | 183/320/366/457 m | 3/0/0/0 | 12 / 0.5x / 0.3x / 0.1x of the 183 m value | order partial: effective 350 yd, peak 200, max 400 yd (CWV p459, V S19, S20); values unverified CT-9 |
| B9 | wall: attacker / defender casualties | 91 m | 2.0 | none | partial: "2-to-1 or more" (CWV p065 9.3; p030 1.4) |
| B10 | killed share of killed+wounded | all runs | 0.15 | 0.30 | unverified placeholder CT-6 |
| B11 | friendly hits / hits | all runs | 0 | 0.02 | unverified placeholder CT-5 |

Curves go to `qc/calibration/f0-curves.json`, the reference for ADR 0002's F1/F2 tolerance tests.

## 10. Public API (exact)
`Vec3Fx`; `AmmoTable`, `WeaponTable`, `DamageTable` (`from_toml_str`; `Tables` gains them); `AmmoKind`, `WeaponClass`, `Posture`, `Projectile`, `ObstacleLayer`, `Obstacle`, `SmokeGrid`, `FireExt`, `FireMode`, `Casualty`, `CasualtyKind`, `Weather {wind, wet_pm, ground_pm}`; `gauss_unit`, `angle_from_urad`, `energy_joules`, `RangeTable::{build, elevation, time_of_flight}`, `flight::step_projectile`, `hit::resolve_chord`, `BattleState::{equip_regiment, set_fire_mode, resupply, step, casualties, counters, smoke}`, `calibration::{run_cal_line, run_band_suite}`. `BattleState` gains `fire, pool, obstacles, smoke, casualties, weather, seq`; `battle_hash` fills SPEC-004's reserved blocks (fire block: all `FireExt` fields ascending then `seq` and the pool; smoke cells; casualty records).

## 11. Acceptance tests (tests first; goldens recorded once from the stated reference)
- **T5-01 loader:** tables load; float, unknown key, provenance violations rejected; step sums match; `validate_data.py` agrees. **T5-02:** metre values equal `yd * 914.4`, `ft * 304.8` exactly. **T5-03:** drag `k` within 1e-6 relative of `f64`.
- **T5-04 vacuum:** `cd = 0`, no wind: range at 1, 5, 10 degrees equals `v^2 sin(2 theta)/g` within 0.2 percent (analytic).
- **T5-05 drag flight:** range, time of flight and impact speed for every ammo at three elevations, with and without wind (5 m/s cross, 3 m/s head), match `qc/fixtures/ref_flight.py` (independent `f64` RK4, dt 0.0005 s, output committed) within 0.5 percent.
- **T5-06 range table:** elevation increases with range; each 25 m entry crosses aim height within 50 mm; `minie_58` at 183 m and 274 m lies in (0.1, 2.5) degrees, at 805 m in (2, 6).
- **T5-07 query_segment** equals brute force (proptest), including border and outside-grid cases.
- **T5-08 geometry:** standing target at 100 m hit at `z = 1.2 m`, missed at 1.9 m; kneeling missed at 1.4 m; prone hit only below 450 mm; a 1000 mm obstacle stops `z = 0.8 m` and passes 1.3 m; own-unit skip only within 2500 mm; down soldiers never hit.
- **T5-09 cover:** over 20,000 hits on woods (300 permille) the absorbed share is 15 percent plus or minus 1.5 points standing and 30 plus or minus 1.5 prone.
- **T5-10 damage:** per band and region, 200,000 rolls reproduce the table within 0.5 points; `disabled_ticks` in 600..2400, mean 1500 plus or minus 2 percent; `energy_joules(32 g, 290 m/s)` equals the golden from `32 * 290^2 / 2000`.
- **T5-11 loading:** ticks equal the `ceil` formula for every weapon and training; measured rounds/min meet B5, B6; initial offsets differ.
- **T5-12 fouling and misfire:** at fouling 1000 the misfire rate is `50000 * wet_pm / 1000` ppm within 3 sigma over 200,000 shots; the ram step lengthens by exactly the penalty; misfire restarts at `prime`.
- **T5-13 ammunition:** `n` rounds give exactly `n` shots or misfires, then stop; `resupply` restarts; no underflow.
- **T5-14 streams:** adding or removing an unrelated soldier leaves another soldier's first 10 shots (seeds and launch vectors) unchanged.
- **T5-15 simultaneity:** resolving the pool in reverse (test hook) gives identical casualties and hash; two shooters killing one target in a tick give one record, lowest `seq`.
- **T5-16 pierce and burst:** a rifle ball at 20 m through a three-man file hits at most 2; a `shot_12` through a file hits at least 3; canister makes exactly 27 children with repeatable directions and seeds; `dud_ppm = 10^6` makes none.
- **T5-17 smoke:** 100 shots into a cell give 300 units before decay and decay follows the formula to 0.
- **T5-18 F0 rule:** over `cal_line`, `projectiles_spawned == shots_fired - misfires + children` and `unit_level_rolls == 0`. **T5-19:** `run_band_suite` passes B1 to B11.
- **T5-20 determinism:** `cal_line(rifle_58, trained, 91440, false)` (tick 2400, seed 1), `cal_canister(183000)` (tick 1200, seed 1) and `f0_large` (20,000 soldiers, 600 ticks, seed 42) hash to literals recorded once on Linux in `qc/golden-replays/expected.json`, verified on three platforms and across processes.
- **T5-21 perf (ignored test, prints mean, p99, max):** `f0_large`, all `AtWill`.

## 12. Calibration targets and performance budget
CT-1 infantry ammo (T5-06 sanity, `ref_flight`). CT-2 gun ammo (B7; if `cd_pm` is fitted record `confidence unverified, note "fitted"`). CT-3 dispersion, misfire (B1, B2, B10). CT-4 load step splits (sums fixed by B5, B6). CT-5 body size, cover, friendly-fire clearance (B11, T5-08). CT-6 damage table (B10). CT-7 aim, range error, bias (B1 shape). CT-8 smoke source and decay, set at the rendering milestone (dense smoke forces shorter fighting, CWV p511 4.3). CT-9 numeric bands B1, B2, B8 from `data/history` battle records with a second source.
Hypothesis H5: 20,000 soldiers firing at will give about 42 shots per tick (20,000 x 2.5 / 60 / 20; the research gives about 830 shots/s, CWV p459) and about 1,250 projectiles in flight. F0 fire, flight and hit phases take at most 12 ms mean on top of SPEC-004's H4, so the whole tick is at most 30 ms mean and 45 ms p99 inside the 50 ms budget, against the measured toy baseline of about 12 ms (`docs/cycle-reports/CYCLE-00-baseline.md`). A one-tick volley of all 20,000 must finish in 250 ms (a spike, not sustained). All figures are hypotheses until T5-21 is recorded.

## 13. Done when
fmt, clippy `-D warnings`, `cargo test --workspace` (also `--release`) and T5-01 to T5-21 pass; schemas, data, goldens and `f0-curves.json` committed and verified on three platforms; scans pass; public items documented; no floating point outside `#[cfg(test)]`; T5-21 numbers recorded in a cycle report.
