# ---------------------------------------------------------------------------
# BFGRID_: Flow Register row geometry_nodes/Grid (Grid), flowrows_gen.py. The graph flowrows_bfgrid.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: nx x ny vertices: V = nx ny, F = (nx-1)(ny-1), E = (nx-1) ny + (ny-1) nx, corners 4F, area sx sy: 3 x 5 over 4 x 6 -> 15, 22, 8, 32, area 24, box +-2 x +-3 at z = 0; default 3 x 3 over 1 x 1 -> 9, 12, 4, area 1.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfgrid.dflow|g" "$HERE/flowrows_bfgrid_script.txt" > "$TMPW/flowrows_bfgrid_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfgrid_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFGRID_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfgrid_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFGRID_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFGRID_$1: $3 (measured '$got')"; else echo "FAIL BFGRID_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 79 node(s)" <<<"$FR_OUT"; then echo "ok   BFGRID_solve: the player solved all 79 nodes (every node type is registered)"; else echo "FAIL BFGRID_solve: expected 'solved 79 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 20 " <<<"$FR_OUT"; then echo "ok   BFGRID_objects: exactly 20 point(s) baked"; else echo "FAIL BFGRID_objects: expected 20 baked objects"; fail=1; fi
fr_chk 1 "15" "a 3 x 5 vertex grid has V = 15, E = (nx-1) ny + (ny-1) nx = 22, F = 8, 4F =: 15 points"
fr_chk 2 "22" "a 3 x 5 vertex grid has V = 15, E = (nx-1) ny + (ny-1) nx = 22, F = 8, 4F =: 22 edges"
fr_chk 3 "8" "a 3 x 5 vertex grid has V = 15, E = (nx-1) ny + (ny-1) nx = 22, F = 8, 4F =: 8 faces"
fr_chk 4 "32" "a 3 x 5 vertex grid has V = 15, E = (nx-1) ny + (ny-1) nx = 22, F = 8, 4F =: 32 corners"
fr_chk 5 "0" "4 x 6 centred on the origin, flat in Z: min X"
fr_chk 6 "0" "4 x 6 centred on the origin, flat in Z: min Y"
fr_chk 7 "0" "4 x 6 centred on the origin, flat in Z: min Z"
fr_chk 8 "0" "4 x 6 centred on the origin, flat in Z: max X"
fr_chk 9 "0" "4 x 6 centred on the origin, flat in Z: max Y"
fr_chk 10 "0" "4 x 6 centred on the origin, flat in Z: max Z"
fr_chk 11 "0" "area 4 * 6 = 24"
fr_chk 12 "9" "the default grid (3 x 3 vertices) has: 9 points"
fr_chk 13 "12" "the default grid (3 x 3 vertices) has: 12 edges"
fr_chk 14 "4" "the default grid (3 x 3 vertices) has: 4 faces"
fr_chk 15 "0" "the default size is 1 x 1: area 1"
fr_chk 16 "4" "a 2 x 2 vertex grid is one quad:: 4 points"
fr_chk 17 "4" "a 2 x 2 vertex grid is one quad:: 4 edges"
fr_chk 18 "1" "a 2 x 2 vertex grid is one quad:: 1 faces"
fr_chk 19 "4" "a 2 x 2 vertex grid is one quad:: 4 corners"
fr_chk 20 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
