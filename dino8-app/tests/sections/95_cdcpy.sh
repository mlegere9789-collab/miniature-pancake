# ---------------------------------------------------------------------------
# CDCPY_: COPY Mode=Single|Multiple and the linear Array option (items include the original; Fit spreads them to the second point) (D8-AUTOCAD-MODIFY-0002)
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case copy
