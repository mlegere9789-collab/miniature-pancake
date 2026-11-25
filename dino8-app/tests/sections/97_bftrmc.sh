# ---------------------------------------------------------------------------
# BFTRMC_: Flow Register row geometry_nodes/Trim Curve (Trim Curve), flowrows_gen.py. The graph flowrows_bftrmc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: line length 10: factors 0.2..0.7 -> z 2..7 (length 5); Length mode 8..3 is swapped -> z 3..8; rectangle 6 x 4 from (3,2): s = 5 is (-2,2), s = 15 is (2,-2), the corners (-3,2) (s = 6) and (-3,-2) (s = 10) lie between: 4 points, length 10, box [-3,2] x [-2,2]; 0..1 keeps the whole line.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bftrmc.dflow|g" "$HERE/flowrows_bftrmc_script.txt" > "$TMPW/flowrows_bftrmc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bftrmc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFTRMC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bftrmc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFTRMC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFTRMC_$1: $3 (measured '$got')"; else echo "FAIL BFTRMC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 141 node(s)" <<<"$FR_OUT"; then echo "ok   BFTRMC_solve: the player solved all 141 nodes (every node type is registered)"; else echo "FAIL BFTRMC_solve: expected 'solved 141 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 26 " <<<"$FR_OUT"; then echo "ok   BFTRMC_objects: exactly 26 point(s) baked"; else echo "FAIL BFTRMC_objects: expected 26 baked objects"; fail=1; fi
fr_chk 1 "0" "factors 0.2..0.7 of length 10 keep length 5"
fr_chk 2 "0" "from z = 2 to z = 7: min X"
fr_chk 3 "0" "from z = 2 to z = 7: min Y"
fr_chk 4 "0" "from z = 2 to z = 7: min Z"
fr_chk 5 "0" "from z = 2 to z = 7: max X"
fr_chk 6 "0" "from z = 2 to z = 7: max Y"
fr_chk 7 "0" "from z = 2 to z = 7: max Z"
fr_chk 8 "0" "Length mode 8..3 is swapped to 3..8: min X"
fr_chk 9 "0" "Length mode 8..3 is swapped to 3..8: min Y"
fr_chk 10 "0" "Length mode 8..3 is swapped to 3..8: min Z"
fr_chk 11 "0" "Length mode 8..3 is swapped to 3..8: max X"
fr_chk 12 "0" "Length mode 8..3 is swapped to 3..8: max Y"
fr_chk 13 "0" "Length mode 8..3 is swapped to 3..8: max Z"
fr_chk 14 "2" "a cut inside one edge has: 2 points"
fr_chk 15 "1" "a cut inside one edge has: 1 splines"
fr_chk 16 "4" "s = 5..15 of the rectangle keeps the two corners (-3,2), (-3,-2) between the cuts (-2,2) and (2,-2):: 4 points"
fr_chk 17 "1" "s = 5..15 of the rectangle keeps the two corners (-3,2), (-3,-2) between the cuts (-2,2) and (2,-2):: 1 splines"
fr_chk 18 "0" "half of the perimeter 20"
fr_chk 19 "0" "box of (-2,2) (-3,2) (-3,-2) (2,-2): min X"
fr_chk 20 "0" "box of (-2,2) (-3,2) (-3,-2) (2,-2): min Y"
fr_chk 21 "0" "box of (-2,2) (-3,2) (-3,-2) (2,-2): min Z"
fr_chk 22 "0" "box of (-2,2) (-3,2) (-3,-2) (2,-2): max X"
fr_chk 23 "0" "box of (-2,2) (-3,2) (-3,-2) (2,-2): max Y"
fr_chk 24 "0" "box of (-2,2) (-3,2) (-3,-2) (2,-2): max Z"
fr_chk 25 "0" "0..1 keeps everything"
fr_chk 26 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
