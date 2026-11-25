# ---------------------------------------------------------------------------
# GREC_: Flow Register row D8-GH-CURVE-0050 (Rectangle), flowgeo_gen.py. The graph flowgeo_grec.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the rectangle is centred on the plane origin, X Size along the plane X axis and Y Size along its Y axis, corners counter-clockwise from (-X/2, -Y/2): 6 x 4 in the XY plane at the origin has the corners (-3,-2), (3,-2), (3,2), (-3,2); in the plane through (1,2,3) with axes (0,1,0) and (0,0,1) the corners are origin + (+-3) (0,1,0) + (+-2) (0,0,1): (1,-1,1), (1,5,1), (1,5,5), (1,-1,5); area 24 and perimeter 20 in both; 3 x 7 in the plane through the origin with normal (1,2,2) has area 21, perimeter 20, the side from the first to the second corner 3 long (X Size) and from the first to the last 7 long (Y Size), the diagonal sqrt(9 + 49) = sqrt 58, every corner sqrt(58)/2 from the origin and the area centroid at the origin; it is a closed curve with 4 corners (5 control points).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_grec.dflow|g" "$HERE/flowgeo_grec_script.txt" > "$TMPW/flowgeo_grec_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_grec_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_grec_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GREC_$1: $3 (measured '$got')"; else echo "FAIL GREC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 178 node(s)" <<<"$FR_OUT"; then echo "ok   GREC_solve: the player solved all 178 nodes (every node type is registered)"; else echo "FAIL GREC_solve: expected 'solved 178 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 30 " <<<"$FR_OUT"; then echo "ok   GREC_objects: exactly 30 point(s) baked"; else echo "FAIL GREC_objects: expected 30 baked objects"; fail=1; fi
fr_chk 1 "5" "6 x 4 at the origin: 4 corners and the closing point"
fr_chk 2 "0" "6 x 4 in the XY plane: corner 0 is (-3, -2, 0)"
fr_chk 3 "0" "6 x 4 in the XY plane: corner 1 is (3, -2, 0)"
fr_chk 4 "0" "6 x 4 in the XY plane: corner 2 is (3, 2, 0)"
fr_chk 5 "0" "6 x 4 in the XY plane: corner 3 is (-3, 2, 0)"
fr_chk 6 "0" "6 x 4 has area 24"
fr_chk 7 "0" "6 x 4 is centred on the plane origin"
fr_chk 8 "1" "6 x 4 is centred on the plane origin (the node under test produced the value)"
fr_chk 9 "0" "6 x 4 has perimeter 20"
fr_chk 10 "1" "6 x 4 is a closed curve"
fr_chk 11 "0" "6 x 4 in the plane at (1,2,3) with axes (0,1,0), (0,0,1): corner 0 is (1, -1, 1)"
fr_chk 12 "0" "6 x 4 in the plane at (1,2,3) with axes (0,1,0), (0,0,1): corner 1 is (1, 5, 1)"
fr_chk 13 "0" "6 x 4 in the plane at (1,2,3) with axes (0,1,0), (0,0,1): corner 2 is (1, 5, 5)"
fr_chk 14 "0" "6 x 4 in the plane at (1,2,3) with axes (0,1,0), (0,0,1): corner 3 is (1, -1, 5)"
fr_chk 15 "0" "the same rectangle has area 24"
fr_chk 16 "0" "and is centred on the plane origin (1,2,3)"
fr_chk 17 "0" "and perimeter 20"
fr_chk 18 "0" "3 x 7 in the tilted plane: the first side (along the plane X axis) is 3 long"
fr_chk 19 "0" "and the last side (along the plane Y axis) is 7 long"
fr_chk 20 "0" "the diagonal is sqrt 58"
fr_chk 21 "0" "corner 0 is sqrt(58)/2 from the origin"
fr_chk 22 "0" "corner 1 is sqrt(58)/2 from the origin"
fr_chk 23 "0" "corner 2 is sqrt(58)/2 from the origin"
fr_chk 24 "0" "corner 3 is sqrt(58)/2 from the origin"
fr_chk 25 "0" "3 x 7 has area 21"
fr_chk 26 "0" "3 x 7 is centred on the plane origin (0,0,0)"
fr_chk 27 "1" "3 x 7 is centred on the plane origin (0,0,0) (the node under test produced the value)"
fr_chk 28 "0" "3 x 7 has perimeter 20"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
