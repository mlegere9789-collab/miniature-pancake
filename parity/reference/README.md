# Reference-output harness (owner-run)

Status: **the scripts in `harness/` have never been run against the real applications.** They were written from public API documentation in a cloud session that has none of these programs installed. They were syntax-checked only (`tools/check_syntax.sh`; results at the end of this file). Expect a first run to need small fixes. Every scenario is wrapped in error handling, so a failing call is recorded in `measurements.json` with the exact error text and the run continues. Send that file back and the scripts get corrected from the real error messages.

## Licence caution (read before running anything)

Some end-user licence agreements restrict benchmarking, reverse engineering or automated use. **Counsel must confirm that scripted observation of each licensed program is permitted before any script is run** (plan section 4.5; report 13.5). Nothing here has been cleared. Each script header repeats this. If counsel says no for a program, skip that program.

## What the harness is for

The Register rows need correctness checks against how the licensed programs actually behave. The scripts produce **data files**: measured values (volumes, areas, bounding boxes, counts) and exported results for twenty fixed scenarios per program, plus a list of the program's commands where the program can list them, plus a manifest of version, build, operating system and settings. Build agents see only the test inputs and the expected numbers, never the program.

## Safety properties (what every script does and does not do)

| Property | How it is met |
|---|---|
| Read-only for your work | Never edits, saves or closes any file you have open. Rhino and Grasshopper use headless or in-memory documents. Revit creates its own new project in memory and closes it without saving. SketchUp, AutoCAD and Archicad refuse to run unless the open model is empty, build test geometry, then erase or abort it; none of them saves. Blender runs headless with factory settings. |
| Writes only to the output folder | Every file name goes through a path check that rejects paths outside the folder you chose. |
| No network calls | The scripts open no sockets. Archicad's Python package talks to your running Archicad on localhost. The Forma page runs inside your own signed-in Forma session and uses only the public embedded-view SDK. `serve.py` binds to 127.0.0.1 and makes no outbound calls. Installing the Python or npm packages named below is a separate step you do yourself. |
| No credentials | None are read, stored or sent. The Forma script never sees a token; it uses the session the browser already has. |
| No lasting setting changes | AutoCAD changes FILEDIA, CMDECHO and EXPERT during the run and restores them. Nothing else changes settings. |
| Reproducibility | `manifest.json` records program version, build, operating system, interpreter version, units, tolerances and the SHA-256 of the script. |

## Output folder (same for every program)

```
manifest.json       program, version, build, OS, settings, scenario counts, file list
measurements.json   one entry per scenario: status, measured values, expected closed-form values, seconds, error text if any
commands.json       commands/tools/components the program could list (see per-program notes)
scenario*_result.*  exported files (STEP, OBJ, DXF, 3DM, IFC and so on, whichever the program supports)
```

Expected values in `measurements.json` (for example `expected_volume`) are closed forms computed by the script from the scenario's dimensions; they are there to show where a program deviates, they are not the reference. The program's own measured number is the reference.

## Per-program instructions

Create an empty folder per run, for example `D:\dino8_ref\rhino_run1`. Set the environment variable `D8_REF_OUT` to it, or follow the prompt each script gives.

| Program | File | Language | How to run | Notes |
|---|---|---|---|---|
| Rhino 8 | `harness/rhino/d8_reference_rhino.py` | Python 3 (RhinoCommon) | Tools > Script Editor > open > Run | Uses a headless document. `commands.json` lists every command name (English and localized) and installed plug-ins. |
| Grasshopper | `harness/grasshopper/d8_reference_grasshopper.py` | Python 3 in Rhino 8 | Start Rhino, type `Grasshopper` once, then run from the Script Editor | Builds throw-away in-memory definitions; never touches an open definition. `components.json` lists every installed component (name, category, GUID, library). Component names are looked up by name and category, so a renamed component shows up as an error to fix. |
| Archicad | `harness/archicad/d8_reference_archicad.py` | Python 3, package `archicad` | `pip install archicad`, start Archicad, File > New (empty), then `python d8_reference_archicad.py --out <folder>` | Refuses to run if the project has elements. JSON parameter shapes are from memory of the public docs and are the most likely to need fixes. `commands.json` records which read-only API commands answered; the tool box and menus are not listable, see manual list below. |
| SketchUp | `harness/sketchup/d8_reference_sketchup.rb` | Ruby | File > New, Window > Ruby Console, `load 'C:/path/d8_reference_sketchup.rb'` | One operation, aborted at the end. `commands.json` lists the API classes and methods by reflection and installed extensions; menus and tools are not listable. No native fillet or loft exists, recorded as unsupported rather than emulated. |
| Revit | `harness/revit/d8_reference_revit.py` | pyRevit (IronPython 2.7 or CPython 3) | Run with pyRevit; optional `D8_REVIT_TEMPLATE` to name a template | Works in a new in-memory project. `commands.json` lists every PostableCommand and built-in category. Export steps need a 3D view and may need the IFC add-in. |
| AutoCAD | `harness/autocad/d8_reference_autocad.lsp` | AutoLISP with ActiveX | `QNEW`, `APPLOAD` the file, type `D8REF` | Refuses to run unless the drawing is empty. `commands.json` lists menus and toolbars with their macros, LISP-defined commands and about 130 system variables. Edge fillet and chamfer pick by point and depend on the view; a miss is recorded, not hidden. |
| Blender | `harness/blender/d8_reference_blender.py` | Python (bpy) | `blender -b --factory-startup --offline-mode --python d8_reference_blender.py -- --out <folder>` | Lists all operators with properties, modifier types, constraints, node types. Drop `--offline-mode` on builds that do not have it. |
| Forma | `harness/forma/` | JavaScript embedded view (public Apache-2.0 SDK 0.96.0) | See below | Runs inside your signed-in Forma project; reads only unless you tick the sandbox box. |

### Forma, step by step

1. In `harness/forma/` run `npm install` then `npm run build` (this is the only network step and it is yours; the SDK and the bundler come from the public npm registry).
2. `python3 serve.py` (serves `index.html` on 127.0.0.1:8080).
3. Register `http://127.0.0.1:8080/index.html` as an embedded view following Autodesk's Forma developer getting-started guide (https://aps.autodesk.com/en/docs/forma/v1/overview/getting-started), open a project, open the view.
4. Optional: create a separate project named `D8-REF-sandbox`, draw the three fixed test sites below, and tick the sandbox box so the sun and noise analyses run on them. Without the box ticked those three scenarios are recorded as skipped.
5. Click **Run 20 scenarios**; choose an output folder first (Chromium browsers) or use **Download results**.

Fixed test sites for the sandbox project (draw by hand, WGS84 location of your choice recorded in the manifest): (A) one 100 m x 60 m x 40 m rectangular block on flat ground; (B) two parallel 100 x 20 x 30 m slabs 20 m apart; (C) a 100 m square site with a single 12 m tall house. The wind scenarios do not need them; they use a synthetic raster inside the script so they are reproducible without any project.

## The twenty scenarios

Common core (names differ slightly where a program has its own way): box 100x60x40, cylinder r15 h40, sphere r25, edge fillet, edge chamfer, boolean difference, union, intersection, offset of a closed rectangle, circle properties, extrude a profile, revolve a profile, loft or taper to a frustum, rotate and scale, linear array of 5, mesh or triangulation statistics, curve and plane intersections, shell or inward offset, split by a plane, export to the program's open formats. BIM programs replace some with levels, walls, floor, roof, room, schedule and area values (Revit), column, slab, zone, polyline and property values (Archicad). Forma runs the project, terrain, area-metric, geometry, sun, noise and wind-comfort calls listed in `harness/forma/src/harness.js`. The exact list is the order of functions named `sNN_...` in each script.

## Manual completion list for what scripts cannot list (owner, once per program)

- Rhino, Revit, AutoCAD, SketchUp, Archicad, Forma: walk the toolbar and menu tree and photograph or export it (AutoCAD: `CUIEXPORT` or copy the CUIx; Rhino: `_-Options` toolbar export; Revit: ribbon screenshots per tab; Archicad: Tool Box and Options > Customize; SketchUp: Window > Preferences > Shortcuts list; Forma: tool bar screenshots). This feeds the "licensed copy walk-through" completeness checks in plan section 4.3.
- Archicad file exports (IFC, DXF/DWG, 3D formats) cannot be driven from the JSON API: follow `MANUAL_EXPORT_STEPS.txt` written into the output folder.

## Uploading

1. Look through `manifest.json` and `commands.json`: they contain operating system, versions, plug-in names and (Forma) the project id. Remove anything you do not want shared.
2. Zip the folder as `<program>_<version>_<os>_<yyyymmdd>.zip`.
3. Upload to the cloud storage location the owner designates for reference data. Agents never depend on your machine being on.
4. Do not add third-party models, templates or licensed content libraries to the folder.

## Syntax checks run in the cloud session (not proof of function)

`bash parity/reference/tools/check_syntax.sh` compiles every Python file with `compile()`, runs `ruby -c` on the SketchUp script, `node --check` on the Forma script, and a parenthesis balance check on the AutoLISP file. All passed when this was written. The AutoLISP and Archicad JSON shapes and the Grasshopper component lookups are the parts most likely to need correction.
