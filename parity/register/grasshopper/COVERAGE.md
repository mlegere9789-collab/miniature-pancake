# Grasshopper parity register: coverage checklist

Scope of this job (after the coordinator scope update): GH1 core components (all tabs), data trees and data matching, solver and canvas, file formats, Galapagos, clusters and scripting, and GH2 differences. The top-40 plugin rows (ids D8-GH-<DOMAIN>-5001 and up, files `plugins-*.jsonl`) are owned by another worker. Ids in this job stay below 5000.

Method and limits:

- One research pass from public-documentation knowledge. The documentation hosts (developer.rhino3d.com, food4rhino, grasshopper3d, docs sites) were blocked by the session network policy, so no page could be fetched live. `source.doc` holds the canonical public API/primer/discourse URL for each group, not a verified per-component page.
- Component names and tab placement are reconstructed offline and carry a verification note in every row. The plan asks for two independent research passes compared; only one pass exists, so a second pass (with network access) must diff names against the Rhino 8 component index before the Register freeze on 30 Nov.
- Every row starts at tier_now None, status open. Acceptance sketches are test outlines, not implemented tests; test ids `gh.<domain>.<nnnn>` are reserved.
- Combined rows (for example arithmetic operators, gates, trig triples) are used only where one acceptance test covers all members; the row name lists the members.

## Counts by group

| Domain | Group | Rows | Factory | Target |
|---|---|---:|---|---|
| PARAMS | Params > Geometry | 24 | flow | Strong |
| PARAMS | Params > Primitive | 16 | flow | Strong |
| PARAMS | Params > Input | 19 | flow | Best |
| PARAMS | Params > Util | 11 | flow | Best |
| MATHS | Maths > Domain | 12 | flow | Strong |
| MATHS | Maths > Operators | 17 | flow | Strong |
| MATHS | Maths > Trig | 13 | flow | Strong |
| MATHS | Maths > Polynomials | 10 | flow | Strong |
| MATHS | Maths > Time | 9 | flow | Strong |
| MATHS | Maths > Matrix | 11 | flow | Strong |
| MATHS | Maths > Script | 8 | scripting-api-ai | Best |
| MATHS | Maths > Util | 29 | flow | Strong |
| SETS | Sets > List | 29 | flow | Best |
| SETS | Sets > Sequence | 4 | flow | Strong |
| SETS | Sets > Sets | 6 | flow | Strong |
| SETS | Sets > Text | 11 | flow | Strong |
| SETS | Sets > Tree | 18 | flow | Best |
| SETS | Sets > List (additions) | 6 | flow | Strong |
| SETS | Sets > Sequence (additions) | 2 | flow | Strong |
| SETS | Sets > Text (additions) | 10 | flow | Strong |
| SETS | Sets > Tree (additions) | 7 | flow | Best |
| VECTOR | Vector > Vector | 16 | flow | Strong |
| VECTOR | Vector > Point | 27 | flow | Strong |
| VECTOR | Vector > Plane | 17 | flow | Strong |
| VECTOR | Vector > Grid | 8 | flow | Strong |
| VECTOR | Vector > Field | 14 | flow | Strong |
| CURVE | Curve > Analysis | 30 | flow | Strong |
| CURVE | Curve > Division | 7 | flow | Strong |
| CURVE | Curve > Primitive | 20 | flow | Strong |
| CURVE | Curve > Spline | 17 | flow | Strong |
| CURVE | Curve > Util | 23 | flow | Strong |
| SURFACE | Surface > Analysis | 21 | flow | Strong |
| SURFACE | Surface > Freeform | 19 | flow | Strong |
| SURFACE | Surface > Primitive | 18 | flow | Strong |
| SURFACE | Surface > SubD | 6 | flow | Best |
| SURFACE | Surface > Util | 13 | flow | Strong |
| MESH | Mesh > Analysis | 19 | flow | Strong |
| MESH | Mesh > Primitive | 6 | flow | Strong |
| MESH | Mesh > Triangulation | 7 | flow | Strong |
| MESH | Mesh > Util | 28 | flow | Strong |
| XSECT | Intersect > Mathematical | 15 | flow | Strong |
| XSECT | Intersect > Physical | 5 | flow | Strong |
| XSECT | Intersect > Region | 3 | flow | Strong |
| XSECT | Intersect > Shape | 6 | flow | Strong |
| XFORM | Transform > Affine | 14 | flow | Strong |
| XFORM | Transform > Array | 4 | flow | Strong |
| XFORM | Transform > Euclidean | 0 | flow | Strong |
| XFORM | Transform > Morph | 5 | flow | Strong |
| DISPLAY | Display > Colour | 9 | flow | Strong |
| DISPLAY | Display > Preview | 6 | flow | Best |
| DISPLAY | Display > Dimensions | 5 | flow | Strong |
| DISPLAY | Display > Vector | 2 | flow | Strong |
| DISPLAY | Display > Graphs | 3 | flow | Strong |
| TREES | Data trees and data matching | 24 | flow | Best |
| CORE | Solver and canvas engine | 25 | flow | Best |
| CORE | File formats and persistence | 7 | interop-migration | Best |
| CORE | Galapagos and optimisation | 6 | simulation | Best |
| SCRIPT | Clusters, scripting and developer API | 22 | scripting-api-ai | Best |
| GH2 | Grasshopper 2 differences | 15 | flow | Best |
| CORER8 | Rhino 8 Grasshopper additions | 9 | flow | Strong |

Total rows: 773

## Checklist against the nine-tab structure

- [x] Params: Geometry, Primitive, Input, Util
- [x] Maths: Domain, Operators, Trig, Polynomials, Time, Matrix, Script, Util
- [x] Sets: List, Sequence, Sets, Text, Tree
- [x] Vector: Vector, Point, Plane, Grid, Field
- [x] Curve: Analysis, Division, Primitive, Spline, Util
- [x] Surface: Analysis, Freeform, Primitive, SubD, Util
- [x] Mesh: Analysis, Primitive, Triangulation, Util
- [x] Intersect: Mathematical, Physical, Region, Shape
- [x] Transform: Affine, Array, Euclidean, Morph (Transform > Util not yet enumerated)
- [x] Display: Colour, Preview, Dimensions, Vector, Graphs
- [x] Built-ins: solver and canvas, file formats, Galapagos, clusters, scripting, SDK, data trees and matching
- [x] GH2 differences named in the report; Rhino 8 additions (partial)

## Known gaps (not yet covered)

- Roughly 1,000 GH1 components exist in the public list; this register enumerates only the components whose names could be recalled with confidence. Long-tail components in Curve > Util, Surface > Util, Mesh > Util, Transform > Util, Params > Util, Sets > Text and Maths > Util remain to be added once a live component index is available.
- Plugins: out of scope for this job (other worker).
- Alias rows for GH1 components renamed in Rhino 8 (for example Rhino 8 script component renames) not yet recorded.

## Validation

All rows validate against `schema/row.schema.json` (jsonschema, Draft 7), ids are unique, and no row contains the forbidden word. Acceptance items carry an extra `sketch` field with the test outline (schema permits extra keys on acceptance items).
