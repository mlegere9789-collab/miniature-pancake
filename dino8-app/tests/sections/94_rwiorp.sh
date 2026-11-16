# ---------------------------------------------------------------------------
# RWIORP: ExportWithOrigin with a construction plane, the written files parsed by the stand-alone rwi_oracle.py. Row: RHINO-FILE-0004 (save objects to a new
# file with a chosen origin and construction plane: the file's axes are the plane's axes, its origin the picked point). Two 10 mm boxes (0..10 and 20..30 in x,
# 0..10 in y and z) in a millimetre document, the origin picked at world (5,5,0). A typed point is read in the active construction plane's coordinates, so the
# picks below are the plane coordinates of that world point, worked out by hand:
#   Front plane (x = world X, y = world Z, normal = X x Z = -world Y): world (u, -w, v) = (5,5,0) is typed 5,0,-5. A world point p lands at
#     (X', Y', Z') = (x - 5, z, 5 - y): boxes X' -5..5 and 15..25, Y' 0..10, Z' -5..5. bbox (-5,0,-5)-(25,10,5), volume 2000 (a rotation keeps it, outward normals stay)
#   45 degree plane (x = (1,1,0)/sqrt2, y = (-1,1,0)/sqrt2, normal +Z), origin 0: world (5,5,0) is u = 5 sqrt2 = 7.0710678118654755, v = 0. With d = p - (5,5,0):
#     X' = (dx + dy)/sqrt2, Y' = (-dx + dy)/sqrt2, Z' = z: dx in {-5,5,15,25}, dy = +-5 so X' -7.0710678..21.2132034, Y' -21.2132034..7.0710678, Z' 0..10
#     (the rotated planar faces tessellate into 44 triangles, not a multiple of 12 as the axis-aligned boxes do: the closed form checked is the bbox, the volume and the closure)
#   World plane (the default): a plain translation, bbox (-5,-5,0)-(25,5,10), as RWIORG checks
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIORP_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiorp"; mkdir -p "$RWI_D"
cat > "$TMPW/rwiorp_script.txt" <<EOS
Box 0,0,0 10,10,0 10
Box 20,0,0 30,10,0 10
CPlane Front
SelAll
ExportWithOrigin 5,0,-5 $RWI_D/fr.obj
SelAll
ExportWithOrigin 5,0,-5 $RWI_D/fr.stl
SelAll
ExportWithOrigin 5,0,-5 $RWI_D/fr.ply
SelAll
ExportWithOrigin 5,0,-5 $RWI_D/fr.igs
CPlane World
CPlane
0,0,0
1,1,0
-1,1,0
SelAll
ExportWithOrigin 7.0710678118654755,0,0 $RWI_D/d45.stl
SelAll
ExportWithOrigin 7.0710678118654755,0,0 $RWI_D/d45.obj
CPlane World
SelAll
ExportWithOrigin 5,5,0 $RWI_D/wo.stl
SelAll
Export $RWI_D/orig.obj
@expect_objects 2
EOS
rwi_app "$TMPW/rwiorp_script.txt"
rwi_has "Exported $RWI_D/fr.obj (origin at 5,5,0" "RWIORP: the Front plane export reports the picked origin (world 5,5,0)"
rwi_oracle "RWIORP: fr.obj (Front plane) parsed independently: bbox (-5,0,-5)-(25,10,5), volume 2000, closed" obj "$RWI_D/fr.obj" --parts 2 --min -5,0,-5 --max 25,10,5 --vol 2000 --closed
rwi_oracle "RWIORP: fr.stl (Front plane): bbox (-5,0,-5)-(25,10,5), volume 2000, closed" stl "$RWI_D/fr.stl" --min -5,0,-5 --max 25,10,5 --vol 2000 --closed --tris-multiple-of 12
rwi_oracle "RWIORP: fr.ply (Front plane): bbox (-5,0,-5)-(25,10,5), volume 2000, closed" ply "$RWI_D/fr.ply" --min -5,0,-5 --max 25,10,5 --vol 2000 --closed
rwi_oracle "RWIORP: fr.igs (Front plane): 12 trimmed surfaces, bbox (-5,0,-5)-(25,10,5)" iges "$RWI_D/fr.igs" --faces 12 --unit-flag 2 --min -5,0,-5 --max 25,10,5
rwi_oracle "RWIORP: d45.stl (45 degree plane): bbox (-7.0710678,-21.2132034,0)-(21.2132034,7.0710678,10), volume 2000, closed" stl "$RWI_D/d45.stl" --min -7.0710678118654755,-21.213203435596427,0 --max 21.213203435596427,7.0710678118654755,10 --tol 1e-5 --vol 2000 --vol-tol 1e-5 --closed
rwi_oracle "RWIORP: d45.obj (45 degree plane): the same bbox and volume" obj "$RWI_D/d45.obj" --parts 2 --min -7.0710678118654755,-21.213203435596427,0 --max 21.213203435596427,7.0710678118654755,10 --tol 1e-5 --vol 2000 --vol-tol 1e-5 --closed
rwi_oracle "RWIORP: wo.stl (world plane) is the plain translation: bbox (-5,-5,0)-(25,5,10), volume 2000" stl "$RWI_D/wo.stl" --min -5,-5,0 --max 25,5,10 --vol 2000 --closed --tris-multiple-of 12
rwi_oracle "RWIORP: the drawing itself is not moved: orig.obj bbox (0,0,0)-(30,10,10)" obj "$RWI_D/orig.obj" --parts 2 --min 0,0,0 --max 30,10,10 --vol 2000 --closed
fi
