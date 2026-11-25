# ---------------------------------------------------------------------------
# CDEXP_: Explode (D8-AUTOCAD-MODIFY-0022): a closed 4-vertex polyline gives its 4 sides; a block of 3 lines gives back the 3 lines on their layer
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case explode
