# ---------------------------------------------------------------------------
# RWIOBI: OBJ import settings (Import <file> Units=, Scale=, UpAxis=), checked by importing a hand-written OBJ and writing the document out again, the written
# files parsed by the stand-alone rwi_oracle.py (Python standard library only). Row: ARCHICAD-IFC-0045 (import OBJ files with unit and up-axis settings; the face
# count of the import equals the source). The source file is a box 2 wide (x), 3 high (y, up) and 4 deep (z), 12 triangles with outward counter-clockwise faces,
# volume 2 x 3 x 4 = 24, written by this script; the document is in millimetres. Expected values by hand:
#   no settings        coordinates as written: bbox (0,0,0)-(2,3,4), volume 24
#   Units=cm           x 10: bbox (0,0,0)-(20,30,40), volume 24000
#   Units=m            x 1000: bbox (0,0,0)-(2000,3000,4000), volume 2.4e10
#   Scale=2            x 2: bbox (0,0,0)-(4,6,8), volume 192
#   UpAxis=Y Units=cm  (x, y, z) -> 10 x (x, -z, y): X 0..20, Y -40..0, Z 0..30, volume 20 x 40 x 30 = 24000 (a proper rotation keeps the volume and the outward faces)
#   UpAxis=Z           the same as no settings
# Refused (nothing imported): Units=furlong, UpAxis=X, Scale=0, and Units= on an .stl.
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIOBI_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiobi"; mkdir -p "$RWI_D"
cat > "$RWI_D/box.obj" <<'EOS'
# a box 2 x 3 x 4, y up, outward counter-clockwise triangles
o box
v 0 0 0
v 2 0 0
v 2 3 0
v 0 3 0
v 0 0 4
v 2 0 4
v 2 3 4
v 0 3 4
f 1 4 3
f 1 3 2
f 5 6 7
f 5 7 8
f 1 2 6
f 1 6 5
f 4 8 7
f 4 7 3
f 1 5 8
f 1 8 4
f 2 3 7
f 2 7 6
EOS
printf 'solid x\nendsolid x\n' > "$RWI_D/empty.stl"
cat > "$TMPW/rwiobi_script.txt" <<EOS
Import $RWI_D/box.obj
@expect_objects 1
SelAll
Export $RWI_D/plain.obj
New
Import $RWI_D/box.obj Units=cm
SelAll
Export $RWI_D/cm.obj
New
Import $RWI_D/box.obj Units=m
SelAll
Export $RWI_D/m.obj
New
Import $RWI_D/box.obj Scale=2
SelAll
Export $RWI_D/s2.obj
New
Import $RWI_D/box.obj UpAxis=Y Units=cm
@expect_objects 1
SelAll
Export $RWI_D/ycm.obj
New
Import $RWI_D/box.obj UpAxis=Z
SelAll
Export $RWI_D/z.obj
New
Import $RWI_D/box.obj Units=furlong
@expect_objects 0
Import $RWI_D/box.obj UpAxis=X
@expect_objects 0
Import $RWI_D/box.obj Scale=0
@expect_objects 0
Import $RWI_D/empty.stl Units=cm
@expect_objects 0
EOS
rwi_app "$TMPW/rwiobi_script.txt"
rwi_oracle "RWIOBI: plain.obj (no settings) parsed independently: 12 triangles, bbox (0,0,0)-(2,3,4), volume 24, closed" obj "$RWI_D/plain.obj" --tris 12 --min 0,0,0 --max 2,3,4 --vol 24 --closed
rwi_oracle "RWIOBI: cm.obj (Units=cm into millimetres, x 10): bbox (0,0,0)-(20,30,40), volume 24000" obj "$RWI_D/cm.obj" --tris 12 --min 0,0,0 --max 20,30,40 --vol 24000 --closed
rwi_oracle "RWIOBI: m.obj (Units=m, x 1000): bbox (0,0,0)-(2000,3000,4000), volume 2.4e10" obj "$RWI_D/m.obj" --tris 12 --min 0,0,0 --max 2000,3000,4000 --vol 2.4e10 --closed
rwi_oracle "RWIOBI: s2.obj (Scale=2): bbox (0,0,0)-(4,6,8), volume 192" obj "$RWI_D/s2.obj" --tris 12 --min 0,0,0 --max 4,6,8 --vol 192 --closed
rwi_oracle "RWIOBI: ycm.obj (UpAxis=Y Units=cm): bbox (0,-40,0)-(20,0,30), volume 24000, outward faces kept" obj "$RWI_D/ycm.obj" --tris 12 --min 0,-40,0 --max 20,0,30 --vol 24000 --closed
rwi_oracle "RWIOBI: z.obj (UpAxis=Z) equals no settings: bbox (0,0,0)-(2,3,4)" obj "$RWI_D/z.obj" --tris 12 --min 0,0,0 --max 2,3,4 --vol 24 --closed
rwi_has "Import: Units=furlong is not a known unit" "RWIOBI: an unknown unit is refused with a message"
rwi_has "Import: UpAxis=X is not Y or Z" "RWIOBI: an up axis other than Y or Z is refused with a message"
rwi_has "Import: Scale=0 is not a positive number" "RWIOBI: a zero scale is refused with a message"
rwi_has "Import: Units=, Scale= and UpAxis= apply to .obj files only" "RWIOBI: the settings on a non-OBJ file are refused with a message"
fi
