# ---------------------------------------------------------------------------
# GDIVL_: Flow Register row D8-GH-CURVE-0033 (Divide Length), flowgeo_gen.py. The graph flowgeo_gdivl.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: points at arc lengths 0, L, 2L, ... from the start, the remainder at the end getting no point of its own: the line (0,0,0)-(10,0,0) with L = 3 gives x = 0, 3, 6, 9 (4 points); with L = 2.5 gives x = 0, 2.5, 5, 7.5, 10 (5 points, the last multiple lands on the end); the circle of radius 2 with L = 1 gives 13 points (circumference 12.566 holds 12 steps) at angles k / 2 radians, (2 cos, 2 sin); the polyline (0,0,0),(4,0,0),(4,3,0) (length 7) with L = 2.5 gives (0,0), (2.5,0), (4,1).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gdivl.dflow|g" "$HERE/flowgeo_gdivl_script.txt" > "$TMPW/flowgeo_gdivl_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gdivl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GDIVL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gdivl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GDIVL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GDIVL_$1: $3 (measured '$got')"; else echo "FAIL GDIVL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 222 node(s)" <<<"$FR_OUT"; then echo "ok   GDIVL_solve: the player solved all 222 nodes (every node type is registered)"; else echo "FAIL GDIVL_solve: expected 'solved 222 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 34 " <<<"$FR_OUT"; then echo "ok   GDIVL_objects: exactly 34 point(s) baked"; else echo "FAIL GDIVL_objects: expected 34 baked objects"; fail=1; fi
fr_chk 1 "4" "line, L = 3: 4 points"
fr_chk 2 "0" "line, L = 3: point 0 at x = 0"
fr_chk 3 "1" "line, L = 3: point 0 at x = 0 (the node under test produced the value)"
fr_chk 4 "0" "line, L = 3: point 1 at x = 3"
fr_chk 5 "0" "line, L = 3: point 2 at x = 6"
fr_chk 6 "0" "line, L = 3: point 3 at x = 9"
fr_chk 7 "5" "line, L = 2.5: 5 points"
fr_chk 8 "0" "line, L = 2.5: point 0 at x = 0"
fr_chk 9 "1" "line, L = 2.5: point 0 at x = 0 (the node under test produced the value)"
fr_chk 10 "0" "line, L = 2.5: point 1 at x = 2.5"
fr_chk 11 "0" "line, L = 2.5: point 2 at x = 5"
fr_chk 12 "0" "line, L = 2.5: point 3 at x = 7.5"
fr_chk 13 "0" "line, L = 2.5: point 4 at x = 10"
fr_chk 14 "13" "circle r = 2, L = 1: 13 points"
fr_chk 15 "0" "circle r = 2, L = 1: point 0 at 0 radians"
fr_chk 16 "0" "circle r = 2, L = 1: point 1 at 0.5 radians"
fr_chk 17 "0" "circle r = 2, L = 1: point 2 at 1 radians"
fr_chk 18 "0" "circle r = 2, L = 1: point 3 at 1.5 radians"
fr_chk 19 "0" "circle r = 2, L = 1: point 4 at 2 radians"
fr_chk 20 "0" "circle r = 2, L = 1: point 5 at 2.5 radians"
fr_chk 21 "0" "circle r = 2, L = 1: point 6 at 3 radians"
fr_chk 22 "0" "circle r = 2, L = 1: point 7 at 3.5 radians"
fr_chk 23 "0" "circle r = 2, L = 1: point 8 at 4 radians"
fr_chk 24 "0" "circle r = 2, L = 1: point 9 at 4.5 radians"
fr_chk 25 "0" "circle r = 2, L = 1: point 10 at 5 radians"
fr_chk 26 "0" "circle r = 2, L = 1: point 11 at 5.5 radians"
fr_chk 27 "0" "circle r = 2, L = 1: point 12 at 6 radians"
fr_chk 28 "3" "polyline of length 7, L = 2.5: 3 points"
fr_chk 29 "0" "polyline, L = 2.5: point 0 at (0, 0)"
fr_chk 30 "1" "polyline, L = 2.5: point 0 at (0, 0) (the node under test produced the value)"
fr_chk 31 "0" "polyline, L = 2.5: point 1 at (2.5, 0)"
fr_chk 32 "0" "polyline, L = 2.5: point 2 at (4, 1)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
