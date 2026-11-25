# ---------------------------------------------------------------------------
# GHXROT_: Flow Register row D8-GH-XFORM-0008 (Rotate), flowrows6_gen.py. The graph flowrows_ghxrot.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: angle in RADIANS, counter-clockwise about the plane normal through the plane origin: (1,0,0) by pi/2 = (0,1,0); by pi = (-1,0,0); (2,0,0) by pi/4 = (sqrt 2, sqrt 2, 0); by -pi/2 = (0,-1,0); 2 pi and 0 are the identity; a typed 1 is one radian = (cos 1, sin 1, 0) and a typed 90 is ninety radians = (cos 90, sin 90, 0); Radians(90 degrees) wired in gives (0,1,0); about origin (1,1,0): (1,0,0) -> (2,1,0); about the X axis (0,1,0) -> (0,0,1); (3,4,5) by 37 degrees = 37 pi / 180 radians stays 5 from the axis.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghxrot.dflow|g" "$HERE/flowrows_ghxrot_script.txt" > "$TMPW/flowrows_ghxrot_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxrot_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXROT_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxrot_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXROT_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHXROT_$1: $3 (measured '$got')"; else echo "FAIL GHXROT_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHXROT_$2"; else echo "FAIL GHXROT_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHXROT_$3 (measured $n)"; else echo "FAIL GHXROT_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 254 node(s)" <<<"$FR_OUT"; then echo "ok   GHXROT_solve: the player solved all 254 nodes (every node type is registered)"; else echo "FAIL GHXROT_solve: expected 'solved 254 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 46 " <<<"$FR_OUT"; then echo "ok   GHXROT_objects: exactly 46 point(s) baked"; else echo "FAIL GHXROT_objects: expected 46 baked objects"; fail=1; fi
fr_chk 1 "1" "(1,0,0) by pi/2 about Z: a value is there"
fr_chk 2 "0" "(1,0,0) by pi/2 about Z: X = 0"
fr_chk 3 "0" "(1,0,0) by pi/2 about Z: Y = 1"
fr_chk 4 "0" "(1,0,0) by pi/2 about Z: Z = 0"
fr_chk 5 "1" "(1,0,0) by pi: a value is there"
fr_chk 6 "0" "(1,0,0) by pi: X = -1"
fr_chk 7 "0" "(1,0,0) by pi: Y = 0"
fr_chk 8 "0" "(1,0,0) by pi: Z = 0"
fr_chk 9 "1" "(2,0,0) by pi/4: 2 (cos 45 deg, sin 45 deg) = (sqrt 2, sqrt 2): a value is there"
fr_chk 10 "0" "(2,0,0) by pi/4: 2 (cos 45 deg, sin 45 deg) = (sqrt 2, sqrt 2): X = 1.414213562"
fr_chk 11 "0" "(2,0,0) by pi/4: 2 (cos 45 deg, sin 45 deg) = (sqrt 2, sqrt 2): Y = 1.414213562"
fr_chk 12 "0" "(2,0,0) by pi/4: 2 (cos 45 deg, sin 45 deg) = (sqrt 2, sqrt 2): Z = 0"
fr_chk 13 "1" "(1,0,0) by -pi/2 (clockwise): a value is there"
fr_chk 14 "0" "(1,0,0) by -pi/2 (clockwise): X = 0"
fr_chk 15 "0" "(1,0,0) by -pi/2 (clockwise): Y = -1"
fr_chk 16 "0" "(1,0,0) by -pi/2 (clockwise): Z = 0"
fr_chk 17 "1" "a full turn of 2 pi is the identity: a value is there"
fr_chk 18 "0" "a full turn of 2 pi is the identity: X = 1"
fr_chk 19 "0" "a full turn of 2 pi is the identity: Y = 2"
fr_chk 20 "0" "a full turn of 2 pi is the identity: Z = 3"
fr_chk 21 "1" "no turn: a value is there"
fr_chk 22 "0" "no turn: X = 1"
fr_chk 23 "0" "no turn: Y = 2"
fr_chk 24 "0" "no turn: Z = 3"
fr_chk 25 "1" "a typed 1 is one radian: (cos 1, sin 1) = (0.5403, 0.8415), not one degree: a value is there"
fr_chk 26 "0" "a typed 1 is one radian: (cos 1, sin 1) = (0.5403, 0.8415), not one degree: X = 0.5403023059"
fr_chk 27 "0" "a typed 1 is one radian: (cos 1, sin 1) = (0.5403, 0.8415), not one degree: Y = 0.8414709848"
fr_chk 28 "0" "a typed 1 is one radian: (cos 1, sin 1) = (0.5403, 0.8415), not one degree: Z = 0"
fr_chk 29 "1" "a typed 90 is ninety radians: (cos 90, sin 90) = (-0.4481, 0.8940), not a quarter turn: a value is there"
fr_chk 30 "0" "a typed 90 is ninety radians: (cos 90, sin 90) = (-0.4481, 0.8940), not a quarter turn: X = -0.4480736161"
fr_chk 31 "0" "a typed 90 is ninety radians: (cos 90, sin 90) = (-0.4481, 0.8940), not a quarter turn: Y = 0.8939966636"
fr_chk 32 "0" "a typed 90 is ninety radians: (cos 90, sin 90) = (-0.4481, 0.8940), not a quarter turn: Z = 0"
fr_chk 33 "1" "Radians(90 degrees) wired into the Angle gives a quarter turn: a value is there"
fr_chk 34 "0" "Radians(90 degrees) wired into the Angle gives a quarter turn: X = 0"
fr_chk 35 "0" "Radians(90 degrees) wired into the Angle gives a quarter turn: Y = 1"
fr_chk 36 "0" "Radians(90 degrees) wired into the Angle gives a quarter turn: Z = 0"
fr_chk 37 "1" "about the plane origin (1,1,0): (1,0,0) is (0,-1) from it, turned pi/2 ccw (x,y) -> (-y,x) = (1,0), so (2,1,0): a value is there"
fr_chk 38 "0" "about the plane origin (1,1,0): (1,0,0) is (0,-1) from it, turned pi/2 ccw (x,y) -> (-y,x) = (1,0), so (2,1,0): X = 2"
fr_chk 39 "0" "about the plane origin (1,1,0): (1,0,0) is (0,-1) from it, turned pi/2 ccw (x,y) -> (-y,x) = (1,0), so (2,1,0): Y = 1"
fr_chk 40 "0" "about the plane origin (1,1,0): (1,0,0) is (0,-1) from it, turned pi/2 ccw (x,y) -> (-y,x) = (1,0), so (2,1,0): Z = 0"
fr_chk 41 "1" "about the axis X (plane normal (1,0,0)): Y -> Z by the right-hand rule: a value is there"
fr_chk 42 "0" "about the axis X (plane normal (1,0,0)): Y -> Z by the right-hand rule: X = 0"
fr_chk 43 "0" "about the axis X (plane normal (1,0,0)): Y -> Z by the right-hand rule: Y = 0"
fr_chk 44 "0" "about the axis X (plane normal (1,0,0)): Y -> Z by the right-hand rule: Z = 1"
fr_chk 45 "0" "any angle keeps the distance 5 from the axis (3,4 -> 5) and Z = 5"
fr_chk 46 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
