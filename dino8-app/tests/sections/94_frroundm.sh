# ---------------------------------------------------------------------------
# FRROUNDM_: Flow Register row D8-GH-MATHS-0091 (Round To Multiple), flowrows_gen.py. The graph flowrows_frroundm.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 13.7 to a multiple of 5: 13.7/5 = 2.74 -> 3 -> 15; -13.7 -> -15; 7 to a multiple of 2: 3.5 -> 4 (half away from zero) -> 8.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frroundm.dflow|g" "$HERE/flowrows_frroundm_script.txt" > "$TMPW/flowrows_frroundm_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frroundm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRROUNDM_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frroundm_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRROUNDM_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRROUNDM_$1: $3 (measured '$got')"; else echo "FAIL FRROUNDM_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 9 node(s)" <<<"$FR_OUT"; then echo "ok   FRROUNDM_solve: the player solved all 9 nodes (every node type is registered)"; else echo "FAIL FRROUNDM_solve: expected 'solved 9 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 3 " <<<"$FR_OUT"; then echo "ok   FRROUNDM_objects: exactly 3 point(s) baked"; else echo "FAIL FRROUNDM_objects: expected 3 baked objects"; fail=1; fi
fr_chk 1 "15" "13.7 to a multiple of 5 = 15"
fr_chk 2 "-15" "-13.7 to a multiple of 5 = -15"
fr_chk 3 "8" "7 to a multiple of 2 = 8 (half rounds away from zero)"
