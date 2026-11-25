# ---------------------------------------------------------------------------
# BFRESA_: Flow Register row geometry_nodes/Resample Curve (Resample Curve), flowrows_gen.py. The graph flowrows_bfresa.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Count mode: open curve keeps both ends, spacing L / (n-1) (line of length 10 to 5 points: z = 0, 2.5, 5, 7.5, 10); cyclic starts at the first point, spacing L / n (6 x 4 rectangle, L = 20: 20 points 1 apart pass every corner and give the rectangle back, 8 points 2.5 apart and 4 points 5 apart give the polygons the independent walk in flowrows3_gen.py computes); a 3-point resample of (0,0,0)-(4,4,0)-(8,0,0) puts its middle point at the corner (4,4,0). The Length mode is untested.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfresa.dflow|g" "$HERE/flowrows_bfresa_script.txt" > "$TMPW/flowrows_bfresa_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfresa_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFRESA_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfresa_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFRESA_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFRESA_$1: $3 (measured '$got')"; else echo "FAIL BFRESA_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 185 node(s)" <<<"$FR_OUT"; then echo "ok   BFRESA_solve: the player solved all 185 nodes (every node type is registered)"; else echo "FAIL BFRESA_solve: expected 'solved 185 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 33 " <<<"$FR_OUT"; then echo "ok   BFRESA_objects: exactly 33 point(s) baked"; else echo "FAIL BFRESA_objects: expected 33 baked objects"; fail=1; fi
fr_chk 1 "5" "resampling a line to 5:: 5 points"
fr_chk 2 "1" "resampling a line to 5:: 1 splines"
fr_chk 3 "0" "both ends kept: length 10"
fr_chk 4 "0" "ends stay at z = 0 and 10: min X"
fr_chk 5 "0" "ends stay at z = 0 and 10: min Y"
fr_chk 6 "0" "ends stay at z = 0 and 10: min Z"
fr_chk 7 "0" "ends stay at z = 0 and 10: max X"
fr_chk 8 "0" "ends stay at z = 0 and 10: max Y"
fr_chk 9 "0" "ends stay at z = 0 and 10: max Z"
fr_chk 10 "8" "a cyclic curve resampled to 8:: 8 points"
fr_chk 11 "1" "a cyclic curve resampled to 8:: 1 splines"
fr_chk 12 "0" "8 points 20 / 8 = 2.5 apart round the rectangle, joined: octagon-like polygon"
fr_chk 13 "0" "box of the 8 resampled points: min X"
fr_chk 14 "0" "box of the 8 resampled points: min Y"
fr_chk 15 "0" "box of the 8 resampled points: min Z"
fr_chk 16 "0" "box of the 8 resampled points: max X"
fr_chk 17 "0" "box of the 8 resampled points: max Y"
fr_chk 18 "0" "box of the 8 resampled points: max Z"
fr_chk 19 "0" "4 points 5 apart"
fr_chk 20 "0" "20 points 1 apart land on every corner (6 and 4 are whole): the rectangle itself, perimeter 20"
fr_chk 21 "0" "and its box: min X"
fr_chk 22 "0" "and its box: min Y"
fr_chk 23 "0" "and its box: min Z"
fr_chk 24 "0" "and its box: max X"
fr_chk 25 "0" "and its box: max Y"
fr_chk 26 "0" "and its box: max Z"
fr_chk 27 "0" "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0): min X"
fr_chk 28 "0" "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0): min Y"
fr_chk 29 "0" "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0): min Z"
fr_chk 30 "0" "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0): max X"
fr_chk 31 "0" "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0): max Y"
fr_chk 32 "0" "3 points on (0,0,0)-(4,4,0)-(8,0,0): the middle one is at half the length, the corner (4,4,0): max Z"
fr_chk 33 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
