# Rhino 8 and Rhino 9 beta parity register: coverage

Generated from public sources only (Rhino 8 User's Guide, Rhino 9 "New in Rhino 9" command list and rhino3d.com pages, developer.rhino3d.com guides). Every row is written in our own words; `source.doc` holds the page URL and `source.ref` the command, feature or setting name.

Total rows: **1356** (198 tagged `notes: "Rhino 9 beta"`).
Source kinds: command 1136, feature 159, setting 61.

## Rows per domain (file `<domain>.jsonl`)

| Domain | Rows | Scope |
|---|---|---|
| ANALYZE | 54 | measurement and analysis |
| ANNOT | 50 | dimensions, text, hatch, linetypes |
| CURVE | 158 | curve creation, editing, analysis points |
| DIGIT | 11 | digitizing arms |
| FILE | 37 | open/save/import/export |
| LAYOUT | 38 | layouts, details, clipping planes and drawings, print |
| LIC | 2 | licensing |
| MESH | 114 | mesh creation, repair, reverse-engineering tools |
| ORG | 118 | layers, blocks, groups, visibility, history, worksessions |
| RENDER | 75 | render, materials mapping, lights, animation |
| SCRIPT | 50 | scripting, Grasshopper entry points, developer platform, AI/MCP |
| SELECT | 125 | selection commands and filters |
| SETTING | 61 | Options and Document Properties pages |
| SOLID | 42 | solid primitives, Boolean, push/pull |
| SUBD | 46 | SubD creation and editing |
| SURF | 144 | surface creation, editing, fillets, trimming |
| UI | 23 | interface, panels, help |
| VIEW | 150 | views, display, snaps, gumball, window layout |
| XFORM | 58 | transforms, arrays, deformations |

## Documentation sections covered

- Rhino 8 Command Quick Reference (`commandlist/commandlist_command_list.htm`): every one of the 1,078 listed entries has at least one row. Commands that differ only by input method share a row; options that change the result (for example Contour curves vs points, Rebuild curve vs surface, Offset loose) have their own rows.
- Rhino 8 "New in Rhino 8" feature page: ShrinkWrap, SubD creases, PushPull, clipping and section styles, Auto CPlane, Gumball, procedural textures, linetypes.
- Rhino 8 Options pages and Document Properties pages (display modes, modeling aids, files, plug-ins, units, grid, hatch, linetypes, mesh, user text).
- Rhino 9 beta "New in Rhino 9" command list: every command listed under "New commands", plus the notable new options (Elmo/Rebuild to tolerance, Patch MultiBlend, setback fillets, PushPull multi-face, Inset group mode, SoftTransform, MoveUVN, GlobalEdgeContinuity, section styles, clipping-drawing blocks and auto-update, Markup, GD&T, MCP/AI, SubD Knife/MatchSubD, selection undo, per-viewport visibility, file I/O additions).
- Developer platform: RhinoCommon, Rhino.Compute and Hops, Rhino.Inside, Yak package manager and package restore, Python 3 and C# scripting, Script Editor, plug-in SDK, .NET runtime.

## Not covered (yet)

- Toolbar and menu layout pages (`toolbarmap`, `toolbarsandmenus`): no behavior of their own beyond the commands.
- Rhino 9 enhancement bullets that are small UI tweaks (for example SVG editor menu items, installer repair tool, splash text); only the enhancements that change a user outcome have rows.
- Method-level RhinoCommon, RhinoScript and RhinoScriptSyntax API (thousands of methods); only the entry points are rows.
- Third-party plug-ins and the Grasshopper 1 and 2 component catalogs, which belong to the `grasshopper` program register.
- McNeel Discourse threads and release-note archives were not mined for rows; no Discourse-only behaviors are included.
- Rhino 9 parametric constraint tooling: no public command or documentation page for it was found in the Rhino 9 command list or the "New in Rhino 9" page, so no rows were invented. The only constraint-related public items (UV Editor straightening constraints, SoftTransform boundary constraints, Sweep1 CPlaneZ axis) are recorded.

## Data notes

- `links.pain_points` is empty on every row: the 25-item pain-point list is not in the repository, so no numbers were guessed.
- `tier_now` is `None` for all rows; `status` is `open`; `evidence` is `null`.
- Ids are assigned in generation order per domain; they are stable only once this branch is merged.
- `target` is `Best` for exact NURBS, solid and SubD rows (CURVE, SURF, SOLID, SUBD) and `Strong` elsewhere.
