# ---------------------------------------------------------------------------
# GHTGRAFT_: Flow Register row D8-GH-TREES-0010 (Graft parameter modifier), flowrows6_gen.py. The graph flowrows_ghtgraft.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: graft appends the item's index to its branch path: one list of 4 at {0} -> {0;0} {0;1} {0;2} {0;3} (4 branches of 1 item); the tree {0;0} {0;1} {0;2} of 2 items each -> {0;0;0} {0;0;1} {0;1;0} {0;1;1} {0;2;0} {0;2;1} (6 branches of 1); an empty list has no branches; without the modifier the list stays one branch of 4.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghtgraft.dflow|g" "$HERE/flowrows_ghtgraft_script.txt" > "$TMPW/flowrows_ghtgraft_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtgraft_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTGRAFT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtgraft_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTGRAFT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHTGRAFT_$1: $3 (measured '$got')"; else echo "FAIL GHTGRAFT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHTGRAFT_$2"; else echo "FAIL GHTGRAFT_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHTGRAFT_$3 (measured $n)"; else echo "FAIL GHTGRAFT_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 40 node(s)" <<<"$FR_OUT"; then echo "ok   GHTGRAFT_solve: the player solved all 40 nodes (every node type is registered)"; else echo "FAIL GHTGRAFT_solve: expected 'solved 40 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 44 " <<<"$FR_OUT"; then echo "ok   GHTGRAFT_objects: exactly 44 point(s) baked"; else echo "FAIL GHTGRAFT_objects: expected 44 baked objects"; fail=1; fi
fr_chk 1 "1" "no modifier on one list of 4: 1 branch(es)"
fr_chk 2 "4" "no modifier on one list of 4: items per branch = '4'"
fr_chk 3 "0" "no modifier on one list of 4: path indices = '0'"
fr_chk 4 "4" "graft: every item its own branch, the item index appended: {0;0} {0;1} {0;2} {0;3}: 4 branch(es)"
fr_chk 5 "1 1 1 1" "graft: every item its own branch, the item index appended: {0;0} {0;1} {0;2} {0;3}: items per branch = '1 1 1 1'"
fr_chk 6 "0 0 0 1 0 2 0 3" "graft: every item its own branch, the item index appended: {0;0} {0;1} {0;2} {0;3}: path indices = '0 0 0 1 0 2 0 3'"
fr_chk 7 "6" "graft of a 3 branch tree: path {a;b} and item i give {a;b;i}: 6 branch(es)"
fr_chk 8 "1 1 1 1 1 1" "graft of a 3 branch tree: path {a;b} and item i give {a;b;i}: items per branch = '1 1 1 1 1 1'"
fr_chk 9 "0 0 0 0 0 1 0 1 0 0 1 1 0 2 0 0 2 1" "graft of a 3 branch tree: path {a;b} and item i give {a;b;i}: path indices = '0 0 0 0 0 1 0 1 0 0 1 1 0 2 0 0 2 1'"
fr_chk 10 "0" "graft of an empty list: no branches: 0 branch(es)"
fr_chk 11 "0" "graft of an empty list: no branches: no lengths"
fr_chk 12 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
