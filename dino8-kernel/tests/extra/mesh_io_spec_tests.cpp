// Mesh exchange formats against their public descriptions (Wavefront OBJ, Stanford PLY, Geomview OFF).
//
// READ side: the fixtures are written by tests/extra/mesh_io_fixtures/gen_mesh_io_fixtures.py with Python's own struct /
// text formatting (not by the kernel), embedded here through mesh_io_fixtures.inc, and the expected values below are the
// numbers that script put into them (coordinates, indices, colours, shoelace area of the pentagon).
// WRITE side: SavePly (both byte orders) / SaveOff / SaveObj output is decoded by small readers written in this file from
// the format descriptions only (header-driven binary decoding, plain token parsing), never through Mesh::Load*.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <string>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "test_registry.h"

#include "mesh_io_fixtures.inc"

namespace {

using dino8::kernel::Color;
using dino8::kernel::Mesh;
using dino8::kernel::Point2d;
using dino8::kernel::Result;

std::string TempPath(const std::string& leaf) {
  return (std::filesystem::temp_directory_path() / ("d8_meshio_" + leaf)).string();
}

// Writes the embedded fixture `name` to a temp file and returns its path.
std::string Fixture(const char* name) {
  for (const MeshIoFixture& f : kMeshIoFixtures) {
    if (std::string(f.name) == name) {
      const std::string path = TempPath(f.name);
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(f.bytes), static_cast<std::streamsize>(f.size));
      return path;
    }
  }
  return std::string();
}

std::vector<int> FaceCorners(const Mesh& m, int fi) {
  const ON_MeshFace& f = m.raw().m_F[fi];
  std::vector<int> c = {f.vi[0], f.vi[1], f.vi[2]};
  if (f.IsQuad()) c.push_back(f.vi[3]);
  return c;
}

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

bool VertexIs(const Mesh& m, int vi, double x, double y, double z) {
  if (vi < 0 || vi >= m.VertexCount()) return false;
  const ON_3fPoint& p = m.raw().m_V[vi];
  return Near(p.x, x) && Near(p.y, y) && Near(p.z, z);
}

double FaceArea(const Mesh& m, int fi) {
  const std::vector<int> c = FaceCorners(m, fi);
  auto P = [&](int i) { return ON_3dPoint(m.raw().m_V[c[static_cast<size_t>(i)]]); };
  double a = 0.5 * ON_CrossProduct(P(1) - P(0), P(2) - P(0)).Length();
  if (c.size() == 4) a += 0.5 * ON_CrossProduct(P(2) - P(0), P(3) - P(0)).Length();
  return a;
}

// ------------------------------------------------------------------------------------------------------------- OBJ

D8_EXTRA_TEST(MeshIoObjNegativeIndicesCrlfGroupMaterial) {
  Mesh m;
  const std::string path = Fixture("obj_neg_crlf.obj");
  d8test::Check(Mesh::LoadObj(path, m) == Result::Ok, "OBJ CRLF + negative indices: loads");
  d8test::Check(m.VertexCount() == 5 && m.FaceCount() == 2, "OBJ CRLF + negative indices: 5 vertices, 2 faces (the quad stays one quad)");
  if (m.VertexCount() == 5 && m.FaceCount() == 2) {
    d8test::Check(VertexIs(m, 2, 2, 3, 0) && VertexIs(m, 4, 0, 0, 1), "OBJ CRLF: vertex coordinates read exactly");
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2, 3}), "OBJ: 'f -5 -4 -3 -2' is the quad 1 2 3 4 (relative to the 5 vertices declared so far)");
    d8test::Check(FaceCorners(m, 1) == std::vector<int>({3, 4, 0}), "OBJ: 'f -2 -1 -5' is the triangle 4 5 1");
    d8test::Check(m.HasFaceGroupNames() && m.FaceGroupNameAt(0) == "top" && m.FaceGroupNameAt(1) == "top", "OBJ CRLF: group name is 'top' (no carriage return in it)");
    d8test::Check(m.HasFaceMaterialNames() && m.FaceMaterialNameAt(0) == "red" && m.FaceMaterialNameAt(1) == "red", "OBJ CRLF: material name is 'red' (no carriage return in it)");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoObjTrailingCommentsAfterData) {
  Mesh m;
  const std::string path = Fixture("obj_trailing_comment.obj");
  d8test::Check(Mesh::LoadObj(path, m) == Result::Ok, "OBJ: '#' comments after v / f data are ignored, the file loads");
  if (m.FaceCount() == 2 && m.VertexCount() == 4) {
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2}) && FaceCorners(m, 1) == std::vector<int>({0, 2, 3}),
                  "OBJ trailing comments: both triangles read (1 2 3) and (1 3 4)");
    d8test::Check(VertexIs(m, 0, 0, 0, 0) && VertexIs(m, 1, 1, 0, 0), "OBJ trailing comments: vertex lines keep their coordinates");
  } else {
    d8test::Check(false, "OBJ trailing comments: expected 4 vertices and 2 faces");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoObjBackslashLineContinuation) {
  Mesh m;
  const std::string path = Fixture("obj_continuation.obj");
  d8test::Check(Mesh::LoadObj(path, m) == Result::Ok, "OBJ: a line ending in a backslash continues on the next line, the file loads");
  if (m.VertexCount() == 5 && m.FaceCount() == 2) {
    d8test::Check(VertexIs(m, 4, 0, 0, 2), "OBJ continuation: 'v 0 0 \\ / 2' is the vertex (0,0,2)");
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2, 3}), "OBJ continuation: 'f 1 2 \\ / 3 4' is one quad (1 2 3 4)");
    d8test::Check(FaceCorners(m, 1) == std::vector<int>({0, 2, 4}), "OBJ continuation: the next face is the triangle 1 3 5");
  } else {
    d8test::Check(false, "OBJ continuation: expected 5 vertices and 2 faces");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoObjVtOneTwoThreeComponents) {
  Mesh m;
  const std::string path = Fixture("obj_vt_dims.obj");
  d8test::Check(Mesh::LoadObj(path, m) == Result::Ok, "OBJ: vt with 1, 2 and 3 components (u [v] [w]) loads");
  if (m.VertexCount() == 3 && m.HasTextureCoordinates()) {
    const Point2d t0 = m.TextureCoordinateAt(0), t1 = m.TextureCoordinateAt(1), t2 = m.TextureCoordinateAt(2);
    d8test::Check(Near(t0.x, 0.25) && Near(t0.y, 0.0), "OBJ 'vt 0.25' is (u, v) = (0.25, 0)");
    d8test::Check(Near(t1.x, 0.5) && Near(t1.y, 0.75), "OBJ 'vt 0.5 0.75' is (0.5, 0.75)");
    d8test::Check(Near(t2.x, 1.0) && Near(t2.y, 0.5), "OBJ 'vt 1 0.5 0.0' is (1, 0.5) (w is dropped)");
  } else {
    d8test::Check(false, "OBJ vt dimensions: expected 3 vertices carrying texture coordinates");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoObjVertexExtrasTabsWColourExponent) {
  Mesh m;
  const std::string path = Fixture("obj_vertex_extras.obj");
  d8test::Check(Mesh::LoadObj(path, m) == Result::Ok, "OBJ: tab separators, 'v x y z w', 'v x y z r g b', exponent / plus-sign numbers, s / l / p lines load");
  if (m.VertexCount() == 3 && m.FaceCount() == 1) {
    d8test::Check(VertexIs(m, 0, 0, 0, 0), "OBJ: tab separated 'v 0 0 0 1.0' (w) is the point (0,0,0)");
    d8test::Check(VertexIs(m, 1, 1, 0, 0), "OBJ: 'v 1e0 0 0 0.5 0.5 0.5' (colour extension) is the point (1,0,0)");
    d8test::Check(VertexIs(m, 2, 2.5, 0.1, 0), "OBJ: 'v +2.5 1E-1 0' is the point (2.5, 0.1, 0)");
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2}), "OBJ: 'f 1//1 2//2 3//1' (v//vn) is the triangle 1 2 3");
    d8test::Check(m.HasFaceGroupNames() && m.FaceGroupNameAt(0) == "a", "OBJ: 'g a b' names the group 'a' (first name)");
  } else {
    d8test::Check(false, "OBJ vertex extras: expected 3 vertices and 1 face");
  }
  std::remove(path.c_str());
}

// ------------------------------------------------------------------------------------------------------------- PLY

D8_EXTRA_TEST(MeshIoPlyLittleEndianDoubleUshortExtraProperties) {
  Mesh m;
  const std::string path = Fixture("ply_le_double_ushort.ply");
  d8test::Check(Mesh::LoadPly(path, m) == Result::Ok, "PLY binary_little_endian: double x y z, extra float / uchar vertex properties, 'list uchar ushort' faces load");
  if (m.VertexCount() == 5 && m.FaceCount() == 2) {
    d8test::Check(VertexIs(m, 2, 4, 2, 0) && VertexIs(m, 4, 0, 0, 3), "PLY LE double positions read exactly");
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2, 3}), "PLY LE ushort list: the quad 0 1 2 3");
    d8test::Check(FaceCorners(m, 1) == std::vector<int>({0, 4, 1}), "PLY LE ushort list: the triangle 0 4 1");
  } else {
    d8test::Check(false, "PLY LE double: expected 5 vertices and 2 faces");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoPlyBigEndianColorAlphaUv) {
  Mesh m;
  const std::string path = Fixture("ply_be_float_color.ply");
  d8test::Check(Mesh::LoadPly(path, m) == Result::Ok, "PLY binary_big_endian with colour, alpha and u v loads");
  if (m.VertexCount() == 3 && m.FaceCount() == 1) {
    d8test::Check(VertexIs(m, 1, 2, 0, 0) && VertexIs(m, 2, 0, 2, 0), "PLY BE float positions read exactly");
    d8test::Check(m.HasVertexColors() && m.VertexColorAt(0).r == 255 && m.VertexColorAt(1).g == 255 && m.VertexColorAt(2).b == 255,
                  "PLY BE: vertex colours (255,0,0) (0,255,0) (0,0,255)");
    d8test::Check(m.HasVertexAlpha() && m.VertexAlphaAt(0) == 200 && m.VertexAlphaAt(1) == 100 && m.VertexAlphaAt(2) == 50, "PLY BE: alpha 200 100 50");
    d8test::Check(m.HasTextureCoordinates() && Near(m.TextureCoordinateAt(1).x, 1.0) && Near(m.TextureCoordinateAt(2).y, 0.5), "PLY BE: u v read");
  } else {
    d8test::Check(false, "PLY BE: expected 3 vertices and 1 face");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoPlyNgonFaceIsTriangulated) {
  Mesh m;
  const std::string path = Fixture("ply_ngon.ply");
  d8test::Check(Mesh::LoadPly(path, m) == Result::Ok, "PLY: a 5-corner face (any list length is valid PLY), comment / obj_info lines: loads");
  if (m.VertexCount() == 6) {
    // the pentagon becomes 3 triangles; with the separate triangle that is 4 faces. Their areas add up to the shoelace area.
    d8test::Check(m.FaceCount() == 4, "PLY pentagon: triangulated into 3 faces, plus the triangle = 4 faces");
    double total = 0;
    for (int i = 0; i < m.FaceCount(); ++i) total += FaceArea(m, i);
    d8test::Check(Near(total, kPlyNgonPentagonArea + kPlyNgonTriangleArea, 1e-4), "PLY pentagon: total face area = shoelace pentagon area + triangle area");
  } else {
    d8test::Check(false, "PLY n-gon: expected 6 vertices");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoPlyFaceElementExtraProperties) {
  for (const char* name : {"ply_face_props_ascii.ply", "ply_face_props_le.ply"}) {
    Mesh m;
    const std::string path = Fixture(name);
    const std::string w = std::string("PLY ") + name;
    d8test::Check(Mesh::LoadPly(path, m) == Result::Ok, (w + ": a face element with extra per-face properties (colour / quality) loads").c_str());
    if (m.VertexCount() == 4 && m.FaceCount() == 2) {
      d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2}) && FaceCorners(m, 1) == std::vector<int>({0, 2, 3}),
                    (w + ": the two triangles keep their own indices").c_str());
    } else {
      d8test::Check(false, (w + ": expected 4 vertices and 2 faces").c_str());
    }
    std::remove(path.c_str());
  }
}

D8_EXTRA_TEST(MeshIoPlyAsciiCrlf) {
  Mesh m;
  const std::string path = Fixture("ply_crlf.ply");
  d8test::Check(Mesh::LoadPly(path, m) == Result::Ok, "PLY ascii with CRLF line endings loads");
  if (m.VertexCount() == 3 && m.FaceCount() == 1) {
    d8test::Check(VertexIs(m, 1, 3, 0, 0) && VertexIs(m, 2, 0, 4, 0) && FaceCorners(m, 0) == std::vector<int>({0, 1, 2}), "PLY CRLF: vertices and face read exactly");
  } else {
    d8test::Check(false, "PLY CRLF: expected 3 vertices and 1 face");
  }
  std::remove(path.c_str());
}

// ------------------------------------------------------------------------------------------------------------- OFF

D8_EXTRA_TEST(MeshIoOffPerFaceColors) {
  Mesh m;
  const std::string path = Fixture("off_face_colors.off");
  d8test::Check(Mesh::LoadOff(path, m) == Result::Ok, "OFF: per-face colours after the indices (r g b ints, r g b a floats) are accepted");
  if (m.VertexCount() == 4 && m.FaceCount() == 2) {
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2}), "OFF face colours: the first face is the triangle 0 1 2");
    d8test::Check(FaceCorners(m, 1) == std::vector<int>({0, 1, 2, 3}), "OFF face colours: the second face is the quad 0 1 2 3");
  } else {
    d8test::Check(false, "OFF face colours: expected 4 vertices and 2 faces");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoOffFacesOnOneLine) {
  Mesh m;
  const std::string path = Fixture("off_faces_one_line.off");
  d8test::Check(Mesh::LoadOff(path, m) == Result::Ok, "OFF: two faces on one line load (tokens, not lines)");
  if (m.VertexCount() == 4 && m.FaceCount() == 2) {
    d8test::Check(FaceCorners(m, 0) == std::vector<int>({0, 1, 2}) && FaceCorners(m, 1) == std::vector<int>({0, 2, 3}),
                  "OFF one line: the faces are (0 1 2) and (0 2 3), the second is not mistaken for a colour");
  } else {
    d8test::Check(false, "OFF one line: expected 4 vertices and 2 faces");
  }
  std::remove(path.c_str());
}

D8_EXTRA_TEST(MeshIoOffCrlfCommentsColoursAlpha) {
  Mesh m;
  const std::string path = Fixture("off_crlf_comments_coff.off");
  d8test::Check(Mesh::LoadOff(path, m) == Result::Ok, "COFF with CRLF line endings and comments loads");
  if (m.VertexCount() == 3 && m.FaceCount() == 1) {
    d8test::Check(m.HasVertexColors() && m.VertexColorAt(0).r == 255 && m.VertexColorAt(1).g == 255 && m.VertexColorAt(2).b == 255, "COFF: vertex colours");
    d8test::Check(m.HasVertexAlpha() && m.VertexAlphaAt(0) == 255 && m.VertexAlphaAt(1) == 128 && m.VertexAlphaAt(2) == 7, "COFF: alpha 255 128 7");
    d8test::Check(VertexIs(m, 1, 2, 0, 0) && FaceCorners(m, 0) == std::vector<int>({0, 1, 2}), "COFF: positions and face");
  } else {
    d8test::Check(false, "COFF: expected 3 vertices and 1 face");
  }
  std::remove(path.c_str());
}

// ------------------------------------------------------------------------------------- writers, independent readers

// A mesh with a quad and a triangle, vertex colours, texture coordinates and per-face group names, built directly.
Mesh MakeWriterMesh() {
  Mesh m;
  ON_Mesh& raw = m.raw();
  const double pts[5][3] = {{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}, {0, 0, 1}};
  for (const auto& p : pts) raw.m_V.Append(ON_3fPoint(p[0], p[1], p[2]));
  ON_MeshFace q;
  q.vi[0] = 0; q.vi[1] = 1; q.vi[2] = 2; q.vi[3] = 3;
  raw.m_F.Append(q);
  ON_MeshFace t;
  t.vi[0] = 3; t.vi[1] = 4; t.vi[2] = 0; t.vi[3] = 0;
  raw.m_F.Append(t);
  m.SetVertexColors({Color{255, 0, 0}, Color{0, 255, 0}, Color{0, 0, 255}, Color{10, 20, 30}, Color{200, 100, 50}});
  m.SetTextureCoordinates({Point2d(0, 0), Point2d(1, 0), Point2d(1, 1), Point2d(0, 1), Point2d(0.5, 0.25)});
  m.SetFaceGroupNames({"top", "side"});
  return m;
}

std::vector<unsigned char> ReadAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct PlyProp { std::string type, name; bool list = false; std::string count_type; };
struct PlyElem { std::string name; int count = 0; std::vector<PlyProp> props; };

int PlyTypeSize(const std::string& t) {
  if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
  if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
  if (t == "int" || t == "uint" || t == "float" || t == "int32" || t == "uint32" || t == "float32") return 4;
  if (t == "double" || t == "float64") return 8;
  return 0;
}

// Reads one binary scalar of PLY type `t` at bytes[pos] (advancing pos), in the byte order the header declared.
double PlyScalar(const std::vector<unsigned char>& b, size_t& pos, const std::string& t, bool big) {
  const int n = PlyTypeSize(t);
  unsigned char raw[8] = {0};
  for (int i = 0; i < n; ++i) raw[i] = b[pos + static_cast<size_t>(big ? n - 1 - i : i)];  // normalise to little endian
  pos += static_cast<size_t>(n);
  if (t == "float" || t == "float32") { float f; std::memcpy(&f, raw, 4); return f; }
  if (t == "double" || t == "float64") { double d; std::memcpy(&d, raw, 8); return d; }
  uint64_t u = 0;
  for (int i = n - 1; i >= 0; --i) u = (u << 8) | raw[i];
  if (t == "char" || t == "int8") return static_cast<int8_t>(u);
  if (t == "short" || t == "int16") return static_cast<int16_t>(u);
  if (t == "int" || t == "int32") return static_cast<int32_t>(u);
  return static_cast<double>(u);
}

void CheckSavedPly(bool big) {
  const std::string tag = big ? "SavePly binary_big_endian" : "SavePly binary_little_endian";
  const Mesh m = MakeWriterMesh();
  const std::string path = TempPath(big ? "saved_be.ply" : "saved_le.ply");
  d8test::Check(m.SavePly(path, true, big) == Result::Ok, (tag + ": writes").c_str());
  const std::vector<unsigned char> bytes = ReadAll(path);
  std::remove(path.c_str());
  const std::string marker = "end_header\n";
  size_t body = std::string::npos;
  for (size_t i = 0; i + marker.size() <= bytes.size(); ++i) {
    if (std::memcmp(&bytes[i], marker.data(), marker.size()) == 0) { body = i + marker.size(); break; }
  }
  if (body == std::string::npos) { d8test::Check(false, (tag + ": no 'end_header' line").c_str()); return; }
  std::istringstream header(std::string(reinterpret_cast<const char*>(bytes.data()), body));
  std::string line, format;
  std::vector<PlyElem> elems;
  bool first = true, ok_magic = false;
  while (std::getline(header, line)) {
    std::istringstream ls(line);
    std::string w;
    ls >> w;
    if (first) { ok_magic = (w == "ply"); first = false; continue; }
    if (w == "format") { ls >> format; }
    else if (w == "element") { PlyElem e; ls >> e.name >> e.count; elems.push_back(e); }
    else if (w == "property" && !elems.empty()) {
      PlyProp p;
      ls >> p.type;
      if (p.type == "list") { p.list = true; ls >> p.count_type >> p.type >> p.name; } else { ls >> p.name; }
      elems.back().props.push_back(p);
    }
  }
  d8test::Check(ok_magic, (tag + ": starts with 'ply'").c_str());
  d8test::Check(format == (big ? "binary_big_endian" : "binary_little_endian"), (tag + ": header declares the requested byte order").c_str());
  size_t pos = body;
  std::vector<std::map<std::string, double>> verts;
  std::vector<std::vector<int>> faces;
  bool decode_ok = true;
  for (const PlyElem& e : elems) {
    for (int r = 0; r < e.count && decode_ok; ++r) {
      std::map<std::string, double> row;
      std::vector<int> idx;
      for (const PlyProp& p : e.props) {
        const int sz = PlyTypeSize(p.list ? p.count_type : p.type);
        if (sz == 0 || pos + static_cast<size_t>(sz) > bytes.size()) { decode_ok = false; break; }
        if (p.list) {
          const int cnt = static_cast<int>(PlyScalar(bytes, pos, p.count_type, big));
          for (int k = 0; k < cnt; ++k) {
            if (pos + static_cast<size_t>(PlyTypeSize(p.type)) > bytes.size()) { decode_ok = false; break; }
            idx.push_back(static_cast<int>(PlyScalar(bytes, pos, p.type, big)));
          }
        } else {
          row[p.name] = PlyScalar(bytes, pos, p.type, big);
        }
      }
      if (e.name == "vertex") verts.push_back(row);
      else if (e.name == "face") faces.push_back(idx);
    }
  }
  d8test::Check(decode_ok && pos == bytes.size(), (tag + ": the payload is exactly as long as the header says").c_str());
  if (!decode_ok || verts.size() != 5 || faces.size() != 2) { d8test::Check(false, (tag + ": decoded 5 vertices and 2 faces").c_str()); return; }
  bool pos_ok = true, col_ok = true, uv_ok = true;
  for (int i = 0; i < 5; ++i) {
    const ON_3fPoint& p = m.raw().m_V[i];
    pos_ok = pos_ok && Near(verts[static_cast<size_t>(i)]["x"], p.x) && Near(verts[static_cast<size_t>(i)]["y"], p.y) && Near(verts[static_cast<size_t>(i)]["z"], p.z);
    const Color c = m.VertexColorAt(i);
    col_ok = col_ok && verts[static_cast<size_t>(i)].count("red") && verts[static_cast<size_t>(i)]["red"] == c.r && verts[static_cast<size_t>(i)]["green"] == c.g &&
             verts[static_cast<size_t>(i)]["blue"] == c.b;
    const Point2d t = m.TextureCoordinateAt(i);
    const auto& row = verts[static_cast<size_t>(i)];
    const bool has_st = row.count("u") != 0 || row.count("s") != 0;
    const double u = row.count("u") ? row.at("u") : (row.count("s") ? row.at("s") : -1);
    const double v = row.count("v") ? row.at("v") : (row.count("t") ? row.at("t") : -1);
    uv_ok = uv_ok && has_st && Near(u, t.x) && Near(v, t.y);
  }
  d8test::Check(pos_ok, (tag + ": decoded positions equal the mesh's").c_str());
  d8test::Check(col_ok, (tag + ": decoded red / green / blue bytes equal the mesh's vertex colours").c_str());
  d8test::Check(uv_ok, (tag + ": decoded texture coordinates equal the mesh's").c_str());
  d8test::Check(faces[0] == std::vector<int>({0, 1, 2, 3}) && faces[1] == std::vector<int>({3, 4, 0}), (tag + ": the quad stays one 4-index face, the triangle one 3-index face").c_str());
}

D8_EXTRA_TEST(MeshIoSavePlyBinaryLittleEndianDecodedIndependently) { CheckSavedPly(false); }
D8_EXTRA_TEST(MeshIoSavePlyBinaryBigEndianDecodedIndependently) { CheckSavedPly(true); }

D8_EXTRA_TEST(MeshIoSaveOffColourDecodedIndependently) {
  const Mesh m = MakeWriterMesh();
  const std::string path = TempPath("saved.off");
  d8test::Check(m.SaveOff(path) == Result::Ok, "SaveOff: writes");
  std::ifstream in(path);
  std::vector<std::string> toks;
  std::string line;
  while (std::getline(in, line)) {
    const size_t h = line.find('#');
    if (h != std::string::npos) line.resize(h);
    std::istringstream ls(line);
    std::string t;
    while (ls >> t) toks.push_back(t);
  }
  in.close();
  std::remove(path.c_str());
  size_t k = 0;
  auto next = [&]() { return k < toks.size() ? toks[k++] : std::string("nan"); };
  d8test::Check(!toks.empty() && toks[0] == "COFF", "SaveOff: a mesh with vertex colours writes the COFF header");
  next();
  const int nv = std::atoi(next().c_str()), nf = std::atoi(next().c_str());
  next();
  d8test::Check(nv == 5 && nf == 2, "SaveOff: counts line is 5 vertices, 2 faces");
  bool ok = (nv == 5 && nf == 2);
  for (int i = 0; ok && i < nv; ++i) {
    const double x = std::atof(next().c_str()), y = std::atof(next().c_str()), z = std::atof(next().c_str());
    const int r = std::atoi(next().c_str()), g = std::atoi(next().c_str()), b = std::atoi(next().c_str()), a = std::atoi(next().c_str());
    const ON_3fPoint& p = m.raw().m_V[i];
    const Color c = m.VertexColorAt(i);
    ok = ok && Near(x, p.x) && Near(y, p.y) && Near(z, p.z) && r == c.r && g == c.g && b == c.b && a == 255;
  }
  d8test::Check(ok, "SaveOff: each vertex line is 'x y z r g b a' with the mesh's own values and alpha 255");
  bool faces_ok = ok;
  const std::vector<std::vector<int>> expect = {{0, 1, 2, 3}, {3, 4, 0}};
  for (int f = 0; faces_ok && f < nf; ++f) {
    const int n = std::atoi(next().c_str());
    std::vector<int> idx;
    for (int c = 0; c < n; ++c) idx.push_back(std::atoi(next().c_str()));
    faces_ok = faces_ok && idx == expect[static_cast<size_t>(f)];
  }
  d8test::Check(faces_ok && k == toks.size(), "SaveOff: face lines are '<n> i0 .. i(n-1)' with 0-based indices, quad stays 4 indices, nothing left over");
}

D8_EXTRA_TEST(MeshIoSaveObjDecodedIndependently) {
  const Mesh m = MakeWriterMesh();
  const std::string path = TempPath("saved.obj");
  d8test::Check(m.SaveObj(path) == Result::Ok, "SaveObj: writes");
  std::ifstream in(path);
  std::vector<ON_3dPoint> v;
  std::vector<std::pair<double, double>> vt;
  struct Face { std::vector<int> v, vt; std::string group; };
  std::vector<Face> faces;
  std::string line, group;
  while (std::getline(in, line)) {
    const size_t h = line.find('#');
    if (h != std::string::npos) line.resize(h);
    std::istringstream ls(line);
    std::string tag;
    ls >> tag;
    if (tag == "v") { double x, y, z; ls >> x >> y >> z; v.push_back(ON_3dPoint(x, y, z)); }
    else if (tag == "vt") { double a, b = 0; ls >> a >> b; vt.push_back({a, b}); }
    else if (tag == "g" || tag == "o") { ls >> group; }
    else if (tag == "f") {
      Face f;
      f.group = group;
      std::string tok;
      while (ls >> tok) {
        int a = 0, b = 0;
        const size_t s1 = tok.find('/');
        a = std::atoi(tok.substr(0, s1).c_str());
        if (s1 != std::string::npos) {
          const size_t s2 = tok.find('/', s1 + 1);
          const std::string mid = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
          if (!mid.empty()) b = std::atoi(mid.c_str());
        }
        f.v.push_back(a < 0 ? static_cast<int>(v.size()) + a : a - 1);
        f.vt.push_back(b == 0 ? -1 : (b < 0 ? static_cast<int>(vt.size()) + b : b - 1));
      }
      faces.push_back(f);
    }
  }
  in.close();
  std::remove(path.c_str());
  d8test::Check(v.size() == 5 && faces.size() == 2, "SaveObj: 5 'v' lines and 2 'f' lines");
  if (v.size() != 5 || faces.size() != 2) return;
  bool pos_ok = true;
  for (int i = 0; i < 5; ++i) pos_ok = pos_ok && Near(v[static_cast<size_t>(i)].x, m.raw().m_V[i].x) && Near(v[static_cast<size_t>(i)].y, m.raw().m_V[i].y) && Near(v[static_cast<size_t>(i)].z, m.raw().m_V[i].z);
  d8test::Check(pos_ok, "SaveObj: 'v' lines equal the mesh's vertices");
  d8test::Check(faces[0].v == std::vector<int>({0, 1, 2, 3}) && faces[1].v == std::vector<int>({3, 4, 0}), "SaveObj: the quad is one 'f' line with 4 corners, the triangle one with 3 (1-based / relative indices resolve to the mesh's)");
  bool uv_ok = true;
  for (const Face& f : faces)
    for (size_t c = 0; c < f.v.size(); ++c) {
      const Point2d t = m.TextureCoordinateAt(f.v[c]);
      uv_ok = uv_ok && f.vt[c] >= 0 && static_cast<size_t>(f.vt[c]) < vt.size() && Near(vt[static_cast<size_t>(f.vt[c])].first, t.x) && Near(vt[static_cast<size_t>(f.vt[c])].second, t.y);
    }
  d8test::Check(uv_ok, "SaveObj: every corner's 'vt' reference resolves to the vertex's texture coordinate");
  d8test::Check(faces[0].group == "top" && faces[1].group == "side", "SaveObj: each face is written under its own 'g' name (top, side)");
}

}  // namespace
