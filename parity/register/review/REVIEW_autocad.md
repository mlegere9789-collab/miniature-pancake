# Review of the AutoCAD register (`parity/register/autocad`)

Reviewer: independent, did not write the rows. Date: 2026-11-14. Branch: `w0-review-b`.

**Verdict: ACCEPT WITH FIXES.** Page fidelity is good, but the `target` field is not usable as it stands (blocking), the factory map sends whole domains to the wrong wave, and there are duplicate and index-driven filler rows. If the target field is not redone before the register is used for weighting or percent-done, the verdict is REJECT.

## Sample

- Random sample: 150 rows of 2,765 (26 original rows, 124 rows added in the verification pass, i.e. ids above each domain's old maximum).
- Top-up: 150 more rows so every domain file has at least 10 (script checks only). Total 300 rows, 295 distinct pages.
- Pages read: 294 of 300 rows resolved; 6 top-up rows (toolset books for Mechanical, Map 3D and Electrical, LT) I could not map to a static page and did not read. All 150 random-sample rows were read.

How the pages were read. The help site is a script-rendered app whose page-lookup service is blocked from this environment, so the `?guid=` links open "Page Not Found" here. I read the static files instead: `/cloudhelp/<release>/ENU/<book>/files/<id>.htm`, with the book folder taken from each product's `data/toctree.json` (which also gives the page title, so I could check `source.ref` against the page title for every row that is in a table of contents). Pages were fetched with a plain browser User-Agent; `www.autodesk.com`, `blogs.autodesk.com` and `forums.autodesk.com` still refused (HTTP 403 or no connection). Sampling: `random.Random(8).sample` over the sorted ids, then topped up to at least 10 rows from every domain file ("top-up" rows). Rates below use the random sample only; top-up rows were checked by script only (reachable, copy overlap, factory rule, Best rule) and are reported separately.

Criteria (a row can fail several):
- **A** page reachable and relevant (the cited page opens, and its title matches `source.ref`).
- **B** statement describes what the page documents, in a testable way, without copying (content not on the cited page, or a run of 8+ identical words, counts as a defect).
- **C** `target` sensible: Best must be rare and justified by a `superiority` text.
- **D** `factory` or `domain` right.
- **E** statement is a testable outcome, not just a label or a note about a release.
- **F** duplicate of another row (same behaviour, same page).


## Results on the random sample (n = 150)

| criterion | base-sample count | rate | row ids |
|---|---|---|---|
| A page unreachable/irrelevant | 0 | 0/150 = 0.0% (95% CI 0-2%) | none |
| B statement not supported by the cited page / copied | 11 | 11/150 = 7.3% (95% CI 4-13%) | `DIM-0016`, `DWGFID-0018`, `LAYOUT-0022`, `MODIFY-0029`, `NET-0017`, `NET-0101`, `NET-0113`, `SCR-0008`, `SOLID3D-0033`, `TEXT-0010`, `TEXT-0023` |
| C Best without justification | 111 | 111/150 = 74.0% (95% CI 66-80%) | `BLOCK-0049`, `BLOCK-0055`, `BLOCK-0070`, `BLOCK-0082`, `BLOCK-0105`, `BLOCK-0125`, `CLOUD-0001`, `CLOUD-0015`, `CLOUD-0041`, `CMDLINE-0041`, `CMDLINE-0070`, `CMDLINE-0076`, `CMDLINE-0112`, `CMDLINE-0132`, `CMDLINE-0133`, `CMDLINE-0136`, `CMDLINE-0150`, `CMDLINE-0173`, `CMDLINE-0181`, `CMDLINE-0183`, `CONSTR-0048`, `CONSTR-0059`, `CONSTR-0060`, `DRAW-0057`, `DRAW-0058`, `DWGFID-0047`, `DWGFID-0085`, `DWGFID-0106`, `DWGFID-0165`, `DWGFID-0176`, `DWGFID-0180`, `DWGFID-0183`, `DWGFID-0198`, `DWGFID-0215`, `EXPRESS-0050`, `EXPRESS-0078`, `HATCH-0031`, `HATCH-0032`, `LAYER-0036`, `LAYER-0051`, `LAYOUT-0071`, `LISP-0069`, `LISP-0075`, `LISP-0087`, `LISP-0116`, `LISP-0127`, `LISP-0131`, `LISP-0133`, `LISP-0169`, `LISP-0190`, `LISP-0291`, `LISP-0300`, `LISP-0305`, `LISP-0310`, `LISP-0314`, `LISP-0344`, `LISP-0362`, `LISP-0366`, `LISP-0367`, `LISP-0380` ... |
| D factory or domain wrong | 22 | 22/150 = 14.7% (95% CI 10-21%) | `CLOUD-0001`, `CLOUD-0015`, `CLOUD-0041`, `CMDLINE-0041`, `CMDLINE-0112`, `CMDLINE-0132`, `CMDLINE-0133`, `CMDLINE-0136`, `CMDLINE-0150`, `CMDLINE-0173`, `CMDLINE-0181`, `CMDLINE-0183`, `CONSTR-0048`, `DWGFID-0047`, `DWGFID-0085`, `LAYOUT-0022`, `RENDER-0013`, `RENDER-0027`, `RENDER-0030`, `RENDER-0056`, `RENDER-0062`, `TOOLSETS-0012` |
| E label or not testable | 5 | 5/150 = 3.3% (95% CI 1-8%) | `NET-0101`, `NET-0232`, `NET-0260`, `RENDER-0062`, `SOLID3D-0122` |
| F duplicate | 3 | 3/150 = 2.0% (95% CI 1-6%) | `CMDLINE-0021`, `DRAW-0032`, `DRAW-0057` |

Rows with at least one defect: 124/150 = 82.7%. Without C: 38/150 = 25.3%.

| group | n | B | C | D | E | F |
|---|---|---|---|---|---|---|
| original rows | 26 | 9 (34.6%) | 0 | 2 | 0 | 2 |
| rows added in the verification pass | 124 | 2 (1.6%) | 111 (89.5%) | 20 (16.1%) | 5 (4.0%) | 1 |

Top-up rows (script checks only, n = 150): pages not read 6 (4.0%), Best without superiority 78 (52.0%), factory rule hits 12 (8.0%), verbatim-copy runs 0.

B is a lower bound. I read every sampled statement against the first part of its page and checked specific claims on about 45 rows (all the LISP rows I opened were accurate; the 11 B rows are the ones where a named command, option or file type is missing from the cited page). Examples with the page text checked: `DIM-0016` (frame shapes, label and rate are on a dialog page, not the DIMINSPECT page), `DWGFID-0018` (the three PROXY variables are not on the page), `LAYOUT-0022` (SteeringWheels on a ViewCube page), `SOLID3D-0033` (five other commands bundled), `TEXT-0010`, `MODIFY-0029`, `NET-0017` (the cited page is a one-line class stub), `NET-0101` and `NET-0113` (stub section pages). Nine of the 26 original rows fail B even though their notes say "Verified"; these are the multi-command rows.

## Systematic problems

1. **Best is the default.** 2,585 of 2,765 rows (93.5%) are Best. All 598 original Best rows carry a `superiority` text; all 1,987 Best rows added in the verification pass carry none (0 of 1,987). Even the originals are 97% Best (598/616), so Best is not rare. Three rows whose notes say REJECTED (`DRAW-0024`, `DRAW-0027`, `PLOT-0015`) are still Best. Proposed rule: added rows without a superiority text become Strong; keep Best only where a measurable superiority is written.
2. **Index-driven filler.** 45 rows exist only to close a palette (one per `*CLOSE` command; 41 are Best), e.g. `BLOCK-0049`, `CLOUD-0014`, `CMDLINE-0064`, `RENDER-0038`. They are testable (the palette closes) but add nothing a command row for the palette does not. Domain `net` has one row per JavaScript API class or enum (264 rows, 243 added, 234 Best with no superiority), e.g. `NET-0232` (constructor exists and derives from another class).
3. **Added rows were written from the first 1,300 to 1,500 characters of each page** (all 2,149 say so in `notes`). Where I checked deeper the statements held, but option-level coverage past the opening is not reviewed, and a handful of pages are stubs (`NET-0017`, `NET-0101`, `NET-0113`).
4. **Factory map sends domains to the wrong wave.** Only 5 of the 16 factories are used. All 67 `render` rows are `direct-modeling` (should be render-anim); all 78 `cloud` rows are `interop-migration` (should be collab-cloud; the domain also holds object counting, `CLOUD-0015`, which is local drafting); about 80 `cmdline` rows are ribbon, CUIx, Quick Access, tool-palette and double-click customisation under `cad-2d` (should be ux-access-i18n, wave 3). `ledger/waves.json` puts these in waves 1, 1 and 1 instead of 2, 2 and 3.
5. **Duplicates.** 30 groups (62 rows) cite the same page for the same command in different domain files. About 13 pairs are true duplicates: `CMDLINE-0021`/`MODIFY-0026` (UNDO), `DRAW-0032`/`MODIFY-0023` (PEDIT), `BLOCK-0032`/`TEXT-0017` (ATTSYNC), `DIM-0008`/`DRAW-0027` (DIMARC), `DRAW-0015`/`TEXT-0021` (REVCLOUD), `MODIFY-0015`/`SOLID3D-0052` (ARRAYPATH), `MODIFY-0043`/`SOLID3D-0038` (3DMOVE), `SNAP-0029`/`SOLID3D-0054` (MASSPROP; one sits in the snap domain), `SOLID3D-0046`/`XREF-0011`, `CMDLINE-0014`/`SCR-0008` (ACTRECORD), `CMDLINE-0022`/`MODIFY-0034` (SELECT). Concept rows repeat command rows: `DRAW-0057` (About Donuts) vs `DRAW-0010`, `DRAW-0056` vs `DRAW-0005` (circles). `LISP-0069` and `LISP-0392` carry the same ref.
6. **Domain mix-ups** seen in the sample: `CONSTR-0048` (action recorder under constraints), `LAYOUT-0022` (navigation under layout), `DWGFID-0047` CLOSEALL and `DWGFID-0085` RENAME under file fidelity.
7. **Style.** 633 statements (23%) begin "Dino 8 must", others "The program must provide", others are bare imperatives. `WEB-0008` puts an editorial note into the requirement ("this page documents the web app"). `NET-0260` requires an obsolete call "to be accepted without crashing".
8. Some rows name a command but cite a concept page (`BLOCK-0015` BPARAMETER, `BLOCK-0016` BACTION and `BLOCK-0018` BCPARAMETER cite dynamic-block overviews): relevant, but not the command page.

What is good: page identity is right. Of 1,605 rows whose page is in the AutoCAD table of contents, only 12 have a ref that shares no word with the page title (and 7 of those are one-word AutoLISP names such as `if` and `or`). I found no verbatim copying above 7 words in the sample (script overlap 0 rows of 150). Statements are specific and, where I checked, correct; I did not run or validate the acceptance sketches.

## Rows added in the verification pass (extra checks)

- Title-only rows: none found. The shortest statements (44-90 characters, 54 rows) all state an outcome ("close the Count palette"), so the problem is value, not form (see 2).
- Near-duplicate statements inside the added rows: 11 pairs with token overlap above 0.45, all sibling commands (`RENDERENVIRONMENTCLOSE`/`RENDEREXPOSURECLOSE`, `TRIM`/`EXTEND`, `COUNTNAVNEXT`/`COUNTNAVPREV`); legitimate but could be merged.
- Added-row error rate on B is low (2/124 in the sample, 1.6%), on C it is 89.5%, on D 16.1%.

## Unreachable pages

Cited page not read: 6 top-up rows (`BLOCK-0088` LT, `TOOLSETS-0006`, `-0007`, `-0008` Mechanical, `TOOLSETS-0013` Map 3D, `TOOLSETS-0014` Electrical). Before I used the table-of-contents route, 41 of 300 pages looked unreachable (ObjectARX, web app, mobile app); they are all reachable that way. The three REJECTED rows cite pages for commands that are legacy, never existed or were removed (see their notes).

## Fix file

`parity/register/review/fixes_autocad.jsonl` (143 proposals): target Best to Strong for the 111 sampled Best rows with no superiority, 22 factory/domain moves, 6 narrowed statements, 4 `alias_of` links. The same rules should be applied across the register by script; I did not touch the rows. **Note:** `validate_register.py --strict` scans every `*.jsonl` below `parity/register`, so these fix files (which are not rows) make it report errors; the validator needs to skip `review/` or the fix files must be moved.

## Missing coverage (40 documented items with no row)

Independent of VERIFY.md, which lists 928 uncovered system-variable pages: 1,218 of the 2,393 topics in the AutoCAD table of contents are not the source of any row (404 of them "About" concept topics), including whole areas: the dialog-box language (DCL), the Visual LISP IDE (editor, debugger, compiler, project files), AutoLISP language semantics, extended data, selection-set filters, plotter driver configuration and the DXF file structure. Items:

1. Designing Dialog Boxes (DCL) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-F4A63A70-EB72-4F7D-A90C-3C5ABD6864A9
2. Action Expressions and Callbacks (DCL) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-78FDD902-C159-4654-8E3B-228E344A78E8
3. Tile Definitions (DCL) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-6F7C60D6-ADCC-48B0-83F9-A69F88D87436
4. Debugging Programs (Visual LISP IDE) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-CBD51D58-C822-42FD-89FE-5E3432B6CC44
5. Breakpoints (Visual LISP IDE) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-9ADDB1E6-4759-476F-808E-B50984EAC019
6. Compiling and Linking Programs (Visual LISP IDE) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-B5390E1B-3279-4B86-87D8-09D5EE958500
7. Making Application Modules (Visual LISP IDE) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-2B4C9290-CFE3-4430-B543-61ED38A7C170
8. Using the Console Window (Visual LISP IDE) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-02140081-3C99-403B-A624-92535F4F4790
9. Visual LISP Projects (Visual LISP IDE) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-CEBA92E7-EE9A-4422-A650-232801C45F15
10. Extended Data - Xdata (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-A94BC605-5517-437F-A6FE-D3EB8116A01A
11. Selection Set Filter Lists (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-7BE77062-C359-4D01-915B-69CF672C653B
12. Namespaces (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-4E949651-A881-4E0F-9DBA-A4645FE473B3
13. Xrecord Objects (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-FA5F2E08-24F5-4947-A470-6CA84E404F2A
14. Dictionary Objects and Entries (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-24E52678-513E-4322-8070-B23C8945DC3D
15. Safearrays (AutoLISP/ActiveX) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-986313E1-0AEB-41DA-BC8B-093437F42B6A
16. Transient and Persistent Reactors (AutoLISP/ActiveX) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-0A632CD0-99B8-4E7E-A958-A0723E50ABB5
17. Wild-Card Matching (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-E8328DF6-43FF-47AA-8524-72B962A9D552
18. Coordinate System Transformations (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-0F0B833D-78ED-4491-9918-9481793ED10B
19. Using the *error* Function (AutoLISP) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-2AC47A3C-7E80-414D-9134-EABA0003193F
20. Binary DXF Files (DXF) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-FC1C3C69-DBC2-49E4-893A-000D6538C0FE
21. Slide Library Files (DXF) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-DE646494-D20B-4837-B01D-996983B226D9
22. Arbitrary Axis Algorithm (DXF) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-E19E5B42-0CC7-4EBA-B29F-5E1D595149EE
23. Using the Plotter Configuration Editor - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-907BB938-AC02-4A64-BAEF-1E5F14B197AD
24. PDF Driver Custom Properties - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-0CE58C07-9828-4D58-B65E-E22ABD24ED77
25. Creating or Modifying a DWF6 Configuration File - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-232B462B-9804-425D-AB19-024E4772DA65
26. About Sheet Set Manager for Web - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-1690E514-A7B0-44EB-8EB0-E1C897D78780
27. About Markup Import and Markup Assist - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-0CD6B2CE-EAF0-478B-B39B-F4821F9DC91D
28. About Memory Tuning - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-4F5F6FBE-ABD0-473F-B065-2BF4C7FD242B
29. MBUTTONPAN (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-07122CB7-EC98-4D5F-B3BE-16EA6FB54CED
30. INSBASE (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-B0A82E39-CB83-484E-95CB-B35D8114B947
31. XREFTYPE (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-8BDF8BEE-0207-413F-96D9-5B067DAF6E60
32. REFPATHTYPE (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-CCC90130-7575-4901-8905-FBB4553DAA3F
33. XEDIT (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-5DA336A1-59BE-4E1E-899C-9349724414E0
34. WORKINGFOLDER (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-45D59F0E-2F4A-4C97-A013-76E8A12D2408
35. TEMPOVERRIDES (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-77C5E101-5641-4630-AB50-6D97A6FD8D24
36. MEASUREINIT (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-5F81863D-FAA6-4837-9071-F91C3E1A1E26
37. FRAMESELECTION (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-4DD5058A-4194-4568-A03E-81CE9BC9C018
38. XFADECTL (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-8B353FAC-0AC4-43AC-8E80-18999A4A7AA1
39. ACTPATH (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-E63ED007-A45B-46EE-9777-3665AA593B6C
40. STEPSIZE (System Variable) - https://help.autodesk.com/view/ACD/2025/ENU/?guid=GUID-C181AF04-E45C-4468-B924-3FB4CA858664
