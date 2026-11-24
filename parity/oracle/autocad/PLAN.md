# AutoCAD: Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 2,765.

## Access route
* Preferred: the no-GUI core console with a script file, loading a small .NET assembly we write against the public managed API
  (references to the public API assemblies are used only to compile; their contents are not inspected). The .NET 8 SDK is already installed;
  recent AutoCAD releases target .NET 8.
* Fallback: full application automation through COM, or AutoLISP scripts (public language) when the core console lacks a command.
* Open question to settle at setup: whether the core console runs under the owner's licence without a licence error (forum reports are mixed). If it
  does not, run the same assembly inside the full application with a startup script, which needs a desktop session.

## What it can measure
| Family | Measured |
|---|---|
| 2D entities | line, arc, circle, ellipse, spline (fit and control-vertex), polyline with bulges: length, area, extents, closest-point results, tessellation vertex lists |
| Curve operations | offset (inside, outside, corners), trim, extend, fillet, chamfer, break: resulting vertex lists and counts |
| Regions and hatch | region Boolean results, mass properties, hatch area for patterned and solid boundaries including islands |
| 3D solids | box, sphere, cone, cylinder, torus, wedge, pyramid, extrude, revolve, sweep, loft; Boolean union, subtract, intersect; slice, section, shell, offset, thicken, fillet and chamfer edges, interference check; mass properties (volume, centroid, inertia) |
| Surfaces and meshes | surface offset, blend, patch counts; mesh smoothing levels; mesh-to-solid conversion counts |
| Dimensions and units | measured values for linear, aligned, angular, radial, ordinate; unit-format conversion of distances and angles through the public converter |
| Blocks, xrefs, layouts | block and attribute transform matrices, viewport scale and transform numbers, table cell extents |
| Files | DWG and DXF round-trip: entity counts, extents, vertex coordinates; DXF coordinate precision |
| System variables | default numeric values of public system variables (names as identifiers only) |
| Precision | public tolerance settings, object-snap result points on closed forms |

## Tolerances
2D doubles: relative 1e-9. Solid properties: relative 1e-6 (solid-modeller tolerance class), area and volume of curved solids 1e-5. Counts exact.
Refusal types (exception names) are recorded as data for degenerate Booleans and fillets.

## Cannot be measured headlessly
Graphics regeneration, hidden-line and shaded views, live plotting fidelity, in-command prompts (SOLIDEDIT sub-options), selection cycling,
ribbon and palette behaviour, cloud features. Command aliases and UI text are not recorded.

## Unknowns (routed to research and own R&D)
Spline fit tolerances in offset, hatch seed-point heuristics, solid-modeller healing choices not visible in outputs.

## Needs from the owner
AutoCAD installed (state version and whether Core Console is available), a licence session that is signed in, and a one-time approval for loading our unsigned
.NET assembly (trust path or sign it with a self-made certificate). Per-program G0: covered by `G0_BLANKET.md`; the owner may still want to read the
AutoCAD licence terms (a note, not a gate).
