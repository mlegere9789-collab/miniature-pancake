// ChangeFaceSeam (kernel/brep_seam.h): SrfSeam on a face of a polysurface (a face that is its whole surface, so no trim loops have to move with the seam).
//
// Independent oracles: the face's ORIGINAL surface (the moved face at parameter u in its new domain [t, t + L] is the original at u wrapped into
// [a, a + L)), the closed forms of the primitives (sphere: every point at radius r from its centre, area 4 pi r^2, volume 4/3 pi r^3; torus: area
// 4 pi^2 R r, volume 2 pi^2 R r^2) computed by the Brep's own Area()/Volume() integrators AFTER the move, and negative controls (box faces are
// open, welded solids have trim loops, a bad face index / direction) that leave the Brep bit-identical.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/brep_seam.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

void Check(bool ok, const std::string& what) { d8test::Check(ok, what.c_str()); }

NurbsSurface FaceSurface(const Brep& b, int face) {
  NurbsSurface s;
  const ON_Surface* srf = b.raw().m_S[b.raw().m_F[face].m_si];
  if (const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(srf)) s.raw() = *ns; else srf->GetNurbForm(s.raw());
  return s;
}

double WrapDeviation(const NurbsSurface& moved, const NurbsSurface& orig, int dir, int nu = 33, int nv = 17) {
  const ON_Interval od = orig.raw().Domain(dir), md = moved.raw().Domain(dir);
  double worst = 0.0;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) {
      const double u = md.ParameterAt(i / (nu - 1.0)), g = j / (nv - 1.0);
      double w = od.Min() + std::fmod(u - od.Min(), od.Length());
      if (w < od.Min()) w += od.Length();
      ON_3dPoint pm, po;
      if (dir == 0) {
        pm = moved.raw().PointAt(u, moved.raw().Domain(1).ParameterAt(g));
        po = orig.raw().PointAt(w, orig.raw().Domain(1).ParameterAt(g));
      } else {
        pm = moved.raw().PointAt(moved.raw().Domain(0).ParameterAt(g), u);
        po = orig.raw().PointAt(orig.raw().Domain(0).ParameterAt(g), w);
      }
      worst = std::max(worst, pm.DistanceTo(po));
    }
  return worst;
}

bool SameSurfaceNet(const Brep& a, const Brep& b) {
  if (a.raw().m_F.Count() != b.raw().m_F.Count()) return false;
  for (int f = 0; f < a.raw().m_F.Count(); ++f) {
    const NurbsSurface sa = FaceSurface(a, f), sb = FaceSurface(b, f);
    if (sa.CVCountU() != sb.CVCountU() || sa.CVCountV() != sb.CVCountV()) return false;
    for (int d = 0; d < 2; ++d)
      if (sa.raw().Domain(d).Min() != sb.raw().Domain(d).Min() || sa.raw().Domain(d).Max() != sb.raw().Domain(d).Max()) return false;
    for (int i = 0; i < sa.CVCountU(); ++i)
      for (int j = 0; j < sa.CVCountV(); ++j) {
        ON_4dPoint pa, pb;
        sa.raw().GetCV(i, j, pa);
        sb.raw().GetCV(i, j, pb);
        if (pa.x != pb.x || pa.y != pb.y || pa.z != pb.z || pa.w != pb.w) return false;
      }
  }
  return true;
}

}  // namespace

D8_EXTRA_TEST(BrepFaceSeamSphere) {
  const Point3d c(1.0, 2.0, 3.0);
  const double r = 5.0;
  Brep b = Brep::Sphere(c, r);
  const NurbsSurface orig = FaceSurface(b, 0);
  const double area0 = b.Area(), vol0 = b.Volume();
  Check(std::abs(area0 - 4.0 * ON_PI * r * r) < 1e-3 * 4.0 * ON_PI * r * r && std::abs(vol0 - 4.0 / 3.0 * ON_PI * r * r * r) < 1e-2 * 4.0 / 3.0 * ON_PI * r * r * r,
        "fixture: the sphere's own Area/Volume match the closed forms before the move");
  const ON_Interval d0 = orig.raw().Domain(0);
  const double t = d0.ParameterAt(0.37);
  std::string why;
  const Result res = dino8::kernel::ChangeFaceSeam(b, 0, 0, t, &why);
  Check(res == Result::Ok, "sphere face: ChangeFaceSeam returns Ok");
  if (res != Result::Ok) return;
  // An ON_BrepFace caches its surface pointer (it is an ON_SurfaceProxy): the kernel's Area()/Volume() read the face through SurfaceOf(), so it must
  // point at the replaced surface (it used to keep the deleted one, which crashed Area() right after the move).
  Check(b.raw().m_F[0].SurfaceOf() == b.raw().m_S[b.raw().m_F[0].m_si], "sphere face: the face's cached surface proxy points at the replaced surface");
  const NurbsSurface moved = FaceSurface(b, 0);
  Check(std::abs(moved.raw().Domain(0).Min() - t) < 1e-12 && std::abs(moved.raw().Domain(0).Length() - d0.Length()) < 1e-12, "sphere face: the face's surface now has the domain [t, t + L]");
  Check(WrapDeviation(moved, orig, 0) < 1e-9, "sphere face: the moved surface IS the original at the wrapped parameter (1e-9)");
  double radius = 0.0;
  for (int i = 0; i <= 32; ++i)
    for (int j = 0; j <= 16; ++j) {
      const ON_3dPoint p = moved.raw().PointAt(moved.raw().Domain(0).ParameterAt(i / 32.0), moved.raw().Domain(1).ParameterAt(j / 16.0));
      radius = std::max(radius, std::abs(p.DistanceTo(ON_3dPoint(c.x, c.y, c.z)) - r));
    }
  Check(radius < 1e-9, "sphere face: every sample is at radius 5 from the centre (closed form, 1e-9)");
  const double area1 = b.Area(), vol1 = b.Volume();
  Check(std::abs(area1 - area0) < 1e-6 * area0 && std::abs(vol1 - vol0) < 1e-6 * vol0, "sphere face: Area() and Volume() of the Brep are unchanged by the move (1e-6 relative)");
  Check(std::abs(area1 - 4.0 * ON_PI * r * r) < 1e-3 * 4.0 * ON_PI * r * r, "sphere face: Area() after the move is still 4 pi r^2 (closed form, 1e-3)");
  // the seam at its current place, and the old seam parameter outside the moved domain
  Check(dino8::kernel::ChangeFaceSeam(b, 0, 0, t) == Result::NoOpAlreadySatisfied, "sphere face: moving the seam to where it now is is NoOpAlreadySatisfied");
  Check(dino8::kernel::ChangeFaceSeam(b, 0, 0, d0.Min(), &why) == Result::Failed && !why.empty(), "sphere face: the old seam parameter is outside the moved domain: refused with a reason");
  Check(dino8::kernel::ChangeFaceSeam(b, 0, 0, d0.Min() + d0.Length()) == Result::Ok && WrapDeviation(FaceSurface(b, 0), orig, 0) < 1e-9,
        "sphere face: moving the seam back (to a + L) restores the original geometry at the wrapped parameter (1e-9)");
}

D8_EXTRA_TEST(BrepFaceSeamTorusBothDirections) {
  const double R = 4.0, rr = 1.0;
  Brep b = Brep::Torus(Point3d(0, 0, 0), dino8::kernel::Vector3d(0, 0, 1), R, rr);
  const double area0 = b.Area();
  Check(std::abs(area0 - 4.0 * ON_PI * ON_PI * R * rr) < 1e-3 * 4.0 * ON_PI * ON_PI * R * rr, "fixture: the torus's own Area matches 4 pi^2 R r before the move");
  for (int dir = 0; dir < 2; ++dir) {
    const NurbsSurface before = FaceSurface(b, 0);
    const ON_Interval d = before.raw().Domain(dir);
    const double t = d.ParameterAt(0.6);
    const std::string tag = std::string("torus face, direction ") + (dir == 0 ? "U" : "V");
    Check(dino8::kernel::ChangeFaceSeam(b, 0, dir, t) == Result::Ok, tag + ": ChangeFaceSeam returns Ok");
    const NurbsSurface after = FaceSurface(b, 0);
    Check(std::abs(after.raw().Domain(dir).Min() - t) < 1e-12 && WrapDeviation(after, before, dir, 33, 33) < 1e-9, tag + ": new domain starts at t and the surface is the original at the wrapped parameter (1e-9)");
    double off = 0.0;
    for (int i = 0; i <= 32; ++i)
      for (int j = 0; j <= 32; ++j) {
        const ON_3dPoint p = after.raw().PointAt(after.raw().Domain(0).ParameterAt(i / 32.0), after.raw().Domain(1).ParameterAt(j / 32.0));
        off = std::max(off, std::abs(std::sqrt((std::hypot(p.x, p.y) - R) * (std::hypot(p.x, p.y) - R) + p.z * p.z) - rr));
      }
    Check(off < 1e-9, tag + ": every sample is at distance 1 from the tube's centre circle (closed form, 1e-9)");
  }
  const double area1 = b.Area(), vol1 = b.Volume();
  Check(std::abs(area1 - area0) < 1e-6 * area0, "torus face: Area() is unchanged by moving both seams (1e-6 relative)");
  Check(std::abs(vol1 - 2.0 * ON_PI * ON_PI * R * rr * rr) < 1e-2 * 2.0 * ON_PI * ON_PI * R * rr * rr, "torus face: Volume() after moving both seams is 2 pi^2 R r^2 (closed form, 1e-2)");
}

D8_EXTRA_TEST(BrepFaceSeamRefusals) {
  // Box faces are open planes: no seam. Welded solids have real trim loops. Both leave the Brep bit-identical and give a reason.
  {
    Brep b = Brep::Box(0, 0, 0, 1, 2, 3);
    const Brep before = b;
    std::string why;
    Check(dino8::kernel::ChangeFaceSeam(b, 0, 0, 0.5, &why) == Result::Failed && SameSurfaceNet(b, before), "a box face has no seam to move: refused, bit-identical");
  }
  {
    Brep b = Brep::SphereWelded(Point3d(0, 0, 0), 2.0);
    const Brep before = b;
    std::string why;
    const NurbsSurface s = FaceSurface(b, 0);
    const Result r = dino8::kernel::ChangeFaceSeam(b, 0, 0, s.raw().Domain(0).ParameterAt(0.4), &why);
    Check(r == Result::Failed && SameSurfaceNet(b, before) && why.find("trim loops") != std::string::npos, "a welded sphere's face has trim loops: refused with that reason, bit-identical");
  }
  {
    Brep b = Brep::Sphere(Point3d(0, 0, 0), 2.0);
    bool threw_face = false, threw_dir = false;
    try { dino8::kernel::ChangeFaceSeam(b, 7, 0, 0.0); } catch (const std::invalid_argument&) { threw_face = true; }
    try { dino8::kernel::ChangeFaceSeam(b, 0, 2, 0.0); } catch (const std::invalid_argument&) { threw_dir = true; }
    Check(threw_face && threw_dir, "a face index out of range and a direction outside 0/1 throw std::invalid_argument");
  }
}
