# ---------------------------------------------------------------------------
# DDFR_: FIND with Replace All (FindReplace, commands/cmd_draftdocs.cpp). dd_findrep_script.txt: single-line texts DOOR, Door2, INDOORS, door, WINDOW ...,
# four passes, DXF exported after each; dd_check.py reads the TEXT entities (group 10/20 position, 1 string). Oracles (hand-computed string edits):
#   pass 1  Find=DOOR Replace=WINDOW (case-insensitive, anywhere)  DOOR -> WINDOW, Door2 -> WINDOW2, INDOORS -> INWINDOWS, door -> WINDOW, WINDOW stays
#   pass 2  MatchCase=Yes Find=door Replace=PORT                    only adoorb -> aPORTb; Door, DOOR, DOORS stay
#   pass 3  WholeWord=Yes Find=door Replace=D                       Door -> D, DOOR -> D, Door.x -> D.x; DOORS, aPORTb and a_door_b (the underscore is a word character) stay
#   pass 4  Scope=Selection Find=D Replace=Z                        only the selected Dxyz -> Zxyz; the other texts holding a D keep it
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddfr"
sed "s|@TMP@|$TMPW/ddfr|g" "$HERE/dd_findrep_script.txt" > "$TMPW/ddfr/script.txt"
dd_run "$TMPW/ddfr/script.txt"
dd_has "DDFR_report: pass 1 changed 4 texts" "FindReplace: 4 text(s) contain \"DOOR\", 4 replacement(s) made in 4 text(s)"
dd_has "DDFR_report: pass 2 changed 1 text" "FindReplace: 1 text(s) contain \"door\", 1 replacement(s) made in 1 text(s)"
dd_has "DDFR_report: pass 3 changed 3 texts" "FindReplace: 3 text(s) contain \"door\", 3 replacement(s) made in 3 text(s)"
dd_has "DDFR_report: pass 4 changed 1 text" "FindReplace: 1 text(s) contain \"D\", 1 replacement(s) made in 1 text(s)"
for i in 1 2 3 4; do [ -s "$TMPW/ddfr/dd_fr$i.dxf" ] || dd_bad "DDFR_export: dd_fr$i.dxf was not written"; done
dd_py findrep "$TMPW/ddfr/dd_fr1.dxf" "$TMPW/ddfr/dd_fr2.dxf" "$TMPW/ddfr/dd_fr3.dxf" "$TMPW/ddfr/dd_fr4.dxf"
