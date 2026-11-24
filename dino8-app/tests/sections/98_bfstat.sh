# ---------------------------------------------------------------------------
# BFSTAT_: Flow Register row geometry_nodes/Attribute Statistic (Attribute Statistic), flowrows_gen.py. The graph flowrows_bfstat.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: values 2 5 8 11 14: sum 40, mean 8, median 8 (the middle of 5), min 2, max 14, range 12, variance (36 + 9 + 0 + 9 + 36) / 5 = 18 and standard deviation sqrt 18; values 1 2 3 4: mean 2.5, sum 10, range 3; values -3 7 -1 5: min -3, max 7, sum 8, mean 2, range 10; one value has variance 0. RECALLED, needs owner check (not independent): the upper-middle median of an even count (3 for 1 2 3 4) and that the variance divides by n rather than n - 1.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfstat.dflow" > "$TMPW/flowrows_bfstat.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfstat.dflow|g" "$HERE/flowrows_bfstat_script.txt" > "$TMPW/flowrows_bfstat_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfstat_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSTAT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfstat_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSTAT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSTAT_$1: $3 (measured '$got')"; else echo "FAIL BFSTAT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 120 node(s)" <<<"$FR_OUT"; then echo "ok   BFSTAT_solve: the player solved all 120 nodes (every node type is registered)"; else echo "FAIL BFSTAT_solve: expected 'solved 120 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 21 " <<<"$FR_OUT"; then echo "ok   BFSTAT_objects: exactly 21 point(s) baked"; else echo "FAIL BFSTAT_objects: expected 21 baked objects"; fail=1; fi
fr_chk 1 "0" "mean 40 / 5"
fr_chk 2 "0" "median of 5 values is the middle one"
fr_chk 3 "0" "sum"
fr_chk 4 "0" "min"
fr_chk 5 "0" "max"
fr_chk 6 "0" "range 14 - 2"
fr_chk 7 "0" "variance (36 + 9 + 0 + 9 + 36) / 5 (over n)"
fr_chk 8 "0" "standard deviation sqrt 18"
fr_chk 9 "0" "1 2 3 4: mean 2.5"
fr_chk 10 "0" "sum 10"
fr_chk 11 "0" "range 3"
fr_chk 12 "0" "median of an even count: the upper middle value (recalled, needs owner check)"
fr_chk 13 "0" "negative min"
fr_chk 14 "0" "max"
fr_chk 15 "0" "sum"
fr_chk 16 "0" "mean"
fr_chk 17 "0" "range"
fr_chk 18 "0" "one value: mean"
fr_chk 19 "0" "variance 0"
fr_chk 20 "0" "range 0"
fr_chk 21 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
