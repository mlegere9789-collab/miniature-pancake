# ---------------------------------------------------------------------------
# LAWH_: a FilletEdge Law= fillet records construction history (commands/cmd_fillet.cpp: RunLaw -> CommitEdgeBlend, replayed by
# RebuildEdgeBlend). Before this a radius-law fillet bypassed the history path: with History On it silently replaced the solid
# in place and nothing followed edits of the source.
# One real app process runs $TMPW/lawh_script.txt, written below.
#
# Oracles (closed-form, none read from the app):
#  * 10 x 10 x 10 box, Law=smooth from radius 1 to 3 on its vertical corner edge: the removed volume of this law is the
#    independent union-of-balls integral 9.4820 of the LAW section, so the result reads Volume 1000 - 9.482 = 990.518 (the app
#    prints 4 significant digits: 990.5; the check allows 0.08).
#  * With History On the source solid (id 1) is kept hidden and the result is a NEW object (id 2) with the same bounding box
#    0,0,0 .. 10,10,10: the fillet removes material inside the box, so the extent does not change.
#  * Move the SOURCE by (50,0,0): the history rebuilds the result from the moved source, so its bounding box is exactly
#    (50,0,0)-(60,10,10) and its Volume is unchanged (990.5). A static result would stay at 0,0,0 .. 10,10,10.
#  * Negative control: a refused law (non-monotone 1 -> 3 -> 1.5) with History On creates no result object and leaves the
#    source visible and unfilleted (SelAll Volume exactly 1000, nothing but the box).
lawh_ok() { echo "ok   $1"; }
lawh_bad() { echo "FAIL $1"; fail=1; }
lawh_exec() {  # lawh_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1
  fi
}
lawh_has() {  # lawh_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then lawh_ok "$1"; else lawh_bad "$1 (missing '$3')"; fi
}

cat > "$TMPW/lawh_script.txt" <<'EOS'
SetActiveViewport Top

History On
Box 0,0,0 10,10,0 10
FilletEdge
Radii=0:1,1:3
Law=smooth
LawTol=0.003
10,10,5
Enter
SelNone
SelID 2
Volume
SelNone
SelID 2
BoundingBox
Show
SelNone
SelID 1
Move
0,0,0
50,0,0
SelNone
SelID 2
Volume
SelNone
SelID 2
BoundingBox
SelNone
SelAll
Delete
Box 0,0,0 10,10,0 10
FilletEdge
Law=0:1,0.5:3,1:1.5
10,10,5
Enter
SelAll
Volume
EOS
LAWH_OUT="$(lawh_exec "$TMPW/lawh_script.txt" 2>&1 || true)"
mapfile -t LAWH_VOL < <(grep -oE '^history: Volume = [-0-9.e+]+' <<<"$LAWH_OUT" | sed 's/^history: Volume = //')
mapfile -t LAWH_BOX < <(grep -E '^history: Bounding box min' <<<"$LAWH_OUT" | sed 's/^history: //')

lawh_has "LAWH: the law fillet was applied as an exact convex non-linear-law fillet" "$LAWH_OUT" "replaced with an exact convex non-linear-law fillet"
lawh_has "LAWH: with History On the source is kept hidden and the result is a new object" "$LAWH_OUT" "History is On: the original solid 1 is kept hidden as the source and the result is object 2"
if [ "${#LAWH_VOL[@]}" = 3 ] && [ "${#LAWH_BOX[@]}" = 2 ]; then lawh_ok "LAWH: 3 Volume and 2 Bounding box readings"; else lawh_bad "LAWH: expected 3 Volume / 2 Bounding box readings, got ${#LAWH_VOL[@]} / ${#LAWH_BOX[@]}"; fi
for i in 0 1; do
  if awk -v v="${LAWH_VOL[$i]:-0}" 'BEGIN { d = v - 990.518; if (d < 0) d = -d; exit !(v > 0 && d <= 0.08) }'; then
    lawh_ok "LAWH result ${i}: Volume ${LAWH_VOL[$i]} matches 1000 - 9.482 = 990.518 (smooth law 1 -> 3, union-of-balls integral)"
  else
    lawh_bad "LAWH result ${i}: Volume ${LAWH_VOL[$i]:-<none>}, expected 990.518 +- 0.08"
  fi
done
lawh_has "LAWH built: the result's bounding box equals the box (the fillet only removes material)" "${LAWH_BOX[0]:-}" "Bounding box min 0,0,0 max 10,10,10"
lawh_has "LAWH after Move (50,0,0) of the source: the result was rebuilt from the moved source" "${LAWH_BOX[1]:-}" "Bounding box min 50,0,0 max 60,10,10"
if [ "${LAWH_VOL[2]:-}" = 1000 ]; then lawh_ok "LAWH refused law with History On: nothing but the untouched 1000 box remains"; else lawh_bad "LAWH refused law: SelAll Volume '${LAWH_VOL[2]:-<none>}', expected exactly 1000"; fi
lawh_has "LAWH refused law: the kernel's reason is printed" "$LAWH_OUT" "not strictly monotone"
