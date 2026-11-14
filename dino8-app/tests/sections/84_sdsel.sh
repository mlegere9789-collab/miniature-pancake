# SDS_: SelSubDEdges Type=... and SelSubDVertices Type=... Valence=... select a SubD's control-net edges / vertices by type (sdsel_script.txt).
# Failing-first: before this change SelSubDVertices did not exist ("Unknown command") and SelSubDEdges took no Type option, always selecting the
# crease edges and printing only "SelSubDEdges: N sub-object(s) added", never the "N <type> edge(s) on M SubD(s)" line these checks read.
# Every expected count is derived by hand from the fixture's topology, not from the app's output:
#   cube cage:   8 vertices (each valence 3, Smooth), 12 edges, 6 faces
#   two edges creased at the origin: those 2 edges are Crease and the other 10 Smooth; the origin has two crease edges (Crease), the far
#       ends of the two edges one each (Dart), the other 5 vertices none (Smooth), no Corner
#   origin pinned as a corner: Corner 1, Crease 0, Dart 2, Smooth 5; the 2 crease edges stay
#   SubDPlane 20 x 20: a 5 x 5 vertex grid, 16 vertices on the boundary (all Crease: exactly two boundary edges), 9 interior (Smooth);
#       the 16 boundary edges are tagged Crease by the SubD and are also the Boundary edges; the 4 x 4 cells are 32 triangles (each cell split by
#       one diagonal, whichever way), so Euler V - E + F = 1 gives E = 25 + 32 - 1 = 56 edges, 56 - 16 = 40 of them Smooth;
#       valences: grid edges give a corner 2, a boundary vertex 3, an interior vertex 4, and each diagonal adds 1 to its two ends:
#       2 corners lie on a diagonal (valence 3) and 2 do not (valence 2); every one of the 12 other boundary vertices lies on exactly one diagonal (valence 4);
#       the 9 interior vertices lie on two diagonals each (valence 6). Check: 2*2 + 2*3 + 12*4 + 9*6 = 112 = 2 * 56.
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  SDSO="$(timeout 900 "$BIN" --smoke 60 --script "$HERE/sdsel_script.txt" 2>&1)" || { echo "$SDSO"; echo "FAIL: sdsel script exited non-zero"; exit 1; }
else
  SDSO="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 60 --script "$HERE/sdsel_script.txt" 2>&1)" || { echo "$SDSO"; echo "FAIL: sdsel script exited non-zero"; exit 1; }
fi
sds_ok() { echo "ok   $1"; }
sds_bad() { echo "FAIL $1"; fail=1; }
sdsblock() { awk -v name="$1" '$0 ~ ("Unknown command: SDSBEGIN_" name "$") { on = 1; next } on && /Unknown command: SDSBEGIN_/ { exit } on { print }' <<<"$SDSO"; }
# sdsn CASE KIND TYPE [VALENCE]: the count printed by the selection command of that kind and type, or empty
sdsn() {
  local kind="$2" type="$3" val="${4:-}" pat
  if [ "$kind" = edges ]; then pat="SelSubDEdges: ([0-9]+) $type edge\\(s\\) on [0-9]+ SubD\\(s\\)"
  elif [ -n "$val" ]; then pat="SelSubDVertices: ([0-9]+) $type vertex/vertices of valence $val on [0-9]+ SubD\\(s\\)"
  else pat="SelSubDVertices: ([0-9]+) $type vertex/vertices on [0-9]+ SubD\\(s\\)"; fi
  { grep -E -- "$pat" <<<"$(sdsblock "$1")" || true; } | head -1 | sed -E "s#.*$pat.*#\\1#"
}
# sdscheck CASE KIND TYPE VALENCE EXPECTED DESC
sdscheck() {
  local got; got="$(sdsn "$1" "$2" "$3" "$4")"
  if [ "$got" = "$5" ]; then sds_ok "$6 ($got, hand-derived $5)"; else sds_bad "$6 (counted '${got:-none}', hand-derived $5)"; { grep -E "SelSubD(Edges|Vertices)" <<<"$(sdsblock "$1")" || true; } | head -3 | sed 's/^/     actual:   /'; fi
}
sdshas() { if grep -qF -- "$2" <<<"$(sdsblock "$1")"; then sds_ok "$3"; else sds_bad "$3"; echo "     expected: $2"; fi; }

# ---- plain cube
sdscheck cube vertices all "" 8 "SDS_cube: every one of the 8 vertices"
sds_first="$({ grep -E "^(history: )?SelSubDVertices: [0-9]+ sub-object" <<<"$(sdsblock cube)" || true; } | head -1)"
if grep -qF -- "SelSubDVertices: 8 sub-object(s) added, 8 selected" <<<"$sds_first"; then sds_ok "SDS_cube: the first selection adds the 8 vertices to an empty sub-object selection"; else sds_bad "SDS_cube: the first selection adds the 8 vertices to an empty sub-object selection (got '${sds_first:-none}')"; fi
sdscheck cube vertices smooth "" 8 "SDS_cube: all 8 vertices are Smooth"
sdscheck cube vertices crease "" 0 "SDS_cube: no Crease vertex"
sdscheck cube vertices corner "" 0 "SDS_cube: no Corner vertex"
sdscheck cube vertices dart "" 0 "SDS_cube: no Dart vertex"
sdscheck cube vertices all 3 8 "SDS_cube: all 8 vertices have valence 3"
sdscheck cube vertices all 4 0 "SDS_cube: no vertex has valence 4"
sdscheck cube edges all "" 12 "SDS_cube: all 12 edges"
sdscheck cube edges smooth "" 12 "SDS_cube: all 12 edges are Smooth"
sdscheck cube edges crease "" 0 "SDS_cube: no crease edge"
sdscheck cube edges boundary "" 0 "SDS_cube: no boundary edge on a closed SubD"
sdscheck cube edges soft "" 0 "SDS_cube: no soft (semi-sharp) edge"

# ---- two edges creased at the origin
sdscheck creased edges crease "" 2 "SDS_creased: the 2 creased edges"
sdscheck creased edges smooth "" 10 "SDS_creased: the other 10 edges are Smooth"
sdscheck creased edges all "" 12 "SDS_creased: all 12 edges"
sdscheck creased vertices crease "" 1 "SDS_creased: the origin, with two crease edges, is the one Crease vertex"
sdscheck creased vertices dart "" 2 "SDS_creased: the far ends of the two creased edges are Darts"
sdscheck creased vertices smooth "" 5 "SDS_creased: the other 5 vertices are Smooth"
sdscheck creased vertices corner "" 0 "SDS_creased: no Corner yet"

# ---- the origin pinned
sdscheck corner vertices corner "" 1 "SDS_corner: the pinned origin is the one Corner"
sdscheck corner vertices crease "" 0 "SDS_corner: no Crease vertex is left"
sdscheck corner vertices dart "" 2 "SDS_corner: the two Darts are unchanged"
sdscheck corner vertices smooth "" 5 "SDS_corner: the 5 Smooth vertices are unchanged"
sdscheck corner edges crease "" 2 "SDS_corner: the 2 crease edges stay"

# ---- open grid
sdscheck plane vertices all "" 25 "SDS_plane: 25 vertices"
sdscheck plane vertices crease "" 16 "SDS_plane: the 16 boundary vertices are Crease"
sdscheck plane vertices smooth "" 9 "SDS_plane: the 9 interior vertices are Smooth"
sdscheck plane vertices all 2 2 "SDS_plane: 2 vertices of valence 2"
sdscheck plane vertices all 3 2 "SDS_plane: 2 vertices of valence 3"
sdscheck plane vertices all 4 12 "SDS_plane: 12 vertices of valence 4"
sdscheck plane vertices all 6 9 "SDS_plane: 9 vertices of valence 6"
sdscheck plane edges all "" 56 "SDS_plane: 56 edges (Euler: 25 + 32 - 1)"
sdscheck plane edges boundary "" 16 "SDS_plane: 16 boundary edges"
sdscheck plane edges crease "" 16 "SDS_plane: the 16 boundary edges are tagged Crease"
sdscheck plane edges smooth "" 40 "SDS_plane: 56 - 16 = 40 Smooth edges"

# ---- two SubDs
sdscheck two vertices all "" 16 "SDS_two: 8 + 8 vertices of two selected SubDs"
sdshas two "16 all vertex/vertices on 2 SubD(s)" "SDS_two: the report names both SubDs"
sdscheck two edges all "" 24 "SDS_two: 12 + 12 edges of two selected SubDs"

# ---- refusals
sds_refusals="$(grep -cF -- "Select SubDs first" <<<"$(sdsblock refused)" || true)"
if [ "$sds_refusals" = 2 ]; then sds_ok "SDS_refused: Enter with nothing selected and Enter with only a Box selected are both refused with 'Select SubDs first' (2 refusals)"; else sds_bad "SDS_refused: expected 2 'Select SubDs first' refusals, found ${sds_refusals:-none}"; fi
