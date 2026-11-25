# ---------------------------------------------------------------------------
# GHXMIGRATE_: Flow Register row D8-GH-XFORM-0008, D8-GH-XFORM-0015, D8-GH-XFORM-0017 (Rotate, Linear Array, Polar Array, Arc: migration of dflow1 files), flowrows6_gen.py. The graph flowrows_ghxmigrate.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: an older file ('format' dflow1) keeps its old meanings: Rotate typed 90 is a quarter turn (0,1,0); Rotate untyped is 45 degrees (sqrt 2 / 2, sqrt 2 / 2); a Rotate angle wired from a node giving 45 is 45 degrees; Polar Array typed 180 with 3 copies at 0, 60, 120 degrees; untyped it is a full circle (quarter turns for 4 copies); an angle wired from a node giving 180 with 2 copies puts the second at 90 degrees; Linear Array without a Distance spaces 5 apart along the unitized direction (Z = 0 5 10), with Distance 3 Z = 0 3 6; Arc: radius 2 typed 0 and 90 runs from (2,0,0) to (0,2,0); Start Angle wired from a node giving 90 with End 180 and radius 1 runs from (0,1,0) to (-1,0,0); End Angle wired from a node giving 180 with Start 90 and radius 2 runs from (0,2,0) to (-2,0,0); radius 3 with no angle typed runs from (3,0,0) to (0,3,0) (the old defaults 0 and 90 degrees); radius 1 from 0 to 360 has length 2 pi.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghxmigrate.dflow|g" "$HERE/flowrows_ghxmigrate_script.txt" > "$TMPW/flowrows_ghxmigrate_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxmigrate_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXMIGRATE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghxmigrate_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHXMIGRATE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHXMIGRATE_$1: $3 (measured '$got')"; else echo "FAIL GHXMIGRATE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHXMIGRATE_$2"; else echo "FAIL GHXMIGRATE_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHXMIGRATE_$3 (measured $n)"; else echo "FAIL GHXMIGRATE_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 422 node(s)" <<<"$FR_OUT"; then echo "ok   GHXMIGRATE_solve: the player solved all 422 nodes (every node type is registered)"; else echo "FAIL GHXMIGRATE_solve: expected 'solved 422 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 84 " <<<"$FR_OUT"; then echo "ok   GHXMIGRATE_objects: exactly 84 point(s) baked"; else echo "FAIL GHXMIGRATE_objects: expected 84 baked objects"; fail=1; fi
fr_chk 1 "1" "legacy Rotate with a typed Angle 90: a quarter turn (90 degrees): a value is there"
fr_chk 2 "0" "legacy Rotate with a typed Angle 90: a quarter turn (90 degrees): X = 0"
fr_chk 3 "0" "legacy Rotate with a typed Angle 90: a quarter turn (90 degrees): Y = 1"
fr_chk 4 "0" "legacy Rotate with a typed Angle 90: a quarter turn (90 degrees): Z = 0"
fr_chk 5 "1" "legacy Rotate with no Angle typed: the old default 45 degrees: a value is there"
fr_chk 6 "0" "legacy Rotate with no Angle typed: the old default 45 degrees: X = 0.7071067812"
fr_chk 7 "0" "legacy Rotate with no Angle typed: the old default 45 degrees: Y = 0.7071067812"
fr_chk 8 "0" "legacy Rotate with no Angle typed: the old default 45 degrees: Z = 0"
fr_chk 9 "1" "legacy Rotate with the Angle wired from a node giving 45: 45 degrees (a Radians node is put in the wire): a value is there"
fr_chk 10 "0" "legacy Rotate with the Angle wired from a node giving 45: 45 degrees (a Radians node is put in the wire): X = 1.414213562"
fr_chk 11 "0" "legacy Rotate with the Angle wired from a node giving 45: 45 degrees (a Radians node is put in the wire): Y = 1.414213562"
fr_chk 12 "0" "legacy Rotate with the Angle wired from a node giving 45: 45 degrees (a Radians node is put in the wire): Z = 0"
fr_chk 13 "1" "legacy Polar Array, typed Angle 180, 3 copies: copy 0 at 60 degrees times 0: a value is there"
fr_chk 14 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 0 at 60 degrees times 0: X = 1"
fr_chk 15 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 0 at 60 degrees times 0: Y = 0"
fr_chk 16 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 0 at 60 degrees times 0: Z = 0"
fr_chk 17 "1" "legacy Polar Array, typed Angle 180, 3 copies: copy 1 at 60 degrees times 1: a value is there"
fr_chk 18 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 1 at 60 degrees times 1: X = 0.5"
fr_chk 19 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 1 at 60 degrees times 1: Y = 0.8660254038"
fr_chk 20 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 1 at 60 degrees times 1: Z = 0"
fr_chk 21 "1" "legacy Polar Array, typed Angle 180, 3 copies: copy 2 at 60 degrees times 2: a value is there"
fr_chk 22 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 2 at 60 degrees times 2: X = -0.5"
fr_chk 23 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 2 at 60 degrees times 2: Y = 0.8660254038"
fr_chk 24 "0" "legacy Polar Array, typed Angle 180, 3 copies: copy 2 at 60 degrees times 2: Z = 0"
fr_chk 25 "1" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 0 of 4 is a quarter turn times 0: a value is there"
fr_chk 26 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 0 of 4 is a quarter turn times 0: X = 2"
fr_chk 27 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 0 of 4 is a quarter turn times 0: Y = 0"
fr_chk 28 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 0 of 4 is a quarter turn times 0: Z = 0"
fr_chk 29 "1" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 1 of 4 is a quarter turn times 1: a value is there"
fr_chk 30 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 1 of 4 is a quarter turn times 1: X = 0"
fr_chk 31 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 1 of 4 is a quarter turn times 1: Y = 2"
fr_chk 32 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 1 of 4 is a quarter turn times 1: Z = 0"
fr_chk 33 "1" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 2 of 4 is a quarter turn times 2: a value is there"
fr_chk 34 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 2 of 4 is a quarter turn times 2: X = -2"
fr_chk 35 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 2 of 4 is a quarter turn times 2: Y = 0"
fr_chk 36 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 2 of 4 is a quarter turn times 2: Z = 0"
fr_chk 37 "1" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 3 of 4 is a quarter turn times 3: a value is there"
fr_chk 38 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 3 of 4 is a quarter turn times 3: X = 0"
fr_chk 39 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 3 of 4 is a quarter turn times 3: Y = -2"
fr_chk 40 "0" "legacy Polar Array with no Angle typed (the old default 360 degrees): copy 3 of 4 is a quarter turn times 3: Z = 0"
fr_chk 41 "1" "legacy Polar Array, Angle wired from a node giving 180, 2 copies: the second at 90 degrees (a Radians node is put in the wire): a value is there"
fr_chk 42 "0" "legacy Polar Array, Angle wired from a node giving 180, 2 copies: the second at 90 degrees (a Radians node is put in the wire): X = 0"
fr_chk 43 "0" "legacy Polar Array, Angle wired from a node giving 180, 2 copies: the second at 90 degrees (a Radians node is put in the wire): Y = 1"
fr_chk 44 "0" "legacy Polar Array, Angle wired from a node giving 180, 2 copies: the second at 90 degrees (a Radians node is put in the wire): Z = 0"
fr_chk 45 "1" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): starts: a value is there"
fr_chk 46 "0" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): starts: X = 2"
fr_chk 47 "0" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): starts: Y = 0"
fr_chk 48 "0" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): starts: Z = 0"
fr_chk 49 "1" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): ends: a value is there"
fr_chk 50 "0" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): ends: X = 0"
fr_chk 51 "0" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): ends: Y = 2"
fr_chk 52 "0" "legacy Arc of radius 2, typed Start 0 and End 90 (degrees): ends: Z = 0"
fr_chk 53 "1" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): starts: a value is there"
fr_chk 54 "0" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): starts: X = 0"
fr_chk 55 "0" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): starts: Y = 1"
fr_chk 56 "0" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): starts: Z = 0"
fr_chk 57 "1" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): ends: a value is there"
fr_chk 58 "0" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): ends: X = -1"
fr_chk 59 "0" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): ends: Y = 0"
fr_chk 60 "0" "legacy Arc of radius 1, Start Angle wired from a node giving 90, typed End 180 (a Radians node is put in the wire): ends: Z = 0"
fr_chk 61 "1" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): starts: a value is there"
fr_chk 62 "0" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): starts: X = 3"
fr_chk 63 "0" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): starts: Y = 0"
fr_chk 64 "0" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): starts: Z = 0"
fr_chk 65 "1" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): ends: a value is there"
fr_chk 66 "0" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): ends: X = 0"
fr_chk 67 "0" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): ends: Y = 3"
fr_chk 68 "0" "legacy Arc of radius 3 with no angle typed (the old defaults 0 and 90 degrees): ends: Z = 0"
fr_chk 69 "1" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): starts: a value is there"
fr_chk 70 "0" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): starts: X = 0"
fr_chk 71 "0" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): starts: Y = 2"
fr_chk 72 "0" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): starts: Z = 0"
fr_chk 73 "1" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): ends: a value is there"
fr_chk 74 "0" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): ends: X = -2"
fr_chk 75 "0" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): ends: Y = 0"
fr_chk 76 "0" "legacy Arc of radius 2, typed Start 90, End Angle wired from a node giving 180 (a Radians node is put in the wire): ends: Z = 0"
fr_chk 77 "0" "legacy Arc of radius 1 from 0 to 360 (degrees) is a full circle: length 2 pi (to 5e-7)"
fr_chk 78 "0 5 10" "legacy Linear Array with no Distance typed: the old default 5 on the unitized direction (0,0,2): Z = 0, 5, 10 (not 0, 2, 4)"
fr_chk 79 "0 3 6" "legacy Linear Array with a typed Distance 3: Z = 0, 3, 6"
fr_chk 80 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_cnt "flagged:" 0 "flagged: exactly 0 node(s) are flagged" 
