# ---------------------------------------------------------------------------
# BFSEP_: Flow Register row geometry_nodes/Shortest Edge Paths (Shortest Edge Paths), flowrows_gen.py. The graph flowrows_bfsep.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: graph distances on the strip file (edges 0-1 0-3 1-2 1-4 2-3 2-5 2-6 3-6 4-5 | 7-8 7-9 8-9) to End Vertex 5: vertex 4 is 1 edge (next 5), 6 and 3 are 2 edges (next 2), 1 is 2, 0 is 3 (7.5 at 2.5 per edge); the end vertex is 0 and its own next vertex; a 5-vertex line at 0.5 per edge: vertex 4 costs 2, next 3; icosahedron: pole to the opposite pole 3, pole to a lower-ring vertex 2. Ties between equal paths go to the lowest vertex number (own rule, untested against Blender).
# ---------------------------------------------------------------------------
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfsep.dflow" > "$TMPW/flowrows_bfsep.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfsep.dflow|g" "$HERE/flowrows_bfsep_script.txt" > "$TMPW/flowrows_bfsep_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsep_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSEP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsep_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSEP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSEP_$1: $3 (measured '$got')"; else echo "FAIL BFSEP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 79 node(s)" <<<"$FR_OUT"; then echo "ok   BFSEP_solve: the player solved all 79 nodes (every node type is registered)"; else echo "FAIL BFSEP_solve: expected 'solved 79 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 16 " <<<"$FR_OUT"; then echo "ok   BFSEP_objects: exactly 16 point(s) baked"; else echo "FAIL BFSEP_objects: expected 16 baked objects"; fail=1; fi
fr_chk 1 "0" "strip, end vertex 5: vertex 4 costs 1 (4 is next to the end 5)"
fr_chk 2 "5" "its next vertex is 5"
fr_chk 3 "0" "strip, end vertex 5: vertex 6 costs 2 (6 -> 2 -> 5 (6 -> 3 -> 2 -> 5 is longer))"
fr_chk 4 "2" "its next vertex is 2"
fr_chk 5 "0" "strip, end vertex 5: vertex 3 costs 2 (3 -> 2 -> 5)"
fr_chk 6 "2" "its next vertex is 2"
fr_chk 7 "0" "strip, end vertex 5: vertex 0 costs 3 (0 needs 3 edges to reach 5)"
fr_chk 8 "0" "strip, end vertex 5: vertex 1 costs 2 (1 -> 2 -> 5 or 1 -> 4 -> 5)"
fr_chk 9 "0" "with every edge costing 2.5 the 3 edges from vertex 0 cost 7.5"
fr_chk 10 "0" "the end vertex itself costs 0"
fr_chk 11 "5" "and is its own next vertex"
fr_chk 12 "0" "line of 5, end vertex 0, 0.5 per edge: vertex 4 is 4 edges away = 2"
fr_chk 13 "3" "its next vertex is 3"
fr_chk 14 "0" "icosahedron: pole to pole is 3 edges"
fr_chk 15 "0" "pole to a vertex of the lower ring is 2 edges"
fr_chk 16 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
