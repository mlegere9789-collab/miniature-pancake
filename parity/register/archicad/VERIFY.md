# Archicad register: verification pass (second pass)

Date: 2026-11-14. Branch `w0-archicad2`. Total rows now **924** (was 678), unique ids, 0 schema errors.

## Counts
- Checked: **678** existing rows (every row, none skipped).
- Verified unchanged (page confirms the statement): **133**.
- Corrected (feature exists, statement, ref or sketch changed to match the page): **457**.
- Rejected (kept, `notes` start with `REJECTED:`): **36**.
- Unverified (only an unreachable source could document it; `notes` start with `UNVERIFIED`): **52**.
- Added: **246** new rows, each backed by a page read in this pass: an Archicad 30 Help page, or for 3 API rows the pypi.org package page (`notes` start with `ADDED+VERIFIED`). No added row is unverified.

| Domain | Checked | Verified | Corrected | Rejected | Unverified | Added |
|---|---|---|---|---|---|---|
| analysis | 22 | 2 | 17 | 3 | 0 | 10 |
| api | 35 | 0 | 11 | 0 | 24 | 7 |
| attrib | 41 | 3 | 35 | 3 | 0 | 12 |
| cad2d | 15 | 0 | 15 | 0 | 0 | 6 |
| edit | 40 | 7 | 33 | 0 | 0 | 11 |
| gdl | 31 | 1 | 17 | 0 | 13 | 16 |
| graphic | 35 | 11 | 21 | 2 | 1 | 10 |
| hotlink | 19 | 10 | 9 | 0 | 0 | 9 |
| ifc | 50 | 19 | 27 | 1 | 3 | 12 |
| mep | 23 | 1 | 21 | 1 | 0 | 11 |
| nav | 37 | 13 | 23 | 1 | 0 | 17 |
| new2930 | 19 | 3 | 10 | 2 | 4 | 7 |
| props | 36 | 10 | 25 | 1 | 0 | 10 |
| reno | 20 | 4 | 11 | 4 | 1 | 6 |
| sched | 26 | 7 | 18 | 1 | 0 | 10 |
| seo | 23 | 5 | 13 | 5 | 0 | 5 |
| struct | 13 | 2 | 8 | 3 | 0 | 9 |
| team | 41 | 15 | 23 | 2 | 1 | 15 |
| toolbox | 121 | 15 | 102 | 4 | 0 | 47 |
| ui | 16 | 1 | 9 | 3 | 3 | 7 |
| viz | 15 | 4 | 9 | 0 | 2 | 9 |
| **Total** | **678** | **133** | **457** | **36** | **52** | **246** |

`source.doc` was replaced with a specific page URL on every row that was verified, corrected or added (850 of 924 rows now point at a cached Help page).

## Method
1. Robots check: `help.graphisoft.com/robots.txt` disallows everything except the current Archicad 30 (`/AC/30/INT/` and other-language copies), `/MEP/30/`, `/BC/`, `/BCSAAS/`, BIMx and a few others. Only allowed paths were fetched; `/AC/29/` was not touched.
2. The sitemap (`sitemap_1.xml`, `sitemap_2.xml`) gave the URL list. All 2,078 Archicad 30 INT pages and the allowed MEP, BIMcloud, BIMx pages were fetched once at low concurrency into a local cache (about 4,000 pages, first ~6,000 characters of text per page). Nothing from the cache is committed.
3. Seven parallel workers each took a set of domain files. For each row they searched the cache by feature name, read the page text, and recorded one outcome in `notes`: `VERIFIED`, `CORRECTED`, `REJECTED` or `UNVERIFIED`. They then added rows for gaps in the assigned topics, each from a page they read.
4. Statements are one sentence in original wording. After the workers finished I ran a word-overlap check of each statement against its cited page and reworded the longest overlaps (remaining overlaps are lists of feature names). I also read a random sample of 8 rows against their pages and found no mismatch. This is a sample, not a full independent re-verification.
5. Validation: Python `jsonschema` (Draft 2020-12) against the schema given in the task (the repo has no `parity/schema/row.schema.json`): required `id` pattern, `program`, `statement`, `source{kind,ref,doc}`, `target` in Strong|Best, `factory`, `domain`, `status`; plus unique ids, domain matching file name and id, no forbidden word. Result: 924 rows, 0 problems.

## Limits and caveats
- The cache holds roughly the first 6,000 characters per page, so long pages may carry details the check did not see. Where that mattered the row says so in `notes` (for example TOOLBOX-0058, 0065, 0090).
- "Verified" means the Archicad 30 Help documents the feature as the row states it. It does not mean Dino 8 behaviour was tested, and `status` stays `open` on every row.
- Archicad 29: only the Archicad 30 Help is crawlable. Features claimed as Archicad 29 additions can be confirmed only if the AC30 Help, Migration Guide or New Features chapter mentions them; unconfirmed ones are marked.
- Rows that were rejected or unverified without a page often still show the old chapter-root URL (`/AC/29/INT/_AC29_Help/`, 28 rows). That URL was not fetched and is only a placeholder.
- Acceptance sketches are closed-form or definitional checks, not measurements from a licensed Archicad. A walk-through in a licensed copy is still needed.

## Pages and hosts unreachable from this session
| Host | Result | Rows affected |
|---|---|---|
| community.graphisoft.com | 403, Cloudflare challenge | none cited |
| archicadapi.graphisoft.com | proxy CONNECT 403 | JSON command API rows (UNVERIFIED) |
| gdl.graphisoft.com | proxy CONNECT 403 | GDL reference rows (UNVERIFIED) |
| graphisoft.github.io (API devkit docs) | proxy CONNECT 403 | C++ Add-On API rows (UNVERIFIED; the README on raw.githubusercontent.com and pypi.org were readable and used where noted) |
| developer.graphisoft.com, www.graphisoft.com, helpcenter.graphisoft.com | proxy CONNECT 403 | none cited |
| aecmag.com | not fetched | 1 row (UNVERIFIED) |
| help.graphisoft.com/AC/29/... | disallowed by robots.txt, not fetched | Archicad 29 claims |

The task statement said these hosts were reachable. In this session only help.graphisoft.com, pypi.org and raw.githubusercontent.com were.

Unverified rows by cited host: archicadapi.graphisoft.com (18), help.graphisoft.com (15), gdl.graphisoft.com (12), pypi.org (3), graphisoft.github.io (3), aecmag.com (1).

## Rejected rows (36)
These rows describe features not found in the Archicad 30 Help after several searches. They are kept, not deleted.
- `D8-ARCHICAD-ANALYSIS-0010`: no daylight factor or illuminance analysis in AC30 help (searched daylight, illuminance, lux); only model-based solar irradiation on openings within Energy Evaluation and Sun Study images exist.
- `D8-ARCHICAD-ANALYSIS-0020`: no road, slope-projection or retaining-wall features on meshes in AC30 help (searched road, retaining wall, slope); the Mesh tool documents regular sloped meshes, point editing, holes and ridge displa
- `D8-ARCHICAD-ANALYSIS-0022`: no placement of map or aerial imagery in AC30 help (searched map, aerial, Google Maps); Location Settings only offers a Show in Google Maps link and KMZ/SKP location import and export.
- `D8-ARCHICAD-ATTRIB-0024`: no markup-style attribute in Archicad 30 help; searched markup, markup style, markup entry. Issue handling documents issues and PDF markup, not a styled-entry attribute.
- `D8-ARCHICAD-ATTRIB-0031`: no named text-style attribute in Archicad 30 help; text formatting is via the formatting palette, text favorites and per-tool Text Style controls. Searched text style, fonts.
- `D8-ARCHICAD-ATTRIB-0041`: no cost or phase attribute type in Archicad 30 help; nearest are renovation status and renovation filters, which are views settings. Searched cost, phase, renovation.
- `D8-ARCHICAD-GRAPHIC-0009`: no AC30 help page describes a legend generated from graphic override rules (searched Graphic Overrides pages and 'legend' across help; only the Keynote Legend exists)
- `D8-ARCHICAD-GRAPHIC-0025`: no AC30 help page describes drawing priority by pen number; stacking is by element class/stack level (Display Order) and wall/material intersection priority
- `D8-ARCHICAD-IFC-0027`: AC30 help documents only importing IDS files to create classifications/properties; validating an IFC or model against IDS rules is not described.
- `D8-ARCHICAD-MEP-0009`: MEP Designer help (System Browser, Ventilation data, Hydronics data, Hydronics Optimizer) documents volume flow, velocity, mass flow, capacities and temperatures but no pressure-drop calculation or cr
- `D8-ARCHICAD-NAV-0035`: no AC30 help page describes scheduled or unattended publisher runs or parameterised sets (searched Publisher, Publishing Process, BIMcloud pages); publishing is started manually
- `D8-ARCHICAD-NEW2930-0003`: no Archicad 30 New Features or migration page describes 'flexible documentation controls'; searched New Features, Documentation and Migration chapters; nearest item is Flexible Project Info (see NEW29
- `D8-ARCHICAD-NEW2930-0011`: Archicad 30 New Features and MEP Designer help contain no statement about MEP Designer expanding to more markets; searched New Features in MEP Design and Welcome to MEP Designer.
- `D8-ARCHICAD-PROPS-0025`: no buildingSMART Data Dictionary integration in Archicad 30 help; searched bSDD, data dictionary and classification pages (Classification Manager has no such link).
- `D8-ARCHICAD-RENO-0009`: no AC30 help page describes renovation phases or phase attributes; renovation is limited to the three statuses plus filters (searched 'phase' across help)
- `D8-ARCHICAD-RENO-0010`: Openings By Renovation Status page (050-111) says moved windows are modelled twice with explicit statuses; no automatic status inheritance from wall to opening is described
- `D8-ARCHICAD-RENO-0017`: no AC30 help page describes keeping or overriding renovation status on hotlinked modules (searched hotlink and renovation pages)
- `D8-ARCHICAD-RENO-0019`: the Renovation chapter (050-101..118) does not describe point-cloud or scan baselines; Point Clouds (120-33) are a separate interoperability feature that mentions renovation only as a use case
- `D8-ARCHICAD-SCHED-0014`: no override-flag or restore-default feature in schedules in Archicad 30 help; only Reset Custom Colors exists. Default linking is a property feature (chain icon), covered by PROPS rows.
- `D8-ARCHICAD-SEO-0006`: Solid Operation Types lists exactly five operations (subtraction, subtraction with upward or downward extrusion, intersection, addition); there is no addition with extrusion.
- `D8-ARCHICAD-SEO-0007`: Solid Operation Types lists exactly five operations; no addition with downward extrusion exists in Archicad 30 help.
- `D8-ARCHICAD-SEO-0012`: no help page documents failure reporting or conflict highlighting for Solid Element Operations; the only related note is that a beam operator with lower 3D priority than its target appears to have no
- `D8-ARCHICAD-SEO-0022`: the help does not list a mesh as a solid operation target; Intersecting Elements That Don't Clean Up states that Mesh is not supported in intersections.
- `D8-ARCHICAD-SEO-0023`: no help page describes performance, incremental recalculation or operator-count limits for Solid Element Operations.
- `D8-ARCHICAD-STRUCT-0002`: searched reinforcement and rebar across AC30 and MEP help; no reinforcement tool or bar schedule is documented (only generic 2D Detail Element objects such as steel profile and timber member symbols).
- `D8-ARCHICAD-STRUCT-0006`: no dormer or Roof Accessories feature in AC30 help; roofs offer holes (Create a Hole in a Roof), a roof tower and RoofMaker GDL parts for rafters, purlins and ridges instead.
- `D8-ARCHICAD-STRUCT-0012`: no timber, CLT, grain-direction or engineered-panel features in AC30 help (searched timber, CLT, layered panel); timber appears only as 2D detail element symbols and AI Visualizer prompts.
- `D8-ARCHICAD-TEAM-0019`: no partial-open of a Teamwork project by story/view/element type found in AC30 or BIMcloud help (only DWG/XREF layer partial open exists); searched open/partial/selected terms.
- `D8-ARCHICAD-TEAM-0024`: the 14-day offline limit is not stated anywhere in AC30 Teamwork or BIMcloud help; offline work itself exists (see TEAM-0017).
- `D8-ARCHICAD-TOOLBOX-0005`: Wall tool geometry methods are straight, trapezoid (thickness varies, height does not) and polygon; no sloped-top method exists (read Wall Tool Settings, Create a Trapezoid Wall); a sloped top needs p
- `D8-ARCHICAD-TOOLBOX-0023`: Archicad 30 slabs are horizontal; help shows only per-edge angles and no Slab Edit palette or multi-plane slab (read Slabs, Slab Tool Settings); sloped planes come from Roof or Morph
- `D8-ARCHICAD-TOOLBOX-0106`: no three-point or edge-and-slope slab plane exists; slab is horizontal with only per-edge angles (read Slabs, Slab Tool Settings); a sloped plane needs a roof or morph
- `D8-ARCHICAD-TOOLBOX-0111`: stairs are not associative to slabs; help says to join them through Solid Element Operations, and openings can be created from a selection, but no automatic stair-boundary slab opening exists (read St
- `D8-ARCHICAD-UI-0003`: no help page documents a global command search or Find Command field in Archicad 30 (searched 'find command', 'search commands'); the only command search is the list filter in the shortcut and menu cu
- `D8-ARCHICAD-UI-0006`: no Archicad 30 help page documents screen-reader labels or full keyboard reach of dialog controls (searched accessibility, screen reader); only keyboard navigation inside tool settings panels is descr
- `D8-ARCHICAD-UI-0009`: duplicate of UI-0003; no global command search found in Archicad 30 help.

## Unverified rows (52)
- `D8-ARCHICAD-API-0001`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI describes the package as a binding for the JSON command interface but gives no transport details.
- `D8-ARCHICAD-API-0002`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; GetAllElements is not named in the PyPI release notes.
- `D8-ARCHICAD-API-0003`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI notes confirm GetSelectedElements (onlySupportedTypes flag in 28.3000) but not GetElementsByType.
- `D8-ARCHICAD-API-0004`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; bounding-box commands are not in the PyPI release notes.
- `D8-ARCHICAD-API-0006`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI 25.3000 notes say built-in properties can be 'retrieved and modified' but name no setter command.
- `D8-ARCHICAD-API-0008`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI 26.3000 notes only say property groups became available.
- `D8-ARCHICAD-API-0009`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI 26.3000 notes only say classification systems were introduced.
- `D8-ARCHICAD-API-0011`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI notes say only that new commands were introduced in 25.x.
- `D8-ARCHICAD-API-0012`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0013`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0014`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI 26.3000 notes only mention more navigator item types handled.
- `D8-ARCHICAD-API-0015`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0016`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0017`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0018`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0019`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0021`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0022`: JSON Interface Documentation (archicadapi.graphisoft.com) is unreachable; PyPI type names such as AttributeIndexAndGuidOrError suggest per-item error results but no codes are listed.
- `D8-ARCHICAD-API-0025`: Python utility helpers are not described in the PyPI page; the reference manual (archicadapi.graphisoft.com) is unreachable.
- `D8-ARCHICAD-API-0026`: Grasshopper integration is not mentioned on PyPI or in reachable help; docs are on unreachable hosts.
- `D8-ARCHICAD-API-0027`: Jupyter use is not documented on PyPI (it only mentions scripts from command line or inside Archicad); reference manual unreachable.
- `D8-ARCHICAD-API-0029`: C++ API docs (graphisoft.github.io) are unreachable; the devkit README only links to them (ACAPI_Element_Create).
- `D8-ARCHICAD-API-0030`: C++ API docs (graphisoft.github.io) are unreachable; the devkit README only links to them (notification services).
- `D8-ARCHICAD-API-0031`: C++ API docs (graphisoft.github.io) are unreachable; the devkit README only links to them (custom tools and element types).
- `D8-ARCHICAD-GDL-0002`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (BLOCK, CYLIND etc.).
- `D8-ARCHICAD-GDL-0003`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (EXTRUDE, TUBE, RULED etc.).
- `D8-ARCHICAD-GDL-0004`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (GROUP/ADDGROUP/SUBGROUP/ISECTGROUP/PLACEGROUP). Note: the row lists ADDG
- `D8-ARCHICAD-GDL-0005`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (ADD, MUL, ROT, DEL).
- `D8-ARCHICAD-GDL-0007`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (IF, FOR, WHILE, REPEAT, GOSUB, CALL). Help only shows macro text precede
- `D8-ARCHICAD-GDL-0009`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (LINE2, RECT2, POLY2 etc.); help documents only the graphical 2D Symbol w
- `D8-ARCHICAD-GDL-0011`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (MATERIAL, PEN, LINE_TYPE, FILL). The Parameters page does confirm pen, l
- `D8-ARCHICAD-GDL-0012`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (DEFINE STYLE, text commands).
- `D8-ARCHICAD-GDL-0013`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (LIGHT). Help only notes that changing subtype to Light turns an object i
- `D8-ARCHICAD-GDL-0014`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (OPEN, INPUT, OUTPUT, REQUEST). Help only says the Master script can acce
- `D8-ARCHICAD-GDL-0015`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (GLOB_* globals).
- `D8-ARCHICAD-GDL-0018`: GDL Reference (gdl.graphisoft.com) is unreachable; the Archicad 30 help only points to it and does not document these commands (BINARY 3D, POLYROOF, MESH, TEVE, PGON).
- `D8-ARCHICAD-GDL-0028`: GSM and XML part formats are not named in the reachable help (only .lcf, .pla and libpack appear); their documentation is on unreachable GDL pages.
- `D8-ARCHICAD-GRAPHIC-0011`: help describes rule criteria as like Find & Select (050-120) but cached pages show no numeric range/comparison operators or gradient colouring; Find & Select criteria detail page not in cache
- `D8-ARCHICAD-IFC-0015`: performance/bounded time and memory is a non-functional goal; help gives no figures; only note is that element parameter export increases file size.
- `D8-ARCHICAD-IFC-0024`: read Import IFC Model; no import report of failed entities with GlobalIds is described in the pages read; IDS import does write results to the session report.
- `D8-ARCHICAD-IFC-0026`: bounded memory for very large IFC imports is a non-functional goal; AC30 help gives no figures; only filtered import (Model Filter for IFC Import) is documented.
- `D8-ARCHICAD-NEW2930-0014`: Archicad 29 introduction of the assistant cannot be confirmed (29 help not crawlable); the AI Assistant (Beta) page is in Archicad 30 help, so statement and source were moved there.
- `D8-ARCHICAD-NEW2930-0016`: the claim names no concrete Archicad 29 documentation feature, and Archicad 29 help is not crawlable; no AC30 page lists 29 documentation changes.
- `D8-ARCHICAD-NEW2930-0017`: Twinmotion and Enscape live links are not mentioned anywhere in the cached Archicad 30 help (searched both names); 29 help is not crawlable.
- `D8-ARCHICAD-NEW2930-0019`: the aecmag.com announcement is unreachable (proxy 403); AI Solutions in Archicad 30 help documents shipped features, not a roadmap.
- `D8-ARCHICAD-RENO-0015`: cached help documents only IFC import handling of renovation status (121-27/28); export-side property naming is not in the cached pages
- `D8-ARCHICAD-TEAM-0040`: searched BIMcloud help for REST/API; no API page in reachable BIMcloud or AC30 help; developer documentation is on unreachable hosts.
- `D8-ARCHICAD-UI-0005`: the help mentions 'localized version' only in passing (shortcuts, commands not available in every localization); no help page documents UI language switching or localised library handling.
- `D8-ARCHICAD-UI-0014`: help shows separate Intel-based Mac and Windows installers in the BIM manager guide only; no help page discusses Apple silicon builds or feature parity; this is a packaging fact, not an editor feature
- `D8-ARCHICAD-UI-0015`: duplicate of UI-0005; only incidental mentions of localised versions in help; no page on localised libraries, templates or language switching was found.
- `D8-ARCHICAD-VIZ-0013`: no AC30 help page mentions Twinmotion (searched all cached AC/MEP pages); the link is documented by Epic/Graphisoft sites that are not reachable
- `D8-ARCHICAD-VIZ-0014`: no AC30 help page mentions Enscape (searched all cached pages); the link is documented by the vendor site, which is not reachable

## COVERAGE.md items from the quality gate
- Teamwork server details: restored (auto-reserve, backups, Delta Cache, snapshots and more in `team.jsonl`).
- More MEP library rows: added in `mep.jsonl`.
- Dormer and roof accessories: not documented in the Archicad 30 Help (STRUCT-0006 rejected); roof tool detail was added instead.
- Structural timber detail: not documented (STRUCT-0012 rejected). Rebar/reinforcement: not documented (STRUCT-0002 rejected).
- Topics searched but not in the Help, so no rows: Twinmotion and Enscape links (one mention of Cadimage on the ProKit page), daylight analysis, LandXML terrain import, cost estimation workflow, aerial imagery.
