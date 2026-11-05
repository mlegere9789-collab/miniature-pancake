# MGST: MergeSrf Smooth=Yes - the exact merge followed by G1 smoothing of the seam (kernel NurbsSurface::JoinTangent; mgst_script.txt).
# Fixture: A is the plane z = 0 over x 0..4, y 0..3; B continues from the shared edge y = 3 tilted up by 30 degrees (to (y, z) = (5.598, 1.5)), so
# the plain exact merge has a 30 degree crease. Expectations are closed forms of that geometry, never the app's own printout of what it built:
#   * the merged surface at u = 0.5 (x = 2): t = 0 is A's far edge (2,0,0), t = 0.5 is the seam (2,3,0), t = 1 is B's far edge (2,5.598,1.5) in both
#     modes - smoothing only moves the two control rows next to the seam, never the seam curve or the outer edges;
#   * a crease shows in the second difference of three equally spaced probes: P(0.45) + P(0.55) - 2 P(0.5). For the plain merge the two halves are
#     straight, so it is exactly 2 * 0.05 * |T_B - T_A| = 0.1 * 3 * |(cos 30 - 1, sin 30)| = 0.1553 (y -0.0402, z 0.15); for a G1 seam it is second
#     order in the probe spacing (measured 0.0155 with the kernel call the command makes), so < 0.05 separates the two by a factor of 3 or more;
#   * the report: the crease before is exactly 30 degrees, the control rows move the surface by at most the quadratic Bezier bound
#     0.75 sin(15 degrees) = 0.1941, and nothing is smoothed without Smooth=Yes;
#   * two planes with no shared edge are refused with Smooth=Yes and nothing is merged.
# The app prints 4 significant digits (coordinates up to 5.6), so positions are checked to 0.01 and the second difference to +-0.006 around 0.1553.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MGSTO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/mgst_script.txt" 2>&1)" || { echo "$MGSTO"; echo "FAIL: mgst script exited non-zero"; exit 1; }
else
  MGSTO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/mgst_script.txt" 2>&1)" || { echo "$MGSTO"; echo "FAIL: mgst script exited non-zero"; exit 1; }
fi
mgst_blk() { awk -v name="$1" '$0 ~ ("Unknown command: MGSTBEGIN_" name "$") { on = 1; next } on && /Unknown command: MGSTBEGIN_/ { exit } on { print }' <<<"$MGSTO"; }
mgst_ok() { echo "ok   $1"; }
mgst_bad() { echo "FAIL $1"; fail=1; }
mgst_has() { # BLOCK PATTERN DESC (fixed string)
  if grep -qF -- "$2" <<<"$(mgst_blk "$1")"; then mgst_ok "$3"; else mgst_bad "$3"; echo "     expected: $2"; { mgst_blk "$1" | grep -E "MergeSrf|Warning|rror" || true; } | head -4 | cut -c1-260 | sed 's/^/     actual:   /'; fi
}
mgst_lacks() { if grep -qF -- "$2" <<<"$(mgst_blk "$1")"; then mgst_bad "$3"; else mgst_ok "$3"; fi; }
mgst_pt() { { grep -oE -- '-> [-0-9.eE+]+,[-0-9.eE+]+,[-0-9.eE+]+' <<<"$(mgst_blk "$1")" || true; } | head -1 | sed 's/^-> //'; }
mgst_near() { # PROBE X Y Z DESC : within 0.01 of (X,Y,Z)
  local pt; pt="$(mgst_pt "$1")"
  if [ -n "$pt" ] && awk -v pt="$pt" -v x="$2" -v y="$3" -v z="$4" 'BEGIN { if (split(pt, a, ",") != 3) exit 1; exit !(sqrt((a[1]-x)^2 + (a[2]-y)^2 + (a[3]-z)^2) <= 0.01) }'; then mgst_ok "$5 (probe $pt)"; else mgst_bad "$5 (probe '${pt:-none}', expected $2,$3,$4)"; fi
}
mgst_second() { # CASE LO HI DESC : |P(.45) + P(.55) - 2 P(.5)| in [LO, HI]
  local p0 p1 p2; p0="$(mgst_pt "$1_t45")"; p1="$(mgst_pt "$1_t50")"; p2="$(mgst_pt "$1_t55")"
  if [ -n "$p0" ] && [ -n "$p1" ] && [ -n "$p2" ] && awk -v p0="$p0" -v p1="$p1" -v p2="$p2" -v lo="$2" -v hi="$3" 'BEGIN {
        split(p0, a, ","); split(p1, b, ","); split(p2, c, ",");
        d = sqrt((a[1]+c[1]-2*b[1])^2 + (a[2]+c[2]-2*b[2])^2 + (a[3]+c[3]-2*b[3])^2); exit !(d >= lo && d <= hi) }'; then
    mgst_ok "$4 (probes $p0 / $p1 / $p2)"; else mgst_bad "$4 (probes '${p0:-none}' / '${p1:-none}' / '${p2:-none}')"; fi
}
for c in smooth plain; do
  mgst_has $c "MergeSrf: merged 2 surfaces into one (exact merge" "MGST_$c: the two patches merged exactly (spliced control nets, no refit)"
  mgst_near ${c}_t0 2 0 0 "MGST_$c: t = 0 is A's far-edge midpoint (2,0,0)"
  mgst_near ${c}_t50 2 3 0 "MGST_$c: t = 0.5 is the seam midpoint (2,3,0)"
  mgst_near ${c}_t100 2 5.598 1.5 "MGST_$c: t = 1 is B's far-edge midpoint (2,5.598,1.5)"
done
mgst_has smooth "seam smoothed to G1 (Smooth=Yes): crease 30 degrees before" "MGST_smooth: the report names the 30 degree crease it removed"
mgst_has smooth "moved the surface by at most 0.1941" "MGST_smooth: the deviation is the Bezier bound 0.75 sin(15 degrees) = 0.1941"
mgst_second smooth 0 0.05 "MGST_smooth: the seam is G1 - the second difference across it is second order (< 0.05; measured 0.0155)"
mgst_lacks plain "smoothed" "MGST_plain: nothing is smoothed without Smooth=Yes"
mgst_second plain 0.147 0.160 "MGST_plain (control): the creased merge has the second difference 2 * 0.05 * |T_B - T_A| = 0.1553"
mgst_has apart "Smooth=Yes needs the exact kernel merge" "MGST_apart: Smooth=Yes on two planes with no shared edge is refused with a reason"
mgst_has apart "nothing was changed" "MGST_apart: the refusal says nothing was changed"
