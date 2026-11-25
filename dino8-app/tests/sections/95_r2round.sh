# ---------------------------------------------------------------------------
# R2ROUND_: Flow Register row D8-GH-MATHS-0021 (Round, Floor, Ceiling, Truncate), flowrows_gen.py. The graph flowrows_r2round.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Round: 2.5 -> 3, -2.5 -> -3 (halves away from zero), 2.4 -> 2, 2.6 -> 3, -2.4 -> -2; Floor (largest integer <= x): 2.7 -> 2, -2.3 -> -3, 4 -> 4; Ceiling (smallest integer >= x): 2.1 -> 3, -2.7 -> -2, 4 -> 4; Truncate (drop the fraction): 2.9 -> 2, -2.9 -> -2, 0.4 -> 0.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2round.dflow|g" "$HERE/flowrows_r2round_script.txt" > "$TMPW/flowrows_r2round_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2round_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2ROUND_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2round_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2ROUND_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2ROUND_$1: $3 (measured '$got')"; else echo "FAIL R2ROUND_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 75 node(s)" <<<"$FR_OUT"; then echo "ok   R2ROUND_solve: the player solved all 75 nodes (every node type is registered)"; else echo "FAIL R2ROUND_solve: expected 'solved 75 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 15 " <<<"$FR_OUT"; then echo "ok   R2ROUND_objects: exactly 15 point(s) baked"; else echo "FAIL R2ROUND_objects: expected 15 baked objects"; fail=1; fi
fr_chk 1 "0" "round 2.5 = 3"
fr_chk 2 "0" "round -2.5 = -3"
fr_chk 3 "0" "round 2.4 = 2"
fr_chk 4 "0" "round 2.6 = 3"
fr_chk 5 "0" "round -2.4 = -2"
fr_chk 6 "0" "floor 2.7 = 2"
fr_chk 7 "0" "floor -2.3 = -3"
fr_chk 8 "0" "floor 4 = 4"
fr_chk 9 "0" "ceiling 2.1 = 3"
fr_chk 10 "0" "ceiling -2.7 = -2"
fr_chk 11 "0" "ceiling 4 = 4"
fr_chk 12 "0" "truncate 2.9 = 2"
fr_chk 13 "0" "truncate -2.9 = -2"
fr_chk 14 "0" "truncate 0.4 = 0"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
