# ---------------------------------------------------------------------------
# UXCAM_: camera and viewport commands checked against closed-form camera geometry, read from the one-line "Camera" report.
# Rows D8-RHINO-VIEW-0057 ZoomLens, 0047 PerspectiveAngle, 0011 DollyZoom, 0075 RotateCamera, 0076 RotateView, 0096 SetView, 0027 Isometric,
# 0049 Plan, 0030 MaxViewport.
# Oracles (tests/ux_camera_check.py): lens and field of view on the 36 mm film width, fov = 2 atan(18 / lens) (50 mm gives 39.6 degrees, 70 mm 28.8,
# 24 mm 73.7); PerspectiveAngle 40 needs lens 18 / tan 20 = 49.4546 mm; a dolly zoom keeps the subject's size so distance / lens is constant; a
# rotation about the target (RotateView) or about the camera (RotateCamera) keeps the other end and the distance; Top / Front / Right look from
# +Z / -Y / +X (the right-handed Z-up convention; recalled for Rhino, needs owner check) and the isometric direction has |dx| = |dy| = |dz|;
# MaxViewport twice returns to the starting state. The numbers are printed with 4 significant digits, so each comparison allows that rounding.
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
  echo "UXBEGIN_lens"; cam; echo "ZoomLens 70"; cam; echo "ZoomLens 24"; cam
  echo "UXBEGIN_angle"; echo "PerspectiveAngle 40"; cam
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
