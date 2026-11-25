# ---------------------------------------------------------------------------
# BFCUBE_: Flow Register row geometry_nodes/Cube (Cube), flowrows_gen.py. The graph flowrows_bfcube.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: surface lattice of nx x ny x nz points: V = nx ny nz - (nx-2)(ny-2)(nz-2) (8 for 2,2,2; 54 for 3,4,5), F = 2((nx-1)(ny-1) + (ny-1)(nz-1) + (nx-1)(nz-1)) (6; 52), E = V + F - 2 by Euler's V - E + F = 2 for a closed surface, corners 4F; volume sx sy sz = 24, area 2 (sx sy + sy sz + sx sz) = 52; the default cube has volume 1. Single precision vertices: volume and area compared within 5e-6.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfcube.dflow|g" "$HERE/flowrows_bfcube_script.txt" > "$TMPW/flowrows_bfcube_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcube_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCUBE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcube_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCUBE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCUBE_$1: $3 (measured '$got')"; else echo "FAIL BFCUBE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 90 node(s)" <<<"$FR_OUT"; then echo "ok   BFCUBE_solve: the player solved all 90 nodes (every node type is registered)"; else echo "FAIL BFCUBE_solve: expected 'solved 90 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 21 " <<<"$FR_OUT"; then echo "ok   BFCUBE_objects: exactly 21 point(s) baked"; else echo "FAIL BFCUBE_objects: expected 21 baked objects"; fail=1; fi
fr_chk 1 "8" "the 2 x 2 x 2 lattice cube has: 8 points"
fr_chk 2 "12" "the 2 x 2 x 2 lattice cube has: 12 edges"
fr_chk 3 "6" "the 2 x 2 x 2 lattice cube has: 6 faces"
fr_chk 4 "24" "the 2 x 2 x 2 lattice cube has: 24 corners"
fr_chk 5 "0" "Size (2,3,4) centred on the origin: min X"
fr_chk 6 "0" "Size (2,3,4) centred on the origin: min Y"
fr_chk 7 "0" "Size (2,3,4) centred on the origin: min Z"
fr_chk 8 "0" "Size (2,3,4) centred on the origin: max X"
fr_chk 9 "0" "Size (2,3,4) centred on the origin: max Y"
fr_chk 10 "0" "Size (2,3,4) centred on the origin: max Z"
fr_chk 11 "0" "volume 2 * 3 * 4 = 24 (outward faces, closed)"
fr_chk 12 "0" "area 2 (2*3 + 3*4 + 2*4) = 52"
fr_chk 13 "54" "3 x 4 x 5 lattice points (surface only):: 54 points"
fr_chk 14 "104" "3 x 4 x 5 lattice points (surface only):: 104 edges"
fr_chk 15 "52" "3 x 4 x 5 lattice points (surface only):: 52 faces"
fr_chk 16 "208" "3 x 4 x 5 lattice points (surface only):: 208 corners"
fr_chk 17 "0" "the subdivided cube has the same volume 24"
fr_chk 18 "8" "the default cube has: 8 points"
fr_chk 19 "6" "the default cube has: 6 faces"
fr_chk 20 "0" "the default cube has volume 1"
fr_chk 21 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
