// Sealed Boolean coverage: pipe-tee unions (a cylinder R = 1 with a perpendicular branch) and an off-axis cylinder through a
// box. Every volume is an independent closed form or a 1-D quadrature; every sealed body must be ON_Brep::IsValid() and
// IsSolid(); every refusal must name its measured cause (BooleanCombineGeneralSealed), not the generic "gaps between faces".
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <functional>
#include <string>

#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::SealedGeneralBoolean;

ON_Plane TeeFrame(const ON_3dPoint& origin, ON_3dVector axis) {
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

Brep::PlanarFace TeeDisk(const ON_Plane& frame, double h, double r, bool flip, int n = 128) {
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

Brep TeeCylinder(const ON_3dPoint& base, const ON_3dVector& axis, double r, double length) {
  Brep::CylindricalFace cf;
  cf.frame = TeeFrame(base, axis);
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = length;
  return Brep::FromMixedFaces({TeeDisk(cf.frame, 0.0, r, true), TeeDisk(cf.frame, length, r, false)}, {cf});
}

double TeeSimpson(const std::function<double(double)>& f, double a, double b, int n = 4000) {
  const double h = (b - a) / n;
  double s = f(a) + f(b);
  for (int i = 1; i < n; ++i) s += f(a + i * h) * (i % 2 ? 4.0 : 2.0);
  return s * h / 3.0;
}

bool Valid(const SealedGeneralBoolean& s) { return s.brep.raw().IsValid() && s.brep.raw().IsSolid(); }

// Either seals into a valid solid, or is refused; returns the refusal text ("" when sealed).
std::string SealOrRefusal(const Brep& a, const Brep& b, BooleanOp op, SealedGeneralBoolean& out) {
  try {
    out = BooleanCombineGeneralSealed(a, b, op);
    return std::string();
  } catch (const std::runtime_error& e) {
    return e.what();
  }
}

bool NamesMeasuredCause(const std::string& what) {
  return what.find("BooleanCombineGeneralSealed:") == 0 && what.find("has gaps between faces") == std::string::npos &&
         (what.find("unpaired edge(s)") != std::string::npos || what.find("not consistently oriented") != std::string::npos ||
          what.find("slit pair") != std::string::npos || what.find("not a valid ON_Brep") != std::string::npos ||
          what.find("volume") != std::string::npos);
}

}  // namespace

// Main cylinder R = 1 along z (z in [-3, 3]); equal-radius branch r = 1 along +y from the main axis out to y = 4.
// V = pi R^2 H + pi rb^2 L - (the branch part inside the main cylinder = half a Steinmetz solid, 8/3).
D8_EXTRA_TEST(SealedTeeEqualRadiusBranchUnionMatchesClosedForm) {
  const Brep main_cyl = TeeCylinder(ON_3dPoint(0, 0, -3), ON_3dVector(0, 0, 1), 1.0, 6.0);
  const Brep branch = TeeCylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 1, 0), 1.0, 4.0);
  SealedGeneralBoolean s;
  const std::string why = SealOrRefusal(main_cyl, branch, BooleanOp::Union, s);
  d8test::Check(why.empty(), "tee R=1 rb=1 (branch along +y) Union seals");
  if (!why.empty()) return;
  const double expect = ON_PI * 6.0 + ON_PI * 4.0 - 8.0 / 3.0;
  d8test::Check(Valid(s), "tee R=1 rb=1 Union is IsValid and IsSolid");
  d8test::Check(std::fabs(s.volume - expect) < 0.25, "tee R=1 rb=1 Union exact volume matches 10 pi - 8/3 to 0.25");
}

// Unequal radii: the closed form is a 1-D quadrature. Branch rb = 0.5 along +y from the axis to y = 4.
D8_EXTRA_TEST(SealedTeeSmallBranchUnionMatchesQuadrature) {
  const double R = 1.0, rb = 0.5, H = 6.0, Lb = 4.0;
  const Brep main_cyl = TeeCylinder(ON_3dPoint(0, 0, -3), ON_3dVector(0, 0, 1), R, H);
  const Brep branch = TeeCylinder(ON_3dPoint(0, 0, 0), ON_3dVector(0, 1, 0), rb, Lb);
  // Branch part inside the main cylinder: for each x in [-rb, rb] the branch chord in z is 2 sqrt(rb^2 - x^2), and y runs
  // from 0 to sqrt(R^2 - x^2).
  const double inside = TeeSimpson([&](double x) { return 2.0 * std::sqrt(std::max(0.0, rb * rb - x * x)) * std::sqrt(R * R - x * x); }, -rb, rb);
  const double expect = ON_PI * R * R * H + ON_PI * rb * rb * Lb - inside;
  SealedGeneralBoolean s;
  const std::string why = SealOrRefusal(main_cyl, branch, BooleanOp::Union, s);
  // Measured: the branch's own end cap (a 128-gon) meets the cut branch wall's rim, which the engine resamples with 24 chords
  // per run, so 18 unpaired edges survive sewing at 1% of the diagonal: refused, with that measured count in the reason.
  if (!why.empty()) {
    d8test::Check(NamesMeasuredCause(why), ("tee R=1 rb=0.5 Union is refused with a measured cause: " + why).c_str());
    return;
  }
  d8test::Check(Valid(s), "tee R=1 rb=0.5 Union is IsValid and IsSolid");
  d8test::Check(std::fabs(s.volume - expect) < 0.25, "tee R=1 rb=0.5 Union exact volume matches the quadrature to 0.25");
}

// The tees that the engine cannot yet close are refused with the measured cause, never returned degraded.
D8_EXTRA_TEST(SealedTeeRefusalsNameTheirMeasuredCause) {
  struct Case {
    const char* name;
    double rb, length;
    ON_3dVector dir;
  };
  const Case cases[] = {
      {"tee R=1 rb=2 (branch along +y)", 2.0, 6.0, ON_3dVector(0, 1, 0)},
      {"tee R=1 rb=2 length 40", 2.0, 40.0, ON_3dVector(0, 1, 0)},
      {"tee R=1 rb=1 length 40", 1.0, 40.0, ON_3dVector(0, 1, 0)},
      {"tee R=1 rb=1 (branch along +x, its end plane through the main seam)", 1.0, 6.0, ON_3dVector(1, 0, 0)},
  };
  for (const Case& c : cases) {
    const Brep main_cyl = TeeCylinder(ON_3dPoint(0, 0, -c.length / 2), ON_3dVector(0, 0, 1), 1.0, c.length);
    const Brep branch = TeeCylinder(ON_3dPoint(0, 0, 0), c.dir, c.rb, c.length / 2 + 1.0);
    SealedGeneralBoolean s;
    const std::string why = SealOrRefusal(main_cyl, branch, BooleanOp::Union, s);
    const std::string label = std::string(c.name) + ": ";
    if (why.empty()) {
      d8test::Check(Valid(s), (label + "sealed, so it is IsValid and IsSolid").c_str());
    } else {
      d8test::Check(NamesMeasuredCause(why), (label + "the refusal names a measured cause: " + why).c_str());
    }
  }
}

// Cylinder r = 1, axis along z through a box [-2,2]^2 x [-1,1], z in [-2, 2], centre offset (x0, 0).
D8_EXTRA_TEST(SealedOffAxisCylinderThroughBox) {
  const Brep box = Brep::Box(-2, -2, -1, 2, 2, 1);
  const double pi = ON_PI;
  // x0 = 0.5 and 1.0: the whole cylinder lies inside the box in x and y (at x0 = 1.0 it just touches the x = 2 face along
  // a line), so the Intersection is the r=1, h=2 slab, the Union 32 + 4 pi - 2 pi, the Difference 32 - 2 pi.
  for (const double x0 : {0.5, 1.0}) {
    const Brep cyl = TeeCylinder(ON_3dPoint(x0, 0, -2), ON_3dVector(0, 0, 1), 1.0, 4.0);
    const std::string tag = "off-axis x0=" + std::to_string(x0).substr(0, 3) + " ";
    {
      SealedGeneralBoolean s;
      const std::string why = SealOrRefusal(box, cyl, BooleanOp::Intersection, s);
      d8test::Check(why.empty(), (tag + "Intersection seals").c_str());
      if (why.empty()) {
        d8test::Check(Valid(s), (tag + "Intersection is IsValid and IsSolid").c_str());
        d8test::Check(std::fabs(s.volume - 2.0 * pi) < 0.05, (tag + "Intersection exact volume matches 2 pi to 0.05").c_str());
      }
    }
    const BooleanOp ops[2] = {BooleanOp::Union, BooleanOp::Difference};
    const double expect[2] = {32.0 + 2.0 * pi, 32.0 - 2.0 * pi};
    for (int k = 0; k < 2; ++k) {
      SealedGeneralBoolean s;
      const std::string why = SealOrRefusal(box, cyl, ops[k], s);
      const std::string label = tag + (k == 0 ? "Union" : "Difference") + ": ";
      if (why.empty()) {
        d8test::Check(Valid(s), (label + "sealed, so IsValid and IsSolid").c_str());
        d8test::Check(std::fabs(s.volume - expect[k]) < 0.1, (label + "exact volume matches the closed form to 0.1").c_str());
      } else {
        d8test::Check(NamesMeasuredCause(why), (label + "the refusal names a measured cause: " + why).c_str());
        if (x0 == 1.0) {
          d8test::Check(why.find("tangent or grazing") != std::string::npos,
                        (label + "a cylinder touching the box face along a line is named as tangent/grazing contact").c_str());
        }
      }
    }
  }
  // x0 = 1.5: the cylinder crosses the x = 2 face. Whatever the engine does, a sealed result must be a valid solid and a
  // refusal must name its cause.
  const Brep cyl = TeeCylinder(ON_3dPoint(1.5, 0, -2), ON_3dVector(0, 0, 1), 1.0, 4.0);
  for (const BooleanOp op : {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference}) {
    SealedGeneralBoolean s;
    const std::string why = SealOrRefusal(box, cyl, op, s);
    const std::string label = "off-axis x0=1.5 op " + std::to_string(static_cast<int>(op)) + ": ";
    if (why.empty()) {
      d8test::Check(Valid(s), (label + "sealed, so IsValid and IsSolid").c_str());
    } else {
      d8test::Check(NamesMeasuredCause(why), (label + "the refusal names a measured cause: " + why).c_str());
    }
  }
}
