# ---------------------------------------------------------------------------
# BFBBOX_: Flow Register row geometry_nodes/Bounding Box (Bounding Box), flowrows_gen.py. The graph flowrows_bfbbox.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: componentwise minimum and maximum of the vertices: cube of Size (2,6,10) -> +-(1,3,5); line (1,2,3)-(4,-2,9) -> (1,-2,3), (4,2,9); a point is its own box; circle r = 3 with 4 points -> +-3 in XY; rectangle 10 x 2 -> +-(5,1). The Blender node also outputs a box mesh (untested).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfbbox.dflow|g" "$HERE/flowrows_bfbbox_script.txt" > "$TMPW/flowrows_bfbbox_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfbbox_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFBBOX_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfbbox_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFBBOX_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFBBOX_$1: $3 (measured '$got')"; else echo "FAIL BFBBOX_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 179 node(s)" <<<"$FR_OUT"; then echo "ok   BFBBOX_solve: the player solved all 179 nodes (every node type is registered)"; else echo "FAIL BFBBOX_solve: expected 'solved 179 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 31 " <<<"$FR_OUT"; then echo "ok   BFBBOX_objects: exactly 31 point(s) baked"; else echo "FAIL BFBBOX_objects: expected 31 baked objects"; fail=1; fi
fr_chk 1 "0" "mesh: Size (2,6,10) centred on the origin: min X"
fr_chk 2 "0" "mesh: Size (2,6,10) centred on the origin: min Y"
fr_chk 3 "0" "mesh: Size (2,6,10) centred on the origin: min Z"
fr_chk 4 "0" "mesh: Size (2,6,10) centred on the origin: max X"
fr_chk 5 "0" "mesh: Size (2,6,10) centred on the origin: max Y"
fr_chk 6 "0" "mesh: Size (2,6,10) centred on the origin: max Z"
fr_chk 7 "0" "curve: componentwise min and max of (1,2,3) and (4,-2,9): min X"
fr_chk 8 "0" "curve: componentwise min and max of (1,2,3) and (4,-2,9): min Y"
fr_chk 9 "0" "curve: componentwise min and max of (1,2,3) and (4,-2,9): min Z"
fr_chk 10 "0" "curve: componentwise min and max of (1,2,3) and (4,-2,9): max X"
fr_chk 11 "0" "curve: componentwise min and max of (1,2,3) and (4,-2,9): max Y"
fr_chk 12 "0" "curve: componentwise min and max of (1,2,3) and (4,-2,9): max Z"
fr_chk 13 "0" "a single point is its own box: min X"
fr_chk 14 "0" "a single point is its own box: min Y"
fr_chk 15 "0" "a single point is its own box: min Z"
fr_chk 16 "0" "a single point is its own box: max X"
fr_chk 17 "0" "a single point is its own box: max Y"
fr_chk 18 "0" "a single point is its own box: max Z"
fr_chk 19 "0" "circle of radius 3 with 4 points: min X"
fr_chk 20 "0" "circle of radius 3 with 4 points: min Y"
fr_chk 21 "0" "circle of radius 3 with 4 points: min Z"
fr_chk 22 "0" "circle of radius 3 with 4 points: max X"
fr_chk 23 "0" "circle of radius 3 with 4 points: max Y"
fr_chk 24 "0" "circle of radius 3 with 4 points: max Z"
fr_chk 25 "0" "rectangle 10 x 2: min X"
fr_chk 26 "0" "rectangle 10 x 2: min Y"
fr_chk 27 "0" "rectangle 10 x 2: min Z"
fr_chk 28 "0" "rectangle 10 x 2: max X"
fr_chk 29 "0" "rectangle 10 x 2: max Y"
fr_chk 30 "0" "rectangle 10 x 2: max Z"
fr_chk 31 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
