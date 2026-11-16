# ---------------------------------------------------------------------------
# BFJSTR_: Flow Register row geometry_nodes/Join Strings (Join Strings), flowrows_gen.py. The graph flowrows_bfjstr.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: concatenation in order with the delimiter between neighbours only: alpha, beta, gamma with ', ' -> 'alpha, beta, gamma' (Text Distance to the expected string is 0 exactly when the strings are equal; the control string without the space is 1 edit away), empty delimiter -> 'alphabetagamma', one string -> itself, none -> ''.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfjstr.dflow|g" "$HERE/flowrows_bfjstr_script.txt" > "$TMPW/flowrows_bfjstr_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfjstr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFJSTR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfjstr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFJSTR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFJSTR_$1: $3 (measured '$got')"; else echo "FAIL BFJSTR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 27 node(s)" <<<"$FR_OUT"; then echo "ok   BFJSTR_solve: the player solved all 27 nodes (every node type is registered)"; else echo "FAIL BFJSTR_solve: expected 'solved 27 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 6 " <<<"$FR_OUT"; then echo "ok   BFJSTR_objects: exactly 6 point(s) baked"; else echo "FAIL BFJSTR_objects: expected 6 baked objects"; fail=1; fi
fr_chk 1 "0" "three strings with delimiter ', ' -> 'alpha, beta, gamma'"
fr_chk 2 "2" "control: against 'alpha,beta,gamma' (both spaces missing) the distance is 2, so the 0 above is a real match"
fr_chk 3 "0" "the default delimiter is empty: 'alphabetagamma'"
fr_chk 4 "0" "one string has no delimiter"
fr_chk 5 "0" "no strings give the empty string"
fr_chk 6 "0" "delimiter '--' between 'a' and 'b'"
