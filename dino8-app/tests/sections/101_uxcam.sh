# ---------------------------------------------------------------------------
# UXCAM_: camera and viewport commands checked against closed-form camera geometry, read from the one-line "Camera" report.
# Rows D8-RHINO-VIEW-0057 ZoomLens, 0047 PerspectiveAngle, 0011 DollyZoom, 0075 RotateCamera, 0076 RotateView, 0096 SetView, 0027 Isometric,
# 0049 Plan, 0030 MaxViewport.
# Oracles (tests/ux_camera_check.py), none of which repeats the formula or the film size of the code under test: for a pinhole camera tan(half field of
# view) = film half-size / lens, so doubling the lens (24, 48, 96) halves the tangent whatever the film is, and lens * tan(half angle) is one constant; a typed
# PerspectiveAngle is reported back as typed and the lenses it sets for 60, 90, 120 degrees have ratios tan 45 / tan 30 = tan 60 / tan 45 = sqrt 3; a dolly zoom
# keeps the subject's size so distance / lens is constant; a rotation about the target (RotateView) or about the camera (RotateCamera) by 30 degrees turns the
# azimuth by exactly 30 and keeps height, radius and the fixed end; Top / Front / Right look from +Z / -Y / +X (right-handed Z-up convention; recalled for
# Rhino, needs owner check) and the isometric direction has |dx| = |dy| = |dz|; MaxViewport twice returns to the starting state.
# FILM SIZE: the app's lens is the 36 mm-film convention with 18 mm as the vertical half-size (50 mm = 39.6 degrees vertical); openNURBS' 35 mm lens
# (GetCamera35mmLensLength) puts 12 mm on the SHORT side of the frustum (50 mm = 27 degrees there), a factor 1.5 apart for a landscape view. Making the renderer
# follow openNURBS would change PerspectiveMatch (it reconstructs lens = 18 x focal and its pinned check expects 50 mm) and every perspective framing, so
# this section deliberately does not assert the film size; it is reported in the check labels. See the notes of D8-RHINO-VIEW-0057 and 0047.
# The numbers are printed with 4 significant digits, so each comparison allows that rounding.
# Not tested (needs a window): what the viewport looks like, mouse navigation.
# ---------------------------------------------------------------------------
. "$HERE/ux_common.sh"
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXCAM (not run on Windows: it needs python3 and the Linux headless launch)"
else
UXCAM_S="$TMPW/uxcam_script.txt"
{
  echo "@wait 2"; echo "SetActiveViewport Perspective"
  cam() { echo "Camera"; echo "@key Escape"; }
  echo "UXBEGIN_lens"; echo "ZoomLens 24"; cam; echo "ZoomLens 48"; cam; echo "ZoomLens 96"; cam
  echo "UXBEGIN_angle"; echo "PerspectiveAngle 60"; cam; echo "PerspectiveAngle 90"; cam; echo "PerspectiveAngle 120"; cam
  echo "UXBEGIN_dolly"; echo "ZoomLens 50"; cam; echo "DollyZoom 100"; cam
  echo "UXBEGIN_rotview"; cam; echo "RotateView 30"; cam
  echo "UXBEGIN_rotcam"; cam; echo "RotateCamera 30"; cam
  echo "UXBEGIN_views"; echo "SetView Top"; cam; echo "SetView Front"; cam; echo "SetView Right"; cam
  echo "UXBEGIN_iso"; echo "Isometric"; cam
  echo "UXBEGIN_plan"; echo "Plan"; cam
  echo "UXBEGIN_max"; echo "SetActiveViewport Front"; echo "MaxViewport"; echo "MaxViewport"; echo "MaxViewport"
  echo "UXBEGIN_end"
} > "$UXCAM_S"
if ux_launch "$UXCAM_S"; then
  UXCAM_R="$(python3 "$HERE/ux_camera_check.py" <<<"$UX_OUT" || true)"
  echo "$UXCAM_R"
  if grep -q '^FAIL' <<<"$UXCAM_R"; then fail=1; fi
  if ! grep -q '^ok ' <<<"$UXCAM_R"; then ux_bad "UXCAM_check: the camera checker printed no result"; fi
  ux_eq UXCAM_MaxViewport max "MaxViewport maximizes, restores and maximizes again (three calls)" "Active viewport: Front" "Front maximized" "Front restored" "Front maximized"
fi
fi
