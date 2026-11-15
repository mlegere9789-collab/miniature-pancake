# RWMS_SUBDCYLINDER (D8-RHINO-SUBD-0017): SubDCylinder creates a SubD cylinder from base centre, radius and height. Oracles: Euler characteristic 2; the two
# cap rims are creased, so the limit surface touches the base and top planes exactly: z extent is exactly [z0, z0 + height]; the creased rim limit is
# the cubic B-spline of the rim polygon, which lies strictly inside the radius (the limit radius is R (2 + cos(2 pi / n)) / 3 < R, above 0.9 R for
# n >= 12) so the x and y half extents lie in (0.9 R, R).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_subdcylinder_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDCYLINDER_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_subdcylinder_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDCYLINDER_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_euler a 1 2 "RWMS_SUBDCYLINDER_a: Euler characteristic 2"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb a 1)"
rwms_check "RWMS_SUBDCYLINDER_a: z extent is exactly [0,10] within 1e-4 ($z0 .. $z1)" "$(rwms_near "$z0" 0 1e-4 && rwms_near "$z1" 10 1e-4; echo $?)"
rwms_check "RWMS_SUBDCYLINDER_a: x and y half extents lie in (4.5, 5) ($x0 .. $x1, $y0 .. $y1)" "$(awk -v a="$x0" -v b="$x1" -v c="$y0" -v d="$y1" 'BEGIN{exit !(-a>4.5 && b>4.5 && -c>4.5 && d>4.5 && -a<5 && b<5 && -c<5 && d<5)}'; echo $?)"
rwms_euler b 1 2 "RWMS_SUBDCYLINDER_b: Euler characteristic 2"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb b 1)"
rwms_check "RWMS_SUBDCYLINDER_b: z extent is exactly [9,12] within 1e-4 ($z0 .. $z1)" "$(rwms_near "$z0" 9 1e-4 && rwms_near "$z1" 12 1e-4; echo $?)"
rwms_check "RWMS_SUBDCYLINDER_b: x half extent about the centre 7 lies in (1.8, 2) and y half extent about 8 lies in (1.8, 2) ($x0 .. $x1, $y0 .. $y1)" "$(awk -v a="$x0" -v b="$x1" -v c="$y0" -v d="$y1" 'BEGIN{exit !(7-a>1.8 && b-7>1.8 && 8-c>1.8 && d-8>1.8 && 7-a<2 && b-7<2 && 8-c<2 && d-8<2)}'; echo $?)"
