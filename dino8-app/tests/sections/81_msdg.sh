# MSDG: MatchSrf Continuity=Curvature against a degree-1 (flat, planar) target (msdg_script.txt; kernel NurbsSurface::MatchEdge now elevates such a target).
# It used to be refused ("the target's degree is too low to carry a curvature row"). Fixture: the plane y -10..0, z = 0 matched to the bottom edge of a
# box's vertical front face (y = 0, z 0..10). The matched surface continues the face downward (z < 0) and its cross parameter u runs from -10 (u = 0) to the
# matched edge (u = 1). Expectations are orders of contact, closed forms of the geometry and not the app's own printout of its residuals:
#   * the edge is the target's edge: u = 1 at v = 0.5 is (2605, 0, 0);
#   * the target is flat, so a G2 match continues it with ZERO curvature: the surface leaves the face's plane y = 0 only at third order, y = c e^3 in the
#     distance e = |z| below the edge (a G1 match only at second order, y = c e^2). So from u = 0.99 to u = 0.98 (e doubles) |y| grows by 8 for G2 and by 4
#     for G1, and at the same e the G2 departure is far smaller than the G1 one (measured: G2 1e-5 / 8e-5, G1 0.001 / 0.004 at e = 0.1 / 0.2);
#   * both continue the face: e = |z| is 0.1 / 0.2 to 2e-3 (the tangent is vertical).
# The app prints 4 significant digits, so ratios are compared with 20% slack.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MSDGO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/msdg_script.txt" 2>&1)" || { echo "$MSDGO"; echo "FAIL: msdg script exited non-zero"; exit 1; }
else
  MSDGO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/msdg_script.txt" 2>&1)" || { echo "$MSDGO"; echo "FAIL: msdg script exited non-zero"; exit 1; }
fi
msdg_blk() { awk -v name="$1" '$0 ~ ("Unknown command: MSDGBEGIN_" name "$") { on = 1; next } on && /Unknown command: MSDGBEGIN_/ { exit } on { print }' <<<"$MSDGO"; }
msdg_pt() { { grep -oE -- '-> [-0-9.eE+]+,[-0-9.eE+]+,[-0-9.eE+]+' <<<"$(msdg_blk "$1")" || true; } | head -1 | sed 's/^-> //'; }
msdg_comp() { local p; p="$(msdg_pt "$1")"; [ -n "$p" ] && awk -v p="$p" -v k="$2" 'BEGIN { split(p, a, ","); print a[k] }'; }
msdg_ok() { echo "ok   $1"; }
msdg_bad() { echo "FAIL $1"; fail=1; }
if grep -qF "MatchSrf: exact edge match (G2) to the target surface, max position error 0, max tangent error 0, max curvature error 0" <<<"$(msdg_blk g2)"; then
  msdg_ok "MSDG: Curvature against a degree-1 planar target is matched exactly (it used to be refused)"
else msdg_bad "MSDG: Curvature against a degree-1 planar target is matched exactly"; { msdg_blk g2 | grep -E "MatchSrf|Warning|rror" || true; } | head -3 | cut -c1-240 | sed 's/^/     actual: /'; fi
p="$(msdg_pt g2_u100)"
if [ -n "$p" ] && awk -v p="$p" 'BEGIN { split(p, a, ","); exit !(sqrt((a[1]-2605)^2 + a[2]^2 + a[3]^2) <= 0.01) }'; then msdg_ok "MSDG: the matched edge is the target's edge point (2605,0,0) (probe $p)"; else msdg_bad "MSDG: the matched edge is the target's edge point (probe '${p:-none}')"; fi
g2y99="$(msdg_comp g2_u99 2)"; g2y98="$(msdg_comp g2_u98 2)"; g2z99="$(msdg_comp g2_u99 3)"; g2z98="$(msdg_comp g2_u98 3)"
g1y99="$(msdg_comp g1_u99 2)"; g1y98="$(msdg_comp g1_u98 2)"; g1z99="$(msdg_comp g1_u99 3)"; g1z98="$(msdg_comp g1_u98 3)"
msdg_chk() { # DESC AWK-EXPR (variables: g2y99 g2y98 g2z99 g2z98 g1y99 g1y98 g1z99 g1z98)
  if [ -n "$g2y99$g2y98$g1y99$g1y98" ] && awk -v g2y99="${g2y99:-0}" -v g2y98="${g2y98:-0}" -v g2z99="${g2z99:-0}" -v g2z98="${g2z98:-0}" -v g1y99="${g1y99:-0}" -v g1y98="${g1y98:-0}" -v g1z99="${g1z99:-0}" -v g1z98="${g1z98:-0}" "BEGIN { exit !($2) }" 2>/dev/null; then
    msdg_ok "$1 (G2 y: $g2y99 / $g2y98; G1 y: $g1y99 / $g1y98; z: $g2z99 / $g2z98)"
  else msdg_bad "$1 (G2 y: '${g2y99:-none}' / '${g2y98:-none}'; G1 y: '${g1y99:-none}' / '${g1y98:-none}')"; fi
}
msdg_chk "MSDG: both continue the face downward, e = |z| = 0.1 / 0.2 to 2e-3 (vertical tangent)" '(g2z99 < 0) && (g2z99 > -0.102) && (g2z99 < -0.098) && (g2z98 < -0.198) && (g2z98 > -0.202) && (g1z99 > -0.102) && (g1z99 < -0.098)'
msdg_chk "MSDG: G2 departs the face plane at third order: |y(e = 0.2)| / |y(e = 0.1)| = 8 (+-20%)" 'g2y99 != 0 && (g2y98 / g2y99 > 6.4) && (g2y98 / g2y99 < 9.6)'
msdg_chk "MSDG: G1 (control) departs at second order: |y(e = 0.2)| / |y(e = 0.1)| = 4 (+-20%)" 'g1y99 != 0 && (g1y98 / g1y99 > 3.2) && (g1y98 / g1y99 < 4.8)'
msdg_chk "MSDG: at the same distance the G2 departure is at least 10 times smaller than the G1 one (zero curvature, not just the tangent)" '(g2y99 < 0 ? -g2y99 : g2y99) * 10 < (g1y99 < 0 ? -g1y99 : g1y99)'
