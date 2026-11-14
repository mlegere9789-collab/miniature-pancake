# GLT_: glTF 2.0 (.gltf with an embedded buffer, and binary .glb) import / export / open / SaveAs in the app (gltf_io_script.txt).
# Kernel reader / writer: dino8-kernel/src/gltf.cpp (specification-based; its own unit tests live in tests/extra/gltf_tests.cpp).
# The files the app writes are decoded by tests/gltf_oracle.py, which shares no code with the app (struct + json + base64, accessors
# resolved by hand, POSITION min / max checked against the data, NORMALs checked unit length, indices checked in range). Expected values
# are closed forms of the construction, in a millimetre document (Z-up) written as metres Y-up, (x, y, z) -> (x, z, -y) * 0.001:
#   two boxes 10 x 10 x 10 mm at x 0..10 and 20..30:  2 parts; the triangle count of a meshed brep box is the mesher's choice (at least 12
#       per box, a multiple of 12 for planar quads), so for the brep boxes it is checked for consistency (the oracle's count in every file
#       equals the count the app reports after re-import) and for a mesh fixture written below (two boxes, 8 vertices and 12 triangles
#       each, as an OBJ file) it is exact: 16 vertices, 24 triangles,
#   position box  min (0, 0, -0.010)  max (0.030, 0.010, 0)  metres,  volume 2 x 1000 mm^3 = 2e-6 m^3 (positive: the winding is kept)
#   material baseColorFactor is LINEAR RGB: red (255,0,0) -> (1,0,0); (128,64,255) -> sRGB transfer function (IEC 61966-2-1):
#       ((c/255 + 0.055) / 1.055)^2.4 -> (0.2158605, 0.0512695, 1)
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
# Fixture: two boxes (corner (x0,0,0), 10 mm cube, x0 = 0 and 20), each 8 vertices and 6 quads split into 12 outward-facing triangles.
awk 'BEGIN {
  split("0 3 2 1  4 5 6 7  0 1 5 4  3 7 6 2  0 4 7 3  1 2 6 5", q, " ")
  for (b = 0; b < 2; b++) {
    x0 = b * 20
    printf "o box%d\n", b + 1
    for (k = 0; k < 8; k++) printf "v %d %d %d\n", x0 + (k == 1 || k == 2 || k == 5 || k == 6) * 10, (k == 2 || k == 3 || k == 6 || k == 7) * 10, (k >= 4) * 10
    for (f = 0; f < 6; f++) { a = q[f * 4 + 1] + 1; bq = q[f * 4 + 2] + 1; c = q[f * 4 + 3] + 1; d = q[f * 4 + 4] + 1
      printf "f %d %d %d\nf %d %d %d\n", a, bq, c, a, c, d }
  }
}' > "$TMPW/gltf_fix.obj"
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
test "$(echo "$GLT" | grep -cE "^history: Object [0-9]+ \(mesh\)")" = "6" && glt_ok "all 6 listed objects (2 after Import, 2 after Open, 2 of the OBJ fixture) are meshes" || { glt_bad "the listed objects are not 6 meshes"; near "$GLT" "(mesh)"; }
glt_has "Could not read a glTF model from $TMPW/gltf_bad.glb" "a file that is not a glTF is refused with a message"
# Mesh vertex counts after Import (the app's own reader) against the oracle's count in the file it was read from.
GLT_V_A="$(echo "$GLT" | grep -E "^history:   [0-9]+ vertices, [0-9]+ faces$" | head -2 | awk '{ v += $2; f += $4 } END { print v " " f }')"
for f in gltf_a.glb gltf_a.gltf gltf_sa.glb gltf_rt.gltf gltf_rt2.glb gltf_fix.glb; do
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
fix = name == "gltf_fix.glb"
if fix:
    if j["tris"] != 24 or j["verts"] != 16: errs.append("fixture: %d vertices, %d triangles != 16, 24" % (j["verts"], j["tris"]))
elif j["tris"] < 24 or j["tris"] % 12: errs.append("triangles %d: at least 24 and a multiple of 12 expected for two meshed boxes" % j["tris"])
for k in range(3):
    if abs(j["min"][k] - want_min[k]) > 1e-7: errs.append("min[%d] %.9g != %.9g" % (k, j["min"][k], want_min[k]))
    if abs(j["max"][k] - want_max[k]) > 1e-7: errs.append("max[%d] %.9g != %.9g" % (k, j["max"][k], want_max[k]))
if abs(j["volume"] - 2e-6) > 2e-6 * 1e-4: errs.append("volume %.9g != 2e-06" % j["volume"])
want_c = [[1.0, 0.0, 0.0], [lin(128), lin(64), 1.0]]
for c, w in zip(j["colors"], want_c):
    if not fix and (c is None or any(abs(c[k] - w[k]) > 2e-6 for k in range(3)) or abs(c[3] - 1.0) > 1e-9):
        errs.append("colour %s != %s" % (c, w))
if len(j["colors"]) != 2: errs.append("%d colours" % len(j["colors"]))
print("OK %d %d" % (j["verts"], j["tris"]) if not errs else "BAD " + "; ".join(errs))
EOP
)"
  case "$GLT_R" in
    OK*) glt_ok "$f: oracle decode: 2 parts, whole triangles, bbox (0,0,-0.01)-(0.03,0.01,0) m Y-up, volume 2e-6 m3, linear colours (1,0,0) and (0.2158605,0.0512695,1)"
         eval "GLT_VERTS_$(echo "$f" | tr '.' '_')=$(echo "$GLT_R" | cut -d' ' -f2)"; eval "GLT_TRIS_$(echo "$f" | tr '.' '_')=$(echo "$GLT_R" | cut -d' ' -f3)" ;;
    *) glt_bad "$f: $GLT_R" ;;
  esac
done
# The app's re-import must see the same vertex and triangle counts as the oracle counted in the file (nothing welded, split or dropped).
GLT_ORACLE_V="${GLT_VERTS_gltf_a_glb:-x}"
GLT_APP_V="${GLT_V_A%% *}"; GLT_APP_F="${GLT_V_A##* }"
[ "$GLT_APP_V" = "$GLT_ORACLE_V" ] && glt_ok "re-imported vertex count $GLT_APP_V = the $GLT_ORACLE_V positions the oracle counted in gltf_a.glb" || glt_bad "re-imported vertex count $GLT_APP_V != oracle $GLT_ORACLE_V"
[ "$GLT_APP_F" = "${GLT_TRIS_gltf_a_glb:-x}" ] && glt_ok "re-imported triangle count $GLT_APP_F = the ${GLT_TRIS_gltf_a_glb} triangles the oracle counted in gltf_a.glb" || glt_bad "re-imported face count $GLT_APP_F != oracle ${GLT_TRIS_gltf_a_glb:-x}"
# The Open of the same file lists the same counts as the Import did (3rd and 4th list lines), and the OBJ fixture was read as 2 x (8, 12).
GLT_V_O="$(echo "$GLT" | grep -E "^history:   [0-9]+ vertices, [0-9]+ faces$" | sed -n 3,4p | awk '{ v += $2; f += $4 } END { print v " " f }')"
[ "$GLT_V_O" = "$GLT_V_A" ] && glt_ok "Open lists the same vertex and face totals as Import ($GLT_V_O)" || glt_bad "Open lists $GLT_V_O but Import listed $GLT_V_A"
GLT_V_F="$(echo "$GLT" | grep -E "^history:   [0-9]+ vertices, [0-9]+ faces$" | sed -n 5,6p | awk '{ v += $2; f += $4 } END { print v " " f }')"
[ "$GLT_V_F" = "16 24" ] && glt_ok "the OBJ fixture imported as 16 vertices and 24 faces (2 boxes of 8 and 12)" || glt_bad "the OBJ fixture imported as '$GLT_V_F', not '16 24'"
# .gltf (embedded) and .glb exports carry the same geometry: same vertex counts in the oracle's decode.
[ "${GLT_VERTS_gltf_a_gltf:-a}" = "${GLT_VERTS_gltf_a_glb:-b}" ] && glt_ok "the .gltf and the .glb export have the same vertex count" || glt_bad "the .gltf and .glb exports differ in vertex count"
[ "${GLT_VERTS_gltf_rt_gltf:-a}" = "${GLT_VERTS_gltf_a_glb:-b}" ] && glt_ok "import then export again keeps the vertex count (no weld, no split)" || glt_bad "the vertex count changed through import and re-export"
head -c4 "$TMPW/gltf_a.glb" | grep -q "glTF" && glt_ok "gltf_a.glb starts with the GLB magic" || glt_bad "gltf_a.glb has no GLB magic"
head -c1 "$TMPW/gltf_a.gltf" | grep -q "{" && grep -q "data:application/octet-stream;base64," "$TMPW/gltf_a.gltf" && glt_ok "gltf_a.gltf is JSON with an embedded base64 buffer" || glt_bad "gltf_a.gltf is not a JSON file with an embedded buffer"
fi
