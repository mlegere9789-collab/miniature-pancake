# ---------------------------------------------------------------------------
# BFREV_: Flow Register row geometry_nodes/Reverse Curve (Reverse Curve), flowrows_gen.py. The graph flowrows_bfrev.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the point order is reversed: line (0,0,0)->(0,0,10) starts at (0,0,10) heading -Z and ends at (0,0,0); the rectangle (3,2) (-3,2) (-3,-2) (3,-2) becomes (3,-2) (-3,-2) (-3,2) (3,2): starts at (3,-2) heading -X, length 20, 4 cyclic points; the length of a reversed open curve does not change.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfrev.dflow|g" "$HERE/flowrows_bfrev_script.txt" > "$TMPW/flowrows_bfrev_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfrev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfrev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFREV_$1: $3 (measured '$got')"; else echo "FAIL BFREV_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 134 node(s)" <<<"$FR_OUT"; then echo "ok   BFREV_solve: the player solved all 134 nodes (every node type is registered)"; else echo "FAIL BFREV_solve: expected 'solved 134 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 24 " <<<"$FR_OUT"; then echo "ok   BFREV_objects: exactly 24 point(s) baked"; else echo "FAIL BFREV_objects: expected 24 baked objects"; fail=1; fi
fr_chk 1 "0" "reversed line starts at (0,0,10) heading -Z: position X"
fr_chk 2 "0" "reversed line starts at (0,0,10) heading -Z: position Y"
fr_chk 3 "0" "reversed line starts at (0,0,10) heading -Z: position Z"
fr_chk 4 "0" "reversed line starts at (0,0,10) heading -Z: tangent X"
fr_chk 5 "0" "reversed line starts at (0,0,10) heading -Z: tangent Y"
fr_chk 6 "0" "reversed line starts at (0,0,10) heading -Z: tangent Z"
fr_chk 7 "0" "and ends at the old start: position X"
fr_chk 8 "0" "and ends at the old start: position Y"
fr_chk 9 "0" "and ends at the old start: position Z"
fr_chk 10 "2" "still a 2-point line:: 2 points"
fr_chk 11 "1" "still a 2-point line:: 1 splines"
fr_chk 12 "0" "reversed rectangle starts at the old last corner (3,-2) heading -X: position X"
fr_chk 13 "0" "reversed rectangle starts at the old last corner (3,-2) heading -X: position Y"
fr_chk 14 "0" "reversed rectangle starts at the old last corner (3,-2) heading -X: position Z"
fr_chk 15 "0" "reversed rectangle starts at the old last corner (3,-2) heading -X: tangent X"
fr_chk 16 "0" "reversed rectangle starts at the old last corner (3,-2) heading -X: tangent Y"
fr_chk 17 "0" "reversed rectangle starts at the old last corner (3,-2) heading -X: tangent Z"
fr_chk 18 "4" "still cyclic with 4 points:: 4 points"
fr_chk 19 "0" "and still length 20"
fr_chk 20 "0" "a reversed curve has the same length"
fr_chk 21 "0" "its first point is the old end (1,0,0): position X"
fr_chk 22 "0" "its first point is the old end (1,0,0): position Y"
fr_chk 23 "0" "its first point is the old end (1,0,0): position Z"
fr_chk 24 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
