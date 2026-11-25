# Dino 8 full audit: consolidated index (Observer, 2026-11-23)

Static audit of OUR work at this checkout (branch `observer-2026-11-23`, based on `w0-rhino-oracle` d320f824; all 505 remote refs fetched). Five parallel read-only reviews,
one file each. Nothing was built, and no CI result could be read (no `gh`, no Actions access), so "real" below means "a named test exists and is wired into CTest or a workflow",
never "observed green". Findings quoted here were taken from the area files; the ones marked (checked) I re-verified myself.

| File | Area | Headline |
|---|---|---|
| 01-research-and-docs.md | plans, specs, claims | the percentage claims (79-98%) are never reconciled and nothing is evidence-backed |
| 02-register-and-parity.md | the 10,660-row Register | 0 rows credited; 97.5% cannot be credited as written |
| 03-geometry-kernel.md | kernel, kverify and w0 branches | 2,203 kernel tests but no trunk CI gates them; Booleans measured at 65.4% |
| 04-application.md | the application | mostly Rhino-only; tests mostly pin our own output; vendor text shipped |
| 05-history-ledger-ci.md | history, ledger, CI, oracle kit | no parity number is oracle-backed; kit has runner and comparer weaknesses |

## Ranked by effect on parity

| # | Finding | Evidence | Fix owner |
|---|---|---|---|
| 1 | **No parity is credited.** All 10,660 Register rows are open, tier None, evidence null. `ledger/evidence/` is empty. The hand-scored 79.3% kernel and 86.5% app headlines reproduce only if every partial counts as half (present-only: 59.8% and 78.6%). | 01, 02, 05 | coordinator |
| 2 | **97.5% of rows cannot be credited as written.** 2,978 Blender rows have no acceptance test; 7,415 rows name test ids that exist nowhere; only 266 rows (2.5%) have all test ids resolvable. | 02 | coordinator |
| 3 | **Nothing connects Dino 8 to the oracle.** No Dino 8 case runner exists, no test or workflow loads `golden/`, 0 rows name an oracle case. The Rhino corpus (200 cases) touches about 1.6% of the 1,356 Rhino rows. Tolerance classes disagree (golden 1e-3, existing tests 1e-9 to 1e-12, Register "1e-6"). | 02, 05, Observer runs | coordinator, implementers |
| 4 | **There is no trunk.** `main` is the Whisper repo plus MediaSuite. Kernel and app live on `claude/pdf-audit-i2bvwm`; the newest Boolean/SSX fixes, 999 Grasshopper plug-in rows, SketchUp row fixes, the ADRs and the ledger CI exist only on `w0-*` branches (1,775 commits off this lineage, about 1,009 patch-unique). | 01, 03, 05 | coordinator |
| 5 | **Booleans are weaker than the self-score.** Independent corpus: 136 of 208 (65.4%); near-tangent 31%, curved 0/11, many-holes 0/5. Tolerances are `constexpr` (1e-6, 1e-4), not per-document; about 1,300 raw literals against 81 uses of the tolerance header. | 03 | implementers |
| 6 | **CI does not gate the kernel.** The full kernel suite runs only on `kverify/**` pushes, Linux only. The app build forces kernel tests off, so Windows and macOS never run them. The ledger-check, nightly and Python workflows are not on HEAD. | 03, 05 | coordinator |
| 7 | **Coverage is Rhino-only.** Registered command names match the Register at Rhino 84%, Grasshopper 28%, Blender 17%, AutoCAD 7%, SketchUp 6%, Revit 3%, Archicad 1.6%, Forma 0.5%. BIM is 17 flat mesh generators; IFC export writes proxies only. No Enscape/Outscape rows or code; the renderer is Phong-like with no luminance test. FBX, SKP, RVT, PLN and .blend are missing. | 02, 04 | implementers |
| 8 | **Tests mostly pin our own output.** Of about 3,975 app assertions only 272 contain a decimal number; "implemented" means a command is registered (259 such commands have no test). Strong tests (closed forms, 1e-9 residuals, independent readers) are the minority. | 04 | implementers |
| 9 | **Contamination and licence risk.** (checked) `dino8-app/data/commands.json` holds help text for 501 of 1,071 commands and names McNeel 337 times and Rhinoceros 339 times, which is vendor text inside the repo, against the clean-room rule. Hatch pattern names/descriptions resemble another vendor's file (wording match unconfirmed). (checked) Root `LICENSE` is MIT "Copyright (c) 2022 OpenAI" (Whisper leftover); no root file states the owner's intended licence. GPLv3 LibreDWG is linked into the default app build. The clean-room documents disagree about whether the owner's decision covers decompiling. | 04, 01, 03 | owner, coordinator |
| 10 | **Process drift.** 98.8% of 5,455 commits are authored "Claude"; 26.5% touch PARITY_MAP.md (76% of non-merge commits in October only edited it); no commits 17-22 Nov by author date; `ledger/pace.json` needs 10% by 24 Nov and 100% by 18 Dec against 0% credited. About 9.5% of files are unrelated products (Whisper, MediaSuite, other apps). | 05 | owner, coordinator |
| 11 | **Oracle-kit weaknesses** (Observer side). The Rhino runner records an empty result as a generic RuntimeError (24 of 200 golden records, untriaged); identical-circle intersection stored as one point; `compare.py` counts Dino succeeding where Rhino failed as a divergence, does not reject NaN, check coverage or duplicate ids, and has no tests; its tolerance is max(1e-3 absolute, 1e-4 relative). Two hazards in the data: BLND-BOX-0011 (degenerate chamfer flagged valid) and sphere-offset bounding boxes (OFFS-SOL). The Grasshopper comparer written today rejects an empty golden set and non-finite numbers; the Rhino comparer should get the same. | 05, specs | Observer, reviewer |

## What is real
Strong, evidence-backed pieces exist: the kernel's 2,203 registered tests include closed-form and negative-control checks, `parity/tools` validates the Register schema (0 errors, strict),
the Register's per-program counts match the claim and ids are unique, and the oracle kit's golden intake works (Rhino 200/200 and Grasshopper 264/264 pass the schema check, both runs deterministic).
The gap is not effort: it is that none of it is tied to an external reference, and the work is scattered.

## Recommended order (for the coordinator)
1. Pick and protect a trunk (merge the live kernel, Register and CI branches); make the ledger-check and a kernel build with tests run on Linux, Windows and macOS.
2. Write a Dino 8 case runner for each corpus and load `golden/` in CI; give every Register row an oracle case id or mark it research-only.
3. Replace placeholder acceptance ids (`acc.*`) with real tests; give the 2,978 Blender rows acceptance and tolerance fields.
4. Remove vendor-derived text from `commands.json` and verify the hatch-pattern and i18n files are original; add a root licence that states the owner's intent (the owner decides the licence).
5. Triage the 24 refusal records into classes; fix the Rhino comparer; keep the degenerate-case hazards out of tolerance comparisons.
6. Separate the unrelated products from the repository.
