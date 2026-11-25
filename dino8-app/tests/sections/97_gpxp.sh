# ---------------------------------------------------------------------------
# GPXP_: Flow Register row D8-GH-XSECT-0005 (Plane x Plane), flowgeo_gen.py. The graph flowgeo_gpxp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: z = 1 (origin (0,0,1), normal (0,0,1)) and x = 2 (origin (2,0,0), normal (1,0,0)) meet in the line x = 2, z = 1 along n1 x n2 = (0,1,0), nearest to (0,0,1) at (2,0,1); x + y = 3 (origin (1,2,3), normal (1,1,0)) and y + z = 0 (origin (0,0,0), normal (0,1,1)) meet along (1,-1,1)/sqrt 3 through (8/3,1/3,-1/3) (it satisfies both equations, and (p - (1,2,3)).(1,-1,1) = 5/3 + 5/3 - 10/3 = 0 makes it the nearest to the first origin); the segment returned is 2 long about that point.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gpxp.dflow|g" "$HERE/flowgeo_gpxp_script.txt" > "$TMPW/flowgeo_gpxp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gpxp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPXP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gpxp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPXP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GPXP_$1: $3 (measured '$got')"; else echo "FAIL GPXP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 71 node(s)" <<<"$FR_OUT"; then echo "ok   GPXP_solve: the player solved all 71 nodes (every node type is registered)"; else echo "FAIL GPXP_solve: expected 'solved 71 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   GPXP_objects: exactly 10 point(s) baked"; else echo "FAIL GPXP_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the point nearest the first origin is (2, 0, 1)"
fr_chk 2 "0" "case 1: the direction is n1 x n2 normalized (0, 1, 0)"
fr_chk 3 "0" "case 1: the segment is 2 long"
fr_chk 4 "0" "case 1: the segment is centred on that point"
fr_chk 5 "0" "case 2: the point nearest the first origin is (2.666667, 0.333333, -0.333333)"
fr_chk 6 "0" "case 2: the direction is n1 x n2 normalized (0.57735, -0.57735, 0.57735)"
fr_chk 7 "0" "case 2: the segment is 2 long"
fr_chk 8 "0" "case 2: the segment is centred on that point"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
