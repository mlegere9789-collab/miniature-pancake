# ---------------------------------------------------------------------------
# GREV_: Flow Register row D8-GH-CURVE-0085 (Reverse Curve), flowgeo_gen.py. The graph flowgeo_grev.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the line (1,2,3)->(4,6,3) reversed starts at (4,6,3) and ends at (1,2,3), has the same length 5 and the middle (2.5,4,3) with unit tangent (-0.6,-0.8,0) (it was (0.6,0.8,0)); the circle of radius 2 at the origin starts at (2,0,0) both ways, its tangent there (0,1,0) turns into (0,-1,0) and a quarter of the way round is (0,-2,0) instead of (0,2,0); the polyline (0,0,0),(4,0,0),(4,3,0) reversed starts at (4,3,0), ends at (0,0,0) and one unit along from its start is (4,2,0).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_grev.dflow|g" "$HERE/flowgeo_grev_script.txt" > "$TMPW/flowgeo_grev_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_grev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_grev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GREV_$1: $3 (measured '$got')"; else echo "FAIL GREV_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 96 node(s)" <<<"$FR_OUT"; then echo "ok   GREV_solve: the player solved all 96 nodes (every node type is registered)"; else echo "FAIL GREV_solve: expected 'solved 96 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   GREV_objects: exactly 14 point(s) baked"; else echo "FAIL GREV_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "0" "line: starts at (4,6,3)"
fr_chk 2 "0" "line: ends at (1,2,3)"
fr_chk 3 "0" "line: still 5 long"
fr_chk 4 "0" "line: the middle is (2.5,4,3)"
fr_chk 5 "0" "line: the tangent is (-0.6,-0.8,0)"
fr_chk 6 "0" "circle: starts at (2,0,0)"
fr_chk 7 "0" "circle: the tangent at the start is (0,-1,0)"
fr_chk 8 "0" "circle: a quarter of the way round is (0,-2,0)"
fr_chk 9 "0" "polyline: starts at (4,3,0)"
fr_chk 10 "0" "polyline: ends at (0,0,0)"
fr_chk 11 "1" "polyline: ends at (0,0,0) (the node under test produced the value)"
fr_chk 12 "0" "polyline: one unit from the new start is (4,2,0)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
