# SDH_: History rebuild of a recorded SubD boolean replays the typed Levels option (sdbhist_script.txt).
# Oracles: two fully creased SubD boxes are exactly the solid boxes (the app's Volume of a SubD reads the control net, equal to the limit volume):
#   A = [0,10]^3, B = [5,15]^3:  A & B = 5 x 5 x 5 = 125;  B moved by (-2,0,0) is [3,13] x [5,15] x [5,15]:  A & B = 7 x 5 x 5 = 175
# and the Levels option is a recorded, editable parameter: Levels=2 in the live command, the same 2 rounds in the rebuild after a Move (before this
# change the rebuild used the automatic 3), and EditHistory levels=1 rebuilds with 1 round; levels=7 is refused.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SDHO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sdbhist_script.txt" 2>&1)" || { echo "$SDHO"; echo "FAIL: sdbhist script exited non-zero"; exit 1; }
else
  SDHO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sdbhist_script.txt" 2>&1)" || { echo "$SDHO"; echo "FAIL: sdbhist script exited non-zero"; exit 1; }
fi
sdh_ok() { echo "ok   $1"; }
sdh_bad() { echo "FAIL $1"; fail=1; }
sdhblock() { awk -v name="$1" '$0 ~ ("Unknown command: SDHBEGIN_" name "$") { on = 1; next } on && /Unknown command: SDHBEGIN_/ { exit } on { print }' <<<"$SDHO"; }
# sdhrebuild NAME N: the N-th "History rebuild of a boolean: SubD boolean ..." line of the case
sdhrebuild() { { grep -E "^history: History rebuild of a boolean: SubD boolean" <<<"$(sdhblock "$1")" || true; } | sed -n "${2}p"; }
sdhvol() { { grep -oE "Volume = [0-9.eE+-]+" <<<"$(sdhblock "$1")" || true; } | sed -n "${2}p" | sed -E 's/Volume = //'; }
sdhnum() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= r * e) }'; then sdh_ok "$4 (measured $1, exact $2 +-${3} rel)"; else sdh_bad "$4 (measured '${1:-none}', exact $2 +-${3} rel)"; fi; }
sdhhas() { if grep -qF -- "$2" <<<"$1"; then sdh_ok "$3"; else sdh_bad "$3"; echo "     expected: $2"; echo "     line: $(cut -c1-420 <<<"${1:-none}")"; fi; }

# ---- creased boxes: move an operand, the rebuilt intersection follows the closed form
sdhnum "$(sdhvol creased 1)" 125 0.002 "SDH_creased: the live intersection of the two creased boxes is 5 x 5 x 5 = 125"
R="$(sdhrebuild creased 1)"
sdhhas "$R" "rounds used: first 0, second 0 (2 operand(s) exactly their cage, not refined)" "SDH_creased: the rebuild after moving B is still exact (0 rounds)"
sdhnum "$(sdhvol creased 2)" 175 0.002 "SDH_creased: after Move B by (-2,0,0) the rebuilt intersection is 7 x 5 x 5 = 175"
sdhhas "$R" "control-net volume 175" "SDH_creased: the rebuild's own report agrees, control-net volume 175"
# ---- the typed Levels option is replayed
sdhhas "$(grep -E "^history: BooleanDifference: SubD boolean" <<<"$(sdhblock levels)" | head -1)" "rounds used: first 2, second 2" "SDH_levels: the live command with Levels=2 reports 2 rounds"
sdhhas "$(sdhrebuild levels 1)" "rounds used: first 2, second 2" "SDH_levels: the rebuild after Move replays Levels=2 (before: the automatic 3)"
sdhhas "$(sdhrebuild levels 2)" "rounds used: first 1, second 1" "SDH_levels: EditHistory levels=1 rebuilds with 1 round"
sdhhas "$(sdhblock levels)" "levels must be a whole number from 0 (automatic) to 5" "SDH_levels: EditHistory levels=7 is refused with the reason"
