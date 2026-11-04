# THK: Thicken (Brep::Thicken wired as the app's Thicken command) gives a MEASURABLE solid (thk_script.txt).
# Before: the thickened body's faces share no edge, so each face sampled its coincident edges at its own spacing
# (a thin wall at its thickness, the 10-unit cap at 2 points) and the display mesh had T-junctions: Volume read 0
# ("not closed") for a flat sheet and for a freeform one. Expectations are independent closed forms / an independent
# numeric integral of the offset map, never the app's own printout:
#   flat 10 x 10 sheet, thickness t:   V = 100 t   (t = 0.5 -> 50), bbox z in [-t, 0] one-sided, [-t/2, t/2] symmetric
#   4x4 Bezier bump (area 9.8261):     V(0.1) = 1.00137, V(0.2) = 2.04024, symmetric 0.2 -> 1.96522 (each +-3%: the
#                                      display mesh is a chord-tolerance polyhedron of the true body)
# Negative controls: the un-thickened bump sheet still reports "not closed" with Volume 0 (Volume is not trivially
# closed), and a Thickness far past the bump's radius of curvature is refused by the kernel and leaves the sheet a sheet.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  THKO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/thk_script.txt" 2>&1)" || { echo "$THKO"; echo "FAIL: thk script exited non-zero"; exit 1; }
else
  THKO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/thk_script.txt" 2>&1)" || { echo "$THKO"; echo "FAIL: thk script exited non-zero"; exit 1; }
fi
thkblock() { awk -v name="$1" '$0 ~ ("Unknown command: THKBEGIN_" name "$") { on = 1; next } on && /Unknown command: THKBEGIN_/ { exit } on { print }' <<<"$THKO"; }
thk_ok() { echo "ok   $1"; }
thk_bad() { echo "FAIL $1"; fail=1; }
thkhas() {
  local blk; blk="$(thkblock "$1")"
  if grep -qF -- "$2" <<<"$blk"; then thk_ok "$3"; else thk_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Thicken|Volume|!|Bounding|  Bounding)" <<<"$blk" || true; } | head -6 | sed 's/^/     actual:   /'; fi
}
thklacks() {
  local blk; blk="$(thkblock "$1")"
  if grep -qF -- "$2" <<<"$blk"; then thk_bad "$3"; echo "     unexpected: $2"; else thk_ok "$3"; fi
}
# thkvol NAME: the printed "Volume = X cubic" value of the case (empty if none)
thkvol() { { grep -oE "Volume = [0-9.eE+-]+" <<<"$(thkblock "$1")" || true; } | head -1 | sed -E 's/Volume = //'; }
# thkbbox NAME INDEX: component 1..6 of the "Bounding box: (a, b, c) to (d, e, f)" line of the case's What
thkbbox() { { grep -E "Bounding box: \(" <<<"$(thkblock "$1")" || true; } | head -1 | sed -E 's/.*\(([^)]*)\) to \(([^)]*)\).*/\1, \2/' | awk -F', *' -v k="$2" '{ print $k }'; }
# thkapprox VALUE EXPECTED RELTOL DESC (absolute floor 1e-6)
thkapprox() {
  if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; a = e < 0 ? -e : e; exit !(d <= r * a + 1e-6) }'; then thk_ok "$4 (measured $1, expected $2 +-${3} rel)"; else thk_bad "$4 (measured '${1:-none}', expected $2 +-${3} rel)"; fi
}
thkabs() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v a="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= a) }'; then thk_ok "$4 (measured $1, expected $2 +-$3)"; else thk_bad "$4 (measured '${1:-none}', expected $2 +-$3)"; fi; }

# ---- flat 10 x 10 sheet
thkhas plane_one "Thicken: object 1 thickened by 0.5" "THK_plane_one: Thickness=0.5 is the thickness used"
thklacks plane_one "is not closed" "THK_plane_one: the thickened sheet is a closed solid (no 'not closed' warning)"
thkapprox "$(thkvol plane_one)" 50 0.001 "THK_plane_one: Volume = 10*10*0.5"
thkabs "$(thkbbox plane_one 3)" -0.5 1e-6 "THK_plane_one: bbox z min = -0.5 (one-sided, along -N)"
thkabs "$(thkbbox plane_one 6)" 0 1e-6 "THK_plane_one: bbox z max = 0"
thkhas plane_sym "(symmetric)" "THK_plane_sym: Symmetric=Yes reported"
thkapprox "$(thkvol plane_sym)" 50 0.001 "THK_plane_sym: symmetric thickening has the same volume 10*10*0.5"
thkabs "$(thkbbox plane_sym 3)" -0.25 1e-6 "THK_plane_sym: bbox z min = -0.25"
thkabs "$(thkbbox plane_sym 6)" 0.25 1e-6 "THK_plane_sym: bbox z max = +0.25"

# ---- freeform bump
thkhas bump_sheet "is not closed" "THK_bump_sheet (negative control): the un-thickened sheet is still reported not closed"
thkabs "$(thkvol bump_sheet)" 0 1e-12 "THK_bump_sheet (negative control): ...with Volume 0"
thkhas bump_t01 "Thicken: object 17 thickened by 0.1" "THK_bump_t01: default Thickness=0.1 thickens the freeform sheet"
thklacks bump_t01 "is not closed" "THK_bump_t01: the thickened freeform sheet is closed"
thkapprox "$(thkvol bump_t01)" 1.00137 0.03 "THK_bump_t01: Volume matches the independent offset-map integral"
thkabs "$(thkbbox bump_t01 3)" 0 1e-6 "THK_bump_t01: bbox z min = 0 (the sheet's flat corners, offset only upward)"
thkabs "$(thkbbox bump_t01 6)" 0.640541 1e-3 "THK_bump_t01: bbox z max = 0.5339 (sheet peak) + 0.1 along the local normal"
thkhas bump_t02 "thickened by 0.2" "THK_bump_t02: Thickness=0.2 is the thickness used"
thkapprox "$(thkvol bump_t02)" 2.04024 0.03 "THK_bump_t02: Volume matches the independent offset-map integral"
thkhas bump_sym "(symmetric)" "THK_bump_sym: Symmetric=Yes reported"
thkapprox "$(thkvol bump_sym)" 1.96522 0.03 "THK_bump_sym: symmetric 0.2 volume matches the independent integral"
thkabs "$(thkbbox bump_sym 3)" -0.1 1e-3 "THK_bump_sym: bbox z min = -0.1 (flat corners, half the thickness below)"
# the two one-sided volumes are NOT the same number: a mesh that merely reported a constant would fail one of them
THK_V1="$(thkvol bump_t01)"; THK_V2="$(thkvol bump_t02)"
if [ -n "$THK_V1" ] && [ -n "$THK_V2" ] && awk -v a="$THK_V1" -v b="$THK_V2" 'BEGIN { r = b / a; exit !(r > 1.9 && r < 2.2) }'; then thk_ok "THK_bump: V(0.2)/V(0.1) is about 2.04 (measured $THK_V2 / $THK_V1)"; else thk_bad "THK_bump: V(0.2)/V(0.1) is not about 2.04 (measured '$THK_V2' / '$THK_V1')"; fi

# ---- negative control: a thickness past the radius of curvature is refused, and nothing is replaced
thklacks bump_fold "thickened by" "THK_bump_fold (negative control): Thickness=5 is refused (no 'thickened by' line)"
thkhas bump_fold "no eligible single-face sheet" "THK_bump_fold: ...the command reports nothing was thickened"
thkhas bump_fold "is not closed" "THK_bump_fold: ...and the sheet stays an open sheet (Volume still 'not closed')"
