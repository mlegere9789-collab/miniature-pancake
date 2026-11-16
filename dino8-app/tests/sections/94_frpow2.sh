# ---------------------------------------------------------------------------
# FRPOW2_: Flow Register row D8-GH-MATHS-0049 (Power Of Two), flowrows_gen.py. The graph flowrows_frpow2.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 2^10 = 1024; 2^0 = 1; 2^-2 = 1/4 = 0.25.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frpow2.dflow|g" "$HERE/flowrows_frpow2_script.txt" > "$TMPW/flowrows_frpow2_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frpow2_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRPOW2_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frpow2_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRPOW2_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRPOW2_$1: $3 (measured '$got')"; else echo "FAIL FRPOW2_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 9 node(s)" <<<"$FR_OUT"; then echo "ok   FRPOW2_solve: the player solved all 9 nodes (every node type is registered)"; else echo "FAIL FRPOW2_solve: expected 'solved 9 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 3 " <<<"$FR_OUT"; then echo "ok   FRPOW2_objects: exactly 3 point(s) baked"; else echo "FAIL FRPOW2_objects: expected 3 baked objects"; fail=1; fi
fr_chk 1 "1024" "2^10 = 1024"
fr_chk 2 "1" "2^0 = 1"
fr_chk 3 "0.25" "2^-2 = 0.25"
