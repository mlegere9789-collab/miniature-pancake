# ---------------------------------------------------------------------------
# BFTRN_: Flow Register row geometry_nodes/Transform Geometry (Transform Geometry), flowrows_gen.py. The graph flowrows_bftrn.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: p' = T + R S p: scale (2,3,4) of the unit cube has volume 24 and box +-(1,1.5,2); rotating a 2 x 4 x 6 box 90 degrees about Z gives extents 4 x 2 x 6, about X 2 x 6 x 4; scale (2,1,1), then rotation about Z by 90 degrees, then move (0,0,5) of the unit cube spans x +-0.5, y +-1, z 4.5..5.5; a mirror keeps the volume +1; Euler XYZ (90, 0, 90 degrees) takes (0,0,1) -> (0,-1,0) -> (1,0,0); point (1,2,3) -> (3,5,7).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bftrn.dflow|g" "$HERE/flowrows_bftrn_script.txt" > "$TMPW/flowrows_bftrn_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bftrn_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFTRN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bftrn_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFTRN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFTRN_$1: $3 (measured '$got')"; else echo "FAIL BFTRN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 328 node(s)" <<<"$FR_OUT"; then echo "ok   BFTRN_solve: the player solved all 328 nodes (every node type is registered)"; else echo "FAIL BFTRN_solve: expected 'solved 328 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 51 " <<<"$FR_OUT"; then echo "ok   BFTRN_objects: exactly 51 point(s) baked"; else echo "FAIL BFTRN_objects: expected 51 baked objects"; fail=1; fi
fr_chk 1 "0" "translation (1,2,3) of the size-2 cube: min X"
fr_chk 2 "0" "translation (1,2,3) of the size-2 cube: min Y"
fr_chk 3 "0" "translation (1,2,3) of the size-2 cube: min Z"
fr_chk 4 "0" "translation (1,2,3) of the size-2 cube: max X"
fr_chk 5 "0" "translation (1,2,3) of the size-2 cube: max Y"
fr_chk 6 "0" "translation (1,2,3) of the size-2 cube: max Z"
fr_chk 7 "0" "scale (2,3,4) of the unit cube: volume 24"
fr_chk 8 "0" "box +-(1, 1.5, 2): min X"
fr_chk 9 "0" "box +-(1, 1.5, 2): min Y"
fr_chk 10 "0" "box +-(1, 1.5, 2): min Z"
fr_chk 11 "0" "box +-(1, 1.5, 2): max X"
fr_chk 12 "0" "box +-(1, 1.5, 2): max Y"
fr_chk 13 "0" "box +-(1, 1.5, 2): max Z"
fr_chk 14 "0" "rotation 90 degrees about Z swaps X and Y extents: min X"
fr_chk 15 "0" "rotation 90 degrees about Z swaps X and Y extents: min Y"
fr_chk 16 "0" "rotation 90 degrees about Z swaps X and Y extents: min Z"
fr_chk 17 "0" "rotation 90 degrees about Z swaps X and Y extents: max X"
fr_chk 18 "0" "rotation 90 degrees about Z swaps X and Y extents: max Y"
fr_chk 19 "0" "rotation 90 degrees about Z swaps X and Y extents: max Z"
fr_chk 20 "0" "rotation 90 degrees about X swaps Y and Z extents: min X"
fr_chk 21 "0" "rotation 90 degrees about X swaps Y and Z extents: min Y"
fr_chk 22 "0" "rotation 90 degrees about X swaps Y and Z extents: min Z"
fr_chk 23 "0" "rotation 90 degrees about X swaps Y and Z extents: max X"
fr_chk 24 "0" "rotation 90 degrees about X swaps Y and Z extents: max Y"
fr_chk 25 "0" "rotation 90 degrees about X swaps Y and Z extents: max Z"
fr_chk 26 "0" "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5: min X"
fr_chk 27 "0" "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5: min Y"
fr_chk 28 "0" "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5: min Z"
fr_chk 29 "0" "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5: max X"
fr_chk 30 "0" "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5: max Y"
fr_chk 31 "0" "scale (2,1,1) comes first, then the 90 degree turn about Z, then the move: x +-0.5, y +-1, z 4.5..5.5: max Z"
fr_chk 32 "0" "a mirror (scale -1) keeps the faces outward: volume +1"
fr_chk 33 "0" "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0): min X"
fr_chk 34 "0" "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0): min Y"
fr_chk 35 "0" "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0): min Z"
fr_chk 36 "0" "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0): max X"
fr_chk 37 "0" "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0): max Y"
fr_chk 38 "0" "XYZ order: (0,0,1) turned 90 degrees about X goes to (0,-1,0), then 90 degrees about Z to (1,0,0): max Z"
fr_chk 39 "0" "circle (4 points) scaled (3,1,1): min X"
fr_chk 40 "0" "circle (4 points) scaled (3,1,1): min Y"
fr_chk 41 "0" "circle (4 points) scaled (3,1,1): min Z"
fr_chk 42 "0" "circle (4 points) scaled (3,1,1): max X"
fr_chk 43 "0" "circle (4 points) scaled (3,1,1): max Y"
fr_chk 44 "0" "circle (4 points) scaled (3,1,1): max Z"
fr_chk 45 "0" "point (1,2,3) scaled by 2 then moved by (1,1,1): min X"
fr_chk 46 "0" "point (1,2,3) scaled by 2 then moved by (1,1,1): min Y"
fr_chk 47 "0" "point (1,2,3) scaled by 2 then moved by (1,1,1): min Z"
fr_chk 48 "0" "point (1,2,3) scaled by 2 then moved by (1,1,1): max X"
fr_chk 49 "0" "point (1,2,3) scaled by 2 then moved by (1,1,1): max Y"
fr_chk 50 "0" "point (1,2,3) scaled by 2 then moved by (1,1,1): max Z"
fr_chk 51 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
