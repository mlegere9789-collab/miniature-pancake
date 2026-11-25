# ---------------------------------------------------------------------------
# RWISLO: the STLOUT command (AutoCAD name; Dino 8 StlOut), the written .stl files parsed by the stand-alone rwi_oracle.py (Python standard library only).
# Row: AUTOCAD-DWGFID-0091 (export 3D solids to a stereolithography .stl file). Two 10 mm cubes (0..10 and 20..30 in x, 0..10 in y and z), millimetre document.
# Expected values are closed forms of the construction:
#   slo.stl    typed STLOUT: binary, size 84 + 50 x facets, outward normals, bounding box (0,0,0)-(30,10,10), volume 2 x 1000 = 2000, closed
#   slo_a.stl  Format=Ascii: the same solid (bounding box, volume 2000, closed)
#   slo_m.stl  Units=m: every coordinate x 0.001: bounding box (0,0,0)-(0.03,0.01,0.01), volume 2000 x 1e-9 = 2e-6
#   one.stl    only the selected cube: bounding box (20,0,0)-(30,10,10), volume 1000
#   wrong.obj  refused (not a .stl name): nothing written, a message says so
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWISLO_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwislo"; mkdir -p "$RWI_D"
cat > "$TMPW/rwislo_script.txt" <<EOS
Box 0,0,0 10,10,0 10
Box 20,0,0 30,10,0 10
SelAll
STLOUT $RWI_D/slo.stl
SelAll
StlOut $RWI_D/slo_a.stl Format=Ascii
SelAll
StlOut $RWI_D/slo_m.stl Units=m
SelAll
StlOut $RWI_D/wrong.obj
SelLast
StlOut $RWI_D/one.stl
EOS
rwi_app "$TMPW/rwislo_script.txt"
rwi_has "Exported $RWI_D/slo.stl" "RWISLO: STLOUT announces slo.stl"
rwi_has "Exported $RWI_D/one.stl" "RWISLO: StlOut of the selection announces one.stl"
rwi_oracle "RWISLO: slo.stl parsed independently: binary size 84+50n, outward normals, bbox (0,0,0)-(30,10,10), volume 2000, closed" stl "$RWI_D/slo.stl" --min 0,0,0 --max 30,10,10 --vol 2000 --closed --tris-multiple-of 12
rwi_oracle "RWISLO: slo_a.stl (Format=Ascii) is the same solid" stl "$RWI_D/slo_a.stl" --min 0,0,0 --max 30,10,10 --vol 2000 --closed --tris-multiple-of 12
rwi_oracle "RWISLO: slo_m.stl (Units=m) is scaled by 0.001: bbox (0,0,0)-(0.03,0.01,0.01), volume 2e-6" stl "$RWI_D/slo_m.stl" --min 0,0,0 --max 0.03,0.01,0.01 --vol 2e-6 --tol 1e-8 --closed
rwi_oracle "RWISLO: one.stl holds only the selected cube: bbox (20,0,0)-(30,10,10), volume 1000" stl "$RWI_D/one.stl" --min 20,0,0 --max 30,10,10 --vol 1000 --closed --tris-multiple-of 12
rwi_has "StlOut: $RWI_D/wrong.obj is not a .stl file name; nothing written" "RWISLO: a name that is not .stl is refused with a message"
rwi_absent "$RWI_D/wrong.obj" "RWISLO: nothing was written for the refused name"
fi
