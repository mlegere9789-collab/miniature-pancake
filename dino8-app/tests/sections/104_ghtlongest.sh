# ---------------------------------------------------------------------------
# GHTLONGEST_: Flow Register row D8-GH-TREES-0002 (Longest-list matching), flowrows6_gen.py. The graph flowrows_ghtlongest.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the shorter list repeats its LAST item: 1 2 3 + 10 20 30 40 50 = 11 22 33 (3+40) (3+50) = 11 22 33 43 53, five results; either order; 5 + (10 .. 50) = 15 25 35 45 55; equal lengths pair item by item (2 4 6); 100 200 + 1 2 3 4 = 101 202 203 204.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghtlongest.dflow|g" "$HERE/flowrows_ghtlongest_script.txt" > "$TMPW/flowrows_ghtlongest_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtlongest_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTLONGEST_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghtlongest_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHTLONGEST_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHTLONGEST_$1: $3 (measured '$got')"; else echo "FAIL GHTLONGEST_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHTLONGEST_$2"; else echo "FAIL GHTLONGEST_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHTLONGEST_$3 (measured $n)"; else echo "FAIL GHTLONGEST_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 30 node(s)" <<<"$FR_OUT"; then echo "ok   GHTLONGEST_solve: the player solved all 30 nodes (every node type is registered)"; else echo "FAIL GHTLONGEST_solve: expected 'solved 30 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 24 " <<<"$FR_OUT"; then echo "ok   GHTLONGEST_objects: exactly 24 point(s) baked"; else echo "FAIL GHTLONGEST_objects: expected 24 baked objects"; fail=1; fi
fr_chk 1 "11 22 33 43 53" "3 + 5 items: five sums, the last item of the short list (3) repeated: 3 + 40, 3 + 50"
fr_chk 2 "5" "the result has the length of the longest list"
fr_chk 3 "11 22 33 43 53" "the order of the inputs does not matter"
fr_chk 4 "15 25 35 45 55" "a single item is repeated against every item of the long list"
fr_chk 5 "2 4 6" "equal lengths pair item by item"
fr_chk 6 "101 202 203 204" "100 200 against 1 2 3 4: 200 is repeated (not 100 again): 101 202 203 204"
fr_chk 7 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
