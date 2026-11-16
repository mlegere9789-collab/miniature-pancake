# ---------------------------------------------------------------------------
# RWIORG: ExportWithOrigin (rwi_org_script.txt), the written files parsed by the stand-alone rwi_oracle.py (Python standard library only). Row:
# RHINO-FILE-0004 (save objects to a new file with a chosen origin).
# Expected values are closed forms: the chosen point becomes (0,0,0), so every coordinate moves by minus that point:
#   origin (5,5,0):   bounding box (0,0,0)-(30,10,10) becomes (-5,-5,0)-(25,5,10); the volume 2000 mm^3 and the shape are unchanged
#   origin (-5,0,4):  becomes (5,0,-4)-(35,10,6) (an origin with a z is honoured too)
#   the document:     exported again with a plain Export after the origin exports: still (0,0,0)-(30,10,10), 2 objects
# Written in OBJ, STL, PLY, IGES and SAT (each read by its own independent parser) and 3dm (the object count and type from the archive's chunks).
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIORG_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiorg"; mkdir -p "$RWI_D"
sed -e "s|@TMP@|$RWI_D|g" "$HERE/rwi_org_script.txt" > "$TMPW/rwiorg_script.txt"
rwi_app "$TMPW/rwiorg_script.txt"
rwi_has "Exported $RWI_D/org.obj (origin at" "RWIORG: the notification says the origin was applied (obj)"
rwi_oracle "RWIORG: org.obj bbox (-5,-5,0)-(25,5,10), volume 2000, closed" obj "$RWI_D/org.obj" --parts 2 --min -5,-5,0 --max 25,5,10 --vol 2000 --closed
rwi_oracle "RWIORG: org.stl bbox (-5,-5,0)-(25,5,10), volume 2000, closed" stl "$RWI_D/org.stl" --min -5,-5,0 --max 25,5,10 --vol 2000 --closed
rwi_oracle "RWIORG: org.ply bbox (-5,-5,0)-(25,5,10), volume 2000, closed" ply "$RWI_D/org.ply" --min -5,-5,0 --max 25,5,10 --vol 2000 --closed
rwi_oracle "RWIORG: org.igs 12 trimmed surfaces, bbox (-5,-5,0)-(25,5,10)" iges "$RWI_D/org.igs" --faces 12 --min -5,-5,0 --max 25,5,10
rwi_oracle "RWIORG: org.sat 2 bodies, bbox (-5,-5,0)-(25,5,10), volume 2000" sat "$RWI_D/org.sat" --parts 2 --euler 2 --min -5,-5,0 --max 25,5,10 --vol 2000
rwi_oracle "RWIORG: org.3dm holds 2 breps (object type 16)" 3dm "$RWI_D/org.3dm" --parts 2 --types 16,16
rwi_oracle "RWIORG: orgz.obj (origin -5,0,4) bbox (5,0,-4)-(35,10,6), volume 2000" obj "$RWI_D/orgz.obj" --parts 2 --min 5,0,-4 --max 35,10,6 --vol 2000 --closed
rwi_oracle "RWIORG: orig.obj, a plain export afterwards, bbox (0,0,0)-(30,10,10): the drawing was not moved" obj "$RWI_D/orig.obj" --parts 2 --min 0,0,0 --max 30,10,10 --vol 2000 --closed
fi
