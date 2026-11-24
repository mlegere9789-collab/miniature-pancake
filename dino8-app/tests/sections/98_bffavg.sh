# ---------------------------------------------------------------------------
# BFFAVG_: Flow Register row geometry_nodes/Field Average (Field Average), flowrows_gen.py. The graph flowrows_bffavg.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: mean and median of one group: 1..5 -> 3 and 3; 9 1 5 7 3 -> 5 and 5 (the median sorts first); the 9 values 0, 0.25 ... 2 -> 1 and 1; 10 20 30 40 -> mean 25. RECALLED, needs owner check (not independent): the median of an even count is the upper middle value (30). Group ID is untested.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bffavg.dflow" > "$TMPW/flowrows_bffavg.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bffavg.dflow|g" "$HERE/flowrows_bffavg_script.txt" > "$TMPW/flowrows_bffavg_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bffavg_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFAVG_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bffavg_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFAVG_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFFAVG_$1: $3 (measured '$got')"; else echo "FAIL BFFAVG_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 62 node(s)" <<<"$FR_OUT"; then echo "ok   BFFAVG_solve: the player solved all 62 nodes (every node type is registered)"; else echo "FAIL BFFAVG_solve: expected 'solved 62 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 9 " <<<"$FR_OUT"; then echo "ok   BFFAVG_objects: exactly 9 point(s) baked"; else echo "FAIL BFFAVG_objects: expected 9 baked objects"; fail=1; fi
fr_chk 1 "0" "mean of 1..5"
fr_chk 2 "0" "median of 1..5"
fr_chk 3 "0" "mean of the unsorted 9 1 5 7 3"
fr_chk 4 "0" "median sorts first: 1 3 5 7 9"
fr_chk 5 "0" "mean of 10 20 30 40"
fr_chk 6 "0" "median of an even count: the upper middle value (recalled, needs owner check)"
fr_chk 7 "0" "mean of 0, 0.25 ... 2"
fr_chk 8 "0" "median 1"
fr_chk 9 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
