# ---------------------------------------------------------------------------
# GPPP_: Flow Register row D8-GH-XSECT-0006 (Plane x Plane x Plane), flowgeo_gen.py. The graph flowgeo_gppp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the planes x = 1, y = 2, z = 3 meet at (1,2,3); the planes n.p = d with n1 = (1,1,1), d1 = 6; n2 = (1,-1,0), d2 = -1; n3 = (0,2,-1), d3 = 1 all hold at (1,2,3) (1 + 2 + 3 = 6, 1 - 2 = -1, 4 - 3 = 1) and the determinant of the normals is 1 (0 + 2) - 1 (0 - 0) + 1 (2 - 0) = 4, nonzero, so it is the only common point; the planes 2x + y = -0.25, y + 3z = 4.75, x - z = -1.5 meet at (0.5,-1.25,2) (1 - 1.25 = -0.25, -1.25 + 6 = 4.75, 0.5 - 2 = -1.5).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gppp.dflow|g" "$HERE/flowgeo_gppp_script.txt" > "$TMPW/flowgeo_gppp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gppp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPPP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gppp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GPPP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GPPP_$1: $3 (measured '$got')"; else echo "FAIL GPPP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 59 node(s)" <<<"$FR_OUT"; then echo "ok   GPPP_solve: the player solved all 59 nodes (every node type is registered)"; else echo "FAIL GPPP_solve: expected 'solved 59 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 5 " <<<"$FR_OUT"; then echo "ok   GPPP_objects: exactly 5 point(s) baked"; else echo "FAIL GPPP_objects: expected 5 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the three planes meet at (1, 2, 3)"
fr_chk 2 "0" "case 2: the three planes meet at (1, 2, 3)"
fr_chk 3 "0" "case 3: the three planes meet at (0.5, -1.25, 2)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
