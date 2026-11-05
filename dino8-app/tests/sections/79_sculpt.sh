# SCU_: SubDSculpt drags a SubD control vertex with a falloff (sculpt_script.txt).
# Expectations are closed forms computed here in awk from the fixture (the 10-unit cube cage, origin dragged by (0,0,5)), never the app's output:
#   falloff weight, t = distance from the ORIGINAL origin / Radius:   Constant 1, Linear 1 - t, Smooth s^2 (3 - 2 s) with s = 1 - t, 0 for t > 1
#   distances from the origin: edge neighbours 10, face-diagonal vertices 10 sqrt 2 = 14.142, far corner 10 sqrt 3 = 17.32
#   moved control point of the edge neighbour (10,0,0):  z = 5 w(10 / Radius)
#   limit point of the dragged vertex (valence 3, Halstead): (9 v + 4 sum(edge neighbours) + sum(face-diagonal vertices)) / 24
#       Constant R = 11:  v = (0,0,5), edge neighbours (10,0,5) (0,10,5) (0,0,15), diagonals unchanged -> (2.5, 2.5, 165/24 = 6.875)
#       R = 5 (only the dragged vertex moves): (2.5, 2.5, (45 + 40 + 20)/24 = 4.375)
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SCUO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sculpt_script.txt" 2>&1)" || { echo "$SCUO"; echo "FAIL: sculpt script exited non-zero"; exit 1; }
else
  SCUO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sculpt_script.txt" 2>&1)" || { echo "$SCUO"; echo "FAIL: sculpt script exited non-zero"; exit 1; }
fi
scu_ok() { echo "ok   $1"; }
scu_bad() { echo "FAIL $1"; fail=1; }
scublock() { awk -v name="$1" '$0 ~ ("Unknown command: SCUBEGIN_" name "$") { on = 1; next } on && /Unknown command: SCUBEGIN_/ { exit } on { print }' <<<"$SCUO"; }
scuinfo() { { grep -E "SubDVertexInfo: object [0-9]+ vertex" <<<"$(scublock "$1")" || true; } | sed -n "${2}p"; }
# scuw FALLOFF T: the falloff weight
scuw() { awk -v f="$1" -v t="$2" 'BEGIN { if (t > 1) w = 0; else if (f == "Constant") w = 1; else { s = 1 - t; w = (f == "Linear") ? s : s * s * (3 - 2 * s) } printf "%.9f", w }'; }
# scuexpect FALLOFF R: "x y z" of the dragged vertex's limit point from the closed form
scuexpect() {
  awk -v f="$1" -v R="$2" '
    function w(d,   t, s) { t = d / R; if (t > 1) return 0; if (f == "Constant") return 1; s = 1 - t; return (f == "Linear") ? s : s * s * (3 - 2 * s) }
    BEGIN {
      dz = 5; e = w(10); g = w(sqrt(200))
      vz = dz                                  # dragged vertex (0,0,0) -> (0,0,5): weight 1
      ez = 2 * (dz * e) + (10 + dz * e)        # edge neighbours (10,0,0) (0,10,0) (0,0,10): z
      gz = dz * g + (10 + dz * g) + (10 + dz * g)  # face-diagonal vertices (10,10,0) (10,0,10) (0,10,10): z
      ex = 10; gx = 20                          # x (and y) sums of the unmoved coordinates
      printf "%.6f %.6f %.6f", (4 * ex + gx) / 24, (4 * ex + gx) / 24, (9 * vz + 4 * ez + gz) / 24
    }'
}
# scupoint LINE "x y z" TOL DESC: the line's limit point equals the point
scupoint() {
  local got want; got="$({ grep -oE "limit point \([^)]*\)" <<<"$1" || true; } | head -1 | sed -E 's/limit point \(([^,]*), ([^,]*), ([^)]*)\)/\1 \2 \3/')"; want="$2"
  if [ -n "$got" ] && awk -v g="$got" -v w="$want" -v t="$3" 'function pt(w,   l, f) { if (w < 0) w = -w; if (w < 1e-9) return 0; l = log(w) / log(10) + 1e-9; f = int(l); if (l < f) f -= 1; return 0.51 * 10 ^ (f - 3) }
    BEGIN { split(g, a, " "); split(w, b, " "); ok = 1; for (i = 1; i <= 3; i++) { d = a[i] - b[i]; if (d < 0) d = -d; e = pt(b[i]); if (e < t) e = t; if (d > e) ok = 0 } exit !ok }'; then
    scu_ok "$4 (limit point $got, closed form $want, per component +-max($3, half a unit of the app's 4th printed significant digit))"
  else
    scu_bad "$4 (limit point '${got:-none}', closed form $want, per component +-max($3, half a unit of the app's 4th printed significant digit))"; echo "     line: ${1:-none}"
  fi
}
# scuvertexz LINE: the z of the "vertex (x, y, z)" control point
scuvertexz() { { grep -oE "vertex \([^)]*\)" <<<"$1" || true; } | head -1 | sed -E 's/vertex \(([^,]*), ([^,]*), ([^)]*)\)/\3/'; }
scunum() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'function pt(w,   l, f) { if (w < 0) w = -w; if (w < 1e-9) return 0; l = log(w) / log(10) + 1e-9; f = int(l); if (l < f) f -= 1; return 0.51 * 10 ^ (f - 3) }
    BEGIN { d = v - e; if (d < 0) d = -d; m = pt(e); if (m < t) m = t; exit !(d <= m) }'; then scu_ok "$4 (measured $1, closed form $2, +-max($3, half a unit of the app's 4th printed significant digit))"; else scu_bad "$4 (measured '${1:-none}', closed form $2, +-max($3, half a unit of the app's 4th printed significant digit))"; fi; }
scuhas() { if grep -qF -- "$2" <<<"$(scublock "$1")"; then scu_ok "$3"; else scu_bad "$3"; echo "     expected: $2"; { grep -E "^history: (SubDSculpt|!)" <<<"$(scublock "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }

# ---- Constant, radius 11: hand-derived exact limit point (2.5, 2.5, 6.875)
scuhas constant "4 control point(s) within radius 11 moved by Constant falloff" "SCU_constant: the dragged vertex and its 3 edge neighbours move (4 control points)"
scupoint "$(scuinfo constant 1)" "2.5 2.5 6.875" 0.001 "SCU_constant: the dragged vertex's limit point"
scunum "$(scuvertexz "$(scuinfo constant 2)")" 5 0.001 "SCU_constant: the edge neighbour (10,0,0) moved by the full (0,0,5)"
# ---- Linear and Smooth, radius 15
for F in linear smooth; do
  FN="$([ "$F" = linear ] && echo Linear || echo Smooth)"
  scuhas "$F" "7 control point(s) within radius 15 moved by $FN falloff" "SCU_$F: the dragged vertex, 3 edge neighbours and 3 face-diagonal vertices move (7 of 8); the far corner stays"
  scupoint "$(scuinfo "$F" 1)" "$(scuexpect "$FN" 15)" 0.001 "SCU_$F: the dragged vertex's limit point from the closed-form weights"
  scunum "$(scuvertexz "$(scuinfo "$F" 2)")" "$(awk -v w="$(scuw "$FN" 0.666666667)" 'BEGIN { printf "%.6f", 5 * w }')" 0.001 "SCU_$F: the edge neighbour (10,0,0) moved by 5 * w(10/15)"
done
# ---- radius 5: only the dragged vertex moves
scuhas single "1 control point(s) within radius 5 moved by Smooth falloff" "SCU_single: radius 5 moves only the dragged vertex"
scupoint "$(scuinfo single 1)" "2.5 2.5 4.375" 0.001 "SCU_single: the dragged vertex's limit point is (2.5, 2.5, 105/24)"
scunum "$(scuvertexz "$(scuinfo single 2)")" 0 0.001 "SCU_single: the edge neighbour stays at z = 0"
# ---- a miss does nothing
scuhas miss "no SubD vertex near" "SCU_miss: a first pick far from every vertex is refused"
scupoint "$(scuinfo miss 1)" "2.5 2.5 2.5" 0.001 "SCU_miss: the cube is untouched (origin limit point still (2.5, 2.5, 2.5))"
