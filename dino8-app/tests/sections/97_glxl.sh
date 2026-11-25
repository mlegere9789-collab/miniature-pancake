# ---------------------------------------------------------------------------
# GLXL_: Flow Register row D8-GH-XSECT-0004 (Line x Line), flowgeo_gen.py. The graph flowgeo_glxl.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: skew lines (0,0,0)-(10,0,0) and (3,5,2)-(3,-5,2): closest points (3,0,0) at 0.3 along the first and (3,0,2) at 0.5 along the second, distance 2; crossing diagonals (0,0,0)-(4,4,0) and (0,4,0)-(4,0,0): both points (2,2,0), distance 0, parameters 0.5 and 0.5; (0,0,0)-(2,2,0) against (1,-3,5)-(1,3,5): (1,1,0) at 0.5 and (1,1,5) at 2/3, distance 5; the lines are infinite: (0,0,0)-(1,0,0) against (3,-1,1)-(3,1,1): (3,0,0) at parameter 3 and (3,0,1) at 0.5, distance 1.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_glxl.dflow|g" "$HERE/flowgeo_glxl_script.txt" > "$TMPW/flowgeo_glxl_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_glxl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLXL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_glxl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLXL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GLXL_$1: $3 (measured '$got')"; else echo "FAIL GLXL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 136 node(s)" <<<"$FR_OUT"; then echo "ok   GLXL_solve: the player solved all 136 nodes (every node type is registered)"; else echo "FAIL GLXL_solve: expected 'solved 136 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 23 " <<<"$FR_OUT"; then echo "ok   GLXL_objects: exactly 23 point(s) baked"; else echo "FAIL GLXL_objects: expected 23 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the nearest point on the first line is (3, 0, 0)"
fr_chk 2 "0" "case 1: the nearest point on the second line is (3, 0, 2)"
fr_chk 3 "0" "case 1: the distance between them is 2"
fr_chk 4 "0" "case 1: the first parameter is 0.3"
fr_chk 5 "0" "case 1: the second parameter is 0.5"
fr_chk 6 "0" "case 2: the nearest point on the first line is (2, 2, 0)"
fr_chk 7 "0" "case 2: the nearest point on the second line is (2, 2, 0)"
fr_chk 8 "0" "case 2: the distance between them is 0"
fr_chk 9 "1" "case 2: the distance between them is 0 (the node under test produced the value)"
fr_chk 10 "0" "case 2: the first parameter is 0.5"
fr_chk 11 "0" "case 2: the second parameter is 0.5"
fr_chk 12 "0" "case 3: the nearest point on the first line is (1, 1, 0)"
fr_chk 13 "0" "case 3: the nearest point on the second line is (1, 1, 5)"
fr_chk 14 "0" "case 3: the distance between them is 5"
fr_chk 15 "0" "case 3: the first parameter is 0.5"
fr_chk 16 "0" "case 3: the second parameter is 0.666667"
fr_chk 17 "0" "case 4: the nearest point on the first line is (3, 0, 0)"
fr_chk 18 "0" "case 4: the nearest point on the second line is (3, 0, 1)"
fr_chk 19 "0" "case 4: the distance between them is 1"
fr_chk 20 "0" "case 4: the first parameter is 3"
fr_chk 21 "0" "case 4: the second parameter is 0.5"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
