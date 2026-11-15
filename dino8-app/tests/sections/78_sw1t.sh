# SW1T: Sweep1 Style=Top / Front / Right and Twist through the app command (sw1t_script.txt).
# Style=Top is Roadlike with Up = world Z (Front = Y, Right = X): the section is kept level against that direction instead of banking
# with the rail's torsion (rotation-minimizing frames, Style=Freeform). Twist=<degrees> is the kernel's twist_total, linear in arc length.
# Closed forms, independent of the kernel's frames:
#   Top on the helix (3 cos t, 3 sin t, t), t in [0, 2pi], radial section (3,0,0)-(5,0,0): the helicoid, area
#       2pi * [ (rho sqrt(1+rho^2) + asinh rho) / 2 ] from 3 to 5 = 51.8434   (0.5% tolerance: the "Area" mesh + a helix interpolation)
#   Twist on the straight rail (0,0,0)-(0,0,6), radial section (1,0,0)-(3,0,0), total twist phi: the helicoid
#       S = (rho cos(k z), rho sin(k z), z), k = phi / 6, area 6 / (2k) * [k rho sqrt(1+k^2 rho^2) + asinh(k rho)] from 1 to 3
#       = 17.5619 for phi = +-180 degrees; 12 for no twist (the 2 x 6 rectangle).
# The kernel's default straight-rail shortcut (2 stations) cuts the helicoid (area 6); the app asks for the adaptive, measured
# sweep when a Twist is given (tests/extra/sweep1_twist_straight_rail.cpp pins both).
# Consistency/negative controls: Style=Right equals Roadlike with Up=1,0,0 (same surface) and is not the Top helicoid; a bad Twist and
# a multi-section Twist are refused with their reasons and build nothing.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SW1TO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sw1t_script.txt" 2>&1)" || { echo "$SW1TO"; echo "FAIL: sw1t script exited non-zero"; exit 1; }
else
  SW1TO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sw1t_script.txt" 2>&1)" || { echo "$SW1TO"; echo "FAIL: sw1t script exited non-zero"; exit 1; }
fi
sw1tblk() { awk -v name="$1" '$0 ~ ("Unknown command: SW1TBEGIN_" name "$") { on = 1; next } on && /Unknown command: SW1TBEGIN_/ { exit } on { print }' <<<"$SW1TO"; }
sw1t_ok() { echo "ok   $1"; }
sw1t_bad() { echo "FAIL $1"; fail=1; }
sw1thas() {
  local blk; blk="$(sw1tblk "$1")"
  if grep -qF -- "$2" <<<"$blk"; then sw1t_ok "$3"; else sw1t_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Sweep1|Area|Style|Unknown|!)" <<<"$blk" || true; } | head -4 | cut -c1-200 | sed 's/^/     actual:   /'; fi
}
sw1tlacks() {
  local blk; blk="$(sw1tblk "$1")"
  if grep -qF -- "$2" <<<"$blk"; then sw1t_bad "$3"; echo "     unexpected: $2"; else sw1t_ok "$3"; fi
}
sw1tarea() { { grep -oE "Area = [0-9.eE+-]+" <<<"$(sw1tblk "$1")" || true; } | head -1 | sed -E 's/Area = //'; }
sw1tapprox() { # VALUE EXPECTED RELTOL DESC
  if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= r * e) }'; then sw1t_ok "$4 (measured $1, expected $2 +-${3} rel)"; else sw1t_bad "$4 (measured '${1:-none}', expected $2 +-${3} rel)"; fi
}
SW1T_HELICOID="$(awk 'BEGIN { pi = 3.14159265358979; f3 = (3 * sqrt(10) + log(3 + sqrt(10))) / 2; f5 = (5 * sqrt(26) + log(5 + sqrt(26))) / 2; print 2 * pi * (f5 - f3) }')"
SW1T_TWIST="$(awk 'BEGIN { pi = 3.14159265358979; k = pi / 6; f1 = (k * 1 * sqrt(1 + k * k) + log(k + sqrt(1 + k * k))) / (2 * k); f3 = (k * 3 * sqrt(1 + 9 * k * k) + log(3 * k + sqrt(1 + 9 * k * k))) / (2 * k); print 6 * (f3 - f1) }')"
sw1thas top "Style=Roadlike" "SW1T_top: Style=Top is the kernel's roadlike_up sweep (Up = world Z)"
sw1tapprox "$(sw1tarea top)" "$SW1T_HELICOID" 0.005 "SW1T_top: the wall is the closed-form helicoid"
sw1thas right "Up=1,0,0" "SW1T_right: Style=Right uses Up = world X"
sw1tapprox "$(sw1tarea right)" "$(sw1tarea road_x)" 0.001 "SW1T_right: the same surface as Style=Roadlike with Up=1,0,0 (area equal to 0.1%)"
if awk -v r="$(sw1tarea right)" -v h="$SW1T_HELICOID" 'BEGIN { d = r - h; if (d < 0) d = -d; exit !(r != "" && d > 0.05 * h) }'; then sw1t_ok "SW1T_right (negative control): Up = X is NOT the Up = Z helicoid (measured $(sw1tarea right), helicoid $SW1T_HELICOID)"; else sw1t_bad "SW1T_right (negative control): the Up = X sweep equals the Up = Z helicoid ('$(sw1tarea right)')"; fi
sw1thas twist_pos "exact kernel sweep with Twist" "SW1T_twist_pos: the Twist option was applied by the kernel sweep"
sw1tapprox "$(sw1tarea twist_pos)" "$SW1T_TWIST" 0.005 "SW1T_twist_pos: the 180 degree twisted wall is the closed-form helicoid, not the cut-corner bowtie (area 6)"
sw1tapprox "$(sw1tarea twist_neg)" "$SW1T_TWIST" 0.005 "SW1T_twist_neg: -180 degrees gives the same area (mirror image)"
sw1tapprox "$(sw1tarea twist_zero)" 12 0.001 "SW1T_twist_zero (control): no twist is the 2 x 6 rectangle"
sw1thas twist_bad "Twist must be a finite number of degrees" "SW1T_twist_bad: Twist=abc is refused with its reason"
sw1thas twist_bad "no surface created" "SW1T_twist_bad: ...and builds no surface"
sw1tlacks twist_bad "history: surface" "SW1T_twist_bad: ...no surface object exists afterwards"
sw1thas twist_two_sections "only applied to a single cross-section" "SW1T_twist_two_sections: a multi-section Twist sweep is refused with its reason"
sw1tlacks twist_two_sections "history: surface" "SW1T_twist_two_sections: ...and is not silently built without the Twist"
