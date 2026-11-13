// BooleanCombineGeneral with Brep::SphereWelded() (a revolved surface with real seam / pole topology) as an operand.
// Closed forms: a radius-R sphere cut by the box [0,10]^2 x [-5,5] (R = 3, centred on the origin) keeps exactly one
// quarter of the sphere when 2R < 10 and the slab is wider than 2R: V_inter = (1/4) 4/3 pi R^3. Sphere + cylinder through a
// diameter (axis z, r = 1, R = 2): intersection 4/3 pi (R^3 - (R^2 - r^2)^1.5).
#include <cmath>
#include <exception>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {
using dino8::kernel::BooleanCombineGeneral;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Point3d;


ON_Plane FrameFromAxis(const Point3d& origin, dino8::kernel::Vector3d axis) {
  axis.Unitize();
  dino8::kernel::Vector3d seed = std::fabs(axis.z) < 0.9 ? dino8::kernel::Vector3d(0, 0, 1) : dino8::kernel::Vector3d(1, 0, 0);
  dino8::kernel::Vector3d x = ON_CrossProduct(seed, axis);
  x.Unitize();
  dino8::kernel::Vector3d y = ON_CrossProduct(axis, x);
  y.Unitize();
  ON_Plane f;
  f.origin = origin;
  f.xaxis = x;
  f.yaxis = y;
  f.zaxis = axis;
  f.UpdateEquation();
  return f;
}

Brep::PlanarFace DiskCap(const ON_Plane& frame, double h, double r, bool flip, int n = 128) {
  Brep::PlanarFace pf;
  const Point3d c = frame.origin + frame.zaxis * h;
  pf.plane = ON_Plane(c, flip ? -frame.zaxis : frame.zaxis);
  for (int i = 0; i < n; ++i) {
    const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
    pf.loop.push_back(c + frame.xaxis * (r * std::cos(a)) + frame.yaxis * (r * std::sin(a)));
  }
  pf.notch_begin = 0;
  pf.notch_count = static_cast<int>(pf.loop.size());
  return pf;
}

Brep MakeCylinder(const Point3d& base, const dino8::kernel::Vector3d& axis, double r, double length) {
  Brep::CylindricalFace cf;
  cf.frame = FrameFromAxis(base, axis);
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(cf.frame, 0.0, r, true), DiskCap(cf.frame, length, r, false)}, {cf});
}

const char* OpName(BooleanOp o) { return o == BooleanOp::Union ? "Union" : o == BooleanOp::Intersection ? "Intersection" : "Difference"; }

void Run(const Brep& a, const Brep& b, BooleanOp op, double expect, const std::string& what) {
  try {
    const Brep r = BooleanCombineGeneral(a, b, op);
    ON_TextLog log;
    d8test::Check(r.raw().IsValid(&log), (what + " " + OpName(op) + ": valid ON_Brep").c_str());
    const auto m = dino8::kernel::TessellateGeneralBooleanClosedMesh(r, 32, 128);
    d8test::Check(m.IsClosedManifold(), (what + " " + OpName(op) + ": closed manifold tessellation").c_str());
    d8test::Check(std::abs(m.Volume() - expect) < 0.03 * expect, (what + " " + OpName(op) + ": volume matches the closed form").c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (what + " " + OpName(op) + " threw: " + e.what()).c_str());
  }
}
}  // namespace

D8_EXTRA_TEST(BooleanGeneralSphereWeldedQuarterBox) {
  const double R = 3.0, V = 4.0 / 3.0 * ON_PI * R * R * R;
  const Brep sph = Brep::SphereWelded(Point3d(0, 0, 0), R);
  const Brep box = Brep::Box(0, 0, -5, 10, 10, 5);
  Run(sph, box, BooleanOp::Intersection, V / 4.0, "welded sphere x box");
  Run(sph, box, BooleanOp::Union, V + 1000.0 - V / 4.0, "welded sphere x box");
  Run(sph, box, BooleanOp::Difference, V - V / 4.0, "welded sphere - box");
  Run(box, sph, BooleanOp::Difference, 1000.0 - V / 4.0, "box - welded sphere");
}

D8_EXTRA_TEST(BooleanGeneralSphereWeldedCylinderThroughCentre) {
  const double R = 2.0, r = 1.0;
  const double V = 4.0 / 3.0 * ON_PI * R * R * R, Vc = ON_PI * r * r * 8.0;
  const double I = 4.0 / 3.0 * ON_PI * (R * R * R - std::pow(R * R - r * r, 1.5));
  const Brep sph = Brep::SphereWelded(Point3d(0, 0, 0), R);
  const Brep cyl = MakeCylinder(Point3d(0, 0, -4), dino8::kernel::Vector3d(0, 0, 1), r, 8.0);
  Run(sph, cyl, BooleanOp::Union, V + Vc - I, "welded sphere x cylinder");
  Run(sph, cyl, BooleanOp::Intersection, I, "welded sphere x cylinder");
}
