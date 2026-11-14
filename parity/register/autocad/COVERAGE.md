# AutoCAD Parity Register: coverage

> **Status update (verification pass):** this file describes the original 616-row batch. The register has since been verified against the AutoCAD 2025 help site (help.autodesk.com was reachable in that pass) and extended; see `VERIFY.md` for current counts, rejected rows and unreachable pages. The statement below that help.autodesk.com is not reachable no longer holds.


Job `autocad` (Wave 0, HORDE4). Branch `w0-autocad`, from `cand2`. Program id `autocad`, ids `D8-AUTOCAD-<DOMAIN>-<nnnn>`, files `parity/register/autocad/<domain>.jsonl`, validated against `row.schema.json` (draft-07, 0 errors, 0 duplicate ids).

Total rows: **616** (plan estimate: about 600). Every row is `status: open`, `tier_now: None`; no credit is taken from existing code.

## Rows per domain

| domain | rows | factory | rows with a registry hint |
|---|---|---|---|
| block | 34 | cad-2d | 20 |
| cmdline | 24 | cad-2d | 10 |
| constr | 27 | cad-2d | 5 |
| dim | 39 | drafting-docs | 22 |
| draw | 40 | cad-2d | 27 |
| dwgfid | 40 | interop-migration | 15 |
| hatch | 22 | drafting-docs | 8 |
| layer | 27 | cad-2d | 12 |
| layout | 32 | drafting-docs | 20 |
| lisp | 23 | scripting-api-ai | 9 |
| modify | 49 | cad-2d | 45 |
| net | 21 | scripting-api-ai | 1 |
| plot | 22 | drafting-docs | 10 |
| scr | 10 | scripting-api-ai | 3 |
| sheetset | 12 | drafting-docs | 7 |
| snap | 33 | cad-2d | 28 |
| solid3d | 56 | direct-modeling | 44 |
| sysvar | 38 | cad-2d | 9 |
| text | 30 | drafting-docs | 19 |
| toolsets | 18 | drafting-docs | 13 |
| xref | 19 | interop-migration | 4 |
| **total** | **616** | | **331** |

## What the `notes` field means

Each `notes` value lists command names that the cand2 app registers (from `Reg(e, "Name", ...)` calls under `dino8-app/src`, cross-read with `dino8-app/AUDIT.md` and `dino8-app/RHINO8_KILLER_AUDIT.md`) and that look related to the row. It is a hint only, not credit. The registry uses Rhino-style names, so the hint names are the nearest Rhino-style commands, and they usually cover only part of the AutoCAD behavior (options, prompts, associativity, DWG storage). `no matching registration found` means no related command was found.

The audits record these AutoCAD-oriented additions in the app: DwgCompare/XrefCompare, activity log and snapshots, SmartBlockDetect, Standards/CheckStandards, SheetSet commands (one PDF per sheet, console only), Centermark/CenterLine, dynamic block visibility/lookup/stretch, DataLink (CSV only), 2D constraint solver (Fixed, Coincident, Parallel, Tangent, Concentric, Equal), basic Mechanical/MEP/Electrical components, and DWG/DXF read-write through LibreDWG. None of that has been checked against the row acceptance sketches.

## Sources and their limits

- Rows were written from public vendor documentation topics (AutoCAD 2025 Help command reference and system variable reference, AutoLISP, ObjectARX and .NET references, the DXF reference) and the public Open Design Alliance DWG specification, in the author's own words. No competitor source, leaked or decompiled material was used.
- `source.doc` holds the documentation root plus the topic name (`<root> (topic: <command>)`). **help.autodesk.com is not reachable from this cloud session** (host did not resolve), so deep GUID links could not be fetched or confirmed. Roots used: https://help.autodesk.com/view/ACD/2025/ENU/ and https://help.autodesk.com/view/OARX/2025/ENU/; DWG spec: https://www.opendesign.com/files/guestdownloads/OpenDesign_Specification_for_.dwg_files.pdf (URL not fetched here). Resolve the topic names to page GUIDs once the host is allowed, or use the owner's licensed-copy harness (plan 4.5).
- A web search confirmed the existence of the AutoCAD 2025 Help site, the Commands and System Variables list and the "New commands and system variables" page; nothing else was verified online. Behavior statements are from documented behavior as known to the author and need the review pass of plan 4.4 step 2.

## Acceptance sketches

`acceptance[].test_id` is `T-AUTOCAD-<DOMAIN>-<nnnn>` and `acceptance[].sketch` is an outline of a scripted scenario with the expected value (closed form where one exists). These are sketches, not tests: no corpus or tolerance has been bound, and a few sketches need an independent oracle chosen before use (stated closed forms were hand-checked).

## Known gaps (not covered, for a follow-up batch)

Express Tools; Autodesk cloud features (Docs, Share, Mobile/Web app, Markup Import/Assist, Activity Insights, Smart Blocks, Trace); Inventor/Revit/Navisworks interop; Dynamic Block deep parameter variants beyond the listed ones; Civil 3D and Plant 3D beyond core drafting; ribbon and palette layout specifics; per-version release deltas (2020 to 2027); localisation variants.

## Regeneration

Rows are generated from a compact data set kept outside the repo (scratchpad); the JSONL files are the artifact of record.
