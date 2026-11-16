# ---------------------------------------------------------------------------
# FRCULLNTH_: Flow Register row D8-GH-SETS-0010 (Cull Nth), flowrows_gen.py. The graph flowrows_frcullnth.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 0..9 with N=3: the 3rd, 6th and 9th items (values 2, 5, 8) go, leaving 0 1 3 4 6 7 9; 0..4 with N=2 removes the 2nd and 4th (1, 3), leaving 0 2 4.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frcullnth.dflow|g" "$HERE/flowrows_frcullnth_script.txt" > "$TMPW/flowrows_frcullnth_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frcullnth_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRCULLNTH_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frcullnth_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRCULLNTH_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRCULLNTH_$1: $3 (measured '$got')"; else echo "FAIL FRCULLNTH_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 8 node(s)" <<<"$FR_OUT"; then echo "ok   FRCULLNTH_solve: the player solved all 8 nodes (every node type is registered)"; else echo "FAIL FRCULLNTH_solve: expected 'solved 8 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   FRCULLNTH_objects: exactly 10 point(s) baked"; else echo "FAIL FRCULLNTH_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0 1 3 4 6 7 9" "Cull Nth N=3 of 0..9 removes 2,5,8"
fr_chk 2 "0 2 4" "Cull Nth N=2 of 0..4 removes 1,3"
