# ---------------------------------------------------------------------------
# DDSP_: DIMSPACE (commands/cmd_draftdocs.cpp). dd_dimspace_script.txt: base D1 (0,0)-(4,0) with its line at y=-5; D2 (0,0)-(9,0) at y=-7;
# D3 (0,0)-(15,0) at y=-20; D4 (0,0)-(20,0) above at y=+6. dd_check.py reads the exported DIMENSION entities (group 10 y = the dimension line, 13/14 the
# measured points, which must not change). Oracles by hand, dimensions keep their order on each side of the base:
#   Spacing=10     D2 -15, D3 -25 (below, nearest first), D4 -5 + 10 = +5 (above)
#   Spacing=Auto   twice the base's text height h each way: D2 -5-2h, D3 -5-4h, D4 -5+2h (h = the TextHeight the app lists for the base)
#   Spacing=0      every dimension line on the base's line y=-5
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddsp"
sed "s|@TMP@|$TMPW/ddsp|g" "$HERE/dd_dimspace_script.txt" > "$TMPW/ddsp/script.txt"
dd_run "$TMPW/ddsp/script.txt"
dd_has "DDSP_report: Spacing=10 spaced 3 dimensions" "DimSpace: 3 dimension(s) spaced 10 apart"
dd_has "DDSP_report: Spacing=0 aligned them" "DimSpace: 3 dimension(s) spaced 0 apart (aligned with the base)"
DDSP_H="$(grep -m1 'TextHeight = ' <<<"$DD_OUT" | sed 's/.*TextHeight = //')"
[ -n "$DDSP_H" ] || { dd_bad "DDSP_height: GetUserText did not list the base's TextHeight"; DDSP_H=2; }
DDSP_2H="$(awk -v h="$DDSP_H" 'BEGIN { print 2 * h }')"
dd_has "DDSP_report: Auto spaced 2 x the text height ($DDSP_H) = $DDSP_2H apart" "DimSpace: 3 dimension(s) spaced $DDSP_2H apart"
for i in 1 2 3; do [ -s "$TMPW/ddsp/dd_sp$i.dxf" ] || dd_bad "DDSP_export: dd_sp$i.dxf was not written"; done
dd_py dimspace "$TMPW/ddsp/dd_sp1.dxf" "$TMPW/ddsp/dd_sp2.dxf" "$TMPW/ddsp/dd_sp3.dxf" "$DDSP_H"
