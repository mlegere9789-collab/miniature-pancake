# ---------------------------------------------------------------------------
# BFFILL_: Flow Register row geometry_nodes/Fill Curve (Fill Curve), flowrows_gen.py. The graph flowrows_bffill.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: polygon area: rectangle 6 x 4 = 24 (one n-gon: V 4, E 4, F 1, corners 4; as triangles F = V - 2 = 2, E = 5, corners 6); concave 5-point star ro 2, ri 1 = P ro ri sin(pi/P) = 10 sin 36 degrees (n-gon: 10/10/1/10; triangles F = 8, E = V + F - 1 = 17); regular hexagon r = 2: 3 r^2 sin 60; normal +Z for a counter-clockwise curve and -Z when reversed.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bffill.dflow|g" "$HERE/flowrows_bffill_script.txt" > "$TMPW/flowrows_bffill_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bffill_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFILL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bffill_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFILL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFFILL_$1: $3 (measured '$got')"; else echo "FAIL BFFILL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 114 node(s)" <<<"$FR_OUT"; then echo "ok   BFFILL_solve: the player solved all 114 nodes (every node type is registered)"; else echo "FAIL BFFILL_solve: expected 'solved 114 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 27 " <<<"$FR_OUT"; then echo "ok   BFFILL_objects: exactly 27 point(s) baked"; else echo "FAIL BFFILL_objects: expected 27 baked objects"; fail=1; fi
fr_chk 1 "4" "rectangle as one n-gon:: 4 points"
fr_chk 2 "4" "rectangle as one n-gon:: 4 edges"
fr_chk 3 "1" "rectangle as one n-gon:: 1 faces"
fr_chk 4 "4" "rectangle as one n-gon:: 4 corners"
fr_chk 5 "0" "area 6 * 4 = 24"
fr_chk 6 "4" "rectangle as triangles (V - 2 = 2):: 4 points"
fr_chk 7 "5" "rectangle as triangles (V - 2 = 2):: 5 edges"
fr_chk 8 "2" "rectangle as triangles (V - 2 = 2):: 2 faces"
fr_chk 9 "6" "rectangle as triangles (V - 2 = 2):: 6 corners"
fr_chk 10 "10" "concave 5-point star as one n-gon:: 10 points"
fr_chk 11 "10" "concave 5-point star as one n-gon:: 10 edges"
fr_chk 12 "1" "concave 5-point star as one n-gon:: 1 faces"
fr_chk 13 "10" "concave 5-point star as one n-gon:: 10 corners"
fr_chk 14 "0" "star area = P ro ri sin(pi/P) (2P triangles from the centre of 1/2 ro ri sin(pi/P))"
fr_chk 15 "10" "star as triangles: F = V - 2 = 8, E = V + F - 1 = 17 (a disk),: 10 points"
fr_chk 16 "17" "star as triangles: F = V - 2 = 8, E = V + F - 1 = 17 (a disk),: 17 edges"
fr_chk 17 "8" "star as triangles: F = V - 2 = 8, E = V + F - 1 = 17 (a disk),: 8 faces"
fr_chk 18 "24" "star as triangles: F = V - 2 = 8, E = V + F - 1 = 17 (a disk),: 24 corners"
fr_chk 19 "0" "same area from triangles"
fr_chk 20 "0" "hexagon of radius 2: (6/2) r^2 sin(60 degrees)"
fr_chk 21 "0" "a counter-clockwise curve fills with a face normal +Z X"
fr_chk 22 "0" "a counter-clockwise curve fills with a face normal +Z Y"
fr_chk 23 "0" "a counter-clockwise curve fills with a face normal +Z Z"
fr_chk 24 "0" "the reversed (clockwise) curve fills with normal -Z X"
fr_chk 25 "0" "the reversed (clockwise) curve fills with normal -Z Y"
fr_chk 26 "0" "the reversed (clockwise) curve fills with normal -Z Z"
fr_chk 27 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
