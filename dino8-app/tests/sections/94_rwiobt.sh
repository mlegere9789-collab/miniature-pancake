# ---------------------------------------------------------------------------
# RWIOBT: OBJ export with a unit record and textures. Row: SKETCHUP-IO-0008 (exports OBJ with units, textures and options). OBJ has no unit field, so the exporter writes the unit of
# the coordinates as a comment line ("# length unit: <unit>") and reports it; a material with a texture file is written as a map_Kd line in the .mtl, named relative to the .mtl
# when the texture lies next to it. Written files are read by the stand-alone rwi_oracle.py (geometry) and by plain text matching of the header and .mtl lines. Expected by hand
# (a 10 mm cube, a 2 x 2 PPM image tex.ppm written here):
#   u.obj    exported with Units=m: header comment "# length unit: m", bbox (0,0,0)-(0.01,0.01,0.01), volume 1e-6
#   d.obj    no Units: "# length unit: Millimeters" (the document's), bbox (0,0,0)-(10,10,10), volume 1000
#   t.obj    the cube assigned a material with Texture=tex.ppm: t.mtl has "map_Kd tex.ppm" in the entry the cube uses; an untextured cube's .mtl has no map_Kd
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIOBT_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwiobt"; mkdir -p "$RWI_D"
printf 'P3\n2 2\n255\n255 0 0 0 255 0 0 0 255 255 255 0\n' > "$RWI_D/tex.ppm"
cat > "$TMPW/rwiobt_script.txt" <<EOS
Box 0,0,0 10,10,0 10
SelAll
Export $RWI_D/u.obj Units=m
SelAll
Export $RWI_D/d.obj
SelAll
RenderAssignMaterialToObjects Painted Texture=$RWI_D/tex.ppm
SelAll
Export $RWI_D/t.obj
EOS
rwi_app "$TMPW/rwiobt_script.txt"
rwi_oracle "RWIOBT: u.obj (Units=m) parsed independently: bbox (0,0,0)-(0.01,0.01,0.01), volume 1e-6, closed" obj "$RWI_D/u.obj" --min 0,0,0 --max 0.01,0.01,0.01 --tol 1e-8 --vol 1e-6 --closed
rwi_oracle "RWIOBT: d.obj (no Units): bbox (0,0,0)-(10,10,10), volume 1000, closed" obj "$RWI_D/d.obj" --min 0,0,0 --max 10,10,10 --vol 1000 --closed
if grep -qx '# length unit: m' "$RWI_D/u.obj"; then rwi_ok "RWIOBT: u.obj records its unit: # length unit: m"; else rwi_bad "RWIOBT: u.obj has no '# length unit: m' line"; fi
if grep -qx '# length unit: Millimeters' "$RWI_D/d.obj"; then rwi_ok "RWIOBT: d.obj records the document's unit: # length unit: Millimeters"; else rwi_bad "RWIOBT: d.obj has no '# length unit: Millimeters' line"; fi
rwi_has "OBJ length unit: m (written as a comment in the file; the format has no unit field)" "RWIOBT: the export reports the unit it recorded"
if grep -qx 'map_Kd tex.ppm' "$RWI_D/t.mtl"; then rwi_ok "RWIOBT: t.mtl names the texture next to it: map_Kd tex.ppm"; else rwi_bad "RWIOBT: t.mtl has no 'map_Kd tex.ppm' line"; cat "$RWI_D/t.mtl" 2>/dev/null | head -8 | sed 's/^/     mtl: /'; fi
if grep -q '^map_Kd' "$RWI_D/d.mtl"; then rwi_bad "RWIOBT: d.mtl (no texture assigned) has a map_Kd line"; else rwi_ok "RWIOBT: d.mtl (no texture assigned) has no map_Kd line"; fi
rwi_oracle "RWIOBT: t.obj (textured cube) still parses: bbox (0,0,0)-(10,10,10), volume 1000, closed" obj "$RWI_D/t.obj" --min 0,0,0 --max 10,10,10 --vol 1000 --closed
fi
