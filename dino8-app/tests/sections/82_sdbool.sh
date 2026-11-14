# SDB_: the app's SubD booleans (BooleanUnion/BooleanDifference/BooleanIntersection on two SubDs) use the kernel's per-operand refinement, with a Levels
# option and a report line (sdbool_script.txt). Oracles are exact box-boolean volumes of two fully creased SubD boxes (a creased box IS the solid box;
# the app's Volume of a SubD reads the control net, which then equals the limit volume):
#   A = [0,10]^3, B = [5,15]^3, overlap [5,10]^3 = 125:   union 1000 + 1000 - 125 = 1875   intersection 125   A - B = 875   B - A = 875
# and the union is still exactly 1875 after two SubDivide steps (the seams and edges are creased). Before this change both operands were refined 3 rounds
# (384 faces each) and the cage interpolated, which is not exact for them. Two smooth boxes check the Levels option and the report: Levels=2 reports
# "first 2, second 2", the automatic value for a 6-face cage reports 3.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SDBO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sdbool_script.txt" 2>&1)" || { echo "$SDBO"; echo "FAIL: sdbool script exited non-zero"; exit 1; }
else
  SDBO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sdbool_script.txt" 2>&1)" || { echo "$SDBO"; echo "FAIL: sdbool script exited non-zero"; exit 1; }
fi
sdb_ok() { echo "ok   $1"; }
sdb_bad() { echo "FAIL $1"; fail=1; }
sdbblock() { awk -v name="$1" '$0 ~ ("Unknown command: SDBBEGIN_" name "$") { on = 1; next } on && /Unknown command: SDBBEGIN_/ { exit } on { print }' <<<"$SDBO"; }
sdbhas() { if grep -qF -- "$2" <<<"$(sdbblock "$1")"; then sdb_ok "$3"; else sdb_bad "$3"; echo "     expected: $2"; { grep -E "^history: (Boolean|!)" <<<"$(sdbblock "$1")" || true; } | head -3 | sed 's/^/     actual:   /' | cut -c1-400; fi; }
# sdbreportvol NAME: the "control-net volume X" of the case's status line
sdbreportvol() { { grep -oE "control-net volume [0-9.eE+-]+" <<<"$(sdbblock "$1")" || true; } | head -1 | sed -E 's/control-net volume //'; }
# sdbvol NAME N: the N-th "Volume = X" of the case
sdbvol() { { grep -oE "Volume = [0-9.eE+-]+" <<<"$(sdbblock "$1")" || true; } | sed -n "${2}p" | sed -E 's/Volume = //'; }
# sdbnum VALUE EXPECTED REL DESC (the printed value carries 4 significant digits, so REL is never below 5e-4)
sdbnum() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= r * e) }'; then sdb_ok "$4 (measured $1, exact $2 +-${3} rel)"; else sdb_bad "$4 (measured '${1:-none}', exact $2 +-${3} rel)"; fi; }

for C in "union 1875" "inter 125" "diff 875" "ba 875"; do
  N="${C% *}"; E="${C#* }"
  sdbhas "$N" "rounds used: first 0, second 0 (2 operand(s) exactly their cage, not refined)" "SDB_$N: both creased boxes are exactly their cages, so no rounds are used"
  sdbnum "$(sdbreportvol "$N")" "$E" 0.002 "SDB_$N: the status line's control-net volume is the exact box-boolean volume"
  sdbnum "$(sdbvol "$N" 1)" "$E" 0.002 "SDB_$N: Volume of the result is the exact box-boolean volume"
done
sdbnum "$(sdbvol union 2)" 1875 0.002 "SDB_union: still exactly 1875 after two SubDivide steps (the seams are creased)"
# The volumes alone do not tell the old behaviour (both boxes refined 3 rounds, 384 faces each, then interpolated: a creased box stays exact under
# refinement) from the new one; the cage size does. The intersection of two boxes is a box: exactly 6 faces; the other results stay small.
sdbfaces() { { grep -oE "seam creased\), [0-9]+ face" <<<"$(sdbblock "$1")" || true; } | head -1 | sed -E 's/seam creased\), ([0-9]+) face/\1/'; }
F="$(sdbfaces inter)"; if [ "$F" = "6" ]; then sdb_ok "SDB_inter: the intersection of two boxes is a 6-face box cage (no refinement of exact operands)"; else sdb_bad "SDB_inter: expected a 6-face cage, the status line says '${F:-none}' face(s)"; fi
for N in union diff ba; do
  F="$(sdbfaces $N)"
  if [ -n "$F" ] && [ "$F" -le 40 ]; then sdb_ok "SDB_$N: a compact cage of $F face(s) (<= 40), not hundreds from refined operands"; else sdb_bad "SDB_$N: expected at most 40 faces, the status line says '${F:-none}'"; fi
done
sdbhas inter "SubD boolean (operands refined to their limit surfaces, seam creased)" "SDB_inter: the SubD boolean path was taken (the result is a SubD)"
# ---- Levels option on smooth boxes
sdbhas smooth_levels "rounds used: first 2, second 2 (0 operand(s) exactly their cage, not refined)" "SDB_smooth_levels: Levels=2 refines both smooth boxes 2 rounds"
sdbhas smooth_auto "rounds used: first 3, second 3 (0 operand(s) exactly their cage, not refined)" "SDB_smooth_auto: without Levels a 6-face smooth box is refined 3 rounds (automatic)"
