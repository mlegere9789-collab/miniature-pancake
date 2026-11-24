# ---------------------------------------------------------------------------
# BFFCOR_: Flow Register row geometry_nodes/Face of Corner (Face of Corner), flowrows_gen.py. The graph flowrows_bffcor.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: the inverse of the corner numbering of the strip file (faces of 4, 4, 3, 3 corners) and the cube file (4 each): corner 5 = face 1 position 1, corner 9 = face 2 position 1, corner 13 = face 3 position 2; cube corner 23 = face 5 position 3, corner 8 = face 2 position 0.
# ---------------------------------------------------------------------------
cat > "$TMPW/cube.obj" <<'EOF_BF'
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 0 0 1
v 1 0 1
v 1 1 1
v 0 1 1
f 1 4 3 2
f 5 6 7 8
f 1 2 6 5
f 4 8 7 3
f 1 5 8 4
f 2 3 7 6
EOF_BF
cat > "$TMPW/strip.obj" <<'EOF_BF'
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 2 0 0
v 2 1 0
v 0.5 2 0
v 5 5 5
v 6 5 5
v 5 6 5
f 1 2 3 4
f 2 5 6 3
f 4 3 7
f 8 9 10
EOF_BF
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bffcor.dflow" > "$TMPW/flowrows_bffcor.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bffcor.dflow|g" "$HERE/flowrows_bffcor_script.txt" > "$TMPW/flowrows_bffcor_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bffcor_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFCOR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bffcor_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFCOR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFFCOR_$1: $3 (measured '$got')"; else echo "FAIL BFFCOR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 52 node(s)" <<<"$FR_OUT"; then echo "ok   BFFCOR_solve: the player solved all 52 nodes (every node type is registered)"; else echo "FAIL BFFCOR_solve: expected 'solved 52 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 20 " <<<"$FR_OUT"; then echo "ok   BFFCOR_objects: exactly 20 point(s) baked"; else echo "FAIL BFFCOR_objects: expected 20 baked objects"; fail=1; fi
fr_chk 1 "0" "corner 0 lies in face 0"
fr_chk 2 "0" "corner 0 is corner 0 of its face"
fr_chk 3 "0" "corner 3 lies in face 0"
fr_chk 4 "3" "corner 3 is corner 3 of its face"
fr_chk 5 "1" "corner 5 lies in face 1"
fr_chk 6 "1" "corner 5 is corner 1 of its face"
fr_chk 7 "1" "corner 7 lies in face 1"
fr_chk 8 "3" "corner 7 is corner 3 of its face"
fr_chk 9 "2" "corner 9 lies in face 2"
fr_chk 10 "1" "corner 9 is corner 1 of its face"
fr_chk 11 "2" "corner 10 lies in face 2"
fr_chk 12 "2" "corner 10 is corner 2 of its face"
fr_chk 13 "3" "corner 11 lies in face 3"
fr_chk 14 "0" "corner 11 is corner 0 of its face"
fr_chk 15 "3" "corner 13 lies in face 3"
fr_chk 16 "2" "corner 13 is corner 2 of its face"
fr_chk 17 "5" "corner 23 lies in face 5"
fr_chk 18 "3" "corner 23 is corner 3 of its face"
fr_chk 19 "2" "corner 8 lies in face 2"
fr_chk 20 "0" "corner 8 is corner 0 of its face"
