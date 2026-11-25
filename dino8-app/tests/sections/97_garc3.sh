# ---------------------------------------------------------------------------
# GARC3_: Flow Register row D8-GH-CURVE-0046 (Arc 3Pt), flowgeo_gen.py. The graph flowgeo_garc3.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: radius 2 about the origin, points at 0, 100 and 250 degrees: the arc starts at the first, passes the second (at arc length 2 * 100 degrees = 10 pi / 9 from the start) and ends at the third after 250 degrees, 25 pi / 9 long, angle 250 degrees = 25 pi / 18, centre (0,0,0), radius 2; the same points taken as 0, 250, 100 degrees run the other way round, 260 degrees = 13 pi / 9 (length 26 pi / 9); the unit-axis points (1,0,0), (0,1,0), (0,0,1) lie on a circle of centre (1/3,1/3,1/3) and radius sqrt(2/3) at 120 degrees from one another, so the arc through the middle one is 240 degrees = 4 pi / 3 long, sqrt(2/3) * 4 pi / 3.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_garc3.dflow|g" "$HERE/flowgeo_garc3_script.txt" > "$TMPW/flowgeo_garc3_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_garc3_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GARC3_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_garc3_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GARC3_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GARC3_$1: $3 (measured '$got')"; else echo "FAIL GARC3_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 144 node(s)" <<<"$FR_OUT"; then echo "ok   GARC3_solve: the player solved all 144 nodes (every node type is registered)"; else echo "FAIL GARC3_solve: expected 'solved 144 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 25 " <<<"$FR_OUT"; then echo "ok   GARC3_objects: exactly 25 point(s) baked"; else echo "FAIL GARC3_objects: expected 25 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the radius is 2"
fr_chk 2 "0" "case 1: the angle is 4.36332 radians"
fr_chk 3 "0" "case 1: the plane origin is the centre (0, 0, 0)"
fr_chk 4 "1" "case 1: the plane origin is the centre (0, 0, 0) (the node under test produced the value)"
fr_chk 5 "0" "case 1: the arc is radius * angle = 8.72665 long"
fr_chk 6 "0" "case 1: it starts at the first point"
fr_chk 7 "0" "case 1: it ends at the third point"
fr_chk 8 "0" "case 1: at arc length 3.49066 from the start it is at the second point"
fr_chk 9 "0" "case 2: the radius is 2"
fr_chk 10 "0" "case 2: the angle is 4.53786 radians"
fr_chk 11 "0" "case 2: the plane origin is the centre (0, 0, 0)"
fr_chk 12 "1" "case 2: the plane origin is the centre (0, 0, 0) (the node under test produced the value)"
fr_chk 13 "0" "case 2: the arc is radius * angle = 9.07571 long"
fr_chk 14 "0" "case 2: it starts at the first point"
fr_chk 15 "0" "case 2: it ends at the third point"
fr_chk 16 "0" "case 2: at arc length 3.83972 from the start it is at the second point"
fr_chk 17 "0" "case 3: the radius is 0.816497"
fr_chk 18 "0" "case 3: the angle is 4.18879 radians"
fr_chk 19 "0" "case 3: the plane origin is the centre (0.333333, 0.333333, 0.333333)"
fr_chk 20 "0" "case 3: the arc is radius * angle = 3.42013 long"
fr_chk 21 "0" "case 3: it starts at the first point"
fr_chk 22 "0" "case 3: it ends at the third point"
fr_chk 23 "0" "case 3: at arc length 1.71007 from the start it is at the second point"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
