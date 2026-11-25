# ---------------------------------------------------------------------------
# BFSPAR_: Flow Register row geometry_nodes/Spline Parameter (Spline Parameter), flowrows_gen.py. The graph flowrows_bfspar.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: distance along the spline: the 5 points of the line z 0..10 (cuts of 2.5) have lengths 0, 2.5, 5, 7.5, 10 and factors 0, 0.25, 0.5, 0.75, 1 and index = position; the open rectangle (3,2) (-3,2) (-3,-2) (3,-2) of total length 16 has lengths 6, 10, 16 at points 1, 2, 3 (factors 0.375, 0.625, 1). The factor of a cyclic spline (closing edge in the total) is untested.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfspar.dflow" > "$TMPW/flowrows_bfspar.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfspar.dflow|g" "$HERE/flowrows_bfspar_script.txt" > "$TMPW/flowrows_bfspar_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfspar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSPAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfspar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSPAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSPAR_$1: $3 (measured '$got')"; else echo "FAIL BFSPAR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 97 node(s)" <<<"$FR_OUT"; then echo "ok   BFSPAR_solve: the player solved all 97 nodes (every node type is registered)"; else echo "FAIL BFSPAR_solve: expected 'solved 97 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 19 " <<<"$FR_OUT"; then echo "ok   BFSPAR_objects: exactly 19 point(s) baked"; else echo "FAIL BFSPAR_objects: expected 19 baked objects"; fail=1; fi
fr_chk 1 "0" "point 0 of the 5-point line: factor 0"
fr_chk 2 "0" "point 0: length 0"
fr_chk 3 "0" "point 0: index 0"
fr_chk 4 "0" "point 1 of the 5-point line: factor 0.25"
fr_chk 5 "0" "point 1: length 2.5"
fr_chk 6 "1" "point 1: index 1"
fr_chk 7 "0" "point 2 of the 5-point line: factor 0.5"
fr_chk 8 "0" "point 2: length 5"
fr_chk 9 "2" "point 2: index 2"
fr_chk 10 "0" "point 4 of the 5-point line: factor 1"
fr_chk 11 "0" "point 4: length 10"
fr_chk 12 "4" "point 4: index 4"
fr_chk 13 "0" "open rectangle, point 1: length 6"
fr_chk 14 "0" "open rectangle, point 1: factor 6 / 16"
fr_chk 15 "0" "open rectangle, point 2: length 10"
fr_chk 16 "0" "open rectangle, point 2: factor 10 / 16"
fr_chk 17 "0" "open rectangle, point 3: length 16"
fr_chk 18 "0" "open rectangle, point 3: factor 16 / 16"
fr_chk 19 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
