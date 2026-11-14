# SPL_: SplitSubD cuts a SubD with a vertical plane into two SubDs (splitsubd_script.txt; kernel SubD::SplitByPlane).
# Expectations are closed forms of the cube [0,10]^3 cut at x = 4, independent of the app's own printout of the cut:
#   fully creased cube (all 12 edges creased): the limit surface is the solid cube, so the halves are boxes
#       x > 4:  6 x 10 x 10 = 600      x < 4:  4 x 10 x 10 = 400      (volumes read with the app's Volume command)
#   topology of a cut parallel to a face: 4 crossing edges -> 4 cut vertices, 4 of the 6 faces split, 1 cap, 6 faces per half
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SPLO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/splitsubd_script.txt" 2>&1)" || { echo "$SPLO"; echo "FAIL: splitsubd script exited non-zero"; exit 1; }
else
  SPLO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/splitsubd_script.txt" 2>&1)" || { echo "$SPLO"; echo "FAIL: splitsubd script exited non-zero"; exit 1; }
fi
spl_ok() { echo "ok   $1"; }
spl_bad() { echo "FAIL $1"; fail=1; }
splblock() { awk -v name="$1" '$0 ~ ("Unknown command: SPLBEGIN_" name "$") { on = 1; next } on && /Unknown command: SPLBEGIN_/ { exit } on { print }' <<<"$SPLO"; }
splhas() { if grep -qF -- "$2" <<<"$(splblock "$1")"; then spl_ok "$3"; else spl_bad "$3"; echo "     expected: $2"; { grep -E "^history: (SplitSubD|Volume|!)" <<<"$(splblock "$1")" || true; } | head -5 | sed 's/^/     actual:   /'; fi; }
# splvol NAME N: the N-th "Volume = X" of the case
splvol() { { grep -oE "Volume = [0-9.eE+-]+" <<<"$(splblock "$1")" || true; } | sed -n "${2}p" | sed -E 's/Volume = //'; }
splnum() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v t="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= t * e) }'; then spl_ok "$4 (measured $1, closed form $2 +-${3} rel)"; else spl_bad "$4 (measured '${1:-none}', closed form $2 +-${3} rel)"; fi; }

# ---- fully creased cube, cut at x = 4
splhas creased "6 face(s) on the positive side, 6 on the negative; 4 cut vertices, 4 split faces, 1 cap(s), cut rim creased" "SPL_creased: 4 cut vertices, 4 split faces, 1 cap, 6 faces per half, rim creased"
splnum "$(splvol creased 1)" 400 0.005 "SPL_creased: the x < 4 half is the box 4 x 10 x 10"
splnum "$(splvol creased 2)" 600 0.005 "SPL_creased: the x > 4 half is the box 6 x 10 x 10"
# ---- KeepInput
splhas keep "4 cut vertices" "SPL_keep: KeepInput=Yes still cuts"
N="$(grep -cE "^history: Object [0-9]+ \(" <<<"$(splblock keep)" || true)"
if [ "$N" = "3" ]; then spl_ok "SPL_keep: the input and both halves remain (3 objects)"; else spl_bad "SPL_keep: expected 3 objects, List shows '$N'"; fi
# ---- negative controls
splhas miss "does not cross object" "SPL_miss: a plane that misses the cube cuts nothing"
N="$(grep -cE "^history: Object [0-9]+ \(" <<<"$(splblock miss)" || true)"
if [ "$N" = "1" ]; then spl_ok "SPL_miss: the cube is untouched (1 object)"; else spl_bad "SPL_miss: expected 1 object, List shows '$N'"; fi
splhas notsubd "is not a SubD; skipped" "SPL_notsubd: a solid is skipped with a warning"
# ---- smooth cube at its symmetry mid-plane: each half is exactly half the uncut limit volume (the oracle needs no formula for V)
# The app's Volume of a SubD reads the control net of its current level, so the script refines everything to the same total level 5 first (uncut cube:
# 5 x SubDivide; a half cut after Refine=3: 2 more; a Refine=0 half: 5). Kernel measurement with the same procedure (control-net volumes, V = 327.895):
# the half volume is 51.77 / 5.15 / 0.643 / 0.077 % off half of V for Refine 0 / 1 / 2 / 3, so the check is 1 % for Refine=3 and 20 % or more for Refine=0.
splhas smooth_refined "refined 3 level(s) before the cut" "SPL_smooth_refined: Refine=3 is reported"
SV="$(splvol smooth_refined 1)"
for N in 2 3; do
  H="$(splvol smooth_refined $N)"
  if [ -n "$SV" ] && [ -n "$H" ] && awk -v v="$SV" -v h="$H" 'BEGIN { d = h - v / 2; if (d < 0) d = -d; exit !(d <= 0.01 * v / 2) }'; then
    spl_ok "SPL_smooth_refined: half $((N - 1)) has half the uncut volume within 1 % (V = $SV, half = $H)"
  else
    spl_bad "SPL_smooth_refined: half $((N - 1)) volume '${H:-none}' is not within 1 % of half the uncut volume ('${SV:-none}' / 2)"
  fi
done
CV="$(splvol smooth_cage 1)"
CH="$(splvol smooth_cage 2)"
if [ -n "$CV" ] && [ -n "$CH" ] && awk -v v="$CV" -v h="$CH" 'BEGIN { d = h - v / 2; if (d < 0) d = -d; exit !(d >= 0.2 * v / 2) }'; then
  spl_ok "SPL_smooth_cage (negative control): the cage-level cut (Refine=0) is more than 20 % off half the volume (V = $CV, half = $CH)"
else
  spl_bad "SPL_smooth_cage (negative control): expected a cage-level half to be 20 % or more off half the volume (V = '${CV:-none}', half = '${CH:-none}')"
fi
