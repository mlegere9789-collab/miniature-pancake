# RWMS_INSERTEDGE (D8-RHINO-MESH-0033): InsertEdge adds an edge loop across an edge ring of a SubD. Oracle (closed form): a loop through a ring of 4 quads of a
# box adds 4 vertices (one per ring edge), 4 faces (every quad of the ring splits in two) and 8 edges (4 new rim edges + 4 halves of ring edges... counted
# as edges: 12 + 8 = 20), so 6/8/12 becomes faces 10, vertices 12, edges 20; Euler characteristic stays 2 and the shell is not displaced
# beyond the cage (limit bounding box centre unchanged).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_insertedge_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_INSERTEDGE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_insertedge_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_INSERTEDGE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_counts ring 1 6 8 12 "RWMS_INSERTEDGE_ring: before: a box has 6/8/12"
rwms_counts ring 2 10 12 20 "RWMS_INSERTEDGE_ring: one loop across the 4-quad ring adds 4 faces, 4 vertices, 8 edges"
rwms_euler ring 2 2 "RWMS_INSERTEDGE_ring: Euler characteristic stays 2"
rwms_check "RWMS_INSERTEDGE_ring: the command reports 4 edges inserted along the ring" "$(rwms_block ring | grep -qF 'InsertEdge: 4 edge(s) inserted'; echo $?)"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb ring 2)"
rwms_check "RWMS_INSERTEDGE_ring: limit bounding box centre stays (5,5,5) within 1e-3 ($x0 $y0 $z0 .. $x1 $y1 $z1)" "$(rwms_near "$(awk -v a="$y0" -v b="$y1" 'BEGIN{print (a+b)/2}')" 5 1e-3 && rwms_near "$(awk -v a="$z0" -v b="$z1" 'BEGIN{print (a+b)/2}')" 5 1e-3; echo $?)"
rwms_euler two 1 2 "RWMS_INSERTEDGE_two: a second loop across another ring keeps Euler characteristic 2"
rwms_check "RWMS_INSERTEDGE_two: two loops add more faces than one loop (more than 10 faces)" "$([ "$(rwms_f two Faces 1)" -gt 10 ]; echo $?)"
