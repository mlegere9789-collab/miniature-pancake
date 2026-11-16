# ---------------------------------------------------------------------------
# R2INSERT_: Flow Register row D8-GH-SETS-0014 (Insert Items), flowrows_gen.py. The graph flowrows_r2insert.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: list 0 1 2: item 9 at index 0 goes in front of everything = 9 0 1 2; at index 1 = 0 9 1 2; at index 3 (the length) appends = 0 1 2 9; items 8,9 at indices 0,3 = 8 0 1 2 9; index 7 past the end is clamped to an append = 0 1 2 9 (4 items).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2insert.dflow|g" "$HERE/flowrows_r2insert_script.txt" > "$TMPW/flowrows_r2insert_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2insert_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2INSERT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2insert_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2INSERT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2INSERT_$1: $3 (measured '$got')"; else echo "FAIL R2INSERT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 26 node(s)" <<<"$FR_OUT"; then echo "ok   R2INSERT_solve: the player solved all 26 nodes (every node type is registered)"; else echo "FAIL R2INSERT_solve: expected 'solved 26 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 22 " <<<"$FR_OUT"; then echo "ok   R2INSERT_objects: exactly 22 point(s) baked"; else echo "FAIL R2INSERT_objects: expected 22 baked objects"; fail=1; fi
fr_chk 1 "9 0 1 2" "insert at 0 prepends"
fr_chk 2 "0 9 1 2" "insert at 1 goes between 0 and 1"
fr_chk 3 "0 1 2 9" "insert at the length appends"
fr_chk 4 "8 0 1 2 9" "items 8,9 at indices 0 and 3"
fr_chk 5 "0 1 2 9" "an index past the end appends"
fr_chk 6 "4" "the result has 4 items"
