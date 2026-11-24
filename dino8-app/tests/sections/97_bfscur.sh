# ---------------------------------------------------------------------------
# BFSCUR_: Flow Register row geometry_nodes/Sample Curve (Sample Curve), flowrows_gen.py. The graph flowrows_bfscur.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: position at a fraction of the length (perimeter included for a cyclic curve): line length 10 at 0.25 = (0,0,2.5), at length 7 = (0,0,7), factor 3 is clamped to (0,0,10); rectangle 6 x 4 from (3,2) through (-3,2), (-3,-2), (3,-2) and back: s = 3 -> (0,2,0) heading -X, s = 12 -> (-1,-2,0) heading +X, s = 18 -> (3,0,0) heading +Y.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfscur.dflow|g" "$HERE/flowrows_bfscur_script.txt" > "$TMPW/flowrows_bfscur_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfscur_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSCUR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfscur_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSCUR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSCUR_$1: $3 (measured '$got')"; else echo "FAIL BFSCUR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 176 node(s)" <<<"$FR_OUT"; then echo "ok   BFSCUR_solve: the player solved all 176 nodes (every node type is registered)"; else echo "FAIL BFSCUR_solve: expected 'solved 176 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 31 " <<<"$FR_OUT"; then echo "ok   BFSCUR_objects: exactly 31 point(s) baked"; else echo "FAIL BFSCUR_objects: expected 31 baked objects"; fail=1; fi
fr_chk 1 "0" "line, factor 0.25 of length 10: position X"
fr_chk 2 "0" "line, factor 0.25 of length 10: position Y"
fr_chk 3 "0" "line, factor 0.25 of length 10: position Z"
fr_chk 4 "0" "line, factor 0.25 of length 10: tangent X"
fr_chk 5 "0" "line, factor 0.25 of length 10: tangent Y"
fr_chk 6 "0" "line, factor 0.25 of length 10: tangent Z"
fr_chk 7 "0" "Length mode 7 along the line: position X"
fr_chk 8 "0" "Length mode 7 along the line: position Y"
fr_chk 9 "0" "Length mode 7 along the line: position Z"
fr_chk 10 "0" "a factor above 1 is clamped to the end: position X"
fr_chk 11 "0" "a factor above 1 is clamped to the end: position Y"
fr_chk 12 "0" "a factor above 1 is clamped to the end: position Z"
fr_chk 13 "0" "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge: position X"
fr_chk 14 "0" "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge: position Y"
fr_chk 15 "0" "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge: position Z"
fr_chk 16 "0" "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge: tangent X"
fr_chk 17 "0" "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge: tangent Y"
fr_chk 18 "0" "rectangle (start (3,2), counter-clockwise), s = 3 on the top edge: tangent Z"
fr_chk 19 "0" "s = 12 on the bottom edge: position X"
fr_chk 20 "0" "s = 12 on the bottom edge: position Y"
fr_chk 21 "0" "s = 12 on the bottom edge: position Z"
fr_chk 22 "0" "s = 12 on the bottom edge: tangent X"
fr_chk 23 "0" "s = 12 on the bottom edge: tangent Y"
fr_chk 24 "0" "s = 12 on the bottom edge: tangent Z"
fr_chk 25 "0" "s = 18 on the closing right edge: position X"
fr_chk 26 "0" "s = 18 on the closing right edge: position Y"
fr_chk 27 "0" "s = 18 on the closing right edge: position Z"
fr_chk 28 "0" "s = 18 on the closing right edge: tangent X"
fr_chk 29 "0" "s = 18 on the closing right edge: tangent Y"
fr_chk 30 "0" "s = 18 on the closing right edge: tangent Z"
fr_chk 31 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
