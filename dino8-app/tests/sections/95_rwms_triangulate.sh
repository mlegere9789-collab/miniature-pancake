# RWMS_TRIANGULATE (D8-RHINO-MESH-0085): TriangulateMesh splits every quad of a mesh into two triangles. Oracles: a mesh box has 6 quads, 8 vertices; after the command
# 12 triangles on the same 8 vertices (each quad gives two), still closed, area 2 (ab + bc + ca) and volume abc unchanged (closed forms of the box: 600 / 1000
# for the 10-cube, 142 / 105 for a 3 x 5 x 7 box). QuadrangulateMesh merges the coplanar pairs back to 6 quads.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_triangulate_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_TRIANGULATE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_triangulate_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_TRIANGULATE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_check "RWMS_TRIANGULATE_cube: before: 6 faces and 8 vertices" "$([ "$(rwms_f cube Faces 1)" = 6 ] && [ "$(rwms_f cube Vertices 1)" = 8 ]; echo $?)"
rwms_check "RWMS_TRIANGULATE_cube: after: 12 faces and the same 8 vertices, still closed" "$([ "$(rwms_f cube Faces 2)" = 12 ] && [ "$(rwms_f cube Vertices 2)" = 8 ] && rwms_block cube | grep -qF 'Closed: yes'; echo $?)"
rwms_check "RWMS_TRIANGULATE_cube: area 600 and volume 1000 before and after (within 1e-6)" "$(rwms_near "$(rwms_num cube Area 1)" 600 1e-6 && rwms_near "$(rwms_num cube Area 2)" 600 1e-6 && rwms_near "$(rwms_num cube Volume 1)" 1000 1e-6 && rwms_near "$(rwms_num cube Volume 2)" 1000 1e-6; echo $?)"
rwms_check "RWMS_TRIANGULATE_cube: QuadrangulateMesh merges the 12 triangles back into 6 faces on 8 vertices" "$([ "$(rwms_f cube Faces 3)" = 6 ] && [ "$(rwms_f cube Vertices 3)" = 8 ]; echo $?)"
rwms_check "RWMS_TRIANGULATE_box: 3 x 5 x 7 box triangulates to 12 faces, 8 vertices" "$([ "$(rwms_f box Faces 1)" = 12 ] && [ "$(rwms_f box Vertices 1)" = 8 ]; echo $?)"
rwms_check "RWMS_TRIANGULATE_box: area 142 = 2 (15 + 35 + 21) and volume 105 = 3 x 5 x 7 within 1e-6" "$(rwms_near "$(rwms_num box Area 1)" 142 1e-6 && rwms_near "$(rwms_num box Volume 1)" 105 1e-6; echo $?)"
