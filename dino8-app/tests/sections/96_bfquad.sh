# ---------------------------------------------------------------------------
# BFQUAD_: Flow Register row geometry_nodes/Quadrilateral (Quadrilateral), flowrows_gen.py. The graph flowrows_bfquad.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: a w x h rectangle centred on the origin: perimeter 2 (w + h) = 20 for 6 x 4, corners at (+-w/2, +-h/2); four points.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfquad.dflow|g" "$HERE/flowrows_bfquad_script.txt" > "$TMPW/flowrows_bfquad_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfquad_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFQUAD_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfquad_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFQUAD_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFQUAD_$1: $3 (measured '$got')"; else echo "FAIL BFQUAD_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 91 node(s)" <<<"$FR_OUT"; then echo "ok   BFQUAD_solve: the player solved all 91 nodes (every node type is registered)"; else echo "FAIL BFQUAD_solve: expected 'solved 91 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 17 " <<<"$FR_OUT"; then echo "ok   BFQUAD_objects: exactly 17 point(s) baked"; else echo "FAIL BFQUAD_objects: expected 17 baked objects"; fail=1; fi
fr_chk 1 "4" "a quadrilateral has: 4 points"
fr_chk 2 "1" "a quadrilateral has: 1 splines"
fr_chk 3 "0" "6 x 4 rectangle: perimeter 2 (6 + 4) = 20"
fr_chk 4 "0" "centred on the origin: x +-3, y +-2: min X"
fr_chk 5 "0" "centred on the origin: x +-3, y +-2: min Y"
fr_chk 6 "0" "centred on the origin: x +-3, y +-2: min Z"
fr_chk 7 "0" "centred on the origin: x +-3, y +-2: max X"
fr_chk 8 "0" "centred on the origin: x +-3, y +-2: max Y"
fr_chk 9 "0" "centred on the origin: x +-3, y +-2: max Z"
fr_chk 10 "0" "the default 2 x 2 square has perimeter 8"
fr_chk 11 "0" "the default square spans +-1: min X"
fr_chk 12 "0" "the default square spans +-1: min Y"
fr_chk 13 "0" "the default square spans +-1: min Z"
fr_chk 14 "0" "the default square spans +-1: max X"
fr_chk 15 "0" "the default square spans +-1: max Y"
fr_chk 16 "0" "the default square spans +-1: max Z"
fr_chk 17 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
