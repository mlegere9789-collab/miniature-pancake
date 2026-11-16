# ---------------------------------------------------------------------------
# RWISTU: STL export with a solid check and units, the written files parsed by the stand-alone rwi_oracle.py (Python standard library only). Row: SKETCHUP-IO-0010 (STL for
# 3D printing with solid check and units). Export <file.stl> [Units=<unit>] reports "STL solid check: <n> facets, mesh is closed and manifold" (or "NOT closed") and the
# unit the coordinates are in, since the STL format has no unit field. A 10 mm cube in a millimetre document, and an open plane 10 x 10 mm. Expected by hand:
#   m.stl     Units=m: x 0.001, bbox (0,0,0)-(0.01,0.01,0.01), volume 1e-6, closed; the app's reported facet count equals the file's (read by the oracle)
#   mm.stl    no Units: bbox (0,0,0)-(10,10,10), volume 1000, closed; units reported "Millimeters" (the document's)
#   plane.stl the open plane: the file is not closed (the oracle's edge count) and the check says NOT closed and manifold
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWISTU_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwistu"; mkdir -p "$RWI_D"
cat > "$TMPW/rwistu_script.txt" <<EOS
Box 0,0,0 10,10,0 10
SelAll
Export $RWI_D/m.stl Units=m
SelAll
Export $RWI_D/mm.stl
New
Plane 0,0,0 10,10,0
SelAll
Export $RWI_D/plane.stl
EOS
rwi_app "$TMPW/rwistu_script.txt"
rwi_oracle "RWISTU: m.stl (Units=m) parsed independently: bbox (0,0,0)-(0.01,0.01,0.01), volume 1e-6, closed" stl "$RWI_D/m.stl" --min 0,0,0 --max 0.01,0.01,0.01 --tol 1e-8 --vol 1e-6 --closed
rwi_oracle "RWISTU: mm.stl (no Units): bbox (0,0,0)-(10,10,10), volume 1000, closed" stl "$RWI_D/mm.stl" --min 0,0,0 --max 10,10,10 --vol 1000 --closed
rwi_oracle "RWISTU: plane.stl is an open mesh" stl "$RWI_D/plane.stl" --open
for f in m mm; do
  T="$(python3 -I "$HERE/rwi_oracle.py" stl "$RWI_D/$f.stl" --json 2>/dev/null | head -1 | python3 -I -c 'import json,sys; print(json.load(sys.stdin)["tris"])' 2>/dev/null)"
  rwi_has "STL solid check: $T facets, mesh is closed and manifold" "RWISTU: the solid check of $f.stl reports the $T facets the file really holds, closed and manifold"
done
rwi_has "STL units: coordinates are in m (the STL format records no unit)" "RWISTU: the unit of m.stl is reported as m"
rwi_has "STL units: coordinates are in Millimeters (the STL format records no unit)" "RWISTU: the unit of mm.stl is reported as the document's Millimeters"
rwi_has "mesh is NOT closed and manifold: open or non-manifold, no volume" "RWISTU: the open plane is reported as NOT closed and manifold"
fi
