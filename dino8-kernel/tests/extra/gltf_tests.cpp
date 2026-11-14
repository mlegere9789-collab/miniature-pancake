// glTF 2.0 read/write. Oracles independent of src/gltf.cpp: every input file is assembled here byte by byte (little-endian packing written out
// below, JSON as text) with the expected geometry computed from closed forms (quaternion / TRS / matrix compositions, triangle-strip and fan
// orderings per the specification); files the writer produces are decoded here with a separate scan of the GLB container and the BIN chunk, and
// by the Python oracle (tests/gltf_oracle.py: struct + json, accessor counts / bounds / min / max) when run on them.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dino8/kernel/gltf.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::GltfMaterial;
using dino8::kernel::GltfModel;
using dino8::kernel::GltfPart;
using dino8::kernel::GltfReadOptions;
using dino8::kernel::GltfWriteOptions;
using dino8::kernel::LoadGltfModel;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::SaveGltfModel;

const char* kGltf = "dino8_kernel_gltf_test.gltf";
const char* kGlb = "dino8_kernel_gltf_test.glb";

void WriteBytes(const std::string& path, const std::string& b) {
  std::ofstream out(path, std::ios::binary);
  out.write(b.data(), static_cast<std::streamsize>(b.size()));
}
std::string ReadBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void U16(std::string& s, uint16_t v) { s.push_back(static_cast<char>(v & 255)); s.push_back(static_cast<char>(v >> 8)); }
void U32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 255)); }
void F32(std::string& s, float f) { uint32_t u; std::memcpy(&u, &f, 4); U32(s, u); }

std::string B64(const std::string& in) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < in.size(); i += 3) {
    const size_t n = std::min<size_t>(3, in.size() - i);
    const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (n > 1 ? static_cast<unsigned char>(in[i + 1]) << 8 : 0) | (n > 2 ? static_cast<unsigned char>(in[i + 2]) : 0);
    o += t[(v >> 18) & 63]; o += t[(v >> 12) & 63]; o += n > 1 ? t[(v >> 6) & 63] : '='; o += n > 2 ? t[v & 63] : '=';
  }
  return o;
}

std::string Uri(const std::string& bin) { return "data:application/octet-stream;base64," + B64(bin); }

// One embedded-buffer glTF: `body` is the JSON between the buffers entry and the closing brace.
std::string Gltf(const std::string& bin, const std::string& body, const std::string& header = "\"asset\": {\"version\": \"2.0\"},") {
  return "{" + header + " \"buffers\": [{\"uri\": \"" + Uri(bin) + "\", \"byteLength\": " + std::to_string(bin.size()) + "}], " + body + "}";
}

std::string Glb(const std::string& json_in, const std::string& bin_in, bool extra_chunk = false) {
  std::string json = json_in, bin = bin_in;
  while (json.size() % 4) json.push_back(' ');
  while (bin.size() % 4) bin.push_back('\0');
  std::string chunks;
  U32(chunks, static_cast<uint32_t>(json.size())); U32(chunks, 0x4E4F534Au); chunks += json;
  if (extra_chunk) { U32(chunks, 8); U32(chunks, 0x12345678u); chunks += std::string(8, 'x'); }
  U32(chunks, static_cast<uint32_t>(bin.size())); U32(chunks, 0x004E4942u); chunks += bin;
  std::string out;
  U32(out, 0x46546C67u); U32(out, 2); U32(out, static_cast<uint32_t>(12 + chunks.size()));
  return out + chunks;
}

bool Load(const std::string& bytes, GltfModel& m, const GltfReadOptions& o = {}, const char* path = kGltf) {
  WriteBytes(path, bytes);
  return LoadGltfModel(path, m, o) == Result::Ok;
}

Point3d V(const Mesh& m, int i) { return Point3d(m.raw().m_V[i]); }
bool Near(const Point3d& a, const Point3d& b, double tol = 1e-5) { return (a - b).Length() < tol; }

// Unit-corner tetrahedron, outward oriented, volume 1/6.
const float kTetV[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const uint16_t kTetT[12] = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};

std::string TetBin16() {
  std::string b;
  for (const auto& v : kTetV) for (float c : v) F32(b, c);
  for (uint16_t i : kTetT) U16(b, i);
  return b;
}
const std::string kTetBody =
    "\"bufferViews\": [{\"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 48}, {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 24}], "
    "\"accessors\": [{\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\"}, {\"bufferView\": 1, \"componentType\": 5123, \"count\": 12, \"type\": \"SCALAR\"}], "
    "\"meshes\": [{\"name\": \"tet\", \"primitives\": [{\"attributes\": {\"POSITION\": 0}, \"indices\": 1}]}]";

}  // namespace

D8_EXTRA_TEST(GltfReaderDecodesStridedOffsetAndIntegerAccessors) {
  // Interleaved layout: position (12 bytes) + 8 bytes of other data per vertex, byteStride 20, view offset 16, accessor offset 4.
  std::string bin(16, '\xAA');
  bin += std::string(4, '\xBB');  // accessor byteOffset 4 skips these
  for (const auto& v : kTetV) { for (float c : v) F32(bin, c); bin += std::string(8, '\xCC'); }
  const size_t idx_at = bin.size();
  for (uint16_t i : kTetT) bin.push_back(static_cast<char>(i));  // unsigned byte indices, view starts here
  GltfModel m;
  const std::string body =
      "\"bufferViews\": [{\"buffer\": 0, \"byteOffset\": 16, \"byteLength\": " + std::to_string(4 + 4 * 20) + ", \"byteStride\": 20}, {\"buffer\": 0, \"byteOffset\": " + std::to_string(idx_at) + ", \"byteLength\": 12}], "
      "\"accessors\": [{\"bufferView\": 0, \"byteOffset\": 4, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\"}, {\"bufferView\": 1, \"componentType\": 5121, \"count\": 12, \"type\": \"SCALAR\"}], "
      "\"meshes\": [{\"primitives\": [{\"attributes\": {\"POSITION\": 0}, \"indices\": 1}]}]";
  d8test::Check(Load(Gltf(bin, body), m) && m.parts.size() == 1, "strided + offset + uint8-index file reads as one part");
  if (m.parts.size() != 1) return;
  const Mesh& t = m.parts[0].mesh;
  d8test::Check(t.VertexCount() == 4 && t.FaceCount() == 4, "4 vertices and 4 triangles");
  bool ok = true;
  for (int i = 0; i < 4; ++i) ok = ok && Near(V(t, i), Point3d(kTetV[i][0], kTetV[i][1], kTetV[i][2]), 1e-7);
  d8test::Check(ok, "every strided position decodes exactly");
  bool tri_ok = true;
  for (int f = 0; f < 4; ++f) tri_ok = tri_ok && t.raw().m_F[f].vi[0] == kTetT[f * 3] && t.raw().m_F[f].vi[1] == kTetT[f * 3 + 1] && t.raw().m_F[f].vi[2] == kTetT[f * 3 + 2];
  d8test::Check(tri_ok, "uint8 indices decode to the expected triangles");
  d8test::Check(std::fabs(t.Volume() - 1.0 / 6.0) < 1e-6, "the tetrahedron keeps its volume 1/6 (outward winding preserved)");
  // uint16 and uint32 indices over the same vertices
  GltfModel m16;
  d8test::Check(Load(Gltf(TetBin16(), kTetBody), m16) && m16.parts.size() == 1 && m16.parts[0].mesh.FaceCount() == 4, "uint16 indices read");
  std::string bin32;
  for (const auto& v : kTetV) for (float c : v) F32(bin32, c);
  for (uint16_t i : kTetT) U32(bin32, i);
  const std::string body32 = "\"bufferViews\": [{\"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 48}, {\"buffer\": 0, \"byteOffset\": 48, \"byteLength\": 48}], "
                             "\"accessors\": [{\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\"}, {\"bufferView\": 1, \"componentType\": 5125, \"count\": 12, \"type\": \"SCALAR\"}], "
                             "\"meshes\": [{\"primitives\": [{\"attributes\": {\"POSITION\": 0}, \"indices\": 1}]}]";
  GltfModel m32;
  d8test::Check(Load(Gltf(bin32, body32), m32) && m32.parts.size() == 1 && m32.parts[0].mesh.FaceCount() == 4, "uint32 indices read");
  std::remove(kGltf);
}

D8_EXTRA_TEST(GltfReaderHandlesNonIndexedStripAndFanPrimitives) {
  // Non-indexed TRIANGLES: three triangles in a row of 9 vertices.
  std::string bin;
  for (int t = 0; t < 3; ++t) { const float x = static_cast<float>(t * 2); const float v[3][3] = {{x, 0, 0}, {x + 1, 0, 0}, {x, 1, 0}}; for (auto& p : v) for (float c : p) F32(bin, c); }
  const std::string body = "\"bufferViews\": [{\"buffer\": 0, \"byteLength\": 108}], \"accessors\": [{\"bufferView\": 0, \"componentType\": 5126, \"count\": 9, \"type\": \"VEC3\"}], "
                           "\"meshes\": [{\"primitives\": [{\"attributes\": {\"POSITION\": 0}}]}]";
  GltfModel m;
  d8test::Check(Load(Gltf(bin, body), m) && m.parts.size() == 1 && m.parts[0].mesh.VertexCount() == 9 && m.parts[0].mesh.FaceCount() == 3, "a primitive without indices uses the vertices in order (3 triangles)");
  // TRIANGLE_STRIP over a unit square: v0 (0,0) v1 (1,0) v2 (0,1) v3 (1,1) -> (0,1,2) then (2,1,3) by the spec's alternating winding.
  std::string sq;
  const float q[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
  for (auto& p : q) for (float c : p) F32(sq, c);
  auto prim = [&](int mode) {
    return "\"bufferViews\": [{\"buffer\": 0, \"byteLength\": 48}], \"accessors\": [{\"bufferView\": 0, \"componentType\": 5126, \"count\": 4, \"type\": \"VEC3\"}], "
           "\"meshes\": [{\"primitives\": [{\"attributes\": {\"POSITION\": 0}, \"mode\": " + std::to_string(mode) + "}]}]";
  };
  GltfModel strip;
  d8test::Check(Load(Gltf(sq, prim(5)), strip) && strip.parts.size() == 1 && strip.parts[0].mesh.FaceCount() == 2, "TRIANGLE_STRIP of 4 vertices gives 2 triangles");
  if (strip.parts.size() == 1 && strip.parts[0].mesh.FaceCount() == 2) {
    const ON_Mesh& r = strip.parts[0].mesh.raw();
    d8test::Check(r.m_F[0].vi[0] == 0 && r.m_F[0].vi[1] == 1 && r.m_F[0].vi[2] == 2 && r.m_F[1].vi[0] == 2 && r.m_F[1].vi[1] == 1 && r.m_F[1].vi[2] == 3, "strip triangles are (0,1,2) and (2,1,3): odd triangles swap their first two to keep the winding");
    d8test::Check(std::fabs(strip.parts[0].mesh.Area() - 1.0) < 1e-6, "both strip triangles face the same way: the square's area is 1");
  }
  GltfModel fan;
  d8test::Check(Load(Gltf(sq, prim(6)), fan) && fan.parts.size() == 1 && fan.parts[0].mesh.FaceCount() == 2, "TRIANGLE_FAN of 4 vertices gives 2 triangles");
  if (fan.parts.size() == 1 && fan.parts[0].mesh.FaceCount() == 2) {
    const ON_Mesh& r = fan.parts[0].mesh.raw();
    d8test::Check(r.m_F[0].vi[0] == 0 && r.m_F[0].vi[1] == 1 && r.m_F[0].vi[2] == 2 && r.m_F[1].vi[0] == 0 && r.m_F[1].vi[1] == 2 && r.m_F[1].vi[2] == 3, "fan triangles are (0,1,2) and (0,2,3)");
  }
  GltfModel pts;
  d8test::Check(!Load(Gltf(sq, prim(0)), pts), "a points-only file has no triangles and is refused");
  std::remove(kGltf);
}

D8_EXTRA_TEST(GltfReaderAppliesNodeHierarchyTransformsMaterialsAndSceneChoice) {
  const std::string bin = TetBin16();
  // Node 0: translate (10,0,0) and rotate 90 degrees about +Y (quaternion (0, sin45, 0, cos45)); its child node 1: scale 2, holds the mesh.
  // Node 2 (second scene): column-major matrix translating by (3,4,5); node 3: mirror x (scale -1, 1, 1).
  const double s45 = std::sqrt(0.5);
  const std::string nodes =
      "\"nodes\": [{\"name\": \"root\", \"translation\": [10, 0, 0], \"rotation\": [0, " + std::to_string(s45) + ", 0, " + std::to_string(s45) + "], \"children\": [1]}, "
      "{\"name\": \"kid\", \"scale\": [2, 2, 2], \"mesh\": 0}, "
      "{\"name\": \"moved\", \"mesh\": 0, \"matrix\": [1,0,0,0, 0,1,0,0, 0,0,1,0, 3,4,5,1]}, "
      "{\"name\": \"mirror\", \"mesh\": 0, \"scale\": [-1, 1, 1]}], "
      "\"scenes\": [{\"nodes\": [0]}, {\"nodes\": [2, 3]}], ";
  const std::string materials = "\"materials\": [{\"pbrMetallicRoughness\": {\"baseColorFactor\": [0.25, 0.5, 0.75, 1]}}], ";
  std::string body = kTetBody;
  body.replace(body.find("\"indices\": 1}"), 13, "\"indices\": 1, \"material\": 0}");
  GltfModel first;
  d8test::Check(Load(Gltf(bin, nodes + materials + "\"scene\": 0, " + body), first) && first.parts.size() == 1, "scene 0: one instance (the mesh node under the root)");
  if (first.parts.size() == 1) {
    const GltfPart& p = first.parts[0];
    d8test::Check(p.name == "kid", "the part is named after its node");
    // world = T(10,0,0) * Ry(90) * S(2): Ry(90) maps (x,y,z) -> (z, y, -x).
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
      const double x = 2 * kTetV[i][0], y = 2 * kTetV[i][1], z = 2 * kTetV[i][2];
      ok = ok && Near(V(p.mesh, i), Point3d(10 + z, y, -x), 1e-5);
    }
    d8test::Check(ok, "world positions = translate(10,0,0) * rotateY(90) * scale(2) of the tetrahedron (closed form)");
    d8test::Check(std::fabs(p.mesh.Volume() - 8.0 / 6.0) < 1e-5, "volume 8 x 1/6: a rotation keeps the orientation, scale 2 cubes the volume");
    d8test::Check(p.material && std::fabs(p.material->base_color[0] - 0.25f) < 1e-6 && std::fabs(p.material->base_color[1] - 0.5f) < 1e-6 && std::fabs(p.material->base_color[2] - 0.75f) < 1e-6 && p.material->base_color[3] == 1.0f, "the material's baseColorFactor is read (0.25, 0.5, 0.75, 1)");
    d8test::Check(p.material && p.material->metallic == 1.0f && p.material->roughness == 1.0f && p.material->alpha_mode == GltfMaterial::AlphaMode::Opaque && !p.material->double_sided, "the unstated material terms take the specification's defaults (metallic 1, roughness 1, OPAQUE, single sided)");
  }
  GltfModel second;
  d8test::Check(Load(Gltf(bin, nodes + materials + "\"scene\": 1, " + body), second) && second.parts.size() == 2, "scene 1 selects the two other nodes");
  if (second.parts.size() == 2) {
    d8test::Check(second.parts[0].name == "moved" && Near(V(second.parts[0].mesh, 1), Point3d(4, 4, 5)), "the column-major matrix translates (1,0,0) to (4,4,5)");
    d8test::Check(second.parts[1].name == "mirror" && Near(V(second.parts[1].mesh, 1), Point3d(-1, 0, 0)), "the mirror node maps (1,0,0) to (-1,0,0)");
    d8test::Check(std::fabs(second.parts[1].mesh.Volume() - 1.0 / 6.0) < 1e-5, "the mirrored copy keeps a positive volume 1/6 (its winding is flipped)");
  }
  // No scenes at all: nodes that nobody uses as a child are the roots.
  GltfModel noscene;
  std::string nodes_only = nodes;
  nodes_only.erase(nodes_only.find("\"scenes\""));
  d8test::Check(Load(Gltf(bin, nodes_only + materials + body), noscene) && noscene.parts.size() == 3, "without scenes the unreferenced nodes are the roots: kid (via root), moved, mirror = 3 parts");
  // Unit and axis options: metres -> millimetres, Y-up -> Z-up.
  GltfModel conv;
  GltfReadOptions opt;
  opt.scale = 1000.0;
  opt.y_up_to_z_up = true;
  d8test::Check(Load(Gltf(bin, nodes + materials + "\"scene\": 1, " + body), conv, opt) && conv.parts.size() == 2, "read with scale 1000 and Y-up -> Z-up");
  if (conv.parts.size() == 2) {
    // "moved" (4,4,5) in glTF is (x, -z, y) = (4, -5, 4), times 1000.
    d8test::Check(Near(V(conv.parts[0].mesh, 1), Point3d(4000, -5000, 4000), 0.01), "(4,4,5) m becomes (4000,-5000,4000) mm: scale applied and Y-up rotated to Z-up");
  }
  std::remove(kGltf);
}

D8_EXTRA_TEST(GltfReaderReadsGlbExternalBinaryAndRefusesUnsafeOrBrokenFiles) {
  const std::string bin = TetBin16();
  const std::string json_no_uri = "{\"asset\": {\"version\": \"2.0\"}, \"buffers\": [{\"byteLength\": " + std::to_string(bin.size()) + "}], " + kTetBody + "}";
  GltfModel m;
  d8test::Check(Load(Glb(json_no_uri, bin), m, {}, kGlb) && m.parts.size() == 1 && m.parts[0].mesh.FaceCount() == 4, "a GLB (JSON chunk + BIN chunk) reads");
  GltfModel m2;
  d8test::Check(Load(Glb(json_no_uri, bin, true), m2, {}, kGlb) && m2.parts.size() == 1, "an unknown extra chunk is skipped");
  // External .bin next to the .gltf; the JSON names it.
  WriteBytes("dino8_kernel_gltf_ext.bin", bin);
  const std::string ext = "{\"asset\": {\"version\": \"2.0\"}, \"buffers\": [{\"uri\": \"dino8_kernel_gltf_ext.bin\", \"byteLength\": " + std::to_string(bin.size()) + "}], " + kTetBody + "}";
  GltfModel m3;
  d8test::Check(Load(ext, m3) && m3.parts.size() == 1, "an external .bin next to the .gltf is read");
  std::remove("dino8_kernel_gltf_ext.bin");
  auto refuses = [&](const std::string& bytes, const char* what, const char* path = kGltf) {
    GltfModel keep;
    keep.parts.resize(1);
    WriteBytes(path, bytes);
    const Result r = LoadGltfModel(path, keep);
    d8test::Check(r == Result::Failed && keep.parts.size() == 1, (std::string(what) + ": Failed, model untouched").c_str());
  };
  auto with_uri = [&](const std::string& uri) { return "{\"asset\": {\"version\": \"2.0\"}, \"buffers\": [{\"uri\": \"" + uri + "\", \"byteLength\": " + std::to_string(bin.size()) + "}], " + kTetBody + "}"; };
  refuses(with_uri("../dino8_kernel_gltf_ext.bin"), "external buffer with a .. segment");
  refuses(with_uri("/etc/passwd"), "external buffer with an absolute path");
  refuses(with_uri("file:///etc/passwd"), "external buffer with a URI scheme");
  refuses(with_uri("dino8_kernel_gltf_missing.bin"), "external buffer that does not exist");
  refuses(with_uri("data:application/octet-stream;base64,!!!!"), "bad base64");
  refuses("{\"asset\": {\"version\": \"2.0\"}, \"buffers\": [{\"uri\": \"" + Uri(bin.substr(0, 20)) + "\", \"byteLength\": " + std::to_string(bin.size()) + "}], " + kTetBody + "}", "buffer shorter than its byteLength");
  refuses(Gltf(bin, kTetBody, "\"asset\": {\"version\": \"1.0\"},"), "glTF 1.x");
  refuses(Gltf(bin, kTetBody, "\"asset\": {\"version\": \"2.0\"}, \"extensionsRequired\": [\"KHR_draco_mesh_compression\"],"), "a required extension (Draco)");
  std::string sparse = kTetBody;
  sparse.replace(sparse.find("\"count\": 4, \"type\": \"VEC3\""), 26, "\"count\": 4, \"type\": \"VEC3\", \"sparse\": {}");
  refuses(Gltf(bin, sparse), "sparse accessor");
  std::string oob = kTetBody;
  oob.replace(oob.find("\"byteLength\": 24"), 16, "\"byteLength\": 90");
  refuses(Gltf(bin, oob), "bufferView past the end of its buffer");
  std::string badtype = kTetBody;
  badtype.replace(badtype.find("\"type\": \"VEC3\""), 14, "\"type\": \"VEC4\"");
  refuses(Gltf(bin, badtype), "position accessor that is not VEC3");
  std::string badidx = TetBin16();
  badidx[48 + 4] = 9;  // an index of 9 with only 4 vertices
  refuses(Gltf(badidx, kTetBody), "index past the vertex list");
  std::string nanbin = TetBin16();
  const float nan = std::nanf("");
  std::memcpy(&nanbin[0], &nan, 4);
  refuses(Gltf(nanbin, kTetBody), "NaN position");
  refuses("{\"asset\": {\"version\": \"2.0\"}", "truncated JSON");
  refuses(std::string(70, '[') + std::string(70, ']'), "JSON nested past the depth limit");
  refuses(Gltf(bin, "\"nodes\": [{\"children\": [1], \"mesh\": 0}, {\"children\": [0]}], \"scenes\": [{\"nodes\": [0]}], " + kTetBody), "node cycle");
  refuses(Gltf(bin, "\"nodes\": [{\"mesh\": 5}], " + kTetBody), "node naming a missing mesh");
  refuses(Gltf(bin, "\"nodes\": [{\"mesh\": 0, \"matrix\": [1,2,3]}], " + kTetBody), "matrix with 3 numbers");
  std::string glb = Glb(json_no_uri, bin);
  std::string bad_magic = glb; bad_magic[0] = 'X';
  refuses(bad_magic, "GLB with a wrong magic (read as JSON, which it is not)", kGlb);
  std::string bad_ver = glb; bad_ver[4] = 1;
  refuses(bad_ver, "GLB version 1", kGlb);
  refuses(glb.substr(0, glb.size() - 8), "truncated GLB", kGlb);
  refuses(Glb("{\"asset\": {\"version\": \"2.0\"}}", ""), "GLB without a mesh", kGlb);
  refuses("", "empty file");
  { GltfModel k; d8test::Check(LoadGltfModel("dino8_kernel_gltf_does_not_exist.gltf", k) == Result::Failed, "missing file"); }
  std::remove(kGltf);
  std::remove(kGlb);
}

D8_EXTRA_TEST(GltfWriterOutputIsAValidContainerWithMinMaxAndTheRequestedUnitsAndAxes) {
  // A 20 x 10 x 5 box (millimetres, Z-up) as one quad-faced mesh, a red one and a plain tetrahedron: two parts.
  Mesh box;
  {
    ON_Mesh& r = box.raw();
    const float x1 = 20, y1 = 10, z1 = 5;
    const float v[8][3] = {{0, 0, 0}, {x1, 0, 0}, {x1, y1, 0}, {0, y1, 0}, {0, 0, z1}, {x1, 0, z1}, {x1, y1, z1}, {0, y1, z1}};
    for (auto& p : v) r.m_V.Append(ON_3fPoint(p[0], p[1], p[2]));
    const int q[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    for (auto& f : q) { ON_MeshFace face; for (int k = 0; k < 4; ++k) face.vi[k] = f[k]; r.m_F.Append(face); }
  }
  Mesh tet;
  {
    ON_Mesh& r = tet.raw();
    for (auto& p : kTetV) r.m_V.Append(ON_3fPoint(p[0], p[1], p[2]));
    for (int f = 0; f < 4; ++f) { ON_MeshFace face; face.vi[0] = kTetT[f * 3]; face.vi[1] = kTetT[f * 3 + 1]; face.vi[2] = kTetT[f * 3 + 2]; face.vi[3] = face.vi[2]; r.m_F.Append(face); }
  }
  GltfModel model;
  GltfPart red;
  red.name = "box \"red\"";
  red.mesh = box;
  GltfMaterial red_mat;
  red_mat.name = "paint";
  red_mat.base_color = {1.0f, 0.0f, 0.25f, 1.0f};
  red_mat.metallic = 0.0f;
  red_mat.roughness = 0.5f;
  red.material = red_mat;
  GltfPart plain;
  plain.name = "tet";
  plain.mesh = tet;
  model.parts = {red, plain};
  GltfWriteOptions opt;
  opt.binary = true;
  opt.scale = 0.001;
  opt.z_up_to_y_up = true;
  d8test::Check(SaveGltfModel(kGlb, model, opt) == Result::Ok, "GLB saves");
  const std::string f = ReadBytes(kGlb);
  auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, f.data() + at, 4); return v; };
  d8test::Check(f.size() > 28 && f.compare(0, 4, "glTF") == 0 && u32(4) == 2 && u32(8) == f.size(), "GLB header: magic glTF, version 2, total length = file size");
  const size_t jl = u32(12);
  d8test::Check(u32(16) == 0x4E4F534Au && jl % 4 == 0 && 20 + jl + 8 <= f.size(), "first chunk is JSON, padded to a multiple of 4");
  const std::string json = f.substr(20, jl);
  const size_t bl = u32(20 + jl);
  d8test::Check(u32(24 + jl) == 0x004E4942u && bl % 4 == 0 && 28 + jl + bl == f.size(), "second chunk is BIN, padded to a multiple of 4, and ends the file");
  const size_t bin_at = 28 + jl;
  d8test::Check(json.find("\"version\": \"2.0\"") != std::string::npos && json.find("\"uri\"") == std::string::npos, "asset 2.0 and no uri on the GLB's own buffer");
  d8test::Check(json.find("\"name\": \"box \\\"red\\\"\"") != std::string::npos, "the part name is JSON-escaped");
  d8test::Check(json.find("\"baseColorFactor\": [1, 0, 0.25, 1]") != std::string::npos, "the red material is written once with its colour");
  d8test::Check(json.find("\"metallicFactor\": 0") != std::string::npos && json.find("\"roughnessFactor\": 0.5") != std::string::npos && json.find("\"name\": \"paint\"") != std::string::npos, "the red material carries its metallic 0, roughness 0.5 and name");
  // Independent decode of the first part's positions from the BIN chunk: 8 vertices as float32 (x, z, -y) * 0.001 -> box 0.02 x 0.005 x -0.01.
  const float expect[8][3] = {{0, 0, 0}, {0.02f, 0, 0}, {0.02f, 0, -0.01f}, {0, 0, -0.01f}, {0, 0.005f, 0}, {0.02f, 0.005f, 0}, {0.02f, 0.005f, -0.01f}, {0, 0.005f, -0.01f}};
  bool pos_ok = true;
  for (int i = 0; i < 8; ++i) {
    for (int k = 0; k < 3; ++k) {
      float c;
      std::memcpy(&c, f.data() + bin_at + static_cast<size_t>(i) * 12 + static_cast<size_t>(k) * 4, 4);
      pos_ok = pos_ok && std::fabs(c - expect[i][k]) < 1e-9;
    }
  }
  d8test::Check(pos_ok, "the first 8 positions in the BIN chunk are the box in metres, Y-up (x, z, -y)");
  d8test::Check(json.find("\"min\": [0, 0, -0.01]") != std::string::npos && json.find("\"max\": [0.02, 0.005, 0]") != std::string::npos, "the position accessor's min / max are the box's extents (0,0,-0.01) .. (0.02,0.005,0)");
  d8test::Check(json.find("\"count\": 36") != std::string::npos && json.find("\"count\": 8,") != std::string::npos, "8 vertices and 36 indices (6 quads = 12 triangles)");
  // The first part's BIN layout is 8 positions (96 bytes), 8 normals (96 bytes, the default GltfNormals::Computed), then 36 indices: all below 8.
  bool idx_ok = true;
  for (int i = 0; i < 36; ++i) { uint32_t v; std::memcpy(&v, f.data() + bin_at + 192 + static_cast<size_t>(i) * 4, 4); idx_ok = idx_ok && v < 8; }
  d8test::Check(idx_ok, "all 36 indices of the first part are below its vertex count");
  // Round trip with the inverse options: names, colours, geometry in millimetres and Z-up again.
  GltfModel back;
  GltfReadOptions ro;
  ro.scale = 1000.0;
  ro.y_up_to_z_up = true;
  d8test::Check(LoadGltfModel(kGlb, back, ro) == Result::Ok && back.parts.size() == 2, "the file reads back as two parts");
  if (back.parts.size() == 2) {
    d8test::Check(back.parts[0].name == "box \"red\"" && back.parts[1].name == "tet", "names round-trip through the escaping");
    d8test::Check(back.parts[0].material && *back.parts[0].material == red_mat && !back.parts[1].material, "the whole material (name, colour, metallic, roughness) round-trips on the red part only");
    bool same = back.parts[0].mesh.VertexCount() == 8 && back.parts[0].mesh.FaceCount() == 12;
    for (int i = 0; same && i < 8; ++i) same = Near(V(back.parts[0].mesh, i), V(box, i), 1e-4);
    d8test::Check(same, "the box is back in millimetres and Z-up, 12 triangles");
    d8test::Check(std::fabs(back.parts[0].mesh.Volume() - 1000.0) < 1e-2 && std::fabs(back.parts[1].mesh.Volume() - 1.0 / 6.0) < 1e-6, "volumes 1000 (box) and 1/6 (tetrahedron): the Z-up to Y-up rotation keeps orientation");
  }
  // .gltf with an embedded buffer, default options (no unit change).
  GltfWriteOptions plain_opt;
  d8test::Check(SaveGltfModel(kGltf, model, plain_opt) == Result::Ok, ".gltf saves");
  const std::string text = ReadBytes(kGltf);
  d8test::Check(text.find("\"uri\": \"data:application/octet-stream;base64,") != std::string::npos && text.compare(0, 1, "{") == 0, "the .gltf embeds its buffer as a base64 data URI");
  GltfModel back2;
  d8test::Check(LoadGltfModel(kGltf, back2) == Result::Ok && back2.parts.size() == 2 && std::fabs(back2.parts[0].mesh.Volume() - 1000.0) < 1e-2, ".gltf reads back (volume 1000, no unit conversion)");
  // Refusals.
  GltfModel empty;
  d8test::Check(SaveGltfModel(kGlb, empty, opt) == Result::Failed, "no parts is refused");
  GltfModel nanm = model;
  nanm.parts[1].mesh.raw().m_V[0].x = std::nanf("");
  d8test::Check(SaveGltfModel(kGlb, nanm, opt) == Result::Failed, "a NaN coordinate is refused");
  GltfWriteOptions bad = opt;
  bad.scale = 0.0;
  d8test::Check(SaveGltfModel(kGlb, model, bad) == Result::Failed, "a zero scale is refused");
  GltfModel noface = model;
  noface.parts[1].mesh.raw().m_F.Empty();
  d8test::Check(SaveGltfModel(kGlb, noface, opt) == Result::Failed, "a part with no faces is refused");
  d8test::Check(SaveGltfModel("dino8_kernel_no_such_dir/x.glb", model, opt) == Result::Failed, "an unwritable path is refused");
  std::remove(kGltf);
  std::remove(kGlb);
}

D8_EXTRA_TEST(GltfMeshEntryPointsStillRoundTripAClosedMeshAndReadTheOtherContainer) {
  Mesh tet;
  {
    ON_Mesh& r = tet.raw();
    for (auto& p : kTetV) r.m_V.Append(ON_3fPoint(p[0], p[1], p[2]));
    for (int f = 0; f < 4; ++f) { ON_MeshFace face; face.vi[0] = kTetT[f * 3]; face.vi[1] = kTetT[f * 3 + 1]; face.vi[2] = kTetT[f * 3 + 2]; face.vi[3] = face.vi[2]; r.m_F.Append(face); }
  }
  d8test::Check(tet.SaveGltf(kGltf) == Result::Ok && tet.SaveGlb(kGlb) == Result::Ok, "Mesh::SaveGltf and SaveGlb succeed");
  Mesh a, b, c;
  d8test::Check(Mesh::LoadGltf(kGltf, a) == Result::Ok && a.VertexCount() == 4 && a.FaceCount() == 4 && std::fabs(a.Volume() - 1.0 / 6.0) < 1e-9, "Mesh::LoadGltf reads the .gltf back (volume 1/6)");
  d8test::Check(Mesh::LoadGlb(kGlb, b) == Result::Ok && b.VertexCount() == 4 && b.FaceCount() == 4, "Mesh::LoadGlb reads the .glb back");
  d8test::Check(Mesh::LoadGlb(kGltf, c) == Result::Ok && Mesh::LoadGltf(kGlb, c) == Result::Ok, "either loader reads either container (told apart by the first bytes)");
  Mesh empty;
  d8test::Check(empty.SaveGltf(kGltf) == Result::Failed && empty.SaveGlb(kGlb) == Result::Failed, "an empty mesh is not written");
  std::remove(kGltf);
  std::remove(kGlb);
}

D8_EXTRA_TEST(GltfWriterNormalsMaterialsAndExternalBinRoundTrip) {
  Mesh tet;
  {
    ON_Mesh& r = tet.raw();
    for (auto& p : kTetV) r.m_V.Append(ON_3fPoint(p[0], p[1], p[2]));
    for (int f = 0; f < 4; ++f) { ON_MeshFace face; face.vi[0] = kTetT[f * 3]; face.vi[1] = kTetT[f * 3 + 1]; face.vi[2] = kTetT[f * 3 + 2]; face.vi[3] = face.vi[2]; r.m_F.Append(face); }
  }
  GltfMaterial glass;
  glass.name = "glass";
  glass.base_color = {0.1f, 0.2f, 0.3f, 0.4f};
  glass.metallic = 0.25f;
  glass.roughness = 0.75f;
  glass.emissive = {0.5f, 0.0f, 1.0f};
  glass.alpha_mode = GltfMaterial::AlphaMode::Blend;
  glass.double_sided = true;
  GltfMaterial cutout = glass;
  cutout.name = "cutout";
  cutout.alpha_mode = GltfMaterial::AlphaMode::Mask;
  cutout.alpha_cutoff = 0.3f;
  GltfModel model;
  for (int i = 0; i < 3; ++i) {
    GltfPart p;
    p.name = "part" + std::to_string(i);
    p.mesh = tet;
    if (i == 0 || i == 2) p.material = glass;  // two parts share one material: it is written once
    if (i == 1) p.material = cutout;
    model.parts.push_back(p);
  }
  GltfWriteOptions opt;
  opt.external_bin = true;
  d8test::Check(SaveGltfModel(kGltf, model, opt) == Result::Ok, ".gltf with an external .bin saves");
  const std::string json = ReadBytes(kGltf);
  const std::string bin = ReadBytes("dino8_kernel_gltf_test.bin");
  d8test::Check(json.find("\"uri\": \"dino8_kernel_gltf_test.bin\"") != std::string::npos && json.find("base64") == std::string::npos, "the buffer is named by a relative uri, not embedded");
  // Per part: 4 positions + 4 normals (float32 x 3) + 12 uint32 indices = 48 + 48 + 48 bytes; three parts.
  d8test::Check(bin.size() == 3 * 144, "the .bin holds 3 x (4 positions + 4 normals + 12 indices) = 432 bytes");
  size_t mats = 0;
  for (size_t at = json.find("\"pbrMetallicRoughness\""); at != std::string::npos; at = json.find("\"pbrMetallicRoughness\"", at + 1)) ++mats;
  d8test::Check(mats == 2, "two distinct materials are written (the shared one once)");
  d8test::Check(json.find("\"alphaMode\": \"BLEND\"") != std::string::npos && json.find("\"alphaMode\": \"MASK\"") != std::string::npos && json.find("\"alphaCutoff\": 0.3") != std::string::npos && json.find("\"doubleSided\": true") != std::string::npos && json.find("\"emissiveFactor\": [0.5, 0, 1]") != std::string::npos, "alphaMode BLEND and MASK with cutoff, doubleSided and emissiveFactor are written");
  GltfModel back;
  d8test::Check(LoadGltfModel(kGltf, back) == Result::Ok && back.parts.size() == 3, "reads back as 3 parts through the external .bin");
  if (back.parts.size() == 3) {
    d8test::Check(back.parts[0].material && *back.parts[0].material == glass && back.parts[1].material && *back.parts[1].material == cutout && back.parts[2].material && *back.parts[2].material == glass, "every material round-trips exactly");
    // Normals: the unit-corner tetrahedron's vertex 0 (the origin) touches the three axis-plane faces, whose area-weighted sum of outward
    // normals (-x, -y, -z each with area 1/2) is (-1, -1, -1)/sqrt(3) after normalisation.
    const ON_Mesh& r = back.parts[0].mesh.raw();
    bool nrm = r.m_N.Count() == 4;
    if (nrm) {
      const double k = -1.0 / std::sqrt(3.0);
      nrm = std::fabs(r.m_N[0].x - k) < 1e-5 && std::fabs(r.m_N[0].y - k) < 1e-5 && std::fabs(r.m_N[0].z - k) < 1e-5;
      for (int i = 0; nrm && i < 4; ++i) nrm = std::fabs(std::sqrt(double(r.m_N[i].x) * r.m_N[i].x + double(r.m_N[i].y) * r.m_N[i].y + double(r.m_N[i].z) * r.m_N[i].z) - 1.0) < 1e-5;
    }
    d8test::Check(nrm, "NORMAL is written and read: unit length, and vertex 0 is (-1,-1,-1)/sqrt(3) (closed form)");
  }
  GltfWriteOptions none;
  none.normals = dino8::kernel::GltfNormals::None;
  GltfModel one;
  one.parts.push_back(model.parts[0]);
  d8test::Check(SaveGltfModel(kGlb, one, none) == Result::Ok, "a part saves without normals");
  GltfModel nb;
  d8test::Check(LoadGltfModel(kGlb, nb) == Result::Ok && nb.parts.size() == 1 && nb.parts[0].mesh.raw().m_N.Count() == 0, "a file without NORMAL reads back with no normals");
  std::remove(kGltf);
  std::remove(kGlb);
  std::remove("dino8_kernel_gltf_test.bin");
}
