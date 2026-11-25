# ---------------------------------------------------------------------------
# GHSTATS_: Flow Register row D8-GH-SETS-0058 (Tree Statistics), flowrows6_gen.py. The graph flowrows_ghstats.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: branch count, items per branch (in branch order) and the paths {a;b;c}: 0..6 cut in threes = 3 branches {0;0} {0;1} {0;2} of 3, 3, 1 items; one list of 5 = 1 branch {0}; an empty list = 0 branches; Entwine of 2 and 3 items = {0;0} {0;1} with 2, 3; a list grafted twice = {0;0;0} {0;1;0} {0;2;0}; one item = 1 branch of 1.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghstats.dflow|g" "$HERE/flowrows_ghstats_script.txt" > "$TMPW/flowrows_ghstats_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghstats_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHSTATS_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghstats_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHSTATS_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHSTATS_$1: $3 (measured '$got')"; else echo "FAIL GHSTATS_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHSTATS_$2"; else echo "FAIL GHSTATS_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHSTATS_$3 (measured $n)"; else echo "FAIL GHSTATS_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 63 node(s)" <<<"$FR_OUT"; then echo "ok   GHSTATS_solve: the player solved all 63 nodes (every node type is registered)"; else echo "FAIL GHSTATS_solve: expected 'solved 63 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 39 " <<<"$FR_OUT"; then echo "ok   GHSTATS_objects: exactly 39 point(s) baked"; else echo "FAIL GHSTATS_objects: expected 39 baked objects"; fail=1; fi
fr_chk 1 "3" "0..6 cut in threes: 3 branch(es)"
fr_chk 2 "3 3 1" "0..6 cut in threes: items per branch = '3 3 1'"
fr_chk 3 "0 0 0 1 0 2" "0..6 cut in threes: path indices = '0 0 0 1 0 2'"
fr_chk 4 "1" "a plain list of 5: 1 branch(es)"
fr_chk 5 "5" "a plain list of 5: items per branch = '5'"
fr_chk 6 "0" "a plain list of 5: path indices = '0'"
fr_chk 7 "0" "an empty list has 0 branches"
fr_chk 8 "0" "and no lengths"
fr_chk 9 "2" "Entwine of 2 and 3 items: {0;0} {0;1}: 2 branch(es)"
fr_chk 10 "2 3" "Entwine of 2 and 3 items: {0;0} {0;1}: items per branch = '2 3'"
fr_chk 11 "0 0 0 1" "Entwine of 2 and 3 items: {0;0} {0;1}: path indices = '0 0 0 1'"
fr_chk 12 "3" "grafted twice: {0;0;0} {0;1;0} {0;2;0}: 3 branch(es)"
fr_chk 13 "1 1 1" "grafted twice: {0;0;0} {0;1;0} {0;2;0}: items per branch = '1 1 1'"
fr_chk 14 "0 0 0 0 1 0 0 2 0" "grafted twice: {0;0;0} {0;1;0} {0;2;0}: path indices = '0 0 0 0 1 0 0 2 0'"
fr_chk 15 "1" "a single item: 1 branch(es)"
fr_chk 16 "1" "a single item: items per branch = '1'"
fr_chk 17 "0" "a single item: path indices = '0'"
fr_chk 18 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
