# ---------------------------------------------------------------------------
# CDR3D_: Rotate3D (D8-AUTOCAD-MODIFY-0039): a cube turned 90 degrees about Z through the origin and about an X-parallel axis, edges by Rodrigues' formula
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case rotate3d
