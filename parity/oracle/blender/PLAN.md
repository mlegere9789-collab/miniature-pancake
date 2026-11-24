# Blender: Phase B plan (design only; nothing has been run)

Conventions: see ../OBSERVER_DECISIONS.md. Register rows: 2,987 (largest program).

## Access route
* Headless Python: `blender --background --factory-startup --python runner.py` with the bundled Python API (`bpy`, `bmesh`, `mathutils`,
  depsgraph-evaluated objects). No GUI needed for geometry, modifiers, geometry nodes, animation evaluation, I/O and Cycles on CPU.
* Record both the current LTS and the newest stable release (two builds; behaviour changes between them are themselves data).
* Source is public but GPL: behaviour evidence only. Nothing is copied or ported into Dino 8; algorithm cards, if any, come from behaviour
  and published papers, never from reading the source into Implementers' hands.

## What it can measure (public API)
| Family | Measured numbers and flags |
|---|---|
| Primitives and mesh stats | vertex/edge/face/loop counts, area, volume (bmesh), bounding box, manifold flags, Euler characteristic |
| Mesh operators | extrude, inset, bevel, bridge, fill, triangulate, merge-by-distance, dissolve: result counts, area, volume, vertex positions on closed-form shapes |
| Modifiers | Boolean (both solvers, all three operations), Bevel, Subdivision Surface (counts plus limit-surface position of a known cube vertex), Solidify, Mirror, Array, Decimate, Remesh, Weld, Displace, Screw |
| Curves | Bezier and NURBS evaluation, length, tessellation counts, bevel/extrude results |
| Transforms | euler orders, matrix composition, parenting, constraints, drivers: 4x4 matrix numbers |
| Rigging | armature deform of a known mesh: per-vertex positions, shape keys, vertex-group weights |
| Geometry Nodes | attribute outputs of small graphs we author (math, field evaluation, instancing counts) |
| UV | unwrap stretch/area-distortion statistics on closed forms (not coordinates) |
| Animation | F-curve interpolation (Bezier handles, easing) sampled at fixed frames; NLA blending numbers |
| Physics | short-horizon rigid-body drops with closed-form answers; cloth and fluid as statistics only |
| Cycles (CPU, fixed seed) | mean and variance of pixel luminance on analytic scenes (furnace, Lambert plane, point-light inverse square) |
| Colour management | pixel values of ramps through each view transform and look |
| I/O | OBJ, STL, PLY, glTF, FBX, USD, Alembic: round-trip counts and coordinates |

## Tolerances
Single-precision geometry: relative 1e-5 plus absolute 1e-5 times scale. Counts exact. Cycles: the golden record stores the variance over
at least 8 seeds, and Dino 8's mean must fall inside 3 standard errors. Physics: only closed-form-checkable cases get numeric tolerances.

## Cannot be measured headlessly
Eevee and viewport output (needs a GPU context; verify at setup whether background mode works on the owner's GPU), sculpt and paint strokes,
interactive operators that need a window context, Grease Pencil drawing feel, add-on UI. These go to research notes.

## Unknowns (not resolvable by observation; routed to research and own R&D)
Exact remesh and decimate heuristics, Boolean fast-solver tie-breaks beyond what sweeps show, fluid and cloth solver internals.

## Needs from the owner
Blender LTS and latest stable installs (free); confirm the GPU model for later Eevee checks. G0 entry: covered by `G0_BLANKET.md`; the licence is GPL so
running it is unrestricted. Nothing else.
