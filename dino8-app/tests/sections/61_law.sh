# ---------------------------------------------------------------------------
# LAW_: FilletEdge with a NON-LINEAR radius law (Law= / LawTol=; commands/cmd_fillet.cpp -> kernel::FilletConvexEdgeLaw).
# One real app process runs $TMPW/law_script.txt, written below. Every box is 10 x 10 in plan; the picked edge is the
# vertical corner edge at (10,10), so the law runs along its length.
#
# Oracles:
#  * Removed volume = (box volume) - (the app's own Volume of the filleted body). The expected removed volumes are an
#    independent union-of-balls integral over the true law (the ball family {centre (x, r(x), r(x)), radius r(x)}, exact
#    in the covered interval per (x,u) column, midpoint rule in x and u; 120 x 200 columns; kernel-test algorithm
#    blend_law_tests::UncoveredVolume with the analytic law instead of the sampled stations), computed once by
#    scratchpad program law_oracle.cpp and written down below:
#        1 + 2 s^2                     6.5458
#        1 + 2 (3 s^2 - 2 s^3)         9.4820
#        PCHIP through 0:1, 0.3:1.5, 1:3   8.5148        (s = position along the 10 mm edge / 10)
#    (the same program printed the kernel's own removed volume for the sampled bodies at LawTol 0.003: 6.5551, 9.4722,
#    8.5262, i.e. within 0.15% of these.)  The app prints Volume with 4 significant digits (993.4 for a 1000 box), so
#    the removed volume carries a +-0.05 rounding band plus the mesh chord error: the check allows 0.08.
#  * Negative controls: the box stays exactly box-sized (Volume 1000 / 6000 = w*d*h) and the message names the reason.
law_ok() { echo "ok   $1"; }
law_bad() { echo "FAIL $1"; fail=1; }
law_exec() {  # law_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1
  fi
}
# law_case <name> <box x0> <box y0> <box z-height> <FilletEdge option lines, \n separated> <pick x,y,z>
law_case() {
  local x0="$1" h="$3" opts="$4" pick="$5"
  printf 'Box %s,0,0 %s,10,0 %s\nFilletEdge\n%b\n%s\nEnter\nSelAll\nVolume\nSelAll\nDelete\n' "$x0" "$((x0 + 10))" "$h" "$opts" "$pick"
}
# law_removed_ok <description> <box volume> <app volume text> <expected removed volume>
law_removed_ok() {
  local what="$1" box="$2" vol="$3" want="$4"
  if awk -v b="$box" -v v="$vol" -v w="$want" 'BEGIN { r = b - v; d = r - w; if (d < 0) d = -d; exit !(v > 0 && d <= 0.08) }'; then
    law_ok "$what: removed volume $(awk -v b="$box" -v v="$vol" 'BEGIN { printf "%.3f", b - v }') matches the union-of-balls integral $want (App Volume $vol of $box)"
  else
    law_bad "$what: App Volume $vol of a $box box removes $(awk -v b="$box" -v v="$vol" 'BEGIN { printf "%.3f", b - v }'), expected $want +- 0.08"
  fi
}

{
  law_case 0 0 10 'Radii=0:1,1:3\nLaw=quadratic\nLawTol=0.003' '10,10,5'      # 1
  law_case 0 0 10 'Radii=0:1,1:3\nLaw=smooth\nLawTol=0.003' '10,10,5'         # 2
  law_case 0 0 10 'Law=0:1,0.3:1.5,1:3\nLawTol=0.003' '10,10,5'               # 3
  law_case 0 0 10 'Radii=0:1,1:3\nLaw=sqrt' '10,10,5'                         # 4 refused: steep start
  law_case 0 0 10 'Law=0:1,0.5:3,1:1.5' '10,10,5'                             # 5 refused: not monotone
  law_case 0 0 60 'Radii=0:6,1:12\nLaw=smooth' '10,10,30'                     # 6 refused: radius past the face width
  law_case 1500000 0 10 'Radii=0:1,1:3\nLaw=quadratic' '1500010,10,5'         # 7 far from the origin: must build
} > "$TMPW/law_script.txt"
LAW_OUT="$(law_exec "$TMPW/law_script.txt" 2>&1 || true)"
mapfile -t LAW_MSG < <(grep -E '^history: ! ?FilletEdge:|^history: FilletEdge:' <<<"$LAW_OUT" | sed 's/^history: //')
mapfile -t LAW_VOL < <(grep -oE '^history: Volume = [-0-9.e+]+' <<<"$LAW_OUT" | sed 's/^history: Volume = //')

if [ "${#LAW_MSG[@]}" = 7 ]; then law_ok "LAW: the script ran 7 FilletEdge commands and printed 7 results"; else law_bad "LAW: expected 7 FilletEdge result lines, got ${#LAW_MSG[@]}"; echo "$LAW_OUT" | grep -n 'FilletEdge' | head -12 | sed 's/^/     /'; fi
# (case 7's Volume is not used: the app meshes in float32, so a body 1.5e6 from the origin has no usable volume)
if [ "${#LAW_VOL[@]}" = 7 ]; then law_ok "LAW: seven Volume readings (cases 1-6 are checked, 7 is the far-from-origin build check)"; else law_bad "LAW: expected 7 Volume lines, got ${#LAW_VOL[@]}"; fi

for i in 0 1 2; do
  names=(quadratic smooth samples); wants=(6.5458 9.4820 8.5148)
  if [[ "${LAW_MSG[$i]:-}" == "FilletEdge: edge "*"replaced with an exact convex non-linear-law fillet"* ]]; then
    law_ok "LAW ${names[$i]}: the fillet was applied as an exact convex non-linear-law fillet"
    seg="${LAW_MSG[$i]#*sampled into }"; seg="${seg%% linear*}"
    if [ "$seg" -ge 4 ] 2>/dev/null; then law_ok "LAW ${names[$i]}: the law was sampled into $seg linear segments (a non-linear law needs many)"; else law_bad "LAW ${names[$i]}: segment count '$seg' (expected >= 4)"; fi
  else
    law_bad "LAW ${names[$i]}: expected the fillet to be applied, got '${LAW_MSG[$i]:-<none>}'"
  fi
  law_removed_ok "LAW ${names[$i]} on the 10-wide box" 1000 "${LAW_VOL[$i]:-0}" "${wants[$i]}"
done
# the three laws remove clearly different volumes (a law-independent result could not satisfy all three checks above)
if awk -v a="${LAW_VOL[0]:-0}" -v b="${LAW_VOL[1]:-0}" -v c="${LAW_VOL[2]:-0}" 'BEGIN { exit !(a > 0 && b > 0 && c > 0 && (1000 - b) - (1000 - c) > 0.5 && (1000 - c) - (1000 - a) > 0.5) }'; then
  law_ok "LAW: removed volumes order quadratic (< 7) < sampled (< 9) < smooth as the radii do (Volumes ${LAW_VOL[0]} / ${LAW_VOL[2]} / ${LAW_VOL[1]})"
else
  law_bad "LAW: the three laws should remove clearly different volumes, got ${LAW_VOL[0]:-?} / ${LAW_VOL[1]:-?} / ${LAW_VOL[2]:-?}"
fi

# refusals: the real kernel reason is printed and the box is untouched
law_refused() {  # <index> <expected substring 1> <expected substring 2> <box volume> <description>
  local m="${LAW_MSG[$1]:-}" v="${LAW_VOL[$1]:-}"
  if [[ "$m" == "! FilletEdge: "* ]]; then
    if grep -qF -- "$2" <<<"$m" && grep -qF -- "$3" <<<"$m"; then law_ok "$5: refused with '$2' / '$3'"; else law_bad "$5: refusal text lacks '$2' / '$3': $m"; fi
  else
    law_bad "$5: expected a refusal, got '$m'"
  fi
  if [ "$v" = "$4" ]; then law_ok "$5: the box is untouched (Volume $v)"; else law_bad "$5: Volume $v, expected the untouched $4"; fi
}
law_refused 3 "too steep" "slope dr/dt" 1000 "LAW sqrt (vertical tangent at the start)"
law_refused 4 "not strictly monotone" "interior radius extremum" 1000 "LAW non-monotone samples 0:1,0.5:3,1:1.5"
law_refused 5 "radius too large for this solid" "contact line leaves the adjacent face" 6000 "LAW radius 6 -> 12 on a 10-wide box"

# far from the origin (the 1.5e6 offset the notch-tolerance bug appeared at): the fillet must build
if [[ "${LAW_MSG[6]:-}" == "FilletEdge: edge "*"replaced with an exact convex non-linear-law fillet"* ]]; then
  law_ok "LAW quadratic at x = 1.5e6: the fillet was applied (no cap-notch refusal)"
else
  law_bad "LAW quadratic at x = 1.5e6: got '${LAW_MSG[6]:-<none>}'"
fi
if grep -qF "cap notch point does not lie" <<<"$LAW_OUT"; then law_bad "LAW: a 'cap notch point' error was printed"; else law_ok "LAW: no 'cap notch point' error anywhere in the output"; fi
