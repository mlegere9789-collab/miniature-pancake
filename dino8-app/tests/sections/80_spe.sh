# ---------------------------------------------------------------------------
# SPE_: SplitEdge through the kernel's Brep::SplitNakedEdgeAt (spe_script.txt). The edge curve and EVERY trim are cut exactly
# at the picked point's own projection on each face (no shared normalized parameter fraction), on a naked (1 trim), a shared
# (2 trim) or a non-manifold (3+ trim) edge, straight or curved. Measured from the app's own output and compared with closed
# forms: validity (Check), exact volume and area of the box / filleted box / cylinder, the new vertex on the fillet circle at
# the clicked angle. Negative controls: a closed rim and a pick on an end vertex are refused with a reason and change nothing.
# Output checks use here-strings (no echo | grep). (The 3+ trim case is measured in the kernel test
# local_edit_tests::TestSplitEdgeNonManifoldFanSpineSplitsOnEveryPage; the app has no convenient way to build such a body.)
# ---------------------------------------------------------------------------
spe_ok() { echo "ok   $1"; }
spe_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SPE_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/spe_script.txt" 2>&1)" || { echo "$SPE_OUT" | tail -40; echo "FAIL SPE_run: script exited non-zero"; fail=1; SPE_OUT=""; }
else
  SPE_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/spe_script.txt" 2>&1)" || { echo "$SPE_OUT" | tail -40; echo "FAIL SPE_run: script exited non-zero"; fail=1; SPE_OUT=""; }
fi
if grep -q "^FAIL" <<<"$SPE_OUT"; then spe_bad "SPE_script: an @expect line of the script failed"; else spe_ok "SPE_script: the script ran without a failed @expect line"; fi
spe_case() { awk -v n="$1" '/Command: SplitEdge( |$)/ { c++ } c == n' <<<"$SPE_OUT"; }
spe_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
spe_has() { if grep -qF -- "$2" <<<"$1"; then spe_ok "$3"; else spe_bad "$3"; near "$1" "$2"; fi; }
spe_hasnot() { if grep -qF -- "$2" <<<"$1"; then spe_bad "$3"; near "$1" "$2"; else spe_ok "$3"; fi; }
# the number after "Volume = " / "Area = " in [$3, $4]
spe_num() {
  local v
  v="$({ grep -o "$2 = [0-9.e+-]*" <<<"$1" || true; } | head -1 | awk '{print $3}')"
  if spe_in "$v" "$3" "$4"; then spe_ok "$5 ($2 $v in $3..$4)"; else spe_bad "$5 ($2 '$v' not in $3..$4)"; fi
}
SPE_1="$(spe_case 1)"; SPE_2="$(spe_case 2)"; SPE_3="$(spe_case 3)"; SPE_4="$(spe_case 4)"; SPE_5="$(spe_case 5)"; SPE_6="$(spe_case 6)"
# 1: shared straight edge, off-centre pick
spe_has "$SPE_1" "SplitEdge: edge " "SPE_1: the shared box edge is split"
spe_has "$SPE_1" "kernel exact split: 2 trim(s) cut on their own faces' surfaces" "SPE_1: both trims of the shared edge are cut by the kernel"
spe_has "$SPE_1" ": valid" "SPE_1: the split box is still a valid object"
spe_num "$SPE_1" "Volume" 999.99 1000.01 "SPE_1: volume stays exactly the box's 1000"
spe_num "$SPE_1" "Area" 599.9 600.1 "SPE_1: area stays exactly the box's 600"
# 2: curved shared arc between the fillet cylinder and the top plane
spe_has "$SPE_2" "kernel exact split: 2 trim(s) cut on their own faces' surfaces" "SPE_2: both trims of the curved cylinder/plane arc are cut by the kernel"
spe_has "$SPE_2" ": valid" "SPE_2: the filleted box with a split arc is still a valid object"
spe_num "$SPE_2" "Volume" 991.40 991.43 "SPE_2: volume stays 1000 - r^2 (1 - pi/4) h = 991.4159"
spe_num "$SPE_2" "Area" 589.65 589.75 "SPE_2: area stays 600 - 40 + pi/2 r h - 2 r^2 (1 - pi/4) = 589.6991"
SPE_2V="$({ grep -o "new vertex [-0-9.e+]*,[-0-9.e+]*,[-0-9.e+]*" <<<"$SPE_2" || true; } | head -1 | sed 's/new vertex //')"
if [ -n "$SPE_2V" ] && awk -v v="$SPE_2V" 'BEGIN { split(v, a, ","); dx = a[1] - 8; dy = a[2] - 8; r = sqrt(dx * dx + dy * dy); th = atan2(dy, dx) * 180 / 3.14159265358979;
     exit !(r > 1.995 && r < 2.005 && th > 29.5 && th < 30.5 && a[3] > 9.999 && a[3] < 10.001) }'; then
  spe_ok "SPE_2_vertex: the new vertex ($SPE_2V) lies on the fillet circle r = 2 about (8,8) at 30 degrees on the top plane z = 10"
else
  spe_bad "SPE_2_vertex: new vertex '$SPE_2V' is not on the r = 2 circle at 30 degrees"
fi
# 3: naked edge
spe_has "$SPE_3" "kernel exact split: 1 trim(s) cut on their own faces' surfaces" "SPE_3: a naked (1 trim) edge is cut by the kernel"
spe_has "$SPE_3" ": valid" "SPE_3: the open box is still a valid object"
# 4: twice
SPE_4N="$(grep -c "kernel exact split: 2 trim(s)" <<<"$SPE_4" || true)"
if [ "$SPE_4N" = "2" ]; then spe_ok "SPE_4: both cuts of the fillet arc (the second on a half the first left) went through the kernel"; else spe_bad "SPE_4: expected 2 kernel splits, got $SPE_4N"; fi
spe_has "$SPE_4" ": valid" "SPE_4: still valid after two cuts"
spe_num "$SPE_4" "Volume" 991.40 991.43 "SPE_4: volume still 991.4159 after two cuts"
# 5: closed rim refused
spe_has "$SPE_5" "is closed (both ends are one vertex); a closed edge is not split - nothing changed" "SPE_5: a closed rim is refused with the reason"
spe_hasnot "$SPE_5" "kernel exact split" "SPE_5: ...and nothing was split"
spe_num "$SPE_5" "Volume" 1175 1178.5 "SPE_5: the cylinder is unchanged (volume pi r^2 h = 1178.097)"
# 6: a pick on a vertex refused
spe_has "$SPE_6" "is on an end vertex of edge" "SPE_6: a pick on an end vertex is refused with the reason"
spe_hasnot "$SPE_6" "kernel exact split" "SPE_6: ...and nothing was split"
spe_num "$SPE_6" "Volume" 999.99 1000.01 "SPE_6: the box is unchanged (volume 1000)"
