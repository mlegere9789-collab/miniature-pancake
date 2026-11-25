# ---------------------------------------------------------------------------
# BFISL_: Flow Register row geometry_nodes/Mesh Island (Mesh Island), flowrows_gen.py. The graph flowrows_bfisl.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: strip file: vertices 0-6 are joined by faces (piece 0), vertices 7-9 form the separate triangle (piece 1): 2 pieces; a cube is 1; two cubes side by side are 2 with vertex 8 first in the second; 4 coincident loose points are 4 pieces; a grid is 1. Pieces are numbered by their lowest vertex (a convention that is not checked against Blender's numbering).
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
cat > "$TMPW/strip.obj" <<'EOF_BF'
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 2 0 0
v 2 1 0
v 0.5 2 0
v 5 5 5
v 6 5 5
v 5 6 5
f 1 2 3 4
f 2 5 6 3
f 4 3 7
f 8 9 10
EOF_BF
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfisl.dflow" > "$TMPW/flowrows_bfisl.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfisl.dflow|g" "$HERE/flowrows_bfisl_script.txt" > "$TMPW/flowrows_bfisl_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfisl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFISL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfisl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFISL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFISL_$1: $3 (measured '$got')"; else echo "FAIL BFISL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 42 node(s)" <<<"$FR_OUT"; then echo "ok   BFISL_solve: the player solved all 42 nodes (every node type is registered)"; else echo "FAIL BFISL_solve: expected 'solved 42 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 12 " <<<"$FR_OUT"; then echo "ok   BFISL_objects: exactly 12 point(s) baked"; else echo "FAIL BFISL_objects: expected 12 baked objects"; fail=1; fi
fr_chk 1 "2" "the strip file has 2 pieces"
fr_chk 2 "0" "strip vertex 0 is in piece 0 (pieces numbered by their lowest vertex)"
fr_chk 3 "0" "strip vertex 3 is in piece 0 (pieces numbered by their lowest vertex)"
fr_chk 4 "0" "strip vertex 6 is in piece 0 (pieces numbered by their lowest vertex)"
fr_chk 5 "1" "strip vertex 7 is in piece 1 (pieces numbered by their lowest vertex)"
fr_chk 6 "1" "strip vertex 9 is in piece 1 (pieces numbered by their lowest vertex)"
fr_chk 7 "1" "a cube is one piece"
fr_chk 8 "2" "two joined cubes are 2 pieces"
fr_chk 9 "1" "vertex 8 is the first vertex of the second cube"
fr_chk 10 "4" "4 loose points are 4 pieces"
fr_chk 11 "3" "the point 3 is piece 3"
fr_chk 12 "1" "a grid is one piece"
