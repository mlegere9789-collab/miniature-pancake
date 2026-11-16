# RWMS_SUBDPLANE (D8-RHINO-SUBD-0024): SubDPlane creates a flat SubD sheet between two corners. Oracles: the sheet is a disc (Euler characteristic 1),
# its boundary edges are creased (so the corners stay put) and the limit bounding box is exactly the rectangle, flat in z (a crease boundary
# interpolates its corner vertices).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_subdplane_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDPLANE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_subdplane_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_SUBDPLANE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_euler big 1 1 "RWMS_SUBDPLANE_big: Euler characteristic 1 (a disc)"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb big 1)"
rwms_check "RWMS_SUBDPLANE_big: limit bounding box is exactly (0,0,0) to (20,10,0) within 1e-4 ($x0 $y0 $z0 .. $x1 $y1 $z1)" "$(rwms_near "$x0" 0 1e-4 && rwms_near "$y0" 0 1e-4 && rwms_near "$z0" 0 1e-4 && rwms_near "$x1" 20 1e-4 && rwms_near "$y1" 10 1e-4 && rwms_near "$z1" 0 1e-4; echo $?)"
rwms_check "RWMS_SUBDPLANE_big: the boundary edges are creased (more than 0 crease edges, fewer than all edges)" "$(c="$(rwms_f big 'Crease edges' 1)"; e="$(rwms_f big Edges 1)"; awk -v c="$c" -v e="$e" 'BEGIN{exit !(c > 0 && c < e)}'; echo $?)"
rwms_euler moved 1 1 "RWMS_SUBDPLANE_moved: Euler characteristic 1"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb moved 1)"
rwms_check "RWMS_SUBDPLANE_moved: limit bounding box is exactly (-5,3,0) to (2,9,0) within 1e-4 ($x0 $y0 $z0 .. $x1 $y1 $z1)" "$(rwms_near "$x0" -5 1e-4 && rwms_near "$y0" 3 1e-4 && rwms_near "$z0" 0 1e-4 && rwms_near "$x1" 2 1e-4 && rwms_near "$y1" 9 1e-4 && rwms_near "$z1" 0 1e-4; echo $?)"
