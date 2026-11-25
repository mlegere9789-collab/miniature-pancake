# ---------------------------------------------------------------------------
# R2DISPATCH_: Flow Register row D8-GH-SETS-0011 (Dispatch), flowrows_gen.py. The graph flowrows_r2dispatch.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 0..5 with the repeating pattern F,T: the items at pattern-true positions 1,3,5 go to A and those at false positions 0,2,4 go to B; the one-entry pattern T sends all six items to A and none to B; the one-entry pattern F sends none to A and all six to B.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2dispatch.dflow|g" "$HERE/flowrows_r2dispatch_script.txt" > "$TMPW/flowrows_r2dispatch_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2dispatch_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2DISPATCH_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2dispatch_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2DISPATCH_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2DISPATCH_$1: $3 (measured '$got')"; else echo "FAIL R2DISPATCH_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 21 node(s)" <<<"$FR_OUT"; then echo "ok   R2DISPATCH_solve: the player solved all 21 nodes (every node type is registered)"; else echo "FAIL R2DISPATCH_solve: expected 'solved 21 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 20 " <<<"$FR_OUT"; then echo "ok   R2DISPATCH_objects: exactly 20 point(s) baked"; else echo "FAIL R2DISPATCH_objects: expected 20 baked objects"; fail=1; fi
fr_chk 1 "1 3 5" "A holds the items at true positions 1 3 5"
fr_chk 2 "0 2 4" "B holds the items at false positions 0 2 4"
fr_chk 3 "0 1 2 3 4 5" "pattern T sends everything to A"
fr_chk 4 "0" "pattern T leaves B empty"
fr_chk 5 "0" "pattern F leaves A empty"
fr_chk 6 "0 1 2 3 4 5" "pattern F sends everything to B"
