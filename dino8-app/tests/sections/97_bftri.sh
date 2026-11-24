# ---------------------------------------------------------------------------
# BFTRI_: Flow Register row geometry_nodes/Triangulate (Triangulate), flowrows_gen.py. The graph flowrows_bftri.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: each quad becomes 2 triangles and adds one edge; an n-gon of k corners becomes k - 2 triangles and k - 3 edges; volume and area do not change: cube (8/12/6/24) -> 8/18/12/36; 8-gon prism (16 vertices, 8 quads + 2 caps) -> 16 + 2 * 6 = 28 triangles; Minimum Vertices above the corner count keeps the face (5 keeps quads; 9 keeps the 8-gon caps and the quads); 8-gon -> 6 triangles; 3 x 3 grid -> 8 triangles, 16 edges.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bftri.dflow|g" "$HERE/flowrows_bftri_script.txt" > "$TMPW/flowrows_bftri_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bftri_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFTRI_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bftri_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFTRI_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFTRI_$1: $3 (measured '$got')"; else echo "FAIL BFTRI_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 89 node(s)" <<<"$FR_OUT"; then echo "ok   BFTRI_solve: the player solved all 89 nodes (every node type is registered)"; else echo "FAIL BFTRI_solve: expected 'solved 89 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 26 " <<<"$FR_OUT"; then echo "ok   BFTRI_objects: exactly 26 point(s) baked"; else echo "FAIL BFTRI_objects: expected 26 baked objects"; fail=1; fi
fr_chk 1 "8" "cube: 6 quads -> 12 triangles, 6 new diagonals:: 8 points"
fr_chk 2 "18" "cube: 6 quads -> 12 triangles, 6 new diagonals:: 18 edges"
fr_chk 3 "12" "cube: 6 quads -> 12 triangles, 6 new diagonals:: 12 faces"
fr_chk 4 "36" "cube: 6 quads -> 12 triangles, 6 new diagonals:: 36 corners"
fr_chk 5 "0" "volume unchanged"
fr_chk 6 "0" "area unchanged"
fr_chk 7 "8" "Minimum Vertices 5 leaves quads alone:: 8 points"
fr_chk 8 "12" "Minimum Vertices 5 leaves quads alone:: 12 edges"
fr_chk 9 "6" "Minimum Vertices 5 leaves quads alone:: 6 faces"
fr_chk 10 "24" "Minimum Vertices 5 leaves quads alone:: 24 corners"
fr_chk 11 "16" "8-gon prism: 8 quads -> 16 triangles, 2 caps -> 6 triangles each:: 16 points"
fr_chk 12 "28" "8-gon prism: 8 quads -> 16 triangles, 2 caps -> 6 triangles each:: 28 faces"
fr_chk 13 "84" "8-gon prism: 8 quads -> 16 triangles, 2 caps -> 6 triangles each:: 84 corners"
fr_chk 14 "0" "prism volume unchanged"
fr_chk 15 "16" "Minimum Vertices 9 keeps the 8-gon caps and the quads:: 16 points"
fr_chk 16 "10" "Minimum Vertices 9 keeps the 8-gon caps and the quads:: 10 faces"
fr_chk 17 "48" "Minimum Vertices 9 keeps the 8-gon caps and the quads:: 48 corners"
fr_chk 18 "8" "one 8-gon -> 6 triangles, 5 diagonals:: 8 points"
fr_chk 19 "13" "one 8-gon -> 6 triangles, 5 diagonals:: 13 edges"
fr_chk 20 "6" "one 8-gon -> 6 triangles, 5 diagonals:: 6 faces"
fr_chk 21 "18" "one 8-gon -> 6 triangles, 5 diagonals:: 18 corners"
fr_chk 22 "0" "area unchanged"
fr_chk 23 "9" "3 x 3 grid: 4 quads -> 8 triangles, 4 diagonals:: 9 points"
fr_chk 24 "16" "3 x 3 grid: 4 quads -> 8 triangles, 4 diagonals:: 16 edges"
fr_chk 25 "8" "3 x 3 grid: 4 quads -> 8 triangles, 4 diagonals:: 8 faces"
fr_chk 26 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
