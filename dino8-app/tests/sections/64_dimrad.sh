# ---------------------------------------------------------------------------
# DIMRAD_: Save3dm writes DimRadius / DimDiameter as a real ON_DimRadial and DimAngle as a real ON_DimAngular (File3dm.cpp
# BuildOnDimRadialForGroup / BuildOnDimAngularForGroup). Before this they were saved as baked extension/dimension-line/arrow/
# glyph curves plus private user-text tags, so a plain OpenNURBS reader (Rhino) saw loose curves, not a dimension (PARITY_MAP
# "Native .3dm read/write": "Dim* ... still written exactly as before"; DimLinear/DimAligned were closed by the DIMWR section).
#
# Two real app processes (export; fresh-process reopen) and the independent checker dimradang3dm_write_check, which parses
# the saved file with OpenNURBS alone.
#
# Oracles (closed-form, none read from the app):
#  * Circle centre (0,0,0) radius 5, DimRadius picked at (10,0,0): radial dimension, centre (0,0,0), point on the arc (5,0,0)
#    so radius = 5, dimension-line point (10,0,0) (the pick).
#  * Circle centre (50,0,0) radius 4, DimDiameter picked at (60,0,0): a Diameter dimension, centre (50,0,0), radius 4, the
#    dimension-line point (60,0,0).
#  * DimAngle vertex (100,0,0), legs to (110,0,0) and (105,8.660254037844386,0): both legs 10 long and the chord between the
#    leg ends 10 long (equilateral triangle), so the angle is exactly 60 degrees.
#  * Negative control: exactly 2 radial + 1 angular dimension and exactly 2 loose curves (the two circles themselves) in 5
#    geometry objects - a dimension written for real must not also leave its baked curves in the file.
#  * Reopen in a fresh process: the dimensions come back with the exact measured values (DimRadiusVal, DimIsDiameter, DimP1).
dimrad_ok() { echo "ok   $1"; }
dimrad_bad() { echo "FAIL $1"; fail=1; }
dimrad_exec() {  # dimrad_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 60 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$1" 2>&1
  fi
}
dimrad_has() {  # dimrad_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then dimrad_ok "$1"; else dimrad_bad "$1 (missing '$3')"; fi
}

cat > "$TMPW/dimrad_export.txt" <<EOS
Circle 0,0,0 5
SelLast
DimRadius
10,0,0
SelNone
Circle 50,0,0 4
SelLast
DimDiameter
60,0,0
SelNone
DimAngle
100,0,0
110,0,0
105,8.660254037844386,0
SaveAs $TMPW/dimrad.3dm
EOS
cat > "$TMPW/dimrad_reopen.txt" <<EOS
Open $TMPW/dimrad.3dm
SelAll
GetUserText
EOS
DIMRAD_E="$(dimrad_exec "$TMPW/dimrad_export.txt" || true)"
dimrad_has "DIMRAD: the export process saved the file" "$DIMRAD_E" "Saved $TMPW/dimrad.3dm"

DIMRAD_CHECKER="$(dirname "$BIN")/dimradang3dm_write_check"
if [ -x "$DIMRAD_CHECKER" ]; then
  DIMRAD_C="$("$DIMRAD_CHECKER" "$TMPW/dimrad.3dm" 2>&1 || true)"
  dimrad_has "DIMRAD: an independent OpenNURBS reader finds 2 real ON_DimRadial, 1 real ON_DimAngular and only the 2 circles as loose curves" "$DIMRAD_C" "2 real ON_DimRadial, 1 real ON_DimAngular, 2 loose curve(s), 5 geometry object(s)"
  dimrad_has "DIMRAD radius: type, centre, point on the arc and radius 5" "$DIMRAD_C" "type=Radius center=(0,0,0) radiuspt=(5,0,0)"
  dimrad_has "DIMRAD radius: the dimension-line point is the pick (10,0,0)" "$DIMRAD_C" "dimline=(10,0,0) radius=5"
  dimrad_has "DIMRAD diameter: type, centre and radius 4" "$DIMRAD_C" "type=Diameter center=(50,0,0)"
  dimrad_has "DIMRAD diameter: the radius is 4 and the dimension-line point the pick (60,0,0)" "$DIMRAD_C" "dimline=(60,0,0) radius=4"
  dimrad_has "DIMRAD angle: vertex and both legs" "$DIMRAD_C" "vertex=(100,0,0) leg1=(110,0,0) leg2=(105,8.66025,0)"
  dimrad_has "DIMRAD angle: the equilateral construction measures exactly 60 degrees" "$DIMRAD_C" "angle=60"
else
  echo "SKIP DIMRAD: dimradang3dm_write_check was not built next to $BIN (DINO8_BUILD_TESTS off) - the independent .3dm checks did not run"
fi

DIMRAD_R="$(dimrad_exec "$TMPW/dimrad_reopen.txt" || true)"
dimrad_has "DIMRAD reopen: the radius dimension keeps radius 5" "$DIMRAD_R" "DimRadiusVal = 5"
dimrad_has "DIMRAD reopen: the diameter dimension is a diameter" "$DIMRAD_R" "DimIsDiameter = 1"
dimrad_has "DIMRAD reopen: the diameter dimension keeps radius 4" "$DIMRAD_R" "DimRadiusVal = 4"
dimrad_has "DIMRAD reopen: the angle dimension's leg point survives" "$DIMRAD_R" "DimP1 = 110,0,0"
