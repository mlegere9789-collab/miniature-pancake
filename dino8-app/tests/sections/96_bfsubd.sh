# ---------------------------------------------------------------------------
# BFSUBD_: Flow Register row geometry_nodes/Subdivide Curve (Subdivide Curve), flowrows_gen.py. The graph flowrows_bfsubd.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: k cuts in each of the m edges: open n points -> (n-1)(k+1) + 1, cyclic n points -> n (k+1): the 4-point rectangle with 2 cuts has 12 points, a 2-point line with 2 cuts 4, with 0 cuts 2, a 5-point curve with 3 cuts 17; the new points lie on the old edges so length and box do not change. The even spacing of the new points is not read back separately (untested).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfsubd.dflow|g" "$HERE/flowrows_bfsubd_script.txt" > "$TMPW/flowrows_bfsubd_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsubd_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSUBD_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsubd_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSUBD_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSUBD_$1: $3 (measured '$got')"; else echo "FAIL BFSUBD_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 83 node(s)" <<<"$FR_OUT"; then echo "ok   BFSUBD_solve: the player solved all 83 nodes (every node type is registered)"; else echo "FAIL BFSUBD_solve: expected 'solved 83 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 15 " <<<"$FR_OUT"; then echo "ok   BFSUBD_objects: exactly 15 point(s) baked"; else echo "FAIL BFSUBD_objects: expected 15 baked objects"; fail=1; fi
fr_chk 1 "12" "a closed 4-point curve with 2 cuts per edge has 4 * 3 =: 12 points"
fr_chk 2 "1" "a closed 4-point curve with 2 cuts per edge has 4 * 3 =: 1 splines"
fr_chk 3 "0" "subdividing keeps the shape: perimeter still 20"
fr_chk 4 "0" "box unchanged: min X"
fr_chk 5 "0" "box unchanged: min Y"
fr_chk 6 "0" "box unchanged: min Z"
fr_chk 7 "0" "box unchanged: max X"
fr_chk 8 "0" "box unchanged: max Y"
fr_chk 9 "0" "box unchanged: max Z"
fr_chk 10 "4" "an open 2-point line with 2 cuts has (2 - 1) * 3 + 1 =: 4 points"
fr_chk 11 "0" "length stays 9"
fr_chk 12 "2" "0 cuts changes nothing:: 2 points"
fr_chk 13 "17" "a 5-point open curve with 3 cuts has 4 * 4 + 1 =: 17 points"
fr_chk 14 "0" "the new points lie on the old edges, so the polyline length is unchanged"
fr_chk 15 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
