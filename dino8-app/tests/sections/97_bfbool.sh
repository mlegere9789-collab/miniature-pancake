# ---------------------------------------------------------------------------
# BFBOOL_: Flow Register row geometry_nodes/Mesh Boolean (Mesh Boolean), flowrows_gen.py. The graph flowrows_bfbool.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: two size-2 cubes shifted by 1 along X overlap in 1 x 2 x 2 = 4: union 8 + 8 - 4 = 12 (box x -1..2), intersection 4 (box x 0..1), difference A - B = 4 (box x -1..0); A - (B and a unit cube covering x -1.5..-0.5 about the axis) = 4 - 0.5 = 3.5. Single precision mesh: volumes compared within 5e-5.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfbool.dflow|g" "$HERE/flowrows_bfbool_script.txt" > "$TMPW/flowrows_bfbool_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfbool_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFBOOL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfbool_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFBOOL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFBOOL_$1: $3 (measured '$got')"; else echo "FAIL BFBOOL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 148 node(s)" <<<"$FR_OUT"; then echo "ok   BFBOOL_solve: the player solved all 148 nodes (every node type is registered)"; else echo "FAIL BFBOOL_solve: expected 'solved 148 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 23 " <<<"$FR_OUT"; then echo "ok   BFBOOL_objects: exactly 23 point(s) baked"; else echo "FAIL BFBOOL_objects: expected 23 baked objects"; fail=1; fi
fr_chk 1 "0" "union of two 2-cubes overlapping by half: 8 + 8 - 4"
fr_chk 2 "0" "union box x -1..2: min X"
fr_chk 3 "0" "union box x -1..2: min Y"
fr_chk 4 "0" "union box x -1..2: min Z"
fr_chk 5 "0" "union box x -1..2: max X"
fr_chk 6 "0" "union box x -1..2: max Y"
fr_chk 7 "0" "union box x -1..2: max Z"
fr_chk 8 "0" "intersection x 0..1: 1 * 2 * 2"
fr_chk 9 "0" "intersection box: min X"
fr_chk 10 "0" "intersection box: min Y"
fr_chk 11 "0" "intersection box: min Z"
fr_chk 12 "0" "intersection box: max X"
fr_chk 13 "0" "intersection box: max Y"
fr_chk 14 "0" "intersection box: max Z"
fr_chk 15 "0" "difference A - B: x -1..0, 1 * 2 * 2"
fr_chk 16 "0" "difference box: min X"
fr_chk 17 "0" "difference box: min Y"
fr_chk 18 "0" "difference box: min Z"
fr_chk 19 "0" "difference box: max X"
fr_chk 20 "0" "difference box: max Y"
fr_chk 21 "0" "difference box: max Z"
fr_chk 22 "0" "A minus (B and a unit cube over x -1.5..-0.5): 4 - 0.5 = 3.5"
fr_chk 23 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
