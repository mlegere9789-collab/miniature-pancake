# ---------------------------------------------------------------------------
# GHGRAFTN_: Flow Register row D8-GH-SETS-0051 (Graft Tree), flowrows6_gen.py. The graph flowrows_ghgraftn.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: every item to its own branch with its index appended to the path: one list of 4 at {0} -> {0;0} {0;1} {0;2} {0;3}; the branches {0;0} {0;1} of 3 items -> {0;0;0} {0;0;1} {0;0;2} {0;1;0} {0;1;1} {0;1;2}; the items keep their order (0..5); an empty list stays empty.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghgraftn.dflow|g" "$HERE/flowrows_ghgraftn_script.txt" > "$TMPW/flowrows_ghgraftn_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghgraftn_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHGRAFTN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghgraftn_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHGRAFTN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHGRAFTN_$1: $3 (measured '$got')"; else echo "FAIL GHGRAFTN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHGRAFTN_$2"; else echo "FAIL GHGRAFTN_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHGRAFTN_$3 (measured $n)"; else echo "FAIL GHGRAFTN_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 35 node(s)" <<<"$FR_OUT"; then echo "ok   GHGRAFTN_solve: the player solved all 35 nodes (every node type is registered)"; else echo "FAIL GHGRAFTN_solve: expected 'solved 35 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 46 " <<<"$FR_OUT"; then echo "ok   GHGRAFTN_objects: exactly 46 point(s) baked"; else echo "FAIL GHGRAFTN_objects: expected 46 baked objects"; fail=1; fi
fr_chk 1 "4" "Graft Tree of one list of 4: {0;0} {0;1} {0;2} {0;3}: 4 branch(es)"
fr_chk 2 "1 1 1 1" "Graft Tree of one list of 4: {0;0} {0;1} {0;2} {0;3}: items per branch = '1 1 1 1'"
fr_chk 3 "0 0 0 1 0 2 0 3" "Graft Tree of one list of 4: {0;0} {0;1} {0;2} {0;3}: path indices = '0 0 0 1 0 2 0 3'"
fr_chk 4 "6" "Graft Tree of two branches of 3: {a;b} + item i = {a;b;i}: 6 branch(es)"
fr_chk 5 "1 1 1 1 1 1" "Graft Tree of two branches of 3: {a;b} + item i = {a;b;i}: items per branch = '1 1 1 1 1 1'"
fr_chk 6 "0 0 0 0 0 1 0 0 2 0 1 0 0 1 1 0 1 2" "Graft Tree of two branches of 3: {a;b} + item i = {a;b;i}: path indices = '0 0 0 0 0 1 0 0 2 0 1 0 0 1 1 0 1 2'"
fr_chk 7 "0 1 2 3 4 5" "the items keep their order: flattening the grafted tree gives 0 1 2 3 4 5 back"
fr_chk 8 "0" "grafting an empty list leaves no branches"
fr_chk 9 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
