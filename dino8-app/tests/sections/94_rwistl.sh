# ---------------------------------------------------------------------------
# RWISTL: STL export and import (rwi_mesh_script.txt), the written files parsed by the stand-alone rwi_oracle.py (Python standard library only).
# Rows: BLENDER-WM-0053 (export: reading it back reproduces the content, a failed export leaves the previous file intact), BLENDER-WM-0054
# (import: content intact, a damaged file is reported, one undo step restores) and AUTOCAD-DWGFID-0091 (solids written to a stereolithography file).
# Expected values are closed forms of the construction (two 10 mm cubes, millimetre document):
#   all.stl  binary, the file size is exactly 84 + 50 x the facet count, every stored facet normal agrees with its winding (outward), bounding
#            box (0,0,0)-(30,10,10), signed volume 2 x 1000 = 2000 mm^3, a closed manifold mesh (every edge shared by two oppositely directed
#            facets once the corners are welded by position), a multiple of 12 facets (at least 24)
#   one.stl  only the selected box: bounding box (20,0,0)-(30,10,10), volume 1000
#   all.stl  after the failed export of a line to the same path: unchanged
#   rt.stl   the imported document exported again: the same facet count, bounding box and volume as all.stl; the import reports the solid check
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWISTL_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwistl"; mkdir -p "$RWI_D"
printf 'this is not an STL file' > "$RWI_D/bad.stl"
sed -e "s|@TMP@|$RWI_D|g" -e 's|@EXT@|stl|g' -e 's|@NOBJ@|1|g' "$HERE/rwi_mesh_script.txt" > "$TMPW/rwistl_script.txt"
rwi_app "$TMPW/rwistl_script.txt"
rwi_has "Exported $RWI_D/all.stl" "RWISTL: Export wrote all.stl"
rwi_has "Exported $RWI_D/one.stl" "RWISTL: Export of the selection wrote one.stl"
rwi_has "Nothing to export" "RWISTL: exporting a line reports that there is nothing to export"
rwi_has "Imported $RWI_D/all.stl" "RWISTL: Import read all.stl"
rwi_has "mesh is closed and manifold" "RWISTL: the import reports the solid check (closed and manifold)"
rwi_has "Exported $RWI_D/rt.stl" "RWISTL: the re-imported document exported again"
rwi_oracle "RWISTL: all.stl parsed independently: binary size 84+50n, outward normals, bbox (0,0,0)-(30,10,10), volume 2000, closed" stl "$RWI_D/all.stl" --min 0,0,0 --max 30,10,10 --vol 2000 --closed --tris-multiple-of 12
rwi_oracle "RWISTL: one.stl holds only the selected box: bbox (20,0,0)-(30,10,10), volume 1000" stl "$RWI_D/one.stl" --min 20,0,0 --max 30,10,10 --vol 1000 --closed --tris-multiple-of 12
rwi_oracle "RWISTL: rt.stl (import then export) equals all.stl" stl "$RWI_D/rt.stl" --same-as "$RWI_D/all.stl" --closed
rwi_has "Could not read a mesh from $RWI_D/bad.stl" "RWISTL: a damaged .stl is refused with a message"
fi
