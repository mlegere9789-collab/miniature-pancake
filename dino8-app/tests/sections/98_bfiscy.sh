# ---------------------------------------------------------------------------
# BFISCY_: Flow Register row geometry_nodes/Is Spline Cyclic (Is Spline Cyclic), flowrows_gen.py. The graph flowrows_bfiscy.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: closed shapes are cyclic (rectangle, circle, star), open ones are not (line, spiral, arc, Bezier segment); Set Spline Cyclic true makes the line cyclic, false opens the rectangle; reversing keeps cyclic.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfiscy.dflow" > "$TMPW/flowrows_bfiscy.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfiscy.dflow|g" "$HERE/flowrows_bfiscy_script.txt" > "$TMPW/flowrows_bfiscy_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfiscy_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFISCY_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfiscy_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFISCY_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFISCY_$1: $3 (measured '$got')"; else echo "FAIL BFISCY_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 44 node(s)" <<<"$FR_OUT"; then echo "ok   BFISCY_solve: the player solved all 44 nodes (every node type is registered)"; else echo "FAIL BFISCY_solve: expected 'solved 44 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   BFISCY_objects: exactly 10 point(s) baked"; else echo "FAIL BFISCY_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0" "an open line: cyclic = 0"
fr_chk 2 "1" "a rectangle: cyclic = 1"
fr_chk 3 "1" "a circle: cyclic = 1"
fr_chk 4 "0" "a spiral: cyclic = 0"
fr_chk 5 "1" "a star: cyclic = 1"
fr_chk 6 "0" "an arc: cyclic = 0"
fr_chk 7 "0" "a Bezier segment: cyclic = 0"
fr_chk 8 "1" "the line made cyclic"
fr_chk 9 "0" "the rectangle made open"
fr_chk 10 "1" "reversing keeps it cyclic"
