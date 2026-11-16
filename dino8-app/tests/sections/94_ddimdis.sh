# ---------------------------------------------------------------------------
# DDDA_: DIMDISASSOCIATE (commands/cmd_draftdocs.cpp). dd_dimdis_script.txt: line L1 (0,0)-(10,0) and L2 (0,20)-(10,20), each with a dimension anchored to both of
# its ends; the second dimension is disassociated; both lines are then scaled x2 about the origin and UpdateDimensions runs. dd_check.py reads the exported
# DIMENSION entities (group 13/14). Oracles by hand: the associative dimension follows L1 to (0,0)-(20,0); the disassociated one keeps (0,20)-(10,20)
# although L2 is now (0,40)-(20,40). The command counts what it changed: 1 disassociated; then 1 / 1 already free / 1 text ignored.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddda"
sed "s|@TMP@|$TMPW/ddda|g" "$HERE/dd_dimdis_script.txt" > "$TMPW/ddda/script.txt"
dd_run "$TMPW/ddda/script.txt"
dd_has "DDDA_report: one dimension disassociated" "DimDisassociate: 1 dimension(s) disassociated, 0 already not associative"
dd_has "DDDA_report: free dimension counted, text ignored" "DimDisassociate: 1 dimension(s) disassociated, 1 already not associative, 1 non-dimension annotation(s) ignored"
[ -s "$TMPW/ddda/dd_dd.dxf" ] || dd_bad "DDDA_export: dd_dd.dxf was not written"
dd_py dimdis "$TMPW/ddda/dd_dd.dxf"
