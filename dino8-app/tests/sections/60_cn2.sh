# CN2_: G0 / G1 / G2 edge continuity on Loft, Patch and NetworkSrf against a CURVED adjacent surface (cn2_script.txt).
# The adjacent surface is a 270 degree cylinder wall (radius 5, axis z, height 10; RATIONAL), edge x = 0, y = -5. Every angle /
# gap / curvature below is a number the app printed from the kernel's MEASUREMENT of the finished surface against the wall;
# each "without the option" figure is the SAME construction with Continuity=Position (G0). WINDOWS: the first push of this
# section ran with wide windows and printed every measured line ("cn2 measured ..."); the windows are then tightened to
# the measured value + margin (see the cn2_win lines).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  CN2O="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/cn2_script.txt" 2>&1)" || { echo "$CN2O"; echo "FAIL: cn2 script exited non-zero"; fail=1; CN2O=""; }
else
  CN2O="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/cn2_script.txt" 2>&1)" || { echo "$CN2O"; echo "FAIL: cn2 script exited non-zero"; fail=1; CN2O=""; }
fi
cn2_block() { awk -v name="$1" '$0 ~ ("Unknown command: CN2BEGIN_" name "$") { on = 1; next } on && /Unknown command: CN2BEGIN_/ { exit } on { print }' <<<"$CN2O"; }
# cn2_val NAME LINE_REGEX PREFIX: the number right after PREFIX in the last matching line of the case (empty if none)
cn2_val() { cn2_block "$1" | grep -E "$2" | tail -1 | sed -nE "s/.*$3([0-9.eE+-]+).*/\1/p" || true; }
# cn2_num NAME LINE_REGEX PREFIX MIN MAX DESC
cn2_num() {
  local v; v="$(cn2_val "$1" "$2" "$3")"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$4" -v hi="$5" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $6 ($3$v in [$4, $5])"; else echo "FAIL $6 ('$3${v:-none}' not in [$4, $5])"; fail=1; fi
}
# cn2_ratio NAME_A LINE_A NAME_B LINE_B PREFIX FACTOR DESC: value in case A is below (value in case B) / FACTOR
cn2_ratio() {
  local a b; a="$(cn2_val "$1" "$2" "$5")"; b="$(cn2_val "$3" "$4" "$5")"
  if [ -n "$a" ] && [ -n "$b" ] && awk -v a="$a" -v b="$b" -v f="$6" 'BEGIN { exit !(a < b / f) }'; then echo "ok   $7 (${5}${a} vs without the option ${b}, factor > $6)"; else echo "FAIL $7 ('${a:-none}' vs '${b:-none}', need factor > $6)"; fail=1; fi
}
cn2_has() { if cn2_block "$1" | grep -q -- "$2"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; cn2_block "$1" | grep -E "^history: (Loft|Patch|NetworkSrf|MeasureEdgeContinuity|!)" | head -4 | cut -c1-500 | sed 's/^/     actual:   /' || true; fail=1; fi; }
cn2_objs() {
  local n; n="$(cn2_block "$1" | grep -cE "^history: Object [0-9]+ \(" || true)"
  if [ "$n" = "$2" ]; then echo "ok   $3 ($n object(s))"; else echo "FAIL $3 ('$n' object(s), expected $2)"; fail=1; fi
}
CN2_LOFT="^history: Loft: G[012] continuity .*MEASURED"
CN2_PATCH="^history: Patch: G[012] continuity .*MEASURED"
CN2_NET="^history: NetworkSrf: G[012] continuity .*MEASURED"
# every measured line, so a window can be set (or re-set) from the log
for cn2_case in cyl_loft_g0 cyl_loft_g1 cyl_loft_g2 cyl_patch_g0 cyl_patch_g1 cyl_patch_g2 cyl_net_g0 cyl_net_g1; do
  cn2_block "$cn2_case" | grep -E "^history: (Loft|Patch|NetworkSrf): .*(MEASURED|refused)" | cut -c1-700 | sed "s/^/cn2 measured $cn2_case: /" || true
done

# --- windows: loft (G1 / G2: the kernel's own cylinder checks pin angle and gap < 1e-6 and curvature < 1e-6)
CN2_LOFT_G1_ANGLE_MAX=1e-6; CN2_LOFT_G2_ANGLE_MAX=1e-6; CN2_LOFT_G0_ANGLE_MIN=1e-3
# patch / network (least squares, not exact): wide until measured
CN2_PATCH_G1_ANGLE_MAX=1; CN2_PATCH_G2_ANGLE_MAX=1; CN2_NET_G1_ANGLE_MAX=1
# --- Loft
cn2_has cyl_loft_g0 "(position only: the tangent angle and curvature above are reported, not constrained)" "CN2_cyl_loft_g0: Continuity=Position says it constrains position only"
cn2_num cyl_loft_g0 "$CN2_LOFT" "tangent angle " "$CN2_LOFT_G0_ANGLE_MIN" 4 "CN2_cyl_loft_g0: the loft without tangent matching is measurably off the cylinder wall's tangent plane"
cn2_objs cyl_loft_g0 5 "CN2_cyl_loft_g0: wall + 3 sections + 1 loft"
cn2_num cyl_loft_g1 "$CN2_LOFT" "tangent angle " 0 "$CN2_LOFT_G1_ANGLE_MAX" "CN2_cyl_loft_g1: Tangency against the RATIONAL wall: measured angle ~0"
cn2_num cyl_loft_g1 "$CN2_LOFT" "position gap " 0 1e-6 "CN2_cyl_loft_g1: measured position gap"
cn2_num cyl_loft_g1 "$CN2_LOFT" "from the finished surface " 0 1e-3 "CN2_cyl_loft_g1: worst section deviation within the document tolerance 0.001"
cn2_ratio cyl_loft_g1 "$CN2_LOFT" cyl_loft_g0 "$CN2_LOFT" "tangent angle " 100 "CN2_cyl_loft_g1: measured angle >= 100x below the loft without matching"
cn2_objs cyl_loft_g1 5 "CN2_cyl_loft_g1: one new surface"
cn2_num cyl_loft_g2 "$CN2_LOFT" "tangent angle " 0 "$CN2_LOFT_G2_ANGLE_MAX" "CN2_cyl_loft_g2: Curvature against the RATIONAL wall (4 sections): measured angle ~0"
cn2_num cyl_loft_g2 "$CN2_LOFT" "position gap " 0 1e-6 "CN2_cyl_loft_g2: measured position gap"
cn2_num cyl_loft_g2 "$CN2_LOFT" "cross-edge curvature difference " 0 1e-6 "CN2_cyl_loft_g2: measured cross-edge curvature difference ~0"
cn2_num cyl_loft_g2 "$CN2_LOFT" "from the finished surface " 0 1e-3 "CN2_cyl_loft_g2: worst section deviation within the document tolerance 0.001"
cn2_objs cyl_loft_g2 6 "CN2_cyl_loft_g2: one new surface (wall + 4 sections + loft)"
# --- Patch
cn2_has cyl_patch_g0 "(position only: the tangent angle and curvature above are reported, not constrained)" "CN2_cyl_patch_g0: Continuity=Position says it constrains position only"
cn2_num cyl_patch_g1 "$CN2_PATCH" "tangent angle " 0 "$CN2_PATCH_G1_ANGLE_MAX" "CN2_cyl_patch_g1: Tangency against the wall: measured angle"
cn2_ratio cyl_patch_g1 "$CN2_PATCH" cyl_patch_g0 "$CN2_PATCH" "tangent angle " 1 "CN2_cyl_patch_g1: measured angle below the patch without the option"
cn2_num cyl_patch_g2 "$CN2_PATCH" "tangent angle " 0 "$CN2_PATCH_G2_ANGLE_MAX" "CN2_cyl_patch_g2: Curvature against the wall: measured angle"
cn2_ratio cyl_patch_g2 "$CN2_PATCH" cyl_patch_g0 "$CN2_PATCH" "cross-edge curvature difference " 1 "CN2_cyl_patch_g2: measured cross-edge curvature difference below the patch without the option"
# --- NetworkSrf
cn2_has cyl_net_g0 "(position only: the tangent angle and curvature above are reported, not constrained)" "CN2_cyl_net_g0: Continuity=Position says it constrains position only"
cn2_num cyl_net_g1 "$CN2_NET" "tangent angle " 0 "$CN2_NET_G1_ANGLE_MAX" "CN2_cyl_net_g1: Tangency against the wall: measured angle"
cn2_ratio cyl_net_g1 "$CN2_NET" cyl_net_g0 "$CN2_NET" "tangent angle " 1 "CN2_cyl_net_g1: measured angle below the network without the option"
