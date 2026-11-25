# ---------------------------------------------------------------------------
# GHVUNIT_: Flow Register row D8-GH-VECTOR-0003 (Unit Vector), flowrows6_gen.py. The graph flowrows_ghvunit.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: v / |v|: (3,4,0)/5 = (0.6,0.8,0); (0,0,5)/5 = (0,0,1); (1,1,1)/sqrt 3; (-2,0,0)/2 = (-1,0,0); (1,2,2)/3 = (1/3,2/3,2/3); (0.003,0.004,0)/0.005 = (0.6,0.8,0); every unit vector has length 1; the zero vector has no direction, the node is flagged and gives nothing.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvunit.dflow|g" "$HERE/flowrows_ghvunit_script.txt" > "$TMPW/flowrows_ghvunit_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvunit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVUNIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvunit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVUNIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVUNIT_$1: $3 (measured '$got')"; else echo "FAIL GHVUNIT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVUNIT_$2"; else echo "FAIL GHVUNIT_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVUNIT_$3 (measured $n)"; else echo "FAIL GHVUNIT_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 173 node(s)" <<<"$FR_OUT"; then echo "ok   GHVUNIT_solve: the player solved all 173 nodes (every node type is registered)"; else echo "FAIL GHVUNIT_solve: expected 'solved 173 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 32 " <<<"$FR_OUT"; then echo "ok   GHVUNIT_objects: exactly 32 point(s) baked"; else echo "FAIL GHVUNIT_objects: expected 32 baked objects"; fail=1; fi
fr_chk 1 "1" "unit vector of (3,4,0): a value is there"
fr_chk 2 "0" "unit vector of (3,4,0): X = 0.6"
fr_chk 3 "0" "unit vector of (3,4,0): Y = 0.8"
fr_chk 4 "0" "unit vector of (3,4,0): Z = 0"
fr_chk 5 "0" "length of the unit vector of (3,4,0) = 1"
fr_chk 6 "1" "unit vector of (0,0,5): a value is there"
fr_chk 7 "0" "unit vector of (0,0,5): X = 0"
fr_chk 8 "0" "unit vector of (0,0,5): Y = 0"
fr_chk 9 "0" "unit vector of (0,0,5): Z = 1"
fr_chk 10 "0" "length of the unit vector of (0,0,5) = 1"
fr_chk 11 "1" "unit vector of (1,1,1): a value is there"
fr_chk 12 "0" "unit vector of (1,1,1): X = 0.5773502692"
fr_chk 13 "0" "unit vector of (1,1,1): Y = 0.5773502692"
fr_chk 14 "0" "unit vector of (1,1,1): Z = 0.5773502692"
fr_chk 15 "0" "length of the unit vector of (1,1,1) = 1"
fr_chk 16 "1" "unit vector of (-2,0,0): a value is there"
fr_chk 17 "0" "unit vector of (-2,0,0): X = -1"
fr_chk 18 "0" "unit vector of (-2,0,0): Y = 0"
fr_chk 19 "0" "unit vector of (-2,0,0): Z = 0"
fr_chk 20 "0" "length of the unit vector of (-2,0,0) = 1"
fr_chk 21 "1" "unit vector of (1,2,2): a value is there"
fr_chk 22 "0" "unit vector of (1,2,2): X = 0.3333333333"
fr_chk 23 "0" "unit vector of (1,2,2): Y = 0.6666666667"
fr_chk 24 "0" "unit vector of (1,2,2): Z = 0.6666666667"
fr_chk 25 "0" "length of the unit vector of (1,2,2) = 1"
fr_chk 26 "1" "unit vector of (0.003,0.004,0): a value is there"
fr_chk 27 "0" "unit vector of (0.003,0.004,0): X = 0.6"
fr_chk 28 "0" "unit vector of (0.003,0.004,0): Y = 0.8"
fr_chk 29 "0" "unit vector of (0.003,0.004,0): Z = 0"
fr_chk 30 "0" "length of the unit vector of (0.003,0.004,0) = 1"
fr_chk 31 "0" "the zero vector has no direction: the node gives nothing"
fr_chk 32 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Unit Vector (#163) flagged: A zero-length vector cannot be unitized" "Unit Vector #163 is flagged: A zero-length vector cannot be unitized"
fr_cnt "flagged:" 1 "flagged: exactly 1 node(s) are flagged" 
