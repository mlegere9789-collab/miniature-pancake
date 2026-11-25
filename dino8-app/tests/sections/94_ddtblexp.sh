# ---------------------------------------------------------------------------
# DDTE_: TABLEEXPORT (commands/cmd_draftdocs.cpp). dd_check.py writes a hand-built CSV (Python's csv module: commas, a double quote and a line break inside
# cells) that a table is filled from with DataLink Mode=Pull; TableExport writes the table out again. Oracle: the output equals what Python's csv module (RFC 4180,
# QUOTE_MINIMAL, "\n" row ends) writes for the same 4 x 3 cells, byte for byte, and parses back to the same cells; a 2 x 2 table with Title=Parts exports
# the title as a first row padded to 2 fields: "Parts," then a,b and c,d.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddte"
dd_py tblfix "$TMPW/ddte/dd_te_in.csv"
sed "s|@TMP@|$TMPW/ddte|g" "$HERE/dd_tblexp_script.txt" > "$TMPW/ddte/script.txt"
dd_run "$TMPW/ddte/script.txt"
dd_has "DDTE_report: the pulled table is 4 x 3 and written as 4 CSV lines" "TableExport: 4 x 3 table written as 4 CSV line(s)"
dd_has "DDTE_report: the titled table is written as 3 CSV lines" "TableExport: 2 x 2 table written as 3 CSV line(s)"
for f in dd_te1 dd_te2; do [ -s "$TMPW/ddte/$f.csv" ] || dd_bad "DDTE_export: $f.csv was not written"; done
dd_py tblexp "$TMPW/ddte/dd_te_in.csv" "$TMPW/ddte/dd_te1.csv" "$TMPW/ddte/dd_te2.csv"
