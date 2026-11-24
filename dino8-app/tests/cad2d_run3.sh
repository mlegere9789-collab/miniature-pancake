# Shared runner for the round-3 CD* sections (tests/sections/96_cd*.sh): builds the app script of one case with cad2d_rows3.py, runs the app on it,
# and lets cad2d_rows3.py check the exported DXF files against the hand-computed values (see that file's header).
cad2d3_case() {  # CASE
  local c="$1" d="$TMPW/cad2d3_$1" out rc
  mkdir -p "$d"
  if ! python3 "$HERE/cad2d_rows3.py" gen "$c" "$d"; then echo "FAIL CAD2D3_$c: could not write the app script"; fail=1; return; fi
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 30 --script "$d/$c.txt" > "$d/app.log" 2>&1 || { tail -20 "$d/app.log"; echo "FAIL CAD2D3_$c: the app script exited non-zero"; fail=1; return; }
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$d/$c.txt" > "$d/app.log" 2>&1 || { tail -20 "$d/app.log"; echo "FAIL CAD2D3_$c: the app script exited non-zero"; fail=1; return; }
  fi
  out="$(python3 "$HERE/cad2d_rows3.py" check "$c" "$d" "$d/app.log" 2>&1)" && rc=0 || rc=$?
  echo "$out"
  if [ "$rc" != "0" ] || grep -q '^FAIL' <<<"$out"; then fail=1; fi
}
