# ---------------------------------------------------------------------------
# GARC_: Flow Register row D8-GH-CURVE-0045 (Arc), flowgeo_gen.py. The graph flowgeo_garc.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: arc length r * (end - start) in radians: radius 2 from 30 to 120 degrees is 2 * pi/2 = pi, starting at 2 (cos 30, sin 30) = (sqrt 3, 1, 0) and ending at 2 (cos 120, sin 120) = (-1, sqrt 3, 0), its point half way along at angle 75 degrees is (2 cos 75, 2 sin 75, 0); from 0 to 270 degrees (3 pi) it starts at (2,0,0), ends at (0,-2,0), half way is at 135 degrees: (-sqrt 2, sqrt 2, 0); in the plane through (1,2,3) with axes (0,1,0) and (0,0,1) (normal (1,0,0)), radius 1.5 from 0 to 90 degrees starts at (1,3.5,3), ends at (1,2,4.5) and is 0.75 pi long.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowgeo_garc.dflow|g" "$HERE/flowgeo_garc_script.txt" > "$TMPW/flowgeo_garc_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowgeo_garc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GARC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowgeo_garc_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GARC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GARC_$1: $3 (measured '$got')"; else echo "FAIL GARC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 91 node(s)" <<<"$FR_OUT"; then echo "ok   GARC_solve: the player solved all 91 nodes (every node type is registered)"; else echo "FAIL GARC_solve: expected 'solved 91 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   GARC_objects: exactly 14 point(s) baked"; else echo "FAIL GARC_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "0" "arc 1: the length is r * angle = 3.14159"
fr_chk 2 "0" "arc 1: it starts at (1.732051, 1, 0)"
fr_chk 3 "0" "arc 1: it ends at (-1, 1.732051, 0)"
fr_chk 4 "0" "arc 1: the point half way along is (0.517638, 1.931852, 0)"
fr_chk 5 "0" "arc 2: the length is r * angle = 9.42478"
fr_chk 6 "0" "arc 2: it starts at (2, 0, 0)"
fr_chk 7 "0" "arc 2: it ends at (0, -2, 0)"
fr_chk 8 "0" "arc 2: the point half way along is (-1.414214, 1.414214, 0)"
fr_chk 9 "0" "arc 3: the length is r * angle = 2.35619"
fr_chk 10 "0" "arc 3: it starts at (1, 3.5, 3)"
fr_chk 11 "0" "arc 3: it ends at (1, 2, 4.5)"
fr_chk 12 "0" "arc 3: the point half way along is (1, 3.06066, 4.06066)"
fr_chk 98 "-1000" "control: the same value against its closed form plus 1e-6 reads -1000, so a 1e-6 error is seen"
fr_chk 99 "-500000000" "control: the residual chain reports Square(2) - 4.5 as -500000000, so the 0 values above are real matches"
