# ---------------------------------------------------------------------------
# FRFIBSEQ_: Flow Register row D8-GH-SETS-0075 (Fibonacci (sequence)), flowrows_gen.py. The graph flowrows_frfibseq.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: first 10 terms 0 1 1 2 3 5 8 13 21 34.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frfibseq.dflow|g" "$HERE/flowrows_frfibseq_script.txt" > "$TMPW/flowrows_frfibseq_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frfibseq_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRFIBSEQ_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frfibseq_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRFIBSEQ_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRFIBSEQ_$1: $3 (measured '$got')"; else echo "FAIL FRFIBSEQ_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 3 node(s)" <<<"$FR_OUT"; then echo "ok   FRFIBSEQ_solve: the player solved all 3 nodes (every node type is registered)"; else echo "FAIL FRFIBSEQ_solve: expected 'solved 3 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   FRFIBSEQ_objects: exactly 10 point(s) baked"; else echo "FAIL FRFIBSEQ_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0 1 1 2 3 5 8 13 21 34" "first 10 Fibonacci terms"
