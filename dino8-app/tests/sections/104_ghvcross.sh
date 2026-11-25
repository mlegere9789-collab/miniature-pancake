# ---------------------------------------------------------------------------
# GHVCROSS_: Flow Register row D8-GH-VECTOR-0008 (Cross Product), flowrows6_gen.py. The graph flowrows_ghvcross.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: determinant expansion: i x j = k; j x i = -k; (1,2,3) x (4,5,6) = (2*6-3*5, 3*4-1*6, 1*5-2*4) = (-3,6,-3), its length sqrt 54 (|a|^2|b|^2 - (a.b)^2 = 14*77 - 32^2 = 54), perpendicular to both ((a x b).a = -3+12-9 = 0, (a x b).b = -12+30-18 = 0); parallel vectors give 0; (3,0,0) x (0,4,0) = (0,0,12), unitized (0,0,1) with the length output 12.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvcross.dflow|g" "$HERE/flowrows_ghvcross_script.txt" > "$TMPW/flowrows_ghvcross_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvcross_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVCROSS_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvcross_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVCROSS_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVCROSS_$1: $3 (measured '$got')"; else echo "FAIL GHVCROSS_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVCROSS_$2"; else echo "FAIL GHVCROSS_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVCROSS_$3 (measured $n)"; else echo "FAIL GHVCROSS_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 220 node(s)" <<<"$FR_OUT"; then echo "ok   GHVCROSS_solve: the player solved all 220 nodes (every node type is registered)"; else echo "FAIL GHVCROSS_solve: expected 'solved 220 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 41 " <<<"$FR_OUT"; then echo "ok   GHVCROSS_objects: exactly 41 point(s) baked"; else echo "FAIL GHVCROSS_objects: expected 41 baked objects"; fail=1; fi
fr_chk 1 "1" "i x j = k: a value is there"
fr_chk 2 "0" "i x j = k: X = 0"
fr_chk 3 "0" "i x j = k: Y = 0"
fr_chk 4 "0" "i x j = k: Z = 1"
fr_chk 5 "1" "j x i = -k (anti-commutative): a value is there"
fr_chk 6 "0" "j x i = -k (anti-commutative): X = 0"
fr_chk 7 "0" "j x i = -k (anti-commutative): Y = 0"
fr_chk 8 "0" "j x i = -k (anti-commutative): Z = -1"
fr_chk 9 "1" "(1,2,3) x (4,5,6): a value is there"
fr_chk 10 "0" "(1,2,3) x (4,5,6): X = -3"
fr_chk 11 "0" "(1,2,3) x (4,5,6): Y = 6"
fr_chk 12 "0" "(1,2,3) x (4,5,6): Z = -3"
fr_chk 13 "1" "(4,5,6) x (1,2,3) is the negative: a value is there"
fr_chk 14 "0" "(4,5,6) x (1,2,3) is the negative: X = 3"
fr_chk 15 "0" "(4,5,6) x (1,2,3) is the negative: Y = -6"
fr_chk 16 "0" "(4,5,6) x (1,2,3) is the negative: Z = 3"
fr_chk 17 "0" "length output of (1,2,3) x (4,5,6) = sqrt 54 (Lagrange identity)"
fr_chk 18 "1" "(a x b) . a = -3 + 12 - 9 = 0 (perpendicular to a): a value is there"
fr_chk 19 "0" "(a x b) . a = -3 + 12 - 9 = 0 (perpendicular to a)"
fr_chk 20 "1" "(a x b) . b = -12 + 30 - 18 = 0 (perpendicular to b): a value is there"
fr_chk 21 "0" "(a x b) . b = -12 + 30 - 18 = 0 (perpendicular to b)"
fr_chk 22 "1" "parallel vectors give the zero vector: a value is there"
fr_chk 23 "0" "parallel vectors give the zero vector: X = 0"
fr_chk 24 "0" "parallel vectors give the zero vector: Y = 0"
fr_chk 25 "0" "parallel vectors give the zero vector: Z = 0"
fr_chk 26 "1" "and the length output is 0: a value is there"
fr_chk 27 "0" "and the length output is 0"
fr_chk 28 "1" "unitized (3,0,0) x (0,4,0): a value is there"
fr_chk 29 "0" "unitized (3,0,0) x (0,4,0): X = 0"
fr_chk 30 "0" "unitized (3,0,0) x (0,4,0): Y = 0"
fr_chk 31 "0" "unitized (3,0,0) x (0,4,0): Z = 1"
fr_chk 32 "0" "length output stays 12 when unitizing"
fr_chk 33 "1" "(3,0,0) x (0,4,0) without unitize: a value is there"
fr_chk 34 "0" "(3,0,0) x (0,4,0) without unitize: X = 0"
fr_chk 35 "0" "(3,0,0) x (0,4,0) without unitize: Y = 0"
fr_chk 36 "0" "(3,0,0) x (0,4,0) without unitize: Z = 12"
fr_chk 37 "1" "unitizing a zero cross product leaves the zero vector: a value is there"
fr_chk 38 "0" "unitizing a zero cross product leaves the zero vector: X = 0"
fr_chk 39 "0" "unitizing a zero cross product leaves the zero vector: Y = 0"
fr_chk 40 "0" "unitizing a zero cross product leaves the zero vector: Z = 0"
fr_chk 41 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Cross Product (#193) warns: Parallel vectors: the cross product is zero and cannot be unitized" "Cross Product #193 warns: Parallel vectors: the cross product is zero and cannot be unitized"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
