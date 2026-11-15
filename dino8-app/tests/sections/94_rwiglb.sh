# ---------------------------------------------------------------------------
# RWIGLB: glTF 2.0 export and import, binary .glb and embedded-buffer .gltf (rwi_mesh_script.txt), the written files decoded by the independent
# gltf_oracle.py (struct + json + base64; accessors resolved by hand, POSITION min / max checked against the data, normals checked unit length,
# indices in range). Rows: BLENDER-EXPORTSC-0002 (export: reading it back reproduces the content, a failed export leaves the previous file intact)
# and BLENDER-IMPORTSC-0002 (import: content intact, a damaged file is reported, one undo step restores).
# Expected values are closed forms of the construction, a millimetre document (Z-up) written as metres Y-up, (x, y, z) -> (x, z, -y) x 0.001:
#   all   2 parts, bounding box (0,0,-0.010)-(0.030,0.010,0) m, signed volume 2 x 1000 mm^3 = 2e-6 m^3, a multiple of 12 triangles (at least 24),
#         baseColorFactor in LINEAR RGB: (255,0,0) -> (1,0,0); (128,64,255) -> ((c/255 + 0.055) / 1.055)^2.4 = (0.2158605, 0.0512695, 1)
#   one   the selected box: 1 part, bounding box (0.020,0,-0.010)-(0.030,0.010,0), volume 1e-6 m^3
#   all   after the failed export of a line to the same path: unchanged (still the two boxes)
#   rt    the imported document exported again: the same triangle count, bounding box, volume and colours as all (nothing lost)
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIGLB_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
for RWI_X in glb gltf; do
RWI_D="$TMPW/rwiglb_$RWI_X"; mkdir -p "$RWI_D"
printf 'this is not a glTF file' > "$RWI_D/bad.$RWI_X"
sed -e "s|@TMP@|$RWI_D|g" -e "s|@EXT@|$RWI_X|g" -e 's|@NOBJ@|2|g' "$HERE/rwi_mesh_script.txt" > "$TMPW/rwiglb_$RWI_X.txt"
rwi_app "$TMPW/rwiglb_$RWI_X.txt"
rwi_has "Exported $RWI_D/all.$RWI_X" "RWIGLB: Export wrote all.$RWI_X"
rwi_has "Exported $RWI_D/one.$RWI_X" "RWIGLB: Export of the selection wrote one.$RWI_X"
rwi_has "Nothing to export" "RWIGLB: exporting a line reports that there is nothing to export ($RWI_X)"
rwi_has "Imported $RWI_D/all.$RWI_X" "RWIGLB: Import read all.$RWI_X"
rwi_has "glTF: 2 part(s)" "RWIGLB: the import summary names 2 parts ($RWI_X)"
rwi_has "Exported $RWI_D/rt.$RWI_X" "RWIGLB: the re-imported document exported again ($RWI_X)"
rwi_has "Could not read a glTF model from $RWI_D/bad.$RWI_X" "RWIGLB: a damaged .$RWI_X is refused with a message"
for RWI_F in all one rt; do
  rwi_exists "$RWI_D/$RWI_F.$RWI_X" || continue
  RWI_J="$(python3 -I "$HERE/gltf_oracle.py" "$RWI_D/$RWI_F.$RWI_X" 2>"$RWI_D/$RWI_F.err")" || { rwi_bad "RWIGLB: $RWI_F.$RWI_X: oracle refused it: $(cat "$RWI_D/$RWI_F.err")"; continue; }
  echo "$RWI_J" > "$RWI_D/$RWI_F.json"
done
RWI_R="$(python3 -I - "$RWI_D" "$RWI_X" <<'EOP'
import json, sys
d, x = sys.argv[1], sys.argv[2]
def lin(c):
    s = c / 255.0
    return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4
def load(n):
    return json.load(open("%s/%s.json" % (d, n)))
errs = []
try:
    a, o, r = load("all"), load("one"), load("rt")
except Exception as e:
    print("BAD missing decode: %s" % e); sys.exit(0)
def box(j, name, parts, lo, hi, vol):
    if j["parts"] != parts: errs.append("%s: parts %d != %d" % (name, j["parts"], parts))
    if j["tris"] < 12 * parts or j["tris"] % 12: errs.append("%s: %d triangles, a multiple of 12 and at least %d expected" % (name, j["tris"], 12 * parts))
    for k in range(3):
        if abs(j["min"][k] - lo[k]) > 1e-7: errs.append("%s: min[%d] %.9g != %.9g" % (name, k, j["min"][k], lo[k]))
        if abs(j["max"][k] - hi[k]) > 1e-7: errs.append("%s: max[%d] %.9g != %.9g" % (name, k, j["max"][k], hi[k]))
    if abs(j["volume"] - vol) > vol * 1e-4: errs.append("%s: volume %.9g != %.9g" % (name, j["volume"], vol))
box(a, "all", 2, [0, 0, -0.010], [0.030, 0.010, 0], 2e-6)
box(o, "one", 1, [0.020, 0, -0.010], [0.030, 0.010, 0], 1e-6)
box(r, "rt", 2, [0, 0, -0.010], [0.030, 0.010, 0], 2e-6)
want = [[1.0, 0.0, 0.0], [lin(128), lin(64), 1.0]]
for name, j in (("all", a), ("rt", r)):
    for c, w in zip(j["colors"], want):
        if c is None or any(abs(c[k] - w[k]) > 2e-6 for k in range(3)) or abs(c[3] - 1.0) > 1e-9:
            errs.append("%s: colour %s != %s" % (name, c, w))
    if len(j["colors"]) != 2: errs.append("%s: %d colours" % (name, len(j["colors"])))
if a["tris"] != r["tris"]: errs.append("triangles %d after the round trip != %d" % (r["tris"], a["tris"]))
print("OK all %d triangles, one %d, rt %d" % (a["tris"], o["tris"], r["tris"]) if not errs else "BAD " + "; ".join(errs))
EOP
)"
case "$RWI_R" in
  OK*) rwi_ok "RWIGLB: $RWI_X decoded independently: bbox (0,0,-0.01)-(0.03,0.01,0) m, volume 2e-6 m3, linear colours (1,0,0) and (0.2158605,0.0512695,1), selection and round trip equal ($RWI_R)" ;;
  *) rwi_bad "RWIGLB: $RWI_X: $RWI_R" ;;
esac
done
fi
