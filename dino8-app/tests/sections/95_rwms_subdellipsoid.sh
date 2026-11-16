# RWMS_SUBDELLIPSOID (D8-RHINO-SUBD-0019): SubDEllipsoid creates a SubD ellipsoid from a centre and three axis end points. Oracle: Catmull-Clark
# commutes with affine maps, so the ellipsoid with semi-axes (5,3,2) at (10,20,30) must have the limit bounding box of the unit SubD sphere scaled by
# (5,3,2) and moved to the centre, and the same cage counts and Euler characteristic 2 as that sphere.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_subdellipsoid_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDELLIPSOID_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_subdellipsoid_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDELLIPSOID_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
fi
rwms_ok() { echo "ok   $1"; }
rwms_bad() { echo "FAIL $1"; fail=1; }
rwms_block() { awk -v name="$1" '$0 ~ ("Unknown command: RWMSBEGIN_" name "$") { on = 1; next } on && /Unknown command: RWMSBEGIN_/ { exit } on { print }' <<<"$RWMS_OUT"; }
# rwms_f CASE LABEL [N]: the number of the N-th "  LABEL: <number>" line of the case (What output)
rwms_f() { rwms_block "$1" | sed -n -E "s/^history:   $2: ([-0-9.e+]+)\$/\\1/p" | sed -n "${3:-1}p"; }
# rwms_bb CASE [N]: "xmin ymin zmin xmax ymax zmax" of the N-th Bounding box line
rwms_bb() { rwms_block "$1" | sed -n -E 's/^history:   Bounding box: \(([^,]+), ([^,]+), ([^)]+)\) to \(([^,]+), ([^,]+), ([^)]+)\)$/\1 \2 \3 \4 \5 \6/p' | sed -n "${2:-1}p"; }
# rwms_num CASE KEY [N]: the number after "KEY = " of the N-th Area/Volume line
rwms_num() { rwms_block "$1" | sed -n -E "s/^history: $2 = ([-0-9.e+]+) .*\$/\\1/p" | sed -n "${3:-1}p"; }
# rwms_near A B TOL: |A - B| <= TOL (empty values fail)
rwms_near() { [ -n "$1" ] && [ -n "$2" ] && awk -v a="$1" -v b="$2" -v t="$3" 'BEGIN { d = a - b; if (d < 0) d = -d; exit !(d <= t) }'; }
# rwms_check DESC CONDITION-EXIT-STATUS
rwms_check() { if [ "$2" = 0 ]; then rwms_ok "$1"; else rwms_bad "$1"; fi; }
# rwms_counts CASE N F V E : the N-th What block reports exactly F faces, V vertices, E edges
rwms_counts() {
  local f v e; f="$(rwms_f "$1" Faces "$2")"; v="$(rwms_f "$1" Vertices "$2")"; e="$(rwms_f "$1" Edges "$2")"
  if [ "$f" = "$3" ] && [ "$v" = "$4" ] && [ "$e" = "$5" ]; then rwms_ok "$6 (faces $f, vertices $v, edges $e)"
  else rwms_bad "$6 (faces ${f:-none}, vertices ${v:-none}, edges ${e:-none}; expected $3, $4, $5)"; fi
}
# rwms_euler CASE N CHI: V - E + F of the N-th What block equals CHI
rwms_euler() {
  local f v e; f="$(rwms_f "$1" Faces "$2")"; v="$(rwms_f "$1" Vertices "$2")"; e="$(rwms_f "$1" Edges "$2")"
  if [ -n "$f" ] && [ -n "$v" ] && [ -n "$e" ] && [ $((v - e + f)) = "$3" ]; then rwms_ok "$4 (V - E + F = $v - $e + $f = $3)"
  else rwms_bad "$4 (V - E + F = ${v:-?} - ${e:-?} + ${f:-?}, expected $3)"; fi
}

rwms_euler ell 1 2 "RWMS_SUBDELLIPSOID_ell: Euler characteristic 2 (a sphere topology)"
fu="$(rwms_f unit Faces 1)"; fe="$(rwms_f ell Faces 1)"; vu="$(rwms_f unit Vertices 1)"; ve="$(rwms_f ell Vertices 1)"
rwms_check "RWMS_SUBDELLIPSOID_ell: same cage counts as the unit sphere (faces $fe / $fu, vertices $ve / $vu)" "$([ -n "$fu" ] && [ "$fu" = "$fe" ] && [ "$vu" = "$ve" ]; echo $?)"
read -r u0 v0 w0 u1 v1 w1 <<<"$(rwms_bb unit 1)"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb ell 1)"
rwms_check "RWMS_SUBDELLIPSOID_ell: x extent equals 5 x the unit sphere x extent within 1e-3 (got $x0..$x1, unit $u0..$u1)" "$(rwms_near "$x0" "$(awk -v u="$u0" 'BEGIN{print 10+5*u}')" 1e-3 && rwms_near "$x1" "$(awk -v u="$u1" 'BEGIN{print 10+5*u}')" 1e-3; echo $?)"
rwms_check "RWMS_SUBDELLIPSOID_ell: y extent equals 3 x the unit sphere y extent within 1e-3 (got $y0..$y1, unit $v0..$v1)" "$(rwms_near "$y0" "$(awk -v u="$v0" 'BEGIN{print 20+3*u}')" 1e-3 && rwms_near "$y1" "$(awk -v u="$v1" 'BEGIN{print 20+3*u}')" 1e-3; echo $?)"
rwms_check "RWMS_SUBDELLIPSOID_ell: z extent equals 2 x the unit sphere z extent within 1e-3 (got $z0..$z1, unit $w0..$w1)" "$(rwms_near "$z0" "$(awk -v u="$w0" 'BEGIN{print 30+2*u}')" 1e-3 && rwms_near "$z1" "$(awk -v u="$w1" 'BEGIN{print 30+2*u}')" 1e-3; echo $?)"
