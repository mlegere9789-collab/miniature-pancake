# ---------------------------------------------------------------------------
# GLINE_: Flow Register row D8-GH-CURVE-0038 (Line), flowgeo_gen.py. The graph flowgeo_gline.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: |(3,4,0)| = 5 (3-4-5); (1,2,3)->(3,4,4) has the difference (2,2,1) of length 3; (-1,-1,-1)->(1,1,1) has (2,2,2) of length 2 sqrt 3; the middle of (0,0,0)->(3,4,0) is (1.5,2,0) and its unit tangent (3,4,0)/5 = (0.6,0.8,0); the end points come back as given.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gline.dflow|g" "$HERE/flowgeo_gline_script.txt" > "$TMPW/flowgeo_gline_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gline_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLINE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gline_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLINE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GLINE_$1: $3 (measured '$got')"; else echo "FAIL GLINE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 88 node(s)" <<<"$FR_OUT"; then echo "ok   GLINE_solve: the player solved all 88 nodes (every node type is registered)"; else echo "FAIL GLINE_solve: expected 'solved 88 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   GLINE_objects: exactly 14 point(s) baked"; else echo "FAIL GLINE_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "0" "(0,0,0)->(3,4,0) has length 5"
fr_chk 2 "0" "the start of the line is the start point given (0, 0, 0)"
fr_chk 3 "1" "the start of the line is the start point given (0, 0, 0) (the node under test produced the value)"
fr_chk 4 "0" "the end of the line is the end point given (3, 4, 0)"
fr_chk 5 "0" "the middle of (0,0,0)->(3,4,0) (t not given: the domain midpoint) is (1.5,2,0)"
fr_chk 6 "0" "its unit tangent is (0.6,0.8,0)"
fr_chk 7 "0" "(1,2,3)->(3,4,4) has length 3"
fr_chk 8 "0" "the start of the line is the start point given (1, 2, 3)"
fr_chk 9 "0" "the end of the line is the end point given (3, 4, 4)"
fr_chk 10 "0" "(-1,-1,-1)->(1,1,1) has length 2 sqrt 3"
fr_chk 11 "0" "the start of the line is the start point given (-1, -1, -1)"
fr_chk 12 "0" "the end of the line is the end point given (1, 1, 1)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
