# ---------------------------------------------------------------------------
# RWIOBG: OBJ export grouped by layer (Export <file.obj> Group=Layer), the written files parsed by the stand-alone rwi_oracle.py (the "o" groups are its parts, in file order, with their names).
# Row: ARCHICAD-IFC-0046 (save the model as OBJ with layer or element grouping and a unit setting; the unit setting is Units=, checked by RWIOBT and again here). Three 10 mm cubes: two on layer
# Walls (0..10 and 20..30 in x) and one on layer Doors (40..50), millimetre document. Expected by hand:
#   per.obj   no option: one group per object: 3 groups, volume 3000
#   lay.obj   Group=Layer: 2 groups, named Walls then Doors (the order the layers first appear), together 3 cubes: bbox (0,0,0)-(50,10,10), volume 3 x 1000 = 3000, closed
#   lay_m.obj Group=Layer Units=m: the same 2 groups, bbox (0,0,0)-(0.05,0.01,0.01), volume 3e-6
#   Group=Both is refused with a message, Group=Layer on an .stl is refused (nothing written)
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIOBG_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiobg"; mkdir -p "$RWI_D"
cat > "$TMPW/rwiobg_script.txt" <<EOS
NewLayer Walls
Box 0,0,0 10,10,0 10
Box 20,0,0 30,10,0 10
NewLayer Doors
Box 40,0,0 50,10,0 10
SelAll
Export $RWI_D/per.obj
SelAll
Export $RWI_D/lay.obj Group=Layer
SelAll
Export $RWI_D/lay_m.obj Group=Layer Units=m
SelAll
Export $RWI_D/bad.obj Group=Both
SelAll
Export $RWI_D/lay.stl Group=Layer
EOS
rwi_app "$TMPW/rwiobg_script.txt"
rwi_oracle "RWIOBG: per.obj (no option): 3 groups, one per cube, volume 3000, closed" obj "$RWI_D/per.obj" --parts 3 --min 0,0,0 --max 50,10,10 --vol 3000 --closed
rwi_oracle "RWIOBG: lay.obj (Group=Layer): 2 groups named Walls, Doors; bbox (0,0,0)-(50,10,10), volume 3000, closed" obj "$RWI_D/lay.obj" --parts 2 --names Walls,Doors --min 0,0,0 --max 50,10,10 --vol 3000 --closed
rwi_oracle "RWIOBG: lay_m.obj (Group=Layer Units=m): the same groups x 0.001: bbox (0,0,0)-(0.05,0.01,0.01), volume 3e-6" obj "$RWI_D/lay_m.obj" --parts 2 --names Walls,Doors --min 0,0,0 --max 0.05,0.01,0.01 --tol 1e-8 --vol 3e-6 --closed
rwi_has "Export: Group=Both is not Layer or Object" "RWIOBG: an unknown grouping is refused with a message"
rwi_absent "$RWI_D/bad.obj" "RWIOBG: nothing was written for the refused grouping"
rwi_has "Export: Group=Layer applies to .obj files only" "RWIOBG: Group=Layer on an .stl is refused with a message"
rwi_absent "$RWI_D/lay.stl" "RWIOBG: nothing was written for the .stl"
fi
