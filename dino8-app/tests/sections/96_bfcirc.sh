# ---------------------------------------------------------------------------
# BFCIRC_: Flow Register row geometry_nodes/Curve Circle (Curve Circle), flowrows_gen.py. The graph flowrows_bfcirc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: n points at angle 2 pi i / n on radius r: perimeter 2 n r sin(pi/n) (n=32, r=3 -> 18.8...; n=4, r=2 -> 8 sqrt 2; n=3, r=2 -> 6 sqrt 3); point count n with the closing point not counted; the n=3 box is x in [-r/2, r], y = +-r sqrt 3 / 2.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfcirc.dflow|g" "$HERE/flowrows_bfcirc_script.txt" > "$TMPW/flowrows_bfcirc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcirc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCIRC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcirc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCIRC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCIRC_$1: $3 (measured '$got')"; else echo "FAIL BFCIRC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 145 node(s)" <<<"$FR_OUT"; then echo "ok   BFCIRC_solve: the player solved all 145 nodes (every node type is registered)"; else echo "FAIL BFCIRC_solve: expected 'solved 145 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 27 " <<<"$FR_OUT"; then echo "ok   BFCIRC_objects: exactly 27 point(s) baked"; else echo "FAIL BFCIRC_objects: expected 27 baked objects"; fail=1; fi
fr_chk 1 "32" "a 32-point circle has (the closing point is not counted): 32 points"
fr_chk 2 "1" "a 32-point circle has (the closing point is not counted): 1 splines"
fr_chk 3 "0" "32-gon of radius 3: perimeter 2 n r sin(pi/n)"
fr_chk 4 "0" "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY: min X"
fr_chk 5 "0" "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY: min Y"
fr_chk 6 "0" "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY: min Z"
fr_chk 7 "0" "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY: max X"
fr_chk 8 "0" "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY: max Y"
fr_chk 9 "0" "32 points include the angles 0, 90, 180, 270 degrees: box +-3 in XY: max Z"
fr_chk 10 "0" "4 points of radius 2 form a square of side 2 sqrt 2"
fr_chk 11 "0" "the square's corners are on the axes: min X"
fr_chk 12 "0" "the square's corners are on the axes: min Y"
fr_chk 13 "0" "the square's corners are on the axes: min Z"
fr_chk 14 "0" "the square's corners are on the axes: max X"
fr_chk 15 "0" "the square's corners are on the axes: max Y"
fr_chk 16 "0" "the square's corners are on the axes: max Z"
fr_chk 17 "0" "3 points of radius 2: equilateral triangle, side 2 sqrt 3, perimeter 6 sqrt 3"
fr_chk 18 "0" "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0)): min X"
fr_chk 19 "0" "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0)): min Y"
fr_chk 20 "0" "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0)): min Z"
fr_chk 21 "0" "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0)): max X"
fr_chk 22 "0" "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0)): max Y"
fr_chk 23 "0" "triangle points at 0, 120, 240 degrees: x from -1 to 2, y +-sqrt 3 (counter-clockwise from (r,0)): max Z"
fr_chk 24 "32" "the default circle has: 32 points"
fr_chk 25 "0" "the default radius is 1"
fr_chk 26 "3" "Resolution below 3 is raised to a triangle, giving: 3 points"
fr_chk 27 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
