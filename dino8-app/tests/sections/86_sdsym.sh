# SDSYM_: live symmetric SubD editing in the app (sdsym_script.txt): SubDSymmetryCheck, SubDMoveSymmetric, SubDCreaseSymmetric (kernel
# SubD::CheckSymmetry / MoveVertexSymmetric / SetCreaseSymmetric). Fixture: the SubD box [0,4] x [0,2] x [0,2], mirror symmetric across the plane x = 2.
# Oracles: (1) the box's own geometry (8 vertices at x = 0 and x = 4, none on the plane, all paired); (2) Catmull-Clark commutes with a reflection, so
# after the symmetric move limit(mirror v) = reflect(limit v): the two SubDVertexInfo limit points must satisfy x1 + x2 = 4 (2 x the plane offset),
# y1 = y2, z1 = z2; (3) negative control: moving only one vertex (SubDSculpt below the edge length) breaks the symmetry (2 unpaired vertices) and the
# symmetric move refuses the vertex without a partner; (4) crease counts of the box: 2 after one symmetric crease, 4 after the repair.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SDSY="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sdsym_script.txt" 2>&1)" || { echo "$SDSY"; echo "FAIL: sdsym script exited non-zero"; exit 1; }
else
  SDSY="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sdsym_script.txt" 2>&1)" || { echo "$SDSY"; echo "FAIL: sdsym script exited non-zero"; exit 1; }
fi
sds_ok() { echo "ok   $1"; }
sds_bad() { echo "FAIL $1"; fail=1; }
sdsblock() { awk -v name="$1" '$0 ~ ("Unknown command: SDSBEGIN_" name "$") { on = 1; next } on && /Unknown command: SDSBEGIN_/ { exit } on { print }' <<<"$SDSY"; }
# sdsline CASE PATTERN N: the N-th line of the case matching PATTERN
sdsline() { { grep -E -- "$2" <<<"$(sdsblock "$1")" || true; } | sed -n "${3:-1}p"; }
sdshas() { # CASE PATTERN N EXPECTED-SUBSTRING DESC
  local l; l="$(sdsline "$1" "$2" "$3")"
  if grep -qF -- "$4" <<<"$l"; then sds_ok "$5"; else sds_bad "$5"; echo "     expected: $4"; echo "     actual:   ${l:-<no line>}"; fi
}
# sdslimit LINE: "x y z" of the line's "limit point (x, y, z)"
sdslimit() { sed -n -E 's/.*limit point \(([^,]+), ([^,]+), ([^)]+)\).*/\1 \2 \3/p' <<<"$1" | head -1; }

# ---- the box is symmetric
sdshas box "SubDSymmetryCheck: object" 1 ": symmetric, 8 vertices (0 on the plane, 8 paired, 0 unpaired), worst pair distance 0, 0 crease/sharpness mismatch(es)" "SDSYM_box: the box [0,4]x[0,2]x[0,2] is symmetric across x = 2: 8 vertices, none on the plane, 8 paired, 0 unpaired"

# ---- symmetric move
sdshas move "SubDMoveSymmetric: vertex" 1 "vertex (0, 0, 0) moved to (-0.3, 0.2, 0.1); its mirror partner moved to (4.3, 0.2, 0.1); the cage is still symmetric (0 unpaired vertices)" "SDSYM_move: (0,0,0) goes to (-0.3,0.2,0.1) and its partner to the reflection (4.3,0.2,0.1) (2 x 2 - x, y, z), the cage stays symmetric"
sdshas move "SubDSymmetryCheck: object" 1 ": symmetric, 8 vertices (0 on the plane, 8 paired, 0 unpaired)" "SDSYM_move: a fresh SubDSymmetryCheck after the move agrees: still symmetric, 8 paired"
L1="$(sdslimit "$(sdsline move 'SubDVertexInfo: object' 1)")"
L2="$(sdslimit "$(sdsline move 'SubDVertexInfo: object' 2)")"
if [ -n "$L1" ] && [ -n "$L2" ] && awk -v a="$L1" -v b="$L2" 'BEGIN { split(a, p, " "); split(b, q, " ");
    dx = (p[1] + q[1]) - 4; dy = p[2] - q[2]; dz = p[3] - q[3]; if (dx < 0) dx = -dx; if (dy < 0) dy = -dy; if (dz < 0) dz = -dz;
    exit !(dx < 2e-3 && dy < 2e-3 && dz < 2e-3) }'; then
  sds_ok "SDSYM_move: Catmull-Clark commutes with the reflection: limit points ($L1) and ($L2) are mirror images (x1 + x2 = 4, y and z equal)"
else
  sds_bad "SDSYM_move: the two limit points are not mirror images across x = 2 ('${L1:-none}' and '${L2:-none}')"
fi

# ---- negative control: one-sided move
sdshas onesided "SubDSymmetryCheck: object" 1 "NOT symmetric, 8 vertices (0 on the plane, 6 paired, 2 unpaired)" "SDSYM_onesided: moving only one vertex (SubDSculpt, radius below the edge length) breaks the symmetry: 2 unpaired vertices (the moved one and its old partner)"
sdshas onesided "SubDMoveSymmetric: the vertex" 1 "has no mirror partner within the tolerance" "SDSYM_onesided: the symmetric move refuses the vertex that has no partner"
sdshas onesided "SubDSymmetryCheck: object" 2 "NOT symmetric, 8 vertices (0 on the plane, 6 paired, 2 unpaired)" "SDSYM_onesided: the refused move changed nothing (still 2 unpaired)"

# ---- creases
sdshas crease "SubDCreaseSymmetric: 1 pick" 1 "1 pick(s) changed an edge and its mirror edge (creased)" "SDSYM_crease: one pick creased the edge at x = 0 and its mirror edge at x = 4"
sdshas crease "SubDSymmetryCheck: object" 1 ": symmetric, 8 vertices (0 on the plane, 8 paired, 0 unpaired), worst pair distance 0, 0 crease/sharpness mismatch(es)" "SDSYM_crease: the cage is symmetric with 0 crease mismatches"
sdshas crease "Crease edges:" 1 "Crease edges: 2" "SDSYM_crease: the box now has exactly 2 crease edges (the pair)"
sdshas crease "SubDSymmetryCheck: object" 2 "NOT symmetric, 8 vertices (0 on the plane, 8 paired, 0 unpaired), worst pair distance 0, 1 crease/sharpness mismatch(es)" "SDSYM_crease: a one-sided crease (Crease on the edge at (0,0,1)) is reported as 1 crease/sharpness mismatch"
sdshas crease "SubDSymmetryCheck: object" 3 ": symmetric, 8 vertices (0 on the plane, 8 paired, 0 unpaired), worst pair distance 0, 0 crease/sharpness mismatch(es)" "SDSYM_crease: SubDCreaseSymmetric on that edge creases its mirror too and the cage is symmetric again"
sdshas crease "Crease edges:" 2 "Crease edges: 4" "SDSYM_crease: 4 crease edges after the repair (2 pairs)"
