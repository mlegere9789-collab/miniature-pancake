# ASM_: STEP assembly import against an independent oracle (tests/asm_check.py writes the MAPPED_ITEM / REPRESENTATION_MAP file with
# its own text, the application only imports it): two instances of one box, one translated to (100,0,0), one rotated 90 degrees
# about z and placed at (0,50,0); the imported bounding boxes must be the closed-form ones.
ASMD="$TMPW/asm"
mkdir -p "$ASMD"
printf 'New\nBox 0,0,0 10,10,0 10\nSelAll\nExport %s/box.stp\n' "$ASMD" > "$ASMD/1.txt"
printf 'New\nImport %s/asm.stp\nSelNone\nSelID 1\nBoundingBox\nCancel\nSelNone\nSelID 2\nBoundingBox\nCancel\n' "$ASMD" > "$ASMD/2.txt"
asm_run() {
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 120 --script "$1" 2>&1 || true
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 120 --script "$1" 2>&1 || true
  fi
}
asm_run "$ASMD/1.txt" > /dev/null
python3 "$HERE/asm_check.py" gen "$ASMD" > /dev/null
asm_run "$ASMD/2.txt" > "$ASMD/app.out"
ASM_RES="$(python3 "$HERE/asm_check.py" check "$ASMD" "$ASMD/app.out" 2>&1)"
echo "$ASM_RES"
if grep -q '^FAIL' <<<"$ASM_RES"; then fail=1; fi
