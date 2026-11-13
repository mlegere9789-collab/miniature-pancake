// QC: OffsetSolidPlanar (offset_solid.h) on a solid FAR FROM THE ORIGIN, and of tiny size.
//
// Found by attacking the routine with translated copies of its own inputs. Everything it does - the divergence-theorem volumes, the
// "inward-oriented" test, the global overlap check on a FLOAT32 tessellation - ran at the solid's own coordinates, so:
//   * a 1 x 1 x 1 cube at x = 1e5 reported volume_after 1.72030719 for the exact 1.728 (0.45 percent off),
//   * at 1e6 it was refused as "the solid is inward-oriented or degenerate: signed volume -4.08622",
//   * at 1e7 it reported volume_before 4558.28 and volume_after -675.367,
//   * a cube rotated about an axis and moved 1e5 away was refused "the offset solid self-intersects: 167 crossing triangle pair(s)",
//   * a cube shrunk to an edge of 2e-5 reported 7.989e-15 for the exact 8e-15 (the fan from the origin cancels 1e-17 terms).
// Oracles, none of them the routine's own construction:
//   * closed forms: cube (L + 2d)^3; the translation invariance of the result (the same solid moved is the same offset moved);
//   * a rectilinear polygon prism has the mitre-offset area A + P d + 4 d^2 (every convex corner adds a d^2 square, every reflex corner
//     removes one, and a simple rectilinear polygon has four more convex than reflex corners), so V(d) = (A + P d + 4 d^2) (H + 2 d),
//     checked on random histogram polygons with many reflex corners;
//   * the result's own faces: every output vertex is the input vertex plus an offset of norm <= sqrt(3) |d| (a cube) in the input frame.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/offset_solid.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::PlanarSolidOffsetReport;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using Loop = std::vector<Point3d>;

std::string S(double v) {
  char b[48];
  std::snprintf(b, sizeof(b), "%.9g", v);
  return b;
}

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

std::vector<Loop> CubeLoops(double x0, double y0, double z0, double L) {
  const double x1 = x0 + L, y1 = y0 + L, z1 = z0 + L;
  const Point3d p[8] = {Point3d(x0, y0, z0), Point3d(x1, y0, z0), Point3d(x1, y1, z0), Point3d(x0, y1, z0),
                        Point3d(x0, y0, z1), Point3d(x1, y0, z1), Point3d(x1, y1, z1), Point3d(x0, y1, z1)};
  return {{p[0], p[3], p[2], p[1]}, {p[4], p[5], p[6], p[7]}, {p[0], p[1], p[5], p[4]},
          {p[1], p[2], p[6], p[5]}, {p[2], p[3], p[7], p[6]}, {p[3], p[0], p[4], p[7]}};
}

void Transform(std::vector<Loop>& ls, const ON_Xform& x) {
  for (auto& l : ls)
    for (auto& p : l) {
      ON_3dPoint q(p.x, p.y, p.z);
      q.Transform(x);
      p = Point3d(q.x, q.y, q.z);
    }
}

}  // namespace

D8_EXTRA_TEST(QcOffsetSolidPlanarUnitCubeFarFromTheOrigin) {
  for (const double off : {1e3, 1e5, 1e6, 1e7}) {
    const std::string tag = "cube 1 at (" + S(off) + ", " + S(-0.7 * off) + ", " + S(0.3 * off) + ") d=0.1: ";
    const Brep cube = MakeSolid(CubeLoops(off, -0.7 * off, 0.3 * off, 1.0));
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(cube, 0.1, out, &rep);
    d8test::Check(r == Result::Ok, (tag + "accepted (" + rep.refusal_reason + ")").c_str());
    // the input's own corners are doubles at that magnitude: 3 faces' worth of one ulp(off) of edge-length error
    const double vtol = 1e-9 + 3e-14 * off;
    d8test::Check(std::fabs(rep.volume_before - 1.0) < vtol, (tag + "volume_before " + S(rep.volume_before) + " is 1").c_str());
    d8test::Check(std::fabs(rep.volume_after - 1.728) < vtol, (tag + "volume_after " + S(rep.volume_after) + " is (1 + 2 * 0.1)^3 = 1.728").c_str());
    if (r != Result::Ok) continue;
    ON_BoundingBox bb;  // of the vertices (ON_Brep::GetBoundingBox() of a planar face is the plane patch's, which overshoots)
    for (int v = 0; v < out.raw().m_V.Count(); ++v) bb.Set(out.raw().m_V[v].Point(), true);
    const double ulp_slack = 1e-9 * std::max(1.0, off);
    d8test::Check(std::fabs(bb.m_min.x - (off - 0.1)) < ulp_slack && std::fabs(bb.m_max.x - (off + 1.1)) < ulp_slack,
                  (tag + "the result sits at the input's own place: x in [" + S(bb.m_min.x) + ", " + S(bb.m_max.x) + "]").c_str());
    d8test::Check(out.raw().IsValid() && out.raw().IsSolid(), (tag + "result is a valid closed solid").c_str());
  }
}

D8_EXTRA_TEST(QcOffsetSolidPlanarRotatedCubeFarFromTheOrigin) {
  ON_Xform R;
  R.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0.3, 0.2, 0.1));
  for (const double off : {0.0, 1e5}) {
    auto ls = CubeLoops(1, 2, 3, 2.0);
    Transform(ls, R);
    ON_Xform T;
    T.Translation(off, 2 * off, -3 * off);
    Transform(ls, T);
    const std::string tag = "rotated 2-cube moved by " + S(off) + ", d=0.25: ";
    Brep out;
    PlanarSolidOffsetReport rep;
    const Result r = dino8::kernel::OffsetSolidPlanar(MakeSolid(ls), 0.25, out, &rep);
    d8test::Check(r == Result::Ok, (tag + "accepted (" + rep.refusal_reason + ")").c_str());
    d8test::Check(std::fabs(rep.volume_after - 15.625) < 1e-8, (tag + "volume_after " + S(rep.volume_after) + " is 2.5^3").c_str());
  }
}

D8_EXTRA_TEST(QcOffsetSolidPlanarNearlyCollapsedCubeKeepsItsVolume) {
  // inward by 0.49999: the cube is a 2e-5 cube. Its volume 8e-15 is the difference of fan terms of size 0.1 from the origin.
  Brep out;
  PlanarSolidOffsetReport rep;
  const Result r = dino8::kernel::OffsetSolidPlanar(MakeSolid(CubeLoops(0, 0, 0, 1.0)), -0.49999, out, &rep);
  d8test::Check(r == Result::Ok, ("cube d=-0.49999 accepted (" + rep.refusal_reason + ")").c_str());
  d8test::Check(std::fabs(rep.volume_after - 8e-15) < 1e-6 * 8e-15, ("cube d=-0.49999: volume_after " + S(rep.volume_after) + " is (2e-5)^3 = 8e-15 (1e-6 relative)").c_str());
}

// A histogram polygon (columns of integer width and height, neighbours of different height) extruded H, at a far offset and a small scale.
D8_EXTRA_TEST(QcOffsetSolidPlanarRectilinearPrismsFarAndSmallMatchTheMitreAreaFormula) {
  std::mt19937 rng(20241111);
  struct Case { double off, scale; };
  int total = 0, ok = 0, within = 0;
  std::string first_bad;
  for (const Case& cs : {Case{1e5, 1.0}, Case{0.0, 1e-3}, Case{1e5, 1e-2}}) {
    for (int it = 0; it < 6; ++it) {
      const int n = 2 + static_cast<int>(rng() % 4);
      std::vector<double> w(n), h(n);
      for (int i = 0; i < n; ++i) {
        w[i] = 1 + static_cast<double>(rng() % 3);
        do h[i] = 1 + static_cast<double>(rng() % 5); while (i > 0 && h[i] == h[i - 1]);
      }
      std::vector<std::pair<double, double>> poly;
      double W = 0;
      for (double v : w) W += v;
      poly.push_back({0, 0});
      poly.push_back({W, 0});
      double x = W;
      for (int i = n - 1; i >= 0; --i) {
        poly.push_back({x, h[i]});
        x -= w[i];
        poly.push_back({x, h[i]});
      }
      double A = 0, P = 0;
      for (size_t i = 0; i < poly.size(); ++i) {
        const auto a = poly[i], b = poly[(i + 1) % poly.size()];
        A += a.first * b.second - b.first * a.second;
        P += std::hypot(b.first - a.first, b.second - a.second);
      }
      A *= 0.5;
      const double H = 2 + static_cast<double>(rng() % 3);
      auto P3 = [&](double px, double py, double pz) { return Point3d(cs.off + cs.scale * px, -cs.off + cs.scale * py, 0.5 * cs.off + cs.scale * pz); };
      std::vector<Loop> faces;
      Loop bottom, top;
      for (const auto& q : poly) bottom.push_back(P3(q.first, q.second, 0));
      std::reverse(bottom.begin(), bottom.end());
      for (const auto& q : poly) top.push_back(P3(q.first, q.second, H));
      faces.push_back(bottom);
      faces.push_back(top);
      for (size_t i = 0; i < poly.size(); ++i) {
        const auto a = poly[i], b = poly[(i + 1) % poly.size()];
        faces.push_back({P3(a.first, a.second, 0), P3(b.first, b.second, 0), P3(b.first, b.second, H), P3(a.first, a.second, H)});
      }
      const Brep solid = MakeSolid(faces);
      for (const double d : {0.4, -0.3}) {
        Brep out;
        PlanarSolidOffsetReport rep;
        ++total;
        const Result r = dino8::kernel::OffsetSolidPlanar(solid, d * cs.scale, out, &rep);
        const double expect = (A + P * d + 4 * d * d) * (H + 2 * d) * cs.scale * cs.scale * cs.scale;
        if (r == Result::Ok) ++ok;
        else if (first_bad.empty()) first_bad = "off " + S(cs.off) + " scale " + S(cs.scale) + " d " + S(d) + ": " + rep.refusal_reason;
        if (r == Result::Ok && std::fabs(rep.volume_after - expect) <= 1e-8 * expect) ++within;
        else if (first_bad.empty()) first_bad = "off " + S(cs.off) + " scale " + S(cs.scale) + " d " + S(d) + ": volume_after " + S(rep.volume_after) + " vs " + S(expect);
      }
    }
  }
  d8test::Check(ok == total, ("random rectilinear prisms: all " + std::to_string(total) + " offsets accepted, accepted " + std::to_string(ok) + " " + first_bad).c_str());
  d8test::Check(within == total, ("random rectilinear prisms: volume_after = (A + P d + 4 d^2)(H + 2 d) to 1e-8 for all " + std::to_string(total) + ", matched " + std::to_string(within) + " " + first_bad).c_str());
}
