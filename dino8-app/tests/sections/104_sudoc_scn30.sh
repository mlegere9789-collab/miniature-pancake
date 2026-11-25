# ---------------------------------------------------------------------------
# SUDOC_SCN30: a scene saves the camera type and the field of view: save and restore FOV 40, the camera is equal (D8-SKETCHUP-SCENE-0030).
# The perspective viewport gets PerspectiveAngle 40 and RotateView 30, its Camera report is taken (a), the scene Fov40 is saved, the viewport is switched to the parallel Top view
# (b), Fov40 is restored and the Camera report is taken again (c). c must equal a, line for line (location, target, distance, lens, angle), and b must not. The document is then
# saved to a .3dm and read back by view3dm_check (a plain openNURBS reader, tests/view3dm_check.cpp), which computes the field of view from the FRUSTUM stored in the file and does not
# use the app's lens: 40 degrees, a 35 mm lens of 12 / tan 20 = 32.970 mm, the camera location and target of (a), a direction that is the unit vector from location to target.
# ---------------------------------------------------------------------------
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip SUDOC_SCN30 (not run on Windows: it uses the Linux headless launch)"
else
SUDOC_SCN30_BIN="$(dirname "$BIN")/view3dm_check"
if [ ! -x "$SUDOC_SCN30_BIN" ] || ! command -v python3 >/dev/null 2>&1; then
  echo "FAIL SUDOC_SCN30_tool: view3dm_check was not built next to $BIN or python3 is missing"; fail=1
else
  . "$HERE/ux_common.sh"
  SUDOC_SCN30_D="$TMPW/sudoc_scn30"; mkdir -p "$SUDOC_SCN30_D"
  cat > "$TMPW/sudoc_scn30_script.txt" <<EOS
@wait 2
SetActiveViewport Perspective
Point 0,0,0
PerspectiveAngle 40
RotateView 30
SUDOCBEGIN_a
Camera
@key Escape
NamedView Save Fov40
SetView Top
SUDOCBEGIN_b
Camera
@key Escape
NamedView Restore Fov40
SUDOCBEGIN_c
Camera
@key Escape
SaveAs $SUDOC_SCN30_D/scn30.3dm
SUDOCBEGIN_end
EOS
  if ux_launch "$TMPW/sudoc_scn30_script.txt"; then
    printf '%s\n' "$UX_OUT" > "$SUDOC_SCN30_D/app.log"
    "$SUDOC_SCN30_BIN" dump "$SUDOC_SCN30_D/scn30.3dm" > "$SUDOC_SCN30_D/dump.txt" 2>&1 || true
    SUDOC_SCN30_R="$(python3 "$HERE/sudoc_view.py" scn30 "$SUDOC_SCN30_D/app.log" "$SUDOC_SCN30_D/dump.txt" 2>&1)" && SUDOC_SCN30_RC=0 || SUDOC_SCN30_RC=$?
    echo "$SUDOC_SCN30_R"
    if [ "$SUDOC_SCN30_RC" != "0" ] || grep -q '^FAIL' <<<"$SUDOC_SCN30_R"; then fail=1; fi
    if ! grep -q '^ok ' <<<"$SUDOC_SCN30_R"; then ux_bad "SUDOC_SCN30_check: the checker printed no result"; fi
  fi
fi
fi
