# ---------------------------------------------------------------------------
# BFVCOR_: Flow Register row geometry_nodes/Vertex of Corner (Vertex of Corner), flowrows_gen.py. The graph flowrows_bfvcor.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: read off the face lists of the files: strip f0 (0 1 2 3), f1 (1 4 5 2), f2 (3 2 6), f3 (7 8 9) give corner 4 = vertex 1, 6 = 5, 7 = 2, 8 = 3, 10 = 6, 11 = 7, 13 = 9; cube f0 (0 3 2 1), f1 (4 5 6 7), f2 (0 1 5 4), f3 (3 7 6 2), f4 (0 4 7 3), f5 (1 2 6 5) give corner 5 = 5, 10 = 5, 14 = 6, 19 = 3, 23 = 5.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfvcor.dflow" > "$TMPW/flowrows_bfvcor.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfvcor.dflow|g" "$HERE/flowrows_bfvcor_script.txt" > "$TMPW/flowrows_bfvcor_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfvcor_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFVCOR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfvcor_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFVCOR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFVCOR_$1: $3 (measured '$got')"; else echo "FAIL BFVCOR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 44 node(s)" <<<"$FR_OUT"; then echo "ok   BFVCOR_solve: the player solved all 44 nodes (every node type is registered)"; else echo "FAIL BFVCOR_solve: expected 'solved 44 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 14 " <<<"$FR_OUT"; then echo "ok   BFVCOR_objects: exactly 14 point(s) baked"; else echo "FAIL BFVCOR_objects: expected 14 baked objects"; fail=1; fi
fr_chk 1 "0" "corner 0 sits on vertex 0"
fr_chk 2 "1" "corner 4 sits on vertex 1"
fr_chk 3 "5" "corner 6 sits on vertex 5"
fr_chk 4 "2" "corner 7 sits on vertex 2"
fr_chk 5 "3" "corner 8 sits on vertex 3"
fr_chk 6 "6" "corner 10 sits on vertex 6"
fr_chk 7 "7" "corner 11 sits on vertex 7"
fr_chk 8 "9" "corner 13 sits on vertex 9"
fr_chk 9 "0" "corner 0 sits on vertex 0"
fr_chk 10 "5" "corner 5 sits on vertex 5"
fr_chk 11 "5" "corner 10 sits on vertex 5"
fr_chk 12 "6" "corner 14 sits on vertex 6"
fr_chk 13 "3" "corner 19 sits on vertex 3"
fr_chk 14 "5" "corner 23 sits on vertex 5"
