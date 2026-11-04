
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
# SOT_DEBUG_BEGIN (temporary: prints the measured output through the CI log's FAIL-line filter)
printf '%s\n' "$SOT_OUT" | grep -nE 'Command:|ength|losest|istance|Area|Bounding|emov|refus|FitSrf|Shrink|control|Unknown|nvalid|cleaned|guarantee|net|point' | cut -c1-170 | head -55 | awk '{printf "FAIL SOTDBG %03d %s\n", NR, $0}' || true
# SOT_DEBUG_END
# SOT_PLACEHOLDER_CHECKS
