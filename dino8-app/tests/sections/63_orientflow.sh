# OFS_: OrientOnSrf / Splop / FlowAlongSrf on curved surfaces, end to end through the commands (orientflow_script.txt, blocks orient / splop / flow). The maths
# is the kernel's (pattern.h) and dino8-kernel/tests/extra/orient_flow_on_surface.cpp pins it independently; the closed forms here:
#   sphere r 5, vertical line (0,0,0)-(0,0,4) oriented / splopped onto P = (1.915111, 1.606969, 4.330127) (30 degrees from the pole, 40 degrees
#   round): the base lands on P and the line runs along the radial normal, so it ends at 9 / 5 P = (3.447200, 2.892544, 7.794229) (distance r + h = 9,
#   its middle at r + h / 2 = 7);
#   plane patch 4 x 10 pi flowed onto the cylinder r 5 height 4: the line over [0.07 L, 0.19 L] of the patch (a non-knot interval of the rational
#   circle) lands on an arc of length 5 * 0.12 * 2 pi = 3.769911 (the parameter-fraction map this replaced gave 3.923).
# Values print with 4 significant digits, so the comparison window for points is 2e-3; the length uses Length digits=6 (window 5e-4).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  OFS_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/orientflow_script.txt" 2>&1)" || { echo "$OFS_OUT" | tail -40; echo "FAIL OFS_run: script exited non-zero"; fail=1; OFS_OUT=""; }
else
  OFS_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/orientflow_script.txt" 2>&1)" || { echo "$OFS_OUT" | tail -40; echo "FAIL OFS_run: script exited non-zero"; fail=1; OFS_OUT=""; }
fi
ofs_ok() { echo "ok   $1"; }
ofs_bad() { echo "FAIL $1"; fail=1; }
ofs_blk() { awk -v name="$1" '$0 ~ ("Unknown command: OFSBEGIN_" name "$") { on = 1; next } on && /Unknown command: OFSBEGIN_/ { exit } on { print }' <<<"$OFS_OUT"; }
ofs_has() { if grep -qF -- "$2" <<<"$1"; then ofs_ok "$3"; else ofs_bad "$3"; near "$1" "$2"; fi; }
# ofs_chk BLOCK "x0,y0,z0 x1,y1,z1" MESSAGE: the first listed curve has its two control points within 2e-3 of the expected points
ofs_chk() {
  local got
  got="$({ grep -o 'CV\[[01]\] [-0-9.e+]*,[-0-9.e+]*,[-0-9.e+]*' <<<"$1" || true; } | sed -n "1,2p" | sed 's/CV\[[01]\] //' | tr '\n' ' ')"
  if [ -n "$got" ] && awk -v g="$got" -v w="$2" 'BEGIN { n = split(g, a, /[ ,]+/); m = split(w, b, /[ ,]+/); if (n < 6 || m < 6) exit 1; for (i = 1; i <= 6; i++) { d = a[i] - b[i]; if (d < 0) d = -d; if (d > 2e-3) exit 1 } exit 0 }'; then ofs_ok "$3 (measured $got)"; else ofs_bad "$3 (measured '$got', expected $2)"; fi
}
OFS_ORIENT="$(ofs_blk orient)"; OFS_SPLOP="$(ofs_blk splop)"; OFS_FLOW="$(ofs_blk flow)"
ofs_has "$OFS_ORIENT" "OrientOnSrf: placed 1 copy(ies) at 1 point(s)" "OFS_1: OrientOnSrf placed the line at the picked sphere point"
ofs_chk "$OFS_ORIENT" "1.915111,1.606969,4.330127 3.447200,2.892544,7.794229" "OFS_1: the base is on P and the line runs along the radial normal to 1.8 P (distance r + h = 9)"
ofs_has "$OFS_SPLOP" "Splop: placed 1 copy(ies) at 1 point(s)" "OFS_2: Splop placed a copy at the picked sphere point"
ofs_chk "$OFS_SPLOP" "1.915111,1.606969,4.330127 3.447200,2.892544,7.794229" "OFS_2: the Splop copy has the same two endpoints"
ofs_has "$OFS_FLOW" "FlowAlongSrf: deformed 1 object(s)" "OFS_3: FlowAlongSrf deformed the line"
OFS_LEN="$({ grep -o "Total length = [0-9.e+-]*" <<<"$OFS_FLOW" || true; } | tail -1 | awk '{print $4}')"
if awk -v v="${OFS_LEN:-}" 'BEGIN { exit !(v != "" && v + 0 >= 3.7695 && v + 0 <= 3.7703) }'; then ofs_ok "OFS_3: the flowed line has length 5 * 0.12 * 2 pi = 3.769911 over a non-knot interval of the rational circle (measured $OFS_LEN)"; else ofs_bad "OFS_3: the flowed line has length '$OFS_LEN', expected 3.769911 (3.7695..3.7703; a parameter-fraction map gives 3.923)"; fi
