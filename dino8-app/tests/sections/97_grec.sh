# ---------------------------------------------------------------------------
# GREC_: Flow Register row D8-GH-CURVE-0050 (Rectangle), flowgeo_gen.py. The graph flowgeo_grec.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: a 6 x 4 rectangle has area 6 * 4 = 24 and perimeter 20; 3 x 7 in the plane through (0,0,0) with normal (1,2,2): area 21, perimeter 20; it is a closed curve.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_grec.dflow|g" "$HERE/flowgeo_grec_script.txt" > "$TMPW/flowgeo_grec_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_grec_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_grec_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GREC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GREC_$1: $3 (measured '$got')"; else echo "FAIL GREC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 40 node(s)" <<<"$FR_OUT"; then echo "ok   GREC_solve: the player solved all 40 nodes (every node type is registered)"; else echo "FAIL GREC_solve: expected 'solved 40 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 8 " <<<"$FR_OUT"; then echo "ok   GREC_objects: exactly 8 point(s) baked"; else echo "FAIL GREC_objects: expected 8 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: 6 x 4 has area 24"
fr_chk 2 "0" "case 1: perimeter 20"
fr_chk 3 "1" "case 1: the rectangle is a closed curve"
fr_chk 4 "0" "case 2: 3 x 7 has area 21"
fr_chk 5 "0" "case 2: perimeter 20"
fr_chk 6 "1" "case 2: the rectangle is a closed curve"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
