// 3MF read/write. Oracles independent of src/threemf.cpp:
//  (1) kFixture is a deflate-compressed package written by Python's zipfile (tests/threemf_oracle.py, which also holds the
//      structure it encodes): a tetrahedron, an assembly of two components (one rotated 90 degrees about z and moved, one
//      mirrored in x) and two build items, one scaled by 2 and lifted 10, in inches. The expected world-space vertices are
//      closed forms written out here, and the volumes are exact (a unit corner tetrahedron has volume 1/6);
//  (2) every other package is assembled byte by byte below (stored ZIP, own CRC-32) so the reader's refusals and its handling
//      of missing relationships, no build list, namespace prefixes and entities are exercised on files the writer never makes;
//  (3) files the kernel writes uncompressed are decoded here with a separate ZIP/XML scan, and the compressed ones are checked
//      by the Python oracle (zipfile + ElementTree) when tests/threemf_oracle.py is run on them.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/threemf.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::SaveThreeMf;
using dino8::kernel::LoadThreeMf;
using dino8::kernel::ThreeMfModel;
using dino8::kernel::ThreeMfPart;
using dino8::kernel::ThreeMfReadOptions;
using dino8::kernel::ThreeMfWriteOptions;

const unsigned char kFixture[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x40, 0xa0, 0x6b, 0x5d, 0xbb, 0x56, 0x92, 0xf4,
    0xc2, 0x00, 0x00, 0x00, 0x3d, 0x01, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0x5b, 0x43, 0x6f, 0x6e, 0x74, 0x65,
    0x6e, 0x74, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x73, 0x5d, 0x2e, 0x78, 0x6d, 0x6c, 0x85, 0x90, 0x5d, 0x6a, 0xc3,
    0x30, 0x10, 0x84, 0xaf, 0x22, 0xf6, 0xb5, 0xd8, 0x72, 0x53, 0x28, 0xa5, 0xd8, 0xce, 0x43, 0xd3, 0x9e, 0x20,
    0x3d, 0xc0, 0x22, 0xaf, 0x7f, 0x88, 0xb4, 0x12, 0xd2, 0x3a, 0xa4, 0xb7, 0xef, 0x3a, 0x69, 0x29, 0x94, 0x42,
    0x1f, 0x67, 0x67, 0x76, 0x3e, 0x98, 0x76, 0x7f, 0x09, 0xde, 0x9c, 0x29, 0x97, 0x25, 0x72, 0x07, 0xf7, 0x75,
    0x03, 0x86, 0xd8, 0xc5, 0x61, 0xe1, 0xa9, 0x83, 0xf7, 0xe3, 0x5b, 0xf5, 0x04, 0xfb, 0xbe, 0x3d, 0x7e, 0x24,
    0x2a, 0x46, 0xa3, 0x5c, 0x3a, 0x98, 0x45, 0xd2, 0xb3, 0xb5, 0xc5, 0xcd, 0x14, 0xb0, 0xd4, 0x31, 0x11, 0xab,
    0x33, 0xc6, 0x1c, 0x50, 0x54, 0xe6, 0xc9, 0x26, 0x74, 0x27, 0x9c, 0xc8, 0xee, 0x9a, 0xe6, 0xd1, 0xba, 0xc8,
    0x42, 0x2c, 0x95, 0x6c, 0x1d, 0xd0, 0xb7, 0x07, 0x1a, 0x71, 0xf5, 0x62, 0x5e, 0x2f, 0x7a, 0xbe, 0x61, 0x33,
    0xf9, 0x02, 0xe6, 0xe5, 0x16, 0xdc, 0x58, 0x1d, 0x60, 0x4a, 0x7e, 0x71, 0x28, 0xea, 0xdb, 0x33, 0x0f, 0xbf,
    0x28, 0xd5, 0x17, 0xa1, 0xd6, 0xcf, 0x6b, 0xa6, 0xcc, 0x4b, 0x2a, 0x77, 0x1a, 0x00, 0xfb, 0x27, 0x21, 0xc4,
    0x81, 0xfc, 0x3f, 0x88, 0xf0, 0x53, 0xfb, 0x30, 0x04, 0xe4, 0x75, 0x44, 0x27, 0x6b, 0xd6, 0x25, 0x2a, 0xd5,
    0x5b, 0xc1, 0x37, 0xc0, 0x5e, 0xf7, 0xe8, 0x3f, 0x01, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08,
    0x00, 0x40, 0xa0, 0x6b, 0x5d, 0xab, 0xf7, 0xfe, 0x47, 0xae, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x0b,
    0x00, 0x00, 0x00, 0x5f, 0x72, 0x65, 0x6c, 0x73, 0x2f, 0x2e, 0x72, 0x65, 0x6c, 0x73, 0x65, 0x8f, 0xc9, 0x0a,
    0xc2, 0x30, 0x10, 0x86, 0x5f, 0x25, 0xcc, 0xdd, 0x4c, 0x6b, 0x41, 0x44, 0x9a, 0xf6, 0x22, 0x82, 0x57, 0xa9,
    0x0f, 0x10, 0x92, 0xe9, 0x82, 0xcd, 0x42, 0x92, 0x8a, 0xbe, 0xbd, 0x51, 0x10, 0x2c, 0x5e, 0x06, 0x86, 0x99,
    0xef, 0x5f, 0xea, 0xf6, 0x61, 0x66, 0x76, 0xa7, 0x10, 0x27, 0x67, 0x05, 0x94, 0xbc, 0x00, 0x46, 0x56, 0x39,
    0x3d, 0xd9, 0x41, 0xc0, 0xb5, 0x3b, 0x6d, 0xf6, 0xd0, 0x36, 0xf5, 0x85, 0x66, 0x99, 0xf2, 0x47, 0x1c, 0x27,
    0x1f, 0x59, 0x46, 0x6c, 0x14, 0x30, 0xa6, 0xe4, 0x0f, 0x88, 0x51, 0x8d, 0x64, 0x64, 0xe4, 0xce, 0x93, 0xcd,
    0x97, 0xde, 0x05, 0x23, 0x53, 0x5e, 0xc3, 0x80, 0x5e, 0xaa, 0x9b, 0x1c, 0x08, 0xb7, 0x45, 0xb1, 0xc3, 0xf0,
    0xab, 0x01, 0x6b, 0x4d, 0xd6, 0xc9, 0x30, 0x50, 0x12, 0x80, 0xd5, 0x11, 0x2b, 0x6d, 0x9c, 0xa6, 0x99, 0x7f,
    0x26, 0xb0, 0xb3, 0x16, 0x90, 0xd9, 0x1c, 0xac, 0x7b, 0x7a, 0xfa, 0xb3, 0x35, 0x93, 0x0a, 0x2e, 0xba, 0x3e,
    0x71, 0xe5, 0xcc, 0x9b, 0x95, 0x76, 0xe9, 0xa5, 0x4a, 0x4b, 0xc8, 0x15, 0xb2, 0x73, 0x59, 0x61, 0x51, 0x7e,
    0x35, 0x01, 0x9b, 0x1a, 0x57, 0x65, 0x9a, 0x17, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00,
    0x40, 0xa0, 0x6b, 0x5d, 0x8d, 0x2b, 0xcc, 0xd9, 0x5f, 0x01, 0x00, 0x00, 0x4b, 0x03, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x33, 0x44, 0x2f, 0x33, 0x64, 0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x2e, 0x6d, 0x6f, 0x64, 0x65, 0x6c,
    0x85, 0x52, 0x6d, 0x6e, 0xc3, 0x20, 0x0c, 0xbd, 0x0a, 0xe2, 0x7f, 0x4b, 0x21, 0x9a, 0x34, 0x55, 0x49, 0xfa,
    0x6f, 0x17, 0x58, 0x7b, 0x00, 0x4a, 0xdc, 0x85, 0x29, 0x40, 0x04, 0x4e, 0x95, 0xee, 0xf4, 0x23, 0x90, 0x76,
    0x69, 0xb7, 0x6a, 0x8a, 0x64, 0x5b, 0x3c, 0x3f, 0x7f, 0x3c, 0xa7, 0xdc, 0x8d, 0xa6, 0x23, 0x67, 0xf0, 0x41,
    0x3b, 0x5b, 0x51, 0xbe, 0xde, 0x50, 0x02, 0x56, 0xb9, 0x46, 0xdb, 0x8f, 0x8a, 0x1e, 0xf6, 0x6f, 0xab, 0x57,
    0xba, 0xab, 0x4b, 0xe3, 0x1a, 0xe8, 0xc8, 0x60, 0x35, 0x56, 0x54, 0x5b, 0xd5, 0x52, 0x12, 0x69, 0xdb, 0x4e,
    0x4e, 0x49, 0x60, 0x57, 0x87, 0xf7, 0xf4, 0x60, 0x43, 0x45, 0x5b, 0xc4, 0x7e, 0xcb, 0x58, 0x50, 0x2d, 0x18,
    0x19, 0xd6, 0x46, 0x2b, 0xef, 0x82, 0x3b, 0xe1, 0x5a, 0x39, 0xc3, 0x8a, 0xc6, 0x48, 0x3b, 0x9c, 0xa4, 0xc2,
    0xc1, 0xc7, 0x06, 0x4c, 0x39, 0x0f, 0x4c, 0x6c, 0xf8, 0x0b, 0xdb, 0x08, 0x1a, 0xbb, 0x00, 0xca, 0x46, 0xa2,
    0x24, 0x56, 0x1a, 0xa8, 0xe8, 0x5e, 0x63, 0x07, 0xb4, 0x3e, 0xe9, 0x31, 0xa6, 0x43, 0xc9, 0xae, 0x70, 0x5d,
    0x7a, 0x08, 0x6e, 0xf0, 0x0a, 0x42, 0x5d, 0xba, 0xe3, 0x27, 0x28, 0x24, 0xba, 0x89, 0xc3, 0xd3, 0x99, 0x88,
    0x80, 0x94, 0xe0, 0xa5, 0x8f, 0x61, 0x1a, 0x3c, 0x95, 0x0e, 0x6d, 0x5d, 0xc6, 0x3d, 0x51, 0x27, 0xda, 0x14,
    0xc1, 0x48, 0xc6, 0x8a, 0xc6, 0x85, 0x2f, 0xc9, 0x7e, 0x4d, 0x96, 0x2d, 0x21, 0xfe, 0x1c, 0xca, 0x2c, 0xfe,
    0x1c, 0xca, 0x2c, 0x3e, 0x41, 0xec, 0xa7, 0x2d, 0x7a, 0x1d, 0x35, 0xeb, 0x96, 0x21, 0x39, 0xf3, 0x94, 0x7d,
    0x16, 0x15, 0x15, 0xd1, 0x15, 0x33, 0xeb, 0x2f, 0x9c, 0x67, 0xbc, 0x78, 0x86, 0x17, 0x19, 0x17, 0xbf, 0x70,
    0x7e, 0x57, 0x3f, 0xf1, 0xd9, 0x62, 0x16, 0x96, 0xe5, 0x61, 0x59, 0xcc, 0x3b, 0x51, 0xc5, 0x55, 0xd4, 0x5e,
    0x6a, 0xff, 0xa8, 0x6a, 0xbc, 0x69, 0xef, 0x2c, 0x58, 0x0c, 0x8b, 0x98, 0x64, 0xf6, 0x7c, 0x11, 0xf4, 0xd2,
    0x86, 0x93, 0xf3, 0x26, 0x0e, 0x49, 0x38, 0xd9, 0x90, 0xd5, 0x64, 0xf2, 0xc7, 0x89, 0x98, 0xfc, 0x34, 0xcd,
    0xff, 0xec, 0x1b, 0x6f, 0xe9, 0x8b, 0xcc, 0x66, 0xcb, 0x41, 0x6e, 0x5b, 0xb0, 0xc5, 0x6f, 0x72, 0x1c, 0x74,
    0xd7, 0xd4, 0xa5, 0x46, 0x30, 0x8b, 0x0e, 0xe2, 0xae, 0x83, 0x98, 0x0b, 0xdf, 0x7b, 0x9e, 0x3a, 0x3c, 0x10,
    0xf3, 0x5d, 0xe7, 0xa2, 0x2c, 0xc9, 0x51, 0x7f, 0x03, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x40, 0xa0, 0x6b, 0x5d, 0xbb, 0x56, 0x92, 0xf4, 0xc2, 0x00, 0x00, 0x00, 0x3d, 0x01, 0x00,
    0x00, 0x13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x5b, 0x43, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x5f, 0x54, 0x79, 0x70, 0x65, 0x73, 0x5d, 0x2e, 0x78,
    0x6d, 0x6c, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x40, 0xa0, 0x6b, 0x5d,
    0xab, 0xf7, 0xfe, 0x47, 0xae, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0xf3, 0x00, 0x00, 0x00, 0x5f, 0x72, 0x65, 0x6c, 0x73, 0x2f,
    0x2e, 0x72, 0x65, 0x6c, 0x73, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x03, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x40,
    0xa0, 0x6b, 0x5d, 0x8d, 0x2b, 0xcc, 0xd9, 0x5f, 0x01, 0x00, 0x00, 0x4b, 0x03, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0xca, 0x01, 0x00, 0x00, 0x33, 0x44, 0x2f,
    0x33, 0x64, 0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x2e, 0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x50, 0x4b, 0x05, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0xb8, 0x00, 0x00, 0x00, 0x57, 0x03, 0x00, 0x00, 0x00, 0x00
};

const char* kPath = "dino8_kernel_threemf_test.3mf";

void WriteBytes(const std::string& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
std::string ReadBytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

uint32_t TestCrc(const std::string& s) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char b : s) {
    crc ^= b;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void P16(std::string& s, uint16_t v) { s.push_back(static_cast<char>(v & 255)); s.push_back(static_cast<char>(v >> 8)); }
void P32(std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 255)); }

// A stored ZIP built from (name, data) pairs.
std::string BuildZip(const std::vector<std::pair<std::string, std::string>>& files, bool corrupt_crc = false) {
  std::string out, central;
  for (const auto& f : files) {
    const uint32_t off = static_cast<uint32_t>(out.size());
    uint32_t crc = TestCrc(f.second);
    if (corrupt_crc) crc ^= 1u;
    P32(out, 0x04034b50u); P16(out, 20); P16(out, 0); P16(out, 0); P16(out, 0); P16(out, 0x21);
    P32(out, crc); P32(out, static_cast<uint32_t>(f.second.size())); P32(out, static_cast<uint32_t>(f.second.size()));
    P16(out, static_cast<uint16_t>(f.first.size())); P16(out, 0);
    out += f.first; out += f.second;
    P32(central, 0x02014b50u); P16(central, 20); P16(central, 20); P16(central, 0); P16(central, 0); P16(central, 0); P16(central, 0x21);
    P32(central, crc); P32(central, static_cast<uint32_t>(f.second.size())); P32(central, static_cast<uint32_t>(f.second.size()));
    P16(central, static_cast<uint16_t>(f.first.size())); P16(central, 0); P16(central, 0); P16(central, 0); P16(central, 0);
    P32(central, 0); P32(central, off);
    central += f.first;
  }
  const uint32_t cd_off = static_cast<uint32_t>(out.size());
  out += central;
  P32(out, 0x06054b50u); P16(out, 0); P16(out, 0); P16(out, static_cast<uint16_t>(files.size())); P16(out, static_cast<uint16_t>(files.size()));
  P32(out, static_cast<uint32_t>(central.size())); P32(out, cd_off); P16(out, 0);
  return out;
}

const std::string kTetVerts = "<vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/><vertex x=\"0\" y=\"1\" z=\"0\"/><vertex x=\"0\" y=\"0\" z=\"1\"/></vertices>";
const std::string kTetTris = "<triangles><triangle v1=\"0\" v2=\"2\" v3=\"1\"/><triangle v1=\"0\" v2=\"1\" v3=\"3\"/><triangle v1=\"0\" v2=\"3\" v3=\"2\"/><triangle v1=\"1\" v2=\"2\" v3=\"3\"/></triangles>";

std::string Model(const std::string& attrs, const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><model " + attrs + " xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">" + body + "</model>";
}
std::string Tet(int id, const std::string& extra = "") {
  return "<object id=\"" + std::to_string(id) + "\" type=\"model\"" + extra + "><mesh>" + kTetVerts + kTetTris + "</mesh></object>";
}
// Zip with only the model part (no rels, no content types).
std::string OnlyModel(const std::string& model) { return BuildZip({{"3D/3dmodel.model", model}}); }

bool Near(const Point3d& a, const Point3d& b, double tol = 1e-5) { return (a - b).Length() < tol; }

bool LoadBytes(const std::string& bytes, ThreeMfModel& model, const ThreeMfReadOptions& opt = {}) {
  WriteBytes(kPath, bytes);
  return LoadThreeMf(kPath, model, opt) == Result::Ok;
}

}  // namespace

D8_EXTRA_TEST(ThreeMfReadsAPythonDeflatedPackageWithComponentsTransformsAndAMirror) {
  WriteBytes(kPath, std::string(reinterpret_cast<const char*>(kFixture), sizeof(kFixture)));
  ThreeMfModel m;
  d8test::Check(LoadThreeMf(kPath, m) == Result::Ok, "the Python-deflated fixture reads");
  d8test::Check(m.unit == "inch" && m.parts.size() == 2, "unit inch, two build items = two parts");
  if (m.parts.size() != 2) { std::remove(kPath); return; }
  d8test::Check(m.parts[0].name == "pair" && m.parts[1].name == "tet", "part names come from the objects");
  const ON_Mesh& a = m.parts[0].mesh.raw();
  d8test::Check(a.m_V.Count() == 8 && a.m_F.Count() == 8, "the assembly flattens to 2 x (4 vertices, 4 triangles)");
  if (a.m_V.Count() == 8) {
    // Component 1: p' = (-y + 2, x, z); item: p'' = 2 p' + (0, 0, 10). Component 2: p' = (-x, y + 3, z); item the same.
    const double tet[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double worst = 0.0;
    for (int i = 0; i < 4; ++i) {
      const Point3d e1(2 * (-tet[i][1] + 2), 2 * tet[i][0], 2 * tet[i][2] + 10);
      const Point3d e2(2 * (-tet[i][0]), 2 * (tet[i][1] + 3), 2 * tet[i][2] + 10);
      worst = std::max({worst, (Point3d(a.m_V[i]) - e1).Length(), (Point3d(a.m_V[4 + i]) - e2).Length()});
    }
    d8test::Check(worst < 1e-5, ("component + item transforms, row-vector convention (worst " + std::to_string(worst) + ")").c_str());
  }
  // Volumes: the rotated copy is 8/6 (scale 2 cubes it); the mirrored copy has to keep a positive volume (its winding is flipped),
  // so the pair is 16/6; the plain tetrahedron is 1/6.
  d8test::Check(std::fabs(m.parts[0].mesh.Volume() - 16.0 / 6.0) < 1e-4, ("assembly volume 16/6 (" + std::to_string(m.parts[0].mesh.Volume()) + ")").c_str());
  d8test::Check(std::fabs(m.parts[1].mesh.Volume() - 1.0 / 6.0) < 1e-6, "plain tetrahedron volume 1/6");
  ThreeMfModel mm;
  ThreeMfReadOptions opt;
  opt.convert_to_millimeters = true;
  d8test::Check(LoadThreeMf(kPath, mm, opt) == Result::Ok && mm.unit == "millimeter" && mm.parts.size() == 2, "convert_to_millimeters reads");
  if (mm.parts.size() == 2) {
    d8test::Check(std::fabs(mm.parts[1].mesh.Volume() - 25.4 * 25.4 * 25.4 / 6.0) < 1e-2, "inch -> mm scales the volume by 25.4^3");
    d8test::Check(Near(Point3d(mm.parts[1].mesh.raw().m_V[1]), Point3d(25.4, 0, 0), 1e-4), "vertex (1,0,0) inch -> (25.4,0,0) mm");
  }
  // Mesh::Load3mf merges the two parts: 12 vertices, 12 triangles, indices rebased.
  Mesh merged;
  d8test::Check(Mesh::Load3mf(kPath, merged) == Result::Ok && merged.VertexCount() == 12 && merged.FaceCount() == 12, "Mesh::Load3mf merges every build item (12 vertices, 12 triangles)");
  d8test::Check(std::fabs(merged.Volume() - 17.0 / 6.0) < 1e-4, "the merged mesh keeps the combined volume 17/6 (indices were rebased)");
  std::remove(kPath);
}

D8_EXTRA_TEST(ThreeMfReaderHandlesRelationshipsBuildListsPrefixesAndEntities) {
  ThreeMfModel m;
  // Model part at a non-default path, found through _rels/.rels.
  {
    const std::string rels = "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/Other/part.model\" Id=\"r\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>";
    const std::string model = Model("unit=\"millimeter\"", "<resources>" + Tet(7) + "</resources><build><item objectid=\"7\"/></build>");
    d8test::Check(LoadBytes(BuildZip({{"_rels/.rels", rels}, {"Other/part.model", model}}), m) && m.parts.size() == 1 && m.parts[0].mesh.FaceCount() == 4, "model part located through the relationship (Other/part.model)");
  }
  // No <build>: every object nothing else uses as a component is built.
  {
    const std::string asm_obj = "<object id=\"3\"><components><component objectid=\"1\"/></components></object>";
    const std::string model = Model("", "<resources>" + Tet(1) + asm_obj + Tet(2) + "</resources>");
    d8test::Check(LoadBytes(OnlyModel(model), m) && m.parts.size() == 2, "no <build>: objects 3 and 2 are built (1 is only a component), 2 parts");
  }
  // A build item listing the same object twice gives two parts; item transform translates.
  {
    const std::string model = Model("unit=\"centimeter\"", "<resources>" + Tet(1) + "</resources><build><item objectid=\"1\"/><item objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 5 6 7\"/></build>");
    d8test::Check(LoadBytes(OnlyModel(model), m) && m.parts.size() == 2 && m.unit == "centimeter", "two items on one object = two parts, unit centimeter");
    if (m.parts.size() == 2) d8test::Check(Near(Point3d(m.parts[1].mesh.raw().m_V[1]), Point3d(6, 6, 7)), "item transform translates (1,0,0) to (6,6,7)");
  }
  // Namespace prefixes on elements / attributes, extension attributes, comments, CDATA metadata, numeric and named entities.
  {
    const std::string model =
        "<?xml version=\"1.0\"?><m:model unit=\"meter\" xmlns:m=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" xmlns:p=\"http://schemas.microsoft.com/3dmanufacturing/production/2015/06\">"
        "<!-- a comment --><m:metadata name=\"Title\"><![CDATA[a < b]]></m:metadata>"
        "<m:resources><m:object id=\"1\" name=\"a&amp;b &#65;&#x42; &lt;&quot;\" p:UUID=\"x\"><m:mesh><m:vertices>"
        "<m:vertex x=\"0\" y=\"0\" z=\"0\"/><m:vertex x=\"1e0\" y=\"0\" z=\"0\"/><m:vertex x=\" 0 \" y=\"+1\" z=\"0.0\"/><m:vertex x=\"0\" y=\"0\" z=\"1.0E+0\"/></m:vertices>"
        "<m:triangles><m:triangle v1=\"0\" v2=\"2\" v3=\"1\"/><m:triangle v1=\"0\" v2=\"1\" v3=\"3\"/><m:triangle v1=\"0\" v2=\"3\" v3=\"2\"/><m:triangle v1=\"1\" v2=\"2\" v3=\"3\"/></m:triangles></m:mesh></m:object></m:resources>"
        "<m:build><m:item objectid=\"1\"/></m:build></m:model>";
    d8test::Check(LoadBytes(OnlyModel(model), m) && m.parts.size() == 1 && m.unit == "meter", "prefixed elements, comments, CDATA, extension attributes read");
    if (m.parts.size() == 1) {
      d8test::Check(m.parts[0].name == "a&b AB <\"", ("entities decode in the object name (\"" + m.parts[0].name + "\")").c_str());
      d8test::Check(std::fabs(m.parts[0].mesh.Volume() - 1.0 / 6.0) < 1e-6, "exponent / sign / padded numbers parse (volume 1/6)");
    }
  }
}

D8_EXTRA_TEST(ThreeMfReaderRefusesWhatItCannotTrust) {
  ThreeMfModel sentinel;
  sentinel.unit = "sentinel";
  auto refuses = [&](const std::string& bytes, const char* what) {
    ThreeMfModel m = sentinel;
    WriteBytes(kPath, bytes);
    const Result r = LoadThreeMf(kPath, m);
    d8test::Check(r == Result::Failed && m.unit == "sentinel" && m.parts.empty(), (std::string(what) + ": Failed, model untouched").c_str());
  };
  const std::string good = OnlyModel(Model("unit=\"millimeter\"", "<resources>" + Tet(1) + "</resources><build><item objectid=\"1\"/></build>"));
  { ThreeMfModel m; d8test::Check(LoadBytes(good, m) && m.parts.size() == 1, "control: the unmodified stored package reads"); }
  const std::string res = "<resources>" + Tet(1) + "</resources>";
  refuses("this is not a zip", "not a ZIP");
  refuses(good.substr(0, good.size() / 2), "truncated ZIP");
  refuses(BuildZip({{"3D/3dmodel.model", Model("", res + "<build><item objectid=\"1\"/></build>")}}, true), "bad CRC");
  refuses(BuildZip({{"readme.txt", "x"}}), "no model part");
  refuses(OnlyModel(Model("unit=\"furlong\"", res)), "unknown unit");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><mesh><vertices><vertex x=\"0\" y=\"0\"/></vertices><triangles/></mesh></object></resources>")), "vertex missing z");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"abc\"/></vertices><triangles/></mesh></object></resources>")), "vertex z not a number");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"nan\"/></vertices><triangles/></mesh></object></resources>")), "vertex z NaN");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><mesh>" + kTetVerts + "<triangles><triangle v1=\"0\" v2=\"1\" v3=\"4\"/></triangles></mesh></object></resources>")), "triangle index past the vertex list");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><mesh>" + kTetVerts + "<triangles><triangle v1=\"0\" v2=\"1\" v3=\"-1\"/></triangles></mesh></object></resources>")), "negative triangle index");
  refuses(OnlyModel(Model("", res + "<build><item objectid=\"9\"/></build>")), "build item naming a missing object");
  refuses(OnlyModel(Model("", res + "<build><item objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 0 0\"/></build>")), "transform with 11 numbers");
  refuses(OnlyModel(Model("", res + "<build><item objectid=\"1\" transform=\"1 0 0 0 1 0 0 0 1 0 0 inf\"/></build>")), "transform with a non-finite number");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><components><component objectid=\"1\"/></components></object></resources><build><item objectid=\"1\"/></build>")), "a component naming its own object (cycle)");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\"><components><component objectid=\"2\"/></components></object><object id=\"2\"><components><component objectid=\"1\"/></components></object></resources><build><item objectid=\"1\"/></build>")), "a two-object component cycle");
  refuses(OnlyModel(Model("", res + "<resources><object id=\"2\"><components><component objectid=\"1\" path=\"/3D/other.model\"/></components></object></resources>")), "external-part component (and a second <resources>)");
  refuses(OnlyModel(Model("", "<resources>" + Tet(1) + Tet(1) + "</resources>")), "duplicate object ids");
  refuses(OnlyModel("<!DOCTYPE model [<!ENTITY x \"y\">]>" + Model("", res)), "DOCTYPE / entity declaration");
  refuses(OnlyModel(Model("", "<resources><object id=\"1\" name=\"&bogus;\"><mesh>" + kTetVerts + kTetTris + "</mesh></object></resources>")), "undefined entity");
  refuses(OnlyModel(Model("", "<resources></resources><build></build>")), "no geometry at all");
  refuses(OnlyModel(Model("", res + "<build><item objectid=\"1\"")), "XML truncated mid-tag");
  refuses(OnlyModel(Model("", res) + "<extra/>"), "a second root element");
  {
    // A deflate entry that is cut short: take the real deflated fixture and zero its compressed model data.
    std::string f(reinterpret_cast<const char*>(kFixture), sizeof(kFixture));
    const size_t at = f.find("3D/3dmodel.model");
    if (at != std::string::npos) {
      for (size_t k = at + 16 + 4; k < at + 16 + 40 && k < f.size(); ++k) f[k] = static_cast<char>(0xFF);
    }
    refuses(f, "corrupted deflate stream");
  }
  { ThreeMfModel m; d8test::Check(!LoadBytes("", m), "an empty file is refused"); }
  { ThreeMfModel m; d8test::Check(LoadThreeMf("dino8_kernel_threemf_does_not_exist.3mf", m) == Result::Failed, "a missing file is refused"); }
  std::remove(kPath);
}

D8_EXTRA_TEST(ThreeMfWriterOutputDecodesWithAnIndependentScan) {
  // A mesh with a quad (split into two triangles) and awkward float coordinates, plus a second part with a name that needs escaping.
  Mesh a;
  {
    ON_Mesh& r = a.raw();
    r.m_V.Append(ON_3fPoint(0.1f, 123.456789f, -1e-7f));
    r.m_V.Append(ON_3fPoint(1.0f / 3.0f, 0.0f, 2.5f));
    r.m_V.Append(ON_3fPoint(1.0f, 1.0f, 1.0f));
    r.m_V.Append(ON_3fPoint(0.0f, 1.0f, 1.0f));
    ON_MeshFace q;
    q.vi[0] = 0; q.vi[1] = 1; q.vi[2] = 2; q.vi[3] = 3;
    r.m_F.Append(q);
  }
  Mesh b;
  {
    ON_Mesh& r = b.raw();
    r.m_V.Append(ON_3fPoint(0, 0, 0)); r.m_V.Append(ON_3fPoint(1, 0, 0)); r.m_V.Append(ON_3fPoint(0, 1, 0));
    ON_MeshFace t;
    t.vi[0] = 0; t.vi[1] = 1; t.vi[2] = 2; t.vi[3] = 2;
    r.m_F.Append(t);
  }
  ThreeMfModel model;
  model.unit = "centimeter";
  model.parts.push_back(ThreeMfPart{"first <&>", a});
  model.parts.push_back(ThreeMfPart{"second \"q\"", b});
  ThreeMfWriteOptions stored;
  stored.compress_entries = false;
  d8test::Check(SaveThreeMf(kPath, model, stored) == Result::Ok, "stored 3MF saves");
  const std::string z = ReadBytes(kPath);
  // Independent scan: find the three entry names and the model text (stored, so the bytes are right there).
  d8test::Check(z.compare(0, 4, "PK\x03\x04") == 0, "ZIP local header signature");
  const size_t m0 = z.find("<model");
  d8test::Check(m0 != std::string::npos, "the model XML is present as plain text in a stored package");
  const std::string xml = m0 == std::string::npos ? "" : z.substr(m0, z.find("</model>") - m0 + 8);
  d8test::Check(z.find("[Content_Types].xml") != std::string::npos && z.find("_rels/.rels") != std::string::npos && z.find("3D/3dmodel.model") != std::string::npos, "three OPC parts");
  d8test::Check(xml.find("unit=\"centimeter\"") != std::string::npos, "unit attribute written");
  d8test::Check(xml.find("name=\"first &lt;&amp;&gt;\"") != std::string::npos && xml.find("name=\"second &quot;q&quot;\"") != std::string::npos, "names are XML-escaped");
  auto count = [&](const std::string& needle) { size_t n = 0, p = 0; while ((p = xml.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); } return n; };
  d8test::Check(count("<object ") == 2 && count("<item ") == 2 && count("<vertex ") == 7 && count("<triangle ") == 3, "2 objects, 2 build items, 4 + 3 vertices, 2 + 1 triangles (the quad is split)");
  // Every written coordinate parses back to exactly the float that was stored (shortest round-trip text).
  bool exact = true;
  size_t p = 0;
  const float expect[4][3] = {{0.1f, 123.456789f, -1e-7f}, {1.0f / 3.0f, 0.0f, 2.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}};
  for (int i = 0; i < 4; ++i) {
    p = xml.find("<vertex ", p);
    for (int c = 0; c < 3; ++c) {
      const std::string key = std::string(1, "xyz"[c]) + "=\"";
      const size_t k = xml.find(key, p) + key.size();
      const float got = std::strtof(xml.c_str() + k, nullptr);
      if (got != expect[i][c]) exact = false;
    }
    p += 8;
  }
  d8test::Check(exact, "the 12 coordinates of part one parse back to exactly the stored floats (0.1, 123.456789, -1e-7, 1/3 ...)");
  // Round trip through the reader: two parts, names (unescaped), unit, counts.
  ThreeMfModel back;
  d8test::Check(LoadThreeMf(kPath, back) == Result::Ok && back.unit == "centimeter" && back.parts.size() == 2, "the writer's package reads back: unit and two parts");
  if (back.parts.size() == 2) {
    d8test::Check(back.parts[0].name == "first <&>" && back.parts[1].name == "second \"q\"", "names round-trip through the escaping");
    d8test::Check(back.parts[0].mesh.VertexCount() == 4 && back.parts[0].mesh.FaceCount() == 2 && back.parts[1].mesh.FaceCount() == 1, "counts round-trip (the quad is two triangles)");
    bool same = true;
    for (int i = 0; i < 4; ++i) same = same && back.parts[0].mesh.raw().m_V[i] == a.raw().m_V[i];
    d8test::Check(same, "vertices round-trip bit for bit");
  }
  // Compressed: smaller, deflate method (8) in the central directory, same content on read.
  ThreeMfModel big;
  big.unit = "inch";
  {
    Mesh grid;
    ON_Mesh& r = grid.raw();
    for (int j = 0; j <= 30; ++j) for (int i = 0; i <= 30; ++i) r.m_V.Append(ON_3fPoint(static_cast<float>(i), static_cast<float>(j), static_cast<float>((i * j) % 7)));
    for (int j = 0; j < 30; ++j) for (int i = 0; i < 30; ++i) { ON_MeshFace f; f.vi[0] = j * 31 + i; f.vi[1] = j * 31 + i + 1; f.vi[2] = (j + 1) * 31 + i + 1; f.vi[3] = (j + 1) * 31 + i; r.m_F.Append(f); }
    big.parts.push_back(ThreeMfPart{"grid", grid});
  }
  d8test::Check(SaveThreeMf(kPath, big, stored) == Result::Ok, "big stored saves");
  const size_t stored_size = ReadBytes(kPath).size();
  d8test::Check(SaveThreeMf(kPath, big) == Result::Ok, "big compressed saves");
  const std::string cz = ReadBytes(kPath);
  d8test::Check(cz.size() * 3 < stored_size, ("deflate shrinks the package at least 3x (" + std::to_string(stored_size) + " -> " + std::to_string(cz.size()) + ")").c_str());
  const size_t cd = cz.find(std::string("PK\x01\x02", 4));
  d8test::Check(cd != std::string::npos, "central directory present");
  // The model entry's central record is the third; find the record whose name is 3D/3dmodel.model and read its method field (offset 10).
  size_t method = 99;
  for (size_t q = cd; q != std::string::npos; q = cz.find(std::string("PK\x01\x02", 4), q + 4)) {
    if (cz.compare(q + 46, 16, "3D/3dmodel.model") == 0) method = static_cast<unsigned char>(cz[q + 10]) | (static_cast<unsigned char>(cz[q + 11]) << 8);
  }
  d8test::Check(method == 8, "the model part is stored with ZIP method 8 (deflate)");
  ThreeMfModel cback;
  d8test::Check(LoadThreeMf(kPath, cback) == Result::Ok && cback.parts.size() == 1 && cback.parts[0].mesh.VertexCount() == 961 && cback.parts[0].mesh.FaceCount() == 1800, "the compressed package reads back (961 vertices, 1800 triangles)");
  std::remove(kPath);
}

D8_EXTRA_TEST(ThreeMfWriterRefusesBadInput) {
  auto fails = [&](const ThreeMfModel& m, const char* what) {
    std::remove(kPath);
    d8test::Check(SaveThreeMf(kPath, m) == Result::Failed, (std::string(what) + ": Failed").c_str());
  };
  Mesh tri;
  {
    ON_Mesh& r = tri.raw();
    r.m_V.Append(ON_3fPoint(0, 0, 0)); r.m_V.Append(ON_3fPoint(1, 0, 0)); r.m_V.Append(ON_3fPoint(0, 1, 0));
    ON_MeshFace t; t.vi[0] = 0; t.vi[1] = 1; t.vi[2] = 2; t.vi[3] = 2; r.m_F.Append(t);
  }
  ThreeMfModel m;
  fails(m, "no parts");
  m.parts.push_back(ThreeMfPart{"t", tri});
  m.unit = "furlong";
  fails(m, "unknown unit");
  m.unit = "millimeter";
  { ThreeMfModel e = m; e.parts[0].mesh = Mesh(); fails(e, "an empty mesh"); }
  { ThreeMfModel e = m; e.parts[0].mesh.raw().m_V[0].x = std::nanf(""); fails(e, "a NaN vertex"); }
  { ThreeMfModel e = m; e.parts[0].mesh.raw().m_F[0].vi[1] = 9; fails(e, "a triangle index past the vertex list"); }
  d8test::Check(SaveThreeMf("dino8_kernel_no_such_dir/x.3mf", m) == Result::Failed, "an unwritable path");
  d8test::Check(dino8::kernel::ThreeMfUnitToMillimeters("inch") == 25.4 && dino8::kernel::ThreeMfUnitToMillimeters("micron") == 0.001 && dino8::kernel::ThreeMfUnitToMillimeters("meter") == 1000.0 && dino8::kernel::ThreeMfUnitToMillimeters("furlong") == 0.0, "unit table: inch 25.4, micron 0.001, meter 1000, unknown 0");
  std::remove(kPath);
}

D8_EXTRA_TEST(ThreeMfMeshEntryPointsStillRoundTripAClosedMesh) {
  // The old Mesh::Save3mf / Load3mf entry points (now thin wrappers) keep their behaviour on a closed solid.
  Mesh tet;
  {
    ON_Mesh& r = tet.raw();
    r.m_V.Append(ON_3fPoint(0, 0, 0)); r.m_V.Append(ON_3fPoint(1, 0, 0)); r.m_V.Append(ON_3fPoint(0, 1, 0)); r.m_V.Append(ON_3fPoint(0, 0, 1));
    const int t[4][3] = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    for (const auto& f : t) { ON_MeshFace face; face.vi[0] = f[0]; face.vi[1] = f[1]; face.vi[2] = f[2]; face.vi[3] = f[2]; r.m_F.Append(face); }
  }
  d8test::Check(tet.Save3mf(kPath) == Result::Ok, "Mesh::Save3mf succeeds");
  Mesh back;
  d8test::Check(Mesh::Load3mf(kPath, back) == Result::Ok && back.VertexCount() == 4 && back.FaceCount() == 4 && std::fabs(back.Volume() - 1.0 / 6.0) < 1e-9, "Mesh::Load3mf reads it back (4 vertices, 4 triangles, volume 1/6)");
  Mesh empty;
  d8test::Check(empty.Save3mf(kPath) == Result::Failed, "an empty mesh is not written (a 3MF mesh needs vertices and triangles)");
  std::remove(kPath);
}
