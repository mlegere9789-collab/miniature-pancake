# ---------------------------------------------------------------------------
# CDAPT_: D8-AUTOCAD-MODIFY-0015 ARRAYPATH: items at the arc-length stations of a line and of a bent polyline, aligned or not
# The app builds the drawing from a command script and exports DXF; cad2d_rows3.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms are commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run3.sh"
cad2d3_case arraypath
