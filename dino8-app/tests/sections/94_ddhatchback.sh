# ---------------------------------------------------------------------------
# DDHB_: HATCHTOBACK (commands/cmd_draftdocs.cpp). dd_hatchback_script.txt: two solid hatches, a circle sent to the back (DrawOrder -1, the app's
# own SendToBack), a line with no draw order, then HatchToBack; GetUserText lists every object's user text before and after. dd_check.py parses the
# listing. Oracle (by hand): the lowest draw order of any non-hatch object is -1, so both hatches must end at -2 (behind everything); the circle keeps -1,
# the line and the boundary rectangles get none.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddhb"
cp "$HERE/dd_hatchback_script.txt" "$TMPW/ddhb/script.txt"
dd_run "$TMPW/ddhb/script.txt"
dd_has "DDHB_report: 2 hatch objects sent behind the 4 others at draw order -2" "HatchToBack: 2 hatch object(s) sent behind 4 other object(s) (draw order -2)"
printf '%s\n' "$DD_OUT" > "$TMPW/ddhb/out.txt"
dd_py hatchback "$TMPW/ddhb/out.txt"
