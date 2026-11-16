# ---------------------------------------------------------------------------
# R2AMP_: Flow Register row D8-GH-VECTOR-0005 (Amplitude), flowrows_gen.py. The graph flowrows_r2amp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: direction kept, length set: (1,1,0) at 5 has length 5 and components 5/sqrt 2 = 3.5355339059 each; (3,4,0) at 10 = (6,8,0); (0,0,3) at -2 = (0,0,-2) (a negative amplitude reverses).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2amp.dflow|g" "$HERE/flowrows_r2amp_script.txt" > "$TMPW/flowrows_r2amp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2amp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2AMP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2amp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2AMP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2AMP_$1: $3 (measured '$got')"; else echo "FAIL R2AMP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 55 node(s)" <<<"$FR_OUT"; then echo "ok   R2AMP_solve: the player solved all 55 nodes (every node type is registered)"; else echo "FAIL R2AMP_solve: expected 'solved 55 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 11 " <<<"$FR_OUT"; then echo "ok   R2AMP_objects: exactly 11 point(s) baked"; else echo "FAIL R2AMP_objects: expected 11 baked objects"; fail=1; fi
fr_chk 1 "0" "length of (1,1,0) at amplitude 5 is 5"
fr_chk 2 "0" "X is 5/sqrt 2"
fr_chk 3 "0" "Y is 5/sqrt 2"
fr_chk 4 "0" "Z is 0"
fr_chk 5 "0" "(3,4,0) at 10 has X 6"
fr_chk 6 "0" "Y 8"
fr_chk 7 "0" "Z 0"
fr_chk 8 "0" "(0,0,3) at -2 has X 0"
fr_chk 9 "0" "Y 0"
fr_chk 10 "0" "Z -2"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
