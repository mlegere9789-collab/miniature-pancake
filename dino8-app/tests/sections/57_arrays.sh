# ARR_: ArrayCrv / ArraySrf copies sit at the closed-form frames of the path / surface (arrays_script.txt, blocks circle / cyl / plane). The
# maths is the kernel's (pattern.h) and the kernel tests pin it independently (array_along_curve.cpp, array_on_surface.cpp); here the
# commands are checked end to end on the documents they build, every endpoint hand-computed:
#   circle r 5, 5 items, vertical unit line at (5,0,0): copy k = rotated k x 90 degrees about z: (5 cos t, 5 sin t, 0) - (.., 1);
#   cylinder r 5 h 10 (ExtrudeCrv Solid=No), 5 x 1 items, unit line at the origin: u stations at the knots (0, 90, 180, 270, 360 degrees),
#   v middle z = 5, the line along the outward normal: (5 cos t, 5 sin t, 5) - (6 cos t, 6 sin t, 5);
#   Plane 0,0,0 10,8,0 (u runs along y, v along x: Su x Sv = -z), 3 x 3 items, unit line: copy k at (5 (k mod 3), 4 (k div 3), 0) pointing to -z.
# Output values print with 4 significant digits, so the comparison window is 2e-3.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  ARR_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/arrays_script.txt" 2>&1)" || { echo "$ARR_OUT" | tail -40; echo "FAIL ARR_run: script exited non-zero"; fail=1; ARR_OUT=""; }
else
  ARR_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/arrays_script.txt" 2>&1)" || { echo "$ARR_OUT" | tail -40; echo "FAIL ARR_run: script exited non-zero"; fail=1; ARR_OUT=""; }
fi
arr_ok() { echo "ok   $1"; }
arr_bad() { echo "FAIL $1"; fail=1; }
arr_blk() { awk -v name="$1" '$0 ~ ("Unknown command: ARRBEGIN_" name "$") { on = 1; next } on && /Unknown command: ARRBEGIN_/ { exit } on { print }' <<<"$ARR_OUT"; }
arr_has() { if grep -qF -- "$2" <<<"$1"; then arr_ok "$3"; else arr_bad "$3"; near "$1" "$2"; fi; }
# arr_chk BLOCK K "x0,y0,z0 x1,y1,z1" MESSAGE: the K-th listed curve (0-based) has control points within 2e-3 of the two expected points
arr_chk() {
  local got
  got="$({ grep -o 'CV\[[01]\] [-0-9.e+]*,[-0-9.e+]*,[-0-9.e+]*' <<<"$1" || true; } | sed -n "$((2 * $2 + 1)),$((2 * $2 + 2))p" | sed 's/CV\[[01]\] //' | tr '\n' ' ')"
  if [ -n "$got" ] && awk -v g="$got" -v w="$3" 'BEGIN { n = split(g, a, /[ ,]+/); m = split(w, b, /[ ,]+/); if (n < 6 || m < 6) exit 1; for (i = 1; i <= 6; i++) { d = a[i] - b[i]; if (d < 0) d = -d; if (d > 2e-3) exit 1 } exit 0 }'; then arr_ok "$4 (measured $got)"; else arr_bad "$4 (measured '$got', expected $3)"; fi
}
ARR_CIRCLE="$(arr_blk circle)"; ARR_CYL="$(arr_blk cyl)"; ARR_PLANE="$(arr_blk plane)"
arr_has "$ARR_CIRCLE" "ArrayCrv: 5 object(s) placed along the curve" "ARR_1: ArrayCrv of one line along a circle places 5 copies"
arr_chk "$ARR_CIRCLE" 0 "5,0,0 5,0,1" "ARR_1: copy 0 is the line itself"
arr_chk "$ARR_CIRCLE" 1 "0,5,0 0,5,1" "ARR_1: copy 1 is the line rotated 90 degrees about the circle's axis: (0,5,0)-(0,5,1)"
arr_chk "$ARR_CIRCLE" 2 "-5,0,0 -5,0,1" "ARR_1: copy 2 (180 degrees): (-5,0,0)-(-5,0,1)"
arr_chk "$ARR_CIRCLE" 3 "0,-5,0 0,-5,1" "ARR_1: copy 3 (270 degrees): (0,-5,0)-(0,-5,1)"
arr_chk "$ARR_CIRCLE" 4 "5,0,0 5,0,1" "ARR_1: copy 4 (360 degrees) is back at the start"
arr_has "$ARR_CYL" "ArraySrf: 5 object(s) placed on the surface" "ARR_2: ArraySrf 5 x 1 of one line on the cylinder places 5 copies"
arr_chk "$ARR_CYL" 0 "5,0,5 6,0,5" "ARR_2: copy 0 stands at (5,0,5) pointing along the outward normal (+x): (5,0,5)-(6,0,5)"
arr_chk "$ARR_CYL" 1 "0,5,5 0,6,5" "ARR_2: copy 1 (90 degrees): (0,5,5)-(0,6,5)"
arr_chk "$ARR_CYL" 2 "-5,0,5 -6,0,5" "ARR_2: copy 2 (180 degrees): (-5,0,5)-(-6,0,5)"
arr_chk "$ARR_CYL" 3 "0,-5,5 0,-6,5" "ARR_2: copy 3 (270 degrees): (0,-5,5)-(0,-6,5)"
arr_chk "$ARR_CYL" 4 "5,0,5 6,0,5" "ARR_2: copy 4 (360 degrees) coincides with copy 0"
arr_has "$ARR_PLANE" "ArraySrf: 9 object(s) placed on the surface" "ARR_3: ArraySrf 3 x 3 on the flat patch places 9 copies"
for k in 0 1 2 3 4 5 6 7 8; do
  x=$((5 * (k % 3))); y=$((4 * (k / 3)))
  arr_chk "$ARR_PLANE" "$k" "$x,$y,0 $x,$y,-1" "ARR_3: copy $k stands at ($x,$y,0) along the patch normal (-z: u runs along y, v along x)"
done
