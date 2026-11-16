# ---------------------------------------------------------------------------
# DDTM_: TXT2MTXT (commands/cmd_draftdocs.cpp). dd_txt2mtxt_script.txt makes three single-line texts AAA (0,0), BBB (0,20), CCC (0,10), all height 3,
# merges them and exports DXF; dd_check.py reads the MTEXT entity (group 10 insertion point, 40 height, 71 attachment, 3/1 the text with \P
# paragraph breaks). Oracles (hand-computed):
#   Order=TopDown   one MTEXT "BBB\PCCC\PAAA" (larger y first), top-left corner = the first line's baseline (0,20) + one text height = (0,23), height 3, attach 1
#   Order=Selection one MTEXT "AAA\PBBB\PCCC" with its corner at AAA's baseline (0,0) + 3 = (0,3)
#   Combine=No      three MTEXT entities, one per text; no single-line TEXT is left
# The user-text tags of the merged object (MTextOrigin = 0,23,0) are checked straight from GetUserText as well.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddtm"
sed "s|@TMP@|$TMPW/ddtm|g" "$HERE/dd_txt2mtxt_script.txt" > "$TMPW/ddtm/script.txt"
dd_run "$TMPW/ddtm/script.txt"
dd_has "DDTM_report: 3 texts -> 1 multiline text (TopDown)" "Txt2Mtxt: 3 text(s) -> 1 multiline text object(s)"
dd_has "DDTM_report: Combine=No gives 3 objects" "Txt2Mtxt: 3 text(s) -> 3 multiline text object(s)"
dd_has "DDTM_tags: MTextOrigin = 0,23,0 (first line's baseline + the text height)" "MTextOrigin = 0,23,0"
dd_has "DDTM_tags: the object is an MText with attachment 1" "MTextAttach = 1"
for i in 1 2 3; do [ -s "$TMPW/ddtm/dd_tm$i.dxf" ] || dd_bad "DDTM_export: dd_tm$i.dxf was not written"; done
dd_py txt2mtxt "$TMPW/ddtm/dd_tm1.dxf" "$TMPW/ddtm/dd_tm2.dxf" "$TMPW/ddtm/dd_tm3.dxf"
