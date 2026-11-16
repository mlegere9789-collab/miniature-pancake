# ---------------------------------------------------------------------------
# CDREG_: Regression guard for the commands the CD* sections changed (Lines, Circle, Polygon, Extend, Trim, Chamfer, Move, Copy, Offset, ArrayPolar): the existing create_script.txt and edit_script.txt still run to the end with no FAIL line
# These two scripts are also part of the BASE section; BASE stops early at an unrelated failure (the surface-edit script), so they are run here on their own.
# ---------------------------------------------------------------------------
cdreg_run() {  # NAME SCRIPT FRAMES
  local out rc
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    out="$(timeout 900 "$BIN" --smoke "$3" --script "$HERE/$2" 2>&1)"; rc=$?
  else
    out="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke "$3" --script "$HERE/$2" 2>&1)"; rc=$?
  fi
  if [ "$rc" != "0" ]; then echo "$out" | tail -20; echo "FAIL CDREG_$1: $2 exited with status $rc"; fail=1; return; fi
  if grep -q '^FAIL' <<<"$out"; then grep '^FAIL' <<<"$out" | head -5; echo "FAIL CDREG_$1: $2 printed FAIL lines"; fail=1; return; fi
  echo "ok   CDREG_$1: $2 ran to the end with exit status 0 and no FAIL line ($(grep -c '' <<<"$out") output lines)"
}
cdreg_run create create_script.txt 150
cdreg_run edit edit_script.txt 150
