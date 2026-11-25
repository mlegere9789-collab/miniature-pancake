# ---------------------------------------------------------------------------
# RVCH: Shell / OffsetSrf of a stepped shaft (two coaxial cylinders united) and the uniform closed hollow / Solid=Yes offset of a sphere
# dome (revchain_script.txt) - kernel::ShellRevolvedArcProfile / OffsetRevolvedArcProfile through a chained revolved-solid meridian.
# The shaft: lower cylinder r5 z 0..4, upper r3 z 4..8, V = 136 pi. Closed forms, read from the display mesh (ranges carry tessellation error):
#   top removed, wall 0.5          cavity r4.5 z .5..3.5 + r2.5 z 3.5..8        136 pi - 88.875 pi = 47.125 pi = 148.05
#   top removed, floor 2, wall 0.5 cavity r4.5 z 2..3.5 + r2.5 z 3.5..8         136 pi - 58.5 pi   = 77.5 pi   = 243.47
#   uniform closed hollow 0.5      inner = the -0.5 offset (r4.5 z .5..3.5, r2.5 z 3.5..7.5)  136 pi - 85.75 pi = 50.25 pi = 157.86
#   OffsetSrf +1                   r6 z -1..5 + r4 z 5..9                       280 pi = 879.65
#   OffsetSrf -0.5                 r4.5 z .5..3.5 + r2.5 z 3.5..7.5             85.75 pi = 269.39
#   OffsetSrf Solid=Yes +1         280 pi - 136 pi = 144 pi = 452.39
#   dome (sphere r5 centred z=-2, base z=0): closed hollow 1  36 pi - 11/3 pi = 32.333 pi = 101.58;  Solid=Yes +1  108.333 pi - 36 pi = 72.333 pi = 227.24
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RVCHOUT="$(timeout 1200 "$BIN" --smoke 60 --script "$HERE/revchain_script.txt" 2>&1)" || { echo "$RVCHOUT"; echo "FAIL: revchain script exited non-zero"; fail=1; RVCHOUT=""; }
else
  RVCHOUT="$(timeout 1200 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/revchain_script.txt" 2>&1)" || { echo "$RVCHOUT"; echo "FAIL: revchain script exited non-zero"; fail=1; RVCHOUT=""; }
fi
rvchblock() { awk -v name="$1" '$0 ~ ("Unknown command: RVCHBEGIN_" name "$") { on = 1; next } on && /Unknown command: RVCHBEGIN_/ { exit } on { print }' <<<"$RVCHOUT"; }
rvchcheck() { local blk; blk="$(rvchblock "$1")"; if grep -q -- "$2" <<<"$blk"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; { grep -E "^(history: )?(Shell|OffsetSrf|Object|Volume|!)" <<<"$blk" || true; } | head -8 | cut -c1-240 | sed 's/^/     actual:   /'; fail=1; fi; }
rvchvol() {
  local blk v; blk="$(rvchblock "$1")"
  v="$({ grep -E "^history: Volume = " <<<"$blk" || true; } | tail -1 | sed -E 's/^history: Volume = ([0-9.eE+-]+).*/\1/')"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $4 (Volume $v in [$2, $3])"; else echo "FAIL $4 (Volume '${v:-none}' not in [$2, $3])"; fail=1; fi
}
rvchcheck shell_top "Shell: thickness 0.5, open (1 face(s) removed), exact B-rep" "RVCH_shell_top: a stepped shaft with its top clicked off is an exact B-rep shell (before: mesh construction)"
rvchvol shell_top 146.3 149.3 "RVCH_shell_top: Volume = 47.125 pi = 148.05"
rvchcheck shell_top "Object [0-9]*: valid" "RVCH_shell_top: ...Check valid"
rvchcheck shell_vary "Shell: thickness 0.5, 1 face(s) at their own thickness, open (1 face(s) removed), exact B-rep" "RVCH_shell_vary: the shaft's floor at its own thickness 2 is exact"
rvchvol shell_vary 241 244.5 "RVCH_shell_vary: Volume = 77.5 pi = 243.47"
rvchcheck shell_vary "Object [0-9]*: valid" "RVCH_shell_vary: ...Check valid"
rvchcheck shell_closed "Shell: thickness 0.5, closed, exact B-rep" "RVCH_shell_closed: the uniform closed hollow of the shaft is exact (outer + reversed inner shell)"
rvchvol shell_closed 155.5 159 "RVCH_shell_closed: Volume = 50.25 pi = 157.86"
rvchcheck shell_closed "Object [0-9]*: valid" "RVCH_shell_closed: ...Check valid"
rvchcheck off_out "OffsetSrf: distance 1, 1 object(s), 1 exact B-rep" "RVCH_off_out: OffsetSrf of the shaft grown by 1 is an exact whole-body offset (before: mesh offset)"
rvchvol off_out 874 882 "RVCH_off_out: Volume = 280 pi = 879.65"
rvchcheck off_out "Object [0-9]*: valid" "RVCH_off_out: ...Check valid"
rvchcheck off_in "OffsetSrf: distance -0.5, 1 object(s), 1 exact B-rep" "RVCH_off_in: the shaft shrunk by 0.5 is exact"
rvchvol off_in 267 270.5 "RVCH_off_in: Volume = 85.75 pi = 269.39"
rvchcheck off_in "Object [0-9]*: valid" "RVCH_off_in: ...Check valid"
rvchcheck off_solid "OffsetSrf: distance 1, 1 object(s), solid, 1 exact B-rep" "RVCH_off_solid: Solid=Yes (the hollow wall) of the shaft is exact"
rvchvol off_solid 448 454 "RVCH_off_solid: Volume = 144 pi = 452.39"
rvchcheck off_solid "Object [0-9]*: valid" "RVCH_off_solid: ...Check valid"
rvchcheck off_collapse "exact B-rep offset not used (" "RVCH_off_collapse: shrinking the shaft past its upper radius is refused WITH a reason (negative control)"
rvchcheck dome_closed "Shell: thickness 1, closed, exact B-rep" "RVCH_dome_closed: the uniform closed hollow of a sphere dome is exact (before: mesh)"
rvchvol dome_closed 100 102.2 "RVCH_dome_closed: Volume = 32.333 pi = 101.58"
rvchcheck dome_closed "Object [0-9]*: valid" "RVCH_dome_closed: ...Check valid"
rvchcheck dome_offsolid "OffsetSrf: distance 1, 1 object(s), solid, 1 exact B-rep" "RVCH_dome_offsolid: Solid=Yes (the hollow wall) of a sphere dome is exact"
rvchvol dome_offsolid 224.5 228.3 "RVCH_dome_offsolid: Volume = 72.333 pi = 227.24"
rvchcheck dome_offsolid "Object [0-9]*: valid" "RVCH_dome_offsolid: ...Check valid"
