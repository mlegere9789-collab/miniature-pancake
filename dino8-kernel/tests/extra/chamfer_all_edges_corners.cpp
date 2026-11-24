// ChamferConvexEdges / FilletConvexEdges applied to EVERY edge of a box at once (the corners where three supplied edges meet).
//
// Independent closed forms (derived here, not read from any reference result). Box a x b x c, one setback d on both faces of every edge
// (2 d < min(a, b, c)):
//   * Each chamfered edge is a strip, a rectangle of width sqrt(2) d. Each face keeps the rectangle inset by d from its four sides. At every corner the
//     three strips end on a planar triangle through the three face corners (d,0,d), (0,d,d), (d,d,0) (corner at the origin), whose three sides all have
//     length sqrt(2) d: an equilateral triangle of area sqrt(3)/2 d^2.
//   * Counts: 6 faces + 12 strips + 8 triangles = 26 faces; every face-corner point is a vertex: 8 x 3 = 24; every strip has 4 sides, every inset face 4,
//     every triangle 3, each side shared by two faces: (12 x 4 + 6 x 4 + 8 x 3) / 2 = 48 edges; Euler V - E + F = 24 - 48 + 26 = 2.
//   * Area = 2[(a-2d)(b-2d) + (b-2d)(c-2d) + (a-2d)(c-2d)] + sqrt(2) d [4(a+b+c) - 24 d] + 8 (sqrt(3)/2) d^2.
//   * Volume: the prisms (d^2/2) x (length - 2 d) of the 12 edges between the corner cubes, plus per corner cube [0,d]^3 what the solid keeps there, which is
//     {x + y >= d, y + z >= d, x + z >= d, x + y + z >= 2 d} within the cube = the tetrahedron at the cube's far vertex, volume d^3/6. Removed per corner
//     d^3 - d^3/6 = 5 d^3/6, so  V = abc - 2 d^2 (a+b+c) + 12 d^3 - 8 (5/6) d^3 = abc - 2 d^2 (a+b+c) + 16 d^3 / 3.
//   * The three edges at ONE corner only (the other ends free): the three prisms overlap only in that corner cube, V = a^3 - (3/2) d^2 a + (2/3) d^3 for a cube.
// FilletConvexEdges on all 12 edges of a cube of side a (b = a - 2 r): rolling-ball body with spherical corners, Steiner form
//   V = b^3 + 6 b^2 r + 3 pi b r^2 + (4/3) pi r^3   (core, six face slabs, twelve quarter cylinders, eight octants of a ball),
//   A = 6 b^2 + 12 b (pi r / 2) + 4 pi r^2 = 6 b^2 + 6 pi b r + 4 pi r^2.
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/convex_hull.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Point3d;
using EdgeList = std::vector<std::pair<Point3d, Point3d>>;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

EdgeList AllEdges(const Brep& b) {
  EdgeList e;
  const ON_Brep& r = b.raw();
  for (int i = 0; i < r.m_E.Count(); ++i) {
    const ON_BrepEdge& edge = r.m_E[i];
    if (edge.m_edge_index < 0) continue;
    e.emplace_back(edge.PointAtStart(), edge.PointAtEnd());
  }
  return e;
}

bool Refuses(const std::function<void()>& f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return true;
  } catch (const std::exception&) {
    return false;
  }
  return false;
}

double RelErr(double got, double want) { return std::fabs(got - want) / std::fabs(want); }

std::string Fmt(const char* what, double d) {
  char buf[160];
  std::snprintf(buf, sizeof buf, "%s (d = %g)", what, d);
  return buf;
}

double ChamferVolume(double a, double b, double c, double d) { return a * b * c - 2.0 * d * d * (a + b + c) + 16.0 / 3.0 * d * d * d; }

double ChamferArea(double a, double b, double c, double d) {
  const double x = a - 2 * d, y = b - 2 * d, z = c - 2 * d;
  return 2.0 * (x * y + y * z + x * z) + std::sqrt(2.0) * d * (4.0 * (a + b + c) - 24.0 * d) + 8.0 * (std::sqrt(3.0) / 2.0) * d * d;
}

void CheckChamferAllEdges(const Brep& box, double a, double b, double c, double d, const char* tag) {
  const EdgeList edges = AllEdges(box);
  Check(edges.size() == 12, "the box has 12 edges");
  Brep out;
  bool built = true;
  try {
    out = dino8::kernel::ChamferConvexEdges(box, edges, d, d);
  } catch (const std::exception& e) {
    built = false;
    std::fprintf(stderr, "chamfer all edges threw: %s\n", e.what());
  }
  const std::string t = std::string(tag) + ", ";
  Check(built, Fmt((t + "all 12 edges chamfered in one call builds").c_str(), d).c_str());
  if (!built) return;
  Check(out.LiveFaceCount() == 26, Fmt((t + "26 faces (6 inset + 12 strips + 8 corner triangles)").c_str(), d).c_str());
  Check(out.LiveEdgeCount() == 48, Fmt((t + "48 edges").c_str(), d).c_str());
  Check(out.LiveVertexCount() == 24, Fmt((t + "24 vertices").c_str(), d).c_str());
  Check(out.LiveVertexCount() - out.LiveEdgeCount() + out.LiveFaceCount() == 2, Fmt((t + "Euler characteristic 2").c_str(), d).c_str());
  Check(out.raw().IsSolid() && out.raw().IsValid(), Fmt((t + "a valid closed solid").c_str(), d).c_str());
  const double v = out.VolumeOfSealedSolid(), area = out.Area();
  Check(RelErr(v, ChamferVolume(a, b, c, d)) < 1e-9, Fmt((t + "volume = abc - 2 d^2 (a+b+c) + 16 d^3 / 3").c_str(), d).c_str());
  Check(RelErr(area, ChamferArea(a, b, c, d)) < 1e-9, Fmt((t + "area = inset faces + strips + corner triangles").c_str(), d).c_str());
  const double mesh_v = out.TessellateToClosedMeshAdaptive(1e-6).Volume();
  Check(RelErr(mesh_v, ChamferVolume(a, b, c, d)) < 1e-6, Fmt((t + "the closed tessellation has the same volume").c_str(), d).c_str());
}

}  // namespace

D8_EXTRA_TEST(ChamferAllEdgesOfCubeCornerPatches) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 2, 2);
  for (const double d : {0.05, 0.25, 0.5, 0.99}) CheckChamferAllEdges(box, 2, 2, 2, d, "cube 2");
}

D8_EXTRA_TEST(ChamferAllEdgesOfUnequalBoxAndMovedBox) {
  CheckChamferAllEdges(Brep::BoxWelded(0, 0, 0, 3, 2, 1.5), 3, 2, 1.5, 0.4, "box 3 x 2 x 1.5");
  CheckChamferAllEdges(Brep::BoxWelded(-5, 7, 40, -5 + 3, 7 + 2, 40 + 1.5), 3, 2, 1.5, 0.3, "box 3 x 2 x 1.5 moved");
  // The same body turned about an oblique axis: the construction reads everything from the faces, not from the axes.
  ON_Xform rot;
  rot.Rotation(0.7, ON_3dVector(1.0, 2.0, 0.5), ON_3dPoint(0.2, 0.1, 0.3));
  const Brep turned = Brep::BoxWelded(0, 0, 0, 3, 2, 1.5).Transform(rot);
  CheckChamferAllEdges(turned, 3, 2, 1.5, 0.35, "box 3 x 2 x 1.5 turned");
}

// Tetrahedra and a triangular prism: every vertex trihedral, dihedral angles other than 90 degrees. Expected volumes come from the independent half-space
// polytope computation in tests/extra/chamfer_polytope_volumes.py (original faces, one plane per edge through the two rails, one plane per vertex through the
// three face corners), not from the kernel. Faces = F + E + V of the original body (each face, edge and vertex becomes one face).
D8_EXTRA_TEST(ChamferAllEdgesOfTetrahedraAndPrism) {
  struct Case { const char* name; std::vector<Point3d> pts; double d; double volume; int faces; };
  const std::vector<Point3d> corner = {Point3d(0, 0, 0), Point3d(3, 0, 0), Point3d(0, 3, 0), Point3d(0, 0, 3)};
  const std::vector<Point3d> regular = {Point3d(1, 1, 1), Point3d(1, -1, -1), Point3d(-1, 1, -1), Point3d(-1, -1, 1)};
  const std::vector<Point3d> prism = {Point3d(0, 0, 0), Point3d(4, 0, 0), Point3d(0, 3, 0), Point3d(0, 0, 2), Point3d(4, 0, 2), Point3d(0, 3, 2)};
  const Case cases[] = {{"corner tetrahedron", corner, 0.1, 4.408350066145, 14},       {"corner tetrahedron", corner, 0.3, 3.770759222010, 14},
                        {"regular tetrahedron", regular, 0.1, 2.591747089837, 14},     {"regular tetrahedron", regular, 0.3, 2.083838092263, 14},
                        {"3-4-5 triangular prism", prism, 0.2, 11.468800000000, 20},   {"3-4-5 triangular prism", prism, 0.5, 9.100000000000, 20}};
  for (const Case& c : cases) {
    const Brep body = dino8::kernel::ExactConvexHull(c.pts);
    Brep out;
    bool built = true;
    try {
      out = dino8::kernel::ChamferConvexEdges(body, AllEdges(body), c.d, c.d);
    } catch (const std::exception& e) {
      built = false;
      std::fprintf(stderr, "%s all edges threw: %s\n", c.name, e.what());
    }
    const std::string t = std::string(c.name) + ", ";
    Check(built, Fmt((t + "all edges chamfered in one call builds").c_str(), c.d).c_str());
    if (!built) continue;
    Check(out.raw().IsSolid() && out.raw().IsValid(), Fmt((t + "a valid closed solid").c_str(), c.d).c_str());
    Check(out.LiveFaceCount() == c.faces, Fmt((t + "faces = F + E + V of the original").c_str(), c.d).c_str());
    Check(RelErr(out.VolumeOfSealedSolid(), c.volume) < 1e-9, Fmt((t + "volume equals the independent half-space polytope volume").c_str(), c.d).c_str());
  }
}

D8_EXTRA_TEST(ChamferAllEdgesRefusesDistancesThatEatAFace) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 2, 2, 2);
  const EdgeList edges = AllEdges(box);
  // d = 1 leaves every inset face with zero width, d = 1.01 would cross: both refused with std::invalid_argument, no half-built body.
  Check(Refuses([&] { dino8::kernel::ChamferConvexEdges(box, edges, 1.0, 1.0); }), "all-edges chamfer of a 2-cube at d = 1.0 (faces would vanish) is refused");
  Check(Refuses([&] { dino8::kernel::ChamferConvexEdges(box, edges, 1.01, 1.01); }), "all-edges chamfer of a 2-cube at d = 1.01 is refused");
  Check(Refuses([&] { dino8::kernel::ChamferConvexEdges(box, edges, 0.0, 0.1); }), "a zero distance is refused");
}

D8_EXTRA_TEST(ChamferThreeEdgesAtOneCorner) {
  // Only the three edges at the origin corner (their far ends are free corners): the corner gets its triangle, each far end its own end condition.
  const double a = 2.0;
  const Brep box = Brep::BoxWelded(0, 0, 0, a, a, a);
  const EdgeList three = {{Point3d(0, 0, 0), Point3d(a, 0, 0)}, {Point3d(0, 0, 0), Point3d(0, a, 0)}, {Point3d(0, 0, 0), Point3d(0, 0, a)}};
  for (const double d : {0.2, 0.6}) {
    Brep out;
    bool built = true;
    try {
      out = dino8::kernel::ChamferConvexEdges(box, three, d, d);
    } catch (const std::exception& e) {
      built = false;
      std::fprintf(stderr, "three edges at one corner threw: %s\n", e.what());
    }
    Check(built, Fmt("three edges meeting at one corner chamfered in one call builds", d).c_str());
    if (!built) continue;
    Check(out.raw().IsSolid() && out.raw().IsValid(), Fmt("three edges at a corner: a valid closed solid", d).c_str());
    Check(out.LiveFaceCount() == 10, Fmt("three edges at a corner: 6 faces + 3 strips + 1 corner triangle", d).c_str());
    const double want = a * a * a - 1.5 * d * d * a + 2.0 / 3.0 * d * d * d;
    Check(RelErr(out.VolumeOfSealedSolid(), want) < 1e-9, Fmt("three edges at a corner: volume = a^3 - 3/2 d^2 a + 2/3 d^3", d).c_str());
    Check(RelErr(out.TessellateToClosedMeshAdaptive(1e-6).Volume(), want) < 1e-6, Fmt("three edges at a corner: the closed tessellation agrees", d).c_str());
  }
}

D8_EXTRA_TEST(ChamferSharedVertexStillRefusedWhenNotAWholeCorner) {
  const Brep box = Brep::BoxWelded(0, 0, 0, 1, 1, 1);
  // Two of the three edges at one corner: out of scope (a mitred two-edge corner), refused rather than misbuilt.
  const EdgeList two = {{Point3d(0, 0, 0), Point3d(1, 0, 0)}, {Point3d(0, 0, 0), Point3d(0, 1, 0)}};
  Check(Refuses([&] { dino8::kernel::ChamferConvexEdges(box, two, 0.2, 0.2); }), "two supplied edges at one corner (third not supplied) are refused");
  // The same edge listed twice is a shared vertex at both ends, never a corner.
  const EdgeList twice = {{Point3d(0, 0, 0), Point3d(1, 0, 0)}, {Point3d(1, 0, 0), Point3d(0, 0, 0)}};
  Check(Refuses([&] { dino8::kernel::ChamferConvexEdges(box, twice, 0.2, 0.2); }), "the same edge listed twice is refused");
}

D8_EXTRA_TEST(FilletAllEdgesOfCubeRollingBallClosedForm) {
  const double a = 2.0;
  const Brep box = Brep::BoxWelded(0, 0, 0, a, a, a);
  const EdgeList edges = AllEdges(box);
  for (const double r : {0.05, 0.25, 0.5, 0.99}) {
    Brep out;
    bool built = true;
    try {
      out = dino8::kernel::FilletConvexEdges(box, edges, r);
    } catch (const std::exception& e) {
      built = false;
      std::fprintf(stderr, "fillet all edges threw: %s\n", e.what());
    }
    Check(built, Fmt("all 12 edges filleted in one call builds", r).c_str());
    if (!built) continue;
    const double b = a - 2 * r;
    const double pi = ON_PI;
    const double vol = b * b * b + 6 * b * b * r + 3 * pi * b * r * r + 4.0 / 3.0 * pi * r * r * r;
    const double area = 6 * b * b + 6 * pi * b * r + 4 * pi * r * r;
    Check(out.raw().IsSolid() && out.raw().IsValid(), Fmt("all-edges fillet: a valid closed solid", r).c_str());
    Check(RelErr(out.VolumeOfSealedSolid(), vol) < 1e-6, Fmt("all-edges fillet: Steiner volume b^3 + 6 b^2 r + 3 pi b r^2 + 4/3 pi r^3", r).c_str());
    Check(RelErr(out.Area(), area) < 1e-6, Fmt("all-edges fillet: area 6 b^2 + 6 pi b r + 4 pi r^2", r).c_str());
  }
  // A radius that eats the faces (r = a/2 and beyond) is refused, as it is for the chamfer.
  Check(Refuses([&] { dino8::kernel::FilletConvexEdges(box, edges, 1.0); }), "all-edges fillet at r = 1 (half the side) is refused");
  Check(Refuses([&] { dino8::kernel::FilletConvexEdges(box, edges, 1.01); }), "all-edges fillet at r = 1.01 is refused");
}
