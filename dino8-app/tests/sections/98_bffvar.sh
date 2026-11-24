# ---------------------------------------------------------------------------
# BFFVAR_: Flow Register row geometry_nodes/Field Variance (Field Variance), flowrows_gen.py. The graph flowrows_bffvar.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the sum of squared distances from the mean over n: 2 4 4 4 5 5 7 9 (mean 5) -> 32 / 8 = 4 and deviation 2; 1 3 -> 1 and 1; 1..5 -> 2 and sqrt 2; equal values -> 0. RECALLED, needs owner check (not independent): that Blender divides by n (population variance) and not by n - 1; Group ID is untested.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bffvar.dflow" > "$TMPW/flowrows_bffvar.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bffvar.dflow|g" "$HERE/flowrows_bffvar_script.txt" > "$TMPW/flowrows_bffvar_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bffvar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFVAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bffvar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFVAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFFVAR_$1: $3 (measured '$got')"; else echo "FAIL BFFVAR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 61 node(s)" <<<"$FR_OUT"; then echo "ok   BFFVAR_solve: the player solved all 61 nodes (every node type is registered)"; else echo "FAIL BFFVAR_solve: expected 'solved 61 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 9 " <<<"$FR_OUT"; then echo "ok   BFFVAR_objects: exactly 9 point(s) baked"; else echo "FAIL BFFVAR_objects: expected 9 baked objects"; fail=1; fi
fr_chk 1 "0" "the textbook data 2 4 4 4 5 5 7 9 (mean 5, squared distances 9 1 1 1 0 0 4 16 = 32): variance 32 / 8 = 4"
fr_chk 2 "0" "standard deviation 2"
fr_chk 3 "0" "equal values: variance 0"
fr_chk 4 "0" "deviation 0"
fr_chk 5 "0" "1 and 3: mean 2, variance (1 + 1) / 2 = 1"
fr_chk 6 "0" "deviation 1"
fr_chk 7 "0" "1..5: variance (4 + 1 + 0 + 1 + 4) / 5 = 2"
fr_chk 8 "0" "deviation sqrt 2"
fr_chk 9 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
