# UXUTXT_: SelKey, SelValue, SelKeyValue and SelLayer (which now takes a layer name) proved headlessly.
# One object carries Hyperlink=https://dino8.example/last: SelKey Hyperlink / SelValue <it> / SelKeyValue select exactly 1, an absent key or value selects 0.
# SelLayer UxExtra selects the 3 points put on that layer, SelLayer Default the 9 others, an unknown layer name selects nothing and warns.
uxutxt_ok() { echo "ok   $1"; }
uxutxt_bad() { echo "FAIL $1"; fail=1; }
UXUTXT_OUT=""
# An @expect_selected that fails makes the app exit non-zero (2) after printing the line; keep the output so the failing checks below
# name what differed, and only call the run broken when the final "smoke:" line is missing (crash, timeout).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  UXUTXT_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/uxutxt_script.txt" 2>&1)" || true
else
  UXUTXT_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/uxutxt_script.txt" 2>&1)" || true
fi
if ! grep -q '^smoke:' <<<"$UXUTXT_OUT"; then echo "$UXUTXT_OUT" | tail -30; uxutxt_bad "UXUTXT_run: the script did not finish (no smoke line)"; UXUTXT_OUT=""; fi
uxutxt_block() { awk -v name="$1" '$0 ~ ("Unknown command: UXBEGIN_" name "$") { on = 1; next } on && /Unknown command: UXBEGIN_/ { exit } on { print }' <<<"$UXUTXT_OUT"; }
uxutxt_has() {  # block, needle, label
  if grep -qF -- "$2" <<<"$(uxutxt_block "$1")"; then uxutxt_ok "$3"; else uxutxt_bad "$3"; echo "     expected: $2"; uxutxt_block "$1" | grep -v 'Command:' | head -6 | sed 's/^/     actual:   /'; fi
}
uxutxt_exp() {  # block, N, label: the ok expect_selected line for N must be present
  uxutxt_has "$1" "ok   expect_selected $2 (got $2)" "$3"
}
if [ -z "$UXUTXT_OUT" ]; then
  uxutxt_bad "UXUTXT_run: the script produced no output"
else
  uxutxt_exp userkey 1 "UXUTXT_SelKey: key Hyperlink is on exactly 1 object"
  uxutxt_has userkey "0 object(s) selected" "UXUTXT_SelKey: an absent key selects 0"
  uxutxt_exp uservalue 1 "UXUTXT_SelValue: the value https://dino8.example/last is on exactly 1 object"
  uxutxt_has uservalue "0 object(s) selected" "UXUTXT_SelValue: an absent value selects 0"
  uxutxt_has userkeyvalue "SelKeyValue: 1 object(s) with Hyperlink=https://dino8.example/last selected" "UXUTXT_SelKeyValue: key+value selects 1"
  uxutxt_has userkeyvalue "SelKeyValue: 0 object(s) with Hyperlink=https://dino8.example/other selected" "UXUTXT_SelKeyValue: a non-matching value selects 0"
  uxutxt_has layer "3 object(s) on layer UxExtra" "UXUTXT_SelLayer: 'SelLayer UxExtra' selects the 3 points on that layer"
  uxutxt_has layer "9 object(s) on layer Default" "UXUTXT_SelLayer: 'SelLayer Default' selects the 9 objects on Default"
  uxutxt_has layer "SelLayer: no layer named 'NoSuchLayer'" "UXUTXT_SelLayer: an unknown layer name is refused with a warning"
  uxutxt_exp layer 3 "UXUTXT_SelLayer: expect_selected 3 after the UxExtra selection"
  uxutxt_exp layer 9 "UXUTXT_SelLayer: expect_selected 9 after the Default selection"
  uxutxt_exp layer 0 "UXUTXT_SelLayer: nothing is selected after the unknown layer name"
  if grep -q '^FAIL' <<<"$UXUTXT_OUT"; then uxutxt_bad "UXUTXT_script: an @expect line failed"; echo "$UXUTXT_OUT" | grep '^FAIL' | head -5; else uxutxt_ok "UXUTXT_script: no @expect line failed"; fi
fi
