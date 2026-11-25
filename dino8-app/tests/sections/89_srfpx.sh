# SRFPX: surface edits on a Brep face (SoftEditSrf, SetSurfaceTangent, MatchSrf) are visible to everything that reads the face's surface (srfpx_script.txt).
# Before kernel::ReplaceBrepSurface these commands stored the new surface into the Brep's surface array only, so ON_BrepFace::SurfaceOf() - which EvaluateUVPt,
# the kernel's Brep::Area / Volume and a second edit of the same face read - still answered with the OLD surface (a prebuilt app without the fix prints the
# untouched face middle 5,0,5 in every case below). The expectations are closed forms of the edit, not the app's own residual printouts. Face 0 of the box is the
# plane y = 0, a degree-1 patch with 4 control points, so its middle (normalized 0.5,0.5) is the average of the four control points:
#   * SoftEditSrf Radius=8: the click (0.5,0,0.5) anchors at distance d = sqrt(0.5) from the corner control point (0,0,0), the only one inside the radius; it moves
#     with the cosine weight w = (1 + cos(pi d / 8)) / 2 by w * (0,-3,0), so the middle moves by (0, -0.75 w, 0): y = -0.7356.
#   * SetSurfaceTangent against the plane z = x / 2 - 10 (unit normal n = (-1,0,2)/sqrt 5): the two control points of the row z = 10 are rotated to cv0 + |s| *
#     unit(s - n (n.s)) with s = cv1 - cv0 = (0,0,10): they move to cv0 + (8.944, 0, 4.472); the middle is (9.472, 0, 2.236).
#   * MatchSrf Continuity=Position onto the edge (x 0..10, y = -5, z = 0) of the plane z = 0: the row z = 0 lands on that edge, the row z = 10 stays: (5, -2.5, 5).
#   * the two edits in a row (SoftEditSrf then SetSurfaceTangent) compose: the second reads the first one's surface, so row 0 is (0, -3w', 0), (10, 0, 0) with
#     w' = w (3 w' = 2.943), s = (0, 2.943, 10) for the first column; the middle is the average of the four resulting control points (computed below).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SRFPXO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/srfpx_script.txt" 2>&1)" || { echo "$SRFPXO"; echo "FAIL: srfpx script exited non-zero"; exit 1; }
else
  SRFPXO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/srfpx_script.txt" 2>&1)" || { echo "$SRFPXO"; echo "FAIL: srfpx script exited non-zero"; exit 1; }
fi
srfpx_blk() { awk -v name="$1" '$0 ~ ("Unknown command: SRFPXBEGIN_" name "$") { on = 1; next } on && /Unknown command: SRFPXBEGIN_/ { exit } on { print }' <<<"$SRFPXO"; }
srfpx_pt() { { grep -oE -- '-> [-0-9.eE+]+,[-0-9.eE+]+,[-0-9.eE+]+' <<<"$(srfpx_blk "$1")" || true; } | head -1 | sed 's/^-> //'; }
# srfpx_chk NAME DESCRIPTION EXPECTED-x EXPECTED-y EXPECTED-z (absolute tolerance 0.002: the app prints four significant digits)
srfpx_chk() {
  local p; p="$(srfpx_pt "$1")"
  if [ -n "$p" ] && awk -v p="$p" -v ex="$3" -v ey="$4" -v ez="$5" 'BEGIN { split(p, a, ","); d = (a[1]-ex)^2 + (a[2]-ey)^2 + (a[3]-ez)^2; exit !(sqrt(d) <= 0.003) }'; then
    echo "ok   SRFPX_$1: $2 (middle of face 0 $p, expected $3,$4,$5)"
  else
    echo "FAIL SRFPX_$1: $2 (middle of face 0 '${p:-none}', expected $3,$4,$5)"; fail=1
    { srfpx_blk "$1" | grep -E "SoftEditSrf|SetSurfaceTangent|MatchSrf|Warning|rror" || true; } | head -3 | cut -c1-240 | sed 's/^/     actual: /'
  fi
}
srfpx_w="$(awk 'BEGIN { pi = atan2(0, -1); d = sqrt(0.5); print 0.5 * (1 + cos(pi * d / 8)) }')"
srfpx_y1="$(awk -v w="$srfpx_w" 'BEGIN { printf "%.6f", -0.75 * w }')"
# soft then tangent: CVs after the soft edit: (0,-3w,0), (10,0,0), (0,0,10), (10,0,10); row z = 0 stays, row z = 10 is rotated per column
srfpx_comp="$(awk -v w="$srfpx_w" 'BEGIN {
  nx = -1 / sqrt(5); nz = 2 / sqrt(5);               # unit normal of the plane z = x/2 - 10
  sx = 0; sy = 3 * w; sz = 10;                       # column at x = 0: s = cv1 - cv0 = (0, 3w, 10)
  nd = nx * sx + nz * sz; px = sx - nd * nx; py = sy; pz = sz - nd * nz; pl = sqrt(px*px + py*py + pz*pz); ml = sqrt(sx*sx + sy*sy + sz*sz);
  ax = px / pl * ml; ay = py / pl * ml - 3 * w; az = pz / pl * ml;   # cv1 of that column = cv0 + |s| unit(perp), cv0 = (0,-3w,0)
  sx = 0; sy = 0; sz = 10;                           # column at x = 10: s = (0,0,10)
  nd = nx * sx + nz * sz; px = sx - nd * nx; pz = sz - nd * nz; pl = sqrt(px*px + pz*pz);
  bx = 10 + px / pl * 10; bz = pz / pl * 10;
  printf "%.6f %.6f %.6f", (0 + 10 + ax + bx) / 4, (-3 * w + 0 + ay + 0) / 4, (0 + 0 + az + bz) / 4 }')"
srfpx_chk base "the untouched face middle is (5,0,5) (fixture sanity)" 5 0 5
srfpx_chk soft "SoftEditSrf's new surface is what face 0 reports (a stale proxy reports 5,0,5)" 5 "$srfpx_y1" 5
srfpx_chk tang "SetSurfaceTangent's rotated tangent row is what face 0 reports" 9.4721360 0 2.2360680
srfpx_chk match "MatchSrf's matched edge is what face 0 reports" 5 -2.5 5
set -- $srfpx_comp
srfpx_chk soft_tang "SetSurfaceTangent after SoftEditSrf starts from the soft-edited surface (both edits visible)" "$1" "$2" "$3"
