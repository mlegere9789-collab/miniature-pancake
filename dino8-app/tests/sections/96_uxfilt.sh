# UXFILT_: SelectionFilterEdges / Faces / Vertices / None and DecimalPoint proved headlessly through the one-line state reports.
# Each filter command flips its own flag (edges, faces, vertices) and prints the three flags and the master flag; SelectionFilterNone clears the three sub-object flags.
# DecimalPoint Comma makes a number that prints 1.05 print 1,05 (SetZoomExtentsBorder reports the border factor), DecimalPoint Point restores 1.05.
uxfilt_ok() { echo "ok   $1"; }
uxfilt_bad() { echo "FAIL $1"; fail=1; }
UXFILT_OUT=""
# An @expect_selected that fails makes the app exit non-zero (2) after printing the line; keep the output so the failing checks below
# name what differed, and only call the run broken when the final "smoke:" line is missing (crash, timeout).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  UXFILT_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/uxfilt_script.txt" 2>&1)" || true
else
  UXFILT_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/uxfilt_script.txt" 2>&1)" || true
fi
if ! grep -q '^smoke:' <<<"$UXFILT_OUT"; then echo "$UXFILT_OUT" | tail -30; uxfilt_bad "UXFILT_run: the script did not finish (no smoke line)"; UXFILT_OUT=""; fi
uxfilt_block() { awk -v name="$1" '$0 ~ ("Unknown command: UXBEGIN_" name "$") { on = 1; next } on && /Unknown command: UXBEGIN_/ { exit } on { print }' <<<"$UXFILT_OUT"; }
uxfilt_has() {  # block, needle, label
  if grep -qF -- "$2" <<<"$(uxfilt_block "$1")"; then uxfilt_ok "$3"; else uxfilt_bad "$3"; echo "     expected: $2"; uxfilt_block "$1" | grep -v 'Command:' | head -6 | sed 's/^/     actual:   /'; fi
}
uxfilt_exp() {  # block, N, label: the ok expect_selected line for N must be present
  uxfilt_has "$1" "ok   expect_selected $2 (got $2)" "$3"
}
if [ -z "$UXFILT_OUT" ]; then
  uxfilt_bad "UXFILT_run: the script produced no output"
else
  uxfilt_has filter "Selection filter edges on (edges on, faces off, vertices off, filter enabled)" "UXFILT_SelectionFilterEdges: turns only the edge flag on"
  uxfilt_has filter "Selection filter faces on (edges on, faces on, vertices off, filter enabled)" "UXFILT_SelectionFilterFaces: turns the face flag on, edges stay on"
  uxfilt_has filter "Selection filter vertices on (edges on, faces on, vertices on, filter enabled)" "UXFILT_SelectionFilterVertices: turns the vertex flag on"
  uxfilt_has filter "Selection filter edges off (edges off, faces on, vertices on, filter enabled)" "UXFILT_SelectionFilterEdges: a second call turns edges off again"
  uxfilt_has filter "Selection filter: whole objects only" "UXFILT_SelectionFilterNone: reports whole objects only"
  UXFILT_N="$(uxfilt_block filter | grep -cF -- "Selection filter edges on (edges on, faces off, vertices off, filter enabled)" || true)"
  if [ "$UXFILT_N" = 2 ]; then uxfilt_ok "UXFILT_SelectionFilterNone: faces and vertices were cleared (edges on alone appears again after it)"; else uxfilt_bad "UXFILT_SelectionFilterNone: expected 'edges on' alone twice, got $UXFILT_N"; fi
  uxfilt_has decimal "DecimalPoint: numbers print with a decimal comma (1,5)" "UXFILT_DecimalPoint: Comma prints with a decimal comma"
  uxfilt_has decimal "current border factor 1,05 (" "UXFILT_DecimalPoint: the border factor 1.05 prints as 1,05 under Comma"
  uxfilt_has decimal "DecimalPoint: numbers print with a decimal point (1.5)" "UXFILT_DecimalPoint: Point prints with a decimal point"
  uxfilt_has decimal "current border factor 1.05 (" "UXFILT_DecimalPoint: the border factor prints as 1.05 again under Point"
  if grep -q '^FAIL' <<<"$UXFILT_OUT"; then uxfilt_bad "UXFILT_script: an @expect line failed"; echo "$UXFILT_OUT" | grep '^FAIL' | head -5; else uxfilt_ok "UXFILT_script: no @expect line failed"; fi
fi
