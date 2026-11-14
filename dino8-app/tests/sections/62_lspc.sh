# ---------------------------------------------------------------------------
# LSPC_: Loft Spacing=Chord|Uniform|Centripetal (lspc_script.txt) - the kernel's Brep::LoftSpacing from the app. Three coaxial
# circles (r 1 z 0, r 1 z 1, r 3 z 4). Every spacing interpolates the sections, so only the shape BETWEEN them (hence the
# lateral area) changes; for Uniform the stations are exactly v = 0, 1/2, 1 and the area has a closed form computed outside
# the program: z(v) = 4 v^2, r(v) = 1 - 2 v + 4 v^2, area = integral 2 pi r sqrt(r'^2 + z'^2) dv = 50.8603 (the app's Area
# is a tessellated figure: +-1 percent accepted). Controls: the default (Chord) stations give a clearly different area (about
# 47.6 by an independent hand estimate of the averaged-chord stations; only "below 49.5" is asserted), Centripetal builds and
# names itself, and Uniform with the end caps left on is refused for the caps WITH the kernel's reason (the start cap plane is
# parallel to the sweep: z'(0) = 0, r'(0) = -2) while the uncapped wall is still built. Here-strings only.
# NOT checked here: the exact Chord / Centripetal areas (the hand estimate of their stations is not an oracle), History replay.
# ---------------------------------------------------------------------------
lspc_ok() { echo "ok   $1"; }
lspc_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LSPC_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/lspc_script.txt" 2>&1)" || { echo "$LSPC_OUT" | tail -40; echo "FAIL LSPC_run: script exited non-zero"; fail=1; LSPC_OUT=""; }
else
  LSPC_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/lspc_script.txt" 2>&1)" || { echo "$LSPC_OUT" | tail -40; echo "FAIL LSPC_run: script exited non-zero"; fail=1; LSPC_OUT=""; }
fi
if grep -q "^FAIL" <<<"$LSPC_OUT"; then lspc_bad "LSPC_script: an @expect line of the script failed"; else lspc_ok "LSPC_script: every @expect_objects line held"; fi
lspc_case() { awk -v n="$1" '/Command: Loft( |$)/ { c++ } c == n' <<<"$LSPC_OUT"; }
lspc_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
lspc_has() { if grep -qF -- "$2" <<<"$1"; then lspc_ok "$3"; else lspc_bad "$3"; near "$1" "$2"; fi; }
lspc_hasnot() { if grep -qF -- "$2" <<<"$1"; then lspc_bad "$3"; near "$1" "$2"; else lspc_ok "$3"; fi; }
lspc_num() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | head -1 | grep -oE '[-0-9.e+]+$' || true)"
  if lspc_in "$v" "$3" "$4"; then lspc_ok "$5 ($v in $3..$4)"; else lspc_bad "$5 ('$v' not in $3..$4)"; fi
}
LSPC_1="$(lspc_case 1)"; LSPC_2="$(lspc_case 2)"; LSPC_3="$(lspc_case 3)"; LSPC_4="$(lspc_case 4)"
lspc_has "$LSPC_1" "every section reproduced" "LSPC_1: Spacing=Uniform builds the exact kernel loft through the three circles"
lspc_has "$LSPC_1" "Spacing=Uniform station parameters" "LSPC_1: ...and the message names the spacing"
lspc_num "$LSPC_1" "Area = " 50.35 51.37 "LSPC_1: lateral area = integral 2 pi r sqrt(r'^2 + z'^2) dv = 50.8603 (stations 0, 1/2, 1)"
lspc_has "$LSPC_2" "every section reproduced" "LSPC_2_control: the default style still builds the exact kernel loft"
lspc_hasnot "$LSPC_2" "Spacing=" "LSPC_2_control: ...and prints no Spacing text"
lspc_num "$LSPC_2" "Area = " 44 49.5 "LSPC_2_control: chord stations give a clearly different area (below 49.5 against Uniform's 50.86)"
lspc_has "$LSPC_3" "Spacing=Centripetal station parameters" "LSPC_3: Spacing=Centripetal builds and names the spacing"
lspc_num "$LSPC_3" "Area = " 44 50.2 "LSPC_3: ...its area differs from Uniform's 50.86 (below 50.2)"
lspc_has "$LSPC_4" "caps not made" "LSPC_4: Uniform with caps left on is refused for the caps"
lspc_has "$LSPC_4" "parallel to the sweep direction" "LSPC_4: ...WITH the kernel's reason (cap plane parallel to the sweep at v = 0)"
lspc_has "$LSPC_4" "every section reproduced" "LSPC_4: ...and the uncapped wall is still built through every section"
