# QC3A_: hostile-input probes of Divide / Array / ArrayPolar / PointGrid / Helix / Circle / Line / Polygon
# (qc3a_script.txt). Each check FAILs on the build that has the bug: Divide 100000000, Divide by length 1e-9,
# Array 3000 x 3000, ArrayPolar 100000000 never returned; PointGrid CountX=100000 CountY=100000 was killed for memory;
# Helix Pitch=1e-9 / Turns=1e9 asked the exact-interpolation search for billions of turns; Circle radius 0 / 1e308,
# Polygon with the corner on the center and a Line of zero length finished silently (the Line made a zero-length curve);
# Divide with nothing to divide left an empty undo step.
qc3a_ok() { echo "ok   $1"; }
qc3a_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  QC3A_OUT="$(timeout 300 "$BIN" --smoke 30 --script "$HERE/qc3a_script.txt" 2>&1)" || { echo "$QC3A_OUT" | tail -20; echo "FAIL QC3A_run: script exited non-zero or timed out (a hang is one of the count bugs)"; fail=1; QC3A_OUT=""; }
else
  QC3A_OUT="$(timeout 300 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/qc3a_script.txt" 2>&1)" || { echo "$QC3A_OUT" | tail -20; echo "FAIL QC3A_run: script exited non-zero or timed out (a hang is one of the count bugs)"; fail=1; QC3A_OUT=""; }
fi
qc3a_case() { awk -v n="QC3ABEGIN_$1" 'index($0, "Unknown command: " n) { on = 1; next } on && /Unknown command: QC3ABEGIN_/ { exit } on' <<<"$QC3A_OUT"; }
qc3a_has() { if grep -qF -- "$2" <<<"$1"; then qc3a_ok "$3"; else qc3a_bad "$3"; fi; }
qc3a_hasnot() { if [ -z "$1" ] || grep -qF -- "$2" <<<"$1"; then qc3a_bad "$3"; else qc3a_ok "$3"; fi; }  # an empty case (the script died earlier) must not pass
A="$(qc3a_case divide_count)"
qc3a_has "$A" "Divide: number of segments must be a number from 1 to 100000" "QC3A_divide_count: 100000000 segments is refused with the allowed range"
qc3a_has "$A" "Divide: 1 curve(s) divided" "QC3A_divide_count: 4 segments typed afterwards still divides"
qc3a_has "$A" "6 object(s) selected" "QC3A_divide_count: the line plus its 5 division points"
A="$(qc3a_case divide_len)"
qc3a_has "$A" "would make about" "QC3A_divide_len: a 1e-9 segment length on a 10 unit line is refused with the point count"
qc3a_has "$A" "Divide: 1 curve(s) divided" "QC3A_divide_len: segment length 2.5 typed afterwards still divides"
A="$(qc3a_case divide_noop_undo)"
qc3a_has "$A" "Divide: 0 curve(s) divided" "QC3A_divide_noop_undo: Divide with no curve selected reports 0 curves"
qc3a_has "$A" "0 object(s) selected" "QC3A_divide_noop_undo: Undo after it removes the Box (the previous step), it did not undo an empty Divide step"
A="$(qc3a_case array_count)"
qc3a_has "$A" "Array: the number must be a number from 1 to 100000" "QC3A_array_count: Y count 1e12 is refused with the allowed range"
qc3a_has "$A" "Array created 11 object(s)" "QC3A_array_count: 4 x 3 x 1 afterwards still arrays (11 new objects)"
A="$(qc3a_case array_product)"
qc3a_has "$A" "that would create more than 100000 objects" "QC3A_array_product: 400 x 400 cells is refused"
A="$(qc3a_case arraypolar_count)"
qc3a_has "$A" "ArrayPolar: the number of items must be a number from 2 to" "QC3A_arraypolar_count: 100000000 items is refused with the allowed range"
qc3a_has "$A" "4 object(s) selected" "QC3A_arraypolar_count: 4 items afterwards still arrays (original + 3 copies)"
A="$(qc3a_case pointgrid_count)"
qc3a_has "$A" "PointGrid: CountX must be a whole number from 2 to 1000, '100000' ignored" "QC3A_pointgrid_count: CountX=100000 is refused"
qc3a_has "$A" "PointGrid: 3 x 3 grid of points" "QC3A_pointgrid_count: CountX=3 CountY=3 afterwards still builds the grid"
A="$(qc3a_case helix_turns)"
qc3a_has "$A" "Helix: Turns must be a number above 0 and at most 1000" "QC3A_helix_turns: Turns=1e9 is refused"
qc3a_has "$A" "turns (limit 1000)" "QC3A_helix_turns: Pitch=1e-9 on a 10 unit axis is refused with the turn count"
A="$(qc3a_case circle_zero)"
qc3a_has "$A" "Circle: the radius must be greater than zero" "QC3A_circle_zero: radius 0 says so"
A="$(qc3a_case circle_huge)"
qc3a_has "$A" "cannot be built" "QC3A_circle_huge: radius 1e308 says it cannot be built"
A="$(qc3a_case line_zero)"
qc3a_has "$A" "no line created" "QC3A_line_zero: a line from a point to itself is refused with a message"
qc3a_has "$A" "0 object(s) selected" "QC3A_line_zero: no zero-length curve was added"
A="$(qc3a_case polygon_zero)"
qc3a_has "$A" "Polygon: the corner is on the center" "QC3A_polygon_zero: a polygon whose corner is the center says so"
