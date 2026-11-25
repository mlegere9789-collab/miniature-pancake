// Regressions for operand pairs of the golden/evil Boolean corpus that the general / planar engines used to refuse or get wrong.
// Each case is built exactly like the corpus runner builds it (ON_BrepCylinder / ON_BrepTorus / ON_BrepSphere / ON_BrepBox with the same
// frame, so the seam sits where the corpus puts it); expected values are the corpus's closed forms.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanCombinePlanar;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;

ON_Plane FrameFor(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  const ON_3dVector seed = std::fabs(axis.x) < 0.9 ? ON_3dVector(1, 0, 0) : ON_3dVector(0, 1, 0);
  ON_3dVector x = seed - (seed * axis) * axis;
  x.Unitize();
  return ON_Plane(origin, x, ON_CrossProduct(axis, x));
}

Brep Wrap(ON_Brep* b) {
  Brep k;
  k.raw() = *b;
  delete b;
  if (k.raw().IsSolid() && k.VolumeOfSealedSolid() < 0) k.raw().Flip();  // outward normals, as the corpus runner does
  return k;
}

Brep Cylinder(const ON_3dPoint& base, double r, double h) {
  ON_Cylinder c(ON_Circle(FrameFor(base, ON_3dVector(0, 0, 1)), r), h);
  return Wrap(ON_BrepCylinder(c, true, true));
}

Brep Box(double x0, double y0, double z0, double x1, double y1, double z1) {
  const ON_3dPoint c[8] = {ON_3dPoint(x0, y0, z0), ON_3dPoint(x1, y0, z0), ON_3dPoint(x1, y1, z0), ON_3dPoint(x0, y1, z0),
                           ON_3dPoint(x0, y0, z1), ON_3dPoint(x1, y0, z1), ON_3dPoint(x1, y1, z1), ON_3dPoint(x0, y1, z1)};
  return Wrap(ON_BrepBox(c));
}

Brep GenericSphere(double r) {
  ON_Sphere s(ON_3dPoint(0, 0, 0), r);
  s.plane = FrameFor(ON_3dPoint(0, 0, 0), ON_3dVector(0.2857142857142857, 0.42857142857142855, 0.8571428571428571));
  return Wrap(ON_BrepSphere(s));
}

Brep Torus(double major, double minor) {
  ON_Torus t(FrameFor(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), major, minor);
  return Wrap(ON_BrepTorus(t));
}

bool RelNear(double got, double want, double rel) { return std::fabs(got - want) <= rel * std::fabs(want); }

}  // namespace

// Two parallel unit cylinders, axes 1 apart, z in [0, 4] and [1, 5]: their intersection curve is two straight rulings joined by arcs, and the
// ruling pair crosses the first cylinder's seam. The notch cut into the wall (a chain whose samples jump one period at the seam) used to
// be spliced as one polygon: the wall and the notch swapped boundaries and "an edge is claimed by 3 or more fragment loops" refused the
// call. Lens area 2 acos(1/2) - sqrt(3)/2 times the overlap height 3.
D8_EXTRA_TEST(LoopFixParallelCylindersIntersectionKeepsSeamNotch) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), 1.0, 4.0), b = Cylinder(ON_3dPoint(1, 0, 1), 1.0, 4.0);
  try {
    const auto s = BooleanCombineGeneralSealed(a, b, BooleanOp::Intersection);
    const double want = (2.0 * std::acos(0.5) - 0.5 * std::sqrt(3.0)) * 3.0;
    d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), "parallel cylinders A*B: a valid closed solid");
    d8test::Check(RelNear(s.brep.VolumeOfSealedSolid(), want, 0.02), "parallel cylinders A*B: volume = lens area x 3 = 3.6851");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("parallel cylinders A*B threw: ") + e.what()).c_str());
  }
}

// A torus cut by the box [-10,10] x [0,10] x [-10,10]: the box face y = 0 contains the torus axis, so the torus seam meridian circle (+x)
// is the face's own seam edge. That 3D-closed full sweep along the seam used to be cut into the face as an open chain (22 zero-width
// fragments, then the non-manifold refusal). Half of the torus: pi r^2 x 2 pi R / 2.
D8_EXTRA_TEST(LoopFixTorusHalfIntersectionSeamCircleIsBoundary) {
  const Brep torus = Torus(3.0, 1.0), box = Box(-10, 0, -10, 10, 10, 10);
  try {
    const auto s = BooleanCombineGeneralSealed(torus, box, BooleanOp::Intersection);
    const double want = ON_PI * 1.0 * ON_PI * 3.0;  // pi r^2 x pi R
    d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), "torus half A*B: a valid closed solid");
    d8test::Check(RelNear(s.brep.VolumeOfSealedSolid(), want, 1e-3), "torus half A*B: volume = pi^2 r^2 R = 29.6088");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("torus half A*B threw: ") + e.what()).c_str());
  }
}

// The same torus and box as B - A: the box with the half torus bored out of its y = 0 face. The torus face's own seam circle must be
// spliced into the boundary so the box face's hole and the torus fragment share ONE polyline (it used to leave the loops unreconciled and
// the sewn body inconsistently oriented).
D8_EXTRA_TEST(LoopFixTorusHalfDifferenceSharesSeamPolyline) {
  const Brep torus = Torus(3.0, 1.0), box = Box(-10, 0, -10, 10, 10, 10);
  try {
    const auto s = BooleanCombineGeneralSealed(box, torus, BooleanOp::Difference);
    const double want = 20.0 * 10.0 * 20.0 - ON_PI * ON_PI * 3.0;
    d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), "box - torus: a valid closed solid");
    d8test::Check(RelNear(s.brep.VolumeOfSealedSolid(), want, 1e-3), "box - torus: volume = 4000 - pi^2 r^2 R = 3970.39");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("box - torus threw: ") + e.what()).c_str());
  }
}

// A 2e-3 x 2e-3 x 4 bar through the centre of a unit sphere: the default 1e-3 tolerance is as large as the bar, so the bar's faces
// collapsed (sphere * bar came back at 2/3 of its volume; sphere - bar as two shells). The tolerance is now capped at 1/200 of the
// thinnest operand extent. Sphere * bar = (2w)^2 x 2 x mean(sqrt(1 - ...)) ~ 8e-6; sphere - bar = 4 pi / 3 - that, one shell of genus 1.
D8_EXTRA_TEST(LoopFixSliverBarThroughSphereKeepsEveryFace) {
  const Brep sphere = GenericSphere(1.0), bar = Box(-2, -0.001, -0.001, 2, 0.001, 0.001);
  try {
    const auto i = BooleanCombineGeneralSealed(sphere, bar, BooleanOp::Intersection);
    d8test::Check(i.brep.raw().IsSolid(), "sphere * sliver bar: a closed solid");
    d8test::Check(RelNear(i.brep.VolumeOfSealedSolid(), 7.999997333332711e-06, 0.02), "sphere * sliver bar: volume 8.0e-6 within 2%");
    const auto d = BooleanCombineGeneralSealed(sphere, bar, BooleanOp::Difference);
    d8test::Check(d.brep.raw().IsSolid(), "sphere - sliver bar: a closed solid");
    d8test::Check(RelNear(d.brep.VolumeOfSealedSolid(), 4.1887822047890575, 0.02), "sphere - sliver bar: volume 4.18878 within 2%");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("sphere / sliver bar threw: ") + e.what()).c_str());
  }
}

// Two cubes of side 2 overlapping in a unit cube at one corner, a billion units from the origin: the planar engine welds and compares with
// absolute tolerances, and doubles a billion out are 1.2e-7 apart, so the union came back an open shell. The planar engine now combines in
// a frame centred on the operands (as the general engine already did) and moves the result back. Volume 8 + 8 - 1 = 15.
D8_EXTRA_TEST(LoopFixPlanarUnionFarFromOriginIsClosed) {
  const double o = 1.0e9;
  const Brep a = Box(o, o, o, o + 2, o + 2, o + 2), b = Box(o + 1, o + 1, o + 1, o + 3, o + 3, o + 3);
  try {
    const Brep u = BooleanCombinePlanar(a, b, BooleanOp::Union);
    d8test::Check(u.raw().IsValid() && u.raw().IsSolid(), "far cubes A+B: a valid closed solid");
    d8test::Check(RelNear(u.VolumeOfSealedSolid(), 15.0, 1e-5), "far cubes A+B: volume 15");
    double lo = 1e300, hi = -1e300;  // vertex extent (the surface boxes carry a tolerance pad)
    for (int vi = 0; vi < u.raw().m_V.Count(); ++vi) {
      lo = std::min(lo, u.raw().m_V[vi].point.x);
      hi = std::max(hi, u.raw().m_V[vi].point.z);
    }
    d8test::Check(std::fabs(lo - o) < 1e-6 && std::fabs(hi - (o + 3)) < 1e-6, "far cubes A+B: the result is back at its own position");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("far cubes A+B threw: ") + e.what()).c_str());
  }
}
