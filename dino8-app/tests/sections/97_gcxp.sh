# ---------------------------------------------------------------------------
# GCXP_: Flow Register row D8-GH-XSECT-0003 (Curve x Plane), flowgeo_gen.py. The graph flowgeo_gcxp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the circle of radius 3 about the origin and the plane x = 0: 2 points, (0,3,0) then (0,-3,0) in curve order; the plane x = 1: (1, sqrt 8, 0) then (1,-sqrt 8, 0) (1 + 8 = 9); the plane x = 5: none; the line (0,0,0)-(0,0,10) and the plane z = 4: (0,0,4); the closed polyline (0,0),(6,0),(6,4),(0,4) and the plane x = 3: (3,0,0) and (3,4,0).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gcxp.dflow|g" "$HERE/flowgeo_gcxp_script.txt" > "$TMPW/flowgeo_gcxp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcxp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCXP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcxp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCXP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GCXP_$1: $3 (measured '$got')"; else echo "FAIL GCXP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 108 node(s)" <<<"$FR_OUT"; then echo "ok   GCXP_solve: the player solved all 108 nodes (every node type is registered)"; else echo "FAIL GCXP_solve: expected 'solved 108 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   GCXP_objects: exactly 14 point(s) baked"; else echo "FAIL GCXP_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "2" "circle x (x = 0): 2 points"
fr_chk 2 "0" "circle x (x = 0): the first point (0,3,0)"
fr_chk 3 "0" "circle x (x = 0): the second point (0,-3,0)"
fr_chk 4 "2" "circle x (x = 1): 2 points"
fr_chk 5 "0" "circle x (x = 1): the first point (1,sqrt 8,0)"
fr_chk 6 "0" "circle x (x = 1): the second point (1,-sqrt 8,0)"
fr_chk 7 "0" "circle x (x = 5): no point"
fr_chk 8 "1" "line x (z = 4): 1 point"
fr_chk 9 "0" "line x (z = 4): the point (0,0,4)"
fr_chk 10 "2" "rectangle x (x = 3): 2 points"
fr_chk 11 "0" "rectangle x (x = 3): the first point (3,0,0)"
fr_chk 12 "0" "rectangle x (x = 3): the second point (3,4,0)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
