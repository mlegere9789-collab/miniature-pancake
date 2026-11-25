# ---------------------------------------------------------------------------
# R2ANGLE_: Flow Register row D8-GH-MATHS-0035 (Degrees, Radians), flowrows_gen.py. The graph flowrows_r2angle.dflow bakes one point per value under test (X = value,
# Y = case tag) and the section reads them back in baking order. Hand-computed oracle: 180 degrees = pi radians: Degrees(pi) = 180, Degrees(pi/2) = 90, Degrees(-pi/4) = -45, Degrees(0) = 0; Radians(180) = pi, Radians(90) = pi/2, Radians(45) = pi/4, Radians(-360) = -2 pi.
# ---------------------------------------------------------------------------
sed "s|@GRAPH@|$HEREW/flowrows_r2angle.dflow|g" "$HERE/flowrows_r2angle_script.txt" > "$TMPW/flowrows_r2angle_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/flowrows_r2angle_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2ANGLE_run: script exited non-zero"; fail=1; FR_OUT=""; }
else
  FR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/flowrows_r2angle_script.txt" 2>&1)" || { echo "$FR_OUT" | tail -20; echo "FAIL R2ANGLE_run: script exited non-zero"; fail=1; FR_OUT=""; }
fi
# fr_chk TAG "x1 x2 ..." MESSAGE: the points baked with Y = TAG have exactly these X values, in this order
fr_chk() {
  local got
  got="$({ grep -oE "  [-0-9.e+]+,$1,0\$" <<<"$FR_OUT" || true; } | sed -E 's/^ +//; s/,[^,]*,0$//' | tr '\n' ' ' | sed -E 's/ $//')"
  if [ "$got" = "$2" ]; then echo "ok   R2ANGLE_$1: $3 (measured '$got')"; else echo "FAIL R2ANGLE_$1: $3 (measured '$got', expected '$2')"; fail=1; fi
}
if grep -qF "GrasshopperPlayer: solved 45 node(s)" <<<"$FR_OUT"; then echo "ok   R2ANGLE_solve: the player solved all 45 nodes (every node type is registered)"; else echo "FAIL R2ANGLE_solve: expected 'solved 45 node(s)'"; fail=1; fi
if grep -qE "^ok +expect_objects 9 " <<<"$FR_OUT"; then echo "ok   R2ANGLE_objects: exactly 9 point(s) baked"; else echo "FAIL R2ANGLE_objects: expected 9 baked objects"; fail=1; fi
fr_chk 1 "0" "pi rad = 180 deg"
fr_chk 2 "0" "pi/2 rad = 90 deg"
fr_chk 3 "0" "-pi/4 rad = -45 deg"
fr_chk 4 "0" "0 rad = 0 deg"
fr_chk 5 "0" "180 deg = pi rad"
fr_chk 6 "0" "90 deg = pi/2 rad"
fr_chk 7 "0" "45 deg = pi/4 rad"
fr_chk 8 "0" "-360 deg = -2 pi rad"
fr_chk 99 "-500000000000" "control: the residual chain reports Square(2) - 4.5 as -500000000000, so the 0 values above are real matches"
