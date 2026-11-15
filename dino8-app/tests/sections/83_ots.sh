# OTS: OffsetSrf of ONE TRIMMED face (kernel::OffsetTrimmedFace wired into the app's OffsetSrf), see ots_script.txt for the cases
# and the closed forms. (No flat-Patch-with-hole case: the app's Patch of FLAT data with two closed curves adds an untrimmed plane, so
# there is no such trimmed face to offer; the kernel test extra/trimmed_offset_tests.cpp covers a flat patch with a hole.) Before: a one-face B-rep fell to the mesh offset (no trimmed face came back).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  OTSO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/ots_script.txt" 2>&1)" || { echo "$OTSO"; echo "FAIL: ots script exited non-zero"; fail=1; OTSO=""; }
else
  OTSO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/ots_script.txt" 2>&1)" || { echo "$OTSO"; echo "FAIL: ots script exited non-zero"; fail=1; OTSO=""; }
fi
otsblock() { awk -v name="$1" '$0 ~ ("Unknown command: OTSBEGIN_" name "$") { on = 1; next } on && /Unknown command: OTSBEGIN_|Unknown command: OTSEND/ { exit } on { print }' <<<"$OTSO"; }
ots_ok() { echo "ok   $1"; }
ots_bad() { echo "FAIL $1"; fail=1; }
otshas() {
  local blk; blk="$(otsblock "$1")"
  if grep -qF -- "$2" <<<"$blk"; then ots_ok "$3"; else ots_bad "$3"; echo "     expected: $2"; { grep -E "^history: (OffsetSrf|Area|Bounding|Object|PlanarSrf|Patch|Created)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fi
}
otslacks() {
  local blk; blk="$(otsblock "$1")"
  if grep -qF -- "$2" <<<"$blk"; then ots_bad "$3"; echo "     unexpected: $2"; else ots_ok "$3"; fi
}
otsre() {
  local blk; blk="$(otsblock "$1")"
  if grep -qE -- "$2" <<<"$blk"; then ots_ok "$3"; else ots_bad "$3"; echo "     expected (regex): $2"; { grep -E "^history: (OffsetSrf|Area|Bounding|Object)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fi
}
# otsarea NAME FIRST|LAST: the n-th "Area = X" value of the case
otsarea() { local a; a="$({ grep -oE "Area = [0-9.eE+-]+" <<<"$(otsblock "$1")" || true; } | sed -E 's/Area = //')"; if [ "$2" = FIRST ]; then head -1 <<<"$a"; else tail -1 <<<"$a"; fi; }
# otsbb NAME FIRST|LAST K: component K (1..6 = min xyz, max xyz) of the case's "Bounding box min a,b,c max d,e,f" line
otsbb() {
  local l; l="$({ grep -E "Bounding box min " <<<"$(otsblock "$1")" || true; } | sed -E 's/.*Bounding box min ([^ ]+) max ([^ ]+).*/\1,\2/')"
  if [ "$2" = FIRST ]; then l="$(head -1 <<<"$l")"; else l="$(tail -1 <<<"$l")"; fi
  awk -F, -v k="$3" '{ print $k }' <<<"$l"
}
otsabs() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v a="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= a) }'; then ots_ok "$4 (measured $1, expected $2 +-$3)"; else ots_bad "$4 (measured '${1:-none}', expected $2 +-$3)"; fi; }
otsrange() { if [ -n "$1" ] && awk -v v="$1" -v l="$2" -v h="$3" 'BEGIN { exit !(v >= l && v <= h) }'; then ots_ok "$4 (measured $1 in [$2, $3])"; else ots_bad "$4 (measured '${1:-none}' not in [$2, $3])"; fi; }
otsnum_after() { { grep -oE "$2 [0-9.eE+-]+" <<<"$(otsblock "$1")" || true; } | head -1 | sed -E "s/$2 //"; }

# ---- the disc: PlanarSrf of a circle r = 5 (a trimmed PLANE), offset by +2 and -2
for c in disc_pos disc_neg; do
  otshas $c "trimmed face offset by the kernel (OffsetTrimmedFace): surface exact analytic offset" "OTS_$c: a PlanarSrf disc takes the kernel's trimmed-face offset, the plane exact (before: mesh offset)"
  otshas $c "(1 translated exactly)" "OTS_$c: its one boundary edge is the original circle translated, not re-fitted"
  otsre $c "Object [0-9]*: valid" "OTS_$c: the offset face is a valid B-rep"
  otsrange "$(otsarea $c FIRST)" 78.0 78.6 "OTS_$c: the disc's area is 25 pi = 78.54 (display polygon: -0.5 percent)"
  otsabs "$(otsarea $c LAST)" "$(otsarea $c FIRST)" 0.01 "OTS_$c: the offset face has the same area as the source (a translation keeps the trim)"
done
ZP_MIN="$(otsbb disc_pos LAST 3)"; ZP_MAX="$(otsbb disc_pos LAST 6)"
ZN_MIN="$(otsbb disc_neg LAST 3)"; ZN_MAX="$(otsbb disc_neg LAST 6)"
otsabs "$ZP_MIN" "$ZP_MAX" 1e-6 "OTS_disc_pos: the offset disc is flat (z min = z max)"
otsabs "${ZP_MAX#-}" 2 1e-6 "OTS_disc_pos: it sits |d| = 2 from the source plane z = 0"
otsabs "$ZN_MIN" "$ZN_MAX" 1e-6 "OTS_disc_neg: the offset disc is flat (z min = z max)"
otsabs "${ZN_MAX#-}" 2 1e-6 "OTS_disc_neg: it sits |d| = 2 from the source plane"
otsabs "$(awk -v a="$ZP_MAX" -v b="$ZN_MAX" 'BEGIN { print a + b }')" 0 1e-6 "OTS_disc: +2 and -2 land on opposite sides of the source (z sum = 0)"
otsabs "$(otsbb disc_pos FIRST 3)" 0 1e-9 "OTS_disc_pos: the source disc is at z = 0 (the offsets above are measured from it)"
# negative control: Exact=No keeps the mesh offset, so no trimmed-face line and no refusal line
otslacks disc_exact_no "trimmed face offset by the kernel" "OTS_disc_exact_no: Exact=No does NOT run the kernel trimmed-face offset (negative control)"

# ---- a freeform saddle Patch with a hole: no closed-form area; the boundary must follow the exact locus S + d N
otshas saddle "trimmed face offset by the kernel (OffsetTrimmedFace): surface tolerance-controlled refit" "OTS_saddle: a freeform Patch face takes the kernel's tolerance-controlled refit (not exact, said so)"
otshas saddle "2 boundary edge(s) rebuilt on the offset surface (0 translated exactly)" "OTS_saddle: both boundary edges (outer ring and hole ring) were rebuilt on the offset surface, none merely translated"
otsre saddle "Object [0-9]*: valid" "OTS_saddle: the offset face is a valid B-rep"
SADDLE_LOCUS="$(otsnum_after saddle 'exact locus S \+ d\*N')"
otsrange "$SADDLE_LOCUS" 0 0.0025 "OTS_saddle: the rebuilt boundary lies within the tolerance bound (2 x 0.001) of the exact locus S + d N of the ORIGINAL surface"
otsre saddle "still one trimmed face" "OTS_saddle: the result is still ONE trimmed face (trims kept)"

# ---- a distance past the saddle's radius of curvature: the kernel refuses with the surface's reason, the command falls back to the mesh offset
otshas saddle_huge "trimmed face offset not used (the surface offset was refused" "OTS_saddle_huge: a 40-unit offset is refused with the surface's own reason (negative control)"
otshas saddle_huge "; using the mesh offset" "OTS_saddle_huge: ...and the command says it fell back to the mesh offset (the documented behaviour)"
otslacks saddle_huge "trimmed face offset by the kernel" "OTS_saddle_huge: ...and the kernel offset did not claim success"
