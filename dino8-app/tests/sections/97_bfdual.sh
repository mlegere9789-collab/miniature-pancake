# ---------------------------------------------------------------------------
# BFDUAL_: Flow Register row geometry_nodes/Dual Mesh (Dual Mesh), flowrows_gen.py. The graph flowrows_bfdual.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: vertices at face centres, one face per original vertex: cube (side 1) -> octahedron 6/12/8 with volume 4/3 * 0.5^3; its dual -> cube of side 1/3 (volume 1/27); icosahedron (R = 1) -> dodecahedron 20/30/12 (12 pentagons, 60 corners) of volume b^3 (15 + 7 sqrt 5)/4 with b = 4 rho / (sqrt 3 (1 + sqrt 5)), rho = a (3 sqrt 3 + sqrt 15)/12, a = 1/sin(2 pi/5); an 8-gon prism (16 vertices, 10 faces) has 10 dual vertices, 16 dual faces and 24 edges (Euler).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfdual.dflow|g" "$HERE/flowrows_bfdual_script.txt" > "$TMPW/flowrows_bfdual_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfdual_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFDUAL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfdual_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFDUAL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFDUAL_$1: $3 (measured '$got')"; else echo "FAIL BFDUAL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 99 node(s)" <<<"$FR_OUT"; then echo "ok   BFDUAL_solve: the player solved all 99 nodes (every node type is registered)"; else echo "FAIL BFDUAL_solve: expected 'solved 99 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 25 " <<<"$FR_OUT"; then echo "ok   BFDUAL_objects: exactly 25 point(s) baked"; else echo "FAIL BFDUAL_objects: expected 25 baked objects"; fail=1; fi
fr_chk 1 "6" "dual of a cube is an octahedron:: 6 points"
fr_chk 2 "12" "dual of a cube is an octahedron:: 12 edges"
fr_chk 3 "8" "dual of a cube is an octahedron:: 8 faces"
fr_chk 4 "24" "dual of a cube is an octahedron:: 24 corners"
fr_chk 5 "0" "volume 4/3 d^3 for vertices at distance d = 0.5 from the centre"
fr_chk 6 "0" "its vertices are the face centres: min X"
fr_chk 7 "0" "its vertices are the face centres: min Y"
fr_chk 8 "0" "its vertices are the face centres: min Z"
fr_chk 9 "0" "its vertices are the face centres: max X"
fr_chk 10 "0" "its vertices are the face centres: max Y"
fr_chk 11 "0" "its vertices are the face centres: max Z"
fr_chk 12 "8" "dual of the octahedron is a cube again:: 8 points"
fr_chk 13 "12" "dual of the octahedron is a cube again:: 12 edges"
fr_chk 14 "6" "dual of the octahedron is a cube again:: 6 faces"
fr_chk 15 "24" "dual of the octahedron is a cube again:: 24 corners"
fr_chk 16 "0" "of side 1/3 (the octahedron's face centres are at +-1/6): volume 1/27"
fr_chk 17 "20" "dual of the icosahedron: a dodecahedron, 12 pentagons:: 20 points"
fr_chk 18 "30" "dual of the icosahedron: a dodecahedron, 12 pentagons:: 30 edges"
fr_chk 19 "12" "dual of the icosahedron: a dodecahedron, 12 pentagons:: 12 faces"
fr_chk 20 "60" "dual of the icosahedron: a dodecahedron, 12 pentagons:: 60 corners"
fr_chk 21 "0" "dodecahedron volume b^3 (15 + 7 sqrt 5) / 4 with edge b = 4 rho / (sqrt 3 (1 + sqrt 5)), rho = inradius of the unit-circumradius icosahedron"
fr_chk 22 "10" "dual of an 8-gon prism (8 quads + 2 caps = 10 vertices, 16 vertices -> 16 faces):: 10 points"
fr_chk 23 "24" "dual of an 8-gon prism (8 quads + 2 caps = 10 vertices, 16 vertices -> 16 faces):: 24 edges"
fr_chk 24 "16" "dual of an 8-gon prism (8 quads + 2 caps = 10 vertices, 16 vertices -> 16 faces):: 16 faces"
fr_chk 25 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
