# ---------------------------------------------------------------------------
# BFSETP_: Flow Register row geometry_nodes/Set Position (Set Position), flowrows_gen.py. The graph flowrows_bfsetp.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: new position = (Position when connected, else the old one) + Offset: unit cube + (1,2,3) spans (1,2,3)-(2,3,4) with volume 1; Position (5,5,5) + Offset (0,0,1) collapses all 8 vertices to (5,5,6); the line (0,0,0)-(0,0,3) + (2,0,-1) spans z -1..2 at x = 2; the point (1,2,3) + (1,1,1) = (2,3,4); no inputs change nothing. Position and Offset are constants, not fields (untested as fields).
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bfsetp.dflow" > "$TMPW/flowrows_bfsetp.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bfsetp.dflow|g" "$HERE/flowrows_bfsetp_script.txt" > "$TMPW/flowrows_bfsetp_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsetp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSETP_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfsetp_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFSETP_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFSETP_$1: $3 (measured '$got')"; else echo "FAIL BFSETP_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 192 node(s)" <<<"$FR_OUT"; then echo "ok   BFSETP_solve: the player solved all 192 nodes (every node type is registered)"; else echo "FAIL BFSETP_solve: expected 'solved 192 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 32 " <<<"$FR_OUT"; then echo "ok   BFSETP_objects: exactly 32 point(s) baked"; else echo "FAIL BFSETP_objects: expected 32 baked objects"; fail=1; fi
fr_chk 1 "0" "Offset (1,2,3) moves the unit cube: min X"
fr_chk 2 "0" "Offset (1,2,3) moves the unit cube: min Y"
fr_chk 3 "0" "Offset (1,2,3) moves the unit cube: min Z"
fr_chk 4 "0" "Offset (1,2,3) moves the unit cube: max X"
fr_chk 5 "0" "Offset (1,2,3) moves the unit cube: max Y"
fr_chk 6 "0" "Offset (1,2,3) moves the unit cube: max Z"
fr_chk 7 "0" "and keeps its volume"
fr_chk 8 "0" "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6): min X"
fr_chk 9 "0" "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6): min Y"
fr_chk 10 "0" "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6): min Z"
fr_chk 11 "0" "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6): max X"
fr_chk 12 "0" "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6): max Y"
fr_chk 13 "0" "Position (5,5,5) puts every vertex there, then Offset (0,0,1): one point (5,5,6): max Z"
fr_chk 14 "0" "curve (0,0,0)-(0,0,3) offset (2,0,-1): min X"
fr_chk 15 "0" "curve (0,0,0)-(0,0,3) offset (2,0,-1): min Y"
fr_chk 16 "0" "curve (0,0,0)-(0,0,3) offset (2,0,-1): min Z"
fr_chk 17 "0" "curve (0,0,0)-(0,0,3) offset (2,0,-1): max X"
fr_chk 18 "0" "curve (0,0,0)-(0,0,3) offset (2,0,-1): max Y"
fr_chk 19 "0" "curve (0,0,0)-(0,0,3) offset (2,0,-1): max Z"
fr_chk 20 "0" "a point (1,2,3) offset (1,1,1): min X"
fr_chk 21 "0" "a point (1,2,3) offset (1,1,1): min Y"
fr_chk 22 "0" "a point (1,2,3) offset (1,1,1): min Z"
fr_chk 23 "0" "a point (1,2,3) offset (1,1,1): max X"
fr_chk 24 "0" "a point (1,2,3) offset (1,1,1): max Y"
fr_chk 25 "0" "a point (1,2,3) offset (1,1,1): max Z"
fr_chk 26 "0" "no Position and a zero Offset change nothing: min X"
fr_chk 27 "0" "no Position and a zero Offset change nothing: min Y"
fr_chk 28 "0" "no Position and a zero Offset change nothing: min Z"
fr_chk 29 "0" "no Position and a zero Offset change nothing: max X"
fr_chk 30 "0" "no Position and a zero Offset change nothing: max Y"
fr_chk 31 "0" "no Position and a zero Offset change nothing: max Z"
fr_chk 32 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
