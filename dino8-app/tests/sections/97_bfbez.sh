# ---------------------------------------------------------------------------
# BFBEZ_: Flow Register row geometry_nodes/Bézier Segment (Bezier Segment), flowrows_gen.py. The graph flowrows_bfbez.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: B(t) = (1-t)^3 P0 + 3 (1-t)^2 t H0 + 3 (1-t) t^2 H1 + t^3 P1 at t = i / Resolution (Resolution + 1 points): P0 = (0,0,0), H0 = (0,6,0), H1 = (6,6,0), P1 = (6,0,0) gives x(t) = 6 (3t^2 - 2t^3) ... y(t) = 18 t (1-t), peak (3, 4.5, 0) at t = 1/2; defaults peak at y = 0.375; 1 edge is the chord. The Resolution = edges convention is from recollection of the Blender manual.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfbez.dflow|g" "$HERE/flowrows_bfbez_script.txt" > "$TMPW/flowrows_bfbez_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfbez_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFBEZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfbez_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFBEZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFBEZ_$1: $3 (measured '$got')"; else echo "FAIL BFBEZ_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 99 node(s)" <<<"$FR_OUT"; then echo "ok   BFBEZ_solve: the player solved all 99 nodes (every node type is registered)"; else echo "FAIL BFBEZ_solve: expected 'solved 99 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 18 " <<<"$FR_OUT"; then echo "ok   BFBEZ_objects: exactly 18 point(s) baked"; else echo "FAIL BFBEZ_objects: expected 18 baked objects"; fail=1; fi
fr_chk 1 "9" "8 edges give: 9 points"
fr_chk 2 "1" "8 edges give: 1 splines"
fr_chk 3 "0" "the 8-edge polyline of the cubic Bezier through (0,0,0) (0,6,0) (6,6,0) (6,0,0)"
fr_chk 4 "0" "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6: min X"
fr_chk 5 "0" "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6: min Y"
fr_chk 6 "0" "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6: min Z"
fr_chk 7 "0" "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6: max X"
fr_chk 8 "0" "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6: max Y"
fr_chk 9 "0" "B(1/2) = (3, 4.5, 0) is the highest point (y(t) = 18 t (1-t)); x runs 0..6: max Z"
fr_chk 10 "17" "the default Resolution 16 gives: 17 points"
fr_chk 11 "0" "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375: min X"
fr_chk 12 "0" "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375: min Y"
fr_chk 13 "0" "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375: min Z"
fr_chk 14 "0" "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375: max X"
fr_chk 15 "0" "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375: max Y"
fr_chk 16 "0" "default (-1,0,0) (-0.5,0.5,0) (0.5,0.5,0) (1,0,0): B(1/2).y = 3 * 0.125 * 0.5 * 2 = 0.375: max Z"
fr_chk 17 "0" "1 edge joins the end points (-1,0,0) and (1,0,0): length 2"
fr_chk 18 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
