# SSC_: ToSubD of a plain NURBS surface builds a quad cage on the surface's own (u, v) grid, interpolates it onto the surface and measures the
# limit surface against the exact surface (sfcage_script.txt).
# Failing-first: before this change a Surface went through MeshOf (an adaptive triangle mesh, two triangles for a flat plane) into
# SubD::FromControlMesh, with no "NURBS surface cage" in the status line, no interpolation and no measured deviation.
# Hand-derived expectations (the cage starts at 6 x 6 cells and doubles per round while the measured deviation exceeds the 0.001 tolerance):
#   flat 12 x 12 plane, 6 x 6 cells: (6+1)^2 = 49 vertices, of which the 4 pinned sheet corners are Corner, the other 20 boundary vertices
#       (4 * 6 - 4) are Crease and the (6-1)^2 = 25 interior ones are Smooth; edges 2 * 6 * 7 = 84, of which 4 * 6 = 24 are boundary edges;
#       the spacing is 12 / 6 = 2, so the cage vertex (4,6,0) is the surface point (u,v) = (1/3, 1/2) and its limit point must be (4,6,0) itself
#       (the limit surface passes through every cage vertex), and the corner (12,12,0) likewise; a flat sheet has deviation 0
#   bilinear patch z = x y / 10: the surface point at x = y = 5 is z = 25/10 = 2.5 and at (10,10) z = 10; the cage vertices at those points are
#       interpolated, so their limit points are (5,5,2.5) and (10,10,10)
#   quarter cylinder radius 10, height 10, N x N cells: (N+1)^2 vertices, N^2 faces and 2 N (N+1) edges; the vertex at the middle of the
#       height on the u = 0 edge is (10,0,5), and by symmetry the one at the middle of both directions is (10 cos 45, 10 sin 45, 5) = (7.0711, 7.0711, 5)
#       (N is even, 6 doubling, so both are cage vertices); a curved surface has a measured deviation above 0 and, verified, at most the tolerance
#   after one SubDivide the new vertices lie between the old cage vertices, so their limit points probe the surface off the interpolated points:
#       bilinear patch: every limit point has z = x y / 10 (the limit surface IS the patch), within 0.002 (the 0.001 tolerance plus the half-unit
#       rounding of the 4 significant digits the app prints). Not done for the cylinder: one SubDivide of its 2304-face cage made the app take more than
#       ten minutes (display / pick cost, not the cage), so its off-vertex deviation is checked outside the app, see below
#   cylinder deviation, closed form: a limit point (x, y, z) of the cylinder has deviation |sqrt(x^2 + y^2) - 10|. A kernel stand-in that builds this very cage
#       (same SurfaceCageForSurface text) measured that radial error over all 9409 limit points of one subdivision step: 0.000399, equal to the app's reported
#       "worst measured deviation 0.000399" (which comes from the exact closest point on the NURBS surface, not from the radius), so the check below
#       that the reported figure lies in (0, 0.001] is anchored to the closed form
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SSCO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sfcage_script.txt" 2>&1)" || { echo "$SSCO"; echo "FAIL: sfcage script exited non-zero"; exit 1; }
else
  SSCO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sfcage_script.txt" 2>&1)" || { echo "$SSCO"; echo "FAIL: sfcage script exited non-zero"; exit 1; }
fi
ssc_ok() { echo "ok   $1"; }
ssc_bad() { echo "FAIL $1"; fail=1; }
sscblock() { awk -v name="$1" '$0 ~ ("Unknown command: SSCBEGIN_" name "$") { on = 1; next } on && /Unknown command: SSCBEGIN_/ { exit } on { print }' <<<"$SSCO"; }
ssctoline() { { grep -E "ToSubD: created" <<<"$(sscblock "$1")" || true; } | head -1; }
sschas() { if grep -qF -- "$2" <<<"$(ssctoline "$1")"; then ssc_ok "$3"; else ssc_bad "$3"; echo "     expected: $2"; echo "     line: $(ssctoline "$1" | cut -c1-400)"; fi; }
# sscn CASE KIND(vertices|edges) TYPE: the count printed by SelSubDVertices / SelSubDEdges for the type
sscn() {
  local pat
  if [ "$2" = edges ]; then pat="SelSubDEdges: ([0-9]+) $3 edge\\(s\\) on [0-9]+ SubD\\(s\\)"; else pat="SelSubDVertices: ([0-9]+) $3 vertex/vertices on [0-9]+ SubD\\(s\\)"; fi
  { grep -E -- "$pat" <<<"$(sscblock "$1")" || true; } | head -1 | sed -E "s#.*$pat.*#\\1#"
}
sscexpect() { if [ "$1" = "$2" ]; then ssc_ok "$3 ($1)"; else ssc_bad "$3 (counted '${1:-none}', hand-derived $2)"; fi; }
# sscinfo CASE N: the N-th SubDVertexInfo line; sscpoint LINE "x y z" TOL DESC: its limit point
sscinfo() { { grep -E "SubDVertexInfo: object [0-9]+ vertex" <<<"$(sscblock "$1")" || true; } | sed -n "${2}p"; }
sscpoint() {
  local got; got="$({ grep -oE "limit point \([^)]*\)" <<<"$1" || true; } | head -1 | sed -E 's/limit point \(([^,]*), ([^,]*), ([^)]*)\)/\1 \2 \3/')"
  if [ -n "$got" ] && awk -v g="$got" -v w="$2" -v t="$3" 'BEGIN { split(g, a, " "); split(w, b, " "); ok = 1; for (i = 1; i <= 3; i++) { d = a[i] - b[i]; if (d < 0) d = -d; if (d > t) ok = 0 } exit !ok }'; then
    ssc_ok "$4 (limit point $got, hand-derived $2, +-$3)"
  else
    ssc_bad "$4 (limit point '${got:-none}', hand-derived $2, +-$3)"; echo "     line: ${1:-none}"
  fi
}
# sscdev CASE: the "worst measured deviation X" of the ToSubD line
sscdev() { { grep -oE "worst measured deviation [0-9.]+" <<<"$(ssctoline "$1")" || true; } | head -1 | sed -E 's/worst measured deviation //'; }
# sscgrid CASE: the N of "(N x N quad grid"
sscgrid() { { grep -oE "\(([0-9]+) x \1 quad grid" <<<"$(ssctoline "$1")" || true; } | head -1 | sed -E 's/\(([0-9]+) x .*/\1/'; }

# ---- flat plane
sschas plane "NURBS surface cage (6 x 6 quad grid of the surface's own parametrisation, 36 cage faces" "SSC_plane: a 6 x 6 quad cage (36 faces) on the plane's own grid"
sschas plane "1 of 1 limit-surface-fidelity verified within tolerance 0.001000, worst measured deviation 0.000000" "SSC_plane: verified, a flat sheet measures deviation 0"
sscexpect "$(sscn plane vertices all)" 49 "SSC_plane: (6+1)^2 cage vertices"
sscexpect "$(sscn plane vertices corner)" 4 "SSC_plane: the 4 sheet corners are pinned Corners"
sscexpect "$(sscn plane vertices crease)" 20 "SSC_plane: the other 20 boundary vertices are Crease"
sscexpect "$(sscn plane vertices smooth)" 25 "SSC_plane: the (6-1)^2 interior vertices are Smooth"
sscexpect "$(sscn plane edges all)" 84 "SSC_plane: 2 * 6 * 7 cage edges (a quad grid, no diagonals)"
sscexpect "$(sscn plane edges boundary)" 24 "SSC_plane: 4 * 6 boundary edges"
sscpoint "$(sscinfo plane 1)" "4 6 0" 0.001 "SSC_plane: the cage vertex (4,6,0) has its limit point on itself"
sscpoint "$(sscinfo plane 2)" "12 12 0" 0.001 "SSC_plane: the corner (12,12,0) has its limit point on itself"

# ---- bilinear patch
sschas twisted "NURBS surface cage (" "SSC_twisted: the NURBS surface cage path was taken"
sschas twisted "1 of 1 limit-surface-fidelity verified within tolerance 0.001000" "SSC_twisted: verified within the tolerance"
sscpoint "$(sscinfo twisted 1)" "5 5 2.5" 0.001 "SSC_twisted: the limit point at x = y = 5 is z = 5 * 5 / 10 = 2.5"
sscpoint "$(sscinfo twisted 2)" "10 10 10" 0.001 "SSC_twisted: the corner (10,10) limit point is z = 10"

# ---- quarter cylinder
sschas cylinder "NURBS surface cage (" "SSC_cylinder: the NURBS surface cage path was taken"
sschas cylinder "1 of 1 limit-surface-fidelity verified within tolerance 0.001000" "SSC_cylinder: verified within the tolerance"
CYN="$(sscgrid cylinder)"; CYD="$(sscdev cylinder)"
if [ -n "$CYN" ] && [ $((CYN % 2)) -eq 0 ]; then ssc_ok "SSC_cylinder: the cage is $CYN x $CYN cells (even, as the 6-doubling grid is)"; else ssc_bad "SSC_cylinder: grid size '${CYN:-none}'"; fi
sschas cylinder "$((CYN * CYN)) cage faces" "SSC_cylinder: N x N cells are N^2 cage faces"
# the List line of the SubD: "F faces, E edges, V vertices, C creases" - an N x N quad grid has N^2 faces, 2 N (N+1) edges and (N+1)^2 vertices
CYL="$({ grep -E "[0-9]+ faces, [0-9]+ edges, [0-9]+ vertices" <<<"$(sscblock cylinder)" || true; } | head -1)"
sscexpect "$(grep -oE '[0-9]+ faces' <<<"$CYL" | grep -oE '[0-9]+')" "$((CYN * CYN))" "SSC_cylinder: List reports N^2 faces"
sscexpect "$(grep -oE '[0-9]+ edges' <<<"$CYL" | grep -oE '[0-9]+')" "$((2 * CYN * (CYN + 1)))" "SSC_cylinder: List reports 2 N (N+1) edges"
sscexpect "$(grep -oE '[0-9]+ vertices' <<<"$CYL" | grep -oE '[0-9]+')" "$(((CYN + 1) * (CYN + 1)))" "SSC_cylinder: List reports (N+1)^2 vertices"
if [ -n "$CYD" ] && awk -v d="$CYD" 'BEGIN { exit !(d > 0 && d <= 0.001) }'; then ssc_ok "SSC_cylinder: a curved surface measures a deviation above 0 and within the tolerance ($CYD)"; else ssc_bad "SSC_cylinder: measured deviation '${CYD:-none}' not in (0, 0.001]"; fi
sscpoint "$(sscinfo cylinder 1)" "10 0 5" 0.001 "SSC_cylinder: the edge vertex (10,0,5) has its limit point on itself"
sscpoint "$(sscinfo cylinder 2)" "7.0711 7.0711 5" 0.001 "SSC_cylinder: the middle vertex (10 cos 45, 10 sin 45, 5) has its limit point on itself"
# sscoff CASE FROM TO KIND DESC: SubDVertexInfo lines FROM..TO each lie on the surface (twisted: z = x y / 10; cylinder: radius 10 and 0 <= z <= 10)
sscoff() {
  local n line got ok=1 desc=""
  for n in $(seq "$2" "$3"); do
    line="$(sscinfo "$1" "$n")"
    got="$({ grep -oE "limit point \([^)]*\)" <<<"$line" || true; } | head -1 | sed -E 's/limit point \(([^,]*), ([^,]*), ([^)]*)\)/\1 \2 \3/')"
    if [ -z "$got" ]; then ok=0; desc="$desc [line $n: no limit point]"; continue; fi
    if awk -v g="$got" -v k="$4" 'BEGIN { split(g, a, " "); if (k == "twisted") { d = a[3] - a[1] * a[2] / 10; if (d < 0) d = -d; exit !(d <= 0.002) } else { r = sqrt(a[1] * a[1] + a[2] * a[2]) - 10; if (r < 0) r = -r; exit !(r <= 0.002 && a[3] >= -0.002 && a[3] <= 10.002) } }'; then desc="$desc [($got) on it]"; else ok=0; desc="$desc [($got) OFF the surface]"; fi
  done
  if [ "$ok" = 1 ]; then ssc_ok "$5:$desc"; else ssc_bad "$5:$desc"; fi
}
sscoff twisted 3 5 twisted "SSC_twisted: after SubDivide, 3 limit points between the old cage vertices satisfy z = x y / 10"
