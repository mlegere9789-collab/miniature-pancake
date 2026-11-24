# ---------------------------------------------------------------------------
# BFP2V_: Flow Register row geometry_nodes/Points to Vertices (Points to Vertices), flowrows_gen.py. The graph flowrows_bfp2v.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the vertex count and positions are the points' own and there are no edges or faces: 5 points at (1,2,3) -> 5 vertices; a cube's 8 vertices with its 12 edges and 6 faces dropped; joined to a cube they add 5 to its 8 vertices.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfp2v.dflow|g" "$HERE/flowrows_bfp2v_script.txt" > "$TMPW/flowrows_bfp2v_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfp2v_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFP2V_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfp2v_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFP2V_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFP2V_$1: $3 (measured '$got')"; else echo "FAIL BFP2V_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 70 node(s)" <<<"$FR_OUT"; then echo "ok   BFP2V_solve: the player solved all 70 nodes (every node type is registered)"; else echo "FAIL BFP2V_solve: expected 'solved 70 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 16 " <<<"$FR_OUT"; then echo "ok   BFP2V_objects: exactly 16 point(s) baked"; else echo "FAIL BFP2V_objects: expected 16 baked objects"; fail=1; fi
fr_chk 1 "5" "5 points become 5 vertices:: 5 points"
fr_chk 2 "0" "5 points become 5 vertices:: 0 edges"
fr_chk 3 "0" "5 points become 5 vertices:: 0 faces"
fr_chk 4 "0" "at (1,2,3): min X"
fr_chk 5 "0" "at (1,2,3): min Y"
fr_chk 6 "0" "at (1,2,3): min Z"
fr_chk 7 "0" "at (1,2,3): max X"
fr_chk 8 "0" "at (1,2,3): max Y"
fr_chk 9 "0" "at (1,2,3): max Z"
fr_chk 10 "8" "faces of the input are dropped:: 8 points"
fr_chk 11 "0" "faces of the input are dropped:: 0 edges"
fr_chk 12 "0" "faces of the input are dropped:: 0 faces"
fr_chk 13 "13" "points joined to a cube keep the mesh intact:: 13 points"
fr_chk 14 "12" "points joined to a cube keep the mesh intact:: 12 edges"
fr_chk 15 "6" "points joined to a cube keep the mesh intact:: 6 faces"
fr_chk 16 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
