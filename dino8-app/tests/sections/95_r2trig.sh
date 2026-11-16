# ---------------------------------------------------------------------------
# R2TRIG_: Flow Register row D8-GH-MATHS-0030 (Sine, Cosine, Tangent), flowrows_gen.py. The graph flowrows_r2trig.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: angles in radians: sin(pi/6) = 1/2; sin 0 = 0; sin(-pi/2) = -1; cos(pi/3) = 1/2; cos 0 = 1; cos pi = -1; tan(pi/4) = 1; tan 0 = 0; tan(-pi/4) = -1.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2trig.dflow|g" "$HERE/flowrows_r2trig_script.txt" > "$TMPW/flowrows_r2trig_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2trig_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2TRIG_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2trig_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2TRIG_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2TRIG_$1: $3 (measured '$got')"; else echo "FAIL R2TRIG_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 50 node(s)" <<<"$FR_OUT"; then echo "ok   R2TRIG_solve: the player solved all 50 nodes (every node type is registered)"; else echo "FAIL R2TRIG_solve: expected 'solved 50 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   R2TRIG_objects: exactly 10 point(s) baked"; else echo "FAIL R2TRIG_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0" "sin(pi/6) = 0.5"
fr_chk 2 "0" "sin 0 = 0"
fr_chk 3 "0" "sin(-pi/2) = -1"
fr_chk 4 "0" "cos(pi/3) = 0.5"
fr_chk 5 "0" "cos 0 = 1"
fr_chk 6 "0" "cos pi = -1"
fr_chk 7 "0" "tan(pi/4) = 1"
fr_chk 8 "0" "tan 0 = 0"
fr_chk 9 "0" "tan(-pi/4) = -1"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
