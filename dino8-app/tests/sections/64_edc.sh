# ---------------------------------------------------------------------------
# EDC_: ExtrudeCrvTapered on a curved closed profile (edc_script.txt). The kernel's Brep::ExtrudeTapered now builds the exact draft
# family for a closed smooth curved profile; the oracle is Steiner's formula for the parallel curves of the ellipse 3 x 2:
# V = A L - P tan(a) L^2/2 + pi tan(a)^2 L^3/3 (A = pi a b, P = 15.8654), computed outside the program. ExtrudeCrvTapered now prints the kernel's
# exact volume; the tessellated Volume is only a cross-check. Negative control: a fold through the centre of
# curvature is refused with the kernel's reason and builds nothing; control: the circle (an exact conic, older path) still gives
# the exact frustum. NOT checked here: History replay of this command, open or cornered curved profiles (they use the old refit).
# ---------------------------------------------------------------------------
edc_ok() { echo "ok   $1"; }
edc_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  EDC_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/edc_script.txt" 2>&1)" || { echo "$EDC_OUT" | tail -40; echo "FAIL EDC_run: script exited non-zero"; fail=1; EDC_OUT=""; }
else
  EDC_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/edc_script.txt" 2>&1)" || { echo "$EDC_OUT" | tail -40; echo "FAIL EDC_run: script exited non-zero"; fail=1; EDC_OUT=""; }
fi
if grep -q "^FAIL" <<<"$EDC_OUT"; then edc_bad "EDC_script: an @expect line of the script failed"; else edc_ok "EDC_script: every @expect_objects line held"; fi
edc_case() { awk -v n="$1" '/Command: ExtrudeCrvTapered( |$)/ { c++ } c == n' <<<"$EDC_OUT"; }
edc_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
edc_has() { if grep -qF -- "$2" <<<"$1"; then edc_ok "$3"; else edc_bad "$3"; near "$1" "$2"; fi; }
# the n-th occurrence of "<prefix><number>" in $1 must lie in [lo, hi]
edc_nth() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | sed -n "${3}p" | grep -oE '[-0-9.e+]+$' || true)"
  if edc_in "$v" "$4" "$5"; then edc_ok "$6 ($v in $4..$5)"; else edc_bad "$6 ('$v' not in $4..$5)"; fi
}
edc_num() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | head -1 | grep -oE '[-0-9.e+]+$' || true)"
  if edc_in "$v" "$3" "$4"; then edc_ok "$5 ($v in $3..$4)"; else edc_bad "$5 ('$v' not in $3..$4)"; fi
}
EDC_1="$(edc_case 1)"; EDC_2="$(edc_case 2)"; EDC_3="$(edc_case 3)"; EDC_4="$(edc_case 4)"; EDC_5="$(edc_case 5)"
edc_has "$EDC_1" "1 object(s)" "EDC_1: the drafted ellipse is built"
edc_num "$EDC_1" "exact volume " 52.4230 52.4240 "EDC_1: kernel exact volume = A L - P tan L^2/2 + pi tan^2 L^3/3 = 52.42352 (Steiner; the old refit read 52.168)"
edc_num "$EDC_1" "Volume = " 52.0 52.6 "EDC_1: the tessellated Volume command agrees to 0.8 percent (cross-check)"
edc_num "$EDC_2" "exact volume " 96.1112 96.1122 "EDC_2: flared outward (-0.15 rad): kernel exact volume 96.11171 (the old refit read 96.0958)"
edc_has "$EDC_3" "fold" "EDC_3: a draft beyond the smallest radius of curvature is refused with the fold reason"
edc_has "$EDC_3" "0 object(s)" "EDC_3: ...and nothing is built"
edc_num "$EDC_4" "exact volume " 32.3265 32.3275 "EDC_4_control: the circle (exact conic path) is the exact frustum pi h/3 (R^2 + R r + r^2) = pi (4 + 3.4 + 2.89) = 32.32699 (the range used to be centred on 32.32566, a mis-added constant)"
# History replay: the drafted ellipse records the ellipse as its source; scaling the source rebuilds the solid
edc_has "$EDC_5" "ExtrudeCrvTapered <- 1" "EDC_5: the History record lists the ellipse (object 1) as the source of the solid"
edc_nth "$EDC_5" "Volume = " 1 52.0 52.6 "EDC_5a: built with History On, the drafted ellipse's volume is 52.42 (Steiner)"
edc_nth "$EDC_5" "Volume = " 2 132.4 135.2 "EDC_5b: the source scaled by 1.5 (ellipse 4.5 x 3), the solid rebuilt: Steiner volume 133.807"
