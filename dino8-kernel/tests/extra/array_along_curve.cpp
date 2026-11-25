// ArrayBrepAlongCurve / ArrayAlongCurveTransforms (pattern.h): copies of a body at arc-length-even stations of a path curve, optionally
// carried by the path's frame. Every expectation is a closed form or an independent measurement, never read off the function's output:
//   * a straight path: copy k is the body translated by k L / (n - 1) along the path (vertex by vertex),
//   * a half circle r = 5 about the origin, up = +z: the frame turns about z with the path, so copy k is the body rotated by
//     theta_k = k pi / (n - 1) about the circle's axis (vertex v -> P(theta_k) + Rz(theta_k) (v - P0), centroid P(theta_k)) - the stations
//     of a circle are equally spaced in angle, an exact oracle for the arc-length division,
//   * a space curve: the map of copy k is rigid (orthonormal, determinant +1), carries the start point onto the path point at the
//     station, carries the start tangent onto the tangent there (central differences of the path), and consecutive stations are
//     L / (n - 1) apart in arc length (L from an independent dense polyline integration),
//   * a path whose tangent is parallel to `up` still gets a frame (rigid copies),
//   * orient = false is pure translation, count < 2 and an invalid path throw std::invalid_argument.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/pattern.h"
#include "test_registry.h"

namespace {

using dino8::kernel::ArrayAlongCurveTransforms;
using dino8::kernel::ArrayBrepAlongCurve;
using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

NurbsCurve Line(const Point3d& a, const Point3d& b) { return NurbsCurve::FromControlPoints({a, b}, 1); }

NurbsCurve HalfCircle(double r) {
  const ON_Arc arc(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), r), ON_PI);
  ON_NurbsCurve nurbs;
  arc.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

double Dist(const ON_3dPoint& a, const ON_3dPoint& b) { return a.DistanceTo(b); }

// Independent arc-length table of a path: parameters and cumulative lengths of a 40001-point polyline.
struct ArcTable {
  std::vector<double> t, s;
  explicit ArcTable(const NurbsCurve& c) {
    const auto d = c.Domain();
    const int n = 40000;
    Point3d prev = c.PointAt(d.min);
    double acc = 0;
    t.push_back(d.min);
    s.push_back(0);
    for (int i = 1; i <= n; ++i) {
      const double ti = d.min + (d.max - d.min) * i / n;
      const Point3d p = c.PointAt(ti);
      acc += prev.DistanceTo(p);
      t.push_back(ti);
      s.push_back(acc);
      prev = p;
    }
  }
  double Length() const { return s.back(); }
  double ParamAtLength(double len) const {
    const size_t k = std::upper_bound(s.begin(), s.end(), len) - s.begin();
    if (k == 0) return t.front();
    if (k >= s.size()) return t.back();
    const double w = (len - s[k - 1]) / std::max(1e-300, s[k] - s[k - 1]);
    return t[k - 1] + w * (t[k] - t[k - 1]);
  }
};

bool Rigid(const ON_Xform& x, double tol) {
  const auto& m = x.m_xform;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double dot = 0;
      for (int k = 0; k < 3; ++k) dot += m[k][i] * m[k][j];
      if (std::fabs(dot - (i == j ? 1.0 : 0.0)) > tol) return false;
    }
  }
  return std::fabs(x.Determinant() - 1.0) < tol;
}

ON_3dPoint At(const Brep& b, int vertex) { return b.raw().m_V[vertex].point; }

std::string Msg(const std::string& a, int k, const char* what) { return a + ", copy " + std::to_string(k) + ": " + what; }

}  // namespace

D8_EXTRA_TEST(ArrayAlongCurveStraightPathIsPureTranslations) {
  const NurbsCurve path = Line(Point3d(0, 0, 0), Point3d(12, 0, 0));
  const Brep box = Brep::BoxWelded(-1, -1, -1, 1, 1, 1);
  for (const bool orient : {true, false}) {
    const std::vector<Brep> copies = ArrayBrepAlongCurve(box, path, 4, orient);
    d8test::Check(copies.size() == 4, "straight path: 4 copies");
    if (copies.size() != 4) continue;
    for (int k = 0; k < 4; ++k) {
      double worst = 0;
      for (int v = 0; v < box.raw().m_V.Count(); ++v) {
        const ON_3dPoint want = At(box, v) + ON_3dVector(4.0 * k, 0, 0);
        worst = std::max(worst, Dist(At(copies[k], v), want));
      }
      std::string m = Msg(orient ? "straight path, oriented" : "straight path, NoRotation", k, "every vertex is the original's + (4k, 0, 0) to 1e-6");
      d8test::Check(worst < 1e-6, m.c_str());
      const auto mp = copies[k].VolumeMassProperties();
      m = Msg("straight path", k, "volume 8 and centroid (4k, 0, 0) to 1e-6");
      d8test::Check(std::fabs(mp.volume - 8.0) < 1e-6 && Dist(ON_3dPoint(mp.centroid.x, mp.centroid.y, mp.centroid.z), ON_3dPoint(4.0 * k, 0, 0)) < 1e-6, m.c_str());
    }
  }
}

D8_EXTRA_TEST(ArrayAlongCurveHalfCircleTurnsTheCopiesWithThePath) {
  const double R = 5.0;
  const NurbsCurve path = HalfCircle(R);
  const Brep box = Brep::BoxWelded(4.5, -1, -1.5, 5.5, 1, 1.5);  // 1 x 2 x 3, centred on the path start P0 = (5, 0, 0)
  const int n = 5;
  const ON_3dPoint p0(R, 0, 0);
  const std::vector<Brep> oriented = ArrayBrepAlongCurve(box, path, n, true);
  const std::vector<Brep> plain = ArrayBrepAlongCurve(box, path, n, false);
  d8test::Check(oriented.size() == static_cast<size_t>(n) && plain.size() == static_cast<size_t>(n), "half circle: 5 copies in both modes");
  if (oriented.size() != static_cast<size_t>(n) || plain.size() != static_cast<size_t>(n)) return;
  for (int k = 0; k < n; ++k) {
    const double th = ON_PI * k / (n - 1);
    const ON_3dPoint pk(R * std::cos(th), R * std::sin(th), 0);
    double worst_o = 0, worst_p = 0;
    for (int v = 0; v < box.raw().m_V.Count(); ++v) {
      const ON_3dVector rel = At(box, v) - p0;
      const ON_3dVector rot(rel.x * std::cos(th) - rel.y * std::sin(th), rel.x * std::sin(th) + rel.y * std::cos(th), rel.z);
      worst_o = std::max(worst_o, Dist(At(oriented[k], v), pk + rot));
      worst_p = std::max(worst_p, Dist(At(plain[k], v), At(box, v) + (pk - p0)));
    }
    std::string m = Msg("half circle, oriented", k, "every vertex is P(theta) + Rz(theta)(v - P0) with theta = k pi / 4 (to 2e-4: the stations come from the arc-length division)");
    d8test::Check(worst_o < 2e-4, m.c_str());
    m = Msg("half circle, NoRotation", k, "every vertex is the original's + (P(theta) - P0) to 2e-4");
    d8test::Check(worst_p < 2e-4, m.c_str());
    const auto mp = oriented[k].VolumeMassProperties();
    m = Msg("half circle, oriented", k, "volume 6 (rigid) and centroid P(theta) to 2e-4");
    d8test::Check(std::fabs(mp.volume - 6.0) < 1e-6 && Dist(ON_3dPoint(mp.centroid.x, mp.centroid.y, mp.centroid.z), pk) < 2e-4, m.c_str());
  }
}

D8_EXTRA_TEST(ArrayAlongCurveSpaceCurveMapsAreRigidFrameCarriers) {
  const NurbsCurve path = NurbsCurve::FromControlPoints({Point3d(0, 0, 0), Point3d(4, 0, 2), Point3d(8, 6, 3), Point3d(12, 6, 8)}, 3);
  const ArcTable tab(path);
  const int n = 6;
  const std::vector<ON_Xform> maps = ArrayAlongCurveTransforms(path, n, true);
  d8test::Check(maps.size() == static_cast<size_t>(n), "space curve: 6 maps");
  if (maps.size() != static_cast<size_t>(n)) return;
  const double L = tab.Length();
  const auto tangent_at = [&](double t) {
    const double h = 1e-5 * (path.Domain().max - path.Domain().min);
    const double a = std::max(path.Domain().min, t - h), b = std::min(path.Domain().max, t + h);
    ON_3dVector d = path.PointAt(b) - path.PointAt(a);
    d.Unitize();
    return d;
  };
  const ON_3dPoint start = path.PointAt(path.Domain().min);
  const ON_3dVector t0 = tangent_at(path.Domain().min);
  double prev_len = 0;
  for (int k = 0; k < n; ++k) {
    const double target = L * k / (n - 1);
    const double tk = tab.ParamAtLength(target);
    std::string m = Msg("space curve", k, "the map is rigid (orthonormal linear part, determinant +1)");
    d8test::Check(Rigid(maps[k], 1e-9), m.c_str());
    const ON_3dPoint img = maps[k] * start;
    m = Msg("space curve", k, "the start point is carried onto the path point at arc length k L / 5 (to 1e-3)");
    d8test::Check(Dist(img, path.PointAt(tk)) < 1e-3, m.c_str());
    ON_3dVector t_img(maps[k].m_xform[0][0] * t0.x + maps[k].m_xform[0][1] * t0.y + maps[k].m_xform[0][2] * t0.z,
                      maps[k].m_xform[1][0] * t0.x + maps[k].m_xform[1][1] * t0.y + maps[k].m_xform[1][2] * t0.z,
                      maps[k].m_xform[2][0] * t0.x + maps[k].m_xform[2][1] * t0.y + maps[k].m_xform[2][2] * t0.z);
    const ON_3dVector want = tangent_at(tk);
    m = Msg("space curve", k, "the start tangent is carried onto the path tangent there (central differences, to 1e-3)");
    d8test::Check((t_img - want).Length() < 1e-3, m.c_str());
    if (k > 0) {
      m = Msg("space curve", k, "consecutive stations are L / 5 apart in arc length (independent polyline integration, to 1e-3 relative)");
      // arc length between the images of the start point at stations k-1 and k, measured along the path
      d8test::Check(std::fabs((target - prev_len) - L / (n - 1)) < 1e-3 * L && Dist(maps[k] * start, path.PointAt(tk)) < 1e-3, m.c_str());
    }
    prev_len = target;
  }
}

D8_EXTRA_TEST(ArrayAlongCurveVerticalPathStillGetsAFrameAndRefusals) {
  const NurbsCurve vertical = Line(Point3d(0, 0, 0), Point3d(0, 0, 9));
  const std::vector<ON_Xform> maps = ArrayAlongCurveTransforms(vertical, 4, true, Vector3d(0, 0, 1));
  d8test::Check(maps.size() == 4, "vertical path (tangent parallel to up): 4 maps");
  if (maps.size() == 4) {
    bool all_rigid = true, on_path = true;
    for (int k = 0; k < 4; ++k) {
      all_rigid = all_rigid && Rigid(maps[k], 1e-9);
      on_path = on_path && Dist(maps[k] * ON_3dPoint(0, 0, 0), ON_3dPoint(0, 0, 3.0 * k)) < 1e-6;
    }
    d8test::Check(all_rigid, "vertical path: every map is rigid (the fallback frame is orthonormal)");
    d8test::Check(on_path, "vertical path: the start point lands on (0, 0, 3k)");
  }
  bool too_few = false, one = false, bad_path = false;
  try { (void)ArrayAlongCurveTransforms(vertical, 1, true); } catch (const std::invalid_argument&) { too_few = true; } catch (const std::exception&) {}
  try { (void)ArrayAlongCurveTransforms(vertical, 0, true); } catch (const std::invalid_argument&) { one = true; } catch (const std::exception&) {}
  try { (void)ArrayAlongCurveTransforms(NurbsCurve(), 3, true); } catch (const std::invalid_argument&) { bad_path = true; } catch (const std::exception&) {}
  d8test::Check(too_few && one, "count 1 and count 0 throw std::invalid_argument");
  d8test::Check(bad_path, "an invalid (empty) path throws std::invalid_argument");
}
