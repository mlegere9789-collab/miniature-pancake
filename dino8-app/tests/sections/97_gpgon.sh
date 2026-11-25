# ---------------------------------------------------------------------------
# GPGON_: Flow Register row D8-GH-CURVE-0054 (Polygon), flowgeo_gen.py. The graph flowgeo_gpgon.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: by radius: regular pentagon of circumradius 2 (Segments = 5): corners 2 (cos 72 k, sin 72 k), k = 0..4; side 2 R sin(36 degrees) = 2.35114100917, perimeter 5 * side, area (1/2) n R^2 sin(72 degrees) = 9.51056516295; the angle between two edges at a corner is (n - 2) 180 / n = 108 degrees (cosine -0.309016994375); hexagon of circumradius 1: perimeter 6, area 3 sqrt 3 / 2; it is closed; by edge length e (Edge Length, which replaces the radius): R = e / (2 sin(pi / n)), perimeter n e, area n e^2 / (4 tan(pi / n)): pentagon with e = 3 has R = 2.55195, perimeter 15, area 15.4843, corners R (cos 72 k, sin 72 k); square with e = 4 has R = 2 sqrt 2, corners (2 sqrt 2, 0), (0, 2 sqrt 2), (-2 sqrt 2, 0), (0, -2 sqrt 2), area 16; triangle with e = 2 about (1,2,3) has perimeter 6, area sqrt 3 and its centroid at (1,2,3); a radius of 100 is ignored when e = 4 is given with 4 segments.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gpgon.dflow|g" "$HERE/flowgeo_gpgon_script.txt" > "$TMPW/flowgeo_gpgon_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gpgon_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPGON_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gpgon_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPGON_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GPGON_$1: $3 (measured '$got')"; else echo "FAIL GPGON_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 189 node(s)" <<<"$FR_OUT"; then echo "ok   GPGON_solve: the player solved all 189 nodes (every node type is registered)"; else echo "FAIL GPGON_solve: expected 'solved 189 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 29 " <<<"$FR_OUT"; then echo "ok   GPGON_objects: exactly 29 point(s) baked"; else echo "FAIL GPGON_objects: expected 29 baked objects"; fail=1; fi
fr_chk 1 "0" "pentagon R = 2: perimeter 5 * 2 R sin 36 degrees"
fr_chk 2 "0" "pentagon R = 2: area (1/2) n R^2 sin(72 degrees)"
fr_chk 3 "0" "pentagon: corner 0 is 2 (cos 0, sin 0) degrees"
fr_chk 4 "0" "pentagon: corner 1 is 2 (cos 72, sin 72) degrees"
fr_chk 5 "0" "pentagon: corner 2 is 2 (cos 144, sin 144) degrees"
fr_chk 6 "0" "pentagon: corner 3 is 2 (cos 216, sin 216) degrees"
fr_chk 7 "0" "pentagon: corner 4 is 2 (cos 288, sin 288) degrees"
fr_chk 8 "0" "pentagon: the angle at a corner has cosine cos(108 degrees)"
fr_chk 9 "1" "pentagon is a closed curve"
fr_chk 10 "0" "hexagon R = 1: perimeter 6"
fr_chk 11 "0" "hexagon R = 1: area 3 sqrt 3 / 2"
fr_chk 12 "0" "pentagon by edge 3 (the radius 100 is ignored): perimeter 15"
fr_chk 13 "0" "pentagon by edge 3: area n e^2 / (4 tan(pi/n))"
fr_chk 14 "0" "pentagon by edge 3: corner 0 is R (cos 0, sin 0) degrees with R = 3 / (2 sin 36 degrees)"
fr_chk 15 "0" "pentagon by edge 3: corner 1 is R (cos 72, sin 72) degrees with R = 3 / (2 sin 36 degrees)"
fr_chk 16 "0" "pentagon by edge 3: corner 2 is R (cos 144, sin 144) degrees with R = 3 / (2 sin 36 degrees)"
fr_chk 17 "0" "pentagon by edge 3: corner 3 is R (cos 216, sin 216) degrees with R = 3 / (2 sin 36 degrees)"
fr_chk 18 "0" "pentagon by edge 3: corner 4 is R (cos 288, sin 288) degrees with R = 3 / (2 sin 36 degrees)"
fr_chk 19 "0" "pentagon by edge 3: the distance between corners 0 and 1 is the edge, 3"
fr_chk 20 "0" "square by edge 4: corner 0 is (2.828427, 0, 0)"
fr_chk 21 "0" "square by edge 4: corner 1 is (0, 2.828427, 0)"
fr_chk 22 "0" "square by edge 4: corner 2 is (-2.828427, 0, 0)"
fr_chk 23 "0" "square by edge 4: corner 3 is (0, -2.828427, 0)"
fr_chk 24 "0" "square by edge 4: area 16"
fr_chk 25 "0" "triangle by edge 2: perimeter 6"
fr_chk 26 "0" "triangle by edge 2: area sqrt 3"
fr_chk 27 "0" "triangle by edge 2 about (1,2,3): centroid (1,2,3)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
