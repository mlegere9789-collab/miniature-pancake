// OffsetSolidPlanar (offset_solid.h): mitre offset of a closed solid bounded by planar faces, convex or not.
//
// Oracles (none is the routine's own construction):
//  * closed forms:  cube (L+2d)^3;  cube minus a corner cube  (10+2d)^3 - 5^3;  right prism over a polygon  area(d) * (H+2d)
//    with area(d) from a 2D mitre offset written here (line-line intersections, not the 3D vertex solve);
//  * an independent signed volume (divergence theorem) over the RESULT's own PlanarFaces();
//  * every result face lies exactly on its source plane moved by d (n . (p' - p0) = d for every vertex of the face);
//  * the existing, independent convex implementation OffsetSolidConvexPlanar (clip against every plane) on convex inputs;
//  * the refusals' numbers recomputed here (the non-concurrent apex residual from a 3-plane solve done in the test);
//  * the output Brep is left untouched on every refusal.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/offset_solid.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::PlanarSolidOffsetReport;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using d8test::Check;

using Loop = std::vector<Point3d>;

ON_3dVector Newell(const Loop& l) {
  ON_3dVector n(0, 0, 0);
  for (size_t i = 0; i < l.size(); ++i) {
    const Point3d& a = l[i];
    const Point3d& b = l[(i + 1) % l.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  n.Unitize();
  return n;
}

Brep MakeSolid(const std::vector<Loop>& loops) {
  std::vector<Brep::PlanarFace> fs;
  for (const Loop& l : loops) {
    Brep::PlanarFace f;
    f.loop = l;
    f.plane = ON_Plane(l[0], Newell(l));
    fs.push_back(f);
  }
  return Brep::FromPlanarFaces(fs);
}

// Signed volume of a set of outward planar polygons: sum over each polygon of the fan (p0, pk, pk+1) tetrahedra with the origin.
double PolyVolume(const std::vector<Brep::PlanarFace>& fs) {
  double v = 0.0;
  for (const auto& f : fs) {
    const Loop& l = f.loop;
    for (size_t k = 1; k + 1 < l.size(); ++k) {
      const ON_3dVector a(l[0].x, l[0].y, l[0].z), b(l[k].x, l[k].y, l[k].z), c(l[k + 1].x, l[k + 1].y, l[k + 1].z);
      v += ON_DotProduct(a, ON_CrossProduct(b, c)) / 6.0;
    }
  }
  return v;
}

// Right prism over a CCW polygon (xy), z in [0, H].
Brep Prism(const std::vector<std::pair<double, double>>& P, double H) {
  std::vector<Loop> ls;
  Loop bottom, top;
  for (size_t i = P.size(); i-- > 0;) bottom.push_back(Point3d(P[i].first, P[i].second, 0));
  for (const auto& p : P) top.push_back(Point3d(p.first, p.second, H));
  ls.push_back(bottom);
  ls.push_back(top);
  for (size_t i = 0; i < P.size(); ++i) {
    const auto& p = P[i];
    const auto& q = P[(i + 1) % P.size()];
    ls.push_back({Point3d(p.first, p.second, 0), Point3d(q.first, q.second, 0), Point3d(q.first, q.second, H), Point3d(p.first, p.second, H)});
  }
  return MakeSolid(ls);
}

// Mitre offset area of a CCW polygon by d, from line-line intersections of the offset edges (independent of the 3D routine).
double MitreArea(const std::vector<std::pair<double, double>>& P, double d) {
  const size_t n = P.size();
  struct Line { double px, py, dx, dy; };
  std::vector<Line> L;
  for (size_t i = 0; i < n; ++i) {
    const auto& p = P[i];
    const auto& q = P[(i + 1) % n];
    double dx = q.first - p.first, dy = q.second - p.second;
    const double len = std::hypot(dx, dy);
    dx /= len;
    dy /= len;
    L.push_back({p.first + d * dy, p.second - d * dx, dx, dy});  // outward normal of a CCW edge = (dy, -dx)
  }
  std::vector<std::pair<double, double>> Q(n);
  for (size_t i = 0; i < n; ++i) {  // vertex i is shared by edge i-1 and edge i
    const Line& a = L[(i + n - 1) % n];
    const Line& b = L[i];
    const double den = a.dx * b.dy - a.dy * b.dx;
    const double t = ((b.px - a.px) * b.dy - (b.py - a.py) * b.dx) / den;
    Q[i] = {a.px + t * a.dx, a.py + t * a.dy};
  }
  double A = 0.0;
  for (size_t i = 0; i < n; ++i) A += Q[i].first * Q[(i + 1) % n].second - Q[(i + 1) % n].first * Q[i].second;
  return 0.5 * A;
}

// Notch solid: the cube [0,10]^3 minus the corner cube [5,10]^3 (nine faces, 14 vertices, every vertex of valence 3).
Brep NotchSolid() {
  const Point3d A(0, 0, 0), B(10, 0, 0), C(10, 10, 0), D(0, 10, 0), E(0, 0, 10), F(10, 0, 10), G(0, 10, 10), P(5, 10, 10), Q(10, 5, 10),
      R(10, 10, 5), S(5, 5, 10), T(5, 10, 5), U(10, 5, 5), W(5, 5, 5);
  return MakeSolid({{A, D, C, B}, {E, F, Q, S, P, G}, {A, E, G, D}, {B, C, R, U, Q, F}, {A, B, F, E}, {D, G, P, T, R, C}, {W, T, P, S}, {W, S, Q, U}, {W, U, R, T}});
}

double RelErr(double a, double b) { return std::fabs(a - b) / std::max(1.0, std::fabs(b)); }

// Every face of `out` lies on the plane of the same-index face of `in` moved by d along its normal.
bool FacesOnMovedPlanes(const Brep& in, const Brep& out, double d, double* worst) {
  const auto fi = in.PlanarFaces();
  const auto fo = out.PlanarFaces();
  *worst = 0.0;
  if (fi.size() != fo.size()) return false;
  for (size_t i = 0; i < fi.size(); ++i) {
    const ON_3dVector n = Newell(fi[i].loop);
    for (const Point3d& p : fo[i].loop) {
      const ON_3dVector r(p.x - fi[i].loop[0].x, p.y - fi[i].loop[0].y, p.z - fi[i].loop[0].z);
      *worst = std::max(*worst, std::fabs(ON_DotProduct(n, r) - d));
    }
  }
  return true;
}

std::string S(double v) { return std::to_string(v); }

}  // namespace

D8_EXTRA_TEST(OffsetSolidPlanarCubeMatchesClosedFormAndConvexRoutine) {
  const Brep cube = Brep::Box(0, 0, 0, 10, 10, 10);
  for (double d : {2.0, 0.5, -0.5, -2.0, -4.0}) {
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(cube, d, out, &rep);
    Check(r == Result::Ok, ("cube d=" + S(d) + ": offset succeeds (" + rep.refusal_reason + ")").c_str());
    if (r != Result::Ok) continue;
    const double want = std::pow(10.0 + 2.0 * d, 3.0);
    Check(RelErr(PolyVolume(out.PlanarFaces()), want) < 1e-12, ("cube d=" + S(d) + ": independent polygon volume = (10+2d)^3 = " + S(want)).c_str());
    Check(RelErr(rep.volume_after, want) < 1e-12 && RelErr(rep.volume_before, 1000.0) < 1e-12, ("cube d=" + S(d) + ": the report's volumes agree").c_str());
    double worst = 0.0;
    Check(FacesOnMovedPlanes(cube, out, d, &worst) && worst < 1e-12, ("cube d=" + S(d) + ": every face lies on its source plane moved by d (worst " + S(worst) + ")").c_str());
    const Brep ref = dino8::kernel::OffsetSolidConvexPlanar(cube, d);
    Check(RelErr(PolyVolume(ref.PlanarFaces()), want) < 1e-12, ("cube d=" + S(d) + ": the independent convex routine agrees with the closed form too").c_str());
    Check(out.raw().IsSolid() && out.raw().IsValid(), ("cube d=" + S(d) + ": result is a valid closed solid").c_str());
  }
}

D8_EXTRA_TEST(OffsetSolidPlanarNotchedCubeNonConvex) {
  const Brep notch = NotchSolid();
  Check(RelErr(PolyVolume(notch.PlanarFaces()), 875.0) < 1e-12, "fixture: cube minus corner cube has volume 1000 - 125 = 875");
  {
    Brep out;
    PlanarSolidOffsetReport rep;
    Check(dino8::kernel::OffsetSolidPlanar(notch, 1.0, out, &rep) == Result::Ok, ("notch d=1: succeeds (" + rep.refusal_reason + ")").c_str());
    Check(rep.faces == 9 && rep.vertices == 14 && rep.edges == 21, ("notch: 9 faces, 14 vertices, 21 edges (got " + std::to_string(rep.faces) + ", " + std::to_string(rep.vertices) + ", " + std::to_string(rep.edges) + ")").c_str());
    Check(rep.vertices_over_three_faces == 0 && rep.rank_deficient_vertices == 0, "notch: every vertex is a plain three-plane corner");
  }
  for (double d : {3.0, 2.0, 1.0, 0.25, -0.5, -1.0, -2.0}) {
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(notch, d, out, &rep);
    Check(r == Result::Ok, ("notch d=" + S(d) + ": offset succeeds (" + rep.refusal_reason + ")").c_str());
    if (r != Result::Ok) continue;
    const double want = std::pow(10.0 + 2.0 * d, 3.0) - 125.0;  // the notch keeps its 5 x 5 x 5 size under a mitre offset
    Check(RelErr(PolyVolume(out.PlanarFaces()), want) < 1e-12, ("notch d=" + S(d) + ": independent polygon volume = (10+2d)^3 - 125 = " + S(want)).c_str());
    double worst = 0.0;
    Check(FacesOnMovedPlanes(notch, out, d, &worst) && worst < 1e-12, ("notch d=" + S(d) + ": every face on its source plane moved by d (worst " + S(worst) + ")").c_str());
    Check(rep.self_intersections == 0 && rep.min_edge_length_ratio > 0.0, ("notch d=" + S(d) + ": no self-intersections, no edge collapsed").c_str());
    const Mesh m = out.TessellateToClosedMesh(4, 4);
    Check(m.IsClosedManifold() && RelErr(std::fabs(m.Volume()), want) < 1e-5, ("notch d=" + S(d) + ": the tessellated result is closed and has that volume").c_str());
  }
}

D8_EXTRA_TEST(OffsetSolidPlanarRightPrismsMatchThe2DMitreClosedForm) {
  struct Case { const char* name; std::vector<std::pair<double, double>> poly; double H; };
  const std::vector<Case> cases = {
      {"L a=20 w=10", {{0, 0}, {20, 0}, {20, 10}, {10, 10}, {10, 20}, {0, 20}}, 8.0},
      {"L a=20 w=6", {{0, 0}, {20, 0}, {20, 6}, {6, 6}, {6, 20}, {0, 20}}, 8.0},
      {"T", {{0, 0}, {12, 0}, {12, 4}, {8, 4}, {8, 10}, {4, 10}, {4, 4}, {0, 4}}, 5.0},
      {"C ring with a 1-wide slit", {{0, 0}, {4.5, 0}, {4.5, 3}, {3, 3}, {3, 7}, {7, 7}, {7, 3}, {5.5, 3}, {5.5, 0}, {10, 0}, {10, 10}, {0, 10}}, 4.0},
  };
  for (const Case& c : cases) {
    const Brep p = Prism(c.poly, c.H);
    for (double d : {0.2, 0.4, -0.3, -1.0}) {
      Brep out;
      PlanarSolidOffsetReport rep;
      const Result r = dino8::kernel::OffsetSolidPlanar(p, d, out, &rep);
      const std::string tag = std::string(c.name) + " d=" + S(d);
      Check(r == Result::Ok, (tag + ": offset succeeds (" + rep.refusal_reason + ")").c_str());
      if (r != Result::Ok) continue;
      const double want = MitreArea(c.poly, d) * (c.H + 2.0 * d);
      Check(RelErr(PolyVolume(out.PlanarFaces()), want) < 1e-11, (tag + ": polygon volume = mitre area * (H + 2d) = " + S(want)).c_str());
      double worst = 0.0;
      Check(FacesOnMovedPlanes(p, out, d, &worst) && worst < 1e-11, (tag + ": every face on its source plane moved by d").c_str());
    }
  }
}

D8_EXTRA_TEST(OffsetSolidPlanarSymmetricPyramidApexAgreesWithConvexRoutine) {
  // The apex joins FOUR faces; their offset planes stay concurrent for a symmetric pyramid, so it must be accepted (valence-4 path).
  const Point3d a(-2, -2, 0), b(2, -2, 0), c(2, 2, 0), e(-2, 2, 0), apex(0, 0, 3);
  const Brep pyr = MakeSolid({{a, e, c, b}, {a, b, apex}, {b, c, apex}, {c, e, apex}, {e, a, apex}});
  for (double d : {0.5, -0.3}) {
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(pyr, d, out, &rep);
    Check(r == Result::Ok, ("pyramid d=" + S(d) + ": accepted (" + rep.refusal_reason + ")").c_str());
    if (r != Result::Ok) continue;
    Check(rep.vertices_over_three_faces == 1 && rep.max_plane_residual < 1e-12, ("pyramid d=" + S(d) + ": one valence-4 vertex, its four offset planes concurrent (residual " + S(rep.max_plane_residual) + ")").c_str());
    const Brep ref = dino8::kernel::OffsetSolidConvexPlanar(pyr, d);
    Check(RelErr(PolyVolume(out.PlanarFaces()), PolyVolume(ref.PlanarFaces())) < 1e-10, ("pyramid d=" + S(d) + ": volume equals the independent convex routine's").c_str());
  }
}

D8_EXTRA_TEST(OffsetSolidPlanarRefusesNonConcurrentApexWithTheMeasuredResidual) {
  // An off-centre apex: the four offset side planes do not meet in a point. The residual is recomputed here by solving three of
  // the planes and evaluating the fourth.
  const Point3d a(0, 0, 0), b(4, 0, 0), c(4, 2, 0), e(0, 2, 0), apex(1, 0.5, 3);
  const Brep pyr = MakeSolid({{a, e, c, b}, {a, b, apex}, {b, c, apex}, {c, e, apex}, {e, a, apex}});
  const double d = 0.3;
  const ON_3dVector n[4] = {Newell({a, b, apex}), Newell({b, c, apex}), Newell({c, e, apex}), Newell({e, a, apex})};
  const Point3d p0[4] = {a, b, c, e};
  // Solve n0.x = n0.p0 + d, n1.x = ..., n2.x = ... by Cramer's rule.
  auto rhs = [&](int i) { return n[i].x * p0[i].x + n[i].y * p0[i].y + n[i].z * p0[i].z + d; };
  const double det = ON_DotProduct(n[0], ON_CrossProduct(n[1], n[2]));
  ON_3dVector x = (rhs(0) * ON_CrossProduct(n[1], n[2]) + rhs(1) * ON_CrossProduct(n[2], n[0]) + rhs(2) * ON_CrossProduct(n[0], n[1])) / det;
  const double resid3 = std::fabs(ON_DotProduct(n[3], x) - rhs(3));
  Check(resid3 > 1e-2, ("fixture: the fourth plane misses the other three's meeting point by " + S(resid3)).c_str());
  Brep out = Brep::Box(0, 0, 0, 1, 1, 1);
  PlanarSolidOffsetReport rep;
  const Result r = dino8::kernel::OffsetSolidPlanar(pyr, d, out, &rep);
  Check(r == Result::Failed, "off-centre pyramid: refused");
  Check(rep.refusal_reason.find("concurrent") != std::string::npos, ("refusal names the concurrency (" + rep.refusal_reason + ")").c_str());
  Check(rep.max_plane_residual > 1e-3 && rep.max_plane_residual < 1.5 * resid3 + 1e-9, ("the reported residual " + S(rep.max_plane_residual) + " is of the order of the recomputed " + S(resid3)).c_str());
  Check(out.raw().m_F.Count() == 6, "the output is left untouched on refusal");
  // d = 0 keeps the input and is accepted (the planes are concurrent at the apex).
  Brep zero;
  Check(dino8::kernel::OffsetSolidPlanar(pyr, 0.0, zero, nullptr) == Result::Ok && RelErr(PolyVolume(zero.PlanarFaces()), PolyVolume(pyr.PlanarFaces())) < 1e-12, "d = 0 returns the same volume");
}

D8_EXTRA_TEST(OffsetSolidPlanarRefusesATopologyEventWithTheMeasuredEdge) {
  const Brep cube = Brep::Box(0, 0, 0, 10, 10, 10);
  for (double d : {-5.0, -6.0}) {
    Brep out = Brep::Box(0, 0, 0, 1, 1, 1);
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(cube, d, out, &rep);
    Check(r == Result::Failed, ("cube d=" + S(d) + ": the solid collapses / inverts, refused").c_str());
    Check(rep.refusal_reason.find("edge") != std::string::npos && rep.min_edge_length_ratio <= 1e-9,
          ("cube d=" + S(d) + ": refusal names the edge and its ratio " + S(rep.min_edge_length_ratio) + " (" + rep.refusal_reason + ")").c_str());
    Check(out.raw().m_F.Count() == 6, ("cube d=" + S(d) + ": output untouched").c_str());
  }
  // A slot closing: the slot's floor edge (width 2) reverses at d = 1.5.
  const Brep u = Prism({{0, 0}, {10, 0}, {10, 10}, {6, 10}, {6, 4}, {4, 4}, {4, 10}, {0, 10}}, 5.0);
  Brep out;
  PlanarSolidOffsetReport rep;
  Check(dino8::kernel::OffsetSolidPlanar(u, 0.5, out, &rep) == Result::Ok, ("U with a 2-wide slot, d=0.5: fine (" + rep.refusal_reason + ")").c_str());
  Check(dino8::kernel::OffsetSolidPlanar(u, 1.5, out, &rep) == Result::Failed && rep.min_edge_length_ratio < 0.0,
        ("U with a 2-wide slot, d=1.5: the slot floor edge reverses, ratio " + S(rep.min_edge_length_ratio) + " (" + rep.refusal_reason + ")").c_str());
}

D8_EXTRA_TEST(OffsetSolidPlanarRefusesAGlobalOverlapNoLocalEdgeShows) {
  // A square ring cut open by a 1-wide slit that reaches the outside: closing the slit moves its two end faces through each other
  // while every edge of the solid keeps its direction and grows. Only the global check can see it.
  const std::vector<std::pair<double, double>> ring = {{0, 0}, {4.5, 0}, {4.5, 3}, {3, 3}, {3, 7}, {7, 7}, {7, 3}, {5.5, 3}, {5.5, 0}, {10, 0}, {10, 10}, {0, 10}};
  const Brep p = Prism(ring, 4.0);
  {
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(p, 0.4, out, &rep);
    Check(r == Result::Ok && rep.self_intersections == 0, ("slit 1 wide, d=0.4 (gap 0.2 left): accepted, no crossing (" + rep.refusal_reason + ")").c_str());
  }
  for (double d : {0.6, 0.8}) {
    Brep out = Brep::Box(0, 0, 0, 1, 1, 1);
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(p, d, out, &rep);
    Check(r == Result::Failed, ("slit 1 wide, d=" + S(d) + ": the two end faces cross, refused").c_str());
    Check(rep.min_edge_length_ratio > 0.0, ("d=" + S(d) + ": no edge collapsed or reversed (ratio " + S(rep.min_edge_length_ratio) + "), so it is the global check that fires").c_str());
    Check(rep.refusal_reason.find("self-intersect") != std::string::npos && rep.self_intersections > 0,
          ("d=" + S(d) + ": refusal reports the measured crossings (" + std::to_string(rep.self_intersections) + "): " + rep.refusal_reason).c_str());
    Check(out.raw().m_F.Count() == 6, "output untouched");
  }
}

D8_EXTRA_TEST(OffsetSolidPlanarRefusesWhatItCannotRepresentWithAReason) {
  {
    Brep out = Brep::Box(0, 0, 0, 1, 1, 1);
    PlanarSolidOffsetReport rep;
    const Brep cyl = Brep::Sphere(Point3d(0, 0, 0), 3.0);
    Check(dino8::kernel::OffsetSolidPlanar(cyl, 0.5, out, &rep) == Result::Failed, "a sphere: refused");
    Check(rep.refusal_reason.find("planar") != std::string::npos, ("...naming the non-planar face (" + rep.refusal_reason + ")").c_str());
    Check(out.raw().m_F.Count() == 6, "...output untouched");
  }
  {
    // A box missing its top: five faces, the rim edges have no partner.
    const Point3d a(0, 0, 0), b(2, 0, 0), c(2, 2, 0), e(0, 2, 0), a1(0, 0, 2), b1(2, 0, 2), c1(2, 2, 2), e1(0, 2, 2);
    const Brep open = MakeSolid({{a, e, c, b}, {a, b, b1, a1}, {b, c, c1, b1}, {c, e, e1, c1}, {e, a, a1, e1}});
    Brep out;
    PlanarSolidOffsetReport rep;
    Check(dino8::kernel::OffsetSolidPlanar(open, 0.2, out, &rep) == Result::Failed, "an open box: refused");
    Check(rep.refusal_reason.find("unpaired") != std::string::npos && rep.refusal_reason.find("4") != std::string::npos, ("...reporting the 4 unpaired edges (" + rep.refusal_reason + ")").c_str());
  }
}

D8_EXTRA_TEST(OffsetSolidPlanarRefusesAFaceThatCrossesItselfWithoutAnyEdgeReversing) {
  // A dovetail slot (1 wide at the mouth, 5 wide at the floor): offsetting outward closes the mouth - the two slanted walls cross
  // each other in the top and bottom faces - while the floor edge (5 -> 5 - 3.236 d) and every wall edge keep their direction.
  const std::vector<std::pair<double, double>> dove = {{0, 0}, {10, 0}, {10, 6}, {5.5, 6}, {7.5, 2}, {2.5, 2}, {4.5, 6}, {0, 6}};
  const Brep p = Prism(dove, 3.0);
  for (double d : {0.1, 0.25}) {  // the mouth closes at d = 0.309
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(p, d, out, &rep);
    Check(r == Result::Ok, ("dovetail d=" + S(d) + ": accepted below the closing distance (" + rep.refusal_reason + ")").c_str());
    if (r != Result::Ok) continue;
    const double want = MitreArea(dove, d) * (3.0 + 2.0 * d);
    Check(RelErr(PolyVolume(out.PlanarFaces()), want) < 1e-11, ("dovetail d=" + S(d) + ": polygon volume = mitre area * (H + 2d) = " + S(want)).c_str());
  }
  for (double d : {0.4, 0.5}) {
    Brep out = Brep::Box(0, 0, 0, 1, 1, 1);
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(p, d, out, &rep);
    Check(r == Result::Failed, ("dovetail d=" + S(d) + ": the mouth has closed, refused").c_str());
    Check(rep.min_edge_length_ratio > 0.0, ("dovetail d=" + S(d) + ": no edge reversed (ratio " + S(rep.min_edge_length_ratio) + ")").c_str());
    Check(rep.refusal_reason.find("crosses itself") != std::string::npos && rep.self_intersections == -1,
          ("dovetail d=" + S(d) + ": refused by the per-face check, before any mesh: " + rep.refusal_reason).c_str());
    Check(out.raw().m_F.Count() == 6, "output untouched");
  }
}
