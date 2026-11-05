# ---------------------------------------------------------------------------
# RVSH: exact open / variable-thickness Shell of a plain revolved solid (revshell_script.txt) - kernel::ShellRevolvedProfile.
# Expected volumes are closed forms, read from the display mesh so the ranges carry tessellation error:
#   cylinder r5 h10, top removed, floor 2, wall 0.5   pi*(250 - 4.5^2*8)                  = 276.46
#   cylinder with both caps removed, wall 1          pi*10*(25 - 16)                      = 282.74
#   cylinder with wall and top removed, floor 1      pi*25*1                              = 78.54
#   frustum r5->r3 h10, top removed, floor 2, wall 0.5 normal   outer frustum 513.13 - cavity frustum 277.42 = 235.71
#   cone r5 h10, cap removed, wall 0.5               pi/3*250 - pi*(10 - 0.5*sqrt5)^3/12   = 78.36
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RVSHOUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/revshell_script.txt" 2>&1)" || { echo "$RVSHOUT"; echo "FAIL: revshell script exited non-zero"; fail=1; RVSHOUT=""; }
else
  RVSHOUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/revshell_script.txt" 2>&1)" || { echo "$RVSHOUT"; echo "FAIL: revshell script exited non-zero"; fail=1; RVSHOUT=""; }
fi
rvshblock() { awk -v name="$1" '$0 ~ ("Unknown command: RVSHBEGIN_" name "$") { on = 1; next } on && /Unknown command: RVSHBEGIN_/ { exit } on { print }' <<<"$RVSHOUT"; }
rvshcheck() { local blk; blk="$(rvshblock "$1")"; if grep -q -- "$2" <<<"$blk"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; { grep -E "^(history: )?(Shell|Object|Volume)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fail=1; fi; }
rvshvol() {
  local blk v; blk="$(rvshblock "$1")"
  v="$({ grep -E "^history: Volume = " <<<"$blk" || true; } | tail -1 | sed -E 's/^history: Volume = ([0-9.eE+-]+).*/\1/')"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $4 (Volume $v in [$2, $3])"; else echo "FAIL $4 (Volume '${v:-none}' not in [$2, $3])"; fail=1; fi
}
rvshcheck cylvary "Shell: thickness 0.5, 1 face(s) at their own thickness, open (1 face(s) removed), exact B-rep" "RVSH_cylvary: a cylinder with its top removed and a 2-thick floor is an exact B-rep (before: mesh, 'no exact construction')"
rvshvol cylvary 274 278.5 "RVSH_cylvary: Volume = pi*(250 - 4.5^2*8) = 276.46"
rvshcheck cylvary "Object [0-9]*: valid" "RVSH_cylvary: ...Check valid"
rvshcheck cyltube "open (2 face(s) removed), exact B-rep" "RVSH_cyltube: both caps removed is an exact tube (before: 'not several faces')"
rvshvol cyltube 280.5 284.5 "RVSH_cyltube: Volume = pi*10*9 = 282.74"
rvshcheck cyltube "Object [0-9]*: valid" "RVSH_cyltube: ...Check valid"
rvshcheck cylplate "open (2 face(s) removed), exact B-rep" "RVSH_cylplate: wall and top removed leaves an exact floor plate"
rvshvol cylplate 77.5 79.5 "RVSH_cylplate: Volume = pi*25 = 78.54"
rvshcheck tconevary "Shell: thickness 0.5, 1 face(s) at their own thickness, open (1 face(s) removed), exact B-rep" "RVSH_tconevary: a frustum cup with a 2-thick floor and a 0.5 normal cone wall is exact"
rvshvol tconevary 233.5 238 "RVSH_tconevary: Volume = 513.13 - 277.42 = 235.71 (outer frustum minus the cavity frustum)"
rvshcheck tconevary "Object [0-9]*: valid" "RVSH_tconevary: ...Check valid"
rvshcheck conewall "open (1 face(s) removed), exact B-rep" "RVSH_conewall: a cone cup with a per-face wall thickness is exact"
rvshvol conewall 77.5 79.2 "RVSH_conewall: Volume = 78.36 (Pappus-free closed form: outer cone minus the offset cone)"
rvshcheck closedvary "exact B-rep shell not used (.*at least one face must be removed" "RVSH_closedvary: a per-face thickness with nothing removed is refused WITH the kernel's reason (negative control)"
rvshcheck thickfloor "exact B-rep shell not used (.*" "RVSH_thickfloor: a floor as thick as the whole cylinder is refused WITH a reason (negative control)"
