# SRFSEC_: a single NURBS surface object is sectioned on the exact route (srfsec_script.txt, blocks arc270 / full / miss). The cylinder
# wall radius 5 cut at z = 4: the 360 degree revolve gives the full circle 2 pi 5 = 31.4159265, the 270 degree one an OPEN arc
# 5 x 3 pi / 2 = 23.5619449 (a mesh slice of the same wall is a chord polygon about 3e-4 short: measured 23.55 and 31.4 before, so the windows below
# (+-5e-4) can only be met by the exact route); the report line names the route; a plane above the wall is the negative control.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SRF_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/srfsec_script.txt" 2>&1)" || { echo "$SRF_OUT" | tail -40; echo "FAIL SRFSEC_run: script exited non-zero"; fail=1; SRF_OUT=""; }
else
  SRF_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/srfsec_script.txt" 2>&1)" || { echo "$SRF_OUT" | tail -40; echo "FAIL SRFSEC_run: script exited non-zero"; fail=1; SRF_OUT=""; }
fi
srf_ok() { echo "ok   $1"; }
srf_bad() { echo "FAIL $1"; fail=1; }
srf_blk() { awk -v name="$1" '$0 ~ ("Unknown command: SRFBEGIN_" name "$") { on = 1; next } on && /Unknown command: SRFBEGIN_/ { exit } on { print }' <<<"$SRF_OUT"; }
srf_has() { if grep -qF -- "$2" <<<"$1"; then srf_ok "$3"; else srf_bad "$3"; near "$1" "$2"; fi; }
srf_hasnot() { if grep -qF -- "$2" <<<"$1"; then srf_bad "$3"; near "$1" "$2"; else srf_ok "$3"; fi; }
# the Nth ($2) "Total length = " number in block $1 within [$3, $4] (the arc block prints the source arc first)
srf_len() {
  local v
  v="$({ grep -o "Total length = [0-9.e+-]*" <<<"$1" || true; } | sed -n "${2}p" | awk '{print $4}')"
  if awk -v v="${v:-}" -v lo="$3" -v hi="$4" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; then srf_ok "$5 (measured length $v in $3..$4)"; else srf_bad "$5 (length '$v' not in $3..$4)"; echo "     block: $(grep -E 'Total length|ClippingSections|^history: Object' <<<"$1" | head -6 | tr '\n' '|' | cut -c1-400)"; fi
}
SRF_ARC="$(srf_blk arc270)"; SRF_FULL="$(srf_blk full)"; SRF_MISS="$(srf_blk miss)"
srf_has "$SRF_ARC" "(surface)" "SRFSEC_0: the revolved wall is a plain surface object, not a B-rep (the case this section is about)"
srf_has "$SRF_ARC" "ClippingSections: 1 curve(s) from 1 plane(s) (1 from the exact B-rep section, 0 from mesh slices)" "SRFSEC_1: the 270 degree wall is cut on the exact route (one arc)"
srf_len "$SRF_ARC" 1 23.5618 23.5621 "SRFSEC_0: the fixture arc (centre 0,0,0, from 5,0,0 counter-clockwise to angle 270) is 5 x 3 pi / 2 = 23.5619449"
srf_len "$SRF_ARC" 2 23.5614 23.5624 "SRFSEC_1: the arc is 5 x 3 pi / 2 = 23.5619449"
srf_has "$SRF_ARC" "non-rational, open" "SRFSEC_1: ...an OPEN degree-3 curve (a quarter of the circle is missing; the mesh slice was a 107-point degree-1 polyline)"
srf_has "$SRF_FULL" "ClippingSections: 1 curve(s) from 1 plane(s) (1 from the exact B-rep section, 0 from mesh slices)" "SRFSEC_2: the full wall is cut on the exact route (one closed circle across the seam)"
srf_len "$SRF_FULL" 1 31.4154 31.4164 "SRFSEC_2: the circle is 2 pi 5 = 31.4159265"
srf_has "$SRF_FULL" "non-rational, closed" "SRFSEC_2: ...a CLOSED degree-3 curve (the mesh slice was a 143-point degree-1 polygon)"
srf_has "$SRF_MISS" "ClippingSections: 0 curve(s) from 1 plane(s) (0 from the exact B-rep section, 0 from mesh slices)" "SRFSEC_3 (negative control): a plane above the wall makes no curve and claims no route"
