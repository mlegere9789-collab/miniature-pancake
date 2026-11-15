# ---------------------------------------------------------------------------
# XAL_: ExtrudeCrvAlongCrv Solid option (xal_script.txt) - the kernel's exact Brep::ExtrudeAlongCurve from the app. Every
# figure is MEASURED from the app's output and compared with a closed form: exact volume (circle along a line = pi r^2 h;
# a RATIONAL circle along a RATIONAL eighth-circle arc in XZ = pi r^2 x net z = pi 3 sin(45 deg) by Cavalieri), the Volume command's own tessellated
# figure as an independent cross-check, BoundingBox, the periodic sheet's area 2 pi r h. Negative controls: a closed profile
# round a closed path and a path inside the profile's own plane are refused WITH the kernel's reason and build nothing; the
# no-option control still builds the unchanged sum surface and prints no kernel message. Here-strings only.
# ---------------------------------------------------------------------------
xal_ok() { echo "ok   $1"; }
xal_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  XAL_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/xal_script.txt" 2>&1)" || { echo "$XAL_OUT" | tail -40; echo "FAIL XAL_run: script exited non-zero"; fail=1; XAL_OUT=""; }
else
  XAL_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/xal_script.txt" 2>&1)" || { echo "$XAL_OUT" | tail -40; echo "FAIL XAL_run: script exited non-zero"; fail=1; XAL_OUT=""; }
fi
if grep -q "^FAIL" <<<"$XAL_OUT"; then xal_bad "XAL_script: an @expect line of the script failed"; else xal_ok "XAL_script: every @expect_objects line held"; fi
xal_case() { awk -v n="$1" '/Command: ExtrudeCrvAlongCrv( |$)/ { c++ } c == n' <<<"$XAL_OUT"; }
xal_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
xal_has() { if grep -qF -- "$2" <<<"$1"; then xal_ok "$3"; else xal_bad "$3"; near "$1" "$2"; fi; }
xal_hasnot() { if grep -qF -- "$2" <<<"$1"; then xal_bad "$3"; near "$1" "$2"; else xal_ok "$3"; fi; }
# first number after prefix $2 in $1, must lie in [$3, $4]
xal_num() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | head -1 | grep -oE '[-0-9.e+]+$' || true)"
  if xal_in "$v" "$3" "$4"; then xal_ok "$5 ($v in $3..$4)"; else xal_bad "$5 ('$v' not in $3..$4)"; fi
}
xal_bbox() {
  local line
  line="$({ grep -o "Bounding box min [^ ]* max [^ ]*" <<<"$1" || true; } | head -1)"
  if [ -n "$line" ] && awk -v r="$2" '{ split($4, a, ","); split($6, b, ","); split(r, q, " ");
        ok = (a[1] >= q[1] && a[1] <= q[2] && a[2] >= q[3] && a[2] <= q[4] && a[3] >= q[5] && a[3] <= q[6] &&
              b[1] >= q[7] && b[1] <= q[8] && b[2] >= q[9] && b[2] <= q[10] && b[3] >= q[11] && b[3] <= q[12]); exit !ok }' <<<"$line"; then
    xal_ok "$3 ($line)"
  else
    xal_bad "$3 (box '$line' outside $2)"
  fi
}
XAL_1="$(xal_case 1)"; XAL_2="$(xal_case 2)"; XAL_3="$(xal_case 3)"; XAL_4="$(xal_case 4)"; XAL_5="$(xal_case 5)"; XAL_6="$(xal_case 6)"
xal_has "$XAL_1" "ExtrudeCrvAlongCrv: 1 solid(s)" "XAL_1: the closed circle along a line with Solid=Yes builds one solid"
xal_num "$XAL_1" "closed solid, exact volume " 62.8318 62.8320 "XAL_1: kernel exact volume = pi 2^2 5 = 62.83185"
xal_num "$XAL_1" "Volume = " 62.5 62.84 "XAL_1: the Volume command's own tessellated figure agrees (cross-check, within 0.5 percent)"
xal_bbox "$XAL_1" "-2.05 -1.9 -2.05 -1.9 -0.01 0.01 1.9 2.05 1.9 2.05 4.99 5.01" "XAL_1_bbox: box is x,y +-2, z = 0..5"
# 2: rational circle x rational arc: Cavalieri pi * 1^2 * 3
xal_has "$XAL_2" "ExtrudeCrvAlongCrv: 1 solid(s)" "XAL_2: a rational circle along a rational eighth-circle arc builds one solid"
xal_num "$XAL_2" "closed solid, exact volume " 6.66425 6.66440 "XAL_2: kernel exact volume = pi x net z 3 sin 45 = 6.664325 (rational x rational)"
xal_num "$XAL_2" "Volume = " 6.55 6.67 "XAL_2: the Volume command's tessellated figure agrees (cross-check)"
xal_bbox "$XAL_2" "-1.93 -1.83 -1.05 -0.95 -0.05 0.05 0.95 1.05 0.95 1.05 2.07 2.17" "XAL_2_bbox: box x -1.8787..1 (circle swung by the arc's x travel 3 cos 45 - 3 = -0.879), y +-1, z 0..2.1213"
# 3: open profile round a closed path: periodic sheet
xal_has "$XAL_3" "open sheet (no caps: the profile is open)" "XAL_3: a vertical line round a closed circle path is reported as an open sheet, not a solid"
xal_num "$XAL_3" "area " 37.6990 37.6993 "XAL_3: kernel exact area = 2 pi 2 3 = 37.69911 (the cylinder wall)"
# 4, 5: refusals with reasons, nothing built
xal_has "$XAL_4" "zero net volume" "XAL_4: a closed profile round a closed path is refused with the zero-net-volume reason"
xal_has "$XAL_4" "ExtrudeCrvAlongCrv: 0 solid(s)" "XAL_4: ...and nothing is built (no silent surface fallback)"
xal_hasnot "$XAL_4" "closed solid, exact volume" "XAL_4: ...no solid is reported"
xal_has "$XAL_5" "the extrusion is flat" "XAL_5: a path inside the profile's own plane is refused with the flat reason"
xal_has "$XAL_5" "ExtrudeCrvAlongCrv: 0 solid(s)" "XAL_5: ...and nothing is built"
# 6: control
xal_has "$XAL_6" "ExtrudeCrvAlongCrv: 1 surface(s)" "XAL_6_control: without Solid the unchanged sum surface is built"
xal_hasnot "$XAL_6" "exact kernel sum surface" "XAL_6_control: ...and no kernel message is printed"
xal_num "$XAL_6" "Area = " 49.9 50.1 "XAL_6_control: 10 x 5 rectangle sum surface area 50"
