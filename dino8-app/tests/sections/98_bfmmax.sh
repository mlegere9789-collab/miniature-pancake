# ---------------------------------------------------------------------------
# BFMMAX_: Flow Register row geometry_nodes/Field Min & Max (Field Min & Max), flowrows_gen.py. The graph flowrows_bfmmax.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: smallest and largest value: 4 -2 9 0.5 -> -2, 9; 3 3 3 -> 3, 3; -8 -1 -4 -> -8, -1; 1e6 2e6 1.5e6 -> 1e6, 2e6; one value is both; the series -5, -3 ... 33 -> -5 and 33.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfmmax.dflow" > "$TMPW/flowrows_bfmmax.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfmmax.dflow|g" "$HERE/flowrows_bfmmax_script.txt" > "$TMPW/flowrows_bfmmax_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmmax_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMMAX_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmmax_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMMAX_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFMMAX_$1: $3 (measured '$got')"; else echo "FAIL BFMMAX_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 82 node(s)" <<<"$FR_OUT"; then echo "ok   BFMMAX_solve: the player solved all 82 nodes (every node type is registered)"; else echo "FAIL BFMMAX_solve: expected 'solved 82 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   BFMMAX_objects: exactly 13 point(s) baked"; else echo "FAIL BFMMAX_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "0" "min of [4, -2, 9, 0.5]"
fr_chk 2 "0" "max of [4, -2, 9, 0.5]"
fr_chk 3 "0" "min of [3, 3, 3]"
fr_chk 4 "0" "max of [3, 3, 3]"
fr_chk 5 "0" "min of [-8, -1, -4]"
fr_chk 6 "0" "max of [-8, -1, -4]"
fr_chk 7 "0" "min of [1000000.0, 2000000.0, 1500000.0]"
fr_chk 8 "0" "max of [1000000.0, 2000000.0, 1500000.0]"
fr_chk 9 "0" "one value: min"
fr_chk 10 "0" "max"
fr_chk 11 "0" "series -5, -3 ... 33: min -5"
fr_chk 12 "0" "max -5 + 19 * 2 = 33"
fr_chk 13 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
