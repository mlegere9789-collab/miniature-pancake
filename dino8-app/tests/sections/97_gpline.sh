# ---------------------------------------------------------------------------
# GPLINE_: Flow Register row D8-GH-CURVE-0055 (Polyline / Control Points), flowgeo_gen.py. The graph flowgeo_gpline.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: a polyline keeps its vertices as the control points of its degree-1 curve: (0,0,0),(4,0,0),(4,3,0),(0,3,0) gives 4 control points at those places, weights 1, and the length 4 + 3 + 4 = 11 (14 closed); the cubic NURBS curve through 6 given control points returns the same 6 points, 6 weights of 1 and 6 + 3 - 1 = 8 knots.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gpline.dflow|g" "$HERE/flowgeo_gpline_script.txt" > "$TMPW/flowgeo_gpline_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gpline_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPLINE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gpline_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPLINE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GPLINE_$1: $3 (measured '$got')"; else echo "FAIL GPLINE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 140 node(s)" <<<"$FR_OUT"; then echo "ok   GPLINE_solve: the player solved all 140 nodes (every node type is registered)"; else echo "FAIL GPLINE_solve: expected 'solved 140 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 21 " <<<"$FR_OUT"; then echo "ok   GPLINE_objects: exactly 21 point(s) baked"; else echo "FAIL GPLINE_objects: expected 21 baked objects"; fail=1; fi
fr_chk 1 "4" "polyline: 4 control points"
fr_chk 2 "0" "polyline: control point 0 is the vertex (0, 0, 0)"
fr_chk 3 "1" "polyline: control point 0 is the vertex (0, 0, 0) (the node under test produced the value)"
fr_chk 4 "0" "polyline: control point 1 is the vertex (4, 0, 0)"
fr_chk 5 "0" "polyline: control point 2 is the vertex (4, 3, 0)"
fr_chk 6 "0" "polyline: control point 3 is the vertex (0, 3, 0)"
fr_chk 7 "4" "polyline: the 4 weights are 1 (sum 4)"
fr_chk 8 "0" "polyline: length 4 + 3 + 4 = 11"
fr_chk 9 "0" "closed polyline: length 14"
fr_chk 10 "6" "cubic curve: 6 control points"
fr_chk 11 "0" "cubic curve: control point 0 is (0, 0, 0)"
fr_chk 12 "1" "cubic curve: control point 0 is (0, 0, 0) (the node under test produced the value)"
fr_chk 13 "0" "cubic curve: control point 1 is (1, 2, 0)"
fr_chk 14 "0" "cubic curve: control point 2 is (2, 3, 1)"
fr_chk 15 "0" "cubic curve: control point 3 is (4, 3, 1)"
fr_chk 16 "0" "cubic curve: control point 4 is (5, 1, 0)"
fr_chk 17 "0" "cubic curve: control point 5 is (6, 0, 0)"
fr_chk 18 "6" "cubic curve: the 6 weights are 1 (sum 6)"
fr_chk 19 "8" "cubic curve: 6 + 3 - 1 = 8 knots"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
