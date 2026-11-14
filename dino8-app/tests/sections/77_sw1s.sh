# SW1S: Sweep1 with Style=Roadlike / Up through the app command (kernel Brep::Sweep1 roadlike_up), sw1s_script.txt.
# Before: the app's Sweep1 had no Style or Up option at all; "Style=Roadlike" was an unknown command and every sweep used
# rotation-minimizing frames (RMF), which bank with the rail's torsion.
# Closed form (independent of the kernel's frames): along the helix (3 cos t, 3 sin t, t), t in [0, 2pi], a Roadlike sweep with Up = world Z
# keeps the section level: right = T x Z is the horizontal radial direction at every station, so the radial segment (3,0,0)-(5,0,0)
# sweeps the helicoid (rho cos t, rho sin t, t), rho in [3, 5], with |S_rho x S_t| = sqrt(1 + rho^2) and
#   area = 2pi * [ (rho sqrt(1+rho^2) + asinh(rho)) / 2 ] from 3 to 5 = 51.8434.
# The RMF sweep of the same section tilts out of the radial plane and gives a clearly different area (measured 45.17 for the
# default Freeform style), so the check separates the two styles. "Area" is a mesh measure printed to 4 digits and the helix is an
# interpolation (deviation 1.3e-4), so the tolerance is 0.5% relative.
# Negative controls: the Freeform sweep must NOT equal the helicoid; Up = the zero vector and a multi-section Roadlike sweep are
# refused with their reasons and build no surface (never silently an RMF sweep).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SW1O="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sw1s_script.txt" 2>&1)" || { echo "$SW1O"; echo "FAIL: sw1s script exited non-zero"; exit 1; }
else
  SW1O="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sw1s_script.txt" 2>&1)" || { echo "$SW1O"; echo "FAIL: sw1s script exited non-zero"; exit 1; }
fi
sw1blk() { awk -v name="$1" '$0 ~ ("Unknown command: SW1SBEGIN_" name "$") { on = 1; next } on && /Unknown command: SW1SBEGIN_/ { exit } on { print }' <<<"$SW1O"; }
sw1_ok() { echo "ok   $1"; }
sw1_bad() { echo "FAIL $1"; fail=1; }
sw1has() {
  local blk; blk="$(sw1blk "$1")"
  if grep -qF -- "$2" <<<"$blk"; then sw1_ok "$3"; else sw1_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Sweep1|Area|Style|Unknown|!)" <<<"$blk" || true; } | head -4 | cut -c1-200 | sed 's/^/     actual:   /'; fi
}
sw1lacks() {
  local blk; blk="$(sw1blk "$1")"
  if grep -qF -- "$2" <<<"$blk"; then sw1_bad "$3"; echo "     unexpected: $2"; else sw1_ok "$3"; fi
}
sw1area() { { grep -oE "Area = [0-9.eE+-]+" <<<"$(sw1blk "$1")" || true; } | head -1 | sed -E 's/Area = //'; }
sw1approx() { # VALUE EXPECTED RELTOL DESC
  if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= r * e) }'; then sw1_ok "$4 (measured $1, expected $2 +-${3} rel)"; else sw1_bad "$4 (measured '${1:-none}', expected $2 +-${3} rel)"; fi
}
SW1_HELICOID="$(awk 'BEGIN { pi = 3.14159265358979; f3 = (3 * sqrt(10) + log(3 + sqrt(10))) / 2; f5 = (5 * sqrt(26) + log(5 + sqrt(26))) / 2; print 2 * pi * (f5 - f3) }')"
sw1has road "Style=Roadlike" "SW1S_road: the Roadlike style was applied (kernel roadlike_up sweep)"
sw1approx "$(sw1area road)" "$SW1_HELICOID" 0.005 "SW1S_road: the wall is the closed-form helicoid (section kept radial and level)"
sw1approx "$(sw1area road_down)" "$SW1_HELICOID" 0.005 "SW1S_road_down: Up = -Z gives the same helicoid (the section flips with Up, so the surface is the same)"
sw1has free "rotation-minimizing frames" "SW1S_free: the default style is still the rotation-minimizing sweep"
if awk -v a="$(sw1area free)" -v h="$SW1_HELICOID" 'BEGIN { d = a - h; if (d < 0) d = -d; exit !(a != "" && d > 0.05 * h) }'; then sw1_ok "SW1S_free (negative control): the Freeform sweep is NOT the helicoid (measured $(sw1area free), helicoid $SW1_HELICOID)"; else sw1_bad "SW1S_free (negative control): the Freeform area equals the helicoid's (measured '$(sw1area free)')"; fi
sw1has bad_up "Up must be" "SW1S_bad_up: Up = 0,0,0 is refused with its reason"
sw1has bad_up "no surface created" "SW1S_bad_up: ...and builds no surface"
sw1lacks bad_up "history: surface" "SW1S_bad_up: ...no surface object exists afterwards"
sw1has two_sections "only applied to a single cross-section" "SW1S_two_sections: a multi-section Roadlike sweep is refused with its reason"
sw1lacks two_sections "history: surface" "SW1S_two_sections: ...and is not silently built with rotation-minimizing frames"
