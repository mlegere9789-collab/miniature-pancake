# ---------------------------------------------------------------------------
# GHXLINARR_: Flow Register row D8-GH-XFORM-0015 (Linear Array), flowrows6_gen.py. The graph flowrows_ghxlinarr.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: copy i sits at p + i * distance * unit(direction): (0,0,0) with direction (0,0,2), distance 3, count 4 -> Z = 0 3 6 9; (1,-2,5) with direction (1,1,0), distance sqrt 2, count 3 -> (1,-2,5), (2,-1,5), (3,0,5); count 1 is the original; distance -2 along X -> 0 -2 -4.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghxlinarr.dflow|g" "$HERE/flowrows_ghxlinarr_script.txt" > "$TMPW/flowrows_ghxlinarr_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxlinarr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXLINARR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxlinarr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXLINARR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHXLINARR_$1: $3 (measured '$got')"; else echo "FAIL GHXLINARR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHXLINARR_$2"; else echo "FAIL GHXLINARR_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHXLINARR_$3 (measured $n)"; else echo "FAIL GHXLINARR_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 99 node(s)" <<<"$FR_OUT"; then echo "ok   GHXLINARR_solve: the player solved all 99 nodes (every node type is registered)"; else echo "FAIL GHXLINARR_solve: expected 'solved 99 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 32 " <<<"$FR_OUT"; then echo "ok   GHXLINARR_objects: exactly 32 point(s) baked"; else echo "FAIL GHXLINARR_objects: expected 32 baked objects"; fail=1; fi
fr_chk 1 "4" "count 4 gives 4 copies"
fr_chk 2 "0 3 6 9" "direction (0,0,2) is unitized: copies at Z = 0, 3, 6, 9 (distance 3 apart)"
fr_chk 3 "0 0 0 0" "X stays 0"
fr_chk 4 "0 0 0 0" "Y stays 0"
fr_chk 5 "1" "copy 0 along (1,1,0) with step length sqrt 2: a value is there"
fr_chk 6 "0" "copy 0 along (1,1,0) with step length sqrt 2: X = 1"
fr_chk 7 "0" "copy 0 along (1,1,0) with step length sqrt 2: Y = -2"
fr_chk 8 "0" "copy 0 along (1,1,0) with step length sqrt 2: Z = 5"
fr_chk 9 "1" "copy 1 along (1,1,0) with step length sqrt 2: a value is there"
fr_chk 10 "0" "copy 1 along (1,1,0) with step length sqrt 2: X = 2"
fr_chk 11 "0" "copy 1 along (1,1,0) with step length sqrt 2: Y = -1"
fr_chk 12 "0" "copy 1 along (1,1,0) with step length sqrt 2: Z = 5"
fr_chk 13 "1" "copy 2 along (1,1,0) with step length sqrt 2: a value is there"
fr_chk 14 "0" "copy 2 along (1,1,0) with step length sqrt 2: X = 3"
fr_chk 15 "0" "copy 2 along (1,1,0) with step length sqrt 2: Y = 0"
fr_chk 16 "0" "copy 2 along (1,1,0) with step length sqrt 2: Z = 5"
fr_chk 17 "1" "count 1 gives just the original"
fr_chk 18 "4" "the single copy is the original: X = 4"
fr_chk 19 "6" "and Z = 6"
fr_chk 20 "0 -2 -4" "distance -2 along X: 0, -2, -4"
fr_chk 21 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
