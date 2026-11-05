# SEC2_: clipping planes and drafting section views cut a B-rep on the EXACT route (sec2_script.txt). ClippingSections,
# ClippingDrawings, ExtractClippingSlices, SectionView/UpdateSectionViews intersect the plane with the body's own faces
# (kernel::SectionBrepByPlaneJoined) instead of slicing the render mesh, and say which route made each curve; a mesh object
# and a plane that misses the body are still reported as what they are. Section/Contour history rebuilds (RebuildSlicePiece)
# go through the same exact route after the source is edited. Every figure is MEASURED from the app (Length) and compared
# with a closed form: sphere R=10 cut at z=6 -> circle 2 pi 8 = 50.2655; box 20x20x10 cut at z=5 -> perimeter 80; the same box
# drilled by a through hole r=0.5 -> 80 + pi = 83.1416; a Section circle after moving the sphere to x=3 -> 2 pi sqrt(91) = 59.9378;
# Contour of the sphere moved up 2 (planes at d = -7, -2, 3) -> 2 pi (sqrt(51) + sqrt(96) + sqrt(91)) = 166.3711. Windows are narrow so a mesh chord
# polygon (about 2e-4 short) could not pass. Output checks use here-strings.
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SEC2_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/sec2_script.txt" 2>&1)" || { echo "$SEC2_OUT" | tail -40; echo "FAIL SEC2_run: script exited non-zero"; fail=1; SEC2_OUT=""; }
else
  SEC2_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/sec2_script.txt" 2>&1)" || { echo "$SEC2_OUT" | tail -40; echo "FAIL SEC2_run: script exited non-zero"; fail=1; SEC2_OUT=""; }
fi
sec2_ok() { echo "ok   $1"; }
sec2_bad() { echo "FAIL $1"; fail=1; }
# the block between the marker SEC2BEGIN_$1 and the next marker
sec2_blk() { awk -v name="$1" '$0 ~ ("Unknown command: SEC2BEGIN_" name "$") { on = 1; next } on && /Unknown command: SEC2BEGIN_/ { exit } on { print }' <<<"$SEC2_OUT"; }
sec2_has() { if grep -qF -- "$2" <<<"$1"; then sec2_ok "$3"; else sec2_bad "$3"; near "$1" "$2"; fi; }
sec2_hasnot() { if grep -qF -- "$2" <<<"$1"; then sec2_bad "$3"; near "$1" "$2"; else sec2_ok "$3"; fi; }
# the Nth "Total length = " number in block text $1 (N = $2) within [$3, $4]
sec2_len() {
  local v
  v="$({ grep -o "Total length = [0-9.e+-]*" <<<"$1" || true; } | sed -n "${2}p" | awk '{print $4}')"
  if awk -v v="${v:-}" -v lo="$3" -v hi="$4" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; then sec2_ok "$5 (measured length $v in $3..$4)"; else sec2_bad "$5 (length '$v' not in $3..$4)"; echo "     block: $(grep -E 'Total length|Contour|Section|UpdateHistory|rebuil|kept|curve' <<<"$1" | cut -c1-110 | tr '\n' '|' | cut -c1-900)"; fi
}
if grep -q "^FAIL" <<<"$SEC2_OUT"; then sec2_bad "SEC2_script: a script-level FAIL line"; else sec2_ok "SEC2_script: no script-level FAIL line"; fi
SEC2_CAP="$(sec2_blk cap)"; SEC2_BOX="$(sec2_blk box)"; SEC2_DRILL="$(sec2_blk drill)"; SEC2_VIEWS="$(sec2_blk drill_views)"; SEC2_SV="$(sec2_blk drill_sv)"
SEC2_MISS="$(sec2_blk miss)"; SEC2_MESH="$(sec2_blk mesh)"
sec2_has "$SEC2_CAP" "ClippingSections: 1 curve(s) from 1 plane(s) (1 from the exact B-rep section, 0 from mesh slices)" "SEC2_1: ClippingSections of the sphere is ONE curve from the exact B-rep route"
sec2_len "$SEC2_CAP" 1 50.261 50.270 "SEC2_1: the cap circle length is 2 pi sqrt(R^2-d^2) = 50.2655 (R=10, d=6)"
sec2_has "$SEC2_CAP" "degree 3, " "SEC2_1: the exact section is a smooth degree-3 curve, not a degree-1 polygon"
sec2_has "$SEC2_CAP" "non-rational, closed" "SEC2_1: ...and it is closed"
sec2_has "$SEC2_BOX" "ClippingSections: 1 curve(s) from 1 plane(s) (1 from the exact B-rep section, 0 from mesh slices)" "SEC2_2: the four box walls were joined into ONE exact contour"
sec2_len "$SEC2_BOX" 1 79.99 80.01 "SEC2_2: the box section length is the perimeter 2 (20 + 20) = 80"
sec2_has "$SEC2_DRILL" "ClippingSections: 2 curve(s) from 1 plane(s) (2 from the exact B-rep section, 0 from mesh slices)" "SEC2_3: the drilled box gives 2 curves (outline + hole), both exact"
sec2_len "$SEC2_DRILL" 1 83.137 83.146 "SEC2_3: outline plus hole is 80 + pi = 83.1416 (the r=0.5 hole circle measures pi)"
sec2_has "$SEC2_VIEWS" "ClippingDrawings: 2 drawing curve(s) on layer 'Clipping Drawings' from 1 plane(s) (2 from the exact B-rep section, 0 from mesh slices)" "SEC2_4: ClippingDrawings takes the exact route for the drilled box"
sec2_has "$SEC2_VIEWS" "ExtractClippingSlices: 2 planar slice surface(s) from 1 plane(s) (2 from the exact B-rep section, 0 from mesh slices)" "SEC2_4: ExtractClippingSlices builds its faces from the exact closed contours"
sec2_has "$SEC2_SV" "(2 outline curve(s) from the exact B-rep section, the rest mesh slices)" "SEC2_5: SectionView's cut outline (rectangle + hole circle) comes from the exact section"
sec2_has "$SEC2_SV" "UpdateSectionViews: 1 section view(s) regenerated (2 outline curve(s) from the exact B-rep section" "SEC2_5: UpdateSectionViews regenerates it on the exact route too"
sec2_has "$SEC2_MISS" "ClippingSections: 0 curve(s) from 1 plane(s) (0 from the exact B-rep section, 0 from mesh slices)" "SEC2_6 (negative control): a plane that misses the sphere makes no curve and claims no route"
sec2_has "$SEC2_MISS" "ClippingDrawings: 0 drawing curve(s)" "SEC2_6: ...ClippingDrawings makes none either"
sec2_hasnot "$SEC2_MISS" "outline curve(s) from the exact B-rep section" "SEC2_6: ...and SectionView reports no exact outline"
sec2_has "$SEC2_MESH" "ClippingSections: 1 curve(s) from 1 plane(s) (0 from the exact B-rep section, 1 from mesh slices)" "SEC2_7 (negative control): a MESH box stays mesh-labelled"
sec2_len "$SEC2_MESH" 1 79.9 80.1 "SEC2_7: ...and its mesh slice still measures the perimeter 80"
sec2_has "$SEC2_MESH" "(mesh slices)" "SEC2_7: SectionView of the mesh box is reported as a mesh slice"
sec2_hasnot "$SEC2_MESH" "from the exact B-rep section, the rest" "SEC2_7: ...never as an exact outline"
SEC2_HS="$(sec2_blk hist_section)"; SEC2_HM="$(sec2_blk hist_section_moved)"; SEC2_HX="$(sec2_blk hist_section_missed)"
SEC2_HC="$(sec2_blk hist_contour)"; SEC2_HCM="$(sec2_blk hist_contour_moved)"
sec2_has "$SEC2_HS" "Section: 1 curve(s) (1 exact B-rep section curve(s)" "SEC2_8: History-recorded Section of the sphere is exact"
sec2_len "$SEC2_HS" 1 50.261 50.270 "SEC2_8: ...x=6 cuts the sphere in a circle of radius 8 (50.2655)"
sec2_len "$SEC2_HM" 1 59.933 59.942 "SEC2_9: after moving the source to x=3 the rebuilt piece is the circle 2 pi sqrt(91) = 59.9378 (exact route)"
sec2_has "$SEC2_HM" "degree 3, " "SEC2_9: ...a smooth degree-3 curve (a mesh rebuild would be a degree-1 polygon)"
sec2_has "$SEC2_HM" "non-rational, closed" "SEC2_9: ...and it is closed"
sec2_len "$SEC2_HX" 1 59.933 59.942 "SEC2_10 (negative control): moved so the plane misses the sphere, the piece is kept as it was"
sec2_has "$SEC2_HX" "kept as it was" "SEC2_10: ...and UpdateHistory says why (the edited source no longer meets the plane)"
sec2_has "$SEC2_HC" "Contour: 3 curve(s) from 5 plane(s) (3 exact B-rep section curve(s)" "SEC2_11: History-recorded Contour of the sphere gives 3 exact circles"
sec2_len "$SEC2_HC" 1 171.6 171.8 "SEC2_11: their total is 2 pi (sqrt(75) + 10 + sqrt(75)) = 171.7"
# SEC2_12 is the original oracle: sphere R=10 moved up 2 puts the z = -5, 0, 5 planes at d = -7, -2, 3, radii sqrt(51), sqrt(96), sqrt(91).
sec2_len "$SEC2_HCM" 1 166.367 166.375 "SEC2_12: after moving the sphere up 2 the three rebuilt circles total 2 pi (sqrt(51) + sqrt(96) + sqrt(91)) = 166.3711 (Length digits=9: the default 4 digits print 166.4)"
sec2_len "$SEC2_HCM" 2 44.8699 44.8719 "SEC2_12: ...the z=-5 circle (d=-7) is 2 pi sqrt(51) = 44.8709"
sec2_len "$SEC2_HCM" 3 61.5614 61.5634 "SEC2_12: ...the z=0 circle (d=-2) is 2 pi sqrt(96) = 61.5624"
sec2_len "$SEC2_HCM" 4 59.9368 59.9388 "SEC2_12: ...the z=5 circle (d=3) is 2 pi sqrt(91) = 59.9378"

# SEC2_13..: SectionView label=A (sec2_script.txt, blocks label / nolabel / label_update). Style text height 2, arrow size 3; the box
# 0..20 x 0..20 x 0..10 is cut by the vertical plane y = 10 along the picked line (-5,10,0)-(25,10,0). Cut plane: x-axis +X, y-axis +Z,
# normal (0,-1,0) = the viewing direction. Closed forms: section line length 30, ends (-5,10,0) and (25,10,0); each viewing arrow is a
# shaft of 3 along the normal plus two head legs of length sqrt(1.2^2 + 0.45^2) = 1.2816 (0.4 and 0.15 of the arrow size), so the arrows
# total 2 (3 + 2 x 1.2816) = 11.1264 and span x -5.45..25.45, y 7..10; each letter sits below its tip: baseline y = 7 - 1 - 2 = 4, cap
# height under the text height (top y <= 6); the title is centred under the outline (box centre x = 10) with its baseline two text
# heights under the outline's lowest point (z = -4, cap height under 2); the ANSI31 hatch at scale 5 has perpendicular spacing
# 0.125 x 5 = 0.625, so its total length is area / spacing = 200 / 0.625 = 320 (the discrete sum is within 2 percent).
# sec2_bbv BLOCK N: the six numbers "x0 y0 z0 x1 y1 z1" of the Nth "Bounding box" line in BLOCK.
sec2_bbv() { { grep -o 'Bounding box min [^ ]* max [^ ]*' <<<"$1" || true; } | sed -n "${2}p" | tr ',' ' ' | awk '{print $4, $5, $6, $8, $9, $10}'; }
# sec2_bbchk BLOCK N "lo:hi (six, - for no check)" MESSAGE
sec2_bbchk() {
  local v
  v="$(sec2_bbv "$1" "$2")"
  if [ -n "$v" ] && awk -v v="$v" -v r="$3" 'BEGIN { n = split(v, a, " "); m = split(r, b, " "); if (n != 6 || m != 6) exit 1; for (i = 1; i <= 6; i++) { if (b[i] == "-") continue; split(b[i], c, ":"); if (a[i] + 0 < c[1] + 0 || a[i] + 0 > c[2] + 0) exit 1 } exit 0 }'; then sec2_ok "$4 (bounding box $v)"; else sec2_bad "$4 (bounding box '$v' not in $3)"; fi
}
SEC2_LB="$(sec2_blk label)"; SEC2_NL="$(sec2_blk nolabel)"; SEC2_LU="$(sec2_blk label_update)"
sec2_len "$SEC2_LB" 1 29.999 30.001 "SEC2_13: the section line is 30 long (the picked line (-5,10,0)-(25,10,0))"
sec2_bbchk "$SEC2_LB" 1 "-5.001:-4.999 9.999:10.001 -0.001:0.001 24.999:25.001 9.999:10.001 -0.001:0.001" "SEC2_13: ...and its ends are exactly the picked points"
sec2_len "$SEC2_LB" 2 11.12 11.14 "SEC2_14: the two viewing arrows total 2 (3 + 2 sqrt(1.2^2 + 0.45^2)) = 11.1264"
sec2_bbchk "$SEC2_LB" 2 "-5.451:-5.449 6.999:7.001 -0.001:0.001 25.449:25.451 9.999:10.001 -0.001:0.001" "SEC2_14: ...spanning x -5.45..25.45 and y 7..10 (arrows point along the cut plane's normal, -Y)"
sec2_bbchk "$SEC2_LB" 3 "-7:-5 3.9:4.1 -0.001:0.001 25:27 5:6.05 -0.001:0.001" "SEC2_15: the letter A sits below each arrow tip: left of -5 and right of 25, baseline y = 4, cap height between 1 and 2"
sec2_bbchk "$SEC2_LB" 4 "- 9.999:10.001 -4.2:-3.9 - 9.999:10.001 -3.2:-1.9" "SEC2_16: the title SECTION A-A lies in the cut plane (y = 10) with its baseline at z = -4 and cap height under 2"
if awk -v v="$(sec2_bbv "$SEC2_LB" 4)" 'BEGIN { split(v, a, " "); c = (a[1] + a[4]) / 2; w = a[4] - a[1]; exit !(c >= 9 && c <= 11 && w >= 8 && w <= 24) }'; then sec2_ok "SEC2_16: ...centred under the outline (box centre x = 10) and 8..24 wide"; else sec2_bad "SEC2_16: title not centred under the outline (bbox '$(sec2_bbv "$SEC2_LB" 4)')"; fi
sec2_len "$SEC2_LB" 3 310 330 "SEC2_17: the ANSI31 hatch (scale 5, spacing 0.625) of the 20 x 10 section totals area / spacing = 200 / 0.625 = 320"
sec2_bbchk "$SEC2_LB" 5 "-0.001:2 9.999:10.001 -0.001:2 18:20.001 9.999:10.001 8:10.001" "SEC2_17: ...and stays inside the section rectangle x 0..20, z 0..10"
sec2_has "$SEC2_NL" "0 object(s) with SectionTitle=1 selected" "SEC2_18 (negative control): without label= there is no title"
if [ "$(grep -c '^history: 0 object(s) selected$' <<<"$SEC2_NL")" = 2 ]; then sec2_ok "SEC2_18: ...no section line and no arrows either"; else sec2_bad "SEC2_18: without label= a section line / arrow object exists"; near "$SEC2_NL" "object(s)"; fi
sec2_bbchk "$SEC2_LU" 1 "-5.001:-4.999 9.999:10.001 -0.001:0.001 24.999:25.001 9.999:10.001 -0.001:0.001" "SEC2_19: UpdateSectionViews rebuilds the label with the same section line"
if awk -v v="$(sec2_bbv "$SEC2_LU" 2)" 'BEGIN { split(v, a, " "); c = (a[1] + a[4]) / 2; exit !(c >= 13 && c <= 15) }'; then sec2_ok "SEC2_19: ...and the title follows the box moved +4 in X (centre x = 14)"; else sec2_bad "SEC2_19: title not re-centred after the move (bbox '$(sec2_bbv "$SEC2_LU" 2)')"; fi
sec2_bbchk "$SEC2_LU" 3 "3.999:6 9.999:10.001 -0.001:2 22:24.001 9.999:10.001 8:10.001" "SEC2_19: ...and the hatch spans the moved section x 4..24"
