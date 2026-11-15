# ---------------------------------------------------------------------------
# CDSEL_: Selection sets (D8-AUTOCAD-CMDLINE-0022): Window, Crossing, All and Previous, and a Move applied to the Previous set
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case select
