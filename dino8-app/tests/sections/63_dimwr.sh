# ---------------------------------------------------------------------------
# DIMWR_: Save3dm writes a DimLinear / DimAligned as a real ON_DimLinear (File3dm.cpp BuildOnDimLinearForGroup). Before this a
# Dino 8 dimension was saved as its baked extension-line / dimension-line / arrow / glyph curves plus private user-text tags,
# so a plain OpenNURBS reader (Rhino) saw loose curves, not a dimension. PARITY_MAP "Native .3dm read/write": "Dim*/block
# instances are still written exactly as before".
#
# Two real app processes (export, then a fresh-process reopen; same split as the Leader write section, because a live
# annotation already in the document when Open runs hits an unrelated static-cache quirk in UpdateAssociativeAnnotations),
# and the independent checker dimlinear3dm_write_check, which parses the saved file with OpenNURBS alone.
#
# Oracles (all closed-form, none read from the app):
#  * DimAligned between (0,0,0) and (30,40,0), third pick (-5.6,4.2,0): the points are 50 apart (3-4-5 triangle); the
#    dimension line sits 7 from the measured segment along n = z x (p1-p0)/|p1-p0| = (-0.8,0.6,0), i.e. at p0 + 7n = (-5.6,4.2,0)
#    (so the third pick is exactly that point, and the checker must report dimline=(-5.6,4.2,0)).
#  * DimLinear (horizontal) between (100,0,0) and (130,40,0), third pick (115,-10,0): the horizontal span is 30, the
#    dimension line is the line y = -10 (the pick lies 30 below the span's centre, farther than the 0 it is outside in x), so
#    the checker's dimension-line point must have y = -10.
#  * Negative control: the file holds exactly the 2 dimensions and 0 loose curves (a dimension written for real must not also
#    leave its baked curves behind) and exactly 2 geometry objects.
#  * Reopen in a fresh process: both come back as dimensions with the exact measured points (DimP0/DimP1 tags) and the
#    aligned one keeps DimAligned = 1 and DimOffset = 7.
dimwr_ok() { echo "ok   $1"; }
dimwr_bad() { echo "FAIL $1"; fail=1; }
dimwr_exec() {  # dimwr_exec <script>
  if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
    timeout 900 "$BIN" --smoke 60 --script "$1" 2>&1
  else
    timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$1" 2>&1
  fi
}
dimwr_has() {  # dimwr_has <what> <text> <fixed string>
  if grep -qF -- "$3" <<<"$2"; then dimwr_ok "$1"; else dimwr_bad "$1 (missing '$3')"; fi
}

cat > "$TMPW/dimwr_export.txt" <<EOS
DimAligned 0,0,0 30,40,0 -5.6,4.2,0
DimLinear 100,0,0 130,40,0 115,-10,0
SaveAs $TMPW/dimwr.3dm
EOS
cat > "$TMPW/dimwr_reopen.txt" <<EOS
Open $TMPW/dimwr.3dm
SelAll
GetUserText
EOS
DIMWR_E="$(dimwr_exec "$TMPW/dimwr_export.txt" || true)"
dimwr_has "DIMWR: the export process saved the file" "$DIMWR_E" "Saved $TMPW/dimwr.3dm"

DIMWR_CHECKER="$(dirname "$BIN")/dimlinear3dm_write_check"
if [ -x "$DIMWR_CHECKER" ]; then
  DIMWR_C="$("$DIMWR_CHECKER" "$TMPW/dimwr.3dm" 2>&1 || true)"
  dimwr_has "DIMWR: an independent OpenNURBS reader finds exactly 2 real ON_DimLinear objects and 0 loose curves in the saved file" "$DIMWR_C" "2 real ON_DimLinear object(s), 0 loose curve(s), 2 geometry object(s)"
  dimwr_has "DIMWR aligned: type, both measured points and the 3-4-5 length 50" "$DIMWR_C" "type=Aligned p0=(0,0,0) p1=(30,40,0)"
  dimwr_has "DIMWR aligned: the measured length is the exact 3-4-5 hypotenuse" "$DIMWR_C" "length=50"
  dimwr_has "DIMWR aligned: the dimension line passes through p0 + 7*n = (-5.6,4.2,0)" "$DIMWR_C" "dimline=(-5.6,4.2,0)"
  dimwr_has "DIMWR rotated: type and both measured points" "$DIMWR_C" "type=Rotated p0=(100,0,0) p1=(130,40,0)"
  if grep -E '^dimlinear [0-9]+: type=Rotated' <<<"$DIMWR_C" | grep -qE 'dimline=\([-0-9.e+]+,-10,0\)'; then
    dimwr_ok "DIMWR rotated: the dimension line lies on y = -10"
  else
    dimwr_bad "DIMWR rotated: dimension-line point not on y = -10 (checker said: $(grep 'type=Rotated' <<<"$DIMWR_C"))"
  fi
else
  echo "SKIP DIMWR: dimlinear3dm_write_check was not built next to $BIN (DINO8_BUILD_TESTS off) - the independent .3dm checks did not run"
fi

DIMWR_R="$(dimwr_exec "$TMPW/dimwr_reopen.txt" || true)"
dimwr_has "DIMWR reopen: the aligned dimension's measured points survive" "$DIMWR_R" "DimP1 = 30,40,0"
dimwr_has "DIMWR reopen: the aligned dimension is aligned" "$DIMWR_R" "DimAligned = 1"
dimwr_has "DIMWR reopen: the aligned dimension keeps offset 7" "$DIMWR_R" "DimOffset = 7"
dimwr_has "DIMWR reopen: the linear dimension's measured points survive" "$DIMWR_R" "DimP1 = 130,40,0"
