# ---------------------------------------------------------------------------
# CDLIN_: LINE chain with the Close option (closing segment, needs two segments) and Undo (removes only the last segment) (D8-AUTOCAD-DRAW-0001)
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case lines
