# ---------------------------------------------------------------------------
# BFIDXS_: Flow Register row geometry_nodes/Index Switch (Index Switch), flowrows_gen.py. The graph flowrows_bfidxs.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Index i in 0..3 passes Item i: 10 20 30 40 for 0..3; an index outside 0..3 gives the default of the type (0, empty text). The node has four fixed items; adding, moving and removing items (node.index_switch_item_*) is untested.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfidxs.dflow|g" "$HERE/flowrows_bfidxs_script.txt" > "$TMPW/flowrows_bfidxs_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfidxs_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIDXS_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfidxs_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIDXS_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFIDXS_$1: $3 (measured '$got')"; else echo "FAIL BFIDXS_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 56 node(s)" <<<"$FR_OUT"; then echo "ok   BFIDXS_solve: the player solved all 56 nodes (every node type is registered)"; else echo "FAIL BFIDXS_solve: expected 'solved 56 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   BFIDXS_objects: exactly 10 point(s) baked"; else echo "FAIL BFIDXS_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0" "index 0 selects Item 0 = 10"
fr_chk 2 "0" "index 1 selects Item 1 = 20"
fr_chk 3 "0" "index 2 selects Item 2 = 30"
fr_chk 4 "0" "index 3 selects Item 3 = 40"
fr_chk 5 "0" "index 4 is out of range: the default 0"
fr_chk 6 "0" "index -1 is out of range: the default 0"
fr_chk 7 "0" "index 100 is out of range: the default 0"
fr_chk 8 "0" "out-of-range index on text gives the empty string"
fr_chk 9 "0" "index 1 selects 'b'"
fr_chk 10 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
