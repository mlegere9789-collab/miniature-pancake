# ---------------------------------------------------------------------------
# FRTXTDIST_: Flow Register row D8-GH-SETS-0049 (Text Distance), flowrows_gen.py. The graph flowrows_frtxtdist.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: Levenshtein: kitten -> sitting = 3 (k->s, e->i, +g); '' -> abc = 3; flaw -> lawn = 2 (-f, +n); Kitten/kitten = 1 case sensitive, 0 ignoring case; same/same = 0.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_frtxtdist.dflow|g" "$HERE/flowrows_frtxtdist_script.txt" > "$TMPW/flowrows_frtxtdist_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_frtxtdist_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRTXTDIST_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_frtxtdist_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL FRTXTDIST_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   FRTXTDIST_$1: $3 (measured '$got')"; else echo "FAIL FRTXTDIST_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 18 node(s)" <<<"$FR_OUT"; then echo "ok   FRTXTDIST_solve: the player solved all 18 nodes (every node type is registered)"; else echo "FAIL FRTXTDIST_solve: expected 'solved 18 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 6 " <<<"$FR_OUT"; then echo "ok   FRTXTDIST_objects: exactly 6 point(s) baked"; else echo "FAIL FRTXTDIST_objects: expected 6 baked objects"; fail=1; fi
fr_chk 1 "3" "kitten -> sitting = 3"
fr_chk 2 "3" "'' -> abc = 3"
fr_chk 3 "2" "flaw -> lawn = 2"
fr_chk 4 "1" "Kitten vs kitten = 1 (case sensitive)"
fr_chk 5 "0" "Kitten vs kitten = 0 ignoring case"
fr_chk 6 "0" "same vs same = 0"
