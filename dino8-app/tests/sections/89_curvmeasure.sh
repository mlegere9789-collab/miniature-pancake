# ---------------------------------------------------------------------------
# CRVM_: CurvatureAnalysis prints the MEASURED median curvature (curvature_measure_script.txt).
# The command used to print only "range LO (blue) .. HI (red)", a 5..95 percentile ramp of the per-vertex curvature, so nothing a script
# could read said whether the discrete Gaussian / mean curvature was right. The report now ends "measured median V (1/length^2 | 1/length)".
# Closed forms (worked by hand, never read from the app): sphere radius r  K = 1/r^2, H = 1/r;  cylinder radius r  K = 0, H = 1/(2r).
#   sphere r=10 : K 0.01   H 0.1         sphere r=5 : K 0.04   H 0.2         cylinder r=5 : K 0   H 0.1
# The display mesh is a chord-tolerance polygon (0.05), so the discrete estimate sits a few percent HIGH (measured offline with the app's own
# mesher and DiscreteCurvature on these exact solids: +4..5%, cylinder K -0.0008..+0.0013), hence a +-8% window, and |K| < 0.005 for the
# cylinder (a sphere-like K would be 0.04). The windows are tight enough to tell r=10 from r=5 (K x4, H x2) and a cylinder's K from a sphere's.
# The six reports come back in script order; each line names its own Style, which the check also verifies.
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CRVMO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/curvature_measure_script.txt" 2>&1)" || { echo "$CRVMO"; echo "FAIL: curvature-measure script exited non-zero"; exit 1; }
else
  CRVMO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/curvature_measure_script.txt" 2>&1)" || { echo "$CRVMO"; echo "FAIL: curvature-measure script exited non-zero"; exit 1; }
fi
echo "$CRVMO" | grep -q "^smoke:" || { echo "$CRVMO"; echo "FAIL: curvature-measure script produced no smoke line"; fail=1; }
CRVM_LINES="$(grep -E "CurvatureAnalysis on 1 object\(s\)" <<<"$CRVMO" | grep -v "Command:" || true)"
# crvm_check N STYLE LO HI DESC: report number N (1-based) has Style=STYLE and a measured median within [LO, HI]
crvm_check() {
  local line val
  line="$(sed -n "${1}p" <<<"$CRVM_LINES")"
  val="$(sed -nE 's/.*measured median (-?[0-9.]+) .*/\1/p' <<<"$line")"
  if [ -n "$val" ] && grep -q "Style=$2 " <<<"$line" && awk -v v="$val" -v lo="$3" -v hi="$4" 'BEGIN { exit !(v >= lo && v <= hi) }'; then
    echo "ok   $5 (measured $val, window $3 .. $4)"
  else
    echo "FAIL $5"; echo "     expected Style=$2 median in [$3, $4]"; echo "     actual:   ${line:-<no report #$1>}"; fail=1
  fi
}
crvm_check 1 Gaussian 0.0092 0.0108 "CRVM_sphere10_K: Gaussian curvature of a radius-10 sphere is 1/r^2 = 0.01"
crvm_check 2 Mean 0.092 0.108 "CRVM_sphere10_H: mean curvature of a radius-10 sphere is 1/r = 0.1"
crvm_check 3 Gaussian 0.0368 0.0432 "CRVM_sphere5_K: Gaussian curvature of a radius-5 sphere is 1/r^2 = 0.04 (4x the r=10 value)"
crvm_check 4 Mean 0.184 0.216 "CRVM_sphere5_H: mean curvature of a radius-5 sphere is 1/r = 0.2 (2x the r=10 value)"
crvm_check 5 Gaussian -0.005 0.005 "CRVM_cyl5_K: Gaussian curvature of a cylinder is 0 (one principal curvature is 0)"
crvm_check 6 Mean 0.092 0.108 "CRVM_cyl5_H: mean curvature of a radius-5 cylinder is 1/(2r) = 0.1"
