# ---------------------------------------------------------------------------
# GHVDECV_: Flow Register row D8-GH-VECTOR-0007 (Deconstruct Vector), flowrows6_gen.py. The graph flowrows_ghvdecv.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the components are the coordinates: (3,-4,12) -> 3, -4, 12; (1.5,-2.25,7) -> 1.5, -2.25, 7; they round-trip through Vector XYZ (length sqrt(9 + 16 + 144) = 13).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvdecv.dflow|g" "$HERE/flowrows_ghvdecv_script.txt" > "$TMPW/flowrows_ghvdecv_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvdecv_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVDECV_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvdecv_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVDECV_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVDECV_$1: $3 (measured '$got')"; else echo "FAIL GHVDECV_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVDECV_$2"; else echo "FAIL GHVDECV_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVDECV_$3 (measured $n)"; else echo "FAIL GHVDECV_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 53 node(s)" <<<"$FR_OUT"; then echo "ok   GHVDECV_solve: the player solved all 53 nodes (every node type is registered)"; else echo "FAIL GHVDECV_solve: expected 'solved 53 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 12 " <<<"$FR_OUT"; then echo "ok   GHVDECV_objects: exactly 12 point(s) baked"; else echo "FAIL GHVDECV_objects: expected 12 baked objects"; fail=1; fi
fr_chk 1 "3" "X component of (3,-4,12) is 3"
fr_chk 2 "-4" "Y component is -4"
fr_chk 3 "12" "Z component is 12"
fr_chk 4 "0" "X component of (1.5,-2.25,7) is 1.5"
fr_chk 5 "0" "Y component is -2.25"
fr_chk 6 "0" "Z component is 7"
fr_chk 7 "1" "round trip through Vector XYZ: a value is there"
fr_chk 8 "0" "round trip through Vector XYZ: X = 3"
fr_chk 9 "0" "round trip through Vector XYZ: Y = -4"
fr_chk 10 "0" "round trip through Vector XYZ: Z = 12"
fr_chk 11 "13" "length of the round-tripped vector is 13"
fr_chk 12 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
