# SELP_: SelSmall / SelID / SelChain argument parsing and reporting (selparse_script.txt, blocks small / id / chain). A bad SelSmall
# size, a non-numeric SelID token and a hidden object used to pass silently (the grid size was used, the token dropped, the hidden
# object counted as selected) and SelChain reported the whole selection as "chained curve(s)". Every expectation is hand-computed from
# the geometry in the script: lines of diagonal 1 and 100 (SelSmall 2 -> exactly 1, SelSmall 200 -> 2); a box plus 3 connected lines
# (chain count 3, selection 4); the box alone is no curve. Negative controls: a valid size / id selects exactly as before.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SELP_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/selparse_script.txt" 2>&1)" || { echo "$SELP_OUT" | tail -40; echo "FAIL SELP_run: script exited non-zero"; fail=1; SELP_OUT=""; }
else
  SELP_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/selparse_script.txt" 2>&1)" || { echo "$SELP_OUT" | tail -40; echo "FAIL SELP_run: script exited non-zero"; fail=1; SELP_OUT=""; }
fi
selp_ok() { echo "ok   $1"; }
selp_bad() { echo "FAIL $1"; fail=1; }
selp_blk() { awk -v name="$1" '$0 ~ ("Unknown command: SELPBEGIN_" name "$") { on = 1; next } on && /Unknown command: SELPBEGIN_/ { exit } on { print }' <<<"$SELP_OUT"; }
selp_has() { if grep -qF -- "$2" <<<"$1"; then selp_ok "$3"; else selp_bad "$3"; near "$1" "$2"; fi; }
selp_hasnot() { if grep -qF -- "$2" <<<"$1"; then selp_bad "$3"; near "$1" "$2"; else selp_ok "$3"; fi; }
# how many lines of block $1 contain the fixed string $2
selp_count() { { grep -cF -- "$2" <<<"$1" || true; }; }
SELP_SMALL="$(selp_blk small)"; SELP_ID="$(selp_blk id)"; SELP_CHAIN="$(selp_blk chain)"
if grep -q "^FAIL" <<<"$SELP_OUT"; then selp_bad "SELP_script: a script-level FAIL line (an @expect_selected miss)"; near "$SELP_OUT" "FAIL"; else selp_ok "SELP_script: every @expect_selected line holds"; fi
selp_has "$SELP_SMALL" "SelSmall: 'abc' is not a size above 0" "SELP_1: SelSmall abc names the bad size instead of silently using the grid"
selp_has "$SELP_SMALL" "SelSmall: '-5' is not a size above 0" "SELP_1: ...and so does SelSmall -5"
selp_has "$SELP_SMALL" "SelSmall: '1x' is not a size above 0" "SELP_1: ...and a size with trailing junk (1x)"
if [ "$(selp_count "$SELP_SMALL" "is not a size above 0")" = 3 ]; then selp_ok "SELP_1 (negative control): SelSmall 2 and SelSmall 200 are valid sizes and warn nothing (3 warnings in all)"; else selp_bad "SELP_1: the valid SelSmall sizes warned"; near "$SELP_SMALL" "size above 0"; fi
selp_has "$SELP_SMALL" "1 small object(s) selected (bounding box diagonal < 2)" "SELP_1: SelSmall 2 selects exactly the 1-unit line (diagonal 1 < 2, 100 is not)"
selp_has "$SELP_SMALL" "2 small object(s) selected (bounding box diagonal < 200)" "SELP_1: SelSmall 200 selects both lines"
selp_has "$SELP_ID" "SelID: 'abc' is not an object id (a whole number)" "SELP_2: SelID names a non-numeric token"
selp_has "$SELP_ID" "SelID: '2x' is not an object id (a whole number)" "SELP_2: ...and one with trailing junk (2x)"
selp_has "$SELP_ID" "SelID: object 2 is hidden and cannot be selected" "SELP_3: a hidden object is refused with its reason, not counted as selected"
selp_has "$SELP_ID" "No object with id 99" "SELP_3 (negative control): a missing id still says so"
selp_has "$SELP_CHAIN" "SelChain: none of the selected objects is a curve" "SELP_4: SelChain on a box alone is refused"
selp_has "$SELP_CHAIN" "3 chained curve(s) selected" "SELP_5: the chain grows to the 3 connected lines and the report counts curves only (the box is not one)"
selp_hasnot "$SELP_CHAIN" "4 chained curve(s)" "SELP_5: ...it no longer reports the whole selection (4) as chained curves"
