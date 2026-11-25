# ---------------------------------------------------------------------------
# DDBL_: DIMBASELINE (commands/cmd_draftdocs.cpp). dd_dimbase_script.txt: a base dimension (0,0)-(4,0) with its line below the points at y=-5, then
# DimBaseline Spacing=3 to x=9 and x=15; and a second base (0,50)-(4,50) with its line ABOVE the points at y=56 and the default spacing (3.75, AutoCAD's
# DIMDLI) to x=10. dd_check.py reads the exported DIMENSION entities. Oracles by hand: every baseline dimension shares the first extension origin, its
# line lies one spacing beyond the previous one, away from the measured points: y = -5, -8, -11 and 56, 59.75.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddbl"
sed "s|@TMP@|$TMPW/ddbl|g" "$HERE/dd_dimbase_script.txt" > "$TMPW/ddbl/script.txt"
dd_run "$TMPW/ddbl/script.txt"
dd_has "DDBL_report: baseline to x=9 reads 9" "DimBaseline 9"
dd_has "DDBL_report: baseline to x=15 reads 15" "DimBaseline 15"
dd_has "DDBL_report: baseline to x=10 reads 10" "DimBaseline 10"
[ -s "$TMPW/ddbl/dd_db.dxf" ] || dd_bad "DDBL_export: dd_db.dxf was not written"
dd_py dimchain "$TMPW/ddbl/dd_db.dxf" baseline
