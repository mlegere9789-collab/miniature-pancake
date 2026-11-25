# ---------------------------------------------------------------------------
# R2REPLACE_: Flow Register row D8-GH-SETS-0013 (Replace Items), flowrows_gen.py. The graph flowrows_r2replace.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 0..4 with items 100,200 put at indices 1,3 = 0 100 2 200 4; a single item 7 for both indices = 0 7 2 7 4; index 5 is outside the list and ignored without wrap = 0 1 2 3 4; with wrap 5 mod 5 = 0 so item 0 becomes 9 = 9 1 2 3 4.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2replace.dflow|g" "$HERE/flowrows_r2replace_script.txt" > "$TMPW/flowrows_r2replace_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2replace_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2REPLACE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2replace_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2REPLACE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2REPLACE_$1: $3 (measured '$got')"; else echo "FAIL R2REPLACE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 18 node(s)" <<<"$FR_OUT"; then echo "ok   R2REPLACE_solve: the player solved all 18 nodes (every node type is registered)"; else echo "FAIL R2REPLACE_solve: expected 'solved 18 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 20 " <<<"$FR_OUT"; then echo "ok   R2REPLACE_objects: exactly 20 point(s) baked"; else echo "FAIL R2REPLACE_objects: expected 20 baked objects"; fail=1; fi
fr_chk 1 "0 100 2 200 4" "items 100,200 replace indices 1,3"
fr_chk 2 "0 7 2 7 4" "one item serves both indices"
fr_chk 3 "0 1 2 3 4" "an out-of-range index is ignored without wrap"
fr_chk 4 "9 1 2 3 4" "with wrap index 5 is index 0"
