# ---------------------------------------------------------------------------
# FRMASSADD_: Flow Register row D8-GH-MATHS-0017 (Mass Addition), flowrows_gen.py. The graph flowrows_frmassadd.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: sum 1..100 = 100*101/2 = 5050; partial sums of 1,2,3,4 = 1,3,6,10.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frmassadd.dflow|g" "$HERE/flowrows_frmassadd_script.txt" > "$TMPW/flowrows_frmassadd_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frmassadd_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRMASSADD_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frmassadd_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRMASSADD_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRMASSADD_$1: $3 (measured '$got')"; else echo "FAIL FRMASSADD_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 8 node(s)" <<<"$FR_OUT"; then echo "ok   FRMASSADD_solve: the player solved all 8 nodes (every node type is registered)"; else echo "FAIL FRMASSADD_solve: expected 'solved 8 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 5 " <<<"$FR_OUT"; then echo "ok   FRMASSADD_objects: exactly 5 point(s) baked"; else echo "FAIL FRMASSADD_objects: expected 5 baked objects"; fail=1; fi
fr_chk 1 "5050" "sum of 1..100 = 5050"
fr_chk 2 "1 3 6 10" "partial sums of 1,2,3,4 = 1,3,6,10"
