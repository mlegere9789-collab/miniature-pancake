# ---------------------------------------------------------------------------
# CDDIS_: D8-AUTOCAD-SNAP-0027 DIST: distance, angle in the XY plane, angle from the XY plane, deltas
# The app builds the drawing from a command script and exports DXF; cad2d_rows3.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms are commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run3.sh"
cad2d3_case dist
