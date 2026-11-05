# MSCF: MatchSrf Follow=Yes - an edge matched to a curve lying on a HOST surface leaves the curve in the host's tangent plane
# (kernel NurbsSurface::MatchEdgeToCurveFollowing; mscf_script.txt). Fixture: the host is the sphere of radius 5 about the origin, the
# target is the quarter arc of its latitude circle z = 3 (centre (0,0,3), radius 4, (4,0,3) to (0,4,3): 4^2 + 3^2 = 5^2), and the wall is
# SrfPt (1,-2,3) (6,-2,3) (6,-2,6) (1,-2,6), whose own cross derivative is axial, (0,0,3) per unit of u (u runs up the wall, v along x; the
# matched bottom edge is u = 0). Expectations are closed forms of that geometry, never the app's own printout:
#   * every probe of the matched edge (u = 0, five v values) lies on the circle: x^2 + y^2 = 16, z = 3, and on the sphere |p| = 5;
#   * the edge ends are the arc's ends (4,0,3) and (0,4,3), in either order; the far edge (u = 1, v = 0.5) is still (3.5,-2,6);
#   * FOLLOW: the sphere's normal at p is p/5 and the axial direction (0,0,h) projected into the tangent plane and rescaled to the wall's
#     speed h is h (-0.6 cos t, -0.6 sin t, 0.8). So at u = 0.02 (h = 3) the point has moved by 0.02 h (...) = (radially in by 0.036, up by
#     0.048): |p| stays 5 (first order; the measured second-order term is ~1e-3), hypot(x, y) falls to 4 - 0.036 and z rises by 0.048;
#   * PLAIN (Follow=No, the preserved axial direction): the same probe rises by 0.06 with hypot(x, y) unchanged, i.e. it leaves the sphere
#     radially: |p| = 5 + 0.02 * 3 * (3/5) = 5.036. This is the negative control that separates a followed match from a preserved one;
#   * Follow with Continuity=Position, and Follow with a curve 0.3 off the sphere, are refused with a reason and leave the wall untouched.
#   * FOLLOW2 (Continuity=Curvature): the second fundamental form of the sphere is added, so the departure stays on the sphere to third
#     order: at u = 0.2 the G1 follow is outside it by |p| - 5 = 0.0226 (straight tangent departure, s^2/(2R) with s = 0.6) while the G2 follow
#     is within 0.0041 (emulated with the kernel call the command makes: |p| = 5.02258 and 5.00405);
# The app prints 4 significant digits, so the tolerances are 0.01 on positions and radii and +-0.008 on the 0.02-scale moves; the 1e-6 versions of the
# tangent-plane and field checks are in the kernel's tests/extra/match_edge_to_curve_following.cpp.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MSCFO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/mscf_script.txt" 2>&1)" || { echo "$MSCFO"; echo "FAIL: mscf script exited non-zero"; exit 1; }
else
  MSCFO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/mscf_script.txt" 2>&1)" || { echo "$MSCFO"; echo "FAIL: mscf script exited non-zero"; exit 1; }
fi
mscf_blk() { awk -v name="$1" '$0 ~ ("Unknown command: MSCFBEGIN_" name "$") { on = 1; next } on && /Unknown command: MSCFBEGIN_/ { exit } on { print }' <<<"$MSCFO"; }
mscf_ok() { echo "ok   $1"; }
mscf_bad() { echo "FAIL $1"; fail=1; }
mscf_has() { # BLOCK PATTERN DESC (fixed string)
  if grep -qF -- "$2" <<<"$(mscf_blk "$1")"; then mscf_ok "$3"; else mscf_bad "$3"; echo "     expected: $2"; { mscf_blk "$1" | grep -E "MatchSrf|Warning|rror" || true; } | head -4 | cut -c1-260 | sed 's/^/     actual:   /'; fi
}
mscf_lacks() { if grep -qF -- "$2" <<<"$(mscf_blk "$1")"; then mscf_bad "$3"; else mscf_ok "$3"; fi; }
mscf_pt() { { grep -oE -- '-> [-0-9.eE+]+,[-0-9.eE+]+,[-0-9.eE+]+' <<<"$(mscf_blk "$1")" || true; } | head -1 | sed 's/^-> //'; }
mscf_chk() { # PROBE_A PROBE_B AWK_CONDITION DESC : a[1..3] / b[1..3] are the two probes (b empty for one-probe checks)
  local pa pb; pa="$(mscf_pt "$1")"; pb=""; [ -n "$2" ] && pb="$(mscf_pt "$2")"
  if [ -n "$pa" ] && { [ -z "$2" ] || [ -n "$pb" ]; } && awk -v pa="$pa" -v pb="$pb" "BEGIN { split(pa, a, \",\"); split(pb, b, \",\"); exit !($3) }"; then
    mscf_ok "$4 (probes $pa${pb:+ / $pb})"; else mscf_bad "$4 (probes '${pa:-none}'${pb:+ / '$pb'})"; fi
}
mscf_edge() { # CASE : the five edge probes are on the latitude circle (x^2+y^2 = 16, z = 3) and on the sphere (|p| = 5), tolerance 0.01
  local i
  for i in 0 1 2 3 4; do
    mscf_chk "$1_e$i" "" 'sqrt(a[1]^2 + a[2]^2) > 3.99 && sqrt(a[1]^2 + a[2]^2) < 4.01 && a[3] > 2.99 && a[3] < 3.01 && sqrt(a[1]^2 + a[2]^2 + a[3]^2) > 4.99 && sqrt(a[1]^2 + a[2]^2 + a[3]^2) < 5.01' \
      "MSCF_$1: edge probe $i of 5 lies on the latitude circle of radius 4 at z = 3 and on the sphere of radius 5"
  done
  mscf_chk "$1_e0" "$1_e4" '((a[1]^2 + (a[2]-4)^2 + (a[3]-3)^2 < 1e-4) && ((b[1]-4)^2 + b[2]^2 + (b[3]-3)^2 < 1e-4)) || (((a[1]-4)^2 + a[2]^2 + (a[3]-3)^2 < 1e-4) && (b[1]^2 + (b[2]-4)^2 + (b[3]-3)^2 < 1e-4))' \
    "MSCF_$1: the edge ends are the arc's ends (4,0,3) and (0,4,3), in either order"
  mscf_chk "$1_far" "" '(a[1]-3.5)^2 + (a[2]+2)^2 + (a[3]-6)^2 < 1e-4' "MSCF_$1: the far edge never moved (u = 1, v = 0.5 is still (3.5,-2,6))"
}
mscf_edge follow
mscf_has follow "MatchSrf: exact edge match (G1) to the target curve following the host surface, max position error 0, max tangent error 0" "MSCF_follow: MatchSrf reports the kernel's exact curve match following the host, residuals 0"
mscf_chk follow_lift "" 'sqrt(a[1]^2 + a[2]^2 + a[3]^2) > 4.99 && sqrt(a[1]^2 + a[2]^2 + a[3]^2) < 5.01' "MSCF_follow: at u = 0.02 the point is still on the sphere (|p| = 5 to 0.01): the wall leaves in the sphere's tangent plane"
mscf_chk follow_e2 follow_lift 'sqrt(a[1]^2 + a[2]^2) - sqrt(b[1]^2 + b[2]^2) > 0.028 && sqrt(a[1]^2 + a[2]^2) - sqrt(b[1]^2 + b[2]^2) < 0.045 && b[3] - a[3] > 0.040 && b[3] - a[3] < 0.056' \
  "MSCF_follow: the lifted point moved radially in by 0.036 and up by 0.048 (h (-0.6, 0.8) per unit u, h = 3), the axial direction rotated into the tangent plane"
mscf_chk follow_l2 "" 'sqrt(a[1]^2 + a[2]^2 + a[3]^2) > 5.015' "MSCF_follow (G1): at u = 0.2 the point has left the sphere by more than 0.015 (|p| = 5.023: the straight G1 departure is outside the curved host, s^2/(2R))"
mscf_edge follow2
mscf_has follow2 "MatchSrf: exact edge match (G2) to the target curve following the host surface, max position error 0, max tangent error 0, max curvature error 0" "MSCF_follow2: MatchSrf reports the exact G2 curve match following the host, residuals 0"
mscf_chk follow2_l2 "" 'sqrt(a[1]^2 + a[2]^2 + a[3]^2) > 4.99 && sqrt(a[1]^2 + a[2]^2 + a[3]^2) < 5.01' "MSCF_follow2 (G2): at u = 0.2 the point is still on the sphere to 0.01 (|p| = 5.004; the G1 follow above leaves it by 0.023): the wall carries the sphere's curvature"
mscf_chk follow2_e2 follow2_lift 'b[3] - a[3] > 0.038 && b[3] - a[3] < 0.058' "MSCF_follow2: the G2 wall leaves the arc in the same tangent direction (z up by ~0.048 at u = 0.02)"
mscf_edge plain
mscf_has plain "MatchSrf: exact edge match (G1) to the target curve, max position error 0" "MSCF_plain: without Follow MatchSrf reports the plain exact curve match"
mscf_lacks plain "following the host surface" "MSCF_plain: no host is mentioned without Follow"
mscf_chk plain_lift "" 'sqrt(a[1]^2 + a[2]^2 + a[3]^2) > 5.025 && sqrt(a[1]^2 + a[2]^2 + a[3]^2) < 5.045' "MSCF_plain (control): the preserved axial direction leaves the sphere radially, |p| = 5.036 at u = 0.02"
mscf_chk plain_e2 plain_lift 'sqrt(a[1]^2 + a[2]^2) - sqrt(b[1]^2 + b[2]^2) > -0.01 && sqrt(a[1]^2 + a[2]^2) - sqrt(b[1]^2 + b[2]^2) < 0.01 && b[3] - a[3] > 0.054 && b[3] - a[3] < 0.066' \
  "MSCF_plain (control): the preserved direction is purely axial (z up by 0.06, hypot(x, y) unchanged)"
mscf_has position "Follow needs Continuity=Tangency" "MSCF_position: Follow with Continuity=Position is refused with a reason"
mscf_has position "nothing was changed" "MSCF_position: the refusal says nothing was changed"
mscf_chk position_e2 position_far '(a[1]-3.5)^2 + (a[2]+2)^2 + (a[3]-3)^2 < 1e-4 && (b[1]-3.5)^2 + (b[2]+2)^2 + (b[3]-6)^2 < 1e-4' "MSCF_position: the wall is untouched (edge midpoint (3.5,-2,3), far edge (3.5,-2,6))"
mscf_has offhost "is not on the host surface" "MSCF_offhost: Follow with a curve 0.3 off the sphere is refused (not on the host surface)"
mscf_chk offhost_e2 offhost_far '(a[1]-3.5)^2 + (a[2]+2)^2 + (a[3]-3)^2 < 1e-4 && (b[1]-3.5)^2 + (b[2]+2)^2 + (b[3]-6)^2 < 1e-4' "MSCF_offhost: the wall is untouched (edge midpoint (3.5,-2,3), far edge (3.5,-2,6))"
