// DRAW-0043, "Faces have front and back sides with distinct style colors; Reverse Faces and Orient Faces flip normals consistently". Acceptance
// sketch: "Orient a connected 20-face set; all normals consistent with the picked face." The 20-face set is a regular icosahedron with its centre
// at the origin: vertices (0, +-1, +-phi), (+-1, +-phi, 0), (+-phi, 0, +-1) with phi = (1 + sqrt 5) / 2, whose faces are exactly the vertex triples
// at mutual distance 2 (found here by brute force, 20 of them). With the centre at the origin the independent test of an outward face is simply
// dot(normal, point on the face) > 0, the normal read from the surface normal with the face's own orientation flag (ON_BrepFace::m_bRev), not from the kernel.
//   * the fixture with 9 faces turned over has exactly 9 faces whose normal points inward;
//   * UnifyNormals() on it leaves 0 inward faces, a closed solid, and reports flips;
//   * an open shell (the same body without one face): the pass makes every face agree with the SEED face (the first face): with the seed
//     untouched all 19 faces end outward, with the seed turned over all 19 end inward, in both cases consistent across every shared edge, which
//     is the "consistent with the picked face" of the row (an open shell has no outward to prefer).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Point3d;

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

std::vector<Point3d> Vertices() {
  const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
  std::vector<Point3d> v;
  for (int a : {-1, 1}) for (int b : {-1, 1}) {
    v.push_back(Point3d(0, a * 1.0, b * phi));
    v.push_back(Point3d(a * 1.0, b * phi, 0));
    v.push_back(Point3d(a * phi, 0, b * 1.0));
  }
  return v;
}

// The 20 faces: triples of vertices at mutual distance 2, each loop counter-clockwise seen from outside (the centre is the origin).
std::vector<std::vector<Point3d>> Triangles() {
  const std::vector<Point3d> v = Vertices();
  std::vector<std::vector<Point3d>> tris;
  for (size_t i = 0; i < v.size(); ++i) for (size_t j = i + 1; j < v.size(); ++j) for (size_t k = j + 1; k < v.size(); ++k) {
    if (std::fabs(v[i].DistanceTo(v[j]) - 2.0) > 1e-9 || std::fabs(v[j].DistanceTo(v[k]) - 2.0) > 1e-9 || std::fabs(v[i].DistanceTo(v[k]) - 2.0) > 1e-9) continue;
    ON_3dVector n = ON_CrossProduct(v[j] - v[i], v[k] - v[i]);
    const ON_3dVector c(v[i].x + v[j].x + v[k].x, v[i].y + v[j].y + v[k].y, v[i].z + v[j].z + v[k].z);
    if (n * c > 0) tris.push_back({v[i], v[j], v[k]});
    else tris.push_back({v[i], v[k], v[j]});
  }
  return tris;
}

Brep Icosahedron(int skip_face = -1) {
  std::vector<Brep::PlanarFace> faces;
  const std::vector<std::vector<Point3d>> tris = Triangles();
  for (size_t i = 0; i < tris.size(); ++i) {
    if (static_cast<int>(i) == skip_face) continue;
    Brep::PlanarFace f;
    const ON_3dVector n = ON_CrossProduct(tris[i][1] - tris[i][0], tris[i][2] - tris[i][0]);
    f.plane = ON_Plane(tris[i][0], n);
    f.loop = tris[i];
    faces.push_back(f);
  }
  return Brep::FromPlanarFaces(faces);
}

// Faces whose oriented normal points toward the centre (the origin).
int InwardFaces(const Brep& b) {
  const ON_Brep& r = b.raw();
  int inward = 0;
  for (int i = 0; i < r.m_F.Count(); ++i) {
    if (r.m_F[i].m_face_index < 0) continue;
    const ON_BrepFace& f = r.m_F[i];
    const double u = f.Domain(0).ParameterAt(0.5), w = f.Domain(1).ParameterAt(0.5);
    ON_3dPoint p;
    ON_3dVector n;
    if (!f.EvPoint(u, w, p) || (n = f.NormalAt(u, w), !n.IsValid())) continue;
    if (f.m_bRev) n = -n;  // the face is turned over relative to its surface: the oriented normal is the reverse of the surface normal
    if (n.x * p.x + n.y * p.y + n.z * p.z < 0) ++inward;
  }
  return inward;
}

int LiveFaces(const Brep& b) {
  int n = 0;
  for (int i = 0; i < b.raw().m_F.Count(); ++i) n += b.raw().m_F[i].m_face_index >= 0 ? 1 : 0;
  return n;
}

void Flip(Brep& b, const std::vector<int>& which) {
  for (int i : which) b.raw().FlipFace(b.raw().m_F[i]);
}

}  // namespace

D8_EXTRA_TEST(SuOrientFacesMakesTheTwentyFaceIcosahedronConsistentWithTheSeedFace) {
  const std::vector<std::vector<Point3d>> tris = Triangles();
  d8test::Check(tris.size() == 20, Msg("the brute-force search finds the 20 faces of the icosahedron", static_cast<double>(tris.size())).c_str());
  Brep solid = Icosahedron();
  d8test::Check(LiveFaces(solid) == 20 && solid.raw().IsSolid() && InwardFaces(solid) == 0, "the fixture is a closed 20-face solid with every face outward");
  const std::vector<int> flipped = {1, 4, 5, 8, 9, 12, 15, 17, 19};
  Flip(solid, flipped);
  d8test::Check(InwardFaces(solid) == 9, Msg("after turning 9 faces over exactly 9 face normals point inward", InwardFaces(solid)).c_str());
  const int flips = solid.UnifyNormals();
  d8test::Check(flips > 0, "Orient reports the faces it flipped");
  d8test::Check(InwardFaces(solid) == 0, Msg("after Orient no face normal points inward", InwardFaces(solid)).c_str());
  d8test::Check(solid.raw().IsSolid(), "the oriented body is a closed solid");

  // an open shell: consistent with the seed face (the first face), whichever way that one points
  Brep open_a = Icosahedron(13);
  Flip(open_a, {2, 3, 6, 7, 10, 14, 16});
  d8test::Check(LiveFaces(open_a) == 19 && !open_a.raw().IsSolid() && InwardFaces(open_a) == 7, "the open 19-face shell with 7 faces turned over has 7 inward normals");
  open_a.UnifyNormals();
  d8test::Check(InwardFaces(open_a) == 0, Msg("seed face outward: all 19 faces end outward", InwardFaces(open_a)).c_str());
  Brep open_b = Icosahedron(13);
  Flip(open_b, {0, 2, 3, 6, 7, 10, 14, 16});
  d8test::Check(InwardFaces(open_b) == 8, "with the seed face turned over too there are 8 inward normals");
  open_b.UnifyNormals();
  d8test::Check(InwardFaces(open_b) == 19, Msg("seed face inward: all 19 faces end inward, consistent with the seed", InwardFaces(open_b)).c_str());
}
