# ---------------------------------------------------------------------------
# BFCYL_: Flow Register row geometry_nodes/Cylinder (Cylinder), flowrows_gen.py. The graph flowrows_bfcyl.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: n-gon prism: V = n (side + 1) (+2 centres with fan caps), F = n side + 2 (n-gon caps) or n side + 2n (fan), E by rings n (side + 1), verticals n side, spokes 2n for fans; volume (n/2) r^2 sin(2 pi/n) depth, area n 2 r sin(pi/n) depth + n r^2 sin(2 pi/n); an open tube has no cap faces (fill type none). Single precision: within 5e-6.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_bfcyl.dflow|g" "$HERE/flowrows_bfcyl_script.txt" > "$TMPW/flowrows_bfcyl_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcyl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCYL_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_bfcyl_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL BFCYL_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   BFCYL_$1: $3 (measured '$got')"; else echo "FAIL BFCYL_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 99 node(s)" <<<"$FR_OUT"; then echo "ok   BFCYL_solve: the player solved all 99 nodes (every node type is registered)"; else echo "FAIL BFCYL_solve: expected 'solved 99 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 25 " <<<"$FR_OUT"; then echo "ok   BFCYL_objects: exactly 25 point(s) baked"; else echo "FAIL BFCYL_objects: expected 25 baked objects"; fail=1; fi
fr_chk 1 "16" "8-gon prism with n-gon caps:: 16 points"
fr_chk 2 "24" "8-gon prism with n-gon caps:: 24 edges"
fr_chk 3 "10" "8-gon prism with n-gon caps:: 10 faces"
fr_chk 4 "48" "8-gon prism with n-gon caps:: 48 corners"
fr_chk 5 "0" "volume = polygon area (n/2) r^2 sin(2 pi/n) times depth"
fr_chk 6 "0" "area = n * chord * depth + 2 * polygon area"
fr_chk 7 "0" "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4): min X"
fr_chk 8 "0" "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4): min Y"
fr_chk 9 "0" "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4): min Z"
fr_chk 10 "0" "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4): max X"
fr_chk 11 "0" "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4): max Y"
fr_chk 12 "0" "z from -depth/2 to +depth/2, x and y +-r (8 is a multiple of 4): max Z"
fr_chk 13 "34" "side segments 3, triangle-fan caps:: 34 points"
fr_chk 14 "72" "side segments 3, triangle-fan caps:: 72 edges"
fr_chk 15 "40" "side segments 3, triangle-fan caps:: 40 faces"
fr_chk 16 "0" "same volume with fan caps"
fr_chk 17 "16" "no caps: open tube: 16 points"
fr_chk 18 "24" "no caps: open tube: 24 edges"
fr_chk 19 "8" "no caps: open tube: 8 faces"
fr_chk 20 "64" "the default cylinder has: 64 points"
fr_chk 21 "96" "the default cylinder has: 96 edges"
fr_chk 22 "34" "the default cylinder has: 34 faces"
fr_chk 23 "192" "the default cylinder has: 192 corners"
fr_chk 24 "0" "default volume 16 sin(pi/16) * 2 (polygon prism)"
fr_chk 25 "500000000000" "control: the residual chain reports |Square(2) - 4.5| as 500000000000, so the 0 values above are real matches"
