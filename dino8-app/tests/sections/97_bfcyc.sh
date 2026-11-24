# ---------------------------------------------------------------------------
# BFCYC_: Flow Register row geometry_nodes/Set Spline Cyclic (Set Spline Cyclic), flowrows_gen.py. The graph flowrows_bfcyc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: cyclic adds the closing edge from the last point to the first: 2-point line of length 4 -> 8, 5-point Bezier + chord 2; not cyclic removes it: rectangle 20 - 4 = 16, 8-gon of radius 1 = 7 * 2 sin(pi/8); the points never change; the default is not cyclic.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfcyc.dflow|g" "$HERE/flowrows_bfcyc_script.txt" > "$TMPW/flowrows_bfcyc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcyc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCYC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcyc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCYC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCYC_$1: $3 (measured '$got')"; else echo "FAIL BFCYC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 60 node(s)" <<<"$FR_OUT"; then echo "ok   BFCYC_solve: the player solved all 60 nodes (every node type is registered)"; else echo "FAIL BFCYC_solve: expected 'solved 60 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 11 " <<<"$FR_OUT"; then echo "ok   BFCYC_objects: exactly 11 point(s) baked"; else echo "FAIL BFCYC_objects: expected 11 baked objects"; fail=1; fi
fr_chk 1 "2" "a cyclic 2-point line still has: 2 points"
fr_chk 2 "1" "a cyclic 2-point line still has: 1 splines"
fr_chk 3 "0" "and its closing edge doubles the length: 4 + 4"
fr_chk 4 "4" "opening the rectangle keeps: 4 points"
fr_chk 5 "0" "but drops the closing edge: 20 - 4 = 16"
fr_chk 6 "0" "open 8-gon of radius 1: 7 chords of 2 sin(pi/8)"
fr_chk 7 "0" "closing the 5-point Bezier adds the chord (1,0,0) -> (-1,0,0) of length 2"
fr_chk 8 "5" "and keeps its 5 points:: 5 points"
fr_chk 9 "1" "and keeps its 5 points:: 1 splines"
fr_chk 10 "0" "the default Cyclic is false"
fr_chk 11 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
