# ---------------------------------------------------------------------------
# GCIRC_: Flow Register row D8-GH-CURVE-0041 (Circle), flowgeo_gen.py. The graph flowgeo_gcirc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: circumference 2 pi r: r = 0.5, 1, 2.5, 10 give pi, 2 pi, 5 pi, 20 pi (the Rhino 8 reference records GH-CIRC-0001..4 read 3.14159265359, 6.28318530718, 15.7079632679, 62.8318530718); radius 0 gives length 0 and radius -1 the circle of radius 1, length 2 pi (records GH-CIRC-0005, -0006); a circle in the XY plane at (1,2,3) of radius 2 passes (3,2,3), (1,4,3), (-1,2,3), (1,0,3) a quarter, half and three quarters of the way round, counter-clockwise; the area pi r^2 and the centre (the plane origin) in the plane with normal (1,1,1) at (1,2,3), radius 2: 4 pi.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gcirc.dflow|g" "$HERE/flowgeo_gcirc_script.txt" > "$TMPW/flowgeo_gcirc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcirc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCIRC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcirc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCIRC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GCIRC_$1: $3 (measured '$got')"; else echo "FAIL GCIRC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 115 node(s)" <<<"$FR_OUT"; then echo "ok   GCIRC_solve: the player solved all 115 nodes (every node type is registered)"; else echo "FAIL GCIRC_solve: expected 'solved 115 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 18 " <<<"$FR_OUT"; then echo "ok   GCIRC_objects: exactly 18 point(s) baked"; else echo "FAIL GCIRC_objects: expected 18 baked objects"; fail=1; fi
fr_chk 1 "0" "r = 0.5 has length pi"
fr_chk 2 "0" "r = 1 has length 2 pi"
fr_chk 3 "0" "r = 2.5 has length 5 pi"
fr_chk 4 "0" "r = 10 has length 20 pi"
fr_chk 5 "0" "r = 0 has length 0 (record GH-CIRC-0005)"
fr_chk 6 "1" "r = 0 has length 0 (record GH-CIRC-0005) (the node under test produced the value)"
fr_chk 7 "0" "r = -1 is the circle of radius 1, length 2 pi (record GH-CIRC-0006)"
fr_chk 8 "0" "r = 0 is still a curve, a point-sized one at the plane origin (1,2,3): its start"
fr_chk 9 "0" "and its end"
fr_chk 10 "0" "the start of the circle of radius 2 at (1,2,3) is (3,2,3)"
fr_chk 11 "0" "at t = pi/2 (a quarter of the way round) it is (1,4,3)"
fr_chk 12 "0" "at t = pi (half way round) it is (-1,2,3)"
fr_chk 13 "0" "at t = 3 pi/2 (three quarters of the way round) it is (1,0,3)"
fr_chk 14 "0" "the circle of radius 2 in the plane with normal (1,1,1) has length 4 pi"
fr_chk 15 "0" "its area is pi r^2 = 4 pi"
fr_chk 16 "0" "its centre is the plane origin (1,2,3)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
