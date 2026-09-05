#!/usr/bin/env bash
# Headless functional QC for Dino 8: runs a command script through the real
# app under Xvfb and checks the resulting history/object counts.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${1:-$HERE/../build/Dino8}"
TMP="$(mktemp -d)"
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
exit $fail
