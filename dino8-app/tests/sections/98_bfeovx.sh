# ---------------------------------------------------------------------------
# BFEOVX_: Flow Register row geometry_nodes/Edges of Vertex (Edges of Vertex), flowrows_gen.py. The graph flowrows_bfeovx.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: valence read off the strip file: vertex 2 touches 1, 3, 5, 6 (4 edges), vertex 0 touches 1, 3 (2), vertex 1 touches 0, 2, 4 (3), vertex 6 touches 2, 3 (2), vertex 7 touches 8, 9 (2); every cube vertex has 3; a 5-vertex line has 1 edge at its ends and 2 inside. Edge numbers are Dino's own (position in the sorted pair list), not Blender's.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfeovx.dflow" > "$TMPW/flowrows_bfeovx.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfeovx.dflow|g" "$HERE/flowrows_bfeovx_script.txt" > "$TMPW/flowrows_bfeovx_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfeovx_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFEOVX_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfeovx_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFEOVX_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFEOVX_$1: $3 (measured '$got')"; else echo "FAIL BFEOVX_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 33 node(s)" <<<"$FR_OUT"; then echo "ok   BFEOVX_solve: the player solved all 33 nodes (every node type is registered)"; else echo "FAIL BFEOVX_solve: expected 'solved 33 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   BFEOVX_objects: exactly 13 point(s) baked"; else echo "FAIL BFEOVX_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "4" "vertex 2 has 4 edges"
fr_chk 2 "2" "vertex 0 has 2 edges"
fr_chk 3 "3" "vertex 1 has 3 edges"
fr_chk 4 "2" "vertex 6 has 2 edges"
fr_chk 5 "2" "vertex 7 has 2 edges"
fr_chk 6 "3" "vertex 0 has 3 edges"
fr_chk 7 "3" "vertex 6 has 3 edges"
fr_chk 8 "2 4 5 6" "vertex 2 of the strip: its edges (1,2) (2,3) (2,5) (2,6) are entries 2, 4, 5, 6 of the sorted pair list (this program's own numbering)"
fr_chk 9 "1" "the end of a 5-vertex line has 1 edge"
fr_chk 10 "2" "a middle vertex has 2"
