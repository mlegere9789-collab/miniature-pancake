# ---------------------------------------------------------------------------
# R2VECXYZ_: Flow Register row D8-GH-VECTOR-0001 (Vector XYZ), flowrows_gen.py. The graph flowrows_r2vecxyz.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: components pass through: (3,4,0) -> 3 4 0; its length sqrt(3^2 + 4^2) = 5; (1,2,2) -> sqrt(1 + 4 + 4) = 3; (-2,3,6) -> sqrt(4 + 9 + 36) = 7; (0,0,0) -> 0; (1,1,1) -> sqrt 3.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2vecxyz.dflow|g" "$HERE/flowrows_r2vecxyz_script.txt" > "$TMPW/flowrows_r2vecxyz_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2vecxyz_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2VECXYZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2vecxyz_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2VECXYZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2VECXYZ_$1: $3 (measured '$got')"; else echo "FAIL R2VECXYZ_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 29 node(s)" <<<"$FR_OUT"; then echo "ok   R2VECXYZ_solve: the player solved all 29 nodes (every node type is registered)"; else echo "FAIL R2VECXYZ_solve: expected 'solved 29 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 9 " <<<"$FR_OUT"; then echo "ok   R2VECXYZ_objects: exactly 9 point(s) baked"; else echo "FAIL R2VECXYZ_objects: expected 9 baked objects"; fail=1; fi
fr_chk 1 "3" "X of (3,4,0) is 3"
fr_chk 2 "4" "Y of (3,4,0) is 4"
fr_chk 3 "0" "Z of (3,4,0) is 0"
fr_chk 4 "5" "length of (3,4,0) is 5"
fr_chk 5 "3" "length of (1,2,2) is 3"
fr_chk 6 "7" "length of (-2,3,6) is 7"
fr_chk 7 "0" "length of the zero vector is 0"
fr_chk 8 "0" "length of (1,1,1) is sqrt 3"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
