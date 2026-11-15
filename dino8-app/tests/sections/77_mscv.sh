# MSCV: MatchSrf against a target CURVE is the kernel's exact NurbsSurface::MatchEdgeToCurve (mscv_script.txt).
# Before: the curve-target path moved each boundary control point onto the curve (and, for Tangency, nudged the next row), so a
# degree-1 wall edge facing an arc became a straight CHORD of it and Curvature was refused outright. Fixture: a vertical wall
# (SrfPt (1,-2,0) (6,-2,0) (6,-2,3) (1,-2,3), cross derivative (0,0,3); SrfPt indexes u from corner 1 to corner 4, i.e. up the wall, so the matched bottom edge is u = 0 and v runs along it) matched to the rational quarter arc centre (0,0,0),
# radius 5, from (5,0,0) to (0,5,0), at Continuity Position (G0), Tangency (G1) and Curvature (G2). Expectations are closed
# forms of the geometry, never the app's own printout of what it built:
#   * every probe of the matched edge (u = 0, five v values) lies on the circle x^2 + y^2 = 25 in the plane z = 0; the old chord
#     would put its midpoint at radius 5 cos(45 deg) = 3.536, so a 0.01 tolerance on the radius separates the two by 1.4;
#   * the two probed ends of the edge are the arc's ends (5,0,0) and (0,5,0), in either order;
#   * the far edge (u = 1) never moves: (3.5,-2,3) at v = 0.5 (a corner-to-corner straight line in the original wall);
#   * the wall keeps its own cross derivative (0,0,3) along the edge (preserved field), so at u = 0.002 the point has risen by
#     exactly 3 * 0.002 = 0.006 in z while x, y have not moved (they move only at second order, ~1e-4): the surface leaves the arc
#     axially, i.e. it is a cylinder wall there (radial normal).
# The app prints coordinates with 4 significant digits, so every tolerance is 0.01 (radius, positions) or 0.0005 (the z lift);
# the 1e-12 versions of the circle and field checks are in the kernel's tests/extra/match_edge_to_curve_rational.cpp.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MSCVO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/mscv_script.txt" 2>&1)" || { echo "$MSCVO"; echo "FAIL: mscv script exited non-zero"; exit 1; }
else
  MSCVO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/mscv_script.txt" 2>&1)" || { echo "$MSCVO"; echo "FAIL: mscv script exited non-zero"; exit 1; }
fi
mscv_blk() { awk -v name="$1" '$0 ~ ("Unknown command: MSCVBEGIN_" name "$") { on = 1; next } on && /Unknown command: MSCVBEGIN_/ { exit } on { print }' <<<"$MSCVO"; }
mscv_ok() { echo "ok   $1"; }
mscv_bad() { echo "FAIL $1"; fail=1; }
mscv_has() { # BLOCK PATTERN DESC (fixed string)
  if grep -qF -- "$2" <<<"$(mscv_blk "$1")"; then mscv_ok "$3"; else mscv_bad "$3"; echo "     expected: $2"; { mscv_blk "$1" | grep -E "MatchSrf|Warning|rror" || true; } | head -4 | cut -c1-240 | sed 's/^/     actual:   /'; fi
}
mscv_pt() { { grep -oE -- '-> [-0-9.eE+]+,[-0-9.eE+]+,[-0-9.eE+]+' <<<"$(mscv_blk "$1")" || true; } | head -1 | sed 's/^-> //'; }
mscv_circle() { # BLOCK DESC : the probe lies on x^2 + y^2 = 25, z = 0 (tolerance 0.01)
  local pt; pt="$(mscv_pt "$1")"
  if [ -n "$pt" ] && awk -v pt="$pt" 'BEGIN { if (split(pt, a, ",") != 3) exit 1; d = sqrt(a[1]^2 + a[2]^2) - 5; if (d < 0) d = -d; z = a[3]; if (z < 0) z = -z; exit !(d <= 0.01 && z <= 0.01) }'; then mscv_ok "$2 (probe $pt)"; else mscv_bad "$2 (probe '${pt:-none}')"; fi
}
mscv_near() { # BLOCK X Y Z DESC : the probe is within 0.01 of (X,Y,Z)
  local pt; pt="$(mscv_pt "$1")"
  if [ -n "$pt" ] && awk -v pt="$pt" -v x="$2" -v y="$3" -v z="$4" 'BEGIN { if (split(pt, a, ",") != 3) exit 1; exit !(sqrt((a[1]-x)^2 + (a[2]-y)^2 + (a[3]-z)^2) <= 0.01) }'; then mscv_ok "$5 (probe $pt)"; else mscv_bad "$5 (probe '${pt:-none}', expected $2,$3,$4)"; fi
}
mscv_ends() { # CASE : the two end probes are the arc's two ends, in either order
  local p0 p1; p0="$(mscv_pt "$1_e0")"; p1="$(mscv_pt "$1_e4")"
  if [ -n "$p0" ] && [ -n "$p1" ] && awk -v p0="$p0" -v p1="$p1" 'BEGIN {
        split(p0, a, ","); split(p1, b, ",");
        d = mscvd(a, 5, 0) + mscvd(b, 0, 5); e = mscvd(a, 0, 5) + mscvd(b, 5, 0);
        exit !((d <= 0.02) || (e <= 0.02)) }
      function mscvd(p, x, y) { return sqrt((p[1]-x)^2 + (p[2]-y)^2 + p[3]^2) }'; then mscv_ok "MSCV_$1: the edge ends are the arc's ends (5,0,0) and (0,5,0) (probes $p0 / $p1)"; else mscv_bad "MSCV_$1: the edge ends are not the arc's ends (probes '${p0:-none}' / '${p1:-none}')"; fi
}
mscv_lift() { # CASE : at v = 0.002 the point is 0.006 above the edge point at the same u, x and y unmoved (axial departure)
  local e l; e="$(mscv_pt "$1_e2")"; l="$(mscv_pt "$1_lift")"
  if [ -n "$e" ] && [ -n "$l" ] && awk -v e="$e" -v l="$l" 'BEGIN {
        split(e, a, ","); split(l, b, ",");
        dz = b[3] - a[3] - 0.006; if (dz < 0) dz = -dz; dxy = sqrt((b[1]-a[1])^2 + (b[2]-a[2])^2);
        exit !(dz <= 0.0005 && dxy <= 0.002) }'; then mscv_ok "MSCV_$1: the wall leaves the arc axially (z rises by 3*0.002 = 0.006, x,y unmoved: edge $e, lifted $l)"; else mscv_bad "MSCV_$1: the departure from the arc is not the wall's own (0,0,3) direction (edge '${e:-none}', lifted '${l:-none}')"; fi
}
for c in g0 g1 g2; do
  case $c in g0) g=G0;; g1) g=G1;; g2) g=G2;; esac
  mscv_has $c "MatchSrf: exact edge match ($g) to the target curve, max position error 0" "MSCV_$c: MatchSrf reports the kernel's exact curve match at $g, residual 0 (not the per-CV fallback)"
  for i in 0 1 2 3 4; do mscv_circle ${c}_e$i "MSCV_$c: edge probe $i of 5 lies on the circle of radius 5 about the origin in the plane z = 0"; done
  mscv_ends $c
  mscv_near ${c}_far 3.5 -2 3 "MSCV_$c: the far edge never moved (u = 1, v = 0.5 is still (3.5,-2,3))"
done
mscv_has g1 "max tangent error 0" "MSCV_g1: the G1 match reports a zero tangent residual"
mscv_has g2 "max curvature error 0" "MSCV_g2: the G2 match reports a zero curvature residual"
mscv_lift g1
mscv_lift g2
# negative controls: the old chord and the old refusal are excluded, and Position really is the weaker match
if awk -v pt="$(mscv_pt g1_e2)" 'BEGIN { if (split(pt, a, ",") != 3) exit 1; d = sqrt(a[1]^2 + a[2]^2) - 3.5355; if (d < 0) d = -d; exit !(d > 1.0) }'; then mscv_ok "MSCV: the G1 edge midpoint is not the chord's (radius 3.536) - the old straight-chord result is excluded"; else mscv_bad "MSCV: the G1 edge midpoint is on the old chord (probe '$(mscv_pt g1_e2)')"; fi
if grep -qF -- "a target curve has no curvature-match fallback" <<<"$(mscv_blk g2)"; then mscv_bad "MSCV: Curvature against a curve is still refused with the old no-fallback message"; else mscv_ok "MSCV: Curvature against a curve is no longer refused with the old no-fallback message"; fi
