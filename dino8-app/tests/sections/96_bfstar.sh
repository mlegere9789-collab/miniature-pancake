# ---------------------------------------------------------------------------
# BFSTAR_: Flow Register row geometry_nodes/Star (Star), flowrows_gen.py. The graph flowrows_bfstar.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 2P vertices alternating outer (angle 2 pi i / P) and inner (angle 2 pi (i + 1/2) / P + twist): without twist every edge is sqrt(ro^2 + ri^2 - 2 ro ri cos(pi/P)); with twist the two edges of each tooth are sqrt(.. cos(pi/P + tw)) and sqrt(.. cos(pi/P - tw)); ro = ri = 1, P = 3 is a hexagon of perimeter 6.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfstar.dflow|g" "$HERE/flowrows_bfstar_script.txt" > "$TMPW/flowrows_bfstar_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfstar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSTAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfstar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSTAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSTAR_$1: $3 (measured '$got')"; else echo "FAIL BFSTAR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 69 node(s)" <<<"$FR_OUT"; then echo "ok   BFSTAR_solve: the player solved all 69 nodes (every node type is registered)"; else echo "FAIL BFSTAR_solve: expected 'solved 69 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 13 " <<<"$FR_OUT"; then echo "ok   BFSTAR_objects: exactly 13 point(s) baked"; else echo "FAIL BFSTAR_objects: expected 13 baked objects"; fail=1; fi
fr_chk 1 "10" "a 5-point star has: 10 points"
fr_chk 2 "1" "a 5-point star has: 1 splines"
fr_chk 3 "0" "10 edges, each sqrt(ro^2 + ri^2 - 2 ro ri cos(pi/5)) by the law of cosines"
fr_chk 4 "0" "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72: min X"
fr_chk 5 "0" "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72: min Y"
fr_chk 6 "0" "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72: min Z"
fr_chk 7 "0" "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72: max X"
fr_chk 8 "0" "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72: max Y"
fr_chk 9 "0" "outer points at 0, 72, ..., 288 degrees: x from 2 cos 144 to 2, y +-2 sin 72: max Z"
fr_chk 10 "0" "twist pi/10 turns each inner point towards the next outer one: edges at angles pi/5 + tw and pi/5 - tw"
fr_chk 11 "0" "equal radii make a regular hexagon of side 1: perimeter 6"
fr_chk 12 "16" "the default star (8 points) has: 16 points"
fr_chk 13 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
