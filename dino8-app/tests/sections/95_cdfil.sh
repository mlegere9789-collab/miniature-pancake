# ---------------------------------------------------------------------------
# CDFIL_: Fillet (D8-AUTOCAD-MODIFY-0009): tangent arc of radius 5 between perpendicular lines (centre (5,5)), radius 0 sharp corner, all polyline vertices (area and perimeter closed forms)
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case fillet
