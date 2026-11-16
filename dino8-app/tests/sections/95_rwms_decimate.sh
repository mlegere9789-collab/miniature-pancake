# RWMS_DECIMATE (D8-BLENDER-MESH-0019, D8-BLENDER-MODIFIER-0012; the Rhino counterpart is ReduceMesh): ReduceMesh decimates a mesh sphere to half its faces in place.
# Oracles: the face count after is the target (half of the faces before, rounded) within one collapse (a collapse of a closed triangle mesh removes 2 faces), the
# mesh stays a closed manifold, the volume of the inscribed polyhedron stays within 10 % of its volume before and never exceeds the ball 4 pi r^3 / 3 (r = 10) by
# more than 2 %, and the reported maximal deviation is at most 10 % of the radius.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_decimate_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_DECIMATE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_decimate_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_DECIMATE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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


before_f="$(rwms_f sphere Faces 1)"; after_f="$(rwms_f sphere Faces 2)"
msg="$(rwms_block sphere | grep -E 'ReduceMesh: object [0-9]+: [0-9]+ -> [0-9]+ faces, max deviation' | head -1)"
rep_before="$(sed -n -E 's/.*ReduceMesh: object [0-9]+: ([0-9]+) -> ([0-9]+) faces.*/\1/p' <<<"$msg")"
rep_after="$(sed -n -E 's/.*ReduceMesh: object [0-9]+: ([0-9]+) -> ([0-9]+) faces.*/\2/p' <<<"$msg")"
dev="$(sed -n -E 's/.*max deviation ([-0-9.e+]+).*/\1/p' <<<"$msg")"
rwms_check "RWMS_DECIMATE_sphere: the command reports the face counts before and after ($rep_before -> $rep_after) and they match What ($before_f, $after_f)" "$([ -n "$before_f" ] && [ "$rep_before" = "$before_f" ] && [ "$rep_after" = "$after_f" ]; echo $?)"
target="$(awk -v n="$before_f" 'BEGIN{ printf "%d", int(n * 0.5 + 0.5) }')"
rwms_check "RWMS_DECIMATE_sphere: faces after ($after_f) reach the target $target (half of $before_f) within one collapse (2 faces)" "$([ -n "$after_f" ] && [ -n "$target" ] && [ "$after_f" -le $((target + 2)) ] && [ "$after_f" -ge $((target - 2)) ]; echo $?)"
rwms_check "RWMS_DECIMATE_sphere: still a closed manifold after decimation" "$(rwms_block sphere | grep -c 'Closed: yes' | awk '{exit !($1 >= 2)}'; echo $?)"
v0="$(rwms_num sphere Volume 1)"; v1="$(rwms_num sphere Volume 2)"
rwms_check "RWMS_DECIMATE_sphere: volume before ($v0) is that of an inscribed polyhedron of the ball of radius 10 (below 4188.79)" "$(awk -v v="$v0" 'BEGIN{exit !(v > 3500 && v < 4188.8)}'; echo $?)"
rwms_check "RWMS_DECIMATE_sphere: volume after ($v1) stays within 10 % of the volume before and below the ball volume plus 2 %" "$(awk -v a="$v0" -v b="$v1" 'BEGIN{d = (b - a) / a; if (d < 0) d = -d; exit !(d <= 0.10 && b < 4188.79 * 1.02)}'; echo $?)"
rwms_check "RWMS_DECIMATE_sphere: reported maximal deviation ($dev) is at most 10 % of the radius (1)" "$(awk -v d="$dev" 'BEGIN{exit !(d != "" && d + 0 <= 1.0)}'; echo $?)"
