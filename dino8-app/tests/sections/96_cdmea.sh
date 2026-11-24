# ---------------------------------------------------------------------------
# CDMEA_: D8-AUTOCAD-DRAW-0012 MEASURE, DRAW-0044 DIVIDE, DRAW-0048 equal intervals: points at multiples of a distance, and at the interior division points
# The app builds the drawing from a command script and exports DXF; cad2d_rows3.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms are commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run3.sh"
cad2d3_case measure
