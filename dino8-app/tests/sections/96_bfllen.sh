# ---------------------------------------------------------------------------
# BFLLEN_: Flow Register row geometry_nodes/List Length (List Length), flowrows_gen.py. The graph flowrows_bfllen.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the item count: Series of 7 -> 7, of 1 -> 1; 3 items with all 3 indices culled -> 0; 12 items with 4 culled -> 8.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfllen.dflow|g" "$HERE/flowrows_bfllen_script.txt" > "$TMPW/flowrows_bfllen_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfllen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFLLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfllen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFLLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFLLEN_$1: $3 (measured '$got')"; else echo "FAIL BFLLEN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 20 node(s)" <<<"$FR_OUT"; then echo "ok   BFLLEN_solve: the player solved all 20 nodes (every node type is registered)"; else echo "FAIL BFLLEN_solve: expected 'solved 20 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 4 " <<<"$FR_OUT"; then echo "ok   BFLLEN_objects: exactly 4 point(s) baked"; else echo "FAIL BFLLEN_objects: expected 4 baked objects"; fail=1; fi
fr_chk 1 "7" "Series of 7 items has length 7"
fr_chk 2 "1" "Series of 1 items has length 1"
fr_chk 3 "0" "culling all three items leaves an empty list of length 0"
fr_chk 4 "8" "12 items minus 4 culled = 8"
