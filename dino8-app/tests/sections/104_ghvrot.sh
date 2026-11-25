# ---------------------------------------------------------------------------
# GHVROT_: Flow Register row D8-GH-VECTOR-0014 (Vector Rotate), flowrows6_gen.py. The graph flowrows_ghvrot.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: right-hand rule, angles in radians: X about Z by pi/2 = Y; Y about X by pi/2 = Z; Z about Y by pi/2 = X; X about Z by pi = -X; by -pi/2 = -Y; a vector along the axis stays; 2 pi/3 about (1,1,1) permutes the axes (X -> Y, Y -> Z); the axis length is irrelevant; a full turn is the identity; the length and the component along the axis are kept (|(1,2,3)| = sqrt 14, (1,2,3).(1,-1,2) = 5); a zero axis is flagged.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvrot.dflow|g" "$HERE/flowrows_ghvrot_script.txt" > "$TMPW/flowrows_ghvrot_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvrot_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVROT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvrot_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVROT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVROT_$1: $3 (measured '$got')"; else echo "FAIL GHVROT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVROT_$2"; else echo "FAIL GHVROT_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVROT_$3 (measured $n)"; else echo "FAIL GHVROT_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 248 node(s)" <<<"$FR_OUT"; then echo "ok   GHVROT_solve: the player solved all 248 nodes (every node type is registered)"; else echo "FAIL GHVROT_solve: expected 'solved 248 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 44 " <<<"$FR_OUT"; then echo "ok   GHVROT_objects: exactly 44 point(s) baked"; else echo "FAIL GHVROT_objects: expected 44 baked objects"; fail=1; fi
fr_chk 1 "1" "X about Z by 90 degrees gives Y: a value is there"
fr_chk 2 "0" "X about Z by 90 degrees gives Y: X = 0"
fr_chk 3 "0" "X about Z by 90 degrees gives Y: Y = 1"
fr_chk 4 "0" "X about Z by 90 degrees gives Y: Z = 0"
fr_chk 5 "1" "Y about X by 90 degrees gives Z: a value is there"
fr_chk 6 "0" "Y about X by 90 degrees gives Z: X = 0"
fr_chk 7 "0" "Y about X by 90 degrees gives Z: Y = 0"
fr_chk 8 "0" "Y about X by 90 degrees gives Z: Z = 1"
fr_chk 9 "1" "Z about Y by 90 degrees gives X: a value is there"
fr_chk 10 "0" "Z about Y by 90 degrees gives X: X = 1"
fr_chk 11 "0" "Z about Y by 90 degrees gives X: Y = 0"
fr_chk 12 "0" "Z about Y by 90 degrees gives X: Z = 0"
fr_chk 13 "1" "X about Z by 180 degrees gives -X: a value is there"
fr_chk 14 "0" "X about Z by 180 degrees gives -X: X = -1"
fr_chk 15 "0" "X about Z by 180 degrees gives -X: Y = 0"
fr_chk 16 "0" "X about Z by 180 degrees gives -X: Z = 0"
fr_chk 17 "1" "X about Z by -90 degrees gives -Y (clockwise): a value is there"
fr_chk 18 "0" "X about Z by -90 degrees gives -Y (clockwise): X = 0"
fr_chk 19 "0" "X about Z by -90 degrees gives -Y (clockwise): Y = -1"
fr_chk 20 "0" "X about Z by -90 degrees gives -Y (clockwise): Z = 0"
fr_chk 21 "1" "a vector along the axis does not move: a value is there"
fr_chk 22 "0" "a vector along the axis does not move: X = 1"
fr_chk 23 "0" "a vector along the axis does not move: Y = 0"
fr_chk 24 "0" "a vector along the axis does not move: Z = 0"
fr_chk 25 "1" "120 degrees about (1,1,1) permutes the axes: X -> Y: a value is there"
fr_chk 26 "0" "120 degrees about (1,1,1) permutes the axes: X -> Y: X = 0"
fr_chk 27 "0" "120 degrees about (1,1,1) permutes the axes: X -> Y: Y = 1"
fr_chk 28 "0" "120 degrees about (1,1,1) permutes the axes: X -> Y: Z = 0"
fr_chk 29 "1" "120 degrees about (1,1,1): Y -> Z: a value is there"
fr_chk 30 "0" "120 degrees about (1,1,1): Y -> Z: X = 0"
fr_chk 31 "0" "120 degrees about (1,1,1): Y -> Z: Y = 0"
fr_chk 32 "0" "120 degrees about (1,1,1): Y -> Z: Z = 1"
fr_chk 33 "1" "the axis length does not matter: (0,0,5) acts as Z: a value is there"
fr_chk 34 "0" "the axis length does not matter: (0,0,5) acts as Z: X = 0"
fr_chk 35 "0" "the axis length does not matter: (0,0,5) acts as Z: Y = 1"
fr_chk 36 "0" "the axis length does not matter: (0,0,5) acts as Z: Z = 0"
fr_chk 37 "1" "a full turn gives the vector back: a value is there"
fr_chk 38 "0" "a full turn gives the vector back: X = 1"
fr_chk 39 "0" "a full turn gives the vector back: Y = 2"
fr_chk 40 "0" "a full turn gives the vector back: Z = 3"
fr_chk 41 "0" "rotation keeps the length sqrt(1 + 4 + 9)"
fr_chk 42 "0" "and the component along the axis: (1,2,3) . (1,-1,2) = 5"
fr_chk 43 "0" "a zero-length axis has no direction: nothing comes out"
fr_chk 44 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Vector Rotate (#237) flagged: A zero-length axis has no direction" "Vector Rotate #237 is flagged: A zero-length axis has no direction"
fr_cnt "flagged:" 1 "flagged: exactly 1 node(s) are flagged" 
