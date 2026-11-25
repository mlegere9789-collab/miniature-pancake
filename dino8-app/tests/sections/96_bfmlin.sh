# ---------------------------------------------------------------------------
# BFMLIN_: Flow Register row geometry_nodes/Mesh Line (Mesh Line), flowrows_gen.py. The graph flowrows_bfmlin.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Count vertices, Count - 1 edges, no face: 10 -> 9; vertex i = Start + i Offset (box z 0..9 for the default; (1,2,3) + i (0.5,0,-1), i = 0..4 -> x 1..3, z -1..3); End Points mode: Start + (End - Start) i / (Count - 1).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfmlin.dflow|g" "$HERE/flowrows_bfmlin_script.txt" > "$TMPW/flowrows_bfmlin_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmlin_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMLIN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmlin_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMLIN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFMLIN_$1: $3 (measured '$got')"; else echo "FAIL BFMLIN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 132 node(s)" <<<"$FR_OUT"; then echo "ok   BFMLIN_solve: the player solved all 132 nodes (every node type is registered)"; else echo "FAIL BFMLIN_solve: expected 'solved 132 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 27 " <<<"$FR_OUT"; then echo "ok   BFMLIN_objects: exactly 27 point(s) baked"; else echo "FAIL BFMLIN_objects: expected 27 baked objects"; fail=1; fi
fr_chk 1 "10" "the default line (10 vertices):: 10 points"
fr_chk 2 "9" "the default line (10 vertices):: 9 edges"
fr_chk 3 "0" "the default line (10 vertices):: 0 faces"
fr_chk 4 "0" "the default line (10 vertices):: 0 corners"
fr_chk 5 "0" "offset (0,0,1): z = 0 .. 9: min X"
fr_chk 6 "0" "offset (0,0,1): z = 0 .. 9: min Y"
fr_chk 7 "0" "offset (0,0,1): z = 0 .. 9: min Z"
fr_chk 8 "0" "offset (0,0,1): z = 0 .. 9: max X"
fr_chk 9 "0" "offset (0,0,1): z = 0 .. 9: max Y"
fr_chk 10 "0" "offset (0,0,1): z = 0 .. 9: max Z"
fr_chk 11 "5" "5 vertices:: 5 points"
fr_chk 12 "4" "5 vertices:: 4 edges"
fr_chk 13 "0" "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1: min X"
fr_chk 14 "0" "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1: min Y"
fr_chk 15 "0" "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1: min Z"
fr_chk 16 "0" "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1: max X"
fr_chk 17 "0" "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1: max Y"
fr_chk 18 "0" "Start + i * (0.5, 0, -1), i = 0..4 -> x 1..3, z 3..-1: max Z"
fr_chk 19 "0" "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex: min X"
fr_chk 20 "0" "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex: min Y"
fr_chk 21 "0" "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex: min Z"
fr_chk 22 "0" "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex: max X"
fr_chk 23 "0" "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex: max Y"
fr_chk 24 "0" "End Points mode: Start + (End - Start) i / 4 reaches (4,0,8) at the last vertex: max Z"
fr_chk 25 "1" "one vertex has no edge:: 1 points"
fr_chk 26 "0" "one vertex has no edge:: 0 edges"
fr_chk 27 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
