# ---------------------------------------------------------------------------
# CDSPL_: D8-AUTOCAD-DRAW-0004 SPLINE: a control-vertex spline keeps its control points (degree 5 and the maximum 10) and a fit-point spline passes through every fit point
# The app builds the drawing from a command script and exports DXF; cad2d_rows3.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms are commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run3.sh"
cad2d3_case spline
