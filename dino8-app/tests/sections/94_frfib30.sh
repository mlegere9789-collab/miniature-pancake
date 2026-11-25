# ---------------------------------------------------------------------------
# FRFIB30_: Flow Register row D8-GH-MATHS-0099 (Fibonacci (nth term)), flowrows_gen.py. The graph flowrows_frfib30.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: F(30) = 832040; F(10) = 55 (F(0)=0, F(1)=1).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frfib30.dflow|g" "$HERE/flowrows_frfib30_script.txt" > "$TMPW/flowrows_frfib30_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frfib30_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRFIB30_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frfib30_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRFIB30_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRFIB30_$1: $3 (measured '$got')"; else echo "FAIL FRFIB30_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 8 node(s)" <<<"$FR_OUT"; then echo "ok   FRFIB30_solve: the player solved all 8 nodes (every node type is registered)"; else echo "FAIL FRFIB30_solve: expected 'solved 8 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 2 " <<<"$FR_OUT"; then echo "ok   FRFIB30_objects: exactly 2 point(s) baked"; else echo "FAIL FRFIB30_objects: expected 2 baked objects"; fail=1; fi
fr_chk 1 "832040" "F(30) = 832040"
fr_chk 2 "55" "F(10) = 55"
