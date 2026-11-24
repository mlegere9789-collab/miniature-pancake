# Audit 02: the Parity Register, its schema and tools, and the parity maps

Plain analysis of our own work. Audited tree: branch `observer-2026-11-23` at `42dd6499`. All numbers below were computed by
read-only Python scripts (kept in the scratchpad, not in the repo); nothing in the repo was changed except this file.

## 1. What I did and did not cover

Covered:
- All 222 `parity/register/**/*.jsonl` files (10,660 rows) plus the 9 COVERAGE/VERIFY markdown files; `parity/schema/row.schema.json`;
  every script in `parity/tools/` and `parity/tests/`; `dino8-kernel/PARITY_MAP.md` (10,794 lines) and `PARITY_MAP_CURRENT.md`;
  `ledger/legacy.json`, `ledger/waves.json`, `.github/workflows/*`.
- Ran: `validate_register.py --strict` (builtin engine), a dry run of `percent.py --strict` (output to the scratchpad), the 121
  `parity/tests` unit tests and `test_evidence_summary.py`.
- Resolved every `acceptance[].test_id` (7,714 entries, 7,670 unique) against the test sources. "Strict" resolution means: a kernel
  test registered by `RunTest("name", ...)` (the last `::` segment is also accepted) or `D8_EXTRA_TEST(Name)` (registered as
  `extra::Name`), or an app section the runner can select (a `# NAME:` header of at most 6 capitals in `smoke.sh`, or a
  `tests/sections/NN_name.sh` file, name upper-cased; `app/` prefix accepted). 2,211 kernel names and 209 app sections were found.
- Looked at the other branches for Register content, PARITY_MAP variants, CI workflows and evidence files (all 504 remote refs scanned for
  `ledger/evidence`; about 110 carry some Register content).

Not covered / could not check:
- I did not build or run any kernel or app test, so "resolvable" means "a test of that name exists in source", not "it passes".
  The kernel-verify CI results live in GitHub Actions (kverify/kvlite branches), which I cannot read; the repo holds no evidence files.
- `jsonschema` is not installed here, so the validator ran on its built-in engine only.
- I did not check statement truth against vendor documentation (research audit 01 owns that), nor the quality of the individual
  C++/shell test bodies beyond sampling a few linked rows.
- The 134 Python unit tests under `parity/` are not cited by any row, so they were not resolved against rows.
- The PARITY_MAP item counts (473 kernel, 148 app) could not be reproduced mechanically from the map text (see F-12).

## 2. Computed numbers

### 2.1 Register size (matches the claim exactly)

| Program | Rows | jsonl files | alias rows | Rows counted by percent.py |
|---|---|---|---|---|
| Rhino 8 | 1,356 | 19 | 0 | 1,356 |
| Grasshopper | 773 | 15 | 0 | 773 |
| Blender | 2,987 | 85 | 6 | 2,981 |
| SketchUp | 635 | 20 | 1 | 634 |
| Revit | 1,034 | 24 | 16 | 1,018 |
| Archicad | 924 | 21 | 0 | 924 |
| Forma | 186 | 11 | 0 | 186 |
| AutoCAD | 2,765 | 27 | 0 | 2,765 |
| Enscape / Outscape | 0 | 0 | 0 | 0 |
| Total | 10,660 (all ids unique, 0 bad ids) | 222 | 23 | 10,637 |

Rows per program equal the 10,660 claim; ids all match `D8-<PROG>-<DOM>-<nnnn>`. Validator: 0 errors, 0 warnings under `--strict`.

### 2.2 Status, maturity and evidence fields (every row is untouched)

| Field | Distribution |
|---|---|
| `status` | open 10,660 (in_progress 0, verified 0) |
| `tier_now` | None 10,660 |
| `evidence` | null 10,660 (the key is absent from all 2,765 AutoCAD rows) |
| `wave` | unset on 10,660 (waves come from `ledger/waves.json` by factory: wave 1 = 4,778, wave 2 = 3,882, wave 3 = 1,977) |
| `target` | Strong 6,405, Best 4,255 |
| `superiority_tests` | present on 0 rows |
| `ledger/evidence/` | empty (`.gitkeep` only) on this branch and on every remote ref scanned; no `credits.jsonl` or `percent.json` anywhere |
| `percent.py` dry run | "0 of 10,637 Register rows passing (0.00%)"; not-passing reasons: missing test 7,665, no tests 2,972 |

No row claims done, verified or passing. The only "done" claims in the repo are the hand-counted parity maps (F-12).

### 2.3 Acceptance criteria and whether the named tests exist

| Program | Rows | Rows with acceptance | Rows with NO acceptance | Rows whose every test_id resolves | Rows with only unresolvable ids |
|---|---|---|---|---|---|
| Rhino | 1,356 | 1,356 | 0 | 190 (+1 part) | 1,165 |
| Grasshopper | 773 | 773 | 0 | 15 | 758 |
| Blender | 2,987 | 9 | 2,978 | 9 | 0 |
| SketchUp | 635 | 635 | 0 | 0 | 635 |
| Revit | 1,034 | 1,034 | 0 | 3 | 1,031 |
| Archicad | 924 | 924 | 0 | 9 | 915 |
| Forma | 186 | 186 | 0 | 0 | 186 |
| AutoCAD | 2,765 | 2,765 | 0 | 40 | 2,725 |
| Total | 10,660 | 7,682 (72.1%) | 2,978 (27.9%) | 266 (2.5%) | 7,415 (69.6%) |

- Of 7,670 unique test ids, 235 resolve (154 kernel tests, 81 app sections) = 3.1%. The other 7,435 are names no test source contains.
- Rows that can ever become creditable today: 267 of 10,660 (2.5%). Rows that cannot: 10,393 (97.5%).
- Rhino rows with resolvable tests, by domain (of rows): CURVE 43/158, SURF 43/144, SOLID 22/42, XFORM 22/58, SUBD 16/46, SELECT 14/125,
  MESH 13/114, ORG 7/118, ANALYZE 5/54, FILE 4/37, UI 1/23. Zero in ANNOT, LAYOUT, RENDER, SCRIPT, SETTING, VIEW, DIGIT, LIC (437 rows).
- Placeholder id shapes: AutoCAD `T-AUTOCAD-<DOM>-<nnnn>`, Revit `T-D8-REVIT-...`, SketchUp `T-SKETCHUP-...`, Archicad `acc.archicad.<dom>.<nnnn>`
  (generated by `normalize_register.py` when blank), Grasshopper `gh.<dom>.<nnnn>`, Rhino `rhino.<dom>.<cmd>`, Forma `forma.<dom>.<name>`.
- Where tests do exist they are shared coarsely: 20 of 234 linked tests serve more than one row (an app section such as `ROWSCR_BITS` credits 6 rows).

### 2.4 Tolerance, corpus, oracle fields

| Measure | Rows |
|---|---|
| any non-empty `tolerance` object | 2,216 (20.8%) |
| tolerance with a machine-readable number or exactness flag | 906 (8.5%): Grasshopper 730, Forma 122, Rhino 42, AutoCAD 12 |
| tolerance present but text-only (e.g. "<= document absolute tolerance", "as requested") | most of Rhino's 1,341; 65 acceptances carry `{}` and 292 carry only `true` |
| no tolerance at all | 8,444 (79.2%): Archicad, Revit, SketchUp 100%; Blender 2,978 no acceptance; AutoCAD 2,753 |
| `corpus` named | 4,005 rows, 333 distinct strings (Archicad 0, AutoCAD 12, Blender 9) |
| `corpus` pointing at `golden/<program>/...` | 1,669 acceptance entries (Revit, SketchUp); no such directory exists |
| row or acceptance that names an oracle case, a measured quantity or a golden file | 0 (no such field) |
| acceptance prose in `notes` ("Acceptance sketch: ...") instead of a checkable field | 6,132 rows; 2,771 of those mention a number or "exact/identical/closed" |

### 2.5 Provenance and quality markers inside `notes`

| Marker | Rows |
|---|---|
| begins VERIFIED (checked against a help page; NOT the `verified` status) | 755 (Revit 513, Archicad 133, Forma 109) |
| begins CORRECTED | 801 (Archicad 457, Revit 344) |
| UNVERIFIED / PARTIAL | 136 / 39 (Archicad 52, Revit 33, Forma 38+39, AutoCAD 13) |
| REJECTED (kept, still `open`, still in the denominator) | 51 |
| AutoCAD: statement written from the first ~1,300-1,500 characters of the help page only | 2,149 (77.7% of AutoCAD) |
| Grasshopper: "single research pass ... network blocked ... need verification" | 773 (100%) |
| Rhino rows tagged Rhino 9 beta (baseline is Rhino 8) | 197; 166 cite Rhino 9 documentation URLs |
| AutoCAD rows citing the 2025 help (baseline named as AutoCAD 2027 in the legacy map) | 2,329 |
| Blender `source.doc` is the literal string "Blender 5.3 Python API", not a URL | 2,987 |
| `superiority` null | 4,310 (AutoCAD 2,149, Blender 1,991, Forma 170) |
| `target: Best` but `superiority` null (AutoCAD) | 1,987; `target: Best` with no acceptance (Blender) 996 |

### 2.6 Duplicates and templating

- Exact duplicate statements: 0 (all programs). Cross-program identical statements: 0. `validate_register` duplicate-statement warnings: 0.
- Near duplicates (3-gram Jaccard >= 0.8): 2 pairs outside Blender, both legitimate siblings (AutoLISP `>=` vs `<=`; two palette-close commands).
  Blender: 5,403 pairs over 2,127 rows, all template twins ("Add the X node to a geometry node graph ... same result as Blender 5.3");
  475 distinct statement openings for 2,987 rows.
- Rows sharing the same (kind, ref) inside a program: Archicad 79 groups/179 rows, Revit 81/180, Grasshopper 46/92, AutoCAD 31/64, Forma 11/29;
  almost never near-identical (1 pair >= 0.5), so these are one help page split into several rows, not duplicates.
- `alias_of`: 23 rows (Blender 6, Revit 16, SketchUp 1); none cross programs, so one behaviour shared by several baselines
  (arc, boolean, offset, hatch, layer...) is held as separate rows with separate tests.
- Short or label-only statements (< 45 characters): 480 (Rhino median length 58, Grasshopper 64); example: "Name a group.", "Null Item (Sets > List): Test for null".
  Statements with no number, tolerance or exactness word: 5,918 (55.5%). These are heuristic counts, a floor not a measurement.

### 2.7 Parity maps

| Item | Value |
|---|---|
| `PARITY_MAP_CURRENT.md` kernel table | 17 categories, 473 items: present 286, partial 182, missing 5; weight 17.75 |
| `PARITY_MAP_CURRENT.md` app table | 8 categories, 148 items: present 115, partial 25, missing 8; weight 7.75 |
| headline recomputed with partial = 0.5 | kernel 79.31% (map: 79.3), app 86.59% (map: 86.5, truncated) |
| same tables counting only [present] | kernel 59.8%, app 78.6% |
| map scope | Kernel vs Parasolid/ACIS; app vs "Rhino 8 + AutoCAD 2027" only |
| test names cited in PARITY_MAP.md | 758 distinct `Test...` names: 718 registered, 40 not (7 appear in no source file) |
| test names cited in PARITY_MAP_CURRENT.md | 14: 13 registered, 1 is a name prefix |
| map versions across refs | 3 distinct PARITY_MAP.md blobs (63.3%/71.5% older, 78.1%/84.3% header, current); 2 distinct CURRENT blobs (headline 78.4% vs 79.3%) |
| Rhino golden corpus (`parity/oracle/rhino`) | 200 cases, 200 golden records (176 ok, 24 RuntimeError: 21 boolean, 3 blend); every record tolerance 0.001; 0 links to Register rows |
| Rhino command catalog coverage | 1,055 of 1,071 catalog commands are named in a Rhino row; 16 are not (13 SubD commands, Pocket, Lisp, LoadLisp) |

## 3. Findings, ranked by effect on parity

### High

**F-1. The Register measures nothing yet: zero rows credited, zero evidence anywhere.**
Evidence: 10,660 rows `open`/`None`/`evidence: null`; `ledger/evidence` empty on every ref; `percent.py` gives 0/10,637. The only parity numbers in
circulation (kernel 79.3%, app 86.5%) are hand-counted legacy figures that `ledger/legacy.json` itself says are "never credit".
Fix: first credited rows must come from golden-vector comparisons (section 4), written by CI only; publish Register % and legacy % side by side
until the map is retired, as the plan says; stop quoting the legacy numbers as parity with the nine baselines.

**F-2. 97.5% of rows cannot be credited as written.**
Evidence: 2,978 Blender rows have no acceptance (percent.py reason "no-tests"); 7,415 rows name test ids that exist nowhere (2.3).
The placeholders are plausible-looking (`T-D8-REVIT-AI-0001`, `acc.archicad.mep.0004`) and `normalize_register.py` creates `acc.*` ids for blank
ones, so a missing test looks like a named test. The validator cannot see this because `unknown-test` needs `--inventory` and applies only to
in_progress/verified/credited rows; CI never passes an inventory.
Fix: (a) ship an inventory generator (kernel `DINO8_TEST_LIST=1`, `run_sections.sh -l`) and run `validate_register --inventory` on every row that
has a status other than open or a non-placeholder id; (b) make blank acceptance an explicit error for non-Blender programs and a tracked backlog
list for Blender; (c) forbid auto-generated ids in `normalize_register.py` (emit `test_id: null` plus a `planned` flag instead).

**F-3. Rows are not golden-vector-checkable: no oracle, no measured quantity, tolerances mostly prose.**
Evidence: only 906 rows (8.5%) have a numeric/exact tolerance; Rhino's 1,341 tolerances are domain templates ("<= document absolute tolerance",
"degree and knots identical or equivalent"), e.g. the AddGuide row (guide lines) carries a NURBS-knot tolerance, and a lasso-select row carries
"layer, color, name, user text preserved". Rhino has only 20 distinct superiority sentences and 23 corpus strings over 1,356 rows. There is no field
that names an oracle build, case ids or measured quantities, and 0 rows reference the 200-case Rhino golden corpus. The golden tolerance (1e-3) also
differs from the 1e-9 / 1e-12 used by the existing closed-form tests and by the row tolerances.
Fix: add an `oracle` block to the schema (section 4) and require it for any row that is to be credited; one tolerance class per measured
quantity, numeric or an enumerated class (exact, abs, rel, ulp, count, image metric), validated.

**F-4. Blender rows (28% of the Register) describe API names, not behaviour.**
Evidence: 2,987 rows generated from an appendix list (`notes`: "Name is the label as printed in Appendix B; ref is derived"); 2,369 are operators,
480 nodes, 83 modifiers; statements are verb-class templates ("Remove or clear content in object mode with 'Clear Track'; the targeted data is gone
afterwards ... one undo step restores it") that never say what the operator does; 2,978 have no acceptance, 0 numeric tolerance, no corpus; 995 non-alias rows
target Best although none has an acceptance test (they do carry a superiority sentence). Blender has no resolvable test beyond 9 import/export rows.
Fix: group by testable behaviour (a node or operator family with its input/output vectors) rather than one row per name; rows for node and modifier
families are the natural golden-vector unit (inputs mesh/field in, vertex/face/attribute counts and positions out); mark pure UI-chrome operators
as `kind: ui-state` with a state-readback oracle.

**F-5. The Register is split across branches and HEAD is not the best copy of any part.**
Evidence: `origin/main` has no Register at all. HEAD carries the 267 test-link rows but lacks `w0-regfix-sketchup` (+28 SketchUp rows, 151 corrected
SketchUp statements/sources, a measured major-defect rate falling from 40.4% to 12% on a 100-row sample) and lacks the 999 Grasshopper plug-in rows on
`w0-ghplugins` (tip 2026-11-24, 49 plug-in files). Conversely regfix lacks the 267 links. `w0-ghplugins` uses `tolerance` as a string and a
string-typed schema; HEAD's schema says object, so those 999 rows (1,240 acceptance entries) would fail the HEAD validator. The scope file lists
Grasshopper "plus its plug-in ecosystem" at 773 rows, which is the core only. SketchUp at HEAD is the pre-review copy.
Fix: one integration branch that merges regfix-sketchup, ghplugins (after reconciling the schema) and the 267 links; then re-run the validator.
Decide the schema type of `tolerance` once.

**F-6. CI does not run the Register checks on this lineage.**
Evidence: `.github/workflows/` here holds `dino8-app.yml`, `kernel-verify.yml`, `mediasuite-ci.yml`; none mentions `parity`, `ledger`, the validator or
percent.py. A `ledger-check.yml` (validate, percent dry run, "CI only" guards, parity and ledger unit tests) exists on `w0-ledgerfix` and
`w0-regfix-sketchup` but is not an ancestor of HEAD. Nothing stops a hand-edited tier or evidence block.
Fix: bring `ledger-check.yml` into HEAD's lineage and require it.

**F-7. "Best" can never be reached, which caps the target-met percentage at 60.0%.**
Evidence: 4,254 non-alias rows target Best, none has `superiority_tests`; the tool only gives Best when all superiority tests pass, and `target_met`
requires Best for these rows. Ceiling by program: AutoCAD 6.5% (2,585 of 2,765 target Best), Blender 66.6%, Rhino 71.2%, Grasshopper 73.7%, others
>= 95.7%. AutoCAD's 1,987 Best rows with no superiority text look like a template default, not a decision.
Also: "Best" (exceed the baseline) and "parity" are one ladder, so "100% parity" and "100% target met" are different numbers; the SCOPE target is parity.
Fix: report two numbers (parity = Strong tier met; superiority = Best) and make AutoCAD/Blender Best rows opt-in with a superiority sentence and test.

### Medium

**F-8. A passing test does not prove the row.** `percent.py` credits a row when every test id string has result ok. Nothing checks that the test
references the row id, exercises the row's corpus or tolerance, or compares to a baseline value (the linked tests are closed-form oracles:
good independent checks, but not baseline measurements); app sections are coarse (one section, many checks, up to 6 rows); `spec_sha` seals
acceptance, superiority tests and target but not `statement` or `source`, so a credited row can be reworded without voiding the credit.
Fix: require the test's evidence record to carry the row id(s) it covers and the oracle case id; extend the seal to statement/source.

**F-9. Baseline-version drift.** AutoCAD rows cite the 2025 help (2,329 rows) while the legacy map says AutoCAD 2027; 197 Rhino rows are Rhino 9
beta though the target and golden build are Rhino 8 (8.36.26296); Archicad mixes 29 and 30 docs; Revit cites 2027; Blender says "5.3" with no URL.
There is no `baseline` field. Fix: add `baseline: {program, version, build}` per row; tag out-of-scope rows (Rhino 9, newer-than-owned versions) so
they leave the parity denominator.

**F-10. Provenance is hidden in free text and weak rows stay in the denominator.** 51 REJECTED, 136 UNVERIFIED, 39 PARTIAL rows have `status: open`;
the word "VERIFIED" (755 rows) means "checked against a help page", colliding with `status: verified`. 2,149 AutoCAD statements were written from
the first ~1,400 characters of a page ("options further down the page were not reviewed"); all 773 Grasshopper rows say component names still need
verification. Fix: add `status` values (`rejected`, `unverified`, `blocked`) or a `confidence` enum; exclude rejected rows from the denominator.

**F-11. Referenced corpora do not exist.** 1,669 acceptance entries cite `golden/revit/...`, `golden/sketchup/...` (COVERAGE says "planned ... not yet
created"); Grasshopper cites `corpus/gh/...`; Rhino's `corpus` strings are prose. Only `parity/oracle/rhino/golden` exists and is cited by no row.
Fix: make `corpus` a repository path or case-id list and let the validator check it exists.

**F-12. The hand-scored maps are not reproducible and cover two baselines.** Item counts (473 + 148) cannot be rebuilt from the map text (kernel
557 marker tokens, app 180, mixed with narrative); the headline works only if a partial counts as half (present-only: 59.8% kernel, 78.6% app); the
map header (78.1% / 84.3%) disagrees with the CURRENT table (79.3% / 86.5%); there are three published copies; 40 of 758 named tests are not
registered and 7 appear nowhere. No map entry exists for Grasshopper, Blender, SketchUp, Revit, Archicad, Forma or Enscape.
Fix: freeze the maps (they are frozen in `legacy.json`), stop editing percentages, and carry only the Register number forward.

**F-13. Schema is permissive where it matters (details in 3a).**

**F-14. Enscape/Outscape has no rows and no slot.** `program` enum lacks it; no image-metric tolerance class exists outside 75 Rhino render rows.
Fix in section 4.

### Low

- F-15. `test_paths_are_shown_relative_to_the_working_directory` fails on Windows (expects `/`); all other 120 parity tests pass, 1 skipped
  (jsonschema absent). CI is Linux so the gate is unaffected. No test asserts Register-level invariants (every Best row has superiority tests, every
  non-Blender row has acceptance, every test id resolves).
- F-16. `wave` is unset on every row (wave comes from a factory map in `ledger/waves.json`); `links.pain_points/wishes` exist only on Rhino rows.
- F-17. Statement style differs by program (AutoCAD "The program must provide ...", GH "Name (Tab > Panel): description", Blender templates), so
  statements are not comparable across programs (median length from 58 characters in Rhino to 210 in Blender).
- F-18. 16 catalog Rhino commands (mostly SubD symmetry/crease/sculpt variants) have no row; Rhino command coverage is otherwise 98.5% by name.

### 3a. Schema weaknesses (row.schema.json)

1. `acceptance` has no `minItems`; an empty list is valid for status open (2,978 rows). `test_id` is any non-empty string.
2. `tolerance` is a free object: `{}` and `{"exact": true}` are valid; values may be prose; no units, no type of tolerance, no required key.
3. `corpus` is an unconstrained string, not a path or case-id list; nothing checks existence.
4. No `oracle`, `baseline` (version/build), `measure` (what is compared), `confidence`/provenance or `kind: ui-state|geometry|api|image` field.
5. `program` enum has eight values (no Enscape/Outscape); `program` is an array but no row uses more than one and there is no cross-program alias.
6. `status` enum is only open/in_progress/verified: no rejected, unverified, blocked, out-of-scope, duplicate.
7. `additionalProperties: false` on `acceptance` forced the prose "sketch" into `notes` (6,132 rows) where it is unstructured.
8. `superiority` is free text with no pointer to a test; `superiority_tests` is an optional array (0 rows use it).
9. `evidence` requires only run_id and commit once present; `evidence.tests` need not equal the acceptance ids; `spec_sha` excludes statement/source.
10. `source.doc` is any string (Blender uses a label); `source.kind` list lacks api-function, shader, render-setting, file-format.
11. The built-in validator ignores `default`, `minimum`, `uniqueItems`, `allOf` etc. by design (it refuses unknown keywords, which is good), so
    richer rules need jsonschema in CI.

### 3b. Do the tools validate what they claim?

| Tool | Claim | Verdict |
|---|---|---|
| `validate_register.py` | schema, unique ids, alias integrity, tier/evidence consistency, unknown tests | True for structure (0 errors). Unknown-test and duplicate-statement checks are weaker than the docstring suggests: only with `--inventory`, only for non-open rows; duplicates only exact normalised matches inside a program. Does not check corpus, tolerance content, or that open-row ids resolve. |
| `percent.py` | "an agent's own statement counts as nothing"; credit only from machine evidence; flaky, vanished, spec-changed tests block credit | Logic is sound and unit-tested; produces 0.00% on empty evidence; strictness stops at the id string (F-8). |
| `evidence_summary.py` | checks evidence JSONL is internally consistent and matches printed counts | Consistency only (12 self-tests pass); cannot show a test is meaningful. |
| `normalize_register.py` | lossless repair | Lossless for ids/statements; but it converts blank test ids into invented `acc.*` ids (F-2) and moves acceptance prose into `notes`. |
| `catalog/*` | static inventory, "hints, not evidence" | Honestly labelled; not used by any gate. |
| `parity/tests` | 121 tests | Cover tool logic only; 1 Windows-only failure. |

## 4. Per baseline: coverage and what a row needs to become golden-vector-checkable

Generic target shape (add to the schema): `oracle: {program, version, build, case_ids[], measured[], tolerance: {quantity: {class, value, unit}}, kind}`,
plus `baseline`, a numeric/enum tolerance per measured quantity, a `status` that can say rejected/unverified, and a test id that resolves in CI.
A row is golden-checkable when a scripted Observer case can be run in the baseline, produces numbers or flags only, and the Dino 8 side can run the
same case and compare under a named tolerance class.

**Rhino 8 (1,356 rows; strongest).** Coverage: 98.5% of the 1,071-command catalog named; 1,136 command, 159 feature, 61 setting rows. 190 rows already
have real, independently derived closed-form tests (CURVE, SURF, SOLID, XFORM, SUBD, SELECT, MESH). 197 rows are Rhino 9 beta and should leave
scope. Gap: the 200-case golden corpus covers primitives, boolean (70), intersect, offset, extrude, blend, revolve, loft, curve but no row points to a
case; 24 cases failed on the Rhino side. Needs: `oracle.case_ids` on SOLID/CURVE/SURF/MESH rows plus a mapping table row -> family; per-command rows
for VIEW (150), SELECT (125), ORG (118), SETTING (61), SCRIPT (50), ANNOT, LAYOUT need state-readback cases (selection set ids, display-mode
flags, document settings, layer tree) rather than geometry; tolerance classes must replace the "document absolute tolerance" prose.

**Grasshopper (773 rows).** Coverage: core components only (730 `flow`); the plug-in ecosystem is 999 rows on `w0-ghplugins`, unmerged and schema
conflicting (F-5). Best-formed rows (730 numeric tolerances such as `abs: 1e-12`, an acceptance sketch with a worked value) but every row says
the component name still needs checking and the test ids `gh.<dom>.<nnnn>` do not exist (15 do, via `FR*` flow-row sections). Component I/O is the
ideal golden unit. Needs: per row `oracle.case_ids` of (input data tree -> output data tree) pairs, tree shape included in the comparison, tolerance
class per output type (number, point, curve, mesh), and the component identity (GUID/nickname/tab) checked against the owner's install.

**Blender (2,987 rows).** Coverage: API-name inventory (operators 2,369, nodes 480, modifiers 83, constraints 29, editors 17); no behaviour, no
acceptance, no tolerance (F-4). Rendering behaviour (Cycles/EEVEE, physics, sculpt, simulation, add-ons) is not represented as rows. Needs: families
of rows keyed to testable behaviour; for nodes and modifiers an input mesh/field -> output counts and attribute checksums case; for renders the
image-metric class (mean luminance, histogram distance) at fixed seed and sampling; GPL rule applies (behaviour evidence only, no code port).

**SketchUp (635 rows).** Coverage: 418 help pages, 136 Ruby API rows, extension/C-API rows; no tolerances, no resolvable tests, `golden/sketchup/*`
absent; HEAD lacks the 151 corrections and 28 added rows (F-5). Needs: Ruby-API-scripted cases (face/edge/group counts, bounds, transformation matrices,
`Geom` method results) which are cleanly numeric; modelling-tool rows (push/pull, follow-me, intersect) need geometry-signature oracles (area, volume,
face count, bounding box) like the Rhino kernel families; scenes/styles/layout rows need state readback.

**Revit (1,034 rows; 18 release-tracking rows, 16 of them alias rows).** Coverage: broad by tab (architecture 151, structure 106, systems 99), verified/corrected against
2027 help pages for most rows; no tolerance, `golden/revit/*-sample` corpora absent, tests are `T-<rowid>` placeholders. Needs: API-scripted cases on a
fixture model (element counts, parameter values, geometry of created elements, schedule contents, IFC export entity counts); a fixture-model plan
that is ours (not vendor sample content); numeric tolerance for geometry and exact match for parameters and counts.

**Archicad (924 rows).** Coverage: 21 domains; 36 REJECTED and 52 UNVERIFIED rows (GDL, JSON API, C++ API pages were unreachable); no tolerance, no
corpus, 915 invented `acc.*` ids. Needs: API (Python/JSON) scripted cases on a small project; GDL rows as script -> element-geometry numbers; IFC
rows as export entity/property counts; remove rejected rows from the denominator; split the 168 toolbox rows into measurable per-tool outcomes.

**Forma (186 rows).** Coverage: well structured with public validation references (solar position, BESTEST, ISO 9613-2, UTCI, wind benchmarks) that give
numeric oracles without running the vendor product (122 numeric tolerances, 206 test ids that are not yet written). 38 UNVERIFIED, 39 PARTIAL. Needs:
for analyses, input scenario -> numeric result vs the cited public reference (this is the easiest golden source in the set); for cloud/collab/extension
rows (HTTP API, SDK) a recorded request/response contract.

**AutoCAD (2,765 rows).** Coverage: largest programme after Blender; 563 AutoLISP, 264 .NET, 239 DWG fidelity, 201 command-line rows; every row has
an acceptance sketch in `notes` but only 12 have a tolerance block and 40 resolvable tests (CDBLK and the LISP sections); 2,149 statements were written from
a page opening; 93% target Best with 1,987 lacking superiority text; docs are 2025. Needs: command cases scripted via the public automation surface,
DXF/DWG readback of entity lists (group codes, coordinates), AutoLISP function return values (deterministic, ideal for exact-match vectors), system
variable get/set round trips; versions pinned to the owner's copy; Best targets reduced to Strong unless justified.

**Enscape / Outscape (0 rows).** Nothing exists: no rows, no program value, no render-measurement tolerance classes beyond the 75 Rhino render rows
(`image_diff: mean abs pixel diff <= 2/255`). When the rows are built they should use a render-case record: fixed scene file + camera + settings + seed
-> outputs `{resolution, mean luminance, luminance histogram (bins), per-channel mean, time ms, memory}`; tolerance classes `exact` (resolution,
frame count, format), `abs` (mean luminance), `distance` (histogram), `bound` (time as a ceiling); a row per setting group, light type, material
class, camera/projection, output, and the live-link workflow; no assets, shaders, UI text or icons from the vendor in the repo (SCOPE rule).
Add `enscape` (or `outscape`) to the `program` enum and a `render-quality` factory before the first row is written.

## 5. Suggested order of repair (effect on parity first)

1. Merge the diverged Register lineages and the CI workflow (F-5, F-6) so there is one Register and one gate.
2. Extend the schema with `oracle`, `baseline`, provenance status and typed tolerance (F-3, F-9, F-10, 3a); run the validator with an inventory.
3. Link the 200 Rhino golden cases to rows and run the first golden comparison to produce the first credited rows and the first real parity number.
4. Regroup Blender into behaviour families and give every non-Blender row a resolvable or explicitly planned test (F-2, F-4).
5. Split the parity number into parity (Strong) and superiority (Best) and report the legacy percentages only as history (F-7, F-12).
