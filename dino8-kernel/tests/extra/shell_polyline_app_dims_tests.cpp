// ShellPolylineExtrusion on the exact L the app's OSE_ smoke section shells (an extruded L: (0,0) (20,0) (20,10) (10,10)
// (10,20) (0,20), 8 high, profile area 300), where the closed-form volumes are hand-derived:
//   cavity of the L with every kept wall moved in by 1 = 224 (window: wall y=0 left on its own plane: 242),
//   cup (top open, floor 1)            2400 - 224*7 = 832
//   window (wall y=0 + top open)       2400 - 242*7 = 706
//   top cap 3 thick, the rest 1        2400 - 224*4 = 1504
//   closed, all 1                      2400 - 224*6 = 1056
// Before Brep::IsWatertightForIntegration() retried a finer weld, every one of these was refused with "Brep::Volume: this
// Brep is not a closed, watertight solid": the coarse (8, 8) closed-mesh weld of this very L leaves 100 open edges (it is closed
// at (24, 24)), and the shell verifies its own volume. The 6 x 6 L of the older tests never hit it.
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using d8test::Check;

NurbsCurve LProfile() {
  std::vector<Point3d> p = {Point3d(0, 0, 0), Point3d(20, 0, 0), Point3d(20, 10, 0), Point3d(10, 10, 0), Point3d(10, 20, 0), Point3d(0, 20, 0)};
  p.push_back(p.front());
  return NurbsCurve::FromControlPoints(p, 1);
}

// Divergence-theorem volume over the planar faces' own loops: independent of any tessellation.
double LoopVolume(const Brep& b) {
  double v = 0.0;
  for (const auto& f : b.PlanarFaces()) {
    const auto& l = f.loop;
    for (size_t i = 1; i + 1 < l.size(); ++i) {
      const Point3d &a = l[0], &p = l[i], &c = l[i + 1];
      v += (a.x * (p.y * c.z - p.z * c.y) - a.y * (p.x * c.z - p.z * c.x) + a.z * (p.x * c.y - p.y * c.x)) / 6.0;
    }
  }
  return v;
}

Brep BarePrism(double w) {
  const double a = 20.0, H = 8.0;
  const std::vector<Point3d> P = {Point3d(0, 0, 0), Point3d(a, 0, 0), Point3d(a, w, 0), Point3d(w, w, 0), Point3d(w, a, 0), Point3d(0, a, 0)};
  std::vector<Brep::PlanarFace> fs;
  auto add = [&](std::vector<Point3d> loop, ON_3dVector n) {
    Brep::PlanarFace f;
    f.loop = loop;
    f.plane = ON_Plane(loop[0], n);
    fs.push_back(f);
  };
  add(std::vector<Point3d>(P.rbegin(), P.rend()), ON_3dVector(0, 0, -1));
  std::vector<Point3d> top = P;
  for (auto& p : top) p.z = H;
  add(top, ON_3dVector(0, 0, 1));
  for (size_t k = 0; k < P.size(); ++k) {
    const Point3d p = P[k], q = P[(k + 1) % P.size()];
    ON_3dVector n(q.y - p.y, -(q.x - p.x), 0);
    n.Unitize();
    add({p, q, Point3d(q.x, q.y, H), Point3d(p.x, p.y, H)}, n);
  }
  return Brep::FromPlanarFaces(fs);
}

bool Refuses(const std::function<void()>& f, const char* needle) {
  try {
    f();
  } catch (const std::invalid_argument& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  } catch (...) {
  }
  return false;
}

void CheckShell(const char* what, const std::vector<double>& walls, double bottom, double top, double expect) {
  const std::string w = what;
  try {
    const Brep s = dino8::kernel::ShellPolylineExtrusion(LProfile(), Vector3d(0, 0, 8), walls, bottom, top);
    Check(s.raw().IsValid() && s.raw().IsSolid(), (w + ": valid closed ON_Brep").c_str());
    Check(std::fabs(LoopVolume(s) - expect) < 1e-9, (w + ": independent face-loop volume equals the hand-derived " + std::to_string(expect)).c_str());
    Check(std::fabs(s.Volume() - expect) < 1e-7 * expect, (w + ": Brep::Volume() equals it too").c_str());
  } catch (const std::exception& e) {
    Check(false, (w + ": threw " + e.what()).c_str());
  }
}

}  // namespace

D8_EXTRA_TEST(ShellPolylineAppDimsBarePrismVolumeIsWellDefined) {
  // The cause, isolated from the shell: the flat-faced L prism itself.
  for (double w : {9.0, 10.0, 11.0}) {
    const Brep p = BarePrism(w);
    const double area = 20.0 * w + (20.0 - w) * w;  // two arms of width w: 20*w + (20-w)*w
    const double expect = area * 8.0;
    Check(p.raw().IsValid() && p.raw().IsSolid(), ("L prism, arm width " + std::to_string(w) + ": a valid closed ON_Brep").c_str());
    Check(std::fabs(LoopVolume(p) - expect) < 1e-9, ("L prism, arm width " + std::to_string(w) + ": face-loop volume equals area*8").c_str());
    bool ok = false;
    double v = 0.0;
    try { v = p.Volume(); ok = true; } catch (...) {}
    Check(ok && std::fabs(v - expect) < 1e-7 * expect, ("L prism, arm width " + std::to_string(w) + ": Brep::Volume() is defined and equals area*8 = " + std::to_string(expect)).c_str());
  }
}

D8_EXTRA_TEST(ShellPolylineAppDimsClosedFormVolumes) {
  CheckShell("L 20/10 closed, all walls and caps 1", std::vector<double>(6, 1.0), 1.0, 1.0, 2400.0 - 224.0 * 6.0);
  CheckShell("L 20/10 cup (top open), floor 1", std::vector<double>(6, 1.0), 1.0, 0.0, 2400.0 - 224.0 * 7.0);
  CheckShell("L 20/10 window (wall y=0 and the top open)", {0.0, 1.0, 1.0, 1.0, 1.0, 1.0}, 1.0, 0.0, 2400.0 - 242.0 * 7.0);
  CheckShell("L 20/10 top cap 3 thick, the rest 1", std::vector<double>(6, 1.0), 1.0, 3.0, 2400.0 - 224.0 * 4.0);
}

D8_EXTRA_TEST(ShellPolylineAppDimsReflexWallRemovalIsRefusedNamingTheWall) {
  Check(Refuses([] { dino8::kernel::ShellPolylineExtrusion(LProfile(), Vector3d(0, 0, 8), {1.0, 1.0, 0.0, 1.0, 1.0, 1.0}, 1.0, 1.0); },
                "removed wall 2: the cavity does not end inside"),
        "removing wall 2 of the 20/10 L (y=10, x 10..20, which ends at the reflex corner) is refused, naming wall 2");
}
