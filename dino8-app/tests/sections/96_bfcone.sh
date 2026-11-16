# ---------------------------------------------------------------------------
# BFCONE_: Flow Register row geometry_nodes/Cone (Cone), flowrows_gen.py. The graph flowrows_bfcone.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: radius linear in height: a zero end radius collapses to one apex vertex with no cap; volume of a polygonal frustum h (A1 + A2 + sqrt(A1 A2)) / 3 with A = (n/2) r^2 sin(2 pi/n), which is A h / 3 for the cone; apex cone: V = n + 1, F = n + 1 (n triangles + the n-gon base), E = 2 n; frustum with 2 side segments: V = 3 n, F = 2 n + 2, E = 3 n + 2 n.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfcone.dflow|g" "$HERE/flowrows_bfcone_script.txt" > "$TMPW/flowrows_bfcone_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcone_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCONE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcone_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCONE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCONE_$1: $3 (measured '$got')"; else echo "FAIL BFCONE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 99 node(s)" <<<"$FR_OUT"; then echo "ok   BFCONE_solve: the player solved all 99 nodes (every node type is registered)"; else echo "FAIL BFCONE_solve: expected 'solved 99 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 25 " <<<"$FR_OUT"; then echo "ok   BFCONE_objects: exactly 25 point(s) baked"; else echo "FAIL BFCONE_objects: expected 25 baked objects"; fail=1; fi
fr_chk 1 "9" "8-gon cone: apex plus ring,: 9 points"
fr_chk 2 "16" "8-gon cone: apex plus ring,: 16 edges"
fr_chk 3 "9" "8-gon cone: apex plus ring,: 9 faces"
fr_chk 4 "32" "8-gon cone: apex plus ring,: 32 corners"
fr_chk 5 "0" "cone volume = base area * height / 3"
fr_chk 6 "0" "base at z = -1.5, apex at +1.5: min X"
fr_chk 7 "0" "base at z = -1.5, apex at +1.5: min Y"
fr_chk 8 "0" "base at z = -1.5, apex at +1.5: min Z"
fr_chk 9 "0" "base at z = -1.5, apex at +1.5: max X"
fr_chk 10 "0" "base at z = -1.5, apex at +1.5: max Y"
fr_chk 11 "0" "base at z = -1.5, apex at +1.5: max Z"
fr_chk 12 "24" "frustum with 2 side segments:: 24 points"
fr_chk 13 "40" "frustum with 2 side segments:: 40 edges"
fr_chk 14 "18" "frustum with 2 side segments:: 18 faces"
fr_chk 15 "0" "volume = h (A1 + A2 + sqrt(A1 A2)) / 3 (radius is linear in height, so the two segments add up to one frustum)"
fr_chk 16 "9" "apex at the bottom:: 9 points"
fr_chk 17 "16" "apex at the bottom:: 16 edges"
fr_chk 18 "9" "apex at the bottom:: 9 faces"
fr_chk 19 "0" "inverted cone has the same volume"
fr_chk 20 "33" "the default cone has: 33 points"
fr_chk 21 "64" "the default cone has: 64 edges"
fr_chk 22 "33" "the default cone has: 33 faces"
fr_chk 23 "128" "the default cone has: 128 corners"
fr_chk 24 "0" "default volume"
fr_chk 25 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
