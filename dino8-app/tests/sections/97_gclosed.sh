# ---------------------------------------------------------------------------
# GCLOSED_: Flow Register row D8-GH-CURVE-0016 (Is Closed), flowgeo_gen.py. The graph flowgeo_gclosed.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: closed: a circle, a rectangle, a polygon, a circle through three points, a polyline given Closed = true; open: a line, a 90 degree arc, a polyline of three vertices, a cubic NURBS curve through 4 control points; a NURBS curve given Closed = true repeats its first control point and closes.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gclosed.dflow|g" "$HERE/flowgeo_gclosed_script.txt" > "$TMPW/flowgeo_gclosed_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gclosed_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCLOSED_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gclosed_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCLOSED_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GCLOSED_$1: $3 (measured '$got')"; else echo "FAIL GCLOSED_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 86 node(s)" <<<"$FR_OUT"; then echo "ok   GCLOSED_solve: the player solved all 86 nodes (every node type is registered)"; else echo "FAIL GCLOSED_solve: expected 'solved 86 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 15 " <<<"$FR_OUT"; then echo "ok   GCLOSED_objects: exactly 15 point(s) baked"; else echo "FAIL GCLOSED_objects: expected 15 baked objects"; fail=1; fi
fr_chk 1 "1" "circle: Is Closed = 1"
fr_chk 2 "1" "rectangle: Is Closed = 1"
fr_chk 3 "1" "polygon: Is Closed = 1"
fr_chk 4 "1" "circle through three points: Is Closed = 1"
fr_chk 5 "1" "closed polyline: Is Closed = 1"
fr_chk 6 "1" "NURBS curve with Closed = true: Is Closed = 1"
fr_chk 7 "0" "line: Is Closed = 0"
fr_chk 8 "1" "line: Is Closed = 0 (the node under test produced the value)"
fr_chk 9 "0" "90 degree arc: Is Closed = 0"
fr_chk 10 "1" "90 degree arc: Is Closed = 0 (the node under test produced the value)"
fr_chk 11 "0" "open polyline: Is Closed = 0"
fr_chk 12 "1" "open polyline: Is Closed = 0 (the node under test produced the value)"
fr_chk 13 "0" "cubic NURBS curve: Is Closed = 0"
fr_chk 14 "1" "cubic NURBS curve: Is Closed = 0 (the node under test produced the value)"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
