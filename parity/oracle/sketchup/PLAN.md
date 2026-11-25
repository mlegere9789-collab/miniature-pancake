# SketchUp: Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 635.

## Access route
* The public Ruby API inside a running SketchUp, started with a Ruby startup script (a public command-line option) that reads `D8_CASES`, builds each
  case in a new model, writes golden JSONL and quits. Needs SketchUp Pro for the solid tools.
* The public C API (an SDK, usable without the application running) for file-level checks: reading and writing models, entity counts, coordinates.
  The C API does not expose the interactive modelling tools, so geometry operations come from Ruby.
* Extensions, 3D Warehouse content, default templates and sample models are never used.

## What it can measure
| Family | Measured |
|---|---|
| Face and edge creation | auto-splitting, coplanar merging, face counts after overlapping geometry, orientation, area |
| Push/Pull, Follow Me, Offset | resulting face, edge counts, volumes of closed forms |
| Intersect Faces | edge and face counts, split results with coincident and tangent inputs |
| Solid tools (union, subtract, trim, split, intersect, outer shell) | volume, face count, manifold flag, result type; refusal behaviour for non-manifold inputs |
| Groups and components | transforms, scaling, instancing and definition counts, explode behaviour |
| Edge flags | soft, smooth, hidden effects on counts |
| Camera and view | view matrix, projection and pick-ray numbers for given eye, target and field of view |
| Dynamic-component formulas | evaluation of arithmetic and conditional formulas as numbers |
| Units | length parsing and formatting of numbers through the public Length conversions |
| Import and export | DXF, DWG, OBJ, STL, FBX, COLLADA: counts and coordinates (numbers only) |
| Tolerances | smallest edge and face sizes accepted (probed with sweeps), recorded as data |

## Tolerances
Internal unit is the inch: record in inches. Relative 1e-6 for lengths and areas, absolute 1e-4 inch near the geometry tolerance. Counts exact.
Failure to create a face or an edge is recorded as a result, not an error.

## Cannot be measured headlessly
Interactive tools (Tape, Rotate, Move with inference), the inference engine, style and shadow rendering output, LayOut documents (a separate SDK that can be a follow-up),
cloud services.

## Unknowns (routed to research and own R&D)
Inference priority rules, auto-fold logic beyond sweeps, exact internal tolerance behaviour at the 1/1000-inch scale.

## Needs from the owner
SketchUp Pro installed (state version, licensed and signed in), and the C SDK download. Per-program G0: covered by `G0_BLANKET.md`.
