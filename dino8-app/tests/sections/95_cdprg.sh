# ---------------------------------------------------------------------------
# CDPRG_: Purge (D8-AUTOCAD-DRAW-0039): unreferenced block and empty layer removed, referenced block, used layer, layer 0 equivalent and current layer kept
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case purge
