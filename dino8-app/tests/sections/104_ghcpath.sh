# ---------------------------------------------------------------------------
# GHCPATH_: Flow Register row D8-GH-SETS-0062 (Construct Path), flowrows6_gen.py. The graph flowrows_ghcpath.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the text form {i;j;k} of the indices, braces and semicolons: 0 1 3 -> {0;1;3}; 7 -> {7}; 12 0 -> {12;0}; 0 1 2 3 -> {0;1;2;3}, compared with literal text by edit distance (0 for the exact text, 1 for {0;1;4}, 2 for {3;1;0} and for the comma form {0,1,3}); no index or a negative one is flagged; it round-trips through Deconstruct Path.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghcpath.dflow|g" "$HERE/flowrows_ghcpath_script.txt" > "$TMPW/flowrows_ghcpath_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghcpath_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHCPATH_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghcpath_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHCPATH_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHCPATH_$1: $3 (measured '$got')"; else echo "FAIL GHCPATH_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHCPATH_$2"; else echo "FAIL GHCPATH_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHCPATH_$3 (measured $n)"; else echo "FAIL GHCPATH_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 52 node(s)" <<<"$FR_OUT"; then echo "ok   GHCPATH_solve: the player solved all 52 nodes (every node type is registered)"; else echo "FAIL GHCPATH_solve: expected 'solved 52 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   GHCPATH_objects: exactly 13 point(s) baked"; else echo "FAIL GHCPATH_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "0" "indices 0 1 3 give the text {0;1;3}"
fr_chk 2 "1" "decoy: {0;1;4} is 1 edit away"
fr_chk 3 "2" "decoy: {3;1;0} is 2 edits away"
fr_chk 4 "2" "decoy: commas instead of semicolons are 2 edits away"
fr_chk 5 "0" "one index 7 gives {7}"
fr_chk 6 "0" "indices 12 and 0 give {12;0}"
fr_chk 7 "0" "four indices 0 1 2 3 give {0;1;2;3}"
fr_chk 8 "0" "no indices: nothing (a path needs at least one)"
fr_chk 9 "0" "a negative index is not a path: nothing"
fr_chk 10 "0 1 3" "round trip: the text {0;1;3} deconstructs to 0 1 3"
fr_chk 11 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Construct Path (#35) flagged: A path needs at least one index" "Construct Path #35 is flagged: A path needs at least one index"
fr_has "GrasshopperPlayer: Construct Path (#40) flagged: A path index must be a non-negative integer" "Construct Path #40 is flagged: A path index must be a non-negative integer"
fr_cnt "flagged:" 2 "flagged: exactly 2 node(s) are flagged" 
