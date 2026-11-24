# SILH_: Silhouette of a sphere is ONE closed curve in every view and at every distance from the origin, and a box seen along an axis gives its
# outline (one curve, length 4 x 10).
#
# The command takes the silhouette of the display mesh (an edge between a face turned toward the view and one turned away). A sphere's own outline is
# one closed curve (the great circle perpendicular to the view); the old face-by-face sign test gave 1 to 25 curves for the same sphere depending on
# the view and on the mesh, which differs between platforms down to a vertex (a sphere at 400,0,0 gave 3 curves on Windows and 1 on Linux, which
# shifted every object id the surface-edit script picks by number and cost it 28 objects). Oracle: a smooth closed convex surface has exactly one
# outline curve; the measured curve is a polyline along mesh edges, so its length lies between the great circle's 2 pi r and 1.6 times that.
SILH_DIR="$TMPW/silh"
mkdir -p "$SILH_DIR"
silh_run() {  # silh_run <script> <out>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 600 "$BIN" --smoke 60 --script "$1" > "$2" 2>&1 || true
  else
    timeout 600 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$1" > "$2" 2>&1 || true
  fi
}
{
  for SILH_X in 0 400 4500; do
    for SILH_V in Default Top Front Right Perspective; do
      printf 'New\nSILHCASE_%s_%s\nSphere %s,0,0 5\n' "$SILH_X" "$SILH_V" "$SILH_X"
      [ "$SILH_V" = Default ] || printf 'SetActiveViewport %s\n' "$SILH_V"
      printf 'SelPolysrf\nSilhouette\nSelCrv\nLength\n'
    done
  done
  printf 'New\nSILHCASE_box\nSetActiveViewport Top\nBox 0,0,0 10,10,0 10\nSelPolysrf\nSilhouette\nSelCrv\nLength\n'
} > "$SILH_DIR/s.txt"
silh_run "$SILH_DIR/s.txt" "$SILH_DIR/s.out"
silh_bad=0
silh_n=0
for SILH_X in 0 400 4500; do
  for SILH_V in Default Top Front Right Perspective; do
    SILH_BLOCK="$(awk -v tag="Command: SILHCASE_${SILH_X}_${SILH_V}\$" '/^history: Command: SILHCASE_/{on=($0 ~ tag)} on' "$SILH_DIR/s.out" | sed 's/\r$//')"
    SILH_COUNT="$(sed -n 's/^history: Silhouette: \([0-9]*\) curve(s).*/\1/p' <<<"$SILH_BLOCK" | head -1)"
    SILH_LEN="$(sed -n 's/^history: Total length = \([0-9.e+-]*\) .*/\1/p' <<<"$SILH_BLOCK" | head -1)"
    silh_n=$((silh_n + 1))
    if [ "$SILH_COUNT" = 1 ] && awk -v l="${SILH_LEN:-0}" 'BEGIN { exit !(l >= 31.4159 * 0.99 && l <= 31.4159 * 1.6) }'; then
      echo "ok   SILH_sphere_${SILH_X}_${SILH_V}: the sphere's outline is 1 closed curve of length $SILH_LEN (great circle 31.416)"
    else
      echo "FAIL SILH_sphere_${SILH_X}_${SILH_V}: expected 1 curve of length 31.1..50.3, got count '${SILH_COUNT:-none}' length '${SILH_LEN:-none}'"; silh_bad=$((silh_bad + 1)); fail=1
    fi
  done
done
SILH_BLOCK="$(awk '/^history: Command: SILHCASE_/{on=($0 ~ /SILHCASE_box$/)} on' "$SILH_DIR/s.out" | sed 's/\r$//')"
SILH_COUNT="$(sed -n 's/^history: Silhouette: \([0-9]*\) curve(s).*/\1/p' <<<"$SILH_BLOCK" | head -1)"
SILH_LEN="$(sed -n 's/^history: Total length = \([0-9.e+-]*\) .*/\1/p' <<<"$SILH_BLOCK" | head -1)"
if [ "$SILH_COUNT" = 1 ] && awk -v l="${SILH_LEN:-0}" 'BEGIN { exit !(l > 39.99 && l < 40.01) }'; then
  echo "ok   SILH_box_top: a box seen along its axis has one outline curve of length $SILH_LEN (4 x 10)"
else
  echo "FAIL SILH_box_top: expected 1 curve of length 40, got count '${SILH_COUNT:-none}' length '${SILH_LEN:-none}'"; fail=1
fi
