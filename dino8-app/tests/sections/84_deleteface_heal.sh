# ---------------------------------------------------------------------------
# DFH_: DeleteFaces Heal=Yes through the exact kernel heal (deleteface_heal_script.txt). FacePickCommand's DeleteFaces gained the option Heal
# (default No: the old plain delete is unchanged); with Heal=Yes it calls kernel::DeleteFaceHealConvexPlanar and, when the kernel declines,
# prints the kernel's own message and deletes the face the old way. Expected values are closed forms of a 10 x 10 x 10 cube (V = 1000,
# S = 600) with distance-2 chamfers on its top edges, independent of the app's printout:
#   one chamfer   : V = 1000 - 2 x 2 / 2 x 10 = 980 ; healed : V = 1000, S = 600
#   two chamfers  : opposite top edges, V = 960 ; first heal 980 ; second heal 1000
#   plain cube top: nothing to regrow into, the kernel declines ("genuinely unbounded"), the face is deleted, 5 faces are left
#   cylinder wall : not planar, the kernel declines naming it
#   default       : plain delete, 6 faces left, no heal message
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DFHO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/deleteface_heal_script.txt" 2>&1)" || { echo "$DFHO" | tail -30; echo "FAIL DFH_run: script exited non-zero"; fail=1; DFHO=""; }
else
  DFHO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/deleteface_heal_script.txt" 2>&1)" || { echo "$DFHO" | tail -30; echo "FAIL DFH_run: script exited non-zero"; fail=1; DFHO=""; }
fi
dfh_ok() { echo "ok   $1"; }
dfh_bad() { echo "FAIL $1"; fail=1; }
dfh_block() { awk -v name="$1" '$0 ~ ("Unknown command: DFHBEGIN_" name "$") { on = 1; next } on && /Unknown command: DFHBEGIN_/ { exit } on { print }' <<<"$DFHO"; }
dfh_has() { if grep -qF -- "$2" <<<"$(dfh_block "$1")"; then dfh_ok "$3"; else dfh_bad "$3"; echo "     expected: $2"; { grep -E "^history: (DeleteFaces|!)" <<<"$(dfh_block "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }
dfh_hasnot() { if grep -qF -- "$2" <<<"$(dfh_block "$1")"; then dfh_bad "$3"; else dfh_ok "$3"; fi; }
# the Nth "<name> = <number>" of a block (N = 1 is the first Volume/Area line printed in it)
dfh_val() { { grep -oE "$2 = [0-9.eE+-]+" <<<"$(dfh_block "$1")" || true; } | sed -n "${3}p" | sed -E "s/$2 = //"; }
dfh_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then dfh_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else dfh_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

dfh_num "$(dfh_val one Volume 1)" 980 0.003 "DFH_one: the chamfered cube before the heal (1000 - 2 x 2 / 2 x 10)"
dfh_has one "DeleteFaces: exact kernel heal (DeleteFaceHealConvexPlanar)" "DFH_one: the chamfer face went through the exact kernel heal"
dfh_hasnot one "declined" "DFH_one: the kernel did not decline"
dfh_num "$(dfh_val one Volume 2)" 1000 0.003 "DFH_one: the healed solid is the cube again, volume = 10^3"
dfh_num "$(dfh_val one Area 1)" 600 0.003 "DFH_one: ... and area = 6 x 10^2"
dfh_num "$(dfh_val two Volume 1)" 960 0.003 "DFH_two: two opposite chamfers (1000 - 2 x 20)"
dfh_num "$(dfh_val two Volume 2)" 980 0.003 "DFH_two: one heal leaves one chamfer (1000 - 20)"
dfh_has two_second "DeleteFaces: exact kernel heal (DeleteFaceHealConvexPlanar)" "DFH_two: the second chamfer healed exactly too"
dfh_num "$(dfh_val two_second Volume 1)" 1000 0.003 "DFH_two: both healed, volume = 10^3"
dfh_num "$(dfh_val two_second Area 1)" 600 0.003 "DFH_two: ... and area = 6 x 10^2"
dfh_has plain "the exact kernel heal declined (" "DFH_plain: a cube's top face has nothing to regrow into, the kernel declines"
dfh_has plain "genuinely unbounded" "DFH_plain: ... and the note carries the kernel's reason"
dfh_has plain "5 face(s) left" "DFH_plain: the face was deleted the old way (5 faces left)"
dfh_num "$(dfh_val plain Area 1)" 500 0.003 "DFH_plain: the open box has area 5 x 10^2"
dfh_has cyl "the exact kernel heal declined (" "DFH_cyl: a cylinder's wall is not planar, the kernel declines"
dfh_has cyl "not planar" "DFH_cyl: ... and the note carries the kernel's reason"
dfh_hasnot default "exact kernel heal" "DFH_default: without Heal the old plain delete runs (no kernel heal message)"
dfh_has default "6 face(s) left" "DFH_default: the chamfer face was simply deleted (6 faces left)"
