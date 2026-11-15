# ---------------------------------------------------------------------------
# CDRED_: Redo (D8-AUTOCAD-CMDLINE-0068): Redo right after Undo restores the line, Redo after any other command does nothing
# The app builds the drawing from a command script and exports DXF; cad2d_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), not with anything the app printed.
# ---------------------------------------------------------------------------
. "$HERE/cad2d_run.sh"
cad2d_case redo
