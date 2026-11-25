# ---------------------------------------------------------------------------
# SUDOC_PRT50: Print at the scale 1:50: a 1 m edge measures 20 mm on the output (D8-SKETCHUP-SCENE-0028, D8-SKETCHUP-LAYOUT-0041). A millimetre document with an edge of 1 m
# ((0,0)-(1000,0)) and one of 0.5 m ((0,0)-(0,500)), viewed from Top, printed with Scale=0.02 (page millimetres per millimetre of the model = 1:50) to p50.svg and p50.pdf, and with
# Scale=0.01 (1:100) to p100.svg. sudoc_view.py reads the files itself (xml.etree for the SVG, the content stream for the PDF, 1 mm = 72 / 25.4 pt): 1000 / 50 = 20 mm and 500 / 50 =
# 10 mm in the SVG (3 decimals) and the PDF, 10 mm at 1:100. Decoy: the 1:50 and 1:100 prints must differ.
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip SUDOC_PRT50 (needs python3)"
else
. "$HERE/rwi_lib.sh"
SUDOC_P_D="$TMPW/sudoc_prt50"; mkdir -p "$SUDOC_P_D"
cat > "$TMPW/sudoc_prt50_script.txt" <<EOS
SetActiveViewport Top
New
Line 0,0,0 1000,0,0
Line 0,0,0 0,500,0
Top
Print $SUDOC_P_D/p50.svg Scale=0.02
Print $SUDOC_P_D/p50.pdf Scale=0.02
Print $SUDOC_P_D/p100.svg Scale=0.01
EOS
rwi_app "$TMPW/sudoc_prt50_script.txt"
rwi_has "Printed $SUDOC_P_D/p50.pdf" "SUDOC_PRT50: Print wrote the 1:50 PDF"
SUDOC_P_R="$(python3 "$HERE/sudoc_view.py" prt50 "$SUDOC_P_D" 2>&1)" && SUDOC_P_RC=0 || SUDOC_P_RC=$?
echo "$SUDOC_P_R"
if [ "$SUDOC_P_RC" != "0" ] || grep -q '^FAIL' <<<"$SUDOC_P_R"; then fail=1; fi
fi
