# ---------------------------------------------------------------------------
# DDST_: SPACETRANS (commands/cmd_draftdocs.cpp). dd_spacetrans_script.txt: a layout with details at scale 0.02 page mm per model unit (1:50), 0.1 (1:10)
# and a free-zoom detail. Oracles (hand arithmetic): a 2.5 mm paper length is 2.5 / 0.02 = 125 model units at 1:50 and 2.5 / 0.1 = 25 at 1:10; the
# reverse conversion of 125 and 25 model units is 125 * 0.02 = 25 * 0.1 = 2.5 paper mm; a free-zoom detail, a negative length and an unknown detail
# name are refused with a message instead of a number.
# ---------------------------------------------------------------------------
. "$HERE/dd_lib.sh"
mkdir -p "$TMPW/ddst"
cp "$HERE/dd_spacetrans_script.txt" "$TMPW/ddst/script.txt"
dd_run "$TMPW/ddst/script.txt"
dd_has "DDST_paper_to_model_1_50: 2.5 mm at 1:50 = 125 model units" "SpaceTrans: 2.5 paper mm = 125 model units (detail D1, scale 0.02)"
dd_has "DDST_paper_to_model_1_10: 2.5 mm at 1:10 = 25 model units" "SpaceTrans: 2.5 paper mm = 25 model units (detail D2, scale 0.1)"
dd_has "DDST_model_to_paper_1_50: 125 model units at 1:50 = 2.5 mm" "SpaceTrans: 125 model units = 2.5 paper mm (detail D1, scale 0.02)"
dd_has "DDST_model_to_paper_1_10: 25 model units at 1:10 = 2.5 mm" "SpaceTrans: 25 model units = 2.5 paper mm (detail D2, scale 0.1)"
dd_has "DDST_free_zoom: a detail without a scale is refused" "SpaceTrans: detail D3 has no scale (free zoom)"
dd_has "DDST_negative: a negative length is refused" "SpaceTrans: the length must be positive"
dd_has "DDST_unknown_detail: an unknown detail is refused" "SpaceTrans: the active layout has no detail named NoSuch"
