# ---------------------------------------------------------------------------
# PRT_: Print to SVG / PDF at forced scales and fit-to-page, checked against closed forms by an independent parser (prt_script.txt,
# print_check.py: xml.etree for the SVG, a minimal PDF content-stream scan plus pdfinfo for the PDF). The drawing is a 10 x 10 square
# and a radius-10 circle centred at (30, 0), extents 40 x 20. Page = A4 landscape 297 x 210 mm (595.276 x 841.89 pt) unless a forced
# scale needs more; the drawing is centred; the square prints 10 s on a side; the circle's perimeter is 2 pi 10 s; each PDF path is the
# SVG path with y flipped and 1 mm = 72 / 25.4 pt.
#   s1   : Scale=1                -> 297 x 210 page
#   half : Scale=0.5 (1:2)        -> the square is 5 x 5 mm
#   fit  : no Scale               -> s = 6.925 inside the 10 mm margin
#   big  : Scale=10               -> 400 x 200 mm needs more than A4, the page grows to 420 x 220 mm
# ---------------------------------------------------------------------------
mkdir -p "$TMPW/prt"
sed "s|@TMP@|$TMPW/prt|g" "$HERE/prt_script.txt" > "$TMPW/prt_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  PRTO="$(timeout 900 "$BIN" --smoke 30 --script "$TMPW/prt_script.txt" 2>&1)" || { echo "$PRTO" | tail -20; echo "FAIL PRT_run: script exited non-zero"; fail=1; PRTO=""; }
else
  PRTO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/prt_script.txt" 2>&1)" || { echo "$PRTO" | tail -20; echo "FAIL PRT_run: script exited non-zero"; fail=1; PRTO=""; }
fi
prt_one() {  # name mode [scale]
  if [ -s "$TMPW/prt/prt_$1.svg" ] && [ -s "$TMPW/prt/prt_$1.pdf" ]; then
    PRT_R="$(python3 "$HERE/print_check.py" "$TMPW/prt/prt_$1.svg" "$TMPW/prt/prt_$1.pdf" "$2" ${3:-} 2>&1)" && PRT_RC=0 || PRT_RC=$?
    echo "$PRT_R"
    if [ "$PRT_RC" != "0" ] || grep -q "^FAIL" <<<"$PRT_R"; then fail=1; fi
  else
    echo "FAIL PRT_$1: prt_$1.svg / prt_$1.pdf were not written"; fail=1
    { grep -E "Nothing to export|Could not|Warning|!" <<<"$PRTO" || true; } | head -3 | sed 's/^/     app: /'
  fi
}
prt_one s1 scale 1
prt_one half scale 0.5
prt_one fit fit
prt_one big scale 10
