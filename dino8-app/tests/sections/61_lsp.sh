# ---------------------------------------------------------------------------
# LSP_: Loft Spacing=Chord|Uniform|Centripetal (lsp_script.txt) - the kernel's Brep::LoftSpacing from the app. Three coaxial
# circles (r 1 z 0, r 1 z 1, r 3 z 4). Every spacing interpolates the sections, so only the shape BETWEEN them (hence the
# lateral area) changes; for Uniform the stations are exactly v = 0, 1/2, 1 and the area has a closed form computed outside
# the program: z(v) = 4 v^2, r(v) = 1 - 2 v + 4 v^2, area = integral 2 pi r sqrt(r'^2 + z'^2) dv = 50.8603 (the app's Area
# is a tessellated figure: +-1 percent accepted). Controls: the default (Chord) stations give a clearly different area (about
# 47.6 by an independent hand estimate of the averaged-chord stations; only "below 49.5" is asserted), Centripetal builds and
# names itself, and Uniform with the end caps left on is refused for the caps WITH the kernel's reason (the start cap plane is
# parallel to the sweep: z'(0) = 0, r'(0) = -2) while the uncapped wall is still built. Here-strings only.
# NOT checked here: the exact Chord / Centripetal areas (the hand estimate of their stations is not an oracle), History replay.
# ---------------------------------------------------------------------------
lsp_ok() { echo "ok   $1"; }
lsp_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  LSP_OUT="$(timeout 900 "$BIN" --smoke 30 --script "$HERE/lsp_script.txt" 2>&1)" || { echo "$LSP_OUT" | tail -40; echo "FAIL LSP_run: script exited non-zero"; fail=1; LSP_OUT=""; }
else
  LSP_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$HERE/lsp_script.txt" 2>&1)" || { echo "$LSP_OUT" | tail -40; echo "FAIL LSP_run: script exited non-zero"; fail=1; LSP_OUT=""; }
fi
if grep -q "^FAIL" <<<"$LSP_OUT"; then lsp_bad "LSP_script: an @expect line of the script failed"; else lsp_ok "LSP_script: every @expect_objects line held"; fi
lsp_case() { awk -v n="$1" '/Command: Loft( |$)/ { c++ } c == n' <<<"$LSP_OUT"; }
lsp_in() { awk -v v="${1:-}" -v lo="$2" -v hi="$3" 'BEGIN { exit !(v != "" && v + 0 >= lo && v + 0 <= hi) }'; }
lsp_has() { if grep -qF -- "$2" <<<"$1"; then lsp_ok "$3"; else lsp_bad "$3"; near "$1" "$2"; fi; }
lsp_hasnot() { if grep -qF -- "$2" <<<"$1"; then lsp_bad "$3"; near "$1" "$2"; else lsp_ok "$3"; fi; }
lsp_num() {
  local v
  v="$({ grep -oE -- "$2[0-9.e+-]+" <<<"$1" || true; } | head -1 | grep -oE '[-0-9.e+]+$' || true)"
  if lsp_in "$v" "$3" "$4"; then lsp_ok "$5 ($v in $3..$4)"; else lsp_bad "$5 ('$v' not in $3..$4)"; fi
}
LSP_1="$(lsp_case 1)"; LSP_2="$(lsp_case 2)"; LSP_3="$(lsp_case 3)"; LSP_4="$(lsp_case 4)"
lsp_has "$LSP_1" "every section reproduced" "LSP_1: Spacing=Uniform builds the exact kernel loft through the three circles"
lsp_has "$LSP_1" "Spacing=Uniform station parameters" "LSP_1: ...and the message names the spacing"
lsp_num "$LSP_1" "Area = " 50.35 51.37 "LSP_1: lateral area = integral 2 pi r sqrt(r'^2 + z'^2) dv = 50.8603 (stations 0, 1/2, 1)"
lsp_has "$LSP_2" "every section reproduced" "LSP_2_control: the default style still builds the exact kernel loft"
lsp_hasnot "$LSP_2" "Spacing=" "LSP_2_control: ...and prints no Spacing text"
lsp_num "$LSP_2" "Area = " 44 49.5 "LSP_2_control: chord stations give a clearly different area (below 49.5 against Uniform's 50.86)"
lsp_has "$LSP_3" "Spacing=Centripetal station parameters" "LSP_3: Spacing=Centripetal builds and names the spacing"
lsp_num "$LSP_3" "Area = " 44 50.2 "LSP_3: ...its area differs from Uniform's 50.86 (below 50.2)"
lsp_has "$LSP_4" "caps not made" "LSP_4: Uniform with caps left on is refused for the caps"
lsp_has "$LSP_4" "parallel to the sweep direction" "LSP_4: ...WITH the kernel's reason (cap plane parallel to the sweep at v = 0)"
lsp_has "$LSP_4" "every section reproduced" "LSP_4: ...and the uncapped wall is still built through every section"
