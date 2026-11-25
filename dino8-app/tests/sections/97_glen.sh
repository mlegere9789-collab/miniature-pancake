# ---------------------------------------------------------------------------
# GLEN_: Flow Register row D8-GH-CURVE-0020 (Curve Length), flowgeo_gen.py. The graph flowgeo_glen.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: circle r = 1: 2 pi; r = 2.5: 5 pi; r = 10: 20 pi (within 1e-9, where the 400-chord sum was short by 6e-5 on r = 1); arc of radius 2 over 270 degrees: r * 3 pi / 2 = 3 pi; line (0,0,0)-(3,4,0): 5; polyline (0,0,0),(6,0,0),(6,4,0),(0,4,0) closed: 6 + 4 + 6 + 4 = 20; a cubic curve whose four control points (0,0,0),(1,1,0),(5,5,0),(6,6,0) lie on one line is that segment: 6 sqrt 2 (a straight clamped curve between its end control points).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_glen.dflow|g" "$HERE/flowgeo_glen_script.txt" > "$TMPW/flowgeo_glen_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_glen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_glen_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GLEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GLEN_$1: $3 (measured '$got')"; else echo "FAIL GLEN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 67 node(s)" <<<"$FR_OUT"; then echo "ok   GLEN_solve: the player solved all 67 nodes (every node type is registered)"; else echo "FAIL GLEN_solve: expected 'solved 67 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 9 " <<<"$FR_OUT"; then echo "ok   GLEN_objects: exactly 9 point(s) baked"; else echo "FAIL GLEN_objects: expected 9 baked objects"; fail=1; fi
fr_chk 1 "0" "circle r = 1: 2 pi"
fr_chk 2 "0" "circle r = 2.5: 5 pi"
fr_chk 3 "0" "circle r = 10: 20 pi"
fr_chk 4 "0" "arc of radius 2 over 270 degrees: 3 pi"
fr_chk 5 "0" "line (0,0,0)-(3,4,0): 5"
fr_chk 6 "0" "closed rectangle polyline 6 x 4: 20"
fr_chk 7 "0" "cubic with four collinear control points (0,0,0)..(6,6,0): the segment, 6 sqrt 2"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
