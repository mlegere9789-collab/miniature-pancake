# ---------------------------------------------------------------------------
# BFSPLT_: Flow Register row geometry_nodes/Split Edges (Split Edges), flowrows_gen.py. The graph flowrows_bfsplt.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: every face gets private vertices: the cube file's 6 quads become 24 vertices, 24 edges, 6 faces and 6 separate pieces with area 6; the strip file's 4 faces (14 corners) become 14 vertices, 14 edges; the default cylinder keeps 34 faces (its 32-gon caps stay one face each) with 32 * 4 + 2 * 32 = 192 vertices and 192 edges and the same area; a 3-edge line becomes 6 vertices; Selection false changes nothing.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfsplt.dflow" > "$TMPW/flowrows_bfsplt.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfsplt.dflow|g" "$HERE/flowrows_bfsplt_script.txt" > "$TMPW/flowrows_bfsplt_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsplt_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSPLT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsplt_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSPLT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSPLT_$1: $3 (measured '$got')"; else echo "FAIL BFSPLT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 71 node(s)" <<<"$FR_OUT"; then echo "ok   BFSPLT_solve: the player solved all 71 nodes (every node type is registered)"; else echo "FAIL BFSPLT_solve: expected 'solved 71 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 22 " <<<"$FR_OUT"; then echo "ok   BFSPLT_objects: exactly 22 point(s) baked"; else echo "FAIL BFSPLT_objects: expected 22 baked objects"; fail=1; fi
fr_chk 1 "24" "6 loose quads: 6 * 4 vertices, 6 * 4 edges:: 24 points"
fr_chk 2 "24" "6 loose quads: 6 * 4 vertices, 6 * 4 edges:: 24 edges"
fr_chk 3 "6" "6 loose quads: 6 * 4 vertices, 6 * 4 edges:: 6 faces"
fr_chk 4 "24" "6 loose quads: 6 * 4 vertices, 6 * 4 edges:: 24 corners"
fr_chk 5 "0" "area unchanged: 6"
fr_chk 6 "6" "6 separate pieces"
fr_chk 7 "14" "strip: 14 corners = 14 vertices = 14 edges:: 14 points"
fr_chk 8 "14" "strip: 14 corners = 14 vertices = 14 edges:: 14 edges"
fr_chk 9 "4" "strip: 14 corners = 14 vertices = 14 edges:: 4 faces"
fr_chk 10 "14" "strip: 14 corners = 14 vertices = 14 edges:: 14 corners"
fr_chk 11 "192" "default cylinder: 32 quads + two 32-gon caps stay faces, each with its own vertices:: 192 points"
fr_chk 12 "192" "default cylinder: 32 quads + two 32-gon caps stay faces, each with its own vertices:: 192 edges"
fr_chk 13 "34" "default cylinder: 32 quads + two 32-gon caps stay faces, each with its own vertices:: 34 faces"
fr_chk 14 "192" "default cylinder: 32 quads + two 32-gon caps stay faces, each with its own vertices:: 192 corners"
fr_chk 15 "0" "area unchanged: sides 32 * 2 sin(pi/32) * 2 + 2 caps"
fr_chk 16 "8" "Selection false changes nothing:: 8 points"
fr_chk 17 "12" "Selection false changes nothing:: 12 edges"
fr_chk 18 "6" "Selection false changes nothing:: 6 faces"
fr_chk 19 "6" "3 loose edges of a 4-vertex line: 6 vertices:: 6 points"
fr_chk 20 "3" "3 loose edges of a 4-vertex line: 6 vertices:: 3 edges"
fr_chk 21 "0" "3 loose edges of a 4-vertex line: 6 vertices:: 0 faces"
fr_chk 22 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
