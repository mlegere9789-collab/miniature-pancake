# ---------------------------------------------------------------------------
# GEVLEN_: Flow Register row D8-GH-CURVE-0013 (Evaluate Length), flowgeo_gen.py. The graph flowgeo_gevlen.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the circle of radius 2 at the origin: arc length 1 from its start is angle 1/2 radian, (2 cos 0.5, 2 sin 0.5, 0); a fraction 0.25 of its length (Normalized) is a quarter turn (0,2,0), tangent (-1,0,0); length 0 is the start (2,0,0); a length past the end is the end; the line (1,2,3)->(4,6,3) at length 2.5 (half of 5) is (2.5,4,3) with tangent (0.6,0.8,0); the polyline (0,0,0),(4,0,0),(4,3,0) at length 5 is (4,1,0): 4 along the first edge and 1 along the second.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gevlen.dflow|g" "$HERE/flowgeo_gevlen_script.txt" > "$TMPW/flowgeo_gevlen_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gevlen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GEVLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gevlen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GEVLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GEVLEN_$1: $3 (measured '$got')"; else echo "FAIL GEVLEN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 75 node(s)" <<<"$FR_OUT"; then echo "ok   GEVLEN_solve: the player solved all 75 nodes (every node type is registered)"; else echo "FAIL GEVLEN_solve: expected 'solved 75 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   GEVLEN_objects: exactly 10 point(s) baked"; else echo "FAIL GEVLEN_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0" "circle: length 1 from the start is at 0.5 radian"
fr_chk 2 "0" "circle: a quarter of the length is (0,2,0)"
fr_chk 3 "0" "circle: the tangent there is (-1,0,0)"
fr_chk 4 "0" "circle: length 0 is the start (2,0,0)"
fr_chk 5 "0" "circle: a length past the end is the end, (2,0,0) again"
fr_chk 6 "0" "line: length 2.5 is (2.5,4,3)"
fr_chk 7 "0" "line: the tangent is (0.6,0.8,0)"
fr_chk 8 "0" "polyline: length 5 is (4,1,0)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
