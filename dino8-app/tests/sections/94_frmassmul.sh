# ---------------------------------------------------------------------------
# FRMASSMUL_: Flow Register row D8-GH-MATHS-0018 (Mass Multiplication), flowrows_gen.py. The graph flowrows_frmassmul.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: product 1..6 = 6! = 720; partial products of 1..5 = 1,2,6,24,120.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frmassmul.dflow|g" "$HERE/flowrows_frmassmul_script.txt" > "$TMPW/flowrows_frmassmul_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frmassmul_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRMASSMUL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frmassmul_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRMASSMUL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRMASSMUL_$1: $3 (measured '$got')"; else echo "FAIL FRMASSMUL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 8 node(s)" <<<"$FR_OUT"; then echo "ok   FRMASSMUL_solve: the player solved all 8 nodes (every node type is registered)"; else echo "FAIL FRMASSMUL_solve: expected 'solved 8 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 6 " <<<"$FR_OUT"; then echo "ok   FRMASSMUL_objects: exactly 6 point(s) baked"; else echo "FAIL FRMASSMUL_objects: expected 6 baked objects"; fail=1; fi
fr_chk 1 "720" "product of 1..6 = 720"
fr_chk 2 "1 2 6 24 120" "partial products of 1..5 = 1,2,6,24,120"
