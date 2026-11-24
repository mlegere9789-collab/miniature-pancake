# ---------------------------------------------------------------------------
# CDOP2_: D8-AUTOCAD-MODIFY-0027 OOPS: identity checks, Delete then Undo, New and Open, purged layers
# The app builds the drawing from a command script and exports DXF; cad2d_rows3.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms are commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run3.sh"
cad2d3_case oopsundo
cad2d3_case oopsnew
cad2d3_case oopslayer
