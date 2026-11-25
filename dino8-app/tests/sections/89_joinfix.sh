# ---------------------------------------------------------------------------
# JOINFIX_: Join leaves no deleted edge/vertex slots, so List counts the real edges and Check reports the polysurface valid
# (joinfix_script.txt; the cause and the closed forms are in the script's header). Controls in the same run: ShowEdges (which always
# skipped deleted slots) must agree with List.
# ---------------------------------------------------------------------------
jnf_ok() { echo "ok   $1"; }
jnf_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  JNF_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/joinfix_script.txt" 2>&1)" || { echo "$JNF_OUT" | tail -40; echo "FAIL JOINFIX_run: script exited non-zero"; fail=1; JNF_OUT=""; }
else
  JNF_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/joinfix_script.txt" 2>&1)" || { echo "$JNF_OUT" | tail -40; echo "FAIL JOINFIX_run: script exited non-zero"; fail=1; JNF_OUT=""; }
fi
jnf_block() { awk -v name="$1" '$0 ~ ("Unknown command: JNFBEGIN_" name "$") { on = 1; next } on && /Unknown command: JNFBEGIN_|Unknown command: JNFEND/ { exit } on { print }' <<<"$JNF_OUT"; }
jnf_has() { if grep -qF -- "$2" <<<"$1"; then jnf_ok "$3"; else jnf_bad "$3"; near "$1" "$2"; fi; }
jnf_hasnot() { if grep -qF -- "$2" <<<"$1"; then jnf_bad "$3"; near "$1" "$2"; else jnf_ok "$3"; fi; }
JNF_Q="$(jnf_block quads)"; JNF_C="$(jnf_block cube)"
# --- two joined quads
jnf_has "$JNF_Q" "Joined 2 surfaces into one polysurface" "JOINFIX_quads: Join makes one polysurface of the two SrfPt quads"
jnf_has "$JNF_Q" "  2 faces, 7 edges, open" "JOINFIX_quads: List counts 7 edges (4 + 4 - 1 shared), not 8 with a dead slot"
jnf_has "$JNF_Q" "ShowEdges: 1 object(s), 7 edge(s), 6 naked edge(s)" "JOINFIX_quads: ShowEdges agrees: 7 edges, 6 naked"
jnf_has "$JNF_Q" ": valid" "JOINFIX_quads: Check reports the joined quads valid"
jnf_hasnot "$JNF_Q" "INVALID" "JOINFIX_quads: ...and nothing INVALID"
# --- a cube exploded and joined back
jnf_has "$JNF_C" "Joined 6 surfaces into one polysurface" "JOINFIX_cube: Join makes one polysurface of the six exploded faces"
jnf_has "$JNF_C" "  6 faces, 12 edges, closed solid" "JOINFIX_cube: List counts 12 edges (Euler 8 - 12 + 6 = 2), not 24 with 12 dead slots, and it is a closed solid"
jnf_has "$JNF_C" "ShowEdges: 1 object(s), 12 edge(s), 0 naked edge(s)" "JOINFIX_cube: ShowEdges agrees: 12 edges, none naked"
jnf_has "$JNF_C" ": valid" "JOINFIX_cube: Check reports the joined cube valid"
jnf_hasnot "$JNF_C" "INVALID" "JOINFIX_cube: ...and nothing INVALID"
if grep -q "gl_error=0" <<<"$JNF_OUT"; then jnf_ok "JOINFIX script ran without OpenGL errors"; else jnf_bad "JOINFIX script ran without OpenGL errors"; fi
