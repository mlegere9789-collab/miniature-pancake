# ---------------------------------------------------------------------------
# GHVPLCP_: Flow Register row D8-GH-VECTOR-0055 (Plane Closest Point), flowrows6_gen.py. The graph flowrows_ghvplcp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: foot of the perpendicular and signed distance along the unit normal: (3,4,7) onto z = 2 -> (3,4,2), +5; (3,4,-1) -> (3,4,2), -3; (1,2,3) onto x + y + z = 0 -> (1,2,3) - ((1+2+3)/sqrt 3) (1,1,1)/sqrt 3 = (-1,0,1), distance 6/sqrt 3 = 2 sqrt 3, (-1,0,1).(1,1,1) = 0; a point on the plane stays (distance 0); (5,-2,4) onto z = 1 -> (5,-2,1), +3.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvplcp.dflow|g" "$HERE/flowrows_ghvplcp_script.txt" > "$TMPW/flowrows_ghvplcp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvplcp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVPLCP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvplcp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVPLCP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVPLCP_$1: $3 (measured '$got')"; else echo "FAIL GHVPLCP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVPLCP_$2"; else echo "FAIL GHVPLCP_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVPLCP_$3 (measured $n)"; else echo "FAIL GHVPLCP_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 164 node(s)" <<<"$FR_OUT"; then echo "ok   GHVPLCP_solve: the player solved all 164 nodes (every node type is registered)"; else echo "FAIL GHVPLCP_solve: expected 'solved 164 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 30 " <<<"$FR_OUT"; then echo "ok   GHVPLCP_objects: exactly 30 point(s) baked"; else echo "FAIL GHVPLCP_objects: expected 30 baked objects"; fail=1; fi
fr_chk 1 "1" "(3,4,7) onto the plane z = 2: a value is there"
fr_chk 2 "0" "(3,4,7) onto the plane z = 2: X = 3"
fr_chk 3 "0" "(3,4,7) onto the plane z = 2: Y = 4"
fr_chk 4 "0" "(3,4,7) onto the plane z = 2: Z = 2"
fr_chk 5 "0" "signed distance +5 above"
fr_chk 6 "1" "(3,4,-1) onto the plane z = 2: a value is there"
fr_chk 7 "0" "(3,4,-1) onto the plane z = 2: X = 3"
fr_chk 8 "0" "(3,4,-1) onto the plane z = 2: Y = 4"
fr_chk 9 "0" "(3,4,-1) onto the plane z = 2: Z = 2"
fr_chk 10 "0" "signed distance -3 below"
fr_chk 11 "1" "(1,2,3) onto x + y + z = 0: (1,2,3) - 2 (1,1,1): a value is there"
fr_chk 12 "0" "(1,2,3) onto x + y + z = 0: (1,2,3) - 2 (1,1,1): X = -1"
fr_chk 13 "0" "(1,2,3) onto x + y + z = 0: (1,2,3) - 2 (1,1,1): Y = 0"
fr_chk 14 "0" "(1,2,3) onto x + y + z = 0: (1,2,3) - 2 (1,1,1): Z = 1"
fr_chk 15 "0" "signed distance (1 + 2 + 3) / sqrt 3 = 2 sqrt 3"
fr_chk 16 "1" "the closest point lies on the plane: (-1,0,1) . (1,1,1) = 0: a value is there"
fr_chk 17 "0" "the closest point lies on the plane: (-1,0,1) . (1,1,1) = 0"
fr_chk 18 "1" "a point on the plane stays where it is: a value is there"
fr_chk 19 "0" "a point on the plane stays where it is: X = 1"
fr_chk 20 "0" "a point on the plane stays where it is: Y = -1"
fr_chk 21 "0" "a point on the plane stays where it is: Z = 0"
fr_chk 22 "1" "distance 0: a value is there"
fr_chk 23 "0" "distance 0"
fr_chk 24 "1" "(5,-2,4) onto the plane z = 1 (normal (0,0,2) acts as Z): a value is there"
fr_chk 25 "0" "(5,-2,4) onto the plane z = 1 (normal (0,0,2) acts as Z): X = 5"
fr_chk 26 "0" "(5,-2,4) onto the plane z = 1 (normal (0,0,2) acts as Z): Y = -2"
fr_chk 27 "0" "(5,-2,4) onto the plane z = 1 (normal (0,0,2) acts as Z): Z = 1"
fr_chk 28 "0" "signed distance +3"
fr_chk 29 "0" "|p - foot| = 2 sqrt 3 = the signed distance in size"
fr_chk 30 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
