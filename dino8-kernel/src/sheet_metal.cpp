#include "dino8/kernel/sheet_metal.h"

#include <cmath>
#include <stdexcept>

#include "dino8/kernel/curve.h"

namespace dino8::kernel {

namespace {

// A circular arc of `radius`, swept `phi` radians from angle 0 (the point
// `center + radius*xaxis`) toward `yaxis`, as a NurbsCurve - the same
// ON_Arc/GetNurbForm construction NurbsCurve::FilletCornerArc() (curve.cpp)
// already uses to turn a plane/radius/angle into a real NURBS arc, just
// from an already-known center/frame/angle here rather than one derived
// from a corner point.
NurbsCurve ArcCurve(Point3d center, Vector3d xaxis, Vector3d yaxis, double radius, double phi) {
  const ON_Plane plane(center, xaxis, yaxis);
  const ON_Arc arc(plane, radius, phi);
  ON_NurbsCurve arc_nurbs;
  if (arc.GetNurbForm(arc_nurbs) == 0) {
    throw std::runtime_error(
        "dino8::kernel::Bend: ON_Arc::GetNurbForm failed building the bend arc (please report this as a bug)");
  }
  NurbsCurve result;
  result.raw() = arc_nurbs;
  return result;
}

}  // namespace

double BendAllowance(double thickness, double inside_radius, double bend_angle_degrees, double k_factor) {
  if (!(thickness > 0.0)) {
    throw std::invalid_argument("dino8::kernel::BendAllowance: thickness must be positive");
  }
  if (!(inside_radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::BendAllowance: inside_radius must be positive");
  }
  if (!(bend_angle_degrees > 0.0) || !(bend_angle_degrees < 180.0)) {
    throw std::invalid_argument("dino8::kernel::BendAllowance: bend_angle_degrees must be in (0, 180)");
  }
  if (k_factor < 0.0 || k_factor > 1.0) {
    throw std::invalid_argument("dino8::kernel::BendAllowance: k_factor must be in [0, 1]");
  }
  const double angle = bend_angle_degrees * ON_PI / 180.0;
  return angle * (inside_radius + k_factor * thickness);
}

Brep Bend(double leg1_length, double leg2_length, double width, double thickness, double inside_radius,
          double bend_angle_degrees) {
  if (!(leg1_length > 0.0)) throw std::invalid_argument("dino8::kernel::Bend: leg1_length must be positive");
  if (!(leg2_length > 0.0)) throw std::invalid_argument("dino8::kernel::Bend: leg2_length must be positive");
  if (!(width > 0.0)) throw std::invalid_argument("dino8::kernel::Bend: width must be positive");
  if (!(thickness > 0.0)) throw std::invalid_argument("dino8::kernel::Bend: thickness must be positive");
  if (!(inside_radius > 0.0)) throw std::invalid_argument("dino8::kernel::Bend: inside_radius must be positive");
  if (!(bend_angle_degrees > 0.0) || !(bend_angle_degrees < 180.0)) {
    throw std::invalid_argument("dino8::kernel::Bend: bend_angle_degrees must be in (0, 180)");
  }

  const double theta = bend_angle_degrees * ON_PI / 180.0;
  const Point3d O(0, 0, 0);
  const Vector3d xaxis(1, 0, 0), yaxis(0, 1, 0), zaxis(0, 0, 1);
  // Tangent direction at angle 0 / theta (derivative of the arc's own
  // position w.r.t. angle, pointing toward increasing angle). Leg 1
  // extends AWAY from the arc at angle 0 (opposite that direction); leg 2
  // continues FORWARD from the arc at angle theta (along that direction).
  const Vector3d t0 = -yaxis;
  const Vector3d t1 = -std::sin(theta) * xaxis + std::cos(theta) * yaxis;

  const double r_in = inside_radius;
  const double r_out = inside_radius + thickness;

  auto tangent_point = [&](double radius, double angle) {
    return O + radius * (std::cos(angle) * xaxis + std::sin(angle) * yaxis);
  };

  const Point3d outer_leg1_tan = tangent_point(r_out, 0.0);
  const Point3d outer_leg1_far = outer_leg1_tan + t0 * leg1_length;
  const Point3d outer_leg2_tan = tangent_point(r_out, theta);
  const Point3d outer_leg2_far = outer_leg2_tan + t1 * leg2_length;

  const Point3d inner_leg1_tan = tangent_point(r_in, 0.0);
  const Point3d inner_leg1_far = inner_leg1_tan + t0 * leg1_length;
  const Point3d inner_leg2_tan = tangent_point(r_in, theta);
  const Point3d inner_leg2_far = inner_leg2_tan + t1 * leg2_length;

  // Assemble the closed profile going all the way around once: outer
  // leg 1 -> outer arc -> outer leg 2 -> end cap at the leg-2 end ->
  // inner leg 2 (reversed) -> inner arc (reversed) -> inner leg 1
  // (reversed) -> end cap at the leg-1 end, back to the start.
  // NurbsCurve::Join() (curve.h) auto-reverses whichever end of the next
  // segment actually meets the curve built so far, so each segment below
  // is built in its own natural direction rather than pre-reversed here.
  NurbsCurve profile = NurbsCurve::FromControlPoints({outer_leg1_far, outer_leg1_tan}, 1);
  const NurbsCurve outer_arc = ArcCurve(O, xaxis, yaxis, r_out, theta);
  const NurbsCurve outer_leg2 = NurbsCurve::FromControlPoints({outer_leg2_tan, outer_leg2_far}, 1);
  const NurbsCurve end_cap_leg2 = NurbsCurve::FromControlPoints({outer_leg2_far, inner_leg2_far}, 1);
  const NurbsCurve inner_leg2 = NurbsCurve::FromControlPoints({inner_leg2_far, inner_leg2_tan}, 1);
  const NurbsCurve inner_arc = ArcCurve(O, xaxis, yaxis, r_in, theta);
  const NurbsCurve inner_leg1 = NurbsCurve::FromControlPoints({inner_leg1_tan, inner_leg1_far}, 1);
  const NurbsCurve end_cap_leg1 = NurbsCurve::FromControlPoints({inner_leg1_far, outer_leg1_far}, 1);

  const double tol = std::max(r_out, 1.0) * 1e-6;
  const char* fail_msg = "dino8::kernel::Bend: failed joining the bend profile's own segments (please report this as a bug)";
  if (profile.Join(outer_arc, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (profile.Join(outer_leg2, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (profile.Join(end_cap_leg2, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (profile.Join(inner_leg2, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (profile.Join(inner_arc, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (profile.Join(inner_leg1, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (profile.Join(end_cap_leg1, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (!profile.IsClosed()) {
    throw std::runtime_error("dino8::kernel::Bend: assembled profile did not close (please report this as a bug)");
  }

  return Brep::Extrude(profile, zaxis * width, /*cap=*/true);
}

}  // namespace dino8::kernel
