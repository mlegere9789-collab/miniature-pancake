# ---------------------------------------------------------------------------
# SME_: RemoveAllSharedMicroEdges (sme_script.txt) - Brep::RemoveSharedMicroEdge from the app, RemoveAllNakedMicroEdges' sibling for
# the 2-trim (interior) edges. Fixture: a 10 x 10 x 10 box (12 edges) whose vertical edge is split twice 0.01 apart (SplitEdge):
# 14 edges, the 0.01 piece between the two new vertices borders the same two faces. Counts are topological and need no tolerance:
# 14 edges before, 13 after, 6 faces and "closed solid" throughout; the volume stays the box's 1000 (the mesh figure is read
# within 0.5 percent) because only the numerical gap closes, never material. Controls: the naked sibling leaves the fixture alone,
# a plain box has nothing to close, a second run finds nothing, Undo restores the 14-edge fixture in one step.
# NOT checked here: edges the kernel refuses (a face left with fewer than four trims), several objects at once.
# ---------------------------------------------------------------------------
sme_ok() { echo "ok   $1"; }
sme_bad() { echo "FAIL $1"; fail=1; }
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SME_OUT="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sme_script.txt" 2>&1)" || { echo "$SME_OUT" | tail -40; echo "FAIL SME_run: script exited non-zero"; fail=1; SME_OUT=""; }
else
  SME_OUT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sme_script.txt" 2>&1)" || { echo "$SME_OUT" | tail -40; echo "FAIL SME_run: script exited non-zero"; fail=1; SME_OUT=""; }
fi
sme_block() { awk -v name="$1" '$0 ~ ("Unknown command: SMEBEGIN_" name "$") { on = 1; next } on && /Unknown command: SMEBEGIN_/ { exit } on { print }' <<<"$SME_OUT"; }
sme_has() { if grep -qF -- "$2" <<<"$1"; then sme_ok "$3"; else sme_bad "$3"; near "$1" "$2"; fi; }
sme_hasnot() { if grep -qF -- "$2" <<<"$1"; then sme_bad "$3"; near "$1" "$2"; else sme_ok "$3"; fi; }
sme_vol() {
  local v
  v="$({ grep -oE "Volume = [0-9.e+-]+" <<<"$1" || true; } | tail -1 | grep -oE '[-0-9.e+]+$' || true)"
  if [ -n "$v" ] && awk -v v="$v" 'BEGIN { exit !(v >= 995 && v <= 1005) }'; then sme_ok "$2 (Volume $v in 995..1005, the box's 1000)"; else sme_bad "$2 (Volume '${v:-none}' not in 995..1005)"; fi
}
SME_S="$(sme_block split)"; SME_C="$(sme_block close)"; SME_T="$(sme_block twice)"; SME_N="$(sme_block none)"; SME_U="$(sme_block undo)"
if grep -q "^FAIL" <<<"$SME_OUT"; then sme_bad "SME_script: an @expect line of the script failed"; else sme_ok "SME_script: ran"; fi
# --- the fixture and the control: the naked sibling leaves a shared micro edge alone
sme_has "$SME_S" "6 faces, 14 edges, closed solid" "SME_split: two SplitEdge cuts leave the box with 14 edges (the fixture)"
sme_has "$SME_S" "RemoveAllNakedMicroEdges: no naked edges shorter than" "SME_split: ...and the NAKED command finds nothing to close (the micro edge is shared)"
sme_has "$SME_S" "Object 1: valid" "SME_split: Check valid"
# --- the new command closes it
sme_has "$SME_C" "RemoveAllSharedMicroEdges: 1 shared micro edge(s) removed (the endpoints merged, each of the two faces' own loop re-trimmed to close the gap)" "SME_close: one shared micro edge removed, with the kernel-route wording"
sme_has "$SME_C" "6 faces, 13 edges, closed solid" "SME_close: 13 edges left, still 6 faces and a closed solid"
sme_has "$SME_C" "Object 1: valid" "SME_close: Check valid"
sme_vol "$SME_C" "SME_close: only the numerical gap closed"
# --- a second run
sme_has "$SME_T" "RemoveAllSharedMicroEdges: no shared edges shorter than" "SME_twice: the second run finds nothing left"
# --- a plain box
sme_has "$SME_N" "RemoveAllSharedMicroEdges: no shared edges shorter than" "SME_none: a plain box has no shared micro edge"
sme_has "$SME_N" "6 faces, 12 edges, closed solid" "SME_none: ...and stays at 12 edges"
sme_vol "$SME_N" "SME_none: volume untouched"
# --- undo
sme_has "$SME_U" "6 faces, 14 edges, closed solid" "SME_undo: one Undo restores the 14-edge fixture"
sme_hasnot "$SME_U" "13 edges" "SME_undo: ...no 13-edge state is left over"
if grep -q "gl_error=0" <<<"$SME_OUT"; then sme_ok "SME script ran without OpenGL errors"; else sme_bad "SME script ran without OpenGL errors"; fi
