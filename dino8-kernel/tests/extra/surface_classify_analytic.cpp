// NurbsSurface::Classify() / AnalyticSurfacePoint() / AnalyticClosestPoint(): the typed analytic surface (plane, sphere,
// cylinder, cone, torus) recovered from NURBS data, with closed-form evaluation and inversion.
//
// Oracles are hand-derived and independent of the code under test:
//   * the NURBS are built here by hand as surfaces of revolution: the 9-point rational quadratic circle (weights 1,
//     sqrt(2)/2) in u times a profile (line / semicircle / circle) in v, in a tilted frame - not via OpenNURBS'
//     GetNurbForm - with the stated radii, apex position, slope;
//   * the expected foot of a probe point is built backwards: probe = analytic point at (u, v) + t * outward normal, with the
//     point and normal written out here from the closed forms, so the expected foot, signed distance t and normal are
//     known exactly;
//   * the finite NURBS patch is cross-checked with the kernel's grid-seeded Newton projection onto the NURBS (a different algorithm).
// Negatives: elliptic cylinder, sphere with one weight changed, a wavy bicubic patch.
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::AnalyticClosestPoint;
using dino8::kernel::AnalyticFoot;
using dino8::kernel::AnalyticSurfacePoint;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::SurfaceClassification;
using dino8::kernel::SurfaceKind;
using dino8::kernel::Vector3d;

const double kTol = 1e-6;

struct Frame {
  Point3d o;
  Vector3d x, y, z;
  Point3d At(double a, double b, double c) const { return o + a * x + b * y + c * z; }
};

Frame TiltedFrame() {
  Vector3d x(1.0, 2.0, -0.5), z(0.3, -0.4, 1.0);
  x.Unitize();
  z = z - ON_DotProduct(z, x) * x;
  z.Unitize();
  Vector3d y = ON_CrossProduct(z, x);
  return Frame{Point3d(1.5, -2.0, 0.75), x, y, z};
}

// Surface of revolution about f.z: the rational quadratic circle net in u, a profile (rho_j, z_j, weight_j, degree, knots)
// in v. `ry_scale` stretches the circle's y direction (1 = circle, else an ellipse).
NurbsSurface Revolved(const Frame& f, const std::vector<double>& rho, const std::vector<double>& z, const std::vector<double>& wv, int degv,
                      const std::vector<double>& kv, double ry_scale = 1.0) {
  const double s = std::sqrt(0.5);
  const double px[9] = {1, 1, 0, -1, -1, -1, 0, 1, 1}, py[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
  NurbsSurface surf;
  ON_NurbsSurface& r = surf.raw();
  const int nv = static_cast<int>(rho.size());
  r.Create(3, true, 3, degv + 1, 9, nv);
  const double ku[10] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4};
  for (int k = 0; k < 10; ++k) r.SetKnot(0, k, ku[k]);
  for (size_t k = 0; k < kv.size(); ++k) r.SetKnot(1, static_cast<int>(k), kv[k]);
  for (int i = 0; i < 9; ++i) {
    for (int j = 0; j < nv; ++j) {
      const double w = (i % 2 == 1 ? s : 1.0) * wv[static_cast<size_t>(j)];
      const Point3d p = f.At(rho[static_cast<size_t>(j)] * px[i], ry_scale * rho[static_cast<size_t>(j)] * py[i], z[static_cast<size_t>(j)]);
      r.SetCV(i, j, ON_4dPoint(p.x * w, p.y * w, p.z * w, w));
    }
  }
  return surf;
}

NurbsSurface Cylinder(const Frame& f, double rad, double z0, double z1, double ry_scale = 1.0) {
  return Revolved(f, {rad, rad}, {z0, z1}, {1, 1}, 1, {0, 1}, ry_scale);
}
NurbsSurface Cone(const Frame& f) { return Revolved(f, {0.6, 1.5}, {1.0, 2.5}, {1, 1}, 1, {0, 1}); }  // apex at f.o, rho = 0.6 z
NurbsSurface Sphere(const Frame& f, double rad, double w_override = -1.0) {
  const double s = std::sqrt(0.5);
  std::vector<double> w = {1, s, 1, s, 1};
  if (w_override > 0.0) w[1] = w_override;
  return Revolved(f, {0, rad, rad, rad, 0}, {-rad, -rad, 0, rad, rad}, w, 2, {0, 0, 1, 1, 2, 2});
}
NurbsSurface Torus(const Frame& f, double R, double r) {
  const double s = std::sqrt(0.5);
  const double qx[9] = {1, 1, 0, -1, -1, -1, 0, 1, 1}, qy[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
  std::vector<double> rho, z, w;
  for (int j = 0; j < 9; ++j) {
    rho.push_back(R + r * qx[j]);
    z.push_back(r * qy[j]);
    w.push_back(j % 2 == 1 ? s : 1.0);
  }
  return Revolved(f, rho, z, w, 2, {0, 0, 1, 1, 2, 2, 3, 3, 4, 4});
}
NurbsSurface Plane(const Frame& f) {
  NurbsSurface surf;
  ON_NurbsSurface& r = surf.raw();
  r.Create(3, false, 2, 2, 2, 2);
  for (int d = 0; d < 2; ++d) {
    r.SetKnot(d, 0, 0.0);
    r.SetKnot(d, 1, 1.0);
  }
  r.SetCV(0, 0, f.At(0, 0, 0));
  r.SetCV(1, 0, f.At(4, 0, 0));
  r.SetCV(0, 1, f.At(0, 3, 0));
  r.SetCV(1, 1, f.At(4, 3, 0));
  return surf;
}

bool Near(const Point3d& a, const Point3d& b, double t = 1e-9) { return a.DistanceTo(b) < t; }

// Hand-written closed forms in the construction frame: (point, outward normal) at (u, v) for each kind.
struct Sample {
  Point3d p;
  Vector3d n;
};
Sample SphereAt(const Frame& f, double rad, double u, double v) {
  const Vector3d n = std::cos(v) * (std::cos(u) * f.x + std::sin(u) * f.y) + std::sin(v) * f.z;
  return {f.o + rad * n, n};
}
Sample CylinderAt(const Frame& f, double rad, double u, double h) {
  const Vector3d n = std::cos(u) * f.x + std::sin(u) * f.y;
  return {f.o + rad * n + h * f.z, n};
}
Sample ConeAt(const Frame& f, double u, double h) {  // rho = 0.6 h, apex f.o
  const Vector3d rad = std::cos(u) * f.x + std::sin(u) * f.y;
  Vector3d n = rad - 0.6 * f.z;
  n.Unitize();
  return {f.o + 0.6 * h * rad + h * f.z, n};
}
Sample TorusAt(const Frame& f, double R, double r, double u, double v) {
  const Vector3d rad = std::cos(u) * f.x + std::sin(u) * f.y;
  const Vector3d n = std::cos(v) * rad + std::sin(v) * f.z;
  return {f.o + R * rad + r * n, n};
}

// Probe = foot + t n; checks the library's foot against the construction, and against the kernel's grid-seeded Newton projection (SurfaceClosestPointGlobal)
// on the finite patch.
void CheckProbes(const char* name, const NurbsSurface& surf, const SurfaceClassification& cls, const std::vector<Sample>& feet, bool signed_normal) {
  for (const Sample& s : feet) {
    for (double t : {0.35, -0.2}) {
      const Point3d probe = s.p + t * s.n;
      AnalyticFoot foot;
      const bool ok = AnalyticClosestPoint(cls, probe, foot);
      char tag[160];
      std::snprintf(tag, sizeof tag, "%s: closest point found for a probe %+.2f off the surface", name, t);
      d8test::Check(ok, tag);
      if (!ok) continue;
      std::snprintf(tag, sizeof tag, "%s: foot is the construction point (probe %+.2f)", name, t);
      d8test::Check(Near(foot.point, s.p), tag);
      std::snprintf(tag, sizeof tag, "%s: AnalyticSurfacePoint at the returned (u, v) is the foot (probe %+.2f)", name, t);
      d8test::Check(Near(AnalyticSurfacePoint(cls, foot.u, foot.v), foot.point), tag);
      std::snprintf(tag, sizeof tag, "%s: |signed distance| is the offset (probe %+.2f)", name, t);
      d8test::Check(std::fabs(std::fabs(foot.signed_distance) - std::fabs(t)) < 1e-9, tag);
      if (signed_normal) {
        std::snprintf(tag, sizeof tag, "%s: outward normal and signed distance (probe %+.2f)", name, t);
        d8test::Check((foot.normal - s.n).Length() < 1e-9 && std::fabs(foot.signed_distance - t) < 1e-9, tag);
      } else {
        std::snprintf(tag, sizeof tag, "%s: normal is the plane normal, signed distance is probe . normal (probe %+.2f)", name, t);
        d8test::Check(std::fabs(std::fabs(ON_DotProduct(foot.normal, s.n)) - 1.0) < 1e-9 && std::fabs(foot.signed_distance - ON_DotProduct(probe - s.p, foot.normal)) < 1e-9, tag);
      }
      double u = 0, v = 0;
      const bool newton = dino8::kernel::SurfaceClosestPointGlobal(surf.raw(), probe, u, v);
      std::snprintf(tag, sizeof tag, "%s: Newton projection onto the NURBS patch agrees (probe %+.2f)", name, t);
      d8test::Check(newton && Near(surf.raw().PointAt(u, v), foot.point, 1e-6), tag);
    }
  }
}

}  // namespace

D8_EXTRA_TEST(SurfaceClassifyRecoversHandBuiltAnalyticSurfaces) {
  const Frame f = TiltedFrame();
  {
    const NurbsSurface s = Sphere(f, 2.0);
    const SurfaceClassification k = s.Classify(kTol);
    d8test::Check(k.kind == SurfaceKind::Sphere, "hand-built rational sphere (radius 2) classifies as Sphere");
    if (k.kind == SurfaceKind::Sphere) {
      d8test::Check(Near(k.sphere.plane.origin, f.o, 1e-7) && std::fabs(k.sphere.radius - 2.0) < 1e-7, "sphere: centre and radius are the construction's");
      CheckProbes("sphere", s, k, {SphereAt(f, 2.0, 0.7, 0.4), SphereAt(f, 2.0, -2.1, -0.9), SphereAt(f, 2.0, 3.0, 1.1)}, true);
    }
  }
  {
    const NurbsSurface s = Cylinder(f, 1.2, 0.5, 2.5);
    const SurfaceClassification k = s.Classify(kTol);
    d8test::Check(k.kind == SurfaceKind::Cylinder, "hand-built rational cylinder (radius 1.2) classifies as Cylinder");
    if (k.kind == SurfaceKind::Cylinder) {
      const Vector3d axis = k.cylinder.circle.plane.zaxis;
      const Vector3d off = k.cylinder.circle.plane.origin - f.o;
      d8test::Check(std::fabs(k.cylinder.circle.radius - 1.2) < 1e-7 && std::fabs(std::fabs(ON_DotProduct(axis, f.z)) - 1.0) < 1e-9, "cylinder: radius and axis direction are the construction's");
      d8test::Check((off - ON_DotProduct(off, f.z) * f.z).Length() < 1e-7, "cylinder: the recovered circle is centred on the construction axis");
      CheckProbes("cylinder", s, k, {CylinderAt(f, 1.2, 0.4, 1.3), CylinderAt(f, 1.2, -2.5, 0.9), CylinderAt(f, 1.2, 2.2, 2.2)}, true);
    }
  }
  {
    const NurbsSurface s = Cone(f);
    const SurfaceClassification k = s.Classify(kTol);
    d8test::Check(k.kind == SurfaceKind::Cone, "hand-built rational truncated cone (slope 0.6, apex at the frame origin) classifies as Cone");
    if (k.kind == SurfaceKind::Cone) {
      d8test::Check(Near(k.cone.plane.origin, f.o, 1e-7), "cone: the apex is the construction apex");
      d8test::Check(std::fabs(std::fabs(ON_DotProduct(k.cone.plane.zaxis, f.z)) - 1.0) < 1e-9 && std::fabs(std::fabs(k.cone.radius / k.cone.height) - 0.6) < 1e-7, "cone: axis and radius / height slope 0.6 are the construction's");
      CheckProbes("cone", s, k, {ConeAt(f, 0.5, 1.4), ConeAt(f, -1.9, 2.0), ConeAt(f, 2.6, 1.1)}, true);
    }
  }
  {
    const NurbsSurface s = Torus(f, 3.0, 1.0);
    const SurfaceClassification k = s.Classify(kTol);
    d8test::Check(k.kind == SurfaceKind::Torus, "hand-built rational torus (R 3, r 1) classifies as Torus");
    if (k.kind == SurfaceKind::Torus) {
      d8test::Check(Near(k.torus.plane.origin, f.o, 1e-7) && std::fabs(k.torus.major_radius - 3.0) < 1e-7 && std::fabs(k.torus.minor_radius - 1.0) < 1e-7 &&
                        std::fabs(std::fabs(ON_DotProduct(k.torus.plane.zaxis, f.z)) - 1.0) < 1e-9,
                    "torus: centre, axis, major and minor radius are the construction's");
      CheckProbes("torus", s, k, {TorusAt(f, 3.0, 1.0, 0.6, 0.5), TorusAt(f, 3.0, 1.0, -2.0, 2.4), TorusAt(f, 3.0, 1.0, 1.9, -1.0)}, true);
    }
  }
  {
    const NurbsSurface s = Plane(f);
    const SurfaceClassification k = s.Classify(kTol);
    d8test::Check(k.kind == SurfaceKind::Plane, "hand-built slanted-frame bilinear patch classifies as Plane");
    if (k.kind == SurfaceKind::Plane) {
      d8test::Check(std::fabs(std::fabs(ON_DotProduct(k.plane.zaxis, f.z)) - 1.0) < 1e-9 && std::fabs(ON_DotProduct(k.plane.origin - f.o, f.z)) < 1e-9, "plane: normal and offset are the construction's");
      CheckProbes("plane", s, k, {Sample{f.At(1.0, 1.0, 0), f.z}, Sample{f.At(3.0, 2.0, 0), f.z}}, false);
    }
  }
}

D8_EXTRA_TEST(SurfaceClassifyNegativesAndDegenerateProbes) {
  const Frame f = TiltedFrame();
  d8test::Check(Cylinder(f, 1.2, 0.5, 2.5, 0.7).Classify(kTol).kind == SurfaceKind::General, "an elliptic cylinder (1.2 x 0.84) is General");
  d8test::Check(Sphere(f, 2.0, 0.6).Classify(kTol).kind == SurfaceKind::General, "a sphere with one weight changed (0.6 for sqrt(1/2)) is General");
  {
    std::vector<Point3d> g;
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) g.push_back(Point3d(i, j, (i * 3 + j * 5) % 4 * 0.4));
    }
    NurbsSurface wavy = NurbsSurface::FromControlGrid(g, 4, 4, 3, 3);
    d8test::Check(wavy.Classify(kTol).kind == SurfaceKind::General, "a wavy bicubic patch is General");
  }
  d8test::Check(NurbsSurface().Classify(kTol).kind == SurfaceKind::General, "an empty surface is General and does not throw");
  // Degenerate probes have no unique foot.
  const SurfaceClassification sph = Sphere(f, 2.0).Classify(kTol), cyl = Cylinder(f, 1.2, 0.5, 2.5).Classify(kTol);
  const SurfaceClassification tor = Torus(f, 3.0, 1.0).Classify(kTol), cone = Cone(f).Classify(kTol);
  AnalyticFoot foot;
  foot.u = 12345.0;
  d8test::Check(!AnalyticClosestPoint(sph, f.o, foot) && foot.u == 12345.0, "sphere: the centre has no unique closest point, output untouched");
  d8test::Check(!AnalyticClosestPoint(cyl, f.At(0, 0, 1.7), foot), "cylinder: a point on the axis has no unique closest point");
  d8test::Check(!AnalyticClosestPoint(tor, f.At(0, 0, 0.4), foot), "torus: a point on the axis has no unique closest point");
  d8test::Check(!AnalyticClosestPoint(cone, f.o, foot), "cone: the apex has no unique closest point");
  d8test::Check(!AnalyticClosestPoint(SurfaceClassification{}, f.o, foot), "a General classification has no closest point");
  bool threw = false;
  try {
    AnalyticSurfacePoint(SurfaceClassification{}, 0.0, 0.0);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  d8test::Check(threw, "AnalyticSurfacePoint refuses a General classification");
  // A cone probe beyond the apex (the other nappe's side) lands on the apex of the single nappe: foot = apex.
  AnalyticFoot below;
  const bool ok = AnalyticClosestPoint(cone, f.At(0.0, 0.3, -2.0), below);
  d8test::Check(ok && Near(below.point, f.o), "cone: a probe behind the apex has the apex as its closest point on the nappe");
}
