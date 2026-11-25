# ---------------------------------------------------------------------------
# GHXMIRROR_: Flow Register row D8-GH-XFORM-0005 (Mirror), flowrows6_gen.py. The graph flowrows_ghxmirror.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: reflection p - 2 ((p - o) . n) n: across XY (1,2,3) -> (1,2,-3); across z = 5: z -> 2*5 - 3 = 7; across the plane with normal X: (-1,2,3); across x + y = 0: (1,0,0) -> (0,-1,0) and (1,-1,5) (on the plane) stays; twice is the identity; point and image are 2 * 3 = 6 apart.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghxmirror.dflow|g" "$HERE/flowrows_ghxmirror_script.txt" > "$TMPW/flowrows_ghxmirror_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxmirror_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXMIRROR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxmirror_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXMIRROR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHXMIRROR_$1: $3 (measured '$got')"; else echo "FAIL GHXMIRROR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHXMIRROR_$2"; else echo "FAIL GHXMIRROR_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHXMIRROR_$3 (measured $n)"; else echo "FAIL GHXMIRROR_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 150 node(s)" <<<"$FR_OUT"; then echo "ok   GHXMIRROR_solve: the player solved all 150 nodes (every node type is registered)"; else echo "FAIL GHXMIRROR_solve: expected 'solved 150 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 26 " <<<"$FR_OUT"; then echo "ok   GHXMIRROR_objects: exactly 26 point(s) baked"; else echo "FAIL GHXMIRROR_objects: expected 26 baked objects"; fail=1; fi
fr_chk 1 "1" "(1,2,3) across the XY plane: Z changes sign: a value is there"
fr_chk 2 "0" "(1,2,3) across the XY plane: Z changes sign: X = 1"
fr_chk 3 "0" "(1,2,3) across the XY plane: Z changes sign: Y = 2"
fr_chk 4 "0" "(1,2,3) across the XY plane: Z changes sign: Z = -3"
fr_chk 5 "1" "across the plane z = 5: 3 -> 2 * 5 - 3 = 7: a value is there"
fr_chk 6 "0" "across the plane z = 5: 3 -> 2 * 5 - 3 = 7: X = 1"
fr_chk 7 "0" "across the plane z = 5: 3 -> 2 * 5 - 3 = 7: Y = 2"
fr_chk 8 "0" "across the plane z = 5: 3 -> 2 * 5 - 3 = 7: Z = 7"
fr_chk 9 "1" "across the plane with normal X: x changes sign: a value is there"
fr_chk 10 "0" "across the plane with normal X: x changes sign: X = -1"
fr_chk 11 "0" "across the plane with normal X: x changes sign: Y = 2"
fr_chk 12 "0" "across the plane with normal X: x changes sign: Z = 3"
fr_chk 13 "1" "across x + y = 0: p - 2 (p . n) n = (1,0,0) - (1,1,0) = (0,-1,0): a value is there"
fr_chk 14 "0" "across x + y = 0: p - 2 (p . n) n = (1,0,0) - (1,1,0) = (0,-1,0): X = 0"
fr_chk 15 "0" "across x + y = 0: p - 2 (p . n) n = (1,0,0) - (1,1,0) = (0,-1,0): Y = -1"
fr_chk 16 "0" "across x + y = 0: p - 2 (p . n) n = (1,0,0) - (1,1,0) = (0,-1,0): Z = 0"
fr_chk 17 "1" "a point on the mirror plane stays: a value is there"
fr_chk 18 "0" "a point on the mirror plane stays: X = 1"
fr_chk 19 "0" "a point on the mirror plane stays: Y = -1"
fr_chk 20 "0" "a point on the mirror plane stays: Z = 5"
fr_chk 21 "1" "mirroring twice is the identity: a value is there"
fr_chk 22 "0" "mirroring twice is the identity: X = 1"
fr_chk 23 "0" "mirroring twice is the identity: Y = 2"
fr_chk 24 "0" "mirroring twice is the identity: Z = 3"
fr_chk 25 "0" "the point and its image are twice the distance to the plane apart: 2 * 3 = 6"
fr_chk 26 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
