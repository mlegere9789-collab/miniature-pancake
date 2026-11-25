# ---------------------------------------------------------------------------
# CDALN_: ALIGN with one or two source/destination point pairs (AlignPoints): move, rotate, and Scale=Yes (D8-AUTOCAD-MODIFY-0018)
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case alignpts
