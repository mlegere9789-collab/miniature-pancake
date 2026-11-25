# ---------------------------------------------------------------------------
# SUDOC_EXT1: Extend four lines to a sloping boundary, one Extend command per line (D8-SKETCHUP-LAYOUT-0015)
# The same drawing as SUDOC_EXT (two horizontal and two sloping lines, boundary y = 0.4 (x - 20)); sudoc_rows.py parses the DXF itself (group codes per the DXF reference)
# and compares the new end points with the closed-form intersections, with a decoy; sudoc_selftest.py shows the checkers accept a hand-authored file and reject doctored copies.
# ---------------------------------------------------------------------------
. "$HERE/sudoc_run.sh"
sudoc_case ext1
