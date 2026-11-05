
# ---------------------------------------------------------------------------
# SOT_: RemoveMultiKnot / FitSrf / ShrinkTrimmedSrf with tolerance contracts (sot_script.txt). Every figure is MEASURED
# by the app (Length, ClosestPt, Area, BoundingBox) and compared with a closed form or an independent bound; the
# negative controls (a corner refused at a small Tolerance and left unchanged as measured, an unreachable FitSrf
# tolerance at MaxPoints=4) are checked against the refusal reasons. Output checks use here-strings.
# ---------------------------------------------------------------------------
sot_ok() { echo "ok   $1"; }
sot_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SOT_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/sot_script.txt" 2>&1)" || { echo "$SOT_OUT" | tail -40; echo "FAIL SOT_run: script exited non-zero"; fail=1; SOT_OUT=""; }
else
  SOT_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/sot_script.txt" 2>&1)" || { echo "$SOT_OUT" | tail -40; echo "FAIL SOT_run: script exited non-zero"; fail=1; SOT_OUT=""; }
fi
if grep -q "^FAIL" <<<"$SOT_OUT"; then sot_bad "SOT_script: an @expect line of the script failed"; else sot_ok "SOT_script: every @expect_objects line held"; fi
sot_has() { if grep -qF -- "$2" <<<"$1"; then sot_ok "$3"; else sot_bad "$3"; near "$1" "$2"; fi; }
sot_hasnot() { if grep -qF -- "$2" <<<"$1"; then sot_bad "$3"; near "$1" "$2"; else sot_ok "$3"; fi; }
sot_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
SOT_LEN="$({ grep -o "Total length = [0-9.e+-]*" <<<"$SOT_OUT" || true; } | awk '{print $4}')"
sot_len() { sed -n "${1}p" <<<"$SOT_LEN"; }
# --- RMK1: a degree-multiplicity knot added by InsertKink on a smooth cubic is removed exactly (bound 0, curve unchanged)
sot_has "$SOT_OUT" "Curve 1: kink inserted (8 control points)" "SOT_RMK1: InsertKink raised the cubic to 8 control points"
sot_has "$SOT_OUT" "RemoveMultiKnot: 1 object(s) cleaned (2 knot copy(ies) removed, guaranteed shape change at most 0, within Tolerance 1e-06)" "SOT_RMK1: Tolerance 1e-6 removes both knot copies with a guaranteed shape change of exactly 0"
sot_has "$SOT_OUT" "degree 3, 6 control points, non-rational, open" "SOT_RMK1: the cleaned curve is back to the original 6 control points"
if [ -n "$(sot_len 1)" ] && [ "$(sot_len 1)" = "$(sot_len 2)" ] && [ "$(sot_len 2)" = "$(sot_len 3)" ] && sot_in "$(sot_len 1)" 11.0 11.3; then sot_ok "SOT_RMK1: Length is identical before the kink, after it and after the removal ($(sot_len 1))"; else sot_bad "SOT_RMK1: Length changed across InsertKink/RemoveMultiKnot ($(sot_len 1) $(sot_len 2) $(sot_len 3))"; fi
# --- RMK2: a genuine C0 corner (polyline 5 + 4 = 9, elevated to degree 3)
if [ "$(sot_len 4)" = "9" ] && [ "$(sot_len 5)" = "9" ]; then sot_ok "SOT_RMK2: the elevated right-angle polyline measures 5 + 4 = 9"; else sot_bad "SOT_RMK2: polyline Length is not 9 ($(sot_len 4) $(sot_len 5))"; fi
sot_has "$SOT_OUT" "left unchanged - no multiple knot can be removed within Tolerance 0.001" "SOT_RMK2: Tolerance 0.001 refuses to smooth the corner and says why"
sot_has "$SOT_OUT" "0 object(s) cleaned; 1 left unchanged (shape change above Tolerance)" "SOT_RMK2: the refused run reports 0 cleaned, 1 left unchanged"
sot_has "$SOT_OUT" "guaranteed shape change at most 1.067, within Tolerance 100" "SOT_RMK2: Tolerance 100 removes the corner with a reported bound of 1.067"
if sot_in "$(sot_len 6)" 7.0 8.5 && awk -v a="$(sot_len 6)" 'BEGIN { exit !(a + 0 < 9) }'; then sot_ok "SOT_RMK2: the smoothed curve is shorter than the corner (7.838 < 9)"; else sot_bad "SOT_RMK2: smoothed Length not in (7, 8.5) and below 9 ($(sot_len 6))"; fi
SOT_CPD="$({ grep -o "Closest point [-0-9.,e]* distance [0-9.e+-]*" <<<"$SOT_OUT" || true; } | awk '{print $NF}' | head -1)"
if sot_in "$SOT_CPD" 0.5 1.067; then sot_ok "SOT_RMK2: ClosestPt of the old corner (5,0,0) is $SOT_CPD from the new curve: positive and within the reported bound 1.067"; else sot_bad "SOT_RMK2: corner-to-curve distance '$SOT_CPD' not in (0.5, 1.067]"; fi
# SOT_DEBUG_BEGIN (temporary: prints the measured output through the CI log's FAIL-line filter)
printf '%s\n' "$SOT_OUT" | awk '/Command: Loft/{f=1} f' | grep -vE 'Command: (Sel|Delete|Enter)' | cut -c1-190 | head -55 | awk '{printf "FAIL SOTDBG %03d %s\n", NR, $0}' || true
# SOT_DEBUG_END
# SOT_PLACEHOLDER_CHECKS
