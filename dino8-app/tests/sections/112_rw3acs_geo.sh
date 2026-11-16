# ---------------------------------------------------------------------------
# RW3ACS_GEO: inters, polar, angle and distance of the AutoLISP-style dialect (src/script/Lisp.cpp).
# Rows of the Parity Register (autocad/lisp.jsonl): D8-AUTOCAD-LISP-0204 (inters), D8-AUTOCAD-LISP-0250 (polar), D8-AUTOCAD-LISP-0111 (angle), D8-AUTOCAD-LISP-0151 (distance).
# Oracle: the documented behaviour of each function (the Autodesk AutoLISP reference, the URL is in each Register row), worked
# out by hand and recomputed here with bash and awk arithmetic, never with the interpreter under test. Where the expected text
# is a documented worked example it is marked "(documented)" in the check label. The vendor pages could not be fetched when
# this section was written: every "(documented)" value is from recollection of the Autodesk help and needs an independent check.
# The construction plane is the WCS XY plane here (no UCS change is made).
# One real app process runs rw3acs_geo_script.txt; each result is read from the "Lisp: <value>" line printed after the typed
# expression. Self-contained (own helpers) so that `run_sections.sh RW3ACS_GEO` works alone.
# ---------------------------------------------------------------------------

r3g_exec() {  # r3g_exec <config home> <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 "$BIN" --smoke 100 --script "$2" 2>&1
  else
    XDG_CONFIG_HOME="$1" APPDATA="$1" timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$2" 2>&1
  fi
}
r3g_ok() { echo "ok   $1"; }
r3g_bad() { echo "FAIL $1"; fail=1; }
r3g_blk() {  # the lines printed after the Nth "Command: <typed text>" echo, up to the next "Command:" (ENVIRON: awk -v would eat backslashes)
  sed "s/^history: //" <<<"$1" | PU_CMD="Command: $2" PU_NTH="$3" awk 'BEGIN { n = ENVIRON["PU_NTH"] + 0; cmd = ENVIRON["PU_CMD"] } $0==cmd{c++; on=(c==n); next} /^Command:/{on=0} on'
}
r3g_is() {  # <output> <typed text> <nth> <expected text after "Lisp: "> <description>
  local blk got
  blk="$(r3g_blk "$1" "$2" "$3")"
  got="$(grep -m1 '^Lisp: ' <<<"$blk" || true)"; got="${got#Lisp: }"
  if [ -n "$got" ] && [ "$got" = "$4" ]; then r3g_ok "$5 ($2 -> $got)"; else r3g_bad "$5: '$2' (#$3) gave '$got', expected '$4'"; near "$blk" "$4"; fi
}
r3g_err() {  # the program was stopped: an error line with the substring and no "Lisp: " result
  local blk; blk="$(r3g_blk "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$blk" && grep -q 'error' <<<"$blk" && ! grep -q '^Lisp: ' <<<"$blk"; then r3g_ok "$5"; else r3g_bad "$5: '$2' (#$3) did not stop with '$4'"; echo "$blk" | head -6 | sed 's/^/     actual:   /'; fi
}
r3g_r() {  # a real the way the app prints it (six decimals, trailing zeros trimmed to one), from an awk expression (PI is defined)
  local s; s="$(awk -v PI=3.14159265358979323846 "BEGIN { printf \"%.6f\", $1 }")"
  while [[ "$s" == *0 && "${s%?}" != *. ]]; do s="${s%?}"; done
  [ "$s" = "-0.0" ] && s="0.0"
  echo "$s"
}

if [ "$IS_WINDOWS" = 1 ]; then R3G_SUB="Dino8"; else R3G_SUB="dino8"; fi
R3G_CFG="$TMP/r3g_cfg"
mkdir -p "$R3G_CFG/$R3G_SUB"
R3G="$(r3g_exec "$R3G_CFG" "$HERE/rw3acs_geo_script.txt")" || { echo "$R3G" | tail -20; r3g_bad "RW3ACS_GEO script exited non-zero"; }
grep -q "^smoke:" <<<"$R3G" || { echo "$R3G" | tail -20; r3g_bad "RW3ACS_GEO: no smoke line (the app did not reach its normal end)"; }
r3g_is "$R3G" '(inters '"'"'(1.0 1.0) '"'"'(9.0 9.0) '"'"'(4.0 1.0) '"'"'(4.0 9.0))' 1 '(4.0 4.0)' 'R3G_inters: y=x and x=4 meet at (4 4), on both segments'
r3g_is "$R3G" '(inters '"'"'(1.0 1.0) '"'"'(9.0 9.0) '"'"'(4.0 1.0) '"'"'(4.0 2.0))' 1 'nil' 'R3G_inters: the lines meet at (4 4), beyond the end of the second segment, nil (documented)'
r3g_is "$R3G" '(inters '"'"'(1.0 1.0) '"'"'(9.0 9.0) '"'"'(4.0 1.0) '"'"'(4.0 2.0) nil)' 1 '(4.0 4.0)' 'R3G_inters: the same call with onseg nil treats them as infinite lines (documented)'
r3g_is "$R3G" '(inters '"'"'(1.0 1.0) '"'"'(9.0 9.0) '"'"'(4.0 1.0) '"'"'(4.0 2.0) T)' 1 'nil' 'R3G_inters: onseg T keeps the segment test'
r3g_is "$R3G" '(inters '"'"'(0 0) '"'"'(4 2) '"'"'(0 3) '"'"'(4 -1))' 1 '(2.0 1.0)' 'R3G_inters: y=x/2 and y=3-x meet at (2 1)'
r3g_is "$R3G" '(inters '"'"'(0 0) '"'"'(1 0) '"'"'(0 1) '"'"'(1 1))' 1 'nil' 'R3G_inters: parallel lines never meet'
r3g_is "$R3G" '(inters '"'"'(0 0) '"'"'(1 0) '"'"'(0 1) '"'"'(1 1) nil)' 1 'nil' 'R3G_inters: parallel lines never meet, even extended'
r3g_is "$R3G" '(inters '"'"'(0 0 0) '"'"'(2 2 2) '"'"'(0 2 0) '"'"'(2 0 2))' 1 '(1.0 1.0 1.0)' 'R3G_inters: four 3D points, two cube diagonals meet at the centre (1 1 1)'
r3g_is "$R3G" '(inters '"'"'(0 0 0) '"'"'(2 0 0) '"'"'(0 1 1) '"'"'(2 1 1))' 1 'nil' 'R3G_inters: four 3D points, skew lines (x axis and a parallel at y=1 z=1) give nil'
r3g_is "$R3G" '(inters '"'"'(0 0 5) '"'"'(2 2 5) '"'"'(0 2) '"'"'(2 0))' 1 '(1.0 1.0)' 'R3G_inters: one 2D point projects onto the plane, Z is ignored, 2D result'
r3g_is "$R3G" '(polar '"'"'(1 1 3.5) 0.785398 1.414214)' 1 "($(r3g_r '1 + 1.414214 * cos(0.785398)') $(r3g_r '1 + 1.414214 * sin(0.785398)') $(r3g_r '3.5'))" 'R3G_polar: 3D point keeps its Z (documented: approximately (2.0 2.0 3.5))'
r3g_is "$R3G" '(polar '"'"'(1 1) 0.785398 1.414214)' 1 "($(r3g_r '1 + 1.414214 * cos(0.785398)') $(r3g_r '1 + 1.414214 * sin(0.785398)'))" 'R3G_polar: a 2D point gives a 2D point'
r3g_is "$R3G" '(polar '"'"'(0 0 0) 0 5)' 1 '(5.0 0.0 0.0)' 'R3G_polar: along the X axis, every coordinate is a real'
r3g_is "$R3G" '(polar '"'"'(0 0 0) (/ pi 2) 3)' 1 "($(r3g_r '3 * cos(PI / 2)') $(r3g_r '3 * sin(PI / 2)') $(r3g_r '0'))" 'R3G_polar: a quarter turn counterclockwise from the X axis'
r3g_is "$R3G" '(polar '"'"'(2 3) pi 4)' 1 "($(r3g_r '2 + 4 * cos(PI)') $(r3g_r '3 + 4 * sin(PI)'))" 'R3G_polar: a half turn from (2 3) by 4 gives (-2 3)'
r3g_is "$R3G" '(polar '"'"'(10 20 30) 0 0)' 1 '(10.0 20.0 30.0)' 'R3G_polar: distance 0 gives the point itself, as reals'
r3g_is "$R3G" '(angle '"'"'(1.0 1.0) '"'"'(1.0 4.0))' 1 "$(r3g_r 'PI / 2')" 'R3G_angle: straight up is 1.5708 (documented)'
r3g_is "$R3G" '(angle '"'"'(5.0 1.33) '"'"'(2.4 1.33))' 1 "$(r3g_r 'PI')" 'R3G_angle: straight left is 3.14159 (documented)'
r3g_is "$R3G" '(angle '"'"'(0 0) '"'"'(1 0))' 1 "$(r3g_r '0')" 'R3G_angle: along the X axis is 0'
r3g_is "$R3G" '(angle '"'"'(0 0) '"'"'(0 -1))' 1 "$(r3g_r '3 * PI / 2')" 'R3G_angle: straight down is 3 pi/2, counterclockwise from the X axis'
r3g_is "$R3G" '(angle '"'"'(0 0) '"'"'(-1 -1))' 1 "$(r3g_r '5 * PI / 4')" 'R3G_angle: the third quadrant diagonal is 5 pi/4'
r3g_is "$R3G" '(angle '"'"'(0 0 5) '"'"'(1 1 9))' 1 "$(r3g_r 'PI / 4')" 'R3G_angle: 3D points are projected onto the plane, Z is dropped'
r3g_is "$R3G" '(distance '"'"'(1.0 2.5 3.0) '"'"'(7.7 2.5 3.0))' 1 "$(r3g_r '7.7 - 1.0')" 'R3G_distance: 6.7 along X (documented)'
r3g_is "$R3G" '(distance '"'"'(1.0 2.0 0.5) '"'"'(3.0 4.0 0.5))' 1 "$(r3g_r 'sqrt(2 * 2 + 2 * 2)')" 'R3G_distance: 2.82843 in a plane of constant Z (documented)'
r3g_is "$R3G" '(distance '"'"'(0 0 0) '"'"'(1 2 2))' 1 "$(r3g_r '3')" 'R3G_distance: the 1-2-2-3 box diagonal'
r3g_is "$R3G" '(distance '"'"'(0 0) '"'"'(3 4 100))' 1 "$(r3g_r '5')" 'R3G_distance: when either point is 2D the Z of the 3D point is ignored'
r3g_is "$R3G" '(distance '"'"'(1 1) '"'"'(1 1))' 1 "$(r3g_r '0')" 'R3G_distance: a point to itself'
r3g_is "$R3G" '(type (distance (quote (0 0)) (quote (3 4))))' 1 'REAL' 'R3G_distance: always a real, even for integer coordinates'
