# ---------------------------------------------------------------------------
# DDTC_: TCOUNT (AutoCAD Express Tools text numbering; commands/cmd_draftdocs.cpp). dd_tcount_script.txt makes single-line texts, numbers them,
# exports DXF after each pass; dd_check.py (stdlib DXF parser, hand-computed expectations in its comments) reads the TEXT entities (group 10/20
# insertion point, 1 string).
#   pass 1  five texts at x = 40, 10, 30, 0, 20: Order=X Start=1 Prefix=P-  -> P-1 .. P-5 by ascending x (not creation order)
#   pass 2  Order=Y ascending, Mode=Replace Find=P-, Start=100 Increment=10 -> 1005, 1102, 1204, 1301, 1403; the text without "P-" keeps its text
#   pass 3  Mode=Prefix on one text -> N-7Z9; Mode=Suffix on another -> Q3.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddtc"
sed "s|@TMP@|$TMPW/ddtc|g" "$HERE/dd_tcount_script.txt" > "$TMPW/ddtc/script.txt"
dd_run "$TMPW/ddtc/script.txt"
dd_has "DDTC_report: pass 1 numbered 5 texts" "TCount: 5 text(s) numbered from 1 by 1 (order x, overwrite)"
dd_has "DDTC_report: pass 2 replaced in 5 of the 6 texts" "TCount: 5 text(s) numbered from 100 by 10 (order y, replace)"
for f in dd_tc1 dd_tc2 dd_tc3; do [ -s "$TMPW/ddtc/$f.dxf" ] || dd_bad "DDTC_export: $f.dxf was not written"; done
dd_py tcount "$TMPW/ddtc/dd_tc1.dxf" "$TMPW/ddtc/dd_tc2.dxf" "$TMPW/ddtc/dd_tc3.dxf"
