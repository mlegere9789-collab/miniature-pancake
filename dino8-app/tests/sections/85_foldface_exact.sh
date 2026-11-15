# ---------------------------------------------------------------------------
# FLD_: FoldFace through the exact kernel fold (foldface_exact_script.txt). FoldFaceCommand calls kernel::FoldFaceConvexPlanar first (the face's
# plane is rotated about one of its OWN edges, every other face is re-trimmed, the solid stays closed) and falls back to the approximate rigid
# fold only when the kernel declines, printing the kernel's reason. Expected values are closed forms of a 10 x 10 x 10 cube whose top face is
# folded about its front edge by theta (the top plane becomes z = 10 + y tan theta), independent of the app's printout:
#   V = 1000 + 500 tan theta ; S = 100 + 100 + 10 (10 + 10 t) + 2 x 10 (10 + 5 t) + 100 / cos theta, t = tan theta
#   +30 : V = 1288.675, S = 730.940 ; -30 : V = 711.325, S = 500.000 ; +60 : V = 1866.025
#   -60 : the back wall would vanish (|tan| > 1): the kernel refuses and says why ; a hinge that is not an edge of the picked face, and a
#         cylinder's cap (non-planar wall) are refused the same way and the approximate rigid fold runs
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  FLDO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/foldface_exact_script.txt" 2>&1)" || { echo "$FLDO" | tail -30; echo "FAIL FLD_run: script exited non-zero"; fail=1; FLDO=""; }
else
  FLDO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/foldface_exact_script.txt" 2>&1)" || { echo "$FLDO" | tail -30; echo "FAIL FLD_run: script exited non-zero"; fail=1; FLDO=""; }
fi
fld_ok() { echo "ok   $1"; }
fld_bad() { echo "FAIL $1"; fail=1; }
fld_block() { awk -v name="$1" '$0 ~ ("Unknown command: FLDBEGIN_" name "$") { on = 1; next } on && /Unknown command: FLDBEGIN_/ { exit } on { print }' <<<"$FLDO"; }
fld_has() { if grep -qF -- "$2" <<<"$(fld_block "$1")"; then fld_ok "$3"; else fld_bad "$3"; echo "     expected: $2"; { grep -E "^history: (FoldFace|!)" <<<"$(fld_block "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }
fld_hasnot() { if grep -qF -- "$2" <<<"$(fld_block "$1")"; then fld_bad "$3"; else fld_ok "$3"; fi; }
fld_val() { { grep -oE "$2 = [0-9.eE+-]+" <<<"$(fld_block "$1")" || true; } | tail -1 | sed -E "s/$2 = //"; }
fld_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then fld_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else fld_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

fld_has p30 "FoldFace: exact kernel fold (FoldFaceConvexPlanar)" "FLD_p30: the fold went through the exact kernel route"
fld_hasnot p30 "declined" "FLD_p30: the kernel did not decline"
fld_num "$(fld_val p30 Volume)" 1288.675 0.003 "FLD_p30: volume = 1000 + 500 tan 30"
fld_num "$(fld_val p30 Area)" 730.940 0.003 "FLD_p30: area = 100 + 100 + 10 (10 + 10 t) + 20 (10 + 5 t) + 100 / cos 30"
fld_has m30 "FoldFace: exact kernel fold (FoldFaceConvexPlanar)" "FLD_m30: the opposite fold went through the exact kernel route"
fld_num "$(fld_val m30 Volume)" 711.325 0.003 "FLD_m30: volume = 1000 - 500 tan 30"
fld_num "$(fld_val m30 Area)" 500 0.003 "FLD_m30: area (t = -tan 30) = 500"
fld_has p60 "FoldFace: exact kernel fold (FoldFaceConvexPlanar)" "FLD_p60: a steep rising fold is still exact"
fld_num "$(fld_val p60 Volume)" 1866.025 0.003 "FLD_p60: volume = 1000 + 500 tan 60"
fld_has m60 "the exact kernel route declined (" "FLD_m60: a fold that would make the back wall vanish is declined"
fld_has m60 "collapses face" "FLD_m60: ... and the note carries the kernel's reason"
fld_has m60 "so this is the approximate rigid fold" "FLD_m60: ... and the approximate rigid fold ran"
fld_has notedge "the hinge edge is not an edge of the picked face" "FLD_notedge: a hinge that is not an edge of the picked face is declined and says so"
fld_has notedge "FoldFace: face 5 folded 30 degree" "FLD_notedge: the approximate fold ran (unchanged legacy message)"
fld_has cyl "is not planar" "FLD_cyl: a cylinder's cap (non-planar wall) is declined with the kernel's reason"
fld_has cyl "so this is the approximate rigid fold" "FLD_cyl: ... and the approximate rigid fold ran"
