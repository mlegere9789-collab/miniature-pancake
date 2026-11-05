# ---------------------------------------------------------------------------
# OSE: exact open / variable-thickness Shell of a NON-CONVEX prism (ose_script.txt) - kernel::ShellPolylineExtrusion.
# An extruded L (area 300, 8 high). Expected volumes are closed forms (the cavity is the L with every kept wall moved in
# by 1; a removed wall's cavity edge stays on its own plane), read from the display mesh so the ranges carry tessellation
# error:  cup  2400 - 224*7 = 832 | window (wall y=0 + top)  2400 - 242*7 = 706 | top cap 3 thick  2400 - 224*4 = 1504.
# The extruded L has ONE wall face over all six profile edges (ExtrudeCrv extrudes a degree-1 NURBS curve: 3 faces), so a
# click on the wall picks the profile edge nearest the click; see ose_script.txt.
# ---------------------------------------------------------------------------
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  OSEOUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/ose_script.txt" 2>&1)" || { echo "$OSEOUT"; echo "FAIL: ose script exited non-zero"; fail=1; OSEOUT=""; }
else
  OSEOUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/ose_script.txt" 2>&1)" || { echo "$OSEOUT"; echo "FAIL: ose script exited non-zero"; fail=1; OSEOUT=""; }
fi
oseblock() { awk -v name="$1" '$0 ~ ("Unknown command: OSEBEGIN_" name "$") { on = 1; next } on && /Unknown command: OSEBEGIN_|Unknown command: OSEEND/ { exit } on { print }' <<<"$OSEOUT"; }
osecheck() { local blk; blk="$(oseblock "$1")"; if grep -q -- "$2" <<<"$blk"; then echo "ok   $3"; else echo "FAIL $3"; echo "     expected: $2"; { grep -E "^(history: )?(Shell|Object|Volume)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fail=1; fi; }
# The volume is the one the Shell command itself prints for the exact B-rep it made ("..., exact B-rep, volume N"):
# a FromPlanarFaces-built body has no Volume() of its own, so (as the other exact-Shell sections do) it is measured from
# the display mesh of the result. A mesh fallback prints no such line, so it cannot satisfy this check by accident.
osevol() {
  local blk v; blk="$(oseblock "$1")"
  v="$({ grep -E "exact B-rep, volume " <<<"$blk" || true; } | tail -1 | sed -E 's/.*exact B-rep, volume ([0-9.eE+-]+).*/\1/')"
  if [ -n "$v" ] && awk -v v="$v" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v >= lo && v <= hi) }'; then echo "ok   $4 (Volume $v in [$2, $3])"; else echo "FAIL $4 (Volume '${v:-none}' not in [$2, $3])"; fail=1; fi
}
osecheck cup "Shell: thickness 1, open (1 face(s) removed), exact B-rep" "OSE_cup: an extruded L with its top clicked off is an exact open B-rep shell (before: mesh construction)"
osevol cup 824 840 "OSE_cup: Volume = 8*300 - 224*7 = 832"
osecheck cup "Object [0-9]*: valid" "OSE_cup: ...Check valid"
osecheck window "Shell: thickness 1, open (2 face(s) removed), exact B-rep" "OSE_window: wall y=0 and the top clicked off (two faces, one an ordinary wall) is exact"
osevol window 699 713 "OSE_window: Volume = 2400 - 242*7 = 706 (cavity runs flush out through the removed wall)"
osecheck window "Object [0-9]*: valid" "OSE_window: ...Check valid"
osecheck vary "Shell: thickness 1, 1 face(s) at their own thickness, closed, exact B-rep" "OSE_vary: the top cap at its own thickness 3 on the non-convex L is exact"
osevol vary 1490 1518 "OSE_vary: Volume = 2400 - 224*4 = 1504"
osecheck vary "Object [0-9]*: valid" "OSE_vary: ...Check valid"
osecheck reflex "exact B-rep shell not used (removed wall [0-9]*: the cavity does not end inside" "OSE_reflex: removing the wall that ends at the reflex corner is refused WITH the kernel's reason (negative control)"
