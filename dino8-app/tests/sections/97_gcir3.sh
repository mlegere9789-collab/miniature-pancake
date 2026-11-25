# ---------------------------------------------------------------------------
# GCIR3_: Flow Register row D8-GH-CURVE-0042 (Circle 3Pt), flowgeo_gen.py. The graph flowgeo_gcir3.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: (0,0,0), (4,0,0), (0,3,0) form a right triangle whose hypotenuse (length 5) is a diameter: centre (2,1.5,0), radius 2.5, length 5 pi, area 6.25 pi; the unit-axis points (1,0,0), (0,1,0), (0,0,1) have the circumcentre (1/3,1/3,1/3) (equidistant: its distances to the three are all sqrt(2/3)), radius sqrt(2/3) and the circle is 2 pi sqrt(2/3) long; every one of the three points is at distance r from the centre and on the circle.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gcir3.dflow|g" "$HERE/flowgeo_gcir3_script.txt" > "$TMPW/flowgeo_gcir3_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcir3_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCIR3_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcir3_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCIR3_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GCIR3_$1: $3 (measured '$got')"; else echo "FAIL GCIR3_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 130 node(s)" <<<"$FR_OUT"; then echo "ok   GCIR3_solve: the player solved all 130 nodes (every node type is registered)"; else echo "FAIL GCIR3_solve: expected 'solved 130 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 27 " <<<"$FR_OUT"; then echo "ok   GCIR3_objects: exactly 27 point(s) baked"; else echo "FAIL GCIR3_objects: expected 27 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the radius is 2.5"
fr_chk 2 "0" "case 1: the plane origin is the centre (2, 1.5, 0)"
fr_chk 3 "0" "case 1: the circle is 2 pi r long"
fr_chk 4 "0" "case 1: the area is pi r^2 = 6.25 pi"
fr_chk 5 "0" "case 1: point 1 is at distance r from the centre"
fr_chk 6 "0" "case 1: point 1 is on the circle (distance to the curve 0)"
fr_chk 7 "1" "case 1: point 1 is on the circle (distance to the curve 0) (the node under test produced the value)"
fr_chk 8 "0" "case 1: point 2 is at distance r from the centre"
fr_chk 9 "0" "case 1: point 2 is on the circle (distance to the curve 0)"
fr_chk 10 "1" "case 1: point 2 is on the circle (distance to the curve 0) (the node under test produced the value)"
fr_chk 11 "0" "case 1: point 3 is at distance r from the centre"
fr_chk 12 "0" "case 1: point 3 is on the circle (distance to the curve 0)"
fr_chk 13 "1" "case 1: point 3 is on the circle (distance to the curve 0) (the node under test produced the value)"
fr_chk 14 "0" "case 2: the radius is 0.816497"
fr_chk 15 "0" "case 2: the plane origin is the centre (0.333333, 0.333333, 0.333333)"
fr_chk 16 "0" "case 2: the circle is 2 pi r long"
fr_chk 17 "0" "case 2: point 1 is at distance r from the centre"
fr_chk 18 "0" "case 2: point 1 is on the circle (distance to the curve 0)"
fr_chk 19 "1" "case 2: point 1 is on the circle (distance to the curve 0) (the node under test produced the value)"
fr_chk 20 "0" "case 2: point 2 is at distance r from the centre"
fr_chk 21 "0" "case 2: point 2 is on the circle (distance to the curve 0)"
fr_chk 22 "1" "case 2: point 2 is on the circle (distance to the curve 0) (the node under test produced the value)"
fr_chk 23 "0" "case 2: point 3 is at distance r from the centre"
fr_chk 24 "0" "case 2: point 3 is on the circle (distance to the curve 0)"
fr_chk 25 "1" "case 2: point 3 is on the circle (distance to the curve 0) (the node under test produced the value)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
