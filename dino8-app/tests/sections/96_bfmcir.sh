# ---------------------------------------------------------------------------
# BFMCIR_: Flow Register row geometry_nodes/Mesh Circle (Mesh Circle), flowrows_gen.py. The graph flowrows_bfmcir.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: n vertices on radius r: fill none = n edges, no face; n-gon = n edges, 1 face with n corners; triangle fan = n + 1 vertices, 2 n edges (n rim + n spokes), n triangles, 3 n corners; polygon area (n/2) r^2 sin(2 pi/n) = 4 sqrt 2 for n = 8, r = 2.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfmcir.dflow|g" "$HERE/flowrows_bfmcir_script.txt" > "$TMPW/flowrows_bfmcir_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmcir_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMCIR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfmcir_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFMCIR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFMCIR_$1: $3 (measured '$got')"; else echo "FAIL BFMCIR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 89 node(s)" <<<"$FR_OUT"; then echo "ok   BFMCIR_solve: the player solved all 89 nodes (every node type is registered)"; else echo "FAIL BFMCIR_solve: expected 'solved 89 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 24 " <<<"$FR_OUT"; then echo "ok   BFMCIR_objects: exactly 24 point(s) baked"; else echo "FAIL BFMCIR_objects: expected 24 baked objects"; fail=1; fi
fr_chk 1 "8" "unfilled 8-gon:: 8 points"
fr_chk 2 "8" "unfilled 8-gon:: 8 edges"
fr_chk 3 "0" "unfilled 8-gon:: 0 faces"
fr_chk 4 "0" "unfilled 8-gon:: 0 corners"
fr_chk 5 "8" "n-gon fill (one face):: 8 points"
fr_chk 6 "8" "n-gon fill (one face):: 8 edges"
fr_chk 7 "1" "n-gon fill (one face):: 1 faces"
fr_chk 8 "8" "n-gon fill (one face):: 8 corners"
fr_chk 9 "0" "n-gon area (n/2) r^2 sin(2 pi/n) = 4 sqrt 2 * ... for r = 2"
fr_chk 10 "9" "triangle-fan fill:: 9 points"
fr_chk 11 "16" "triangle-fan fill:: 16 edges"
fr_chk 12 "8" "triangle-fan fill:: 8 faces"
fr_chk 13 "24" "triangle-fan fill:: 24 corners"
fr_chk 14 "0" "fan area is the same polygon area"
fr_chk 15 "0" "radius 2 in the XY plane: min X"
fr_chk 16 "0" "radius 2 in the XY plane: min Y"
fr_chk 17 "0" "radius 2 in the XY plane: min Z"
fr_chk 18 "0" "radius 2 in the XY plane: max X"
fr_chk 19 "0" "radius 2 in the XY plane: max Y"
fr_chk 20 "0" "radius 2 in the XY plane: max Z"
fr_chk 21 "32" "the default (32 vertices, no fill) has: 32 points"
fr_chk 22 "32" "the default (32 vertices, no fill) has: 32 edges"
fr_chk 23 "0" "the default (32 vertices, no fill) has: 0 faces"
fr_chk 24 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
