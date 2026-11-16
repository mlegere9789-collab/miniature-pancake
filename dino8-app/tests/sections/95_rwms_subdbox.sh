# RWMS_SUBDBOX (D8-RHINO-SUBD-0014): SubDBox creates a SubD box. Oracles: the control cage of a box has 8 vertices, 12 edges and 6 faces (Euler
# characteristic 2, no crease edge); Catmull-Clark commutes with the symmetry of the box, so the centre of the limit surface bounding box is the box centre.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_subdbox_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDBOX_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_subdbox_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDBOX_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_counts cube 1 6 8 12 "RWMS_SUBDBOX_cube: the 10-cube SubD box has the cage counts of a hexahedron"
rwms_euler cube 1 2 "RWMS_SUBDBOX_cube: Euler characteristic 2"
rwms_check "RWMS_SUBDBOX_cube: no crease edge on a smooth box" "$([ "$(rwms_f cube 'Crease edges' 1)" = 0 ]; echo $?)"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb cube 1)"
rwms_check "RWMS_SUBDBOX_cube: limit bounding box centre is the box centre (5,5,5) within 1e-3 ($x0 $y0 $z0 .. $x1 $y1 $z1)" "$(rwms_near "$(awk -v a="$x0" -v b="$x1" 'BEGIN{print (a+b)/2}')" 5 1e-3 && rwms_near "$(awk -v a="$y0" -v b="$y1" 'BEGIN{print (a+b)/2}')" 5 1e-3 && rwms_near "$(awk -v a="$z0" -v b="$z1" 'BEGIN{print (a+b)/2}')" 5 1e-3; echo $?)"
rwms_counts slab 1 6 8 12 "RWMS_SUBDBOX_slab: a 4 x 4 x 3 box has the same 6/8/12 counts"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb slab 1)"
rwms_check "RWMS_SUBDBOX_slab: bounding box centre (4,-2,1.5) within 1e-3 and the shell stays inside [2,6]x[-4,0]x[0,3] ($x0 $y0 $z0 .. $x1 $y1 $z1)" "$(rwms_near "$(awk -v a="$x0" -v b="$x1" 'BEGIN{print (a+b)/2}')" 4 1e-3 && rwms_near "$(awk -v a="$y0" -v b="$y1" 'BEGIN{print (a+b)/2}')" -2 1e-3 && rwms_near "$(awk -v a="$z0" -v b="$z1" 'BEGIN{print (a+b)/2}')" 1.5 1e-3 && awk -v a="$x0" -v b="$x1" -v c="$y0" -v d="$y1" -v e="$z0" -v f="$z1" 'BEGIN{exit !(a>2 && b<6 && c>-4 && d<0 && e>0 && f<3)}'; echo $?)"
