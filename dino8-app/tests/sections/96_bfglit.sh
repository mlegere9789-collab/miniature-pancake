# ---------------------------------------------------------------------------
# BFGLIT_: Flow Register row geometry_nodes/Get List Item (Get List Item), flowrows_gen.py. The graph flowrows_bfglit.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: zero-based indexing: item 0, 1, 3 of 10 20 30 40 = 10, 20, 40; an index outside the list (4, -1, 50) gives the type's default 0; the second of two points (1,2,3), (4,5,6) is (4,5,6) and index 2 gives (0,0,0).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfglit.dflow|g" "$HERE/flowrows_bfglit_script.txt" > "$TMPW/flowrows_bfglit_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfglit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFGLIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfglit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFGLIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFGLIT_$1: $3 (measured '$got')"; else echo "FAIL BFGLIT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 80 node(s)" <<<"$FR_OUT"; then echo "ok   BFGLIT_solve: the player solved all 80 nodes (every node type is registered)"; else echo "FAIL BFGLIT_solve: expected 'solved 80 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   BFGLIT_objects: exactly 13 point(s) baked"; else echo "FAIL BFGLIT_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "0" "item 0 of 10 20 30 40 is 10"
fr_chk 2 "0" "item 1 of 10 20 30 40 is 20"
fr_chk 3 "0" "item 3 of 10 20 30 40 is 40"
fr_chk 4 "0" "index 4 is outside the list: the default 0"
fr_chk 5 "0" "index -1 is outside the list: the default 0"
fr_chk 6 "0" "index 50 is outside the list: the default 0"
fr_chk 7 "0" "item 1 of the point list (1,2,3), (4,5,6) X"
fr_chk 8 "0" "item 1 of the point list (1,2,3), (4,5,6) Y"
fr_chk 9 "0" "item 1 of the point list (1,2,3), (4,5,6) Z"
fr_chk 10 "0" "index 2 of a 2-point list is the default point X"
fr_chk 11 "0" "index 2 of a 2-point list is the default point Y"
fr_chk 12 "0" "index 2 of a 2-point list is the default point Z"
fr_chk 13 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
