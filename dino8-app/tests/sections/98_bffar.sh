# ---------------------------------------------------------------------------
# BFFAR_: Flow Register row geometry_nodes/Face Area (Face Area), flowrows_gen.py. The graph flowrows_bffar.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: strip file: f0 and f1 are unit squares (1), f2 is the triangle (0,1) (1,1) (0.5,2) of base 1 and height 1 (0.5), f3 the triangle with legs 1 (0.5); cube faces 1; default cylinder: side quad 2 sin(pi/32) * 2 = 0.3925..., each cap one 32-gon of area 16 sin(2 pi/32); a 3 x 3 grid of 4 x 6 has quads of 2 x 3 = 6. Single precision vertices: within 5e-6.
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
sed "s|@TMPW@|$TMPW|g" "$HERE/flowrows_bffar.dflow" > "$TMPW/flowrows_bffar.dflow"
sed "s|@GRAPH@|$TMPW/flowrows_bffar.dflow|g" "$HERE/flowrows_bffar_script.txt" > "$TMPW/flowrows_bffar_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bffar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bffar_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFFAR_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFFAR_$1: $3 (measured '$got')"; else echo "FAIL BFFAR_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 64 node(s)" <<<"$FR_OUT"; then echo "ok   BFFAR_solve: the player solved all 64 nodes (every node type is registered)"; else echo "FAIL BFFAR_solve: expected 'solved 64 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 10 " <<<"$FR_OUT"; then echo "ok   BFFAR_objects: exactly 10 point(s) baked"; else echo "FAIL BFFAR_objects: expected 10 baked objects"; fail=1; fi
fr_chk 1 "0" "strip f0 is a unit square"
fr_chk 2 "0" "f1 is a unit square"
fr_chk 3 "0" "f2: base (1,1)-(0,1) of length 1, apex (0.5,2) at height 1"
fr_chk 4 "0" "the far triangle has legs 1 and 1"
fr_chk 5 "0" "a cube face"
fr_chk 6 "0" "another cube face"
fr_chk 7 "0" "default cylinder side quad: chord 2 sin(pi/32) times depth 2"
fr_chk 8 "0" "the cap is one 32-gon: (32/2) sin(2 pi/32)"
fr_chk 9 "0" "a 3 x 3 grid of 4 x 6 has quads of 2 x 3 = 6"
fr_chk 10 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
