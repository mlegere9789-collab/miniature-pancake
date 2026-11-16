# ---------------------------------------------------------------------------
# RWIPBR: glTF export and import of the PBR metallic and roughness factors, the written .gltf read by plain JSON parsing in Python (standard library only; the embedded buffer is not
# needed for the material factors). Row: SKETCHUP-IO-0013 (GLB with PBR material mapping: imported metalness and roughness equal the file values). A document material carries
# Metallic= and Roughness= (RenderAssignMaterialToObjects options, 0 to 1); glTF export writes them as pbrMetallicRoughness.metallicFactor / roughnessFactor of the object's material and
# import turns the file's factors into a document material. Three 10 mm cubes: Steel (Metallic=1 Roughness=0.25), Plastic (Metallic=0 Roughness=0.8) and one with no material assigned.
# Expected values by hand (the order of the parts is the order of the objects):
#   pbr.gltf   materials 0, 1, 2 hold (1, 0.25), (0, 0.8) and the defaults the exporter always wrote for an unassigned object, (0, 0.6); a factor of 0.25 is exact in binary float
#   pbr2.gltf  the file imported into an empty document and exported again holds the same three pairs (so the import read the factors, not defaults): 1e-6 absolute
#   an option value out of range (Metallic=2) is clamped to 1, a word is refused with "Unknown material option"
# ---------------------------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  echo "skip RWIPBR_all (needs python3)"
else
. "$HERE/rwi_lib.sh"
RWI_D="$TMPW/rwipbr"; mkdir -p "$RWI_D"
cat > "$TMPW/rwipbr_script.txt" <<EOS
Box 0,0,0 10,10,0 10
SelLast
RenderAssignMaterialToObjects Steel Color=200,200,210 Metallic=1 Roughness=0.25
SelNone
Box 20,0,0 30,10,0 10
SelLast
RenderAssignMaterialToObjects Plastic Metallic=0 Roughness=0.8
SelNone
Box 40,0,0 50,10,0 10
SelAll
Export $RWI_D/pbr.gltf
New
Import $RWI_D/pbr.gltf
@expect_objects 3
SelAll
Export $RWI_D/pbr2.gltf
New
Box 0,0,0 10,10,0 10
SelLast
RenderAssignMaterialToObjects Odd Metallic=2 Roughness=bogus
SelAll
Export $RWI_D/odd.gltf
EOS
rwi_app "$TMPW/rwipbr_script.txt"
pbr_check() {  # pbr_check FILE "m0,r0;m1,r1;..." LABEL
  local r
  r="$(python3 -I - "$1" "$2" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1]))
want = [tuple(float(x) for x in p.split(",")) for p in sys.argv[2].split(";")]
got = []
for m in d.get("materials", []):
    pbr = m.get("pbrMetallicRoughness", {})
    got.append((pbr.get("metallicFactor", 1.0), pbr.get("roughnessFactor", 1.0)))
if len(got) != len(want):
    print("BAD %d materials, wanted %d: %s" % (len(got), len(want), got)); sys.exit(1)
for i, (g, w) in enumerate(zip(got, want)):
    if abs(g[0] - w[0]) > 1e-6 or abs(g[1] - w[1]) > 1e-6:
        print("BAD material %d: metallic/roughness %s != %s" % (i, g, w)); sys.exit(1)
print("OK " + str(got))
PYEOF
)" && rwi_ok "$3: $r" || { rwi_bad "$3"; echo "     $r"; }
}
pbr_check "$RWI_D/pbr.gltf" "1,0.25;0,0.8;0,0.6" "RWIPBR: pbr.gltf holds (metallic, roughness) (1, 0.25), (0, 0.8) and the unassigned default (0, 0.6)"
pbr_check "$RWI_D/pbr2.gltf" "1,0.25;0,0.8;0,0.6" "RWIPBR: pbr2.gltf (imported, then exported again) holds the same three pairs"
pbr_check "$RWI_D/odd.gltf" "1,0.6" "RWIPBR: Metallic=2 is clamped to 1, Roughness=bogus is ignored (the default 0.6 stays)"
rwi_has "Unknown material option roughness=bogus" "RWIPBR: a roughness that is not a number is refused with a message"
fi
