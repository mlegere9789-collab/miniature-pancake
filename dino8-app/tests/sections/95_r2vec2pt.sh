# ---------------------------------------------------------------------------
# R2VEC2PT_: Flow Register row D8-GH-VECTOR-0002 (Vector 2Pt), flowrows_gen.py. The graph flowrows_r2vec2pt.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: tip - base: (1,2,3) -> (4,6,3) is (3,4,0) of length 5; unitized it is (3,4,0)/5 = (0.6,0.8,0) and the reported length stays 5; equal points give the zero vector of length 0; swapping the points negates it: (-3,-4,0).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2vec2pt.dflow|g" "$HERE/flowrows_r2vec2pt_script.txt" > "$TMPW/flowrows_r2vec2pt_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2vec2pt_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2VEC2PT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2vec2pt_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2VEC2PT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2VEC2PT_$1: $3 (measured '$got')"; else echo "FAIL R2VEC2PT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 51 node(s)" <<<"$FR_OUT"; then echo "ok   R2VEC2PT_solve: the player solved all 51 nodes (every node type is registered)"; else echo "FAIL R2VEC2PT_solve: expected 'solved 51 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 16 " <<<"$FR_OUT"; then echo "ok   R2VEC2PT_objects: exactly 16 point(s) baked"; else echo "FAIL R2VEC2PT_objects: expected 16 baked objects"; fail=1; fi
fr_chk 1 "3" "X of (4,6,3)-(1,2,3) is 3"
fr_chk 2 "4" "Y is 4"
fr_chk 3 "0" "Z is 0"
fr_chk 4 "5" "length is 5"
fr_chk 5 "0" "unitized X is 0.6"
fr_chk 6 "0" "unitized Y is 0.8"
fr_chk 7 "0" "unitized Z is 0"
fr_chk 8 "5" "length stays 5 when unitizing"
fr_chk 9 "0" "equal points give X 0"
fr_chk 10 "0" "Y 0"
fr_chk 11 "0" "Z 0"
fr_chk 12 "0" "length 0"
fr_chk 13 "-3" "reversed X is -3"
fr_chk 14 "-4" "reversed Y is -4"
fr_chk 15 "0" "reversed Z is 0"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
