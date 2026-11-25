# ---------------------------------------------------------------------------
# GCNR_: Flow Register row D8-GH-CURVE-0043 (Circle CNR), flowgeo_gen.py. The graph flowgeo_gcnr.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the circle about centre c with normal n and radius r: every point is at distance r from c and (p - c).n = 0; it runs counter-clockwise about n, so (p - c) x tangent = r * unit(n) (centre (1,2,3), normal (0,0,2), r = 2: (0,0,2); centre (-1,0,2), normal (1,2,2) (length 3), r = 3: 3 * (1,2,2)/3 = (1,2,2)); the length is 2 pi r.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_gcnr.dflow|g" "$HERE/flowgeo_gcnr_script.txt" > "$TMPW/flowgeo_gcnr_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcnr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCNR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_gcnr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GCNR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GCNR_$1: $3 (measured '$got')"; else echo "FAIL GCNR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 117 node(s)" <<<"$FR_OUT"; then echo "ok   GCNR_solve: the player solved all 117 nodes (every node type is registered)"; else echo "FAIL GCNR_solve: expected 'solved 117 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 20 " <<<"$FR_OUT"; then echo "ok   GCNR_objects: exactly 20 point(s) baked"; else echo "FAIL GCNR_objects: expected 20 baked objects"; fail=1; fi
fr_chk 1 "0" "case 1: the length is 2 pi r"
fr_chk 2 "0" "case 1, t = 0: the point is at distance r from the centre"
fr_chk 3 "0" "case 1, t = 0: the point lies in the plane (radius vector . normal = 0)"
fr_chk 4 "1" "case 1, t = 0: the point lies in the plane (radius vector . normal = 0) (the node under test produced the value)"
fr_chk 5 "0" "case 1, t = 0: radius x tangent = r * unit(normal) (0, 0, 2), counter-clockwise about the normal"
fr_chk 6 "0" "case 1, t = 1.9: the point is at distance r from the centre"
fr_chk 7 "0" "case 1, t = 1.9: the point lies in the plane (radius vector . normal = 0)"
fr_chk 8 "1" "case 1, t = 1.9: the point lies in the plane (radius vector . normal = 0) (the node under test produced the value)"
fr_chk 9 "0" "case 1, t = 1.9: radius x tangent = r * unit(normal) (0, 0, 2), counter-clockwise about the normal"
fr_chk 10 "0" "case 2: the length is 2 pi r"
fr_chk 11 "0" "case 2, t = 0: the point is at distance r from the centre"
fr_chk 12 "0" "case 2, t = 0: the point lies in the plane (radius vector . normal = 0)"
fr_chk 13 "1" "case 2, t = 0: the point lies in the plane (radius vector . normal = 0) (the node under test produced the value)"
fr_chk 14 "0" "case 2, t = 0: radius x tangent = r * unit(normal) (1, 2, 2), counter-clockwise about the normal"
fr_chk 15 "0" "case 2, t = 1.9: the point is at distance r from the centre"
fr_chk 16 "0" "case 2, t = 1.9: the point lies in the plane (radius vector . normal = 0)"
fr_chk 17 "1" "case 2, t = 1.9: the point lies in the plane (radius vector . normal = 0) (the node under test produced the value)"
fr_chk 18 "0" "case 2, t = 1.9: radius x tangent = r * unit(normal) (1, 2, 2), counter-clockwise about the normal"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
