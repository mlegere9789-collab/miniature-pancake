# ---------------------------------------------------------------------------
# LCL_: Loft Closed=Yes (lcl_script.txt) - the kernel's closed Brep::Loft (a periodic interpolation from the last curve back to the
# first) from the app. Eight unit circles in the eight radial planes of a ring of radius 5 sample a torus; they are rotated copies of
# one circle, so their seams already correspond. The closed cases run with the DEFAULT options: the closed loft re-seams each curve
# to the start point that minimizes the point-for-point twist against the previous one (the old nearest-point rule twisted the
# correspondence round a ring: the closed cubic loft read 185.1, not 197.4, the closed straight one 166.3, not 185.1, and the mesh had
# vertices 1.2 off the tube). Oracles computed outside the program: the closed cubic loft is within about 0.3 percent of the torus
# area 4 pi^2 R r = 197.392; the closed Straight loft is 8 equal ruled strips, 23.1386 each by a midpoint rule over the unit circle
# (400 x 40 cells), 185.109 in all; SeamAlign=No (sections as given) must give the same figures. The same circles lofted WITHOUT Closed
# (SeamAlign=No: the open default re-seam is not changed here) keep 7 of the 8 gaps and, being closed planar curves, get two end caps
# (2 pi = 6.283 in all): 7/8 x 197.392 + 6.283 = 179.02; the History edit of the straight loft to Closed=No is 7 strips + the two
# caps = 161.97 + 6.283 = 168.25. The app's Area is the tessellated figure; every window allows 1.5 percent or more.
# Two curves cannot be lofted on back to the first: refused with the reason, nothing built. Here-strings only.
# NOT checked here: Closed=Yes on OPEN curves, sections whose seams are far from corresponding, the Continuity= / tangent
# combinations (refused with a message).
# ---------------------------------------------------------------------------
lcl_ok() { echo "ok   $1"; }
lcl_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LCL_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/lcl_script.txt" 2>&1)" || { echo "$LCL_OUT" | tail -40; echo "FAIL LCL_run: script exited non-zero"; fail=1; LCL_OUT=""; }
else
  LCL_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/lcl_script.txt" 2>&1)" || { echo "$LCL_OUT" | tail -40; echo "FAIL LCL_run: script exited non-zero"; fail=1; LCL_OUT=""; }
fi
lcl_block() { awk -v name="$1" '$0 ~ ("Unknown command: LCLBEGIN_" name "$") { on = 1; next } on && /Unknown command: LCLBEGIN_/ { exit } on { print }' <<<"$LCL_OUT"; }
lcl_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
lcl_has() { if grep -qF -- "$2" <<<"$1"; then lcl_ok "$3"; else lcl_bad "$3"; near "$1" "$2"; fi; }
lcl_hasnot() { if grep -qF -- "$2" <<<"$1"; then lcl_bad "$3"; near "$1" "$2"; else lcl_ok "$3"; fi; }
# lcl_area BLOCK N LO HI DESC: the N-th "Area = " line of the block lies in [LO, HI]
lcl_area() {
  local v
  v="$({ grep -oE -- "Area = [0-9.e+-]+" <<<"$1" || true; } | sed -n "${2}p" | grep -oE '[-0-9.e+]+$' || true)"
  if lcl_in "$v" "$3" "$4"; then lcl_ok "$5 (Area $v in $3..$4)"; else lcl_bad "$5 (Area '${v:-none}' not in $3..$4)"; fi
}
LCL_C="$(lcl_block closed)"; LCL_O="$(lcl_block open)"; LCL_S="$(lcl_block straight)"; LCL_U="$(lcl_block uniform)"; LCL_A="$(lcl_block noalign)"; LCL_2="$(lcl_block two)"; LCL_H="$(lcl_block history)"
if grep -q "^FAIL" <<<"$LCL_OUT"; then lcl_bad "LCL_script: an @expect line of the script failed"; else lcl_ok "LCL_script: ran"; fi
# --- closed cubic loft
lcl_has "$LCL_C" "exact interpolating kernel loft through 8 curves (degree 3 in the loft direction); every section reproduced" "LCL_closed: the exact kernel loft through the eight circles"
lcl_has "$LCL_C" "Closed=Yes: lofted on from the last curve back to the first (periodic in the loft direction, closed surface, no ends and no caps)" "LCL_closed: ...built as a closed periodic loft"
lcl_hasnot "$LCL_C" "older control-point fit" "LCL_closed: ...never the old control-point fit"
lcl_has "$LCL_C" "Object 9" "LCL_closed: one new object"
lcl_area "$LCL_C" 1 194.4 200.4 "LCL_closed: area = torus 4 pi^2 R r = 197.392"
# --- control: without Closed the gap between the last and the first circle stays open
lcl_hasnot "$LCL_O" "Closed=Yes" "LCL_open_control: no Closed text without the option"
lcl_area "$LCL_O" 1 175 183 "LCL_open_control: 7 of the 8 gaps (7/8 of the torus = 172.74) plus the two end caps (2 pi = 6.28) = 179.02"
# --- closed straight
lcl_has "$LCL_S" "(degree 1 in the loft direction)" "LCL_straight: degree 1 in the loft direction"
lcl_has "$LCL_S" "Closed=Yes: lofted on" "LCL_straight: closed"
lcl_area "$LCL_S" 1 182.3 187.9 "LCL_straight: eight ruled strips = 185.109 (midpoint rule outside the program)"
# --- closed with uniform stations
lcl_has "$LCL_U" "Spacing=Uniform station parameters" "LCL_uniform: the spacing is named"
lcl_has "$LCL_U" "Closed=Yes: lofted on" "LCL_uniform: closed"
lcl_area "$LCL_U" 1 194.4 200.4 "LCL_uniform: area = torus 197.392 (by the symmetry of the eight sections uniform and chord stations coincide)"
# --- control: SeamAlign=No on the same ring gives the same torus (the sections are used exactly as given)
lcl_area "$LCL_A" 1 194.4 200.4 "LCL_noalign: SeamAlign=No = torus 197.392, the figure the default must reproduce"
lcl_hasnot "$LCL_A" "seam(s) moved" "LCL_noalign: ...no seam moved"
lcl_hasnot "$LCL_C" "does not meet the first" "LCL_closed: the default's last seam meets the first (no twist note)"
lcl_hasnot "$LCL_S" "does not meet the first" "LCL_straight: ...no twist note either"
# --- refusal with two curves
lcl_has "$LCL_2" "Closed=Yes needs at least 3 curves" "LCL_two: two curves are refused WITH the reason"
lcl_has "$LCL_2" "2 selected; nothing was built" "LCL_two: ...naming the count"
if [ "$(grep -cE "^history: Object [0-9]+ \(" <<<"$LCL_2")" = 2 ]; then lcl_ok "LCL_two: nothing was built (the two circles only)"; else lcl_bad "LCL_two: nothing was built (the two circles only)"; fi
# --- History: Closed is recorded and editable
lcl_area "$LCL_H" 1 194.4 200.4 "LCL_history: the recorded closed loft = torus 197.392"
lcl_area "$LCL_H" 2 182.3 187.9 "LCL_history: EditHistory style=straight -> eight ruled strips 185.109"
lcl_area "$LCL_H" 3 165 171 "LCL_history: EditHistory closed=no -> seven of the eight strips (161.97) plus the two end caps (2 pi) = 168.25"
lcl_hasnot "$LCL_H" "Loft: exact kernel loft not used" "LCL_history: the rebuilds stay on the exact kernel loft"
if grep -q "gl_error=0" <<<"$LCL_OUT"; then lcl_ok "LCL script ran without OpenGL errors"; else lcl_bad "LCL script ran without OpenGL errors"; fi
