# ---------------------------------------------------------------------------
# RWIPLY: PLY export and import (rwi_mesh_script.txt), the written files parsed by the stand-alone rwi_oracle.py (Python standard library only).
# Rows: BLENDER-WM-0026 (export: reading it back reproduces the content, a failed export leaves the previous file intact) and BLENDER-WM-0027
# (import: content intact, a damaged file is reported, one undo step restores).
# Expected values are closed forms of the construction (two 10 mm cubes, millimetre document):
#   all.ply  header and body parsed by element / property declarations (every face index in range, no unread bytes), bounding box (0,0,0)-(30,10,10),
#            signed volume 2000 mm^3, closed manifold mesh, a multiple of 12 triangles (at least 24)
#   one.ply  only the selected box: bounding box (20,0,0)-(30,10,10), volume 1000
#   all.ply  after the failed export of a line to the same path: unchanged
#   rt.ply   the imported document exported again: the same triangle count, bounding box and volume as all.ply
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIPLY_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiply"; mkdir -p "$RWI_D"
printf 'this is not a PLY file\n' > "$RWI_D/bad.ply"
sed -e "s|@TMP@|$RWI_D|g" -e 's|@EXT@|ply|g' -e 's|@NOBJ@|1|g' "$HERE/rwi_mesh_script.txt" > "$TMPW/rwiply_script.txt"
rwi_app "$TMPW/rwiply_script.txt"
rwi_has "Exported $RWI_D/all.ply" "RWIPLY: Export wrote all.ply"
rwi_has "Exported $RWI_D/one.ply" "RWIPLY: Export of the selection wrote one.ply"
rwi_has "Nothing to export" "RWIPLY: exporting a line reports that there is nothing to export"
rwi_has "Imported $RWI_D/all.ply" "RWIPLY: Import read all.ply"
rwi_has "Exported $RWI_D/rt.ply" "RWIPLY: the re-imported document exported again"
rwi_oracle "RWIPLY: all.ply parsed independently: bbox (0,0,0)-(30,10,10), volume 2000, closed" ply "$RWI_D/all.ply" --min 0,0,0 --max 30,10,10 --vol 2000 --closed --tris-multiple-of 12
rwi_oracle "RWIPLY: one.ply holds only the selected box: bbox (20,0,0)-(30,10,10), volume 1000" ply "$RWI_D/one.ply" --min 20,0,0 --max 30,10,10 --vol 1000 --closed --tris-multiple-of 12
rwi_oracle "RWIPLY: rt.ply (import then export) equals all.ply" ply "$RWI_D/rt.ply" --same-as "$RWI_D/all.ply" --closed
rwi_has "Not a PLY file: $RWI_D/bad.ply" "RWIPLY: a damaged .ply is refused with a message"
fi
