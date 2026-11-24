# ---------------------------------------------------------------------------
# BFIMPS_: Flow Register row geometry_nodes/Import STL (Import STL), flowrows_gen.py. The graph flowrows_bfimps.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the ascii file lists the 12 triangles of the unit cube (36 vertex lines); welded it is the closed solid with 8 vertices, 18 edges (12 cube edges + 6 face diagonals), 12 faces, volume 1, area 6, box [0,1]^3.
# ---------------------------------------------------------------------------
cat > "$TMPW/cube.stl" <<'EOF_BF'
solid cube
facet normal 0 0 0
 outer loop
  vertex 0 0 0
  vertex 0 1 0
  vertex 1 1 0
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 0
  vertex 1 1 0
  vertex 1 0 0
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 1
  vertex 1 0 1
  vertex 1 1 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 1
  vertex 1 1 1
  vertex 0 1 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 0
  vertex 1 0 0
  vertex 1 0 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 0
  vertex 1 0 1
  vertex 0 0 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 1 0
  vertex 0 1 1
  vertex 1 1 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 1 0
  vertex 1 1 1
  vertex 1 1 0
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 0
  vertex 0 0 1
  vertex 0 1 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 0 0 0
  vertex 0 1 1
  vertex 0 1 0
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 1 0 0
  vertex 1 1 0
  vertex 1 1 1
 endloop
endfacet
facet normal 0 0 0
 outer loop
  vertex 1 0 0
  vertex 1 1 1
  vertex 1 0 1
 endloop
endfacet
endsolid cube
EOF_BF
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfimps.dflow" > "$TMPW/flowrows_bfimps.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfimps.dflow|g" "$HERE/flowrows_bfimps_script.txt" > "$TMPW/flowrows_bfimps_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimps_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPS_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimps_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPS_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFIMPS_$1: $3 (measured '$got')"; else echo "FAIL BFIMPS_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 61 node(s)" <<<"$FR_OUT"; then echo "ok   BFIMPS_solve: the player solved all 61 nodes (every node type is registered)"; else echo "FAIL BFIMPS_solve: expected 'solved 61 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   BFIMPS_objects: exactly 13 point(s) baked"; else echo "FAIL BFIMPS_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "8" "12 facets welded into 8 vertices, 12 + 6 diagonals = 18 edges:: 8 points"
fr_chk 2 "18" "12 facets welded into 8 vertices, 12 + 6 diagonals = 18 edges:: 18 edges"
fr_chk 3 "12" "12 facets welded into 8 vertices, 12 + 6 diagonals = 18 edges:: 12 faces"
fr_chk 4 "36" "12 facets welded into 8 vertices, 12 + 6 diagonals = 18 edges:: 36 corners"
fr_chk 5 "0" "closed after welding: volume 1"
fr_chk 6 "0" "area 6"
fr_chk 7 "0" "box [0,1]^3: min X"
fr_chk 8 "0" "box [0,1]^3: min Y"
fr_chk 9 "0" "box [0,1]^3: min Z"
fr_chk 10 "0" "box [0,1]^3: max X"
fr_chk 11 "0" "box [0,1]^3: max Y"
fr_chk 12 "0" "box [0,1]^3: max Z"
fr_chk 13 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
