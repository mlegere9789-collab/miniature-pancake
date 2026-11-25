# ---------------------------------------------------------------------------
# GHTREV_: Flow Register row D8-GH-TREES-0012 (Reverse parameter modifier), flowrows6_gen.py. The graph flowrows_ghtrev.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: reverse turns each branch's list back to front: 1 2 3 4 5 -> 5 4 3 2 1; two branches 0 1 2 | 3 4 5 -> 2 1 0 | 5 4 3 (the branches keep their order); it is a property of one input: Merge(1..5, reversed 10 20 30) = 1 2 3 4 5 30 20 10.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghtrev.dflow|g" "$HERE/flowrows_ghtrev_script.txt" > "$TMPW/flowrows_ghtrev_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtrev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtrev_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTREV_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHTREV_$1: $3 (measured '$got')"; else echo "FAIL GHTREV_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHTREV_$2"; else echo "FAIL GHTREV_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHTREV_$3 (measured $n)"; else echo "FAIL GHTREV_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 25 node(s)" <<<"$FR_OUT"; then echo "ok   GHTREV_solve: the player solved all 25 nodes (every node type is registered)"; else echo "FAIL GHTREV_solve: expected 'solved 25 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 31 " <<<"$FR_OUT"; then echo "ok   GHTREV_objects: exactly 31 point(s) baked"; else echo "FAIL GHTREV_objects: expected 31 baked objects"; fail=1; fi
fr_chk 1 "1 2 3 4 5" "no modifier: the list as it is"
fr_chk 2 "5 4 3 2 1" "reverse: 1..5 back to front"
fr_chk 3 "0 1 2 3 4 5" "no modifier on two branches of 3: 0 1 2 3 4 5"
fr_chk 4 "2 1 0 5 4 3" "reverse works on each branch: 2 1 0 and 5 4 3 (not 5 4 3 2 1 0)"
fr_chk 5 "1 2 3 4 5 30 20 10" "reverse on B only: A as is, then 30 20 10"
fr_chk 6 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
