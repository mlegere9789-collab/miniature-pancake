# ---------------------------------------------------------------------------
# PIPEV_: Pipe EndRadius= (tapered) and InnerRadius= (hollow) (pipev_script.txt) - the kernel's exact Brep::PipeVariable and
# Brep::PipeThickWalled from the app, on the straight rail (0,0,0)-(0,0,10). Closed forms computed outside the program: the
# frustum pi L/3 (R^2 + R r + r^2) = 73.3038, the hollow cylinder pi (R^2 - r^2) L = 94.2478, the History rebuild after the rail is
# scaled 2x: 146.608. Negative controls: the two options together, an inner radius >= the outer one, a closed rail with unequal
# end radii - each refused with a reason and nothing built; control: the plain pipe still gives the solid cylinder.
# NOT checked here: tapered / hollow pipes along curved rails, Cap=No, History edit of the new options (EditHistory lists no parameter for them).
# ---------------------------------------------------------------------------
piv_ok() { echo "ok   $1"; }
piv_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  PIV_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/pipev_script.txt" 2>&1)" || { echo "$PIV_OUT" | tail -40; echo "FAIL PIV_run: script exited non-zero"; fail=1; PIV_OUT=""; }
else
  PIV_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/pipev_script.txt" 2>&1)" || { echo "$PIV_OUT" | tail -40; echo "FAIL PIV_run: script exited non-zero"; fail=1; PIV_OUT=""; }
fi
if grep -q "^FAIL" <<<"$PIV_OUT"; then piv_bad "PIV_script: an @expect line of the script failed"; else piv_ok "PIV_script: every @expect_objects line held"; fi
piv_case() { awk -v n="$1" '/Command: Pipe( |$)/ { c++ } c == n' <<<"$PIV_OUT"; }
piv_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
piv_has() { if grep -qF -- "$2" <<<"$1"; then piv_ok "$3"; else piv_bad "$3"; near "$1" "$2"; fi; }
piv_nth() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | sed -n "${3}p" | grep -oE '[-0-9.e+]+$' || true)"
  if piv_in "$v" "$4" "$5"; then piv_ok "$6 ($v in $4..$5)"; else piv_bad "$6 ('$v' not in $4..$5)"; fi
}
PIV_1="$(piv_case 1)"; PIV_2="$(piv_case 2)"; PIV_3="$(piv_case 3)"; PIV_4="$(piv_case 4)"; PIV_5="$(piv_case 5)"; PIV_6="$(piv_case 6)"; PIV_7="$(piv_case 7)"
piv_has "$PIV_1" "tapered pipe" "PIV_1: EndRadius= builds the kernel's tapered pipe"
piv_nth "$PIV_1" "exact volume " 1 73.300 73.308 "PIV_1: kernel exact volume = pi 10/3 (4 + 2 + 1) = 73.3038 (the cone frustum)"
piv_nth "$PIV_1" "Volume = " 1 72.5 73.6 "PIV_1: the tessellated Volume command agrees (cross-check)"
piv_has "$PIV_2" "hollow (thick-walled) pipe" "PIV_2: InnerRadius= builds the kernel's hollow pipe"
piv_nth "$PIV_2" "exact volume " 1 94.244 94.252 "PIV_2: kernel exact volume = pi (4 - 1) 10 = 94.2478 (the hollow cylinder)"
piv_has "$PIV_3" "cannot be combined" "PIV_3: EndRadius= with InnerRadius= is refused"
piv_has "$PIV_3" "nothing was built" "PIV_3: ...and nothing is built"
piv_has "$PIV_4" "nothing was built" "PIV_4: an inner radius not below the outer one is refused by the kernel, nothing built"
piv_has "$PIV_5" "1 pipe(s)" "PIV_5_control: the plain pipe is built"
piv_nth "$PIV_5" "Volume = " 1 124.5 126.0 "PIV_5_control: the solid cylinder pi 4 10 = 125.664 (differs from the taper's 73.30)"
piv_has "$PIV_6" "nothing was built" "PIV_6: a closed rail with unequal end radii is refused, nothing built"
piv_nth "$PIV_7" "Volume = " 1 72.5 73.6 "PIV_7a: with History On the tapered pipe's volume is 73.30"
piv_nth "$PIV_7" "Volume = " 2 144.8 148.4 "PIV_7b: the rail scaled 2x, the taper rebuilt with the recorded radii: pi 20/3 (4 + 2 + 1) = 146.608"
