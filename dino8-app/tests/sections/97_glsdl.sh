# ---------------------------------------------------------------------------
# GLSDL_: Flow Register row D8-GH-CURVE-0039 (Line SDL), flowgeo_gen.py. The graph flowgeo_glsdl.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: end = start + unit(direction) * length: (1,2,3) along (0,3,4) (unit (0,0.6,0.8)) for 10 ends at (1,8,11); (0,0,0) along (2,0,0) for 5 ends at (5,0,0); (0,0,0) along (1,1,1) (unit (1,1,1)/sqrt 3) for 3 sqrt 3 ends at (3,3,3); the line is that long.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_glsdl.dflow|g" "$HERE/flowgeo_glsdl_script.txt" > "$TMPW/flowgeo_glsdl_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_glsdl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLSDL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_glsdl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLSDL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GLSDL_$1: $3 (measured '$got')"; else echo "FAIL GLSDL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 78 node(s)" <<<"$FR_OUT"; then echo "ok   GLSDL_solve: the player solved all 78 nodes (every node type is registered)"; else echo "FAIL GLSDL_solve: expected 'solved 78 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   GLSDL_objects: exactly 13 point(s) baked"; else echo "FAIL GLSDL_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the line starts at the start point (1, 2, 3)"
fr_chk 2 "0" "case 1: the line ends at start + unit(direction) * length = (1, 8, 11)"
fr_chk 3 "0" "case 1: the line is 10 long"
fr_chk 4 "0" "case 2: the line starts at the start point (0, 0, 0)"
fr_chk 5 "1" "case 2: the line starts at the start point (0, 0, 0) (the node under test produced the value)"
fr_chk 6 "0" "case 2: the line ends at start + unit(direction) * length = (5, 0, 0)"
fr_chk 7 "0" "case 2: the line is 5 long"
fr_chk 8 "0" "case 3: the line starts at the start point (0, 0, 0)"
fr_chk 9 "1" "case 3: the line starts at the start point (0, 0, 0) (the node under test produced the value)"
fr_chk 10 "0" "case 3: the line ends at start + unit(direction) * length = (3, 3, 3)"
fr_chk 11 "0" "case 3: the line is 5.19615 long"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
