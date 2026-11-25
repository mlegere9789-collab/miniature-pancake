# SDSR_: live RADIAL symmetric SubD editing in the app (sdsymrad_script.txt): SubDSymmetryCheck / SubDMoveSymmetric / SubDCreaseSymmetric with
# Mode=Radial, Count=N (kernel SubD::CheckRadialSymmetry / MoveVertexRadial / SetCreaseRadial). Fixture: the SubD box [-1,1] x [-1,1] x [0,2], 4-fold
# symmetric about the z axis. Oracles: (1) the box's own geometry (8 vertices, none on the axis; 90 and 180 degree rotations map the corner set onto
# itself, 120 degrees does not, so Count=3 pairs none of the 8); (2) Catmull-Clark commutes with a rotation, so after the radial move the limit points
# at (1.2,0.9,2.5) and at its 90-degree image (-0.9,1.2,2.5) satisfy x2 = -y1, y2 = x1, z2 = z1; (3) negative control: moving only one corner (SubDSculpt
# below the edge length) leaves 2 unpaired vertices and the radial move refuses the corner without images; (4) crease counts of the box: 4 after one radial
# crease on a vertical edge, 5 after a one-sided crease (the orbit's odd edge and its pre-image differ from their images: 2 mismatches), 8 after the repair.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SDSR="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sdsymrad_script.txt" 2>&1)" || { echo "$SDSR"; echo "FAIL: sdsymrad script exited non-zero"; exit 1; }
else
  SDSR="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sdsymrad_script.txt" 2>&1)" || { echo "$SDSR"; echo "FAIL: sdsymrad script exited non-zero"; exit 1; }
fi
sdr_ok() { echo "ok   $1"; }
sdr_bad() { echo "FAIL $1"; fail=1; }
sdrblock() { awk -v name="$1" '$0 ~ ("Unknown command: SDSBEGIN_" name "$") { on = 1; next } on && /Unknown command: SDSBEGIN_/ { exit } on { print }' <<<"$SDSR"; }
sdrline() { { grep -E -- "$2" <<<"$(sdrblock "$1")" || true; } | sed -n "${3:-1}p"; }
sdrhas() { # CASE PATTERN N EXPECTED-SUBSTRING DESC
  local l; l="$(sdrline "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$l"; then sdr_ok "$5"; else sdr_bad "$5"; echo "     expected: $4"; echo "     actual:   ${l:-<no line>}"; fi
}
sdrlimit() { sed -n -E 's/.*limit point \(([^,]+), ([^,]+), ([^)]+)\).*/\1 \2 \3/p' <<<"$1" | head -1; }

# ---- check
sdrhas check "SubDSymmetryCheck: object" 1 "about the axis through (0, 0, 0), 4-fold: symmetric, 8 vertices (0 on the axis, 8 paired, 0 unpaired), worst pair distance 0, 0 crease/sharpness mismatch(es)" "SDSR_check: the box is 4-fold symmetric about the z axis: 8 vertices, none on the axis, 8 paired, 0 unpaired"
sdrhas check "SubDSymmetryCheck: object" 2 "2-fold: symmetric, 8 vertices (0 on the axis, 8 paired, 0 unpaired)" "SDSR_check: the 180 degree subgroup (Count=2) is symmetric too"
sdrhas check "SubDSymmetryCheck: object" 3 "3-fold: NOT symmetric, 8 vertices (0 on the axis, 0 paired, 8 unpaired)" "SDSR_check: Count=3 is no symmetry of a square cage: 0 paired, 8 unpaired"

# ---- radial move
sdrhas move "SubDMoveSymmetric: vertex" 1 "vertex (1, 1, 2) moved to (1.2, 0.9, 2.5); its 3 rotated image(s) moved with it; the cage is still symmetric (0 unpaired vertices)" "SDSR_move: (1,1,2) goes to (1.2,0.9,2.5) and its 3 rotated images follow, the cage stays symmetric"
sdrhas move "SubDSymmetryCheck: object" 1 "4-fold: symmetric, 8 vertices (0 on the axis, 8 paired, 0 unpaired)" "SDSR_move: a fresh SubDSymmetryCheck after the move agrees: still symmetric, 8 paired"
L1="$(sdrlimit "$(sdrline move 'SubDVertexInfo: object' 1)")"
L2="$(sdrlimit "$(sdrline move 'SubDVertexInfo: object' 2)")"
if [ -n "$L1" ] && [ -n "$L2" ] && awk -v a="$L1" -v b="$L2" 'BEGIN { split(a, p, " "); split(b, q, " ");
    dx = q[1] + p[2]; dy = q[2] - p[1]; dz = q[3] - p[3]; if (dx < 0) dx = -dx; if (dy < 0) dy = -dy; if (dz < 0) dz = -dz;
    exit !(dx < 2e-3 && dy < 2e-3 && dz < 2e-3) }'; then
  sdr_ok "SDSR_move: Catmull-Clark commutes with the rotation: limit points ($L1) and ($L2) are 90-degree images (x2 = -y1, y2 = x1, z equal)"
else
  sdr_bad "SDSR_move: the two limit points are not 90-degree rotated images about the z axis ('${L1:-none}' and '${L2:-none}')"
fi

# ---- negative control: one-sided move
sdrhas onesided "SubDSymmetryCheck: object" 1 "NOT symmetric, 8 vertices (0 on the axis, 6 paired, 2 unpaired)" "SDSR_onesided: moving only one corner (SubDSculpt, radius below the edge length) breaks the symmetry: 2 unpaired vertices (the moved one and the one whose image it was)"
sdrhas onesided "SubDMoveSymmetric: the vertex" 1 "has no rotated image within the tolerance" "SDSR_onesided: the radial move refuses the corner that has no rotated images"
sdrhas onesided "SubDSymmetryCheck: object" 2 "NOT symmetric, 8 vertices (0 on the axis, 6 paired, 2 unpaired)" "SDSR_onesided: the refused move changed nothing (still 2 unpaired)"

# ---- creases
sdrhas crease "SubDCreaseSymmetric: 1 pick" 1 "1 pick(s) changed an edge and its rotated image edges (creased)" "SDSR_crease: one pick on the vertical edge at (1,1) creased it and its three rotated images"
sdrhas crease "SubDSymmetryCheck: object" 1 "4-fold: symmetric, 8 vertices (0 on the axis, 8 paired, 0 unpaired), worst pair distance 0, 0 crease/sharpness mismatch(es)" "SDSR_crease: the cage is symmetric with 0 crease mismatches"
sdrhas crease "Crease edges:" 1 "Crease edges: 4" "SDSR_crease: the box now has exactly 4 crease edges (the four vertical edges)"
sdrhas crease "SubDSymmetryCheck: object" 2 "NOT symmetric, 8 vertices (0 on the axis, 8 paired, 0 unpaired), worst pair distance 0, 2 crease/sharpness mismatch(es)" "SDSR_crease: a one-sided crease (Crease on the top edge at (0,1,2)) is reported as 2 crease/sharpness mismatches (the edge and its pre-image)"
sdrhas crease "SubDSymmetryCheck: object" 3 "4-fold: symmetric, 8 vertices (0 on the axis, 8 paired, 0 unpaired), worst pair distance 0, 0 crease/sharpness mismatch(es)" "SDSR_crease: SubDCreaseSymmetric Mode=Radial on that edge creases the other three top edges and the cage is symmetric again"
sdrhas crease "Crease edges:" 2 "Crease edges: 8" "SDSR_crease: 8 crease edges after the repair (4 vertical + 4 top)"
