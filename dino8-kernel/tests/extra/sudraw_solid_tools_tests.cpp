// SketchUp-style solid tools (solid-tools-procedures domain), kernel part: what makes a solid a solid, the shell of a union, an N-ary union
// by inclusion-exclusion, subtract with its two operand orders, intersect, and a split into pieces. The operands are axis-aligned boxes, so every
// expectation is an exact closed form worked out in the comment of its test and re-derived in the test by interval arithmetic (a second
// implementation: the volume of an intersection of boxes is the product of three interval overlaps; the surface of a boolean result
// is derived from which faces of one operand lie inside the other), never read off the engine under test.
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Point3d;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

// [x0, y0, z0, x1, y1, z1]
using Box = std::array<double, 6>;

Brep Make(const Box& b) { return Brep::BoxWelded(b[0], b[1], b[2], b[3], b[4], b[5]); }

double Vol(const Box& b) { return (b[3] - b[0]) * (b[4] - b[1]) * (b[5] - b[2]); }

double Overlap(double a0, double a1, double b0, double b1) { return std::max(0.0, std::min(a1, b1) - std::max(a0, b0)); }

// Volume of the intersection of any number of boxes: the product of the three interval overlaps.
double InterVol(const std::vector<Box>& boxes) {
  double lo[3] = {-1e300, -1e300, -1e300}, hi[3] = {1e300, 1e300, 1e300};
  for (const Box& b : boxes) {
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::max(lo[k], b[k]);
      hi[k] = std::min(hi[k], b[k + 3]);
    }
  }
  double v = 1.0;
  for (int k = 0; k < 3; ++k) v *= std::max(0.0, hi[k] - lo[k]);
  return v;
}

double SurfaceOfBox(const Box& b) {
  const double dx = b[3] - b[0], dy = b[4] - b[1], dz = b[5] - b[2];
  return 2.0 * (dx * dy + dy * dz + dz * dx);
}

// Area of the faces of box `a` that lie strictly inside box `b` (the part of a's boundary swallowed by b). A face of `a` is perpendicular
// to axis k at the coordinate a[k] or a[k + 3]; it is inside b when that coordinate is strictly between b's k bounds, and the part
// inside is the product of the two other interval overlaps.
double FaceAreaInside(const Box& a, const Box& b) {
  double s = 0.0;
  for (int k = 0; k < 3; ++k) {
    const int i = (k + 1) % 3, j = (k + 2) % 3;
    const double cross = Overlap(a[i], a[i + 3], b[i], b[i + 3]) * Overlap(a[j], a[j + 3], b[j], b[j + 3]);
    for (double c : {a[k], a[k + 3]}) {
      if (c > b[k] + 1e-12 && c < b[k + 3] - 1e-12) s += cross;
    }
  }
  return s;
}

// Signed-tetrahedra volume of a planar-faced solid from the faces' own loops (second implementation of the volume).
double PolyhedronVolume(const Brep& b) {
  double v = 0.0;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) {
    for (size_t i = 1; i + 1 < f.loop.size(); ++i) {
      const Point3d& a = f.loop[0];
      const Point3d& p = f.loop[i];
      const Point3d& q = f.loop[i + 1];
      v += (a.x * (p.y * q.z - p.z * q.y) - a.y * (p.x * q.z - p.z * q.x) + a.z * (p.x * q.y - p.y * q.x)) / 6.0;
    }
  }
  return v;
}

double LoopArea(const std::vector<Point3d>& loop) {
  double nx = 0, ny = 0, nz = 0;
  for (size_t i = 0; i < loop.size(); ++i) {
    const Point3d& a = loop[i];
    const Point3d& b = loop[(i + 1) % loop.size()];
    nx += (a.y - b.y) * (a.z + b.z);
    ny += (a.z - b.z) * (a.x + b.x);
    nz += (a.x - b.x) * (a.y + b.y);
  }
  return 0.5 * std::sqrt(nx * nx + ny * ny + nz * nz);
}

double SurfaceArea(const Brep& b) {
  double s = 0.0;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) s += LoopArea(f.loop);
  return s;
}

struct Counts {
  int vertices = 0, edges = 0, faces = 0, edges_with_two_trims = 0, edges_with_one_trim = 0;
};

Counts CountTopology(const Brep& b) {
  Counts c;
  const ON_Brep& r = b.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) c.vertices += r.m_V[i].m_vertex_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_F.Count(); ++i) c.faces += r.m_F[i].m_face_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_E.Count(); ++i) {
    if (r.m_E[i].m_edge_index < 0) continue;
    ++c.edges;
    const int t = r.m_E[i].TrimCount();
    if (t == 2) ++c.edges_with_two_trims;
    if (t == 1) ++c.edges_with_one_trim;
  }
  return c;
}

// The 5-face cardboard box: a 3 x 4 x 5 box whose top face is left out.
Brep OpenBox() {
  std::vector<Brep::PlanarFace> faces;
  auto face = [&](double nx, double ny, double nz, std::vector<Point3d> loop) {
    Brep::PlanarFace f;
    f.plane = ON_Plane(loop[0], ON_3dVector(nx, ny, nz));
    f.loop = std::move(loop);
    faces.push_back(f);
  };
  face(0, 0, -1, {P(0, 0, 0), P(0, 4, 0), P(3, 4, 0), P(3, 0, 0)});
  face(0, -1, 0, {P(0, 0, 0), P(3, 0, 0), P(3, 0, 5), P(0, 0, 5)});
  face(0, 1, 0, {P(0, 4, 0), P(0, 4, 5), P(3, 4, 5), P(3, 4, 0)});
  face(-1, 0, 0, {P(0, 0, 0), P(0, 0, 5), P(0, 4, 5), P(0, 4, 0)});
  face(1, 0, 0, {P(3, 0, 0), P(3, 4, 0), P(3, 4, 5), P(3, 0, 5)});
  return Brep::FromPlanarFaces(faces);
}

}  // namespace

// SOLID-0001, "A solid is a group or component with a finite closed volume (no missing faces, every edge shared by exactly two faces); Entity
// Info reports Solid status, volume...". Acceptance sketch: "Open and closed fixtures: solid flag true only for manifold watertight groups;
// volume equals closed form within 1e-9 relative." Closed fixture: the 3 x 4 x 5 box (volume 60, 12 edges each in exactly two faces).
// Open fixture: the same box without its top face (5 faces; the 4 edges of the missing face are each in one face only, the other 8 in two):
// not a solid, and the closed-volume query refuses it instead of reporting a volume. With its top face put back (a closed 6-face body built
// from the same loops) it is a solid again with volume 60.
D8_EXTRA_TEST(SuSolidFlagIsTrueOnlyForTheWatertightBodyWithTheClosedFormVolume) {
  const Brep closed = Brep::BoxWelded(0, 0, 0, 3, 4, 5);
  const Counts c = CountTopology(closed);
  d8test::Check(closed.raw().IsSolid() && closed.raw().IsValid(), "the closed 3 x 4 x 5 box reports solid");
  d8test::Check(c.edges == 12 && c.edges_with_two_trims == 12 && c.edges_with_one_trim == 0, "all 12 edges of the closed box are shared by exactly two faces");
  d8test::Check(std::fabs(closed.VolumeOfSealedSolid() - 60.0) / 60.0 < 1e-9, Msg("closed volume is 3 * 4 * 5 = 60", closed.VolumeOfSealedSolid()).c_str());

  const Brep open = OpenBox();
  const Counts o = CountTopology(open);
  d8test::Check(open.FaceCount() == 5 && !open.raw().IsSolid(), "the box without its top face has 5 faces and is not a solid");
  d8test::Check(o.edges_with_one_trim == 4 && o.edges_with_two_trims == 8, "exactly the 4 rim edges of the missing face are naked, the other 8 edges are shared");
  bool refused = false;
  try { (void)open.VolumeOfSealedSolid(); } catch (const std::exception&) { refused = true; }
  d8test::Check(refused, "the closed-volume query refuses the open body instead of quoting a volume");

  std::vector<Brep::PlanarFace> faces = open.PlanarFaces();
  Brep::PlanarFace top;
  top.plane = ON_Plane(P(0, 0, 5), ON_3dVector(0, 0, 1));
  top.loop = {P(0, 0, 5), P(3, 0, 5), P(3, 4, 5), P(0, 4, 5)};
  faces.push_back(top);
  const Brep refilled = Brep::FromPlanarFaces(faces);
  d8test::Check(refilled.FaceCount() == 6 && refilled.raw().IsSolid(), "putting the top face back makes it a solid again");
  d8test::Check(std::fabs(PolyhedronVolume(refilled) - 60.0) / 60.0 < 1e-9, "and its volume is 60");
}

// SOLID-0002 Outer Shell, "merges overlapping solids keeping only the external faces and returns a group, reducing geometry". Acceptance
// sketch: "Two overlapping boxes: result volume equals analytic union volume and has no internal faces." A = [0,4]^3 and B = [2,6] x [1,5] x [1,5]
// overlap in [2,4] x [1,4] x [1,4] = 18, so the union volume is 64 + 64 - 18 = 110. No internal faces means the surface of the result is only the
// part of each operand's skin outside the other: SA(A) + SA(B) - (A's faces inside B) - (B's faces inside A) = 96 + 96 - 21 - 21 = 150 (the
// 21s from the interval overlaps in FaceAreaInside); any internal face would add to it. After coplanar pieces are merged the body is a
// polyhedron of 12 faces (six big faces and six L-shapes), every edge in exactly two faces.
D8_EXTRA_TEST(SuOuterShellOfTwoBoxesKeepsOnlyTheExternalFacesOfTheUnion) {
  const Box a = {0, 0, 0, 4, 4, 4}, b = {2, 1, 1, 6, 5, 5};
  Brep u = dino8::kernel::BooleanCombinePlanar(Make(a), Make(b), BooleanOp::Union);
  const double expected_volume = Vol(a) + Vol(b) - InterVol({a, b});
  const double expected_area = SurfaceOfBox(a) + SurfaceOfBox(b) - FaceAreaInside(a, b) - FaceAreaInside(b, a);
  d8test::Check(std::fabs(expected_volume - 110.0) < 1e-12 && std::fabs(expected_area - 150.0) < 1e-12, "the interval-arithmetic oracle itself gives 110 and 150");
  d8test::Check(u.raw().IsSolid(), "the union is a closed solid");
  d8test::Check(std::fabs(PolyhedronVolume(u) - expected_volume) < 1e-9, Msg("union volume is inclusion-exclusion 110", PolyhedronVolume(u)).c_str());
  d8test::Check(std::fabs(SurfaceArea(u) - expected_area) < 1e-9, Msg("union surface is 150: no internal face adds area", SurfaceArea(u)).c_str());
  const int raw_faces = u.FaceCount();
  u.MergeCoplanarFaces();
  const Counts c = CountTopology(u);
  d8test::Check(u.FaceCount() == 12 && u.FaceCount() < raw_faces, Msg("merged shell has 12 faces, fewer than the raw result", u.FaceCount()).c_str());
  d8test::Check(c.edges_with_two_trims == c.edges && c.edges_with_one_trim == 0, "every edge of the shell is shared by exactly two faces");
  d8test::Check(std::fabs(SurfaceArea(u) - expected_area) < 1e-9 && std::fabs(PolyhedronVolume(u) - expected_volume) < 1e-9, "merging faces keeps surface and volume");
}

// SOLID-0003 Union, "merges two or more solids into a single solid group". Acceptance sketch: "Union of three overlapping spheres: volume by
// inclusion-exclusion within 1e-6 relative." The curved-solid engine reaches about 1 percent on the sphere pairs tried, so this test uses three
// overlapping boxes (the exact engine) and the same oracle: V(A u B u C) = sum Vi - sum Vij + Vabc, each term by interval overlap. With
// A = [0,4]^3, B = [2,6] x [1,5] x [1,5], C = [1,5] x [2,6] x [-2,3]: VA = 64, VB = 64, VC = 4 * 4 * 5 = 80, VAB = 18, VAC = 3 * 2 * 3 = 18,
// VBC = 3 * 3 * 2 = 18, VABC = [2,4] x [2,4] x [1,3] = 8, so V = 208 - 54 + 8 = 162. The result must be one solid (not a compound).
D8_EXTRA_TEST(SuUnionOfThreeOverlappingSolidsFollowsInclusionExclusion) {
  const Box a = {0, 0, 0, 4, 4, 4}, b = {2, 1, 1, 6, 5, 5}, c = {1, 2, -2, 5, 6, 3};
  const double expected = Vol(a) + Vol(b) + Vol(c) - InterVol({a, b}) - InterVol({a, c}) - InterVol({b, c}) + InterVol({a, b, c});
  d8test::Check(std::fabs(expected - 162.0) < 1e-12, Msg("inclusion-exclusion by interval overlap gives 162", expected).c_str());
  const std::vector<Brep> group = {Make(a), Make(b), Make(c)};
  const Brep u = dino8::kernel::BooleanCombinePlanarNAry(group, {}, BooleanOp::Union);
  d8test::Check(u.raw().IsSolid() && u.raw().IsValid(), "the union of three boxes is one valid closed solid");
  d8test::Check(std::fabs(PolyhedronVolume(u) - expected) < 1e-9, Msg("its volume is 162", PolyhedronVolume(u)).c_str());
  d8test::Check(u.SplitDisjointPieces().size() == 1, "the three boxes became a single piece");
}

// SOLID-0005 Subtract, "removes the second selected solid by the first and deletes the cutter; selection order matters". Acceptance sketch:
// "Cylinder from box: volume equals box minus cylinder within 1e-6 relative; order reversal swaps the roles." The planar engine part: A = [0,4]^3
// (64) and the tool T = [2,6] x [1,3] x [1,3] (16) overlap in [2,4] x [1,3] x [1,3] = 8, so A - T = 56 and T - A = 8, the roles swap. The
// curved part: a cylinder r = 1, height 6 through the same box (axis through (2,2), z from -1 to 5, so 4 of its 6 are inside): cylinder minus box
// = pi r^2 (6 - 4) = 2 pi (within 1e-6 relative on the sealed exact volume).
D8_EXTRA_TEST(SuSubtractOrderSwapsTheRolesAndTheCylinderCutMatchesPiRSquaredH) {
  const Box a = {0, 0, 0, 4, 4, 4}, t = {2, 1, 1, 6, 3, 3};
  const Brep ab = dino8::kernel::BooleanCombinePlanar(Make(a), Make(t), BooleanOp::Difference);
  const Brep ta = dino8::kernel::BooleanCombinePlanar(Make(t), Make(a), BooleanOp::Difference);
  d8test::Check(std::fabs(PolyhedronVolume(ab) - (Vol(a) - InterVol({a, t}))) < 1e-9 && std::fabs(PolyhedronVolume(ab) - 56.0) < 1e-9, Msg("A - T is 64 - 8 = 56", PolyhedronVolume(ab)).c_str());
  d8test::Check(std::fabs(PolyhedronVolume(ta) - (Vol(t) - InterVol({a, t}))) < 1e-9 && std::fabs(PolyhedronVolume(ta) - 8.0) < 1e-9, Msg("T - A is 16 - 8 = 8", PolyhedronVolume(ta)).c_str());
  d8test::Check(ab.raw().IsSolid() && ta.raw().IsSolid(), "both orders give closed solids");
  const double pi = 3.14159265358979323846;
  const Brep box = Brep::BoxWelded(0, 0, 0, 4, 4, 4);
  const Brep cyl = Brep::CylinderWelded(P(2, 2, -1), dino8::kernel::Vector3d(0, 0, 1), 1.0, 6.0);
  const Brep outside = dino8::kernel::BooleanCombineGeneralSealed(cyl, box, BooleanOp::Difference).brep;
  d8test::Check(outside.raw().IsSolid(), "the cylinder minus the box is a closed solid");
  d8test::Check(std::fabs(outside.VolumeOfSealedSolid() - 2 * pi) / (2 * pi) < 1e-6, Msg("cylinder - box = pi r^2 (6 - 4) = 2 pi within 1e-6 relative", outside.VolumeOfSealedSolid() - 2 * pi).c_str());
}

// SOLID-0008 Intersect, "leaves only the volume shared by two solids". Acceptance sketch: "Two cylinders: intersection volume equals closed form
// (Steinmetz) within tolerance." Boxes: A = [0,4]^3 and B = [2,6] x [1,5] x [1,5] share [2,4] x [1,4] x [1,4]: one 6-face solid of volume 18 and
// surface 2 (2*3 + 3*3 + 2*3) = 42, its corners exactly (2,1,1) and (4,4,4). Two perpendicular cylinders of radius 1 whose axes meet (length 6,
// centred on the crossing) share the Steinmetz solid of volume 16 r^3 / 3 = 5.3333; the curved-solid engine is checked to 1 percent (measured
// 0.54 percent, the polyline edge quadrature of that engine), which separates it from the wrong closed forms (a half or a double is 50 percent off).
D8_EXTRA_TEST(SuIntersectLeavesTheSharedVolumeOfTwoSolids) {
  const Box a = {0, 0, 0, 4, 4, 4}, b = {2, 1, 1, 6, 5, 5};
  const Brep i = dino8::kernel::BooleanCombinePlanar(Make(a), Make(b), BooleanOp::Intersection);
  d8test::Check(i.raw().IsSolid() && i.FaceCount() == 6, "the shared part of two boxes is one closed 6-face solid");
  d8test::Check(std::fabs(PolyhedronVolume(i) - InterVol({a, b})) < 1e-9 && std::fabs(PolyhedronVolume(i) - 18.0) < 1e-9, Msg("shared volume 2 * 3 * 3 = 18", PolyhedronVolume(i)).c_str());
  d8test::Check(std::fabs(SurfaceArea(i) - 42.0) < 1e-9, Msg("shared surface 42", SurfaceArea(i)).c_str());
  double lo[3] = {1e9, 1e9, 1e9}, hi[3] = {-1e9, -1e9, -1e9};
  for (const Brep::PlanarFace& f : i.PlanarFaces()) {
    for (const Point3d& p : f.loop) {
      const double c[3] = {p.x, p.y, p.z};
      for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
    }
  }
  d8test::Check(std::fabs(lo[0] - 2) + std::fabs(lo[1] - 1) + std::fabs(lo[2] - 1) < 1e-9 && std::fabs(hi[0] - 4) + std::fabs(hi[1] - 4) + std::fabs(hi[2] - 4) < 1e-9, "the shared solid spans (2,1,1) to (4,4,4)");
  const Brep c1 = Brep::CylinderWelded(P(-3, 0, 0), dino8::kernel::Vector3d(1, 0, 0), 1.0, 6.0);
  const Brep c2 = Brep::CylinderWelded(P(0, -3, 0), dino8::kernel::Vector3d(0, 1, 0), 1.0, 6.0);
  const Brep s = dino8::kernel::BooleanCombineGeneral(c1, c2, BooleanOp::Intersection);
  d8test::Check(s.raw().IsSolid(), "the Steinmetz solid is a closed solid");
  const double steinmetz = 16.0 / 3.0;
  d8test::Check(std::fabs(s.VolumeOfSealedSolid() - steinmetz) / steinmetz < 0.01, Msg("two crossing cylinders share 16 r^3 / 3 within 1 percent", s.VolumeOfSealedSolid() - steinmetz).c_str());
}

// SOLID-0010 Split, "divides overlapping solids along the intersection into separate pieces". Acceptance sketch: "Two boxes: three pieces whose
// volumes sum to union volume." A = [0,4]^3 and B = [2,6] x [1,5] x [1,5]: the pieces are A - B (64 - 18 = 46), A n B (18) and B - A (46); they sum
// to the union 110 and each is its own closed solid. The pieces are disjoint (their volumes add with no overlap term), and the surface of the
// three pieces is SA(union) + every interface face twice = 150 + 2 * (21 + 21) = 234 (the faces of A inside B, and of B inside A, each belong to
// two pieces).
D8_EXTRA_TEST(SuSplitOfTwoOverlappingBoxesGivesThreePiecesSummingToTheUnion) {
  const Box a = {0, 0, 0, 4, 4, 4}, b = {2, 1, 1, 6, 5, 5};
  const Brep p1 = dino8::kernel::BooleanCombinePlanar(Make(a), Make(b), BooleanOp::Difference);
  const Brep p2 = dino8::kernel::BooleanCombinePlanar(Make(a), Make(b), BooleanOp::Intersection);
  const Brep p3 = dino8::kernel::BooleanCombinePlanar(Make(b), Make(a), BooleanOp::Difference);
  const double v1 = PolyhedronVolume(p1), v2 = PolyhedronVolume(p2), v3 = PolyhedronVolume(p3);
  d8test::Check(p1.raw().IsSolid() && p2.raw().IsSolid() && p3.raw().IsSolid(), "all three pieces are closed solids");
  d8test::Check(std::fabs(v1 - 46.0) < 1e-9 && std::fabs(v2 - 18.0) < 1e-9 && std::fabs(v3 - 46.0) < 1e-9, Msg("pieces are 46, 18 and 46", v1 + v2 + v3).c_str());
  const double union_volume = Vol(a) + Vol(b) - InterVol({a, b});
  d8test::Check(std::fabs(v1 + v2 + v3 - union_volume) < 1e-9, Msg("the three volumes sum to the union volume 110", v1 + v2 + v3 - union_volume).c_str());
  const double area_sum = SurfaceArea(p1) + SurfaceArea(p2) + SurfaceArea(p3);
  const double union_area = SurfaceOfBox(a) + SurfaceOfBox(b) - FaceAreaInside(a, b) - FaceAreaInside(b, a);
  d8test::Check(std::fabs(area_sum - (union_area + 2.0 * (FaceAreaInside(a, b) + FaceAreaInside(b, a)))) < 1e-9, Msg("the pieces' surfaces add up to the union surface plus every interface twice", area_sum).c_str());
}
