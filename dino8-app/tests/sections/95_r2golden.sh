# ---------------------------------------------------------------------------
# R2GOLDEN_: Flow Register row D8-GH-MATHS-0039 (Golden Ratio), flowrows_gen.py. The graph flowrows_r2golden.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: phi = (1 + sqrt 5) / 2 = 1.61803398874989; factor 2 -> 1 + sqrt 5; factor -1 -> -phi; the defining property phi^2 = phi + 1 (so (phi^2 - phi) / 1 = 1).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2golden.dflow|g" "$HERE/flowrows_r2golden_script.txt" > "$TMPW/flowrows_r2golden_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2golden_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2GOLDEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2golden_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2GOLDEN_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2GOLDEN_$1: $3 (measured '$got')"; else echo "FAIL R2GOLDEN_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 27 node(s)" <<<"$FR_OUT"; then echo "ok   R2GOLDEN_solve: the player solved all 27 nodes (every node type is registered)"; else echo "FAIL R2GOLDEN_solve: expected 'solved 27 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 5 " <<<"$FR_OUT"; then echo "ok   R2GOLDEN_objects: exactly 5 point(s) baked"; else echo "FAIL R2GOLDEN_objects: expected 5 baked objects"; fail=1; fi
fr_chk 1 "0" "phi = (1 + sqrt 5) / 2"
fr_chk 2 "0" "2 phi = 1 + sqrt 5"
fr_chk 3 "0" "-phi"
fr_chk 4 "0" "phi^2 - phi = 1"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
