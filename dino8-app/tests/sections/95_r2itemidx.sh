# ---------------------------------------------------------------------------
# R2ITEMIDX_: Flow Register row D8-GH-SETS-0020 (Item Index), flowrows_gen.py. The graph flowrows_r2itemidx.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: in 10 20 30 40 (indices 0..3): 30 is at index 2, 10 at 0, 40 at 3, 25 is absent (-1); in 7 7 7 7 the first match 0 is returned; in -2 -1 0 1 2 the value 0 is at index 2.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2itemidx.dflow|g" "$HERE/flowrows_r2itemidx_script.txt" > "$TMPW/flowrows_r2itemidx_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2itemidx_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2ITEMIDX_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2itemidx_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2ITEMIDX_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2ITEMIDX_$1: $3 (measured '$got')"; else echo "FAIL R2ITEMIDX_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 21 node(s)" <<<"$FR_OUT"; then echo "ok   R2ITEMIDX_solve: the player solved all 21 nodes (every node type is registered)"; else echo "FAIL R2ITEMIDX_solve: expected 'solved 21 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 6 " <<<"$FR_OUT"; then echo "ok   R2ITEMIDX_objects: exactly 6 point(s) baked"; else echo "FAIL R2ITEMIDX_objects: expected 6 baked objects"; fail=1; fi
fr_chk 1 "2" "30 is at index 2"
fr_chk 2 "0" "10 is at index 0"
fr_chk 3 "3" "40 is at index 3"
fr_chk 4 "-1" "25 is not in the list"
fr_chk 5 "0" "the first of equal items is found"
fr_chk 6 "2" "0 in -2..2 is at index 2"
