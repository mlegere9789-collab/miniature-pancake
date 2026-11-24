# ---------------------------------------------------------------------------
# BFIMPO_: Flow Register row geometry_nodes/Import OBJ (Import OBJ), flowrows_gen.py. The graph flowrows_bfimpo.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the unit cube file (8 vertices, 6 outward quads) reads back as V 8, E 12, F 6, corners 24, volume 1, area 6, box [0,1]^3; the strip file (7 + 3 vertices; quads 1-2-3-4 and 2-5-6-3, triangles 4-3-7 and 8-9-10) has 10 vertices, 12 distinct edges, 4 faces, 14 corners, area 1 + 1 + 0.5 + 0.5 = 3, box x 0..6, y 0..6, z 0..5. No axis conversion is applied: the Blender importer's default axis settings are recalled, not checked.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfimpo.dflow" > "$TMPW/flowrows_bfimpo.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfimpo.dflow|g" "$HERE/flowrows_bfimpo_script.txt" > "$TMPW/flowrows_bfimpo_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimpo_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPO_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimpo_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPO_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFIMPO_$1: $3 (measured '$got')"; else echo "FAIL BFIMPO_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 110 node(s)" <<<"$FR_OUT"; then echo "ok   BFIMPO_solve: the player solved all 110 nodes (every node type is registered)"; else echo "FAIL BFIMPO_solve: expected 'solved 110 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 24 " <<<"$FR_OUT"; then echo "ok   BFIMPO_objects: exactly 24 point(s) baked"; else echo "FAIL BFIMPO_objects: expected 24 baked objects"; fail=1; fi
fr_chk 1 "8" "the cube file has 8 v, 6 quad f:: 8 points"
fr_chk 2 "12" "the cube file has 8 v, 6 quad f:: 12 edges"
fr_chk 3 "6" "the cube file has 8 v, 6 quad f:: 6 faces"
fr_chk 4 "24" "the cube file has 8 v, 6 quad f:: 24 corners"
fr_chk 5 "0" "volume of the unit cube 1"
fr_chk 6 "0" "area 6"
fr_chk 7 "0" "box [0,1]^3 in the file's own coordinates: min X"
fr_chk 8 "0" "box [0,1]^3 in the file's own coordinates: min Y"
fr_chk 9 "0" "box [0,1]^3 in the file's own coordinates: min Z"
fr_chk 10 "0" "box [0,1]^3 in the file's own coordinates: max X"
fr_chk 11 "0" "box [0,1]^3 in the file's own coordinates: max Y"
fr_chk 12 "0" "box [0,1]^3 in the file's own coordinates: max Z"
fr_chk 13 "10" "the strip file (2 quads, 2 triangles, 12 distinct edges):: 10 points"
fr_chk 14 "12" "the strip file (2 quads, 2 triangles, 12 distinct edges):: 12 edges"
fr_chk 15 "4" "the strip file (2 quads, 2 triangles, 12 distinct edges):: 4 faces"
fr_chk 16 "14" "the strip file (2 quads, 2 triangles, 12 distinct edges):: 14 corners"
fr_chk 17 "0" "area 1 + 1 + 0.5 + 0.5"
fr_chk 18 "0" "box of the 10 vertices: x 0..6, y 0..6, z 0..5: min X"
fr_chk 19 "0" "box of the 10 vertices: x 0..6, y 0..6, z 0..5: min Y"
fr_chk 20 "0" "box of the 10 vertices: x 0..6, y 0..6, z 0..5: min Z"
fr_chk 21 "0" "box of the 10 vertices: x 0..6, y 0..6, z 0..5: max X"
fr_chk 22 "0" "box of the 10 vertices: x 0..6, y 0..6, z 0..5: max Y"
fr_chk 23 "0" "box of the 10 vertices: x 0..6, y 0..6, z 0..5: max Z"
fr_chk 24 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
