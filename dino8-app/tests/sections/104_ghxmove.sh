# ---------------------------------------------------------------------------
# GHXMOVE_: Flow Register row D8-GH-XFORM-0010 (Move), flowrows6_gen.py. The graph flowrows_ghxmove.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: translation adds the vector: (1,2,3) + (4,5,6) = (5,7,9); + (-1,-2,-3) = (0,0,0); + 0 unchanged; (0.5,-0.25,8) + (0.25,0.75,-8) = (0.75,0.5,0); there and back is the identity; two moves add ((1,2,3) + (4,5,6) + (10,20,30) = (15,27,39)); the distance of two points moved alike stays 5 (3-4-5).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghxmove.dflow|g" "$HERE/flowrows_ghxmove_script.txt" > "$TMPW/flowrows_ghxmove_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxmove_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXMOVE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxmove_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXMOVE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHXMOVE_$1: $3 (measured '$got')"; else echo "FAIL GHXMOVE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHXMOVE_$2"; else echo "FAIL GHXMOVE_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHXMOVE_$3 (measured $n)"; else echo "FAIL GHXMOVE_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 151 node(s)" <<<"$FR_OUT"; then echo "ok   GHXMOVE_solve: the player solved all 151 nodes (every node type is registered)"; else echo "FAIL GHXMOVE_solve: expected 'solved 151 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 26 " <<<"$FR_OUT"; then echo "ok   GHXMOVE_objects: exactly 26 point(s) baked"; else echo "FAIL GHXMOVE_objects: expected 26 baked objects"; fail=1; fi
fr_chk 1 "1" "(1,2,3) moved by (4,5,6): a value is there"
fr_chk 2 "0" "(1,2,3) moved by (4,5,6): X = 5"
fr_chk 3 "0" "(1,2,3) moved by (4,5,6): Y = 7"
fr_chk 4 "0" "(1,2,3) moved by (4,5,6): Z = 9"
fr_chk 5 "1" "moved by its own negative: the origin: a value is there"
fr_chk 6 "0" "moved by its own negative: the origin: X = 0"
fr_chk 7 "0" "moved by its own negative: the origin: Y = 0"
fr_chk 8 "0" "moved by its own negative: the origin: Z = 0"
fr_chk 9 "1" "a zero motion leaves the point: a value is there"
fr_chk 10 "0" "a zero motion leaves the point: X = 1"
fr_chk 11 "0" "a zero motion leaves the point: Y = 2"
fr_chk 12 "0" "a zero motion leaves the point: Z = 3"
fr_chk 13 "1" "(0.5,-0.25,8) moved by (0.25,0.75,-8): a value is there"
fr_chk 14 "0" "(0.5,-0.25,8) moved by (0.25,0.75,-8): X = 0.75"
fr_chk 15 "0" "(0.5,-0.25,8) moved by (0.25,0.75,-8): Y = 0.5"
fr_chk 16 "0" "(0.5,-0.25,8) moved by (0.25,0.75,-8): Z = 0"
fr_chk 17 "1" "moving there and back is the identity: a value is there"
fr_chk 18 "0" "moving there and back is the identity: X = 1"
fr_chk 19 "0" "moving there and back is the identity: Y = 2"
fr_chk 20 "0" "moving there and back is the identity: Z = 3"
fr_chk 21 "1" "two moves add: (1,2,3) + (4,5,6) + (10,20,30): a value is there"
fr_chk 22 "0" "two moves add: (1,2,3) + (4,5,6) + (10,20,30): X = 15"
fr_chk 23 "0" "two moves add: (1,2,3) + (4,5,6) + (10,20,30): Y = 27"
fr_chk 24 "0" "two moves add: (1,2,3) + (4,5,6) + (10,20,30): Z = 39"
fr_chk 25 "0" "a rigid move keeps the distance 5 between (0,0,0) and (3,4,0)"
fr_chk 26 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
