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

// A flat rectangular plate `flat_length` x `width` x `thickness`, used by
// UnfoldBend()/UnfoldMultiBend() below for their own flat-pattern result.
// Built via Brep::Extrude() of a rectangular profile (the same
// AssembleSweptBody() construction Bend()/MultiBend() themselves use, not
// Brep::BoxWelded()): a FromPlanarFaces()-built BoxWelded() genuinely
// welds its own topology but trims each face with a real ON_BrepLoop, so
// Brep::FaceCoversWholeDomain() (brep.cpp) correctly reports it as
// trimmed and Brep::Volume()'s own exact per-face integration refuses it
// outright ("face 0 is trimmed") - confirmed directly, dino8_scratch_test,
// reproducing the exact same refusal on a bare `Brep::BoxWelded(0,0,0,2,
// 3,4)` with no sheet-metal code involved at all. Extrude()'s own faces,
// by contrast, get a real ON_Brep loop running along the surface's own
// boundary (`raw().FaceIsSurface()` - see FaceCoversWholeDomain()'s own
// doc comment), so Volume() integrates them directly - verified exact to
// float precision for this rectangular, axis-aligned case,
// dino8_scratch_test.
Brep FlatPlate(double flat_length, double width, double thickness) {
  const NurbsCurve rect = NurbsCurve::FromControlPoints(
      {Point3d(0, 0, 0), Point3d(flat_length, 0, 0), Point3d(flat_length, width, 0), Point3d(0, width, 0),
       Point3d(0, 0, 0)},
      1);
  return Brep::Extrude(rect, Vector3d(0, 0, 1) * thickness, /*cap=*/true);
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

Brep MultiBend(const std::vector<double>& leg_lengths, const std::vector<double>& bend_angles_degrees,
               const std::vector<double>& inside_radii, double width, double thickness) {
  const size_t num_bends = bend_angles_degrees.size();
  if (num_bends < 1) {
    throw std::invalid_argument(
        "dino8::kernel::MultiBend: at least one bend is required (bend_angles_degrees is empty) - a flat, unbent "
        "sheet is out of this function's own scope");
  }
  if (leg_lengths.size() != num_bends + 1) {
    throw std::invalid_argument(
        "dino8::kernel::MultiBend: leg_lengths must have exactly one more entry than bend_angles_degrees (N+1 legs "
        "around N bends)");
  }
  if (inside_radii.size() != num_bends) {
    throw std::invalid_argument("dino8::kernel::MultiBend: inside_radii must have exactly one entry per bend");
  }
  if (!(width > 0.0)) throw std::invalid_argument("dino8::kernel::MultiBend: width must be positive");
  if (!(thickness > 0.0)) throw std::invalid_argument("dino8::kernel::MultiBend: thickness must be positive");
  for (double len : leg_lengths) {
    if (!(len > 0.0)) throw std::invalid_argument("dino8::kernel::MultiBend: every leg length must be positive");
  }
  for (double r : inside_radii) {
    if (!(r > 0.0)) throw std::invalid_argument("dino8::kernel::MultiBend: every inside_radius must be positive");
  }
  for (double a : bend_angles_degrees) {
    if (!(a > 0.0) || !(a < 180.0)) {
      throw std::invalid_argument(
          "dino8::kernel::MultiBend: every bend_angles_degrees entry must be in (0, 180) - this function only "
          "builds a chain that turns the SAME rotational sense throughout (a U-channel/hat-channel-like convex "
          "profile); a chain that reverses direction partway (a Z/S-bend) is a separate, unimplemented shape");
    }
  }

  const Vector3d zaxis(0, 0, 1);
  // The same (radial, tangent) pair Bend()'s own fixed (xaxis, yaxis) is,
  // generalized to an arbitrary running absolute bearing `a` instead of a
  // single fixed frame: `radial(a)` is the outward direction from whatever
  // this bend's own center is (the direction OUTER sits further along than
  // INNER), `heading(a)` is the direction of travel along the path at that
  // bearing (its derivative) - the same CCW relationship Bend() itself
  // uses (confirmed there: outer_tan = inner_tan + thickness*radial(0),
  // t0 = -heading(0)).
  auto radial = [](double a) { return Vector3d(std::cos(a), std::sin(a), 0.0); };
  auto heading = [](double a) { return Vector3d(-std::sin(a), std::cos(a), 0.0); };

  double angle = 0.0;
  Point3d center(0, 0, 0);
  Point3d inner_tan = center + inside_radii[0] * radial(angle);
  Point3d outer_tan = inner_tan + thickness * radial(angle);

  // The chain's own two free (cut) ends, filled in once each below.
  Point3d outer_start_far, inner_start_far, outer_end_far, inner_end_far;

  std::vector<NurbsCurve> outer_pieces, inner_pieces;  // entry-to-exit order, both chains in parallel

  for (size_t i = 0; i < num_bends; ++i) {
    const double r_in = inside_radii[i];
    const double r_out = r_in + thickness;
    const double theta = bend_angles_degrees[i] * ON_PI / 180.0;

    // The leg leading INTO this bend: ONLY built here for the very first
    // bend (leg_lengths[0], the chain's own free starting end) - every
    // later bend's own "entry leg" is leg_lengths[i], the SAME piece the
    // PREVIOUS iteration already pushed as ITS OWN "exit leg" below
    // (leg_lengths[i_prev + 1] == leg_lengths[i]) - building it again here
    // would push that one segment twice (a genuine, confirmed bug found
    // this way: Join()'s own auto-reversal then silently retraces back
    // across the duplicate instead of erroring on it immediately, and the
    // NEXT piece - that bend's own arc - fails to join at all, off by
    // exactly that leg's own length, confirmed directly via
    // dino8_scratch_test on a real 2-bend chain).
    if (i == 0) {
      const Point3d inner_leg_far = inner_tan - heading(angle) * leg_lengths[0];
      const Point3d outer_leg_far = outer_tan - heading(angle) * leg_lengths[0];
      inner_start_far = inner_leg_far;
      outer_start_far = outer_leg_far;
      outer_pieces.push_back(NurbsCurve::FromControlPoints({outer_leg_far, outer_tan}, 1));
      inner_pieces.push_back(NurbsCurve::FromControlPoints({inner_leg_far, inner_tan}, 1));
    }

    // This bend's own arc, placed via a local frame whose own xaxis sits at
    // the running absolute bearing `angle` - so sweeping it by `theta` in
    // that local frame reproduces the absolute bearing range
    // [angle, angle + theta], exactly like Bend()'s own fixed-frame arc
    // does for its one bend.
    outer_pieces.push_back(ArcCurve(center, radial(angle), heading(angle), r_out, theta));
    inner_pieces.push_back(ArcCurve(center, radial(angle), heading(angle), r_in, theta));

    const double new_angle = angle + theta;
    const Point3d inner_tan_end = center + r_in * radial(new_angle);
    const Point3d outer_tan_end = center + r_out * radial(new_angle);

    // The leg leading OUT of this bend (leg_lengths[i + 1]), forward along
    // heading(new_angle).
    const Point3d inner_next = inner_tan_end + heading(new_angle) * leg_lengths[i + 1];
    const Point3d outer_next = outer_tan_end + heading(new_angle) * leg_lengths[i + 1];

    if (i + 1 == num_bends) {
      // Last leg: these are the chain's OTHER free (cut) end, not another
      // arc's own entry tangent point.
      inner_end_far = inner_next;
      outer_end_far = outer_next;
      outer_pieces.push_back(NurbsCurve::FromControlPoints({outer_tan_end, outer_next}, 1));
      inner_pieces.push_back(NurbsCurve::FromControlPoints({inner_tan_end, inner_next}, 1));
    } else {
      outer_pieces.push_back(NurbsCurve::FromControlPoints({outer_tan_end, outer_next}, 1));
      inner_pieces.push_back(NurbsCurve::FromControlPoints({inner_tan_end, inner_next}, 1));
      // Next bend's own center: wherever ITS OWN inside_radii[i + 1] places
      // it so its own tangent point at bearing `new_angle` lands exactly on
      // `inner_next` (derived algebraically from the same radial()
      // relationship every tangent point above already uses - verified the
      // matching outer_next falls out of this SAME center/angle pair too,
      // not just inner_next, since (outer_next - inner_next) ==
      // thickness*radial(new_angle) by construction above).
      center = inner_next - inside_radii[i + 1] * radial(new_angle);
      angle = new_angle;
      inner_tan = inner_next;
      outer_tan = outer_next;
    }
  }

  // Assemble the closed profile exactly like Bend()'s own single-arc
  // version: every outer piece in entry-to-exit order, the end cap, every
  // inner piece in EXIT-to-entry (reverse) order, then the start cap -
  // NurbsCurve::Join() auto-reverses whichever end of the curve-so-far
  // actually meets the next segment, so each piece above is built in its
  // own natural direction rather than pre-reversed here.
  const double tol = std::max(1.0, std::abs(outer_tan.x) + std::abs(outer_tan.y)) * 1e-6;
  const char* fail_msg =
      "dino8::kernel::MultiBend: failed joining the bent profile's own segments (please report this as a bug)";
  NurbsCurve profile = outer_pieces.front();
  for (size_t i = 1; i < outer_pieces.size(); ++i) {
    if (profile.Join(outer_pieces[i], tol) != Result::Ok) throw std::runtime_error(fail_msg);
  }
  const NurbsCurve end_cap = NurbsCurve::FromControlPoints({outer_end_far, inner_end_far}, 1);
  if (profile.Join(end_cap, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  for (size_t i = inner_pieces.size(); i-- > 0;) {
    if (profile.Join(inner_pieces[i], tol) != Result::Ok) throw std::runtime_error(fail_msg);
  }
  const NurbsCurve start_cap = NurbsCurve::FromControlPoints({inner_start_far, outer_start_far}, 1);
  if (profile.Join(start_cap, tol) != Result::Ok) throw std::runtime_error(fail_msg);
  if (!profile.IsClosed()) {
    throw std::runtime_error("dino8::kernel::MultiBend: assembled profile did not close (please report this as a bug)");
  }

  return Brep::Extrude(profile, zaxis * width, /*cap=*/true);
}

Brep UnfoldBend(double leg1_length, double leg2_length, double width, double thickness, double inside_radius,
                double bend_angle_degrees, double k_factor) {
  if (!(leg1_length > 0.0)) throw std::invalid_argument("dino8::kernel::UnfoldBend: leg1_length must be positive");
  if (!(leg2_length > 0.0)) throw std::invalid_argument("dino8::kernel::UnfoldBend: leg2_length must be positive");
  if (!(width > 0.0)) throw std::invalid_argument("dino8::kernel::UnfoldBend: width must be positive");

  // Delegates thickness/inside_radius/bend_angle_degrees/k_factor
  // validation to BendAllowance() itself (same preconditions Bend() would
  // enforce on those same four parameters) rather than duplicating it.
  const double allowance = BendAllowance(thickness, inside_radius, bend_angle_degrees, k_factor);
  const double flat_length = leg1_length + leg2_length + allowance;
  return FlatPlate(flat_length, width, thickness);
}

Brep UnfoldMultiBend(const std::vector<double>& leg_lengths, const std::vector<double>& bend_angles_degrees,
                      const std::vector<double>& inside_radii, double width, double thickness, double k_factor) {
  const size_t num_bends = bend_angles_degrees.size();
  if (num_bends < 1) {
    throw std::invalid_argument(
        "dino8::kernel::UnfoldMultiBend: at least one bend is required (bend_angles_degrees is empty)");
  }
  if (leg_lengths.size() != num_bends + 1) {
    throw std::invalid_argument(
        "dino8::kernel::UnfoldMultiBend: leg_lengths must have exactly one more entry than bend_angles_degrees");
  }
  if (inside_radii.size() != num_bends) {
    throw std::invalid_argument("dino8::kernel::UnfoldMultiBend: inside_radii must have exactly one entry per bend");
  }
  if (!(width > 0.0)) throw std::invalid_argument("dino8::kernel::UnfoldMultiBend: width must be positive");
  for (double len : leg_lengths) {
    if (!(len > 0.0)) {
      throw std::invalid_argument("dino8::kernel::UnfoldMultiBend: every leg length must be positive");
    }
  }

  double flat_length = 0.0;
  for (double len : leg_lengths) flat_length += len;
  // Delegates each bend's own thickness/inside_radius/bend_angle_degrees/
  // k_factor validation to BendAllowance() itself, exactly like
  // UnfoldBend() above - thrown at the first invalid bend encountered,
  // not necessarily the first one in the array (matching MultiBend()'s
  // own "throws if ANY bend angle is out of range, not just the first"
  // convention).
  for (size_t i = 0; i < num_bends; ++i) {
    flat_length += BendAllowance(thickness, inside_radii[i], bend_angles_degrees[i], k_factor);
  }
  return FlatPlate(flat_length, width, thickness);
}

}  // namespace dino8::kernel
