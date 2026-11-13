# Archicad 29/30 Parity Register: coverage

678 rows, program `archicad`, all `tier_now` None and `status` open (no free credit). Target is Strong, or Best for 24 crown-jewel rows. Ids are `D8-ARCHICAD-<DOMAIN>-<nnnn>`. Each row has a statement in original wording, a source (kind, ref, doc), an acceptance sketch (`acceptance[0].sketch`, test id `acc.archicad.<domain>.<nnnn>`) and a superiority note.

## Rows by domain (file = lowercase domain)
| Domain | Rows | Factory | Scope |
|---|---|---|---|
| TOOLBOX | 121 | bim-arch | Wall (all geometry methods, slanted, profiles), Column, Beam, Slab, Roof, Shell, Stair, Railing, Curtain Wall, Door, Window, Skylight, Zone, Morph, Mesh, Object, Lamp, Grid, 2D tools, markers, tool settings |
| EDIT | 40 | bim-arch | move/rotate/mirror/multiply/stretch/split/trim/fillet/offset, groups, selection, snaps, pet palette, undo |
| SEO | 23 | bim-arch | Solid Element Operations (add, subtract, intersect, up/down extrusions), links, trim to roof/shell |
| ATTRIB | 41 | bim-arch | layers, line types, fills, surfaces, building materials, composites, profiles, pen sets, zone categories, attribute manager |
| PROPS | 36 | bim-arch | Property Manager, classification, bSDD, expression properties |
| SCHED | 26 | drafting-docs | Interactive Schedules (element, component, surface), criteria, fields, export |
| NAV | 37 | drafting-docs | Navigator, Project Map, View Map, Organizer, Layout Book, master layouts, drawings, Publisher |
| GRAPHIC | 35 | drafting-docs | graphic overrides, model view options, partial structure display, display order, 3D style |
| RENO | 20 | drafting-docs | renovation status and filters |
| CAD2D | 15 | drafting-docs | 2D drafting, dimensions, detailing |
| TEAM | 41 | collab-cloud | Teamwork reserve/release/send/receive, BIMcloud Manager and server |
| HOTLINK | 19 | collab-cloud | hotlink modules, references |
| IFC | 50 | interop-migration | IFC export/import/translators, IDS, BCF, DWG/DXF, other formats |
| GDL | 31 | scripting-api-ai | GDL language features and Object Editor |
| API | 35 | scripting-api-ai | JSON command API, Python package, C++ Add-On API, MCP, AI features |
| MEP | 23 | bim-struct-mep | MEP Designer and MEP Modeler |
| STRUCT | 13 | bim-struct-mep | structural element details |
| ANALYSIS | 22 | analysis-site | energy evaluation, analytical model, terrain, geolocation |
| VIZ | 15 | render-anim | CineRender, Redshift, live links, 3D window |
| UI | 16 | ux-access-i18n | work environment, preferences, platform |
| NEW2930 | 19 | bim-arch | Archicad 29 and 30 additions |

## Method and limits (read before trusting)
- Research was clean room from public knowledge of vendor help, developer documentation and published announcement coverage. No vendor code or leaked material was used.
- **help.graphisoft.com, community.graphisoft.com and the developer-doc hosts were not reachable from this session** (the egress policy refused them, and the proxy status lists them as denied). Row content therefore comes from general public knowledge plus web search snippets, not from page-by-page reading. `source.doc` is the help chapter root (`/AC/29/INT/_AC29_Help/`), the GDL reference, the JSON API documentation or PyPI page, or a verified search-result URL (Archicad 30 new features pages, AEC Magazine announcement). Deep links to individual topics were not verified and are named in `notes` by chapter title only.
- The Archicad 30 feature list is drawn from public announcement coverage and two Graphisoft help pages seen in search results; exact release-note wording was not read. Re-verify NEW2930 rows once the host is allowed.
- Numeric figures in acceptance sketches are closed-form or definitional checks (volumes, areas, formulas), not measurements from Archicad. A licensed-copy walk-through (plan 4.3) is still needed to confirm completeness and behaviors.
- A quality gate removed 99 draft rows whose acceptance or superiority text was too thin; some of those topics are therefore not yet in the register (examples: several teamwork server details, dormer and roof accessories detail, more MEP library rows, structural timber detail). Target count of about 700 is not fully reached (678).
- Known thin areas: library management, cost and quantity workflows beyond schedules, energy and daylight detail, and CAD2D. A second pass with the help site reachable should add about 40 to 80 rows and replace the chapter-root `doc` URLs with deep links.
