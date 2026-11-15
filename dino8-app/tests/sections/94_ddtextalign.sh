# ---------------------------------------------------------------------------
# DDTA_: TEXTALIGN (commands/cmd_draftdocs.cpp). dd_textalign_script.txt builds base B "Hi" at (10,10) height 4, T1 "Hi" at (30,25) height 4 and
# T2 "Hi" at (-5,40) height 2, aligns them step by step and exports DXF after each step; dd_check.py reads the TEXT insertion points (group 10/20)
# and heights (40). Oracles (hand-computed, none taken from the app):
#   Left   x = 10 for all, y untouched          Top    top of B = 14 -> T1.y = 10, T2.y = 12          Bottom  every baseline y = 10
#   Middle B middle = 12 -> T1.y = 10, T2.y = 11   Right / Center  T2 is half as wide as B (width is proportional to height) so Right moves it by
#   wB - w2 = wB / 2 and Center by half of that: the Right shift is exactly twice the Center shift, whatever the font; T1 (same width as B) never moves.
#   Step 1 and step 8 pick the base at the second prompt (SelName + Enter); steps 2-7 use Base=First.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddta"
sed "s|@TMP@|$TMPW/ddta|g" "$HERE/dd_textalign_script.txt" > "$TMPW/ddta/script.txt"
dd_run "$TMPW/ddta/script.txt"
dd_has "DDTA_report: Left moved the 2 other texts" "TextAlign: 2 text(s) aligned left to the base text"
dd_has "DDTA_report: Top moved the 2 other texts" "TextAlign: 2 text(s) aligned top to the base text"
dd_has "DDTA_report: Right moved 1 text (T1 has the width of the base)" "TextAlign: 1 text(s) aligned right to the base text"
for i in 1 2 3 4 5 6 7 8; do [ -s "$TMPW/ddta/dd_ta$i.dxf" ] || dd_bad "DDTA_export: dd_ta$i.dxf was not written"; done
dd_py textalign "$TMPW"/ddta/dd_ta1.dxf "$TMPW"/ddta/dd_ta2.dxf "$TMPW"/ddta/dd_ta3.dxf "$TMPW"/ddta/dd_ta4.dxf "$TMPW"/ddta/dd_ta5.dxf "$TMPW"/ddta/dd_ta6.dxf "$TMPW"/ddta/dd_ta7.dxf "$TMPW"/ddta/dd_ta8.dxf
