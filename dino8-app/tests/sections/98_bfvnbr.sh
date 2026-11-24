# ---------------------------------------------------------------------------
# BFVNBR_: Flow Register row geometry_nodes/Vertex Neighbors (Vertex Neighbors), flowrows_gen.py. The graph flowrows_bfvnbr.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: from the strip file: vertex 2 has edges to 1, 3, 5, 6 and lies in 3 faces; vertex 1 3 edges 2 faces; vertex 0 2 edges 1 face; vertex 3 3 edges 2 faces; cube 3 and 3; a default cylinder rim vertex 3 edges, 3 faces (the cap n-gon counts once); icosahedron vertex 5 and 5; 3 x 3 grid centre 4 and 4, corner 2 and 1.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfvnbr.dflow" > "$TMPW/flowrows_bfvnbr.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfvnbr.dflow|g" "$HERE/flowrows_bfvnbr_script.txt" > "$TMPW/flowrows_bfvnbr_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfvnbr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFVNBR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfvnbr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFVNBR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFVNBR_$1: $3 (measured '$got')"; else echo "FAIL BFVNBR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 65 node(s)" <<<"$FR_OUT"; then echo "ok   BFVNBR_solve: the player solved all 65 nodes (every node type is registered)"; else echo "FAIL BFVNBR_solve: expected 'solved 65 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 24 " <<<"$FR_OUT"; then echo "ok   BFVNBR_objects: exactly 24 point(s) baked"; else echo "FAIL BFVNBR_objects: expected 24 baked objects"; fail=1; fi
fr_chk 1 "4" "vertex 2: 4 edges"
fr_chk 2 "3" "vertex 2: 3 faces"
fr_chk 3 "3" "vertex 1: 3 edges"
fr_chk 4 "2" "vertex 1: 2 faces"
fr_chk 5 "2" "vertex 0: 2 edges"
fr_chk 6 "1" "vertex 0: 1 faces"
fr_chk 7 "3" "vertex 3: 3 edges"
fr_chk 8 "2" "vertex 3: 2 faces"
fr_chk 9 "2" "vertex 6: 2 edges"
fr_chk 10 "1" "vertex 6: 1 faces"
fr_chk 11 "2" "vertex 7: 2 edges"
fr_chk 12 "1" "vertex 7: 1 faces"
fr_chk 13 "3" "vertex 0: 3 edges"
fr_chk 14 "3" "vertex 0: 3 faces"
fr_chk 15 "3" "vertex 7: 3 edges"
fr_chk 16 "3" "vertex 7: 3 faces"
fr_chk 17 "3" "a cylinder rim vertex has 3 edges"
fr_chk 18 "3" "and 3 faces: two quads and the cap n-gon, counted once"
fr_chk 19 "5" "an icosahedron vertex has 5 edges"
fr_chk 20 "5" "and 5 faces"
fr_chk 21 "4" "the centre vertex of a 3 x 3 grid has 4 edges"
fr_chk 22 "4" "and 4 faces"
fr_chk 23 "2" "a grid corner has 2 edges"
fr_chk 24 "1" "and 1 face"
