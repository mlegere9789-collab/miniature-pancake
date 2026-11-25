# ---------------------------------------------------------------------------
# GAREA_: Flow Register row D8-GH-CURVE-0027 (Curve Area), flowgeo_gen.py. The graph flowgeo_garea.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: circle of radius 2.5 in the plane z = 3 about (1,2,3): 6.25 pi, centroid (1,2,3); triangle (0,0,0),(4,0,0),(0,3,0): 6, centroid (4/3,1,0); L-shaped polygon (0,0),(4,0),(4,1),(1,1),(1,3),(0,3): 4 * 1 + 1 * 2 = 6, centroid ((2 * 4 + 0.5 * 2)/6, (0.5 * 4 + 2 * 2)/6) = (1.5, 1); regular hexagon of circumradius 2: 3 sqrt 3 / 2 * 4 = 6 sqrt 3, centroid at the centre; the same triangle traced the other way has the same area.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_garea.dflow|g" "$HERE/flowgeo_garea_script.txt" > "$TMPW/flowgeo_garea_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_garea_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GAREA_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_garea_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GAREA_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GAREA_$1: $3 (measured '$got')"; else echo "FAIL GAREA_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 89 node(s)" <<<"$FR_OUT"; then echo "ok   GAREA_solve: the player solved all 89 nodes (every node type is registered)"; else echo "FAIL GAREA_solve: expected 'solved 89 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 12 " <<<"$FR_OUT"; then echo "ok   GAREA_objects: exactly 12 point(s) baked"; else echo "FAIL GAREA_objects: expected 12 baked objects"; fail=1; fi
fr_chk 1 "0" "circle r = 2.5: area 6.25 pi"
fr_chk 2 "0" "circle r = 2.5 at (1,2,3): centroid (1,2,3)"
fr_chk 3 "0" "triangle 4 x 3: area 6"
fr_chk 4 "0" "triangle: centroid (4/3,1,0)"
fr_chk 5 "0" "the triangle traced the other way: area 6"
fr_chk 6 "0" "L-shape: area 6"
fr_chk 7 "0" "L-shape: centroid (1.5,1,0)"
fr_chk 8 "0" "hexagon of circumradius 2: area 6 sqrt 3"
fr_chk 9 "0" "hexagon: centroid at the centre"
fr_chk 10 "1" "hexagon: centroid at the centre (the node under test produced the value)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
