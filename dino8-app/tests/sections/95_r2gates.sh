# ---------------------------------------------------------------------------
# R2GATES_: Flow Register row D8-GH-MATHS-0028 (Gate And, Or, Not, Xor, Nand, Nor, Xnor), flowrows_gen.py. The graph flowrows_r2gates.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: truth tables over (A,B) = FF, FT, TF, TT: And 0001, Or 0111, Xor 0110, Nand 1110, Nor 1000, Xnor 1001; Not F = T, Not T = F.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2gates.dflow|g" "$HERE/flowrows_r2gates_script.txt" > "$TMPW/flowrows_r2gates_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2gates_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2GATES_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2gates_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2GATES_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2GATES_$1: $3 (measured '$got')"; else echo "FAIL R2GATES_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 78 node(s)" <<<"$FR_OUT"; then echo "ok   R2GATES_solve: the player solved all 78 nodes (every node type is registered)"; else echo "FAIL R2GATES_solve: expected 'solved 78 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 26 " <<<"$FR_OUT"; then echo "ok   R2GATES_objects: exactly 26 point(s) baked"; else echo "FAIL R2GATES_objects: expected 26 baked objects"; fail=1; fi
fr_chk 1 "0" "And(F,F) = 0"
fr_chk 2 "0" "And(F,T) = 0"
fr_chk 3 "0" "And(T,F) = 0"
fr_chk 4 "1" "And(T,T) = 1"
fr_chk 5 "0" "Or(F,F) = 0"
fr_chk 6 "1" "Or(F,T) = 1"
fr_chk 7 "1" "Or(T,F) = 1"
fr_chk 8 "1" "Or(T,T) = 1"
fr_chk 9 "0" "Xor(F,F) = 0"
fr_chk 10 "1" "Xor(F,T) = 1"
fr_chk 11 "1" "Xor(T,F) = 1"
fr_chk 12 "0" "Xor(T,T) = 0"
fr_chk 13 "1" "Nand(F,F) = 1"
fr_chk 14 "1" "Nand(F,T) = 1"
fr_chk 15 "1" "Nand(T,F) = 1"
fr_chk 16 "0" "Nand(T,T) = 0"
fr_chk 17 "1" "Nor(F,F) = 1"
fr_chk 18 "0" "Nor(F,T) = 0"
fr_chk 19 "0" "Nor(T,F) = 0"
fr_chk 20 "0" "Nor(T,T) = 0"
fr_chk 21 "1" "Xnor(F,F) = 1"
fr_chk 22 "0" "Xnor(F,T) = 0"
fr_chk 23 "0" "Xnor(T,F) = 0"
fr_chk 24 "1" "Xnor(T,T) = 1"
fr_chk 25 "1" "Not(F) = 1"
fr_chk 26 "0" "Not(T) = 0"
