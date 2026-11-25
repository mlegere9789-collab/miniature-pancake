# ---------------------------------------------------------------------------
# GCURV_: Flow Register row D8-GH-CURVE-0003 (Curve Curvature), flowgeo_gen.py. The graph flowgeo_gcurv.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: a circle of radius R has curvature 1/R everywhere, the vector pointing at the centre: R = 2.5 at the origin gives length 0.4 at t = 0.1, 0.5, 0.9 and centre = point + vector * R^2 = (0,0,0); the arc of radius 4 (0 to 90 degrees) has curvature 0.25; the circle of radius 2 about (1,2,3) in the plane with normal (1,1,1) has curvature 0.5 and the same centre property; a straight line has curvature 0.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gcurv.dflow|g" "$HERE/flowgeo_gcurv_script.txt" > "$TMPW/flowgeo_gcurv_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcurv_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCURV_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcurv_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCURV_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GCURV_$1: $3 (measured '$got')"; else echo "FAIL GCURV_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 112 node(s)" <<<"$FR_OUT"; then echo "ok   GCURV_solve: the player solved all 112 nodes (every node type is registered)"; else echo "FAIL GCURV_solve: expected 'solved 112 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 19 " <<<"$FR_OUT"; then echo "ok   GCURV_objects: exactly 19 point(s) baked"; else echo "FAIL GCURV_objects: expected 19 baked objects"; fail=1; fi
fr_chk 1 "0" "circle R = 2.5, t = 0.1: curvature 1/R = 0.4"
fr_chk 2 "0" "circle R = 2.5, t = 0.1: point + vector * R^2 is the centre"
fr_chk 3 "1" "circle R = 2.5, t = 0.1: point + vector * R^2 is the centre (the node under test produced the value)"
fr_chk 4 "0" "circle R = 2.5, t = 0.5: curvature 1/R = 0.4"
fr_chk 5 "0" "circle R = 2.5, t = 0.5: point + vector * R^2 is the centre"
fr_chk 6 "1" "circle R = 2.5, t = 0.5: point + vector * R^2 is the centre (the node under test produced the value)"
fr_chk 7 "0" "circle R = 2.5, t = 0.9: curvature 1/R = 0.4"
fr_chk 8 "0" "circle R = 2.5, t = 0.9: point + vector * R^2 is the centre"
fr_chk 9 "1" "circle R = 2.5, t = 0.9: point + vector * R^2 is the centre (the node under test produced the value)"
fr_chk 10 "0" "arc R = 4: curvature 0.25"
fr_chk 11 "0" "arc R = 4: point + vector * R^2 is the centre (0,0,0)"
fr_chk 12 "1" "arc R = 4: point + vector * R^2 is the centre (0,0,0) (the node under test produced the value)"
fr_chk 13 "0" "circle R = 2 in a tilted plane: curvature 0.5"
fr_chk 14 "0" "tilted circle: point + vector * R^2 is the centre (1,2,3)"
fr_chk 15 "0" "line: curvature 0"
fr_chk 16 "1" "line: curvature 0 (the node under test produced the value)"
fr_chk 17 "1" "line: the curvature vector exists (the node under test produced the value)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
