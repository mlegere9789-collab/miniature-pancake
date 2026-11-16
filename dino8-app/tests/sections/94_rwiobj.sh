# ---------------------------------------------------------------------------
# RWIOBJ: Wavefront OBJ export and import (rwi_mesh_script.txt), the written files parsed by the stand-alone rwi_oracle.py (Python standard
# library only, no code shared with the application). Rows: BLENDER-WM-0019 (export: reading it back reproduces the content, a failed export
# leaves the previous file intact) and BLENDER-WM-0020 (import: content intact, a damaged file is reported, one undo step restores).
# Expected values are closed forms of the construction (two 10 mm cubes, millimetre document):
#   all.obj  2 groups ("o"), bounding box (0,0,0)-(30,10,10), signed volume 2 x 1000 = 2000 mm^3 (outward winding), a closed manifold mesh
#            (every edge shared by two oppositely directed faces), a multiple of 12 triangles (at least 24), Kd colours (1,0,0) and
#            (128/255, 64/255, 1) in the .mtl (written as the 0..1 value of the 8 bit channel)
#   one.obj  only the selected box: 1 group, bounding box (20,0,0)-(30,10,10), volume 1000
#   all.obj  after the failed export of a line to the same path: unchanged (still the two boxes)
#   rt.obj   the imported document exported again: the same triangle count, bounding box, volume and colours as all.obj (nothing lost)
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIOBJ_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiobj"; mkdir -p "$RWI_D"
printf 'this is not an OBJ file\nv 1 2\nf 1 2 3\n' > "$RWI_D/bad.obj"
sed -e "s|@TMP@|$RWI_D|g" -e 's|@EXT@|obj|g' -e 's|@NOBJ@|2|g' "$HERE/rwi_mesh_script.txt" > "$TMPW/rwiobj_script.txt"
rwi_app "$TMPW/rwiobj_script.txt"
rwi_has "Exported $RWI_D/all.obj" "RWIOBJ: Export wrote all.obj"
rwi_has "Exported $RWI_D/one.obj" "RWIOBJ: Export of the selection wrote one.obj"
rwi_has "Nothing to export" "RWIOBJ: exporting a line reports that there is nothing to export"
rwi_has "Imported $RWI_D/all.obj" "RWIOBJ: Import read all.obj"
rwi_has "Exported $RWI_D/rt.obj" "RWIOBJ: the re-imported document exported again"
rwi_oracle "RWIOBJ: all.obj parsed independently: 2 groups, bbox (0,0,0)-(30,10,10), volume 2000, closed, red and violet Kd" obj "$RWI_D/all.obj" --parts 2 --min 0,0,0 --max 30,10,10 --vol 2000 --closed --tris-multiple-of 12 --colors "1,0,0;0.501961,0.25098,1"
rwi_oracle "RWIOBJ: one.obj holds only the selected box: bbox (20,0,0)-(30,10,10), volume 1000" obj "$RWI_D/one.obj" --parts 1 --min 20,0,0 --max 30,10,10 --vol 1000 --closed
rwi_oracle "RWIOBJ: rt.obj (import then export) equals all.obj" obj "$RWI_D/rt.obj" --same-as "$RWI_D/all.obj" --colors "1,0,0;0.501961,0.25098,1" --closed
RWI_T="$(python3 -I "$HERE/rwi_oracle.py" obj "$RWI_D/all.obj" --json | head -1 | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["tris"])')"
[ "${RWI_T:-0}" -ge 24 ] && rwi_ok "RWIOBJ: all.obj has $RWI_T triangles (at least 12 per box)" || rwi_bad "RWIOBJ: all.obj has too few triangles ($RWI_T)"
rwi_has "Malformed .obj: bad v line in $RWI_D/bad.obj" "RWIOBJ: a damaged .obj is refused with a message"
fi
