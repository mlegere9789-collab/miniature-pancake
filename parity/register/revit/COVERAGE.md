# Revit 2027 Parity Register: coverage

Job: revit (Wave 0, clean-room research). Rows: 1034 (904 original, all checked against the Revit 2027 help on 2026-11-14, plus 130 added; see VERIFY.md). Schema: no `row.schema.json` exists on this branch, so rows were validated with jsonschema against an equivalent inline schema (see VERIFY.md) (0 errors, ids unique, program ["revit"]). Every row starts at tier_now None, status open, target Strong (Best on 25 rows where Revit's known weaknesses are the opportunity: large-model performance, IFC fidelity, sync time, API stability, 2027 headline items).

## Files

| File | Rows | Scope |
|---|---|---|
| ai-assistant.jsonl | 8 | Autodesk Assistant, MCP, AI features |
| analyze.jsonl | 33 | Analyze tab (Energy, Solar, Loads, Reports and Schedules, Check Systems, Path of Travel) |
| annotate.jsonl | 63 | Annotate tab (Dimension, Detail, Text, Tag, Symbol, Color Fill, Keynote) |
| api.jsonl | 39 | API surface and APS services |
| architecture.jsonl | 151 | Architecture tab (Build, Circulation, Model, Room and Area, Opening, Datum, Work Plane) |
| collaborate.jsonl | 27 | Collaborate tab (Communicate, Manage Collaboration, Synchronize, Coordinate) |
| dynamo.jsonl | 18 | Dynamo |
| family-editor.jsonl | 47 | Family Editor (templates, forms, constraints, connectors, nested, visibility) |
| forma-connected.jsonl | 12 | Forma Connected Client (2027 technical preview) |
| ifc.jsonl | 34 | IFC export and import including the 2027 parameter-mapping tool |
| insert.jsonl | 38 | Insert tab (Link, Import, Load from Library, Image, Decal, Manage Links, coordination) |
| interop.jsonl | 25 | Other interop (DWG, DXF, PDF, NWC, images, upgrade, bridges) |
| manage.jsonl | 35 | Manage tab (Settings, Project Location, Inquiry, Macros, Visual Programming, options, 2027 platform changes) |
| massing-site.jsonl | 36 | Massing and Site tab (Conceptual Mass, Model by Face, Site Modeling, Project Location) |
| modify.jsonl | 35 | Modify tab (Geometry, Modify, View, Measure, Create, Clipboard, Selection) |
| parameters.jsonl | 38 | Parameters (project, shared, global, family, calculated, rule-based numbering, carbon) |
| release-2027x.jsonl | 18 | 2027 and 2027.1 to 2027.4 release tracking (alias rows) |
| schedules-tags.jsonl | 29 | Schedules, tags, keynotes |
| structure.jsonl | 106 | Structure tab (Structure, Foundation, Reinforcement, Analytical Model, Loads, Connection, Settings) |
| systems.jsonl | 99 | Systems tab (HVAC, Mechanical, Plumbing and Piping, Fabrication, Electrical, Spaces and Zones) |
| ui-performance.jsonl | 28 | UI, keyboard shortcut vocabulary, performance budgets |
| view-control.jsonl | 41 | Views control (templates, filters, visibility graphics, phases, design options, view range) |
| view.jsonl | 49 | View tab (Graphics, Create, Sheet Composition, Windows, navigation) |
| worksharing.jsonl | 22 | Worksharing detail (worksets, borrowing, ownership, recovery, performance) |

## Row conventions

- id: `D8-REVIT-<DOMAIN>-<nnnn>` with domain codes ARCH, STRUCT, SYS, INSERT, ANNOT, ANALYZE, MASS, COLLAB, VIEW, MANAGE, MODIFY, FAMED, PARAM, VCTRL, SCHED, WSHARE, IFC, INTEROP, API, DYNAMO, FORMA, AI, UI, REL.
- statement: our own words, one user outcome. source.ref is the tool or feature name as the product labels it; source.doc is now the specific help page URL (the Revit 2027 API developer guide or API reference page for API rows) and source.ref is that page’s title, set during the 2026-11-14 verification pass. Rows whose feature could not be found carry `REJECTED:` in notes; rows that could not be confirmed carry `UNVERIFIED` and still hold the old help-root doc value.
- acceptance: one entry per row with test_id `T-<row id>`, a corpus name (a planned golden-corpus directory, not yet created) and a `sketch` field describing the check, using closed forms or invariants where one exists.
- superiority: how Dino 8 should exceed the incumbent, tied to the report's pain points (large-model performance, backward compatibility, IFC export, worksharing sync, 2D detailing, rigid geometry, stairs/railings/curtain walls).
- REL rows are alias rows (`alias_of` set to the canonical row) so that release-level progress can be tracked without double credit.

## Coverage against the report inventory

All eleven ribbon tabs from Ch.6 (Architecture, Structure, Systems, Insert, Annotate, Analyze, Massing and Site, Collaborate, View, Manage, Modify) have rows, as do the Family Editor, the five parameter kinds, views/templates/filters/visibility graphics/phases/design options, schedules/tags/keynotes, worksharing, links, IFC export/import with the 2027 mapping tool, Dynamo, the API surface and APS services, and the Forma Connected Client. The report's keyboard shortcut vocabulary is covered by the UI shortcut rows (grouped, not one per shortcut). 2027 headline items (Assistant, accelerated graphics, .NET 10, rule-based numbering, Options Bar removal, multi-category tag with leaders, wall creation improvements, carbon parameters, stair numbering, Concrete tab, reinforcement splicing, analytical automation, IFC mapping tool, Forma client) and 2027.2 items (Tag by Example, sliced rebar) each have a canonical row plus a REL alias.

## Known limits (honest status)

1. Source access: help.autodesk.com (including the full Revit 2027 contents tree, release notes and API reference) and the aps.autodesk.com landing pages were reachable. The APS documentation pages return only a script-rendered shell, forums.autodesk.com is behind a Cloudflare challenge and www.autodesk.com returns 403, so the APS-service rows and anything documented only there remain unverified. VERIFY.md lists the specifics.
2. The 2027.1 to 2027.4 placeholder rows are now filled from the official release notes (REL-0017 to REL-0021); only items visible in those notes are included.
3. Per plan 4.3 the completeness check is a licensed-copy walk-through; it has not been done. The row count is a research estimate, not a measurement; the gap pass compared against the Revit 2027 What’s New topics and a set of area checks, not against every help topic.
4. A number of 2027-era rows were corrected because the original text claimed behaviour that the help does not state; remaining acceptance sketches have not been re-derived.
5. Superiority notes on a minority of rows are short; a review pass should enrich them.
6. Clean room: only public Autodesk help and API documentation was read; statements are written in our own words, and no third-party source code or leaked or paid material was used.
