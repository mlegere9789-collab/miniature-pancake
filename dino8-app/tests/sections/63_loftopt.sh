# ---------------------------------------------------------------------------
# LOFTOPT_: Loft SeamAlign= and StartTangent= / EndTangent= / StartWeight= / EndWeight= (loftopt_script.txt) - the kernel's
# Brep::Loft seam handling and Brep::LoftWithSectionTangents from the app. Oracles computed outside the program: the cylinder
# (area 2 pi r h = 62.832); the twisted ruled wall between a circle and the same circle turned 90 degrees (area 52.839, numeric
# integral of |S_t x S_v| over the wall); the Hermite barrel (volume 7.981207 = pi * integral R(v)^2 z'(v) dv, Simpson on the
# closed-form Hermite polynomials). Negative controls: unknown tangent name, a tangent that cannot be matched, a bad weight.
# NOT checked here: History replay of these options (a tangent loft is not recorded), tangents on more than the first / last curve.
# ---------------------------------------------------------------------------
lop_ok() { echo "ok   $1"; }
lop_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LOP_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/loftopt_script.txt" 2>&1)" || { echo "$LOP_OUT" | tail -40; echo "FAIL LOP_run: script exited non-zero"; fail=1; LOP_OUT=""; }
else
  LOP_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/loftopt_script.txt" 2>&1)" || { echo "$LOP_OUT" | tail -40; echo "FAIL LOP_run: script exited non-zero"; fail=1; LOP_OUT=""; }
fi
if grep -q "^FAIL" <<<"$LOP_OUT"; then lop_bad "LOP_script: an @expect line of the script failed"; else lop_ok "LOP_script: every @expect_objects line held"; fi
lop_case() { awk -v n="$1" '/Command: Loft( |$)/ { c++ } c == n' <<<"$LOP_OUT"; }
lop_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
lop_has() { if grep -qF -- "$2" <<<"$1"; then lop_ok "$3"; else lop_bad "$3"; near "$1" "$2"; fi; }
lop_hasnot() { if grep -qF -- "$2" <<<"$1"; then lop_bad "$3"; near "$1" "$2"; else lop_ok "$3"; fi; }
# the n-th occurrence of "<prefix><number>" in $1 must lie in [lo, hi]
lop_nth() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | sed -n "${3}p" | grep -oE '[-0-9.e+]+$' || true)"
  if lop_in "$v" "$4" "$5"; then lop_ok "$6 ($v in $4..$5)"; else lop_bad "$6 ('$v' not in $4..$5)"; fi
}
lop_num() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | head -1 | grep -oE '[-0-9.e+]+$' || true)"
  if lop_in "$v" "$3" "$4"; then lop_ok "$5 ($v in $3..$4)"; else lop_bad "$5 ('$v' not in $3..$4)"; fi
}
LOP_1="$(lop_case 1)"; LOP_2="$(lop_case 2)"; LOP_3="$(lop_case 3)"; LOP_4="$(lop_case 4)"; LOP_5="$(lop_case 5)"; LOP_6="$(lop_case 6)"; LOP_7="$(lop_case 7)"; LOP_8="$(lop_case 8)"; LOP_9="$(lop_case 9)"
lop_has "$LOP_1" "1 seam(s) moved" "LOP_1: the default SeamAlign=Yes re-seams the turned circle"
lop_num "$LOP_1" "Area = " 62.5 63.2 "LOP_1: aligned, the wall is the exact cylinder: area 2 pi 2 5 = 62.832"
lop_hasnot "$LOP_2" "aligned first" "LOP_2: SeamAlign=No moves no seam"
lop_has "$LOP_2" "SeamAlign=No" "LOP_2: ...and the message says the sections were used as given"
lop_num "$LOP_2" "Area = " 52.2 53.5 "LOP_2: the twisted wall has the numeric-integral area 52.839 (not the cylinder's 62.832)"
lop_has "$LOP_3" "tangent loft through 2 curves" "LOP_3: StartTangent / EndTangent build the exact kernel tangent loft through two curves"
lop_has "$LOP_3" "end caps added, closed solid" "LOP_3: ...a capped closed solid"
lop_num "$LOP_3" "exact volume " 7.98115 7.98125 "LOP_3: kernel exact volume = pi * integral R(v)^2 z'(v) dv = 7.981207 (Hermite barrel)"
lop_num "$LOP_3" "Volume = " 7.90 8.06 "LOP_3: the Volume command's tessellated figure agrees (cross-check, 1 percent)"
lop_has "$LOP_4" "no curve object is named 'NoSuchCurve'" "LOP_4: an unknown tangent name is refused with that reason"
lop_has "$LOP_4" "nothing was built" "LOP_4: ...and nothing is built"
lop_has "$LOP_5" "tangent loft refused by the kernel" "LOP_5: an open line cannot be a tangent field on a closed circle: refused by the kernel"
lop_has "$LOP_5" "closed curve" "LOP_5: ...with the closedness reason"
lop_has "$LOP_6" "need a finite number" "LOP_6: StartWeight=abc is refused"
lop_hasnot "$LOP_7" "tangent" "LOP_7_control: the plain loft prints no tangent text"
lop_num "$LOP_7" "Volume = " 6.2 6.3 "LOP_7_control: without tangents the same two circles are the cylinder, volume pi 1 2 = 6.2832 (differs from the barrel's 7.981)"
# History replay: the sources of a recorded tangent loft are the sections followed by the tangent curves
lop_has "$LOP_8" "Loft <- 1,2,3,4" "LOP_8: the History record lists the two sections and the two tangent curves as sources"
lop_nth "$LOP_8" "Volume = " 1 7.90 8.06 "LOP_8a: built with History On, the barrel's volume is 7.981 (Hermite integral)"
lop_nth "$LOP_8" "Volume = " 2 11.93 12.18 "LOP_8b: section 2 moved +1 in z, the wall rebuilt: volume 12.0546 (z1 = 3 in the Hermite formulas)"
lop_nth "$LOP_8" "Volume = " 3 11.85 12.09 "LOP_8c: the START TANGENT curve moved +1 in z (b0 = 2), rebuilt: volume 11.9718"
lop_has "$LOP_9" "SeamAlign=No" "LOP_9: SeamAlign=No built with History On"
lop_nth "$LOP_9" "Area = " 1 52.2 53.5 "LOP_9a: the turned pair is the twisted wall, area 52.839"
lop_nth "$LOP_9" "Area = " 2 52.7 53.9 "LOP_9b: section 2 moved +1 in x, the rebuild KEEPS SeamAlign=No: area 53.297 (an aligned rebuild would read 63.46)"
