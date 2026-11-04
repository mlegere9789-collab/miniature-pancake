# QC2_: hostile-input probes of Section / Contour (qc2_script.txt). Finds from the QC pass, each check FAILs on the
# build that has the bug: a non-finite typed point ("nan,5,0", "inf,5,0") was accepted and poisoned the running
# command; Contour with a negative spacing gave no message, with nan spacing finished with "0 plane(s)", and with a
# tiny spacing (1e-9) never returned.
qc2_ok() { echo "ok   $1"; }
qc2_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  QC2_OUT="$(timeout 300 "$BIN" --smoke 30 --script "$HERE/qc2_script.txt" 2>&1)" || { echo "$QC2_OUT" | tail -20; echo "FAIL QC2_run: script exited non-zero or timed out (a hang is the Contour tiny-spacing bug)"; fail=1; QC2_OUT=""; }
else
  QC2_OUT="$(timeout 300 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/qc2_script.txt" 2>&1)" || { echo "$QC2_OUT" | tail -20; echo "FAIL QC2_run: script exited non-zero or timed out (a hang is the Contour tiny-spacing bug)"; fail=1; QC2_OUT=""; }
fi
qc2_case() { awk -v n="QC2BEGIN_$1" 'index($0, "Unknown command: " n) { on = 1; next } on && /Unknown command: QC2BEGIN_/ { exit } on' <<<"$QC2_OUT"; }
qc2_has() { if grep -qF -- "$2" <<<"$1"; then qc2_ok "$3"; else qc2_bad "$3"; fi; }
qc2_hasnot() { if grep -qF -- "$2" <<<"$1"; then qc2_bad "$3"; else qc2_ok "$3"; fi; }
QC2_A="$(qc2_case sec_nan_point)"
qc2_hasnot "$QC2_A" "nan,nan,nan" "QC2_sec_nan_point: a typed nan coordinate is refused, not accepted as a point"
qc2_has "$QC2_A" "Section: 1 curve(s)" "QC2_sec_nan_point: after the refused nan the next two points still cut the box (1 curve)"
QC2_B="$(qc2_case sec_inf_point)"
qc2_has "$QC2_B" "Section: 1 curve(s)" "QC2_sec_inf_point: an inf coordinate is refused; the next two points still cut the box"
QC2_C="$(qc2_case contour_tiny)"
qc2_has "$QC2_C" "planes (limit" "QC2_contour_tiny: a 1e-9 spacing on a 10 mm box is refused with the plane count (no hang)"
qc2_has "$QC2_C" "from 3 plane(s)" "QC2_contour_tiny: the next spacing 5 then cuts at z = 0, 5, 10 (3 planes)"
QC2_D="$(qc2_case contour_bad_spacing)"
qc2_has "$QC2_D" "must be a positive number" "QC2_contour_bad_spacing: a negative/nan spacing gets a message"
qc2_hasnot "$QC2_D" "from 0 plane(s)" "QC2_contour_bad_spacing: nan spacing does not finish the command with 0 planes"
qc2_has "$QC2_D" "from 3 plane(s)" "QC2_contour_bad_spacing: spacing 5 afterwards still works (3 planes)"
QC2_E="$(qc2_case rmk_bad_tol)"
qc2_has "$QC2_E" "'nan' ignored" "QC2_rmk_bad_tol: RemoveMultiKnot Tolerance=nan is refused with a message"
qc2_has "$QC2_E" "'-1' ignored" "QC2_rmk_bad_tol: RemoveMultiKnot Tolerance=-1 is refused with a message"
qc2_has "$QC2_E" "1 object(s) cleaned" "QC2_rmk_bad_tol: Tolerance=100 afterwards still removes the corner"
QC2_F="$(qc2_case fit_bad_opts)"
qc2_has "$QC2_F" "Tolerance must be a positive number, 'abc' ignored" "QC2_fit_bad_opts: FitSrf Tolerance=abc is refused with a message"
qc2_has "$QC2_F" "MaxPoints must be a whole number from 4 to 120, '500' ignored" "QC2_fit_bad_opts: FitSrf MaxPoints=500 is refused with a message"
qc2_has "$QC2_F" "FitSrf: 1 surface(s) fitted" "QC2_fit_bad_opts: Tolerance=0.05 afterwards still fits the surface"
