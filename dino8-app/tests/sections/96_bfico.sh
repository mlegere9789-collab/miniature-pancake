# ---------------------------------------------------------------------------
# BFICO_: Flow Register row geometry_nodes/Ico Sphere (Ico Sphere), flowrows_gen.py. The graph flowrows_bfico.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Subdivisions s: F = 20 * 4^(s-1), E = 30 * 4^(s-1), V = 10 * 4^(s-1) + 2 (Euler: V - E + F = 2): 12/30/20, 42/120/80, 162/480/320, 642/1920/1280; the s = 1 solid is the icosahedron of circumradius R: edge a = R / sin(2 pi/5), volume 5 (3 + sqrt 5) a^3 / 12, vertices at the poles and two rings of five at height +-R/sqrt 5, radius 2 R / sqrt 5.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfico.dflow|g" "$HERE/flowrows_bfico_script.txt" > "$TMPW/flowrows_bfico_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfico_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFICO_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfico_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFICO_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFICO_$1: $3 (measured '$got')"; else echo "FAIL BFICO_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 106 node(s)" <<<"$FR_OUT"; then echo "ok   BFICO_solve: the player solved all 106 nodes (every node type is registered)"; else echo "FAIL BFICO_solve: expected 'solved 106 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 28 " <<<"$FR_OUT"; then echo "ok   BFICO_objects: exactly 28 point(s) baked"; else echo "FAIL BFICO_objects: expected 28 baked objects"; fail=1; fi
fr_chk 1 "12" "icosahedron: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 12 points"
fr_chk 2 "30" "icosahedron: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 30 edges"
fr_chk 3 "20" "icosahedron: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 20 faces"
fr_chk 4 "60" "icosahedron: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 60 corners"
fr_chk 5 "42" "2 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 42 points"
fr_chk 6 "120" "2 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 120 edges"
fr_chk 7 "80" "2 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 80 faces"
fr_chk 8 "240" "2 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 240 corners"
fr_chk 9 "162" "3 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 162 points"
fr_chk 10 "480" "3 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 480 edges"
fr_chk 11 "320" "3 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 320 faces"
fr_chk 12 "960" "3 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 960 corners"
fr_chk 13 "642" "4 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 642 points"
fr_chk 14 "1920" "4 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 1920 edges"
fr_chk 15 "1280" "4 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 1280 faces"
fr_chk 16 "3840" "4 subdivisions: V = 10 * 4^(s-1) + 2, E = 30 * 4^(s-1), F = 20 * 4^(s-1),: 3840 corners"
fr_chk 17 "0" "icosahedron of circumradius 2: edge a = R / sin(2 pi/5), volume 5 (3 + sqrt 5) a^3 / 12"
fr_chk 18 "0" "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5: min X"
fr_chk 19 "0" "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5: min Y"
fr_chk 20 "0" "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5: min Z"
fr_chk 21 "0" "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5: max X"
fr_chk 22 "0" "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5: max Y"
fr_chk 23 "0" "poles at +-R; two rings of 5 at height +-R/sqrt 5 and radius 2R/sqrt 5: max Z"
fr_chk 24 "0" "the poles stay at z = +-1 after subdividing: min Z"
fr_chk 25 "0" "the poles stay at z = +-1 after subdividing: max Z"
fr_chk 26 "12" "the default has one subdivision level:: 12 points"
fr_chk 27 "20" "the default has one subdivision level:: 20 faces"
fr_chk 28 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
