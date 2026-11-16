# RWMS_UNWELD (D8-RHINO-MESH-0089; weld back is the app side of D8-RHINO-MESH-0092): Unweld creates coincident vertices along edges whose dihedral angle exceeds the tolerance.
# Oracles (closed forms of a box): its edges are 90 degrees, so with a tolerance of 100 degrees nothing is unwelded (8 vertices, closed), with 30 degrees
# every corner is split into its 3 faces: 8 x 3 = 24 vertices (16 added), 6 faces, area still 600, the mesh is open (naked seams); Weld 180 merges the 16
# copies again: 8 vertices, closed, volume 1000.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_unweld_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_UNWELD_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_unweld_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_UNWELD_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_check "RWMS_UNWELD_cube: tolerance 100 degrees keeps all edges welded: 8 vertices, 6 faces, closed" "$([ "$(rwms_f cube Vertices 1)" = 8 ] && [ "$(rwms_f cube Faces 1)" = 6 ] && rwms_block cube | grep -qF 'Closed: yes'; echo $?)"
rwms_check "RWMS_UNWELD_cube: tolerance 30 degrees splits each of the 8 corners into 3 vertices: 24 vertices, 6 faces (16 copies added)" "$([ "$(rwms_f cube Vertices 2)" = 24 ] && [ "$(rwms_f cube Faces 2)" = 6 ] && rwms_block cube | grep -qF 'Unweld: added 16 vertex copy(ies)'; echo $?)"
rwms_check "RWMS_UNWELD_cube: the unwelded mesh is open and its area is still 600" "$([ "$(rwms_block cube | grep -c 'Closed: no')" = 1 ] && rwms_near "$(rwms_num cube Area 1)" 600 1e-6; echo $?)"
rwms_check "RWMS_UNWELD_cube: Weld 180 merges the 16 copies: 8 vertices, 6 faces, closed again" "$([ "$(rwms_f cube Vertices 3)" = 8 ] && [ "$(rwms_f cube Faces 3)" = 6 ] && rwms_block cube | grep -qF 'Weld: merged 16 vertex(es); 8 vertices remain'; echo $?)"
rwms_check "RWMS_UNWELD_cube: after the weld area is 600 and volume 1000 within 1e-6" "$(rwms_near "$(rwms_num cube Area 2)" 600 1e-6 && rwms_near "$(rwms_num cube Volume 2)" 1000 1e-6; echo $?)"
