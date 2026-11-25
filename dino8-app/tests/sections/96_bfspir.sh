# ---------------------------------------------------------------------------
# BFSPIR_: Flow Register row geometry_nodes/Spiral (Spiral), flowrows_gen.py. The graph flowrows_bfspir.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: n = floor(Resolution * Rotations) = 8 edges, point i at angle 2 pi Rotations i / n, radius r0 + (r1 - r0) i / n = 1 + i/4, height 4 i / 8: last point (3,0,4); x in [-2.5 (i=6), 3], y in [-2.75 (i=7), 2.25 (i=5)]; Reverse mirrors y. The point-count convention is from recollection of the Blender manual.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfspir.dflow|g" "$HERE/flowrows_bfspir_script.txt" > "$TMPW/flowrows_bfspir_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfspir_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSPIR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfspir_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSPIR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSPIR_$1: $3 (measured '$got')"; else echo "FAIL BFSPIR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 118 node(s)" <<<"$FR_OUT"; then echo "ok   BFSPIR_solve: the player solved all 118 nodes (every node type is registered)"; else echo "FAIL BFSPIR_solve: expected 'solved 118 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 22 " <<<"$FR_OUT"; then echo "ok   BFSPIR_objects: exactly 22 point(s) baked"; else echo "FAIL BFSPIR_objects: expected 22 baked objects"; fail=1; fi
fr_chk 1 "9" "4 points per rotation over 2 rotations gives 8 edges, so: 9 points"
fr_chk 2 "1" "4 points per rotation over 2 rotations gives 8 edges, so: 1 splines"
fr_chk 3 "0" "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2): min X"
fr_chk 4 "0" "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2): min Y"
fr_chk 5 "0" "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2): min Z"
fr_chk 6 "0" "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2): max X"
fr_chk 7 "0" "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2): max Y"
fr_chk 8 "0" "box of the 9 points (radius 1 + i/4, angle i pi/2, height i/2): max Z"
fr_chk 9 "0" "length of the 9-point helix polyline"
fr_chk 10 "0" "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5): min X"
fr_chk 11 "0" "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5): min Y"
fr_chk 12 "0" "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5): min Z"
fr_chk 13 "0" "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5): max X"
fr_chk 14 "0" "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5): max Y"
fr_chk 15 "0" "hand check: the last point is (3,0,4), y runs -2.75 (i=7) to 2.25 (i=5): max Z"
fr_chk 16 "0" "Reverse winds the other way: y mirrored: min X"
fr_chk 17 "0" "Reverse winds the other way: y mirrored: min Y"
fr_chk 18 "0" "Reverse winds the other way: y mirrored: min Z"
fr_chk 19 "0" "Reverse winds the other way: y mirrored: max X"
fr_chk 20 "0" "Reverse winds the other way: y mirrored: max Y"
fr_chk 21 "0" "Reverse winds the other way: y mirrored: max Z"
fr_chk 22 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
