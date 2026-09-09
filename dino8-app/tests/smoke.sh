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
  UI="$("$BIN" --smoke 320 --script "$HERE/ui_script.txt" 2>&1)" || true
else
  UI="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 320 --script "$HERE/ui_script.txt" 2>&1)" || true
fi
echo "$UI" | grep -E "^(ok|FAIL)"
if echo "$UI" | grep -q "^FAIL"; then fail=1; fi
echo "$UI" | grep -q "^ok   expect_objects 1" || { echo "FAIL ui script produced no checks"; fail=1; }
# LockViewport: the second Camera printout (after a locked middle-drag) must
# match the first exactly; the third (after unlocking and dragging again)
# must differ.
LV1="$(echo "$UI" | grep "^history: Camera (Top): location" | sed -n '1p')"
LV2="$(echo "$UI" | grep "^history: Camera (Top): location" | sed -n '2p')"
LV3="$(echo "$UI" | grep "^history: Camera (Top): location" | sed -n '3p')"
if [ -n "$LV1" ] && [ "$LV1" = "$LV2" ]; then echo "ok   LockViewport blocked a middle-button drag pan"; else echo "FAIL LockViewport did not block the pan ('$LV1' vs '$LV2')"; fail=1; fi
if [ -n "$LV3" ] && [ "$LV2" != "$LV3" ]; then echo "ok   toggling LockViewport off let the next drag pan the camera"; else echo "FAIL camera did not move after LockViewport was toggled back off ('$LV2' vs '$LV3')"; fail=1; fi
# OrthoAngle 45: a clicked line end near (12,5,0) from a (0,0,0) start must
# land exactly on a 45-degree ray (equal X and Y offsets), not on the
# world-axis-only 90-degree snap.
UI_CV1="$(echo "$UI" | grep -A1 'CV\[0\]' | grep 'CV\[1\]' | head -1)"
X0="$(echo "$UI" | grep 'CV\[0\]' | head -1 | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f1)"
Y0="$(echo "$UI" | grep 'CV\[0\]' | head -1 | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f2)"
X1="$(echo "$UI_CV1" | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f1)"
Y1="$(echo "$UI_CV1" | grep -oE '[-0-9.]+,[-0-9.]+,[-0-9.]+' | cut -d, -f2)"
if [ -n "$X0" ] && [ -n "$X1" ] && python3 -c "import sys; dx=$X1-($X0); dy=$Y1-($Y0); sys.exit(0 if dx>1 and abs(dx-dy)<0.05*dx else 1)"; then
  echo "ok   OrthoAngle 45 constrained the clicked line end to an exact 45-degree ray"
else
  echo "FAIL OrthoAngle 45 did not constrain the line to 45 degrees (CV[0] $X0,$Y0 CV[1] $X1,$Y1)"; fail=1
fi

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
  C2="$("$BIN" --smoke 400 --script "$HERE/curves2_script.txt" 2>&1)" || { echo "$C2"; echo "FAIL: curve-tools script exited non-zero"; exit 1; }
else
  C2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 400 --script "$HERE/curves2_script.txt" 2>&1)" || { echo "$C2"; echo "FAIL: curve-tools script exited non-zero"; exit 1; }
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
c2check "MergeCrv: joined 2 curve(s) into one, merged 1 tangent junction(s) into a single span" "MergeCrv collapsed a tangent joint into one span"
c2check "Blend: curvature-continuous (G2) blend curve created" "Blend built a real G2 quintic blend"
c2check "ArcBlend: two-arc tangent blend created (joint tangent match 1)" "ArcBlend found a genuine two-arc biarc, tangent-verified"
c2check "Domain: 1 curve(s) now have domain 0 to 5" "Domain actually set a new domain, not just reported it"
c2check "ModifyRadius: 1 curve(s) now have radius 12" "ModifyRadius rebuilt the circle in place"
c2check "Match: reshaped curve .* to meet curve .* tangentially" "Match reshaped one curve's end to meet another"
c2check "Curve does not self-intersect" "IntersectSelf ran a genuine self-intersection check"
c2check "SoftEditCrv: 4 control point(s) moved with falloff 5" "SoftEditCrv applied a real falloff drag"
c2check "FixedLengthCrvEdit: point moved and curve rescaled .* to keep length 30" "FixedLengthCrvEdit preserved the curve's total length"
c2check "CurveThroughSrfControlPt: 4 curve(s) through the control point rows and columns" "CurveThroughSrfControlPt built curves through the CV grid directly"
c2check "OffsetMultiple: 3 curve(s) created (3 offsets x 1)" "OffsetMultiple created several offsets in one command"
c2check "InsertLineIntoCrv: inserted a 5-long line and rejoined" "InsertLineIntoCrv split, inserted, and rejoined automatically"
c2check "CSec: 1 curve(s) from 5 section(s) along the rail" "CSec sectioned perpendicular to a rail curve"
c2check "ContinueCurve: curve extended with 2 new point(s) and joined" "ContinueCurve extended and auto-joined a curve"
c2check "EndBulge: end handle scaled by 2" "EndBulge scaled the end tangent handle"
c2check "InterpCrvOnSrf: curve interpolated through 3 point(s) on the surface" "InterpCrvOnSrf projected points onto the surface before interpolating"
c2check "^ok   expect_objects 70" "curve-tools script produced the expected object count"
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
# DXF fidelity: a freeform NURBS curve and a full ellipse must round-trip
# exactly (SPLINE/ELLIPSE entities), not as sampled polylines (see
# dxf_fidelity_script.txt).
sed "s|@TMP@|$TMP|g" "$HERE/dxf_fidelity_script.txt" > "$TMP/dxf_fidelity_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  DF="$("$BIN" --smoke 50 --script "$TMP/dxf_fidelity_script.txt" 2>&1)" || { echo "$DF"; echo "FAIL: DXF fidelity script exited non-zero"; exit 1; }
else
  DF="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 50 --script "$TMP/dxf_fidelity_script.txt" 2>&1)" || { echo "$DF"; echo "FAIL: DXF fidelity script exited non-zero"; exit 1; }
fi
dfcheck() { if echo "$DF" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
dfcheck "Exported $TMP/dxf_fidelity.dxf" "DXF export wrote a file"
[ "$(echo "$DF" | grep -c "degree 3, 4 control points, non-rational, open")" = "2" ] && echo "ok   DXF SPLINE round-tripped the curve's exact degree/CV count" || { echo "FAIL DXF SPLINE degree/CV count did not survive round-trip"; fail=1; }
[ "$(echo "$DF" | grep -c "CV\[0\] 0,0,0")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[1\] 5,10,0")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[2\] 10,-5,0")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[3\] 15,5,0")" = "2" ] && echo "ok   DXF SPLINE round-tripped the exact control points" || { echo "FAIL DXF SPLINE control points did not survive round-trip"; fail=1; }
[ "$(echo "$DF" | grep -c "Total length = ")" = "2" ] && [ "$(echo "$DF" | grep "Total length = " | sort -u | wc -l)" = "1" ] && echo "ok   DXF SPLINE round-tripped the exact combined curve length (freeform curve + rational ellipse)" || { echo "FAIL DXF round-trip changed the combined curve length"; fail=1; }
[ "$(echo "$DF" | grep -c "degree 2, 9 control points, rational, closed")" = "2" ] && [ "$(echo "$DF" | grep -c "CV\[1\] 38,3,0")" = "2" ] && echo "ok   DXF SPLINE round-tripped the ellipse's rational control points and weights" || { echo "FAIL DXF SPLINE lost the ellipse's rational control points/weights"; fail=1; }
grep -q "^SPLINE$" "$TMP/dxf_fidelity.dxf" && echo "ok   dxf_fidelity.dxf uses exact SPLINE entities, not sampled polylines" || { echo "FAIL dxf_fidelity.dxf entity types"; fail=1; }
# DWG round-trip (via GNU LibreDWG, see FileExchange.cpp's ExportDwg/
# ImportDwg): a line, a circle and a closed 4-point polyline must survive a
# real Export to .dwg and a real Open back, with exact control-point
# counts/rational/closed flags and exact combined curve length (not just an
# object count - see dwg_script.txt).
DWGBIN="$(dirname "$BIN")/dwg_fixture_gen"
sed "s|@TMP@|$TMP|g" "$HERE/dwg_script.txt" > "$TMP/dwg_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  DW="$("$BIN" --smoke 50 --script "$TMP/dwg_script.txt" 2>&1)" || { echo "$DW"; echo "FAIL: DWG round-trip script exited non-zero"; exit 1; }
else
  DW="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 50 --script "$TMP/dwg_script.txt" 2>&1)" || { echo "$DW"; echo "FAIL: DWG round-trip script exited non-zero"; exit 1; }
fi
dwcheck() { if echo "$DW" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
dwcheck "Exported $TMP/dwg_roundtrip.dwg" "DWG export wrote a file"
dwcheck "DWG: 3 curves, 0 points" "DWG import read the line, circle and closed polyline back"
[ "$(echo "$DW" | grep -c "degree 2, 9 control points, rational, closed")" = "2" ] && echo "ok   DWG CIRCLE round-tripped as an exact rational NURBS circle" || { echo "FAIL DWG CIRCLE did not survive round-trip"; fail=1; }
[ "$(echo "$DW" | grep -c "degree 1, 5 control points, non-rational, closed")" = "2" ] && echo "ok   DWG closed LWPOLYLINE round-tripped with the right point count and closed flag" || { echo "FAIL DWG closed polyline did not survive round-trip"; fail=1; }
[ "$(echo "$DW" | grep -c "CV\[0\] 0,0,0")" = "2" ] && [ "$(echo "$DW" | grep -c "CV\[1\] 12,0,0")" = "2" ] && echo "ok   DWG LINE kept its exact endpoints" || { echo "FAIL DWG LINE endpoints did not survive round-trip"; fail=1; }
[ "$(echo "$DW" | grep -c "Total length = ")" = "2" ] && [ "$(echo "$DW" | grep "Total length = " | sort -u | wc -l)" = "1" ] && echo "ok   DWG round-trip kept the exact combined curve length (line + circle + polyline)" || { echo "FAIL DWG round-trip changed the combined curve length"; fail=1; }
[ "$(head -c 6 "$TMP/dwg_roundtrip.dwg")" = "AC1015" ] && echo "ok   dwg_roundtrip.dwg is a real binary DWG (AC1015/AutoCAD 2000 header)" || { echo "FAIL dwg_roundtrip.dwg is not a real DWG file"; fail=1; }
# BLOCK_HEADER/INSERT (block instance): Dino 8 cannot itself write a real
# DWG INSERT (a block instance placed in-app is stored pre-flattened - see
# InstantiateBlock), so this fixture is built independently through
# LibreDWG's own API (dwg_fixture_gen, see tests/dwg_fixture_gen.c) and
# proves ImportDwg's INSERT-flattening code against a real block reference.
if [ -x "$DWGBIN" ]; then
  "$DWGBIN" "$TMP/dwg_insert_fixture.dwg" >/dev/null || { echo "FAIL: dwg_fixture_gen failed to write the INSERT fixture"; exit 1; }
  sed "s|@TMP@|$TMP|g" "$HERE/dwg_insert_script.txt" > "$TMP/dwg_insert_script.txt"
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
    DI="$("$BIN" --smoke 30 --script "$TMP/dwg_insert_script.txt" 2>&1)" || { echo "$DI"; echo "FAIL: DWG INSERT script exited non-zero"; exit 1; }
  else
    DI="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMP/dwg_insert_script.txt" 2>&1)" || { echo "$DI"; echo "FAIL: DWG INSERT script exited non-zero"; exit 1; }
  fi
  dicheck() { if echo "$DI" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
  dicheck "DWG: 3 curves, 0 points, 1 block instance flattened" "ImportDwg flattened the independently-built BLOCK_HEADER/INSERT fixture"
  dicheck "CV\[0\] 100,50,0" "DWG INSERT's flattened line starts at the scaled/rotated/translated insertion point"
  dicheck "CV\[1\] 100,56,0" "DWG INSERT's flattened line ends where a 2-unit block line scaled 3x and rotated 90 degrees should (100,50,0)-(100,56,0)"
else
  echo "FAIL dwg_fixture_gen was not built next to $BIN (DINO8_BUILD_TESTS off?) - skipping the INSERT fixture check"
  fail=1
fi
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
# Solids: Ellipsoid/SubDEllipsoid (real axis picking), Pyramid (NumSides=),
# Loft (Normal vs Style=Straight), Cap (multiple separate openings) (see solids_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SO="$("$BIN" --smoke 150 --script "$HERE/solids_script.txt" 2>&1)" || { echo "$SO"; echo "FAIL: solids script exited non-zero"; exit 1; }
else
  SO="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 150 --script "$HERE/solids_script.txt" 2>&1)" || { echo "$SO"; echo "FAIL: solids script exited non-zero"; exit 1; }
fi
socheck() { if echo "$SO" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
socheck "Volume = 132 cubic" "Ellipsoid volume is close to the analytic 4/3*pi*5*3*2 (~125.7)"
socheck "48 faces, 72 edges, 26 vertices, 0 creases" "SubDEllipsoid built a SubD from the same three axes"
socheck "7 vertices, 10 faces" "Pyramid NumSides=6 built a 7-vertex hexagonal-base mesh"
socheck "degree 3 x 2, CVs 24 x 3" "Loft (Normal) fit a cubic-through-control-points v-direction for 3 sections"
socheck "degree 3 x 1, CVs 24 x 3" "Loft Style=Straight dropped to a linear (ruled) v-direction"
socheck "4 faces, 12 edges, open" "DeleteFaces removed the box's top and bottom (two separate naked-edge loops)"
socheck "Capped 1 object(s), 2 opening(s)" "Cap closed both separate openings in one call"
socheck "6 faces, 76 edges, open" "Cap added both cap faces back (4 sides + 2 caps)"
socheck "smoke: frames=150 objects=12" "solids script produced the expected object count"
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
secheck "SrfSeam: seam moved to" "SrfSeam rotated a real closed surface's periodic seam"
secheck "SphereTangentToThreeSurfaces: sphere at" "SphereTangentToThreeSurfaces solved a real equidistant sphere"
secheck "SetSurfaceTangent: 2 boundary tangent-row control point" "SetSurfaceTangent rotated the tangent row to match the target's normal"
secheck "VariableOffsetSrf: offset 1 at the U-min edge to 3 at U-max" "VariableOffsetSrf built a real per-CV linearly-varying offset"
secheck "FitCurveToSurface: fitted through 13 points" "FitCurveToSurface pulled a coarse fit onto the surface"
secheck "PatchSingleFace: face 5 of object" "PatchSingleFace replaced a box face with a real Coons patch"
secheck "Volume = 1000 cubic" "PatchSingleFace's Coons patch kept the box's exact volume (still flat)"
secheck "Flow: base length 10 -> target length 99.35" "ApplyCrv aliased into Flow's real curve deformation"
secheck "ApplyMesh: 1 object(s) reshaped via nearest-triangle barycentric" "ApplyMesh reshaped a mesh sphere onto a same-topology target"
secheck "ApplyMeshUVN: 1 object(s) reshaped via nearest-triangle barycentric" "ApplyMeshUVN ran the same real mesh mapping"
secheck "Boss: protrusion of height 5 built following the base's local surface normal at 48 points along the curve; unioned with the base solid" "Boss built a real solid protrusion and unioned it with the box"
secheck "Volume = 1063 cubic" "Boss's union really added the r=2 h=5 cylinder's volume (1000 + pi*4*5 = 1062.8)"
secheck "Rib: tapered wall of height 2 built following the base's local surface normal at 64 points along the curve; unioned with the base solid" "Rib built a real tapered wall and unioned it with the box"
secheck "Volume = 1006 cubic" "Rib's union really added the tapered wall's volume"
secheck "FilletSrfToRail: fillet surface built along the rail's own points" "FilletSrfToRail built real rolling-ball arcs along a picked rail"
secheck "SoftEditSrf: 4 control point(s) moved with a cosine falloff within radius 8 (max displacement 3)" "SoftEditSrf moved control points with a real falloff"
secheck "^ok   expect_objects 95" "surface-edit script produced the expected object count"

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
sdcheck "InsertPoint: 1 point(s) inserted on edges" "InsertPoint split an edge on the new box"
sdcheck "Slide: vertex moved" "Slide moved the picked vertex along its best-aligned edge"
sdcheck "SubDSpinEdge: 1 edge(s) spun" "SubDSpinEdge spun the picked edge"
sdcheck "SubDExpandEdges: 2 strip face(s) added, width 1" "SubDExpandEdges added a strip on both sides of the picked edge"
sdcheck "gl_error=0" "subd script ran without OpenGL errors"
echo "$SD" | grep -E "^(ok|FAIL)"
if echo "$SD" | grep -q "^FAIL"; then fail=1; fi
sdcheck "smoke: frames=150 objects=7" "subd script produced the expected object count"
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
rncheck "RenderBlowup: .* region rendered as a true optical zoom" "RenderBlowup rendered the picked region as a real optical zoom (full viewport resolution, off-axis frustum), not a crop"
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
if echo "$A2" | grep -A1 "^history: Command: SelDim$" | grep -q "^history: 70 object(s) selected$"; then
  echo "ok   SelDim finds DimArea/DimCurveLength (its label list had gone stale and silently excluded them, plus DimVolume/DimOrdinate/DimCreaseAngle - see cmd_select2.cpp)"
else
  echo "FAIL SelDim finds DimArea/DimCurveLength"; fail=1
fi
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
a2check "BlockEdit: editing 'D'" "BlockEdit placed an editable copy of the block instance"
a2check "BlockEdit: block 'D' redefined with 1 object(s), 2 instance(s) updated" "BlockEdit's redefinition propagated to every instance of the block (real linked instancing)"
a2check "Copied 2 object(s)" "Mirror Copy=Yes made real mirror copies of both MB block instance objects (the definition's own object plus the Insert copy)"
if echo "$A2" | grep -A1 "^history: Command: SelMirroredBlocks$" | grep -q "^history: 2 object(s) selected$"; then
  echo "ok   SelMirroredBlocks found the mirror-copied block instances (was a permanently non-functional stub - see cmd_transform.cpp/MirrorCommand::Apply)"
else
  echo "FAIL SelMirroredBlocks found the mirror-copied block instances"; fail=1
fi
a2check "gl_error=0" "annotate2 script ran without OpenGL errors"
# Solid tools: RoundHole, CurveBoolean, Clash, Cage/CageEdit, Flow, ScaleByPlane (see solidtools_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  ST="$("$BIN" --smoke 220 --script "$HERE/solidtools_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: solid-tools script exited non-zero"; exit 1; }
else
  ST="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 220 --script "$HERE/solidtools_script.txt" 2>&1)" || { echo "$ST"; echo "FAIL: solid-tools script exited non-zero"; exit 1; }
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
stcheck "ArrayHole: 4 hole position(s), radius 2, 1 solid(s) cut" "ArrayHole cut a round-hole grid"
stcheck "ArrayHole: object [0-9]* replaced by a mesh solid with 4260 faces, volume 1.551e+04" "ArrayHole's 2x2 grid removed the expected volume"
stcheck "ArrayHole: 3 hole position(s), profile [0-9]*, 1 solid(s) cut" "ArrayHole used a profile curve instead of round holes"
stcheck "ArrayHole: object [0-9]* replaced by a mesh solid with 152 faces, volume 15520" "ArrayHole's profile row removed the expected 3 x 4x4x10 volume exactly"
stcheck "ArrayHolePolar: 4 hole position(s), radius 2, 1 solid(s) cut" "ArrayHolePolar cut 4 round holes on a circle"
stcheck "ArrayHolePolar: object [0-9]* replaced by a mesh solid with 5280 faces, volume 1.541e+04" "ArrayHolePolar removed the expected volume"
stcheck "MoveHole: object [0-9]* re-cut at the new placement" "MoveHole re-cut the RoundHole feature at its new placement"
stcheck "CopyHole: copied object [0-9]* to object [0-9]*" "CopyHole cut a second copy of the hole"
stcheck "RotateHole: object [0-9]* re-cut at the new placement" "RotateHole rotated the hole feature and re-cut it"
stcheck "MirrorHole: copied object [0-9]* to object [0-9]*" "MirrorHole mirrored the hole feature into a copy"
stcheck "CutVolume: 1 cut volume(s) as meshes, total volume 785.3" "CutVolume measured pi*5^2*10 = 785.4 of the box inside the circle's extrusion"
stcheck "Bounce: polyline with 1 bounce(s)" "Bounce traced a ray straight down off the box top and back up"
stcheck "CreateSolid: 1 surface(s) joined into a closed mesh solid" "CreateSolid welded a single closed Brep's own faces into a closed mesh solid"
stcheck "Splop: placed 1 copy(ies) at 1 point(s)" "Splop placed a copy at the picked surface point"
stcheck "Reflect: mirrored across the plane through 1005,0,0 and welded original . mirror image into one symmetric mesh" "Reflect mirrored and welded the mesh into one symmetric solid"
stcheck "Radiate: baked diffuse.specular vertex colours from 1 light(s)/sun onto 1 mesh(es)" "Radiate baked vertex colours from the Sun onto the mesh"
stcheck "RadiateFind: 0 enabled light source(s) selected (the Sun also lights Radiate" "RadiateFind reported the Sun as Radiate's only light source"
stcheck "OrientCrvToEdge: placed 1 copy(ies) at 1 point(s)" "OrientCrvToEdge picked the box edge directly and oriented a copy onto it"
echo "$ST" | grep -E "^(ok|FAIL)"
if echo "$ST" | grep -q "^FAIL"; then fail=1; fi
stcheck "smoke: frames=[12][0-9][0-9] objects=35" "solid-tools script produced the expected object count"

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
flcheck "FilletEdge: edge 10 of object .* replaced with an exact fillet (variable radius: 1 at t=0, 3 at t=1 (exact))" "FilletEdge Radii= built a genuine variable-radius fillet via the exact planar closed form, not the constant-radius approximation"
flcheck "Volume = 990.7 cubic" "a 10x10x10 box minus a variable r=1->3 edge fillet has volume 1000 - (1-pi/4)*10*(1+3+9)/3 = 990.70, matching the closed-form integral of a linear radius ramp"
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
flcheck "^ok   expect_objects 25" "fillet script produced the expected object count"

# Adversarial fillets: tiny/at-the-limit/too-large radii relative to the
# shortest adjacent edge, a huge-coordinate-scale box (a genuine kernel
# limitation - see adversarial_corpus_notes.md), and a shallow-bend FilletSrf
# (see fillet_adversarial_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FA="$("$BIN" --smoke 200 --script "$HERE/fillet_adversarial_script.txt" 2>&1)" || { echo "$FA"; echo "FAIL: fillet-adversarial script exited non-zero"; exit 1; }
else
  FA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/fillet_adversarial_script.txt" 2>&1)" || { echo "$FA"; echo "FAIL: fillet-adversarial script exited non-zero"; exit 1; }
fi
facheck() { if echo "$FA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
facheck "FilletEdge: edge 10 of object 1 replaced with an exact fillet (radius 0.02)" "a radius far smaller than the shortest adjacent edge still built a real, exact fillet"
facheck "FilletEdge: edge 10 of object 2 replaced with an exact fillet (radius 1)" "a radius at exactly half the shortest adjacent edge's length still built a valid fillet"
facheck "7 faces, 15 edges, closed solid" "the at-the-limit fillet is a genuine closed solid, not degenerate"
facheck "! FilletEdge: the offset surfaces do not meet" "a radius more than double what the geometry supports failed with a clear diagnostic, not a hang or garbage surface"
facheck "! FilletEdge: could not build a watertight result at this object's coordinate scale" "a huge-coordinate-scale box's otherwise-ordinary fillet failed gracefully instead of silently returning a broken 'closed solid' (documented kernel limitation)"
facheck "! FilletSrf: the offset surfaces do not meet" "FilletSrf on two nearly-flat planes failed with its own clear diagnostic instead of a garbage surface"
echo "$FA" | grep -E "^(ok|FAIL)"
if echo "$FA" | grep -q "^FAIL"; then fail=1; fi
facheck "^ok   expect_objects 0" "fillet-adversarial script cleaned up to zero objects at the end"

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
  VT="$("$BIN" --smoke 190 --script "$TMP/viewtools_script.txt" 2>&1)" || { echo "$VT"; echo "FAIL: view-tools script exited non-zero"; exit 1; }
else
  VT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 190 --script "$TMP/viewtools_script.txt" 2>&1)" || { echo "$VT"; echo "FAIL: view-tools script exited non-zero"; exit 1; }
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
vtcheck "ClippingDrawings: 2 drawing curve(s) on layer 'Clipping Drawings' from 2 plane(s)" "ClippingDrawings drew section curves onto a dedicated layer"
vtcheck "UpdateClippingDrawings: 2 drawing curve(s) on layer 'Clipping Drawings' from 2 plane(s)" "UpdateClippingDrawings regenerated them"
vtcheck "EditClippingDrawings: 2 drawing curve(s) selected for editing" "EditClippingDrawings selected the drawing curves"
vtcheck "ExportClippingDrawings: wrote 2 drawing curve(s) to $TMP/vt/drawings.3dm" "ExportClippingDrawings wrote them to a file"
vtcheck "ClippingSections: 2 curve(s) from 2 plane(s)" "ClippingSections handled two planes"
vtcheck "ExtractClippingSlices: 2 planar slice surface(s) from 2 plane(s)" "ExtractClippingSlices built trimmed planar faces, not just outline curves"
vtcheck "1 faces, 1 edges, open" "a clipping slice is a single trimmed planar face"
vtcheck "MPlane: CPlane of Perspective follows object [0-9]*, tracking its orientation (origin 58.54,1.464,2.5 x 0.7071,0.7071,0" "MPlane tracked a rotated box's own plane, not just its bounding-box centre"
vtcheck "MPlane: detached" "MPlane with nothing selected detaches (min=0 so Enter isn't needed)"
vtcheck "CPlane Perspective: World (origin 0,0,0 x 1,0,0 y 0,1,0)" "CPlane World reset after detaching MPlane"
vtcheck "OrientCameraToSrf: camera looks along the normal of object [0-9]* at 80,10,0 (picked point)" "OrientCameraToSrf used the picked point, not just the surface centre"
vtcheck "Camera (Perspective): location 80,10,-96.05, target 80,10,0" "OrientCameraToSrf's camera looks straight down at the picked point"
vtcheck "Zoom1To1Calibrate: 3.78 pixels per mm (96 dpi)" "Zoom1To1Calibrate read a dpi value from the command line"
vtcheck "Zoom1To1: Perspective shows" "Zoom1To1 read back the calibrated pixels-per-mm"
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
vtcheck "SetOneDaySunAnimation: 4 frame(s); PlayAnimation/RecordAnimation will sweep the Sun (Altitude -5 to 60)" "SetOneDaySunAnimation built a sun-only animation"
vtcheck "ViewFirstFrame: frame 1 of 4" "ViewFirstFrame stepped into the one-day sun animation"
vtcheck "Azimuth=70 Altitude=-5" "the one-day animation's first frame is sunrise (low altitude, easterly azimuth)"
vtcheck "ViewLastFrame: frame 4 of 4" "ViewLastFrame stepped to the animation's last frame"
vtcheck "Azimuth=290 Altitude=-5" "the one-day animation's last frame is sunset (low altitude again, westerly azimuth)"
vtcheck "SetSeasonalSunAnimation: 4 frame(s); PlayAnimation/RecordAnimation will sweep the Sun (Altitude -10 to 65)" "SetSeasonalSunAnimation built a sun-only animation"
vtcheck "Azimuth=180 Altitude=-10" "the seasonal animation's first and last frames are both winter (fixed solar-noon azimuth, lowest altitude)"
vtcheck "SplitViewportHorizontal: added Perspective 2 (5 viewports)" "SplitViewportHorizontal added a viewport"
vtcheck "CloseViewport: closed Perspective 2 (4 left)" "CloseViewport removed it"
vtcheck "Layouts: 2 layout(s); active: Model" "layouts survived the .3dm round-trip"
vtcheck "NamedCPlane: 3 named CPlane(s)" "named CPlanes survived the .3dm round-trip"
vtcheck "SelClippingPlane: 2 clipping plane(s) selected" "clipping planes survived the .3dm round-trip"
vtcheck "^ok   expect_objects 5" "view-tools script ended with the sphere, 2 clipping slices, the MPlane box and the Plane surface"
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

# Python scripting: RunPythonScript through the embedded `dino8` module
# (see src/script/PythonEngine.cpp), when this build was compiled with a
# Python 3 development install (DINO8_HAVE_PYTHON - see CMakeLists.txt).
# Builds a box and a sphere, sets Name/Color on the box (and reads its
# Layer), selects it, deletes the sphere, and exercises dino8.RunCommand
# and Point3d/Vector3d arithmetic. Unlike script_script.txt's Lua test
# there is no trailing script token: PythonEngine runs a script
# start-to-finish with no GetPoint-style mid-script suspend (see
# python_script.txt's own header).
#
# dino8.RunCommand only *queues* a nested command line while called from
# inside another command's callback (CommandEngine::RunNested -> Execute,
# see cmd_misc.cpp/CommandEngine.cpp: callback_depth_ > 0 defers it to
# deferred_, run once the calling command finishes) - the same as Lua's
# rs.Command would if it were called from inside a running rs.* script.
# So NewLayer's effect is not visible to the rest of *this* script; it
# runs right after RunPythonScript's own command finishes instead, which
# is why the layer check below reads the pre-existing "Default" layer and
# the RunCommand check greps the run's full output rather than "history:
# layer: Parts" inline.
cat > "$TMP/t.py" <<'PY'
import dino8

box_id = dino8.doc.Objects.AddBox(dino8.Point3d(0, 0, 0), dino8.Vector3d(10, 10, 10))
sphere_id = dino8.doc.Objects.AddSphere(dino8.Point3d(5, 5, 5), 2)

obj = dino8.doc.Objects.Find(box_id)
obj.Name = "Widget"
obj.Color = (255, 0, 0)
print("name: " + obj.Name)
print("layer: " + obj.Layer)
print("color: %d,%d,%d" % obj.Color)
print("kind: " + obj.ObjectType)

obj.Select()
print("selected count: %d" % len(dino8.doc.Objects.GetSelectedObjects()))
print("object count before delete: %d" % len(dino8.doc.Objects.AllObjects()))

deleted = dino8.doc.Objects.Delete(sphere_id)
print("deleted sphere: " + str(deleted))
print("object count after delete: %d" % len(dino8.doc.Objects.AllObjects()))

v = dino8.Vector3d(3, 4, 0)
print("vector length: %.2f" % v.Length)
p1 = dino8.Point3d(1, 2, 3)
p2 = p1 + v
print("point plus vector: %.0f,%.0f,%.0f" % (p2.X, p2.Y, p2.Z))

by_name = [o for o in dino8.doc.Objects.AllObjects() if o.Name == "Widget"]
print("found by name: %d" % len(by_name))

dino8.RunCommand("NewLayer", "Parts")
PY
sed "s|@TMP@|$TMP|g" "$HERE/python_script.txt" > "$TMP/python_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  PS="$("$BIN" --smoke 100 --script "$TMP/python_script.txt" 2>&1)" || { echo "$PS"; echo "FAIL: python script exited non-zero"; exit 1; }
else
  PS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/python_script.txt" 2>&1)" || { echo "$PS"; echo "FAIL: python script exited non-zero"; exit 1; }
fi
if echo "$PS" | grep -q "no Python 3 development install"; then
  echo "skip Python scripting not available in this build (compiled without Python3 Development.Embed - see CMakeLists.txt)"
else
  echo "$PS" | grep -E "^(ok|FAIL)"
  if echo "$PS" | grep -q "^FAIL"; then fail=1; fi
  pscheck() { if echo "$PS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
  pscheck "history: name: Widget" "RunPythonScript's Dino8Object.Name getter/setter round-tripped"
  pscheck "history: layer: Default" "Dino8Object.Layer read back the box's layer"
  pscheck "Layer 'Parts' created and made current" "dino8.RunCommand(\"NewLayer\", \"Parts\") ran the real NewLayer command (deferred until RunPythonScript's own command finished, same as rs.Command would be)"
  pscheck "history: color: 255,0,0" "Dino8Object.Color setter/getter round-tripped"
  pscheck "history: kind: polysurface" "Dino8Object.ObjectType reported the box as a polysurface"
  pscheck "history: selected count: 1" "Dino8Object.Select() selected the box"
  pscheck "history: object count before delete: 2" "AllObjects saw both the box and the sphere"
  pscheck "history: deleted sphere: True" "dino8.doc.Objects.Delete removed the sphere"
  pscheck "history: object count after delete: 1" "AllObjects reflects the deletion"
  pscheck "history: vector length: 5.00" "Vector3d.Length computed a 3-4-5 triangle"
  pscheck "history: point plus vector: 4,6,3" "Point3d.__add__(Vector3d) matched RhinoCommon's operator+"
  pscheck "history: found by name: 1" "list comprehension over AllObjects() found the renamed box"
  pscheck "^ok   expect_objects 1" "RunPythonScript left exactly the box (the sphere was deleted from inside the script)"
  grep -q "! Python error" <<<"$PS" && { echo "FAIL python_script.txt printed a Python error"; fail=1; } || echo "ok   no Python script errors"
fi

# Dino Flow + plug-ins: node editor, the HelloDino sample plug-in (command +
# Dino Flow node), and GrasshopperPlayer headless solve/bake (see flow_script.txt).
# The .dflow is copied to $TMP first so the second run below can edit that
# copy in place (the source tree's copy stays untouched).
cp "$HERE/flow_graph.dflow" "$TMP/flow_graph.dflow"
sed -e "s|@FLOWFILE@|$TMP/flow_graph.dflow|g" -e "s|@FLOWSAVE@|$TMP/flow_saved.3dm|g" "$HERE/flow_script.txt" > "$TMP/flow_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FL="$("$BIN" --smoke 100 --script "$TMP/flow_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: flow script exited non-zero"; exit 1; }
else
  FL="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/flow_script.txt" 2>&1)" || { echo "$FL"; echo "FAIL: flow script exited non-zero"; exit 1; }
fi
flcheck() { if echo "$FL" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
flcheck "Dino Flow: opened the node editor" "Grasshopper opened the Dino Flow panel"
flcheck "GrasshopperPluginList: 4 plug-in(s) found" "the plug-in loader found all four sample plug-ins"
flcheck "HelloDino 1.0.0 - 1 command(s), 1 node(s)" "HelloDino loaded its command and Dino Flow node"
flcheck "MeshTools 1.0.0 - 1 command(s), 3 node(s)" "MeshTools loaded its command and 3 Dino Flow nodes (Terrain Height, Terrain Mesh, Plugin Mesh Info)"
flcheck "CurveTools 1.0.0 - 1 command(s), 3 node(s)" "CurveTools loaded its command and 3 Dino Flow nodes (Helix Point, Spiral Curve, Plugin Curve Length)"
flcheck "AnalysisTools 1.0.0 - 1 command(s), 1 node(s)" "AnalysisTools loaded its command and Dino Flow node"
flcheck "HelloDino: hello from the sample plug-in!" "the HelloDino command ran"
flcheck "GrasshopperPlayer: solved 4 node(s)" "GrasshopperPlayer solved the sample graph"
flcheck "gl_error=0" "flow script ran without OpenGL errors"
echo "$FL" | grep -E "^(ok|FAIL)"
if echo "$FL" | grep -q "^FAIL"; then fail=1; fi
flcheck "^ok   expect_objects 2" "GrasshopperPlayer baked the Line into the document"
flcheck "Total length = 30.41 " "the first bake's line has the slider=30 length"

# Associativity, across a real close/reopen: edit the same .dflow file on
# disk (slider 30 -> 55), then a *separate* process reopens the saved
# document and runs GrasshopperUpdateBakes against that same path - it
# should replace the previously-baked FlowLine object in place (same object
# count, new length) rather than add a second one next to it (see
# flow_update_script.txt).
sed -i 's/"slider_value":30/"slider_value":55/' "$TMP/flow_graph.dflow"
sed -e "s|@FLOWFILE@|$TMP/flow_graph.dflow|g" -e "s|@FLOWSAVE@|$TMP/flow_saved.3dm|g" "$HERE/flow_update_script.txt" > "$TMP/flow_update_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FL2="$("$BIN" --smoke 100 --script "$TMP/flow_update_script.txt" 2>&1)" || { echo "$FL2"; echo "FAIL: flow update script exited non-zero"; exit 1; }
else
  FL2="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/flow_update_script.txt" 2>&1)" || { echo "$FL2"; echo "FAIL: flow update script exited non-zero"; exit 1; }
fi
fl2check() { if echo "$FL2" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
echo "$FL2" | grep -E "^(ok|FAIL)"
if echo "$FL2" | grep -q "^FAIL"; then fail=1; fi
fl2check "gl_error=0" "flow update script ran without OpenGL errors"
FL2_EXPECT2_COUNT=$(echo "$FL2" | grep -c "^ok   expect_objects 2 (got 2)")
if [ "$FL2_EXPECT2_COUNT" = "2" ]; then echo "ok   GrasshopperUpdateBakes kept the object count at 2 both before and after the re-bake (replaced in place, no duplicate)"; else echo "FAIL GrasshopperUpdateBakes kept the object count at 2 both before and after the re-bake (replaced in place, no duplicate)"; fail=1; fi
fl2check "Total length = 30.41 " "the reopened document still has the original slider=30 line"
fl2check "GrasshopperUpdateBakes: replaced 1 object(s) from a previous bake of .* with 1 freshly-solved object(s)" "GrasshopperUpdateBakes found and replaced (not duplicated) the previously-baked line"
fl2check "Total length = 55.23 " "the re-baked line picked up the slider=55 edit made to the .dflow file on disk - associative, not frozen"

# Dino Flow data trees: Range -> Graft Tree -> Flatten Tree -> List Item ->
# Construct Point -> Bake (see flow_tree_script.txt / flow_tree_graph.dflow).
# AttachGHSData/GetUserText surface each node's Tree::Summary() so the
# branch structure Graft/Flatten produce is directly checkable as text.
sed "s|@TREEFILE@|$HERE/flow_tree_graph.dflow|g" "$HERE/flow_tree_script.txt" > "$TMP/flow_tree_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FT="$("$BIN" --smoke 100 --script "$TMP/flow_tree_script.txt" 2>&1)" || { echo "$FT"; echo "FAIL: flow tree script exited non-zero"; exit 1; }
else
  FT="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/flow_tree_script.txt" 2>&1)" || { echo "$FT"; echo "FAIL: flow tree script exited non-zero"; exit 1; }
fi
ftcheck() { if echo "$FT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
ftcheck "^ok   expect_objects 1" "the tree graph baked exactly one point"
ftcheck "Range=5 items" "Range produced a 5-item, single-branch list"
ftcheck "Graft Tree=5 items in 5 branches" "Graft Tree wrapped each item in its own branch"
ftcheck "Flatten Tree=5 items;" "Flatten Tree collapsed the 5 grafted branches back into one"
ftcheck "List Item=1 item" "List Item picked a single value out of the flattened list"
ftcheck "  4,0,0" "List Item(index 2) of Range(0,10,5) read back as 4 via the baked point's coordinates"

# Dino Flow evolutionary solver: Gene Pool -> (x-3)^2 fitness -> Evolutionary
# Solver, a known-optimum problem (minimum 0 at x=3) checked two ways: the
# GrasshopperPlayer summary line, and the baked (best-x, best-fitness) point
# (see flow_solver_script.txt / flow_solver_graph.dflow).
sed "s|@SOLVERFILE@|$HERE/flow_solver_graph.dflow|g" "$HERE/flow_solver_script.txt" > "$TMP/flow_solver_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FS="$("$BIN" --smoke 100 --script "$TMP/flow_solver_script.txt" 2>&1)" || { echo "$FS"; echo "FAIL: flow solver script exited non-zero"; exit 1; }
else
  FS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/flow_solver_script.txt" 2>&1)" || { echo "$FS"; echo "FAIL: flow solver script exited non-zero"; exit 1; }
fi
fscheck() { if echo "$FS" | grep -qE "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
fscheck "^ok   expect_objects 1" "the solver graph baked exactly one point"
fscheck "Evolutionary Solver \(#4\) best fitness 0 after 60 generation\(s\)" "the solver ran all 60 generations and converged to fitness 0 for (x-3)^2 with this fixed seed"
fscheck "  3,0,0" "the baked point (best gene, best fitness) is exactly (3, 0, 0) - the true optimum of (x-3)^2"

# Dino Flow plug-in geometry values: Spiral Curve (a plug-in node output of
# kind CURVE) wired directly into Plugin Curve Length (a plug-in node INPUT
# of kind CURVE) - proving the plugin ABI's opaque geometry handles round-
# trip plugin-to-plugin, not just plugin-to-document (see
# flow_plugin_geom_script.txt / flow_plugin_geom_graph.dflow).
sed "s|@GEOMFILE@|$HERE/flow_plugin_geom_graph.dflow|g" "$HERE/flow_plugin_geom_script.txt" > "$TMP/flow_plugin_geom_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FG="$("$BIN" --smoke 100 --script "$TMP/flow_plugin_geom_script.txt" 2>&1)" || { echo "$FG"; echo "FAIL: flow plugin geom script exited non-zero"; exit 1; }
else
  FG="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$TMP/flow_plugin_geom_script.txt" 2>&1)" || { echo "$FG"; echo "FAIL: flow plugin geom script exited non-zero"; exit 1; }
fi
fgcheck() { if echo "$FG" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
fgcheck "^ok   expect_objects 1" "the plugin geometry graph baked exactly one point"
fgcheck "GrasshopperPlayer: solved 4 node(s)" "GrasshopperPlayer solved Spiral Curve -> Plugin Curve Length -> Construct Point -> Bake"
fgcheck "  10,0,0" "Plugin Curve Length correctly computed 10 for a radius=0 turns=1 pitch=10 helix (a straight segment), round-tripped through two plug-in nodes and baked as the point's X"
fgcheck "gl_error=0" "flow plugin geometry script ran without OpenGL errors"

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

# Booleans: BooleanUnion/BooleanIntersection, Boolean2Objects (Result
# cycling), BooleanSplit/MeshSplit/MeshBooleanSplit, WireCut, MeshSmooth
# (see boolean_script.txt).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  BO="$("$BIN" --smoke 200 --script "$HERE/boolean_script.txt" 2>&1)" || { echo "$BO"; echo "FAIL: boolean script exited non-zero"; exit 1; }
else
  BO="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$HERE/boolean_script.txt" 2>&1)" || { echo "$BO"; echo "FAIL: boolean script exited non-zero"; exit 1; }
fi
echo "$BO" | grep -E "^(ok|FAIL)"
if echo "$BO" | grep -q "^FAIL"; then fail=1; fi
echo "$BO" | grep -q "^smoke:" || { echo "$BO"; echo "FAIL: boolean script produced no smoke line"; fail=1; }

# Adversarial booleans: near-tangent/barely-overlapping/coincident solids,
# an extreme-aspect-ratio sliver, a huge-coordinate-scale pair, a 10-deep
# chained-difference feature, and non-manifold input (see
# boolean_adversarial_script.txt and adversarial_corpus_notes.md).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  BA="$("$BIN" --smoke 300 --script "$HERE/boolean_adversarial_script.txt" 2>&1)" || { echo "$BA"; echo "FAIL: boolean-adversarial script exited non-zero"; exit 1; }
else
  BA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 300 --script "$HERE/boolean_adversarial_script.txt" 2>&1)" || { echo "$BA"; echo "FAIL: boolean-adversarial script exited non-zero"; exit 1; }
fi
bacheck() { if echo "$BA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
bacheck "BooleanUnion: 44 faces, volume 2000" "near-tangent boxes (1e-6 overlap) unioned into one solid at the correct volume"
bacheck "BooleanIntersection: 16 faces, volume 0.01" "barely-overlapping boxes (1e-4 overlap) still intersected into a real, non-empty sliver"
bacheck "BooleanUnion: 16 faces, volume 1000" "coincident duplicate boxes unioned without collapsing or crashing"
bacheck "BooleanIntersection: 24 faces, volume 1000" "coincident duplicate boxes intersected back to the exact original volume"
bacheck "BooleanDifference: 35992 faces, volume 750" "a 1000x1000x0.001 sliver survived a corner-clipping difference at its exact expected volume (1000 - 250)"
bacheck "BooleanUnion: 40 faces, volume 1.4e+04" "two boxes at 1e6-unit coordinates still unioned to the exact expected volume (8000+8000-2000), no precision collapse"
bacheck "BooleanDifference: 908 faces, volume 8800" "10 chained BooleanDifference cuts on one solid stayed valid through the final cut, ending at the exact expected volume (10000 - 10x120)"
if echo "$BA" | grep -q "! No object with id"; then echo "FAIL boolean-adversarial script's own SelID bookkeeping was wrong (references a missing id)"; fail=1; else echo "ok   boolean-adversarial script's SelID bookkeeping matched every object the app actually created"; fi
echo "$BA" | grep -E "^(ok|FAIL)"
if echo "$BA" | grep -q "^FAIL"; then fail=1; fi
bacheck "^ok   expect_objects 0" "boolean-adversarial script cleaned up to zero objects at the end"

# Adversarial curve self-intersection: a closed bowtie polyline fed into
# PlanarSrf (must be rejected) and a solid-capping Extrude (must degrade to
# an open surface), an open self-crossing polyline confirming IntersectSelf
# still finds real crossings after the shared CurveSelfIntersects refactor,
# and a simple closed curve confirming no false positive on ordinary
# geometry (see curve_adversarial_script.txt and adversarial_corpus_notes.md).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  CA="$("$BIN" --smoke 300 --script "$HERE/curve_adversarial_script.txt" 2>&1)" || { echo "$CA"; echo "FAIL: curve-adversarial script exited non-zero"; exit 1; }
else
  CA="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 300 --script "$HERE/curve_adversarial_script.txt" 2>&1)" || { echo "$CA"; echo "FAIL: curve-adversarial script exited non-zero"; exit 1; }
fi
cacheck() { if echo "$CA" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
cacheck "! PlanarSrf: 1 curve(s) cross themselves" "PlanarSrf rejected a closed bowtie polyline instead of silently building a self-crossing trim loop"
cacheck "! Extrude: the selected curve crosses itself - building an open surface instead of a solid cap" "Extrude Solid=Yes degraded a bowtie curve to an open surface instead of claiming a solid it cannot honestly build"
cacheck "  intersection at 5,5,0" "IntersectSelf still finds a real self-crossing after the shared CurveSelfIntersects refactor"
echo "$CA" | grep -E "^(ok|FAIL)"
if echo "$CA" | grep -q "^FAIL"; then fail=1; fi
cacheck "Created 1 planar surface(s)" "a simple (non-self-intersecting) closed curve still built a normal planar surface - no false positive from the self-intersection check"
CA_CROSS_COUNT=$(echo "$CA" | grep -c "cross themselves\|crosses itself")
if [ "$CA_CROSS_COUNT" = "2" ]; then echo "ok   exactly the two bowtie cases were flagged as self-intersecting, nothing else"; else echo "FAIL exactly the two bowtie cases were flagged as self-intersecting, nothing else (got $CA_CROSS_COUNT)"; fail=1; fi
cacheck "gl_error=0" "curve-adversarial script ran without OpenGL errors"

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
s2check "Named position 'Pos1' saved (1 object" "NamedPosition Save captured the box"
s2check "Bounding box: (160, 0, 0) to (170, 10, 10)" "the box actually moved before NamedPosition Restore"
s2check "Named position 'Pos1' restored: 1 of 1 object" "NamedPosition Restore reported success"
s2check "Bounding box: (60, 0, 0) to (70, 10, 10)" "NamedPosition Restore put the box back exactly"
s2check "Named position 'Pos1' deleted" "NamedPosition Delete"
s2check "[0-9]* snapshot(s)" "Snapshots (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "HistoryPurge: no construction history is recorded; nothing to purge" "HistoryPurge"
s2check "HistoryUpdate: no construction history is recorded; nothing to update" "HistoryUpdate"
s2check "[0-9]* attached reference model" "Worksession (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "LimitReferenceModel: 0 object(s) removed from 'nonexistent.3dm'" "LimitReferenceModel (cmd_session.cpp's real implementation, no longer shadowed)"
s2check "ContentFilter: 'Wood' (Materials and Textures panels; ContentFilter Clear to remove)" "ContentFilter set a name filter"
s2check "ContentFilter: off (showing every entry)" "ContentFilter Clear removed it"
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
flcheck "Opened $TMP/file/file1.3dm (3 objects)" "Open re-read file1.3dm"
flcheck "Saved $TMP/file/file2.3dm" "SaveAs wrote file2.3dm"
flcheck "Opened $TMP/file/file2.3dm (3 objects)" "Open re-read file2.3dm"
flcheck "Imported $TMP/file/file1.3dm" "Import brought file1.3dm's objects in"
# Real geometric-fidelity proof, not just object counts: List (run once
# right before the first Save, once right after the matching Open) prints
# each object's exact fingerprint - a degree/CV count for curves, face/edge
# count for breps, face/edge/vertex/crease count for SubDs. Asserting each
# line appears *exactly* twice (grep -c over the whole captured script
# output, since grep -q would already be satisfied by the pre-save
# occurrence alone) proves the .3dm round trip didn't silently drop or
# corrupt control points, faces, or SubD topology - Box (brep), Sphere
# (brep - confirmed via the real List output, not assumed), and SubDBox
# (the first SubD object any .3dm round-trip test in this suite covers).
FL_BOX_COUNT=$(echo "$FL" | grep -c "^history:   6 faces, 12 edges, closed solid$")
[ "$FL_BOX_COUNT" = "2" ] && echo "ok   Box's exact 6 faces/12 edges fingerprint survived the .3dm round trip (List ran twice, both matched)" || { echo "FAIL Box's exact fingerprint did not appear exactly twice (got $FL_BOX_COUNT) - the .3dm round trip silently changed the brep"; fail=1; }
FL_SPHERE_COUNT=$(echo "$FL" | grep -c "^history:   1 faces, 1 edges, closed solid$")
[ "$FL_SPHERE_COUNT" = "2" ] && echo "ok   Sphere's exact 1 face/1 edge fingerprint survived the .3dm round trip" || { echo "FAIL Sphere's exact fingerprint did not appear exactly twice (got $FL_SPHERE_COUNT) - the .3dm round trip silently changed the brep"; fail=1; }
FL_SUBD_COUNT=$(echo "$FL" | grep -c "^history:   6 faces, 12 edges, 8 vertices, 0 creases$")
[ "$FL_SUBD_COUNT" = "2" ] && echo "ok   SubDBox's exact 6 faces/12 edges/8 vertices/0 creases fingerprint survived the .3dm round trip" || { echo "FAIL SubDBox's exact fingerprint did not appear exactly twice (got $FL_SUBD_COUNT) - the .3dm round trip silently changed the SubD topology"; fail=1; }
flcheck "Exported $TMP/file/export1.obj" "Export wrote export1.obj"
test -s "$TMP/file/file1.3dm" && echo "ok   file1.3dm exists" || { echo "FAIL file1.3dm missing"; fail=1; }
test -s "$TMP/file/file2.3dm" && echo "ok   file2.3dm exists" || { echo "FAIL file2.3dm missing"; fail=1; }
test -s "$TMP/file/file2_1.3dm" && echo "ok   IncrementalSave wrote file2_1.3dm" || { echo "FAIL file2_1.3dm missing"; fail=1; }
test -s "$TMP/file/file2_1_2.3dm" && echo "ok   IncrementalSave wrote file2_1_2.3dm" || { echo "FAIL file2_1_2.3dm missing"; fail=1; }
test -s "$TMP/file/export1.obj" && echo "ok   export1.obj exists" || { echo "FAIL export1.obj missing"; fail=1; }
flcheck "Exported $TMP/file/exportorigin.obj (origin at 5,5,0)" "ExportWithOrigin re-based to the picked point"
test -s "$TMP/file/exportorigin.obj" && echo "ok   exportorigin.obj exists" || { echo "FAIL exportorigin.obj missing"; fail=1; }
grep -q "^v -5 -5 0$" "$TMP/file/exportorigin.obj" && echo "ok   ExportWithOrigin translated the box corner to -5,-5,0" || { echo "FAIL ExportWithOrigin did not re-base the geometry"; fail=1; }

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
d2check "WeldSymbol: Groove (Below)" "WeldSymbol drew the groove glyph"
d2check "WeldSymbol: Spot (Above)" "WeldSymbol drew the spot glyph"
d2check "MultiLeader: 2 arrow(s), \"Note\"" "MultiLeader built two arrows to one landing"
D2_DIMTOL_COUNT=$(echo "$D2" | grep -c "DimTolerance: 1 dimension(s) updated")
if [ "$D2_DIMTOL_COUNT" = "2" ]; then echo "ok   DimTolerance ran twice, each updating the dimension"; else echo "FAIL DimTolerance ran twice, each updating the dimension"; fail=1; fi
d2check "Text = .*0\.03" "the rebuilt dimension text carries the second (0.03) tolerance"
if echo "$D2" | grep -q "Text = .*0\.02.*0\.03\|Text = .*0\.03.*0\.02.*0\.02"; then echo "FAIL DimTolerance compounded the suffix on the second run"; fail=1; else echo "ok   DimTolerance did not compound the suffix on the second run"; fi
d2check "BillOfMaterials: " "BillOfMaterials built a table over the scene objects"
d2check "SectionView: " "SectionView sliced the box"
d2check "UpdateSectionViews: 1 section view(s) regenerated" "UpdateSectionViews rebuilt the section from its stored plane"

# Associativity: UpdateBillOfMaterials re-derives a row's material from the
# object's *current* material after RenderAssignMaterialToObjects changes it
# (the row was built while the object had no material at all).
D2_UBOM_COUNT=$(echo "$D2" | grep -c "UpdateBillOfMaterials: 2 table(s) regenerated")
if [ "$D2_UBOM_COUNT" = "2" ]; then echo "ok   UpdateBillOfMaterials regenerated both tables, twice"; else echo "FAIL UpdateBillOfMaterials regenerated both tables, twice"; fail=1; fi
d2check "(none) qty=1 material=(none)" "the By=Material row was built while BomBall had no material at all"
d2check "NewMaterial qty=1 material=NewMaterial" "UpdateBillOfMaterials picked up BomBall's newly-assigned material, not the empty one baked at creation time"

# Associativity: UpdateDimensions re-measures a DimLinear anchored to a real
# Line object's endpoints after Scale1D stretches it from 20 to 40 units.
d2check "Total length = 20 " "the line measured 20 units before the stretch"
d2check "Total length = 40 " "Scale1D stretched the line to 40 units"
d2check "UpdateDimensions:   DimLinear now measures 40" "UpdateDimensions redrew the dimension text from the stretched line's new length, not the 20 baked at creation time"
d2check "DimRadius 5 (associative to selected arc/circle)" "DimRadius recorded the selected circle as its associative reference"
d2check "UpdateDimensions:   DimRadius now measures 10" "UpdateDimensions redrew DimRadius from the circle's doubled radius, not the 5 baked at creation time"
d2check "DimAngle 90 deg (associative to 3 point(s))" "DimAngle anchored all three points (vertex + two direction points) to real Point objects"
d2check "UpdateDimensions:   DimAngle now measures 45 deg" "UpdateDimensions redrew DimAngle from a moved direction point's new position, not the 90 deg baked at creation time"

# Associativity survives a .3dm round trip: DimRefObj1/2/3 and group_id (see
# cmd_annotate.cpp/File3dm.cpp) must still resolve after Save/New/Open, so
# the post-Open UpdateDimensions re-run above finds and redraws the exact
# same 4 associative dimensions (the tolerance-anchor DimLinear at 20, the
# stretched DimLinear at 40, DimRadius at 10, DimAngle at 45 deg) as the
# last pre-save run did - counting occurrences (not grep -q) so a broken
# round trip that drops back to "no associative dimensions" or only
# partially resolves them is actually caught, not masked by the pre-save
# occurrences already having satisfied a plain substring match.
D2_LEN20_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimLinear now measures 20")
[ "$D2_LEN20_COUNT" = "4" ] && echo "ok   the tolerance-anchor DimLinear (=20) round-tripped and was redrawn on every UpdateDimensions call, including after Open" || { echo "FAIL DimLinear=20 redrawn $D2_LEN20_COUNT times, expected 4 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_LEN40_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimLinear now measures 40")
[ "$D2_LEN40_COUNT" = "4" ] && echo "ok   the stretched DimLinear (=40) round-tripped and was redrawn on every UpdateDimensions call, including after Open" || { echo "FAIL DimLinear=40 redrawn $D2_LEN40_COUNT times, expected 4 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_RAD10_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimRadius now measures 10")
[ "$D2_RAD10_COUNT" = "3" ] && echo "ok   DimRadius round-tripped and was redrawn after Open (3 calls: the two pre-save runs once it existed, plus the post-Open run)" || { echo "FAIL DimRadius redrawn $D2_RAD10_COUNT times, expected 3 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_ANG45_COUNT=$(echo "$D2" | grep -c "UpdateDimensions:   DimAngle now measures 45 deg")
[ "$D2_ANG45_COUNT" = "2" ] && echo "ok   DimAngle round-tripped and was redrawn after Open (the one pre-save run once it existed, plus the post-Open run)" || { echo "FAIL DimAngle redrawn $D2_ANG45_COUNT times, expected 2 (associativity did not survive the .3dm round trip)"; fail=1; }
D2_REGEN4_COUNT=$(echo "$D2" | grep -c "UpdateDimensions: 4 dimension(s) regenerated")
[ "$D2_REGEN4_COUNT" = "2" ] && echo "ok   UpdateDimensions regenerated all 4 associative dimensions with 0 skipped, both before Save and again after Open" || { echo "FAIL UpdateDimensions: 4 dimension(s) regenerated seen $D2_REGEN4_COUNT times, expected 2 (some dimensions failed to resolve after the .3dm round trip)"; fail=1; }

d2check "gl_error=0" "drafting2 script ran without OpenGL errors"

# CPU path tracer: material library presets, RenderAssignMaterialToObjects
# Preset=, the RayTracedViewport display mode, Render/RenderArctic/
# RenderPreview with Quality=Raytraced (see raytrace_script.txt).
sed "s|@TMP@|$TMP/rt|g" "$HERE/raytrace_script.txt" > "$TMP/raytrace_script.txt"
mkdir -p "$TMP/rt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  RT="$(env DINO8_RT_FRAMES=1 "$BIN" --smoke 60 --script "$TMP/raytrace_script.txt" 2>&1)" || { echo "$RT"; echo "FAIL: raytrace script exited non-zero"; exit 1; }
else
  RT="$(xvfb-run -a -s "-screen 0 1600x900x24" env DINO8_RT_FRAMES=1 "$BIN" --smoke 60 --script "$TMP/raytrace_script.txt" 2>&1)" || { echo "$RT"; echo "FAIL: raytrace script exited non-zero"; exit 1; }
fi
rtcheck() { if echo "$RT" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
rtcheck "Created material ChromeMat from preset Chrome" "RenderAssignMaterialToObjects Preset= created a material from the built-in library"
rtcheck "Material GoldMat assigned to 1 object(s)" "material from a preset assigned to an object"
rtcheck "MaterialLibrary: 48 built-in preset(s)" "MaterialLibrary reports the full preset count"
rtcheck "Render: rendered Perspective at 96 x 64 .* \[Raytraced Samples=4 Bounces=2 Denoise=Yes\]" "Render honoured Quality=Raytraced Samples= Bounces="
rtcheck "Saved rendering $TMP/rt/raytrace.bmp (96 x 64)" "SaveRenderWindowAs wrote the raytraced BMP"
rtcheck "RenderArctic: rendered Perspective at 1280 x 720 .* \[Raytraced" "RenderArctic ran the path tracer at the document size"
rtcheck "RenderPreview: rendered Perspective .* \[Raytraced" "RenderPreview ran the path tracer at viewport size"
rtcheck "RenderBlowup:.*region rendered as a true optical zoom.*\[Raytraced\]" "RenderBlowup did a real optical zoom with the path tracer too, not a crop"
rtcheck "Saved $TMP/rt/raytrace.3dm" "the raytraced scene saved to a .3dm"
rtcheck "gl_error=0" "no OpenGL errors while the viewport was in RayTracedViewport mode"
# Real proof RayTracedViewport actually produced a frame, not just that no
# GL error happened (gl_error=0 alone would pass just as well if
# GpuRaytracer::Init() silently failed and the whole mode were a no-op -
# this is exactly the gap tests/gpu_render_notes.md itself documented and
# that a fully broken shader once hid: see the DINO8_RT_FRAMES-gated
# rt_accum_frames print in Viewport.cpp).
if echo "$RT" | grep -qE "rt_accum_frames=[1-9][0-9]* empty=0"; then
  echo "ok   RayTracedViewport actually accumulated real frames (empty=0, nonzero count), not just a clean but silently-no-op GL error queue"
else
  echo "FAIL RayTracedViewport never produced a real frame (no 'rt_accum_frames=N>0 empty=0' line) - gl_error=0 alone does not prove the raytracer ran"; fail=1
fi
if echo "$RT" | grep -q "GpuRaytracer::Init failed"; then
  echo "FAIL GpuRaytracer::Init failed during the raytrace script - the GPU raytracer is non-functional in this environment"; fail=1
else
  echo "ok   GpuRaytracer::Init did not fail during the raytrace script"
fi
check_nonflat_bmp() {
  # $1 = bmp path, $2 = label. Reads w/h from the BMP header itself (rather
  # than hardcoding them) since RenderArctic/RenderPreview/RenderBlowup don't
  # all render at a fixed size, and asserts the pixel data is non-flat - real
  # proof PathTracer::Render() produced real, varied pixel data for that call
  # site, not just that the command printed a normal-looking status line.
  python3 - "$1" <<'PY' && echo "ok   $2 is a valid, non-flat 24-bit BMP" || { echo "FAIL $2 invalid or flat"; fail=1; }
import struct, sys
d = open(sys.argv[1], 'rb').read()
assert d[:2] == b'BM', 'signature'
size, off, hdr, w, h, planes, bpp = struct.unpack('<IxxxxIIiiHH', d[2:30])
assert size == len(d) and hdr == 40 and w > 0 and h > 0 and planes == 1 and bpp == 24, (size, len(d), w, h, bpp)
px = d[off:]
assert len(px) == ((w * 3 + 3) & ~3) * abs(h), 'pixel data size'
assert max(px) > 0 and min(px) < 255, 'image is flat'
PY
}
check_nonflat_bmp "$TMP/rt/raytrace.bmp" "raytrace.bmp"
check_nonflat_bmp "$TMP/rt/arctic.bmp" "arctic.bmp (RenderArctic's own pixel output, not just its printed status line)"
check_nonflat_bmp "$TMP/rt/preview.bmp" "preview.bmp (RenderPreview's own pixel output, not just its printed status line)"
check_nonflat_bmp "$TMP/rt/blowup.bmp" "blowup.bmp (RenderBlowup's own pixel output, not just its printed status line)"

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
cat > "$TMP/sess/dig_points2.txt" <<'EOP'
10,0,0
0,0,50
0,0,0
20,0,0
30,0,0
40,0,0
50,0,0
51,1,0
52,0,0
53,1,0
EOP
sed "s|@TMP@|$TMP/sess|g" "$HERE/session_script.txt" > "$TMP/sess/session_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  SS="$("$BIN" --smoke 200 --script "$TMP/sess/session_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: session script exited non-zero"; exit 1; }
else
  SS="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 200 --script "$TMP/sess/session_script.txt" 2>&1)" || { echo "$SS"; echo "FAIL: session script exited non-zero"; exit 1; }
fi
sscheck() { if echo "$SS" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
sscheck "Digitizer: connected, protocol File, file $TMP/sess/dig_points.txt" "DigConnect opened the fixture file"
sscheck "DigPoint: digitized 1,2,3" "DigPoint read the first fixture point"
sscheck "DigPoint: digitized 4.5,5.5,6.5 (button 1)" "DigPoint read the second point and its button"
sscheck "DigPoint: no point available" "DigPoint warns once the fixture file is exhausted"
sscheck "DigScale: scale set to 25.4" "DigScale set the unit scale"
sscheck "DigDisconnect: disconnected" "DigDisconnect"
sscheck "Digitizer: not connected" "DigStatus reports disconnected after DigDisconnect"
sscheck "DigScale: scale set to 1" "DigScale reset to 1 for the second connection"
sscheck "Digitize: digitized 10,0,0" "Digitize read a real point off the digitizer"
sscheck "DigCamera: camera set from two digitized points" "DigCamera read an eye and target point"
sscheck "DigClick: digitized 20,0,0" "DigClick read and printed a point"
sscheck "DigLine: line digitized" "DigLine read two points and built a line"
sscheck "CV\[0\] 30,0,0" "the DigLine curve starts at the first digitized point"
sscheck "CV\[1\] 40,0,0" "the DigLine curve ends at the second digitized point"
sscheck "DigBeep on" "DigBeep toggled on"
sscheck "Digitize: digitized 50,0,0" "Digitize still works with DigBeep on"
sscheck "DigSection: 3 point(s) digitized into a curve" "DigSection read the remaining points into a curve"
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

# Remaining-command QC: curve conversion, tween/extruded/developable surfaces,
# thickness/continuity analysis, mesh clean-up incl. connected-face isolation
# and curve splitting, layer/window/point-cloud/file-recovery utilities (see
# remaining_script.txt).
sed "s|@TMP@|$TMP|g" "$HERE/remaining_script.txt" > "$TMP/remaining_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  RN="$("$BIN" --smoke 900 --script "$TMP/remaining_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: remaining script exited non-zero"; exit 1; }
else
  RN="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 900 --script "$TMP/remaining_script.txt" 2>&1)" || { echo "$RN"; echo "FAIL: remaining script exited non-zero"; exit 1; }
fi
rncheck2() { if echo "$RN" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
rncheck2 "1 object(s) selected" "Convert built a curve to select and inspect"
rncheck2 "degree 1, 5 control points, non-rational, closed" "Convert Output=Lines kept the 4-point polyline exact (closed 5-CV polyline)"
rncheck2 "RibbonOffset: 1 ribbon(s) of width 2" "RibbonOffset built one ribbon"
rncheck2 "TweenSurfaces: 1 surface(s) between the two inputs" "TweenSurfaces built the midway surface"
rncheck2 "MultiPipe: " "MultiPipe ran on two separate lines"
rncheck2 "CheckNewObjects on: every object a command adds is validated" "CheckNewObjects toggled on"
rncheck2 "ClearAnalysisMeshes: " "ClearAnalysisMeshes ran"
rncheck2 "DupLayer: " "DupLayer copied the current layer"
rncheck2 "LayerBook: page 1 of [0-9]*: Default shown alone" "LayerBook Next showed a single page"
rncheck2 "LayerBook: all [0-9]* layer(s) on" "LayerBook All turned every layer back on"
rncheck2 "IsolateLock: " "IsolateLock locked the rest of the scene"
rncheck2 "JoinCopy: " "JoinCopy joined copies of the selection"
rncheck2 "MatchProperties: layer, color, material, linetype" "MatchProperties copied properties across"
rncheck2 "MoveTargetToObjects: " "MoveTargetToObjects moved the camera target"
rncheck2 "ClearAllObjectDisplayModes: " "ClearAllObjectDisplayModes ran"
rncheck2 "SaveWindowLayout: saved QCLayout" "SaveWindowLayout wrote a layout"
rncheck2 "WindowLayout: restored QCLayout" "WindowLayout restored it"
rncheck2 "AcadSchemes: there are no per-version export 'schemes'" "AcadSchemes explains the real limitation (DWG now writes for real, via LibreDWG, as AC1015)"
rncheck2 "Unwrap: no per-triangle flattening/unwrapping algorithm exists" "Unwrap explains the real limitation and extracts a UV mesh"
# IgesImportOptions/STEPTree open a file dialog (owned by cmd_exchange2.cpp,
# not this file) and EditScript opens the Lua Script Editor panel silently
# (owned by cmd_misc.cpp); none of the three print text in script mode, so
# this just exercises that the catalog still resolves them to those real
# commands and not to a shadowing stub (see cmd_remaining.cpp's removal of
# its old, dead duplicate registrations for all three).
rncheck2 "PointCloudContour: 1 contour(s) from 1 plane(s) 10 apart (band 5)" "PointCloudContour built one contour from the flat 8-point circle"
rncheck2 "degree 1, 9 control points, non-rational, closed" "PointCloudContour's contour is a closed 8-gon (8 points + seam)"
rncheck2 "PointCloudSection: 1 section curve(s) through 0,0,0" "PointCloudSection sliced the same cloud through the origin"
rncheck2 "ShortPath: [0-9]* point(s), length 28.2[0-9]* (straight-line distance 28.2[0-9]*)" "ShortPath on a flat plane matches the straight-line diagonal"
rncheck2 "DevLoft: ruled surface with 40 adjusted rulings (mean twist [0-9.e-]*)" "DevLoft built a ruled surface between the two straight rails"
SP_LEN="$(echo "$RN" | sed -n 's/.*ShortPath: [0-9]* point(s), length \([0-9.]*\).*/\1/p' | head -1)"
python3 -c "import sys; v=float('$SP_LEN'); sys.exit(0 if abs(v-28.284) < 0.05 else 1)" \
  && echo "ok   ShortPath length ($SP_LEN) matches the exact diagonal of a 20x20 square (28.284)" \
  || { echo "FAIL ShortPath length ($SP_LEN) does not match the expected diagonal 28.284"; fail=1; }
DL_TWIST="$(echo "$RN" | sed -n 's/.*mean twist \([0-9.eE+-]*\)).*/\1/p' | head -1)"
python3 -c "import sys; v=float('$DL_TWIST'); sys.exit(0 if v < 1e-3 else 1)" \
  && echo "ok   DevLoft mean twist ($DL_TWIST) is essentially zero for two straight parallel rails (already developable)" \
  || { echo "FAIL DevLoft mean twist ($DL_TWIST) is not near zero"; fail=1; }
rncheck2 "SplitMeshWithCurve: 2 mesh piece(s) (split by the curve's plane)" "SplitMeshWithCurve split the mesh box into two pieces"
rncheck2 "SelConnectedMeshFaces: 6 connected face(s) split off into object [0-9]* and selected (mesh had 2 disconnected part(s))" "SelConnectedMeshFaces split the picked box out of the two-box mesh"
rncheck2 "CreaseSplitting on: AutomaticSubDFromMesh creases edges sharper than its Angle" "CreaseSplitting On"
rncheck2 "CreaseSplitting off: AutomaticSubDFromMesh makes smooth SubDs" "CreaseSplitting Off"
rncheck2 "ChangeSpace: 0 object(s) moved back to model space (Space tag removed)" "ChangeSpace with no layout active found no Space tag to remove on a plain object"
rncheck2 "Rescue3dmFile: recovered 2 object(s) from $TMP/rescue_src.3dm" "Rescue3dmFile recovered exactly the box and the sphere"
rncheck2 "^ok   expect_objects 2" "Rescue3dmFile left the document with exactly those 2 recovered objects"
rncheck2 "ExportBitmaps: 0 texture file(s) copied to $TMP/bitmaps_out" "ExportBitmaps ran cleanly with no textures assigned"
echo "$RN" | grep -E "^(ok|FAIL)"
if echo "$RN" | grep -q "^FAIL"; then fail=1; fi
rncheck2 "gl_error=0" "remaining script ran without OpenGL errors"

# Command-line autocomplete: real fuzzy/subsequence matching, not just
# prefix matching (see fuzzy_autocomplete_script.txt). "bdiff" and "zmanif"
# are neither prefixes nor contiguous substrings of any catalog command,
# but are in-order subsequences of exactly one or two command names each;
# Tab-completing them must pick the (shorter) real command. A plain
# prefix ("Box") must still Tab-complete to itself, unchanged.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  FZ="$("$BIN" --smoke 60 --script "$HERE/fuzzy_autocomplete_script.txt" 2>&1)" || { echo "$FZ"; echo "FAIL: fuzzy-autocomplete script exited non-zero"; exit 1; }
else
  FZ="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/fuzzy_autocomplete_script.txt" 2>&1)" || { echo "$FZ"; echo "FAIL: fuzzy-autocomplete script exited non-zero"; exit 1; }
fi
fzcheck() { if echo "$FZ" | grep -qE "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
fzcheck "^history: Command: BooleanDifference$" "fuzzy subsequence 'bdiff' (not a prefix/substring of any command) autocompleted to BooleanDifference"
fzcheck "^history: Command: Box$" "plain prefix 'Box' still autocompletes to itself (no regression from adding fuzzy matching)"
fzcheck "^history: Command: ZoomNonManifold$" "fuzzy subsequence 'zmanif' (not a prefix/substring of any command) autocompleted to the unique match ZoomNonManifold"
fzcheck "gl_error=0" "fuzzy-autocomplete script ran without OpenGL errors"

# i18n: SetLanguage actually swaps the active string table, a key missing
# from a language's table (panel.imgui_demo is deliberately absent from
# es.json - see cmd_state.cpp's I18nSelfTest) falls back to English instead
# of a blank string or the raw key, an unknown key falls back to itself
# rather than crashing, and an unrecognised language name fails with a
# clear diagnostic instead of silently doing nothing.
cat > "$TMP/i18n_script.txt" <<'EOS'
SetLanguage es
I18nSelfTest
SetLanguage English
I18nSelfTest
SetLanguage nope
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  I18N="$("$BIN" --smoke 60 --script "$TMP/i18n_script.txt" 2>&1)" || { echo "$I18N"; echo "FAIL: i18n script exited non-zero"; exit 1; }
else
  I18N="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMP/i18n_script.txt" 2>&1)" || { echo "$I18N"; echo "FAIL: i18n script exited non-zero"; exit 1; }
fi
i18ncheck() { if echo "$I18N" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
i18ncheck "SetLanguage: es" "SetLanguage switched to Spanish"
i18ncheck "I18nSelfTest: active=es" "the active language is now es"
i18ncheck "I18nSelfTest: menu\.file=Archivo" "a translated string (menu.file) reads Archivo in Spanish, proving the switch changes displayed text"
i18ncheck "I18nSelfTest: fallback(panel\.imgui_demo)=ImGui Demo (developer)" "a key missing from es.json falls back to the English text, not a blank string or the raw key"
i18ncheck "I18nSelfTest: unknown_key=this\.key\.does\.not\.exist\.anywhere" "a key present in no language table at all falls back to the key itself rather than crashing or blanking"
i18ncheck "SetLanguage: en" "SetLanguage switched back to English"
i18ncheck "I18nSelfTest: active=en" "the active language is en again"
i18ncheck "I18nSelfTest: menu\.file=File\$" "the same key reads back in plain English once switched back"
i18ncheck "SetLanguage: unknown language 'nope'" "an unrecognised language name fails with a clear diagnostic instead of doing nothing"

# a11y: High Contrast is a distinct palette (pure black bg / pure white
# text / a forced 1px frame border), not merely a filter over Dark, and
# switching themes actually rewrites the live ImGui style colours (see
# docs/ACCESSIBILITY.md and SetTheme/ThemeSelfTest in cmd_state.cpp).
cat > "$TMP/a11y_script.txt" <<'EOS'
SetTheme HighContrast
ThemeSelfTest
SetTheme Dark
ThemeSelfTest
SetTheme nope
EOS
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1; then
  A11Y="$("$BIN" --smoke 60 --script "$TMP/a11y_script.txt" 2>&1)" || { echo "$A11Y"; echo "FAIL: a11y script exited non-zero"; exit 1; }
else
  A11Y="$(xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$TMP/a11y_script.txt" 2>&1)" || { echo "$A11Y"; echo "FAIL: a11y script exited non-zero"; exit 1; }
fi
a11ycheck() { if echo "$A11Y" | grep -q "$1"; then echo "ok   $2"; else echo "FAIL $2"; fail=1; fi; }
a11ycheck "SetTheme: highcontrast" "SetTheme switched to High Contrast"
a11ycheck "ThemeSelfTest: mode=2" "the active theme mode is now HighContrast (2)"
a11ycheck "ThemeSelfTest: window_bg=0\.000,0\.000,0\.000" "High Contrast's window background is pure black, not a tint of Dark's background"
a11ycheck "ThemeSelfTest: text=1\.000,1\.000,1\.000" "High Contrast's text colour is pure white (~21:1 contrast on black)"
a11ycheck "ThemeSelfTest: frame_border=1\.00" "High Contrast forces a visible 1px frame border (0 in Dark/Light), so state is legible from outline, not colour alone"
a11ycheck "SetTheme: dark" "SetTheme switched back to Dark"
a11ycheck "ThemeSelfTest: mode=0" "the active theme mode is Dark (0) again"
a11ycheck "ThemeSelfTest: window_bg=0\.110,0\.118,0\.137" "switching back to Dark actually restores Dark's own background colour, not black"
a11ycheck "ThemeSelfTest: frame_border=0\.00" "Dark's frame border is 0, confirming the forced border is High-Contrast-specific"
a11ycheck "SetTheme: unknown theme 'nope'" "an unrecognised theme name fails with a clear diagnostic instead of doing nothing"

# Frustum culling (Viewport.cpp - see tests/performance_notes.md's "No LOD
# or frustum culling" item and tests/cull_test.sh for the full A/B/pixel-
# diff proof): fold its pass/fail lines into this script's own count so a
# regression here fails smoke.sh, not just a separately-run script.
CULL="$(bash "$HERE/cull_test.sh" "$BIN" 2>&1)" || true
echo "$CULL" | grep -E "^(ok|FAIL)"
if echo "$CULL" | grep -q "^FAIL"; then fail=1; fi
echo "$CULL" | grep -q "^ok   cull-on and cull-off screenshots are pixel-identical" || { echo "FAIL cull_test.sh did not run to completion"; fail=1; }

# Docs tutorials (docs/site/tutorials.html and README's "10 tutorials,
# verified by running them" claim): run every 01..10 tutorial script
# through the real binary via docs/tutorial_scripts/run_all.sh and fold
# its pass/fail into this script's own count. Without this, the tutorial
# scripts are real and do pass, but nothing gates them - a change that
# breaks a documented tutorial sequence would only be noticed by a human
# manually re-running run_all.sh, not by smoke.sh or CI.
TUT="$(bash "$HERE/../docs/tutorial_scripts/run_all.sh" "$BIN" 2>&1)" || true
echo "$TUT" | grep -E "^(FAIL|PASS:)"
if echo "$TUT" | grep -q "^FAIL"; then fail=1; fi
TUT_PASS_COUNT=$(echo "$TUT" | grep -c "^PASS:")
[ "$TUT_PASS_COUNT" -eq 10 ] || { echo "FAIL: expected 10/10 tutorial scripts to pass, got $TUT_PASS_COUNT"; fail=1; }

exit $fail
