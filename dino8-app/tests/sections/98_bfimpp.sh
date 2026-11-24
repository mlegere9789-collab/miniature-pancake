# ---------------------------------------------------------------------------
# BFIMPP_: Flow Register row geometry_nodes/Import PLY (Import PLY), flowrows_gen.py. The graph flowrows_bfimpp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the ascii cube file reads back as V 8, E 12, F 6, volume 1, box [0,1]^3; the pentagon (0,0) (2,0) (2,1) (1,2) (0,1) has area 2 * 1 + 2 * 1 / 2 = 3 and is split into 5 - 2 = 3 triangles with 5 + 2 = 7 edges.
# ---------------------------------------------------------------------------
cat > "$TMPW/cube.ply" <<'EOF_BF'
ply
format ascii 1.0
element vertex 8
property float x
property float y
property float z
element face 6
property list uchar int vertex_indices
end_header
0 0 0
1 0 0
1 1 0
0 1 0
0 0 1
1 0 1
1 1 1
0 1 1
4 0 3 2 1
4 4 5 6 7
4 0 1 5 4
4 3 7 6 2
4 0 4 7 3
4 1 2 6 5
EOF_BF
cat > "$TMPW/house.ply" <<'EOF_BF'
ply
format ascii 1.0
element vertex 5
property float x
property float y
property float z
element face 1
property list uchar int vertex_indices
end_header
0 0 0
2 0 0
2 1 0
1 2 0
0 1 0
5 0 1 2 3 4
EOF_BF
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfimpp.dflow" > "$TMPW/flowrows_bfimpp.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfimpp.dflow|g" "$HERE/flowrows_bfimpp_script.txt" > "$TMPW/flowrows_bfimpp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimpp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimpp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFIMPP_$1: $3 (measured '$got')"; else echo "FAIL BFIMPP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 71 node(s)" <<<"$FR_OUT"; then echo "ok   BFIMPP_solve: the player solved all 71 nodes (every node type is registered)"; else echo "FAIL BFIMPP_solve: expected 'solved 71 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 17 " <<<"$FR_OUT"; then echo "ok   BFIMPP_objects: exactly 17 point(s) baked"; else echo "FAIL BFIMPP_objects: expected 17 baked objects"; fail=1; fi
fr_chk 1 "8" "the cube file (ascii, 8 vertices, 6 quads):: 8 points"
fr_chk 2 "12" "the cube file (ascii, 8 vertices, 6 quads):: 12 edges"
fr_chk 3 "6" "the cube file (ascii, 8 vertices, 6 quads):: 6 faces"
fr_chk 4 "24" "the cube file (ascii, 8 vertices, 6 quads):: 24 corners"
fr_chk 5 "0" "volume 1"
fr_chk 6 "0" "box [0,1]^3: min X"
fr_chk 7 "0" "box [0,1]^3: min Y"
fr_chk 8 "0" "box [0,1]^3: min Z"
fr_chk 9 "0" "box [0,1]^3: max X"
fr_chk 10 "0" "box [0,1]^3: max Y"
fr_chk 11 "0" "box [0,1]^3: max Z"
fr_chk 12 "5" "a pentagon is split into n - 2 = 3 triangles (7 edges: 5 + 2 diagonals):: 5 points"
fr_chk 13 "7" "a pentagon is split into n - 2 = 3 triangles (7 edges: 5 + 2 diagonals):: 7 edges"
fr_chk 14 "3" "a pentagon is split into n - 2 = 3 triangles (7 edges: 5 + 2 diagonals):: 3 faces"
fr_chk 15 "9" "a pentagon is split into n - 2 = 3 triangles (7 edges: 5 + 2 diagonals):: 9 corners"
fr_chk 16 "0" "house area 2 * 1 + (2 * 1) / 2 = 3"
fr_chk 17 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
