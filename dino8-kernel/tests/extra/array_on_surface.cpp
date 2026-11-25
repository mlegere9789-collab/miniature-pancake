// ArrayBrepOnSurface / ArrayOnSurfaceTransforms (pattern.h): copies of a body at a (u, v) lattice of a NURBS surface, each carried by
// the surface frame (origin S(u, v), z = unit(Su x Sv), x = unit Su, y = z cross x). Closed-form oracles on four surfaces, none read
// off the function's own output:
//   * a flat 10 x 8 patch in z = 0 (control grid listed (0,0) (0,8) (10,0) (10,8): u runs along x), 3 x 3: copy (i, j) is the body translated by (5 i, 4 j, 0),
//   * a tilted patch (0,0,0) (10,0,0) (0,8,6) (10,8,6): constant normal (0, -0.6, 0.8), x = (1, 0, 0), y = n x x = (0, 0.8, 0.6), the
//     origin of copy (i, j) is (5 i, 4 j, 3 j) (3 x 3 lattice),
//   * a cylinder r = 5, h = 10 about the z axis: the 5 u stations of a rational circle hit its knots, i.e. angles 0, 90, 180, 270, 360
//     degrees; the v middle is z = 5; normal = radial outward, x = counter-clockwise tangent, y = +z,
//   * the sphere r = 5, nv = 1 (the equator): origins (5 cos t, 5 sin t, 0), normal radial outward, x = tangent, y = +z; nv = 3 also
//     includes the two poles, where Su x Sv vanishes: the frames must still be orthonormal and the maps rigid (a documented fallback),
// and for every copy the vertex v of the body lands at S + v.x x_hat + v.y y_hat + v.z n_hat (a rigid placement), the volume is
// preserved, orient = false is a pure translation by S - reference.origin, and nu < 1 throws std::invalid_argument.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/pattern.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::ArrayAlongCurveTransforms;
using dino8::kernel::ArrayBrepOnSurface;
using dino8::kernel::ArrayFrameOnSurface;
using dino8::kernel::ArrayOnSurfaceTransforms;
using dino8::kernel::Brep;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;

double Dist(const ON_3dPoint& a, const ON_3dPoint& b) { return a.DistanceTo(b); }

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

NurbsSurface Wrap(const ON_NurbsSurface& n) {
  NurbsSurface s;
  s.raw() = n;
  return s;
}

ON_Plane WorldBase() { return ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0)); }

// A 1 x 1 x 1 box standing on z = 0, footprint centre at the origin.
Brep Box() { return Brep::BoxWelded(-0.5, -0.5, 0, 0.5, 0.5, 1); }

// Expected placement of the box vertex `v` at surface point `s` with unit frame (ex, ey, n).
ON_3dPoint Place(const ON_3dPoint& s, const ON_3dVector& ex, const ON_3dVector& ey, const ON_3dVector& n, const ON_3dPoint& v) {
  return s + ex * v.x + ey * v.y + n * v.z;
}

struct Expect {
  ON_3dPoint s;
  ON_3dVector ex, ey, n;
};

// Checks every copy of `box` against the closed-form frames in `want` (i-major), plus rigid maps and volume.
void CheckLattice(const char* label, const Brep& box, const NurbsSurface& surface, int nu, int nv, const std::vector<Expect>& want, double tol) {
  const std::vector<Brep> copies = ArrayBrepOnSurface(box, surface, nu, nv, WorldBase());
  const std::vector<ON_Xform> maps = ArrayOnSurfaceTransforms(surface, nu, nv, WorldBase());
  d8test::Check(copies.size() == static_cast<size_t>(nu) * nv && maps.size() == copies.size() && want.size() == copies.size(),
                (std::string(label) + ": nu x nv copies and maps").c_str());
  if (copies.size() != want.size() || maps.size() != want.size()) return;
  for (size_t k = 0; k < want.size(); ++k) {
    double worst = 0;
    for (int v = 0; v < box.raw().m_V.Count(); ++v) {
      worst = std::max(worst, Dist(copies[k].raw().m_V[v].point, Place(want[k].s, want[k].ex, want[k].ey, want[k].n, box.raw().m_V[v].point)));
    }
    const std::string m = std::string(label) + ", copy " + std::to_string(k) + ": every vertex is S + vx ex + vy ey + vz n of the closed-form frame (to 1e-9), the map is rigid, volume 1";
    const auto mp = copies[k].VolumeMassProperties();
    d8test::Check(worst < tol && Rigid(maps[k], 1e-9) && std::fabs(mp.volume - 1.0) < 1e-6, m.c_str());
  }
}

}  // namespace

D8_EXTRA_TEST(ArrayOnSurfaceFlatAndTiltedPlanes) {
  const Brep box = Box();
  const NurbsSurface flat = NurbsSurface::FromControlGrid({Point3d(0, 0, 0), Point3d(0, 8, 0), Point3d(10, 0, 0), Point3d(10, 8, 0)}, 2, 2, 1, 1);
  std::vector<Expect> w;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) w.push_back({ON_3dPoint(5.0 * i, 4.0 * j, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0), ON_3dVector(0, 0, 1)});
  CheckLattice("flat plane 3 x 3", box, flat, 3, 3, w, 1e-9);

  const NurbsSurface tilt = NurbsSurface::FromControlGrid({Point3d(0, 0, 0), Point3d(0, 8, 6), Point3d(10, 0, 0), Point3d(10, 8, 6)}, 2, 2, 1, 1);
  std::vector<Expect> t;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) t.push_back({ON_3dPoint(5.0 * i, 4.0 * j, 3.0 * j), ON_3dVector(1, 0, 0), ON_3dVector(0, 0.8, 0.6), ON_3dVector(0, -0.6, 0.8)});
  CheckLattice("tilted plane 3 x 3", box, tilt, 3, 3, t, 1e-9);

  // one point per direction: the middle of the patch
  const std::vector<ON_Xform> mid = ArrayOnSurfaceTransforms(flat, 1, 1, WorldBase());
  d8test::Check(mid.size() == 1 && Dist(mid[0] * ON_3dPoint(0, 0, 0), ON_3dPoint(5, 4, 0)) < 1e-9, "a 1 x 1 lattice is the middle of the domain: (5, 4, 0)");
  // orient = false: a pure translation
  const std::vector<ON_Xform> plain = ArrayOnSurfaceTransforms(tilt, 3, 3, WorldBase(), false);
  bool pure = plain.size() == 9;
  for (size_t k = 0; pure && k < plain.size(); ++k) {
    const int i = static_cast<int>(k) / 3, j = static_cast<int>(k) % 3;
    pure = Dist(plain[k] * ON_3dPoint(1, 2, 3), ON_3dPoint(1 + 5.0 * i, 2 + 4.0 * j, 3 + 3.0 * j)) < 1e-9 && std::fabs(plain[k].m_xform[0][0] - 1) < 1e-12 && std::fabs(plain[k].m_xform[1][1] - 1) < 1e-12 &&
           std::fabs(plain[k].m_xform[2][2] - 1) < 1e-12;
  }
  d8test::Check(pure, "orient = false on the tilted patch: copy (i, j) is a pure translation by (5 i, 4 j, 3 j)");
  bool zero_throws = false, neg_throws = false;
  try { (void)ArrayOnSurfaceTransforms(flat, 0, 3, WorldBase()); } catch (const std::invalid_argument&) { zero_throws = true; } catch (const std::exception&) {}
  try { (void)ArrayOnSurfaceTransforms(flat, 3, -1, WorldBase()); } catch (const std::invalid_argument&) { neg_throws = true; } catch (const std::exception&) {}
  d8test::Check(zero_throws && neg_throws, "nu = 0 and nv = -1 throw std::invalid_argument");
}

D8_EXTRA_TEST(ArrayOnSurfaceCylinderAndSphereEquator) {
  const Brep box = Box();
  ON_NurbsSurface cn;
  ON_Cylinder(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 5.0), 10.0).GetNurbForm(cn);
  const NurbsSurface cyl = Wrap(cn);
  std::vector<Expect> c;
  for (int i = 0; i < 5; ++i) {
    const double th = ON_PI * 0.5 * i;
    const ON_3dVector radial(std::cos(th), std::sin(th), 0), tangent(-std::sin(th), std::cos(th), 0);
    c.push_back({ON_3dPoint(5.0 * radial.x, 5.0 * radial.y, 5.0), tangent, ON_3dVector(0, 0, 1), radial});
  }
  CheckLattice("cylinder r=5, 5 x 1 (angles 0, 90, 180, 270, 360 at the knots)", box, cyl, 5, 1, c, 1e-9);

  ON_NurbsSurface sn;
  ON_Sphere(ON_3dPoint(0, 0, 0), 5.0).GetNurbForm(sn);
  const NurbsSurface sph = Wrap(sn);
  std::vector<Expect> s;
  for (int i = 0; i < 5; ++i) {
    const double th = ON_PI * 0.5 * i;
    const ON_3dVector radial(std::cos(th), std::sin(th), 0), tangent(-std::sin(th), std::cos(th), 0);
    s.push_back({ON_3dPoint(5.0 * radial.x, 5.0 * radial.y, 0), tangent, ON_3dVector(0, 0, 1), radial});
  }
  CheckLattice("sphere r=5 equator 5 x 1", box, sph, 5, 1, s, 1e-9);

  // nv = 3 reaches both poles: Su x Sv vanishes there, the frame falls back but stays orthonormal and rigid, on the right point
  const std::vector<ON_Xform> maps = ArrayOnSurfaceTransforms(sph, 5, 3, WorldBase());
  d8test::Check(maps.size() == 15, "sphere 5 x 3: 15 maps (the lattice includes the poles)");
  if (maps.size() == 15) {
    bool rigid = true, on_sphere = true;
    for (size_t k = 0; k < maps.size(); ++k) {
      rigid = rigid && Rigid(maps[k], 1e-9);
      const ON_3dPoint p = maps[k] * ON_3dPoint(0, 0, 0);
      on_sphere = on_sphere && std::fabs(p.DistanceTo(ON_3dPoint(0, 0, 0)) - 5.0) < 1e-9;
    }
    d8test::Check(rigid, "sphere 5 x 3: every map is rigid, poles included");
    d8test::Check(on_sphere, "sphere 5 x 3: every copy's origin is on the sphere (|p| = 5), poles included");
    const auto f = ArrayFrameOnSurface(sph, 0.0, sph.Domain(1).max);
    d8test::Check(std::fabs(f.u_dir.Length() - 1) < 1e-9 && std::fabs(f.v_dir.Length() - 1) < 1e-9 && std::fabs(f.normal.Length() - 1) < 1e-9 &&
                      std::fabs(ON_DotProduct(f.u_dir, f.v_dir)) < 1e-9 && std::fabs(ON_DotProduct(f.u_dir, f.normal)) < 1e-9 &&
                      std::fabs(ON_DotProduct(f.v_dir, f.normal)) < 1e-9 && ON_DotProduct(ON_CrossProduct(f.u_dir, f.v_dir), f.normal) > 0.999999,
                  "the frame at the north pole is orthonormal and right-handed");
  }
}
