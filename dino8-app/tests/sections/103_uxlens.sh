# ---------------------------------------------------------------------------
# UXLENS_: the lens convention of the app is the one of the 3dm file (openNURBS' 35 mm lens), checked against an independent openNURBS reader.
# Rows D8-RHINO-VIEW-0057 ZoomLens and D8-RHINO-VIEW-0047 PerspectiveAngle.
# openNURBS (ON_Viewport::GetCamera35mmLensLength / SetCamera35mmLensLength, source comment "35 mm film has a height of 24 mm and a width of 36 mm") measures the
# lens against the half extent of the SHORT side of the frustum, 12 mm: tan(half angle of the short side) = 12 / lens. The oracle here is therefore
#   (a) the field of view that view3dm_check (a plain openNURBS reader, tests/view3dm_check.cpp) computes from the FRUSTUM stored in the file, which does not
#       use the lens at all, and
#   (b) the closed form 2 atan(12 / lens): 28 mm gives 46.397 degrees, 35 mm gives 37.849 degrees.
# Two directions: an openNURBS-authored named view with a 28 mm lens (view3dm_check fixture) restored in the app must report the angle of that frustum; a view
# the app saves with ZoomLens 35 must have a frustum of the angle the app reports. Before the app followed openNURBS the app reported 54.4 degrees for 35 mm
# (18 mm film half-size) against 37.8 in the file. The angle printed by the Camera command is the short-side angle.
# Only the landscape (width > height) viewport is exercised; the portrait branch (short side = width) is derived the same way and not run here.
# ---------------------------------------------------------------------------
if [ "${IS_WINDOWS:-0}" = 1 ]; then
  echo "skip UXLENS (not run on Windows: it uses the Linux headless launch)"
else
UXLENS_BIN="$(dirname "$BIN")/view3dm_check"
if [ ! -x "$UXLENS_BIN" ]; then
  echo "FAIL UXLENS_tool: view3dm_check was not built next to $BIN"; fail=1
else
  . "$HERE/ux_common.sh"
  UXLENS_D="$TMPW/uxlens"; mkdir -p "$UXLENS_D"
  "$UXLENS_BIN" fixture "$UXLENS_D/fixture.3dm" >/dev/null || { ux_bad "UXLENS_tool: view3dm_check could not author the fixture"; }
  cat > "$TMPW/uxlens_script.txt" <<EOS
@wait 2
Open $UXLENS_D/fixture.3dm
SetActiveViewport Perspective
NamedView Restore RhinoPersp
UXBEGIN_fixture
Camera
@key Escape
ZoomLens 35
UXBEGIN_set35
Camera
@key Escape
NamedView Save PerspV
Point 0,0,0
SaveAs $UXLENS_D/out.3dm
UXBEGIN_end
EOS
  if ux_launch "$TMPW/uxlens_script.txt"; then
    uxlens_app() {  # block -> "lens angle" of the Camera line
      ux_lines "$1" | sed -nE 's/^Camera \(.*, lens ([0-9.eE+-]+) mm \(([0-9.eE+-]+) deg\)$/\1 \2/p' | head -1
    }
    uxlens_file() {  # file view-name -> angle computed from the frustum alone
      "$UXLENS_BIN" dump "$1" 2>&1 | grep -E "^view [0-9]+ name=$2 " | sed -nE 's/.* angle=([0-9.eE+-]+) lens=.*/\1/p' | head -1
    }
    uxlens_near() { awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(v != "" && d <= t) }'; }
    # 2 atan(12 / lens) in degrees, computed here (not by the app)
    uxlens_cf() { awk -v l="$1" 'BEGIN { printf "%.4f", 2 * atan2(12, l) * 180 / 3.14159265358979 }'; }
    read -r FA_L FA_A <<<"$(uxlens_app fixture)"
    FF_A="$(uxlens_file "$UXLENS_D/fixture.3dm" RhinoPersp)"
    CF28="$(uxlens_cf 28)"
    if uxlens_near "$FF_A" "$CF28" 0.01; then ux_ok "UXLENS_fixture: the frustum openNURBS authored for a 28 mm lens spans $FF_A degrees = 2 atan(12/28) = $CF28"; else ux_bad "UXLENS_fixture: the file's frustum angle is '${FF_A}', closed form $CF28"; fi
    if uxlens_near "$FA_L" 28 0.01; then ux_ok "UXLENS_fixture: the app reads the lens back as $FA_L mm"; else ux_bad "UXLENS_fixture: the app lens is '${FA_L}' (want 28)"; fi
    if uxlens_near "$FA_A" "$FF_A" 0.06; then ux_ok "UXLENS_fixture: the app reports $FA_A degrees for the restored view = the $FF_A degrees of the frustum in the file"; else ux_bad "UXLENS_fixture: the app reports '${FA_A}' degrees, the file's frustum is '${FF_A}'"; fi
    read -r S35_L S35_A <<<"$(uxlens_app set35)"
    SF_A="$(uxlens_file "$UXLENS_D/out.3dm" PerspV)"
    CF35="$(uxlens_cf 35)"
    if uxlens_near "$S35_L" 35 0.01; then ux_ok "UXLENS_ZoomLens: ZoomLens 35 reports lens $S35_L mm"; else ux_bad "UXLENS_ZoomLens: the app lens is '${S35_L}' (want 35)"; fi
    if uxlens_near "$S35_A" "$CF35" 0.06; then ux_ok "UXLENS_ZoomLens: 35 mm is reported as $S35_A degrees = 2 atan(12/35) = $CF35"; else ux_bad "UXLENS_ZoomLens: 35 mm reported as '${S35_A}' degrees, closed form $CF35"; fi
    if uxlens_near "$SF_A" "$S35_A" 0.06; then ux_ok "UXLENS_write: the frustum the app wrote spans $SF_A degrees = the $S35_A degrees the app reports"; else ux_bad "UXLENS_write: the saved frustum spans '${SF_A}' degrees, the app reports '${S35_A}'"; fi
    if uxlens_near "$SF_A" "$CF35" 0.01; then ux_ok "UXLENS_write: ...and that is the closed form $CF35"; else ux_bad "UXLENS_write: the saved frustum angle '${SF_A}' is not the closed form $CF35"; fi
  fi
fi
fi
