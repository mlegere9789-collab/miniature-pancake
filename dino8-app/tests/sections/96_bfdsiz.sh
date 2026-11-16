# ---------------------------------------------------------------------------
# BFDSIZ_: Flow Register row geometry_nodes/Domain Size (Domain Size), flowrows_gen.py. The graph flowrows_bfdsiz.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Point/Edge/Face/Face Corner counts of closed forms: cube 8/12/6/24, 4 x 2 grid 8/10/3/12, n-gon hexagon 6/6/1/6 (a fan of triangles inside an n-gon counts as one face with its boundary corners), hexagon fan 7/12/6/18, 7-vertex wire 7/6/0/0, icosahedron 12/30/20/60; a cyclic 12-point curve has 12 points (the stored closing point is not counted) and 1 spline.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfdsiz.dflow|g" "$HERE/flowrows_bfdsiz_script.txt" > "$TMPW/flowrows_bfdsiz_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfdsiz_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFDSIZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfdsiz_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFDSIZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFDSIZ_$1: $3 (measured '$got')"; else echo "FAIL BFDSIZ_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 86 node(s)" <<<"$FR_OUT"; then echo "ok   BFDSIZ_solve: the player solved all 86 nodes (every node type is registered)"; else echo "FAIL BFDSIZ_solve: expected 'solved 86 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 34 " <<<"$FR_OUT"; then echo "ok   BFDSIZ_objects: exactly 34 point(s) baked"; else echo "FAIL BFDSIZ_objects: expected 34 baked objects"; fail=1; fi
fr_chk 1 "8" "cube:: 8 points"
fr_chk 2 "12" "cube:: 12 edges"
fr_chk 3 "6" "cube:: 6 faces"
fr_chk 4 "24" "cube:: 24 corners"
fr_chk 5 "0" "cube:: 0 splines"
fr_chk 6 "8" "4 x 2 grid: V = 8, E = 3 * 2 + 1 * 4 = 10, F = 3, corners 12;: 8 points"
fr_chk 7 "10" "4 x 2 grid: V = 8, E = 3 * 2 + 1 * 4 = 10, F = 3, corners 12;: 10 edges"
fr_chk 8 "3" "4 x 2 grid: V = 8, E = 3 * 2 + 1 * 4 = 10, F = 3, corners 12;: 3 faces"
fr_chk 9 "12" "4 x 2 grid: V = 8, E = 3 * 2 + 1 * 4 = 10, F = 3, corners 12;: 12 corners"
fr_chk 10 "6" "hexagon n-gon: 6 points, 6 edges, one face with 6 corners;: 6 points"
fr_chk 11 "6" "hexagon n-gon: 6 points, 6 edges, one face with 6 corners;: 6 edges"
fr_chk 12 "1" "hexagon n-gon: 6 points, 6 edges, one face with 6 corners;: 1 faces"
fr_chk 13 "6" "hexagon n-gon: 6 points, 6 edges, one face with 6 corners;: 6 corners"
fr_chk 14 "7" "hexagon fan:: 7 points"
fr_chk 15 "12" "hexagon fan:: 12 edges"
fr_chk 16 "6" "hexagon fan:: 6 faces"
fr_chk 17 "18" "hexagon fan:: 18 corners"
fr_chk 18 "7" "7-vertex line: wire edges count, no face:: 7 points"
fr_chk 19 "6" "7-vertex line: wire edges count, no face:: 6 edges"
fr_chk 20 "0" "7-vertex line: wire edges count, no face:: 0 faces"
fr_chk 21 "0" "7-vertex line: wire edges count, no face:: 0 corners"
fr_chk 22 "12" "icosahedron:: 12 points"
fr_chk 23 "30" "icosahedron:: 30 edges"
fr_chk 24 "20" "icosahedron:: 20 faces"
fr_chk 25 "60" "icosahedron:: 60 corners"
fr_chk 26 "0" "icosahedron:: 0 splines"
fr_chk 27 "12" "12-point cyclic curve: 12 points, 1 spline, no mesh elements;: 12 points"
fr_chk 28 "0" "12-point cyclic curve: 12 points, 1 spline, no mesh elements;: 0 edges"
fr_chk 29 "0" "12-point cyclic curve: 12 points, 1 spline, no mesh elements;: 0 faces"
fr_chk 30 "0" "12-point cyclic curve: 12 points, 1 spline, no mesh elements;: 0 corners"
fr_chk 31 "1" "12-point cyclic curve: 12 points, 1 spline, no mesh elements;: 1 splines"
fr_chk 32 "2" "open line:: 2 points"
fr_chk 33 "1" "open line:: 1 splines"
fr_chk 34 "1" "a point:: 1 points"
