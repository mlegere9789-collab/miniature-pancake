# ---------------------------------------------------------------------------
# CDBLK_: Block and Insert (D8-AUTOCAD-BLOCK-0001): definition with 3 lines relative to the base point, reference at the pick, sources removed
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case block
