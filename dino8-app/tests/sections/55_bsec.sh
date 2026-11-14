# BSEC_: Body sectioning (kernel: Feature operations): Section / Contour of a B-rep take the EXACT route
# (kernel::SectionBrepByPlaneJoined: the plane is intersected with every face's own surface and the pieces are joined
# into one NURBS curve per physical contour); a mesh object still slices its mesh (see bsec_script.txt for the cases).
# Everything is MEASURED from the app's own output and compared with a closed form: the sphere cap circle
# 2 pi sqrt(100-36) = 50.2655 (one curve, via Length), the box rectangle perimeter 2 (20 + 10) = 60 (ONE curve stitched
# from the four walls), the mesh box the same 60 but reported as a mesh slice. Negative controls: a plane missing the
# sphere makes 0 curves; the mesh box is NOT reported as exact. Output checks use here-strings (no echo | grep).
# ---------------------------------------------------------------------------
bsec_ok() { echo "ok   $1"; }
bsec_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  BSEC_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/bsec_script.txt" 2>&1)" || { echo "$BSEC_OUT" | tail -40; echo "FAIL BSEC_run: script exited non-zero"; fail=1; BSEC_OUT=""; }
else
  BSEC_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/bsec_script.txt" 2>&1)" || { echo "$BSEC_OUT" | tail -40; echo "FAIL BSEC_run: script exited non-zero"; fail=1; BSEC_OUT=""; }
fi
bsec_case() { awk -v n="$1" '/Command: (Section|Contour)( |$)/ { c++ } c == n' <<<"$BSEC_OUT"; }
bsec_has() { if grep -qF -- "$2" <<<"$1"; then bsec_ok "$3"; else bsec_bad "$3"; near "$1" "$2"; fi; }
bsec_hasnot() { if grep -qF -- "$2" <<<"$1"; then bsec_bad "$3"; near "$1" "$2"; else bsec_ok "$3"; fi; }
# the number after "Total length = " in case text $1 within [$2, $3]
bsec_len() {
  local v
  v="$({ grep -o "Total length = [0-9.e+-]*" <<<"$1" || true; } | head -1 | awk '{print $4}')"
  if awk -v v="${v:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; then bsec_ok "$4 (measured length $v in $2..$3)"; else bsec_bad "$4 (length '$v' not in $2..$3)"; fi
}
if grep -q "^FAIL" <<<"$BSEC_OUT"; then bsec_bad "BSEC_script: an @expect line of the script failed"; else bsec_ok "BSEC_script: no script-level FAIL line"; fi
BSEC_1="$(bsec_case 1)"; BSEC_2="$(bsec_case 2)"; BSEC_3="$(bsec_case 3)"; BSEC_4="$(bsec_case 4)"; BSEC_5="$(bsec_case 5)"
bsec_has "$BSEC_1" "Section: 1 curve(s) (1 exact B-rep section curve(s)" "BSEC_1: the sphere section is ONE curve and came from the exact B-rep route"
bsec_len "$BSEC_1" 50.20 50.31 "BSEC_1: the sphere section's length equals the cap circle 2 pi sqrt(R^2-d^2) = 50.2655 (a mesh chord polygon would be shorter)"
bsec_has "$BSEC_2" "Section: 1 curve(s) (1 exact B-rep section curve(s)" "BSEC_2: the four box walls were stitched into ONE exact contour curve"
bsec_len "$BSEC_2" 59.95 60.05 "BSEC_2: the box section's length equals the rectangle perimeter 2 (20 + 10) = 60"
bsec_has "$BSEC_3" "Section: 0 curve(s)" "BSEC_3 (negative control): a plane that misses the sphere makes no curve"
bsec_hasnot "$BSEC_3" "exact B-rep section curve(s)" "BSEC_3: ...and nothing is reported as an exact section curve"
bsec_has "$BSEC_4" "(mesh slices)" "BSEC_4 (negative control): a MESH box is still sliced as a mesh and reported as such"
bsec_hasnot "$BSEC_4" "exact B-rep section curve(s)" "BSEC_4: ...and is never claimed as an exact B-rep section"
bsec_len "$BSEC_4" 59.9 60.1 "BSEC_4: the mesh slice still measures the rectangle perimeter 60"
bsec_has "$BSEC_5" "Contour: 3 curve(s) from 5 plane(s) (3 exact B-rep section curve(s)" "BSEC_5: Contour of the B-rep sphere gives 3 circles (the two pole planes only touch), all on the exact route"
