# ---------------------------------------------------------------------------
# R2VECLEN_: Flow Register row D8-GH-VECTOR-0006 (Vector Length), flowrows_gen.py. The graph flowrows_r2veclen.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: sqrt(x^2 + y^2 + z^2): (3,4,0) = 5; (3,4,12) = sqrt(169) = 13; (-1,-2,-2) = sqrt 9 = 3; (0,0,0) = 0; (0,0,-7) = 7; (1,1,0) = sqrt 2.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2veclen.dflow|g" "$HERE/flowrows_r2veclen_script.txt" > "$TMPW/flowrows_r2veclen_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2veclen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2VECLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2veclen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2VECLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2VECLEN_$1: $3 (measured '$got')"; else echo "FAIL R2VECLEN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 41 node(s)" <<<"$FR_OUT"; then echo "ok   R2VECLEN_solve: the player solved all 41 nodes (every node type is registered)"; else echo "FAIL R2VECLEN_solve: expected 'solved 41 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 7 " <<<"$FR_OUT"; then echo "ok   R2VECLEN_objects: exactly 7 point(s) baked"; else echo "FAIL R2VECLEN_objects: expected 7 baked objects"; fail=1; fi
fr_chk 1 "0" "|(3,4,0)| = 5"
fr_chk 2 "0" "|(3,4,12)| = 13"
fr_chk 3 "0" "|(-1,-2,-2)| = 3"
fr_chk 4 "0" "|0| = 0"
fr_chk 5 "0" "|(0,0,-7)| = 7"
fr_chk 6 "0" "|(1,1,0)| = sqrt 2"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
