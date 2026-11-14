# TNF_: ToNURBS FitInterior=Yes (tonurbs_fit_script.txt). Default No keeps every existing ToNURBS pin; FitInterior=Yes fits the irregular patches' edge
# curves and interior control points to the limit surface (kernel SubD::ToNurbsPatchesWatertight(fit_interior)) and prints the measured deviation of both
# interiors against the same independent reference (exact limit points of the cage refined 2 more rounds), the fitted patch count, and the usual watertight
# summary. Oracles: the closed-form valence-3 corner limit point (2.5, 2.5, 2.5) / (7.5, ...) of the cube [0,10]^3 (read back with SubDVertexInfo; the
# polysurface's corners are checked in the kernel test), and the kernel's own dense measurement (default 0.0056, fitted 0.0014; here 5 x 5 samples per patch: default 0.0072, fitted 0.0007).
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  TNF="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/tonurbs_fit_script.txt" 2>&1)" || { echo "$TNF"; echo "FAIL: tonurbs fit script exited non-zero"; exit 1; }
else
  TNF="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/tonurbs_fit_script.txt" 2>&1)" || { echo "$TNF"; echo "FAIL: tonurbs fit script exited non-zero"; exit 1; }
fi
tnf_ok() { echo "ok   $1"; }
tnf_bad() { echo "FAIL $1"; fail=1; }
tnfblock() { awk -v name="$1" '$0 ~ ("Unknown command: TNFBEGIN_" name "$") { on = 1; next } on && /Unknown command: TNFBEGIN_/ { exit } on { print }' <<<"$TNF"; }
tnfhas() { if grep -qF -- "$2" <<<"$(tnfblock "$1")"; then tnf_ok "$3"; else tnf_bad "$3"; echo "     expected: $2"; { grep -E "^history: ToNURBS" <<<"$(tnfblock "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi; }
# tnfnum CASE REGEX-WITH-ONE-GROUP: the number captured by the group on the case's lines
tnfnum() { { grep -oE "$2" <<<"$(tnfblock "$1")" || true; } | head -1 | sed -E "s/$2/\\1/"; }
tnflt() { if [ -n "$1" ] && awk -v v="$1" -v b="$2" 'BEGIN { exit !(v < b) }'; then tnf_ok "$3 (measured $1 < $2)"; else tnf_bad "$3 (measured '${1:-none}', expected < $2)"; fi; }
tnfgt() { if [ -n "$1" ] && awk -v v="$1" -v b="$2" 'BEGIN { exit !(v > b) }'; then tnf_ok "$3 (measured $1 > $2)"; else tnf_bad "$3 (measured '${1:-none}', expected > $2)"; fi; }
# tnfvertices CASE: the 8 SubDVertexInfo lines of the cube corners read back the closed-form limit point (2.5 for a 0 coordinate, 7.5 for a 10)
tnfvertices() {
  local n=0 bad=0 line
  while IFS= read -r line; do
    n=$((n + 1))
    if ! awk -v l="$line" 'BEGIN {
      if (match(l, /vertex \([^)]*\)/) == 0) exit 1; v = substr(l, RSTART + 8, RLENGTH - 9); split(v, a, /, */)
      if (match(l, /limit point \([^)]*\)/) == 0) exit 1; p = substr(l, RSTART + 13, RLENGTH - 14); split(p, b, /, */)
      for (i = 1; i <= 3; i++) { e = (a[i] + 0 == 0) ? 2.5 : 7.5; d = b[i] - e; if (d < 0) d = -d; if (d > 1e-3) exit 1 }
      exit 0 }'; then bad=$((bad + 1)); fi
  done < <({ grep -E "SubDVertexInfo: object" <<<"$(tnfblock "$1")" || true; })
  if [ "$n" = 8 ] && [ "$bad" = 0 ]; then tnf_ok "TNF_$1: SubDVertexInfo reads the closed-form limit point back at all 8 cube corners (2.5 / 7.5, 8 lines)"; else tnf_bad "TNF_$1: $n SubDVertexInfo line(s), $bad off the closed-form corner limit point (2.5 / 7.5)"; fi
}

# ---- default: no fit, no fit line
tnfhas default "ToNURBS: watertight conversion of a 6-face SubD (2 Catmull-Clark refinement round(s) first): 96 bicubic patches" "TNF_default: Levels=2 converts the 6-face cube through 2 rounds into 96 patches"
if grep -qF "FitInterior=Yes" <<<"$(tnfblock default)"; then tnf_bad "TNF_default: the fit line is printed although FitInterior was not requested"; else tnf_ok "TNF_default: no FitInterior line (default No keeps the old output)"; fi

# ---- FitInterior=Yes at Levels=2
tnfhas fit "0 naked edge(s) left, closed solid: yes, manifold" "TNF_fit: the fitted polysurface is still a closed manifold solid with 0 naked edges (boundaries shared exactly)"
tnfhas fit "ToNURBS FitInterior=Yes: 24 of 24 irregular patch(es) fitted to the limit surface" "TNF_fit: all 24 irregular patches (3 around each of the 8 valence-3 corners) are fitted"
tnfvertices fit
FIT_DEV="$(tnfnum fit 'fitted ([0-9.eE+-]+), default Coons')"
DEF_DEV="$(tnfnum fit 'default Coons interior ([0-9.eE+-]+)')"
tnflt "$FIT_DEV" 0.0015 "TNF_fit: the fitted deviation at Levels=2 against the limit-point reference"
tnfgt "$DEF_DEV" 0.0050 "TNF_fit: the default Coons interior deviation at Levels=2 against the same reference"
if [ -n "$FIT_DEV" ] && [ -n "$DEF_DEV" ] && awk -v f="$FIT_DEV" -v d="$DEF_DEV" 'BEGIN { exit !(f * 4 <= d) }'; then tnf_ok "TNF_fit: the fit is at least 4x closer than the Coons interior ($FIT_DEV vs $DEF_DEV)"; else tnf_bad "TNF_fit: expected the fit at least 4x closer ($FIT_DEV vs $DEF_DEV)"; fi
# the default run's own watertight line reports its own (older) deviation figure, which the fit run must not change for the default path
DEFAULT_PRINTED="$(tnfnum default 'limit reference ([0-9.eE+-]+)')"
if [ -n "$DEFAULT_PRINTED" ]; then tnf_ok "TNF_default: the existing deviation line is still printed ($DEFAULT_PRINTED)"; else tnf_bad "TNF_default: the existing 'limit reference' deviation line is missing"; fi

# ---- automatic levels (6-face cage -> 3 rounds)
tnfhas fit_auto "ToNURBS: watertight conversion of a 6-face SubD (3 Catmull-Clark refinement round(s) first): 384 bicubic patches" "TNF_fit_auto: without Levels the 6-face cage is refined 3 rounds as before"
tnfhas fit_auto "ToNURBS FitInterior=Yes: 24 of 24 irregular patch(es) fitted" "TNF_fit_auto: the 24 irregular patches are fitted at the automatic level too"
AUTO_FIT="$(tnfnum fit_auto 'fitted ([0-9.eE+-]+), default Coons')"
AUTO_DEF="$(tnfnum fit_auto 'default Coons interior ([0-9.eE+-]+)')"
if [ -n "$AUTO_FIT" ] && [ -n "$AUTO_DEF" ] && awk -v f="$AUTO_FIT" -v d="$AUTO_DEF" 'BEGIN { exit !(f * 4 <= d) }'; then tnf_ok "TNF_fit_auto: fitted $AUTO_FIT vs default $AUTO_DEF at 3 rounds (kernel measurement 0.00013 vs 0.00146): at least 4x closer"; else tnf_bad "TNF_fit_auto: expected the fit at least 4x closer ($AUTO_FIT vs $AUTO_DEF)"; fi
