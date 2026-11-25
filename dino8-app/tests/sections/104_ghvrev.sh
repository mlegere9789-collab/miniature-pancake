# ---------------------------------------------------------------------------
# GHVREV_: Flow Register row D8-GH-VECTOR-0013 (Vector Reverse), flowrows6_gen.py. The graph flowrows_ghvrev.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: every component changes sign: (3,-4,12) -> (-3,4,-12) with the same length 13; reversing twice returns the vector; v + reverse(v) = 0; the zero vector stays zero; (0.5,-0.25,8) -> (-0.5,0.25,-8).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvrev.dflow|g" "$HERE/flowrows_ghvrev_script.txt" > "$TMPW/flowrows_ghvrev_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvrev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvrev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVREV_$1: $3 (measured '$got')"; else echo "FAIL GHVREV_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVREV_$2"; else echo "FAIL GHVREV_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVREV_$3 (measured $n)"; else echo "FAIL GHVREV_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 113 node(s)" <<<"$FR_OUT"; then echo "ok   GHVREV_solve: the player solved all 113 nodes (every node type is registered)"; else echo "FAIL GHVREV_solve: expected 'solved 113 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 22 " <<<"$FR_OUT"; then echo "ok   GHVREV_objects: exactly 22 point(s) baked"; else echo "FAIL GHVREV_objects: expected 22 baked objects"; fail=1; fi
fr_chk 1 "1" "reverse of (3,-4,12): a value is there"
fr_chk 2 "0" "reverse of (3,-4,12): X = -3"
fr_chk 3 "0" "reverse of (3,-4,12): Y = 4"
fr_chk 4 "0" "reverse of (3,-4,12): Z = -12"
fr_chk 5 "13" "the reversed vector keeps its length 13"
fr_chk 6 "1" "reversing twice gives the vector back: a value is there"
fr_chk 7 "0" "reversing twice gives the vector back: X = 3"
fr_chk 8 "0" "reversing twice gives the vector back: Y = -4"
fr_chk 9 "0" "reversing twice gives the vector back: Z = 12"
fr_chk 10 "1" "v + reverse(v) = 0: a value is there"
fr_chk 11 "0" "v + reverse(v) = 0: X = 0"
fr_chk 12 "0" "v + reverse(v) = 0: Y = 0"
fr_chk 13 "0" "v + reverse(v) = 0: Z = 0"
fr_chk 14 "1" "reverse of the zero vector is the zero vector: a value is there"
fr_chk 15 "0" "reverse of the zero vector is the zero vector: X = 0"
fr_chk 16 "0" "reverse of the zero vector is the zero vector: Y = 0"
fr_chk 17 "0" "reverse of the zero vector is the zero vector: Z = 0"
fr_chk 18 "1" "reverse of (0.5,-0.25,8): a value is there"
fr_chk 19 "0" "reverse of (0.5,-0.25,8): X = -0.5"
fr_chk 20 "0" "reverse of (0.5,-0.25,8): Y = 0.25"
fr_chk 21 "0" "reverse of (0.5,-0.25,8): Z = -8"
fr_chk 22 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
