// FollowMePolyline (follow_me.h): a profile swept along a polyline path with mitred corners. The oracle is the same for every case and does not
// use the function's own arithmetic: a prism with obliquely cut ends has the volume (cross-section area) x (length of the line through the
// centroid), and the line through the centroid of a profile that sits a lateral distance delta INSIDE a 90 degree corner is shorter by delta at each
// leg (the mitre plane crosses the offset line delta before the corner); outside it is longer. So for a profile of area A:
//   L path (6 along x, then 4 along y), centroid on the path:     V = A (6 + 4)
//   the same, centroid 0.1 to the inside (left):                  V = A ((6 - 0.1) + (4 - 0.1)) = 9.8 A
//   closed 6 x 4 rectangle, centroid on the path:                 V = A 20
//   the same, centroid 0.2 inside:                                V = A (2 (6 - 0.4) + 2 (4 - 0.4)) = 18.4 A
// Topology by Euler's formula (open L: 12 vertices, 20 edges, 10 faces; closed ring: 20, 40, 20, characteristic 0) and planarity of every face from
// Newell's normal.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/follow_me.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::FollowMePolyline;
using dino8::kernel::Point3d;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

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

double WorstPlanarity(const Brep& b) {
  double worst = 0.0;
  for (const Brep::PlanarFace& f : b.PlanarFaces()) {
    double nx = 0, ny = 0, nz = 0, cx = 0, cy = 0, cz = 0;
    for (size_t i = 0; i < f.loop.size(); ++i) {
      const Point3d& a = f.loop[i];
      const Point3d& c = f.loop[(i + 1) % f.loop.size()];
      nx += (a.y - c.y) * (a.z + c.z);
      ny += (a.z - c.z) * (a.x + c.x);
      nz += (a.x - c.x) * (a.y + c.y);
      cx += a.x; cy += a.y; cz += a.z;
    }
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    const double k = static_cast<double>(f.loop.size());
    for (const Point3d& p : f.loop) worst = std::max(worst, std::fabs(((p.x - cx / k) * nx + (p.y - cy / k) * ny + (p.z - cz / k) * nz) / len));
  }
  return worst;
}

struct Counts { int v = 0, e = 0, f = 0, two = 0; };
Counts Count(const Brep& b) {
  Counts c;
  const ON_Brep& r = b.raw();
  for (int i = 0; i < r.m_V.Count(); ++i) c.v += r.m_V[i].m_vertex_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_F.Count(); ++i) c.f += r.m_F[i].m_face_index >= 0 ? 1 : 0;
  for (int i = 0; i < r.m_E.Count(); ++i) {
    if (r.m_E[i].m_edge_index < 0) continue;
    ++c.e;
    if (r.m_E[i].TrimCount() == 2) ++c.two;
  }
  return c;
}

// A square profile of side s in the plane x = x0 (perpendicular to a path running along x), centred at (x0, cy, cz).
std::vector<Point3d> SquareYZ(double x0, double cy, double cz, double s) {
  return {P(x0, cy - s / 2, cz - s / 2), P(x0, cy + s / 2, cz - s / 2), P(x0, cy + s / 2, cz + s / 2), P(x0, cy - s / 2, cz + s / 2)};
}

bool Refuses(const std::vector<Point3d>& profile, const std::vector<Point3d>& path, bool closed) {
  try { (void)FollowMePolyline(profile, path, closed); } catch (const std::exception&) { return true; }
  return false;
}

}  // namespace

// EDIT-0024, "Follow Me drags a face along a path of edges, highlighting the path in red; used for moldings, gutters and lathes". Acceptance
// sketch: "Extrude a 5 cm square profile along an L-path; solid volume equals profile area times path length (with mitre) within 1e-9." Units
// are metres: a 0.05 square centred on the start of the L path (0,0,0) - (6,0,0) - (6,4,0). Volume 0.0025 x 10 = 0.025; the same profile moved
// 0.1 to the inside of the turn gives 0.0025 x 9.8; a profile moved 0.1 outside gives 0.0025 x 10.2. The solid has 12 vertices, 20 edges and 10
// faces (Euler 2), every face planar within 1e-9, and the mitre corner is exact: the corner section lies in the plane x + y = 6.
D8_EXTRA_TEST(SuFollowMeSweepsASquareProfileAlongAnLPathWithAMitre) {
  const std::vector<Point3d> path = {P(0, 0, 0), P(6, 0, 0), P(6, 4, 0)};
  const double area = 0.05 * 0.05;
  const Brep centred = FollowMePolyline(SquareYZ(0, 0, 0, 0.05), path);
  const Counts c = Count(centred);
  d8test::Check(centred.raw().IsSolid() && centred.raw().IsValid(), "the swept L is a closed valid solid");
  d8test::Check(c.v == 12 && c.e == 20 && c.f == 10 && c.v - c.e + c.f == 2 && c.two == 20, "12 vertices, 20 edges, 10 faces: Euler's formula V - E + F = 2 holds and every edge is in two faces");
  d8test::Check(std::fabs(PolyhedronVolume(centred) - area * 10.0) < 1e-9 * 0.025 + 1e-15, Msg("volume is area x path length = 0.025", PolyhedronVolume(centred) - 0.025).c_str());
  d8test::Check(WorstPlanarity(centred) < 1e-9, Msg("every face is planar within 1e-9", WorstPlanarity(centred)).c_str());
  // the corner section: four vertices with x + y = 6 (the plane through (6,0,0) with normal (1,1,0))
  int on_mitre = 0;
  for (const Brep::PlanarFace& f : centred.PlanarFaces()) {
    bool all = f.loop.size() == 4;
    int corner_points = 0;
    for (const Point3d& p : f.loop) { if (std::fabs(p.x + p.y - 6.0) < 1e-9) ++corner_points; }
    if (all && corner_points == 4) ++on_mitre;
  }
  d8test::Check(on_mitre == 0, "no side face lies inside the mitre plane (the plane only cuts the sweep)");
  int mitre_vertices = 0;
  const std::vector<Brep::PlanarFace> pf = centred.PlanarFaces();
  std::vector<Point3d> seen;
  for (const Brep::PlanarFace& f : pf) for (const Point3d& p : f.loop) {
    if (std::fabs(p.x + p.y - 6.0) < 1e-9 && std::fabs(p.z) <= 0.025 + 1e-9) {
      bool dup = false;
      for (const Point3d& q : seen) if (q.DistanceTo(p) < 1e-9) dup = true;
      if (!dup) { seen.push_back(p); ++mitre_vertices; }
    }
  }
  d8test::Check(mitre_vertices == 4, Msg("the corner section has its 4 vertices on the plane x + y = 6", mitre_vertices).c_str());
  const Brep inside = FollowMePolyline(SquareYZ(0, 0.1, 0, 0.05), path);
  d8test::Check(std::fabs(PolyhedronVolume(inside) - area * 9.8) < 1e-9 * 0.025 + 1e-15, Msg("0.1 to the inside of the turn: 9.8 x area", PolyhedronVolume(inside) - area * 9.8).c_str());
  const Brep outside = FollowMePolyline(SquareYZ(0, -0.1, 0, 0.05), path);
  d8test::Check(std::fabs(PolyhedronVolume(outside) - area * 10.2) < 1e-9 * 0.025 + 1e-15, Msg("0.1 to the outside of the turn: 10.2 x area", PolyhedronVolume(outside) - area * 10.2).c_str());
  d8test::Check(WorstPlanarity(inside) < 1e-9 && WorstPlanarity(outside) < 1e-9, "the offset sweeps are planar too");
  // refusals: a profile plane that is not perpendicular to the first segment, and a corner that reverses direction
  d8test::Check(Refuses({P(0, 0, 0), P(0, 1, 0), P(1, 1, 0)}, path, false), "a profile in the plane z = 0 (parallel to the path) is refused");
  d8test::Check(Refuses(SquareYZ(0, 0, 0, 0.05), {P(0, 0, 0), P(6, 0, 0), P(0, 0, 0)}, false), "a path that turns back on itself is refused");
  d8test::Check(Refuses(SquareYZ(0, 0, 0, 3.0), {P(0, 0, 0), P(6, 0, 0), P(6, 1, 0)}, false), "a profile 3 wide on a leg only 1 long (the mitre plane lies behind part of the profile) is refused");
  d8test::Check(!Refuses(SquareYZ(0, 0, 0, 3.0), path, false), "the same 3 wide profile on the 6 and 4 long legs is fine");
}

// EDIT-0025, "Pre-selecting a continuous edge path then Follow Me and clicking the profile extrudes automatically". Acceptance sketch: "Preselect
// a 4-edge rectangular path; extrusion volume equals area times perimeter within 1e-9 (with mitre corrections)." The closed path is the rectangle
// (0,0,0) (6,0,0) (6,4,0) (0,4,0); the 0.1 square profile sits in the plane x = 3 on the first edge. Centred: V = 0.01 x 20 = 0.2. Moved 0.2 to the
// inside (+y): the centroid path is the inset rectangle 5.6 x 3.6, so V = 0.01 x 18.4. The ring has no caps: 20 vertices, 40 edges, 20 faces
// (Euler characteristic 0 of a torus), all edges in two faces, every face planar. A profile whose plane meets no segment is refused.
D8_EXTRA_TEST(SuFollowMeSweepsAProfileAroundAClosedRectangularPath) {
  const std::vector<Point3d> path = {P(0, 0, 0), P(6, 0, 0), P(6, 4, 0), P(0, 4, 0)};
  const double area = 0.1 * 0.1;
  const Brep ring = FollowMePolyline(SquareYZ(3, 0, 0, 0.1), path, true);
  const Counts c = Count(ring);
  d8test::Check(ring.raw().IsSolid() && ring.raw().IsValid(), "the swept ring is a valid closed shell");
  d8test::Check(c.v == 20 && c.e == 40 && c.f == 20 && c.v - c.e + c.f == 0 && c.two == 40, "20 vertices, 40 edges, 20 faces: characteristic 0 and every edge in two faces");
  d8test::Check(std::fabs(PolyhedronVolume(ring) - area * 20.0) < 1e-9 * 0.2 + 1e-15, Msg("volume is area x perimeter = 0.2", PolyhedronVolume(ring) - 0.2).c_str());
  d8test::Check(WorstPlanarity(ring) < 1e-9, Msg("every face is planar within 1e-9", WorstPlanarity(ring)).c_str());
  const Brep inset = FollowMePolyline(SquareYZ(3, 0.2, 0, 0.1), path, true);
  d8test::Check(std::fabs(PolyhedronVolume(inset) - area * 18.4) < 1e-9 * 0.2 + 1e-15, Msg("0.2 inside: area x (2 (6 - 0.4) + 2 (4 - 0.4)) = 0.184", PolyhedronVolume(inset) - 0.184).c_str());
  const Brep tall = FollowMePolyline({P(3, -0.05, 0), P(3, 0.05, 0), P(3, 0.05, 0.4), P(3, -0.05, 0.4)}, path, true);
  d8test::Check(std::fabs(PolyhedronVolume(tall) - 0.1 * 0.4 * 20.0) < 1e-9 * 0.8 + 1e-15, Msg("a 0.1 x 0.4 profile (a wall) gives area x perimeter = 0.8", PolyhedronVolume(tall) - 0.8).c_str());
  d8test::Check(Refuses(SquareYZ(7, 0, 0, 0.1), path, true), "a profile plane beyond the first segment (x = 7 on a 6 long edge) is refused");
}
