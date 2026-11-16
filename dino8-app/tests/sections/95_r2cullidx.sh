# ---------------------------------------------------------------------------
# R2CULLIDX_: Flow Register row D8-GH-SETS-0008 (Cull Index), flowrows_gen.py. The graph flowrows_r2cullidx.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 0..7 without indices 0,2,4 = 1 3 5 6 7; indices 8 and -1 without wrap are outside the list and ignored = 0..7; with wrap 8 mod 8 = 0 and -1 = 7 so items 0 and 7 go = 1 2 3 4 5 6; index 3 listed three times removes only item 3 = 0 1 2 4 5 6 7.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2cullidx.dflow|g" "$HERE/flowrows_r2cullidx_script.txt" > "$TMPW/flowrows_r2cullidx_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2cullidx_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2CULLIDX_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2cullidx_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2CULLIDX_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2CULLIDX_$1: $3 (measured '$got')"; else echo "FAIL R2CULLIDX_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 16 node(s)" <<<"$FR_OUT"; then echo "ok   R2CULLIDX_solve: the player solved all 16 nodes (every node type is registered)"; else echo "FAIL R2CULLIDX_solve: expected 'solved 16 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 26 " <<<"$FR_OUT"; then echo "ok   R2CULLIDX_objects: exactly 26 point(s) baked"; else echo "FAIL R2CULLIDX_objects: expected 26 baked objects"; fail=1; fi
fr_chk 1 "1 3 5 6 7" "culling indices 0,2,4 of 0..7 leaves 1 3 5 6 7"
fr_chk 2 "0 1 2 3 4 5 6 7" "indices 8 and -1 without wrap are ignored"
fr_chk 3 "1 2 3 4 5 6" "with wrap 8 -> 0 and -1 -> 7 are removed"
fr_chk 4 "0 1 2 4 5 6 7" "a repeated index removes the item once"
