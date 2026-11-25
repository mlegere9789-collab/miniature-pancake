# ---------------------------------------------------------------------------
# FRGCDLCM_: Flow Register row D8-GH-MATHS-0098 (Greatest Common Divisor, Least Common Multiple), flowrows_gen.py. The graph flowrows_frgcdlcm.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 84 = 2^2*3*7, 36 = 2^2*3^2: gcd = 2^2*3 = 12, lcm = 2^2*3^2*7 = 252; gcd(0,5) = 5; lcm(4,6) = 12; gcd(-12,18) = 6.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frgcdlcm.dflow|g" "$HERE/flowrows_frgcdlcm_script.txt" > "$TMPW/flowrows_frgcdlcm_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frgcdlcm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRGCDLCM_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frgcdlcm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRGCDLCM_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRGCDLCM_$1: $3 (measured '$got')"; else echo "FAIL FRGCDLCM_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 15 node(s)" <<<"$FR_OUT"; then echo "ok   FRGCDLCM_solve: the player solved all 15 nodes (every node type is registered)"; else echo "FAIL FRGCDLCM_solve: expected 'solved 15 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 5 " <<<"$FR_OUT"; then echo "ok   FRGCDLCM_objects: exactly 5 point(s) baked"; else echo "FAIL FRGCDLCM_objects: expected 5 baked objects"; fail=1; fi
fr_chk 1 "12" "gcd(84,36) = 12"
fr_chk 2 "252" "lcm(84,36) = 252"
fr_chk 3 "5" "gcd(0,5) = 5"
fr_chk 4 "12" "lcm(4,6) = 12"
fr_chk 5 "6" "gcd(-12,18) = 6"
