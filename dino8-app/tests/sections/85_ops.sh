# OPS: OffsetSrf of a closed planar-faced solid that is neither convex nor a right prism (kernel::OffsetSolidPlanar), see
# ops_script.txt for the fixture (cube minus a 5 x 5 x 6 corner block) and the closed forms: volume (10+2d)^3 - 150, bounding box
# [-d, 10+d]^3, shortest edge ratio 1 (d = +1) / 0.5 (d = -1).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  OPSO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/ops_script.txt" 2>&1)" || { echo "$OPSO"; echo "FAIL: ops script exited non-zero"; fail=1; OPSO=""; }
else
  OPSO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/ops_script.txt" 2>&1)" || { echo "$OPSO"; echo "FAIL: ops script exited non-zero"; fail=1; OPSO=""; }
fi
opsblock() { awk -v name="$1" '$0 ~ ("Unknown command: OPSBEGIN_" name "$") { on = 1; next } on && /Unknown command: OPSBEGIN_|Unknown command: OPSEND/ { exit } on { print }' <<<"$OPSO"; }
ops_ok() { echo "ok   $1"; }
ops_bad() { echo "FAIL $1"; fail=1; }
opshas() {
  local blk; blk="$(opsblock "$1")"
  if grep -qF -- "$2" <<<"$blk"; then ops_ok "$3"; else ops_bad "$3"; echo "     expected: $2"; { grep -E "^history: (OffsetSrf|Volume|Bounding|Object|BooleanDifference)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fi
}
opslacks() {
  local blk; blk="$(opsblock "$1")"
  if grep -qF -- "$2" <<<"$blk"; then ops_bad "$3"; echo "     unexpected: $2"; else ops_ok "$3"; fi
}
opsre() {
  local blk; blk="$(opsblock "$1")"
  if grep -qE -- "$2" <<<"$blk"; then ops_ok "$3"; else ops_bad "$3"; echo "     expected (regex): $2"; { grep -E "^history: (OffsetSrf|Volume|Bounding|Object)" <<<"$blk" || true; } | head -8 | sed 's/^/     actual:   /'; fi
}
# opsvol NAME FIRST|LAST: the first / last "Volume = X" value of the case
opsvol() { local a; a="$({ grep -oE "Volume = [0-9.eE+-]+" <<<"$(opsblock "$1")" || true; } | sed -E 's/Volume = //')"; if [ "$2" = FIRST ]; then head -1 <<<"$a"; else tail -1 <<<"$a"; fi; }
# opsbb NAME FIRST|LAST K: component K (1..6 = min xyz, max xyz) of the case's "Bounding box min a,b,c max d,e,f" line
opsbb() {
  local l; l="$({ grep -E "Bounding box min " <<<"$(opsblock "$1")" || true; } | sed -E 's/.*Bounding box min ([^ ]+) max ([^ ]+).*/\1,\2/')"
  if [ "$2" = FIRST ]; then l="$(head -1 <<<"$l")"; else l="$(tail -1 <<<"$l")"; fi
  awk -F, -v k="$3" '{ print $k }' <<<"$l"
}
# opsnum_after NAME LABEL: the number printed right after LABEL (a fixed string without regex characters) in the case's offset line
opsnum_after() { { grep -oE "$2 [0-9.eE+-]+" <<<"$(opsblock "$1")" || true; } | head -1 | sed -E "s/^$2 //"; }
opsapprox() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v r="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; a = e < 0 ? -e : e; exit !(d <= r * a + 1e-6) }'; then ops_ok "$4 (measured $1, expected $2 +-${3} rel)"; else ops_bad "$4 (measured '${1:-none}', expected $2 +-${3} rel)"; fi; }
opsabs() { if [ -n "$1" ] && awk -v v="$1" -v e="$2" -v a="$3" 'BEGIN { d = v - e; if (d < 0) d = -d; exit !(d <= a) }'; then ops_ok "$4 (measured $1, expected $2 +-$3)"; else ops_bad "$4 (measured '${1:-none}', expected $2 +-$3)"; fi; }

# ---- d = +1 and d = -1: closed-form volume, bounding box and shortest-edge ratio of the mitre offset
for c in notch_out notch_in; do
  if [ $c = notch_out ]; then D=1; RATIO=1; else D=-1; RATIO=0.5; fi
  opshas $c "planar-faced solid offset by the kernel (OffsetSolidPlanar): 24 faces," "OPS_$c: the notched cube takes the kernel's planar-solid offset (before: mesh offset)"
  opsre $c "[1-9][0-9]* with more than three faces" "OPS_$c: the boolean's split faces make valence-4+ vertices; their offset planes were checked for concurrency"
  opsabs "$(opsnum_after $c 'offset-plane residual')" 0 1e-9 "OPS_$c: the worst offset-plane residual over every vertex is zero (concurrent)"
  opshas $c "0 self-intersections" "OPS_$c: ...and its global overlap check found none"
  opshas $c "1 object(s), 1 exact B-rep" "OPS_$c: the result is an exact B-rep, not a mesh"
  opsre $c "Object [0-9]*: valid" "OPS_$c: the offset solid is a valid B-rep"
  WANT="$(awk -v d=$D 'BEGIN { printf "%.6f", (10 + 2 * d) ^ 3 - 150 }')"
  opsapprox "$(opsvol $c LAST)" "$WANT" 0.002 "OPS_$c: volume = (10 + 2d)^3 - 150 for d = $D"
  opsabs "$(opsnum_after $c 'shortest edge kept')" "$RATIO" 1e-6 "OPS_$c: the shortest edge keeps $RATIO of its length (closed form)"
  LO="$(awk -v d=$D 'BEGIN { print -d }')"; HI="$(awk -v d=$D 'BEGIN { print 10 + d }')"
  opsabs "$(opsbb $c LAST 1)" "$LO" 1e-6 "OPS_$c: bounding box min x = -d"
  opsabs "$(opsbb $c LAST 3)" "$LO" 1e-6 "OPS_$c: bounding box min z = -d"
  opsabs "$(opsbb $c LAST 4)" "$HI" 1e-6 "OPS_$c: bounding box max x = 10 + d"
  opsabs "$(opsbb $c LAST 6)" "$HI" 1e-6 "OPS_$c: bounding box max z = 10 + d"
done
opsapprox "$(opsvol notch_out FIRST)" 850 0.002 "OPS_notch_out: the fixture itself is 1000 - 150 = 850 (so the offsets above are measured from the right solid)"
opsabs "$(opsbb notch_out FIRST 4)" 10 1e-6 "OPS_notch_out: the fixture's bounding box max x = 10"

# ---- d = -3: the notch ceiling passes the top; refused with the measured edge, mesh offset used
opshas notch_gone "exact B-rep offset not used (the edge between (5, 10, 6) and (5, 10, 10) reverses under this offset: new length / old length = -0.5" "OPS_notch_gone: a collapsing offset is refused naming the ceiling edge (5,10,6)-(5,10,10) with its closed-form ratio (7 - 9) / 4 = -0.5"
opshas notch_gone "using the mesh offset" "OPS_notch_gone: ...and the command says it fell back to the mesh offset"
opslacks notch_gone "planar-faced solid offset by the kernel" "OPS_notch_gone: the kernel construction did not claim success"

# ---- Exact=No never runs the kernel construction (negative control)
opslacks notch_exact_no "planar-faced solid offset by the kernel" "OPS_notch_exact_no: Exact=No does not run OffsetSolidPlanar"
