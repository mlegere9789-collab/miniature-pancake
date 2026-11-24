# ---------------------------------------------------------------------------
# BFHULL_: Flow Register row geometry_nodes/Convex Hull (Convex Hull), flowrows_gen.py. The graph flowrows_bfhull.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the hull of a convex polyhedron is itself: UV sphere 8 x 4 radius 2 has the frustum-stack volume (the same closed form as BFUVSP) and box +-2; a size-2 cube plus one point 2 above its top face centre adds a square pyramid of base 4 and height 2: 8 + 8/3 = 10.667, box z -1..3; a 3 x 3 x 3 lattice cube has the hull of its 8 corners, volume 8. Single precision mesh: within 5e-5.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfhull.dflow|g" "$HERE/flowrows_bfhull_script.txt" > "$TMPW/flowrows_bfhull_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfhull_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFHULL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfhull_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFHULL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFHULL_$1: $3 (measured '$got')"; else echo "FAIL BFHULL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 102 node(s)" <<<"$FR_OUT"; then echo "ok   BFHULL_solve: the player solved all 102 nodes (every node type is registered)"; else echo "FAIL BFHULL_solve: expected 'solved 102 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 16 " <<<"$FR_OUT"; then echo "ok   BFHULL_objects: exactly 16 point(s) baked"; else echo "FAIL BFHULL_objects: expected 16 baked objects"; fail=1; fi
fr_chk 1 "0" "the 8 x 4 sphere polyhedron is convex: the hull has its volume"
fr_chk 2 "0" "box +-2: min X"
fr_chk 3 "0" "box +-2: min Y"
fr_chk 4 "0" "box +-2: min Z"
fr_chk 5 "0" "box +-2: max X"
fr_chk 6 "0" "box +-2: max Y"
fr_chk 7 "0" "box +-2: max Z"
fr_chk 8 "0" "cube + apex above the top face: a square pyramid of base 4, height 2 on top: 8 + 8/3"
fr_chk 9 "0" "box z -1..3: min X"
fr_chk 10 "0" "box z -1..3: min Y"
fr_chk 11 "0" "box z -1..3: min Z"
fr_chk 12 "0" "box z -1..3: max X"
fr_chk 13 "0" "box z -1..3: max Y"
fr_chk 14 "0" "box z -1..3: max Z"
fr_chk 15 "0" "a 3 x 3 x 3 lattice cube has the hull of its 8 corners: volume 8"
fr_chk 16 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
