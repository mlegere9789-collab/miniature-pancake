# ACIS SAT (.sat text) notes - clean-room record

Implementation: `src/acis_sat.cpp`, `include/dino8/kernel/acis_sat.h`.
Round 1 covers the TEXT format only (no `.sab`).

## Clean-room statement

Every line of the reader and writer was written from scratch. No ACIS SDK
code, no decompiled/reverse-engineered binary, and no parser source code from
any other project (ezdxf, acadrust, FreeCAD gists, ...) was read or copied.
Only prose documentation was used, plus the author's own fixtures.

## Documents actually used

1. ezdxf documentation, "ACIS Tools" (prose, MIT-licensed docs):
   https://raw.githubusercontent.com/mozman/ezdxf/stable/docs/source/acis.rst
   (file `docs/source/acis.rst`, fetched with curl, read in full; 17298 bytes).
   It documents, entity by entity, the attributes of: Body, Lump, Shell, Face,
   Loop, Coedge, Edge, Vertex, Transform, Plane, StraightCurve, Point, and the
   generic entity (`attributes`, `id`). Only this prose was used, not the
   `ezdxf.acis` Python source.
2. Web-search result summaries (titles/snippets only; the pages themselves were
   NOT readable): Spatial "SAT Save File Format" chapter copies (Paul Bourke's
   `sat.pdf`, Scribd copy, CSDN copy), ACIS R10 Kernel "SAT Save File Format"
   chapter (University of Arizona mirror), a UMD example page.
   Those hosts do not resolve from this environment (`getaddrinfo ENOTFOUND`
   for paulbourke.net, docs.rs, scribd.com, blog.csdn.net, isr.umd.edu,
   www-isl.ece.arizona.edu). Spatial's own "ACIS SAT Save File Format"
   document could therefore NOT be read in round 1. **Nothing below is
   verified against it.** The next round should allow those hosts (or the
   sat.pdf URL) and re-check every UNVERIFIED line.

## What the documentation states (VERIFIED by item 1)

| Entity | Fields, in the order the documentation lists them |
|---|---|
| any entity | `attributes` (pointer), `id` (int, -1 if unset) |
| body | pattern, lump, wire, transform |
| lump | next lump, shell, body |
| shell | next shell, subshell, face, wire, lump |
| face | next face, loop, shell, subshell, surface, sense (reversed = true), sides (single/double), containment (in/out when double) |
| loop | next loop, coedge, face |
| coedge | next, previous, partner, edge, loop, pcurve (circular lists; partners circular) |
| edge | start vertex, start param, end vertex, end param, coedge, curve, sense (reversed = edge runs against its curve), convexity (always the string "unknown") |
| vertex | edge, point |
| point | location (x y z) |
| plane-surface | origin, normal (unit), u direction (unit), reverse_v flag (reverse_v: left-handed, normal = v x u; forward_v: normal = u x v), u/v bounds (may be infinite) |
| straight-curve | origin, direction (unit), bounds (may be infinite) |
| transform | affine matrix |

Semantics also stated: face sense is the direction of the face normal relative
to the surface; a single-sided face's normal points away from the material;
double-sided faces belong to hollow (sheet) objects; an edge's start/end
parameter evaluates to its start/end vertex; shells are disconnected.

## What the documentation does NOT state (UNVERIFIED - implemented from recollection)

These were taken from the author's recollection of the published format and
from self-consistency only. They are exercised by tests but have never been
compared with a file written by a real ACIS-based product.

* Header: line 1 `version records entities flags`; line 2 three length-prefixed
  strings (`N text` or `@N text`); line 3 `units resabs resnor`. Reader accepts
  versions 400..1100 and refuses all others; the writer emits version 700.
* Text syntax: optional `-N` record index prefix (must equal the record's
  position), `$N` pointers (`$-1` null), `#` record terminator, `@N text`
  strings, `End-of-ACIS-data` trailer. Braces `{}` (sub-types) are tokenised
  but any entity using them is refused.
* The per-record header: the documentation lists `attributes` and `id` only.
  The writer emits THREE leading items (`$attrib id $history`, i.e.
  `$-1 -1 $-1`); the reader infers 1, 2 or 3 leading items from the field
  count of the first `point` record (4, 5, 6 tokens).
* The position of the coedge `sense` token (between `edge` and `loop`): the
  documentation's coedge list omits a sense attribute, but a coedge needs one.
* The `I` / `F value` parameter-range tokens after the plane and
  straight-curve data (4 for surfaces, 2 for curves).
* The `@7 unknown` string token for edge convexity (the documentation says the
  value is the string "unknown", not how it is encoded).
* `cone-surface` (center, axis, major-axis vector with |major| = radius, ratio,
  sine, cosine of the half angle, optional u-scale, sense, 4 ranges),
  `sphere-surface` (center, radius, u vector, pole vector, sense, ranges),
  `torus-surface` (center, axis, major, minor, optional reference direction,
  sense, ranges), `ellipse-curve` (center, normal, major vector, ratio, 2
  ranges; parameterisation `C + cos t * major + sin t * (normal x major) *
  ratio`). **None of these appear in the documentation used.** They are
  therefore REFUSED by default (`AcisSatOptions::allow_unverified_layouts =
  false`) on read and on write, with a reason string that says UNVERIFIED.
  Tests that exercise them opt in explicitly and are labelled "UNVERIFIED
  layout" - they prove self-consistency (write-then-read, hand-written fixture
  vs analytic volume), not compatibility with real files.
* The sign convention of a cone's sine half-angle: the reader fits both signs
  against the edge geometry and refuses if neither matches.
* The orientation of faces is taken from the loop winding (right-hand rule
  about the outward normal) rather than from the stated sense; a disagreement
  adds a warning.

## Mapping to the kernel Brep

* Reader builds genuine `ON_Brep` topology (shared vertices/edges, one trim per
  coedge, exact NURBS surfaces: bilinear plane patch, exact rational cone/
  cylinder). A periodic cone/cylinder face with two circle loops gets a
  synthesized seam edge (requires both circles to start at the same angle).
  Whole-sphere / whole-torus faces (no loop) use `Brep::Sphere` / `Brep::Torus`.
* Writer emits one surface and one curve entity per face / edge, classifying
  with OpenNURBS' `IsPlanar/IsCylinder/IsSphere/IsTorus` and `IsLinear/IsArc`;
  seam and singular trims of periodic faces are dropped (ACIS periodic faces
  carry one loop per boundary circle). Open bodies are written double-sided.
* Units: coordinates and radii are multiplied by the header `units` value on
  read; the writer writes units 1.

## Refused on read (each with a reason in `error` / `warnings`)

Unsupported versions, truncated data, dangling pointers, wire bodies, subshells,
non-identity body transforms, unknown or UNVERIFIED entity types, elliptical
cones, cone apex faces, trimmed sphere/torus faces, loops that wind more than
once other than the two-circle periodic case, seam misalignment, any B-spline /
`spline-surface` / `intcurve-curve` / `exactsur` / `procedural` geometry.

## Refused on write

NURBS surfaces and non-line/non-circle edge curves; trimmed sphere/torus faces;
(unless opted in) cone/sphere/torus surfaces and circular arcs.
