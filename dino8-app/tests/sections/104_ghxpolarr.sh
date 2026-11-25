# ---------------------------------------------------------------------------
# GHXPOLARR_: Flow Register row D8-GH-XFORM-0017 (Polar Array), flowrows6_gen.py. The graph flowrows_ghxpolarr.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the default Angle is a full circle of 2 pi RADIANS: N copies in equal steps of 2 pi / N, copy i at angle 2 pi i / N (counter-clockwise about the plane normal): (2,0,0) in 4 steps -> (2,0,0), (0,2,0), (-2,0,0), (0,-2,0); (1,0,0) in 3 steps with Angle 2 pi -> (cos 120 deg, sin 120 deg) = (-1/2, sqrt 3 / 2) and (-1/2, -sqrt 3 / 2); about (1,1,0) with 2 copies (2,1,0) -> (0,1,0); about the X axis (0,1,0) -> (0,0,1) -> (0,-1,0) -> (0,0,-1); 6 steps of (1,0,0): neighbours are 1 apart (regular hexagon, side = circumradius).
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghxpolarr.dflow|g" "$HERE/flowrows_ghxpolarr_script.txt" > "$TMPW/flowrows_ghxpolarr_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxpolarr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXPOLARR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxpolarr_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXPOLARR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHXPOLARR_$1: $3 (measured '$got')"; else echo "FAIL GHXPOLARR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHXPOLARR_$2"; else echo "FAIL GHXPOLARR_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHXPOLARR_$3 (measured $n)"; else echo "FAIL GHXPOLARR_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 314 node(s)" <<<"$FR_OUT"; then echo "ok   GHXPOLARR_solve: the player solved all 314 nodes (every node type is registered)"; else echo "FAIL GHXPOLARR_solve: expected 'solved 314 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 56 " <<<"$FR_OUT"; then echo "ok   GHXPOLARR_objects: exactly 56 point(s) baked"; else echo "FAIL GHXPOLARR_objects: expected 56 baked objects"; fail=1; fi
fr_chk 1 "4" "count 4 gives 4 copies"
fr_chk 2 "1" "copy 0 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): a value is there"
fr_chk 3 "0" "copy 0 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): X = 2"
fr_chk 4 "0" "copy 0 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Y = 0"
fr_chk 5 "0" "copy 0 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Z = 0"
fr_chk 6 "1" "copy 1 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): a value is there"
fr_chk 7 "0" "copy 1 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): X = 0"
fr_chk 8 "0" "copy 1 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Y = 2"
fr_chk 9 "0" "copy 1 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Z = 0"
fr_chk 10 "1" "copy 2 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): a value is there"
fr_chk 11 "0" "copy 2 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): X = -2"
fr_chk 12 "0" "copy 2 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Y = 0"
fr_chk 13 "0" "copy 2 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Z = 0"
fr_chk 14 "1" "copy 3 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): a value is there"
fr_chk 15 "0" "copy 3 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): X = 0"
fr_chk 16 "0" "copy 3 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Y = -2"
fr_chk 17 "0" "copy 3 of (2,0,0) in 4 steps of pi/2 (the default Angle is a full circle): Z = 0"
fr_chk 18 "1" "copy 0 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): a value is there"
fr_chk 19 "0" "copy 0 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): X = 1"
fr_chk 20 "0" "copy 0 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): Y = 0"
fr_chk 21 "0" "copy 0 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): Z = 0"
fr_chk 22 "1" "copy 1 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): a value is there"
fr_chk 23 "0" "copy 1 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): X = -0.5"
fr_chk 24 "0" "copy 1 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): Y = 0.8660254038"
fr_chk 25 "0" "copy 1 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): Z = 0"
fr_chk 26 "1" "copy 2 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): a value is there"
fr_chk 27 "0" "copy 2 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): X = -0.5"
fr_chk 28 "0" "copy 2 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): Y = -0.8660254038"
fr_chk 29 "0" "copy 2 of (1,0,0) in 3 steps of 2 pi/3 with Angle 2 pi: (cos, sin): Z = 0"
fr_chk 30 "1" "about (1,1,0) with 2 copies the second is the point turned by pi: (0,1,0): a value is there"
fr_chk 31 "0" "about (1,1,0) with 2 copies the second is the point turned by pi: (0,1,0): X = 0"
fr_chk 32 "0" "about (1,1,0) with 2 copies the second is the point turned by pi: (0,1,0): Y = 1"
fr_chk 33 "0" "about (1,1,0) with 2 copies the second is the point turned by pi: (0,1,0): Z = 0"
fr_chk 34 "1" "copy 0 about the X axis: a value is there"
fr_chk 35 "0" "copy 0 about the X axis: X = 0"
fr_chk 36 "0" "copy 0 about the X axis: Y = 1"
fr_chk 37 "0" "copy 0 about the X axis: Z = 0"
fr_chk 38 "1" "copy 1 about the X axis: a value is there"
fr_chk 39 "0" "copy 1 about the X axis: X = 0"
fr_chk 40 "0" "copy 1 about the X axis: Y = 0"
fr_chk 41 "0" "copy 1 about the X axis: Z = 1"
fr_chk 42 "1" "copy 2 about the X axis: a value is there"
fr_chk 43 "0" "copy 2 about the X axis: X = 0"
fr_chk 44 "0" "copy 2 about the X axis: Y = -1"
fr_chk 45 "0" "copy 2 about the X axis: Z = 0"
fr_chk 46 "1" "copy 3 about the X axis: a value is there"
fr_chk 47 "0" "copy 3 about the X axis: X = 0"
fr_chk 48 "0" "copy 3 about the X axis: Y = 0"
fr_chk 49 "0" "copy 3 about the X axis: Z = -1"
fr_chk 50 "0" "hexagon: copies 0 and 1 are 1 apart (side of the regular hexagon of circumradius 1)"
fr_chk 51 "0" "hexagon: copies 1 and 2 are 1 apart (side of the regular hexagon of circumradius 1)"
fr_chk 52 "0" "hexagon: copies 2 and 3 are 1 apart (side of the regular hexagon of circumradius 1)"
fr_chk 53 "0" "hexagon: copies 3 and 4 are 1 apart (side of the regular hexagon of circumradius 1)"
fr_chk 54 "0" "hexagon: copies 4 and 5 are 1 apart (side of the regular hexagon of circumradius 1)"
fr_chk 55 "0" "hexagon: copies 5 and 0 are 1 apart (side of the regular hexagon of circumradius 1)"
fr_chk 56 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
