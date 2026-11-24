# ---------------------------------------------------------------------------
# BFCOMB_: Flow Register row geometry_nodes/Combine List (Combine List), flowrows_gen.py. The graph flowrows_bfcomb.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the items come back as one list in the order they were given: 1 2 3 then 10 11 -> length 5, item 3 = 10; a single text item is a list of 1; text items 'x', 'y' -> item 1 = 'y'.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfcomb.dflow|g" "$HERE/flowrows_bfcomb_script.txt" > "$TMPW/flowrows_bfcomb_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcomb_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCOMB_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcomb_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCOMB_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCOMB_$1: $3 (measured '$got')"; else echo "FAIL BFCOMB_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 31 node(s)" <<<"$FR_OUT"; then echo "ok   BFCOMB_solve: the player solved all 31 nodes (every node type is registered)"; else echo "FAIL BFCOMB_solve: expected 'solved 31 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   BFCOMB_objects: exactly 10 point(s) baked"; else echo "FAIL BFCOMB_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "1 2 3 10 11" "the five items come out in the order they went in"
fr_chk 2 "5" "list length 5"
fr_chk 3 "0" "item 3 is 10"
fr_chk 4 "1" "one item is a list of length 1"
fr_chk 5 "0" "text items keep their order: item 1 is 'y'"
fr_chk 6 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
