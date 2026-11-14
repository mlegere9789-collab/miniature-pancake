# ---------------------------------------------------------------------------
# MEX_: MoveEdge of ONE edge through the exact kernel route (moveedge_exact_script.txt). MovePartsCommand calls kernel::MoveEdgeConvexPlanar
# first (both endpoints move, every incident face is re-planed through its moved corners, the others are re-trimmed, the solid stays closed)
# and falls back to the approximate move only when the kernel declines, printing the kernel's reason. Expected values are closed forms of a
# 10 x 10 x 10 cube whose top-front edge is moved, independent of the app's printout:
#   up by d along z : V = 1000 + 50 d, S = 500 + 20 d + 10 sqrt(100 + d^2) ; d = +3 : 1150 / 664.403 ; d = -3 : 850 / 544.403
#   slid by 2 along +y in the top plane : V = 1000 - 100 = 900, S = 561.980
#   vertical edge (0,0,*) slid by 2 along +y : V = 900, S = 561.980 ; vertical edge (10,10,*) slid by -3 along x : V = 850, S = 544.403
#   every exact result must also be a VALID B-rep (the app's Check); the old approximate route gets the same volume and area but an INVALID one
#   slid by 2 along +x : the x = 0 face stops being planar, the kernel declines ; a cylinder's rim edge : non-planar wall, declined
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MEXO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/moveedge_exact_script.txt" 2>&1)" || { echo "$MEXO" | tail -30; echo "FAIL MEX_run: script exited non-zero"; fail=1; MEXO=""; }
else
  MEXO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/moveedge_exact_script.txt" 2>&1)" || { echo "$MEXO" | tail -30; echo "FAIL MEX_run: script exited non-zero"; fail=1; MEXO=""; }
fi
mex_ok() { echo "ok   $1"; }
mex_bad() { echo "FAIL $1"; fail=1; }
mex_block() { awk -v name="$1" '$0 ~ ("Unknown command: MEXBEGIN_" name "$") { on = 1; next } on && /Unknown command: MEXBEGIN_/ { exit } on { print }' <<<"$MEXO"; }
mex_has() { if grep -qF -- "$2" <<<"$(mex_block "$1")"; then mex_ok "$3"; else mex_bad "$3"; echo "     expected: $2"; { grep -E "^history: (MoveEdge|!)" <<<"$(mex_block "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }
mex_hasnot() { if grep -qF -- "$2" <<<"$(mex_block "$1")"; then mex_bad "$3"; else mex_ok "$3"; fi; }
mex_val() { { grep -oE "$2 = [0-9.eE+-]+" <<<"$(mex_block "$1")" || true; } | tail -1 | sed -E "s/$2 = //"; }
mex_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then mex_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else mex_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

mex_has up "MoveEdge: exact kernel route (MoveEdgeConvexPlanar): edge" "MEX_up: the edge move went through the exact kernel route"
mex_hasnot up "declined" "MEX_up: the kernel did not decline"
mex_num "$(mex_val up Volume)" 1150 0.003 "MEX_up: volume = 1000 + 50 x 3"
mex_num "$(mex_val up Area)" 664.403 0.003 "MEX_up: area = 500 + 20 x 3 + 10 sqrt(100 + 9)"
mex_has up "Object 1: valid" "MEX_up: the moved solid is a valid B-rep (Check)"
mex_has down "MoveEdge: exact kernel route (MoveEdgeConvexPlanar): edge" "MEX_down: the downward move is exact too"
mex_num "$(mex_val down Volume)" 850 0.003 "MEX_down: volume = 1000 - 50 x 3"
mex_num "$(mex_val down Area)" 544.403 0.003 "MEX_down: area = 500 - 60 + 10 sqrt(100 + 9)"
mex_has slide "MoveEdge: exact kernel route (MoveEdgeConvexPlanar): edge" "MEX_slide: sliding the edge within the top plane is exact"
mex_num "$(mex_val slide Volume)" 900 0.003 "MEX_slide: volume = 1000 - 50 x 2"
mex_num "$(mex_val slide Area)" 561.980 0.003 "MEX_slide: area = 100 + 100 + 80 + 10 sqrt(104) + 180"
mex_has slide "Object 1: valid" "MEX_slide: the moved solid is a valid B-rep (Check)"
mex_has vert "MoveEdge: exact kernel route (MoveEdgeConvexPlanar): edge" "MEX_vert: a vertical edge slid inward goes through the exact kernel route"
mex_num "$(mex_val vert Volume)" 900 0.003 "MEX_vert: volume = 1000 - 100"
mex_num "$(mex_val vert Area)" 561.980 0.003 "MEX_vert: area = 90 + 90 + 100 + 100 + 80 + 10 sqrt(104)"
mex_has vert "Object 1: valid" "MEX_vert: the moved solid is a valid B-rep (Check)"
mex_has vertx "MoveEdge: exact kernel route (MoveEdgeConvexPlanar): edge" "MEX_vertx: a vertical edge slid along x goes through the exact kernel route"
mex_num "$(mex_val vertx Volume)" 850 0.003 "MEX_vertx: volume = 85 x 10"
mex_num "$(mex_val vertx Area)" 544.403 0.003 "MEX_vertx: area = 85 + 85 + 100 + 70 + 100 + 10 sqrt(109)"
mex_has vertx "Object 1: valid" "MEX_vertx: the moved solid is a valid B-rep (Check)"
mex_has sidewaysx "the exact kernel route declined (" "MEX_sidewaysx: a slide that bends the x = 0 face is declined"
mex_has sidewaysx "planar" "MEX_sidewaysx: ... and the note carries the kernel's reason"
mex_has sidewaysx "so this is the approximate rigid edge move" "MEX_sidewaysx: ... and the approximate move ran"
mex_has sidewaysx "MoveEdge: 1 object(s) updated" "MEX_sidewaysx: the fallback updated the object"
mex_has cyl "the exact kernel route declined (" "MEX_cyl: a cylinder's rim edge is declined"
mex_has cyl "so this is the approximate rigid edge move" "MEX_cyl: ... and the approximate move ran"
