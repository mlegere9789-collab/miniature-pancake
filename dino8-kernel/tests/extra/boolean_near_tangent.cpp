// Booleans of surfaces that cross at a very shallow angle: a cylinder wall 1e-3 inside a box face, two cylinders or two spheres
// overlapping by 1e-3, two skew cylinders 1e-3 into each other. The seed meshes of the surface intersector (chord tolerance 0.02) are
// coarser than the overlap, so they only cross the true curve over part of it or not at all; the curve used to come back as stubs (or
// not at all) and the general Boolean refused, or returned two disjoint shells for what is one connected solid.
//
// Every expectation is a closed form that does not use the code under test: the overlap of two equal circles / the circular segment
// cut off a disc (r^2 acos((r - d) / r) - (r - d) sqrt(2 r d - d^2)), the two caps of a lens (pi h^2 (3 r - h) / 3), the Euler
// characteristic and shell count of one connected solid (2, 1). Volumes are compared at 2e-2: the kernel's own exact volume of a
// cylinder wall polygonizes the circular trims (a plain cylinder measures 3.8e-3 low), so a tighter gate would test the integrator.
#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>
#include <set>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::IntersectionCurve;
using dino8::kernel::IntersectOptions;

constexpr double kPi = 3.14159265358979323846;

ON_Plane PlaneFor(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  const ON_3dVector seed = std::fabs(axis.x) < 0.9 ? ON_3dVector(1, 0, 0) : ON_3dVector(0, 1, 0);
  ON_3dVector x = seed - (seed * axis) * axis;
  x.Unitize();
  return ON_Plane(origin, x, ON_CrossProduct(axis, x));
}

Brep Wrap(ON_Brep* b) {
  Brep k;
  if (!b) return k;
  k.raw() = *b;
  delete b;
  if (k.raw().IsSolid() && k.VolumeOfSealedSolid() < 0) k.raw().Flip();
  return k;
}

Brep BoxOf(double x0, double y0, double z0, double x1, double y1, double z1) {
  const ON_3dPoint c[8] = {ON_3dPoint(x0, y0, z0), ON_3dPoint(x1, y0, z0), ON_3dPoint(x1, y1, z0), ON_3dPoint(x0, y1, z0),
                           ON_3dPoint(x0, y0, z1), ON_3dPoint(x1, y0, z1), ON_3dPoint(x1, y1, z1), ON_3dPoint(x0, y1, z1)};
  return Wrap(ON_BrepBox(c));
}

Brep CylinderOf(const ON_3dPoint& base, const ON_3dVector& axis, double radius, double height) {
  return Wrap(ON_BrepCylinder(ON_Cylinder(ON_Circle(PlaneFor(base, axis), radius), height), true, true));
}

Brep SphereOf(const ON_3dPoint& center, const ON_3dVector& axis, double radius) {
  ON_Sphere s(center, radius);
  s.plane = PlaneFor(center, axis);
  return Wrap(ON_BrepSphere(s));
}

// V - E + F - (inner loops) and the number of face-connected shells of the result, counted on the B-rep itself.
void Topology(const ON_Brep& b, int& chi, int& shells) {
  int V = 0, E = 0, F = 0, R = 0;
  std::vector<int> parent(static_cast<size_t>(b.m_F.Count()));
  std::iota(parent.begin(), parent.end(), 0);
  auto find = [&](int x) { while (parent[static_cast<size_t>(x)] != x) x = parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])]; return x; };
  for (int i = 0; i < b.m_V.Count(); ++i) if (b.m_V[i].m_vertex_index == i && b.m_V[i].m_ei.Count() > 0) ++V;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    const ON_BrepEdge& e = b.m_E[i];
    if (e.m_edge_index != i) continue;
    ++E;
    int f0 = -1;
    for (int k = 0; k < e.m_ti.Count(); ++k) {
      const ON_BrepFace* f = b.m_T[e.m_ti[k]].Face();
      if (!f) continue;
      if (f0 < 0) f0 = f->m_face_index; else parent[static_cast<size_t>(find(f->m_face_index))] = find(f0);
    }
  }
  for (int i = 0; i < b.m_F.Count(); ++i) if (b.m_F[i].m_face_index == i) ++F;
  for (int i = 0; i < b.m_L.Count(); ++i) if (b.m_L[i].m_type == ON_BrepLoop::inner && b.m_L[i].Face()) ++R;
  std::set<int> roots;
  for (int i = 0; i < b.m_F.Count(); ++i) if (b.m_F[i].m_face_index == i) roots.insert(find(i));
  chi = V - E + F - R;
  shells = static_cast<int>(roots.size());
}

double SegmentArea(double r, double d) { return r * r * std::acos((r - d) / r) - (r - d) * std::sqrt(2.0 * r * d - d * d); }

// One connected closed solid of the given volume: valid, closed, outward, Euler characteristic 2, a single shell.
void CheckConnectedSolid(const char* name, const Brep& result, double expected_volume) {
  const ON_Brep& b = result.raw();
  d8test::Check(b.m_F.Count() > 0, (std::string(name) + ": the result is not empty").c_str());
  if (b.m_F.Count() == 0) return;
  d8test::Check(b.IsValid() && b.IsSolid(), (std::string(name) + ": a valid closed solid").c_str());
  int chi = 0, shells = 0;
  Topology(b, chi, shells);
  d8test::Check(chi == 2 && shells == 1, (std::string(name) + ": one connected shell of Euler characteristic 2 (not two touching bodies)").c_str());
  double v = 0;
  try { v = result.VolumeOfSealedSolid(); } catch (const std::exception&) { v = -1; }
  d8test::Check(v > 0 && std::fabs(v - expected_volume) <= 2e-2 * expected_volume, (std::string(name) + ": the exact volume matches the closed form to 2e-2").c_str());
}

}  // namespace

// Cylinder wall 1e-3 inside the face x = 0 of a box: the box face and the wall cross along two lines 0.0894 apart. Union volume
// = box + cylinder - 4 * (circular segment of depth 1e-3).
D8_EXTRA_TEST(BooleanNearTangentCylinderWallInsideBoxFaceUnion) {
  const Brep box = BoxOf(0, 0, 0, 4, 4, 4);
  const Brep cyl = CylinderOf(ON_3dPoint(-0.999, 2, -1), ON_3dVector(0, 0, 1), 1.0, 6.0);
  try {
    const Brep r = BooleanCombineGeneralSealed(box, cyl, BooleanOp::Union).brep;
    CheckConnectedSolid("cylinder 1e-3 into a box face, union", r, 64.0 + 6.0 * kPi - 4.0 * SegmentArea(1.0, 1e-3));
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("cylinder 1e-3 into a box face, union: refused: ") + e.what()).c_str());
  }
}

// Two parallel unit cylinders whose axes are 1.999 apart (overlap 1e-3), offset along z: union = 2 pi * 3 - overlap prism, where the
// common prism is the lens of two unit circles (2 segments of depth 5e-4) times the common height 2.5.
D8_EXTRA_TEST(BooleanNearTangentParallelCylindersUnion) {
  const Brep a = CylinderOf(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 3.0);
  const Brep b = CylinderOf(ON_3dPoint(1.999, 0, 0.5), ON_3dVector(0, 0, 1), 1.0, 3.0);
  try {
    const Brep r = BooleanCombineGeneralSealed(a, b, BooleanOp::Union).brep;
    CheckConnectedSolid("parallel cylinders 1e-3 apart, union", r, 2.0 * kPi * 3.0 - 2.0 * SegmentArea(1.0, 5e-4) * 2.5);
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("parallel cylinders 1e-3 apart, union: refused: ") + e.what()).c_str());
  }
}

// Two unit spheres overlapping by 1e-3 (generic axes, so the seams are nowhere special). The seed meshes never cross, so there was no
// intersection curve at all: the intersection came back empty and the union as two separate shells.
D8_EXTRA_TEST(BooleanNearTangentSpheresIntersectionIsTheLens) {
  const Brep a = SphereOf(ON_3dPoint(0, 0, 0), ON_3dVector(2, 3, 6), 1.0);
  const Brep b = SphereOf(ON_3dPoint(1.999, 0, 0), ON_3dVector(-3, 2, 6), 1.0);
  const double h = 5e-4;  // each cap
  const double lens = 2.0 * kPi * h * h * (3.0 - h) / 3.0;
  try {
    const Brep r = BooleanCombineGeneralSealed(a, b, BooleanOp::Intersection).brep;
    CheckConnectedSolid("spheres 1e-3 into each other, intersection", r, lens);
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("spheres 1e-3 into each other, intersection: refused: ") + e.what()).c_str());
  }
}

D8_EXTRA_TEST(BooleanNearTangentSpheresUnionIsOneShell) {
  const Brep a = SphereOf(ON_3dPoint(0, 0, 0), ON_3dVector(2, 3, 6), 1.0);
  const Brep b = SphereOf(ON_3dPoint(1.999, 0, 0), ON_3dVector(-3, 2, 6), 1.0);
  const double h = 5e-4;
  const double lens = 2.0 * kPi * h * h * (3.0 - h) / 3.0;
  try {
    const Brep r = BooleanCombineGeneralSealed(a, b, BooleanOp::Union).brep;
    CheckConnectedSolid("spheres 1e-3 into each other, union", r, 2.0 * 4.0 * kPi / 3.0 - lens);
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("spheres 1e-3 into each other, union: refused: ") + e.what()).c_str());
  }
}

// Skew cylinders (axes z and y, 1.499 apart in x; radii 1 and 0.5 so the walls cross 1e-3 deep): the crossing curve is a loop 0.063
// long, a few mesh cells of the seed tessellation.
D8_EXTRA_TEST(BooleanNearTangentSkewCylindersUnionIsOneShell) {
  const Brep a = CylinderOf(ON_3dPoint(0, 0, -3), ON_3dVector(0, 0, 1), 1.0, 6.0);
  const Brep b = CylinderOf(ON_3dPoint(1.499, -3, 0), ON_3dVector(0, 1, 0), 0.5, 6.0);
  try {
    const Brep r = BooleanCombineGeneralSealed(a, b, BooleanOp::Union).brep;
    const ON_Brep& rb = r.raw();
    int chi = 0, shells = 0;
    Topology(rb, chi, shells);
    d8test::Check(rb.IsValid() && rb.IsSolid() && chi == 2 && shells == 1,
                  "skew cylinders 1e-3 into each other, union: one valid closed shell (the pair touches over a 0.06 loop and is one body)");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("skew cylinders 1e-3 into each other, union: refused: ") + e.what()).c_str());
  }
}

// The intersection curve itself: the two walls of equal unit cylinders 1.999 apart meet along the two lines x = 0.9995, y = +-sqrt(1 - 0.9995^2),
// and the caps cut them off, so the face-level curves must reach the caps (they used to stop one seed-mesh cell short, 0.004 of 0.063).
D8_EXTRA_TEST(SsxNearTangentWallCurvesReachTheCapRims) {
  const Brep a = CylinderOf(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 3.0);
  const Brep b = CylinderOf(ON_3dPoint(1.999, 0, 0.5), ON_3dVector(0, 0, 1), 1.0, 3.0);
  const double y = std::sqrt(1.0 - 0.9995 * 0.9995);
  IntersectOptions opt;
  const auto xs = dino8::kernel::IntersectBreps(a.raw(), b.raw(), opt);
  int wall_lines = 0, cap_arcs = 0;
  double worst = 0.0;
  for (const auto& x : xs) {
    const IntersectionCurve& c = x.curve;
    const ON_Surface* sa = a.raw().m_F[x.face_a].SurfaceOf();
    const ON_Surface* sb = b.raw().m_F[x.face_b].SurfaceOf();
    ON_Cylinder cya, cyb;
    const bool walls = sa->IsCylinder(&cya, 1e-6) && sb->IsCylinder(&cyb, 1e-6);
    double len = 0;
    for (size_t i = 1; i < c.points.size(); ++i) len += c.points[i].DistanceTo(c.points[i - 1]);
    if (walls) {
      for (const auto& p : c.points) {
        worst = std::max(worst, std::fabs(std::hypot(p.x, p.y) - 1.0));          // on cylinder A
        worst = std::max(worst, std::fabs(std::hypot(p.x - 1.999, p.y) - 1.0));  // and on cylinder B
      }
      ++wall_lines;
      d8test::Check(std::fabs(len - 2.5) < 1e-4 && std::fabs(std::fabs(c.points.front().y) - y) < 1e-4, "wall/wall curve is the 2.5 long line at y = +-0.0316 from z = 0.5 to z = 3");
    } else if (std::fabs(c.points.front().z - 0.5) < 1e-5 || std::fabs(c.points.front().z - 3.0) < 1e-5) {
      ++cap_arcs;
      d8test::Check(len > 0.03, "a cap/wall arc runs the whole 0.0316 from the seam to the rim (it used to stop at 0.0078)");
    }
  }
  d8test::Check(wall_lines == 2 && cap_arcs >= 3, "two wall lines and the cap arcs are found");
  d8test::Check(worst < 1e-6, "every wall/wall curve point lies on both cylinder walls to 1e-6 (not merely within the 1e-3 acceptance tolerance)");
}
