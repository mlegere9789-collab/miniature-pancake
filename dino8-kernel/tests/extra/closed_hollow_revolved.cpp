// ShellClosedRevolvedArcProfile(): CLOSED hollow of a solid of revolution with a thickness of its OWN for every face (the PARITY_MAP
// "Per-face (multi-thickness) shell" gap for curved bodies: ShellRevolvedArcProfile refuses a shell with nothing removed because the
// closed form is two lumps; this builds those two lumps - the original and the per-face inward offset reversed - as one Brep).
//
// Oracles are independent of the construction (edge-line / circle intersections + Green's integral):
//  - cylinder, floor 2 / wall 0.5 / top 1: pi (R^2 H - (R - tw)^2 (H - tb - tt));
//  - dome (sphere radius 5 centred z = -2, base z = 0), base 0.5 / sphere 1: cap(5, 3) - cap(4, 1.5), cap(rho, h) = pi h^2 (3 rho - h) / 3;
//  - stepped shaft (r5 z 0..4 + r3 z 4..8) with a 1.0 lower wall: cavity r4 z .5..3.5 + r2.5 z 3.5..7.5, shell 136 pi - 73 pi = 63 pi;
//  - half torus ring, arc 0.5 / chord 0.3: Pappus 2 pi R (area of the half disc - area of the cavity segment);
//  - a uniform thickness equals ShellClosedExtrusion() of the same cylinder (an independent construction);
//  - tessellation point membership of the compound against the same closed form.
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MeridianEdge;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::ShellClosedRevolvedArcProfile;
using dino8::kernel::Vector3d;

MeridianEdge L(double r, double z) {
  MeridianEdge e;
  e.r = r;
  e.z = z;
  return e;
}
MeridianEdge A(double r, double z, double cr, double cz, bool ccw) {
  MeridianEdge e;
  e.r = r;
  e.z = z;
  e.arc = true;
  e.cr = cr;
  e.cz = cz;
  e.counter_clockwise = ccw;
  return e;
}

Brep Hollow(const std::vector<MeridianEdge>& e, const std::vector<double>& th, std::string* what = nullptr, bool* threw = nullptr) {
  Brep b;
  try {
    b = ShellClosedRevolvedArcProfile(e, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), th);
    if (threw) *threw = false;
  } catch (const std::exception& ex) {
    if (what) *what = ex.what();
    if (threw) *threw = true;
  }
  return b;
}

double Cap(double rho, double h) { return ON_PI * h * h * (3.0 * rho - h) / 3.0; }

// 1 inside [lo, hi], 0 outside, -1 within m of a bound
int Band(double q, double lo, double hi, double m) {
  if (std::fabs(q - lo) < m || std::fabs(q - hi) < m) return -1;
  return q > lo && q < hi ? 1 : 0;
}

void CheckHollow(const std::string& tag, const std::vector<MeridianEdge>& e, const std::vector<double>& th, double expected,
                 const std::function<int(double, double, double)>& material, double box) {
  std::string what;
  bool threw = false;
  const Brep b = Hollow(e, th, &what, &threw);
  d8test::Check(!threw, (tag + ": builds (" + what + ")").c_str());
  if (threw) return;
  d8test::Check(b.LumpFaceRanges().size() == 2, (tag + ": two closed lumps (the outer shell and the reversed cavity)").c_str());
  const double vol = b.Volume();
  d8test::Check(std::fabs(vol - expected) <= 1e-5 * expected,
                (tag + ": Brep::Volume() " + std::to_string(vol) + " = independent closed form " + std::to_string(expected)).c_str());
  const Mesh mesh = b.TessellateToClosedMesh(64, 96);
  d8test::Check(mesh.Volume() < expected * 1.0005 && mesh.Volume() > 0.985 * expected, (tag + ": tessellated volume within 1.5% of the closed form").c_str());
  int judged = 0, bad = 0;
  const int n = 30;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      for (int k = 0; k < n; ++k) {
        const double x = -box + 2 * box * (i + 0.37) / n, y = -box + 2 * box * (j + 0.41) / n, z = -box + 2 * box * (k + 0.29) / n;
        const int want = material(x, y, z);
        if (want < 0) continue;
        ++judged;
        if (mesh.ContainsPoint(Point3d(x, y, z)) != (want == 1)) ++bad;
      }
    }
  }
  d8test::Check(judged > 3000 && bad == 0, (tag + ": point membership agrees with the closed form at " + std::to_string(judged) + " points, mismatches " + std::to_string(bad)).c_str());
}

}  // namespace

D8_EXTRA_TEST(ClosedHollowRevolvedCylinderAndDomePerFace) {
  {  // cylinder R5 H10: floor 2, wall 0.5, top 1
    const double R = 5, H = 10, tf = 2.0, tw = 0.5, tt = 1.0;
    CheckHollow("cylinder floor 2 / wall 0.5 / top 1", {L(0, 0), L(R, 0), L(R, H), L(0, H)}, {tf, tw, tt, 0.0},
                ON_PI * (R * R * H - (R - tw) * (R - tw) * (H - tf - tt)),
                [&](double x, double y, double z) {
                  const double r = std::hypot(x, y);
                  const double m = 0.12;
                  if (std::fabs(z) < m || std::fabs(z - H) < m || std::fabs(z - tf) < m || std::fabs(z - (H - tt)) < m || std::fabs(r - R) < m || std::fabs(r - (R - tw)) < m) return -1;
                  if (z < 0 || z > H || r > R) return 0;
                  return (r < R - tw && z > tf && z < H - tt) ? 0 : 1;
                },
                7.0);
  }
  {  // dome: sphere radius 5 centred z=-2, flat base z=0: base 0.5, sphere 1.0
    const double cz = -2, R = 5, tb = 0.5, ts = 1.0, w = std::sqrt(R * R - cz * cz);
    const double expected = Cap(R, R + cz) - Cap(R - ts, R - ts + cz - tb);
    CheckHollow("dome base 0.5 / sphere 1.0", {L(0, 0), A(w, 0, 0, cz, true), L(0, cz + R)}, {tb, ts, 0.0}, expected,
                [&](double x, double y, double z) {
                  const double q = std::sqrt(x * x + y * y + (z - cz) * (z - cz));
                  if (std::fabs(z) < 0.12 || std::fabs(z - tb) < 0.12 || std::fabs(q - R) < 0.14 || std::fabs(q - (R - ts)) < 0.14) return -1;
                  if (z < 0 || q > R) return 0;
                  return (z > tb && q < R - ts) ? 0 : 1;
                },
                7.0);
  }
}

D8_EXTRA_TEST(ClosedHollowRevolvedSteppedShaftTorusAndCrossEngine) {
  {  // stepped shaft: bottom 0.5, lower wall 1.0, annulus 0.5, upper wall 0.5, top 0.5
    CheckHollow("stepped shaft with a 1.0 lower wall", {L(0, 0), L(5, 0), L(5, 4), L(3, 4), L(3, 8), L(0, 8)}, {0.5, 1.0, 0.5, 0.5, 0.5, 0.0},
                136.0 * ON_PI - ON_PI * (16.0 * 3.0 + 6.25 * 4.0),
                [&](double x, double y, double z) {
                  const double r = std::hypot(x, y);
                  const double m = 0.12;
                  for (double zz : {0.0, 0.5, 3.5, 4.0, 7.5, 8.0}) if (std::fabs(z - zz) < m) return -1;
                  for (double rr : {5.0, 4.0, 3.0, 2.5}) if (std::fabs(r - rr) < m) return -1;
                  const double rout = z < 4.0 ? 5.0 : 3.0;
                  if (z < 0 || z > 8 || r > rout) return 0;
                  const bool cav = (z > 0.5 && z < 3.5 && r < 4.0) || (z >= 3.5 && z < 7.5 && r < 2.5);
                  return cav ? 0 : 1;
                },
                7.0);
  }
  {  // half torus ring section (no edge on the axis): arc 0.5, chord 0.3
    const double cr = 4, a = 2, ta = 0.5, tc = 0.3, rho = a - ta;
    const double seg = rho * rho * std::acos(tc / rho) - tc * std::sqrt(rho * rho - tc * tc);  // cavity segment above the chord
    const double expected = 2.0 * ON_PI * cr * (ON_PI * a * a / 2.0 - seg);
    CheckHollow("half torus, arc 0.5 / chord 0.3", {A(cr + a, 0, cr, 0, true), L(cr - a, 0)}, {ta, tc}, expected,
                [&](double x, double y, double z) {
                  const double rr = std::hypot(x, y);
                  const double q = std::sqrt((rr - cr) * (rr - cr) + z * z);
                  if (std::fabs(z) < 0.12 || std::fabs(z - tc) < 0.12 || std::fabs(q - a) < 0.14 || std::fabs(q - rho) < 0.14) return -1;
                  if (z < 0 || q > a) return 0;
                  return (z > tc && q < rho) ? 0 : 1;
                },
                7.0);
  }
  {  // uniform thickness: equals ShellClosedExtrusion of the same cylinder (an independent construction)
    const double R = 5, H = 10, t = 1.0;
    const Brep mine = Hollow({L(0, 0), L(R, 0), L(R, H), L(0, H)}, {t, t, t, 0.0});
    const ON_Circle c(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), R);
    ON_NurbsCurve nc;
    c.GetNurbForm(nc);
    NurbsCurve circle;
    circle.raw() = nc;
    const Brep ref = dino8::kernel::ShellClosedExtrusion(circle, Vector3d(0, 0, H), t);
    d8test::Check(std::fabs(mine.Volume() - ref.Volume()) <= 1e-5 * ref.Volume(), "uniform t=1 cylinder: volume equals ShellClosedExtrusion()'s");
  }
}

D8_EXTRA_TEST(ClosedHollowRevolvedRefusalsAreHonest) {
  const std::vector<MeridianEdge> can = {L(0, 0), L(5, 0), L(5, 10), L(0, 10)};
  std::string what;
  bool threw = false;
  Hollow(can, {1, 1, 1, 0}, &what, &threw);
  d8test::Check(!threw, "baseline: the uniform closed hollow builds");
  Hollow(can, {1, 0, 1, 0}, &what, &threw);
  d8test::Check(threw && what.find("positive") != std::string::npos, "a zero thickness on a real face is refused, naming positivity (a removed face is ShellRevolvedArcProfile's job)");
  Hollow(can, {1, 1, 1, 0.5}, &what, &threw);
  d8test::Check(threw && what.find("axis") != std::string::npos, "a thickness on the axis edge is refused, naming the axis");
  Hollow(can, {1, 1, 1}, &what, &threw);
  d8test::Check(threw, "wrong-sized thickness vector refused");
  Hollow(can, {-1, 1, 1, 0}, &what, &threw);
  d8test::Check(threw, "negative thickness refused");
  Hollow(can, {1, std::nan(""), 1, 0}, &what, &threw);
  d8test::Check(threw, "NaN thickness refused");
  Hollow(can, {1, 5.0, 1, 0}, &what, &threw);
  d8test::Check(threw, "wall as thick as the radius: the cavity crosses the axis - refused");
  Hollow(can, {5, 1, 5, 0}, &what, &threw);
  d8test::Check(threw, "floor + top as thick as the whole height: no cavity left - refused");
  Hollow(can, {5, 1, 4.9, 0}, &what, &threw);
  d8test::Check(!threw, "baseline: floor 5 + top 4.9 leaves a 0.1 cavity and builds");
  // The shell-with-removal API still refuses a closed hollow, which is why this entry point exists.
  bool refused = false;
  try {
    dino8::kernel::ShellRevolvedArcProfile(can, Point3d(0, 0, 0), Vector3d(0, 0, 1), Vector3d(1, 0, 0), {1, 1, 1, 0});
  } catch (const std::invalid_argument& ex) {
    refused = std::string(ex.what()).find("removed") != std::string::npos;
  }
  d8test::Check(refused, "ShellRevolvedArcProfile with nothing removed refuses (names the removed face) - the closed hollow needs this entry point");
}
