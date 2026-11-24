# ---------------------------------------------------------------------------
# BFCVER_: Flow Register row geometry_nodes/Corners of Vertex (Corners of Vertex), flowrows_gen.py. The graph flowrows_bfcver.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: from the corner numbering and the face lists: strip vertex 2 owns corners 2 (f0), 7 (f1), 9 (f2); vertex 3 owns 3 and 8; vertex 0 owns 0; vertex 6 owns 10; cube vertex 0 owns 0, 8, 16 and vertex 6 owns 6, 14, 22 (always one corner per face around the vertex).
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfcver.dflow" > "$TMPW/flowrows_bfcver.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfcver.dflow|g" "$HERE/flowrows_bfcver_script.txt" > "$TMPW/flowrows_bfcver_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcver_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCVER_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcver_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCVER_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCVER_$1: $3 (measured '$got')"; else echo "FAIL BFCVER_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 32 node(s)" <<<"$FR_OUT"; then echo "ok   BFCVER_solve: the player solved all 32 nodes (every node type is registered)"; else echo "FAIL BFCVER_solve: expected 'solved 32 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 19 " <<<"$FR_OUT"; then echo "ok   BFCVER_objects: exactly 19 point(s) baked"; else echo "FAIL BFCVER_objects: expected 19 baked objects"; fail=1; fi
fr_chk 1 "2 7 9" "strip vertex 2 is in f0 (position 2), f1 (position 3), f2 (position 1)"
fr_chk 2 "3" "strip vertex 2 is in f0 (position 2), f1 (position 3), f2 (position 1) (Total)"
fr_chk 3 "0" "vertex 0 is only in f0"
fr_chk 4 "1" "vertex 0 is only in f0 (Total)"
fr_chk 5 "10" "vertex 6 is only in f2, position 2"
fr_chk 6 "1" "vertex 6 is only in f2, position 2 (Total)"
fr_chk 7 "3 8" "vertex 3 is in f0 (position 3) and f2 (position 0)"
fr_chk 8 "2" "vertex 3 is in f0 (position 3) and f2 (position 0) (Total)"
fr_chk 9 "0 8 16" "cube vertex 0 is in f0, f2, f4 at position 0"
fr_chk 10 "3" "cube vertex 0 is in f0, f2, f4 at position 0 (Total)"
fr_chk 11 "6 14 22" "cube vertex 6 is in f1, f3, f5 at position 2"
fr_chk 12 "3" "cube vertex 6 is in f1, f3, f5 at position 2 (Total)"
