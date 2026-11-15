# ---------------------------------------------------------------------------
# CDARR_: Rectangular ARRAY (D8-AUTOCAD-MODIFY-0051): 4 columns x 3 rows at 10 units make 12 independent circles on the grid; a 120000-element request is rejected
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case array
