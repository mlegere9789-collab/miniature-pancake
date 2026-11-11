# ---------------------------------------------------------------------------
# DAM_: DraftAngleAnalysis prints the MEASURED draft-angle range, not only a colour ramp (draft_angle_measure_script.txt).
# The command used to say only "DraftAngleAnalysis on 1 object(s)  Direction=.. MinAngle=-90 MaxAngle=90", so nothing a script could read
# showed whether the per-vertex draft angle (90 - angle(normal, pull)) was right. The line now ends "measured draft angle LO .. HI deg"
# (min / max over the display vertices of every analysed object, one decimal). A box has six flat faces with normals +-X, +-Y, +-Z, so for a
# pull p the draft of each face is asin(n . p_unit) and the range is -asin(m) .. asin(m), m = the largest |n . p_unit|. Expected values are
# worked by hand (python math.degrees(math.asin(..)) for the two irrational ones), never read from the app:
#   Direction=0,1,2 : m = 2/sqrt5  -> asin = 63.4349 deg -> -63.4 .. 63.4   (Z faces; the Y faces give 26.6)
#   Direction=3,4,0 : m = 4/5      -> asin = 53.1301 deg -> -53.1 .. 53.1   (Y faces; the X faces give 36.9)
#   Direction=World : m = 1        -> asin = 90 deg      -> -90.0 .. 90.0   (Z faces face along / against the pull)
# The Direction text printed on the same line ties each reading to its own case.
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  DAMO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/draft_angle_measure_script.txt" 2>&1)" || { echo "$DAMO"; echo "FAIL: draft-angle-measure script exited non-zero"; exit 1; }
else
  DAMO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/draft_angle_measure_script.txt" 2>&1)" || { echo "$DAMO"; echo "FAIL: draft-angle-measure script exited non-zero"; exit 1; }
fi
echo "$DAMO" | grep -q "^smoke:" || { echo "$DAMO"; echo "FAIL: draft-angle-measure script produced no smoke line"; fail=1; }
dam_check() { if grep -qE -- "$1" <<<"$DAMO"; then echo "ok   $2"; else echo "FAIL $2"; echo "     expected: $1"; grep -F "DraftAngleAnalysis on" <<<"$DAMO" | head -5 | sed 's/^/     actual:   /' || true; fail=1; fi; }
dam_check "DraftAngleAnalysis on 1 object\(s\)  Direction=0,1,2 .*measured draft angle -63\.4 \.\. 63\.4 deg" "DAM_yz: a box pulled along (0,1,2) measures -63.4 .. 63.4 degrees (asin(2/sqrt5) = 63.4349, the Z faces)"
dam_check "DraftAngleAnalysis on 1 object\(s\)  Direction=3,4,0 .*measured draft angle -53\.1 \.\. 53\.1 deg" "DAM_xy: a box pulled along (3,4,0) measures -53.1 .. 53.1 degrees (asin(4/5) = 53.1301, the Y faces); a different value, so the readout is not a constant"
dam_check "DraftAngleAnalysis on 1 object\(s\)  Direction=0,0,1 .*measured draft angle -90\.0 \.\. 90\.0 deg" "DAM_z: a box pulled along World Z measures -90.0 .. 90.0 degrees (the top and bottom faces face along / against the pull)"
