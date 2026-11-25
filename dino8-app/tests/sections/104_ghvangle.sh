# ---------------------------------------------------------------------------
# GHVANGLE_: Flow Register row D8-GH-VECTOR-0010 (Angle), flowrows6_gen.py. The graph flowrows_ghvangle.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: angle = arccos(a.b / |a||b|) in [0, pi] radians, reflex = 2 pi - angle: perpendicular pi/2 (reflex 3 pi/2); (1,0,0),(1,1,0) = pi/4; opposite = pi (reflex pi); same direction = 0 (reflex 2 pi); (1,0,0),(1,1,1): cos = 1/sqrt 3, tan = sqrt 2, angle = atan(sqrt 2); (0,0,2),(0,3,3) = pi/4; (1,sqrt 3,0),(1,0,0) = pi/3; swapping keeps the angle; a zero vector is flagged.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_ghvangle.dflow|g" "$HERE/flowrows_ghvangle_script.txt" > "$TMPW/flowrows_ghvangle_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvangle_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVANGLE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_ghvangle_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL GHVANGLE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   GHVANGLE_$1: $3 (measured '$got')"; else echo "FAIL GHVANGLE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
# fr_has "text" MESSAGE: the player's output contains this text; fr_cnt "text" N MESSAGE: it contains exactly N lines with it
fr_has() {
  if grep -qF -- "$1" <<<"$FR_OUT"; then echo "ok   GHVANGLE_$2"; else echo "FAIL GHVANGLE_$2 (no line containing '$1')"; fail=1; fi
}
fr_cnt() {
  local n
  n="$({ grep -cF -- "$1" <<<"$FR_OUT" || true; })"
  if [ "$n" = "$2" ]; then echo "ok   GHVANGLE_$3 (measured $n)"; else echo "FAIL GHVANGLE_$3 (measured $n, expected $2)"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 117 node(s)" <<<"$FR_OUT"; then echo "ok   GHVANGLE_solve: the player solved all 117 nodes (every node type is registered)"; else echo "FAIL GHVANGLE_solve: expected 'solved 117 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 19 " <<<"$FR_OUT"; then echo "ok   GHVANGLE_objects: exactly 19 point(s) baked"; else echo "FAIL GHVANGLE_objects: expected 19 baked objects"; fail=1; fi
fr_chk 1 "0" "angle, perpendicular"
fr_chk 2 "0" "reflex angle, perpendicular = 2 pi - angle"
fr_chk 3 "0" "angle, 45 degrees"
fr_chk 4 "0" "reflex angle, 45 degrees = 2 pi - angle"
fr_chk 5 "0" "angle, opposite"
fr_chk 6 "0" "reflex angle, opposite = 2 pi - angle"
fr_chk 7 "1" "angle, parallel, same direction: a value is there"
fr_chk 8 "0" "angle, parallel, same direction"
fr_chk 9 "0" "reflex angle, parallel, same direction = 2 pi - angle"
fr_chk 10 "0" "angle, (1,0,0) and (1,1,1): tan = sqrt(1 - 1/3) / (1/sqrt 3) = sqrt 2"
fr_chk 11 "0" "reflex angle, (1,0,0) and (1,1,1): tan = sqrt(1 - 1/3) / (1/sqrt 3) = sqrt 2 = 2 pi - angle"
fr_chk 12 "0" "angle, (0,0,2) and (0,3,3): cos = 6 / (2 * 3 sqrt 2) = 1/sqrt 2"
fr_chk 13 "0" "reflex angle, (0,0,2) and (0,3,3): cos = 6 / (2 * 3 sqrt 2) = 1/sqrt 2 = 2 pi - angle"
fr_chk 14 "0" "angle, (1,sqrt 3,0) and (1,0,0): cos = 1/2"
fr_chk 15 "0" "reflex angle, (1,sqrt 3,0) and (1,0,0): cos = 1/2 = 2 pi - angle"
fr_chk 16 "0" "swapping the vectors keeps the angle (unsigned)"
fr_chk 17 "0" "a zero vector has no direction: no angle"
fr_chk 18 "0" "and no reflex angle"
fr_chk 19 "500000000000" "control: the residual chain reports Square(2) - 4.5 as 500000000000, so the 0 values above are real matches"
fr_has "GrasshopperPlayer: Angle (#103) flagged: A zero-length vector has no direction" "Angle #103 is flagged: A zero-length vector has no direction"
fr_cnt "flagged:" 1 "flagged: exactly 1 node(s) are flagged" 
