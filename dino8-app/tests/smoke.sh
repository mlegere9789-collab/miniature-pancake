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
c2check "^ok   expect_objects 34" "curve-tools script produced the expected object count"
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
secheck "^ok   expect_objects 42" "surface-edit script produced the expected object count"

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
mtcheck "Paraboloid: 544 faces, closed" "Paraboloid is closed"
mtcheck "PlanarMesh: created 1 mesh(es)" "PlanarMesh triangulated the circle"
mtcheck "Slab: created 1 mesh(es)" "Slab thickened the rectangle"
mtcheck "ExtractMeshPart: all 6 faces extracted" "ExtractMeshPart extracted the box"
mtcheck "MeshIntersect: 14 segment(s) in 2 polyline(s)" "MeshIntersect found the box/box intersection"
mtcheck "MeshPatch: 3 triangles from 4 points" "MeshPatch triangulated the points"
mtcheck "MeshOutline: created 1 outline curve(s)" "MeshOutline built the outline"
mtcheck "Drape: 20x20 grid draped over" "Drape hit the visible meshes"
echo "$MT" | grep -E "^(ok|FAIL)"
if echo "$MT" | grep -q "^FAIL"; then fail=1; fi
mtcheck "smoke: frames=1[0-9][0-9] objects=30" "mesh-tools script produced the expected object count"

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
sdcheck "ShrinkWrap: convex hull with 64 faces around 141 points" "ShrinkWrap built the 3D convex hull"
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
exit $fail
