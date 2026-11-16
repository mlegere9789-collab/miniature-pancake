# ---------------------------------------------------------------------------
# BFLINE_: Flow Register row geometry_nodes/Curve Line (Curve Line), flowrows_gen.py. The graph flowrows_bfline.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: |(4,6,15) - (1,2,3)| = sqrt(3^2 + 4^2 + 12^2) = 13; Direction mode ends at Start + unit(Direction) * Length: (3,4,0)/5 * 10 = (6,8,0), (0,0,5) at 2 = (0,0,2); the default line is (0,0,0)->(0,0,1) of length 1.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfline.dflow|g" "$HERE/flowrows_bfline_script.txt" > "$TMPW/flowrows_bfline_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfline_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFLINE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfline_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFLINE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFLINE_$1: $3 (measured '$got')"; else echo "FAIL BFLINE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 137 node(s)" <<<"$FR_OUT"; then echo "ok   BFLINE_solve: the player solved all 137 nodes (every node type is registered)"; else echo "FAIL BFLINE_solve: expected 'solved 137 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 24 " <<<"$FR_OUT"; then echo "ok   BFLINE_objects: exactly 24 point(s) baked"; else echo "FAIL BFLINE_objects: expected 24 baked objects"; fail=1; fi
fr_chk 1 "0" "Points mode (1,2,3)->(4,6,15) has length sqrt(9+16+144) = 13"
fr_chk 2 "2" "a line has: 2 points"
fr_chk 3 "1" "a line has: 1 splines"
fr_chk 4 "0" "box of the line: min X"
fr_chk 5 "0" "box of the line: min Y"
fr_chk 6 "0" "box of the line: min Z"
fr_chk 7 "0" "box of the line: max X"
fr_chk 8 "0" "box of the line: max Y"
fr_chk 9 "0" "box of the line: max Z"
fr_chk 10 "0" "Direction mode (3,4,0) is made unit length first: Length 10"
fr_chk 11 "0" "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0): min X"
fr_chk 12 "0" "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0): min Y"
fr_chk 13 "0" "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0): min Z"
fr_chk 14 "0" "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0): max X"
fr_chk 15 "0" "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0): max Y"
fr_chk 16 "0" "Direction mode ends at 10 * (0.6, 0.8, 0) = (6,8,0): max Z"
fr_chk 17 "0" "Direction (0,0,5) with Length 2 ends at (0,0,2): min X"
fr_chk 18 "0" "Direction (0,0,5) with Length 2 ends at (0,0,2): min Y"
fr_chk 19 "0" "Direction (0,0,5) with Length 2 ends at (0,0,2): min Z"
fr_chk 20 "0" "Direction (0,0,5) with Length 2 ends at (0,0,2): max X"
fr_chk 21 "0" "Direction (0,0,5) with Length 2 ends at (0,0,2): max Y"
fr_chk 22 "0" "Direction (0,0,5) with Length 2 ends at (0,0,2): max Z"
fr_chk 23 "0" "the default line runs (0,0,0)->(0,0,1)"
fr_chk 24 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
