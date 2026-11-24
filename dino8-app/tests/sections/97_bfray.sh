# ---------------------------------------------------------------------------
# BFRAY_: Flow Register row geometry_nodes/Raycast (Raycast), flowrows_gen.py. The graph flowrows_bfray.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: size-2 cube: from (5,0,0) along -X hits (1,0,0), normal +X, distance 4 (also when the direction is (-3,0,0)); from (0.5,0.5,5) down hits (0.5,0.5,1) normal +Z; from the centre along +Y hits (0,1,0) at 1; (4,2,0.5) along (-2,-1,0) hits the x = 1 face at t = 1.5 so distance 1.5 sqrt 5; Ray Length 3 < 4 misses and reports Hit Distance = Ray Length; pointing away and passing beside miss; a unit grid hit from below reports normal +Z at 2. Position and normal of a miss are zero. The Blender miss values are from recollection.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfray.dflow|g" "$HERE/flowrows_bfray_script.txt" > "$TMPW/flowrows_bfray_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfray_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFRAY_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfray_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFRAY_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFRAY_$1: $3 (measured '$got')"; else echo "FAIL BFRAY_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 273 node(s)" <<<"$FR_OUT"; then echo "ok   BFRAY_solve: the player solved all 273 nodes (every node type is registered)"; else echo "FAIL BFRAY_solve: expected 'solved 273 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 51 " <<<"$FR_OUT"; then echo "ok   BFRAY_objects: exactly 51 point(s) baked"; else echo "FAIL BFRAY_objects: expected 51 baked objects"; fail=1; fi
fr_chk 1 "1" "from +X towards the cube: Is Hit 1"
fr_chk 2 "0" "from +X towards the cube: hit position X"
fr_chk 3 "0" "from +X towards the cube: hit position Y"
fr_chk 4 "0" "from +X towards the cube: hit position Z"
fr_chk 5 "0" "from +X towards the cube: hit normal X"
fr_chk 6 "0" "from +X towards the cube: hit normal Y"
fr_chk 7 "0" "from +X towards the cube: hit normal Z"
fr_chk 8 "0" "from +X towards the cube: distance"
fr_chk 9 "1" "the direction is normalised: Is Hit 1"
fr_chk 10 "0" "the direction is normalised: hit position X"
fr_chk 11 "0" "the direction is normalised: hit position Y"
fr_chk 12 "0" "the direction is normalised: hit position Z"
fr_chk 13 "0" "the direction is normalised: hit normal X"
fr_chk 14 "0" "the direction is normalised: hit normal Y"
fr_chk 15 "0" "the direction is normalised: hit normal Z"
fr_chk 16 "0" "the direction is normalised: distance"
fr_chk 17 "1" "from above: Is Hit 1"
fr_chk 18 "0" "from above: hit position X"
fr_chk 19 "0" "from above: hit position Y"
fr_chk 20 "0" "from above: hit position Z"
fr_chk 21 "0" "from above: hit normal X"
fr_chk 22 "0" "from above: hit normal Y"
fr_chk 23 "0" "from above: hit normal Z"
fr_chk 24 "0" "from above: distance"
fr_chk 25 "1" "from inside the box the first hit is the wall ahead: Is Hit 1"
fr_chk 26 "0" "from inside the box the first hit is the wall ahead: hit position X"
fr_chk 27 "0" "from inside the box the first hit is the wall ahead: hit position Y"
fr_chk 28 "0" "from inside the box the first hit is the wall ahead: hit position Z"
fr_chk 29 "0" "from inside the box the first hit is the wall ahead: hit normal X"
fr_chk 30 "0" "from inside the box the first hit is the wall ahead: hit normal Y"
fr_chk 31 "0" "from inside the box the first hit is the wall ahead: hit normal Z"
fr_chk 32 "0" "from inside the box the first hit is the wall ahead: distance"
fr_chk 33 "1" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): Is Hit 1"
fr_chk 34 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): hit position X"
fr_chk 35 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): hit position Y"
fr_chk 36 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): hit position Z"
fr_chk 37 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): hit normal X"
fr_chk 38 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): hit normal Y"
fr_chk 39 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): hit normal Z"
fr_chk 40 "0" "an oblique ray hits the +X face at t = 1.5 (distance 1.5 sqrt 5): distance"
fr_chk 41 "0" "out of reach (4 > Ray Length 3): the distance is the Ray Length: Is Hit 0"
fr_chk 42 "0" "out of reach (4 > Ray Length 3): the distance is the Ray Length: distance"
fr_chk 43 "0" "pointing away: Is Hit 0"
fr_chk 44 "0" "pointing away: distance"
fr_chk 45 "0" "passing beside the cube: Is Hit 0"
fr_chk 46 "0" "passing beside the cube: distance"
fr_chk 47 "0" "a ray from below hits the grid's back: distance 2"
fr_chk 48 "0" "and reports the face normal +Z X"
fr_chk 49 "0" "and reports the face normal +Z Y"
fr_chk 50 "0" "and reports the face normal +Z Z"
fr_chk 51 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
