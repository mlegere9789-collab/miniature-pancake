# ---------------------------------------------------------------------------
# MFX_: MoveFace of ONE planar face along its own normal through the exact kernel route (moveface_exact_script.txt). MovePartsCommand calls
# kernel::Brep::OffsetPlanarFaceAlongNormal first and falls back to the approximate rigid face move only when the kernel declines, printing
# the kernel's reason. Expected values are closed forms of an extruded circle R = 3 (V = pi R^2 h, S = 2 pi R^2 + 2 pi R h), independent of
# the app's printout:
#   cylgrow : top cap raised 2.5 (h 8 -> 10.5): V = 296.8805, S = 254.4690
#   cylpull : top cap lowered 5 (h 8 -> 3)    : V =  84.8230, S = 113.0973
#   undo    : one undo step restores h = 8    : V = 226.1947
#   taper   : drafted (cone) wall: the kernel declines, the note names the cause, the approximate move runs
#   box     : the app's Box is a welded B-rep: top face raised 3 (10 -> 13): V = 1300, S = 720 (planar neighbours, exact too)
#   side    : a sideways move is not an offset: no exact route, the note says so
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MFXO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/moveface_exact_script.txt" 2>&1)" || { echo "$MFXO" | tail -30; echo "FAIL MFX_run: script exited non-zero"; fail=1; MFXO=""; }
else
  MFXO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/moveface_exact_script.txt" 2>&1)" || { echo "$MFXO" | tail -30; echo "FAIL MFX_run: script exited non-zero"; fail=1; MFXO=""; }
fi
mfx_ok() { echo "ok   $1"; }
mfx_bad() { echo "FAIL $1"; fail=1; }
mfx_block() { awk -v name="$1" '$0 ~ ("Unknown command: MFXBEGIN_" name "$") { on = 1; next } on && /Unknown command: MFXBEGIN_/ { exit } on { print }' <<<"$MFXO"; }
mfx_has() { if grep -qF -- "$2" <<<"$(mfx_block "$1")"; then mfx_ok "$3"; else mfx_bad "$3"; echo "     expected: $2"; { grep -E "^history: (MoveFace|!)" <<<"$(mfx_block "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }
mfx_hasnot() { if grep -qF -- "$2" <<<"$(mfx_block "$1")"; then mfx_bad "$3"; else mfx_ok "$3"; fi; }
mfx_val() { { grep -oE "$2 = [0-9.eE+-]+" <<<"$(mfx_block "$1")" || true; } | tail -1 | sed -E "s/$2 = //"; }
mfx_num() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then mfx_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else mfx_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

mfx_has cylgrow "MoveFace: exact kernel route (OffsetPlanarFaceAlongNormal): face" "MFX_cylgrow: the cap move went through the exact kernel route"
mfx_hasnot cylgrow "declined" "MFX_cylgrow: the kernel did not decline"
mfx_num "$(mfx_val cylgrow Volume)" 296.8805 0.003 "MFX_cylgrow: volume = pi 3^2 10.5"
mfx_num "$(mfx_val cylgrow Area)" 254.4690 0.003 "MFX_cylgrow: area = 2 pi 3^2 + 2 pi 3 10.5"
mfx_has cylpull "MoveFace: exact kernel route (OffsetPlanarFaceAlongNormal): face" "MFX_cylpull: the cap pull went through the exact kernel route"
mfx_num "$(mfx_val cylpull Volume)" 84.8230 0.003 "MFX_cylpull: volume = pi 3^2 3"
mfx_num "$(mfx_val cylpull Area)" 113.0973 0.003 "MFX_cylpull: area = 2 pi 3^2 + 2 pi 3 3"
mfx_has undo "MoveFace: exact kernel route" "MFX_undo: the move ran through the exact route before the undo"
mfx_num "$(mfx_val undo Volume)" 226.1947 0.003 "MFX_undo: undo restores the original cylinder (volume = pi 3^2 8)"
mfx_has taper "the exact kernel route declined (" "MFX_taper: a drafted wall declines the exact route"
mfx_has taper "not a surface of translation along the face normal" "MFX_taper: ... and the note names the cause"
mfx_has taper "so this is the approximate rigid face move" "MFX_taper: ... and the approximate move ran as the fallback"
mfx_has taper "MoveFace: 1 object(s) updated" "MFX_taper: the fallback updated the object"
mfx_has box "MoveFace: exact kernel route (OffsetPlanarFaceAlongNormal): face" "MFX_box: a welded box's top face goes through the exact route too"
mfx_num "$(mfx_val box Volume)" 1300 0.003 "MFX_box: volume = 10 x 10 x 13"
mfx_num "$(mfx_val box Area)" 720 0.003 "MFX_box: area = 2 x 100 + 40 x 13"
mfx_has side "the move is not along the face normal" "MFX_side: a sideways move has no exact route and says so"
mfx_hasnot side "exact kernel route (OffsetPlanarFaceAlongNormal)" "MFX_side: no exact offset was claimed"
