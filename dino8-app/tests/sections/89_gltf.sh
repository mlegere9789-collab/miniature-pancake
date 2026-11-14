# GLT_: glTF 2.0 (.gltf with an embedded buffer, and binary .glb) import / export / open / SaveAs in the app (gltf_io_script.txt).
# Kernel reader / writer: dino8-kernel/src/gltf.cpp (specification-based; its own unit tests live in tests/extra/gltf_tests.cpp).
# The files the app writes are decoded by tests/gltf_oracle.py, which shares no code with the app (struct + json + base64, accessors
# resolved by hand, POSITION min / max checked against the data, NORMALs checked unit length, indices checked in range). Expected values
# are closed forms of the construction, in a millimetre document (Z-up) written as metres Y-up, (x, y, z) -> (x, z, -y) * 0.001:
#   two boxes 10 x 10 x 10 mm at x 0..10 and 20..30:  2 parts, 24 triangles (6 planar quads -> 12 triangles each),
#   position box  min (0, 0, -0.010)  max (0.030, 0.010, 0)  metres,  volume 2 x 1000 mm^3 = 2e-6 m^3 (positive: the winding is kept)
#   material baseColorFactor is LINEAR RGB: red (255,0,0) -> (1,0,0); (128,64,255) -> sRGB transfer function (IEC 61966-2-1):
#       ((c/255 + 0.055) / 1.055)^2.4 -> (0.2158605, 0.0513271, 1)
# The re-imported document must give back exactly the same measurements when exported again (units and axes round trip), the .gltf and
# the .glb exports must agree with each other, and a damaged file must be refused with a message and add nothing.
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip GLT_all (needs python3)"
else
glt_ok() { echo "ok   $1"; }
glt_bad() { echo "FAIL $1"; fail=1; }
GLT_ORACLE="$HERE/gltf_oracle.py"
GLT_DIR="$TMPW/glt"; mkdir -p "$GLT_DIR"
printf 'this is not a glTF file' > "$TMPW/gltf_bad.glb"
sed "s|@TMP@|$TMPW|g" "$HERE/gltf_io_script.txt" > "$TMPW/gltf_io_script.txt"
if [ -n "${DISPLAY:-}" ] && xset q >/dev/null 2>&1 || ! command -v xvfb-run >/dev/null 2>&1; then
  GLT="$(timeout 900 "$BIN" --smoke 40 --script "$TMPW/gltf_io_script.txt" 2>&1)" || { echo "$GLT"; echo "FAIL: glTF script exited non-zero"; exit 1; }
else
  GLT="$(timeout 900 xvfb-run -a -s "-screen 0 1600x900x24" "$BIN" --smoke 40 --script "$TMPW/gltf_io_script.txt" 2>&1)" || { echo "$GLT"; echo "FAIL: glTF script exited non-zero"; exit 1; }
fi
echo "$GLT" | grep -E "^(ok|FAIL)"
if echo "$GLT" | grep -q "^FAIL"; then fail=1; fi
glt_has() { if echo "$GLT" | grep -qF "$1"; then glt_ok "$2"; else glt_bad "$2"; near "$GLT" "$1"; fi; }
glt_has "Exported $TMPW/gltf_a.glb" "Export wrote gltf_a.glb"
glt_has "Exported $TMPW/gltf_a.gltf" "Export wrote gltf_a.gltf"
glt_has "Exported $TMPW/gltf_sa.glb" "SaveAs wrote gltf_sa.glb through the mesh exporter"
glt_has "Imported $TMPW/gltf_a.glb" "Import read gltf_a.glb"
glt_has "glTF: 2 part(s), metres Y-up converted to the document unit Millimeters Z-up" "the import summary names 2 parts and the unit / axis conversion"
glt_has "Opened $TMPW/gltf_a.gltf (2 objects)" "Open read the embedded-buffer gltf_a.gltf as a document of 2 objects"
glt_has "Exported $TMPW/gltf_rt.gltf" "the re-imported document exported again (gltf_rt.gltf)"
glt_has "Exported $TMPW/gltf_rt2.glb" "the opened document exported again (gltf_rt2.glb)"
test "$(echo "$GLT" | grep -cE "^history: Object [0-9]+ \(Mesh\)")" = "4" && glt_ok "all 4 listed objects (2 after Import, 2 after Open) came back as meshes" || { glt_bad "the listed objects are not 4 meshes"; near "$GLT" "(Mesh)"; }
glt_has "Could not read a glTF model from $TMPW/gltf_bad.glb" "a file that is not a glTF is refused with a message"
# Mesh vertex counts after Import (the app's own reader) against the oracle's count in the file it was read from.
GLT_V_A="$(echo "$GLT" | grep -E "^history:   [0-9]+ vertices, [0-9]+ faces$" | head -2 | awk '{ v += $2; f += $4 } END { print v " " f }')"
for f in gltf_a.glb gltf_a.gltf gltf_sa.glb gltf_rt.gltf gltf_rt2.glb; do
  test -s "$TMPW/$f" && glt_ok "$f exists" || { glt_bad "$f missing"; continue; }
  GLT_J="$(python3 -I "$GLT_ORACLE" "$TMPW/$f" 2>"$GLT_DIR/$f.err")" || { glt_bad "$f: oracle refused it: $(cat "$GLT_DIR/$f.err")"; continue; }
  GLT_R="$(python3 -I - "$GLT_J" "$f" <<'EOP'
import json, math, sys
j = json.loads(sys.argv[1]); name = sys.argv[2]
def lin(c):
    s = c / 255.0
    return s / 12.92 if s <= 0.04045 else ((s + 0.055) / 1.055) ** 2.4
want_min, want_max = [0.0, 0.0, -0.010], [0.030, 0.010, 0.0]
errs = []
if j["parts"] != 2: errs.append("parts %d != 2" % j["parts"])
if j["tris"] != 24: errs.append("triangles %d != 24" % j["tris"])
for k in range(3):
    if abs(j["min"][k] - want_min[k]) > 1e-7: errs.append("min[%d] %.9g != %.9g" % (k, j["min"][k], want_min[k]))
    if abs(j["max"][k] - want_max[k]) > 1e-7: errs.append("max[%d] %.9g != %.9g" % (k, j["max"][k], want_max[k]))
if abs(j["volume"] - 2e-6) > 2e-6 * 1e-4: errs.append("volume %.9g != 2e-06" % j["volume"])
want_c = [[1.0, 0.0, 0.0], [lin(128), lin(64), 1.0]]
for c, w in zip(j["colors"], want_c):
    if c is None or any(abs(c[k] - w[k]) > 2e-6 for k in range(3)) or abs(c[3] - 1.0) > 1e-9:
        errs.append("colour %s != %s" % (c, w))
if len(j["colors"]) != 2: errs.append("%d colours" % len(j["colors"]))
print("OK %d %d" % (j["verts"], j["tris"]) if not errs else "BAD " + "; ".join(errs))
EOP
)"
  case "$GLT_R" in
    OK*) glt_ok "$f: oracle decode: 2 parts, 24 triangles, bbox (0,0,-0.01)-(0.03,0.01,0) m Y-up, volume 2e-6 m3, linear colours (1,0,0) and (0.2158605,0.0513271,1)"
         GLT_VT="$(echo "$GLT_R" | cut -d' ' -f2)"; eval "GLT_VERTS_$(echo "$f" | tr '.' '_')=$GLT_VT" ;;
    *) glt_bad "$f: $GLT_R" ;;
  esac
done
# The app's re-import must see the same vertex and triangle counts as the oracle counted in the file (nothing welded, split or dropped).
GLT_ORACLE_V="${GLT_VERTS_gltf_a_glb:-x}"
GLT_APP_V="${GLT_V_A%% *}"; GLT_APP_F="${GLT_V_A##* }"
[ "$GLT_APP_V" = "$GLT_ORACLE_V" ] && glt_ok "re-imported vertex count $GLT_APP_V = the $GLT_ORACLE_V positions the oracle counted in gltf_a.glb" || glt_bad "re-imported vertex count $GLT_APP_V != oracle $GLT_ORACLE_V"
[ "$GLT_APP_F" = "24" ] && glt_ok "re-imported triangle count 24 (2 boxes x 12)" || glt_bad "re-imported face count $GLT_APP_F != 24"
# .gltf (embedded) and .glb exports carry the same geometry: same vertex counts in the oracle's decode.
[ "${GLT_VERTS_gltf_a_gltf:-a}" = "${GLT_VERTS_gltf_a_glb:-b}" ] && glt_ok "the .gltf and the .glb export have the same vertex count" || glt_bad "the .gltf and .glb exports differ in vertex count"
[ "${GLT_VERTS_gltf_rt_gltf:-a}" = "${GLT_VERTS_gltf_a_glb:-b}" ] && glt_ok "import then export again keeps the vertex count (no weld, no split)" || glt_bad "the vertex count changed through import and re-export"
head -c4 "$TMPW/gltf_a.glb" | grep -q "glTF" && glt_ok "gltf_a.glb starts with the GLB magic" || glt_bad "gltf_a.glb has no GLB magic"
head -c1 "$TMPW/gltf_a.gltf" | grep -q "{" && grep -q "data:application/octet-stream;base64," "$TMPW/gltf_a.gltf" && glt_ok "gltf_a.gltf is JSON with an embedded base64 buffer" || glt_bad "gltf_a.gltf is not a JSON file with an embedded buffer"
fi
