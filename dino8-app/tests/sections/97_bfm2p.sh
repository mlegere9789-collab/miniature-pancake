# ---------------------------------------------------------------------------
# BFM2P_: Flow Register row geometry_nodes/Mesh to Points (Mesh to Points), flowrows_gen.py. The graph flowrows_bfm2p.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Vertices mode keeps one point per vertex and no edges or faces: cube 8, UV sphere 8 x 4 = 8 * 3 + 2 = 26, line of 4 = 4 with the same box. The Edges, Faces and Corners modes and the Radius / Selection fields are untested.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfm2p.dflow|g" "$HERE/flowrows_bfm2p_script.txt" > "$TMPW/flowrows_bfm2p_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfm2p_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFM2P_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfm2p_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFM2P_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFM2P_$1: $3 (measured '$got')"; else echo "FAIL BFM2P_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 100 node(s)" <<<"$FR_OUT"; then echo "ok   BFM2P_solve: the player solved all 100 nodes (every node type is registered)"; else echo "FAIL BFM2P_solve: expected 'solved 100 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 22 " <<<"$FR_OUT"; then echo "ok   BFM2P_objects: exactly 22 point(s) baked"; else echo "FAIL BFM2P_objects: expected 22 baked objects"; fail=1; fi
fr_chk 1 "8" "the 8 corners of a cube as points:: 8 points"
fr_chk 2 "0" "the 8 corners of a cube as points:: 0 edges"
fr_chk 3 "0" "the 8 corners of a cube as points:: 0 faces"
fr_chk 4 "0" "the 8 corners of a cube as points:: 0 corners"
fr_chk 5 "0" "same box: min X"
fr_chk 6 "0" "same box: min Y"
fr_chk 7 "0" "same box: min Z"
fr_chk 8 "0" "same box: max X"
fr_chk 9 "0" "same box: max Y"
fr_chk 10 "0" "same box: max Z"
fr_chk 11 "26" "UV sphere 8 x 4 has 26 vertices:: 26 points"
fr_chk 12 "0" "UV sphere 8 x 4 has 26 vertices:: 0 edges"
fr_chk 13 "0" "UV sphere 8 x 4 has 26 vertices:: 0 faces"
fr_chk 14 "4" "the edges of a line are dropped:: 4 points"
fr_chk 15 "0" "the edges of a line are dropped:: 0 edges"
fr_chk 16 "0" "z = 0..3: min X"
fr_chk 17 "0" "z = 0..3: min Y"
fr_chk 18 "0" "z = 0..3: min Z"
fr_chk 19 "0" "z = 0..3: max X"
fr_chk 20 "0" "z = 0..3: max Y"
fr_chk 21 "0" "z = 0..3: max Z"
fr_chk 22 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
