# ---------------------------------------------------------------------------
# CHH: closed hollow with a per-face thickness of revolved solids (chhshell_script.txt) - kernel::ShellClosedRevolvedArcProfile.
# Closed forms (kernel-measured Brep::Volume in parentheses), display-mesh ranges allow ~1.5% tessellation error:
#   cylinder r5 h10, floor 2 / top 1 / wall 0.5   pi (250 - 4.5^2 * 7)            = 340.0774
#   dome (r5, centre z=-2), base 0.5 / sphere 1    cap(5,3) - cap(4,1.5)           = 88.3573
#   stepped shaft, lower wall 1.0, rest 0.5        136 pi - 73 pi = 63 pi          = 197.9203
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CHHOUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/chhshell_script.txt" 2>&1)" || { echo "$CHHOUT"; echo "FAIL: chhshell script exited non-zero"; fail=1; CHHOUT=""; }
else
  CHHOUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/chhshell_script.txt" 2>&1)" || { echo "$CHHOUT"; echo "FAIL: chhshell script exited non-zero"; fail=1; CHHOUT=""; }
fi
chhblock() { awk -v name="$1" '$0 ~ ("Unknown command: CHHBEGIN_" name "$") { on = 1; next } on && /Unknown command: CHHBEGIN_/ { exit } on { print }' <<<"$CHHOUT"; }
chhcheck() { local blk; blk="$(chhblock "$1")"; if grep -q -- "$2" <<<"$blk"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; { grep -E "^(history: )?(Shell|Object|Volume|!)" <<<"$blk" || true; } | head -8 | cut -c1-240 | sed 's/^/     actual:   /'; fail=1; fi; }
chhvol() {
  local blk v; blk="$(chhblock "$1")"
  v="$({ grep -E "^history: Volume = " <<<"$blk" || true; } | tail -1 | sed -E 's/^history: Volume = ([0-9.eE+-]+).*/\1/')"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $4 (Volume $v in [$2, $3])"; else echo "FAIL $4 (Volume '${v:-none}' not in [$2, $3])"; fail=1; fi
}
chhcheck cyl "closed, exact B-rep" "CHH_cyl: a cylinder's closed hollow with floor 2 / top 1 / wall 0.5 is an exact B-rep (before: refused, mesh)"
chhvol cyl 334.5 341.5 "CHH_cyl: Volume = 340.0774"
chhcheck cyl "Object [0-9]*: valid" "CHH_cyl: ...Check valid"
chhcheck dome "closed, exact B-rep" "CHH_dome: a dome's closed hollow with base 0.5 / sphere 1 is exact"
chhvol dome 86.8 89 "CHH_dome: Volume = 88.3573"
chhcheck dome "Object [0-9]*: valid" "CHH_dome: ...Check valid"
chhcheck shaft "closed, exact B-rep" "CHH_shaft: a stepped shaft's closed hollow with a 1.0 lower wall is exact"
chhvol shaft 194.5 198.5 "CHH_shaft: Volume = 197.9203"
chhcheck shaft "Object [0-9]*: valid" "CHH_shaft: ...Check valid"
