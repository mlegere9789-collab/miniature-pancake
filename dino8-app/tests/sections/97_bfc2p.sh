# ---------------------------------------------------------------------------
# BFC2P_: Flow Register row geometry_nodes/Curve to Points (Curve to Points), flowrows_gen.py. The graph flowrows_bfc2p.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Count points evenly spaced by length: line length 10, 5 points z = 0, 2.5, ... 10 with tangent (0,0,1); rectangle 6 x 4 (L = 20) with 8 points 2.5 apart (box from an independent perimeter walk), with 20 points 1 apart on every corner (box +-3, +-2); the first rectangle point (3,2) heads -X; Count 1 is the start point. The Evaluated and Length modes are untested.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfc2p.dflow|g" "$HERE/flowrows_bfc2p_script.txt" > "$TMPW/flowrows_bfc2p_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfc2p_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFC2P_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfc2p_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFC2P_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFC2P_$1: $3 (measured '$got')"; else echo "FAIL BFC2P_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 193 node(s)" <<<"$FR_OUT"; then echo "ok   BFC2P_solve: the player solved all 193 nodes (every node type is registered)"; else echo "FAIL BFC2P_solve: expected 'solved 193 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 36 " <<<"$FR_OUT"; then echo "ok   BFC2P_objects: exactly 36 point(s) baked"; else echo "FAIL BFC2P_objects: expected 36 baked objects"; fail=1; fi
fr_chk 1 "5" "5 points on the line:: 5 points"
fr_chk 2 "0" "5 points on the line:: 0 edges"
fr_chk 3 "0" "5 points on the line:: 0 faces"
fr_chk 4 "0" "both ends kept: min X"
fr_chk 5 "0" "both ends kept: min Y"
fr_chk 6 "0" "both ends kept: min Z"
fr_chk 7 "0" "both ends kept: max X"
fr_chk 8 "0" "both ends kept: max Y"
fr_chk 9 "0" "both ends kept: max Z"
fr_chk 10 "0" "tangent of the line X"
fr_chk 11 "0" "tangent of the line Y"
fr_chk 12 "0" "tangent of the line Z"
fr_chk 13 "8" "8 points 2.5 apart round the rectangle:: 8 points"
fr_chk 14 "0" "their box (independent perimeter walk): min X"
fr_chk 15 "0" "their box (independent perimeter walk): min Y"
fr_chk 16 "0" "their box (independent perimeter walk): min Z"
fr_chk 17 "0" "their box (independent perimeter walk): max X"
fr_chk 18 "0" "their box (independent perimeter walk): max Y"
fr_chk 19 "0" "their box (independent perimeter walk): max Z"
fr_chk 20 "0" "the first point (3,2) heads -X X"
fr_chk 21 "0" "the first point (3,2) heads -X Y"
fr_chk 22 "0" "the first point (3,2) heads -X Z"
fr_chk 23 "0" "20 points 1 apart land on all four corners: min X"
fr_chk 24 "0" "20 points 1 apart land on all four corners: min Y"
fr_chk 25 "0" "20 points 1 apart land on all four corners: min Z"
fr_chk 26 "0" "20 points 1 apart land on all four corners: max X"
fr_chk 27 "0" "20 points 1 apart land on all four corners: max Y"
fr_chk 28 "0" "20 points 1 apart land on all four corners: max Z"
fr_chk 29 "1" "Count 1:: 1 points"
fr_chk 30 "0" "a single point sits at the start: min X"
fr_chk 31 "0" "a single point sits at the start: min Y"
fr_chk 32 "0" "a single point sits at the start: min Z"
fr_chk 33 "0" "a single point sits at the start: max X"
fr_chk 34 "0" "a single point sits at the start: max Y"
fr_chk 35 "0" "a single point sits at the start: max Z"
fr_chk 36 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
