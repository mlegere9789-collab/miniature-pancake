// Constrained Delaunay triangulation of polygons with holes, and the trimmed-surface mesh built on it. Every expectation is
// computed here, independently of the code under test: the shoelace area, the Euler count of a triangulated polygon
// (T = 2 V_interior + V_boundary + 2 holes - 2), an in-circle determinant written out again, a ray-cast point-in-polygon, and
// the boundary-edge multiset of a triangulation.
#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dino8/kernel/delaunay.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Mesh;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point2d;
using dino8::kernel::Point3d;
using dino8::kernel::Triangulation2D;
using dino8::kernel::TriangulateConstrainedDelaunay;
using Loop = std::vector<Point2d>;

double Shoelace(const Loop& l) {
  double a = 0;
  for (size_t i = 0; i < l.size(); ++i) a += l[i].x * l[(i + 1) % l.size()].y - l[(i + 1) % l.size()].x * l[i].y;
  return 0.5 * a;
}

double Cross(const Point2d& a, const Point2d& b, const Point2d& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

bool Inside(const Point2d& p, const Loop& l) {
  bool in = false;
  for (size_t i = 0, j = l.size() - 1; i < l.size(); j = i++) {
    if (((l[i].y > p.y) != (l[j].y > p.y)) && (p.x < (l[j].x - l[i].x) * (p.y - l[i].y) / (l[j].y - l[i].y) + l[i].x)) in = !in;
  }
  return in;
}

// the determinant form of the in-circle test, rebuilt from the textbook 4x4 matrix rather than the 3x3 shortcut the code uses
double InCircle4(const Point2d& a, const Point2d& b, const Point2d& c, const Point2d& d) {
  const double m[4][4] = {{a.x, a.y, a.x * a.x + a.y * a.y, 1}, {b.x, b.y, b.x * b.x + b.y * b.y, 1},
                          {c.x, c.y, c.x * c.x + c.y * c.y, 1}, {d.x, d.y, d.x * d.x + d.y * d.y, 1}};
  // cofactor expansion along the last column
  double det = 0;
  for (int r = 0; r < 4; ++r) {
    double sub[3][3];
    for (int i = 0, ri = 0; i < 4; ++i) {
      if (i == r) continue;
      for (int j = 0; j < 3; ++j) sub[ri][j] = m[i][j];
      ++ri;
    }
    const double d3 = sub[0][0] * (sub[1][1] * sub[2][2] - sub[1][2] * sub[2][1]) - sub[0][1] * (sub[1][0] * sub[2][2] - sub[1][2] * sub[2][0]) +
                      sub[0][2] * (sub[1][0] * sub[2][1] - sub[1][1] * sub[2][0]);
    det += ((r + 3) % 2 == 0 ? 1.0 : -1.0) * m[r][3] * d3;
  }
  return det;
}

struct Verdict {
  bool areas = false, orientation = false, euler = false, locally_delaunay = false, boundary = false, inside = false;
  double area_error = 0;
  int interior_edges_checked = 0;
};

Verdict Judge(const Triangulation2D& t, const Loop& outer, const std::vector<Loop>& holes) {
  Verdict v;
  double area = 0;
  v.orientation = true;
  for (const auto& tri : t.triangles) {
    const double a = 0.5 * Cross(t.points[tri[0]], t.points[tri[1]], t.points[tri[2]]);
    area += a;
    if (!(a > 0)) v.orientation = false;
  }
  double want = std::fabs(Shoelace(outer));
  for (const Loop& h : holes) want -= std::fabs(Shoelace(h));
  v.area_error = std::fabs(area - want);
  v.areas = v.area_error <= 1e-9 * want;
  // Euler: T = 2 V_interior + V_boundary + 2 h - 2
  const int vb = t.outer_count + [&] { int s = 0; for (int h : t.hole_sizes) s += h; return s; }();
  const int vi = static_cast<int>(t.points.size()) - vb;
  v.euler = static_cast<int>(t.triangles.size()) == 2 * vi + vb + 2 * static_cast<int>(holes.size()) - 2;
  // boundary edges: exactly the polygon edges, each used once; every other edge used twice
  std::map<std::pair<int, int>, int> use;
  for (const auto& tri : t.triangles) for (int k = 0; k < 3; ++k) ++use[{std::min(tri[k], tri[(k + 1) % 3]), std::max(tri[k], tri[(k + 1) % 3])}];
  std::set<std::pair<int, int>> polygon;
  int base = 0;
  const auto add_loop = [&](int n) {
    for (int i = 0; i < n; ++i) polygon.insert({std::min(base + i, base + (i + 1) % n), std::max(base + i, base + (i + 1) % n)});
    base += n;
  };
  add_loop(t.outer_count);
  for (int h : t.hole_sizes) add_loop(h);
  v.boundary = true;
  for (const auto& [e, n] : use) {
    if (polygon.count(e) ? n != 1 : n != 2) v.boundary = false;
  }
  for (const auto& e : polygon) if (!use.count(e) || use[e] != 1) v.boundary = false;
  // locally Delaunay across every interior edge, with the 4x4 determinant
  v.locally_delaunay = true;
  std::map<std::pair<int, int>, std::vector<int>> apex;
  for (const auto& tri : t.triangles) for (int k = 0; k < 3; ++k) apex[{std::min(tri[k], tri[(k + 1) % 3]), std::max(tri[k], tri[(k + 1) % 3])}].push_back(tri[(k + 2) % 3]);
  for (const auto& tri : t.triangles) {
    for (int k = 0; k < 3; ++k) {
      const std::pair<int, int> e{std::min(tri[k], tri[(k + 1) % 3]), std::max(tri[k], tri[(k + 1) % 3])};
      if (polygon.count(e)) continue;
      for (int d : apex[e]) {
        if (d == tri[(k + 2) % 3]) continue;
        ++v.interior_edges_checked;
        if (InCircle4(t.points[tri[0]], t.points[tri[1]], t.points[tri[2]], t.points[d]) > 1e-9) v.locally_delaunay = false;
      }
    }
  }
  // every triangle centroid is in the domain
  v.inside = true;
  for (const auto& tri : t.triangles) {
    const Point2d c((t.points[tri[0]].x + t.points[tri[1]].x + t.points[tri[2]].x) / 3, (t.points[tri[0]].y + t.points[tri[1]].y + t.points[tri[2]].y) / 3);
    if (!Inside(c, outer)) v.inside = false;
    for (const Loop& h : holes) if (Inside(c, h)) v.inside = false;
  }
  return v;
}

void Report(const std::string& name, const Verdict& v, const Triangulation2D& t) {
  d8test::Check(v.areas, (name + ": the triangle areas sum to the shoelace area of the domain (error " + std::to_string(v.area_error) + ")").c_str());
  d8test::Check(v.orientation, (name + ": every triangle is counter-clockwise with positive area").c_str());
  d8test::Check(v.euler, (name + ": T = 2 V_interior + V_boundary + 2 holes - 2 (" + std::to_string(t.triangles.size()) + " triangles)").c_str());
  d8test::Check(v.boundary, (name + ": the polygon edges are each used by exactly one triangle, every other edge by two").c_str());
  d8test::Check(v.locally_delaunay, (name + ": every interior edge passes the in-circle test (" + std::to_string(v.interior_edges_checked) + " checked)").c_str());
  d8test::Check(v.inside, (name + ": every triangle centroid lies in the domain (inside the outer loop, outside the holes)").c_str());
}

Loop Square(double x0, double y0, double x1, double y1) { return {Point2d(x0, y0), Point2d(x1, y0), Point2d(x1, y1), Point2d(x0, y1)}; }

Loop Circle(double cx, double cy, double r, int n, double phase = 0.0) {
  Loop l;
  for (int i = 0; i < n; ++i) l.push_back(Point2d(cx + r * std::cos(phase + 2 * ON_PI * i / n), cy + r * std::sin(phase + 2 * ON_PI * i / n)));
  return l;
}

}  // namespace

D8_EXTRA_TEST(DelaunayConvexPolygonAndSquareWithSquareHole) {
  {
    const Loop sq = Square(0, 0, 1, 1);
    const Triangulation2D t = TriangulateConstrainedDelaunay(sq);
    d8test::Check(t.triangles.size() == 2, "unit square: n - 2 = 2 triangles");
    Report("unit square", Judge(t, sq, {}), t);
  }
  {
    const Loop outer = Square(0, 0, 10, 10), hole = Square(4, 4, 6, 6);
    const Triangulation2D t = TriangulateConstrainedDelaunay(outer, {hole});
    d8test::Check(t.triangles.size() == 8, "square with a square hole: 8 vertices + 2*1 - 2 = 8 triangles");
    Report("square with hole", Judge(t, outer, {hole}), t);
    double a = 0;
    for (const auto& tri : t.triangles) a += 0.5 * Cross(t.points[tri[0]], t.points[tri[1]], t.points[tri[2]]);
    d8test::Check(std::fabs(a - 96.0) < 1e-9, "square with hole: area 100 - 4 = 96");
  }
}

D8_EXTRA_TEST(DelaunayLShapeAndStarWithTwoHolesAndSteinerLattice) {
  // a non-convex L, and a 5-pointed star outer loop with a square hole and a circular hole, with a Steiner lattice
  const Loop ell = {Point2d(0, 0), Point2d(6, 0), Point2d(6, 2), Point2d(2, 2), Point2d(2, 6), Point2d(0, 6)};
  {
    const Triangulation2D t = TriangulateConstrainedDelaunay(ell);
    d8test::Check(t.triangles.size() == 4, "L shape: 6 vertices - 2 = 4 triangles");
    Report("L shape", Judge(t, ell, {}), t);
  }
  Loop star;
  for (int i = 0; i < 10; ++i) {
    const double r = (i % 2 == 0) ? 9.0 : 4.0;
    star.push_back(Point2d(r * std::cos(ON_PI / 2 + ON_PI * i / 5), r * std::sin(ON_PI / 2 + ON_PI * i / 5)));
  }
  const Loop hole1 = Square(-1.5, -1.0, 0.5, 1.0), hole2 = Circle(2.2, 0.0, 0.9, 16);
  std::vector<Point2d> lattice;
  for (int i = -9; i <= 9; ++i) for (int j = -9; j <= 9; ++j) lattice.push_back(Point2d(i * 0.97 + 0.013, j * 0.97 + 0.021));
  const Triangulation2D t = TriangulateConstrainedDelaunay(star, {hole1, hole2}, lattice);
  d8test::Check(t.steiner_accepted > 60 && t.steiner_accepted < static_cast<int>(lattice.size()), ("star: " + std::to_string(t.steiner_accepted) + " of 361 lattice points accepted (those outside the star or inside a hole are skipped)").c_str());
  Report("star with two holes and a Steiner lattice", Judge(t, star, {hole1, hole2}), t);
  d8test::Check(t.flips > 0, "star: Lawson flips were needed to reach the Delaunay condition (the ear-clipped start is not Delaunay)");
  // control: without Steiner points the ear-clipped + flipped triangulation of the star is much worse shaped than with the lattice
  const Triangulation2D bare = TriangulateConstrainedDelaunay(star, {hole1, hole2});
  Report("star without Steiner points", Judge(bare, star, {hole1, hole2}), bare);
  d8test::Check(bare.triangles.size() < t.triangles.size() / 4, "the lattice refines the mesh: several times more triangles than the bare polygon triangulation");
}

D8_EXTRA_TEST(DelaunayRejectsInvalidLoopsAndSkipsUnusableSteinerPoints) {
  const auto throws = [](const std::vector<Point2d>& outer, const std::vector<Loop>& holes = {}) {
    try {
      TriangulateConstrainedDelaunay(outer, holes);
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  d8test::Check(throws({Point2d(0, 0), Point2d(1, 0)}), "a 2-point loop is refused");
  d8test::Check(throws({Point2d(0, 0), Point2d(1, 1), Point2d(1, 0), Point2d(0, 1)}), "a bow-tie (self-intersecting) loop is refused");
  d8test::Check(throws({Point2d(0, 0), Point2d(1, 0), Point2d(2, 0)}), "a zero-area loop is refused");
  d8test::Check(throws(Square(0, 0, 10, 10), {Square(20, 20, 22, 22)}), "a hole outside the outer loop is refused");
  d8test::Check(throws(Square(0, 0, 10, 10), {Square(8, 8, 12, 12)}), "a hole crossing the outer loop is refused");
  d8test::Check(throws(Square(0, 0, 10, 10), {Square(2, 2, 6, 6), Square(4, 4, 8, 8)}), "two overlapping holes are refused");
  d8test::Check(throws(Square(0, 0, 10, 10), {Square(0, 3, 2, 5)}), "a hole touching the outer loop is refused");
  // Steiner points on a vertex, on a boundary edge, outside and inside a hole are skipped, an interior one is taken
  const Loop outer = Square(0, 0, 10, 10), hole = Square(4, 4, 6, 6);
  const Triangulation2D t = TriangulateConstrainedDelaunay(outer, {hole}, {Point2d(0, 0), Point2d(5, 0), Point2d(-3, 5), Point2d(5, 5), Point2d(2, 2), Point2d(8, 7)});
  d8test::Check(t.steiner_accepted == 2, ("only the 2 usable Steiner points are inserted (" + std::to_string(t.steiner_accepted) + ")").c_str());
  Report("square with hole and two Steiner points", Judge(t, outer, {hole}), t);
  // a clockwise outer loop and a counter-clockwise hole are accepted and normalised
  Loop cw = Square(0, 0, 10, 10);
  std::reverse(cw.begin(), cw.end());
  Loop ccw_hole = Square(4, 4, 6, 6);
  const Triangulation2D t2 = TriangulateConstrainedDelaunay(cw, {ccw_hole});
  Report("reversed loops", Judge(t2, Square(0, 0, 10, 10), {Square(4, 4, 6, 6)}), t2);
}

D8_EXTRA_TEST(DelaunayTrimmedSurfaceMeshAreaBoundaryAndSphereCap) {
  // (1) a flat sheet 0..8 x 0..8 trimmed by a 24-gon disc radius 3.4 about (4,4) with a 12-gon hole of radius 1: the mesh area is
  // the polygon area (shoelace), the boundary loops are exactly the polygon vertices, the surface normal is +z
  std::vector<Point3d> grid = {Point3d(0, 0, 0), Point3d(0, 8, 0), Point3d(8, 0, 0), Point3d(8, 8, 0)};
  const NurbsSurface sheet = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);  // parameters 0..1: (8u, 8v, 0)
  const Loop outer = Circle(0.5, 0.5, 0.425, 24), hole = Circle(0.5, 0.5, 0.125, 12, 0.2);
  const Mesh m = dino8::kernel::TessellateTrimmedDelaunay(sheet, outer, {hole}, 12, 12);
  const ON_Mesh& raw = m.raw();
  double area = 0, zsum = 0;
  int up = 0;
  for (int f = 0; f < raw.m_F.Count(); ++f) {
    const ON_3dPoint a(raw.m_V[raw.m_F[f].vi[0]]), b(raw.m_V[raw.m_F[f].vi[1]]), c(raw.m_V[raw.m_F[f].vi[2]]);
    const double cz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    area += 0.5 * cz;
    if (cz > 0) ++up;
    zsum += std::fabs(a.z) + std::fabs(b.z) + std::fabs(c.z);
  }
  const double polygon_area = 64.0 * (std::fabs(Shoelace(outer)) - std::fabs(Shoelace(hole)));  // 8 x 8 scale of the (u, v) loops
  d8test::Check(std::fabs(area - polygon_area) < 1e-4 * polygon_area, ("flat trimmed sheet: mesh area " + std::to_string(area) + " = polygon area " + std::to_string(polygon_area)).c_str());
  d8test::Check(up == raw.m_F.Count() && zsum < 1e-6, "flat trimmed sheet: every face is wound counter-clockwise seen from +z (the surface normal) and lies in z = 0");
  d8test::Check(raw.m_V.Count() > 24 + 12 + 40 && raw.m_F.Count() > 100, ("flat trimmed sheet: interior points were added (" + std::to_string(raw.m_V.Count()) + " vertices, " + std::to_string(raw.m_F.Count()) + " faces)").c_str());
  // boundary edges of the mesh: 24 + 12
  std::map<std::pair<int, int>, int> use;
  for (int f = 0; f < raw.m_F.Count(); ++f) for (int k = 0; k < 3; ++k) {
    const int a = raw.m_F[f].vi[k], b = raw.m_F[f].vi[(k + 1) % 3];
    ++use[{std::min(a, b), std::max(a, b)}];
  }
  int naked = 0, nonmanifold = 0;
  for (const auto& [e, n] : use) { if (n == 1) ++naked; if (n > 2) ++nonmanifold; }
  d8test::Check(naked == 24 + 12 && nonmanifold == 0, ("flat trimmed sheet: exactly the 36 loop edges are naked (" + std::to_string(naked) + "), no edge is used 3 times").c_str());

  // (2) a spherical cap of radius 2 trimmed by a polygonal disc of half-angle 45 degrees in the (u, v) plane: the mesh area tends to
  // 2 pi R^2 (1 - cos 45 deg) = 2 pi 4 (1 - 0.70711) = 7.3604
  ON_Sphere sph(ON_3dPoint(0, 0, 0), 2.0);
  ON_NurbsSurface raws;
  sph.GetNurbForm(raws);
  NurbsSurface sphere;
  sphere.raw() = raws;
  const auto du = sphere.Domain(0), dv = sphere.Domain(1);
  // the sphere's own (u, v): u is the azimuth, v the latitude (south pole at dv.min, north at dv.max); a disc about the north pole
  // of colatitude <= 45 deg is the band v >= v_cut, a rectangle in (u, v): outer loop = the four corners of that band, which is the
  // cap with its seam cut open
  // a rational sphere's v is NOT linear in latitude, so the parameter whose latitude is 45 degrees (z = R sin 45 deg) is found by
  // bisection on the surface itself, independently of the mesher
  double lo = dv.min, hi = dv.max;
  for (int it = 0; it < 80; ++it) {
    const double mid = 0.5 * (lo + hi);
    if (sphere.PointAt(du.min, mid).z < 2.0 * std::sin(ON_PI / 4)) lo = mid; else hi = mid;
  }
  const double v_cut = 0.5 * (lo + hi);
  // the loop must sample the cut circle finely: a polygon edge is a straight chord in 3D, so a 3-point bottom edge would cut the sphere
  Loop band;
  for (int i = 0; i <= 24; ++i) band.push_back(Point2d(du.min + (du.max - du.min) * i / 24.0, v_cut));            // bottom, left to right
  for (int i = 24; i >= 0; --i) band.push_back(Point2d(du.min + (du.max - du.min) * i / 24.0, dv.max));          // top (the pole row), right to left
  const Mesh cap = dino8::kernel::TessellateTrimmedDelaunay(sphere, band, {}, 24, 8);
  double cap_area = 0;
  for (int f = 0; f < cap.raw().m_F.Count(); ++f) {
    const ON_3dPoint a(cap.raw().m_V[cap.raw().m_F[f].vi[0]]), b(cap.raw().m_V[cap.raw().m_F[f].vi[1]]), c(cap.raw().m_V[cap.raw().m_F[f].vi[2]]);
    cap_area += 0.5 * ON_CrossProduct(b - a, c - a).Length();
  }
  const double exact = 2 * ON_PI * 4.0 * (1 - std::cos(ON_PI / 4));
  d8test::Check(std::fabs(cap_area - exact) < 0.01 * exact, ("sphere cap: mesh area " + std::to_string(cap_area) + " is within 1% of 2 pi R^2 (1 - cos 45 deg) = " + std::to_string(exact)).c_str());
  double rmax_dev = 0;
  for (int i = 0; i < cap.raw().m_V.Count(); ++i) {
    const ON_3fPoint& v = cap.raw().m_V[i];
    rmax_dev = std::max(rmax_dev, std::fabs(std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z) - 2.0));
  }
  d8test::Check(rmax_dev < 1e-5, "sphere cap: every mesh vertex lies on the sphere of radius 2 (the vertices are surface evaluations)");
  // argument check
  bool threw = false;
  try {
    dino8::kernel::TessellateTrimmedDelaunay(sheet, outer, {}, 0, 4);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  d8test::Check(threw, "TessellateTrimmedDelaunay refuses 0 divisions");
}
