# ---------------------------------------------------------------------------
# GENDP_: Flow Register row D8-GH-CURVE-0011 (End Points), flowgeo_gen.py. The graph flowgeo_gendp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: a circle of radius 3 at (1,2,0) starts and ends at (4,2,0) (its X axis point); the line (1,2,3)->(3,4,4) at its two points; the polyline (0,0,0),(4,0,0),(4,3,0) at (0,0,0) and (4,3,0); the arc of radius 2 from 0 to 90 degrees at (2,0,0) and (0,2,0).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gendp.dflow|g" "$HERE/flowgeo_gendp_script.txt" > "$TMPW/flowgeo_gendp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gendp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GENDP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gendp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GENDP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GENDP_$1: $3 (measured '$got')"; else echo "FAIL GENDP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 87 node(s)" <<<"$FR_OUT"; then echo "ok   GENDP_solve: the player solved all 87 nodes (every node type is registered)"; else echo "FAIL GENDP_solve: expected 'solved 87 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   GENDP_objects: exactly 13 point(s) baked"; else echo "FAIL GENDP_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "0" "circle: starts at (4,2,0)"
fr_chk 2 "0" "circle: ends at (4,2,0), where it started"
fr_chk 3 "0" "circle: start and end are one point"
fr_chk 4 "1" "circle: start and end are one point (the node under test produced the value)"
fr_chk 5 "0" "line: starts at (1,2,3)"
fr_chk 6 "0" "line: ends at (3,4,4)"
fr_chk 7 "0" "polyline: starts at its first vertex (0,0,0)"
fr_chk 8 "1" "polyline: starts at its first vertex (0,0,0) (the node under test produced the value)"
fr_chk 9 "0" "polyline: ends at its last vertex (4,3,0)"
fr_chk 10 "0" "arc: starts at (2,0,0)"
fr_chk 11 "0" "arc: ends at (0,2,0)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
