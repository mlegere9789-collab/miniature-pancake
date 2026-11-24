# AUDIT 05: project history, ledger, CI, side areas and the oracle kit

Plain analysis of our own work. Snapshot: branch `observer-2026-11-23` (HEAD `42dd6499`), all `origin/*` refs, 2026-11-23/24.
Method: `git log --all` (5,455 commits), `git cherry` patch-id comparison against HEAD, tree listings, and the files named below.
Numbers marked (h) are heuristics; everything else was counted directly.

## 1. Scope

Covered: full commit history and all 505 remote refs; `ledger/` (378 files); the 3 workflows on HEAD plus the 11 other
workflow files that exist only in history; `mediasuite/ modules/ orchestrator/ scripts/ data/ dino8-core/`; root files;
`parity/oracle/` (SCOPE, G0_BLANKET, CLEANROOM, REVIEW_LOG, README, `cases.jsonl`, `gen_cases.py`, `run_rhino.py`,
`run_here.py`, `compare.py`, the golden file); the Register's acceptance fields; `DINO8_PLAYBOOK.md`; `PARITY_MAP_CURRENT.md`.

Not covered: kernel/app source quality (other AUDIT files); GitHub Actions run results, run logs, PR/review state and
branch protection (no network or `gh` access used, so nothing here says whether any workflow was green); Rhino itself (I
cannot re-run the golden corpus); the Rev 2 Master Plan, Deep Dive and cost/usage billing (not in the repo); whether any
non-HEAD workflow still runs on GitHub; the 2nd golden pass the commit message cites (not in the repo).

## 2. Timeline and branch families

### 2.1 Commits per month (all refs, 5,455 unique commits; 1,280 are merges)

| Month | Commits | Main content |
|---|---:|---|
| 2026-04 | 1 | "Add files via upload": the OpenAI Whisper tree, still the root README/LICENSE/pyproject today |
| 2026-08 | 38 | income orchestrator, AfterNoon, MediaSuite (18 "build steps" in 5 days), Vitals/growmate |
| 2026-09 | 1,627 | Wildkey and Vitals continue to Sep 9; Dino 8 kernel+app build-out starts Sep 2-3 (peaks 323 on Sep 24, 247 on Sep 30) |
| 2026-10 | 1,382 | 497 commits on Oct 12; Oct 13 last day of the "horde" phase; silence Oct 19 to Oct 30; restart Oct 30 (82) |
| 2026-11 | 2,407 | Nov 4 (551) kverify + refuter-checked re-score; Nov 11-16 (1,561) "wave 0": Register import, ledger, 136 `w0-*` branches; Nov 23-24 (10) Observer/oracle |

Authors: "Claude" 5,391 (98.8%); owner accounts (MICHAEL, mlegere9789-collab) 46; "agent" 14; "spike-c" 2; "Dino8 Rhino Observer" 2.
There is no human review trail inside the repo: every substantive commit is model-authored; review is also model-run (refuter agents).

Phases (my reading from dates + subjects):
1. Aug 1 - Sep 9: unrelated products (income orchestrator, AfterNoon, MediaSuite, Vitals, Wildkey) built in the same repo.
2. Sep 2 - Oct 13: Dino 8 "dispatch horde" (Playbook: 38+ standing sessions refilled hourly). 2,917 commits Sep+Oct 1-13.
3. Oct 13 - Oct 30: collapse (weekly usage cap per Playbook sec.1); 92 commits in 17 days.
4. Oct 30 - Nov 5: recovery under `DINO8_PLAYBOOK.md`; `PARITY_MAP_CURRENT.md` created; refuters narrow claims; kverify CI born.
5. Nov 11 - Nov 16: wave 0: Register (10,660 rows) imported ~20 times (see F-9), ledger tooling, row-work branches.
6. Nov 17 - Nov 22: no commits on any ref (7 days) although `ledger/pace.json` rules start 2026-11-18.
7. Nov 23-24: Observer branch: oracle protocol, golden corpus (10 commits).

What the commits are (non-merge, 4,175 by file path): kernel src 22.6%, app src 19.8%, tests-only 15.1%, **PARITY_MAP bookkeeping
15.0% (627)**, verify-trigger (`kvlite:`/`kverify:` selector commits) 8.2%, register/ledger/oracle 7.3%, unrelated products 5.6%,
other 4.3%, CI/release/docs 2.2%. In October 360 of 476 non-merge commits (76%) only edit PARITY_MAP.
1,445 of 5,455 commits (26.5%) touch `dino8-kernel/PARITY_MAP.md` (10,794 lines, ~3 MB); 1,013 merges are literally
"Merge remote-tracking branch ... into claude/pdf-audit-i2bvwm" (self-merges of one trunk by many sessions).

### 2.2 Branch families (505 refs: 501 non-main/origin-HEAD refs scanned)

| Family | Refs | Patch-unique commits not on HEAD | What it is | Parity relevance |
|---|---:|---:|---|---|
| `kverify/*` | 210 | 272 | Throwaway refs that carry a verify workflow (`kverify: carry the verify workflow`) so a push runs the kernel/app suites on unmerged work. 39 are `full*`, rest per-feature (stp, hp, vol, sweepadapt, sliv...) | Verification plumbing; the feature commits underneath are parity work but ~90% are duplicates of trunk content |
| `kvlite/*` | 144 | 363 | One-shot "replay" refs: `kvlite: sections [..] filters [..]` selects which smoke sections/kernel filters run; `kvlite/win-*`, `kvlite/py-*` replay the Windows job / Python CI | Plumbing; 305 of the union's 1,009 patch-unique commits are pure selector/trigger commits |
| `w0-*` | 136 | 361 (109 refs have some)| Wave-0 work: 43 Register-row implementation (rowimpl/rowpilot/rowwork/rw2/rw3), 19 Register/ledger/credit/review/ADR tooling, 15 per-program row work (archicad, autocad, forma, gh, revit, sketchup, blender, dxf, fileio, appbatch), 12 spikes/engine/threads/perf, 2 rhino oracle, 45 kernel geometry fixes/features | High: this is where all row work and the 5 ADRs live; 133 of 136 not fully merged by SHA, 109 still hold patch-unique commits (many `-r2` re-runs) |
| `claude/*` | 11 | 216 | `pdf-audit-i2bvwm` (the real Dino 8 trunk until 11-04, tip `53a680b4`), two Dino 8 session forks (`fervent-babbage`, `jolly-bohr`), and 7 unrelated product branches: growmate/vitals (65 unique), github-image-editor/mediasuite (97), wildkey (42), `intelligent-volta` (42; a game "A House Divided" specs, moved to "special-memory"), mediasuite (35), AfterNoon (2), income-orchestrator (2) | Only the first 3 relevant |
| `backup/*` | 2 | 0 | `local-diverged-pdf-audit-...-6742f71`, `pdf-audit-...-stale-6742f71`, plus `claude/pdf-audit-...-orphaned-local-backup`: evidence of a diverged local/remote trunk that had to be backed up (fully contained in HEAD) | None |
| `main` | 1 | n/a | Whisper + MediaSuite + income orchestrator; last commit 2026-09-05 ("remove pages ... (#49)"); contains NO Dino 8 code (89 commits) | Default branch is not the project |

Union off-trunk: 1,775 SHA-unique commits (5,455 minus 3,680 reachable from HEAD); only 1,009 are patch-unique (`git cherry`), i.e. ~766
were re-applied copies. Of the 1,009, 305 are trigger commits, so ~704 carry content, ~216 of those are unrelated products.
By SHA only 8 of 501 refs are fully merged into HEAD; by patch-id 54 more carry nothing new (w0 27, kverify 25, claude 2, e.g. `w0-step`: +0/-24); all 144 `kvlite` refs carry a unique trigger commit.
True unmerged work (patch-unique): `w0-regfix-sketchup` 21, `w0-ledger` 12, `w0-ledgerfix` 16, `w0-review-a/b(2)` 16-18 each, `w0-taskgen` 15,
`w0-rowpilot` 15, `w0-spike-a` 6, `w0-adrs` 1 (5 ADRs: `docs/adr/ADR-001..005`, absent from HEAD, whose `docs/` holds one file).

Reverts/abandonment: 13 commits start with "Revert"; 91 mention "revert"; 12 mention "accident" (e.g. `943764c8` restores DeleteFaces
Heal=Yes that `2ecd6815` "deleted by accident" - an unrelated fillet test commit); 204 contain WIP/unverified; 121 "Restore". Re-runs
are named `-r2`, `-run1`, `2`, `3` (`tangentfix`, `tangentfix-old`, `tangentfix-r2`; `ae7-offrefit`,`2`,`3`). The `backup/*` refs and the
orphaned local backup are the only force-push-like evidence available (reflogs are not in a clone; no force-push can be proven from here).

## 3. Findings ranked by effect on parity

| # | Sev | Finding | Evidence | Fix |
|---|---|---|---|---|
| F-1 | High | **No parity number is backed by the oracle.** All 10,660 Register rows are `status: open, tier_now: None, evidence: null`. `ledger/evidence`, `incidents`, `heartbeats`, `usage` contain only `.gitkeep`; there is no `ledger/percent.json`/`credits.jsonl`. The only headlines (kernel 79.3%, app 86.5%) are hand-scored; I recomputed both from `PARITY_MAP_CURRENT.md` tables (79.31 / 86.59, partial = 0.5) so the arithmetic is honest, but the denominators are 473 kernel + 148 app self-authored bullets (621 items vs 10,660 rows, 17x coarser) and the baseline for the kernel table is "Parasolid/ACIS", not Rhino 8. Commit `09bbbab8` says "266 credited rows": that figure exists only if CI evidence is present, and none is stored. | `parity/register/*/*.jsonl`; `ledger/legacy.json`; `PARITY_MAP.md:3`; `PARITY_MAP_CURRENT.md` tables | Treat today's measured parity as 0 rows credited. Commit CI evidence (`run.json`, per-task JSON) from every full run to `ledger/evidence/`; publish `percent.json` from `percent.py`; retire the map headline (Playbook rule already says so) |
| F-2 | High | **Row acceptance ids mostly do not resolve to any test.** 2,978 of 2,987 Blender rows (99.7%) have `acceptance: []`. Revit/SketchUp ids are `T-D8-REVIT-AI-0001` / `T-SKETCHUP-AI-0001` with `corpus: "golden/revit/..."`, a directory that does not exist; Archicad `acc.archicad.*` (915), GH `gh.core.*` (758), Forma `forma.*` (186), Rhino `rhino.<domain>.<name>` (1,165) are symbolic. A string search of all test/source files (h) finds an id in only 258 rows (2.4%): rhino 194, autocad 28, gh 15, archicad 9, blender 9, revit 3. `rhino.analyze.angle` appears nowhere except the row. | counts via script over `parity/register`; example `D8-RHINO-ANALYZE-0001` | Add a validator rule: every non-alias row must have >=1 acceptance id that resolves in the evidence inventory (`--inventory` already exists, never enforced in CI); mark unbacked rows `unspecified` not `open` |
| F-3 | High | **The credit rule is circular for parity.** A row counts when its named Dino test has >=1 passing check (`ledger/README.md` rule of `percent.py`). Tests are written by the same model that wrote the code, with closed forms or "expected values written from memory of Blender" (message of `85edc1b9`: 15 Blender rows left unlinked "the Register has no flag for an unverified oracle"). Nothing requires an external reference: `grep` finds no consumer of `parity/oracle/**/golden` in any test, CMake file or workflow, and no Dino 8 case runner (`dino_results.jsonl`) exists. Result: a row can be credited with zero comparison to the baseline program. | `ledger/README.md` "When a test counts"; `85edc1b9`; README of oracle kit "Not yet built" | Add `oracle_ref` to the row schema; a row reaches Strong only if the cited test loads a golden record and compares with `compare.py` classes; block credit for rows whose oracle is `memory` |
| F-4 | High | **CI does not gate the kernel on the trunk.** `dino8-app.yml` builds the kernel with `DINO8_KERNEL_BUILD_TESTS OFF` (`dino8-app/CMakeLists.txt:53`), so its `ctest` runs app unit tests only. The kernel suite (`test_basic.cpp` 109,934 lines + 133 `tests/extra/*.cpp` + `continuity2_tests`) and the 175 smoke section files run only from `kernel-verify.yml`, which triggers on `kverify/**` pushes or manual dispatch, `ubuntu-22.04` only. A normal push or PR to the trunk never runs them. Logs are kept 3 days; nothing is written back to the repo. | `.github/workflows/dino8-app.yml:3-9`, `kernel-verify.yml:12-16`, `retention-days: 3` | Run the sharded kernel suite on every push to the integration branch and on PRs; commit evidence; keep kverify for unmerged work only |
| F-5 | High | **The real trunk is unnamed and unprotected by CI.** Default branch `main` has no Dino 8 code (Whisper + MediaSuite, last commit 09-05). Dino 8 CI (`dino8-app.yml`) triggers on `main` and `claude/pdf-audit-i2bvwm` only; that branch's tip is 11-04 (`53a680b4`), 717 commits behind HEAD. HEAD's lineage runs through `w0-rhino-oracle` (3,679 commits), which only triggers on PR. Whatever is pushed to `observer-*`/`w0-*` is untested by the workflows on HEAD. | `git branch -r --contains 85edc1b9` = `w0-credit1`, `w0-rhino-oracle`, `kvlite/credit1-004710`; workflow `on:` blocks | Pick one integration branch (e.g. `dino8/main`), make it the default, move all triggers to it, rename away from "pdf-audit" |
| F-6 | High | **Oracle golden capture has runner artefacts; failure semantics are wrong.** 24 of 200 golden records (12%) are `ok:false, error:"RuntimeError"`: the runner's own `raise RuntimeError("boolean returned no result")` is recorded, so an empty array, a null return and a refusal are indistinguishable, and a legitimate geometric empty (disjoint intersection) is stored as a "failure". 21 are Booleans (BXSP 8/27, BXBX 6/18, CYCY 4/15, SPSP 3/10), 3 are blends. `INTS-CRCR-0001` (identical circles) is stored as `count 1, points [[1,0,0]]` because the runner reads only `PointA` of the first event and ignores overlap events: a runner artefact recorded as Rhino behaviour. `compare.py:91-94` also counts "Rhino refused, Dino succeeded" as a divergence, contradicting Best-tier superiority, and `:95-97` counts "both failed" as a match without comparing error type. | `golden/*.jsonl`; `run_rhino.py:105,128,175,248`; `compare.py:91-97` | Record `status` in {ok, empty, refused, exception} with the public-API return value; capture overlap events; add a `rhino_defect`/`expect_superset` flag so Dino may exceed Rhino; re-run the 24 failing cases with a second API route (command-line `_BooleanUnion` through `RhinoApp.RunScript`) and diff |
| F-7 | Med | **Corpus covers ~1.6% of the Rhino Register and 0 rows of 9 other domains.** 200 cases in 9 families (boolean 70, primitive 38, intersect 20, offset 18, extrude 16, blend 12, revolve 12, curve 10, loft 4), 12 ops, all origin-centred, sizes 0.5-12, one document tolerance 0.001 (no unit system, no angular tolerance, no large-coordinate or tiny-feature scale). Direct hits: ~20-25 of 1,356 rows (Box/Sphere/Cylinder/Cone/Torus, Boolean x3, Blend/Chamfer/FilletEdge, Offset, Extrude, Revolve, Loft, Arc, interpolated curve, plane sections). Rows per domain with no case: SELECT 125, VIEW 150, ORG 118, RENDER 75, SETTING 61, UI 23, ANNOT 50, LAYOUT 38, FILE 37 (no round-trip case), MESH 114, SUBD 46, XFORM 58, ANALYZE 54, SCRIPT 50. 196 rows are tagged "Rhino 9 beta" and cannot be observed on Rhino 8.36. ~55% of rows (UI/state/display) need non-numeric oracles (selection sets, image diffs, file round-trips) that the schema (`ALLOWED_OUT_KEYS`, 14 keys) cannot hold. | `gen_cases.py`; `parity/register/rhino/COVERAGE.md` | Add per-domain PLAN (already ordered in SCOPE); extend schema with `set`, `image_hash`, `roundtrip`; add scale/unit sweeps; drop or park the 196 Rhino 9 rows |
| F-8 | Med | **Nothing but kverify was ever independent of the same model.** The model writing code, tests, refuter reviews, Register rows and the oracle protocol is one model family (Claude-Session trailer `01HtPNV3...` on coordinator and oracle commits). Playbook admits "done" was self-reported and a crashing test was normalised ("fragment-loop" crash, `878ecdb`). Refuter agents narrowed claims (`98d32f9b`, `e6a6154a`: Boolean 66.0 -> 65.5%) which is real evidence of a working check, but still same-model. | `DINO8_PLAYBOOK.md` sec.1; commits cited | Make the golden oracle the only credit path for Rhino rows; require a human or owner-run spot check of N random credited rows per wave |
| F-9 | Med | **Process churn dwarfs output.** Register import was repeated ~20 times on Nov 16 (e.g. `09bbbab8`, `7b238e1f`, `deca7f15`, `754ff0e3` ... "import Register and ledger tooling from w0-credit1"): one commit per parallel branch. 12 "reconcile headline" merges on Oct 12-13 resolve concurrent edits to one shared doc, the exact "shared mutable file" the ledger README forbids. 1,445 PARITY_MAP commits vs 303 register/ledger/oracle commits. | `git log --all --grep 'import.*w0-credit1'`; `PARITY_MAP.md` | Freeze PARITY_MAP.md read-only (archive to `docs/history/`); one append-only file per agent as the ledger already specifies |
| F-10 | Med | **Wave-0 commit counts are real; pace is not.** `pace.json`: 10% by 11-24 (~1,066 rows), 25% by 11-30, 100% by 12-18, `pace_rules_start` 11-18. Linked rows: 267 test ids set (verified from diff `85edc1b9`: 267 `test_id` changes + 69 notes-only = 336 lines; claim "267" is accurate) = 2.5% *linked*, 0% credited. No commit on any ref from 11-17 to 11-22. Ledger tasks: 338 files, 10,660 rows, 743 points, all `open`; hint coverage says 79.9% of rows have no existing command or kernel symbol (`ledger/tasks/REPORT.md`). `waves.json` is "proposed, to be confirmed". Enscape (9th baseline) has 0 rows. | `ledger/pace.json`; `ledger/tasks/*.json`; `git log --since=2026-11-17` | Re-plan: the stated Dec-18 freeze is not reachable on 0 credited rows; publish a new pace from measured credit per day once F-1 is fixed |
| F-11 | Med | **Repo identity and licence mismatch.** Root `README.md`, `pyproject.toml` (`openai-whisper`), `LICENSE` (MIT, (c) 2022 OpenAI), `MANIFEST.in`, `requirements.txt`, `CHANGELOG.md`, `model-card.md`, `approach.png`, `language-breakdown.svg` are the Whisper fork. The owner's stated Dino 8 licence ("free, open source, never sold for gain") appears only in `G0_BLANKET.md`; MIT permits sale. | root listing | New Dino 8 `README` + `LICENSE` decision (the owner's terms are not a standard OSI licence; counsel/choice needed); move Whisper files out |
| F-12 | Med | **Unrelated products share the repo and CI** (see sec.5). Cost is small in bytes, large in noise (branches, history, a Python CI job, `mediasuite-ci.yml`). | sec.5 | Split to separate repos; archive the 7 `claude/*` product branches |
| F-13 | Med | **The Master Plan Rev 2, Deep Dive and 5 ADRs are not on the trunk.** `ledger/pace.json`, `ledger/README.md`, `waves.json`, `percent.py` cite "plan 6.4, 7, 11.2, Appendix B/C" but no plan file exists in the tree; ADRs are on `w0-adrs` only. SCOPE item 0(a) assumes they are in `docs/` (it holds `CODE_SIGNING.md`). | `ls docs`; `git ls-tree origin/w0-adrs docs/adr` | Merge `w0-adrs`; commit the plan (or its numbers) so budgets/pace can be checked |
| F-14 | Low | Release-engineering weight: `dino8-app.yml` 628 lines (Authenticode self-tests, notarisation, cosign, SLSA attest, ARM64, `.deb`/`.dmg`/Inno) vs 0 parity gates. Useful later; it is where most CI effort has gone. | workflow | Keep, but do not count as parity progress |
| F-15 | Low | Oracle docs inconsistent: `CLEANROOM.md` title says "v1, DRAFT" while SCOPE/G0_BLANKET call it v2; `REVIEW_LOG.md` G0 states decompilation "is NOT covered" and `CLEANROOM.md` Phase D covers it; REVIEW_LOG intake log is empty although a golden file was accepted (commit message claims G2 pass, I reproduced `compare.py --check-golden`: 200/200, 0 rejected); owner countersign unticked; `README.md` still says runner "UNTESTED". | oracle files | Update headers; log the intake line; one source of truth for scope |

Claims in commit messages I could verify or not:

| Claim | Result |
|---|---|
| `85edc1b9` "link 267 Register rows" | True: 267 `test_id` changes (336 changed rows incl. 69 notes-only) |
| `42dd6499` golden "200/200, 0 rejected" | True (re-ran `compare.py --check-golden`) |
| `42dd6499` "second full pass matched record-for-record" | Not verifiable: second pass was written to a temp dir (`run_here.py:24`), is not in the repo (`run_here.py:24`), and ran in the same Rhino process/session |
| `09bbbab8` "266 credited rows" | Not verifiable: Register `evidence` is null for all rows; the count depends on CI evidence not in the repo |
| PARITY_MAP headline 79.3 / 86.5 | Arithmetic reproduces from the category tables (kernel 79.31 vs doc 79.3; app 86.59 vs doc 86.56/86.5): within 0.03, doc truncates by stated rule |
| Playbook "645-assertion smoke harness" | Not rechecked; `smoke.sh` is 14,049 lines and 175 section files exist (counts grew since) |
| 485 commits whose subject contains "%" | Typically `PARITY_MAP` +0.1% moves; each reflects a hand-edited bullet flip, not a measured result |

### Incident patterns (what keeps failing)
`ledger/incidents` is empty, so patterns come from history instead. (1) CI environment: apt mirror DNS failures
(`d5c353f3`, `f1e43010`, `26a919ad`: three workflow patches in one day), runner image quirks (xvfb on Windows `ci: fix Windows fallback ...`),
artifact exec-bit loss (`b9b805b3`). (2) Concurrent edits to the shared PARITY_MAP (12+ reconcile merges). (3) Re-run suffixes
`-r2`/`2`/`3` on ~25 w0 branches = retries of the same task. (4) Silent session death after the usage cap (Oct 13-30; Playbook).
(5) Accidental deletions by unrelated commits (`2ecd6815`, `943764c8`). (6) Known-crash normalisation (`878ecdb`). (7) Windows/macOS-only failures found
late because kernel tests never run there (F-4).

## 4. CI gap table

| Workflow (on HEAD unless marked) | Trigger | OS | What it truly gates | Gaps |
|---|---|---|---|---|
| `dino8-app.yml` linux | push `main`/`claude/pdf-audit-i2bvwm`, PR touching dino8-*, scripts | ubuntu-22.04 | app build; `ctest` = app unit tests (brep mesher, aci palette, fuzz geometry, g2 blend, i18n, xlsx...); `smoke.sh` headless with touch/tutorial evidence checks (grep-gated, good); .deb | kernel suite OFF; no numeric golden; smoke asserts self-authored values |
| same, windows | same | windows-latest (MSVC, Mesa llvmpipe) | build, ctest, smoke, installer, Authenticode self-test, verify-release negative test | kernel suite never run on Windows; signing inert without secrets |
| same, windows-arm | same | windows-11-arm | build, version, dependents check, zip | no tests run at all |
| same, macos | same | macos-15 | build, ctest; .dmg; notarise only with secrets | **no smoke** (NSGL limit stated in YAML); kernel tests off; ad-hoc sign self-test |
| same, release-tooling / release | same | ubuntu-latest | verify-release tamper test; beta pre-release with cosign + provenance | publishes on every push to the two branches; a release does not imply parity |
| `kernel-verify.yml` | `kverify/**` push or manual only | ubuntu-22.04 | 12-shard full kernel suite (`exit $rc`, binary returns 1 on any failure: good), continuity2, optional fillet-extra (`|| true`), 12-group app sections; per-feature filters on shard 0 (exit codes ignored) | not on trunk/PR; no Windows/macOS; evidence discarded after 3 days; filter run does not fail the job |
| `mediasuite-ci.yml` | `main`, PR on `mediasuite/**` | windows-latest | WPF build + Core tests | unrelated product |
| NOT on HEAD: `ci.yml` (black/isort/flake8 + unittest on `modules/ orchestrator/`) | `main`, PR | ubuntu-24.04 | Python style/tests | only on `origin/main`; unrelated product |
| NOT on HEAD: `ledger-check.yml` | push on `parity/**`,`ledger/**` | ubuntu-22.04 | validate_register (jsonschema + builtin), percent.py recompute, guard.py, ledger tests | exists on `w0-ledger`/`kvlite/ledger-*` only; **no workflow on HEAD runs `parity/tests`, `ledger/tests`, or the Register validator** (grep in `.github` finds none) |
| NOT on HEAD: `nightly.yml` | cron 02:23 | ubuntu-22.04 | perf budgets (3% regression rule) + ASan/UBSan full kernel in 12 shards | only on `w0-perfci`/`kvlite/perf-*` |
| NOT on HEAD: `kverify-win/perf/lite/prlinux`, `kvpy`, `kernel-verify-ose`, `house-divided-ci`, `vitals-ci` | throwaway refs | mixed | replays of single jobs | generated per run, never committed; results exist only in Actions logs |

Matrix gaps in one line each: no Windows or macOS kernel test; no numeric-golden gate (nothing loads `parity/oracle/*/golden`);
no sanitizer, no performance gate and no Register/ledger validation on HEAD; no coverage measure; no determinism check across
platforms (floating-point differences between MSVC/clang/gcc are untested for kernel results); CI result is not persisted
(`ledger/evidence` empty); nothing checks that `oracle` cases still exist when the corpus is edited; the observer branch triggers nothing.

## 5. Side areas: do they support the mission?

| Area | What it is | Size at HEAD | Verdict |
|---|---|---|---|
| `mediasuite/` | Windows WPF media converter (18 build steps, installer, FreeConvert "format-parity audit") | 176 files, 20.5k lines, 1.5 MB | Unrelated product; separate repo |
| `modules/` | 5 "income" folders (deal-alert bot, digital products, dropshipping, micro-SaaS, stock licensing) | 15 files, 1.0k lines | Unrelated; delete from trunk |
| `orchestrator/` | Python income-program scheduler/dashboard; NOT the plan's cloud coordinator despite the name | 12 files, 1.4k lines | Unrelated; name clash with the coordinator role in SCOPE |
| `data/` | one placeholder file | 1 | Delete |
| `scripts/` | verify-release, stage-release, signing negative tests (called by `dino8-app.yml`) | 5 files, 766 lines | Supports Dino 8 release trust; keep |
| `dino8-core/` | thread-safety audit + TSAN probe (Spike C input) | 4 files, 1.2k lines | Supports kernel threading; keep, fold into kernel docs |
| root Whisper files | see F-11 | ~9 files | Unrelated |
| `claude/*` product branches | growmate/vitals, wildkey, mediasuite, github-image-editor, AfterNoon, income, "A House Divided" game | ~216 patch-unique commits | Unrelated |

Quantified: on HEAD, unrelated top-level content is ~213 of 2,252 tracked files (9.5%) but ~23k of ~560k text lines (4%) and ~3 MB of
~50 MB. In history 234 non-merge commits touch only unrelated dirs (5.6%) plus 38 in Aug; across all refs ~216 further patch-unique
commits sit on product branches. Recommendation: (1) create `legelabs/mediasuite`, `legelabs/income-orchestrator`,
`legelabs/growmate` (+ wildkey, game) by `git subtree split`/`filter-repo` from the existing branches, then delete the folders from the Dino 8 trunk;
(2) remove Whisper root files; (3) delete or archive the 7 `claude/*` product branches and the 354 `kverify`/`kvlite` throwaway refs once
their evidence is committed; (4) keep `scripts/` and `dino8-core/`. Distraction cost is mostly coordination: product CI (`ci.yml`,
`mediasuite-ci.yml`) shares the default branch with Dino 8; Dino 8 CI is not on the default branch.

## 6. Oracle kit review (`parity/oracle/`)

Protocol (CLEANROOM.md v2, G0_BLANKET, SCOPE):
* Sound: black-box first, cases authored by us from closed forms (G1), numbers/flags only, fixed tolerance classes, build string per record,
  generated corpus is deterministic (`gen_cases.py`, assert unique ids), `compare.py --check-golden` is a real intake check.
* Weak: (a) "Observer" and "Reviewer" gates are one session family; the REVIEW_LOG intake log is empty, so G2/G3 have no record.
  (b) The Observer is also ordered to audit the whole Dino 8 repo (SCOPE item 0), i.e. one context holds Rhino behaviour (and, under Phase D,
  decompiler knowledge) and all Dino 8 code: this breaks rule 2 ("a single Claude session must not do both in one context"). Run the audit in a
  context with no baseline-program output (this file was written by a separate agent for that reason).
  (c) G3 contamination test = "ask a fresh agent to re-derive the behaviour from the card": a fresh agent of the same model with training memory
  of openNURBS/Blender/Rhino documentation cannot detect leakage; it only checks sufficiency. (d) Implementers read openNURBS source (McNeel) by
  design: that is the permitted channel but should be listed as a contamination source in the log. (e) No negative-control cases (a corpus subset
  with answers our kernel is known to get wrong) to show the comparer can fail. (f) G5 second pass is same-process/same-session; cross-machine or
  cross-build determinism is never tested; golden files carry only the build string, not OS, culture, CPU, thread count or unit system.
  (g) Phase D (decompilation) is an owner-accepted legal risk (REVIEW_LOG: counsel not obtained); engineering-wise it adds an un-auditable path
  (cards are checked only by claims "each backed by a Phase B vector", which cannot show absence of copied structure). Recommended: require that each
  card list the exact golden ids that confirm each claim and keep the card-to-vector coverage as a machine-checked field.
  (h) Document drift between REVIEW_LOG and CLEANROOM/G0_BLANKET (F-15).

Corpus vs Register: see F-7. Of 1,356 Rhino rows, ~616 (45%) sit in domains where numbers can be recorded (CURVE 158, SURF 144, SOLID 42, MESH 114,
SUBD 46, XFORM 58, ANALYZE 54); the 200 cases touch ~1.6% of all rows. For the Booleans there are 70 cases (strongest family) but 21 of 70 refusals.
Case ids carry no link to Register ids (`D8-RHINO-*`), so coverage per row cannot be computed; add `rows: [...]` to each case.

`compare.py` weaknesses (line numbers refer to `parity/oracle/rhino/compare.py`):
| Line | Issue | Effect |
|---|---|---|
| 15,50-51 | `close()` passes if `|a-b| <= max(abs_tol, rel*max)`, with `abs_tol` = document tolerance 1e-3 | For area/volume below 10 the effective window is 1e-3 absolute (>= 1e-4 relative); Register rows demand "<= 1e-6 relative". Unit cube volume may be off by 0.001 and pass |
| 21-29 | duplicate ids silently overwrite (`recs[r["id"]] = r`); no duplicate check in golden or Dino results | a second run appended to the same file hides the first |
| 32-47 | `check_golden` does not require golden to cover every case, nor every key in the case's `measure` list (currently 0 missing, verified), nor value types/ranges per key (a `points` list may contain strings; only `error` has a 200-char bound), nor reject `NaN`/`Infinity` (`json.loads` accepts them) | an incomplete or poisoned golden file passes G2 |
| 41 | unexpected-field test allows any int/float/bool value for unknown top-level keys | a hidden numeric channel per record |
| 54-64 | `bbox` uses absolute tol only; `points` are paired after lexicographic sort of tuples, so two points whose first coordinates differ by < tol can swap partners; `zip` truncates when dimensions differ; no angular, parametric or relative class | false divergences and false matches on intersection points |
| 64 | flags/counts use `==` (`True == 1`, `1.0 == 1`) | type confusion between bool and count |
| 91-97 | outcome logic: Rhino ok / Dino fail and the converse are both divergence; both fail = match, error type ignored | see F-6 |
| 100-103 | iterates golden keys only; a key missing from golden is never tested; a Dino `None` is a divergence (good), a NaN is a divergence (good: `abs(nan) <= x` is False) | asymmetric coverage |
| 89,93,106 | `cases[cid]["family"]` raises `KeyError` if `--dino` is run while golden has an unknown id (bad ids only print, then continue) | crash instead of report |
| 98 | `tolerance` defaults to 0.001 if absent | silent default |
| n/a | no tests for `compare.py`, `gen_cases.py`, `run_rhino.py` (`parity/tests` covers only register tools); timing (`ms`) is never compared though performance is a parity row family | regression risk in the gate itself |

Runner (`run_rhino.py`): multi-piece Boolean results sum faces/edges/vertices and Euler numbers across pieces (lines 98-112), which makes the
record depend on how Rhino splits results, not on shape; `blend_edges` fillets all edges with one radius (cases above half-size are expected refusals but
indistinguishable from API misuse); `CreateOffsetBrep(... solid=False ...)` was switched after the first run (commit text), so offset records
measure a different call than the Register row "OffsetSrf solid" implies; `_round` to 12 significant digits hides last-bit differences (fine, intentional).
Golden `tolerance` is always 0.001 and is the only tolerance in the file: Dino's own kernel tolerance is never compared.

Net: the kit is a good first version (it is the only baseline-derived evidence in the repository), but today it proves nothing about Dino 8
because no Dino 8 runner exists, the comparer is untested and lenient on tolerance, and 12% of golden records are failure placeholders.

## 7. Minimum fix list (ordered)
1. Write the Dino 8 case runner (`dino_results.jsonl`) and run `compare.py` in CI on every push (F-3, F-4, sec.6).
2. Replace failure semantics in runner and comparer; re-record the 24 refusal cases and `INTS-CRCR-0001` (F-6).
3. Commit CI evidence; compute `percent.json`; add the resolver check for acceptance ids (F-1, F-2).
4. Choose and protect one integration branch; run the full kernel suite there; port `ledger-check.yml`, `nightly.yml` from `w0-*` (F-4, F-5).
5. Merge `w0-adrs`, `w0-ledger*`, `w0-regfix-sketchup`; archive the throwaway `kverify`/`kvlite` refs (sec.2.2).
6. Split unrelated products and Whisper files; settle the licence (F-11, F-12).
7. Add tests for `compare.py` (NaN, duplicate id, missing key, tolerance window, point pairing) before trusting any divergence ticket.
