# ---------------------------------------------------------------------------
# GHTSIMP_: Flow Register row D8-GH-TREES-0011 (Simplify parameter modifier), flowrows6_gen.py. The graph flowrows_ghtsimp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: simplify removes the leading path indices every branch shares and nothing else: {0;k;i} (k = 0..2, i = 0..1) -> {k;i} (only the first index 0 is shared); {0;0;i} for i = 0..2 -> {i} (the shared prefix {0;0} goes); {0;i;0} -> {i;0}; without the modifier the paths stay.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghtsimp.dflow|g" "$HERE/flowrows_ghtsimp_script.txt" > "$TMPW/flowrows_ghtsimp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtsimp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTSIMP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtsimp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTSIMP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHTSIMP_$1: $3 (measured '$got')"; else echo "FAIL GHTSIMP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHTSIMP_$2"; else echo "FAIL GHTSIMP_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHTSIMP_$3 (measured $n)"; else echo "FAIL GHTSIMP_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 63 node(s)" <<<"$FR_OUT"; then echo "ok   GHTSIMP_solve: the player solved all 63 nodes (every node type is registered)"; else echo "FAIL GHTSIMP_solve: expected 'solved 63 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 88 " <<<"$FR_OUT"; then echo "ok   GHTSIMP_objects: exactly 88 point(s) baked"; else echo "FAIL GHTSIMP_objects: expected 88 baked objects"; fail=1; fi
fr_chk 1 "6" "no modifier on the grafted tree: 6 branch(es)"
fr_chk 2 "1 1 1 1 1 1" "no modifier on the grafted tree: items per branch = '1 1 1 1 1 1'"
fr_chk 3 "0 0 0 0 0 1 0 1 0 0 1 1 0 2 0 0 2 1" "no modifier on the grafted tree: path indices = '0 0 0 0 0 1 0 1 0 0 1 1 0 2 0 0 2 1'"
fr_chk 4 "6" "simplify: the first index 0 is shared by every branch and goes: {0;k;i} -> {k;i}: 6 branch(es)"
fr_chk 5 "1 1 1 1 1 1" "simplify: the first index 0 is shared by every branch and goes: {0;k;i} -> {k;i}: items per branch = '1 1 1 1 1 1'"
fr_chk 6 "0 0 0 1 1 0 1 1 2 0 2 1" "simplify: the first index 0 is shared by every branch and goes: {0;k;i} -> {k;i}: path indices = '0 0 0 1 1 0 1 1 2 0 2 1'"
fr_chk 7 "3" "no modifier on three branches sharing {0;0}: 3 branch(es)"
fr_chk 8 "1 1 1" "no modifier on three branches sharing {0;0}: items per branch = '1 1 1'"
fr_chk 9 "0 0 0 0 0 1 0 0 2" "no modifier on three branches sharing {0;0}: path indices = '0 0 0 0 0 1 0 0 2'"
fr_chk 10 "3" "simplify: the shared prefix {0;0} goes: {0;0;i} -> {i}: 3 branch(es)"
fr_chk 11 "1 1 1" "simplify: the shared prefix {0;0} goes: {0;0;i} -> {i}: items per branch = '1 1 1'"
fr_chk 12 "0 1 2" "simplify: the shared prefix {0;0} goes: {0;0;i} -> {i}: path indices = '0 1 2'"
fr_chk 13 "3" "grafted twice: {0;0;0} {0;1;0} {0;2;0}: 3 branch(es)"
fr_chk 14 "1 1 1" "grafted twice: {0;0;0} {0;1;0} {0;2;0}: items per branch = '1 1 1'"
fr_chk 15 "0 0 0 0 1 0 0 2 0" "grafted twice: {0;0;0} {0;1;0} {0;2;0}: path indices = '0 0 0 0 1 0 0 2 0'"
fr_chk 16 "3" "simplify drops only the shared first index: {i;0}: 3 branch(es)"
fr_chk 17 "1 1 1" "simplify drops only the shared first index: {i;0}: items per branch = '1 1 1'"
fr_chk 18 "0 0 1 0 2 0" "simplify drops only the shared first index: {i;0}: path indices = '0 0 1 0 2 0'"
fr_chk 19 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
