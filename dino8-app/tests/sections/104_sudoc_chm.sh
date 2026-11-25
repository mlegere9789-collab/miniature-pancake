# ---------------------------------------------------------------------------
# SUDOC_CHM: Chamfer distance 10 between perpendicular lines (D8-SKETCHUP-LAYOUT-0017)
# The app builds the drawing from a command script and exports DXF; sudoc_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares with values worked out by hand (closed forms commented next to each check), with a decoy per case; sudoc_selftest.py shows the
# checkers accept a hand-authored file and reject doctored copies.
# ---------------------------------------------------------------------------
. "$HERE/sudoc_run.sh"
sudoc_case chm
