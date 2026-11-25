# ---------------------------------------------------------------------------
# BFJOIN_: Flow Register row geometry_nodes/Join Geometry (Join Geometry), flowrows_gen.py. The graph flowrows_bfjoin.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: counts add and nothing is welded: two cubes 16/24/12/48, volume 1 + 1, box x -0.5..3.5; a default cylinder (64/96/34/192) plus a cube (8/12/6/24) = 72/108/40/216 (n-gons kept, so the cap faces are not split); three grids 22/28/9, area 3.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfjoin.dflow|g" "$HERE/flowrows_bfjoin_script.txt" > "$TMPW/flowrows_bfjoin_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfjoin_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFJOIN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfjoin_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFJOIN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFJOIN_$1: $3 (measured '$got')"; else echo "FAIL BFJOIN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 96 node(s)" <<<"$FR_OUT"; then echo "ok   BFJOIN_solve: the player solved all 96 nodes (every node type is registered)"; else echo "FAIL BFJOIN_solve: expected 'solved 96 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 20 " <<<"$FR_OUT"; then echo "ok   BFJOIN_objects: exactly 20 point(s) baked"; else echo "FAIL BFJOIN_objects: expected 20 baked objects"; fail=1; fi
fr_chk 1 "16" "two cubes:: 16 points"
fr_chk 2 "24" "two cubes:: 24 edges"
fr_chk 3 "12" "two cubes:: 12 faces"
fr_chk 4 "48" "two cubes:: 48 corners"
fr_chk 5 "0" "volumes add: 1 + 1"
fr_chk 6 "0" "box spans both cubes: min X"
fr_chk 7 "0" "box spans both cubes: min Y"
fr_chk 8 "0" "box spans both cubes: min Z"
fr_chk 9 "0" "box spans both cubes: max X"
fr_chk 10 "0" "box spans both cubes: max Y"
fr_chk 11 "0" "box spans both cubes: max Z"
fr_chk 12 "72" "a default cylinder (2 n-gon caps) and a cube: the n-gons stay one face each:: 72 points"
fr_chk 13 "108" "a default cylinder (2 n-gon caps) and a cube: the n-gons stay one face each:: 108 edges"
fr_chk 14 "40" "a default cylinder (2 n-gon caps) and a cube: the n-gons stay one face each:: 40 faces"
fr_chk 15 "216" "a default cylinder (2 n-gon caps) and a cube: the n-gons stay one face each:: 216 corners"
fr_chk 16 "22" "three grids:: 22 points"
fr_chk 17 "28" "three grids:: 28 edges"
fr_chk 18 "9" "three grids:: 9 faces"
fr_chk 19 "0" "areas add: 1 + 1 + 1"
fr_chk 20 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
