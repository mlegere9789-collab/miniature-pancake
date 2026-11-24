# Revit: Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 1,034.

## Access route
* A .NET add-in (public Revit API, .NET 8 for current releases) with an external command and external event that reads `D8_CASES`, builds the
  case's model in a fresh project, measures, and writes golden JSONL. Revit has no true headless mode: a signed-in desktop session with a template-free
  new project is required. The add-in is loaded through the standard manifest; the first load needs the owner to accept the "unsigned add-in"
  prompt once.
* Cloud automation for Revit exists but would call Autodesk servers; not used (hard line: no vendor-server interaction beyond ordinary licensed use).
* Case models are created through the API from our own numbers. No Autodesk template, family or sample file is used; the few families that the API
  needs are created from scratch (extrusions, voids, parameters).

## What it can measure
| Family | Measured |
|---|---|
| Walls | length, area, volume, per-layer areas for layered types we define, joins at corners and T-junctions, openings deducted from area and volume |
| Floors, roofs, ceilings | area, volume, slope effects, shape-edit points, openings |
| Stairs and ramps | riser and tread counts for given heights and run rules; run lengths |
| Rooms and areas | boundary results, area, perimeter, computation height, separation lines |
| Hosted elements | door and window cut effects on host area and volume; curtain-wall grid and panel counts |
| Family geometry | extrusion, blend, revolve, sweep and void cuts: volume, area, bounding box |
| Parameters and formulas | formula evaluation (arithmetic, conditionals, trig, units), unit conversions and rounding via the public unit utilities |
| Geometry queries | solids from element geometry: face, edge counts, volume, area; join and cut geometry results |
| Schedules | summed quantities and rounding for schedules built by the API |
| Phases and options | element counts per phase or option |
| Interop | IFC export: entity counts, base quantities; DWG export: entity counts and extents |
| Structure and MEP | analytical node coordinates; pipe and duct sizing results where the API exposes the calculation |
| Tolerances | the public short-curve, vertex and angle tolerance properties are read and stored with every record |

## Tolerances
Internal units are feet, so every record stores model units. Geometry: relative 1e-6 or the recorded vertex tolerance, whichever is larger. Counts exact.
Rounding and display rules are compared as strings of digits only, never as vendor text.

## Cannot be measured headlessly
Rendering, graphic display and view templates, worksharing, cloud models, interactive sketch editing feel, generative or analysis services that run
on Autodesk servers.

## Unknowns (routed to research and own R&D)
Join-resolution order for three or more walls, room-boundary heuristics for unclosed regions, structural analytical model auto-generation.

## Needs from the owner
Revit installed (version), signed-in session, and acceptance of the add-in load prompt. Per-program G0: covered by `G0_BLANKET.md`.
