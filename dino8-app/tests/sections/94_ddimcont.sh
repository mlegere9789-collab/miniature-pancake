# ---------------------------------------------------------------------------
# DDCN_: DIMCONTINUE (commands/cmd_draftdocs.cpp). dd_dimcont_script.txt: a horizontal dimension (0,0)-(4,0) with its line at y=-5, then DimContinue to
# x=9 and x=15 (the register's sketch: points 0,4,9,15 read 4,5,6 with collinear dimension lines). dd_check.py reads the exported DIMENSION entities
# (group 13/14 = the measured points, 10 = a point on the dimension line). Oracles by hand: the three dimensions run (0,0)-(4,0), (4,0)-(9,0),
# (9,0)-(15,0) on the one line y=-5 and measure 4, 5, 6.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddcn"
sed "s|@TMP@|$TMPW/ddcn|g" "$HERE/dd_dimcont_script.txt" > "$TMPW/ddcn/script.txt"
dd_run "$TMPW/ddcn/script.txt"
dd_has "DDCN_report: the base dimension measures 4" "DimLinear 4"
dd_has "DDCN_report: first continued dimension reads 5" "DimContinue 5"
dd_has "DDCN_report: second continued dimension reads 6" "DimContinue 6"
[ -s "$TMPW/ddcn/dd_dc.dxf" ] || dd_bad "DDCN_export: dd_dc.dxf was not written"
dd_py dimchain "$TMPW/ddcn/dd_dc.dxf" continue
