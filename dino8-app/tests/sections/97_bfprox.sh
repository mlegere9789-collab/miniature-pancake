# ---------------------------------------------------------------------------
# BFPROX_: Flow Register row geometry_nodes/Geometry Proximity (Geometry Proximity), flowrows_gen.py. The graph flowrows_bfprox.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: closest point of a size-2 cube: (5,0,0) -> (1,0,0) at 4; (3,3,0) -> (1,1,0) at 2 sqrt 2; (3,3,3) -> (1,1,1) at 2 sqrt 3; inside points give the nearest face: (0,0,0.5) -> (0,0,1) at 0.5, (0.25,0.5,0.75) -> (0.25,0.5,1) at 0.25; unit grid: (0.2,0.3,2) -> (0.2,0.3,0) at 2, (3,0,1) -> (0.5,0,0) at sqrt(2.5^2 + 1). Single precision: within 5e-6.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfprox.dflow|g" "$HERE/flowrows_bfprox_script.txt" > "$TMPW/flowrows_bfprox_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfprox_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFPROX_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfprox_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFPROX_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFPROX_$1: $3 (measured '$got')"; else echo "FAIL BFPROX_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 170 node(s)" <<<"$FR_OUT"; then echo "ok   BFPROX_solve: the player solved all 170 nodes (every node type is registered)"; else echo "FAIL BFPROX_solve: expected 'solved 170 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 29 " <<<"$FR_OUT"; then echo "ok   BFPROX_objects: exactly 29 point(s) baked"; else echo "FAIL BFPROX_objects: expected 29 baked objects"; fail=1; fi
fr_chk 1 "0" "a point 4 outside a face: position X"
fr_chk 2 "0" "a point 4 outside a face: position Y"
fr_chk 3 "0" "a point 4 outside a face: position Z"
fr_chk 4 "0" "a point 4 outside a face: distance"
fr_chk 5 "0" "beside an edge: the edge point (1,1,0), distance 2 sqrt 2: position X"
fr_chk 6 "0" "beside an edge: the edge point (1,1,0), distance 2 sqrt 2: position Y"
fr_chk 7 "0" "beside an edge: the edge point (1,1,0), distance 2 sqrt 2: position Z"
fr_chk 8 "0" "beside an edge: the edge point (1,1,0), distance 2 sqrt 2: distance"
fr_chk 9 "0" "beyond a corner: position X"
fr_chk 10 "0" "beyond a corner: position Y"
fr_chk 11 "0" "beyond a corner: position Z"
fr_chk 12 "0" "beyond a corner: distance"
fr_chk 13 "0" "inside: the distance to the surface, 0.5 to the top face: position X"
fr_chk 14 "0" "inside: the distance to the surface, 0.5 to the top face: position Y"
fr_chk 15 "0" "inside: the distance to the surface, 0.5 to the top face: position Z"
fr_chk 16 "0" "inside: the distance to the surface, 0.5 to the top face: distance"
fr_chk 17 "0" "inside near the top: 0.25: position X"
fr_chk 18 "0" "inside near the top: 0.25: position Y"
fr_chk 19 "0" "inside near the top: 0.25: position Z"
fr_chk 20 "0" "inside near the top: 0.25: distance"
fr_chk 21 "0" "grid, point above: foot (0.2,0.3,0) X"
fr_chk 22 "0" "grid, point above: foot (0.2,0.3,0) Y"
fr_chk 23 "0" "grid, point above: foot (0.2,0.3,0) Z"
fr_chk 24 "0" "grid, point above: distance 2"
fr_chk 25 "0" "grid, point beside: nearest is the edge point (0.5,0,0) X"
fr_chk 26 "0" "grid, point beside: nearest is the edge point (0.5,0,0) Y"
fr_chk 27 "0" "grid, point beside: nearest is the edge point (0.5,0,0) Z"
fr_chk 28 "0" "grid, point beside: distance sqrt(2.5^2 + 1)"
fr_chk 29 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
