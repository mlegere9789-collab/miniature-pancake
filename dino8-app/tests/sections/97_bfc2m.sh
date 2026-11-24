# ---------------------------------------------------------------------------
# BFC2M_: Flow Register row geometry_nodes/Curve to Mesh (Curve to Mesh), flowrows_gen.py. The graph flowrows_bfc2m.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: n points along the curve x m profile points: V = n m, F = (n-1) m quads (+2 n-gon caps), E = V + F - 2 for a capped tube (Euler): 8-gon profile along a 2-point line V 16, E 24, F 10 (8 + 2), corners 48; volume = profile area (m/2) r^2 sin(2 pi/m) x length 5 = 14.14...; the same with 4 rings (V 32, F 26, E 56); a cyclic path with a cyclic profile is a torus: V - E + F = 0 (32 / 64 / 32); an open 2-point profile along 4 points is a sheet of 3 quads of area 2 x 5 = 10. The profile frame (X along cross(Z, T)) is from recollection; only frame-independent quantities are checked off the Z axis.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfc2m.dflow|g" "$HERE/flowrows_bfc2m_script.txt" > "$TMPW/flowrows_bfc2m_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfc2m_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFC2M_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfc2m_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFC2M_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFC2M_$1: $3 (measured '$got')"; else echo "FAIL BFC2M_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 146 node(s)" <<<"$FR_OUT"; then echo "ok   BFC2M_solve: the player solved all 146 nodes (every node type is registered)"; else echo "FAIL BFC2M_solve: expected 'solved 146 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 35 " <<<"$FR_OUT"; then echo "ok   BFC2M_objects: exactly 35 point(s) baked"; else echo "FAIL BFC2M_objects: expected 35 baked objects"; fail=1; fi
fr_chk 1 "16" "8-gon profile along a 2-point line with caps: V = 2 * 8, F = 8 quads + 2 caps, E = 24,: 16 points"
fr_chk 2 "24" "8-gon profile along a 2-point line with caps: V = 2 * 8, F = 8 quads + 2 caps, E = 24,: 24 edges"
fr_chk 3 "10" "8-gon profile along a 2-point line with caps: V = 2 * 8, F = 8 quads + 2 caps, E = 24,: 10 faces"
fr_chk 4 "48" "8-gon profile along a 2-point line with caps: V = 2 * 8, F = 8 quads + 2 caps, E = 24,: 48 corners"
fr_chk 5 "0" "volume = profile area (8/2) sin(45 degrees) times length 5"
fr_chk 6 "0" "the profile keeps its X and Y, the curve gives Z: min X"
fr_chk 7 "0" "the profile keeps its X and Y, the curve gives Z: min Y"
fr_chk 8 "0" "the profile keeps its X and Y, the curve gives Z: min Z"
fr_chk 9 "0" "the profile keeps its X and Y, the curve gives Z: max X"
fr_chk 10 "0" "the profile keeps its X and Y, the curve gives Z: max Y"
fr_chk 11 "0" "the profile keeps its X and Y, the curve gives Z: max Z"
fr_chk 12 "16" "without caps:: 16 points"
fr_chk 13 "24" "without caps:: 24 edges"
fr_chk 14 "8" "without caps:: 8 faces"
fr_chk 15 "32" "without caps:: 32 corners"
fr_chk 16 "32" "4 curve points x 8 profile points:: 32 points"
fr_chk 17 "56" "4 curve points x 8 profile points:: 56 edges"
fr_chk 18 "26" "4 curve points x 8 profile points:: 26 faces"
fr_chk 19 "0" "the same volume with 4 rings"
fr_chk 20 "0" "along X the swept volume is the same"
fr_chk 21 "32" "cyclic path x cyclic profile (a torus): V - E + F = 0 with V = 8 * 4,: 32 points"
fr_chk 22 "64" "cyclic path x cyclic profile (a torus): V - E + F = 0 with V = 8 * 4,: 64 edges"
fr_chk 23 "32" "cyclic path x cyclic profile (a torus): V - E + F = 0 with V = 8 * 4,: 32 faces"
fr_chk 24 "128" "cyclic path x cyclic profile (a torus): V - E + F = 0 with V = 8 * 4,: 128 corners"
fr_chk 25 "0" "the square profile's Y becomes world Z for a path in the XY plane: min Z"
fr_chk 26 "0" "the square profile's Y becomes world Z for a path in the XY plane: max Z"
fr_chk 27 "8" "open profile along 4 points: a sheet of 3 quads,: 8 points"
fr_chk 28 "10" "open profile along 4 points: a sheet of 3 quads,: 10 edges"
fr_chk 29 "3" "open profile along 4 points: a sheet of 3 quads,: 3 faces"
fr_chk 30 "12" "open profile along 4 points: a sheet of 3 quads,: 12 corners"
fr_chk 31 "0" "sheet area width 2 x length 5"
fr_chk 32 "2" "no profile: the curve's points joined by edges:: 2 points"
fr_chk 33 "1" "no profile: the curve's points joined by edges:: 1 edges"
fr_chk 34 "0" "no profile: the curve's points joined by edges:: 0 faces"
fr_chk 35 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
