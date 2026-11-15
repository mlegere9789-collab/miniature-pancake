# ---------------------------------------------------------------------------
# DDDA_: DIMDISASSOCIATE (commands/cmd_draftdocs.cpp). dd_dimdis_script.txt: line L1 (0,0)-(10,0) and L2 (0,20)-(10,20), each with a dimension anchored to both of
# its ends; the second dimension is disassociated; both lines are then scaled x2 about the origin and UpdateDimensions runs. dd_check.py reads the exported
# DIMENSION entities (group 13/14). Oracles by hand: the associative dimension follows L1 to (0,0)-(20,0); the disassociated one keeps (0,20)-(10,20)
# although L2 is now (0,40)-(20,40). The command reports what it converted and what it skipped: 1 converted, 0 skipped; then 1 converted, 2 skipped (1 already free, 1 text).
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddda"
sed "s|@TMP@|$TMPW/ddda|g" "$HERE/dd_dimdis_script.txt" > "$TMPW/ddda/script.txt"
dd_run "$TMPW/ddda/script.txt"
dd_has "DDDA_report: one dimension disassociated" "DimDisassociate: 1 dimension(s) disassociated, 0 skipped (0 not associative, 0 locked, 0 not dimensions)"
dd_has "DDDA_report: free dimension and text skipped" "DimDisassociate: 1 dimension(s) disassociated, 2 skipped (1 not associative, 0 locked, 1 not dimensions)"
[ -s "$TMPW/ddda/dd_dd.dxf" ] || dd_bad "DDDA_export: dd_dd.dxf was not written"
dd_py dimdis "$TMPW/ddda/dd_dd.dxf"
