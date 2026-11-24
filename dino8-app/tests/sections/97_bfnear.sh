# ---------------------------------------------------------------------------
# BFNEAR_: Flow Register row geometry_nodes/Sample Nearest (Sample Nearest), flowrows_gen.py. The graph flowrows_bfnear.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: vertex i of the 10-vertex line sits at z = i, so the nearest to z = 3.4 is 3, to 3.6 is 4, to (5,5,-5) is 0, to z = 99 is 9, to the vertex itself is its own index. The index order of other meshes and the tie rule are untested against Blender.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfnear.dflow|g" "$HERE/flowrows_bfnear_script.txt" > "$TMPW/flowrows_bfnear_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfnear_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFNEAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfnear_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFNEAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFNEAR_$1: $3 (measured '$got')"; else echo "FAIL BFNEAR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 31 node(s)" <<<"$FR_OUT"; then echo "ok   BFNEAR_solve: the player solved all 31 nodes (every node type is registered)"; else echo "FAIL BFNEAR_solve: expected 'solved 31 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 7 " <<<"$FR_OUT"; then echo "ok   BFNEAR_objects: exactly 7 point(s) baked"; else echo "FAIL BFNEAR_objects: expected 7 baked objects"; fail=1; fi
fr_chk 1 "3" "z = 3.4 is nearest to vertex 3"
fr_chk 2 "4" "z = 3.6 is nearest to vertex 4"
fr_chk 3 "0" "far off to the side, below: vertex 0"
fr_chk 4 "9" "far above: the last vertex 9"
fr_chk 5 "0" "exactly at vertex 0"
fr_chk 6 "6" "off axis: z = 6.1 -> vertex 6"
fr_chk 7 "0" "a point at the centre of a cube is equally near all corners: the lowest index wins"
