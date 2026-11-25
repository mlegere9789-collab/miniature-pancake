// ShellRevolvedProfile(): shell WITH face removal (and per-face thickness) of a solid of revolution whose meridian
// section is a closed polyline (cylinder, cone / frustum, stacks of them).
//
// Oracles are independent of the construction (which uses edge-line miter intersections and Green's integral):
//  - hand-derived closed-form volumes: cylinder cups pi(R^2 H - (R-t)^2 (H-t)); a cone cup as an outer frustum
//    pi h/3 (R0^2+R0 R1+R1^2) minus the cavity frustum whose radii come from the explicit normal-offset line
//    r(z) = R0 - t H/L + (z - t a/L) a/H; a ring section; and a slanted-rim can by Pappus on a triangle;
//  - point membership of the TESSELLATION (Mesh::ContainsPoint) against point-in-polygon of the hand-written meridian
//    section in (r, z);
//  - cross-engine: the cylinder cup equals ShellOpenExtrusion() of the circle.
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
using dino8::kernel::ShellOpenExtrusion;
using dino8::kernel::ShellRevolvedProfile;
using dino8::kernel::Vector3d;
using RZ = std::vector<std::pair<double, double>>;

// Meridian section in the XZ plane (r along +x, axis = +z) through `origin`, optionally in a frame (ex, axis).
NurbsCurve Section(const RZ& rz, const ON_3dPoint& origin = ON_3dPoint(0, 0, 0), ON_3dVector axis = ON_3dVector(0, 0, 1),
                   ON_3dVector ex = ON_3dVector(1, 0, 0)) {
  axis.Unitize();
  ex.Unitize();
  std::vector<Point3d> pts;
  for (const auto& q : rz) {
    const ON_3dPoint p = origin + q.first * ex + q.second * axis;
    pts.emplace_back(p.x, p.y, p.z);
  }
  pts.push_back(pts.front());
  return NurbsCurve::FromControlPoints(pts, 1);
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

bool PointInPoly(const RZ& p, double x, double y) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    if ((p[i].second > y) != (p[j].second > y) &&
        x < p[i].first + (y - p[i].second) * (p[j].first - p[i].first) / (p[j].second - p[i].second)) {
      in = !in;
    }
  }
  return in;
}

double DistToPoly(const RZ& p, double x, double y) {
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

// Build the shell, check validity, the closed-form volume, the tessellation volume and point membership.
void CheckShell(const std::string& tag, const RZ& outer, const std::vector<double>& th, const RZ& material, double expected_volume) {
  Brep shell;
  std::string what;
  const bool threw = Throws([&] { shell = ShellRevolvedProfile(Section(outer), Point3d(0, 0, 0), Vector3d(0, 0, 1), th); }, &what);
  d8test::Check(!threw, (tag + ": builds (" + what + ")").c_str());
  if (threw) return;
  d8test::Check(shell.raw().IsValid() && shell.raw().IsSolid(), (tag + ": IsValid() and IsSolid()").c_str());
  const double vol = shell.Volume();
  d8test::Check(std::fabs(vol - expected_volume) <= 1e-6 * expected_volume,
                (tag + ": exact Brep::Volume() " + std::to_string(vol) + " = hand closed form " + std::to_string(expected_volume)).c_str());
  const Mesh mesh = shell.TessellateToClosedMesh(64, 96);
  const double mv = mesh.Volume();
  d8test::Check(mv < expected_volume * 1.0001 && mv > 0.99 * expected_volume,
                (tag + ": tessellated closed-mesh volume within 1% below the closed form (" + std::to_string(mv) + ")").c_str());
  double rmax = 0, zmax = -1e9, zmin = 1e9;
  for (const auto& q : outer) {
    rmax = std::max(rmax, q.first);
    zmax = std::max(zmax, q.second);
    zmin = std::min(zmin, q.second);
  }
  int judged = 0, bad = 0;
  const int n = 30;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      for (int k = 0; k < n; ++k) {
        const double x = -rmax - 1 + (2 * rmax + 2) * (i + 0.37) / n;
        const double y = -rmax - 1 + (2 * rmax + 2) * (j + 0.41) / n;
        const double z = zmin - 1 + (zmax - zmin + 2) * (k + 0.29) / n;
        const double r = std::hypot(x, y);
        if (DistToPoly(material, r, z) < 0.12) continue;
        ++judged;
        const bool want = PointInPoly(material, r, z);
        if (mesh.ContainsPoint(Point3d(x, y, z)) != want) ++bad;
      }
    }
  }
  d8test::Check(judged > 4000 && bad == 0,
                (tag + ": point membership agrees with the meridian polygon at " + std::to_string(judged) + " points, mismatches " + std::to_string(bad)).c_str());
}

}  // namespace

D8_EXTRA_TEST(ShellRevolvedProfileCylinderCupAndVariableFloor) {
  const double R = 5, H = 10;
  const RZ can = {{0, 0}, {R, 0}, {R, H}, {0, H}};  // e0 floor, e1 wall, e2 top, e3 axis
  {
    const double t = 1.0;
    CheckShell("cylinder cup t=1", can, {t, t, 0, 0}, {{0, 0}, {R, 0}, {R, H}, {R - t, H}, {R - t, t}, {0, t}},
               ON_PI * (R * R * H - (R - t) * (R - t) * (H - t)));
    // Cross-engine: the circle-profile ShellOpenExtrusion is an independent construction of the same body.
    const ON_Circle c(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), R);
    ON_NurbsCurve nc;
    c.GetNurbForm(nc);
    NurbsCurve circle;
    circle.raw() = nc;
    const Brep ref = ShellOpenExtrusion(circle, Vector3d(0, 0, H), t, ExtrusionOpening::Top);
    const Brep mine = ShellRevolvedProfile(Section(can), Point3d(0, 0, 0), Vector3d(0, 0, 1), {t, t, 0, 0});
    d8test::Check(std::fabs(ref.Volume() - mine.Volume()) < 1e-6 * ref.Volume(), "cylinder cup equals ShellOpenExtrusion(circle, Top) in volume");
  }
  {  // variable thickness: floor 2, wall 0.5
    const double tf = 2.0, tw = 0.5;
    CheckShell("cylinder cup floor 2 / wall 0.5", can, {tf, tw, 0, 0}, {{0, 0}, {R, 0}, {R, H}, {R - tw, H}, {R - tw, tf}, {0, tf}},
               ON_PI * (R * R * H - (R - tw) * (R - tw) * (H - tf)));
  }
  {  // both ends removed: a tube (single zero run e2,e3,e0)
    const double t = 1.0;
    CheckShell("cylinder tube", can, {0, t, 0, 0}, {{R, 0}, {R, H}, {R - t, H}, {R - t, 0}}, ON_PI * H * (R * R - (R - t) * (R - t)));
  }
  {  // reversed vertex order gives the same body
    const double t = 1.0;
    const RZ rev = {{0, H}, {R, H}, {R, 0}, {0, 0}};  // clockwise: e0 top, e1 wall, e2 floor, e3 axis
    const Brep a = ShellRevolvedProfile(Section(rev), Point3d(0, 0, 0), Vector3d(0, 0, 1), {0, t, t, 0});
    d8test::Check(a.raw().IsSolid() && std::fabs(a.Volume() - ON_PI * (R * R * H - (R - t) * (R - t) * (H - t))) < 1e-6 * a.Volume(),
                  "clockwise section (top edge first) builds the same cup");
  }
}

D8_EXTRA_TEST(ShellRevolvedProfileConeAndSlantedRim) {
  {  // frustum cup: normal-thickness wall on a cone
    const double R0 = 6, R1 = 3, H = 8, tb = 1.0, tw = 0.5;
    const double a = R1 - R0, L = std::hypot(a, H);
    auto offset_r = [&](double z) { return R0 - tw * H / L + (z - tw * a / L) * a / H; };
    const double r_lo = offset_r(tb), r_hi = offset_r(H);
    const double outer = ON_PI * H / 3.0 * (R0 * R0 + R0 * R1 + R1 * R1);
    const double cav = ON_PI * (H - tb) / 3.0 * (r_lo * r_lo + r_lo * r_hi + r_hi * r_hi);
    CheckShell("frustum cup (cone wall, normal thickness 0.5)", {{0, 0}, {R0, 0}, {R1, H}, {0, H}}, {tb, tw, 0, 0},
               {{0, 0}, {R0, 0}, {R1, H}, {r_hi, H}, {r_lo, tb}, {0, tb}}, outer - cav);
  }
  {  // can whose rim is cut by a removed CONICAL face: removed run e2 (cone) + e3 (top) + e4 (axis)
    const double R = 5, t = 0.5;
    const double expected = ON_PI * (R * R * t + (R * R - (R - t) * (R - t)) * (4 - t)) + 2.0 * ON_PI * t * t * (R - 2.0 * t / 3.0);
    CheckShell("cylinder with removed cone face: slanted rim", {{0, 0}, {R, 0}, {R, 4}, {3, 8}, {0, 8}}, {t, t, 0, 0, 0},
               {{0, 0}, {R, 0}, {R, 4}, {R - t, 4 + 2 * t}, {R - t, t}, {0, t}}, expected);
  }
  {  // ring section (no axis edge): a hollow ring with its top face removed
    const double r0 = 3, r1 = 6, h = 4, t = 0.5;
    const double expected = ON_PI * ((r1 * r1 - r0 * r0) * h - ((r1 - t) * (r1 - t) - (r0 + t) * (r0 + t)) * (h - t));
    CheckShell("ring section, top removed", {{r0, 0}, {r1, 0}, {r1, h}, {r0, h}}, {t, t, 0, t},
               {{r0, 0}, {r1, 0}, {r1, h}, {r1 - t, h}, {r1 - t, t}, {r0 + t, t}, {r0 + t, h}, {r0, h}}, expected);
  }
}

D8_EXTRA_TEST(ShellRevolvedProfileTiltedOffOriginAxis) {
  // Axis (1,2,2)/3 through (3,-2,4); section built in the frame (ex, axis): same volume as the upright cup.
  ON_3dVector axis(1, 2, 2);
  axis.Unitize();
  ON_3dVector ex = ON_CrossProduct(axis, ON_3dVector(0, 0, 1));
  ex.Unitize();
  const ON_3dPoint o(3, -2, 4);
  const double R = 3, H = 7, t = 0.6;
  const NurbsCurve sec = Section({{0, 0}, {R, 0}, {R, H}, {0, H}}, o, axis, ex);
  const Brep b = ShellRevolvedProfile(sec, Point3d(o.x, o.y, o.z), Vector3d(axis.x, axis.y, axis.z), {t, t, 0, 0});
  const double expected = ON_PI * (R * R * H - (R - t) * (R - t) * (H - t));
  d8test::Check(b.raw().IsValid() && b.raw().IsSolid() && std::fabs(b.Volume() - expected) < 1e-6 * expected,
                "tilted off-origin axis: valid solid, closed-form volume");
  const Mesh mesh = b.TessellateToClosedMesh(64, 96);
  const ON_3dPoint inside_floor = o + 0.3 * axis, in_cavity = o + 3.0 * axis, in_wall = o + 3.0 * axis + (R - 0.3) * ex;
  d8test::Check(mesh.ContainsPoint(Point3d(inside_floor.x, inside_floor.y, inside_floor.z)) &&
                    !mesh.ContainsPoint(Point3d(in_cavity.x, in_cavity.y, in_cavity.z)) &&
                    mesh.ContainsPoint(Point3d(in_wall.x, in_wall.y, in_wall.z)),
                "tilted axis: floor and wall are material, the cavity above the floor is empty");
}

D8_EXTRA_TEST(ShellRevolvedProfileRefusalsAreHonest) {
  const double R = 5, H = 10;
  const RZ can = {{0, 0}, {R, 0}, {R, H}, {0, H}};
  const Point3d o(0, 0, 0);
  const Vector3d z(0, 0, 1);
  std::string what;
  d8test::Check(!Throws([&] { ShellRevolvedProfile(Section(can), o, z, {1, 1, 0, 0}); }), "baseline: the cup builds");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {1, 1, 1, 0}); }, &what) && what.find("removed") != std::string::npos,
                "no removed face: refused, saying a face must be removed");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {1, 1, 0, 0.5}); }, &what) && what.find("axis") != std::string::npos,
                "positive thickness on the axis edge: refused, naming the axis");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {1, 1, 0}); }), "wrong-sized thickness vector refused");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {-1, 1, 0, 0}); }), "negative thickness refused");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {1, std::nan(""), 0, 0}); }), "NaN thickness refused");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {0, 0, 0, 0}); }), "all-zero thickness refused");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {1, 5.5, 0, 0}); }, &what),
                "wall thicker than the radius: the cavity would cross the axis - refused");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(can), o, z, {10, 1, 0, 0}); }), "floor as thick as the whole height: refused");
  // Two separate removed runs on a ring section (no axis edge): the material would split into two lumps.
  const RZ ring = {{3, 0}, {6, 0}, {6, 4}, {3, 4}};
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(ring), o, z, {0, 0.5, 0, 0.5}); }, &what) && what.find("run") != std::string::npos,
                "two separate removed faces: refused, naming the separate runs");
  // A 1-wide ring wall with 0.6 on both sides: the offset walls cross.
  const RZ thin = {{3, 0}, {4, 0}, {4, 4}, {3, 4}};
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section(thin), o, z, {0.6, 0.6, 0, 0.6}); }), "wall pair thicker than the section is wide: refused");
  d8test::Check(!Throws([&] { ShellRevolvedProfile(Section(thin), o, z, {0.3, 0.3, 0, 0.3}); }), "the same section at 0.3 per side builds");
  // Axis not in the section's plane / section straddling the axis / open / curved profile.
  const NurbsCurve off_plane = Section(can, ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
  d8test::Check(Throws([&] { ShellRevolvedProfile(off_plane, o, Vector3d(0, 1, 0), {1, 1, 0, 0}); }, &what) && what.find("axis") != std::string::npos,
                "axis not in the profile's plane: refused");
  d8test::Check(Throws([&] { ShellRevolvedProfile(Section({{-2, 0}, {3, 0}, {3, 5}, {-2, 5}}), o, z, {1, 1, 0, 1}); }, &what) && what.find("side") != std::string::npos,
                "section straddling the axis: refused");
  const NurbsCurve open_line = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(5, 0, 0), Point3d(5, 0, 5)}, 1);
  d8test::Check(Throws([&] { ShellRevolvedProfile(open_line, o, z, {1, 1, 0}); }), "open profile refused");
  std::vector<Point3d> arc;
  for (int i = 0; i < 24; ++i) arc.emplace_back(2.0 + std::cos(2 * ON_PI * i / 24), 0.0, 3.0 + std::sin(2 * ON_PI * i / 24));
  arc.push_back(arc.front());
  d8test::Check(Throws([&] { ShellRevolvedProfile(NurbsCurve::FromControlPoints(arc, 3), o, z, std::vector<double>(24, 0.1)); }, &what) && what.find("polyline") != std::string::npos,
                "curved (cubic) profile refused, naming the polyline requirement");
}
