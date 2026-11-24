# ---------------------------------------------------------------------------
# BFNORM_: Flow Register row geometry_nodes/Normal (Normal), flowrows_gen.py. The graph flowrows_bfnorm.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: cube file faces: f0 (z = 0) -Z, f1 (z = 1) +Z, f2 (y = 0) -Y, f3 (y = 1) +Y, f4 (x = 0) -X, f5 (x = 1) +X; a cube vertex normal is the diagonal outward (+-1,+-1,+-1)/sqrt 3 (three faces at equal angles, so any angle weights give it); strip vertex 2 and triangle f2 are flat and counter-clockwise: +Z; icosahedron poles (0,0,+-1) by symmetry. Single precision vertices: within 5e-6.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfnorm.dflow" > "$TMPW/flowrows_bfnorm.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfnorm.dflow|g" "$HERE/flowrows_bfnorm_script.txt" > "$TMPW/flowrows_bfnorm_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfnorm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFNORM_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfnorm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFNORM_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFNORM_$1: $3 (measured '$got')"; else echo "FAIL BFNORM_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 247 node(s)" <<<"$FR_OUT"; then echo "ok   BFNORM_solve: the player solved all 247 nodes (every node type is registered)"; else echo "FAIL BFNORM_solve: expected 'solved 247 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 43 " <<<"$FR_OUT"; then echo "ok   BFNORM_objects: exactly 43 point(s) baked"; else echo "FAIL BFNORM_objects: expected 43 baked objects"; fail=1; fi
fr_chk 1 "0" "cube face 0 faces (0, 0, -1) X"
fr_chk 2 "0" "cube face 0 faces (0, 0, -1) Y"
fr_chk 3 "0" "cube face 0 faces (0, 0, -1) Z"
fr_chk 4 "0" "cube face 1 faces (0, 0, 1) X"
fr_chk 5 "0" "cube face 1 faces (0, 0, 1) Y"
fr_chk 6 "0" "cube face 1 faces (0, 0, 1) Z"
fr_chk 7 "0" "cube face 2 faces (0, -1, 0) X"
fr_chk 8 "0" "cube face 2 faces (0, -1, 0) Y"
fr_chk 9 "0" "cube face 2 faces (0, -1, 0) Z"
fr_chk 10 "0" "cube face 3 faces (0, 1, 0) X"
fr_chk 11 "0" "cube face 3 faces (0, 1, 0) Y"
fr_chk 12 "0" "cube face 3 faces (0, 1, 0) Z"
fr_chk 13 "0" "cube face 4 faces (-1, 0, 0) X"
fr_chk 14 "0" "cube face 4 faces (-1, 0, 0) Y"
fr_chk 15 "0" "cube face 4 faces (-1, 0, 0) Z"
fr_chk 16 "0" "cube face 5 faces (1, 0, 0) X"
fr_chk 17 "0" "cube face 5 faces (1, 0, 0) Y"
fr_chk 18 "0" "cube face 5 faces (1, 0, 0) Z"
fr_chk 19 "0" "cube corner vertex 0 points along its diagonal (the 3 faces meet at equal angles) X"
fr_chk 20 "0" "cube corner vertex 0 points along its diagonal (the 3 faces meet at equal angles) Y"
fr_chk 21 "0" "cube corner vertex 0 points along its diagonal (the 3 faces meet at equal angles) Z"
fr_chk 22 "0" "cube corner vertex 6 points along its diagonal (the 3 faces meet at equal angles) X"
fr_chk 23 "0" "cube corner vertex 6 points along its diagonal (the 3 faces meet at equal angles) Y"
fr_chk 24 "0" "cube corner vertex 6 points along its diagonal (the 3 faces meet at equal angles) Z"
fr_chk 25 "0" "cube corner vertex 1 points along its diagonal (the 3 faces meet at equal angles) X"
fr_chk 26 "0" "cube corner vertex 1 points along its diagonal (the 3 faces meet at equal angles) Y"
fr_chk 27 "0" "cube corner vertex 1 points along its diagonal (the 3 faces meet at equal angles) Z"
fr_chk 28 "0" "cube corner vertex 3 points along its diagonal (the 3 faces meet at equal angles) X"
fr_chk 29 "0" "cube corner vertex 3 points along its diagonal (the 3 faces meet at equal angles) Y"
fr_chk 30 "0" "cube corner vertex 3 points along its diagonal (the 3 faces meet at equal angles) Z"
fr_chk 31 "0" "strip triangle f2 is counter-clockwise: +Z X"
fr_chk 32 "0" "strip triangle f2 is counter-clockwise: +Z Y"
fr_chk 33 "0" "strip triangle f2 is counter-clockwise: +Z Z"
fr_chk 34 "0" "strip vertex 2 lies in three flat faces: +Z X"
fr_chk 35 "0" "strip vertex 2 lies in three flat faces: +Z Y"
fr_chk 36 "0" "strip vertex 2 lies in three flat faces: +Z Z"
fr_chk 37 "0" "icosahedron top vertex (0,0,1) points along +Z by symmetry X"
fr_chk 38 "0" "icosahedron top vertex (0,0,1) points along +Z by symmetry Y"
fr_chk 39 "0" "icosahedron top vertex (0,0,1) points along +Z by symmetry Z"
fr_chk 40 "0" "and the bottom one along -Z X"
fr_chk 41 "0" "and the bottom one along -Z Y"
fr_chk 42 "0" "and the bottom one along -Z Z"
fr_chk 43 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
