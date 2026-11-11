# ---------------------------------------------------------------------------
# DCN_: DraftAngleAnalysis median draft angle of cone and cylinder walls (draft_cone_script.txt).
# The DraftAngleAnalysis report ends "measured draft angle LO .. HI deg, median M deg" (M = median of the per-vertex draft angle, 90 - the
# angle between the display normal and the pull direction, of the first analysed object). Closed forms, worked by hand and never read from
# the app: a cone of base radius R and height H has a wall tilted atan(R/H) from its axis, so with the pull along the axis the wall's draft is
# +-atan(R/H) (sign = which way the apex points, so the check takes |M| and checks the sign flips with the pull):
#   cone R10 H10 : 45.000 deg      cone R5 H10 : 26.565 deg      cone R10 H5 : 63.435 deg      cylinder wall : 0
# The median of the wall's display vertices was measured offline with the app's own mesher (BrepMeshOptions chord 0.05, the display
# tolerance) on these exact cones: 45.020 / 26.569 / 63.443 (0.02 deg high: averaged vertex normals), cylinder 0.000; hence +-0.3 deg.
# The reports come back in script order; each line names its own pull Direction, which the check also verifies.
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DCNO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/draft_cone_script.txt" 2>&1)" || { echo "$DCNO"; echo "FAIL: draft-cone script exited non-zero"; exit 1; }
else
  DCNO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/draft_cone_script.txt" 2>&1)" || { echo "$DCNO"; echo "FAIL: draft-cone script exited non-zero"; exit 1; }
fi
echo "$DCNO" | grep -q "^smoke:" || { echo "$DCNO"; echo "FAIL: draft-cone script produced no smoke line"; fail=1; }
DCN_LINES="$(grep -E "DraftAngleAnalysis on 1 object\(s\)" <<<"$DCNO" | grep -v "Command:" || true)"
dcn_median() { sed -nE 's/.*measured draft angle .* deg, median (-?[0-9.]+) deg.*/\1/p' <<<"$(sed -n "${1}p" <<<"$DCN_LINES")"; }
# dcn_check N DIRECTION EXPECTED_ABS DESC: report N pulls along DIRECTION and its median draft has |M| within 0.3 of EXPECTED_ABS
dcn_check() {
  local line val
  line="$(sed -n "${1}p" <<<"$DCN_LINES")"
  val="$(dcn_median "$1")"
  if [ -n "$val" ] && grep -q "Direction=$2 " <<<"$line" && awk -v v="$val" -v e="$3" 'BEGIN { a = v < 0 ? -v : v; d = a - e; exit !(d >= -0.3 && d <= 0.3) }'; then
    echo "ok   $4 (median $val)"
  else
    echo "FAIL $4"; echo "     expected Direction=$2 and |median| = $3 +- 0.3"; echo "     actual:   ${line:-<no report #$1>}"; fail=1
  fi
}
dcn_check 1 0,0,1 45 "DCN_cone_45: a cone of R=10, H=10 has a wall draft of atan(10/10) = 45 deg"
dcn_check 2 0,0,-1 45 "DCN_cone_45_rev: pulled the opposite way the same cone still reads 45 deg in magnitude"
m1="$(dcn_median 1)"; m2="$(dcn_median 2)"
if [ -n "$m1" ] && [ -n "$m2" ] && awk -v a="$m1" -v b="$m2" 'BEGIN { d = a + b; exit !(d >= -0.2 && d <= 0.2 && (a > 1 || a < -1)) }'; then
  echo "ok   DCN_flip: reversing the pull direction negates the wall's draft ($m1 then $m2)"
else
  echo "FAIL DCN_flip: reversing the pull direction negates the wall's draft (got '$m1' then '$m2')"; fail=1
fi
dcn_check 3 0,0,1 26.565 "DCN_cone_26: a cone of R=5, H=10 has a wall draft of atan(5/10) = 26.565 deg"
dcn_check 4 0,0,1 63.435 "DCN_cone_63: a cone of R=10, H=5 has a wall draft of atan(10/5) = 63.435 deg"
dcn_check 5 0,0,1 0 "DCN_cyl_0: a cylinder wall is parallel to the pull, draft 0 deg"
