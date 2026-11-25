# SW2R: the app's Sweep2 between two CLOSED polygon rails (square rings) is the kernel's exact Brep::Sweep2 (sw2r_script.txt).
# Before: the kernel refused a closed rail pair with a corner, so the app printed "exact sweep not used - the kernel refused"
# and fell back to the scaled-frame fit, whose wall cuts the corners (flat ring 155.3 and lifted ring 185.5 square units).
# Expectations are closed forms of the geometry, never the app's own printout:
#   concentric squares +-5 and +-8 in z = 0, section joining the rails at the seam: the planar annulus, 16^2 - 10^2 = 156
#   outer square lifted to z = 2: four planar trapezoids (parallel sides 10 and 16, horizontal gap 3, rise 2),
#                                 area 4 * (10 + 16)/2 * sqrt(13) = 52 sqrt(13) = 187.4887
# Both with the seam at a corner and with the seam at the middle of a side. "Area" prints 4 significant digits, so the
# tolerance is 3e-4 relative. Negative controls: the lifted ring differs from the flat one (Area is not a constant), and the
# old fit's numbers (155.3 / 185.5) are outside the tolerance, so the check separates the exact sweep from the fallback.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SW2O="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sw2r_script.txt" 2>&1)" || { echo "$SW2O"; echo "FAIL: sw2r script exited non-zero"; exit 1; }
else
  SW2O="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sw2r_script.txt" 2>&1)" || { echo "$SW2O"; echo "FAIL: sw2r script exited non-zero"; exit 1; }
fi
sw2blk() { awk -v name="$1" '$0 ~ ("Unknown command: SW2RBEGIN_" name "$") { on = 1; next } on && /Unknown command: SW2RBEGIN_/ { exit } on { print }' <<<"$SW2O"; }
sw2_ok() { echo "ok   $1"; }
sw2_bad() { echo "FAIL $1"; fail=1; }
sw2has() {
  local blk; blk="$(sw2blk "$1")"
  if grep -qF -- "$2" <<<"$blk"; then sw2_ok "$3"; else sw2_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Sweep2|Area|!)" <<<"$blk" || true; } | head -4 | cut -c1-200 | sed 's/^/     actual:   /'; fi
}
sw2lacks() {
  local blk; blk="$(sw2blk "$1")"
  if grep -qF -- "$2" <<<"$blk"; then sw2_bad "$3"; echo "     unexpected: $2"; else sw2_ok "$3"; fi
}
sw2area() { { grep -oE "Area = [0-9.eE+-]+" <<<"$(sw2blk "$1")" || true; } | head -1 | sed -E 's/Area = //'; }
sw2approx() { # VALUE EXPECTED RELTOL DESC
  if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= r * e) }'; then sw2_ok "$4 (measured $1, expected $2 +-${3} rel)"; else sw2_bad "$4 (measured '${1:-none}', expected $2 +-${3} rel)"; fi
}
SW2_LIFT="$(awk 'BEGIN { print 52 * sqrt(13) }')"
for c in flat mid_flat; do
  sw2has $c "Sweep2: exact kernel sweep" "SW2R_$c: the kernel's exact Brep::Sweep2 built the ring (not the fallback fit)"
  sw2lacks $c "the kernel refused" "SW2R_$c: no kernel refusal reported"
  sw2approx "$(sw2area $c)" 156 0.0003 "SW2R_$c: wall area is the planar annulus 16^2 - 10^2"
done
for c in lift mid_lift; do
  sw2has $c "Sweep2: exact kernel sweep" "SW2R_$c: the kernel's exact Brep::Sweep2 built the lifted ring (not the fallback fit)"
  sw2lacks $c "the kernel refused" "SW2R_$c: no kernel refusal reported"
  sw2approx "$(sw2area $c)" "$SW2_LIFT" 0.0003 "SW2R_$c: wall area is four trapezoids, 52*sqrt(13)"
done
# negative controls
if awk -v a="$(sw2area flat)" -v b="$(sw2area lift)" 'BEGIN { exit !(a != "" && b != "" && b - a > 20) }'; then sw2_ok "SW2R: the lifted ring is measurably larger than the flat one (Area is not a constant)"; else sw2_bad "SW2R: lifted vs flat area not separated (flat '$(sw2area flat)', lifted '$(sw2area lift)')"; fi
if awk -v a="$(sw2area flat)" 'BEGIN { d = a - 155.3; if (d < 0) d = -d; exit !(a != "" && d > 0.3) }'; then sw2_ok "SW2R: the flat-ring area is not the old corner-cutting fit's 155.3"; else sw2_bad "SW2R: flat-ring area equals the old fit's 155.3 (measured '$(sw2area flat)')"; fi
