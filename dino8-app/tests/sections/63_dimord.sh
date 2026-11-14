# ---------------------------------------------------------------------------
# DIMORD_: Save3dm writes a DimOrdinate as a real ON_DimOrdinate and a Centermark as a real ON_Centermark (File3dm.cpp
# BuildOnDimOrdinateForGroup / BuildOnCentermarkForGroup). Before this both were saved as their baked leader line / glyph /
# crossing-line curves plus private user-text tags, so a plain OpenNURBS reader (Rhino) saw loose curves, not annotations.
# PARITY_MAP "Native .3dm read/write": the writer half of the ON_DimOrdinate / ON_Centermark readers.
#
# Same split as DIMWR_: one app process exports, the independent checker dimordcm3dm_write_check parses the saved file with
# OpenNURBS alone, a second fresh process reopens it (a live annotation already in the document when Open runs hits an unrelated
# static-cache quirk in UpdateAssociativeAnnotations).
#
# Oracles (closed-form, none read from the app):
#  * DimOrdinate Direction=X, base (0,0,0), feature (400,10,0): the measured coordinate is x1 - x0 = 400, the base and the
#    feature come back as picked.
#  * DimOrdinate Direction=Y, base (0,0,0), feature (30,70,0): the measured coordinate is y1 - y0 = 70.
#  * The leader point of an X ordinate is the feature moved 2.5 text heights along +y, of a Y ordinate along +x (the layout
#    BuildOrdinateDimensionGeometry draws); the text height is the one of the dimension style the saved object references, read
#    by the checker, so the offset is 2.5 x that number and is computed here.
#  * Centermark Size=2 on the circle centre (60,-50,0) r 5: centre (60,-50,0) and the style's CenterMark size 2 (the reader takes
#    the drawn size from that field, not from the radius).
#  * Negative control: the file holds exactly 2 ordinates + 1 centermark + 1 loose curve (the circle the centermark marks, which
#    the test made itself) and 4 geometry objects: an annotation written for real must not also leave its baked curves behind.
#  * Reopen in a fresh process: the annotations come back with their measured points / direction / centre / size tags.
dimord_ok() { echo "ok   $1"; }
dimord_bad() { echo "FAIL $1"; fail=1; }
dimord_exec() {  # dimord_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 60 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$1" 2>&1
  fi
}
dimord_has() {  # dimord_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then dimord_ok "$1"; else dimord_bad "$1 (missing '$3')"; fi
}

cat > "$TMPW/dimord_export.txt" <<EOS
SetActiveViewport Top
New
DimOrdinate Direction=X
0,0,0
400,10,0
Enter
DimOrdinate Direction=Y
0,0,0
30,70,0
Enter
Circle 60,-50,0 5
SelLast
Centermark Size=2
SaveAs $TMPW/dimord.3dm
EOS
cat > "$TMPW/dimord_reopen.txt" <<EOS
SetActiveViewport Top
Open $TMPW/dimord.3dm
SelAll
GetUserText
EOS
DIMORD_E="$(dimord_exec "$TMPW/dimord_export.txt" || true)"
dimord_has "DIMORD: the export process saved the file" "$DIMORD_E" "Saved $TMPW/dimord.3dm"

DIMORD_CHECKER="$(dirname "$BIN")/dimordcm3dm_write_check"
if [ -x "$DIMORD_CHECKER" ]; then
  DIMORD_C="$("$DIMORD_CHECKER" "$TMPW/dimord.3dm" 2>&1 || true)"
  dimord_has "DIMORD: an independent OpenNURBS reader finds exactly 2 real ON_DimOrdinate, 1 real ON_Centermark, 1 loose curve (the user's own circle) and 4 geometry objects" "$DIMORD_C" "2 real ON_DimOrdinate object(s), 1 real ON_Centermark object(s), 1 loose curve(s), 4 geometry object(s)"
  dimord_has "DIMORD X ordinate: direction, base, feature and the measured coordinate 400" "$DIMORD_C" "dir=X base=(0,0,0) feature=(400,10,0)"
  dimord_has "DIMORD X ordinate: the measured coordinate is x1 - x0 = 400" "$(grep '^dimord 1:' <<<"$DIMORD_C")" "value=400 "
  dimord_has "DIMORD Y ordinate: direction, base and feature" "$DIMORD_C" "dir=Y base=(0,0,0) feature=(30,70,0)"
  dimord_has "DIMORD Y ordinate: the measured coordinate is y1 - y0 = 70" "$(grep '^dimord 2:' <<<"$DIMORD_C")" "value=70 "
  # leader point = feature + 2.5 * text height along +y (X ordinate) / +x (Y ordinate), the height read from the saved style
  if awk '/^dimord [0-9]+:/ {
            n = split($0, f, /[=(), ]+/); dir = ""; for (i = 1; i <= n; i++) if (f[i] == "dir") dir = f[i + 1]
            for (i = 1; i <= n; i++) { if (f[i] == "feature") { fx = f[i + 1]; fy = f[i + 2]; fz = f[i + 3] } if (f[i] == "leader") { lx = f[i + 1]; ly = f[i + 2]; lz = f[i + 3] } if (f[i] == "texth") h = f[i + 1] }
            dx = lx - fx; dy = ly - fy; dz = lz - fz
            ex = (dir == "Y") ? 2.5 * h : 0; ey = (dir == "X") ? 2.5 * h : 0
            if (h <= 0 || (dx - ex) ^ 2 + (dy - ey) ^ 2 + dz ^ 2 > 1e-10) bad = 1; seen++
          } END { exit !(seen == 2 && !bad) }' <<<"$DIMORD_C"; then
    dimord_ok "DIMORD: both leader points are the feature moved 2.5 text heights along +y (X) / +x (Y), the height read from the saved dimension style"
  else
    dimord_bad "DIMORD: a leader point is not feature + 2.5 x text height ($(grep '^dimord' <<<"$DIMORD_C" | tr '\n' '|'))"
  fi
  dimord_has "DIMORD centermark: centre (60,-50,0) and the style's CenterMark size 2" "$DIMORD_C" "centermark 1: center=(60,-50,0) size=2 "
else
  echo "SKIP DIMORD: dimordcm3dm_write_check was not built next to $BIN (DINO8_BUILD_TESTS off) - the independent .3dm checks did not run"
fi

DIMORD_R="$(dimord_exec "$TMPW/dimord_reopen.txt" || true)"
dimord_has "DIMORD reopen: the X ordinate's direction survives" "$DIMORD_R" "DimOrdinateDir = X"
dimord_has "DIMORD reopen: the X ordinate's measured points survive" "$DIMORD_R" "DimP1 = 400,10,0"
dimord_has "DIMORD reopen: the Y ordinate's direction survives" "$DIMORD_R" "DimOrdinateDir = Y"
dimord_has "DIMORD reopen: the Y ordinate's measured points survive" "$DIMORD_R" "DimP1 = 30,70,0"
dimord_has "DIMORD reopen: the centermark's centre survives" "$DIMORD_R" "CenterCenter = 60,-50,0"
dimord_has "DIMORD reopen: the centermark's size survives" "$DIMORD_R" "CenterSize = 2"
