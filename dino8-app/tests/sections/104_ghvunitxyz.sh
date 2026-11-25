# ---------------------------------------------------------------------------
# GHVUNITXYZ_: Flow Register row D8-GH-VECTOR-0004 (Unit X, Unit Y, Unit Z), flowrows6_gen.py. The graph flowrows_ghvunitxyz.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the world axes scaled by the factor (default 1): X = (1,0,0), Y = (0,1,0), Z = (0,0,1); factor 2.5 on X = (2.5,0,0); factor -3 on Y = (0,-3,0); factor 0 on Z = (0,0,0); right-handed and orthonormal: X x Y = Z, Y x Z = X, X . Y = 0, Z . Z = 1.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvunitxyz.dflow|g" "$HERE/flowrows_ghvunitxyz_script.txt" > "$TMPW/flowrows_ghvunitxyz_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvunitxyz_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVUNITXYZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvunitxyz_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVUNITXYZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVUNITXYZ_$1: $3 (measured '$got')"; else echo "FAIL GHVUNITXYZ_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVUNITXYZ_$2"; else echo "FAIL GHVUNITXYZ_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVUNITXYZ_$3 (measured $n)"; else echo "FAIL GHVUNITXYZ_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 184 node(s)" <<<"$FR_OUT"; then echo "ok   GHVUNITXYZ_solve: the player solved all 184 nodes (every node type is registered)"; else echo "FAIL GHVUNITXYZ_solve: expected 'solved 184 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 36 " <<<"$FR_OUT"; then echo "ok   GHVUNITXYZ_objects: exactly 36 point(s) baked"; else echo "FAIL GHVUNITXYZ_objects: expected 36 baked objects"; fail=1; fi
fr_chk 1 "1" "Unit X: a value is there"
fr_chk 2 "0" "Unit X: X = 1"
fr_chk 3 "0" "Unit X: Y = 0"
fr_chk 4 "0" "Unit X: Z = 0"
fr_chk 5 "1" "Unit Y: a value is there"
fr_chk 6 "0" "Unit Y: X = 0"
fr_chk 7 "0" "Unit Y: Y = 1"
fr_chk 8 "0" "Unit Y: Z = 0"
fr_chk 9 "1" "Unit Z: a value is there"
fr_chk 10 "0" "Unit Z: X = 0"
fr_chk 11 "0" "Unit Z: Y = 0"
fr_chk 12 "0" "Unit Z: Z = 1"
fr_chk 13 "1" "Unit X times 2.5: a value is there"
fr_chk 14 "0" "Unit X times 2.5: X = 2.5"
fr_chk 15 "0" "Unit X times 2.5: Y = 0"
fr_chk 16 "0" "Unit X times 2.5: Z = 0"
fr_chk 17 "1" "Unit Y times -3: a value is there"
fr_chk 18 "0" "Unit Y times -3: X = 0"
fr_chk 19 "0" "Unit Y times -3: Y = -3"
fr_chk 20 "0" "Unit Y times -3: Z = 0"
fr_chk 21 "1" "Unit Z times 0: a value is there"
fr_chk 22 "0" "Unit Z times 0: X = 0"
fr_chk 23 "0" "Unit Z times 0: Y = 0"
fr_chk 24 "0" "Unit Z times 0: Z = 0"
fr_chk 25 "1" "X x Y = Z (right-handed): a value is there"
fr_chk 26 "0" "X x Y = Z (right-handed): X = 0"
fr_chk 27 "0" "X x Y = Z (right-handed): Y = 0"
fr_chk 28 "0" "X x Y = Z (right-handed): Z = 1"
fr_chk 29 "1" "Y x Z = X: a value is there"
fr_chk 30 "0" "Y x Z = X: X = 1"
fr_chk 31 "0" "Y x Z = X: Y = 0"
fr_chk 32 "0" "Y x Z = X: Z = 0"
fr_chk 33 "1" "X . Y = 0 (orthogonal): a value is there"
fr_chk 34 "0" "X . Y = 0 (orthogonal)"
fr_chk 35 "0" "Z . Z = 1 (unit length)"
fr_chk 36 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
