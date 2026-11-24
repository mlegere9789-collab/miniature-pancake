# ---------------------------------------------------------------------------
# BFFLIP_: Flow Register row geometry_nodes/Flip Faces (Flip Faces), flowrows_gen.py. The graph flowrows_bfflip.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: reversing every face negates the signed volume of a closed mesh (+8 -> -8 for a cube of size 2, back to +8 when flipped twice) and the normal of a grid (+Z -> -Z) and leaves every count unchanged; Selection false does nothing.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfflip.dflow|g" "$HERE/flowrows_bfflip_script.txt" > "$TMPW/flowrows_bfflip_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfflip_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFLIP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfflip_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFLIP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFFLIP_$1: $3 (measured '$got')"; else echo "FAIL BFFLIP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 91 node(s)" <<<"$FR_OUT"; then echo "ok   BFFLIP_solve: the player solved all 91 nodes (every node type is registered)"; else echo "FAIL BFFLIP_solve: expected 'solved 91 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 18 " <<<"$FR_OUT"; then echo "ok   BFFLIP_objects: exactly 18 point(s) baked"; else echo "FAIL BFFLIP_objects: expected 18 baked objects"; fail=1; fi
fr_chk 1 "0" "an unflipped cube has volume +8"
fr_chk 2 "0" "flipped: signed volume -8"
fr_chk 3 "8" "same elements:: 8 points"
fr_chk 4 "12" "same elements:: 12 edges"
fr_chk 5 "6" "same elements:: 6 faces"
fr_chk 6 "24" "same elements:: 24 corners"
fr_chk 7 "0" "Selection false changes nothing"
fr_chk 8 "0" "flipping twice restores +8"
fr_chk 9 "0" "a grid faces +Z X"
fr_chk 10 "0" "a grid faces +Z Y"
fr_chk 11 "0" "a grid faces +Z Z"
fr_chk 12 "0" "the flipped grid faces -Z X"
fr_chk 13 "0" "the flipped grid faces -Z Y"
fr_chk 14 "0" "the flipped grid faces -Z Z"
fr_chk 15 "9" "grid elements unchanged:: 9 points"
fr_chk 16 "12" "grid elements unchanged:: 12 edges"
fr_chk 17 "4" "grid elements unchanged:: 4 faces"
fr_chk 18 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
