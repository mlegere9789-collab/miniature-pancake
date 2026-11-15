# ---------------------------------------------------------------------------
# DDAR_: DIMARC (commands/cmd_draftdocs.cpp). dd_dimarc_script.txt: a 90 degree arc of radius 10 about the origin, dimensioned with its line through a point at
# radius 14; then, in a new document, the same arc with Partial=Yes between the points at 0 and 45 degrees. GetUserText lists the user text; dd_check.py
# parses it. Oracles by hand: arc length = r * angle = 10 * pi / 2 = 15.708 -> "15.71" and 10 * pi / 4 = 7.854; the text sits on the ray of the arc's mid angle
# (45 and 22.5 degrees) at radius 14 + 0.6 text heights.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddar"
cp "$HERE/dd_dimarc_script.txt" "$TMPW/ddar/script.txt"
dd_run "$TMPW/ddar/script.txt"
dd_has "DDAR_report: whole arc 15.71, sweep 90" "DimArc 15.71 (radius 10, sweep 90 deg)"
dd_has "DDAR_report: partial arc 7.854, sweep 45" "DimArc 7.854 (radius 10, sweep 45 deg)"
printf '%s\n' "$DD_OUT" > "$TMPW/ddar/out.txt"
dd_py dimarc "$TMPW/ddar/out.txt"
