# ---------------------------------------------------------------------------
# FRRELDIFF_: Flow Register row D8-GH-MATHS-0024 (Relative Differences), flowrows_gen.py. The graph flowrows_frreldiff.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: {1,4,9} -> {3,5}; {10,7,4,1} -> {-3,-3,-3}.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frreldiff.dflow|g" "$HERE/flowrows_frreldiff_script.txt" > "$TMPW/flowrows_frreldiff_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frreldiff_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRRELDIFF_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frreldiff_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRRELDIFF_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRRELDIFF_$1: $3 (measured '$got')"; else echo "FAIL FRRELDIFF_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 9 node(s)" <<<"$FR_OUT"; then echo "ok   FRRELDIFF_solve: the player solved all 9 nodes (every node type is registered)"; else echo "FAIL FRRELDIFF_solve: expected 'solved 9 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 5 " <<<"$FR_OUT"; then echo "ok   FRRELDIFF_objects: exactly 5 point(s) baked"; else echo "FAIL FRRELDIFF_objects: expected 5 baked objects"; fail=1; fi
fr_chk 1 "3 5" "differences of 1,4,9 = 3,5"
fr_chk 2 "-3 -3 -3" "differences of 10,7,4,1 = -3,-3,-3"
