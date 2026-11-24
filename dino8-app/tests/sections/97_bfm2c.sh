# ---------------------------------------------------------------------------
# BFM2C_: Flow Register row geometry_nodes/Mesh to Curve (Mesh to Curve), flowrows_gen.py. The graph flowrows_bfm2c.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Mesh Line of 5 vertices = 1 spline of 5 points, length 4; an unfilled 8-gon = 1 cyclic spline of 8 points, length 8 * 2 r sin(pi/8) with r = 2; the 12 edges of a cube (degree 3 at every corner) = 12 splines of length 1; the 4 edges of one quad = 1 cyclic spline. Splitting at corners with three or more edges is from recollection of the Blender behaviour.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfm2c.dflow|g" "$HERE/flowrows_bfm2c_script.txt" > "$TMPW/flowrows_bfm2c_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfm2c_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFM2C_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfm2c_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFM2C_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFM2C_$1: $3 (measured '$got')"; else echo "FAIL BFM2C_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 57 node(s)" <<<"$FR_OUT"; then echo "ok   BFM2C_solve: the player solved all 57 nodes (every node type is registered)"; else echo "FAIL BFM2C_solve: expected 'solved 57 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 12 " <<<"$FR_OUT"; then echo "ok   BFM2C_objects: exactly 12 point(s) baked"; else echo "FAIL BFM2C_objects: expected 12 baked objects"; fail=1; fi
fr_chk 1 "1" "a chain of 5 vertices is one spline"
fr_chk 2 "5" "of 5 points:: 5 points"
fr_chk 3 "1" "of 5 points:: 1 splines"
fr_chk 4 "0" "length 4 (4 edges of 1)"
fr_chk 5 "1" "an edge loop is one spline"
fr_chk 6 "8" "of 8 points:: 8 points"
fr_chk 7 "1" "of 8 points:: 1 splines"
fr_chk 8 "0" "cyclic: 8 chords of 2 r sin(pi/8) with r = 2, the closing edge included"
fr_chk 9 "12" "the 12 edges of a cube meet three at every corner: one spline per edge"
fr_chk 10 "0" "of length 1 (default cube size)"
fr_chk 11 "1" "the 4 edges around one quad are one cyclic spline"
fr_chk 12 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
