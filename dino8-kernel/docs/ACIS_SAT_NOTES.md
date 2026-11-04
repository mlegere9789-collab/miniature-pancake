# ACIS SAT (.sat text) notes - clean-room record

Implementation: `src/acis_sat.cpp`, `include/dino8/kernel/acis_sat.h`.
Round 1 covered the TEXT format only; round 3 (below) adds the binary `.sab` encoding with an UNVERIFIED tag table.

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

## Round 2: public documentation re-check (what could and could not be added)

Fetched again this round (WebFetch, raw.githubusercontent.com only; the GitHub
API, the repository file-listing tool and every other host were not reachable):

* https://raw.githubusercontent.com/mozman/ezdxf/master/docs/source/acis.rst -
  the same document as round 1. It names these entities: Transform, Body,
  Pattern, Lump, Wire, Shell, Subshell, Face, Loop, Coedge, Edge, Vertex,
  Surface, Plane, Curve, StraightCurve, PCurve, Point. Pattern, Wire, Subshell
  and PCurve are marked "Not implemented" there. `cone-surface`,
  `sphere-surface`, `torus-surface`, `ellipse-curve`, `intcurve-curve` are NOT
  described (`spline-surface` appears only as a name in the Face description).
  `.sab` appears only as the name of the binary export (`export_sab`, for DXF
  R2013-R2018 data); no byte layout is given.
* https://raw.githubusercontent.com/mozman/ezdxf/master/docs/source/dxfentities/3dsolid.rst -
  describes the DXF 3DSOLID entity only; no SAT record layout.

Consequences: nothing on the "not supported" list could be justified from text
that was actually fetched, so the reader's accepted set is UNCHANGED this
round. In particular: still no cone/sphere/torus/ellipse verification, no body
transforms (the `Transform` entity's attribute is documented as a matrix, but
how the matrix and its flags are serialised in a SAT record is not), no wire
bodies or subshells (documented as unimplemented), no splines/procedural
geometry, and **no binary `.sab`** (no public byte-layout documentation was
found to cite). The ezdxf Python source was again deliberately not read
(clean-room rule above).

## Application wiring (round 2)

`dino8-app/src/io/FileSat.{h,cpp}`: `ImportSat` / `ExportSat`, dispatched by the
`.sat` extension from `Application::OpenDocument`, `ImportFile`, `SaveDocument`
(Save As), `ExportSelected`, the Open / Import / Export / SaveAs / ExportWithOrigin
file dialogs and extension lists (`cmd_file.cpp`), the command-line open path
(`main.cpp` calls `OpenDocument`) and the drawing-compare loader
(`DwgCompare.cpp`). The application has no drag-and-drop handler at all, so
there is nothing to wire there.

Policy, stated so nobody mistakes it for verification:

* Import first reads with the kernel default (documented layouts only). If the
  file also contains cone/sphere/torus/ellipse entities, it is read a second
  time with `allow_unverified_layouts`, and the printed summary says
  `UNVERIFIED LAYOUT: N of M bodies use ...`.
* Export tests each Brep with the documented layouts first, then with the
  UNVERIFIED ones; bodies needing the latter are marked on their summary line and
  the summary carries an `UNVERIFIED LAYOUT` paragraph. Objects the writer cannot
  express (meshes, curves, points, SubDs, NURBS-surface bodies, trimmed
  sphere/torus faces, ...) are left out and listed under `Not written:` with the
  writer's reason; if nothing can be written the export fails with the reasons.
* Every summary ends with the sentence that no real ACIS-written file has been
  used to test the reader/writer.
* Coordinates are taken as document units (no unit conversion); the writer
  writes header units 1 and does not record the document unit system.

## Measured round trips (round 2, kernel test output, this build)

All are write-then-read with this library only (self-consistency, not
compatibility with another product). "documented" = planes and straight curves
only, no opt-in needed; "UNVERIFIED" = needed the opt-in.

| Body | Layouts | Volume before -> after | Area before -> after | F / E / V before -> after |
|---|---|---|---|---|
| box 2x3x4 (`Brep::BoxWelded`) | documented | 24 -> 24 | 52 -> 52 | 6/12/8 -> 6/12/8 |
| app-style box 20x20x10 (`ON_BrepBox`, as the Box command builds it) | documented | 4000 -> 4000 | 1600 -> 1600 | 6/12/8 -> 6/12/8 |
| boolean pocket (box 4^3 minus 2x2x3 blind pocket) | documented | 52 -> 52 | 120 -> 120 | 46/92/48 -> 46/92/48 |
| boolean through-hole (4^3 minus 2x2 hole) | documented | 48 -> 48 | 120 -> 120 | 32/64/32 -> 32/64/32 |
| boolean L-shaped union | documented | 24 -> 24 | 56 -> 56 | 14/28/16 -> 14/28/16 |
| cylinder r2 h5 (`Brep::Revolve`) | UNVERIFIED | 62.8318 -> 62.8234 (mesh) | 87.9646 -> 87.9544 (mesh) | 3/5/4 -> 3/3/2 (seam edge dropped) |
| app-style cylinder r5 h10 (`ON_BrepCylinder`) | UNVERIFIED | 785.2923 -> 785.2923 (mesh) | 471.1754 -> 471.1754 (mesh) | 3/3/2 -> 3/3/2 |
| sphere r2.5 (`Brep::Sphere`) | UNVERIFIED | 65.44997 -> 65.44997 | 78.53996 -> 78.53996 | 1/0/0 -> 1/0/0 |
| app-style sphere r4 (`ON_BrepSphere`) | UNVERIFIED | not measurable at source (RevSurface); read back 268.0831 vs analytic 268.0826 | read back 201.0623 vs analytic 201.0619 | 1 face |
| torus | UNVERIFIED | 59.2176 -> 59.2176 | 118.4353 -> 118.4353 | 1/0/0 -> 1/0/0 |
| filleted box | UNVERIFIED | 23.89270 -> 23.89270 (mesh) | 51.46350 -> 51.46350 | 7/15/10 -> 7/15/10 |

The boolean rows are the "trimmed faces" case: faces with an inner loop and
non-convex outer loops come back with identical face/edge/vertex counts and
volume/area to 1e-9, using documented layouts only.

### Measured in the application (headless, `tests/satf_script.txt`, `satf_bool_script.txt`)

Export (selected bodies) then Open/Import of the written file, in the real app.
The app's `Volume`/`Area` commands are tessellation based for non-NURBS
surfaces, hence the small deviations from the analytic values.

| Body (as the app's commands build it) | Layouts | Before -> after |
|---|---|---|
| Box 20x20x10 | documented | Volume 4000 -> 4000, area 1600 -> 1600, bbox 20x20x10 -> 20x20x10, 6/12/8 -> 6/12/8 |
| Box 4x6x8 via `SaveAs x.sat` then Open | documented | Volume 192 -> 192 |
| BooleanDifference: 20x20x10 box minus a 10x10 through-hole box | documented | Volume 3000 -> 3000, area 1800 -> 1800, bbox 20x20x10 -> 20x20x10, 32/64/32 -> 32/64/32 |
| Cylinder r5 h10 | UNVERIFIED | Volume re-opened 784.8 (analytic 785.4), 3/3/2 -> 3/3/2 |
| Sphere r8 | UNVERIFIED | Volume re-imported 2132 (analytic 2144.7), area 802 (804.2) |
| MeshBox | refused | "SAT export wrote nothing ... Brep bodies only" |
| text file that is not ACIS | refused | "SAT import refused (...): malformed header line 1" |

Finding fixed on the way: a whole-sphere body from a file (one face, no edges)
used to import as an object the application could neither display nor measure
(bounding box, Volume and Area all 0). `ImportSat` now rebuilds such a body as
the same native `ON_BrepSphere` / `ON_BrepTorus` the Sphere / Torus commands
create (outward orientation) and says so in the summary.

Not measured in the application: torus and the other UNVERIFIED shapes beyond
the table (the kernel table above covers them); Windows (no Windows machine).

## Round 3: binary `.sab` (ReadAcisSab / WriteAcisSab / ReadAcis)

Implementation: same `src/acis_sat.cpp`. SAB shares the entity/record layer with
the text reader: `ParseSab()` produces the same `Rec`/`Tok` stream as
`ParseSat()`, so every entity builder, refusal and hardening check below the
tokenizer is one code path; the writer builds one encoding-neutral record list
and either prints it (SAT) or packs it (SAB). `ReadAcis` picks the encoding from
the first 15 bytes (`ACIS BinaryFile` means SAB, anything else is tried as text);
`ReadAcisSat` on a SAB file and `ReadAcisSab` on text each refuse with a reason
that names the other encoding.

### Sources: none for the byte layout

The only public document used (ezdxf "ACIS Tools", re-fetched this round)
mentions SAB solely as the name of an export function for DXF R2013-R2018 data
and gives no byte layout. No other host on the network allowlist could be read.
**Every SAB fact below is therefore UNVERIFIED**: the token tag values are the
author's unsourced recollection of the published format, and the places where
recollection was silent are first-principles choices (marked "chosen"). The
full table is in the comment block "SAB wire format" in `acis_sat.cpp`. In short:

| UNVERIFIED item | Value used |
|---|---|
| signature | `ACIS BinaryFile` (15 bytes) then four untagged little-endian i32: version, record count, entity count, flags (layout chosen) |
| header strings / numbers | three strings (tag 0x07, 8-bit length), three doubles (tag 0x06): units, resabs, resnor |
| numeric tags | 0x01 byte, 0x02 char, 0x03 short, 0x04 int, 0x05 float, 0x06 double, 0x13 position (3 doubles), 0x14 vector (3 doubles) |
| strings | 0x07 / 0x08 / 0x09: 8 / 16 / 32-bit length prefix |
| other tags | 0x0A true, 0x0B false, 0x0C pointer (i32), 0x0D name, 0x0F `{`, 0x10 `}`, 0x11 record terminator |
| two-valued words | encoded as bools (chosen): forward=false/reversed=true, single=false/double=true, in=false/out=true, forward_v=false/reverse_v=true, range `I`=false, range `F value`=true then a double |
| record framing | entity name is one 0x0D token holding the dashed name (`plane-surface`), not split; no record index (position is the index); the end record is the name `End-of-ACIS-data` plus a terminator (chosen) |
| not accepted | tags 0x0E, 0x12, 0x15 and every other value: refused with a reason naming the tag |

On top of that, everything already UNVERIFIED for SAT stays UNVERIFIED for SAB:
the three-item record header, the coedge sense position, cone/sphere/torus/ellipse
layouts (still refused unless `allow_unverified_layouts` is set), and so on. The
real format is very likely different in details this table cannot show (for
example how split entity names, enumerations and sub-type braces are encoded),
so a real `.sab` file from another product will probably be refused, or worse
misread; none was available to test. The SAB writer is round-trip tested against
this library's reader and against a second encoder written in the test file from
the same table; **its output has never been read by any other product and must
not be called ACIS-compatible.**

### Hostile-input hardening (SAT and SAB share it unless noted)

* Input larger than 128 MiB, more than 2 Mi tokens or 1 Mi records: refused with
  the limit named; the reader stops reading at the limit instead of draining the stream.
* SAB: every read is bounds-checked against the bytes left; string lengths are
  checked against the remaining data before any allocation; a header record
  count that cannot fit in the file, negative counts, an unknown tag, a missing
  or unterminated end record, unbalanced or more than 64-deep braces, non-printable
  entity names and a missing terminator are all refused with the byte offset.
* Non-finite doubles (inf/nan, binary or text) are refused (SAB at the byte
  level, SAT in the number accessor and in the header).
* Pointers outside the record range are refused (as before); lump, shell, face,
  loop chains and coedge rings are now cycle-checked with visited sets instead of
  fixed iteration counts, and a work budget (64 record visits per record plus
  100000) stops files whose pointers make the builder revisit records.
* Every failure comes back in `error` / `warnings`; the entry points catch
  `std::bad_alloc` and other exceptions as well.
* Not hardened: opennurbs itself on adversarial but finite geometry (huge magnitudes
  etc.) is only covered by the fuzz run below, which is not a proof.

## Round 3 measurements (kernel tests `acis_sat_tests::TestAcisSab*`, this build, Linux)

All figures are write-SAB-then-read with this library only (self-consistency,
not compatibility with another product). "vs source" uses the SAT tests'
tolerances (1e-9 for planar bodies, 1e-3 for mesh-measured/curved ones); "vs SAT"
is the same body through the SAT text path, agreeing to 1e-9 in every row.

| Body | SAB bytes | Volume source -> SAB read-back | Area source -> SAB read-back | F / E / V before -> after |
|---|---|---|---|---|
| box 2x3x4 | 5263 | 24 -> 24 | 52 -> 52 | 6/12/8 -> 6/12/8 |
| boolean pocket (4^3 minus 2x2x3) | 37543 | 52 -> 52 | 120 -> 120 | 46/92/48 -> 46/92/48 |
| cylinder r2 h5 (mesh-measured) | 1678 | 62.8318488654 -> 62.8233850038 | 87.9645916133 -> 87.9544350148 | 3/5/4 -> 3/3/2 (seam dropped, as for SAT) |
| sphere r2.5 | 427 | 65.4499660292 -> 65.4499660292 | 78.5399592351 -> 78.5399592351 | 1/0/0 -> 1/0/0 |
| torus 3/1 | 435 | 59.2176439326 -> 59.2176439326 | 118.435287865 -> 118.435287865 | 1/0/0 -> 1/0/0 |
| filleted box (mesh-measured) | 6517 | 23.89269898 -> 23.8926989781 | 51.4634954264 -> 51.4634955009 | 7/15/10 -> 7/15/10 |

* The library writer's bytes are identical to a second encoder written in the
  test file from the same table (box, cylinder, sphere, torus, filleted box).
  That checks the writer against the table, not the table against any product.
* SAT -> SAB -> SAT: for each body above, re-writing the SAB-path read-back as
  SAT text, and as SAB bytes, is byte-identical to the same re-write of the
  SAT-path read-back.
* Fuzz (fixed seeds, `TestAcisSabFuzzTruncationAndBitFlips`): 23267 inputs
  (every proper prefix of the 5263-byte box and 435-byte torus files, every 5th/3rd
  prefix of the pocket and cylinder files, plus 3000/2500/2500/1500 random one-bit,
  one-byte, 0x00/0xFF and four-bit mutations): 19821 refused with a reason, 3446
  still read as a valid body (e.g. a flipped coordinate), 0 malformed results, 0
  exceptions escaped. Every proper prefix was refused. Not run under a sanitizer.
* An endless zero stream was refused after 134283264 bytes served (the 128 MiB limit).

### App wiring (round 3)

`.sab` is dispatched next to `.sat` in `Application::OpenDocument`, `ImportFile`,
`SaveDocument` (Save As), `ExportSelected`, the Open / Import / Export / SaveAs /
ExportWithOrigin dialogs (`cmd_file.cpp`) and the drawing-compare loader. Import
uses `ReadAcis`, i.e. the encoding is taken from the file's first bytes, not its
extension (a text file named `.sab` opens as text, a SAB file named `.sat` as
binary); the summary says `SAB import` or `SAT import` accordingly and every SAB
summary carries the UNVERIFIED byte-layout note. Smoke section `SABF_`
(`tests/sabf_script.txt`, `tests/sabf_detect_script.txt`): 21 checks, all passing
in the run made for this commit (box 4000 -> 4000, sphere 2132 vs analytic 2144.7,
cylinder 784.8 vs 785.4: the app's Volume is tessellation based).
