# ---------------------------------------------------------------------------
# BFQBEZ_: Flow Register row geometry_nodes/Quadratic Bézier (Quadratic Bezier), flowrows_gen.py. The graph flowrows_bfqbez.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: B(t) = (1-t)^2 P0 + 2 t (1-t) P1 + t^2 P2 sampled at t = i / Resolution (Resolution + 1 points): P0 = (0,0,0), P1 = (4,8,0), P2 = (8,0,0) gives (8 t, 16 t (1-t), 0), the peak B(1/2) = (4,4,0); 2 edges is (0,0,0)-(4,4,0)-(8,0,0). The Resolution = edges convention is from recollection of the Blender manual.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfqbez.dflow|g" "$HERE/flowrows_bfqbez_script.txt" > "$TMPW/flowrows_bfqbez_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfqbez_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFQBEZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfqbez_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFQBEZ_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFQBEZ_$1: $3 (measured '$got')"; else echo "FAIL BFQBEZ_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 98 node(s)" <<<"$FR_OUT"; then echo "ok   BFQBEZ_solve: the player solved all 98 nodes (every node type is registered)"; else echo "FAIL BFQBEZ_solve: expected 'solved 98 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 18 " <<<"$FR_OUT"; then echo "ok   BFQBEZ_objects: exactly 18 point(s) baked"; else echo "FAIL BFQBEZ_objects: expected 18 baked objects"; fail=1; fi
fr_chk 1 "9" "8 edges give: 9 points"
fr_chk 2 "1" "8 edges give: 1 splines"
fr_chk 3 "0" "the 8-edge polyline of B(t) = (8 t, 16 t (1-t), 0)"
fr_chk 4 "0" "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0): min X"
fr_chk 5 "0" "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0): min Y"
fr_chk 6 "0" "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0): min Z"
fr_chk 7 "0" "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0): max X"
fr_chk 8 "0" "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0): max Y"
fr_chk 9 "0" "ends at Start and End, peak B(1/2) = (Start + 2 Middle + End) / 4 = (4, 4, 0): max Z"
fr_chk 10 "0" "2 edges: (0,0,0) -> (4,4,0) -> (8,0,0), length 2 * 4 sqrt 2"
fr_chk 11 "17" "the default Resolution 16 gives: 17 points"
fr_chk 12 "0" "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0): min X"
fr_chk 13 "0" "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0): min Y"
fr_chk 14 "0" "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0): min Z"
fr_chk 15 "0" "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0): max X"
fr_chk 16 "0" "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0): max Y"
fr_chk 17 "0" "default Start (-1,0,0), Middle (0,2,0), End (1,0,0): peak (0,1,0): max Z"
fr_chk 18 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
