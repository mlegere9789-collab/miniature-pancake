# ---------------------------------------------------------------------------
# BFUVSP_: Flow Register row geometry_nodes/UV Sphere (UV Sphere), flowrows_gen.py. The graph flowrows_bfuvsp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: S segments, R rings: V = S (R - 1) + 2, F = S R (triangles at the poles, quads between), E = S (R - 1) ring edges + S R meridian edges, corners 6 S + 4 S (R - 2); volume = (S/6) sin(2 pi/S) sum over ring pairs h (rho1^2 + rho1 rho2 + rho2^2) with rho_k = r sin(pi k / R), z_k = r cos(pi k / R) (S/2 rho^2 sin(2 pi/S) is the polygon area); poles on the Z axis so the box is +-r.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfuvsp.dflow|g" "$HERE/flowrows_bfuvsp_script.txt" > "$TMPW/flowrows_bfuvsp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfuvsp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFUVSP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfuvsp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFUVSP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFUVSP_$1: $3 (measured '$got')"; else echo "FAIL BFUVSP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 69 node(s)" <<<"$FR_OUT"; then echo "ok   BFUVSP_solve: the player solved all 69 nodes (every node type is registered)"; else echo "FAIL BFUVSP_solve: expected 'solved 69 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 16 " <<<"$FR_OUT"; then echo "ok   BFUVSP_objects: exactly 16 point(s) baked"; else echo "FAIL BFUVSP_objects: expected 16 baked objects"; fail=1; fi
fr_chk 1 "26" "8 segments, 4 rings:: 26 points"
fr_chk 2 "56" "8 segments, 4 rings:: 56 edges"
fr_chk 3 "32" "8 segments, 4 rings:: 32 faces"
fr_chk 4 "112" "8 segments, 4 rings:: 112 corners"
fr_chk 5 "0" "poles on +-Z, rings at 90 degree multiples: box +-2: min X"
fr_chk 6 "0" "poles on +-Z, rings at 90 degree multiples: box +-2: min Y"
fr_chk 7 "0" "poles on +-Z, rings at 90 degree multiples: box +-2: min Z"
fr_chk 8 "0" "poles on +-Z, rings at 90 degree multiples: box +-2: max X"
fr_chk 9 "0" "poles on +-Z, rings at 90 degree multiples: box +-2: max Y"
fr_chk 10 "0" "poles on +-Z, rings at 90 degree multiples: box +-2: max Z"
fr_chk 11 "0" "volume of the 8 x 4 frustum stack, radius 2"
fr_chk 12 "482" "the default 32 x 16 sphere has: 482 points"
fr_chk 13 "992" "the default 32 x 16 sphere has: 992 edges"
fr_chk 14 "512" "the default 32 x 16 sphere has: 512 faces"
fr_chk 15 "0" "default volume (frustum stack, within 5e-6 of the 4.1219 polyhedron)"
fr_chk 16 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
