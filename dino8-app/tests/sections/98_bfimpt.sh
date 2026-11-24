# ---------------------------------------------------------------------------
# BFIMPT_: Flow Register row geometry_nodes/Import Text (Import Text), flowrows_gen.py. The graph flowrows_bfimpt.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the file 'alpha<newline>beta<newline>' (11 bytes) reads back as exactly that string (Text Distance 0; against 'alpha beta' the distance is 2); a tab-separated two-line file with a trailing newline reads back unchanged.
# ---------------------------------------------------------------------------
cat > "$TMPW/two.txt" <<'EOF_BF'
alpha
beta
EOF_BF
cat > "$TMPW/tabs.txt" <<'EOF_BF'
x	y
1	2
EOF_BF
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfimpt.dflow" > "$TMPW/flowrows_bfimpt.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfimpt.dflow|g" "$HERE/flowrows_bfimpt_script.txt" > "$TMPW/flowrows_bfimpt_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimpt_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfimpt_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFIMPT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFIMPT_$1: $3 (measured '$got')"; else echo "FAIL BFIMPT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 11 node(s)" <<<"$FR_OUT"; then echo "ok   BFIMPT_solve: the player solved all 11 nodes (every node type is registered)"; else echo "FAIL BFIMPT_solve: expected 'solved 11 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 3 " <<<"$FR_OUT"; then echo "ok   BFIMPT_objects: exactly 3 point(s) baked"; else echo "FAIL BFIMPT_objects: expected 3 baked objects"; fail=1; fi
fr_chk 1 "0" "a two-line file comes back byte for byte (with its final newline)"
fr_chk 2 "2" "control: against 'alpha beta' (a space for the first newline, the last one missing) the distance is 2"
fr_chk 3 "0" "tabs and the trailing newline are kept"
