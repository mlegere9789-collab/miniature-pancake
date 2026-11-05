# SVI_: SubDVertexInfo / SubDCorner / SubDRemoveCorner wired to the kernel's SubD::VertexInfoAt / SetVertexCorner (svi_script.txt).
# Failing-first: before these commands existed the script printed "Unknown command: SubDVertexInfo" and nothing else.
# Expectations are hand-derived limit points of the 10-unit cube cage, not the kernel's own output:
#   smooth valence-3 vertex   (9*0 + 4*(10,10,10) + (20,20,20)) / 24 = (2.5, 2.5, 2.5)
#   Crease (two edges)        (a + 4 v + b) / 6 = ((10,0,0) + 0 + (0,10,0)) / 6 = (1.6667, 1.6667, 0)
#   pinned Corner             limit point == control point == (0, 0, 0)
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SVIO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/svi_script.txt" 2>&1)" || { echo "$SVIO"; echo "FAIL: svi script exited non-zero"; exit 1; }
else
  SVIO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/svi_script.txt" 2>&1)" || { echo "$SVIO"; echo "FAIL: svi script exited non-zero"; exit 1; }
fi
svi_ok() { echo "ok   $1"; }
svi_bad() { echo "FAIL $1"; fail=1; }
sviblock() { awk -v name="$1" '$0 ~ ("Unknown command: SVIBEGIN_" name "$") { on = 1; next } on && /Unknown command: SVIBEGIN_/ { exit } on { print }' <<<"$SVIO"; }
# sviinfo NAME N: the N-th "SubDVertexInfo: object ..." line of the case
sviinfo() { { grep -E "SubDVertexInfo: object [0-9]+ vertex" <<<"$(sviblock "$1")" || true; } | sed -n "${2}p"; }
# svilimit LINE: "x y z" of its "limit point (x, y, z)"
svilimit() { { grep -oE "limit point \([^)]*\)" <<<"$1" || true; } | head -1 | sed -E 's/limit point \(([^,]*), ([^,]*), ([^)]*)\)/\1 \2 \3/'; }
# svipoint LINE X Y Z TOL DESC: the line's limit point equals (X, Y, Z) within TOL
svipoint() {
  local got; got="$(svilimit "$1")"
  if [ -n "$got" ] && awk -v g="$got" -v x="$2" -v y="$3" -v z="$4" -v t="$5" 'BEGIN { split(g, a, " "); dx = a[1] - x; dy = a[2] - y; dz = a[3] - z; exit !(dx * dx + dy * dy + dz * dz <= t * t) }'; then
    svi_ok "$6 (limit point $got, hand-derived ($2, $3, $4) +-$5)"
  else
    svi_bad "$6 (limit point '${got:-none}', hand-derived ($2, $3, $4) +-$5)"; echo "     line: ${1:-none}"
  fi
}
svihas() { if grep -qF -- "$2" <<<"$1"; then svi_ok "$3"; else svi_bad "$3"; echo "     expected: $2"; echo "     line: ${1:-none}"; fi; }
svicount() { grep -cF -- "$2" <<<"$(sviblock "$1")" || true; }

# ---- smooth cube vertex
L="$(sviinfo smooth 1)"
svihas "$L" "Smooth, valence 3, 3 face(s), 0 crease edge(s)" "SVI_smooth: the origin of the plain cube is a Smooth valence-3 vertex with 3 faces and no crease"
svipoint "$L" 2.5 2.5 2.5 0.001 "SVI_smooth: limit point is the Halstead valence-3 value"
L="$(sviinfo smooth 2)"
svipoint "$L" 7.5 7.5 7.5 0.001 "SVI_smooth: the opposite corner's limit point is (7.5, 7.5, 7.5) by symmetry"
# ---- crease -> corner -> released
L="$(sviinfo corner 1)"
svihas "$L" "Crease, valence 3, 3 face(s), 2 crease edge(s)" "SVI_corner: two creased edges make the origin a Crease vertex"
svipoint "$L" 1.666667 1.666667 0 0.001 "SVI_corner: Crease limit point is (a + 4 v + b)/6"
PIN="$(sed -n '1p' <<<"$(grep -E 'SubDCorner: vertex' <<<"$(sviblock corner)" || true)")"
svihas "$PIN" "is now Corner" "SVI_corner: SubDCorner pins the Crease vertex"
svipoint "$PIN" 0 0 0 0.001 "SVI_corner: SubDCorner reports limit point == control point (0, 0, 0)"
L="$(sviinfo corner 2)"
svihas "$L" "Corner, valence 3, 3 face(s), 2 crease edge(s)" "SVI_corner: SubDVertexInfo now reads Corner"
svipoint "$L" 0 0 0 0.001 "SVI_corner: Corner limit point is the control point"
REL="$(sed -n '1p' <<<"$(grep -E 'SubDRemoveCorner: vertex' <<<"$(sviblock corner)" || true)")"
svihas "$REL" "is now Crease" "SVI_corner: SubDRemoveCorner releases it"
L="$(sviinfo corner 3)"
svihas "$L" "Crease, valence 3" "SVI_corner: released back to a Crease vertex"
svipoint "$L" 1.666667 1.666667 0 0.001 "SVI_corner: released limit point is the Crease value again"
# ---- negative controls
svihas "$(sviblock refused)" "cannot be pinned" "SVI_refused: a smooth vertex cannot be pinned (warning)"
svihas "$(sviblock refused)" "no SubD vertex near" "SVI_refused: a pick far from every vertex does nothing"
svihas "$(sviblock refused)" "0 vertex/vertices pinned" "SVI_refused: nothing was pinned"
L="$(sviinfo refused 1)"
svihas "$L" "Smooth, valence 3" "SVI_refused: the refused vertex is still Smooth"
svipoint "$L" 7.5 7.5 7.5 0.001 "SVI_refused: ...with its unchanged limit point"
