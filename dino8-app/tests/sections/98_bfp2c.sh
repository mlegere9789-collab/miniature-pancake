# ---------------------------------------------------------------------------
# BFP2C_: Flow Register row geometry_nodes/Points to Curves (Points to Curves), flowrows_gen.py. The graph flowrows_bfp2c.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: one poly spline through the points in vertex order: a 5-vertex line has length 4; the 8 vertices of the cube file in file order (0,0,0) (1,0,0) (1,1,0) (0,1,0) (0,0,1) (1,0,1) (1,1,1) (0,1,1) are joined by edges of 1, 1, 1, sqrt 2, 1, 1, 1: length 6 + sqrt 2. Curve Group ID and Weight are untested.
# ---------------------------------------------------------------------------
cat > "$TMPW/cube.obj" <<'EOF_BF'
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 0 0 1
v 1 0 1
v 1 1 1
v 0 1 1
f 1 4 3 2
f 5 6 7 8
f 1 2 6 5
f 4 8 7 3
f 1 5 8 4
f 2 3 7 6
EOF_BF
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfp2c.dflow" > "$TMPW/flowrows_bfp2c.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfp2c.dflow|g" "$HERE/flowrows_bfp2c_script.txt" > "$TMPW/flowrows_bfp2c_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfp2c_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFP2C_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfp2c_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFP2C_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFP2C_$1: $3 (measured '$got')"; else echo "FAIL BFP2C_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 66 node(s)" <<<"$FR_OUT"; then echo "ok   BFP2C_solve: the player solved all 66 nodes (every node type is registered)"; else echo "FAIL BFP2C_solve: expected 'solved 66 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   BFP2C_objects: exactly 13 point(s) baked"; else echo "FAIL BFP2C_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "5" "5 points in a line:: 5 points"
fr_chk 2 "1" "5 points in a line:: 1 splines"
fr_chk 3 "0" "length 4 (4 edges of 1)"
fr_chk 4 "0" "the 8 cube-file vertices in file order: 1 + 1 + 1 + sqrt 2 + 1 + 1 + 1"
fr_chk 5 "8" "8 points on one spline:: 8 points"
fr_chk 6 "1" "8 points on one spline:: 1 splines"
fr_chk 7 "0" "box of the cube: min X"
fr_chk 8 "0" "box of the cube: min Y"
fr_chk 9 "0" "box of the cube: min Z"
fr_chk 10 "0" "box of the cube: max X"
fr_chk 11 "0" "box of the cube: max Y"
fr_chk 12 "0" "box of the cube: max Z"
fr_chk 13 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
