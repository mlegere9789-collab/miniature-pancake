# ---------------------------------------------------------------------------
# BFSWIT_: Flow Register row geometry_nodes/Switch (Switch), flowrows_gen.py. The graph flowrows_bfswit.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Switch false passes the False input, true the True input, for numbers (10 / 20), text ('no' / 'yes') and points ((1,2,3) / (4,5,6)); default false.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfswit.dflow|g" "$HERE/flowrows_bfswit_script.txt" > "$TMPW/flowrows_bfswit_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfswit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSWIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfswit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSWIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSWIT_$1: $3 (measured '$got')"; else echo "FAIL BFSWIT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 68 node(s)" <<<"$FR_OUT"; then echo "ok   BFSWIT_solve: the player solved all 68 nodes (every node type is registered)"; else echo "FAIL BFSWIT_solve: expected 'solved 68 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 12 " <<<"$FR_OUT"; then echo "ok   BFSWIT_objects: exactly 12 point(s) baked"; else echo "FAIL BFSWIT_objects: expected 12 baked objects"; fail=1; fi
fr_chk 1 "0" "false selects the False input 10"
fr_chk 2 "0" "true selects the True input 20"
fr_chk 3 "0" "text: true selects 'yes'"
fr_chk 4 "0" "text: false selects 'no'"
fr_chk 5 "0" "point: true selects (4,5,6) X"
fr_chk 6 "0" "point: true selects (4,5,6) Y"
fr_chk 7 "0" "point: true selects (4,5,6) Z"
fr_chk 8 "0" "point: false selects (1,2,3) X"
fr_chk 9 "0" "point: false selects (1,2,3) Y"
fr_chk 10 "0" "point: false selects (1,2,3) Z"
fr_chk 11 "0" "the default Switch is false"
fr_chk 12 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
