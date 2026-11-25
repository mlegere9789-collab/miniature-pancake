# ---------------------------------------------------------------------------
# GREC2_: Flow Register row D8-GH-CURVE-0052 (Rectangle 2Pt), flowgeo_gen.py. The graph flowgeo_grec2.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: in the XY plane, corners (1,2) and (5,5): sides 4 and 3, area 12, perimeter 14, centroid (3,3.5,0); the corners given the other way round (5,5), (1,2) give the same; in the plane through the origin with axes (0,1,0) and (0,0,1) the points (0,1,2) and (7,4,6) are projected to (u,v) = (1,2) and (4,6): sides 3 and 4, area 12, perimeter 14, centroid at u = 2.5, v = 4, i.e. (0,2.5,4).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_grec2.dflow|g" "$HERE/flowgeo_grec2_script.txt" > "$TMPW/flowgeo_grec2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_grec2_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREC2_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_grec2_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREC2_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GREC2_$1: $3 (measured '$got')"; else echo "FAIL GREC2_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 79 node(s)" <<<"$FR_OUT"; then echo "ok   GREC2_solve: the player solved all 79 nodes (every node type is registered)"; else echo "FAIL GREC2_solve: expected 'solved 79 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   GREC2_objects: exactly 14 point(s) baked"; else echo "FAIL GREC2_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: area 12"
fr_chk 2 "0" "case 1: centroid (3, 3.5, 0)"
fr_chk 3 "0" "case 1: perimeter 14"
fr_chk 4 "1" "case 1: the rectangle is a closed curve"
fr_chk 5 "0" "case 2: area 12"
fr_chk 6 "0" "case 2: centroid (3, 3.5, 0)"
fr_chk 7 "0" "case 2: perimeter 14"
fr_chk 8 "1" "case 2: the rectangle is a closed curve"
fr_chk 9 "0" "case 3: area 12"
fr_chk 10 "0" "case 3: centroid (0, 2.5, 4)"
fr_chk 11 "0" "case 3: perimeter 14"
fr_chk 12 "1" "case 3: the rectangle is a closed curve"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
