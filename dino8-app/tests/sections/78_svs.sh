# SVS_: Reflect (kernel SubD::Symmetrize) keeps a pinned SubD corner on both halves (svs_script.txt).
# Failing-first, measured on the kernel in isolation (tests/extra/subd_symmetrize_corner.cpp): before the change the mirror image of a
# pinned corner read Crease (limit (28.3333, 1.6667, 0)) and, when a face lay in the mirror plane, the original pin was lost as well;
# 4 of 9 checks failed, 0 of 9 after. Expectations are hand-derived limit points of the 10-unit cube cage:
#   pinned Corner      limit == control point:  (0,0,0), mirrored across x = 15: (30,0,0), across x = 10: (20,0,0)
#   unpinned Crease    (a + 4 v + b) / 6 with a = (20,0,0), b = (30,10,0), v = (30,0,0)  =  (28.3333, 1.6667, 0)
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SVSO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/svs_script.txt" 2>&1)" || { echo "$SVSO"; echo "FAIL: svs script exited non-zero"; exit 1; }
else
  SVSO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/svs_script.txt" 2>&1)" || { echo "$SVSO"; echo "FAIL: svs script exited non-zero"; exit 1; }
fi
svs_ok() { echo "ok   $1"; }
svs_bad() { echo "FAIL $1"; fail=1; }
svsblock() { awk -v name="$1" '$0 ~ ("Unknown command: SVSBEGIN_" name "$") { on = 1; next } on && /Unknown command: SVSBEGIN_/ { exit } on { print }' <<<"$SVSO"; }
# svsinfo NAME N: the N-th "SubDVertexInfo: object ..." line of the case
svsinfo() { { grep -E "SubDVertexInfo: object [0-9]+ vertex" <<<"$(svsblock "$1")" || true; } | sed -n "${2}p"; }
svslimit() { { grep -oE "limit point \([^)]*\)" <<<"$1" || true; } | head -1 | sed -E 's/limit point \(([^,]*), ([^,]*), ([^)]*)\)/\1 \2 \3/'; }
svspoint() {
  local got; got="$(svslimit "$1")"
  if [ -n "$got" ] && awk -v g="$got" -v x="$2" -v y="$3" -v z="$4" -v t="$5" 'function pt(w,   l, f) { if (w < 0) w = -w; if (w < 1e-9) return 0; l = log(w) / log(10) + 1e-9; f = int(l); if (l < f) f -= 1; return 0.51 * 10 ^ (f - 3) }
    BEGIN { split(g, a, " "); ok = 1; w[1] = x; w[2] = y; w[3] = z; for (i = 1; i <= 3; i++) { d = a[i] - w[i]; if (d < 0) d = -d; e = pt(w[i]); if (e < t) e = t; if (d > e) ok = 0 } exit !ok }'; then
    svs_ok "$6 (limit point $got, hand-derived ($2, $3, $4), per component +-max($5, half a unit of the app's 4th printed significant digit))"
  else
    svs_bad "$6 (limit point '${got:-none}', hand-derived ($2, $3, $4), per component +-max($5, half a unit of the app's 4th printed significant digit))"; echo "     line: ${1:-none}"
  fi
}
svshas() { if grep -qF -- "$2" <<<"$1"; then svs_ok "$3"; else svs_bad "$3"; echo "     expected: $2"; echo "     line: ${1:-none}"; fi; }

# ---- plane x = 15: the original keeps its pin, the image is pinned too
svshas "$(svsblock apart)" "Reflect: mirrored across the plane" "SVS_apart: Reflect produced the mirrored SubD"
L="$(svsinfo apart 1)"
svshas "$L" "vertex (0, 0, 0): Corner" "SVS_apart: the original origin is still a Corner"
svspoint "$L" 0 0 0 0.001 "SVS_apart: ...limit point (0, 0, 0)"
L="$(svsinfo apart 2)"
svshas "$L" "vertex (30, 0, 0): Corner, valence 3, 3 face(s), 2 crease edge(s)" "SVS_apart: the mirror image (30,0,0) is a Corner on exactly two crease edges"
svspoint "$L" 30 0 0 0.001 "SVS_apart: ...limit point == control point (30, 0, 0)"
# ---- plane x = 10: the far face lies in the plane (rebuild path)
L="$(svsinfo inplane 1)"
svshas "$L" "vertex (0, 0, 0): Corner" "SVS_inplane: the original pin survives the face-by-face rebuild"
svspoint "$L" 0 0 0 0.001 "SVS_inplane: ...limit point (0, 0, 0)"
L="$(svsinfo inplane 2)"
svshas "$L" "vertex (20, 0, 0): Corner" "SVS_inplane: the image (20,0,0) is a Corner"
svspoint "$L" 20 0 0 0.001 "SVS_inplane: ...limit point (20, 0, 0)"
# ---- control: without a pin the image is an ordinary Crease vertex
L="$(svsinfo unpinned 1)"
svshas "$L" "vertex (30, 0, 0): Crease" "SVS_unpinned (control): without a pin the image is a Crease vertex"
svspoint "$L" 28.333333 1.666667 0 0.001 "SVS_unpinned (control): its limit point is the crease rule (a + 4 v + b)/6"
