# ---------------------------------------------------------------------------
# CDCTL_: CopyToLayer (D8-AUTOCAD-LAYER-0031): a copy sits on the named new layer at the same position, the original stays
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case copytolayer
