// Sealed general Booleans whose sewn result keeps a naked edge SHORTER than the sew tolerance.
//
// BooleanCombineGeneralSealed() sews the engine's result with SewTJunctions + JoinNakedEdges, which weld naked edges
// that have a PARTNER within the tolerance. Parallel-axis cylinders (union / intersection / difference) and a cylinder
// pierced by a box leave one or two naked edges that are themselves shorter than the tolerance (0.0004 .. 0.0126 on
// these bodies) with no partner: the result was refused as "not a closed solid". SealGeneralBooleanResult() now
// collapses such a sliver edge (deletes its trims, merges its two vertices, snaps the neighbouring 2D trim ends).
//
// Every expected number is a closed form or a Simpson quadrature computed here, not by the kernel:
//   * two r = 1 cylinders, axes d = 1 apart, z-overlap h: intersection = h * lens(d), lens = 2 acos(d/2) - (d/2) sqrt(4 - d^2)
//     (cross-checked against a Simpson quadrature of the overlap chord);
//   * a cylinder with a rectangular tunnel: chord-length quadrature in the tunnel's cross-section.
// The faces' caps are 128-gons (inscribed), so volumes are compared to ~1% (the same order as the existing
// sealed_boolean_tests, whose caps are the same 128-gons).
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::SealedGeneralBoolean;

ON_Plane FrameFromAxis(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  ON_3dVector seed = std::fabs(axis.z) < 0.9 ? ON_3dVector(0, 0, 1) : ON_3dVector(1, 0, 0);
  ON_3dVector x = ON_CrossProduct(seed, axis);
  x.Unitize();
  ON_3dVector y = ON_CrossProduct(axis, x);
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
  const ON_3dPoint c = frame.origin + frame.zaxis * h;
  pf.plane = ON_Plane(c, flip ? -frame.zaxis : frame.zaxis);
  for (int i = 0; i < n; ++i) {
    const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
    pf.loop.push_back(c + frame.xaxis * (r * std::cos(a)) + frame.yaxis * (r * std::sin(a)));
  }
  pf.notch_begin = 0;
  pf.notch_count = static_cast<int>(pf.loop.size());
  return pf;
}

Brep Cylinder(const ON_3dPoint& base, const ON_3dVector& axis, double r, double length) {
  Brep::CylindricalFace cf;
  cf.frame = FrameFromAxis(base, axis);
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({DiskCap(cf.frame, 0.0, r, true), DiskCap(cf.frame, length, r, false)}, {cf});
}

double Simpson(const std::function<double(double)>& f, double a, double b, int n = 4000) {
  const double h = (b - a) / n;
  double s = f(a) + f(b);
  for (int i = 1; i < n; ++i) s += f(a + i * h) * (i % 2 ? 4.0 : 2.0);
  return s * h / 3.0;
}

// Area of the intersection of two unit discs whose centres are d apart: the closed form, and a Simpson quadrature of
// the chord of the overlap (the two agree; both are used so neither is trusted alone).
double LensClosedForm(double d) { return 2.0 * std::acos(d / 2.0) - (d / 2.0) * std::sqrt(4.0 - d * d); }
double LensQuadrature(double d) {
  // x in [d - 1, 1]; the chord at x is 2 min(sqrt(1 - x^2), sqrt(1 - (x - d)^2)).
  return Simpson([&](double x) { return 2.0 * std::min(std::sqrt(std::max(0.0, 1.0 - x * x)), std::sqrt(std::max(0.0, 1.0 - (x - d) * (x - d)))); },
                 d - 1.0, 1.0, 20000);
}

struct Outcome {
  bool sealed = false;
  std::string reason;
  SealedGeneralBoolean s;
};

Outcome Run(const Brep& a, const Brep& b, BooleanOp op) {
  Outcome o;
  try {
    o.s = BooleanCombineGeneralSealed(a, b, op);
    o.sealed = true;
  } catch (const std::exception& e) {
    o.reason = e.what();
  }
  return o;
}

// IsValid + IsSolid + exact volume against `expect` (relative tolerance `rel`). A refusal is a failed check that names its reason.
void CheckSealed(const Outcome& o, const char* label, double expect, double rel, int expect_faces = -1) {
  const std::string l(label);
  d8test::Check(o.sealed, (l + ": seals (no refusal" + (o.sealed ? "" : "; refused: " + o.reason) + ")").c_str());
  if (!o.sealed) return;
  d8test::Check(o.s.brep.raw().IsValid(), (l + ": ON_Brep::IsValid()").c_str());
  d8test::Check(o.s.brep.raw().IsSolid(), (l + ": ON_Brep::IsSolid()").c_str());
  d8test::Check(std::fabs(o.s.volume - o.s.brep.VolumeOfSealedSolid()) < 1e-9, (l + ": the reported volume is VolumeOfSealedSolid()").c_str());
  const double err = std::fabs(o.s.volume - expect) / expect;
  d8test::Check(err < rel, (l + ": exact B-rep volume " + std::to_string(o.s.volume) + " matches the closed form " + std::to_string(expect) +
                            " (rel err " + std::to_string(err) + ")")
                               .c_str());
  if (expect_faces >= 0) d8test::Check(o.s.brep.FaceCount() == expect_faces, (l + ": face count " + std::to_string(o.s.brep.FaceCount()) +
                                                                                " == " + std::to_string(expect_faces)).c_str());
}

}  // namespace

// The lens closed form and the quadrature are the same number (guards the oracle itself).
D8_EXTRA_TEST(SealedSliversLensOracleAgrees) {
  d8test::Check(std::fabs(LensClosedForm(1.0) - LensQuadrature(1.0)) < 1e-6, "lens area d = 1: closed form 2 pi/3 - sqrt(3)/2 equals the Simpson quadrature");
  d8test::Check(std::fabs(LensClosedForm(1.0) - (2.0 * ON_PI / 3.0 - std::sqrt(3.0) / 2.0)) < 1e-12, "lens area d = 1 is 2 pi/3 - sqrt(3)/2");
}

// Two r = 1 cylinders, axes 1 apart (parallel to z). A: z in [0, 4]; B: z in [1, 5]  (z-overlap h = 3).
D8_EXTRA_TEST(SealedSliversParallelCylindersUnion) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep b = Cylinder(ON_3dPoint(1, 0, 1), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const double vi = 3.0 * LensQuadrature(1.0);
  CheckSealed(Run(a, b, BooleanOp::Union), "parallel cylinders (axes 1 apart) Union", 2.0 * ON_PI * 4.0 - vi, 0.012);
}

D8_EXTRA_TEST(SealedSliversParallelCylindersIntersection) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep b = Cylinder(ON_3dPoint(1, 0, 1), ON_3dVector(0, 0, 1), 1.0, 4.0);
  CheckSealed(Run(a, b, BooleanOp::Intersection), "parallel cylinders (axes 1 apart) Intersection", 3.0 * LensQuadrature(1.0), 0.012);
}

D8_EXTRA_TEST(SealedSliversParallelCylindersDifference) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep b = Cylinder(ON_3dPoint(1, 0, 1), ON_3dVector(0, 0, 1), 1.0, 4.0);
  CheckSealed(Run(a, b, BooleanOp::Difference), "parallel cylinders (axes 1 apart) Difference A-B", ON_PI * 4.0 - 3.0 * LensQuadrature(1.0), 0.012);
}

// Same pair with both ends flush (z in [0, 4] for both): the two cap planes coincide.
D8_EXTRA_TEST(SealedSliversParallelCylindersFlushUnion) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep b = Cylinder(ON_3dPoint(1, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  CheckSealed(Run(a, b, BooleanOp::Union), "flush parallel cylinders Union", 4.0 * (2.0 * ON_PI - LensQuadrature(1.0)), 0.012);
}

D8_EXTRA_TEST(SealedSliversParallelCylindersFlushIntersection) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep b = Cylinder(ON_3dPoint(1, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  CheckSealed(Run(a, b, BooleanOp::Intersection), "flush parallel cylinders Intersection", 4.0 * LensQuadrature(1.0), 0.012);
}

D8_EXTRA_TEST(SealedSliversParallelCylindersFlushDifference) {
  const Brep a = Cylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep b = Cylinder(ON_3dPoint(1, 0, 0), ON_3dVector(0, 0, 1), 1.0, 4.0);
  CheckSealed(Run(a, b, BooleanOp::Difference), "flush parallel cylinders Difference A-B", 4.0 * (ON_PI - LensQuadrature(1.0)), 0.012);
}

// Cylinder (r = 1, axis z, z in [-2, 2]) minus a box that goes THROUGH it: x in [-2, 2] (past both sides of the wall),
// |y| < 0.5, |z| < 0.5 (inside the cylinder's length): a rectangular tunnel.
// Removed volume = 1 (z-width) * Int_{-0.5}^{0.5} 2 sqrt(1 - y^2) dy.
D8_EXTRA_TEST(SealedSliversCylinderMinusThroughBox) {
  const Brep cyl = Cylinder(ON_3dPoint(0, 0, -2), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep box = Brep::Box(-2, -0.5, -0.5, 2, 0.5, 0.5);
  const double tunnel = 1.0 * Simpson([](double y) { return 2.0 * std::sqrt(1.0 - y * y); }, -0.5, 0.5);
  // The closed form of that integral: y sqrt(1 - y^2) + asin(y) between -0.5 and 0.5 = sqrt(3)/2 + pi/3.
  d8test::Check(std::fabs(tunnel - (std::sqrt(3.0) / 2.0 + ON_PI / 3.0)) < 1e-9, "tunnel volume: Simpson equals sqrt(3)/2 + pi/3");
  CheckSealed(Run(cyl, box, BooleanOp::Difference), "cylinder minus a through box", ON_PI * 4.0 - tunnel, 0.012);
}

// The same body by Intersection: the part of the cylinder inside the through box.
D8_EXTRA_TEST(SealedSliversCylinderIntersectThroughBox) {
  const Brep cyl = Cylinder(ON_3dPoint(0, 0, -2), ON_3dVector(0, 0, 1), 1.0, 4.0);
  const Brep box = Brep::Box(-2, -0.5, -0.5, 2, 0.5, 0.5);
  const double tunnel = std::sqrt(3.0) / 2.0 + ON_PI / 3.0;
  CheckSealed(Run(cyl, box, BooleanOp::Intersection), "cylinder intersect a through box", tunnel, 0.012);
}
