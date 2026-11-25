# ---------------------------------------------------------------------------
# GHVPLOFF_: Flow Register row D8-GH-VECTOR-0054 (Plane Offset), flowrows6_gen.py. The graph flowrows_ghvploff.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the origin moves along the unit normal: XY plane by 2.5 -> (0,0,2.5) with the normal still Z; by -4 -> (0,0,-4); by 0 unchanged; 2.5 then 2 -> z = 4.5; the plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3 -> (1,0,0) + (1,1,1) = (2,1,1), normal unchanged; axes (2,0,0),(0,3,0) have normal (0,0,6), unit (0,0,1), so offset 1 -> (0,0,1).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvploff.dflow|g" "$HERE/flowrows_ghvploff_script.txt" > "$TMPW/flowrows_ghvploff_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvploff_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVPLOFF_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvploff_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVPLOFF_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVPLOFF_$1: $3 (measured '$got')"; else echo "FAIL GHVPLOFF_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVPLOFF_$2"; else echo "FAIL GHVPLOFF_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVPLOFF_$3 (measured $n)"; else echo "FAIL GHVPLOFF_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 147 node(s)" <<<"$FR_OUT"; then echo "ok   GHVPLOFF_solve: the player solved all 147 nodes (every node type is registered)"; else echo "FAIL GHVPLOFF_solve: expected 'solved 147 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 27 " <<<"$FR_OUT"; then echo "ok   GHVPLOFF_objects: exactly 27 point(s) baked"; else echo "FAIL GHVPLOFF_objects: expected 27 baked objects"; fail=1; fi
fr_chk 1 "1" "XY plane offset by 2.5: origin: a value is there"
fr_chk 2 "0" "XY plane offset by 2.5: origin: X = 0"
fr_chk 3 "0" "XY plane offset by 2.5: origin: Y = 0"
fr_chk 4 "0" "XY plane offset by 2.5: origin: Z = 2.5"
fr_chk 5 "0" "its normal is still Z"
fr_chk 6 "1" "offset by -4 goes against the normal: a value is there"
fr_chk 7 "0" "offset by -4 goes against the normal: X = 0"
fr_chk 8 "0" "offset by -4 goes against the normal: Y = 0"
fr_chk 9 "0" "offset by -4 goes against the normal: Z = -4"
fr_chk 10 "1" "offset 0 leaves the plane: a value is there"
fr_chk 11 "0" "offset 0 leaves the plane: X = 0"
fr_chk 12 "0" "offset 0 leaves the plane: Y = 0"
fr_chk 13 "0" "offset 0 leaves the plane: Z = 0"
fr_chk 14 "1" "offsets add: 2.5 then 2 = 4.5: a value is there"
fr_chk 15 "0" "offsets add: 2.5 then 2 = 4.5: X = 0"
fr_chk 16 "0" "offsets add: 2.5 then 2 = 4.5: Y = 0"
fr_chk 17 "0" "offsets add: 2.5 then 2 = 4.5: Z = 4.5"
fr_chk 18 "1" "plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3: (1,0,0) + (1,1,1): a value is there"
fr_chk 19 "0" "plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3: (1,0,0) + (1,1,1): X = 2"
fr_chk 20 "0" "plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3: (1,0,0) + (1,1,1): Y = 1"
fr_chk 21 "0" "plane through (1,0,0) with normal (1,1,1)/sqrt 3 offset by sqrt 3: (1,0,0) + (1,1,1): Z = 1"
fr_chk 22 "0" "the tilted plane keeps its normal (1,1,1)/sqrt 3"
fr_chk 23 "1" "axes of length 2 and 3 do not scale the offset: origin (0,0,1): a value is there"
fr_chk 24 "0" "axes of length 2 and 3 do not scale the offset: origin (0,0,1): X = 0"
fr_chk 25 "0" "axes of length 2 and 3 do not scale the offset: origin (0,0,1): Y = 0"
fr_chk 26 "0" "axes of length 2 and 3 do not scale the offset: origin (0,0,1): Z = 1"
fr_chk 27 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
