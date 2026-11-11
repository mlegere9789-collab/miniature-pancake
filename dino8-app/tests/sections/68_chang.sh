# ---------------------------------------------------------------------------
# CHANG_: a ChamferEdge Distance/Angle chamfer (Radius= distance1, Angle= degrees from face 1) records construction history
# (commands/cmd_fillet.cpp: CommitEdgeBlend records the angle, RebuildEdgeBlend replays it through
# kernel::ChamferConvexEdgeAngle / ChamferConcaveEdgeAngle; commands/cmd_history.cpp: EditHistory angle=). Before this the note
# "not recorded in history (this chamfer mode (distance and angle) is not one the rebuild reproduces)" was printed and the
# result replaced the solid in place.
# One real app process runs $TMPW/chang_script.txt, written below.
#
# Oracles (closed-form, none read from the app). A chamfer of a right-angle edge of length L = 10 with setbacks d_i (in face 1)
# and d_j removes a right triangle prism of volume d_i * d_j * L / 2, and for an angle `a` from face 1 the law of sines in the
# cross-section gives d_j = d_i * sin(a) / sin(theta + a) with theta = 90 degrees:
#    d = 4, a = 30:  d_j = 4 * 0.5 / sin(120) = 2.309401      removed 4 * 2.309401 * 10 / 2 = 46.188  -> Volume 953.812
#    d = 3, a = 30:  d_j = 3 * 0.5 / sin(120) = 1.732051      removed 25.981                          -> Volume 974.019
#    d = 3, a = 45:  d_j = 3 * sin(45) / sin(135) = 3         removed 3 * 3 * 10 / 2 = 45             -> Volume 955.000
#  (the app prints 4 significant digits, so the checks allow 0.08)
#  * With History On the source solid (id 1) is kept hidden and the result is a NEW object (id 2) whose bounding box is the
#    box's (0,0,0)-(10,10,10); moving the SOURCE by (50,0,0) rebuilds it at (50,0,0)-(60,10,10) with the same volume.
#  * EditHistory 2 distance=3 re-runs the recorded angle chamfer with the new distance (974.019); EditHistory 2 angle=45 turns it
#    into the symmetric chamfer (955).
#  * Negative control: EditHistory 2 distance2=1 is refused (an angle chamfer has no second distance to set) and the volume
#    stays 955.
chang_ok() { echo "ok   $1"; }
chang_bad() { echo "FAIL $1"; fail=1; }
chang_exec() {  # chang_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 100 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 100 --script "$1" 2>&1
  fi
}
chang_has() {  # chang_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then chang_ok "$1"; else chang_bad "$1 (missing '$3')"; fi
}
chang_vol() {  # chang_vol <what> <got> <want>
  if awk -v v="$2" -v w="$3" 'BEGIN { d = v - w; if (d < 0) d = -d; exit !(v != "" && d <= 0.08) }'; then chang_ok "$1: Volume $2 (expected $3)"; else chang_bad "$1: Volume '${2:-<none>}', expected $3 +- 0.08"; fi
}

cat > "$TMPW/chang_script.txt" <<'EOS'
SetActiveViewport Top

History On
Box 0,0,0 10,10,0 10
ChamferEdge
Radius=4
Angle=30
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
EditHistory 2 distance=3
SelNone
SelID 2
Volume
EditHistory 2 angle=45
SelNone
SelID 2
Volume
EditHistory 2 distance2=1
SelNone
SelID 2
Volume
EOS
CHANG_OUT="$(chang_exec "$TMPW/chang_script.txt" 2>&1 || true)"
mapfile -t CHANG_VOL < <(grep -oE '^history: Volume = [-0-9.e+]+' <<<"$CHANG_OUT" | sed 's/^history: Volume = //')
mapfile -t CHANG_BOX < <(grep -E '^history: Bounding box min' <<<"$CHANG_OUT" | sed 's/^history: //')

chang_has "CHANG: the angle chamfer was applied as an exact chamfer" "$CHANG_OUT" "replaced with an exact chamfer (distance1 4, angle 30 degrees from face 1)"
chang_has "CHANG: with History On the source is kept hidden and the result is a new object" "$CHANG_OUT" "History is On: the original solid 1 is kept hidden as the source and the result is object 2"
if [ "${#CHANG_VOL[@]}" = 5 ] && [ "${#CHANG_BOX[@]}" = 2 ]; then chang_ok "CHANG: 5 Volume and 2 Bounding box readings"; else chang_bad "CHANG: expected 5 Volume / 2 Bounding box readings, got ${#CHANG_VOL[@]} / ${#CHANG_BOX[@]}"; fi
chang_vol "CHANG built d=4 a=30 (removed 46.188)" "${CHANG_VOL[0]:-}" 953.812
chang_has "CHANG built: the result's bounding box equals the box" "${CHANG_BOX[0]:-}" "Bounding box min 0,0,0 max 10,10,10"
chang_vol "CHANG after Move (50,0,0) of the source" "${CHANG_VOL[1]:-}" 953.812
chang_has "CHANG after Move: the result was rebuilt from the moved source" "${CHANG_BOX[1]:-}" "Bounding box min 50,0,0 max 60,10,10"
chang_vol "CHANG EditHistory distance=3 (angle 30 replayed: removed 25.981)" "${CHANG_VOL[2]:-}" 974.019
chang_vol "CHANG EditHistory angle=45 (symmetric chamfer, removed 45)" "${CHANG_VOL[3]:-}" 955
chang_has "CHANG: distance2 on an angle chamfer is refused" "$CHANG_OUT" "defined by an angle"
chang_vol "CHANG after the refused distance2: unchanged" "${CHANG_VOL[4]:-}" 955
