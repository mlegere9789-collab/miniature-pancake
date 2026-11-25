# ---------------------------------------------------------------------------
# FRSPLIT_: Flow Register row D8-GH-SETS-0016 (Split List), flowrows_gen.py. The graph flowrows_frsplit.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 0..9 split at index 4: A = 0 1 2 3, B = 4 5 6 7 8 9; 0..2 split at 15 (past the end, clamped): A = 0 1 2, B has length 0.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frsplit.dflow|g" "$HERE/flowrows_frsplit_script.txt" > "$TMPW/flowrows_frsplit_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frsplit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRSPLIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frsplit_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRSPLIT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRSPLIT_$1: $3 (measured '$got')"; else echo "FAIL FRSPLIT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 13 node(s)" <<<"$FR_OUT"; then echo "ok   FRSPLIT_solve: the player solved all 13 nodes (every node type is registered)"; else echo "FAIL FRSPLIT_solve: expected 'solved 13 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   FRSPLIT_objects: exactly 14 point(s) baked"; else echo "FAIL FRSPLIT_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "0 1 2 3" "A of 0..9 split at 4 = 0 1 2 3"
fr_chk 2 "4 5 6 7 8 9" "B of 0..9 split at 4 = 4..9"
fr_chk 3 "0 1 2" "split past the end keeps everything in A"
fr_chk 4 "0" "split past the end leaves B with length 0"
