# MSHW_: STL / PLY import welds coincident vertices (mshw_script.txt). The fixtures are a 10 x 10 x 10 cube written the way a
# tessellator writes a solid: every triangle (STL) or every face (PLY) carries its own copy of each corner, so the file has no
# shared vertex list. Hand-computed oracles: 24 PLY vertices (36 STL triangle corners) weld to the cube's 8; volume 1000, area
# 600. Controls: a PLY that carries per-vertex normals marks deliberately split vertices and must NOT be welded (24 stay); a
# corner copy 0.001 away from its two partners is a real gap, not float rounding, and must stay separate (one fewer weld, 9
# vertices) - a true distance test at 1e-6 of the bounding-box diagonal, not a grid snap. These fixtures are not exported by
# the app, so they exercise the import weld itself; the round trip of an exported solid (RTA_) no longer does.
mshw_ok() { echo "ok   $1"; }
mshw_bad() { echo "FAIL $1"; fail=1; }
sed -e "s|@HERE@|$HEREW|g" -e "s|@TMP@|$TMPW|g" "$HERE/mshw_script.txt" > "$TMPW/mshw_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  MSHW_OUT="$(timeout 300 "$BIN" --smoke 30 --script "$TMPW/mshw_script.txt" 2>&1)" || { echo "$MSHW_OUT" | tail -30; echo "FAIL MSHW_run: script exited non-zero"; fail=1; MSHW_OUT=""; }
else
  MSHW_OUT="$(timeout 300 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 30 --script "$TMPW/mshw_script.txt" 2>&1)" || { echo "$MSHW_OUT" | tail -30; echo "FAIL MSHW_run: script exited non-zero"; fail=1; MSHW_OUT=""; }
fi
mshw_block() { awk -v n="MSHWB_$1" 'index($0, "Unknown command: " n) { on = 1; next } on && /Unknown command: MSHWB_/ { exit } on' <<<"$MSHW_OUT"; }
mshw_has() { if grep -qF -- "$2" <<<"$1"; then mshw_ok "$3"; else mshw_bad "$3"; near "$1" "$2"; fi; }
mshw_hasnot() { if [ -z "$1" ] || grep -qF -- "$2" <<<"$1"; then mshw_bad "$3"; else mshw_ok "$3"; fi; }  # an empty block (the script died earlier) must not pass
A="$(mshw_block ply_split)"
mshw_has "$A" "PLY: welded 16 coincident vertices" "MSHW_ply_split: 24 per-face corner points weld to the cube's 8 (16 welded)"
mshw_has "$A" "8 vertices, 12 faces" "MSHW_ply_split: the welded mesh has 8 vertices and 12 triangles"
mshw_has "$A" "Volume = 1000 " "MSHW_ply_split: the welded cube has Volume 10^3 = 1000"
mshw_has "$A" "Area = 600 " "MSHW_ply_split: ...and Area 6 x 10^2 = 600"
A="$(mshw_block ply_normals)"
mshw_hasnot "$A" "welded" "MSHW_ply_normals: a PLY that carries its own vertex normals is not welded"
mshw_has "$A" "24 vertices, 12 faces" "MSHW_ply_normals: all 24 split vertices are kept"
A="$(mshw_block ply_gap)"
mshw_has "$A" "PLY: welded 15 coincident vertices" "MSHW_ply_gap: a corner copy 0.001 off its partners is NOT welded (15 welded, not 16)"
mshw_has "$A" "9 vertices, 12 faces" "MSHW_ply_gap: ...so the mesh keeps 9 vertices"
A="$(mshw_block stl_split)"
mshw_has "$A" "STL: welded 28 coincident vertices of 12 triangles (mesh is closed and manifold)" "MSHW_stl_split: 36 triangle corners weld to 8 (28 welded) and the mesh is closed and manifold"
mshw_has "$A" "8 vertices, 12 faces" "MSHW_stl_split: the welded mesh has 8 vertices and 12 triangles"
mshw_has "$A" "Volume = 1000 " "MSHW_stl_split: Volume 1000"
mshw_has "$A" "Area = 600 " "MSHW_stl_split: Area 600"
