# ---------------------------------------------------------------------------
# GEVAL_: Flow Register row D8-GH-CURVE-0012 (Evaluate Curve), flowgeo_gen.py. The graph flowgeo_geval.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the circle of radius 2 about (1,2,0) at t = 0, 0.25, 0.5, 0.75 (fractions of the domain) is (3,2,0), (1,4,0), (-1,2,0), (1,0,0) with unit tangents (0,1,0), (-1,0,0), (0,-1,0), (1,0,0); the line (0,0,0)->(3,4,0) at t = 0, 0.5, 1 is (0,0,0), (1.5,2,0), (3,4,0), tangent (0.6,0.8,0); the arc of radius 2 from 0 to 90 degrees at t = 0.5 is at 45 degrees, (sqrt 2, sqrt 2, 0), tangent (-sqrt(1/2), sqrt(1/2), 0).
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
if grep -qF "GrasshopperPlayer: solved 125 node(s)" <<<"$FR_OUT"; then echo "ok   GEVAL_solve: the player solved all 125 nodes (every node type is registered)"; else echo "FAIL GEVAL_solve: expected 'solved 125 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 19 " <<<"$FR_OUT"; then echo "ok   GEVAL_objects: exactly 19 point(s) baked"; else echo "FAIL GEVAL_objects: expected 19 baked objects"; fail=1; fi
fr_chk 1 "0" "circle, t = 0: the point (3, 2, 0)"
fr_chk 2 "0" "circle, t = 0: the unit tangent (0, 1, 0)"
fr_chk 3 "0" "circle, t = 0.25: the point (1, 4, 0)"
fr_chk 4 "0" "circle, t = 0.25: the unit tangent (-1, 0, 0)"
fr_chk 5 "0" "circle, t = 0.5: the point (-1, 2, 0)"
fr_chk 6 "0" "circle, t = 0.5: the unit tangent (0, -1, 0)"
fr_chk 7 "0" "circle, t = 0.75: the point (1, 0, 0)"
fr_chk 8 "0" "circle, t = 0.75: the unit tangent (1, 0, 0)"
fr_chk 9 "0" "line, t = 0: the point (0, 0, 0)"
fr_chk 10 "1" "line, t = 0: the point (0, 0, 0) (the node under test produced the value)"
fr_chk 11 "0" "line, t = 0: the unit tangent (0.6,0.8,0)"
fr_chk 12 "0" "line, t = 0.5: the point (1.5, 2, 0)"
fr_chk 13 "0" "line, t = 0.5: the unit tangent (0.6,0.8,0)"
fr_chk 14 "0" "line, t = 1: the point (3, 4, 0)"
fr_chk 15 "0" "line, t = 1: the unit tangent (0.6,0.8,0)"
fr_chk 16 "0" "arc, t = 0.5: the point at 45 degrees (sqrt 2, sqrt 2)"
fr_chk 17 "0" "arc, t = 0.5: the unit tangent (-sqrt(1/2), sqrt(1/2))"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
