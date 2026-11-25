# ---------------------------------------------------------------------------
# GHDPATH_: Flow Register row D8-GH-SETS-0063 (Deconstruct Path), flowrows6_gen.py. The graph flowrows_ghdpath.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the integers between the braces, in order: {0;1;3} -> 0 1 3 (3 indices); {12} -> 12; { 2 ; 5 } -> 2 5; {0;0;0;7} -> 0 0 0 7 (4 indices); text without braces, with no index, with letters, with a negative or an empty index or without the closing brace is not a path and the node is flagged.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghdpath.dflow|g" "$HERE/flowrows_ghdpath_script.txt" > "$TMPW/flowrows_ghdpath_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghdpath_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHDPATH_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghdpath_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHDPATH_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHDPATH_$1: $3 (measured '$got')"; else echo "FAIL GHDPATH_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHDPATH_$2"; else echo "FAIL GHDPATH_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHDPATH_$3 (measured $n)"; else echo "FAIL GHDPATH_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 50 node(s)" <<<"$FR_OUT"; then echo "ok   GHDPATH_solve: the player solved all 50 nodes (every node type is registered)"; else echo "FAIL GHDPATH_solve: expected 'solved 50 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 21 " <<<"$FR_OUT"; then echo "ok   GHDPATH_objects: exactly 21 point(s) baked"; else echo "FAIL GHDPATH_objects: expected 21 baked objects"; fail=1; fi
fr_chk 1 "0 1 3" "{0;1;3} deconstructs to 0 1 3"
fr_chk 2 "3" "{0;1;3} has 3 indices"
fr_chk 3 "12" "{12} deconstructs to 12"
fr_chk 4 "1" "{12} has 1 indices"
fr_chk 5 "2 5" "{ 2 ; 5 } deconstructs to 2 5"
fr_chk 6 "2" "{ 2 ; 5 } has 2 indices"
fr_chk 7 "0 0 0 7" "{0;0;0;7} deconstructs to 0 0 0 7"
fr_chk 8 "4" "{0;0;0;7} has 4 indices"
fr_chk 9 "0" "0;1 (no braces) is not a path: nothing comes out"
fr_chk 10 "0" "{} (no index) is not a path: nothing comes out"
fr_chk 11 "0" "{a;b} (letters) is not a path: nothing comes out"
fr_chk 12 "0" "{-1} (negative index) is not a path: nothing comes out"
fr_chk 13 "0" "{1;;2} (an empty index) is not a path: nothing comes out"
fr_chk 14 "0" "{1;2 (no closing brace) is not a path: nothing comes out"
fr_chk 15 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Deconstruct Path (#21) flagged: Not a path: expected indices between braces such as {0;1;3}" "Deconstruct Path #21 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_has "GrasshopperPlayer: Deconstruct Path (#25) flagged: Not a path: expected indices between braces such as {0;1;3}" "Deconstruct Path #25 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_has "GrasshopperPlayer: Deconstruct Path (#29) flagged: Not a path: expected indices between braces such as {0;1;3}" "Deconstruct Path #29 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_has "GrasshopperPlayer: Deconstruct Path (#33) flagged: Not a path: expected indices between braces such as {0;1;3}" "Deconstruct Path #33 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_has "GrasshopperPlayer: Deconstruct Path (#37) flagged: Not a path: expected indices between braces such as {0;1;3}" "Deconstruct Path #37 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_has "GrasshopperPlayer: Deconstruct Path (#41) flagged: Not a path: expected indices between braces such as {0;1;3}" "Deconstruct Path #41 is flagged: Not a path: expected indices between braces such as {0;1;3}"
fr_cnt "flagged:" 6 "flagged: exactly 6 node(s) are flagged" 
