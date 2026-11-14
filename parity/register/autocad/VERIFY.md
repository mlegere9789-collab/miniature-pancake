# AutoCAD register: verification pass (VERIFY)

Branch `w0-autocad2`. Scope: `parity/register/autocad/` only. Every `source.doc` in the register now points at a specific public help page (or is explicitly marked UNVERIFIED in `notes`).

## Counts

| measure | count |
|---|---|
| rows checked (original register) | 616 |
| confirmed as written (verdict ok) | 366 |
| corrected (statement, ref or kind changed to match the page) | 234 |
| rejected (REJECTED note, row kept) | 3 |
| left unverified (UNVERIFIED note) | 13 |
| rows added | 2149 |
| total rows now | 2765 |
| help pages deliberately given no row (skipped) | 42 |
| command and Express Tool pages found in help | 1019 |
| of those, now referenced by a row | 1015 |

Original rows: 616, ids and files untouched (no row deleted, no id reused or renumbered). Added rows continue each domain numbering after its old maximum. Six new domain files hold the new domains: `express`, `web`, `mobile`, `cloud`, `render`, `ptcloud`.

### Rows added, by source of the page

| group of help pages | rows |
|---|---|
| commands, Express Tools and web-app command pages | 734 |
| AutoLISP, Visual LISP and ActiveX-library function and reference pages | 506 |
| User's Guide concept (About) topics | 310 |
| Developer-guide sections (ObjectARX, .NET, ActiveX/VBA, JavaScript API, Sheet Set object) | 244 |
| Customization and administration topics | 140 |
| DXF reference sections (entities, objects, tables, header, blocks, classes) | 121 |
| AutoCAD web and mobile app help topics | 94 |

### Rows per domain

| domain | original | added | total |
|---|---|---|---|
| block | 34 | 105 | 139 |
| cloud | 0 | 78 | 78 |
| cmdline | 24 | 177 | 201 |
| constr | 27 | 34 | 61 |
| dim | 39 | 33 | 72 |
| draw | 40 | 21 | 61 |
| dwgfid | 40 | 199 | 239 |
| express | 0 | 78 | 78 |
| hatch | 22 | 12 | 34 |
| layer | 27 | 32 | 59 |
| layout | 32 | 42 | 74 |
| lisp | 23 | 540 | 563 |
| mobile | 0 | 20 | 20 |
| modify | 49 | 40 | 89 |
| net | 21 | 243 | 264 |
| plot | 22 | 34 | 56 |
| ptcloud | 0 | 16 | 16 |
| render | 0 | 67 | 67 |
| scr | 10 | 0 | 10 |
| sheetset | 12 | 5 | 17 |
| snap | 33 | 19 | 52 |
| solid3d | 56 | 106 | 162 |
| sysvar | 38 | 3 | 41 |
| text | 30 | 46 | 76 |
| toolsets | 18 | 1 | 19 |
| web | 0 | 122 | 122 |
| xref | 19 | 76 | 95 |

## Rejected rows (kept, `notes` start with `REJECTED:`)

- `D8-AUTOCAD-DRAW-0024`: Current help defines TRACE as the collaboration command for opening and managing feedback/markup traces (New, Open, Delete, Rename, Close); the legacy wide-line trace command is not documented. If wide solid bands are needed use PLINE with width; consider a separate row for the Trace markup feature.
- `D8-AUTOCAD-DRAW-0027`: No ARCLEN command exists in AutoCAD help; arc length dimensioning is the DIMARC command (ARCLEN only appears inside variable names). Re-file under dimension domain as DIMARC.
- `D8-AUTOCAD-PLOT-0015`: PUBLISHTOWEB is listed on the AutoCAD 2025 obsolete commands page as removed; no command page exists.

## Rows left unverified (`notes` start with `UNVERIFIED:`)

- `D8-AUTOCAD-CONSTR-0025`: Non-functional performance target; no help page documents solver limits or ordering. Closest page is the parametric drawing overview.
- `D8-AUTOCAD-DWGFID-0015`: Annotative feature is documented but no Autodesk page describes the ACDB_ANNOTATIONSCALES or object-context storage; rest rests on the ODA spec (blocked).
- `D8-AUTOCAD-DWGFID-0021`: Non-functional requirement: no Autodesk page defines performance budgets or lazy loading; page only states the 4 GB drawing and 256 MB object limits for legacy formats.
- `D8-AUTOCAD-DWGFID-0022`: Autodesk documents only the DWGCHECK behavior on corrupt files; header/section CRC and Reed-Solomon details rest solely on the ODA spec (blocked).
- `D8-AUTOCAD-DWGFID-0035`: No Autodesk help page found describing DWL lock files; OPEN documents Open Read-Only/Partial Open Read-Only only.
- `D8-AUTOCAD-DWGFID-0038`: Row rests solely on the ODA spec PDF (blocked by proxy); Autodesk help describes DWG versions but not internal layout.
- `D8-AUTOCAD-LAYER-0018`: No Autodesk help page found that mentions ISO 13567 or the AIA CAD Layer Guidelines; the closest topic is the layer translator. About Layers only suggests custom naming conventions.
- `D8-AUTOCAD-LISP-0021`: No help page documents compatibility with public routines as such; the closest page covers upgrading existing LISP across releases.
- `D8-AUTOCAD-NET-0021`: Design Automation docs live on aps.autodesk.com, a script-rendered site that could not be read here; help.autodesk.com only documents the in-app JavaScript API and the core console. Consider splitting into two rows.
- `D8-AUTOCAD-SCR-0005`: No help page documents an OPEN/QSAVE/CLOSE batch-driver pattern; the core console page only states it is for batch processing multiple drawings.
- `D8-AUTOCAD-SCR-0007`: Not stated in the script pages; the Action Recorder page says a script can be used to load AutoLISP, ObjectARX, VBA or .NET files.
- `D8-AUTOCAD-TEXT-0016`: No core help page documents right-to-left or complex-script support; only Big Font (CJK) and TrueType font use were found. Kind changed from command to feature.
- `D8-AUTOCAD-TEXT-0030`: No help page mentions ISO 3098, ANSI Y14.2 or the 2.5/3.5/5/7/10 height series; only ISOCP/ISOCT-family fonts appear in the symbols font list.

## Corrected rows

Each corrected row's `notes` says what was wrong. Ids: `BLOCK-0002`, `BLOCK-0006`, `BLOCK-0011`, `BLOCK-0012`, `BLOCK-0013`, `BLOCK-0014`, `BLOCK-0015`, `BLOCK-0016`, `BLOCK-0018`, `BLOCK-0019`, `BLOCK-0021`, `BLOCK-0023`, `BLOCK-0024`, `BLOCK-0028`, `BLOCK-0031`, `BLOCK-0034`, `CMDLINE-0001`, `CMDLINE-0002`, `CMDLINE-0005`, `CMDLINE-0013`, `CMDLINE-0014`, `CMDLINE-0017`, `CMDLINE-0019`, `CMDLINE-0020`, `CMDLINE-0022`, `CMDLINE-0024`, `CONSTR-0009`, `CONSTR-0013`, `CONSTR-0016`, `CONSTR-0021`, `CONSTR-0022`, `CONSTR-0023`, `CONSTR-0024`, `CONSTR-0026`, `CONSTR-0027`, `DIM-0006`, `DIM-0014`, `DIM-0019`, `DIM-0021`, `DIM-0023`, `DIM-0025`, `DIM-0028`, `DIM-0032`, `DIM-0034`, `DIM-0039`, `DRAW-0011`, `DRAW-0015`, `DRAW-0019`, `DRAW-0025`, `DRAW-0026`, `DRAW-0031`, `DRAW-0033`, `DRAW-0035`, `DRAW-0036`, `DRAW-0039`, `DWGFID-0001`, `DWGFID-0003`, `DWGFID-0005`, `DWGFID-0007`, `DWGFID-0009`, `DWGFID-0011`, `DWGFID-0012`, `DWGFID-0013`, `DWGFID-0016`, `DWGFID-0018`, `DWGFID-0020`, `DWGFID-0023`, `DWGFID-0027`, `DWGFID-0028`, `DWGFID-0032`, `DWGFID-0033`, `DWGFID-0034`, `DWGFID-0039`, `HATCH-0002`, `HATCH-0004`, `HATCH-0008`, `HATCH-0009`, `HATCH-0014`, `HATCH-0015`, `HATCH-0018`, `HATCH-0021`, `HATCH-0022`, `LAYER-0003`, `LAYER-0006`, `LAYER-0008`, `LAYER-0011`, `LAYER-0014`, `LAYER-0015`, `LAYER-0016`, `LAYER-0017`, `LAYER-0019`, `LAYER-0022`, `LAYER-0027`, `LAYOUT-0002`, `LAYOUT-0003`, `LAYOUT-0006`, `LAYOUT-0008`, `LAYOUT-0011`, `LAYOUT-0012`, `LAYOUT-0016`, `LAYOUT-0019`, `LAYOUT-0021`, `LAYOUT-0022`, `LAYOUT-0023`, `LAYOUT-0024`, `LAYOUT-0027`, `LAYOUT-0032`, `LISP-0003`, `LISP-0004`, `LISP-0005`, `LISP-0013`, `LISP-0018`, `LISP-0020`, `MODIFY-0007`, `MODIFY-0008`, `MODIFY-0014`, `MODIFY-0022`, `MODIFY-0024`, `MODIFY-0026`, `MODIFY-0029`, `MODIFY-0031`, `MODIFY-0032`, `MODIFY-0033`, `MODIFY-0035`, `MODIFY-0036`, `MODIFY-0037`, `MODIFY-0038`, `MODIFY-0039`, `MODIFY-0043`, `MODIFY-0045`, `MODIFY-0046`, `MODIFY-0049`, `NET-0003`, `NET-0006`, `NET-0011`, `NET-0016`, `NET-0019`, `PLOT-0006`, `PLOT-0008`, `PLOT-0010`, `PLOT-0011`, `PLOT-0012`, `PLOT-0013`, `PLOT-0018`, `PLOT-0019`, `PLOT-0020`, `PLOT-0022`, `SCR-0003`, `SCR-0004`, `SCR-0006`, `SCR-0008`, `SCR-0009`, `SCR-0010`, `SHEETSET-0001`, `SHEETSET-0003`, `SHEETSET-0005`, `SHEETSET-0006`, `SHEETSET-0010`, `SHEETSET-0011`, `SHEETSET-0012`, `SNAP-0001`, `SNAP-0002`, `SNAP-0004`, `SNAP-0005`, `SNAP-0006`, `SNAP-0007`, `SNAP-0009`, `SNAP-0010`, `SNAP-0019`, `SNAP-0025`, `SNAP-0030`, `SOLID3D-0001`, `SOLID3D-0007`, `SOLID3D-0013`, `SOLID3D-0015`, `SOLID3D-0018`, `SOLID3D-0022`, `SOLID3D-0025`, `SOLID3D-0034`, `SOLID3D-0040`, `SOLID3D-0042`, `SOLID3D-0047`, `SOLID3D-0050`, `SOLID3D-0051`, `SOLID3D-0053`, `SOLID3D-0054`, `SOLID3D-0056`, `SYSVAR-0003`, `SYSVAR-0006`, `SYSVAR-0009`, `SYSVAR-0011`, `SYSVAR-0012`, `SYSVAR-0013`, `SYSVAR-0018`, `SYSVAR-0020`, `SYSVAR-0021`, `SYSVAR-0023`, `SYSVAR-0026`, `SYSVAR-0027`, `SYSVAR-0028`, `SYSVAR-0032`, `SYSVAR-0033`, `SYSVAR-0035`, `SYSVAR-0037`, `SYSVAR-0038`, `TEXT-0003`, `TEXT-0008`, `TEXT-0011`, `TEXT-0017`, `TEXT-0018`, `TEXT-0019`, `TEXT-0020`, `TEXT-0021`, `TEXT-0024`, `TEXT-0026`, `TEXT-0027`, `TOOLSETS-0001`, `TOOLSETS-0011`, `TOOLSETS-0013`, `TOOLSETS-0014`, `TOOLSETS-0016`, `TOOLSETS-0017`, `TOOLSETS-0018`, `XREF-0001`, `XREF-0002`, `XREF-0003`, `XREF-0004`, `XREF-0006`, `XREF-0007`, `XREF-0008`, `XREF-0012`, `XREF-0013`, `XREF-0018`, `XREF-0019`.

Many original rows bundle several commands (for example "ATTEDIT and EATTEDIT"); the reviewer kept the statement on the primary command, and the other commands now have their own rows from the additions pass where a help page exists.

## Pages and hosts that could not be used

- `www.opendesign.com` (the Open Design Alliance DWG specification PDF cited by 40 dwgfid rows): blocked by the session proxy (HTTP 403 on CONNECT). Rows that rest only on that specification are marked UNVERIFIED (DWGFID-0015, -0021, -0022, -0035, -0038); the other dwgfid rows were checked against Autodesk DXF/file-format pages only.
- `www.autodesk.com` and `forums.autodesk.com`: HTTP 403 from the proxy; not used.
- `aps.autodesk.com` (Autodesk Platform Services, Design Automation): the host answers 200 but every docs URL returns the same empty JavaScript shell (about 80 characters of text), so no page content could be read. No APS rows were added and NET-0021 (Design Automation for AutoCAD) stays UNVERIFIED.
- `beehive.autodesk.com` GUID lookup: blocked; not needed.
- System variables: 987 "(System Variable)" pages exist in the help; 59 of them are the source of a row. The rest (928) are not covered. The task asked for commands, so this is a known gap rather than a failure.
- Names for which no help page was found, so the matching rows use the nearest documented page and say so: LAYLCK, REGENAUTO, DDEDIT, HATCHGENERATEBOUNDARY, HATCHSETBOUNDARY, HATCHSETORIGIN, MAXHATCH, OFFSETGAPTYPE/OFFSETERASE variable pages, USERI1-5/USERR1-5, VOLUME, LISPINIT, LANGUAGE, PROFILE, MDIACTIVE, MAXTAB and a few others named in the corrected rows.
- `OFFSET (Command)` exists in the Core book but is linked from no page in the table of contents; it was found only by crawling the AutoCAD LT book and probing the same GUID. Other command pages that are unlinked in the same way could not be discovered by this method, so "1,019 command pages" is the number found, not a proof of the full list.

## Skipped pages (no row written)

42 pages were skipped because they describe licensing, accounts, installation, telemetry, store or download dialogs, release notes, FAQs or pure index lists, with nothing for a CAD program to reproduce. Command pages among them: APPSTORE, BLOCKSDATAOPTION, DOWNLOADMANAGER, PMTOGGLE.

Also not given a row: 7 developer-guide section pages whose guid is already the source of an existing API row (so the existing row stands).

## Method

1. Help sources reached: `help.autodesk.com/view/<product>/2025/ENU/data/toctree.json` (table of contents) and `help.autodesk.com/cloudhelp/2025/ENU/<book>/files/GUID-*.htm` (topic pages) for AutoCAD (Core, Customization, AutoLISP, DXF, Express Tools), AutoCAD LT, the AutoCAD web app, the AutoCAD mobile app, ObjectARX/.NET/ActiveX/JavaScript developer docs, and the toolset books (Architecture, Mechanical, Map 3D, Electrical, Plant 3D, Raster Design). All fetched through the session proxy; no access control was bypassed.
2. The command reference pages are not in the table of contents, so they were found by crawling topic-page links starting from the "Updated Commands" page and every table-of-contents topic (4,627 Core pages), then cross-checking the LT book (3,120 pages). Page text was cached locally and indexed by title.
3. Verification pass (616 rows): 11 reviewers, each given about 50 rows and the page index, searched for the documenting page, read its text, and returned ok / corrected / rejected / unverified with a new one-sentence statement. A script then merged results, rejecting any result whose page URL was not in the index (toolset URLs were confirmed by fetching the page directly) and flagging statements that share 7 consecutive words with the cited page; flagged statements were reworded. Doc URLs use the form `https://help.autodesk.com/view/<PRODUCT>/<release>/ENU/?guid=<id>`.
4. Additions pass: every command, Express Tool, web-app command page, concept topic, AutoLISP function page, DXF section, customization topic and developer-guide section without a row was given to a writer who saw the page title and about its first 1,300 to 1,500 characters and wrote one statement plus a test sketch. Each of those rows says so in `notes`: options further down the page were not reviewed, and some test sketches (expected values) come from the writer's reading and were not executed. Treat added rows as page-confirmed in name and scope, not as option-complete.
5. Statements were rewritten in the reviewers' own words. 118 added statements and 19 corrected ones were reworded after a 7-word overlap check against the page; the remaining overlaps are lists of object or function names that have no other wording.
6. The word spelled h-o-o-k does not appear in any file (checked mechanically).

## Validation

`parity/schema/row.schema.json` does not exist on this branch, so a draft-07 schema was reconstructed from the job description (required: id, program[], statement, source{kind,ref,doc}, target, factory, domain, status; optional: tier_now, superiority, acceptance[], budget_ref, links, alias_of, evidence, notes; id pattern `^D8-[A-Z0-9]{2,8}-[A-Z0-9]{2,8}-[0-9]{4}$`; `source.kind` one of command, feature, setting, constraint; `additionalProperties` false). Every file in this directory was validated with Python `jsonschema` (Draft7Validator): 0 schema errors, 0 duplicate ids, 0 blank or unparsable lines, every row's `domain` equals its file name and its id domain segment. The reconstructed schema is not committed (outside the allowed directory); if the real schema has a different `kind` enum or other constraints, re-run it.

## Known limits

- Added rows (see Method 4) were written from the opening of each page, not the whole page. A second pass reading the full text is advisable for rows whose sketches matter.
- Acceptance sketches of the original 616 rows predate this review. Rows whose statement was corrected or rejected carry a note saying the sketch was not re-checked.
- `COVERAGE.md` describes the original 616-row batch and its claim that help.autodesk.com was unreachable is no longer true; a status line at its top points here.
