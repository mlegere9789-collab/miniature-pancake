# ---------------------------------------------------------------------------
# FRSQUARE_: Flow Register row D8-GH-MATHS-0020 (Square), flowrows_gen.py. The graph flowrows_frsquare.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 12^2 = 144; (-3)^2 = 9; 0.5^2 = 0.25.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frsquare.dflow|g" "$HERE/flowrows_frsquare_script.txt" > "$TMPW/flowrows_frsquare_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frsquare_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRSQUARE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frsquare_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRSQUARE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRSQUARE_$1: $3 (measured '$got')"; else echo "FAIL FRSQUARE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 9 node(s)" <<<"$FR_OUT"; then echo "ok   FRSQUARE_solve: the player solved all 9 nodes (every node type is registered)"; else echo "FAIL FRSQUARE_solve: expected 'solved 9 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 3 " <<<"$FR_OUT"; then echo "ok   FRSQUARE_objects: exactly 3 point(s) baked"; else echo "FAIL FRSQUARE_objects: expected 3 baked objects"; fail=1; fi
fr_chk 1 "144" "12^2 = 144"
fr_chk 2 "9" "(-3)^2 = 9"
fr_chk 3 "0.25" "0.5^2 = 0.25"
