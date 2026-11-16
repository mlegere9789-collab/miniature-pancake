# RWMS_BRIDGE (D8-RHINO-SUBD-0002): Bridge joins two SubD faces with a tube. Oracle (closed form): two hexahedra have 6 faces, 8 vertices, 12 edges each; removing
# one face of each and adding a tube of 4 quads between the two 4-edge openings gives faces 5 + 5 + 4 = 14, vertices 16, edges 12 + 12 + 4 = 28, Euler
# characteristic 16 - 28 + 14 = 2 (a tube between two spheres is still a sphere), one merged SubD.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  RWMS_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/rwms_bridge_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_BRIDGE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
else
  RWMS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/rwms_bridge_script.txt" 2>&1)" || { echo "$RWMS_OUT" | tail -40; echo "FAIL RWMS_BRIDGE_run: script exited non-zero"; fail=1; RWMS_OUT=""; }
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

rwms_counts before 1 6 8 12 "RWMS_BRIDGE_before: a single box has 6/8/12"
rwms_counts bridge 1 14 16 28 "RWMS_BRIDGE_bridge: two boxes bridged: 5 + 5 + 4 faces, 8 + 8 vertices, 12 + 12 + 4 edges"
rwms_euler bridge 1 2 "RWMS_BRIDGE_bridge: Euler characteristic 2"
rwms_check "RWMS_BRIDGE_bridge: the command reports a tube of 4 quads and 2 SubDs merged into one" "$(rwms_block bridge | grep -qF 'joined 2 faces with a tube of 4 quads, 2 SubDs merged'; echo $?)"
rwms_check "RWMS_BRIDGE_bridge: exactly one SubD object is left (List shows one object)" "$([ "$(rwms_block bridge | grep -cE '^history: Object [0-9]+ \(SubD\)')" = 1 ]; echo $?)"
read -r x0 y0 z0 x1 y1 z1 <<<"$(rwms_bb bridge 1)"
rwms_check "RWMS_BRIDGE_bridge: the merged shell spans both boxes: x from near 0 to near 30 ($x0 .. $x1)" "$(awk -v a="$x0" -v b="$x1" 'BEGIN{exit !(a>0 && a<1.5 && b<30 && b>28.5)}'; echo $?)"
