# ---------------------------------------------------------------------------
# GDIVC_: Flow Register row D8-GH-CURVE-0031 (Divide Curve), flowgeo_gen.py. The graph flowgeo_gdivc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: circle of radius 2 about (1,2,0) into 8: a closed curve gives 8 points (the ninth would be the first again), at angles 45 k degrees: (1 + 2 cos, 2 + 2 sin, 0), the tangent at k = 2 is (-1,0,0); the line (0,0,0)-(10,0,0) into 4: 5 points at x = 0, 2.5, 5, 7.5, 10; the arc of radius 3 from 0 to 90 degrees into 3: 4 points at 0, 30, 60, 90 degrees, 3 (cos, sin); the closed polyline (0,0),(6,0),(6,4),(0,4) (perimeter 20) into 10: 10 points 2 apart along the outline: (0,0),(2,0),(4,0),(6,0),(6,2),(6,4),(4,4),(2,4),(0,4),(0,2).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gdivc.dflow|g" "$HERE/flowgeo_gdivc_script.txt" > "$TMPW/flowgeo_gdivc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gdivc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GDIVC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gdivc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GDIVC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GDIVC_$1: $3 (measured '$got')"; else echo "FAIL GDIVC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 245 node(s)" <<<"$FR_OUT"; then echo "ok   GDIVC_solve: the player solved all 245 nodes (every node type is registered)"; else echo "FAIL GDIVC_solve: expected 'solved 245 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 36 " <<<"$FR_OUT"; then echo "ok   GDIVC_objects: exactly 36 point(s) baked"; else echo "FAIL GDIVC_objects: expected 36 baked objects"; fail=1; fi
fr_chk 1 "8" "circle into 8: 8 points (closed)"
fr_chk 2 "0" "circle into 8: point 0 at 0 degrees"
fr_chk 3 "0" "circle into 8: point 1 at 45 degrees"
fr_chk 4 "0" "circle into 8: point 2 at 90 degrees"
fr_chk 5 "0" "circle into 8: point 3 at 135 degrees"
fr_chk 6 "0" "circle into 8: point 4 at 180 degrees"
fr_chk 7 "0" "circle into 8: point 5 at 225 degrees"
fr_chk 8 "0" "circle into 8: point 6 at 270 degrees"
fr_chk 9 "0" "circle into 8: point 7 at 315 degrees"
fr_chk 10 "0" "circle into 8: the tangent at point 2 (90 degrees) is (-1,0,0)"
fr_chk 11 "5" "line into 4: 5 points (open)"
fr_chk 12 "0" "line into 4: point 0 at x = 0"
fr_chk 13 "1" "line into 4: point 0 at x = 0 (the node under test produced the value)"
fr_chk 14 "0" "line into 4: point 1 at x = 2.5"
fr_chk 15 "0" "line into 4: point 2 at x = 5"
fr_chk 16 "0" "line into 4: point 3 at x = 7.5"
fr_chk 17 "0" "line into 4: point 4 at x = 10"
fr_chk 18 "4" "arc into 3: 4 points (open)"
fr_chk 19 "0" "arc into 3: point 0 at 0 degrees"
fr_chk 20 "0" "arc into 3: point 1 at 30 degrees"
fr_chk 21 "0" "arc into 3: point 2 at 60 degrees"
fr_chk 22 "0" "arc into 3: point 3 at 90 degrees"
fr_chk 23 "10" "closed polyline into 10: 10 points"
fr_chk 24 "0" "closed polyline into 10: point 0 at (0, 0)"
fr_chk 25 "1" "closed polyline into 10: point 0 at (0, 0) (the node under test produced the value)"
fr_chk 26 "0" "closed polyline into 10: point 1 at (2, 0)"
fr_chk 27 "0" "closed polyline into 10: point 2 at (4, 0)"
fr_chk 28 "0" "closed polyline into 10: point 3 at (6, 0)"
fr_chk 29 "0" "closed polyline into 10: point 4 at (6, 2)"
fr_chk 30 "0" "closed polyline into 10: point 5 at (6, 4)"
fr_chk 31 "0" "closed polyline into 10: point 6 at (4, 4)"
fr_chk 32 "0" "closed polyline into 10: point 7 at (2, 4)"
fr_chk 33 "0" "closed polyline into 10: point 8 at (0, 4)"
fr_chk 34 "0" "closed polyline into 10: point 9 at (0, 2)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
