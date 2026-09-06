#!/usr/bin/env bash
# Headless functional QC for Dino 8: runs a command script through the real
# app under Xvfb and checks the resulting history/object counts.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../build/Dino8}"
TMP="$(mktemp -d)"
# Isolate settings so persisted toggles (Ortho, snaps, theme) from earlier runs cannot leak into the checks.
export XDG_CONFIG_HOME="$TMP/config"
mkdir -p "$XDG_CONFIG_HOME"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/script.txt" <<EOS
Box 0,0,0 20,20,0 10
Sphere 10,10,10 8
SelNone
SelID 1
BooleanDifference
SelID 2
Enter
SelAll
Volume
SelNone
Circle 40,0,0 5
SelCrv
ExtrudeCrv 12
SelAll
Save $TMP/test.3dm
New
Open $TMP/test.3dm
SelAll
List
Undo
Redo
Line 0,0,0 10,10,0
SelCrv
Length
SelAll
Export $TMP/test.obj
SelNone
Cylinder 60,0,0 5 15
SelLast
What
EOS

if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  OUT="$("$BIN" --smoke 200 --script "$TMP/script.txt" 2>&1)" || { echo "$OUT"; echo "FAIL: app exited non-zero"; exit 1; }
else
  OUT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMP/script.txt" 2>&1)" || { echo "$OUT"; echo "FAIL: app exited non-zero"; exit 1; }
fi
echo "$OUT" | grep "^smoke:" || { echo "$OUT"; echo "FAIL: no smoke line"; exit 1; }
fail=0
check() { if echo "$OUT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
check "1055 commands loaded" "catalog has all 1055 commands"
check "BooleanDifference:" "boolean difference ran"
check "Volume = " "volume measured"
check "Extruded 1 object" "extrusion created a solid"
check "Saved " "save wrote a .3dm"
check "Opened " "open re-read the .3dm"
check "Exported " "OBJ export wrote a file"
check "length = 14.14" "line length measured"
test -s "$TMP/test.3dm" && echo "ok   test.3dm exists" || { echo "FAIL test.3dm missing"; fail=1; }
test -s "$TMP/test.obj" && echo "ok   test.obj exists" || { echo "FAIL test.obj missing"; fail=1; }
check "gl_error=0" "no OpenGL errors"
echo "$OUT" | grep -E "^(smoke|history)" | tail -120

# Interactive UI replay: typed command, viewport picks, click-select, Delete, Undo.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  UI="$("$BIN" --smoke 80 --script "$HERE/ui_script.txt" 2>&1)" || true
else
  UI="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 80 --script "$HERE/ui_script.txt" 2>&1)" || true
fi
echo "$UI" | grep -E "^(ok|FAIL)"
if echo "$UI" | grep -q "^FAIL"; then fail=1; fi
echo "$UI" | grep -q "^ok   expect_objects 1" || { echo "FAIL ui script produced no checks"; fail=1; }

# Curve editing: Intersect, Split, Trim, Fillet, Chamfer, FilletCorners (see curveedit_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  CE="$("$BIN" --smoke 150 --script "$HERE/curveedit_script.txt" 2>&1)" || { echo "$CE"; echo "FAIL: curve-edit script exited non-zero"; exit 1; }
else
  CE="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/curveedit_script.txt" 2>&1)" || { echo "$CE"; echo "FAIL: curve-edit script exited non-zero"; exit 1; }
fi
cecheck() { if echo "$CE" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
cecheck "intersection at 10,0,0" "Intersect found the line/line crossing"
cecheck "Split 1 curve into 2 pieces" "Split cut the line at the crossing"
cecheck "Trimmed curve 6" "Trim removed the clicked portion"
cecheck "CV\[1\] 40,0,0" "Trim kept the piece up to the cutter"
cecheck "Fillet: Radius=5, arc added" "Fillet built a tangent arc"
cecheck "CV\[1\] 75,0,0" "Fillet trimmed the first line to the tangent point"
cecheck "Chamfer: joined result, Distance=4" "Chamfer joined the pieces into one curve"
cecheck "FilletCorners: rounded 4 corners on 1 curve" "FilletCorners rounded the rectangle"
cecheck "intersection at 305,0,0" "Intersect found the circle/line crossing"
cecheck "Split into 2 piece(s)" "Split still delegates solids to the plane split"
cecheck "smoke: frames=150 objects=17" "curve-edit script produced the expected object count"

# Curve tools: conics, catenary, CloseCrv, ReducePolyline, SubCrv, Contour, Section, Align, Distribute, TweenCurves, ArrayCrv, fits (see curves2_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  C2="$("$BIN" --smoke 150 --script "$HERE/curves2_script.txt" 2>&1)" || { echo "$C2"; echo "FAIL: curve-tools script exited non-zero"; exit 1; }
else
  C2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/curves2_script.txt" 2>&1)" || { echo "$C2"; echo "FAIL: curve-tools script exited non-zero"; exit 1; }
fi
c2check() { if echo "$C2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
c2check "Conic: rho = 0.4" "Conic passed through the shoulder point"
c2check "Parabola: focal length 5" "Parabola built from vertex and focus"
c2check "Hyperbola: a = 5" "Hyperbola built from center and vertex"
c2check "Catenary: length 50" "Catenary solved for the cable length"
c2check "CloseCrv: 1 curve(s) closed" "CloseCrv closed the polyline"
c2check "ReducePolyline: 1 polyline(s) reduced" "ReducePolyline dropped collinear vertices"
c2check "CV\[0\] 120,0,0" "SubCrv kept the picked span"
c2check "Contour: 3 curve(s) from 5 plane(s)" "Contour sliced the sphere"
c2check "Section: 1 curve(s)" "Section cut the sphere"
c2check "aligned Bottom" "Align moved the boxes"
c2check "spaced evenly along X" "Distribute spaced the boxes"
c2check "TweenCurves: 3 curve(s) created" "TweenCurves interpolated between the lines"
c2check "ArrayCrv: 6 object(s) placed" "ArrayCrv placed copies along the circle"
c2check "best-fit line through 4 points" "LineThroughPt fitted a line"
c2check "best-fit plane through 4 points" "PlaneThroughPt fitted a plane"
c2check "MarkFoci: 1 point(s) added" "MarkFoci added the parabola's focus"
c2check "0,5,0" "MarkFoci found the parabola focus (0,5,0) from curve geometry alone"
c2check "MarkFoci: 2 point(s) added" "MarkFoci added both hyperbola foci"
c2check "6.807" "MarkFoci found the analytic hyperbola focus distance (c=6.807 for a=5, b^2=64/3)"
c2check "^ok   expect_objects 39" "curve-tools script produced the expected object count"
# Exchange formats: DXF round-trip, SVG / PDF vector output, PLY round-trip (see exchange_script.txt).
sed "s|@TMP@|$TMP|g" "$HERE/exchange_script.txt" > "$TMP/exchange_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  EX="$("$BIN" --smoke 150 --script "$TMP/exchange_script.txt" 2>&1)" || { echo "$EX"; echo "FAIL: exchange script exited non-zero"; exit 1; }
else
  EX="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/exchange_script.txt" 2>&1)" || { echo "$EX"; echo "FAIL: exchange script exited non-zero"; exit 1; }
fi
excheck() { if echo "$EX" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
excheck "Exported $TMP/exchange.dxf" "DXF export wrote a file"
excheck "DXF: 16 curves, 1 point, 1 mesh, 1 new layer" "DXF import read every entity back (line, circle, arc, polyline, 12 box edges, point, mesh, layer)"
excheck "degree 2, 9 control points, rational, closed" "DXF CIRCLE came back as an exact rational circle"
excheck "CV\[1\] 10,0,0" "DXF LINE kept its coordinates"
excheck "CV\[2\] 10,30,0" "DXF LWPOLYLINE kept its vertices"
excheck "(point) layer Default" "DXF POINT imported"
excheck "(mesh) layer Solids" "DXF 3DFACEs became a mesh on the imported layer"
excheck "Printed $TMP/exchange.pdf" "Print wrote a PDF"
excheck "Exported $TMP/exchange.svg" "SVG export wrote a file"
excheck "Exported $TMP/exchange.ply" "PLY export wrote a file"
excheck "8 vertices, 6 faces" "PLY round-trip kept the mesh box"
excheck "smoke: frames=150 objects=1 " "exchange script ended with the re-opened PLY mesh"
grep -q "^0$" "$TMP/exchange.dxf" && grep -q "^AC1015$" "$TMP/exchange.dxf" && grep -q "^EOF$" "$TMP/exchange.dxf" && echo "ok   exchange.dxf is a complete AC1015 DXF" || { echo "FAIL exchange.dxf malformed"; fail=1; }
grep -q "^CIRCLE$" "$TMP/exchange.dxf" && grep -q "^ARC$" "$TMP/exchange.dxf" && grep -q "^LWPOLYLINE$" "$TMP/exchange.dxf" && grep -q "^3DFACE$" "$TMP/exchange.dxf" && echo "ok   exchange.dxf uses CIRCLE/ARC/LWPOLYLINE/3DFACE entities" || { echo "FAIL exchange.dxf entity types"; fail=1; }
grep -q "^Solids$" "$TMP/exchange.dxf" && echo "ok   exchange.dxf carries the Solids layer" || { echo "FAIL exchange.dxf layer table"; fail=1; }
grep -q "<svg" "$TMP/exchange.svg" && grep -q "<path" "$TMP/exchange.svg" && echo "ok   exchange.svg has paths" || { echo "FAIL exchange.svg has no paths"; fail=1; }
grep -q ' Z"' "$TMP/exchange.svg" && echo "ok   exchange.svg closes the circle path with Z" || { echo "FAIL exchange.svg has no closed path"; fail=1; }
grep -q 'id="Solids"' "$TMP/exchange.svg" && echo "ok   exchange.svg groups paths by layer" || { echo "FAIL exchange.svg layer groups"; fail=1; }
head -c 5 "$TMP/exchange.pdf" | grep -q "%PDF-" && echo "ok   exchange.pdf starts with %PDF-" || { echo "FAIL exchange.pdf header"; fail=1; }
grep -aq "^xref$" "$TMP/exchange.pdf" && grep -aq "^startxref$" "$TMP/exchange.pdf" && grep -aq "%%EOF" "$TMP/exchange.pdf" && echo "ok   exchange.pdf has an xref table and trailer" || { echo "FAIL exchange.pdf xref"; fail=1; }
PDFOFF="$(grep -a -A1 "^startxref$" "$TMP/exchange.pdf" | tail -1)"
[ "$(tail -c +$((PDFOFF + 1)) "$TMP/exchange.pdf" | head -c 4)" = "xref" ] && echo "ok   exchange.pdf startxref points at the xref table" || { echo "FAIL exchange.pdf startxref offset"; fail=1; }
grep -aq "^h$" "$TMP/exchange.pdf" && echo "ok   exchange.pdf closes paths with h" || { echo "FAIL exchange.pdf closed paths"; fail=1; }
if command -v qpdf >/dev/null 2>&1; then qpdf --check "$TMP/exchange.pdf" >/dev/null 2>&1 && echo "ok   qpdf --check passes" || { echo "FAIL qpdf --check"; fail=1; }; fi
head -1 "$TMP/exchange.ply" | grep -q "^ply" && grep -q "^element face 6" "$TMP/exchange.ply" && echo "ok   exchange.ply is an ASCII PLY with 6 faces" || { echo "FAIL exchange.ply"; fail=1; }
# Surfaces: Pipe, OffsetSrf, Shell, Sweep1/2, NetworkSrf, Patch, ExtrudeCrvAlongCrv,
# ExtrudeCrvTapered, Project, Pull (see surface_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SF="$("$BIN" --smoke 200 --script "$HERE/surface_script.txt" 2>&1)" || { echo "$SF"; echo "FAIL: surface script exited non-zero"; exit 1; }
else
  SF="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/surface_script.txt" 2>&1)" || { echo "$SF"; echo "FAIL: surface script exited non-zero"; exit 1; }
fi
sfcheck() { if echo "$SF" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
sfcheck "Pipe: radius 1, 1 pipe(s) (capped mesh)" "Pipe built a capped mesh"
sfcheck "Volume = 31.06 cubic" "Pipe r=1 along 10 units has volume ~ pi*10"
sfcheck "Area = 62.79 square" "uncapped Pipe surface area ~ 2*pi*10"
sfcheck "degree 3 x 1, CVs 27 x 2" "uncapped Pipe is a periodic NURBS tube"
sfcheck "Bounding box min 20,0,-2 max 30,10,-2" "OffsetSrf moved the plane by 2 along its normal"
sfcheck "Volume = 200 cubic" "OffsetSrf Solid=Yes closed a 10x10x2 slab"
sfcheck "Shell: thickness 1, volume 1408" "Shell hollowed the box (4000 - 18*18*8)"
sfcheck "ExtrudeCrvAlongCrv: 1 surface(s)" "ExtrudeCrvAlongCrv built a sum surface"
sfcheck "Sweep1: 1 section(s) along 2 rail stations" "Sweep1 swept the circle along the line"
sfcheck "Area = 124.2 square" "Sweep1 area ~ 2*pi*2*10 (cubic circle approximation)"
sfcheck "Sweep2: 1 section(s) along 2 rail stations" "Sweep2 spanned the two rails"
sfcheck "NetworkSrf: Coons patch through 4 curves" "NetworkSrf sorted 4 curves into a loop"
sfcheck "NetworkSrf: ruled surface between 2 curves" "NetworkSrf ruled two curves"
sfcheck "Patch: planar face bounded by the closed curve" "Patch trimmed a plane with the circle"
sfcheck "Area = 78.51 square" "Patch disc area ~ pi*25"
sfcheck "Patch: least-squares plane through 0 curve(s) and 3 point(s)" "Patch fitted a plane through points"
sfcheck "ExtrudeCrvTapered: distance 5, draft 10 deg, 1 object(s)" "ExtrudeCrvTapered ran"
sfcheck "3 faces, 5 edges, closed solid" "ExtrudeCrvTapered capped and joined into a closed solid"
sfcheck "Project: 1 curve(s), 0 point(s)" "Project produced one curve"
sfcheck "CV\[1\] 240,20,0" "Project landed the line on the plane"
sfcheck "Pull: 0 curve(s), 1 point(s)" "Pull produced one point"
sfcheck "  225,5,0" "Pull moved the point onto the plane"
sfcheck "smoke: frames=200 objects=39" "surface script produced the expected object count"
# Surface editing: ExtractSrf, DeleteFaces, DupBorder/DupEdge, Untrim, isocurves, ExtendSrf, UnrollSrf, Silhouette, RailRevolve, Fin/Ribbon, grids (see srfedit_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SE="$("$BIN" --smoke 150 --script "$HERE/srfedit_script.txt" 2>&1)" || { echo "$SE"; echo "FAIL: surface-edit script exited non-zero"; exit 1; }
else
  SE="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/srfedit_script.txt" 2>&1)" || { echo "$SE"; echo "FAIL: surface-edit script exited non-zero"; exit 1; }
fi
secheck() { if echo "$SE" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
secheck "ExtractSrf: face 5 extracted from object 1" "ExtractSrf pulled the top face off the box"
secheck "5 faces, 12 edges, open" "ExtractSrf left a 5-face open polysurface"
secheck "DeleteFaces: face 4 deleted, 4 face(s) left" "DeleteFaces removed the bottom face"
secheck "DupBorder: 3 border curve(s)" "DupBorder found the naked edge loops"
secheck "DupEdge: edge 0 of object 1 duplicated" "DupEdge copied an edge"
secheck "DupFaceBorder: 1 curve(s)" "DupFaceBorder copied the face loop"
secheck "Untrim: face 0 replaced by its untrimmed surface" "Untrim removed the circular trim"
secheck "ExtractIsocurve: 2 curve(s)" "ExtractIsocurve extracted U and V isocurves"
secheck "ExtractWireframe: 6 curve(s)" "ExtractWireframe extracted the cylinder wires"
secheck "Bounding box min 200,0,0 max 220,10,0" "ExtendSrf grew the plane by 10"
secheck "UnrollSrf: face 0 flattened, area 313" "UnrollSrf preserved the cylinder wall area"
secheck "Silhouette: [0-9]* curve(s)" "Silhouette produced outline curves"
secheck "RailRevolve: surface created" "RailRevolve built a surface"
secheck "Ribbon: 1 ribbon surface(s), width 5" "Ribbon built a ribbon surface"
secheck "SrfControlPtGrid: surface from 2 x 2 points" "SrfControlPtGrid built a surface from points"
secheck "UntrimBorder: face 0's outer trim removed (holes kept)" "UntrimBorder removed only the outer trim"
secheck "JoinEdge: 1 naked edge pair(s) joined" "JoinEdge force-joined two just-touching planes"
secheck "MoveFace: 1 object(s) updated" "MoveFace ran"
secheck "Bounding box min 1100,0,0 max 1110,10,15" "MoveFace actually raised the box's top face"
secheck "MoveEdge: 1 object(s) updated" "MoveEdge ran"
secheck "Bounding box min 1200,0,0 max 1210,10,15" "MoveEdge actually raised the picked edge"
secheck "MoveUntrimmedFace: 1 object(s) updated" "MoveUntrimmedFace ran"
secheck "Bounding box min 1300,0,0 max 1310,10,20" "MoveUntrimmedFace actually raised the untrimmed top face"
secheck "MoveUntrimmedEdge: 1 object(s) updated" "MoveUntrimmedEdge ran"
secheck "Bounding box min 1400,0,0 max 1410,10,25" "MoveUntrimmedEdge actually raised the picked iso edge"
secheck "MoveUVN: control point (0,0) moved u=0 v=0 n=7" "MoveUVN moved a surface CV along its local normal"
secheck "FoldFace: face 5 folded 30 degree" "FoldFace rotated a face about its hinge edge"
secheck "Bounding box min 1600,-5,0 max 1610,10,13.66" "FoldFace actually moved the folded face's geometry"
secheck "SetPlanar: 2 object(s) projected" "SetPlanar ran"
secheck "Bounding box min 1700,0,0 max 1710,0,0" "SetPlanar actually flattened the points onto z=0"
secheck "volume 1000, centroid 1805,5,5" "VolumeMoments reported the box's real volume and centroid"
secheck "radii of gyration about centroid: x=4.082 y=4.082 z=4.082" "VolumeMoments computed real radii of gyration for a 10-cube (sqrt(1000\*6/60/10)=4.082 exactly for a cube)"
secheck "area 100, centroid 1905,5,0" "AreaMoments reported the plane's real area and centroid"
secheck "displacement (below the construction plane): 26" "Hydrostatics computed a real submerged volume (half of a r=5 sphere is 4/3*pi*125/2 = 261.8)"
secheck "waterplane area 78.3" "Hydrostatics computed the real waterplane cross-section area (pi\*5\^2 = 78.5)"
secheck "ExtractBadSrf: 0 invalid polysurface(s) extracted as copies" "ExtractBadSrf found no invalid polysurfaces in a valid box"
secheck "RemoveAllNakedMicroEdges: no naked edges shorter than" "RemoveAllNakedMicroEdges scanned a closed box and found none"
secheck "ShrinkTrimmedSrf: 0 polysurface(s) shrunk" "ShrinkTrimmedSrfToEdge ran (this trimmed disk is already shrunk tight)"
secheck "^ok   expect_objects 65" "surface-edit script produced the expected object count"

# Mesh tools: deformations, mesh editing and mesh primitives (see meshtools_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  MT="$("$BIN" --smoke 150 --script "$HERE/meshtools_script.txt" 2>&1)" || { echo "$MT"; echo "FAIL: mesh-tools script exited non-zero"; exit 1; }
else
  MT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/meshtools_script.txt" 2>&1)" || { echo "$MT"; echo "FAIL: mesh-tools script exited non-zero"; exit 1; }
fi
mtcheck() { if echo "$MT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
mtcheck "Twist: 90 degrees over 20 units" "Twist read its axis and angle"
mtcheck "Twist: deformed 1 object(s)" "Twist deformed the mesh cylinder"
mtcheck "Bend: deformed 1 object(s) (1 converted to meshes)" "Bend converted the box to a mesh and bent it"
mtcheck "Taper: 5 -> 2" "Taper read both distances"
mtcheck "Stretch: 20 -> 30" "Stretch lengthened the line"
mtcheck "Shear: deformed 1 object(s)" "Shear ran with three points"
mtcheck "Maelstrom: deformed 1 object(s)" "Maelstrom ran"
mtcheck "SoftMove: 4 units with falloff radius 5" "SoftMove took its Radius option"
mtcheck "Smooth: smoothed 1 object(s), factor 0.5" "Smooth ran on the mesh sphere"
mtcheck "SoftTransform (Rotate): 45 degrees about the CPlane normal" "SoftTransform Rotate mode ran"
mtcheck "SoftTransform (Scale): factor 1.5 from" "SoftTransform Scale mode ran"
mtcheck "ExtrudeMesh: extruded open mesh into a closed solid" "ExtrudeMesh closed the open mesh plane"
mtcheck "Volume = 300" "the extruded 10x10x3 mesh has volume 300"
mtcheck "OffsetMesh: solid shell" "OffsetMesh built a solid shell"
mtcheck "FillMeshHoles: filled 1 hole(s)" "FillMeshHoles closed the naked loop"
mtcheck "Unweld: added 72 vertex copy(ies)" "Unweld split the sharp edges"
mtcheck "Weld: merged 72 vertex(es)" "Weld merged them back"
mtcheck "0 naked edge(s), 0 non-manifold edge(s), closed" "MeshRepair reports a closed mesh"
mtcheck "MeshEllipsoid: radii 10, 7, 5" "MeshEllipsoid built"
mtcheck "MeshTruncatedCone: 96 faces, closed" "MeshTruncatedCone is closed"
mtcheck "TruncatedPyramid: 12 faces, closed" "TruncatedPyramid is closed"
mtcheck "Paraboloid: NURBS surface of revolution, radius 10, height 10, 2 face(s), closed" "Paraboloid built a closed NURBS surface of revolution"
mtcheck "PlanarMesh: created 1 mesh(es)" "PlanarMesh triangulated the circle"
mtcheck "Slab: created 1 mesh(es)" "Slab thickened the rectangle"
mtcheck "ExtractMeshPart: all 6 faces extracted" "ExtractMeshPart extracted the box"
mtcheck "MeshIntersect: 14 segment(s) in 2 polyline(s)" "MeshIntersect found the box/box intersection"
mtcheck "MeshPatch: 3 triangles from 4 points" "MeshPatch triangulated the points"
mtcheck "MeshOutline: created 1 outline curve(s)" "MeshOutline built the outline"
mtcheck "Drape: 20x20 grid draped over" "Drape hit the visible meshes"
mtcheck "TriangulateRenderMeshes: 12 triangles" "TriangulateRenderMeshes split the box's quads"
mtcheck "AddNgonsToMesh: object [0-9]*: 6 ngon(s) from coplanar face groups" "AddNgonsToMesh grouped the box's triangle pairs back into ngons"
mtcheck "ComputeVertexColors: coloured 1 mesh(es) by vertex normal" "ComputeVertexColors coloured the mesh"
mtcheck "UnweldEdge: unwelded the edge nearest the pick" "UnweldEdge separated a single picked edge"
mtcheck "CollapseMeshVertex: collapsed the vertex nearest the pick into its nearest neighbour" "CollapseMeshVertex picked a vertex"
mtcheck "CollapseMeshFace: collapsed the face nearest the pick into its centroid" "CollapseMeshFace picked a face"
mtcheck "CollapseMeshEdge: collapsed the edge nearest the pick into its midpoint" "CollapseMeshEdge picked an edge"
mtcheck "DupMeshEdge: 1 edge(s) duplicated so far" "DupMeshEdge duplicated the single picked edge"
mtcheck "ExtractMeshFaces: extracted 1 of 6 faces" "ExtractMeshFaces extracted the single picked face"
mtcheck "FlatShade: added 16 vertex copy(ies)" "FlatShade fully unwelded the box for faceted shading"
echo "$MT" | grep -E "^(ok|FAIL)"
if echo "$MT" | grep -q "^FAIL"; then fail=1; fi
mtcheck "smoke: frames=1[0-9][0-9] objects=41" "mesh-tools script produced the expected object count"

# SubD editing: Crease, ExtrudeSubD, Inset, Bridge, OffsetSubD, RepairSubD, InsertEdge,
# DivideAlongCreases, Fill, AutomaticSubDFromMesh, SubDTruncatedCone, ShrinkWrap (see subd_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SD="$("$BIN" --smoke 150 --script "$HERE/subd_script.txt" 2>&1)" || { echo "$SD"; echo "FAIL: subd script exited non-zero"; exit 1; }
else
  SD="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/subd_script.txt" 2>&1)" || { echo "$SD"; echo "FAIL: subd script exited non-zero"; exit 1; }
fi
sdcheck() { if echo "$SD" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
sdcheck "Crease: 2 edge(s) creased on 1 SubD(s)" "Crease tagged the two picked edges"
sdcheck "Crease edges: 2" "What reports the crease edges on the control net"
sdcheck "ExtrudeSubD: extruded 1 face(s) by 3 on 1 SubD(s), 4 side face(s) added" "ExtrudeSubD extruded the top face with four side faces"
sdcheck "Inset: 1 face(s) inset by 1" "Inset built an inner face and ring"
sdcheck "Bridge: joined 2 faces with a tube of 4 quads, 2 SubDs merged" "Bridge joined the two boxes"
sdcheck "Faces: 22" "the bridged SubD has 22 control faces (6+4+5 for the boxes and tube, plus the inset)"
sdcheck "OffsetSubD: solid shell, distance 1, 1 SubD(s)" "OffsetSubD Solid=Yes built a shell"
sdcheck "44 face(s) (was 44), 0 naked edge(s), 0 non-manifold edge(s), closed" "RepairSubD reports the offset shell closed"
sdcheck "InsertEdge: 4 edge(s) inserted at 0.5 along the ring" "InsertEdge inserted an edge loop around the box"
sdcheck "Crease: 6 edge(s) creased on 1 SubD(s)" "Crease tagged the loop around the split top face"
sdcheck "DivideAlongCreases: 1 SubD(s) divided into 2 piece(s)" "DivideAlongCreases split the box at the crease loop"
sdcheck "Fill: 1 hole(s) filled with a face" "Fill closed the naked loop"
sdcheck "AutomaticSubDFromMesh: SubD with 6 face(s), 12 crease edge(s) sharper than 30 degrees" "AutomaticSubDFromMesh creased the box edges"
sdcheck "SubDTruncatedCone: base radius 5, top radius 2.5, height 10, 32 faces" "SubDTruncatedCone built its control net"
sdcheck "SubDDisplayToggle: 4 SubD(s) show the control polygon" "SubDDisplayToggle switched the closed SubDs to the control polygon"
sdcheck "ShrinkWrap: signed-distance wrap with [0-9]* vertices, [0-9]* faces (closed)" "ShrinkWrap built a closed signed-distance wrap"
sdcheck "Volume = 3[0-9][0-9][0-9] cubic" "ShrinkWrap volume is close to the union of the wrapped solids"
sdcheck "gl_error=0" "subd script ran without OpenGL errors"
echo "$SD" | grep -E "^(ok|FAIL)"
if echo "$SD" | grep -q "^FAIL"; then fail=1; fi
sdcheck "smoke: frames=150 objects=6" "subd script produced the expected object count"
# Rendering: materials (scripted options), texture mapping, lights, sun, ground plane,
# Render / RenderArctic / SaveRenderWindowAs, ExtractRenderMesh, .3dm round-trip (see render_script.txt).
sed "s|@TMP@|$TMP|g" "$HERE/render_script.txt" > "$TMP/render_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  RN="$("$BIN" --smoke 200 --script "$TMP/render_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: render script exited non-zero"; exit 1; }
else
  RN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMP/render_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: render script exited non-zero"; exit 1; }
fi
rncheck() { if echo "$RN" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
rncheck "Created material Plastic" "RenderAssignMaterialToObjects created a material"
rncheck "Material Glass: color=200,225,240 transparency=0.6 reflectivity=0.3 gloss=0.9" "material options were applied"
rncheck "Material Brass assigned to 1 object(s)" "material assigned to the cylinder"
rncheck "Cylindrical mapping applied to 1 object(s), Scale=2" "ApplyCylindricalMapping set the mapping and scale"
rncheck "AssignBlankTexture: .*blank_texture.ppm assigned to 1 object(s)" "AssignBlankTexture wrote and assigned a checker texture"
rncheck "SynchronizeRenderColors: 1 material(s)" "SynchronizeRenderColors made a material from the display colour"
rncheck "Point light 1: Point light at 10,-30,40, Intensity=1.5" "PointLight took its point and Intensity option"
rncheck "Spot light 2: Spot light at -30,-30,40, cone angle 7.12" "Spotlight built its cone from base, radius and end"
rncheck "Directional light 3: Directional light direction" "DirectionalLight ran"
rncheck "Sun on: Azimuth=200 Altitude=50" "Sun options applied"
rncheck "GroundPlane on: Height=Automatic Color=150,158,168 Shadows=Yes" "GroundPlane options applied"
rncheck "Environment: background Sky" "Environments switched to the sky background"
rncheck "Render: rendered Perspective at 320 x 240" "Render produced an offscreen image"
rncheck "Saved rendering $TMP/render.bmp (320 x 240)" "SaveRenderWindowAs wrote the BMP"
rncheck "RenderArctic: rendered Perspective at 1280 x 720" "RenderArctic rendered at the document size"
rncheck "Saved rendering $TMP/arctic.ppm (1280 x 720)" "SaveRenderWindowAs wrote a PPM"
rncheck "RenderPreview: rendered Perspective" "RenderPreview rendered at viewport size"
rncheck "PolygonCount: [0-9]* triangles in 4 visible object(s)" "PolygonCount counted the display meshes"
rncheck "RenderReportMissingImageFiles: 0 missing image file(s)" "RenderReportMissingImageFiles found every texture"
rncheck "ExtractRenderMesh: 4 mesh(es)" "ExtractRenderMesh added the display meshes"
rncheck "SetSpotlightToView: 1 spotlight(s) moved" "SetSpotlightToView moved the spotlight"
rncheck "Opened $TMP/render.3dm (8 objects)" "the .3dm with materials and lights re-opened"
rncheck "RenderReportImageFiles: 1 image file(s) referenced" "material textures survived the .3dm round-trip"
rncheck "^history: 3 light(s) selected" "lights survived the .3dm round-trip"
rncheck "Current renderer: Dino 8 built-in renderer" "SetCurrentRenderPlugIn reports the built-in renderer"
rncheck "EditLightByHighlight: selected Point light 1 (nearest the pick, 0 units away)" "EditLightByHighlight picked the nearest light"
rncheck "EditLightByLooking: Perspective now looks through Point light 1" "EditLightByLooking aimed the view through the light"
rncheck "EditLightByLooking: Point light 1 re-aimed from the current view" "EditLightByLooking saved the new aim back to the light"
rncheck "ApplyCustomMapping: 1 object(s) mapped from a custom plane at 0,0,0, size 5, Scale=2" "ApplyCustomMapping used a picked reference frame"
rncheck "ShadeSelected: 1 object(s) now shaded even in Wireframe" "ShadeSelected set the per-object override"
rncheck "ShowRenderMesh: showing the tessellation wireframe on 1 object" "ShowRenderMesh overlaid the render mesh"
rncheck "HideRenderMesh: hid the tessellation wireframe on 1 object" "HideRenderMesh cleared the overlay"
rncheck "ToggleRenderMesh: tessellation wireframe now shown on 1 of 1 object" "ToggleRenderMesh flipped the overlay"
rncheck "SetMeshSurfaceParameters: 1 object(s) now tessellate at tolerance 0.05" "SetMeshSurfaceParameters set a per-object tolerance"
rncheck "SetPerFaceColorByFacePack: coloured 44 face(s) across 1 mesh(es)" "SetPerFaceColorByFacePack coloured every face"
rncheck "RemovePerFaceColors: cleared stored vertex colours on 1 mesh(es)" "RemovePerFaceColors cleared them"
rncheck "Textures: procedural wood/marble/stone/concrete/brick textures are already available" "Textures points at the real proc: textures"
rncheck "PackTextures: 1 image(s) copied into .*render_textures" "PackTextures copied a texture into a document-local folder"
rncheck "UnpackTextures: 1 image(s) copied to .*unpacked_textures" "UnpackTextures copied it back out to a chosen folder"
rncheck "Environment: background Image (.*render.bmp)" "Environments Background=Image set the image background"
rncheck "BackgroundBitmap: .*render.bmp (on)" "BackgroundBitmap set the modelling-aid picture"
rncheck "RenderBlowup: 64 x 20 region rendered" "RenderBlowup rendered the picked region"
rncheck "Bake: baked 1 procedural texture" "Bake rasterized the procedural texture to a file"
rncheck "BakeMapping: baked the current mapping into mesh UVs for 1 object" "BakeMapping froze the mapping into mesh UVs"
rncheck "MappingWidget: this app has no draggable 3D mapping gizmo" "MappingWidget explains the real limitation"
rncheck "DownloadLibraryTextures: Dino 8 does not download anything" "DownloadLibraryTextures explains there is nothing to fetch"
rncheck "CopyRenderWindowToClipboard: this build has no OS image-clipboard integration" "CopyRenderWindowToClipboard explains the real limitation"
rncheck "gl_error=0" "no OpenGL errors in the render script"
python3 - "$TMP/render.bmp" <<'PY' && echo "ok   render.bmp is a valid, non-black 24-bit BMP" || { echo "FAIL render.bmp invalid or black"; fail=1; }
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:2] == b'BM', 'signature'
size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
assert size == len(d) and hdr == 40 and w == 320 and h == 240 and planes == 1 and bpp == 24, (size, len(d), w, h, bpp)
px = d[off:]
assert len(px) == ((w * 3 + 3) & ~3) * h, 'pixel data size'
assert max(px) > 0 and min(px) < 255, 'image is flat'
# The rendering must contain more than one colour (background + shaded objects).
assert len(set(px[i:i + 3] for i in range(0, len(px) - 3, 3 * 97))) > 8, 'too few colours'
PY
head -c 2 "$TMP/arctic.ppm" | grep -q "P6" && echo "ok   arctic.ppm is a binary PPM" || { echo "FAIL arctic.ppm"; fail=1; }
# Annotation, linetype, hatch and block tools (see annotate2_script.txt).
sed "s|@TMP@|$TMP|g" "$HERE/annotate2_script.txt" > "$TMP/annotate2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  A2="$("$BIN" --smoke 150 --script "$TMP/annotate2_script.txt" 2>&1)" || { echo "$A2"; echo "FAIL: annotate2 script exited non-zero"; exit 1; }
else
  A2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/annotate2_script.txt" 2>&1)" || { echo "$A2"; echo "FAIL: annotate2 script exited non-zero"; exit 1; }
fi
a2check() { if echo "$A2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
a2check "DimArea: Area = 314 square" "DimArea measured the circle (pi * 100)"
a2check "DimCurveLength: Length = 40" "DimCurveLength measured the line"
a2check "Centermark: 1 center mark(s)" "Centermark marked the circle"
a2check "RevCloud: 26 arc(s), closed curve" "RevCloud built a closed cloud of 26 arcs around the 30x20 rectangle"
a2check "TextProperties: 1 annotation(s) updated to \"World\", height 5" "TextProperties rebuilt the text with new content and height"
a2check "FindText: 1 annotation(s) containing \"World\" selected" "FindText found the edited text"
a2check "ConvertDots: 1 dot(s) converted to text" "ConvertDots turned the dot into text"
a2check "HatchScale: 1 hatch(es) rebuilt" "HatchScale rebuilt the hatch from its boundary"
a2check "SetCustomLinetype: Foo = 5,5" "SetCustomLinetype stored the pattern"
a2check "SetLinetype: 1 object(s) set to Foo" "SetLinetype assigned the custom linetype"
a2check "ExtractLineTypeSegments: 10 segment(s) from 1 curve(s)" "ExtractLineTypeSegments split the 100-unit line into 10 dashes"
a2check "SetLayerLinetype: layer Default uses Dashed" "SetLayerLinetype changed the layer linetype"
a2check "Linetype Foo: 5,5" "Linetypes lists the custom linetype"
a2check "1 curve(s) with linetype Foo selected" "linetype table and object linetype survived the 3dm round-trip"
a2check "ReplaceBlock: 2 instance(s) now 'B'" "ReplaceBlock swapped both A instances for B"
a2check "3 object(s) in instances of 'B' selected" "SelBlockInstanceNamed found the replaced instances"
a2check "CreateUniqueBlock: 'C' copied from 'B', 3 instance(s) switched" "CreateUniqueBlock copied the definition"
a2check "gl_error=0" "annotate2 script ran without OpenGL errors"
# Solid tools: RoundHole, CurveBoolean, Clash, Cage/CageEdit, Flow, ScaleByPlane (see solidtools_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  ST="$("$BIN" --smoke 150 --script "$HERE/solidtools_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: solid-tools script exited non-zero"; exit 1; }
else
  ST="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/solidtools_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: solid-tools script exited non-zero"; exit 1; }
fi
stcheck() { if echo "$ST" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
stcheck "RoundHole: radius 3, through, cut 1 solid(s)" "RoundHole cut the box"
stcheck "Volume = 37[12][0-9] cubic" "box minus a r=3 through hole has volume ~ 4000 - 90*pi"
stcheck "CurveBoolean: Union of 2 region(s) -> 1 closed curve(s)" "CurveBoolean united two overlapping circles into one curve"
stcheck "degree 1, [0-9]* control points, non-rational, closed" "the union outline is a closed polyline"
stcheck "Clash: objects 6 and 7 intersect" "Clash found the overlapping spheres"
stcheck "Clash: 1 clashing pair(s) among 2 object(s)" "Clash reported one pair"
stcheck "Cage: object 9, 2 x 2 x 2 divisions (27 control points)" "Cage built a 3x3x3 lattice"
stcheck "CageEdit: 1 object(s) (14 points) bound to cage 9" "CageEdit bound the box (as a mesh) to the cage"
stcheck "Bounding box min 300,-\?0,5 max 310,10,15" "moving the cage moved the captive"
stcheck "Bounding box min 305,5,5 max 325,25,25" "scaling the cage scaled the captive"
stcheck "Flow: base length 20 -> target length 31.42 (stretched to fit)" "Flow measured both curves"
stcheck "Flow: deformed 1 object(s)" "Flow deformed the line"
stcheck "CV\[0\] 610,0,0" "the flowed line starts at the arc start"
stcheck "CV\[23\] 590,0,0" "the flowed line ends at the arc end"
stcheck "length = 31.[34]" "the flowed line follows the arc length"
stcheck "ScaleByPlane: factor 2 along the normal of the plane through 400,0,0" "ScaleByPlane read its plane and factor"
stcheck "Bounding box min 400,0,0 max 410,10,20" "ScaleByPlane doubled the height about z=0"
echo "$ST" | grep -E "^(ok|FAIL)"
if echo "$ST" | grep -q "^FAIL"; then fail=1; fi
stcheck "smoke: frames=1[0-9][0-9] objects=14" "solid-tools script produced the expected object count"

# Fillet family: FilletEdge/ChamferEdge exact box-corner trims, FilletSrf, BlendEdge,
# MatchSrf, SplitFace, MergeFaces, ConnectSrf, surface/surface and curve/surface
# Intersect (see fillet_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 200 --script "$HERE/fillet_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: fillet script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/fillet_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: fillet script exited non-zero"; exit 1; }
fi
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
flcheck "FilletEdge: edge 10 of object 1 replaced with an exact fillet (radius 2)" "FilletEdge trimmed the box corner into a real B-rep, not a mesh fallback"
flcheck "Volume = 991.4 cubic" "a 10x10x10 box minus a r=2 edge fillet has volume 1000 - 10*4*(1-pi/4) = 991.42"
flcheck "ChamferEdge: edge 10 of object 2 replaced with an exact chamfer (radius 3)" "ChamferEdge trimmed the box corner exactly"
flcheck "Volume = 955 cubic" "a 10x10x10 box minus a 3x3 edge chamfer has volume 1000 - 10*3^2/2 = 955 exactly"
flcheck "FilletSrf: built between object 4 and 6, radius 2; both surfaces trimmed" "FilletSrf trimmed two independently-picked planar surfaces"
flcheck "Area = 31.41 square" "the r=2 fillet's quarter-cylinder lateral area is (pi/2)*2*10 = 31.42"
flcheck "BlendEdge: blend surface added between the two faces at edge 10" "BlendEdge built a separate G1 blend surface"
flcheck "MatchSrf: 2 boundary control point.s. moved to position on the target curve" "MatchSrf moved a plane's edge onto a target line"
flcheck "SplitFace: face 0 split into 2 surfaces along the curve's crossing" "SplitFace found a real CSX crossing of a piercing polyline (a coplanar line can't cross a flat face twice)"
flcheck "MergeFaces: 2 coplanar face.s. merged into 1" "MergeFaces recombined two joined coplanar planes"
flcheck "Area = 100 square" "the merged 5x10 + 5x10 planes have area 100"
flcheck "ConnectSrf: extended both surfaces to their intersection curve" "ConnectSrf found the real SSX join line between two already-touching planes"
flcheck "Intersect: 1 surface intersection curve.s., 0 curve/surface point.s." "Intersect (SSX) found the crossing line of two planes meeting at a right angle"
flcheck "Intersect: 0 surface intersection curve.s., 1 curve/surface point.s." "Intersect (CSX) found where a line pierces a plane"
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
flcheck "^ok   expect_objects 24" "fillet script produced the expected object count"
# Extended selection and state commands: SelDupAll, SelShortCrv, SelKeyValue, SelVolumeSphere, Dot, Camera, SetActiveViewport, licence rule (see state_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  ST="$("$BIN" --smoke 120 --script "$HERE/state_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: state script exited non-zero"; exit 1; }
else
  ST="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$HERE/state_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: state script exited non-zero"; exit 1; }
fi
stcheck() { if echo "$ST" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
stcheck "SelDupAll: 2 duplicate object(s) selected" "SelDupAll found the two identical lines"
stcheck "SelShortCrv: 1 curve(s) shorter than 5 selected" "SelShortCrv picked the 2-unit line"
stcheck "Hyperlink 'https://example.org/dino8' set on 1 object(s)" "Hyperlink stored user text"
stcheck "SelKeyValue: 1 object(s) with Hyperlink=https://example.org/dino8 selected" "SelKeyValue matched the user text"
stcheck "SelVolumeSphere: radius 15, 2 object(s) selected" "SelVolumeSphere selected the objects near the origin"
stcheck "Dot 'Hello' at 5,5,0 (object 5)" "Dot created a text dot"
stcheck "Named selection 'dots' restored: 1 object(s) selected" "NamedSelections saved and restored a set"
stcheck "Camera (Perspective): location" "Camera reported the perspective camera"
stcheck "Active viewport: Top" "SetActiveViewport switched to Top"
stcheck "Camera (Top): location .*parallel projection" "Camera reported the Top (parallel) camera"
stcheck "Perspective angle 60 deg" "PerspectiveAngle set the lens"
stcheck "Dino 8 is free software: no licenses, accounts or subscriptions." "CheckOutLicense/Login print the no-licence rule"
stcheck "Selection filter edges on" "SelectionFilterEdges toggled the filter"
stcheck "Document user text Project = Dino" "SetDocumentUserText stored a key"
stcheck "Gumball: on, alignment CPlane" "GumballSettings reported the widget state"
echo "$ST" | grep -E "^(ok|FAIL)"
if echo "$ST" | grep -q "^FAIL"; then fail=1; fi
stcheck "^ok   expect_selected 5" "state script ended with every object selected"
# View tools: clipping planes + sections, layouts + details, named CPlanes, animation playback/recording (see viewtools_script.txt).
mkdir -p "$TMP/vt"
sed "s|@TMP@|$TMP/vt|g" "$HERE/viewtools_script.txt" > "$TMP/viewtools_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  VT="$("$BIN" --smoke 150 --script "$TMP/viewtools_script.txt" 2>&1)" || { echo "$VT"; echo "FAIL: view-tools script exited non-zero"; exit 1; }
else
  VT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/viewtools_script.txt" 2>&1)" || { echo "$VT"; echo "FAIL: view-tools script exited non-zero"; exit 1; }
fi
vtcheck() { if echo "$VT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
vtcheck "ClippingPlane: created Clipping Plane 1 (40 x 40, normal 0,0,1, clips all viewports)" "ClippingPlane built a plane from two corners"
vtcheck "ClippingSections: 1 curve(s) from 1 plane(s)" "ClippingSections cut the sphere's equator"
vtcheck "degree 1, [0-9]* control points, non-rational, closed" "the section is a closed polyline"
vtcheck "ClearClippingSections: removed 1 section curve(s)" "ClearClippingSections removed the section"
vtcheck "DisableClippingPlane: 1 plane(s) disabled" "DisableClippingPlane"
vtcheck "EnableClippingPlane: 1 plane(s) enabled" "EnableClippingPlane"
vtcheck "ClippingPlane: created Upper (40 x 40, normal 0,0,1, clips Perspective)" "ClippingPlane honoured Name= and Viewports=Active"
vtcheck "SaveClippingSectionCPlanes: 2 named CPlane(s) saved" "SaveClippingSectionCPlanes"
vtcheck "SaveClippingSectionViews: 2 named view(s) saved" "SaveClippingSectionViews"
vtcheck "ClippingSections: 2 curve(s) from 2 plane(s)" "ClippingSections handled two planes"
vtcheck "CPlane Perspective: elevation 7 (origin 0,0,7" "CPlane Elevation= moved the CPlane"
vtcheck "NamedCPlane: saved Deck (origin 0,0,7" "NamedCPlane Save"
vtcheck "NamedCPlane: restored Deck in Perspective (origin 0,0,7" "NamedCPlane Restore"
vtcheck "CPlanePrevious: origin 0,0,0" "CPlanePrevious stepped back through the CPlane history"
vtcheck "CPlaneNext: origin 0,0,7" "CPlaneNext stepped forward again"
vtcheck "CPlane Perspective: rotated 45 degrees (origin 0,0,7 x 0.7071,0.7071,0" "CPlane Rotate="
vtcheck "CopyCPlaneToAll: CPlane of Perspective copied to 3 viewport(s)" "CopyCPlaneToAll"
vtcheck "Layout: created Sheet1 (297 x 210 mm)" "Layout created a page"
vtcheck "Detail: added Detail 1 (130 x 100 mm, Top) to Sheet1" "Detail added a Top detail from two page corners"
vtcheck "Detail: added Iso (110 x 170 mm, Perspective) to Sheet1" "Detail honoured View= and Name="
vtcheck "SelDetail: 1 detail(s) selected" "SelDetail selected the named detail"
vtcheck "HideLayersInDetail: 1 layer(s) in 1 detail(s)" "HideLayersInDetail"
vtcheck "CopyDetailToViewport: Iso -> Perspective" "CopyDetailToViewport"
vtcheck "CopyLayout: created Sheet1 Copy with 2 detail(s)" "CopyLayout duplicated the page"
vtcheck "LayoutProperties: Sheet2 420 x 297 mm" "LayoutProperties renamed and resized the page"
vtcheck "Layouts: switched to Model" "Layouts Model switched back to model space"
vtcheck "SetTurntableAnimation: 6 frames over 360 degrees in Perspective" "SetTurntableAnimation"
vtcheck "ViewFrameNumber: frame 3 of 6" "ViewFrameNumber"
vtcheck "ViewLastFrame: frame 6 of 6" "ViewLastFrame"
vtcheck "PlayAnimation: finished 6 frames" "PlayAnimation stepped through every frame without blocking"
vtcheck "RecordAnimation: wrote 6 frames to $TMP/vt/frames" "RecordAnimation wrote every frame"
vtcheck "SplitViewportHorizontal: added Perspective 2 (5 viewports)" "SplitViewportHorizontal added a viewport"
vtcheck "CloseViewport: closed Perspective 2 (4 left)" "CloseViewport removed it"
vtcheck "Layouts: 2 layout(s); active: Model" "layouts survived the .3dm round-trip"
vtcheck "NamedCPlane: 3 named CPlane(s)" "named CPlanes survived the .3dm round-trip"
vtcheck "SelClippingPlane: 2 clipping plane(s) selected" "clipping planes survived the .3dm round-trip"
vtcheck "^ok   expect_objects 1" "view-tools script ended with the sphere only"
vtcheck "gl_error=0" "view-tools script: no OpenGL errors (clip distances)"
FRAMES="$(ls "$TMP/vt/frames"/frame_*.bmp 2>/dev/null | wc -l)"
[ "$FRAMES" -ge 3 ] && echo "ok   RecordAnimation wrote $FRAMES BMP frames" || { echo "FAIL RecordAnimation frames ($FRAMES)"; fail=1; }
[ -s "$TMP/vt/frames/frame_0001.bmp" ] && [ "$(head -c 2 "$TMP/vt/frames/frame_0001.bmp")" = "BM" ] && echo "ok   frame_0001.bmp is a BMP" || { echo "FAIL frame_0001.bmp"; fail=1; }
grep -q "^Upper" "$TMP/vt/clipping.txt" && echo "ok   ExportClippingSectionInfo listed the Upper plane" || { echo "FAIL clipping.txt"; fail=1; }

# Lua scripting: RunScript/rs.* API, "= expr" inline evaluation, rs.GetPoint
# fed by a trailing script token (see script_script.txt).
cat > "$TMP/t.lua" <<'LUA'
-- Builds a box and a sphere, unions them, tags and files the result, then
-- reports its bounding box and volume before picking up a marker point.
rs.AddLayer("Parts")
local box = rs.AddBox({0, 0, 0}, {10, 10, 10})
local sphere = rs.AddSphere({5, 5, 12}, 4)
rs.MoveObject(sphere, {0, 0, -3})
local made = rs.BooleanUnion({box, sphere})
if not made or #made == 0 then error("BooleanUnion produced nothing") end
local widget = made[1]
rs.ObjectName(widget, "Widget")
rs.ObjectLayer(widget, "Parts")
local bb = rs.BoundingBox(widget)
print(string.format("bbox min %.0f,%.0f,%.0f", bb[1].x, bb[1].y, bb[1].z))
print(string.format("bbox max %.0f,%.0f,%.0f", bb[7].x, bb[7].y, bb[7].z))
local vol = rs.SurfaceVolume(widget)
print(string.format("widget volume %.2f", vol))
if vol < 1000 or vol > 1268 then error("volume out of expected range: " .. tostring(vol)) end
local p = rs.GetPoint("Pick a marker point")
rs.AddPoint(p.x, p.y, p.z)
print(string.format("picked point %.0f,%.0f,%.0f", p.x, p.y, p.z))
rs.UnselectAllObjects()
rs.SelectObject(widget)
print("selected by name lookup: " .. tostring(#rs.ObjectsByName("Widget")))
LUA
sed "s|@TMP@|$TMP|g" "$HERE/script_script.txt" > "$TMP/script_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SC="$("$BIN" --smoke 100 --script "$TMP/script_script.txt" 2>&1)" || { echo "$SC"; echo "FAIL: script script exited non-zero"; exit 1; }
else
  SC="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/script_script.txt" 2>&1)" || { echo "$SC"; echo "FAIL: script script exited non-zero"; exit 1; }
fi
echo "$SC" | grep -E "^(ok|FAIL)"
if echo "$SC" | grep -q "^FAIL"; then fail=1; fi
sccheck() { if echo "$SC" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
sccheck "history: inline curve length 5.00" "the \"= expr\" command line ran Lua inline (3-4-5 triangle)"
sccheck "history: bbox min 0,0,0" "RunScript's rs.BoundingBox reported the box's own corner"
sccheck "history: bbox max 10,10,13" "RunScript's rs.BoundingBox included the raised, unioned sphere"
sccheck "history: widget volume" "RunScript's rs.SurfaceVolume reported the unioned solid's volume"
sccheck "history: picked point 20,20,20" "rs.GetPoint was fed by the trailing script token"
sccheck "^ok   expect_objects 1" "the inline Lua expression added exactly the one line"
sccheck "^ok   expect_objects 3" "RunScript left the union, its point marker, and the earlier line"
sccheck "selected by name lookup: 1" "rs.ObjectsByName found the object rs.ObjectName renamed to Widget"
grep -q "! Script error" <<<"$SC" && { echo "FAIL script.txt printed a script error"; fail=1; } || echo "ok   no Lua script errors"

# Dino Flow + plug-ins: node editor, the HelloDino sample plug-in (command +
# Dino Flow node), and GrasshopperPlayer headless solve/bake (see flow_script.txt).
sed "s|@FLOWFILE@|$HERE/flow_graph.dflow|g" "$HERE/flow_script.txt" > "$TMP/flow_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 100 --script "$TMP/flow_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: flow script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/flow_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: flow script exited non-zero"; exit 1; }
fi
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
flcheck "Dino Flow: opened the node editor" "Grasshopper opened the Dino Flow panel"
flcheck "GrasshopperPluginList: 1 plug-in(s) found" "the plug-in loader found the HelloDino sample plug-in"
flcheck "HelloDino 1.0.0 - 1 command(s), 1 node(s)" "HelloDino loaded its command and Dino Flow node"
flcheck "HelloDino: hello from the sample plug-in!" "the HelloDino command ran"
flcheck "GrasshopperPlayer: solved 4 node(s)" "GrasshopperPlayer solved the sample graph"
flcheck "gl_error=0" "flow script ran without OpenGL errors"
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
flcheck "^ok   expect_objects 2" "GrasshopperPlayer baked the Line into the document"

# Object editing: Join/Explode/Rebuild/ChangeDegree/Offset/Extend/Flip/Dir/MakePeriodic/
# Weight/InsertKnot/PointsOn/SetObjectName/Group/Hide/Lock/clipboard/Undo (see edit_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  ED="$("$BIN" --smoke 150 --script "$HERE/edit_script.txt" 2>&1)" || { echo "$ED"; echo "FAIL: edit script exited non-zero"; exit 1; }
else
  ED="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/edit_script.txt" 2>&1)" || { echo "$ED"; echo "FAIL: edit script exited non-zero"; exit 1; }
fi
echo "$ED" | grep -E "^(ok|FAIL)"
if echo "$ED" | grep -q "^FAIL"; then fail=1; fi
echo "$ED" | grep -q "^smoke:" || { echo "$ED"; echo "FAIL: edit script produced no smoke line"; fail=1; }

# Layers: NewLayer/SetLayer/ChangeLayer/ChangeToCurrentLayer/MatchLayer/SetLayerToObject/
# OneLayerOn/OneLayerOff/AllLayersOn/LayerOn/LayerOff/LayerLock/LayerUnlock/Purge (see layer_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  LY="$("$BIN" --smoke 150 --script "$HERE/layer_script.txt" 2>&1)" || { echo "$LY"; echo "FAIL: layer script exited non-zero"; exit 1; }
else
  LY="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/layer_script.txt" 2>&1)" || { echo "$LY"; echo "FAIL: layer script exited non-zero"; exit 1; }
fi
echo "$LY" | grep -E "^(ok|FAIL)"
if echo "$LY" | grep -q "^FAIL"; then fail=1; fi
echo "$LY" | grep -q "^smoke:" || { echo "$LY"; echo "FAIL: layer script produced no smoke line"; fail=1; }

# Selection: every Sel* command in cmd_select.cpp and cmd_select2.cpp (see select_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SL="$("$BIN" --smoke 200 --script "$HERE/select_script.txt" 2>&1)" || { echo "$SL"; echo "FAIL: select script exited non-zero"; exit 1; }
else
  SL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/select_script.txt" 2>&1)" || { echo "$SL"; echo "FAIL: select script exited non-zero"; exit 1; }
fi
echo "$SL" | grep -E "^(ok|FAIL)"
if echo "$SL" | grep -q "^FAIL"; then fail=1; fi
echo "$SL" | grep -q "^smoke:" || { echo "$SL"; echo "FAIL: select script produced no smoke line"; fail=1; }

# Transforms: exact coordinates after Move/Copy/Rotate/Scale*/Mirror/Array*/Orient*/
# ProjectToCPlane/SetPt/Nudge, in Top/Front/Right (see transform_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  TR="$("$BIN" --smoke 200 --script "$HERE/transform_script.txt" 2>&1)" || { echo "$TR"; echo "FAIL: transform script exited non-zero"; exit 1; }
else
  TR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/transform_script.txt" 2>&1)" || { echo "$TR"; echo "FAIL: transform script exited non-zero"; exit 1; }
fi
echo "$TR" | grep -E "^(ok|FAIL)"
if echo "$TR" | grep -q "^FAIL"; then fail=1; fi
echo "$TR" | grep -q "^smoke:" || { echo "$TR"; echo "FAIL: transform script produced no smoke line"; fail=1; }

# Analysis: Distance/Length/Area/Volume/AreaCentroid/VolumeCentroid/What/List/BoundingBox/
# Dir/Check/SelBadObjects/Angle/Radius/Diameter/Curvature/CurvatureGraph/Zebra/EMap/
# CurvatureAnalysis/DraftAngleAnalysis/ShowEdges/CrvDeviation/PointDeviation/Audit/SystemInfo
# (see analyze_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  AN="$("$BIN" --smoke 150 --script "$HERE/analyze_script.txt" 2>&1)" || { echo "$AN"; echo "FAIL: analyze script exited non-zero"; exit 1; }
else
  AN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/analyze_script.txt" 2>&1)" || { echo "$AN"; echo "FAIL: analyze script exited non-zero"; exit 1; }
fi
echo "$AN" | grep -E "^(ok|FAIL)"
if echo "$AN" | grep -q "^FAIL"; then fail=1; fi
echo "$AN" | grep -q "^smoke:" || { echo "$AN"; echo "FAIL: analyze script produced no smoke line"; fail=1; }

# Views: standard views, Zoom variants, display modes, NamedView Save/Restore, 4View/3View/
# MaxViewport, CPlane commands, viewport cycling (see view_script.txt).
mkdir -p "$TMP/view"
sed "s|@TMP@|$TMP/view|g" "$HERE/view_script.txt" > "$TMP/view_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  VW="$("$BIN" --smoke 150 --script "$TMP/view_script.txt" 2>&1)" || { echo "$VW"; echo "FAIL: view script exited non-zero"; exit 1; }
else
  VW="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/view_script.txt" 2>&1)" || { echo "$VW"; echo "FAIL: view script exited non-zero"; exit 1; }
fi
echo "$VW" | grep -E "^(ok|FAIL)"
if echo "$VW" | grep -q "^FAIL"; then fail=1; fi
echo "$VW" | grep -q "^smoke:" || { echo "$VW"; echo "FAIL: view script produced no smoke line"; fail=1; }
[ -s "$TMP/view/view.bmp" ] && echo "ok   ViewCaptureToFile wrote view.bmp" || { echo "FAIL ViewCaptureToFile"; fail=1; }
[ -s "$TMP/view/screen.bmp" ] && echo "ok   ScreenCaptureToFile wrote screen.bmp" || { echo "FAIL ScreenCaptureToFile"; fail=1; }

# Extended state/window/misc: the remaining cmd_state.cpp and cmd_misc.cpp
# commands not already exercised elsewhere (see state_script2.txt).
mkdir -p "$TMP/state2"
sed "s|@TMP@|$TMP/state2|g" "$HERE/state_script2.txt" > "$TMP/state_script2.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  S2="$("$BIN" --smoke 200 --script "$TMP/state_script2.txt" 2>&1)" || { echo "$S2"; echo "FAIL: state2 script exited non-zero"; exit 1; }
else
  S2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMP/state_script2.txt" 2>&1)" || { echo "$S2"; echo "FAIL: state2 script exited non-zero"; exit 1; }
fi
echo "$S2" | grep -E "^(ok|FAIL)"
if echo "$S2" | grep -q "^FAIL"; then fail=1; fi
echo "$S2" | grep -q "^smoke:" || { echo "$S2"; echo "FAIL: state2 script produced no smoke line"; fail=1; }
s2check() { if echo "$S2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
s2check "Echo on" "Echo toggled on"
s2check "Echo off" "Echo toggled off"
s2check "Redraw on" "SetRedrawOn"
s2check "Redraw off" "SetRedrawOff"
s2check "ResetMessageBoxes: all 'do not show again' choices cleared" "ResetMessageBoxes"
s2check "Fullscreen on" "Fullscreen on"
s2check "Fullscreen off" "Fullscreen off"
s2check "Window maximized" "Maximize"
s2check "Window restored" "Restore"
s2check "SplitViewportVertical: added .* viewports" "SplitViewportVertical (cmd_viewtools's real implementation)"
s2check "BringViewportToTop: " "BringViewportToTop (cmd_viewtools's real implementation)"
s2check "SetMaximizedViewport: Perspective" "SetMaximizedViewport (cmd_viewtools's real implementation)"
s2check "ViewportTabs: hidden" "ViewportTabs (cmd_viewtools's real implementation)"
s2check "Zoom1To1Calibrate: .* pixels per mm" "Zoom1To1Calibrate (cmd_viewtools's real implementation)"
s2check "SetZoomExtentsBorder: border factor 1.2" "SetZoomExtentsBorder (cmd_viewtools's real implementation)"
s2check "Ortho angle = 45 deg" "OrthoAngle"
s2check "Grid snap size = 2.5" "SnapSize"
s2check "Grid snap on, size = 5" "SetSnap"
s2check "DragMode = World" "DragMode World (also covers the removed Dragmode duplicate)"
s2check "Drag strength = 50%" "DragStrength"
s2check "Drag copy on" "DragCopy"
s2check "Options exported to " "OptionsExport"
s2check "Options imported from " "OptionsImport"
s2check "Working folder: " "SetWorkingFolder"
s2check "Autosave: " "Autosave"
s2check "Model base point 1,2,3" "ModelBasepoint"
s2check "Earth anchor point 4,5,6" "EarthAnchorPoint"
s2check "PointCloud: 2 point(s) grouped" "PointCloud"
s2check "InfinitePlane: 10000 x 10000 plane" "InfinitePlane"
s2check "BringToFront: 5 object(s)" "BringToFront (DrawOrder family)"
s2check "Bounce: polyline with 1 bounce(s)" "Bounce (cmd_solidtools's real ray-bounce, no longer shadowed)"
s2check "GumballAlignment = World" "GumballAlignment"
s2check "GumballScaleMode = Uniform" "GumballScaleMode"
s2check "Gumball auto reset off" "GumballAutoReset"
s2check "Gumball dynamic relocate on" "GumballDynamicRelocate"
s2check "Gumball origin 5,5,5" "GumballRelocate"
s2check "Gumball reset" "GumballReset"
s2check "ViewCaptureToClipboard: image written to" "ViewCaptureToClipboard"
s2check "ScreenCaptureToClipboard: image written to" "ScreenCaptureToClipboard"
s2check "Alias qq -> Box" "Alias"
if echo "$S2" | grep -qF "2+3*4 = 14"; then echo "ok   Calc"; else echo "FAIL Calc"; fail=1; fi
s2check "Left sidebar" "ToggleLeftSidebar"
s2check "Pause: continuing" "Pause"
s2check "MultiPause: continuing" "MultiPause"
s2check "GetIssueState: ok - no issues reported" "GetIssueState"
s2check "Minimize: skipped in headless mode" "Minimize (headless-safe)"
s2check "Swapped views of " "SwapView"
s2check "Single viewport layout" "OneView"
s2check "Ortho on (angle 90 deg)" "SetOrtho"
s2check "Ortho snap to CPlane Z on" "OrthoSnapToCPlaneZ"
s2check "Snap to locked objects off" "SnapToLocked"
s2check "Snap to occluded objects off" "SnapToOccluded"
s2check "Snap to meshes off" "SnapToMeshes"
s2check "Snap to mesh objects off" "SnapToMeshObject"
s2check "Snap to SubD objects off" "SnapToSubDObject"
s2check "Osnap panel hidden" "ShowOsnap"
s2check "Remember copy options on" "RememberCopyOptions"
s2check "DollyZoom: lens 85 mm" "DollyZoom"
s2check "Right sidebar (Layers, Properties) hidden" "ToggleRightSidebar"
s2check "Toolbar shown" "ShowToolbar"
s2check "Toolbar lock on" "ToolbarLock"
s2check "Recent commands:" "PopupPopular"
s2check "Menus: File, Edit, View, Curve" "Menus"
s2check "Reset: snaps, panels, toolbar and preferences restored" "Reset"
s2check "FileExplorer: " "FileExplorer"
s2check "OpenURL: https://example.org/open" "OpenURL"
s2check "WebBrowser: https://example.org/browser" "WebBrowser"
s2check "Gumball: on, alignment CPlane, scale Uniform" "GumballSettings"
s2check "NamedPosition: saved object positions are planned" "NamedPosition"
s2check "[0-9]* snapshot(s)" "Snapshots (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "HistoryPurge: no construction history is recorded; nothing to purge" "HistoryPurge"
s2check "HistoryUpdate: no construction history is recorded; nothing to update" "HistoryUpdate"
s2check "[0-9]* attached reference model" "Worksession (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "LimitReferenceModel: 0 object(s) removed from 'nonexistent.3dm'" "LimitReferenceModel (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "ContentFilter: render content filtering is planned" "ContentFilter"
s2check "Dino 8 is free software. No licence keys" "Licenses"
s2check "Dino 8 does not phone home" "CheckForUpdates"
s2check "Support: open an issue at " "TechSupport"
s2check "History: not recorded. Every edit is captured by the snapshot undo instead" "History"
s2check "RecordHistory: not needed; undo snapshots cover every change" "RecordHistory"
s2check "Text: [0-9]* curve(s) from " "Text (annotate's real text-curve command, no longer shadowed)"
s2check "Dino Flow: opened the node editor" "Grasshopper"
s2check "PackageManager: opened the package manager" "PackageManager"
s2check "PluginManager: opened the plug-in manager" "PluginManager"

# Files: New/Open/Revert/Save/SaveAs/SaveSmall/IncrementalSave/SaveAsTemplate/
# Import/Export/ExportSelected/ExportWithOrigin/Notes/DocumentProperties/Units/
# Audit3dmFile (see file_script.txt).
mkdir -p "$TMP/file"
sed "s|@TMP@|$TMP/file|g" "$HERE/file_script.txt" > "$TMP/file_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 150 --script "$TMP/file_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: file script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/file_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: file script exited non-zero"; exit 1; }
fi
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
echo "$FL" | grep -q "^smoke:" || { echo "$FL"; echo "FAIL: file script produced no smoke line"; fail=1; }
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
flcheck "Saved $TMP/file/file1.3dm" "Save wrote file1.3dm"
flcheck "Opened $TMP/file/file1.3dm (2 objects)" "Open re-read file1.3dm"
flcheck "Saved $TMP/file/file2.3dm" "SaveAs wrote file2.3dm"
flcheck "Opened $TMP/file/file2.3dm (2 objects)" "Open re-read file2.3dm"
flcheck "Imported $TMP/file/file1.3dm" "Import brought file1.3dm's objects in"
flcheck "Exported $TMP/file/export1.obj" "Export wrote export1.obj"
test -s "$TMP/file/file1.3dm" && echo "ok   file1.3dm exists" || { echo "FAIL file1.3dm missing"; fail=1; }
test -s "$TMP/file/file2.3dm" && echo "ok   file2.3dm exists" || { echo "FAIL file2.3dm missing"; fail=1; }
test -s "$TMP/file/file2_1.3dm" && echo "ok   IncrementalSave wrote file2_1.3dm" || { echo "FAIL file2_1.3dm missing"; fail=1; }
test -s "$TMP/file/file2_1_2.3dm" && echo "ok   IncrementalSave wrote file2_1_2.3dm" || { echo "FAIL file2_1_2.3dm missing"; fail=1; }
test -s "$TMP/file/export1.obj" && echo "ok   export1.obj exists" || { echo "FAIL export1.obj missing"; fail=1; }

# Creation: Points/Lines/InterpCrv/CurveThroughPt/Sketch/Circle3Pt/CircleD/Arc3Pt/
# Rectangle3Pt/Polygon/PolygonStar/Ellipse/Helix/Spiral/PointGrid/Divide/ClosestPt/
# Plane3Pt/SrfPt (see create_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  CR="$("$BIN" --smoke 150 --script "$HERE/create_script.txt" 2>&1)" || { echo "$CR"; echo "FAIL: create script exited non-zero"; exit 1; }
else
  CR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/create_script.txt" 2>&1)" || { echo "$CR"; echo "FAIL: create script exited non-zero"; exit 1; }
fi
echo "$CR" | grep -E "^(ok|FAIL)"
if echo "$CR" | grep -q "^FAIL"; then fail=1; fi
echo "$CR" | grep -q "^smoke:" || { echo "$CR"; echo "FAIL: create script produced no smoke line"; fail=1; }
crcheck() { if echo "$CR" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
crcheck "degree 2, 9 control points, rational, closed" "Circle3Pt/CircleD/Ellipse built rational closed conics"
crcheck "CV\[0\] 10,0,0" "Circle3Pt through (10,0,0),(-10,0,0),(0,10,0) is centred on the origin with radius 10"
crcheck "CV\[2\] 5,5,0" "CircleD from (0,0,0) to (10,0,0) passes through the diameter's midpoint region"
crcheck "degree 2, 5 control points, rational, open" "Arc3Pt built an open rational arc"
crcheck "degree 1, 5 control points, non-rational, closed" "Rectangle3Pt closed with 5 CVs (start repeated)"
crcheck "CV\[2\] 0,5,0" "Ellipse's minor axis is 5 units, matching the second axis point"
crcheck "degree 1, 6 control points, non-rational, closed" "Polygon NumSides=5 built a closed 6-CV (5+seam) polygon"
crcheck "CV\[0\] 10,0,0" "Polygon's first corner sits at the picked radius"
crcheck "degree 1, 13 control points, non-rational, closed" "PolygonStar NumSides=6 built a closed 13-CV (12+seam) star"
crcheck "Closest point 5,0,0 distance 5" "ClosestPt found the nearest point on the line"
crcheck "degree 1 x 1, CVs 2 x 2" "Plane3Pt/SrfPt built flat 4-CV surfaces"

# Second-wave drafting tools: hatch library, tables, GD&T, multi-leaders,
# live section views (see drafting2_script.txt).
sed "s|@TMP@|$TMP|g" "$HERE/drafting2_script.txt" > "$TMP/drafting2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  D2="$("$BIN" --smoke 150 --script "$TMP/drafting2_script.txt" 2>&1)" || { echo "$D2"; echo "FAIL: drafting2 script exited non-zero"; exit 1; }
else
  D2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/drafting2_script.txt" 2>&1)" || { echo "$D2"; echo "FAIL: drafting2 script exited non-zero"; exit 1; }
fi
d2check() { if echo "$D2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
d2check "Hatch: 1 boundary(ies) hatched (ANSI31)" "Hatch used the ANSI31 library pattern"
d2check "Table: 2x2 table created" "Table built a 2x2 grid"
d2check "TableEdit: table rebuilt (2x2)" "TableEdit rebuilt the table in place"
d2check "RevisionTable: 0 revision" "RevisionTable started with a header only"
d2check "RevisionTable: 1 revision" "RevisionTable appended a row"
d2check "TitleBlock: Widget (Sheet A1, Scale 1:2)" "TitleBlock recorded name/sheet/scale"
d2check "FeatureControlFrame: Position 0.1 | A,B" "FeatureControlFrame built a frame with datums"
d2check "DatumFeature: 'A'" "DatumFeature labelled the datum"
d2check "SurfaceFinish: Ra 1.6" "SurfaceFinish recorded the roughness value"
d2check "WeldSymbol: Fillet (Above)" "WeldSymbol drew the fillet glyph"
d2check "MultiLeader: 2 arrow(s), \"Note\"" "MultiLeader built two arrows to one landing"
d2check "DimTolerance: 1 dimension(s) updated" "DimTolerance appended a tolerance to the dimension"
d2check "BillOfMaterials: " "BillOfMaterials built a table over the scene objects"
d2check "SectionView: " "SectionView sliced the box"
d2check "UpdateSectionViews: 1 section view(s) regenerated" "UpdateSectionViews rebuilt the section from its stored plane"
d2check "gl_error=0" "drafting2 script ran without OpenGL errors"

# CPU path tracer: material library presets, RenderAssignMaterialToObjects
# Preset=, the RayTracedViewport display mode, Render/RenderArctic/
# RenderPreview with Quality=Raytraced (see raytrace_script.txt).
sed "s|@TMP@|$TMP/rt|g" "$HERE/raytrace_script.txt" > "$TMP/raytrace_script.txt"
mkdir -p "$TMP/rt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  RT="$("$BIN" --smoke 60 --script "$TMP/raytrace_script.txt" 2>&1)" || { echo "$RT"; echo "FAIL: raytrace script exited non-zero"; exit 1; }
else
  RT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMP/raytrace_script.txt" 2>&1)" || { echo "$RT"; echo "FAIL: raytrace script exited non-zero"; exit 1; }
fi
rtcheck() { if echo "$RT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
rtcheck "Created material ChromeMat from preset Chrome" "RenderAssignMaterialToObjects Preset= created a material from the built-in library"
rtcheck "Material GoldMat assigned to 1 object(s)" "material from a preset assigned to an object"
rtcheck "MaterialLibrary: 48 built-in preset(s)" "MaterialLibrary reports the full preset count"
rtcheck "Render: rendered Perspective at 96 x 64 .* \[Raytraced Samples=4 Bounces=2 Denoise=Yes\]" "Render honoured Quality=Raytraced Samples= Bounces="
rtcheck "Saved rendering $TMP/rt/raytrace.bmp (96 x 64)" "SaveRenderWindowAs wrote the raytraced BMP"
rtcheck "RenderArctic: rendered Perspective at 1280 x 720 .* \[Raytraced" "RenderArctic ran the path tracer at the document size"
rtcheck "RenderPreview: rendered Perspective .* \[Raytraced" "RenderPreview ran the path tracer at viewport size"
rtcheck "Saved $TMP/rt/raytrace.3dm" "the raytraced scene saved to a .3dm"
rtcheck "gl_error=0" "no OpenGL errors while the viewport was in RayTracedViewport mode"
python3 - "$TMP/rt/raytrace.bmp" <<'PY' && echo "ok   raytrace.bmp is a valid, non-flat 24-bit BMP" || { echo "FAIL raytrace.bmp invalid or flat"; fail=1; }
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:2] == b'BM', 'signature'
size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
assert size == len(d) and hdr == 40 and w == 96 and h == 64 and planes == 1 and bpp == 24, (size, len(d), w, h, bpp)
px = d[off:]
assert len(px) == ((w * 3 + 3) & ~3) * h, 'pixel data size'
assert max(px) > 0 and min(px) < 255, 'image is flat'
PY

# IGES / STEP round-trip: Box, Sphere, Cylinder, a trimmed planar surface,
# a free NURBS curve, a point, and a hand-written STEP fixture (see
# igesstep_script.txt and step_plane_face.stp).
sed "s|@TMP@|$TMP|g" "$HERE/igesstep_script.txt" > "$TMP/igesstep_script.txt"
cp "$HERE/step_plane_face.stp" "$TMP/step_plane_face.stp"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  IS="$("$BIN" --smoke 200 --script "$TMP/igesstep_script.txt" 2>&1)" || { echo "$IS"; echo "FAIL: iges/step script exited non-zero"; exit 1; }
else
  IS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMP/igesstep_script.txt" 2>&1)" || { echo "$IS"; echo "FAIL: iges/step script exited non-zero"; exit 1; }
fi
ischeck() { if echo "$IS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
ischeck "Exported $TMP/box.igs" "IGES export wrote a file"
ischeck "Exported $TMP/box.stp" "STEP export wrote a file"
ischeck "Volume = 1000" "box volume survived an IGES and a STEP round-trip"
ischeck "IGES: .*[1-9][0-9]* brep" "IGES import rebuilt at least one brep from the combined scene"
ischeck "STEP: [1-9][0-9]* brep" "STEP import rebuilt at least one brep from the combined scene"
ischeck "STEP: .*[1-9][0-9]* curve" "STEP import read the free NURBS curve back"
ischeck "IGES: .*[1-9][0-9]* curve" "IGES import read the free NURBS curve back"
ischeck "STEP: .*[1-9][0-9]* point" "STEP import read the point back"
ischeck "IGES: .*[1-9][0-9]* point" "IGES import read the point back"
ischeck "STEP: 1 brep (1 trimmed face), 1 curve, 0 points" "the hand-written STEP fixture (PLANE face + CIRCLE) imported as 2 objects"
grep -qE "^ {5}128" "$TMP/t.igs" && grep -qE "^ {5}144" "$TMP/t.igs" && echo "ok   t.igs uses 128 (surface) and 144 (trimmed surface) entities" || { echo "FAIL t.igs entity types"; fail=1; }
grep -q "=ADVANCED_FACE(" "$TMP/t.stp" && grep -q "B_SPLINE_SURFACE_WITH_KNOTS(" "$TMP/t.stp" && echo "ok   t.stp uses ADVANCED_FACE and B_SPLINE_SURFACE_WITH_KNOTS entities" || { echo "FAIL t.stp entity types"; fail=1; }
grep -q "^ISO-10303-21;$" "$TMP/t.stp" && grep -q "^END-ISO-10303-21;$" "$TMP/t.stp" && echo "ok   t.stp is a complete Part 21 file" || { echo "FAIL t.stp malformed"; fail=1; }

# SpaceMouse / 3Dconnexion: Protocol=File replay drives a real background
# thread (see input/SpaceMouse.cpp) that Application::Frame() drains every
# frame, so @wait gives it real wall-clock time before each check below.
cat > "$TMP/spacemouse_deltas.txt" <<'EOS'
BUTTON 1
0 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
1 0 0 0 0 0
EOS
sed "s|@TMP@|$TMP|g" "$HERE/spacemouse_script.txt" > "$TMP/spacemouse_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SM="$("$BIN" --smoke 250 --script "$TMP/spacemouse_script.txt" 2>&1)" || { echo "$SM"; echo "FAIL: spacemouse script exited non-zero"; exit 1; }
else
  SM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 250 --script "$TMP/spacemouse_script.txt" 2>&1)" || { echo "$SM"; echo "FAIL: spacemouse script exited non-zero"; exit 1; }
fi
smcheck() { if echo "$SM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
smcheck "SpaceMouse protocol set to File" "SpaceMouseProtocol switched to the File protocol"
smcheck "connected (File:" "SpaceMouse connected over the File protocol"
smcheck "^history: Command: Top$" "a button carried on a motion sample still fired its mapped command (button 1 default -> Top)"
smcheck "SpaceMouse mode set to Camera" "SpaceMouseMode set Camera mode"
smcheck "SpaceMouse mode set to Object" "SpaceMouseMode set Object mode"
# Camera mode's own pan/dolly/orbit is driven by the same drained-delta path
# Object mode exercises below, so it is not re-asserted numerically here:
# a Camera::Pan() screen-space shift depends on how many of the 15 File
# samples land in a single frame's drain, which is a real race against the
# background thread's 15ms-per-sample pacing - deterministic in Object
# mode (a flat per-sample point offset) but not worth pinning down to the
# pixel for Camera mode too.
if echo "$SM" | grep -q "Bounding box min 0,0,0 max 10,10,10"; then echo "FAIL: SpaceMouse Object mode did not move the selected box"; fail=1; else echo "ok   SpaceMouse Object mode moved the selected box (30 units along +tx: 15 samples x sensitivity 1 x scale 2)"; fi
smcheck "Bounding box min 30,0,0 max 40,10,10" "the box moved by exactly 15 x 2 = 30 units in Object mode"
echo "$SM" | grep -E "^(ok|FAIL)" || true
if echo "$SM" | grep -q "^FAIL"; then fail=1; fi

# Parametric 2D sketch constraints: Coincident/Horizontal/Vertical/Distance/
# Radius/Perpendicular/Parallel/Fixed/Midpoint, auto re-solve, glyph overlay.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  CN="$("$BIN" --smoke 120 --script "$HERE/constraints_script.txt" 2>&1)" || { echo "$CN"; echo "FAIL: constraints script exited non-zero"; exit 1; }
else
  CN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$HERE/constraints_script.txt" 2>&1)" || { echo "$CN"; echo "FAIL: constraints script exited non-zero"; exit 1; }
fi
cncheck() { if echo "$CN" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
cncheck "Coincident constraint #1 added." "Constrain built a Coincident constraint"
cncheck "^history: Coincident #1: ok$" "Coincident solved"
cncheck "Horizontal constraint #2 added." "Constrain built a Horizontal constraint"
cncheck "Vertical constraint #3 added." "Constrain built a Vertical constraint"
cncheck "Distance constraint #4 added." "Constrain built a Distance constraint"
cncheck "Distance #4: length 10" "Distance solved to exactly 10"
cncheck "Radius constraint #5 added." "Constrain built a Radius constraint"
cncheck "Radius #5: radius 5" "Radius solved to exactly 5"
cncheck "Perpendicular constraint #6 added." "Constrain built a Perpendicular constraint"
cncheck "Perpendicular #6: angle 90" "Perpendicular solved to exactly 90 degrees"
cncheck "Parallel constraint #7 added." "Constrain built a Parallel constraint"
cncheck "Fixed constraint #8 added." "Constrain built a Fixed constraint"
cncheck "Midpoint constraint #9 added." "Constrain built a Midpoint constraint"
cncheck "ConstraintsShow: glyphs on (9 constraint(s))" "ConstraintsShow toggled the glyph overlay"
cncheck "ConstraintDelete: removed every constraint" "ConstraintDelete All cleared the list"
echo "$CN" | grep -E "^(ok|FAIL)" || true
if echo "$CN" | grep -q "^FAIL"; then fail=1; fi

# Parametric architectural components: Wall/Door/Window/Slab/Roof/Stair/
# Column/Beam, ArchEdit rebuild, ArchDelete, ArchSchedule.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  AR="$("$BIN" --smoke 150 --script "$HERE/arch_script.txt" 2>&1)" || { echo "$AR"; echo "FAIL: arch script exited non-zero"; exit 1; }
else
  AR="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/arch_script.txt" 2>&1)" || { echo "$AR"; echo "FAIL: arch script exited non-zero"; exit 1; }
fi
archeck() { if echo "$AR" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
archeck "Wall #1 added." "Wall command built a wall"
archeck "Door #2 added." "Door command cut an opening into the wall"
archeck "Window #3 added." "Window command cut a second opening into the wall"
archeck "ArchSlab #4 added." "ArchSlab command built a slab"
archeck "Roof #5 added." "Roof command built a gable roof"
archeck "Stair #6 added." "Stair command built a 3-riser stair"
archeck "Column #7 added." "Column command built a column"
archeck "Column #7 rebuilt (Height = 5.000000)." "ArchEdit rebuilt the column at a new height"
archeck "Beam #8 added." "Beam command built a beam"
archeck "ArchDelete: removed 1 component(s)" "ArchDelete removed the beam"
archeck "Architectural schedule (8 component(s)):" "ArchSchedule counted every component before the delete"
archeck "Architectural schedule (7 component(s)):" "ArchSchedule counted every component after the delete"
archeck "Object 3 (mesh) layer Default name 'Wall'" "the wall survived two boolean cuts as one mesh object"
echo "$AR" | grep -E "^(ok|FAIL)" || true
if echo "$AR" | grep -q "^FAIL"; then fail=1; fi

# Session: 3D digitizer (Dig*, Protocol=File test mode), Worksession /
# LimitReferenceModel, Snapshots, draw order, and real hole features
# (Move/Copy/Rotate/MirrorHole) (see session_script.txt).
mkdir -p "$TMP/sess"
cat > "$TMP/sess/dig_points.txt" <<'EOP'
# comments and blank lines are ignored
1, 2, 3
4.5 5.5 6.5 1
EOP
sed "s|@TMP@|$TMP/sess|g" "$HERE/session_script.txt" > "$TMP/sess/session_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SS="$("$BIN" --smoke 150 --script "$TMP/sess/session_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: session script exited non-zero"; exit 1; }
else
  SS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$TMP/sess/session_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: session script exited non-zero"; exit 1; }
fi
sscheck() { if echo "$SS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
sscheck "Digitizer: connected, protocol File, file $TMP/sess/dig_points.txt" "DigConnect opened the fixture file"
sscheck "DigPoint: digitized 1,2,3" "DigPoint read the first fixture point"
sscheck "DigPoint: digitized 4.5,5.5,6.5 (button 1)" "DigPoint read the second point and its button"
sscheck "DigPoint: no point available" "DigPoint warns once the fixture file is exhausted"
sscheck "DigScale: scale set to 25.4" "DigScale set the unit scale"
sscheck "DigDisconnect: disconnected" "DigDisconnect"
sscheck "Digitizer: not connected" "DigStatus reports disconnected after DigDisconnect"
sscheck "BringToFront: " "BringToFront ran on the overlapping circles"
sscheck "Worksession: attached $TMP/sess/ref.3dm (1 object" "Worksession Attach copied the box in"
sscheck "Worksession: 1 attached reference model" "Worksession List shows the attached model"
sscheck "LimitReferenceModel: 0 object(s) removed" "LimitReferenceModel kept the box inside the limit box"
sscheck "Worksession: saved $TMP/sess/session.rws" "Worksession Save wrote the .rws file"
sscheck "Worksession: detached 1 object" "Worksession Detach removed the reference objects"
sscheck "Worksession: attached 1 model(s) from $TMP/sess/session.rws" "Worksession Load re-attached from the .rws file"
sscheck "Snapshot 'Before' saved" "Snapshots Save captured the sphere-only state"
sscheck "Snapshot 'Before' restored" "Snapshots Restore reverted the later Box"
sscheck "^ok   expect_objects 1" "Snapshots Restore actually removed the Box"
sscheck "Snapshot 'Before' deleted" "Snapshots Delete"
sscheck "RoundHole: object .* replaced by a mesh solid" "RoundHole cut the box and tagged it a hole feature"
sscheck "MoveHole: object .* re-cut at the new placement" "MoveHole replayed the boolean at the new position"
sscheck "CopyHole: copied object .* to object .*" "CopyHole added a second hole into a duplicate"
sscheck "RotateHole: object .* re-cut at the new placement" "RotateHole replayed the boolean"
sscheck "MirrorHole: copied object .* to object .*" "MirrorHole added a mirrored hole into a duplicate"

# Volumetric remesh tools: ShrinkWrap (signed-distance + marching cubes, incl.
# a concave L-shaped union), QuadRemesh (surface UV grid + dual contouring),
# ReduceMesh (quadric-error decimation) (see remesh_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  RM="$("$BIN" --smoke 150 --script "$HERE/remesh_script.txt" 2>&1)" || { echo "$RM"; echo "FAIL: remesh script exited non-zero"; exit 1; }
else
  RM="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/remesh_script.txt" 2>&1)" || { echo "$RM"; echo "FAIL: remesh script exited non-zero"; exit 1; }
fi
rmcheck() { if echo "$RM" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
rmcheck "BooleanUnion: 54 faces, volume 3000" "BooleanUnion built the L-shaped union (2000 + 2000 - 1000 overlap)"
rmcheck "ShrinkWrap: signed-distance wrap with [0-9]* vertices, [0-9]* faces (closed)" "ShrinkWrap wrapped the L-shape into a closed mesh"
rmcheck "Volume = 2[0-9][0-9][0-9] cubic" "ShrinkWrap volume stays close to the L-shape's own volume (3000)"
SW_VOL="$(echo "$RM" | sed -n 's/.*ShrinkWrap: .*volume \([0-9.eE+]*\)$/\1/p' | head -1)"
python3 -c "import sys; v=float('$SW_VOL'); sys.exit(0 if v < 3500 else 1)" \
  && echo "ok   ShrinkWrap volume ($SW_VOL) is well under the convex hull volume (3500) - concavity preserved, not convex-hulled" \
  || { echo "FAIL ShrinkWrap volume ($SW_VOL) is not below the convex hull volume (3500) - looks convex-hulled"; fail=1; }
rmcheck "QuadRemesh: [0-9]* quad(s)" "QuadRemesh ran"
rmcheck "QuadRemesh: 1 object(s) remeshed" "QuadRemesh remeshed the surface"
rmcheck "ReduceMesh: object 9: 576 -> 288 faces" "ReduceMesh halved the mesh sphere's faces"
echo "$RM" | grep -E "^(ok|FAIL)"
if echo "$RM" | grep -q "^FAIL"; then fail=1; fi
rmcheck "smoke: frames=1[0-9][0-9] objects=7" "remesh script produced the expected object count"

exit $fail
