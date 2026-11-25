// Brep::Loft start_tangent / end_tangent for RATIONAL and closed-curve sections (circles, ellipses).
//
// The wall is S(u, v) = sum_i N_i(u) H_i(v) / sum_i N_i(u) w_i(v), H_i = w_i P_i. With the weight derivative pinned to 0
// at a constrained end and H_i'(end) = w_i d_i, dS/dv(u, end) = sum_i N_i w_i d_i / sum_i N_i w_i: the rational curve with
// the SECTION's weights and the tangent curve's control points read as vectors. Oracle used here (independent of the
// kernel's skinning): the tangent FIELD T(x, y, z) = (a x, a y, b) of the position S(u, end), i.e. a prescribed radial
// flare a and axial speed b, evaluated from the wall's own end point and compared with openNURBS' Ev1Der of the wall.
//
// Stations are fixed with Spacing::Uniform (v = 0, 1/2, 1 for three sections) so the sections can be checked by their
// implicit equations, never by a kernel parameter.
#include <cmath>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using Spacing = Brep::LoftSpacing;

// Horizontal circle (x scaled by sx: an ellipse with semi-axes sx*r, r) about the z axis at height z. normal_z = -1
// makes it run clockwise seen from +z, so the loft toward +z must reverse sections AND tangent curves.
NurbsCurve Section(double z, double r, double sx = 1.0, double normal_z = 1.0) {
  const ON_Circle circle(ON_Plane(Point3d(0, 0, z), Vector3d(0, 0, normal_z)), r);
  ON_NurbsCurve n;
  circle.GetNurbForm(n);
  if (sx != 1.0) n.Transform(ON_Xform::DiagonalTransformation(sx, 1.0, 1.0));
  NurbsCurve k;
  k.raw() = n;
  return k;
}

// The tangent field T = (a x, a y, b) as a curve over the same control structure as `sec`: control point i is the
// vector (a x_i, a y_i, b) built from the section's Euclidean control point, weights = the section's weights times
// `weight_scale`. As a function of u it is a * (section point) + b e_z exactly.
NurbsCurve FieldLike(const NurbsCurve& sec, double a, double b, double weight_scale = 1.0) {
  ON_NurbsCurve t = sec.raw();
  for (int i = 0; i < t.CVCount(); ++i) {
    ON_3dPoint p;
    t.GetCV(i, p);
    const double w = t.Weight(i) * weight_scale;
    const ON_3dPoint d(a * p.x, a * p.y, b);
    t.SetCV(i, ON_4dPoint(d.x * w, d.y * w, d.z * w, w));
  }
  NurbsCurve k;
  k.raw() = t;
  return k;
}

const ON_NurbsSurface* Wall(const Brep& b) { return ON_NurbsSurface::Cast(b.raw().m_F[0].SurfaceOf()); }

bool ThrowsWith(const std::function<void()>& f, const char* needle) {
  try {
    f();
  } catch (const std::invalid_argument& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

// Largest |dS/dv(u, v_end) - (a x, a y, b)| over a u grid, where (x, y) is the wall's own point at (u, v_end). Also
// reports the worst violation of the implicit section equation (x / sx)^2 + y^2 = r^2 and |z - z_end| at that end.
void CheckEnd(const ON_NurbsSurface& wall, double v_end, double a, double b, double sx, double r, double z, double* deriv_err,
              double* section_err) {
  const ON_Interval du = wall.Domain(0);
  for (int j = 0; j <= 64; ++j) {
    ON_3dPoint pt;
    ON_3dVector su, sv;
    if (!wall.Ev1Der(du.ParameterAt(j / 64.0), v_end, pt, su, sv)) {
      *deriv_err = 1e9;
      continue;
    }
    *deriv_err = std::max(*deriv_err, (sv - ON_3dVector(a * pt.x, a * pt.y, b)).Length());
    *section_err = std::max(*section_err, std::fabs(std::hypot(pt.x / sx, pt.y) - r));
    *section_err = std::max(*section_err, std::fabs(pt.z - z));
  }
}

// Interior section: the wall at v = 1/2 lies on the middle section's implicit curve.
double MiddleErr(const ON_NurbsSurface& wall, double sx, double r, double z) {
  const ON_Interval du = wall.Domain(0);
  double worst = 0.0;
  for (int j = 0; j <= 64; ++j) {
    const ON_3dPoint p = wall.PointAt(du.ParameterAt(j / 64.0), 0.5);
    worst = std::max(worst, std::fabs(std::hypot(p.x / sx, p.y) - r));
    worst = std::max(worst, std::fabs(p.z - z));
  }
  return worst;
}

void RunCircleOrEllipse(double sx, double normal_z, const char* tag) {
  // z = 0, 1, 3 with radii 1, 1.5, 2: a flaring bowl. Start field: flare 0.5, axial speed 1. End field: flare 0.7, speed 2.5.
  const double zs[3] = {0.0, 1.0, 3.0}, rs[3] = {1.0, 1.5, 2.0};
  std::vector<NurbsCurve> secs;
  for (int k = 0; k < 3; ++k) secs.push_back(Section(zs[k], rs[k], sx, normal_z));
  const NurbsCurve t0 = FieldLike(secs.front(), 0.5, 1.0);
  const NurbsCurve t1 = FieldLike(secs.back(), 0.7, 2.5);
  const std::string s(tag);
  Brep loft;
  bool built = true;
  try {
    loft = Brep::Loft(secs, 2, false, false, &t0, &t1, Spacing::Uniform);
  } catch (const std::exception& e) {
    built = false;
    std::fprintf(stderr, "loft threw: %s\n", e.what());
  }
  d8test::Check(built, (s + ": a tangent-constrained loft through rational closed-curve sections is accepted").c_str());
  if (!built) return;
  d8test::Check(loft.FaceCount() == 1 && loft.raw().IsValid(), (s + ": one valid face").c_str());
  const ON_NurbsSurface* wall = Wall(loft);
  d8test::Check(wall != nullptr && wall->IsRational(), (s + ": the wall is rational").c_str());
  if (!wall) return;
  d8test::Check(wall->CVCount(1) == 5, (s + ": 3 stations + 1 extra control row per constrained end = 5").c_str());
  double d0 = 0, s0 = 0, d1 = 0, s1 = 0;
  CheckEnd(*wall, 0.0, 0.5, 1.0, sx, rs[0], zs[0], &d0, &s0);
  CheckEnd(*wall, 1.0, 0.7, 2.5, sx, rs[2], zs[2], &d1, &s1);
  d8test::Check(s0 < 1e-12 && s1 < 1e-12, (s + ": the end sections are reproduced (implicit ellipse equation) to 1e-12").c_str());
  d8test::Check(MiddleErr(*wall, sx, rs[1], zs[1]) < 1e-12, (s + ": the middle section is reproduced at v = 1/2 to 1e-12").c_str());
  d8test::Check(d0 < 1e-9, (s + ": dS/dv(u, 0) equals the prescribed field (0.5 x, 0.5 y, 1) for every u to 1e-9").c_str());
  d8test::Check(d1 < 1e-9, (s + ": dS/dv(u, 1) equals the prescribed field (0.7 x, 0.7 y, 2.5) for every u to 1e-9").c_str());
}

}  // namespace

D8_EXTRA_TEST(LoftRationalTangentCircleSections) { RunCircleOrEllipse(1.0, 1.0, "circle sections"); }

D8_EXTRA_TEST(LoftRationalTangentEllipseSections) { RunCircleOrEllipse(2.0, 1.0, "ellipse sections (x semi-axis 2 r)"); }

D8_EXTRA_TEST(LoftRationalTangentClockwiseSectionsReverseTheTangentsToo) {
  RunCircleOrEllipse(1.0, -1.0, "clockwise circle sections (sections and tangents both reversed)");
}

D8_EXTRA_TEST(LoftRationalTangentOneEndAndConstantAndScaledWeights) {
  std::vector<NurbsCurve> secs;
  for (double z : {0.0, 1.0, 3.0}) secs.push_back(Section(z, 1.0 + 0.5 * z));
  // Start only: one extra row, end unconstrained.
  const NurbsCurve t0 = FieldLike(secs.front(), 0.25, 1.5);
  const Brep one = Brep::Loft(secs, 2, false, false, &t0, nullptr, Spacing::Uniform);
  const ON_NurbsSurface* w1 = Wall(one);
  d8test::Check(w1 != nullptr && w1->CVCount(1) == 4, "start-only tangent: 3 stations + 1 extra row = 4");
  double d = 0, e = 0;
  if (w1) CheckEnd(*w1, 0.0, 0.25, 1.5, 1.0, 1.0, 0.0, &d, &e);
  d8test::Check(d < 1e-9 && e < 1e-12, "start-only tangent matches exactly and keeps the section");

  // Weights a common multiple of the section's (x3): the same rational function, accepted, exact.
  const NurbsCurve t3 = FieldLike(secs.front(), 0.25, 1.5, 3.0);
  const Brep scaled = Brep::Loft(secs, 2, false, false, &t3, nullptr, Spacing::Uniform);
  d = 0;
  e = 0;
  if (const ON_NurbsSurface* w = Wall(scaled)) CheckEnd(*w, 0.0, 0.25, 1.5, 1.0, 1.0, 0.0, &d, &e);
  d8test::Check(d < 1e-9, "a tangent curve with weights 3x the section's is the same rational field and matches to 1e-9");

  // Constant field given as a plain NON-rational curve (weights 1, every control point (0, 0, 1)).
  ON_NurbsCurve c = secs.front().raw();
  c.MakeNonRational();
  for (int i = 0; i < c.CVCount(); ++i) c.SetCV(i, ON_3dPoint(0, 0, 1));
  NurbsCurve axial;
  axial.raw() = c;
  const Brep k = Brep::Loft(secs, 2, false, false, &axial, nullptr, Spacing::Uniform);
  d = 0;
  e = 0;
  if (const ON_NurbsSurface* w = Wall(k)) CheckEnd(*w, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, &d, &e);
  d8test::Check(d < 1e-9 && e < 1e-12, "a constant axial field (0, 0, 1) given as a non-rational curve is matched exactly");
}

D8_EXTRA_TEST(LoftRationalTangentCappedSolidAndNegativeControls) {
  std::vector<NurbsCurve> secs;
  for (double z : {0.0, 1.0, 3.0}) secs.push_back(Section(z, 1.0 + 0.5 * z));
  const NurbsCurve t0 = FieldLike(secs.front(), 0.5, 1.0);
  const NurbsCurve t1 = FieldLike(secs.back(), 0.7, 2.5);
  // Closed planar sections with cap=true: three faces, a closed oriented solid (the caps are planar trimmed faces).
  const Brep solid = Brep::Loft(secs, 2, false, true, &t0, &t1, Spacing::Uniform);
  d8test::Check(solid.FaceCount() == 3 && solid.raw().IsValid() && solid.raw().IsSolid(),
                "tangent-constrained loft through closed sections with cap=true is a valid closed solid of 3 faces");
  d8test::Check(solid.Volume() > 0.0, "...with positive volume (outward orientation)");

  // Negative controls, each with its stated reason.
  ON_NurbsCurve plain = FieldLike(secs.front(), 0.5, 1.0).raw();
  plain.MakeNonRational();  // weights 1, control points NOT constant: not representable against the circle's weights
  NurbsCurve plain_curve;
  plain_curve.raw() = plain;
  d8test::Check(ThrowsWith([&] { Brep::Loft(secs, 2, false, false, &plain_curve, nullptr, Spacing::Uniform); }, "cannot be matched exactly"),
                "a varying non-rational tangent curve against a rational section is refused with the weights reason");
  const NurbsCurve open_tangent = NurbsCurve::FromControlPoints({Point3d(0, 0, 1), Point3d(0, 1, 1)}, 1);
  d8test::Check(ThrowsWith([&] { Brep::Loft(secs, 2, false, false, &open_tangent, nullptr, Spacing::Uniform); }, "closed"),
                "an OPEN tangent curve for closed-curve sections is refused (it must be closed)");
  d8test::Check(ThrowsWith([&] { Brep::Loft(secs, 2, true, false, &t0, &t1, Spacing::Uniform); }, "closed (periodic) loft"),
                "tangents on a closed (periodic) loft are still refused");
  d8test::Check(ThrowsWith([&] { Brep::Loft(secs, 1, false, false, &t0, nullptr, Spacing::Uniform); }, "degree >= 2"),
                "degree 1 is still refused");
}
