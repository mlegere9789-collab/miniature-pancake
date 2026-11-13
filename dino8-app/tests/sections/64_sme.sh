# ---------------------------------------------------------------------------
# SME_: RemoveAllSharedMicroEdges (sme_script.txt) - Brep::RemoveSharedMicroEdge from the app, RemoveAllNakedMicroEdges' sibling for
# the 2-trim (interior) edges. Fixture: the kernel test's two quads (areas 6 and 24, each a triangle with one corner split into two
# points 1e-4 apart; one SrfPt surface each) joined along the micro edge: 2 faces, 7 edges, area 30 by hand (triangles 4 x 3 / 2 and
# 6 x 8 / 2). Counts are topological and need no tolerance: 7 edges before, 6 after, 2 faces throughout; the area stays 30 (the mesh
# figure is read within 1 percent) because only the numerical gap closes. A first fixture, a box edge split twice, was wrong: both
# faces then share the same loop neighbour and the kernel declines it. A second one, Polyline + PlanarSrf + Join, never built a
# polysurface at all: PlanarSrf refuses a closed polyline with a 1e-4 segment at tolerance 0.001 ("cross themselves") and, where it
# accepts a polyline, gives the face ONE edge for its whole boundary, so the commands ran on the two curves (Area 0, "no shared
# edges ... found"). Each quad is now one SrfPt surface, whose four sides Join turns into four edges. Controls: the naked sibling
# leaves the fixture alone, a plain box has nothing to close, a second run finds nothing, Undo restores the 7-edge fixture in one step.
# Where the 7 is read: List prints the edge SLOTS of a polysurface and Join leaves the edge it merged away behind as a deleted slot
# (List says 8 for these 7 edges), so the fixture's edges are counted by ShowEdges, which skips deleted slots and counts the naked
# edges too (7 edges, 6 naked = exactly one shared edge). The kernel method compacts, so after the command List's "6 edges" is the
# real count and ShowEdges agrees.
# NOT checked here: edges the kernel refuses, several objects at once.
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
# sme_area BLOCK DESC: the block's last "Area = " lies in 29.7..30.3 (two triangles, 6 + 24, read from the display mesh)
sme_area() {
  local v
  v="$({ grep -oE "Area = [0-9.e+-]+" <<<"$1" || true; } | tail -1 | grep -oE '[-0-9.e+]+$' || true)"
  if [ -n "$v" ] && awk -v v="$v" 'BEGIN { exit !(v >= 29.7 && v <= 30.3) }'; then sme_ok "$2 (Area $v in 29.7..30.3, the hand figure 30)"; else sme_bad "$2 (Area '${v:-none}' not in 29.7..30.3)"; fi
}
sme_vol() {
  local v
  v="$({ grep -oE "Volume = [0-9.e+-]+" <<<"$1" || true; } | tail -1 | grep -oE '[-0-9.e+]+$' || true)"
  if [ -n "$v" ] && awk -v v="$v" 'BEGIN { exit !(v >= 995 && v <= 1005) }'; then sme_ok "$2 (Volume $v in 995..1005, the box's 1000)"; else sme_bad "$2 (Volume '${v:-none}' not in 995..1005)"; fi
}
SME_S="$(sme_block split)"; SME_C="$(sme_block close)"; SME_T="$(sme_block twice)"; SME_N="$(sme_block none)"; SME_U="$(sme_block undo)"
if grep -q "^FAIL" <<<"$SME_OUT"; then sme_bad "SME_script: an @expect line of the script failed"; else sme_ok "SME_script: ran"; fi
# --- the fixture and the control: the naked sibling leaves a shared micro edge alone
sme_has "$SME_S" "Joined 2 surfaces into one polysurface" "SME_split: Join makes one polysurface of the two SrfPt quads"
sme_has "$SME_S" "  2 faces, " "SME_split: the two joined quads are 2 faces..."
sme_has "$SME_S" "ShowEdges: 1 object(s), 7 edge(s), 6 naked edge(s)" "SME_split: ...and 7 edges, 6 of them naked, so one shared micro edge (the fixture)"
sme_area "$SME_S" "SME_split: ...area 6 + 24"
sme_has "$SME_S" "RemoveAllNakedMicroEdges: no naked edges shorter than" "SME_split: ...and the NAKED command finds nothing to close (the micro edge is shared)"
# --- the new command closes it
sme_has "$SME_C" "RemoveAllSharedMicroEdges: 1 shared micro edge(s) removed (the endpoints merged, each of the two faces' own loop re-trimmed to close the gap)" "SME_close: one shared micro edge removed, with the kernel-route wording"
sme_has "$SME_C" "2 faces, 6 edges" "SME_close: 6 edges left, still 2 faces (each a triangle)"
sme_has "$SME_C" "ShowEdges: 1 object(s), 6 edge(s), 6 naked edge(s)" "SME_close: ...all 6 naked, no shared edge is left"
sme_area "$SME_C" "SME_close: only the numerical gap closed"
sme_has "$SME_C" "valid" "SME_close: Check valid"
sme_hasnot "$SME_C" "INVALID" "SME_close: ...and Check reports nothing INVALID"
# --- a second run
sme_has "$SME_T" "RemoveAllSharedMicroEdges: 1 shared micro edge(s) removed" "SME_twice: the first run closes the micro edge"
sme_has "$SME_T" "RemoveAllSharedMicroEdges: no shared edges shorter than" "SME_twice: the second run finds nothing left"
# --- a plain box
sme_has "$SME_N" "RemoveAllSharedMicroEdges: no shared edges shorter than" "SME_none: a plain box has no shared micro edge"
sme_has "$SME_N" "6 faces, 12 edges, closed solid" "SME_none: ...and stays at 12 edges"
sme_vol "$SME_N" "SME_none: volume untouched"
# --- undo
sme_has "$SME_U" "RemoveAllSharedMicroEdges: 1 shared micro edge(s) removed" "SME_undo: the command closed the micro edge before the Undo"
sme_has "$SME_U" "  2 faces, " "SME_undo: after one Undo the polysurface still has 2 faces..."
sme_has "$SME_U" "ShowEdges: 1 object(s), 7 edge(s), 6 naked edge(s)" "SME_undo: ...and one Undo restores the 7-edge fixture"
sme_hasnot "$SME_U" "6 edges" "SME_undo: ...no 6-edge state is left over"
sme_area "$SME_U" "SME_undo: ...and the area is the fixture's again"
if grep -q "gl_error=0" <<<"$SME_OUT"; then sme_ok "SME script ran without OpenGL errors"; else sme_bad "SME script ran without OpenGL errors"; fi
