# SketchUp Register: re-fix report

Branch `w0-regfix-sketchup`, based on the independent review (`parity/register/review/REVIEW_sketchup.md`). Register: 635 rows reviewed, 663 rows now (28 added). `validate_register.py --register parity/register --strict` reports 0 errors.

## What was done

1. `parity/tools/apply_fixes.py` applies a fixes file ({id, field, old, new, reason}; dotted field paths such as `source.ref`) all-or-nothing and refuses when `old` does not match the current value, when the id or field is missing, or on a second run (reported as "already applied"). Tests: `parity/tests/test_apply_fixes.py`. The validator no longer reads `review` directories (`regio.register_files`), test `test_review_directories_are_not_read_as_rows`.
2. The 72 review corrections were applied (`fixes_sketchup.jsonl`); later passes are in `parity/register/review/fixes_regfix_sketchup.jsonl`, `fixes_regfix2_...`, `fixes_regfix3_...`, `fixes_regfix4_...` (each applied once, in that order, with the exact old values).
3. Re-sourcing of the 50 rows that cited generic pages (or were named by the review):
   - CAPI-0001..0016: the C API reference exists on an allowed host (`extensions.sketchup.com/developers/sketchup_c_api/sketchup/`, struct and header pages). Statements now name the functions listed on the page. CAPI-0014 (LayOut document API) stays UNVERIFIED: the reference's LayOut section is not reachable.
   - EXT rows with a SketchUcation store page (`sketchucation.com/pluginstore?pln=<name>`): statements reduced to what the store page says. Rows whose only source is the script-rendered Extension Warehouse, a vendor site or a forum (hosts outside the allowed list were not fetched) are marked `UNVERIFIED: <reason>` and stay open: 45 of the 66 top-extensions rows.
   - EDIT-0032, EDIT-0037, ORG-0011, RUBY-0133, ECO-0013 re-sourced or rewritten; ECO-0013 is an internal Dino acceptance gate and says so.
4. Row-by-row re-review of c-api-sdk, ai-tools, extension-platform and top-extensions against their pages (112 rows). Method counts in ruby-api now say "non-deprecated" where the page lists deprecated methods.
5. Boilerplate: all 140 `superiority` sentences that were shared by ruby-api rows were replaced by sentences built from the row's class and its listed methods (distinct for every row; the most common sentence now occurs once). INFER rows: 20 of 23 Best targets lowered to Strong (no row-specific reason); four keep Best with a written, testable justification in `notes` (INFER-0002, 0009, 0018, 0021); INFER-0024 was already Strong.
6. 28 rows added for documented features that had no row: RUBY-0135..0146 (Geom::Point3d, Vector3d, Point2d, Vector2d, Bounds2d, OrientedBounds2d, Transformation2d, Layout::LockedEntityError, LockedLayerError, the Layout module, Sketchup::Http, the 16 observer classes), LAYOUT-0061..0062, COLLAB-0019..0028, ORG-0032..0033, IO-0024, EDIT-0040. Review items 15 (Managing Changes and Updates, already cited by LAYOUT-0046/0047) and 25..28 (dynamic-component articles, already rows DYNCOMP-0004/0005/0006/0015/0018) turned out to have rows; they were replaced by the four 3D Warehouse guideline pages (testing, stray geometry, axes and origin points, orienting faces) and a Viewer end-of-support row.

## New independent sample: `random.Random(18).sample(rows sorted by id, 100)` of the 663 rows

Every sampled row was read against its cited page (curl/WebFetch on allowed hosts only; Ruby class rows also had the method count recomputed from the live class page). Verdicts per row: `parity/register/review/sample18_sketchup.jsonl`.

| Verdict | Rows |
|---|---|
| OK | 68 |
| Minor (wording, one incidental detail) | 6 |
| Label-only statement | 2 |
| Major (claim not on the page, wrong, or wrong page) | 12 |
| UNVERIFIED (page not reachable on an allowed host; marked in `notes`) | 12 |

Major-defect rate: **12/100 = 12%** (14% if the two label-only rows count). Before: 69/171 = 40.4% on the review's sample. The 12 unverified rows are not counted as defect-free: treating them all as defects the upper bound is 26%. The majors were found and then fixed (`fixes_regfix4_sketchup.jsonl`); RUBY-0133 was a miscount that my own earlier pass introduced (48 instead of 49).

Four worst domains in this sample: top-extensions 13 rows (0 major, 11 UNVERIFIED), extension-platform 3 (0 major, 1 UNVERIFIED), ai-tools 1, c-api-sdk 0 (too few to bound them). The criterion "second check shows under 15% major defects in the four worst domains" is met only for the defect count: top-extensions is mostly unverifiable, not verified correct.

## Limits stated plainly

- The fixer drew and graded this sample; it is not independent of the fixes. The draw is a different seed (18) from the review's (8) and was made after all fixes. A second independent reviewer should repeat it.
- The remaining 78+ rows outside the four re-reviewed domains had only the review's 64 fixes plus the sample's; the review's estimate that the rest of the file has the same defect rate was only partly retired (12% in a random sample).
- 50 rows are UNVERIFIED (45 top-extensions, CAPI-0014, ECO-0013, EDIT-0032, INFER-0020, ORG-0006). They stay `open`; no acceptance test should be derived from their statements until a page is found.
- 32 rows still have statements under 45 characters (import-export 8, interface-preferences 5, top-extensions 4, collaboration-cloud 4, others); not all are labels. Not rewritten.
- SketchUp Style Builder is no longer bundled and is out of support (STYLEB rows); the target stays Strong until the coordinator decides.
