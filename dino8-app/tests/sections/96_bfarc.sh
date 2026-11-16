# ---------------------------------------------------------------------------
# BFARC_: Flow Register row geometry_nodes/Arc (Arc), flowrows_gen.py. The graph flowrows_bfarc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: points at angle a0 + sweep i / (n-1), both ends included: 5 points over pi at r=2 = 4 chords of 2 r sin(pi/8) = 16 sin(pi/8), box [-2,2] x [0,2]; default 16 points r=1 sweep 5 pi/4 -> 15 chords of 2 sin(5 pi/120).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfarc.dflow|g" "$HERE/flowrows_bfarc_script.txt" > "$TMPW/flowrows_bfarc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfarc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFARC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfarc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFARC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFARC_$1: $3 (measured '$got')"; else echo "FAIL BFARC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 95 node(s)" <<<"$FR_OUT"; then echo "ok   BFARC_solve: the player solved all 95 nodes (every node type is registered)"; else echo "FAIL BFARC_solve: expected 'solved 95 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 18 " <<<"$FR_OUT"; then echo "ok   BFARC_objects: exactly 18 point(s) baked"; else echo "FAIL BFARC_objects: expected 18 baked objects"; fail=1; fi
fr_chk 1 "5" "5 points on a half circle:: 5 points"
fr_chk 2 "1" "5 points on a half circle:: 1 splines"
fr_chk 3 "0" "4 chords of 2 r sin(sweep / (2 * 4)) with r = 2, sweep = pi"
fr_chk 4 "0" "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2: min X"
fr_chk 5 "0" "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2: min Y"
fr_chk 6 "0" "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2: min Z"
fr_chk 7 "0" "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2: max X"
fr_chk 8 "0" "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2: max Y"
fr_chk 9 "0" "half circle of radius 2 from angle 0 to pi: x +-2, y 0..2: max Z"
fr_chk 10 "16" "the default arc has: 16 points"
fr_chk 11 "0" "default: 16 points, radius 1, sweep 5 pi / 4 -> 15 chords of 2 sin(sweep / 30)"
fr_chk 12 "0" "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1: min X"
fr_chk 13 "0" "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1: min Y"
fr_chk 14 "0" "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1: min Z"
fr_chk 15 "0" "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1: max X"
fr_chk 16 "0" "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1: max Y"
fr_chk 17 "0" "3 points from 90 to 270 degrees: (0,1), (-1,0), (0,-1) -> x -1..0, y -1..1: max Z"
fr_chk 18 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
