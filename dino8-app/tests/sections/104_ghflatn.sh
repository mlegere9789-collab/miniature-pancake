# ---------------------------------------------------------------------------
# GHFLATN_: Flow Register row D8-GH-SETS-0052 (Flatten Tree), flowrows6_gen.py. The graph flowrows_ghflatn.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: one branch with every item in order: three branches {0;0} {0;1} {0;2} of 2 -> {0} of 6 holding 0 1 2 3 4 5; with the optional path {2;1} the branch is {2;1}; a grafted (depth 3) tree gives the same; one list sent to {7}; a path without braces is flagged.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghflatn.dflow|g" "$HERE/flowrows_ghflatn_script.txt" > "$TMPW/flowrows_ghflatn_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghflatn_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHFLATN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghflatn_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHFLATN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHFLATN_$1: $3 (measured '$got')"; else echo "FAIL GHFLATN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHFLATN_$2"; else echo "FAIL GHFLATN_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHFLATN_$3 (measured $n)"; else echo "FAIL GHFLATN_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 52 node(s)" <<<"$FR_OUT"; then echo "ok   GHFLATN_solve: the player solved all 52 nodes (every node type is registered)"; else echo "FAIL GHFLATN_solve: expected 'solved 52 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 21 " <<<"$FR_OUT"; then echo "ok   GHFLATN_objects: exactly 21 point(s) baked"; else echo "FAIL GHFLATN_objects: expected 21 baked objects"; fail=1; fi
fr_chk 1 "1" "Flatten Tree of 3 branches: one branch {0} of 6: 1 branch(es)"
fr_chk 2 "6" "Flatten Tree of 3 branches: one branch {0} of 6: items per branch = '6'"
fr_chk 3 "0" "Flatten Tree of 3 branches: one branch {0} of 6: path indices = '0'"
fr_chk 4 "0 1 2 3 4 5" "the items keep their order"
fr_chk 5 "1" "with the path {2;1} the single branch is {2;1}: 1 branch(es)"
fr_chk 6 "6" "with the path {2;1} the single branch is {2;1}: items per branch = '6'"
fr_chk 7 "2 1" "with the path {2;1} the single branch is {2;1}: path indices = '2 1'"
fr_chk 8 "1" "a depth 3 tree flattens to the same single branch {0}: 1 branch(es)"
fr_chk 9 "6" "a depth 3 tree flattens to the same single branch {0}: items per branch = '6'"
fr_chk 10 "0" "a depth 3 tree flattens to the same single branch {0}: path indices = '0'"
fr_chk 11 "1" "one list sent to the path {7}: 1 branch(es)"
fr_chk 12 "3" "one list sent to the path {7}: items per branch = '3'"
fr_chk 13 "7" "one list sent to the path {7}: path indices = '7'"
fr_chk 14 "0" "a path without braces is not a path: nothing comes out"
fr_chk 15 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Flatten Tree (#43) flagged: Not a path: expected indices between braces such as {0;1;3}" "Flatten Tree #43 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_cnt "flagged:" 1 "flagged: exactly 1 node(s) are flagged" 
