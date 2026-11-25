// ShellOpenExtrusion(): shell WITH face removal (cup / tube) of a prism over a polyline or circle profile.
//
// Oracles are independent of the construction:
//  - closed-form volumes (pi*(R^2 H - (R-t)^2 (H-t)) for a circular cup, pi*H*(R^2-(R-t)^2) for a tube; hand-derived
//    inner polygons and shoelace areas for the rectilinear L);
//  - a point-membership oracle in cylindrical / polygon coordinates compared with Mesh::ContainsPoint() on the
//    TESSELLATION of the result (a different code path from the construction and from Brep::Volume());
//  - cross-engine relations: the cup of a box profile equals ShellConvexPlanar(box, {top}, t) in volume and face
//    count; a polyline cup equals ShellClosedExtrusion() minus (inner area * t);
//  - every refusal is checked to be a refusal (and, where the reason is measurable, to name it).
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::ExtrusionOpening;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::ShellClosedExtrusion;
using dino8::kernel::ShellConvexPlanar;
using dino8::kernel::ShellOpenExtrusion;
using dino8::kernel::Vector3d;
using Poly = std::vector<std::pair<double, double>>;

NurbsCurve PolyCurve(const Poly& p, double z = 0.0) {
  std::vector<Point3d> pts;
  for (const auto& q : p) pts.emplace_back(q.first, q.second, z);
  pts.push_back(pts.front());
  return NurbsCurve::FromControlPoints(pts, 1);
}

NurbsCurve CircleCurve(const ON_3dPoint& center, ON_3dVector normal, double radius) {
  normal.Unitize();
  const ON_Circle c(ON_Plane(center, normal), radius);
  ON_NurbsCurve n;
  c.GetNurbForm(n);
  NurbsCurve out;
  out.raw() = n;
  return out;
}

double Shoelace(const Poly& p) {
  double a = 0.0;
  for (size_t i = 0; i < p.size(); ++i) {
    const auto& u = p[i];
    const auto& v = p[(i + 1) % p.size()];
    a += u.first * v.second - v.first * u.second;
  }
  return std::fabs(0.5 * a);
}

bool Throws(const std::function<void()>& f, std::string* what = nullptr) {
  try {
    f();
  } catch (const std::exception& e) {
    if (what) *what = e.what();
    return true;
  }
  return false;
}

bool PointInPoly(const Poly& p, double x, double y) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    if ((p[i].second > y) != (p[j].second > y) &&
        x < p[i].first + (y - p[i].second) * (p[j].first - p[i].first) / (p[j].second - p[i].second)) {
      in = !in;
    }
  }
  return in;
}

double DistToPolyBoundary(const Poly& p, double x, double y) {
  double best = 1e300;
  for (size_t i = 0; i < p.size(); ++i) {
    const auto& a = p[i];
    const auto& b = p[(i + 1) % p.size()];
    const double dx = b.first - a.first, dy = b.second - a.second;
    const double l2 = dx * dx + dy * dy;
    double s = l2 > 0 ? ((x - a.first) * dx + (y - a.second) * dy) / l2 : 0.0;
    s = std::min(1.0, std::max(0.0, s));
    best = std::min(best, std::hypot(x - (a.first + s * dx), y - (a.second + s * dy)));
  }
  return best;
}

// Material closed form of a cup/tube over a polygon (z measured along the extrusion direction from the profile).
struct PolyShellOracle {
  Poly outer, inner;
  double h, t;
  bool open_top, open_bottom;
  // returns -1 for "too close to a boundary to judge", 1 material, 0 empty
  int Classify(double x, double y, double z, double margin) const {
    const double lo = open_bottom ? 0.0 : t;
    const double hi = open_top ? h : h - t;
    const double d_out = DistToPolyBoundary(outer, x, y), d_in = DistToPolyBoundary(inner, x, y);
    if (std::fabs(z) < margin || std::fabs(z - h) < margin || std::fabs(z - lo) < margin || std::fabs(z - hi) < margin) return -1;
    if (d_out < margin || d_in < margin) return -1;
    if (z < 0.0 || z > h || !PointInPoly(outer, x, y)) return 0;
    const bool in_cavity = PointInPoly(inner, x, y) && z > lo && z < hi;
    return in_cavity ? 0 : 1;
  }
};

int CountMismatches(const Mesh& mesh, const std::function<int(const Point3d&)>& oracle, const Point3d& lo, const Point3d& hi,
                    int n, int* judged) {
  int bad = 0;
  *judged = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      for (int k = 0; k < n; ++k) {
        const Point3d p(lo.x + (hi.x - lo.x) * (i + 0.37) / n, lo.y + (hi.y - lo.y) * (j + 0.41) / n,
                        lo.z + (hi.z - lo.z) * (k + 0.29) / n);
        const int want = oracle(p);
        if (want < 0) continue;
        ++*judged;
        if (mesh.ContainsPoint(p) != (want == 1)) ++bad;
      }
    }
  }
  return bad;
}

// Cylindrical-coordinate oracle of a circular cup/tube about `center` with unit axis `ax` and reference `ex`.
struct CircleShellOracle {
  ON_3dPoint c;
  ON_3dVector ax;
  double R, h, t;
  bool open_top, open_bottom;
  int Classify(const Point3d& p, double margin) const {
    const ON_3dVector d = ON_3dPoint(p) - c;
    const double z = d * ax;
    const double r = (d - z * ax).Length();
    const double lo = open_bottom ? 0.0 : t;
    const double hi = open_top ? h : h - t;
    for (double zz : {0.0, h, lo, hi}) if (std::fabs(z - zz) < margin) return -1;
    for (double rr : {R, R - t}) if (std::fabs(r - rr) < margin) return -1;
    if (z < 0.0 || z > h || r > R) return 0;
    const bool cavity = r < R - t && z > lo && z < hi;
    return cavity ? 0 : 1;
  }
};

void CheckCircleShell(const char* tag, const ON_3dPoint& center, ON_3dVector axis, double R, double H, double t,
                      ExtrusionOpening op) {
  axis.Unitize();
  const bool open_top = op != ExtrusionOpening::Bottom;
  const bool open_bottom = op != ExtrusionOpening::Top;
  const NurbsCurve profile = CircleCurve(center, axis, R);
  const Vector3d dir(axis.x * H, axis.y * H, axis.z * H);
  Brep shell;
  std::string what;
  const bool threw = Throws([&] { shell = ShellOpenExtrusion(profile, dir, t, op); }, &what);
  d8test::Check(!threw, (std::string(tag) + ": builds (" + what + ")").c_str());
  if (threw) return;
  d8test::Check(shell.raw().IsValid() && shell.raw().IsSolid(), (std::string(tag) + ": ON_Brep IsValid() and IsSolid()").c_str());
  const double expected = open_top && open_bottom ? ON_PI * H * (R * R - (R - t) * (R - t))
                                                  : ON_PI * (R * R * H - (R - t) * (R - t) * (H - t));
  const double vol = shell.Volume();
  d8test::Check(std::fabs(vol - expected) <= 1e-6 * expected,
                (std::string(tag) + ": exact Brep::Volume() = closed form " + std::to_string(expected) + " (got " + std::to_string(vol) + ")").c_str());
  const Mesh mesh = shell.TessellateToClosedMesh(96, 64);
  const double mv = mesh.Volume();
  // The angular direction has 64 divisions: a 64-gon has area ratio sin(2pi/64)*64/(2pi) = 0.99839 (-0.16%), plus up to ~0.1% from
  // the profile-direction division count (measured: the deficit converges to the 64-gon limit), so the mesh sits just below.
  d8test::Check(mv < expected && mv > 0.995 * expected,
                (std::string(tag) + ": tessellated closed-mesh volume within 0.5% below the closed form (" + std::to_string(mv) + ")").c_str());
  const CircleShellOracle oracle{center, axis, R, H, t, open_top, open_bottom};
  // Sample box in the frame's own axes is awkward for a tilted axis: sample a world box that contains the body.
  const double ext = R + H + 1.0;
  int judged = 0;
  const int bad = CountMismatches(mesh, [&](const Point3d& p) { return oracle.Classify(p, 0.12); },
                                  Point3d(center.x - ext, center.y - ext, center.z - ext),
                                  Point3d(center.x + ext, center.y + ext, center.z + ext), 30, &judged);
  d8test::Check(judged > 5000 && bad == 0,
                (std::string(tag) + ": point membership (tessellation) agrees with the cylindrical closed form at " + std::to_string(judged) +
                 " judged points, mismatches " + std::to_string(bad)).c_str());
  // The opening really is open: a point just inside the removed cap's plane, on the axis, is empty space reached from outside.
  const ON_3dPoint near_top = center + (H - 0.5 * t) * axis;
  const ON_3dPoint near_bottom = center + (0.5 * t) * axis;
  d8test::Check(mesh.ContainsPoint(Point3d(near_top.x, near_top.y, near_top.z)) == !open_top,
                (std::string(tag) + ": axis point just below the top cap plane is " + (open_top ? "empty (top open)" : "wall material (top closed)")).c_str());
  d8test::Check(mesh.ContainsPoint(Point3d(near_bottom.x, near_bottom.y, near_bottom.z)) == !open_bottom,
                (std::string(tag) + ": axis point just above the bottom cap plane is " + (open_bottom ? "empty (bottom open)" : "wall material (bottom closed)")).c_str());
}

}  // namespace

D8_EXTRA_TEST(ShellOpenExtrusionCircleCupAndTube) {
  const ON_3dPoint o(0, 0, 0);
  CheckCircleShell("circle cup top-open R5 H10 t1", o, ON_3dVector(0, 0, 1), 5.0, 10.0, 1.0, ExtrusionOpening::Top);
  CheckCircleShell("circle cup bottom-open R5 H10 t1", o, ON_3dVector(0, 0, 1), 5.0, 10.0, 1.0, ExtrusionOpening::Bottom);
  CheckCircleShell("circle tube both-open R5 H10 t1", o, ON_3dVector(0, 0, 1), 5.0, 10.0, 1.0, ExtrusionOpening::Both);
  // A tube is not limited by its height: a short thick ring (H < t) is legal, a cup of the same height is not.
  CheckCircleShell("circle ring both-open R5 H0.6 t1", o, ON_3dVector(0, 0, 1), 5.0, 0.6, 1.0, ExtrusionOpening::Both);
}

D8_EXTRA_TEST(ShellOpenExtrusionCircleCupTiltedOffOrigin) {
  // Off-origin centre, oblique axis; "Top" follows `direction`, so a reversed direction flips which side is open.
  const ON_3dPoint c(3.0, -2.0, 4.0);
  CheckCircleShell("tilted circle cup (axis (1,2,2)/3)", c, ON_3dVector(1, 2, 2), 3.0, 7.0, 0.5, ExtrusionOpening::Top);
  CheckCircleShell("tilted circle tube", c, ON_3dVector(-2, 1, 3), 2.5, 4.0, 0.75, ExtrusionOpening::Both);
  // direction = -normal of the profile's own plane.
  const NurbsCurve prof = CircleCurve(c, ON_3dVector(0, 0, 1), 3.0);
  const Brep down = ShellOpenExtrusion(prof, Vector3d(0, 0, -6.0), 0.5, ExtrusionOpening::Top);
  const double expected = ON_PI * (9.0 * 6.0 - 6.25 * 5.5);
  d8test::Check(down.raw().IsSolid() && std::fabs(down.Volume() - expected) < 1e-6 * expected,
                "circle cup extruded against the profile normal: closed-solid with the closed-form volume");
  const Mesh dm = down.TessellateToClosedMesh(96, 64);
  // Extruded downward from z=4: body spans z in [-2, 4]; the open end ("Top") is at z = -2.
  d8test::Check(dm.ContainsPoint(Point3d(3.0, -2.0, 4.0 - 0.25)) && !dm.ContainsPoint(Point3d(3.0, -2.0, -1.0)) &&
                    !dm.ContainsPoint(Point3d(3.0, -2.0, -2.0 + 0.25)) && dm.ContainsPoint(Point3d(3.0 + 2.75, -2.0, 1.0)),
                "reversed direction: floor on the profile side (z=4), cavity open towards z=-2, side wall present");
}

D8_EXTRA_TEST(ShellOpenExtrusionConcaveLPolylineCupAndTube) {
  const Poly L = {{0, 0}, {6, 0}, {6, 2}, {2, 2}, {2, 6}, {0, 6}};
  const double h = 4.0;
  for (const double t : {0.5, 0.75}) {
    const Poly cav = {{t, t}, {6 - t, t}, {6 - t, 2 - t}, {2 - t, 2 - t}, {2 - t, 6 - t}, {t, 6 - t}};
    const double Ao = Shoelace(L), Ai = Shoelace(cav);
    for (const ExtrusionOpening op : {ExtrusionOpening::Top, ExtrusionOpening::Bottom, ExtrusionOpening::Both}) {
      const bool open_top = op != ExtrusionOpening::Bottom, open_bottom = op != ExtrusionOpening::Top;
      const std::string tag = std::string("L polyline t=") + std::to_string(t) + (open_top && open_bottom ? " tube" : open_top ? " cup(top)" : " cup(bottom)");
      Brep shell;
      std::string what;
      const bool threw = Throws([&] { shell = ShellOpenExtrusion(PolyCurve(L), Vector3d(0, 0, h), t, op); }, &what);
      d8test::Check(!threw, (tag + ": builds (" + what + ")").c_str());
      if (threw) continue;
      d8test::Check(shell.raw().IsValid() && shell.raw().IsSolid(), (tag + ": IsValid() and IsSolid()").c_str());
      const double expected = open_top && open_bottom ? (Ao - Ai) * h : Ao * h - Ai * (h - t);
      d8test::Check(std::fabs(shell.Volume() - expected) < 1e-6, (tag + ": Brep::Volume() = shoelace closed form " + std::to_string(expected)).c_str());
      // Face count: 2n walls + (n rim trapezoids per opening) + 2 faces per closed cap.
      const size_t n = L.size();
      const size_t want_faces = 2 * n + (open_top ? n : 2) + (open_bottom ? n : 2);
      d8test::Check(shell.FaceCount() == static_cast<int>(want_faces) && shell.PlanarFaces().size() == want_faces,
                    (tag + ": face count " + std::to_string(shell.FaceCount()) + " = 2n walls + rims/caps (" + std::to_string(want_faces) + "), all planar").c_str());
      const Mesh mesh = shell.TessellateToClosedMesh(4, 4);
      d8test::Check(std::fabs(mesh.Volume() - expected) < 1e-6, (tag + ": tessellated mesh volume (all faces planar: exact)").c_str());
      const PolyShellOracle oracle{L, cav, h, t, open_top, open_bottom};
      int judged = 0;
      const int bad = CountMismatches(mesh, [&](const Point3d& p) { return oracle.Classify(p.x, p.y, p.z, 0.06); },
                                      Point3d(-1, -1, -1), Point3d(7, 7, h + 1), 44, &judged);
      d8test::Check(judged > 20000 && bad == 0,
                    (tag + ": point membership agrees with the polygon closed form at " + std::to_string(judged) + " points, mismatches " + std::to_string(bad)).c_str());
    }
    // Cross-engine relation with the closed shell: removing the top cap's wall also empties the slab under it.
    const Brep closed = ShellClosedExtrusion(PolyCurve(L), Vector3d(0, 0, h), t);
    const Brep cup = ShellOpenExtrusion(PolyCurve(L), Vector3d(0, 0, h), t, ExtrusionOpening::Top);
    d8test::Check(std::fabs((closed.Volume() - cup.Volume()) - Ai * t) < 1e-6,
                  "cup volume = ShellClosedExtrusion volume - (inner area * t): the roof slab is the only difference");
  }
}

D8_EXTRA_TEST(ShellOpenExtrusionBoxMatchesShellConvexPlanar) {
  // A square-profile cup is a box with its top face removed: the convex-planar engine is an independent construction.
  const double a = 6.0, b = 4.0, h = 5.0, t = 0.5;
  const Poly rect = {{0, 0}, {a, 0}, {a, b}, {0, b}};
  const Brep cup = ShellOpenExtrusion(PolyCurve(rect), Vector3d(0, 0, h), t, ExtrusionOpening::Top);
  const Brep box = Brep::Box(0, 0, 0, a, b, h);
  const auto faces = box.PlanarFaces();
  int top = -1;
  for (size_t i = 0; i < faces.size(); ++i) {
    if (faces[i].plane.zaxis.z > 0.99 && faces[i].plane.origin.z > h - 1e-9) top = static_cast<int>(i);
  }
  d8test::Check(top >= 0, "fixture: found the box's top face");
  const Brep ref = ShellConvexPlanar(box, {top}, t);
  const double expected = a * b * h - (a - 2 * t) * (b - 2 * t) * (h - t);
  d8test::Check(std::fabs(ref.Volume() - expected) < 1e-9, "fixture: ShellConvexPlanar box cup has the hand closed-form volume");
  d8test::Check(std::fabs(cup.Volume() - ref.Volume()) < 1e-9, "ShellOpenExtrusion(rectangle) volume == ShellConvexPlanar(box, {top}) volume");
  d8test::Check(cup.FaceCount() == ref.FaceCount(),
                (std::string("...and the same face count (") + std::to_string(cup.FaceCount()) + " vs " + std::to_string(ref.FaceCount()) + ")").c_str());
  d8test::Check(cup.raw().IsValid() && cup.raw().IsSolid(), "rectangle cup: IsValid() and IsSolid()");
}

D8_EXTRA_TEST(ShellOpenExtrusionRefusalsAreHonest) {
  const Poly L = {{0, 0}, {6, 0}, {6, 2}, {2, 2}, {2, 6}, {0, 6}};
  const NurbsCurve lp = PolyCurve(L);
  const NurbsCurve circle = CircleCurve(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 5.0);
  std::string what;
  const Vector3d up(0, 0, 4);
  d8test::Check(!Throws([&] { ShellOpenExtrusion(lp, up, 0.5, ExtrusionOpening::Top); }), "baseline: the L cup builds");
  // Cup: the floor must leave a cavity.
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, up, 4.0, ExtrusionOpening::Top); }, &what) && what.find("height") != std::string::npos,
                "cup with t == height refused, naming the height");
  d8test::Check(Throws([&] { ShellOpenExtrusion(circle, Vector3d(0, 0, 1.0), 1.5, ExtrusionOpening::Bottom); }), "circle cup with t > height refused");
  // Circle: wall must not reach the axis.
  d8test::Check(Throws([&] { ShellOpenExtrusion(circle, Vector3d(0, 0, 10), 5.0, ExtrusionOpening::Top); }, &what) && what.find("radius") != std::string::npos,
                "circle with t == radius refused, naming the radius");
  d8test::Check(Throws([&] { ShellOpenExtrusion(circle, Vector3d(0, 0, 10), 5.5, ExtrusionOpening::Both); }), "circle tube with t > radius refused");
  // Polyline: narrow arm of the L is 2 wide, so t >= 1 collapses the arm's inward offset.
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, up, 1.0, ExtrusionOpening::Top); }), "L cup with t at the arm half-width refused");
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, up, 1.3, ExtrusionOpening::Both); }), "L tube with t beyond the arm half-width refused");
  // Argument checks.
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, up, 0.0, ExtrusionOpening::Top); }), "t == 0 refused");
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, up, -0.5, ExtrusionOpening::Top); }), "t < 0 refused");
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, up, std::nan(""), ExtrusionOpening::Top); }), "t NaN refused");
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, Vector3d(0, 0, 0), 0.5, ExtrusionOpening::Top); }), "zero direction refused");
  d8test::Check(Throws([&] { ShellOpenExtrusion(lp, Vector3d(1, 0, 4), 0.5, ExtrusionOpening::Top); }, &what) && what.find("perpendicular") != std::string::npos,
                "oblique direction refused, naming perpendicularity");
  const NurbsCurve open_line = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(5, 0, 0), Point3d(5, 5, 0)}, 1);
  d8test::Check(Throws([&] { ShellOpenExtrusion(open_line, up, 0.5, ExtrusionOpening::Top); }), "open profile refused");
  // Curved profile other than a circle: refused honestly (not a bad body).
  const double a = 5.0, b = 3.0;
  std::vector<Point3d> ell;
  for (int i = 0; i < 24; ++i) ell.emplace_back(a * std::cos(2 * ON_PI * i / 24), b * std::sin(2 * ON_PI * i / 24), 0.0);
  ell.push_back(ell.front());
  const NurbsCurve ellipse = NurbsCurve::FromControlPoints(ell, 3);
  d8test::Check(Throws([&] { ShellOpenExtrusion(ellipse, Vector3d(0, 0, 8), 0.5, ExtrusionOpening::Top); }, &what) && what.find("polyline or a circle") != std::string::npos,
                "a freeform closed profile is refused, naming the supported families");
}
