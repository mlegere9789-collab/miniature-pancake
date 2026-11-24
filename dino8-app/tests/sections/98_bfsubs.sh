# ---------------------------------------------------------------------------
# BFSUBS_: Flow Register row geometry_nodes/Subdivision Surface (Subdivision Surface), flowrows_gen.py. The graph flowrows_bfsubs.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Catmull-Clark: V' = V + E + F, E' = 2E + corners, F' = corners (every face of k corners gives k quads): cube 8/12/6 -> 26/48/24 -> 98/192/96; icosahedron 12/30/20 -> 62/120/60; the 8-gon prism 16/24/10 -> 50/96/48; level 1 of the unit cube (hand-derived: about the centre, corners move to 5/9 of the half side, edge points to 3/4 on two axes, face points stay on the faces) has its volume summed by the independent routine cc_cube_volume() in flowrows5_gen.py, box [0,1]^3. Open meshes (boundary rules) are untested.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfsubs.dflow" > "$TMPW/flowrows_bfsubs.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfsubs.dflow|g" "$HERE/flowrows_bfsubs_script.txt" > "$TMPW/flowrows_bfsubs_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsubs_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSUBS_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsubs_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSUBS_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSUBS_$1: $3 (measured '$got')"; else echo "FAIL BFSUBS_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 96 node(s)" <<<"$FR_OUT"; then echo "ok   BFSUBS_solve: the player solved all 96 nodes (every node type is registered)"; else echo "FAIL BFSUBS_solve: expected 'solved 96 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 27 " <<<"$FR_OUT"; then echo "ok   BFSUBS_objects: exactly 27 point(s) baked"; else echo "FAIL BFSUBS_objects: expected 27 baked objects"; fail=1; fi
fr_chk 1 "26" "cube level 1: V + E + F, 2E + corners, corners:: 26 points"
fr_chk 2 "48" "cube level 1: V + E + F, 2E + corners, corners:: 48 edges"
fr_chk 3 "24" "cube level 1: V + E + F, 2E + corners, corners:: 24 faces"
fr_chk 4 "96" "cube level 1: V + E + F, 2E + corners, corners:: 96 corners"
fr_chk 5 "0" "volume of the hand-derived level-1 solid (corners at 5/9, edge points at 3/4, face points at 1 of the half side)"
fr_chk 6 "0" "face points stay on the faces: the box is the original [0,1]^3: min X"
fr_chk 7 "0" "face points stay on the faces: the box is the original [0,1]^3: min Y"
fr_chk 8 "0" "face points stay on the faces: the box is the original [0,1]^3: min Z"
fr_chk 9 "0" "face points stay on the faces: the box is the original [0,1]^3: max X"
fr_chk 10 "0" "face points stay on the faces: the box is the original [0,1]^3: max Y"
fr_chk 11 "0" "face points stay on the faces: the box is the original [0,1]^3: max Z"
fr_chk 12 "98" "level 2: 26 + 48 + 24 = 98, 2 * 48 + 96 = 192, 96:: 98 points"
fr_chk 13 "192" "level 2: 26 + 48 + 24 = 98, 2 * 48 + 96 = 192, 96:: 192 edges"
fr_chk 14 "96" "level 2: 26 + 48 + 24 = 98, 2 * 48 + 96 = 192, 96:: 96 faces"
fr_chk 15 "384" "level 2: 26 + 48 + 24 = 98, 2 * 48 + 96 = 192, 96:: 384 corners"
fr_chk 16 "62" "icosahedron: each triangle becomes 3 quads, so 62 / 120 / 60 (Euler 2):: 62 points"
fr_chk 17 "120" "icosahedron: each triangle becomes 3 quads, so 62 / 120 / 60 (Euler 2):: 120 edges"
fr_chk 18 "60" "icosahedron: each triangle becomes 3 quads, so 62 / 120 / 60 (Euler 2):: 60 faces"
fr_chk 19 "240" "icosahedron: each triangle becomes 3 quads, so 62 / 120 / 60 (Euler 2):: 240 corners"
fr_chk 20 "8" "Level 0 changes nothing:: 8 points"
fr_chk 21 "12" "Level 0 changes nothing:: 12 edges"
fr_chk 22 "6" "Level 0 changes nothing:: 6 faces"
fr_chk 23 "50" "8-gon prism (16 vertices, 24 edges, 10 faces with 48 corners): V + E + F = 50, 2E + corners = 96, corners = 48:: 50 points"
fr_chk 24 "96" "8-gon prism (16 vertices, 24 edges, 10 faces with 48 corners): V + E + F = 50, 2E + corners = 96, corners = 48:: 96 edges"
fr_chk 25 "48" "8-gon prism (16 vertices, 24 edges, 10 faces with 48 corners): V + E + F = 50, 2E + corners = 96, corners = 48:: 48 faces"
fr_chk 26 "192" "8-gon prism (16 vertices, 24 edges, 10 faces with 48 corners): V + E + F = 50, 2E + corners = 96, corners = 48:: 192 corners"
fr_chk 27 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
