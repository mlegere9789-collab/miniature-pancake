# UXSEL_: SelAll, SelNone, SelPrev, SelLast, SelClosedCrv and SelOpenCrv proved headlessly on a hand-built scene (see the script header).
# Expected values are hand counts: 9 objects, 5 curves of which 2 are closed (circle, polyline) and 3 open (2 lines, arc). SelAll 9, SelNone 0, SelClosedCrv 2,
# SelOpenCrv 3, SelCrv then SelNone then SelPrev restores the 5 curves, SelLast selects the single newest object (1).
uxsel_ok() { echo "ok   $1"; }
uxsel_bad() { echo "FAIL $1"; fail=1; }
UXSEL_OUT=""
# An @expect_selected that fails makes the app exit non-zero (2) after printing the line; keep the output so the failing checks below
# name what differed, and only call the run broken when the final "smoke:" line is missing (crash, timeout).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  UXSEL_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/uxsel_script.txt" 2>&1)" || true
else
  UXSEL_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/uxsel_script.txt" 2>&1)" || true
fi
if ! grep -q '^smoke:' <<<"$UXSEL_OUT"; then echo "$UXSEL_OUT" | tail -30; uxsel_bad "UXSEL_run: the script did not finish (no smoke line)"; UXSEL_OUT=""; fi
uxsel_block() { awk -v name="$1" '$0 ~ ("Unknown command: UXBEGIN_" name "$") { on = 1; next } on && /Unknown command: UXBEGIN_/ { exit } on { print }' <<<"$UXSEL_OUT"; }
uxsel_has() {  # block, needle, label
  if grep -qF -- "$2" <<<"$(uxsel_block "$1")"; then uxsel_ok "$3"; else uxsel_bad "$3"; echo "     expected: $2"; uxsel_block "$1" | grep -v 'Command:' | head -6 | sed 's/^/     actual:   /'; fi
}
uxsel_exp() {  # block, N, label: the ok expect_selected line for N must be present
  uxsel_has "$1" "ok   expect_selected $2 (got $2)" "$3"
}
if [ -z "$UXSEL_OUT" ]; then
  uxsel_bad "UXSEL_run: the script produced no output"
else
  uxsel_exp selall 9 "UXSEL_SelAll: 9 objects in the scene, SelAll selects all 9"
  uxsel_has selall "9 object(s) selected" "UXSEL_SelAll: the history reports '9 object(s) selected'"
  uxsel_exp selnone 0 "UXSEL_SelNone: after SelAll, SelNone leaves 0 selected"
  uxsel_exp closed 2 "UXSEL_SelClosedCrv: circle + closed polyline = 2"
  uxsel_exp open 3 "UXSEL_SelOpenCrv: two lines + arc = 3"
  uxsel_has prev "5 object(s) selected (previous selection)" "UXSEL_SelPrev: reports restoring the previous 5-curve selection"
  uxsel_exp prev 5 "UXSEL_SelPrev: after SelCrv (5) and SelNone, SelPrev brings back exactly 5"
  uxsel_has last "SelLast: last created object selected (1 selected)" "UXSEL_SelLast: reports the last created object, 1 selected"
  uxsel_exp last 1 "UXSEL_SelLast: exactly 1 selected, the newest object"
  if grep -q '^FAIL' <<<"$UXSEL_OUT"; then uxsel_bad "UXSEL_script: an @expect line failed"; echo "$UXSEL_OUT" | grep '^FAIL' | head -5; else uxsel_ok "UXSEL_script: no @expect line failed"; fi
fi
