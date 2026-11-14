# Revit 2027 Parity Register: coverage

Job: revit (Wave 0, clean-room research). Rows: 904. Schema: `$S/schema/row.schema.json`, validated with jsonschema (0 errors, ids unique, program ["revit"]). Every row starts at tier_now None, status open, target Strong (Best on 25 rows where Revit's known weaknesses are the opportunity: large-model performance, IFC fidelity, sync time, API stability, 2027 headline items).

## Files

| File | Rows | Scope |
|---|---|---|
| ai-assistant.jsonl | 8 | Autodesk Assistant, MCP, AI features |
| analyze.jsonl | 29 | Analyze tab (Energy, Solar, Loads, Reports and Schedules, Check Systems, Path of Travel) |
| annotate.jsonl | 59 | Annotate tab (Dimension, Detail, Text, Tag, Symbol, Color Fill, Keynote) |
| api.jsonl | 33 | API surface and APS services |
| architecture.jsonl | 140 | Architecture tab (Build, Circulation, Model, Room and Area, Opening, Datum, Work Plane) |
| collaborate.jsonl | 22 | Collaborate tab (Communicate, Manage Collaboration, Synchronize, Coordinate) |
| dynamo.jsonl | 15 | Dynamo |
| family-editor.jsonl | 42 | Family Editor (templates, forms, constraints, connectors, nested, visibility) |
| forma-connected.jsonl | 9 | Forma Connected Client (2027 technical preview) |
| ifc.jsonl | 32 | IFC export and import including the 2027 parameter-mapping tool |
| insert.jsonl | 34 | Insert tab (Link, Import, Load from Library, Image, Decal, Manage Links, coordination) |
| interop.jsonl | 18 | Other interop (DWG, DXF, PDF, NWC, images, upgrade, bridges) |
| manage.jsonl | 31 | Manage tab (Settings, Project Location, Inquiry, Macros, Visual Programming, options, 2027 platform changes) |
| massing-site.jsonl | 34 | Massing and Site tab (Conceptual Mass, Model by Face, Site Modeling, Project Location) |
| modify.jsonl | 34 | Modify tab (Geometry, Modify, View, Measure, Create, Clipboard, Selection) |
| parameters.jsonl | 36 | Parameters (project, shared, global, family, calculated, rule-based numbering, carbon) |
| release-2027x.jsonl | 18 | 2027 and 2027.1 to 2027.4 release tracking (alias rows) |
| schedules-tags.jsonl | 25 | Schedules, tags, keynotes |
| structure.jsonl | 84 | Structure tab (Structure, Foundation, Reinforcement, Analytical Model, Loads, Connection, Settings) |
| systems.jsonl | 88 | Systems tab (HVAC, Mechanical, Plumbing and Piping, Fabrication, Electrical, Spaces and Zones) |
| ui-performance.jsonl | 23 | UI, keyboard shortcut vocabulary, performance budgets |
| view-control.jsonl | 33 | Views control (templates, filters, visibility graphics, phases, design options, view range) |
| view.jsonl | 38 | View tab (Graphics, Create, Sheet Composition, Windows, navigation) |
| worksharing.jsonl | 19 | Worksharing detail (worksets, borrowing, ownership, recovery, performance) |

## Row conventions

- id: `D8-REVIT-<DOMAIN>-<nnnn>` with domain codes ARCH, STRUCT, SYS, INSERT, ANNOT, ANALYZE, MASS, COLLAB, VIEW, MANAGE, MODIFY, FAMED, PARAM, VCTRL, SCHED, WSHARE, IFC, INTEROP, API, DYNAMO, FORMA, AI, UI, REL.
- statement: our own words, one user outcome. source.ref is the tool or feature name as the product labels it; source.doc is the Revit 2027 help root `https://help.autodesk.com/view/RVT/2027/ENU/` (or the API developer guide URL for API rows) followed by the topic name.
- acceptance: one entry per row with test_id `T-<row id>`, a corpus name (a planned golden-corpus directory, not yet created) and a `sketch` field describing the check, using closed forms or invariants where one exists.
- superiority: how Dino 8 should exceed the incumbent, tied to the report's pain points (large-model performance, backward compatibility, IFC export, worksharing sync, 2D detailing, rigid geometry, stairs/railings/curtain walls).
- REL rows are alias rows (`alias_of` set to the canonical row) so that release-level progress can be tracked without double credit.

## Coverage against the report inventory

All eleven ribbon tabs from Ch.6 (Architecture, Structure, Systems, Insert, Annotate, Analyze, Massing and Site, Collaborate, View, Manage, Modify) have rows, as do the Family Editor, the five parameter kinds, views/templates/filters/visibility graphics/phases/design options, schedules/tags/keynotes, worksharing, links, IFC export/import with the 2027 mapping tool, Dynamo, the API surface and APS services, and the Forma Connected Client. The report's keyboard shortcut vocabulary is covered by the UI shortcut rows (grouped, not one per shortcut). 2027 headline items (Assistant, accelerated graphics, .NET 10, rule-based numbering, Options Bar removal, multi-category tag with leaders, wall creation improvements, carbon parameters, stair numbering, Concrete tab, reinforcement splicing, analytical automation, IFC mapping tool, Forma client) and 2027.2 items (Tag by Example, sliced rebar) each have a canonical row plus a REL alias.

## Known limits (honest status)

1. Source access: during this session the egress proxy refused help.autodesk.com and aps.autodesk.com (connect rejected), so no page could be fetched. Rows were written from public product knowledge and the report digest; the doc field gives the help root plus topic name, NOT a verified deep link. A reviewer with access must confirm each topic and add the page GUID URL.
2. 2027.1, 2027.3 and 2027.4 contents are not enumerated in the report; two placeholder REL rows (release-2027x.jsonl) hold the gap until release notes can be read. 2027.2 items are limited to Tag by Example and sliced rebar.
3. Per plan 4.3 the completeness check is a licensed-copy walk-through; it has not been done. The row count is a research estimate, not a measurement.
4. Several rows describe 2027 behaviour only as far as the report states it (for example Options Bar removal, wall creation improvements, Forma client scope); acceptance sketches for those need refinement once the release notes are read.
5. Superiority notes on a minority of rows are short; a review pass should enrich them.
6. Clean room: only vendor-level public documentation knowledge and the owner's report were used; no third-party source code, no leaked or paid material.
