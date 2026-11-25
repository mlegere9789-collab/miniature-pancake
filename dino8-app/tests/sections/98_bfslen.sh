# ---------------------------------------------------------------------------
# BFSLEN_: Flow Register row geometry_nodes/Spline Length (Spline Length), flowrows_gen.py. The graph flowrows_bfslen.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: total length and point count: line 10 / 2; rectangle 6 x 4 = 20 / 4 (closing edge included); 8-gon of radius 2 = 8 * 2 * 2 sin(pi/8) / 8 points; 5-point star ro 2, ri 1 = 10 sqrt(5 - 4 cos 36 degrees) / 10 points; the 1-edge Bezier chord is 2 / 2 points.
# ---------------------------------------------------------------------------
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfslen.dflow" > "$TMPW/flowrows_bfslen.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfslen.dflow|g" "$HERE/flowrows_bfslen_script.txt" > "$TMPW/flowrows_bfslen_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfslen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfslen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSLEN_$1: $3 (measured '$got')"; else echo "FAIL BFSLEN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 53 node(s)" <<<"$FR_OUT"; then echo "ok   BFSLEN_solve: the player solved all 53 nodes (every node type is registered)"; else echo "FAIL BFSLEN_solve: expected 'solved 53 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 11 " <<<"$FR_OUT"; then echo "ok   BFSLEN_objects: exactly 11 point(s) baked"; else echo "FAIL BFSLEN_objects: expected 11 baked objects"; fail=1; fi
fr_chk 1 "0" "line (0,0,0)-(0,0,10): length"
fr_chk 2 "2" "line (0,0,0)-(0,0,10): 2 points"
fr_chk 3 "0" "6 x 4 rectangle (closing edge included): length"
fr_chk 4 "4" "6 x 4 rectangle (closing edge included): 4 points"
fr_chk 5 "0" "8-gon of radius 2: length"
fr_chk 6 "8" "8-gon of radius 2: 8 points"
fr_chk 7 "0" "5-point star: length"
fr_chk 8 "10" "5-point star: 10 points"
fr_chk 9 "0" "one Bezier edge is the chord of length 2"
fr_chk 10 "2" "with 2 points"
fr_chk 11 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
