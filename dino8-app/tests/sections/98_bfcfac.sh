# ---------------------------------------------------------------------------
# BFCFAC_: Flow Register row geometry_nodes/Corners of Face (Corners of Face), flowrows_gen.py. The graph flowrows_bfcfac.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: corners are numbered face by face: strip file faces have 4, 4, 3, 3 corners so face 0 owns corners 0-3, face 1 4-7, face 2 8-10, face 3 11-13 (14 in all); cube file face f owns 4f .. 4f + 3 (face 4: 16-19).
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfcfac.dflow" > "$TMPW/flowrows_bfcfac.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfcfac.dflow|g" "$HERE/flowrows_bfcfac_script.txt" > "$TMPW/flowrows_bfcfac_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcfac_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCFAC_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcfac_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCFAC_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCFAC_$1: $3 (measured '$got')"; else echo "FAIL BFCFAC_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 32 node(s)" <<<"$FR_OUT"; then echo "ok   BFCFAC_solve: the player solved all 32 nodes (every node type is registered)"; else echo "FAIL BFCFAC_solve: expected 'solved 32 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 28 " <<<"$FR_OUT"; then echo "ok   BFCFAC_objects: exactly 28 point(s) baked"; else echo "FAIL BFCFAC_objects: expected 28 baked objects"; fail=1; fi
fr_chk 1 "0 1 2 3" "strip face 0 (quad): corners 0..3"
fr_chk 2 "4" "strip face 0 (quad): corners 0..3 (Total)"
fr_chk 3 "4 5 6 7" "face 1 (quad): corners 4..7"
fr_chk 4 "4" "face 1 (quad): corners 4..7 (Total)"
fr_chk 5 "8 9 10" "face 2 (triangle) starts after 4 + 4 corners"
fr_chk 6 "3" "face 2 (triangle) starts after 4 + 4 corners (Total)"
fr_chk 7 "11 12 13" "face 3 (triangle)"
fr_chk 8 "3" "face 3 (triangle) (Total)"
fr_chk 9 "16 17 18 19" "cube face 4 starts at 4 * 4 = 16"
fr_chk 10 "4" "cube face 4 starts at 4 * 4 = 16 (Total)"
fr_chk 11 "0 1 2 3" "cube face 0"
fr_chk 12 "4" "cube face 0 (Total)"
