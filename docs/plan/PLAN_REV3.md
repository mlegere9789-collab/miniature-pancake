# Dino 8 (LegeLabs) Plan Rev 3: full parity, pain-point fixes, UX, Outscape. Author: XO (coordinator). Status: PROPOSED, cloud horde PAUSED.

## 1. Non-negotiables (owner, 2026-11-24)
1. Dino 8 does everything the 8 programs (Rhino 8, Grasshopper+plug-ins, Blender, SketchUp, Revit, Archicad, Forma, AutoCAD) AND Enscape
   (built in as **Outscape**) can do, down to the most obscure tool, command, menu option and file format.
2. Every user pain point reported about them is fixed; every long-requested feature the vendors never shipped is built.
3. Dino 8 is the most user-friendly of the group, looks as polished as the best, and is quick and buttery smooth.
4. Ironclad QC. Steady, honest, measured gain every day. Nothing stalls. No usage is spent on anything that does not move Dino 8 closer.

**Definition of "done" (so progress is measurable).** The Register holds one row per capability/pain point/request/UX target. A row is
CREDITED only when a named test passes in CI with evidence, against an oracle independent of our implementation. Row classes:
EXACT, TOLERANCE (stated), BEHAVIOURAL, MEASURED (perf/UX), UNTESTABLE-HEADLESS (owner decides: hand-test or descope). 100% = every row
credited or owner-decided. The percentage reported to the owner is credited rows only, never agent claims.

## 2. Workstreams
| WS | What | Output | Where it runs |
|---|---|---|---|
| A Census | every command/option/menu item/API call/setting/format per program (9) | `census/<program>.jsonl` -> Register rows | Observer (local) enumerates through public APIs; cloud mines public docs; **completeness proven** by comparing two independent enumerations (capture-recapture): census closes when estimated unseen < 1% |
| B Voice of customer | pain points, feature requests, UI/usability sentiment per program (9) | `voc/<program>/pain.jsonl` (own-words summary, source URL, frequency, severity), clusters, UI sentiment notes; each item becomes a Register row (PAIN-/REQ-/UX-) with a measurable acceptance test | Observer (open internet). Sources: official forums/idea boards, Reddit, review sites, tutorial/YouTube comment themes. Quotes are summarised, not copied |
| C UX and polish | design system, UI audit rubric per program, perf budgets (command latency, frame time, load time), clicks-per-task | UX rows + CI perf gates + weekly owner-test builds | cloud implementers; owner tests (I cannot judge looks) |
| D Parity engine | implement and verify rows | credited rows | cloud implementer squads |
| E Kernel hard problems | Boolean/SSX robustness, blends, offsets, surfacing, Rust core systems (ADR 001-005) | corpus pass rates vs golden | cloud kernel engineers |
| F Outscape | real-time renderer, materials, lighting, GI, exposure, camera, asset library of OUR OWN assets | image-statistics + perf rows; software-rasteriser CI; owner GPU runs | cloud Outscape engineers + owner machine for GPU perf |
| G Integration and QC | merge train, full CI, independent reviews, mutation checks | green candidates, defects | integrator + QC agents |
| H Ops | ledger, watchtower, reports, usage governor | daily digest | XO |

## 3. The horde (steady state; hard cap 20 in-session agents + up to 3 local Observers)
| Role | Count | Job | Stop rule |
|---|---|---|---|
| XO (me) | 1 | plan, ledger, scheduling, verification of every hand-back, reports | n/a |
| Row squads | 6 | pull claimable batches of 25 rows (kernel-geometry; mesh/SubD/sculpt; 2D CAD and drafting; BIM; flow/scripting; interop and UX) | batch done or 60 min without a commit/CI result |
| Kernel engineers | 2 | Boolean/SSX robustness; blends/offsets/surfaces; Rust core systems | per-problem budget; golden-corpus pass-rate gate |
| Outscape engineers | 2 | renderer core; materials/lighting/GI (start in week 2, after Outscape rows exist) | per-milestone budget |
| UX/design-system engineer | 1 | design system, perf budgets, interaction latency | per-milestone budget |
| Integrator | 1 | merge train: 3-4 branches per full run, triage red | never idle: runs only when branches are ready |
| QC auditors | 2 | (a) oracle-independence audit of newly credited rows, (b) mutation checks and adversarial re-verification of a daily sample | daily sample done |
| Public-source researcher | 1 | docs mining for the census where cloud network allows | census closure |
| **Observers (local)** | 1-3 | O1: Rhino, Grasshopper; O2: Blender, SketchUp; O3: AutoCAD, Revit, Archicad, Forma, Enscape. Each: census enumeration, Phase B corpora, Phase D algorithm cards, VoC research, audit | heartbeat commit every 30 min only on change |
Peak in-session: 16, burst to 20 only when the usage governor allows. Agents are bounded jobs that terminate; no long-lived idlers.

## 4. Usage governor (the efficiency rules)
* One weekly budget B in agent-hours (owner supplies a number; until then I use ~100/week, which is an inference). Allocation: 55% row squads and kernel, 15% Outscape and UX, 12% integration and QC, 10% census/VoC (cloud side), 8% XO. Reserve 10% of B for defect fixing; spawning stops at 85% of the week's budget.
* Yield tracking: verified rows per million tokens by squad. A squad below half the median for 2 days is reassigned or shrunk. Agents with no commit/CI result in 60 minutes are resumed once, then stopped.
* Banned: duplicate work (every batch is claimed in the ledger), exploratory chatter, re-reading large files without need, polling loops, per-agent CI waiting (one shared waiter notifies), idle agents, reports that nobody acts on, retrying a failed identical run, launching more agents than the weekly budget supports.
* When the weekly budget is nearly spent: only zero-token work runs (CI, golden sweeps on the owner's machine, scheduled long jobs); a scheduled task restarts the horde at the reset. The earlier 44-hour stall must not recur.
* CI is the cheap compute: heavy verification runs on GitHub runners (sharded), not in agent context.

## 5. Daily cadence (gain every day)
* **Morning:** claim tomorrow's batches; the daily target = remaining rows / working days left, with a floor. Merge train produces one green candidate per day; rows credit only after it.
* **During the day:** squads work; QC samples 10% of newly credited rows each day (oracle independence, mutation check: inject a bug, the test must fail). Any weak or circular test is un-credited and the row reopened.
* **Every 4 hours (not hourly):** a compact digest only if something changed, plus immediate alerts for stalls and failures. **Daily one-page ledger report:** credited rows total and by program, change since yesterday, agents, usage by squad, open failures with owners, next day's target. A day with zero credited gain triggers a written cause and fix, same day.
* **Weekly:** independent audit by an Observer, re-estimate of completion, usage-vs-yield review, owner test build for UX.

## 6. QC (non-negotiable list)
Credit only with named test + CI evidence + independent oracle; no tolerance changes except by XO with written reason; no deleted/skipped/weakened tests; evidence JSONL must equal printed counts; every hand-back verified by XO from raw CI logs; full sharded CI on every candidate; clean-room firewall checks on every Observer artefact; recalled-from-memory oracles are flagged and un-creditable until an Observer vector replaces them; PR branch receives only fully green, pre-flighted candidates.

## 7. Command and control, including the Rhino 8 clean-room oracle chat
* I cannot message local sessions directly (they are not reachable from the cloud). So control runs through the repo: `ops/orders/<session>/NNN.md` (my orders), `ops/reports/<session>/` (their reports), plus a heartbeat file each Observer updates only when work changed. Each local session's standing order: at the start of every work cycle `git pull` orders, act, push report.
* I watch every Dino 8 chat each check-in: cloud subagents (ListAgents), child sessions (list_sessions status buckets), Observer branches (commit times). A session with no commit/report for 60 minutes is resumed or I tell the owner it needs a nudge. Blocked sessions (e.g. the two Grasshopper child sessions awaiting approval) are listed to the owner until resolved.
* Owner's local sessions burn the owner's usage too: Observers run bounded corpora and write short reports, not conversations.

## 8. Phases (calendar starts when the owner says resume; today 24 Nov; clock not started)
| Phase | Weeks | Gate |
|---|---|---|
| 0 Stand-up | 0-1 | ops/ mailbox, ledger claims, usage governor live; cand5 green and merged (credits the ~210 built rows); Rhino golden in; census started for all 9; VoC started; Enscape/Outscape Register rows built |
| 1 Census and ramp | 1-3 | census closed for Rhino, Blender, AutoCAD (largest); VoC clusters for all 9 become rows; row throughput at target; Outscape architecture + first render rows |
| 2 Build | 3-9 | bulk row credit; kernel hard problems at corpus-pass gates; UX rows and perf CI gates met; Outscape feature-complete on testable rows |
| 3 Harden | 9-11 | all census rows credited or owner-decided; full burn-in 7 days; independent audit |
| 4 Close | 11-12 | residual rows, owner acceptance tests, release candidate |
Estimate: the added census/VoC/pain-fix/UX scope adds roughly 25% rows; central finish ~11-12 weeks from resume (range 8-18), re-estimated at the end of week 1 when the census gives real counts. Resuming Mon 30 Nov puts the central date in mid-to-late February 2027. 25 December is not reachable at this quality bar.

## 9. Inputs the XO needs (statements, not blockers; defaults apply if absent)
Weekly usage budget number (default: ~100 agent-hours/week, 85% spawn stop); network allowlist for cloud research hosts, or confirmation that research runs on the local Observers (default: local); a machine with a GPU for Outscape performance runs (default: the owner's MSI, software rasteriser in CI); second and third local Observer sessions (default: one, so the census is slower); a weekly slot for owner UX testing; decisions on UNTESTABLE-HEADLESS rows as they appear.
