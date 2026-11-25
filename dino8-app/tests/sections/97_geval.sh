# ---------------------------------------------------------------------------
# GEVAL_: Flow Register row D8-GH-CURVE-0012 (Evaluate Curve), flowgeo_gen.py. The graph flowgeo_geval.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: t is the curve's own parameter, clamped to its domain, the domain midpoint when not given, and the Parameter output is the value evaluated: the circle of radius 2 about (1,2,0) (domain 0..2 pi) at t = 0, pi/2, pi, 3 pi/2 is (3,2,0), (1,4,0), (-1,2,0), (1,0,0) with unit tangents (0,1,0), (-1,0,0), (0,-1,0), (1,0,0), at t = pi/4 (the middle of the first span, 45 degrees) (1 + sqrt 2, 2 + sqrt 2, 0), tangent (-sqrt(1/2), sqrt(1/2), 0); without t it is the midpoint pi, (-1,2,0); t = 100 clamps to 2 pi (the start point again) and t = -3 to 0; the line (0,0,0)->(3,4,0) at the midpoint is (1.5,2,0), tangent (0.6,0.8,0), clamped far past either end it is (3,4,0) or (0,0,0); the arc of radius 2 from 0 to pi/2 (domain 0..pi/2) at t = 0, pi/4, pi/2 is (2,0,0), (sqrt 2, sqrt 2, 0), (0,2,0), tangent at pi/4 (-sqrt(1/2), sqrt(1/2), 0), and from pi/6 to 2 pi/3 at t = pi/6 and 2 pi/3 (domain ends) (sqrt 3, 1, 0) and (-1, sqrt 3, 0); the polyline (0,0,0),(4,0,0),(4,3,0) with vertex k at parameter k: t = 0.5 is (2,0,0) tangent (1,0,0), t = 1.5 is (4,1.5,0) tangent (0,1,0), t = 2 is (4,3,0); the parabola y = x^2 (Bezier through (-1,1), (0,-1), (1,1), x = 2t - 1) at t = 0.25 is (-1/2,1/4,0), tangent (1,-1,0)/sqrt 2, t = 0.5 is (0,0,0).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_geval.dflow|g" "$HERE/flowgeo_geval_script.txt" > "$TMPW/flowgeo_geval_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_geval_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GEVAL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_geval_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GEVAL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GEVAL_$1: $3 (measured '$got')"; else echo "FAIL GEVAL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 286 node(s)" <<<"$FR_OUT"; then echo "ok   GEVAL_solve: the player solved all 286 nodes (every node type is registered)"; else echo "FAIL GEVAL_solve: expected 'solved 286 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 46 " <<<"$FR_OUT"; then echo "ok   GEVAL_objects: exactly 46 point(s) baked"; else echo "FAIL GEVAL_objects: expected 46 baked objects"; fail=1; fi
fr_chk 1 "0" "circle, t = 0: the point (3, 2, 0)"
fr_chk 2 "0" "circle, t = 0: the unit tangent (0, 1, 0)"
fr_chk 3 "0" "circle, t = 0: the Parameter output is t"
fr_chk 4 "1" "circle, t = 0: the Parameter output is t (the node under test produced the value)"
fr_chk 5 "0" "circle, t = 1.5708: the point (1, 4, 0)"
fr_chk 6 "0" "circle, t = 1.5708: the unit tangent (-1, 0, 0)"
fr_chk 7 "0" "circle, t = 1.5708: the Parameter output is t"
fr_chk 8 "0" "circle, t = 3.14159: the point (-1, 2, 0)"
fr_chk 9 "0" "circle, t = 3.14159: the unit tangent (0, -1, 0)"
fr_chk 10 "0" "circle, t = 3.14159: the Parameter output is t"
fr_chk 11 "0" "circle, t = 4.71239: the point (1, 0, 0)"
fr_chk 12 "0" "circle, t = 4.71239: the unit tangent (1, 0, 0)"
fr_chk 13 "0" "circle, t = 4.71239: the Parameter output is t"
fr_chk 14 "0" "circle, t = 0.785398: the point (2.414214, 3.414214, 0)"
fr_chk 15 "0" "circle, t = 0.785398: the unit tangent (-0.707107, 0.707107, 0)"
fr_chk 16 "0" "circle, t = 0.785398: the Parameter output is t"
fr_chk 17 "0" "circle, t not given: the domain midpoint pi, the point (-1,2,0)"
fr_chk 18 "0" "circle, t not given: the Parameter output is the midpoint pi"
fr_chk 19 "0" "circle, t = 100: clamped to the end 2 pi, the start point (3,2,0)"
fr_chk 20 "0" "circle, t = 100: the Parameter output is the domain end 2 pi"
fr_chk 21 "0" "circle, t = -3: clamped to the start 0, the point (3,2,0)"
fr_chk 22 "0" "circle, t = -3: the Parameter output is the domain start 0"
fr_chk 23 "1" "circle, t = -3: the Parameter output is the domain start 0 (the node under test produced the value)"
fr_chk 24 "0" "line, t not given: the midpoint (1.5,2,0)"
fr_chk 25 "0" "line, t not given: the unit tangent (0.6,0.8,0)"
fr_chk 26 "0" "line, t = 1000: clamped to the end (3,4,0)"
fr_chk 27 "0" "line, t = -1000: clamped to the start (0,0,0)"
fr_chk 28 "1" "line, t = -1000: clamped to the start (0,0,0) (the node under test produced the value)"
fr_chk 29 "0" "arc 0..pi/2, t = 0: the point (2, 0, 0)"
fr_chk 30 "0" "arc 0..pi/2, t = 0.785398: the point (1.414214, 1.414214, 0)"
fr_chk 31 "0" "arc 0..pi/2, t = pi/4: the unit tangent (-sqrt(1/2), sqrt(1/2))"
fr_chk 32 "0" "arc 0..pi/2, t = 1.5708: the point (0, 2, 0)"
fr_chk 33 "0" "arc pi/6..2 pi/3, t = pi/6 (the domain start): (sqrt 3, 1, 0)"
fr_chk 34 "0" "arc pi/6..2 pi/3, t = 2 pi/3 (the domain end): (-1, sqrt 3, 0)"
fr_chk 35 "0" "polyline, t = 0.5: (2,0,0)"
fr_chk 36 "0" "polyline, t = 0.5: tangent (1,0,0)"
fr_chk 37 "0" "polyline, t = 1.5: (4,1.5,0)"
fr_chk 38 "0" "polyline, t = 1.5: tangent (0,1,0)"
fr_chk 39 "0" "polyline, t = 2: the end (4,3,0)"
fr_chk 40 "0" "parabola, t = 0.25: (-1/2,1/4,0)"
fr_chk 41 "0" "parabola, t = 0.25: the unit tangent (1,-1,0)/sqrt 2"
fr_chk 42 "0" "parabola, t = 0.25: the Parameter output is 0.25"
fr_chk 43 "0" "parabola, t = 0.5: the vertex (0,0,0)"
fr_chk 44 "1" "parabola, t = 0.5: the vertex (0,0,0) (the node under test produced the value)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
