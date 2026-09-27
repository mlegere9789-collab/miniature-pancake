#include "dino8/kernel/fillet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/detail/circle_clip3d.h"
#include "dino8/kernel/detail/ellipse_clip3d.h"
#include "dino8/kernel/detail/halfspace_clip3d.h"

namespace dino8::kernel {

namespace {

// Same relative-tolerance idea as boolean.cpp's own RelativeTol (not
// shared verbatim - it's three lines and this is a different translation
// unit with its own, unrelated input): scale with the solid's own size
// instead of using one fixed epsilon on both a millimeter part and a
// kilometer-scale one.
double RelativeTol(const std::vector<Brep::PlanarFace>& faces) {
  double max_extent = 0.0;
  for (const Brep::PlanarFace& f : faces) {
    for (const Point3d& p : f.loop) {
      max_extent = std::max({max_extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
    }
  }
  return std::max(1e-9, max_extent * 1e-9);
}

bool PointsEqual(const Point3d& a, const Point3d& b, double tol) { return a.DistanceTo(b) <= tol; }

// Discriminates a CONVEX from a CONCAVE dihedral edge - genuinely needed
// by FilletConcaveEdge, since arccos(n_i . n_j) alone (always in [0, pi])
// is IDENTICAL for a convex edge and its "mirror" concave edge (the same
// two face planes, with material on the opposite side of the shared
// edge): confirmed directly, not assumed - feeding FilletConvexEdge a
// genuine concave fixture (two boxes unioned into an L-shape) left its
// own "theta in (0, pi)" check passing unchanged, and only the unrelated,
// confusingly-worded "radius too large to fit" extent check downstream
// happened to reject it, for every radius tried down to 0.001 - an
// accidental side effect of the convex-only sign-fix math picking the
// wrong in-face direction for a concave input (see FilletConcaveEdge's
// own doc comment), not a principled safeguard, and not something to
// rely on for a NEW function's own primary validation.
//
// Picks loop_i's own vertex (other than p0/p1) with the LARGEST signed
// distance to plane_j (in either direction) - robust against a
// numerically-near-collinear neighbor - and reports `degenerate` (letting
// the caller's own downstream degeneracy checks decide) if even that
// vertex is within tolerance of plane_j.
bool EdgeConvexity(const std::vector<Point3d>& loop_i, size_t k_p0, size_t k_p1, const ON_Plane& plane_j, double tol,
                    bool* degenerate_out) {
  const size_t n = loop_i.size();
  double best_abs = 0.0, best_signed = 0.0;
  for (size_t k = 0; k < n; ++k) {
    if (k == k_p0 || k == k_p1) continue;
    const double d = plane_j.DistanceTo(loop_i[k]);
    if (std::fabs(d) > best_abs) {
      best_abs = std::fabs(d);
      best_signed = d;
    }
  }
  if (degenerate_out) *degenerate_out = best_abs <= tol;
  return best_signed < 0.0;
}

// Closes the genuine gap left when the filleted edge runs all the way to
// one of its own endpoints' OTHER faces (a face perpendicular to the
// edge, meeting it at that single vertex - e.g. a box's own end faces
// when the filleted edge spans a full box edge corner-to-corner): the
// fillet's own trimmed patch has FOUR boundary edges, not two - besides
// the two straight rails re-trimming faces i/j (step 2), the patch's
// other two edges are the quarter-circle ARCS at height 0 and height
// `length` (the v=0/v=length sides of its own [0,length]x[0,angle] trim
// rectangle), lying exactly in the plane through each endpoint
// perpendicular to the edge. Any OTHER face sharing that endpoint vertex
// and lying in that exact perpendicular plane still has its own ORIGINAL
// sharp corner there - unless that corner is itself replaced by the same
// arc, the two surfaces don't actually meet and the resulting Brep isn't
// watertight (confirmed directly: leaving this ungapped measurably
// changes the closed solid's own volume, not just its trim topology).
//
// This is a genuine ADDITION beyond the plain two-face edge fillet this
// function's own doc comment describes - a minimal form of the general
// "vertex blend" problem solid-modeling texts treat as its own topic,
// narrowed here to the one case a straight two-face edge fillet actually
// needs: a THIRD face whose own plane is perpendicular to the fillet
// axis (so the arc, which lies entirely in that one plane, can sit
// exactly on its boundary). An oblique third face (not perpendicular to
// the edge) is out of scope - the arc wouldn't lie in its plane at all -
// and is left untouched, a real, narrower-than-general scope disclosed
// here rather than silently producing a non-watertight result for that
// harder case.
//
// Every `Brep::PlanarFace` loop is a straight-edged polygon (see its own
// doc comment), so the true circular arc is realized here as a fine
// polygonal approximation (kNotchSamples segments) instead of an exact
// curve - the one place this function's own geometry isn't exact to
// floating-point precision, by construction of the representation, not
// by approximation of the fillet math itself (which stays exact
// everywhere else - the main cylindrical patch and the two rail-matching
// re-trims). kNotchSamples is chosen generously enough that this
// polygonal notch's own area error is many orders of magnitude below any
// volume tolerance a caller would reasonably check.
constexpr int kNotchSamples = 200;

// Records one freshly spliced notch run [begin, begin+count) on `f`: the
// first run on a face takes the legacy notch_begin/notch_count pair (so
// every single-fillet result is bit-identical to before PlanarFace::
// notch_runs existed), later runs go to notch_runs. The splice that
// created this run inserted (count - 1) points at index `begin`, so every
// EARLIER-recorded run whose begin lies at or after `begin` has already
// shifted by that much in the loop and is re-indexed here - a face
// notched at several corners by FilletConvexEdges (fillet.h) or by two
// sequential FilletConvexEdge calls needs exactly this. Throws
// std::runtime_error if the splice point fell strictly inside an
// existing run (the vertex being notched was itself an interior arc
// sample - impossible for the trihedral-corner patterns these notches
// serve, checked rather than assumed).
void RegisterNotchRun(Brep::PlanarFace& f, int begin, int count) {
  const int shift = count - 1;
  auto fix = [&](int& run_begin, int run_count) {
    if (run_count <= 1) return;
    if (begin > run_begin && begin < run_begin + run_count - 1) {
      throw std::runtime_error(
          "dino8::kernel::NotchCornerAtVertex: a corner notch was spliced strictly inside an "
          "existing notch run on the same face - please report this as a bug");
    }
    if (run_begin >= begin) run_begin += shift;
  };
  fix(f.notch_begin, f.notch_count);
  for (std::pair<int, int>& r : f.notch_runs) fix(r.first, r.second);
  if (f.notch_count <= 1) {
    f.notch_begin = begin;
    f.notch_count = count;
  } else {
    f.notch_runs.emplace_back(begin, count);
  }
}

void NotchCornerAtVertex(std::vector<Brep::PlanarFace>& other_faces, const Point3d& vertex,
                          const Vector3d& e, const ON_Plane& plane_i, const ON_Plane& plane_j,
                          const Point3d& axis_pt, const Vector3d& n_i, const Vector3d& frame_yaxis,
                          double radius, double sweep_angle, double tol) {
  for (Brep::PlanarFace& f : other_faces) {
    // Only a face perpendicular to the fillet axis can have this arc -
    // it lies entirely in the plane through `vertex` perpendicular to
    // `e`, which is exactly such a face's own plane.
    if (std::fabs(f.plane.zaxis * e) < 1.0 - 1e-6) continue;

    std::vector<Point3d>& loop = f.loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      if (loop[k].DistanceTo(vertex) > tol) continue;
      const Point3d& pred = loop[(k + n - 1) % n];
      const Point3d& succ = loop[(k + 1) % n];
      const bool pred_on_i = std::fabs(plane_i.DistanceTo(pred)) <= tol;
      const bool pred_on_j = std::fabs(plane_j.DistanceTo(pred)) <= tol;
      const bool succ_on_i = std::fabs(plane_i.DistanceTo(succ)) <= tol;
      const bool succ_on_j = std::fabs(plane_j.DistanceTo(succ)) <= tol;
      bool i_to_j;
      if (pred_on_i && succ_on_j) {
        i_to_j = true;
      } else if (pred_on_j && succ_on_i) {
        i_to_j = false;
      } else {
        // Can't unambiguously tell which neighbor is on face i's side and
        // which is on face j's - a more exotic vertex topology than the
        // simple trihedral corner this notch handles; leave this face's
        // sharp corner as-is rather than guess.
        continue;
      }

      std::vector<Point3d> arc;
      arc.reserve(static_cast<size_t>(kNotchSamples) + 1);
      for (int s = 0; s <= kNotchSamples; ++s) {
        const double frac = static_cast<double>(s) / kNotchSamples;
        const double ang = i_to_j ? frac * sweep_angle : (1.0 - frac) * sweep_angle;
        arc.push_back(axis_pt + radius * (std::cos(ang) * n_i + std::sin(ang) * frame_yaxis));
      }

      std::vector<Point3d> new_loop;
      new_loop.reserve(n - 1 + arc.size());
      for (size_t m = 0; m < n; ++m) {
        if (m == k) {
          new_loop.insert(new_loop.end(), arc.begin(), arc.end());
        } else {
          new_loop.push_back(loop[m]);
        }
      }
      loop = std::move(new_loop);

      // Mark this run for Brep::FromMixedFaces (see PlanarFace's own
      // notch_begin/notch_count doc comment): `arc` is spliced in exactly
      // at position k (every point before k is copied 1:1 first), so it
      // occupies new_loop[k .. k+arc.size()-1] - and, per this function's
      // own doc comment, arc.front()/arc.back() are exactly the same two
      // 3D points the adjacent CylindricalFace's own cap corner is built
      // from, so FromMixedFaces' own vertex welder is what actually
      // proves the identity, not this assignment alone. If this same
      // face were ever notched at BOTH of the fillet's own endpoints (a
      // real but narrower case this function doesn't attempt - see this
      // note - since only one notch_begin/notch_count pair fits on a
      // PlanarFace), this second call's own assignment below would
      // simply overwrite the first's, leaving that earlier corner with
      // its old, still-individually-valid-but-unshared polygonal notch
      // rather than crashing or silently misbuilding either one.
      RegisterNotchRun(f, static_cast<int>(k), static_cast<int>(arc.size()));
      break;  // this face's corner is notched; a face shouldn't need it twice at the same vertex
    }
  }
}

// Same corner-notch splicing NotchCornerAtVertex performs (see its own doc
// comment for the shared machinery this mirrors) but for
// FilletConvexEdgeTapered's own cone patch: sampling the TRUE ELLIPSE
// where the cone meets a third face's cutting plane (the plane through
// `vertex` perpendicular to `e`, the SAME plane that face's own boundary
// already lies in) instead of a plain circle - see fillet.h's own doc
// comment for the worked closed-form derivation this evaluates:
//   g(phi) = u_hat + tan_half_angle*(cos(phi)*xaxis + sin(phi)*yaxis)
//   h(phi) = ((vertex - apex) . e) / (g(phi) . e)
//   P(phi) = apex + h(phi)*g(phi)
//
// Besides splicing the dense sample run into whichever PLANAR face is
// found perpendicular to `e` at `vertex` (identical mechanics to
// NotchCornerAtVertex, including its own "can't unambiguously tell which
// neighbor is on face i's/j's side, leave alone" and "no matching face,
// silently no-op" behavior), this ALSO returns the SAME dense sample list
// via `cap_notch_points_out` - always in increasing-angle (i-to-j, angle 0
// -> sweep_angle) order regardless of which physical direction the found
// face's own loop happens to walk (matching ConicalFace::
// cap0_notch_points/cap1_notch_points' own fixed-direction convention) -
// plus a genuinely measured (not guessed) sagitta-style tolerance via
// `cap_notch_tolerance_out`, so FilletConvexEdgeTapered can feed the
// IDENTICAL points into the ConicalFace's own cap0_notch_points/
// cap1_notch_points: the fillet's own true cap patch and the notched
// third face then share a LITERAL boundary curve, not two independently
// -plausible approximations of two different curves (see fillet.h's own
// doc comment for why that distinction is the actual, checked-directly
// finding this construction exists to fix).
//
// Throws std::runtime_error if g(phi).e changes sign (or gets too close to
// 0) across the swept angle: this would mean the cutting plane is
// asymptotically parallel to (or crosses) one of the cone's own rulings
// strictly inside the sweep - a real geometric degeneracy the closed
// form's own division would otherwise silently blow up on, checked
// directly here rather than assumed impossible (unlike `c` in
// FilletConvexEdgeTapered's own cone construction, which IS provably
// never degenerate for any valid convex dihedral/taper - see that
// function's own doc comment; this is a genuinely different, real failure
// mode that only arises once a THIRD face's own cutting plane is brought
// into the picture).
void EllipseNotchCornerAtVertex(std::vector<Brep::PlanarFace>& other_faces, const Point3d& vertex,
                                 const Vector3d& e, const ON_Plane& plane_i, const ON_Plane& plane_j,
                                 const Point3d& apex, const Vector3d& u_hat, const Vector3d& xaxis,
                                 const Vector3d& yaxis, double tan_half_angle, double sweep_angle, double tol,
                                 std::vector<Point3d>& cap_notch_points_out, double& cap_notch_tolerance_out) {
  auto g = [&](double phi) {
    return u_hat + tan_half_angle * (std::cos(phi) * xaxis + std::sin(phi) * yaxis);
  };
  auto h_of = [&](double phi) { return ((vertex - apex) * e) / (g(phi) * e); };
  auto ellipse_pt = [&](double phi) { return apex + h_of(phi) * g(phi); };

  // Checked invariant: g(phi).e must not change sign (or vanish) across
  // [0, sweep_angle] - see this function's own doc comment.
  {
    const double ge0 = g(0.0) * e;
    const double sign0 = ge0 >= 0.0 ? 1.0 : -1.0;
    for (int s = 0; s <= kNotchSamples; ++s) {
      const double phi = sweep_angle * static_cast<double>(s) / kNotchSamples;
      if (g(phi) * e * sign0 < 1e-9) {
        throw std::runtime_error(
            "dino8::kernel::EllipseNotchCornerAtVertex: the cutting plane is "
            "asymptotically parallel to (or crosses) one of the cone's own "
            "rulings within the swept angle - the closed-form h(phi) is "
            "degenerate here, a genuine geometric limit of this "
            "construction (see fillet.h's own doc comment), not a bug");
      }
    }
  }

  for (Brep::PlanarFace& f : other_faces) {
    // Only a face perpendicular to `e` can share this cutting plane at
    // all - same test NotchCornerAtVertex uses.
    if (std::fabs(f.plane.zaxis * e) < 1.0 - 1e-6) continue;

    std::vector<Point3d>& loop = f.loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      if (loop[k].DistanceTo(vertex) > tol) continue;
      const Point3d& pred = loop[(k + n - 1) % n];
      const Point3d& succ = loop[(k + 1) % n];
      const bool pred_on_i = std::fabs(plane_i.DistanceTo(pred)) <= tol;
      const bool pred_on_j = std::fabs(plane_j.DistanceTo(pred)) <= tol;
      const bool succ_on_i = std::fabs(plane_i.DistanceTo(succ)) <= tol;
      const bool succ_on_j = std::fabs(plane_j.DistanceTo(succ)) <= tol;
      bool i_to_j;
      if (pred_on_i && succ_on_j) {
        i_to_j = true;
      } else if (pred_on_j && succ_on_i) {
        i_to_j = false;
      } else {
        continue;
      }

      // Canonical i->j (angle 0 -> sweep_angle) sample list - always in
      // this fixed order, matching ConicalFace::cap0_notch_points/
      // cap1_notch_points' own documented convention; only the SPLICE
      // direction into this particular face's own loop (below) depends on
      // i_to_j, exactly mirroring NotchCornerAtVertex's own handling.
      std::vector<Point3d> canonical;
      canonical.reserve(static_cast<size_t>(kNotchSamples) + 1);
      for (int s = 0; s <= kNotchSamples; ++s) {
        const double phi = sweep_angle * static_cast<double>(s) / kNotchSamples;
        canonical.push_back(ellipse_pt(phi));
      }

      // Genuine sagitta-style tolerance: the max distance, over every
      // sample segment, from that segment's own straight-line midpoint to
      // the TRUE curve's own point at the matching midpoint angle - a
      // real, directly measured bound on this polygonal approximation's
      // own error (not a guess), the same quantity
      // FilletConvexEdge's own kNotchSamples doc comment argues is "far
      // below any reasonable volume tolerance" for the circle case,
      // computed here explicitly since it needs independent verification
      // for this genuinely different (non-circular) curve.
      double max_sagitta = 0.0;
      for (int s = 0; s < kNotchSamples; ++s) {
        const double phi_mid = sweep_angle * (static_cast<double>(s) + 0.5) / kNotchSamples;
        const Point3d chord_mid =
            0.5 * (canonical[static_cast<size_t>(s)] + canonical[static_cast<size_t>(s) + 1]);
        max_sagitta = std::max(max_sagitta, chord_mid.DistanceTo(ellipse_pt(phi_mid)));
      }

      std::vector<Point3d> arc = canonical;
      if (!i_to_j) std::reverse(arc.begin(), arc.end());

      std::vector<Point3d> new_loop;
      new_loop.reserve(n - 1 + arc.size());
      for (size_t mm = 0; mm < n; ++mm) {
        if (mm == k) {
          new_loop.insert(new_loop.end(), arc.begin(), arc.end());
        } else {
          new_loop.push_back(loop[mm]);
        }
      }
      loop = std::move(new_loop);

      RegisterNotchRun(f, static_cast<int>(k), static_cast<int>(arc.size()));

      // Only the FIRST matching face's own sample list feeds the
      // ConicalFace's own cap - there is exactly one true cutting plane
      // per end, so exactly one canonical sample list is meaningful,
      // mirroring NotchCornerAtVertex's own "a face shouldn't need it
      // twice" assumption. Still keep iterating `other_faces` afterward
      // (matching NotchCornerAtVertex's own control flow) rather than
      // returning outright, in case a more exotic solid legitimately has
      // more than one perpendicular face here.
      if (cap_notch_points_out.empty()) {
        cap_notch_points_out = std::move(canonical);
        cap_notch_tolerance_out = max_sagitta;
      }
      break;  // this face's corner is notched; move on to the next face
    }
  }
}

// The genuine per-segment cone geometry FilletConvexEdgeTapered's own
// derivation produces (fillet.h's own doc comment, sections 1-3),
// factored out so it can be applied - verbatim, not re-derived - to any
// [seg_p0, seg_p0 + Lseg*e] sub-interval of a piecewise-linear taper
// profile, not only to the whole edge. Nothing in that derivation
// assumed the taper covered the whole edge; it only assumed r(t) is
// linear on the interval considered, which is true of any segment of a
// piecewise-linear profile by definition - so this is a strict
// re-application of an already-exact construction, not a new one. Shared
// by BOTH BuildTwoStationTaperedFillet (the two-radius overload's own
// single, whole-edge segment) and the N-station overload's own
// per-segment loop, so the two constructions are provably the same code
// - see dino8-kernel's own regression test proving a 2-station call into
// the N-station overload and a direct call to the two-radius overload
// produce bit-identical Breps.
struct TaperedConeSegment {
  Point3d apex;
  Vector3d u_hat, xaxis, yaxis;
  double radius0_true = 0.0;
  double radius1_true = 0.0;
  double length_true = 0.0;
  double cone_sweep_angle = 0.0;
  double tan_half_angle = 0.0;
  // The rolling-ball radius slope over THIS segment, (r_hi - r_lo) /
  // Lseg - needed by the caller to re-trim faces i/j along this
  // segment's own tilted rail direction (see fillet.h's own N-station
  // doc comment).
  double m = 0.0;
};

TaperedConeSegment BuildTaperedConeSegment(const Point3d& seg_p0, double Lseg, double r_lo, double r_hi,
                                            const Vector3d& e, const Vector3d& bis, double cosb,
                                            const Vector3d& n_i, double dot_ij) {
  TaperedConeSegment seg;
  const double m = (r_hi - r_lo) / Lseg;
  seg.m = m;
  const double t_star = -r_lo / m;
  seg.apex = seg_p0 + t_star * e;

  const Vector3d U = e - bis * (m / cosb);
  const double Umag = U.Length();
  Vector3d u_hat = U;
  u_hat.Unitize();
  seg.u_hat = u_hat;

  const double m_over_Umag = m / Umag;
  const double c = std::sqrt(std::max(0.0, 1.0 - m_over_Umag * m_over_Umag));
  if (c < 1e-9) {
    // Should not happen per the proof in the two-radius overload's own
    // doc comment for any finite m and cosb in (0,1) - kept as a checked
    // invariant, not silently trusted.
    throw std::runtime_error(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate cone construction "
        "(c too small) - this should be mathematically impossible for a valid "
        "convex dihedral; please report this as a bug");
  }

  double cos_cone_sweep = (dot_ij - m_over_Umag * m_over_Umag) / (c * c);
  cos_cone_sweep = std::max(-1.0, std::min(1.0, cos_cone_sweep));
  seg.cone_sweep_angle = std::acos(cos_cone_sweep);

  Vector3d xaxis = n_i - (n_i * u_hat) * u_hat;
  if (!xaxis.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate cone frame (face "
        "i's own normal is parallel to the cone's own axis) - should not "
        "happen given c > 0 above; please report this as a bug");
  }
  Vector3d yaxis = ON_CrossProduct(u_hat, xaxis);
  yaxis.Unitize();
  seg.xaxis = xaxis;
  seg.yaxis = yaxis;

  seg.radius0_true = r_lo * c;
  seg.radius1_true = r_hi * c;
  seg.length_true = Lseg * c * c * Umag;
  seg.tan_half_angle = (seg.radius1_true - seg.radius0_true) / seg.length_true;
  return seg;
}

// Reports where a THIRD face, oblique to the fillet edge (not perpendicular
// to `e`), crosses the fillet's own two rail lines at `vertex` - the
// generalization FilletConvexEdge's own doc comment describes for its
// end condition, needed because an oblique face's true cut through the
// cylinder is an ELLIPSE (see EllipseNotchCornerAtVertexCylindrical
// below), not the flat circle NotchCornerAtVertex hardcodes, and its two
// rail corners generally sit at DIFFERENT heights along `e` (unlike the
// perpendicular case, where both are at `vertex`'s own height).
//
// The two rail lines are contact_i(t) = vertex + t*e + radius*n_i and
// contact_j(t) = vertex + t*e + radius*n_j (both parametrized from
// `vertex`, per FilletConvexEdge's own construction); a third face's
// plane through `vertex` with normal n_f crosses each at the single t
// solving (t*e + radius*n_i).n_f == 0 (or n_j) - a linear equation in t
// since e/n_i/n_j are all fixed vectors, giving t_i = -radius*(n_i.n_f) /
// (e.n_f) and likewise for t_j. Only ever reports a crossing for a face
// found via the SAME trihedral-corner pattern (one loop neighbour on
// face i's plane, the other on face j's) every corner-notch in this file
// already uses; a perpendicular face (e.n_f == +-1) is left to
// NotchCornerAtVertex's own plain-circle case, and no match/an ambiguous
// match/a grazing face (e.n_f too close to 0) all report `found = false`
// - the caller then keeps today's flat, unshifted end exactly as before.
//
// Also checked here, not left for a later silent failure: each rail
// crossing must land strictly between `vertex` and the third face's own
// matching neighbour vertex (the same "does the cut overrun this face"
// question ChamferEndAtVertex already asks for the planar chamfer case) -
// throws std::invalid_argument if not, rather than building a
// CylindricalFace notch whose own splice point falls off the third
// face's real boundary.
struct ObliqueEndCrossing {
  bool found = false;
  double t_i = 0.0;
  double t_j = 0.0;
};

// `D_i`/`D_j` are contact_i(vertex)-vertex and contact_j(vertex)-vertex -
// i.e. radius*n_i - bis*offset and radius*n_j - bis*offset in
// FilletConvexEdge's own notation (the SAME fixed vectors its own
// contact_i/contact_j lambdas add to a point on the edge; passed in
// rather than recomputed here so this function never risks drifting from
// that single source of truth). A rail point at arc-length t from
// `vertex` is then exactly `vertex + t*e + D_i` (or `D_j`) - genuinely
// NOT `vertex + t*e + radius*n_i`, which omits the bisector offset and
// would misplace every crossing computed from it (caught during this
// function's own development by cross-checking against a direct
// substitution into the oblique plane's equation, not merely assumed).
ObliqueEndCrossing FindObliqueThirdFaceCrossing(const std::vector<Brep::PlanarFace>& other_faces,
                                                const Point3d& vertex, const Vector3d& e, const ON_Plane& plane_i,
                                                const ON_Plane& plane_j, const Vector3d& D_i, const Vector3d& D_j,
                                                double tol) {
  ObliqueEndCrossing result;
  for (const Brep::PlanarFace& f : other_faces) {
    if (std::fabs(f.plane.zaxis * e) >= 1.0 - 1e-6) continue;  // perpendicular - NotchCornerAtVertex's own case
    const std::vector<Point3d>& loop = f.loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      if (loop[k].DistanceTo(vertex) > tol) continue;
      const Point3d& pred = loop[(k + n - 1) % n];
      const Point3d& succ = loop[(k + 1) % n];
      const bool pred_on_i = std::fabs(plane_i.DistanceTo(pred)) <= tol;
      const bool pred_on_j = std::fabs(plane_j.DistanceTo(pred)) <= tol;
      const bool succ_on_i = std::fabs(plane_i.DistanceTo(succ)) <= tol;
      const bool succ_on_j = std::fabs(plane_j.DistanceTo(succ)) <= tol;
      bool i_to_j;
      if (pred_on_i && succ_on_j && !pred_on_j && !succ_on_i) {
        i_to_j = true;
      } else if (pred_on_j && succ_on_i && !pred_on_i && !succ_on_j) {
        i_to_j = false;
      } else {
        break;  // this face touches the vertex but not in the trihedral pattern - leave it alone, as elsewhere
      }
      const Vector3d n_f = f.plane.zaxis;
      const double e_dot_n = e * n_f;
      if (std::fabs(e_dot_n) < 1e-9) {
        break;  // grazing (near-perpendicular-to-e-but-not-quite) - leave this corner untouched, a real limit
      }
      result.found = true;
      result.t_i = -(D_i * n_f) / e_dot_n;
      result.t_j = -(D_j * n_f) / e_dot_n;
      const Point3d& nb_i = i_to_j ? pred : succ;
      const Point3d& nb_j = i_to_j ? succ : pred;
      auto fraction_along = [&](double t, const Vector3d& D, const Point3d& nb) {
        const Point3d Q = vertex + t * e + D;
        const Vector3d w = nb - vertex;
        const double len2 = w * w;
        return len2 > 0.0 ? ((Q - vertex) * w) / len2 : -1.0;
      };
      const double frac_i = fraction_along(result.t_i, D_i, nb_i);
      const double frac_j = fraction_along(result.t_j, D_j, nb_j);
      if (frac_i < 0.0 || frac_i >= 1.0 - 1e-9 || frac_j < 0.0 || frac_j >= 1.0 - 1e-9) {
        throw std::invalid_argument(
            "dino8::kernel::FilletConvexEdge: an oblique third face's own cut overruns that face (a fillet rail "
            "pierces its plane beyond the far end of the face's own edge, or off that edge entirely) - radius too "
            "large for this solid's geometry");
      }
      return result;
    }
  }
  return result;
}

// Same corner-notch splicing NotchCornerAtVertex performs (see its own
// doc comment for the shared mechanics this mirrors: locating the vertex
// in a third face's own loop, telling apart which neighbour is on face
// i's/j's side, splicing the dense sample in, RegisterNotchRun) but for
// an OBLIQUE third face at a FilletConvexEdge fillet's own end: the true
// boundary there is the ELLIPSE where the third face's own plane cuts the
// fillet's CIRCULAR CYLINDER, not the plain circle NotchCornerAtVertex
// hardcodes - computed via detail/ellipse_clip3d.h's own
// ComputeEllipseFrame3d/EllipsePointAt (built for exactly this: an
// oblique plane's true intersection with a circular cylinder, already
// exercised by BooleanCombineMixed's own oblique plane+cylinder case),
// not a bespoke re-derivation. Unlike EllipseNotchCornerAtVertex's own
// CONE case above (fillet.h's own tapered doc comment), a CONSTANT-
// radius cylinder's ellipse parametrization has NO phi-dependent
// denominator at all (C is one fixed scalar, not a function of phi), so
// ComputeEllipseFrame3d's own single |C| >= min_abs_C check (grazing
// incidence) is the only degeneracy here - no per-sample scan needed.
//
// `cyl` must already reflect whatever origin/length shift
// FindObliqueThirdFaceCrossing's own t_i led the caller to apply (see
// FilletConvexEdge's own doc comment: the angle-0/i-side rail corner at
// an oblique end must be the cylinder's own flat v=0 or v=length corner,
// matching CylindricalFace::cap0_notch_points' own "the first point is
// always the flat angle-0 corner" contract) - this function only reads
// cyl's frame/radius, it never assumes where along the edge it sits.
void EllipseNotchCornerAtVertexCylindrical(std::vector<Brep::PlanarFace>& other_faces, const Point3d& vertex,
                                           const Vector3d& e, const ON_Plane& plane_i, const ON_Plane& plane_j,
                                           const Brep::CylindricalFace& cyl, double sweep_angle, double tol,
                                           std::vector<Point3d>& cap_notch_points_out,
                                           double& cap_notch_tolerance_out) {
  for (Brep::PlanarFace& f : other_faces) {
    if (std::fabs(f.plane.zaxis * e) >= 1.0 - 1e-6) continue;  // perpendicular - NotchCornerAtVertex's own case

    std::vector<Point3d>& loop = f.loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      if (loop[k].DistanceTo(vertex) > tol) continue;
      const Point3d& pred = loop[(k + n - 1) % n];
      const Point3d& succ = loop[(k + 1) % n];
      const bool pred_on_i = std::fabs(plane_i.DistanceTo(pred)) <= tol;
      const bool pred_on_j = std::fabs(plane_j.DistanceTo(pred)) <= tol;
      const bool succ_on_i = std::fabs(plane_i.DistanceTo(succ)) <= tol;
      const bool succ_on_j = std::fabs(plane_j.DistanceTo(succ)) <= tol;
      bool i_to_j;
      if (pred_on_i && succ_on_j) {
        i_to_j = true;
      } else if (pred_on_j && succ_on_i) {
        i_to_j = false;
      } else {
        continue;
      }

      detail::EllipseFrame3d ef;
      try {
        ef = detail::ComputeEllipseFrame3d(cyl, f.plane);
      } catch (const std::runtime_error&) {
        // Grazing incidence - leave this face's sharp corner untouched,
        // the same disclosed limit every other notch in this file has
        // for a degenerate cutting geometry.
        continue;
      }

      std::vector<Point3d> canonical = detail::EllipseBoundarySample3d(ef, 0.0, sweep_angle, kNotchSamples);

      // Same genuine sagitta-style tolerance EllipseNotchCornerAtVertex's
      // own doc comment describes, computed here independently since this
      // is a different (though related) curve family.
      double max_sagitta = 0.0;
      for (int s = 0; s < kNotchSamples; ++s) {
        const double phi_mid = sweep_angle * (static_cast<double>(s) + 0.5) / kNotchSamples;
        const Point3d chord_mid =
            0.5 * (canonical[static_cast<size_t>(s)] + canonical[static_cast<size_t>(s) + 1]);
        max_sagitta = std::max(max_sagitta, chord_mid.DistanceTo(detail::EllipsePointAt(ef, phi_mid)));
      }

      std::vector<Point3d> arc = canonical;
      if (!i_to_j) std::reverse(arc.begin(), arc.end());

      std::vector<Point3d> new_loop;
      new_loop.reserve(n - 1 + arc.size());
      for (size_t mm = 0; mm < n; ++mm) {
        if (mm == k) {
          new_loop.insert(new_loop.end(), arc.begin(), arc.end());
        } else {
          new_loop.push_back(loop[mm]);
        }
      }
      loop = std::move(new_loop);

      RegisterNotchRun(f, static_cast<int>(k), static_cast<int>(arc.size()));

      if (cap_notch_points_out.empty()) {
        cap_notch_points_out = std::move(canonical);
        cap_notch_tolerance_out = max_sagitta;
      }
      break;
    }
  }
}

}  // namespace

Brep FilletConvexEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius) {
  if (!(radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdge: radius must be positive");
  }

  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  // --- (1) locate the two faces sharing (edge_p0, edge_p1), walked in
  // opposite directions on their own loops - the only topology this
  // needs, since two CCW-outward loops always walk a shared boundary
  // edge oppositely.
  int idx_i = -1, idx_j = -1;
  for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const Point3d& a = loop[k];
      const Point3d& b = loop[(k + 1) % n];
      if (idx_i < 0 && PointsEqual(a, edge_p0, tol) && PointsEqual(b, edge_p1, tol)) {
        idx_i = static_cast<int>(f);
      }
      if (idx_j < 0 && PointsEqual(a, edge_p1, tol) && PointsEqual(b, edge_p0, tol)) {
        idx_j = static_cast<int>(f);
      }
    }
  }
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: edge_p0->edge_p1 is not a shared boundary "
        "edge of two distinct faces of `solid`, walked in opposite directions on "
        "their own loops - see this function's own doc comment for the required "
        "topology");
  }

  const ON_Plane& plane_i = faces[static_cast<size_t>(idx_i)].plane;
  const ON_Plane& plane_j = faces[static_cast<size_t>(idx_j)].plane;
  const Vector3d n_i = plane_i.zaxis;
  const Vector3d n_j = plane_j.zaxis;

  // Every OTHER face, moved up here (from step (4) below) since the new
  // oblique-end scan just below needs it before `fillet_face` is even
  // built - a pure reordering of an existing, unmodified loop, not a
  // change to what it computes.
  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) {
      others.push_back(faces[f]);
    }
  }

  Vector3d e = edge_p1 - edge_p0;
  if (!e.Unitize()) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdge: edge_p0 and edge_p1 coincide");
  }

  // theta = interior dihedral angle; the standard vector angle between
  // n_i and n_j (arccos of their dot product, always in [0, pi]) is
  // exactly pi - theta - the angle the fillet's own cylindrical patch
  // sweeps through, since n_i and n_j are the patch's own outward-normal
  // directions at its two rail curves.
  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double sweep_angle = std::acos(dot_ij);  // = pi - theta
  const double theta = ON_PI - sweep_angle;
  if (!(theta > 0.0) || !(theta < ON_PI)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: edge is not a convex dihedral edge "
        "(interior angle theta is <= 0 or >= pi) - concave/degenerate edges are "
        "out of scope, see this function's own doc comment");
  }

  // --- (1) axis, radius, contact lines ---
  Vector3d bis = n_i + n_j;
  if (!bis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: the two adjacent faces' normals sum to "
        "(near) zero - a degenerate (near-180-degree) dihedral");
  }
  const double cosb = bis * n_i;  // > 0 since theta < pi
  if (cosb < 1e-9) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: degenerate bisector geometry (cosb too "
        "small)");
  }

  const double offset = radius / cosb;
  auto axis_point = [&](const Point3d& p) { return p - bis * offset; };
  auto contact_i = [&](const Point3d& p) { return axis_point(p) + n_i * radius; };
  auto contact_j = [&](const Point3d& p) { return axis_point(p) + n_j * radius; };

  // Standard inscribed-circle tangent length for a wedge of interior
  // angle theta: radius * cot(theta / 2).
  const double trim_back = radius / std::tan(theta / 2.0);

  // --- (2) re-trimming each adjacent loop ---
  // Face i's trim-plane normal: m_i = n_i x e, sign-fixed so the sliver
  // nearest the original sharp edge (containing edge_p0) is the side
  // that gets cut away.
  Vector3d m_i = ON_CrossProduct(n_i, e);
  if (!m_i.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: degenerate face/edge geometry (face i's "
        "normal is parallel to the edge)");
  }
  const Point3d contact_i0 = contact_i(edge_p0);
  if (m_i * (edge_p0 - contact_i0) >= 0.0) m_i = -m_i;

  // Face j's trim-plane normal: m_j = n_j x (-e) (face j's own loop walks
  // the shared edge edge_p1 -> edge_p0), sign-fixed the same way, anchored
  // at edge_p1.
  Vector3d m_j = ON_CrossProduct(n_j, -e);
  if (!m_j.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: degenerate face/edge geometry (face j's "
        "normal is parallel to the edge)");
  }
  const Point3d contact_j1 = contact_j(edge_p1);
  if (m_j * (edge_p1 - contact_j1) >= 0.0) m_j = -m_j;

  // Reject a radius that would trim back further than either face's own
  // extent from the edge: the largest perpendicular distance (along that
  // face's own into-material direction m_i/m_j) from the edge line to any
  // vertex of that face's own loop.
  auto max_extent_from_edge = [](const std::vector<Point3d>& loop, const Vector3d& m,
                                  const Point3d& edge_ref) {
    double best = 0.0;
    for (const Point3d& v : loop) best = std::max(best, m * (v - edge_ref));
    return best;
  };
  const double extent_i = max_extent_from_edge(faces[static_cast<size_t>(idx_i)].loop, m_i, edge_p0);
  const double extent_j = max_extent_from_edge(faces[static_cast<size_t>(idx_j)].loop, m_j, edge_p1);
  if (trim_back > extent_i || trim_back > extent_j) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: radius is too large to fit - the "
        "fillet's own trim-back distance exceeds one of the adjacent faces' "
        "extent from the edge");
  }

  // ClipByHalfspace3d keeps the side its plane's normal points AWAY from
  // (see its own doc comment - the convention BooleanIntersectConvexPlanar
  // needs, where the clip plane is another solid's own OUTWARD normal and
  // "inside that solid" is the kept side). m_i/m_j above point TOWARD the
  // kept material instead (the spec's own "dot(m_i,x) >= ..." convention),
  // so the cut planes handed to ClipByHalfspace3d use the negated normal
  // to keep the same material side while still reporting m_i/m_j
  // themselves in the spec's own sign convention for the extent checks.
  const ON_Plane cut_i(contact_i0, -m_i);
  const ON_Plane cut_j(contact_j1, -m_j);

  Brep::PlanarFace retrimmed_i = faces[static_cast<size_t>(idx_i)];
  retrimmed_i.loop = detail::ClipByHalfspace3d(retrimmed_i.loop, cut_i, tol);
  Brep::PlanarFace retrimmed_j = faces[static_cast<size_t>(idx_j)];
  retrimmed_j.loop = detail::ClipByHalfspace3d(retrimmed_j.loop, cut_j, tol);
  if (retrimmed_i.loop.size() < 3 || retrimmed_j.loop.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: re-trimming an adjacent face left fewer "
        "than 3 vertices - radius too large for this solid's geometry");
  }

  // --- (3) the fillet face ---
  // frame.xaxis = n_i (the angle=0 reference direction) and frame.zaxis =
  // e are already orthogonal (e is an edge of face i, hence perpendicular
  // to that face's own normal by definition), so frame.yaxis = e x n_i
  // completes a right-handed orthonormal frame with n_i x (e x n_i) = e
  // exactly (vector triple product, using |n_i| = 1 and n_i . e = 0) -
  // i.e. this frame's own zaxis comes out to exactly e, not merely
  // parallel to it.
  //
  // OBLIQUE END CONDITION, a genuine generalization of the plain
  // perpendicular case: if a third face at edge_p0 (or edge_p1) is
  // OBLIQUE to the edge, FindObliqueThirdFaceCrossing reports where its
  // plane crosses the fillet's own two rail lines - generally at TWO
  // DIFFERENT heights along `e`, not both at the vertex's own height as
  // in the perpendicular case. The i-side (angle-0) crossing becomes this
  // cylinder's own new v=0 (or v=length) reference - required by
  // CylindricalFace::cap0_notch_points' own "the first point is always
  // the flat angle-0 corner" contract - by shifting frame.origin/length
  // to match; the j-side crossing then becomes that cap's own genuinely
  // SLOPED back point, exactly the "sloped cut chain" case that field's
  // own doc comment already anticipates for an unrelated producer. When
  // neither end has an oblique third face (found == false at both, the
  // overwhelmingly common case and every input this function was tested
  // against before this generalization), v0_start == 0 and v1_end == L
  // exactly, so frame.origin/length come out bit-identical to before.
  const double L = edge_p0.DistanceTo(edge_p1);
  // D_i/D_j: contact_i(V)-V and contact_j(V)-V at ANY point V on the edge
  // (a fixed vector, independent of V, since contact_i/contact_j are each
  // V plus a constant offset - see FindObliqueThirdFaceCrossing's own doc
  // comment for why this exact vector, not radius*n_i/radius*n_j, is what
  // a rail point actually adds to its own arc-length reference point).
  const Vector3d D_i = radius * n_i - bis * offset;
  const Vector3d D_j = radius * n_j - bis * offset;
  const ObliqueEndCrossing cross_p0 = FindObliqueThirdFaceCrossing(others, edge_p0, e, plane_i, plane_j, D_i, D_j, tol);
  const ObliqueEndCrossing cross_p1 = FindObliqueThirdFaceCrossing(others, edge_p1, e, plane_i, plane_j, D_i, D_j, tol);
  const double v0_start = cross_p0.found ? cross_p0.t_i : 0.0;
  const double v1_end = cross_p1.found ? (L + cross_p1.t_i) : L;
  if (!(v1_end - v0_start > tol)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: the oblique end condition(s) leave no positive cylinder length between "
        "them - radius too large for this solid's geometry");
  }

  Brep::CylindricalFace fillet_face;
  fillet_face.frame.origin = axis_point(edge_p0) + v0_start * e;
  fillet_face.frame.xaxis = n_i;
  Vector3d frame_yaxis = ON_CrossProduct(e, n_i);
  if (!frame_yaxis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdge: degenerate fillet frame (edge parallel "
        "to face i's own normal)");
  }
  fillet_face.frame.yaxis = frame_yaxis;
  fillet_face.frame.zaxis = e;
  fillet_face.frame.UpdateEquation();
  fillet_face.radius = radius;
  fillet_face.angle = sweep_angle;  // = pi - theta
  fillet_face.length = v1_end - v0_start;

  // --- (4) assemble: all untouched faces, then the two re-trimmed ones,
  // then the one new CylindricalFace.

  // Close the two ends of the fillet, where it meets a face at edge_p0/
  // edge_p1 - see NotchCornerAtVertex's own doc comment for the
  // perpendicular case (a flat corner notch) and
  // EllipseNotchCornerAtVertexCylindrical's own doc comment for the
  // oblique one (a splice of the true ellipse the third face's plane
  // cuts from the cylinder) - genuinely needed either way for a
  // watertight, volume-correct result whenever the filleted edge runs
  // all the way to such a face, which each dispatch below applies as
  // FindObliqueThirdFaceCrossing already determined.
  if (cross_p0.found) {
    EllipseNotchCornerAtVertexCylindrical(others, edge_p0, e, plane_i, plane_j, fillet_face, sweep_angle, tol,
                                          fillet_face.cap0_notch_points, fillet_face.cap0_notch_tolerance);
  } else {
    NotchCornerAtVertex(others, edge_p0, e, plane_i, plane_j, fillet_face.frame.origin, n_i, frame_yaxis, radius,
                        sweep_angle, tol);
  }
  if (cross_p1.found) {
    EllipseNotchCornerAtVertexCylindrical(others, edge_p1, e, plane_i, plane_j, fillet_face, sweep_angle, tol,
                                          fillet_face.cap1_notch_points, fillet_face.cap1_notch_tolerance);
  } else {
    NotchCornerAtVertex(others, edge_p1, e, plane_i, plane_j,
                        fillet_face.frame.origin + fillet_face.length * e, n_i, frame_yaxis, radius,
                        sweep_angle, tol);
  }

  std::vector<Brep::PlanarFace> mixed_planar = std::move(others);
  mixed_planar.push_back(std::move(retrimmed_i));
  mixed_planar.push_back(std::move(retrimmed_j));

  return Brep::FromMixedFaces(mixed_planar, {fillet_face});
}

// CONCAVE (reflex) edge fillet - the rolling ball's own OTHER case, ADDING
// a smooth quarter-round to a concave (interior dihedral angle > pi)
// straight edge between two planar faces, instead of cutting a convex one
// away. This is the geometric mirror of FilletConvexEdge, not an
// independent construction: the ball sits OUTSIDE the material (in the
// empty wedge the concave edge notches out of it), tangent to both faces
// from that side, and the two faces are re-trimmed to meet the new
// cylindrical patch at the tangent lines exactly as FilletConvexEdge's
// own two faces are - just with the SIGN of every "into material"
// direction flipped:
//   - axis_point(p) = p + bis*offset (FilletConvexEdge: p - bis*offset) -
//     the ball center moves INTO the empty wedge being filled, not into
//     the material being cut.
//   - contact_i(p)/contact_j(p) = axis_point(p) -/- n_i/n_j*radius
//     (FilletConvexEdge: +/+) - the tangent point is reached from the
//     (now externally-located) ball center BACK toward each face's own
//     plane.
//   - theta = pi - psi (psi = arccos(n_i . n_j), the angle between the two
//     outward normals) is UNCHANGED in form from FilletConvexEdge, and
//     still the right "wedge angle" for trim_back = radius/tan(theta/2):
//     for a convex edge theta is the interior MATERIAL angle; for a
//     concave edge the interior material angle is pi + psi, so the EMPTY
//     wedge being filled here is 2*pi - (pi + psi) = pi - psi - the exact
//     same expression, a genuine identity checked algebraically, not a
//     coincidence of any one test angle.
//   - the fillet cylinder's own frame uses xaxis = -n_i (FilletConvexEdge:
//     +n_i) so that frame.origin + radius*xaxis lands exactly on
//     contact_i, not on the (unwanted) point radius AWAY from it on the
//     material side; yaxis = e x xaxis follows the same construction
//     FilletConvexEdge's own frame uses. `outward = false` is then the
//     one remaining bit (Brep::CylindricalFace's own doc comment) that
//     tells FromMixedFaces() this patch bounds material from the concave
//     side, so its presented normal points radially INWARD, toward the
//     ball center, matching a genuine boundary-representation outward
//     normal for material that is now OUTSIDE the swept circle rather
//     than inside it.
// The corner-notch end condition (a third face exactly PERPENDICULAR to
// the edge at edge_p0/edge_p1) reuses NotchCornerAtVertex UNCHANGED - it
// is already a generic "splice this vertex into an arc around axis_pt,
// from radius*xaxis to radius*(cos(sweep_angle)*xaxis+sin(sweep_angle)*
// yaxis)" operation with no convex-specific assumption baked in, so
// passing this function's own (negated) frame.xaxis/frame.yaxis produces
// the correct OUTWARD-bulging notch (adding, not cutting, that face's own
// corner) automatically, from the same code the convex case uses to cut
// one.
//
// VALIDATION: `radius` > 0; the two-face shared-boundary-edge topology
// FilletConvexEdge itself requires; and, genuinely new here, an explicit
// check (EdgeConvexity, see its own doc comment) that the edge really IS
// concave - thrown with a clear message rather than relying on the
// accidental (and, for other dihedral angles, unproven) protection a
// convex edge fed to this function's own math might or might not still
// receive downstream.
//
// SCOPE: a straight edge between exactly two PLANAR faces (same
// PlanarFaces() precondition as FilletConvexEdge), one constant radius,
// with any third face at either endpoint either a free boundary or
// exactly PERPENDICULAR to the edge (closed via the corner notch above).
// UNLIKE FilletConvexEdge, an OBLIQUE third face at an endpoint is left
// untouched here - the ellipse-cap machinery FilletConvexEdge's own
// oblique-end generalization uses (FindObliqueThirdFaceCrossing /
// EllipseNotchCornerAtVertexCylindrical) is convex-specific in its own
// derivation and has not been re-derived for the concave sign convention;
// a genuine, disclosed future increment, not silently approximated.
// Multi-edge propagation and vertex blends at concave (or mixed convex/
// concave) corners are likewise out of scope for this first increment,
// matching how FilletConvexEdge itself started before FilletConvexEdges
// generalized it.
//
// CLOSED FORM this was checked against: for a concave edge of length L
// with interior dihedral angle 3*pi/2 (a 90-degree notch, e.g. the
// reflex edge of an L-shaped solid built by unioning two boxes), the
// fillet ADDS exactly L * radius^2 * (1 - pi/4) of volume - the same
// magnitude FilletConvexEdges' own Steiner-formula cross term uses for a
// 90-degree CONVEX corner's own REMOVED area, here added instead of
// removed, by the same "square minus quarter-disk" cross-section a
// rolling ball of that radius traces filling a 90-degree notch.
Brep FilletConcaveEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius) {
  if (!(radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::FilletConcaveEdge: radius must be positive");
  }

  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  int idx_i = -1, idx_j = -1;
  size_t k_i = 0;
  for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const Point3d& a = loop[k];
      const Point3d& b = loop[(k + 1) % n];
      if (idx_i < 0 && PointsEqual(a, edge_p0, tol) && PointsEqual(b, edge_p1, tol)) {
        idx_i = static_cast<int>(f);
        k_i = k;
      }
      if (idx_j < 0 && PointsEqual(a, edge_p1, tol) && PointsEqual(b, edge_p0, tol)) {
        idx_j = static_cast<int>(f);
      }
    }
  }
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: edge_p0->edge_p1 is not a shared boundary "
        "edge of two distinct faces of `solid`, walked in opposite directions on "
        "their own loops - see FilletConvexEdge's own doc comment for the required "
        "topology, which this function shares");
  }

  {
    const ON_Plane& plane_j_pre = faces[static_cast<size_t>(idx_j)].plane;
    const std::vector<Point3d>& loop_i = faces[static_cast<size_t>(idx_i)].loop;
    const size_t k_i1 = (k_i + 1) % loop_i.size();
    bool degenerate = false;
    const bool convex = EdgeConvexity(loop_i, k_i, k_i1, plane_j_pre, tol, &degenerate);
    if (!degenerate && convex) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdge: edge is a CONVEX dihedral edge, not concave - see FilletConvexEdge "
          "instead");
    }
  }

  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) {
      others.push_back(faces[f]);
    }
  }

  Vector3d e = edge_p1 - edge_p0;
  if (!e.Unitize()) {
    throw std::invalid_argument("dino8::kernel::FilletConcaveEdge: edge_p0 and edge_p1 coincide");
  }

  // Which of the two faces serves as the fillet cylinder's own angle-0
  // reference (frame.xaxis) is not a free choice: it must be whichever
  // one makes a RIGHT-HANDED frame (xaxis, yaxis = e x xaxis, zaxis = e)
  // also land on the OTHER face's own tangent point at angle =
  // sweep_angle - and unlike FilletConvexEdge (where "face i", the one
  // whose loop walks edge_p0->edge_p1, always works), a concave edge does
  // not: confirmed directly, sign((n_i x n_j) . e) is +1 for a standard
  // convex box corner but -1 for a concave one built with the exact same
  // walked-direction convention, so using "face i" unconditionally
  // reached the WRONG tangent point at angle = sweep_angle here (caught
  // by this function's own volume regression - the mismatch between the
  // retrimmed OTHER face's own rail and the cylinder's own cap silently
  // built a non-solid result, not assumed safe from one example). The
  // identity this relies on is proven algebraically for ANY n_i, e with
  // n_i . e == 0, not fit to one case: n_i x (cos(psi)*n_i +
  // sin(psi)*(e x n_i)) = sin(psi)*e via the vector triple product - so
  // whichever face gives a POSITIVE (n_i x n_j) . e is the correct
  // angle-0 reference, and swapping the face labels when it is negative
  // is a real fix.
  const Vector3d n_i_pre = faces[static_cast<size_t>(idx_i)].plane.zaxis;
  const Vector3d n_j_pre = faces[static_cast<size_t>(idx_j)].plane.zaxis;
  if ((ON_CrossProduct(n_i_pre, n_j_pre) * e) < 0.0) {
    std::swap(idx_i, idx_j);
  }

  const ON_Plane& plane_i = faces[static_cast<size_t>(idx_i)].plane;
  const ON_Plane& plane_j = faces[static_cast<size_t>(idx_j)].plane;
  const Vector3d n_i = plane_i.zaxis;
  const Vector3d n_j = plane_j.zaxis;

  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double sweep_angle = std::acos(dot_ij);  // = psi, angle between the two outward normals
  const double theta = ON_PI - sweep_angle;      // the EMPTY wedge angle this fillet fills
  if (!(theta > 0.0) || !(theta < ON_PI)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: degenerate edge (interior angle is <= 0 or >= pi)");
  }

  Vector3d bis = n_i + n_j;
  if (!bis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: the two adjacent faces' normals sum to "
        "(near) zero - a degenerate (near-180-degree) dihedral");
  }
  const double cosb = bis * n_i;
  if (cosb < 1e-9) {
    throw std::invalid_argument("dino8::kernel::FilletConcaveEdge: degenerate bisector geometry (cosb too small)");
  }

  const double offset = radius / cosb;
  auto axis_point = [&](const Point3d& p) { return p + bis * offset; };
  auto contact_i = [&](const Point3d& p) { return axis_point(p) - n_i * radius; };
  auto contact_j = [&](const Point3d& p) { return axis_point(p) - n_j * radius; };

  const double trim_back = radius / std::tan(theta / 2.0);

  Vector3d m_i = ON_CrossProduct(n_i, e);
  if (!m_i.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: degenerate face/edge geometry (face i's normal is parallel to the edge)");
  }
  const Point3d contact_i0 = contact_i(edge_p0);
  if (m_i * (edge_p0 - contact_i0) >= 0.0) m_i = -m_i;

  Vector3d m_j = ON_CrossProduct(n_j, -e);
  if (!m_j.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: degenerate face/edge geometry (face j's normal is parallel to the edge)");
  }
  const Point3d contact_j1 = contact_j(edge_p1);
  if (m_j * (edge_p1 - contact_j1) >= 0.0) m_j = -m_j;

  auto max_extent_from_edge = [](const std::vector<Point3d>& loop, const Vector3d& m, const Point3d& edge_ref) {
    double best = 0.0;
    for (const Point3d& v : loop) best = std::max(best, m * (v - edge_ref));
    return best;
  };
  const double extent_i = max_extent_from_edge(faces[static_cast<size_t>(idx_i)].loop, m_i, edge_p0);
  const double extent_j = max_extent_from_edge(faces[static_cast<size_t>(idx_j)].loop, m_j, edge_p1);
  if (trim_back > extent_i || trim_back > extent_j) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: radius is too large to fit - the fillet's "
        "own trim-back distance exceeds one of the adjacent faces' extent from the edge");
  }

  const ON_Plane cut_i(contact_i0, -m_i);
  const ON_Plane cut_j(contact_j1, -m_j);

  Brep::PlanarFace retrimmed_i = faces[static_cast<size_t>(idx_i)];
  retrimmed_i.loop = detail::ClipByHalfspace3d(retrimmed_i.loop, cut_i, tol);
  Brep::PlanarFace retrimmed_j = faces[static_cast<size_t>(idx_j)];
  retrimmed_j.loop = detail::ClipByHalfspace3d(retrimmed_j.loop, cut_j, tol);
  if (retrimmed_i.loop.size() < 3 || retrimmed_j.loop.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: re-trimming an adjacent face left fewer than 3 vertices - radius too "
        "large for this solid's geometry");
  }

  const double L = edge_p0.DistanceTo(edge_p1);

  // OBLIQUE END CONDITION, the concave mirror of FilletConvexEdge's own
  // (see that function's own doc comment for the full derivation this
  // reuses verbatim): FindObliqueThirdFaceCrossing and
  // EllipseNotchCornerAtVertexCylindrical are both already generic in
  // `radius`/`frame`/D_i/D_j - neither hardcodes a convex-only sign
  // convention - so the ONLY thing that needs to change here is D_i/D_j
  // themselves: contact_i(p) - p is a FIXED vector (independent of p,
  // same fact FilletConvexEdge's own doc comment relies on), and for this
  // function's own contact_i(p) = axis_point(p) - n_i*radius =
  // p + bis*offset - n_i*radius, so D_i = bis*offset - n_i*radius - the
  // NEGATION of FilletConvexEdge's own D_i = radius*n_i - bis*offset, not
  // the same formula reused as-is (confirmed by direct substitution, not
  // assumed from the sign pattern elsewhere in this function).
  const Vector3d D_i = bis * offset - n_i * radius;
  const Vector3d D_j = bis * offset - n_j * radius;
  const ObliqueEndCrossing cross_p0 = FindObliqueThirdFaceCrossing(others, edge_p0, e, plane_i, plane_j, D_i, D_j, tol);
  const ObliqueEndCrossing cross_p1 = FindObliqueThirdFaceCrossing(others, edge_p1, e, plane_i, plane_j, D_i, D_j, tol);
  const double v0_start = cross_p0.found ? cross_p0.t_i : 0.0;
  const double v1_end = cross_p1.found ? (L + cross_p1.t_i) : L;
  if (!(v1_end - v0_start > tol)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: the oblique end condition(s) leave no positive cylinder length between "
        "them - radius too large for this solid's geometry");
  }

  Brep::CylindricalFace fillet_face;
  fillet_face.frame.origin = axis_point(edge_p0) + v0_start * e;
  fillet_face.frame.xaxis = -n_i;
  Vector3d frame_yaxis = ON_CrossProduct(e, fillet_face.frame.xaxis);
  if (!frame_yaxis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConcaveEdge: degenerate fillet frame (edge parallel to face i's own normal)");
  }
  fillet_face.frame.yaxis = frame_yaxis;
  fillet_face.frame.zaxis = e;
  fillet_face.frame.UpdateEquation();
  fillet_face.radius = radius;
  fillet_face.angle = sweep_angle;
  fillet_face.length = v1_end - v0_start;
  fillet_face.outward = false;

  if (cross_p0.found) {
    EllipseNotchCornerAtVertexCylindrical(others, edge_p0, e, plane_i, plane_j, fillet_face, sweep_angle, tol,
                                          fillet_face.cap0_notch_points, fillet_face.cap0_notch_tolerance);
  } else {
    NotchCornerAtVertex(others, edge_p0, e, plane_i, plane_j, fillet_face.frame.origin, fillet_face.frame.xaxis,
                        frame_yaxis, radius, sweep_angle, tol);
  }
  if (cross_p1.found) {
    EllipseNotchCornerAtVertexCylindrical(others, edge_p1, e, plane_i, plane_j, fillet_face, sweep_angle, tol,
                                          fillet_face.cap1_notch_points, fillet_face.cap1_notch_tolerance);
  } else {
    NotchCornerAtVertex(others, edge_p1, e, plane_i, plane_j, fillet_face.frame.origin + fillet_face.length * e,
                        fillet_face.frame.xaxis, frame_yaxis, radius, sweep_angle, tol);
  }

  std::vector<Brep::PlanarFace> mixed_planar = std::move(others);
  mixed_planar.push_back(std::move(retrimmed_i));
  mixed_planar.push_back(std::move(retrimmed_j));

  return Brep::FromMixedFaces(mixed_planar, {fillet_face});
}

namespace {

// The two-radius overload's OWN former standalone implementation, moved
// (not rewritten) into a private helper so the public N-station overload
// can dispatch its own stations.size()==2, non-flat case into EXACTLY
// this code - see fillet.h's own N-station doc comment and
// dino8-kernel's own regression test proving this produces bit-identical
// output to the public two-radius overload's own direct call. The only
// change from this function's own former body is that the per-segment
// cone math now goes through BuildTaperedConeSegment (above) instead of
// being inlined here - the SAME formulas, same evaluation order, so the
// floating-point result is unchanged.
Brep BuildTwoStationTaperedFillet(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius0,
                                   double radius1) {
  if (!(radius0 > 0.0) || !(radius1 > 0.0)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: radius0 and radius1 must both "
        "be strictly positive (a radius reaching zero partway along the edge "
        "would put the swept patch's own apex INSIDE the trimmed region - a "
        "genuinely different, out-of-scope topology - see this function's own "
        "doc comment)");
  }

  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  // --- locate the two faces sharing (edge_p0, edge_p1) - verbatim from
  // FilletConvexEdge's own step (1) (see its own doc comment).
  int idx_i = -1, idx_j = -1;
  for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const Point3d& a = loop[k];
      const Point3d& b = loop[(k + 1) % n];
      if (idx_i < 0 && PointsEqual(a, edge_p0, tol) && PointsEqual(b, edge_p1, tol)) {
        idx_i = static_cast<int>(f);
      }
      if (idx_j < 0 && PointsEqual(a, edge_p1, tol) && PointsEqual(b, edge_p0, tol)) {
        idx_j = static_cast<int>(f);
      }
    }
  }
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: edge_p0->edge_p1 is not a "
        "shared boundary edge of two distinct faces of `solid`, walked in "
        "opposite directions on their own loops - see FilletConvexEdge's own "
        "doc comment for the required topology, which this function shares");
  }

  const ON_Plane& plane_i = faces[static_cast<size_t>(idx_i)].plane;
  const ON_Plane& plane_j = faces[static_cast<size_t>(idx_j)].plane;
  const Vector3d n_i = plane_i.zaxis;
  const Vector3d n_j = plane_j.zaxis;

  Vector3d e = edge_p1 - edge_p0;
  const double L = edge_p0.DistanceTo(edge_p1);
  if (!e.Unitize()) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdgeTapered: edge_p0 and edge_p1 coincide");
  }

  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double sweep_angle0 = std::acos(dot_ij);  // = pi - theta, the m=0 (planar dihedral) sweep
  const double theta = ON_PI - sweep_angle0;
  if (!(theta > 0.0) || !(theta < ON_PI)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: edge is not a convex dihedral "
        "edge (interior angle theta is <= 0 or >= pi) - concave/degenerate "
        "edges are out of scope, see FilletConvexEdge's own doc comment");
  }

  Vector3d bis = n_i + n_j;
  if (!bis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: the two adjacent faces' "
        "normals sum to (near) zero - a degenerate (near-180-degree) dihedral");
  }
  const double cosb = bis * n_i;  // > 0 since theta < pi; also == bis*n_j (bis is symmetric in i,j)
  if (cosb < 1e-9) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate bisector geometry "
        "(cosb too small)");
  }

  // --- dispatch to today's FilletConvexEdge, UNCHANGED, when the taper is
  // negligible - the exact m=0 case is a genuine separate code path, never
  // run through the cone construction below as a very-flat approximation
  // of one (see this function's own doc comment).
  const double radius_tol = std::max(1e-9, std::max(radius0, radius1) * 1e-9);
  if (std::fabs(radius1 - radius0) <= radius_tol) {
    return FilletConvexEdge(solid, edge_p0, edge_p1, radius0);
  }

  // --- Step 1 (rail exactness, for ANY r(t)): k_i, k_j fixed vectors;
  // rail_i(t) = edge_p0 + t*e + r(t)*k_i always lies exactly in plane i
  // (k_i . n_i == 0, checked directly: n_i.n_i - (bis.n_i)/cosb == 1 - 1
  // == 0), and analogously for rail_j/plane j - see this function's own
  // doc comment for the full derivation.
  const Vector3d k_i = n_i - bis * (1.0 / cosb);
  const Vector3d k_j = n_j - bis * (1.0 / cosb);
  const double m0 = (radius1 - radius0) / L;
  auto r_of = [&](double t) { return radius0 + m0 * t; };
  auto rail_i = [&](double t) { return edge_p0 + t * e + r_of(t) * k_i; };
  auto rail_j = [&](double t) { return edge_p0 + t * e + r_of(t) * k_j; };

  // --- Steps 2-3 (cone apex/axis/frame/true-radii/true-length/true-sweep):
  // the whole edge, [0, L], is itself just ONE segment of a piecewise-
  // linear profile - BuildTaperedConeSegment (above) is this exact
  // derivation, factored out so it can also be re-applied per-segment by
  // the N-station overload.
  const TaperedConeSegment seg = BuildTaperedConeSegment(edge_p0, L, radius0, radius1, e, bis, cosb, n_i, dot_ij);
  const double m = seg.m;
  const Point3d& apex = seg.apex;
  const Vector3d& u_hat = seg.u_hat;
  const Vector3d& xaxis = seg.xaxis;
  const Vector3d& yaxis = seg.yaxis;
  const double cone_sweep_angle = seg.cone_sweep_angle;

  Brep::ConicalFace fillet_face;
  fillet_face.frame.origin = apex;
  fillet_face.frame.xaxis = xaxis;
  fillet_face.frame.yaxis = yaxis;
  fillet_face.frame.zaxis = u_hat;
  fillet_face.frame.UpdateEquation();
  fillet_face.radius0 = seg.radius0_true;
  fillet_face.radius1 = seg.radius1_true;
  fillet_face.angle = cone_sweep_angle;
  fillet_face.length = seg.length_true;

  // --- re-trim faces i/j: same single-plane-clip FilletConvexEdge's own
  // step 3 uses, generalized so the cut plane's own in-plane normal is
  // perpendicular to the TILTED rail direction d_i/d_j = e + m*k_i/k_j
  // (still, provably, a single straight line per face - Step 1 above)
  // instead of perpendicular to e itself. d_i is guaranteed nonzero and
  // perpendicular to n_i (d_i.n_i == e.n_i + m*(k_i.n_i) == 0 + 0 == 0,
  // both terms checked directly above/in FilletConvexEdge's own
  // derivation), so this is always well-defined.
  const Vector3d d_i = e + m * k_i;
  const Vector3d d_j = e + m * k_j;

  Vector3d m_i = ON_CrossProduct(n_i, d_i);
  if (!m_i.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate face/edge geometry "
        "(face i's own tilted rail direction is parallel to its own normal)");
  }
  const Point3d contact_i0 = rail_i(0.0);
  if (m_i * (edge_p0 - contact_i0) >= 0.0) m_i = -m_i;

  Vector3d m_j = ON_CrossProduct(n_j, d_j);
  if (!m_j.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate face/edge geometry "
        "(face j's own tilted rail direction is parallel to its own normal)");
  }
  const Point3d contact_j1 = rail_j(L);
  if (m_j * (edge_p1 - contact_j1) >= 0.0) m_j = -m_j;

  // Unlike FilletConvexEdge, this function does NOT attempt FilletConvexEdge's
  // own closed-form "does the radius fit" pre-check here: its
  // trim_back = radius/tan(theta/2) relies on k_i being exactly
  // perpendicular to the (m=0) rail direction e, which is no longer true
  // once m != 0 (k_i . d_i == m * |k_i|^2 != 0 - a real, checked
  // difference from the m=0 case, not an oversight) - so that clean closed
  // form does not carry over exactly.
  //
  // A real gap CONFIRMED DURING DEVELOPMENT, not merely theorized: relying
  // on the post-clip loop.size()<3 check ALONE (as FilletConvexEdge's own
  // "if the pre-check isn't binding, the post-check still is" fallback
  // does) is NOT sufficient here. A large enough radius1 on a small solid
  // pushes rail_i(L)/rail_j(0) entirely OUTSIDE the adjacent face's own
  // finite extent - and ClipByHalfspace3d, given a cut plane whose
  // (tilted) intended rail line doesn't actually pass through the face's
  // own polygon at all, does not reliably empty the polygon to fewer than
  // 3 vertices; it can instead produce a small, entirely unrelated corner
  // sliver that still has exactly 3 vertices (confirmed directly: radius0
  // =0.1, radius1=5.0 on a unit cube produced a 3-vertex "retrimmed" face
  // whose own vertices were NOT rail_i(0)/rail_i(L) at all, and the
  // resulting Brep's tessellated volume came out at 5.3x the original
  // unit cube - silently wrong, not merely rejected). The real, checked
  // fix: after clipping, verify BOTH of face i's own intended rail
  // endpoints (rail_i(0), rail_i(L) - exact, closed-form points, not
  // approximations) are genuinely present as vertices of the retrimmed
  // loop, and likewise for face j - if either is missing, the tilted cut
  // plane did not actually intersect that face's own bounded extent along
  // the intended rail line, so the taper does not fit this solid's
  // geometry and this function refuses to silently ship whatever
  // ClipByHalfspace3d happened to produce instead.
  const Point3d rail_i_far = rail_i(L);
  const Point3d rail_j_near = rail_j(0.0);
  auto loop_has_point_near = [&](const std::vector<Point3d>& loop, const Point3d& p) {
    for (const Point3d& v : loop) {
      if (PointsEqual(v, p, tol)) return true;
    }
    return false;
  };

  const ON_Plane cut_i(contact_i0, -m_i);
  const ON_Plane cut_j(contact_j1, -m_j);

  Brep::PlanarFace retrimmed_i = faces[static_cast<size_t>(idx_i)];
  retrimmed_i.loop = detail::ClipByHalfspace3d(retrimmed_i.loop, cut_i, tol);
  Brep::PlanarFace retrimmed_j = faces[static_cast<size_t>(idx_j)];
  retrimmed_j.loop = detail::ClipByHalfspace3d(retrimmed_j.loop, cut_j, tol);
  if (retrimmed_i.loop.size() < 3 || retrimmed_j.loop.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: re-trimming an adjacent face "
        "left fewer than 3 vertices - radius0/radius1 too large for this "
        "solid's geometry");
  }
  if (!loop_has_point_near(retrimmed_i.loop, contact_i0) || !loop_has_point_near(retrimmed_i.loop, rail_i_far) ||
      !loop_has_point_near(retrimmed_j.loop, rail_j_near) || !loop_has_point_near(retrimmed_j.loop, contact_j1)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: radius0/radius1 too large to "
        "fit - the intended rail line (rail_i(0)-rail_i(L) on face i, "
        "rail_j(0)-rail_j(L) on face j) no longer lies within one of the "
        "adjacent faces' own bounded extent, so the tilted retrim plane cut "
        "through a different, unrelated region instead of the intended sliver");
  }

  // --- assemble: all untouched faces, then the two re-trimmed ones, then
  // the one new ConicalFace. Close the two ends exactly as
  // FilletConvexEdge's own NotchCornerAtVertex does for the
  // constant-radius case, generalized to sample the TRUE ELLIPSE (not a
  // circle) where a third face perpendicular to `e` meets this now-tilted
  // cone - see EllipseNotchCornerAtVertex's own doc comment and fillet.h's
  // own doc comment for the closed-form derivation this closes v1's own
  // disclosed gap with. `tan_half_angle` is recomputed here from
  // fillet_face's own already-finalized radius0/radius1/length fields
  // (the exact same formula Brep::FromMixedFaces independently recomputes
  // from the SAME fields - see fillet.h's own doc comment - keeping the
  // two constructions provably consistent rather than threading one more
  // parameter through by hand).
  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) {
      others.push_back(faces[f]);
    }
  }

  const double tan_half_angle = (fillet_face.radius1 - fillet_face.radius0) / fillet_face.length;
  EllipseNotchCornerAtVertex(others, edge_p0, e, plane_i, plane_j, apex, u_hat, xaxis, yaxis, tan_half_angle,
                              cone_sweep_angle, tol, fillet_face.cap0_notch_points,
                              fillet_face.cap0_notch_tolerance);
  EllipseNotchCornerAtVertex(others, edge_p1, e, plane_i, plane_j, apex, u_hat, xaxis, yaxis, tan_half_angle,
                              cone_sweep_angle, tol, fillet_face.cap1_notch_points,
                              fillet_face.cap1_notch_tolerance);

  std::vector<Brep::PlanarFace> mixed_planar = std::move(others);
  mixed_planar.push_back(std::move(retrimmed_i));
  mixed_planar.push_back(std::move(retrimmed_j));

  return Brep::FromMixedFaces(mixed_planar, {}, {fillet_face});
}

// Replaces the ONE loop edge from `from` to `to` (which must be
// consecutive loop vertices, `loop[k] == from`, `loop[(k+1)%n] == to`,
// within `tol` - the same guarantee PlanarFaces()'s own convention
// already gives the (edge_p0, edge_p1) pair this is used for) with the
// literal points of `replacement` (whose own first/last points are NOT
// required to equal `from`/`to` exactly - the caller supplies the exact
// intended points, e.g. rail_i(t_0)..rail_i(t_last), which coincide with
// `from`/`to` only up to this same `tol`). Used by
// BuildMultiStationTaperedFillet (below) to splice the FULL piecewise
// rail_i(t)/rail_j(t) polyline (through every station, not just the two
// outer endpoints) into face i's/face j's own loop: each resulting
// station-to-station sub-run is then just an ORDINARY straight loop edge
// that coincides exactly (to floating-point precision) with the matching
// ConicalFace segment's own straight rail side, so it welds into the
// same real ON_BrepEdge automatically via FromMixedFaces()'s own
// existing coincident-point vertex welding - no notch_begin/notch_count
// collapsing needed here at all, unlike NotchCornerAtVertex/
// EllipseNotchCornerAtVertex's own corner-arc splices (which collapse
// several polygon edges into ONE shared edge against a single neighbor -
// genuinely a different situation from here, where each station-to-
// station sub-run is its own real edge, shared with its own distinct
// ConicalFace segment).
std::vector<Point3d> SpliceLoopEdge(const std::vector<Point3d>& loop, const Point3d& from, const Point3d& to,
                                     const std::vector<Point3d>& replacement, double tol) {
  const size_t n = loop.size();
  for (size_t k = 0; k < n; ++k) {
    if (PointsEqual(loop[k], from, tol) && PointsEqual(loop[(k + 1) % n], to, tol)) {
      std::vector<Point3d> new_loop;
      new_loop.reserve(n - 2 + replacement.size());
      const size_t k2 = (k + 1) % n;
      for (size_t mm = 0; mm < n; ++mm) {
        if (mm == k) {
          new_loop.insert(new_loop.end(), replacement.begin(), replacement.end());
        } else if (mm == k2) {
          continue;
        } else {
          new_loop.push_back(loop[mm]);
        }
      }
      return new_loop;
    }
  }
  throw std::invalid_argument(
      "dino8::kernel::FilletConvexEdgeTapered: could not find the shared edge "
      "to splice a multi-station rail polyline into - please report this as a "
      "bug");
}

// The N-station (N >= 3) general case: builds N-1 genuine
// Brep::ConicalFace segments (one per [stations[k].t, stations[k+1].t]
// sub-interval, via BuildTaperedConeSegment), splices the FULL piecewise
// rail_i(t)/rail_j(t) polyline into faces i/j (see SpliceLoopEdge above -
// a real correction versus re-using the two-station overload's own
// single half-space clip, which does NOT work once there is more than
// one segment: once adjacent segments have different slopes, rail_i(t)
// genuinely bends at each interior station, so no single plane contains
// the whole rail), splices the interior-station cap join between every
// pair of adjacent segments (see fillet.h's own N-station doc comment
// for the closed-form join and Brep::ConicalFace::
// cap0_surface_fit_tolerance/cap1_surface_fit_tolerance's own doc
// comment for the genuinely non-vanishing bound this carries), and
// applies the corner-notch (EllipseNotchCornerAtVertex, unchanged) at
// only the two OUTER endpoints, using only the first/last segment's own
// cone parameters there.
Brep BuildMultiStationTaperedFillet(const Brep& solid, Point3d edge_p0, Point3d edge_p1,
                                     const std::vector<FilletRadiusStation>& stations) {
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  int idx_i = -1, idx_j = -1;
  for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const Point3d& a = loop[k];
      const Point3d& b = loop[(k + 1) % n];
      if (idx_i < 0 && PointsEqual(a, edge_p0, tol) && PointsEqual(b, edge_p1, tol)) {
        idx_i = static_cast<int>(f);
      }
      if (idx_j < 0 && PointsEqual(a, edge_p1, tol) && PointsEqual(b, edge_p0, tol)) {
        idx_j = static_cast<int>(f);
      }
    }
  }
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: edge_p0->edge_p1 is not a "
        "shared boundary edge of two distinct faces of `solid`, walked in "
        "opposite directions on their own loops - see FilletConvexEdge's own "
        "doc comment for the required topology, which this function shares");
  }

  const ON_Plane& plane_i = faces[static_cast<size_t>(idx_i)].plane;
  const ON_Plane& plane_j = faces[static_cast<size_t>(idx_j)].plane;
  const Vector3d n_i = plane_i.zaxis;
  const Vector3d n_j = plane_j.zaxis;

  Vector3d e = edge_p1 - edge_p0;
  const double L = edge_p0.DistanceTo(edge_p1);
  if (!e.Unitize()) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdgeTapered: edge_p0 and edge_p1 coincide");
  }

  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double sweep_angle0 = std::acos(dot_ij);
  const double theta = ON_PI - sweep_angle0;
  if (!(theta > 0.0) || !(theta < ON_PI)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: edge is not a convex dihedral "
        "edge (interior angle theta is <= 0 or >= pi) - concave/degenerate "
        "edges are out of scope, see FilletConvexEdge's own doc comment");
  }

  Vector3d bis = n_i + n_j;
  if (!bis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: the two adjacent faces' "
        "normals sum to (near) zero - a degenerate (near-180-degree) dihedral");
  }
  const double cosb = bis * n_i;
  if (cosb < 1e-9) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate bisector geometry "
        "(cosb too small)");
  }

  const Vector3d k_i = n_i - bis * (1.0 / cosb);
  const Vector3d k_j = n_j - bis * (1.0 / cosb);

  const size_t n_stations = stations.size();
  const size_t n_segs = n_stations - 1;

  // Global, piecewise-linear r(t): the SAME r(t) every segment's own
  // local r_lo/r_hi are sampled from, so rail_i(t)/rail_j(t) below are
  // genuinely continuous across every station (see fillet.h's own doc
  // comment) - not merely two independently-evaluated one-sided limits.
  auto r_of = [&](double t) {
    size_t k = 0;
    while (k + 1 < n_segs && t > stations[k + 1].t) ++k;
    const double t_lo = stations[k].t, t_hi = stations[k + 1].t;
    const double r_lo = stations[k].radius, r_hi = stations[k + 1].radius;
    return r_lo + (r_hi - r_lo) * (t - t_lo) / (t_hi - t_lo);
  };
  auto rail_i = [&](double t) { return edge_p0 + t * e + r_of(t) * k_i; };
  auto rail_j = [&](double t) { return edge_p0 + t * e + r_of(t) * k_j; };

  std::vector<TaperedConeSegment> segs;
  segs.reserve(n_segs);
  std::vector<Brep::ConicalFace> conical_faces(n_segs);
  for (size_t k = 0; k < n_segs; ++k) {
    const Point3d seg_p0 = edge_p0 + stations[k].t * e;
    const double Lseg = stations[k + 1].t - stations[k].t;
    TaperedConeSegment seg =
        BuildTaperedConeSegment(seg_p0, Lseg, stations[k].radius, stations[k + 1].radius, e, bis, cosb, n_i, dot_ij);
    Brep::ConicalFace cf;
    cf.frame.origin = seg.apex;
    cf.frame.xaxis = seg.xaxis;
    cf.frame.yaxis = seg.yaxis;
    cf.frame.zaxis = seg.u_hat;
    cf.frame.UpdateEquation();
    cf.radius0 = seg.radius0_true;
    cf.radius1 = seg.radius1_true;
    cf.angle = seg.cone_sweep_angle;
    cf.length = seg.length_true;
    conical_faces[k] = std::move(cf);
    segs.push_back(std::move(seg));
  }

  // --- interior-station cap joins (fillet.h's own doc comment): the
  // EARLIER segment's own natural v1 circle, sampled explicitly at
  // kNotchSamples+1 points (the plain isocurve, made explicit as points -
  // the same formula ellipse_pt/cap_point use elsewhere in this file,
  // just with h fixed at v1 rather than solved from a cutting-plane
  // equation), becomes the LITERAL shared boundary with the LATER
  // segment's own cap0.
  for (size_t k = 0; k + 1 < n_segs; ++k) {
    const TaperedConeSegment& segA = segs[k];
    const TaperedConeSegment& segB = segs[k + 1];
    const double h1A = segA.radius1_true / segA.tan_half_angle;
    const Point3d centerA = segA.apex + h1A * segA.u_hat;

    std::vector<Point3d> shared;
    shared.reserve(static_cast<size_t>(kNotchSamples) + 1);
    for (int s = 0; s <= kNotchSamples; ++s) {
      const double phi = segA.cone_sweep_angle * static_cast<double>(s) / kNotchSamples;
      shared.push_back(centerA + segA.radius1_true * (std::cos(phi) * segA.xaxis + std::sin(phi) * segA.yaxis));
    }

    // Genuine sagitta-style bound on segA's OWN polyline-vs-true-curve
    // error (shrinks with kNotchSamples, mirroring
    // EllipseNotchCornerAtVertex's own max_sagitta) and the genuine,
    // NON-shrinking radial deviation of these SAME points from segB's
    // own true cone surface (see Brep::ConicalFace::
    // cap0_surface_fit_tolerance's own doc comment for exactly what this
    // measures and why it's needed): both are computed here and the
    // WORSE of the two is stored on BOTH sides, so the shared
    // ON_BrepEdge's own m_tolerance is an honest bound regardless of
    // which segment FromMixedFaces() happens to visit first (segA is
    // always added to `conical_faces` before segB, so segA's own cap1
    // normally wins that race - but storing the same conservative bound
    // on both sides costs nothing and does not depend on that ordering).
    double max_sagittaA = 0.0;
    for (int s = 0; s < kNotchSamples; ++s) {
      const double phi_mid = segA.cone_sweep_angle * (static_cast<double>(s) + 0.5) / kNotchSamples;
      const Point3d chord_mid = 0.5 * (shared[static_cast<size_t>(s)] + shared[static_cast<size_t>(s) + 1]);
      const Point3d true_mid =
          centerA + segA.radius1_true * (std::cos(phi_mid) * segA.xaxis + std::sin(phi_mid) * segA.yaxis);
      max_sagittaA = std::max(max_sagittaA, chord_mid.DistanceTo(true_mid));
    }

    double max_deviationB = 0.0;
    for (const Point3d& p : shared) {
      const Vector3d d = p - segB.apex;
      const double height = d * segB.u_hat;
      const double x = d * segB.xaxis, y = d * segB.yaxis;
      const double true_radius = segB.tan_half_angle * height;
      const double actual_radial = std::sqrt(x * x + y * y);
      max_deviationB = std::max(max_deviationB, std::fabs(actual_radial - true_radius));
    }
    // Small safety margin so this precomputed bound - measured via this
    // function's own formula - is never defeated by an infinitesimally
    // different floating-point path inside FromMixedFaces()'s own
    // independent re-derivation of the same quantity (brep.cpp's own
    // notch_uv).
    const double combined_tol = std::max(max_sagittaA, max_deviationB) * (1.0 + 1e-9) + 1e-12;

    conical_faces[k].cap1_notch_points = shared;
    conical_faces[k].cap1_notch_tolerance = combined_tol;
    conical_faces[k + 1].cap0_notch_points = shared;
    conical_faces[k + 1].cap0_notch_tolerance = combined_tol;
    conical_faces[k + 1].cap0_surface_fit_tolerance = combined_tol;
  }

  // --- splice the FULL piecewise rail_i(t)/rail_j(t) polyline into faces
  // i/j (see SpliceLoopEdge's own doc comment above).
  std::vector<Point3d> railI_pts, railJ_pts;
  railI_pts.reserve(n_stations);
  railJ_pts.reserve(n_stations);
  for (size_t k = 0; k < n_stations; ++k) railI_pts.push_back(rail_i(stations[k].t));
  for (size_t k = n_stations; k-- > 0;) railJ_pts.push_back(rail_j(stations[k].t));

  Brep::PlanarFace retrimmed_i = faces[static_cast<size_t>(idx_i)];
  retrimmed_i.loop = SpliceLoopEdge(retrimmed_i.loop, edge_p0, edge_p1, railI_pts, tol);
  Brep::PlanarFace retrimmed_j = faces[static_cast<size_t>(idx_j)];
  retrimmed_j.loop = SpliceLoopEdge(retrimmed_j.loop, edge_p1, edge_p0, railJ_pts, tol);

  // --- assemble the remaining untouched faces, apply the corner-notch at
  // only the two outer endpoints (first segment's own cone at edge_p0,
  // last segment's own cone at edge_p1 - EllipseNotchCornerAtVertex
  // itself needs no changes at all), and build.
  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) {
      others.push_back(faces[f]);
    }
  }

  const TaperedConeSegment& first = segs.front();
  const TaperedConeSegment& last = segs.back();
  EllipseNotchCornerAtVertex(others, edge_p0, e, plane_i, plane_j, first.apex, first.u_hat, first.xaxis, first.yaxis,
                              first.tan_half_angle, first.cone_sweep_angle, tol, conical_faces.front().cap0_notch_points,
                              conical_faces.front().cap0_notch_tolerance);
  EllipseNotchCornerAtVertex(others, edge_p1, e, plane_i, plane_j, last.apex, last.u_hat, last.xaxis, last.yaxis,
                              last.tan_half_angle, last.cone_sweep_angle, tol, conical_faces.back().cap1_notch_points,
                              conical_faces.back().cap1_notch_tolerance);

  std::vector<Brep::PlanarFace> mixed_planar = std::move(others);
  mixed_planar.push_back(std::move(retrimmed_i));
  mixed_planar.push_back(std::move(retrimmed_j));

  return Brep::FromMixedFaces(mixed_planar, {}, conical_faces);
}

}  // namespace

Brep FilletConvexEdgeTapered(const Brep& solid, Point3d edge_p0, Point3d edge_p1,
                              const std::vector<FilletRadiusStation>& stations) {
  if (stations.size() < 2) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: `stations` must have at "
        "least 2 entries (the two outer endpoints)");
  }
  for (const FilletRadiusStation& s : stations) {
    if (!(s.radius > 0.0)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdgeTapered: every station's radius "
          "must be strictly positive (a radius reaching zero would put the "
          "swept patch's own apex INSIDE the trimmed region - a genuinely "
          "different, out-of-scope topology - see this function's own doc "
          "comment)");
    }
  }

  const double L = edge_p0.DistanceTo(edge_p1);
  const double t_tol = std::max(1e-9, L * 1e-9);
  if (std::fabs(stations.front().t - 0.0) > t_tol) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: stations.front().t must be "
        "0.0 (arc length is measured from edge_p0)");
  }
  if (std::fabs(stations.back().t - L) > t_tol) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeTapered: stations.back().t must equal "
        "edge_p0.DistanceTo(edge_p1) - the profile must span the whole edge");
  }
  for (size_t k = 1; k < stations.size(); ++k) {
    if (!(stations[k].t > stations[k - 1].t + t_tol)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdgeTapered: `stations` must be sorted "
          "by strictly increasing t");
    }
  }

  double max_radius = 0.0;
  for (const FilletRadiusStation& s : stations) max_radius = std::max(max_radius, s.radius);
  const double radius_tol = std::max(1e-9, max_radius * 1e-9);

  if (stations.size() == 2) {
    if (std::fabs(stations[1].radius - stations[0].radius) <= radius_tol) {
      return FilletConvexEdge(solid, edge_p0, edge_p1, stations[0].radius);
    }
    return BuildTwoStationTaperedFillet(solid, edge_p0, edge_p1, stations[0].radius, stations[1].radius);
  }

  // stations.size() >= 3: reject a locally-flat sub-segment (would need a
  // CylindricalFace mixed into the middle of the run, out of scope here
  // - see this function's own doc comment) and reject a non-monotonic
  // profile (an interior radius extremum, out of scope - see this
  // function's own doc comment for the checked-directly reason).
  const bool increasing_overall = stations.back().radius > stations.front().radius;
  for (size_t k = 1; k < stations.size(); ++k) {
    const double d = stations[k].radius - stations[k - 1].radius;
    if (std::fabs(d) <= radius_tol) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdgeTapered: two consecutive stations "
          "have (near-)equal radius inside a >2-station profile - a locally- "
          "flat sub-segment would need a CylindricalFace mixed into the "
          "middle of the run, out of scope for this function (see its own "
          "doc comment); the only supported flat case is a top-level "
          "2-station profile, which dispatches to FilletConvexEdge");
    }
    if (increasing_overall != (d > 0.0)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdgeTapered: `stations`' own radii are "
          "not monotonic (non-decreasing or non-increasing) across the whole "
          "profile - an interior radius extremum is out of scope for this "
          "function (see its own doc comment for the checked-directly reason)");
    }
  }

  return BuildMultiStationTaperedFillet(solid, edge_p0, edge_p1, stations);
}

Brep FilletConvexEdgeTapered(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius0, double radius1) {
  return FilletConvexEdgeTapered(solid, edge_p0, edge_p1,
                                  std::vector<FilletRadiusStation>{{0.0, radius0}, {edge_p0.DistanceTo(edge_p1), radius1}});
}


// ---------------------------------------------------------------------------
// Exact planar chamfer (see fillet.h's own ChamferConvexEdge doc comment for
// the construction this implements step by step).

namespace {

// Locates the two faces of `faces` sharing the directed edge (p0 -> p1 on
// face i, p1 -> p0 on face j) - verbatim the same topology test
// FilletConvexEdge's own step (1) uses, factored for ChamferConvexEdge.
// Throws std::invalid_argument (with `who` in the message) if not found.
void FindEdgeFaces(const std::vector<Brep::PlanarFace>& faces, const Point3d& p0, const Point3d& p1,
                   double tol, const char* who, int& idx_i, int& idx_j) {
  idx_i = -1;
  idx_j = -1;
  for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const Point3d& a = loop[k];
      const Point3d& b = loop[(k + 1) % n];
      if (idx_i < 0 && PointsEqual(a, p0, tol) && PointsEqual(b, p1, tol)) idx_i = static_cast<int>(f);
      if (idx_j < 0 && PointsEqual(a, p1, tol) && PointsEqual(b, p0, tol)) idx_j = static_cast<int>(f);
    }
  }
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                ": edge_p0->edge_p1 is not a shared boundary edge of two distinct "
                                "faces of `solid`, walked in opposite directions on their own loops - "
                                "see FilletConvexEdge's own doc comment for the required topology");
  }
}

// One end of a chamfer: the two rail points Q_i/Q_j where the chamfer's
// own end edge sits (see fillet.h's own step 5), plus every third face
// re-cornered there. `vertex` is edge_p0 or edge_p1; `R_i`/`R_j` are the
// rails' own points in the plane through `vertex` perpendicular to `e`
// (vertex + distance*m). Returns Q_i/Q_j via the out-params; leaves them
// equal to R_i/R_j when no third face touches `vertex` at all.
void ChamferEndAtVertex(std::vector<Brep::PlanarFace>& other_faces, const Point3d& vertex, const Vector3d& e,
                        const ON_Plane& plane_i, const ON_Plane& plane_j, const Point3d& R_i, const Point3d& R_j,
                        double tol, Point3d& Q_i_out, Point3d& Q_j_out) {
  Q_i_out = R_i;
  Q_j_out = R_j;
  int touching = 0, matched = 0;
  for (Brep::PlanarFace& f : other_faces) {
    std::vector<Point3d>& loop = f.loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      if (loop[k].DistanceTo(vertex) > tol) continue;
      ++touching;
      const Point3d& pred = loop[(k + n - 1) % n];
      const Point3d& succ = loop[(k + 1) % n];
      const bool pred_on_i = std::fabs(plane_i.DistanceTo(pred)) <= tol;
      const bool pred_on_j = std::fabs(plane_j.DistanceTo(pred)) <= tol;
      const bool succ_on_i = std::fabs(plane_i.DistanceTo(succ)) <= tol;
      const bool succ_on_j = std::fabs(plane_j.DistanceTo(succ)) <= tol;
      bool i_first;
      if (pred_on_i && succ_on_j && !pred_on_j && !succ_on_i) {
        i_first = true;   // walk: (on i) -> vertex -> (on j)  ==>  splice Q_i, Q_j
      } else if (pred_on_j && succ_on_i && !pred_on_i && !succ_on_j) {
        i_first = false;  // walk: (on j) -> vertex -> (on i)  ==>  splice Q_j, Q_i
      } else {
        break;  // not the trihedral pattern on this face; counted as touching only
      }
      const Vector3d n_k = f.plane.zaxis;
      const double e_dot_n = e * n_k;
      if (std::fabs(e_dot_n) < 1e-9) {
        throw std::invalid_argument(
            "dino8::kernel::ChamferConvexEdge: a third face at an edge endpoint is "
            "parallel to the edge itself - the chamfer's own rails never pierce its "
            "plane (degenerate vertex geometry)");
      }
      // Q = R + t*e with (Q - vertex).n_k == 0  (R - vertex is perpendicular
      // to e, so this is a single linear equation in t; for a face
      // perpendicular to e, (R - vertex).n_k == 0 already and t == 0).
      const Point3d Q_i = R_i - (((R_i - vertex) * n_k) / e_dot_n) * e;
      const Point3d Q_j = R_j - (((R_j - vertex) * n_k) / e_dot_n) * e;
      // Each Q must lie ON face k's own existing edge toward that
      // neighbour (between the vertex and the neighbour), else the
      // chamfer overruns the third face.
      auto along = [&](const Point3d& Q, const Point3d& neighbour) {
        const Vector3d w = neighbour - vertex;
        const double len2 = w * w;
        if (len2 <= 0.0) return -1.0;
        const double s = ((Q - vertex) * w) / len2;
        // Q must also actually sit on that line (its perpendicular
        // distance from the line must vanish) - a checked invariant of
        // step 5's own argument, not merely trusted.
        const Point3d foot = vertex + s * w;
        if (foot.DistanceTo(Q) > 1e3 * tol) return -1.0;
        return s;
      };
      const Point3d& nb_i = i_first ? pred : succ;
      const Point3d& nb_j = i_first ? succ : pred;
      const double s_i = along(Q_i, nb_i);
      const double s_j = along(Q_j, nb_j);
      if (s_i < 0.0 || s_j < 0.0 || s_i > 1.0 + 1e-9 || s_j > 1.0 + 1e-9) {
        throw std::invalid_argument(
            "dino8::kernel::ChamferConvexEdge: the chamfer overruns a third face at an "
            "edge endpoint (a rail pierces that face's plane beyond the far end of the "
            "face's own edge, or off that edge entirely) - distances too large for this "
            "solid's geometry");
      }
      // A rail piercing face k EXACTLY at that edge's far vertex (s == 1) is
      // legitimate, not an overrun: the chamfer's end edge then simply
      // terminates at an existing vertex of the solid, which becomes a
      // valence-4 vertex. The one producer this kernel itself has is two
      // equal-setback chamfers meeting at a box corner (the second
      // chamfer's plane passes exactly through the first chamfer face's
      // own corner on the shared side face) - checked directly by
      // dino8-kernel's own chained-chamfer test. That vertex is already in
      // face k's loop, so the splice below must not insert it a second
      // time (a zero-length loop edge is not a shape FromMixedFaces or
      // the exact-clip tessellator is meant for).
      const bool q_i_is_nb = Q_i.DistanceTo(nb_i) <= tol;
      const bool q_j_is_nb = Q_j.DistanceTo(nb_j) <= tol;
      if (matched > 0 && (Q_i.DistanceTo(Q_i_out) > tol || Q_j.DistanceTo(Q_j_out) > tol)) {
        throw std::invalid_argument(
            "dino8::kernel::ChamferConvexEdge: two different third faces meet the edge "
            "at the same endpoint with different planes - not a manifold trihedral "
            "vertex; out of scope");
      }
      Q_i_out = Q_i;
      Q_j_out = Q_j;
      ++matched;

      std::vector<Point3d> new_loop;
      new_loop.reserve(n + 1);
      for (size_t m = 0; m < n; ++m) {
        if (m == k) {
          if (i_first) {
            if (!q_i_is_nb) new_loop.push_back(Q_i);
            if (!q_j_is_nb) new_loop.push_back(Q_j);
          } else {
            if (!q_j_is_nb) new_loop.push_back(Q_j);
            if (!q_i_is_nb) new_loop.push_back(Q_i);
          }
        } else {
          new_loop.push_back(loop[m]);
        }
      }
      loop = std::move(new_loop);
      break;  // this face's corner is done
    }
  }
  if (touching > 0 && matched == 0) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdge: faces meet the edge at an endpoint but none "
        "has the simple trihedral corner pattern (its two loop neighbours on faces i "
        "and j) - a genuine vertex-blend problem this function does not attempt, see "
        "fillet.h's own doc comment");
  }
}

}  // namespace

Brep ChamferConvexEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                        double distance_j) {
  if (!(distance_i > 0.0) || !(distance_j > 0.0)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdge: distance_i and distance_j must both be strictly positive");
  }

  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  int idx_i = -1, idx_j = -1;
  FindEdgeFaces(faces, edge_p0, edge_p1, tol, "ChamferConvexEdge", idx_i, idx_j);
  const Brep::PlanarFace& face_i = faces[static_cast<size_t>(idx_i)];
  const Brep::PlanarFace& face_j = faces[static_cast<size_t>(idx_j)];
  const ON_Plane& plane_i = face_i.plane;
  const ON_Plane& plane_j = face_j.plane;
  const Vector3d n_i = plane_i.zaxis;
  const Vector3d n_j = plane_j.zaxis;

  Vector3d e = edge_p1 - edge_p0;
  if (!e.Unitize()) {
    throw std::invalid_argument("dino8::kernel::ChamferConvexEdge: edge_p0 and edge_p1 coincide");
  }

  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double theta = ON_PI - std::acos(dot_ij);  // interior dihedral angle
  if (!(theta > 1e-9) || !(theta < ON_PI - 1e-9)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdge: edge is not a convex dihedral edge (interior "
        "angle theta is <= 0 or >= pi) - concave/degenerate edges are out of scope, see "
        "FilletConvexEdge's own doc comment");
  }

  // Into-material, in-plane, perpendicular-to-the-edge directions. For a
  // CCW-outward loop walking p0 -> p1 the interior is to the LEFT, i.e.
  // along n x e; checked against the face's own vertex extent rather
  // than trusted (a sign error here would chamfer thin air).
  auto extent_along = [](const std::vector<Point3d>& loop, const Vector3d& m, const Point3d& ref) {
    double best = -std::numeric_limits<double>::infinity();
    for (const Point3d& v : loop) best = std::max(best, m * (v - ref));
    return best;
  };
  Vector3d m_i = ON_CrossProduct(n_i, e);
  Vector3d m_j = ON_CrossProduct(n_j, -e);
  if (!m_i.Unitize() || !m_j.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdge: degenerate face/edge geometry (a face normal is "
        "parallel to the edge)");
  }
  if (extent_along(face_i.loop, m_i, edge_p0) <= tol) m_i = -m_i;
  if (extent_along(face_j.loop, m_j, edge_p0) <= tol) m_j = -m_j;
  const double extent_i = extent_along(face_i.loop, m_i, edge_p0);
  const double extent_j = extent_along(face_j.loop, m_j, edge_p0);
  if (!(distance_i < extent_i - tol) || !(distance_j < extent_j - tol)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdge: a chamfer distance is too large to fit - it "
        "reaches or exceeds that face's own extent from the edge");
  }

  // Rails: R_i(t) = edge_p0 + distance_i*m_i + t*e, likewise R_j.
  const Point3d R_i0 = edge_p0 + distance_i * m_i;
  const Point3d R_j0 = edge_p0 + distance_j * m_j;
  const Point3d R_i1 = edge_p1 + distance_i * m_i;
  const Point3d R_j1 = edge_p1 + distance_j * m_j;

  // Re-trim faces i/j by their own rail line (ClipByHalfspace3d keeps the
  // side the plane normal points AWAY from - see FilletConvexEdge).
  // When the rail passes EXACTLY through an existing loop vertex (the
  // chained equal-setback corner case - see ChamferEndAtVertex's own
  // s == 1 comment), Sutherland-Hodgman emits that vertex twice (once as
  // the kept vertex, once as the crossing point at t == 0), leaving a
  // zero-length loop edge that FromMixedFaces would turn into a
  // degenerate trim and a non-manifold vertex. Collapse consecutive
  // coincident points (wraparound included) - a pure representation
  // clean-up, not a geometric change.
  auto dedupe_consecutive = [tol](std::vector<Point3d>& loop) {
    std::vector<Point3d> out;
    out.reserve(loop.size());
    for (const Point3d& p : loop) {
      if (!out.empty() && out.back().DistanceTo(p) <= tol) continue;
      out.push_back(p);
    }
    while (out.size() > 1 && out.front().DistanceTo(out.back()) <= tol) out.pop_back();
    loop = std::move(out);
  };
  Brep::PlanarFace retrimmed_i = face_i;
  retrimmed_i.loop = detail::ClipByHalfspace3d(retrimmed_i.loop, ON_Plane(R_i0, -m_i), tol);
  dedupe_consecutive(retrimmed_i.loop);
  Brep::PlanarFace retrimmed_j = face_j;
  retrimmed_j.loop = detail::ClipByHalfspace3d(retrimmed_j.loop, ON_Plane(R_j0, -m_j), tol);
  dedupe_consecutive(retrimmed_j.loop);
  if (retrimmed_i.loop.size() < 3 || retrimmed_j.loop.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdge: re-trimming an adjacent face left fewer than 3 "
        "vertices - distances too large for this solid's geometry");
  }

  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) others.push_back(faces[f]);
  }

  // End conditions (step 5): the chamfer quad's own four corners are the
  // rail/third-face piercing points at each end.
  Point3d Q_i0, Q_j0, Q_i1, Q_j1;
  ChamferEndAtVertex(others, edge_p0, e, plane_i, plane_j, R_i0, R_j0, tol, Q_i0, Q_j0);
  ChamferEndAtVertex(others, edge_p1, e, plane_i, plane_j, R_i1, R_j1, tol, Q_i1, Q_j1);

  // The chamfer face itself: outward normal n_c strictly between n_i and
  // n_j, loop wound CCW as seen from outside (checked via its own Newell
  // normal against n_i + n_j, not assumed from the corner order).
  Brep::PlanarFace chamfer;
  std::vector<Point3d> quad = {Q_i0, Q_i1, Q_j1, Q_j0};
  Vector3d newell(0, 0, 0);
  for (size_t k = 0; k < quad.size(); ++k) {
    const Point3d& a = quad[k];
    const Point3d& b = quad[(k + 1) % quad.size()];
    newell.x += (a.y - b.y) * (a.z + b.z);
    newell.y += (a.z - b.z) * (a.x + b.x);
    newell.z += (a.x - b.x) * (a.y + b.y);
  }
  if (newell * (n_i + n_j) < 0.0) std::reverse(quad.begin(), quad.end());
  Vector3d n_c = ON_CrossProduct(quad[1] - quad[0], quad[2] - quad[0]);
  if (!n_c.Unitize() || n_c * (n_i + n_j) <= 0.0) {
    throw std::runtime_error(
        "dino8::kernel::ChamferConvexEdge: degenerate chamfer quad - please report this as a bug");
  }
  chamfer.plane = ON_Plane(quad[0], n_c);
  chamfer.loop = std::move(quad);

  std::vector<Brep::PlanarFace> all = std::move(others);
  all.push_back(std::move(retrimmed_i));
  all.push_back(std::move(retrimmed_j));
  all.push_back(std::move(chamfer));
  return Brep::FromMixedFaces(all, {});
}

Brep ChamferConvexEdgeAngle(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                             double angle_from_i) {
  if (!(distance_i > 0.0)) {
    throw std::invalid_argument("dino8::kernel::ChamferConvexEdgeAngle: distance_i must be strictly positive");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  int idx_i = -1, idx_j = -1;
  FindEdgeFaces(faces, edge_p0, edge_p1, tol, "ChamferConvexEdgeAngle", idx_i, idx_j);
  const Vector3d n_i = faces[static_cast<size_t>(idx_i)].plane.zaxis;
  const Vector3d n_j = faces[static_cast<size_t>(idx_j)].plane.zaxis;
  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double theta = ON_PI - std::acos(dot_ij);
  if (!(theta > 1e-9) || !(theta < ON_PI - 1e-9)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdgeAngle: edge is not a convex dihedral edge - see "
        "ChamferConvexEdge");
  }
  if (!(angle_from_i > 1e-9) || !(angle_from_i < ON_PI - theta - 1e-9)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConvexEdgeAngle: angle_from_i must lie strictly between 0 and "
        "pi - theta (the edge's exterior angle) for the chamfer plane to reach face j");
  }
  // Law of sines in the chamfer cross-section triangle (see fillet.h).
  const double distance_j = distance_i * std::sin(angle_from_i) / std::sin(theta + angle_from_i);
  return ChamferConvexEdge(solid, edge_p0, edge_p1, distance_i, distance_j);
}

namespace {

// Shared by ChamferConcaveEdge/ChamferConcaveEdgeAngle: throws unless
// edge_p0->edge_p1 is a genuinely CONCAVE shared boundary edge of `solid`
// - see ChamferConcaveEdge's own doc comment (fillet.h) for why this
// check exists at all (arccos(n_i . n_j) alone cannot tell convex from
// concave) and why it is safe to then dispatch straight to
// ChamferConvexEdge's own construction.
void RequireConcaveEdge(const std::vector<Brep::PlanarFace>& faces, Point3d edge_p0, Point3d edge_p1, double tol,
                        const char* who) {
  int idx_i = -1, idx_j = -1;
  size_t k_i = 0;
  for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const Point3d& a = loop[k];
      const Point3d& b = loop[(k + 1) % n];
      if (idx_i < 0 && PointsEqual(a, edge_p0, tol) && PointsEqual(b, edge_p1, tol)) {
        idx_i = static_cast<int>(f);
        k_i = k;
      }
      if (idx_j < 0 && PointsEqual(a, edge_p1, tol) && PointsEqual(b, edge_p0, tol)) {
        idx_j = static_cast<int>(f);
      }
    }
  }
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                ": edge_p0->edge_p1 is not a shared boundary edge of two distinct "
                                "faces of `solid`, walked in opposite directions on their own loops");
  }
  const std::vector<Point3d>& loop_i = faces[static_cast<size_t>(idx_i)].loop;
  const ON_Plane& plane_j = faces[static_cast<size_t>(idx_j)].plane;
  const size_t k_i1 = (k_i + 1) % loop_i.size();
  bool degenerate = false;
  const bool convex = EdgeConvexity(loop_i, k_i, k_i1, plane_j, tol, &degenerate);
  if (!degenerate && convex) {
    throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                ": edge is a CONVEX dihedral edge, not concave - see ChamferConvexEdge instead");
  }
}

}  // namespace

Brep ChamferConcaveEdge(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i, double distance_j) {
  if (!(distance_i > 0.0) || !(distance_j > 0.0)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConcaveEdge: distance_i and distance_j must both be strictly positive");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  RequireConcaveEdge(faces, edge_p0, edge_p1, tol, "ChamferConcaveEdge");
  return ChamferConvexEdge(solid, edge_p0, edge_p1, distance_i, distance_j);
}

Brep ChamferConcaveEdgeAngle(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                             double angle_from_i) {
  if (!(distance_i > 0.0)) {
    throw std::invalid_argument("dino8::kernel::ChamferConcaveEdgeAngle: distance_i must be strictly positive");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  RequireConcaveEdge(faces, edge_p0, edge_p1, tol, "ChamferConcaveEdgeAngle");

  int idx_i = -1, idx_j = -1;
  FindEdgeFaces(faces, edge_p0, edge_p1, tol, "ChamferConcaveEdgeAngle", idx_i, idx_j);
  const Vector3d n_i = faces[static_cast<size_t>(idx_i)].plane.zaxis;
  const Vector3d n_j = faces[static_cast<size_t>(idx_j)].plane.zaxis;
  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  // Same expression as ChamferConvexEdgeAngle's own `theta`, and correct
  // for the concave case too - see ChamferConcaveEdge's own doc comment
  // for why arccos(m_i . m_j) (the chamfer triangle's own true vertex
  // angle) equals `pi - psi` in both the convex and the concave case.
  const double theta = ON_PI - std::acos(dot_ij);
  if (!(angle_from_i > 1e-9) || !(angle_from_i < ON_PI - theta - 1e-9)) {
    throw std::invalid_argument(
        "dino8::kernel::ChamferConcaveEdgeAngle: angle_from_i must lie strictly between 0 and "
        "pi - theta for the chamfer plane to reach face j");
  }
  const double distance_j = distance_i * std::sin(angle_from_i) / std::sin(theta + angle_from_i);
  return ChamferConcaveEdge(solid, edge_p0, edge_p1, distance_i, distance_j);
}


// ---------------------------------------------------------------------------
// Exact conic ("rho") cross-section blend (see fillet.h's own
// FilletConvexEdgeConic doc comment for the full derivation this
// implements step by step).

Brep FilletConvexEdgeConic(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double distance_i,
                            double distance_j, double rho) {
  if (!(distance_i > 0.0) || !(distance_j > 0.0)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeConic: distance_i and distance_j must both be strictly positive");
  }
  if (!(rho > 0.0) || !(rho < 1.0)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeConic: rho must lie strictly between 0 and 1 (0.5 is the exact "
        "parabola; rho -> 0 degenerates onto the flat chord, rho -> 1 onto the untouched sharp edge - see "
        "this function's own doc comment)");
  }

  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  int idx_i = -1, idx_j = -1;
  FindEdgeFaces(faces, edge_p0, edge_p1, tol, "FilletConvexEdgeConic", idx_i, idx_j);
  const Brep::PlanarFace& face_i = faces[static_cast<size_t>(idx_i)];
  const Brep::PlanarFace& face_j = faces[static_cast<size_t>(idx_j)];
  const ON_Plane& plane_i = face_i.plane;
  const ON_Plane& plane_j = face_j.plane;
  const Vector3d n_i = plane_i.zaxis;
  const Vector3d n_j = plane_j.zaxis;

  Vector3d e = edge_p1 - edge_p0;
  if (!e.Unitize()) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdgeConic: edge_p0 and edge_p1 coincide");
  }

  const double dot_ij = std::max(-1.0, std::min(1.0, n_i * n_j));
  const double theta = ON_PI - std::acos(dot_ij);  // interior dihedral angle
  if (!(theta > 1e-9) || !(theta < ON_PI - 1e-9)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeConic: edge is not a convex dihedral edge (interior angle theta is "
        "<= 0 or >= pi) - concave/degenerate edges are out of scope, see FilletConvexEdge's own doc comment");
  }

  // Into-material, in-plane, perpendicular-to-the-edge directions -
  // verbatim ChamferConvexEdge's own construction (see that function's
  // own doc comment/body).
  auto extent_along = [](const std::vector<Point3d>& loop, const Vector3d& m, const Point3d& ref) {
    double best = -std::numeric_limits<double>::infinity();
    for (const Point3d& v : loop) best = std::max(best, m * (v - ref));
    return best;
  };
  Vector3d m_i = ON_CrossProduct(n_i, e);
  Vector3d m_j = ON_CrossProduct(n_j, -e);
  if (!m_i.Unitize() || !m_j.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeConic: degenerate face/edge geometry (a face normal is parallel to "
        "the edge)");
  }
  if (extent_along(face_i.loop, m_i, edge_p0) <= tol) m_i = -m_i;
  if (extent_along(face_j.loop, m_j, edge_p0) <= tol) m_j = -m_j;
  const double extent_i = extent_along(face_i.loop, m_i, edge_p0);
  const double extent_j = extent_along(face_j.loop, m_j, edge_p0);
  if (!(distance_i < extent_i - tol) || !(distance_j < extent_j - tol)) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeConic: a setback distance is too large to fit - it reaches or "
        "exceeds that face's own extent from the edge");
  }

  // SCOPE (see this function's own doc comment): no end-condition/vertex
  // splicing in this increment - both edge_p0 and edge_p1 must be free
  // boundaries of `solid` outside faces i/j, or this throws rather than
  // silently building a self-overlapping shape.
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) == idx_i || static_cast<int>(f) == idx_j) continue;
    for (const Point3d& v : faces[f].loop) {
      if (PointsEqual(v, edge_p0, tol) || PointsEqual(v, edge_p1, tol)) {
        throw std::invalid_argument(
            "dino8::kernel::FilletConvexEdgeConic: a third face of `solid` touches edge_p0 or edge_p1 - "
            "end-condition/vertex splicing for the conic blend is out of scope for this increment (see this "
            "function's own doc comment); both edge endpoints must be free boundaries outside faces i/j");
      }
    }
  }

  // Re-trim faces i/j by their own rail line - identical to
  // ChamferConvexEdge's own step 3.
  const Point3d R_i0 = edge_p0 + distance_i * m_i;
  const Point3d R_j0 = edge_p0 + distance_j * m_j;
  Brep::PlanarFace retrimmed_i = face_i;
  retrimmed_i.loop = detail::ClipByHalfspace3d(retrimmed_i.loop, ON_Plane(R_i0, -m_i), tol);
  Brep::PlanarFace retrimmed_j = face_j;
  retrimmed_j.loop = detail::ClipByHalfspace3d(retrimmed_j.loop, ON_Plane(R_j0, -m_j), tol);
  if (retrimmed_i.loop.size() < 3 || retrimmed_j.loop.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::FilletConvexEdgeConic: re-trimming an adjacent face left fewer than 3 vertices - "
        "distances too large for this solid's geometry");
  }

  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) others.push_back(faces[f]);
  }
  std::vector<Brep::PlanarFace> all = std::move(others);
  all.push_back(std::move(retrimmed_i));
  all.push_back(std::move(retrimmed_j));
  Brep planar_shell = Brep::FromMixedFaces(all, {});

  // The conic cross-section: control polygon (P0, O, P2), O = edge_p0
  // itself (see this function's own doc comment, step 2, for why the
  // sharp edge point is exactly the right middle control point), weight
  // w = rho/(1 - rho) on O (step 3).
  const Point3d P0 = R_i0;
  const Point3d P2 = R_j0;
  const double w = rho / (1.0 - rho);
  NurbsCurve profile = NurbsCurve::FromControlPoints({P0, edge_p0, P2}, 2);
  // Weight-compensation order (step 4 of this function's own doc comment,
  // and NurbsCurve::SetWeightAt's own doc comment): pre-scale the stored
  // homogeneous numerator by w via a plain (unweighted) move FIRST, then
  // set the weight - NOT the other order, which would silently move the
  // control point to edge_p0/w instead of leaving it at edge_p0.
  profile.SetControlPointAt(1, Point3d(edge_p0.x * w, edge_p0.y * w, edge_p0.z * w));
  profile.SetWeightAt(1, w);

  // Exact translational sweep (step 5): the wall's own u=0/u=1 rails are,
  // by construction, the SAME two points/lines the re-trim above already
  // cut faces i/j along.
  Brep wall = Brep::Extrude(profile, edge_p1 - edge_p0, /*cap=*/false);

  Brep combined = Brep::Compound({planar_shell, wall});
  combined.JoinNakedEdges(tol);
  return combined;
}


// ---------------------------------------------------------------------------
// FilletConvexEdges: multi-edge constant-radius fillet with spherical
// vertex blends (see fillet.h's own doc comment for the construction).

namespace {

struct MultiEdge {
  Point3d p0, p1;
  int idx_i = -1, idx_j = -1;
  Vector3d e, n_i, n_j, bis, m_i, m_j, frame_y;
  double L = 0.0, cosb = 0.0, sweep = 0.0, theta = 0.0;
  // Set-backs along e from p0 (t_start) and toward p1 (t_end); the
  // cylinder spans [t_start, t_end], initially the whole edge.
  double t_start = 0.0, t_end = 0.0;
  // Vertex-blend bookkeeping: which of the two endpoints got a spherical
  // corner (so the m == 1 notch is skipped there).
  bool sphere_at_p0 = false, sphere_at_p1 = false;
};

}  // namespace

Brep FilletConvexEdges(const Brep& solid, const std::vector<std::pair<Point3d, Point3d>>& edges, double radius) {
  if (!(radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdges: radius must be positive");
  }
  if (edges.empty()) {
    throw std::invalid_argument("dino8::kernel::FilletConvexEdges: at least one edge is required");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  // --- per-edge geometry, verbatim FilletConvexEdge's own steps 1-2 ---
  std::vector<MultiEdge> me;
  me.reserve(edges.size());
  for (const std::pair<Point3d, Point3d>& ed : edges) {
    MultiEdge m;
    m.p0 = ed.first;
    m.p1 = ed.second;
    for (const MultiEdge& other : me) {
      if ((PointsEqual(other.p0, m.p0, tol) && PointsEqual(other.p1, m.p1, tol)) ||
          (PointsEqual(other.p0, m.p1, tol) && PointsEqual(other.p1, m.p0, tol))) {
        throw std::invalid_argument("dino8::kernel::FilletConvexEdges: an edge is listed twice");
      }
    }
    FindEdgeFaces(faces, m.p0, m.p1, tol, "FilletConvexEdges", m.idx_i, m.idx_j);
    const ON_Plane& plane_i = faces[static_cast<size_t>(m.idx_i)].plane;
    const ON_Plane& plane_j = faces[static_cast<size_t>(m.idx_j)].plane;
    m.n_i = plane_i.zaxis;
    m.n_j = plane_j.zaxis;
    m.e = m.p1 - m.p0;
    m.L = m.p0.DistanceTo(m.p1);
    if (!m.e.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConvexEdges: an edge's two endpoints coincide");
    }
    const double dot_ij = std::max(-1.0, std::min(1.0, m.n_i * m.n_j));
    m.sweep = std::acos(dot_ij);
    m.theta = ON_PI - m.sweep;
    if (!(m.theta > 0.0) || !(m.theta < ON_PI)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdges: an edge is not a convex dihedral edge (interior angle theta is <= 0 or "
          ">= pi) - concave/degenerate edges are out of scope, see FilletConvexEdge's own doc comment");
    }
    m.bis = m.n_i + m.n_j;
    if (!m.bis.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConvexEdges: degenerate (near-180-degree) dihedral");
    }
    m.cosb = m.bis * m.n_i;
    if (m.cosb < 1e-9) {
      throw std::invalid_argument("dino8::kernel::FilletConvexEdges: degenerate bisector geometry (cosb too small)");
    }
    const double offset = radius / m.cosb;
    const Point3d contact_i0 = m.p0 - m.bis * offset + m.n_i * radius;
    const Point3d contact_j1 = m.p1 - m.bis * offset + m.n_j * radius;
    m.m_i = ON_CrossProduct(m.n_i, m.e);
    m.m_j = ON_CrossProduct(m.n_j, -m.e);
    if (!m.m_i.Unitize() || !m.m_j.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConvexEdges: degenerate face/edge geometry (a face normal is parallel to the edge)");
    }
    if (m.m_i * (m.p0 - contact_i0) >= 0.0) m.m_i = -m.m_i;
    if (m.m_j * (m.p1 - contact_j1) >= 0.0) m.m_j = -m.m_j;
    const double trim_back = radius / std::tan(m.theta / 2.0);
    auto max_extent_from_edge = [](const std::vector<Point3d>& loop, const Vector3d& mm, const Point3d& edge_ref) {
      double best = 0.0;
      for (const Point3d& v : loop) best = std::max(best, mm * (v - edge_ref));
      return best;
    };
    if (trim_back > max_extent_from_edge(faces[static_cast<size_t>(m.idx_i)].loop, m.m_i, m.p0) ||
        trim_back > max_extent_from_edge(faces[static_cast<size_t>(m.idx_j)].loop, m.m_j, m.p1)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdges: radius is too large to fit - a fillet's own trim-back distance exceeds "
          "one of the adjacent faces' extent from the edge");
    }
    m.frame_y = ON_CrossProduct(m.e, m.n_i);
    if (!m.frame_y.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConvexEdges: degenerate fillet frame");
    }
    m.t_start = 0.0;
    m.t_end = m.L;
    me.push_back(m);
  }

  // --- distinct vertices touched by filleted edges ---
  struct VertexUse {
    Point3d p;
    std::vector<std::pair<int, int>> uses;  // (edge index, end 0 or 1)
  };
  std::vector<VertexUse> verts;
  for (size_t k = 0; k < me.size(); ++k) {
    for (int end = 0; end < 2; ++end) {
      const Point3d& p = end == 0 ? me[k].p0 : me[k].p1;
      bool found = false;
      for (VertexUse& v : verts) {
        if (PointsEqual(v.p, p, tol)) {
          v.uses.emplace_back(static_cast<int>(k), end);
          found = true;
          break;
        }
      }
      if (!found) verts.push_back({p, {{static_cast<int>(k), end}}});
    }
  }

  // Working copies of every planar face (re-trimmed / notched below) and
  // the corner spheres.
  std::vector<Brep::PlanarFace> work = faces;
  std::vector<Brep::SphericalFace> spheres;

  auto faces_touching = [&](const Point3d& p) {
    std::vector<int> out;
    for (size_t f = 0; f < faces.size(); ++f) {
      for (const Point3d& q : faces[f].loop) {
        if (PointsEqual(q, p, tol)) {
          out.push_back(static_cast<int>(f));
          break;
        }
      }
    }
    return out;
  };

  for (VertexUse& v : verts) {
    const int m_count = static_cast<int>(v.uses.size());
    if (m_count == 1) continue;  // handled after the re-trim, with FilletConvexEdge's own notch
    const std::vector<int> touching = faces_touching(v.p);
    if (m_count != 3 || touching.size() != 3) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdges: a vertex has " + std::to_string(m_count) + " filleted edge(s) and " +
          std::to_string(touching.size()) +
          " incident face(s) - only a lone filleted edge (m == 1) or all three edges of a trihedral corner (m == 3, "
          "valence 3) are supported vertex configurations, see fillet.h's own doc comment");
    }
    // The three edges' faces must be exactly the three touching faces.
    for (const std::pair<int, int>& u : v.uses) {
      const MultiEdge& ed = me[static_cast<size_t>(u.first)];
      if (std::find(touching.begin(), touching.end(), ed.idx_i) == touching.end() ||
          std::find(touching.begin(), touching.end(), ed.idx_j) == touching.end()) {
        throw std::invalid_argument(
            "dino8::kernel::FilletConvexEdges: a filleted edge at a trihedral corner is bounded by a face that does "
            "not touch that corner - inconsistent topology");
      }
    }
    const Vector3d na = faces[static_cast<size_t>(touching[0])].plane.zaxis;
    const Vector3d nb = faces[static_cast<size_t>(touching[1])].plane.zaxis;
    const Vector3d nc = faces[static_cast<size_t>(touching[2])].plane.zaxis;
    // Ball center C: n_f . (C - V) = -radius for all three planes.
    const double det = na * ON_CrossProduct(nb, nc);
    if (std::fabs(det) < 1e-9) {
      throw std::invalid_argument("dino8::kernel::FilletConvexEdges: degenerate trihedral corner (coplanar normals)");
    }
    // Cramer's rule on [na; nb; nc] X = (-r, -r, -r).
    const Vector3d rhs(-radius, -radius, -radius);
    const Vector3d X(
        (rhs.x * (nb.y * nc.z - nb.z * nc.y) - na.y * (rhs.y * nc.z - nb.z * rhs.z) + na.z * (rhs.y * nc.y - nb.y * rhs.z)) / det,
        (na.x * (rhs.y * nc.z - nb.z * rhs.z) - rhs.x * (nb.x * nc.z - nb.z * nc.x) + na.z * (nb.x * rhs.z - rhs.y * nc.x)) / det,
        (na.x * (nb.y * rhs.z - rhs.y * nc.y) - na.y * (nb.x * rhs.z - rhs.y * nc.x) + rhs.x * (nb.x * nc.y - nb.y * nc.x)) / det);
    const Point3d C = v.p + X;
    // Checked invariants: C is at distance radius inside every plane, and
    // lies on each incident fillet's own axis.
    for (const Vector3d& n : {na, nb, nc}) {
      if (std::fabs(n * (C - v.p) + radius) > 1e3 * tol) {
        throw std::runtime_error("dino8::kernel::FilletConvexEdges: ball center solve failed - please report this as a bug");
      }
    }
    // Pole face: perpendicular to the other two.
    int pole = -1;
    const Vector3d ns[3] = {na, nb, nc};
    for (int c = 0; c < 3 && pole < 0; ++c) {
      const Vector3d& p = ns[c];
      const Vector3d& q = ns[(c + 1) % 3];
      const Vector3d& r = ns[(c + 2) % 3];
      if (std::fabs(p * q) <= 1e-9 && std::fabs(p * r) <= 1e-9) pole = c;
    }
    if (pole < 0) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdges: a trihedral corner has no face perpendicular to the other two, so its "
          "spherical blend is a general spherical triangle (two of its three great-circle arcs would not be "
          "isocurves of any latitude/longitude parameterization) - out of scope, see fillet.h's own doc comment");
    }
    const int pole_face = touching[pole];
    // The equator edge: the one whose two faces are the two NON-pole faces.
    int eq_edge = -1;
    for (const std::pair<int, int>& u : v.uses) {
      const MultiEdge& ed = me[static_cast<size_t>(u.first)];
      if (ed.idx_i != pole_face && ed.idx_j != pole_face) eq_edge = u.first;
    }
    if (eq_edge < 0) {
      throw std::runtime_error("dino8::kernel::FilletConvexEdges: could not identify the equator edge at a corner - please report this as a bug");
    }
    const MultiEdge& eq = me[static_cast<size_t>(eq_edge)];
    // Set-backs and axis checks for all three edges.
    for (const std::pair<int, int>& u : v.uses) {
      MultiEdge& ed = me[static_cast<size_t>(u.first)];
      const Vector3d d = C - v.p;
      const double t_along_e = d * ed.e;
      // Set-back measured INTO the edge from this endpoint: along +e from
      // p0, along -e from p1.
      const double t = (u.second == 0) ? t_along_e : -t_along_e;
      const Vector3d perp = d - t_along_e * ed.e;
      const Vector3d expected_perp = -ed.bis * (radius / ed.cosb);
      if (t <= tol || (perp - expected_perp).Length() > 1e3 * tol) {
        throw std::runtime_error("dino8::kernel::FilletConvexEdges: the corner ball center does not lie on an incident fillet's axis - please report this as a bug");
      }
      if (u.second == 0) {
        ed.t_start = t;
        ed.sphere_at_p0 = true;
      } else {
        ed.t_end = ed.L - t;
        ed.sphere_at_p1 = true;
      }
    }
    // Sphere frame: equator arc parameterized exactly like the equator
    // cylinder's own cap (xaxis = its frame.xaxis = n_i, yaxis = its
    // frame.yaxis = e x n_i), pole along +-n_pole.
    Brep::SphericalFace sf;
    const Vector3d x = eq.n_i;
    const Vector3d y = eq.frame_y;
    Vector3d z = ON_CrossProduct(x, y);
    z.Unitize();
    const Vector3d n_pole = faces[static_cast<size_t>(pole_face)].plane.zaxis;
    const double zdot = z * n_pole;
    if (std::fabs(std::fabs(zdot) - 1.0) > 1e-9) {
      throw std::runtime_error("dino8::kernel::FilletConvexEdges: sphere frame is not aligned with the pole face normal - please report this as a bug");
    }
    sf.frame = ON_Plane(C, x, y);
    sf.frame.zaxis = z;
    sf.frame.UpdateEquation();
    sf.radius = radius;
    sf.angle = eq.sweep;
    if (zdot > 0.0) {
      sf.lat0 = 0.0;
      sf.lat1 = 0.5 * ON_PI;
    } else {
      sf.lat0 = -0.5 * ON_PI;
      sf.lat1 = 0.0;
    }
    spheres.push_back(sf);
  }

  for (const MultiEdge& ed : me) {
    if (!(ed.t_end - ed.t_start > tol)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdges: two spherical corners on one edge overlap (the edge is shorter than the "
          "two set-backs) - radius too large for this solid");
    }
  }

  // --- re-trim every face by every filleted edge it carries ---
  for (const MultiEdge& ed : me) {
    const double offset = radius / ed.cosb;
    const Point3d contact_i0 = ed.p0 - ed.bis * offset + ed.n_i * radius;
    const Point3d contact_j1 = ed.p1 - ed.bis * offset + ed.n_j * radius;
    Brep::PlanarFace& fi = work[static_cast<size_t>(ed.idx_i)];
    Brep::PlanarFace& fj = work[static_cast<size_t>(ed.idx_j)];
    fi.loop = detail::ClipByHalfspace3d(fi.loop, ON_Plane(contact_i0, -ed.m_i), tol);
    fj.loop = detail::ClipByHalfspace3d(fj.loop, ON_Plane(contact_j1, -ed.m_j), tol);
    if (fi.loop.size() < 3 || fj.loop.size() < 3) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConvexEdges: re-trimming an adjacent face left fewer than 3 vertices - radius too "
          "large for this solid's geometry");
    }
  }

  // --- cylinders, and the m == 1 corner notches ---
  std::vector<Brep::CylindricalFace> cyls;
  cyls.reserve(me.size());
  for (const MultiEdge& ed : me) {
    Brep::CylindricalFace cf;
    const Point3d axis_p0 = ed.p0 - ed.bis * (radius / ed.cosb);
    cf.frame.origin = axis_p0 + ed.t_start * ed.e;
    cf.frame.xaxis = ed.n_i;
    cf.frame.yaxis = ed.frame_y;
    cf.frame.zaxis = ed.e;
    cf.frame.UpdateEquation();
    cf.radius = radius;
    cf.angle = ed.sweep;
    cf.length = ed.t_end - ed.t_start;
    cyls.push_back(cf);
  }
  for (size_t k = 0; k < me.size(); ++k) {
    const MultiEdge& ed = me[k];
    const ON_Plane& plane_i = faces[static_cast<size_t>(ed.idx_i)].plane;
    const ON_Plane& plane_j = faces[static_cast<size_t>(ed.idx_j)].plane;
    const Point3d axis_p0 = ed.p0 - ed.bis * (radius / ed.cosb);
    // NotchCornerAtVertex wants "the other faces" - here every face
    // except this edge's own i/j; it only touches faces with a loop
    // vertex AT the endpoint, so passing all others is safe.
    std::vector<Brep::PlanarFace> others;
    std::vector<size_t> others_idx;
    for (size_t f = 0; f < work.size(); ++f) {
      if (static_cast<int>(f) == ed.idx_i || static_cast<int>(f) == ed.idx_j) continue;
      others.push_back(work[f]);
      others_idx.push_back(f);
    }
    if (!ed.sphere_at_p0) {
      NotchCornerAtVertex(others, ed.p0, ed.e, plane_i, plane_j, axis_p0, ed.n_i, ed.frame_y, radius, ed.sweep, tol);
    }
    if (!ed.sphere_at_p1) {
      NotchCornerAtVertex(others, ed.p1, ed.e, plane_i, plane_j, axis_p0 + ed.L * ed.e, ed.n_i, ed.frame_y, radius,
                          ed.sweep, tol);
    }
    for (size_t o = 0; o < others.size(); ++o) work[others_idx[o]] = std::move(others[o]);
  }

  return Brep::FromMixedFaces(work, cyls, {}, spheres);
}

// MULTI-EDGE concave-edge fillet - FilletConcaveEdge's own counterpart to
// FilletConvexEdges, letting several INDEPENDENT concave edges of the
// same solid be filleted in one call (unlike chaining single
// FilletConcaveEdge calls, which is not possible at all here: the first
// call's own output already carries a curved CylindricalFace, and
// PlanarFaces() - which every one of these functions calls first -
// rejects any solid already carrying one, exactly as it does for
// FilletConvexEdge; confirmed directly, not assumed, while developing
// this function).
//
// Reuses `MultiEdge` (defined just above, for FilletConvexEdges) purely
// as a per-edge data record - every field it holds is generic geometry
// (a face index, a normal, a fixed offset vector), nothing convex-
// specific baked into the TYPE itself, only into how FilletConvexEdges
// happens to populate it. This function populates the SAME fields with
// FilletConcaveEdge's own mirrored formulas instead (see that function's
// own doc comment for the sign derivations: axis_point(p) = p +
// bis*offset, contact_i/j(p) = axis_point(p) -/- n_i/j*radius, and the
// "which face is the angle-0 xaxis reference" hand-sign fix that a
// concave edge - unlike a convex one - genuinely needs).
// `sphere_at_p0`/`sphere_at_p1`/`t_start`/`t_end` are left at their
// defaults (false / 0 / L) since this function's own SCOPE (below) never
// runs the trihedral-corner branch that would set them.
//
// SCOPE, stated plainly rather than silently narrowed: every edge must
// be a genuine shared concave boundary edge (same EdgeConvexity check
// FilletConcaveEdge itself uses, applied per edge); one radius for all
// edges; no edge listed twice; and - the one deliberate limit this first
// multi-edge increment carries, matching where FilletConvexEdges ITSELF
// started before its own trihedral spherical-corner support was added -
// every filleted edge's own two endpoints must have EXACTLY ONE filleted
// edge incident (m == 1): two or more concave edges meeting at a shared
// vertex is a genuine vertex-blend problem (and, for concave corners,
// one this codebase has not attempted at all yet - not even the m == 3
// trihedral case FilletConvexEdges already closes for the convex side)
// and throws std::invalid_argument rather than guessing at a shape.
// Oblique third faces are likewise out of scope here (unlike the single-
// edge FilletConcaveEdge, which already closes that case) - only a free
// boundary or a third face exactly PERPENDICULAR to the edge is closed,
// via the same NotchCornerAtVertex splice FilletConvexEdges' own m == 1
// case uses; both gaps are genuine, disclosed future increments for THIS
// function specifically, not something either sibling (FilletConcaveEdge
// or FilletConvexEdges) already covers for it.
//
// CLOSED FORM this was checked against (dino8-kernel's own regression
// tests): two INDEPENDENT 90-degree concave notches (no shared vertex) on
// the same prism, each filleted with the same radius r, together ADD
// exactly 2 * r^2 * (1 - pi/4) of volume - the same per-notch closed form
// FilletConcaveEdge's own single-edge tests check, simply summed, since
// the two notches share no geometry to interact through.
Brep FilletConcaveEdges(const Brep& solid, const std::vector<std::pair<Point3d, Point3d>>& edges, double radius) {
  if (!(radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: radius must be positive");
  }
  if (edges.empty()) {
    throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: at least one edge is required");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  std::vector<MultiEdge> me;
  me.reserve(edges.size());
  for (const std::pair<Point3d, Point3d>& ed : edges) {
    MultiEdge m;
    m.p0 = ed.first;
    m.p1 = ed.second;
    for (const MultiEdge& other : me) {
      if ((PointsEqual(other.p0, m.p0, tol) && PointsEqual(other.p1, m.p1, tol)) ||
          (PointsEqual(other.p0, m.p1, tol) && PointsEqual(other.p1, m.p0, tol))) {
        throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: an edge is listed twice");
      }
    }

    int idx_i = -1, idx_j = -1;
    size_t k_i = 0;
    for (size_t f = 0; f < faces.size() && (idx_i < 0 || idx_j < 0); ++f) {
      const std::vector<Point3d>& loop = faces[f].loop;
      const size_t n = loop.size();
      for (size_t k = 0; k < n; ++k) {
        const Point3d& a = loop[k];
        const Point3d& b = loop[(k + 1) % n];
        if (idx_i < 0 && PointsEqual(a, m.p0, tol) && PointsEqual(b, m.p1, tol)) {
          idx_i = static_cast<int>(f);
          k_i = k;
        }
        if (idx_j < 0 && PointsEqual(a, m.p1, tol) && PointsEqual(b, m.p0, tol)) idx_j = static_cast<int>(f);
      }
    }
    if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: an edge is not a shared boundary edge of two distinct faces of "
          "`solid`, walked in opposite directions on their own loops");
    }
    {
      const std::vector<Point3d>& loop_i = faces[static_cast<size_t>(idx_i)].loop;
      const ON_Plane& plane_j_pre = faces[static_cast<size_t>(idx_j)].plane;
      const size_t k_i1 = (k_i + 1) % loop_i.size();
      bool degenerate = false;
      const bool convex = EdgeConvexity(loop_i, k_i, k_i1, plane_j_pre, tol, &degenerate);
      if (!degenerate && convex) {
        throw std::invalid_argument(
            "dino8::kernel::FilletConcaveEdges: an edge is a CONVEX dihedral edge, not concave - see "
            "FilletConvexEdges instead");
      }
    }

    m.e = m.p1 - m.p0;
    m.L = m.p0.DistanceTo(m.p1);
    if (!m.e.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: an edge's two endpoints coincide");
    }

    // Same hand-sign fix FilletConcaveEdge's own doc comment derives:
    // whichever face gives a POSITIVE (n_i x n_j) . e is the correct
    // angle-0 xaxis reference for a right-handed frame that also reaches
    // the other face's own tangent point at angle = sweep_angle.
    const Vector3d n_i_pre = faces[static_cast<size_t>(idx_i)].plane.zaxis;
    const Vector3d n_j_pre = faces[static_cast<size_t>(idx_j)].plane.zaxis;
    if ((ON_CrossProduct(n_i_pre, n_j_pre) * m.e) < 0.0) std::swap(idx_i, idx_j);
    m.idx_i = idx_i;
    m.idx_j = idx_j;
    m.n_i = faces[static_cast<size_t>(idx_i)].plane.zaxis;
    m.n_j = faces[static_cast<size_t>(idx_j)].plane.zaxis;

    const double dot_ij = std::max(-1.0, std::min(1.0, m.n_i * m.n_j));
    m.sweep = std::acos(dot_ij);
    m.theta = ON_PI - m.sweep;
    if (!(m.theta > 0.0) || !(m.theta < ON_PI)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: degenerate edge (interior angle is <= 0 or >= pi)");
    }
    m.bis = m.n_i + m.n_j;
    if (!m.bis.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: degenerate (near-180-degree) dihedral");
    }
    m.cosb = m.bis * m.n_i;
    if (m.cosb < 1e-9) {
      throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: degenerate bisector geometry (cosb too small)");
    }
    const double offset = radius / m.cosb;
    // Concave contact points: axis_point(p) = p + bis*offset,
    // contact_i/j(p) = axis_point(p) - n_i/j*radius (see
    // FilletConcaveEdge's own doc comment - the negation of
    // FilletConvexEdges' own contact formula just above).
    const Point3d contact_i0 = m.p0 + m.bis * offset - m.n_i * radius;
    const Point3d contact_j1 = m.p1 + m.bis * offset - m.n_j * radius;
    m.m_i = ON_CrossProduct(m.n_i, m.e);
    m.m_j = ON_CrossProduct(m.n_j, -m.e);
    if (!m.m_i.Unitize() || !m.m_j.Unitize()) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: degenerate face/edge geometry (a face normal is parallel to the edge)");
    }
    if (m.m_i * (m.p0 - contact_i0) >= 0.0) m.m_i = -m.m_i;
    if (m.m_j * (m.p1 - contact_j1) >= 0.0) m.m_j = -m.m_j;
    const double trim_back = radius / std::tan(m.theta / 2.0);
    auto max_extent_from_edge = [](const std::vector<Point3d>& loop, const Vector3d& mm, const Point3d& edge_ref) {
      double best = 0.0;
      for (const Point3d& v : loop) best = std::max(best, mm * (v - edge_ref));
      return best;
    };
    if (trim_back > max_extent_from_edge(faces[static_cast<size_t>(idx_i)].loop, m.m_i, m.p0) ||
        trim_back > max_extent_from_edge(faces[static_cast<size_t>(idx_j)].loop, m.m_j, m.p1)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: radius is too large to fit - a fillet's own trim-back distance "
          "exceeds one of the adjacent faces' extent from the edge");
    }
    // frame.xaxis = -n_i (concave sign - see FilletConcaveEdge's own doc
    // comment); m.n_i is kept as the plain, ORIGINAL face normal
    // throughout (not pre-negated) - every use site below negates it
    // explicitly instead, to keep this one sign flip visible at each
    // point it matters rather than baked silently into the stored value.
    m.frame_y = ON_CrossProduct(m.e, -m.n_i);
    if (!m.frame_y.Unitize()) {
      throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: degenerate fillet frame");
    }
    m.t_start = 0.0;
    m.t_end = m.L;
    me.push_back(m);
  }

  // --- distinct vertices touched by filleted edges: m == 1 (a plain
  // corner notch, below) or m == 3 at a trihedral (valence-3) corner - a
  // CONCAVE spherical vertex blend, the exact mirror of FilletConvexEdges'
  // own m == 3 case (see this function's own doc comment for what
  // changes and what does not) - are the only supported configurations.
  struct VertexUse {
    Point3d p;
    std::vector<std::pair<int, int>> uses;  // (edge index, end 0 or 1)
  };
  std::vector<VertexUse> verts;
  for (size_t k = 0; k < me.size(); ++k) {
    for (int end = 0; end < 2; ++end) {
      const Point3d& p = end == 0 ? me[k].p0 : me[k].p1;
      bool found = false;
      for (VertexUse& v : verts) {
        if (PointsEqual(v.p, p, tol)) {
          v.uses.emplace_back(static_cast<int>(k), end);
          found = true;
          break;
        }
      }
      if (!found) verts.push_back({p, {{static_cast<int>(k), end}}});
    }
  }

  std::vector<Brep::PlanarFace> work = faces;
  std::vector<Brep::SphericalFace> spheres;

  auto faces_touching = [&](const Point3d& p) {
    std::vector<int> out;
    for (size_t f = 0; f < faces.size(); ++f) {
      for (const Point3d& q : faces[f].loop) {
        if (PointsEqual(q, p, tol)) {
          out.push_back(static_cast<int>(f));
          break;
        }
      }
    }
    return out;
  };

  for (VertexUse& v : verts) {
    const int m_count = static_cast<int>(v.uses.size());
    if (m_count == 1) continue;  // handled after the re-trim, with FilletConcaveEdge's own notch
    const std::vector<int> touching = faces_touching(v.p);
    if (m_count != 3 || touching.size() != 3) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: a vertex has " + std::to_string(m_count) + " filleted edge(s) and " +
          std::to_string(touching.size()) +
          " incident face(s) - only a lone filleted edge (m == 1) or all three edges of a trihedral corner (m == 3, "
          "valence 3) are supported vertex configurations, see fillet.h's own doc comment");
    }
    for (const std::pair<int, int>& u : v.uses) {
      const MultiEdge& ed = me[static_cast<size_t>(u.first)];
      if (std::find(touching.begin(), touching.end(), ed.idx_i) == touching.end() ||
          std::find(touching.begin(), touching.end(), ed.idx_j) == touching.end()) {
        throw std::invalid_argument(
            "dino8::kernel::FilletConcaveEdges: a filleted edge at a trihedral corner is bounded by a face that "
            "does not touch that corner - inconsistent topology");
      }
    }
    const Vector3d na = faces[static_cast<size_t>(touching[0])].plane.zaxis;
    const Vector3d nb = faces[static_cast<size_t>(touching[1])].plane.zaxis;
    const Vector3d nc = faces[static_cast<size_t>(touching[2])].plane.zaxis;
    // Ball center C: n_f . (C - V) = +radius for all three planes - the
    // CONCAVE mirror of FilletConvexEdges' own n_f . (C - V) = -radius
    // (the ball sits OUTSIDE the material, in the empty corner the three
    // concave edges notch out of it, exactly as FilletConcaveEdge's own
    // single-edge axis_point does for two planes).
    const double det = na * ON_CrossProduct(nb, nc);
    if (std::fabs(det) < 1e-9) {
      throw std::invalid_argument("dino8::kernel::FilletConcaveEdges: degenerate trihedral corner (coplanar normals)");
    }
    const Vector3d rhs(radius, radius, radius);
    const Vector3d X(
        (rhs.x * (nb.y * nc.z - nb.z * nc.y) - na.y * (rhs.y * nc.z - nb.z * rhs.z) + na.z * (rhs.y * nc.y - nb.y * rhs.z)) / det,
        (na.x * (rhs.y * nc.z - nb.z * rhs.z) - rhs.x * (nb.x * nc.z - nb.z * nc.x) + na.z * (nb.x * rhs.z - rhs.y * nc.x)) / det,
        (na.x * (nb.y * rhs.z - rhs.y * nc.y) - na.y * (nb.x * rhs.z - rhs.y * nc.x) + rhs.x * (nb.x * nc.y - nb.y * nc.x)) / det);
    const Point3d C = v.p + X;
    for (const Vector3d& n : {na, nb, nc}) {
      if (std::fabs(n * (C - v.p) - radius) > 1e3 * tol) {
        throw std::runtime_error("dino8::kernel::FilletConcaveEdges: ball center solve failed - please report this as a bug");
      }
    }
    // Pole face: perpendicular to the other two (identical topological
    // test to FilletConvexEdges' own - convexity plays no role in it).
    int pole = -1;
    const Vector3d ns[3] = {na, nb, nc};
    for (int c = 0; c < 3 && pole < 0; ++c) {
      const Vector3d& p = ns[c];
      const Vector3d& q = ns[(c + 1) % 3];
      const Vector3d& r = ns[(c + 2) % 3];
      if (std::fabs(p * q) <= 1e-9 && std::fabs(p * r) <= 1e-9) pole = c;
    }
    if (pole < 0) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: a trihedral corner has no face perpendicular to the other two, so its "
          "spherical blend is a general spherical triangle - out of scope, see fillet.h's own doc comment");
    }
    const int pole_face = touching[pole];
    int eq_edge = -1;
    for (const std::pair<int, int>& u : v.uses) {
      const MultiEdge& ed = me[static_cast<size_t>(u.first)];
      if (ed.idx_i != pole_face && ed.idx_j != pole_face) eq_edge = u.first;
    }
    if (eq_edge < 0) {
      throw std::runtime_error(
          "dino8::kernel::FilletConcaveEdges: could not identify the equator edge at a corner - please report this "
          "as a bug");
    }
    const MultiEdge& eq = me[static_cast<size_t>(eq_edge)];
    // Set-backs and axis checks for all three edges - `t = (C - v.p) . e`
    // is a directionless projection, unaffected by the convex/concave
    // sign convention; `expected_perp` uses the CONCAVE axis_point's own
    // +bis*offset (FilletConcaveEdge's own doc comment), the negation of
    // FilletConvexEdges' own -ed.bis*(radius/ed.cosb).
    for (const std::pair<int, int>& u : v.uses) {
      MultiEdge& ed = me[static_cast<size_t>(u.first)];
      const Vector3d d = C - v.p;
      const double t_along_e = d * ed.e;
      const double t = (u.second == 0) ? t_along_e : -t_along_e;
      const Vector3d perp = d - t_along_e * ed.e;
      const Vector3d expected_perp = ed.bis * (radius / ed.cosb);
      if (t <= tol || (perp - expected_perp).Length() > 1e3 * tol) {
        throw std::runtime_error(
            "dino8::kernel::FilletConcaveEdges: the corner ball center does not lie on an incident fillet's axis - "
            "please report this as a bug");
      }
      if (u.second == 0) {
        ed.t_start = t;
        ed.sphere_at_p0 = true;
      } else {
        ed.t_end = ed.L - t;
        ed.sphere_at_p1 = true;
      }
    }
    // Sphere frame: equator arc parameterized exactly like the equator
    // cylinder's own cap (xaxis = -eq.n_i, the concave sign; yaxis =
    // eq.frame_y), pole along +-n_pole, `outward = false` (the concave
    // mirror of FilletConvexEdges' own default-true) - the patch bounds
    // material from the concave side, same role CylindricalFace::outward
    // already plays for this pairing's single-edge case.
    Brep::SphericalFace sf;
    const Vector3d x = -eq.n_i;
    const Vector3d y = eq.frame_y;
    Vector3d z = ON_CrossProduct(x, y);
    z.Unitize();
    const Vector3d n_pole = faces[static_cast<size_t>(pole_face)].plane.zaxis;
    const double zdot = z * n_pole;
    if (std::fabs(std::fabs(zdot) - 1.0) > 1e-9) {
      throw std::runtime_error(
          "dino8::kernel::FilletConcaveEdges: sphere frame is not aligned with the pole face normal - please "
          "report this as a bug");
    }
    sf.frame = ON_Plane(C, x, y);
    sf.frame.zaxis = z;
    sf.frame.UpdateEquation();
    sf.radius = radius;
    sf.angle = eq.sweep;
    // The pole itself is at lat = +-pi/2, i.e. C + radius*(+-z) via the
    // struct's own fixed "+radius*(...)" position formula (SphericalFace's
    // own doc comment) - this must land on the CONCAVE tangent point on
    // the pole face, C - radius*n_pole (the mirror of FilletConvexEdges'
    // own C + radius*n_pole, which is correct there because ITS OWN ball
    // center sits INSIDE the material at distance -radius from each
    // plane, so the tangent point is reached moving further in +n_f; this
    // function's own ball center sits OUTSIDE at distance +radius, so the
    // tangent point is reached moving BACK in -n_f instead - the exact
    // same sign flip FilletConcaveEdge's own contact_i/contact_j already
    // make for the two-face case). Since `z` itself is computed the SAME
    // way as FilletConvexEdges' own (a pure cross product, unaware of
    // convex/concave), matching this now-opposite target means using the
    // OPPOSITE branch of FilletConvexEdges' own zdot sign check - swapped
    // here, not re-derived from scratch, and confirmed by direct
    // substitution against a concave fixture's own hand-computed tangent
    // point, not assumed from the algebra alone (an earlier draft used
    // FilletConvexEdges' own branches unchanged and left two of the three
    // meridian cylinder caps landing on the WRONG pole of their own
    // great circle - a real, caught-directly bug, not a hypothetical one).
    if (zdot > 0.0) {
      sf.lat0 = -0.5 * ON_PI;
      sf.lat1 = 0.0;
    } else {
      sf.lat0 = 0.0;
      sf.lat1 = 0.5 * ON_PI;
    }
    sf.outward = false;
    spheres.push_back(sf);
  }

  for (const MultiEdge& ed : me) {
    if (!(ed.t_end - ed.t_start > tol)) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: two spherical corners on one edge overlap (the edge is shorter than "
          "the two set-backs) - radius too large for this solid");
    }
  }

  // --- re-trim every face by every filleted edge it carries ---
  for (const MultiEdge& ed : me) {
    const double offset = radius / ed.cosb;
    const Point3d contact_i0 = ed.p0 + ed.bis * offset - ed.n_i * radius;
    const Point3d contact_j1 = ed.p1 + ed.bis * offset - ed.n_j * radius;
    Brep::PlanarFace& fi = work[static_cast<size_t>(ed.idx_i)];
    Brep::PlanarFace& fj = work[static_cast<size_t>(ed.idx_j)];
    fi.loop = detail::ClipByHalfspace3d(fi.loop, ON_Plane(contact_i0, -ed.m_i), tol);
    fj.loop = detail::ClipByHalfspace3d(fj.loop, ON_Plane(contact_j1, -ed.m_j), tol);
    if (fi.loop.size() < 3 || fj.loop.size() < 3) {
      throw std::invalid_argument(
          "dino8::kernel::FilletConcaveEdges: re-trimming an adjacent face left fewer than 3 vertices - radius too "
          "large for this solid's geometry");
    }
  }

  // --- cylinders, and the m == 1 corner notches ---
  std::vector<Brep::CylindricalFace> cyls;
  cyls.reserve(me.size());
  for (const MultiEdge& ed : me) {
    Brep::CylindricalFace cf;
    const Point3d axis_p0 = ed.p0 + ed.bis * (radius / ed.cosb);
    cf.frame.origin = axis_p0 + ed.t_start * ed.e;
    cf.frame.xaxis = -ed.n_i;  // concave sign
    cf.frame.yaxis = ed.frame_y;
    cf.frame.zaxis = ed.e;
    cf.frame.UpdateEquation();
    cf.radius = radius;
    cf.angle = ed.sweep;
    cf.length = ed.t_end - ed.t_start;
    cf.outward = false;
    cyls.push_back(cf);
  }
  for (const MultiEdge& ed : me) {
    const ON_Plane& plane_i = faces[static_cast<size_t>(ed.idx_i)].plane;
    const ON_Plane& plane_j = faces[static_cast<size_t>(ed.idx_j)].plane;
    const Point3d axis_p0 = ed.p0 + ed.bis * (radius / ed.cosb);
    std::vector<Brep::PlanarFace> others;
    std::vector<size_t> others_idx;
    for (size_t f = 0; f < work.size(); ++f) {
      if (static_cast<int>(f) == ed.idx_i || static_cast<int>(f) == ed.idx_j) continue;
      others.push_back(work[f]);
      others_idx.push_back(f);
    }
    if (!ed.sphere_at_p0) {
      NotchCornerAtVertex(others, ed.p0, ed.e, plane_i, plane_j, axis_p0, -ed.n_i, ed.frame_y, radius, ed.sweep, tol);
    }
    if (!ed.sphere_at_p1) {
      NotchCornerAtVertex(others, ed.p1, ed.e, plane_i, plane_j, axis_p0 + ed.L * ed.e, -ed.n_i, ed.frame_y, radius,
                          ed.sweep, tol);
    }
    for (size_t o = 0; o < others.size(); ++o) work[others_idx[o]] = std::move(others[o]);
  }

  return Brep::FromMixedFaces(work, cyls, {}, spheres);
}


// ---------------------------------------------------------------------------
// ChamferConvexVertex / ChamferConcaveVertex: single-facet trihedral vertex
// chamfer (see fillet.h's own doc comment for both).

namespace {

// Shared topology-finding for ChamferConvexVertex/ChamferConcaveVertex: the
// same "faces touching a vertex, grouped into the 3 edges of a trihedral
// corner" combinatorics FilletConvexEdges'/FilletConcaveEdges' own m == 3
// case already uses (their own VertexUse/faces_touching), generalized here
// to also report which 2 faces border each edge - needed both for the
// EdgeConvexity check below and by ChamferVertexCore's own construction.
struct VertexEdge {
  Point3d neighbor;
  int face_a, face_b;
};

struct TrihedralCorner {
  std::vector<int> touching;      // exactly 3 face indices
  std::vector<VertexEdge> edges;  // exactly 3
};

TrihedralCorner FindTrihedralCorner(const std::vector<Brep::PlanarFace>& faces, const Point3d& vertex, double tol,
                                    const char* who) {
  struct Touch {
    int face_idx;
    Point3d pred, succ;
  };
  std::vector<Touch> touch;
  for (size_t f = 0; f < faces.size(); ++f) {
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      if (PointsEqual(loop[k], vertex, tol)) {
        touch.push_back({static_cast<int>(f), loop[(k + n - 1) % n], loop[(k + 1) % n]});
        break;
      }
    }
  }
  if (touch.size() != 3) {
    throw std::invalid_argument(std::string("dino8::kernel::") + who + ": `vertex` is touched by " +
                                std::to_string(touch.size()) +
                                " face(s), not 3 - only a trihedral (valence-3) corner is supported");
  }
  TrihedralCorner tc;
  for (const Touch& t : touch) tc.touching.push_back(t.face_idx);
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      const Point3d cand_i[2] = {touch[static_cast<size_t>(i)].pred, touch[static_cast<size_t>(i)].succ};
      const Point3d cand_j[2] = {touch[static_cast<size_t>(j)].pred, touch[static_cast<size_t>(j)].succ};
      for (const Point3d& ci : cand_i) {
        for (const Point3d& cj : cand_j) {
          if (PointsEqual(ci, cj, tol)) {
            tc.edges.push_back({ci, touch[static_cast<size_t>(i)].face_idx, touch[static_cast<size_t>(j)].face_idx});
          }
        }
      }
    }
  }
  if (tc.edges.size() != 3) {
    throw std::invalid_argument(
        std::string("dino8::kernel::") + who +
        ": the 3 faces touching `vertex` do not form a genuine trihedral corner (each pair of faces should share "
        "exactly one edge at the vertex) - inconsistent topology");
  }
  return tc;
}

// Convexity of the edge (vertex, neighbor), which lies on face_a's own
// loop - wraps EdgeConvexity (see its own doc comment) with the loop-index
// lookup a VertexEdge doesn't itself carry.
bool VertexEdgeConvexity(const std::vector<Brep::PlanarFace>& faces, int face_a, int face_b, const Point3d& vertex,
                         const Point3d& neighbor, double tol, bool* degenerate_out) {
  const std::vector<Point3d>& loop_a = faces[static_cast<size_t>(face_a)].loop;
  const size_t n = loop_a.size();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    if ((PointsEqual(loop_a[k], vertex, tol) && PointsEqual(loop_a[k1], neighbor, tol)) ||
        (PointsEqual(loop_a[k], neighbor, tol) && PointsEqual(loop_a[k1], vertex, tol))) {
      return EdgeConvexity(loop_a, k, k1, faces[static_cast<size_t>(face_b)].plane, tol, degenerate_out);
    }
  }
  throw std::runtime_error(
      "dino8::kernel::VertexEdgeConvexity: edge not found on face_a's own loop - please report this as a bug");
}

// Shared construction for ChamferConvexVertex/ChamferConcaveVertex. Takes
// no stance on convex vs. concave itself - see ChamferConcaveVertex's own
// doc comment for why that is safe: every sign here (which of the new
// chamfer plane's two normal directions cuts `vertex`'s own corner sliver
// away, which winding order makes the new triangular face CCW as seen
// from outside) is derived directly from the fixture's own geometry.
Brep ChamferVertexCore(const std::vector<Brep::PlanarFace>& faces, double tol, const TrihedralCorner& tc,
                        const Point3d& vertex, const std::array<double, 3>& distances, const char* who) {
  std::vector<Point3d> corner(3);
  for (int k = 0; k < 3; ++k) {
    Vector3d e = tc.edges[static_cast<size_t>(k)].neighbor - vertex;
    const double L = e.Length();
    const double distance = distances[static_cast<size_t>(k)];
    if (!(distance < L - tol)) {
      throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                  ": a chamfer distance is too large to fit - it reaches or exceeds that edge's own "
                                  "length");
    }
    e.Unitize();
    corner[static_cast<size_t>(k)] = vertex + distance * e;
  }

  // Orientation: the new face's own outward normal, checked via a Newell
  // normal against the sum of the 3 touching faces' own outward normals -
  // exactly ChamferConvexEdge's own convention (see its doc comment).
  Vector3d bis(0, 0, 0);
  for (int f : tc.touching) bis = bis + faces[static_cast<size_t>(f)].plane.zaxis;
  Vector3d newell(0, 0, 0);
  for (size_t k = 0; k < corner.size(); ++k) {
    const Point3d& a = corner[k];
    const Point3d& b = corner[(k + 1) % corner.size()];
    newell.x += (a.y - b.y) * (a.z + b.z);
    newell.y += (a.z - b.z) * (a.x + b.x);
    newell.z += (a.x - b.x) * (a.y + b.y);
  }
  if (newell * bis < 0.0) std::reverse(corner.begin(), corner.end());
  Vector3d n_c = ON_CrossProduct(corner[1] - corner[0], corner[2] - corner[0]);
  if (!n_c.Unitize()) {
    throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                ": degenerate trihedral corner (the 3 chamfer points are collinear)");
  }
  if (n_c * bis <= 0.0) {
    throw std::runtime_error(std::string("dino8::kernel::") + who +
                             ": degenerate chamfer triangle orientation - please report this as a bug");
  }

  // Which of +-n_c cuts `vertex`'s own corner sliver away is checked
  // directly (not assumed from convexity - see ChamferConcaveVertex's own
  // doc comment for why the two cases actually need OPPOSITE signs here
  // even though n_c's own orientation above is convex/concave-agnostic).
  Vector3d cut_normal = n_c;
  if (!(ON_Plane(corner[0], cut_normal).DistanceTo(vertex) > tol)) cut_normal = -n_c;
  const ON_Plane cut_plane(corner[0], cut_normal);
  if (!(cut_plane.DistanceTo(vertex) > tol)) {
    throw std::runtime_error(std::string("dino8::kernel::") + who +
                             ": `vertex` lies on its own chamfer plane - please report this as a bug");
  }

  std::vector<Brep::PlanarFace> work = faces;
  for (int f : tc.touching) {
    Brep::PlanarFace& pf = work[static_cast<size_t>(f)];
    pf.loop = detail::ClipByHalfspace3d(pf.loop, cut_plane, tol);
    if (pf.loop.size() < 3) {
      throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                  ": re-trimming an adjacent face left fewer than 3 vertices - distance too large "
                                  "for this solid's geometry");
    }
  }

  Brep::PlanarFace chamfer;
  chamfer.plane = ON_Plane(corner[0], n_c);
  chamfer.loop = corner;
  work.push_back(std::move(chamfer));
  return Brep::FromPlanarFaces(work);
}

// Shared by all 4 ChamferConvexVertex/ChamferConcaveVertex overloads:
// finds the trihedral corner at `vertex` and validates every one of its 3
// edges has the required convexity sense (`require_convex` true for the
// convex overloads, false for the concave ones) - the two throw messages
// ChamferConvexVertex/ChamferConcaveVertex used to each have inline,
// factored here since all 4 overloads need exactly the same check.
TrihedralCorner FindAndValidateTrihedralCorner(const std::vector<Brep::PlanarFace>& faces, const Point3d& vertex,
                                               double tol, bool require_convex, const char* who) {
  const TrihedralCorner tc = FindTrihedralCorner(faces, vertex, tol, who);
  for (const VertexEdge& ev : tc.edges) {
    bool degenerate = false;
    const bool convex = VertexEdgeConvexity(faces, ev.face_a, ev.face_b, vertex, ev.neighbor, tol, &degenerate);
    if (degenerate) continue;
    if (require_convex && !convex) {
      throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                  ": an edge at `vertex` is not a convex dihedral edge - concave/degenerate corners "
                                  "are out of scope, see ChamferConcaveVertex for the concave mirror");
    }
    if (!require_convex && convex) {
      throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                  ": an edge at `vertex` is a CONVEX dihedral edge, not concave - see "
                                  "ChamferConvexVertex instead");
    }
  }
  return tc;
}

// Shared by the per-edge-distance overloads: matches each `edge_distances`
// entry's own point to the one edge of `tc` it identifies (by the same
// point-identifies-an-edge convention every other function in this file
// already uses), in any order, and returns the 3 distances re-indexed to
// `tc.edges`' own order. Throws if the size isn't exactly 3, or an entry's
// point matches no edge (or one already matched by an earlier entry).
std::array<double, 3> MatchEdgeDistances(const TrihedralCorner& tc,
                                         const std::vector<std::pair<Point3d, double>>& edge_distances, double tol,
                                         const char* who) {
  if (edge_distances.size() != 3) {
    throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                ": edge_distances must have exactly 3 entries, one per edge at `vertex`");
  }
  std::array<double, 3> out{};
  std::array<bool, 3> matched{false, false, false};
  for (const std::pair<Point3d, double>& ed : edge_distances) {
    bool found = false;
    for (int k = 0; k < 3 && !found; ++k) {
      if (!matched[static_cast<size_t>(k)] && PointsEqual(ed.first, tc.edges[static_cast<size_t>(k)].neighbor, tol)) {
        out[static_cast<size_t>(k)] = ed.second;
        matched[static_cast<size_t>(k)] = true;
        found = true;
      }
    }
    if (!found) {
      throw std::invalid_argument(std::string("dino8::kernel::") + who +
                                  ": an edge_distances entry's own point does not match any of `vertex`'s own 3 "
                                  "edge neighbors (or duplicates an already-matched one)");
    }
  }
  return out;
}

}  // namespace

Brep ChamferConvexVertex(const Brep& solid, Point3d vertex, double distance) {
  if (!(distance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::ChamferConvexVertex: distance must be positive");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  const TrihedralCorner tc = FindAndValidateTrihedralCorner(faces, vertex, tol, true, "ChamferConvexVertex");
  return ChamferVertexCore(faces, tol, tc, vertex, {distance, distance, distance}, "ChamferConvexVertex");
}

Brep ChamferConvexVertex(const Brep& solid, Point3d vertex,
                          const std::vector<std::pair<Point3d, double>>& edge_distances) {
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  const TrihedralCorner tc = FindAndValidateTrihedralCorner(faces, vertex, tol, true, "ChamferConvexVertex");
  const std::array<double, 3> distances = MatchEdgeDistances(tc, edge_distances, tol, "ChamferConvexVertex");
  for (double d : distances) {
    if (!(d > 0.0)) {
      throw std::invalid_argument("dino8::kernel::ChamferConvexVertex: every edge distance must be positive");
    }
  }
  return ChamferVertexCore(faces, tol, tc, vertex, distances, "ChamferConvexVertex");
}

Brep ChamferConcaveVertex(const Brep& solid, Point3d vertex, double distance) {
  if (!(distance > 0.0)) {
    throw std::invalid_argument("dino8::kernel::ChamferConcaveVertex: distance must be positive");
  }
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  const TrihedralCorner tc = FindAndValidateTrihedralCorner(faces, vertex, tol, false, "ChamferConcaveVertex");
  return ChamferVertexCore(faces, tol, tc, vertex, {distance, distance, distance}, "ChamferConcaveVertex");
}

Brep ChamferConcaveVertex(const Brep& solid, Point3d vertex,
                           const std::vector<std::pair<Point3d, double>>& edge_distances) {
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);
  const TrihedralCorner tc = FindAndValidateTrihedralCorner(faces, vertex, tol, false, "ChamferConcaveVertex");
  const std::array<double, 3> distances = MatchEdgeDistances(tc, edge_distances, tol, "ChamferConcaveVertex");
  for (double d : distances) {
    if (!(d > 0.0)) {
      throw std::invalid_argument("dino8::kernel::ChamferConcaveVertex: every edge distance must be positive");
    }
  }
  return ChamferVertexCore(faces, tol, tc, vertex, distances, "ChamferConcaveVertex");
}


namespace {

// Replaces the loop edge whose two consecutive points equal {P, Q} (in
// EITHER walk order, within tol) with {P2, Q2} in the matching order -
// the genuine inverse of the half-space clip FilletConvexEdge's own step
// 2 performs (that step replaced the sharp edge with the rail; this
// replaces the rail with the restored sharp edge). Throws
// std::runtime_error if no such edge exists (should not happen for a
// face this function has already confirmed shares this cylinder's own
// rail - checked rather than silently doing nothing).
void ReplaceLoopEdge(std::vector<Point3d>& loop, const Point3d& P, const Point3d& Q, const Point3d& P2,
                     const Point3d& Q2, double tol) {
  const size_t n = loop.size();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    if (PointsEqual(loop[k], P, tol) && PointsEqual(loop[k1], Q, tol)) {
      loop[k] = P2;
      loop[k1] = Q2;
      return;
    }
    if (PointsEqual(loop[k], Q, tol) && PointsEqual(loop[k1], P, tol)) {
      loop[k] = Q2;
      loop[k1] = P2;
      return;
    }
  }
  throw std::runtime_error(
      "dino8::kernel::RemoveBlend: a face expected to share the fillet's own rail edge does not - please report "
      "this as a bug");
}

// The genuine inverse of NotchCornerAtVertex's own splice (see its doc
// comment): finds a run of MORE than 2 consecutive loop points running
// from a point near `p1` to one near `p2` (in either direction the loop
// happens to walk it) and collapses the whole run to the single vertex
// `restored`. A face with no such run (an untouched sharp corner, or a
// free boundary - NotchCornerAtVertex's own "no matching face, no-op"
// cases) is left alone, exactly mirroring that function's own silent
// no-op contract; only the FIRST face where a run is found is touched,
// matching "a face shouldn't need it twice at the same vertex" elsewhere
// in this file.
void CollapseNotchRun(std::vector<Brep::PlanarFace>& other_faces, const Point3d& p1, const Point3d& p2,
                      const Point3d& restored, double tol) {
  for (Brep::PlanarFace& f : other_faces) {
    std::vector<Point3d>& loop = f.loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const bool at_p1 = PointsEqual(loop[k], p1, tol);
      const bool at_p2 = PointsEqual(loop[k], p2, tol);
      if (!at_p1 && !at_p2) continue;
      const Point3d& target = at_p1 ? p2 : p1;
      // len starts at 1 (not 2): a run can be a genuine dense notch
      // polyline (RemoveBlend's own fillet-corner-notch case, len ~200)
      // OR a plain 2-point edge with NOTHING between p1 and p2 at all
      // (RemoveChamfer's own end-face splice - see ChamferEndAtVertex's
      // own doc comment: a chamfer's end condition replaces a corner
      // with exactly two adjacent points, no dense polyline). Both are
      // "a run between two known points, collapse it to one vertex" in
      // exactly the same sense; nothing below assumes len > 1.
      for (size_t len = 1; len < n; ++len) {
        const size_t idx = (k + len) % n;
        if (!PointsEqual(loop[idx], target, tol)) continue;
        if (idx < k) {
          throw std::runtime_error(
              "dino8::kernel::RemoveBlend: a notch run wraps around its own face's loop start/end - out of scope, "
              "please report this as a bug");
        }
        // The genuine inverse of RegisterNotchRun's own insertion-time
        // index bookkeeping (see its own doc comment): removing [k, idx]
        // (idx - k + 1 points, collapsed to ONE) shifts every OTHER
        // recorded run's own begin index that sits at or after idx+1 down
        // by (idx - k) - and this run's own metadata entry (wherever it
        // is - the legacy pair or a notch_runs entry) is dropped entirely,
        // since after collapsing it is no longer a notch at all, just an
        // ordinary vertex. Every entry not on THIS face is untouched by
        // definition (this loop only ever mutates `f`'s own fields).
        const int collapse_begin = static_cast<int>(k);
        const int collapse_count = static_cast<int>(idx - k + 1);
        std::vector<std::pair<int, int>> remaining;
        auto consider = [&](int run_begin, int run_count) {
          if (run_count <= 1) return;
          if (run_begin == collapse_begin && run_count == collapse_count) return;  // this is the one being collapsed
          remaining.emplace_back(run_begin, run_count);
        };
        consider(f.notch_begin, f.notch_count);
        for (const std::pair<int, int>& r : f.notch_runs) consider(r.first, r.second);
        for (std::pair<int, int>& r : remaining) {
          if (r.first >= collapse_begin + collapse_count) r.first -= (collapse_count - 1);
        }
        f.notch_begin = 0;
        f.notch_count = 0;
        f.notch_runs.clear();
        if (!remaining.empty()) {
          f.notch_begin = remaining.front().first;
          f.notch_count = remaining.front().second;
          f.notch_runs.assign(remaining.begin() + 1, remaining.end());
        }

        // A plain in-place erase-and-insert (loop[0..k-1], restored,
        // loop[idx+1..n-1]) - NOT a rotation starting at idx+1 - so that
        // every OTHER recorded run's own shifted begin index (computed
        // just above, assuming positions before k are untouched and
        // `restored` lands exactly at k) actually matches this array.
        std::vector<Point3d> new_loop;
        new_loop.reserve(n - static_cast<size_t>(collapse_count) + 1);
        for (size_t m = 0; m < k; ++m) new_loop.push_back(loop[m]);
        new_loop.push_back(restored);
        for (size_t m = idx + 1; m < n; ++m) new_loop.push_back(loop[m]);
        loop = std::move(new_loop);
        return;
      }
      break;  // this vertex didn't lead to a matching run within a full lap - not this fillet's own notch
    }
  }
}

}  // namespace

namespace {

// Shared by both the cylindrical and conical branches of RemoveBlend:
// locates a face among `faces` whose own loop has an edge exactly
// matching {A, B} (either walk order) - the same rail-sharing fact
// FilletConvexEdge's/FilletConvexEdgeTapered's own doc comments rely on
// to weld a fillet patch to its two adjacent planar faces in the first
// place. Returns -1 if none matches.
// `exclude` (default -1, i.e. none) skips one face index entirely - needed
// by RemoveChamfer, where the chamfer quad being removed is ITSELF one of
// `faces` and its own loop trivially contains {A, B} as one of its own four
// consecutive-point edges whenever A/B are two of the quad's own corners
// (the "third face" rail search there is deliberately re-using the quad's
// own corner points). Without this, a scan order where the quad's own
// index precedes the genuine third face's index returns a spurious
// self-match instead of the real neighbor - confirmed directly: a second,
// independently-chamfered corner on the same solid shifted the quad to an
// earlier array index than its own top-face neighbor, and the unguarded
// search silently "found" the rail edge on the quad itself.
int FindFaceWithEdge(const std::vector<Brep::PlanarFace>& faces, const Point3d& A, const Point3d& B, double tol,
                      int exclude = -1) {
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) == exclude) continue;
    const std::vector<Point3d>& loop = faces[f].loop;
    const size_t n = loop.size();
    for (size_t k = 0; k < n; ++k) {
      const size_t k1 = (k + 1) % n;
      if ((PointsEqual(loop[k], A, tol) && PointsEqual(loop[k1], B, tol)) ||
          (PointsEqual(loop[k], B, tol) && PointsEqual(loop[k1], A, tol))) {
        return static_cast<int>(f);
      }
    }
  }
  return -1;
}

// The genuine inverse of FilletConvexEdge's own construction (see
// RemoveBlend's own doc comment for the full derivation) - removes
// `mf.cylindrical[best]` and restores the sharp edge it rounded off.
Brep RemoveCylindricalBlend(const Brep::MixedFacesResult& mf, int best, const std::vector<Brep::PlanarFace>& faces,
                            double tol) {
  const Brep::CylindricalFace& cf = mf.cylindrical[static_cast<size_t>(best)];
  if (!cf.cap0_notch_points.empty() || !cf.cap1_notch_points.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: this cylindrical face has a sloped (oblique-end) or Steinmetz-style cap notch "
        "- out of scope, see this function's own doc comment");
  }

  const Point3d R0 = cf.frame.origin + cf.radius * cf.frame.xaxis;
  const Point3d R1 = R0 + cf.length * cf.frame.zaxis;
  const Point3d S0 = cf.frame.origin + cf.radius * (std::cos(cf.angle) * cf.frame.xaxis + std::sin(cf.angle) * cf.frame.yaxis);
  const Point3d S1 = S0 + cf.length * cf.frame.zaxis;

  const int idx_i = FindFaceWithEdge(faces, R0, R1, tol);
  const int idx_j = FindFaceWithEdge(faces, S0, S1, tol);
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: could not find the two planar faces sharing this cylinder's own two straight "
        "rails - is this really a FilletConvexEdge-built face?");
  }

  const Vector3d n_i = faces[static_cast<size_t>(idx_i)].plane.zaxis;
  const Vector3d n_j = faces[static_cast<size_t>(idx_j)].plane.zaxis;
  Vector3d bis = n_i + n_j;
  if (!bis.Unitize()) {
    throw std::invalid_argument("dino8::kernel::RemoveBlend: degenerate (near-180-degree) adjacent-face dihedral");
  }
  const double cosb = bis * n_i;
  if (cosb < 1e-9) {
    throw std::invalid_argument("dino8::kernel::RemoveBlend: degenerate bisector geometry (cosb too small)");
  }
  const double offset = cf.radius / cosb;
  // `bis`/`cosb`/`offset` are symmetric in n_i/n_j (bis = normalize(n_i +
  // n_j) doesn't care which face is "i"), so the ONLY thing that depends
  // on whether this patch was built by FilletConvexEdge (cf.outward ==
  // true, frame.origin = edge_p0 - bis*offset - see that function's own
  // doc comment) or by FilletConcaveEdge (cf.outward == false,
  // frame.origin = edge_p0 + bis*offset - the ball center moves into the
  // EMPTY wedge instead of into the material) is the SIGN of this one
  // term - a genuine mirror, not a guess: FilletConcaveEdge's own
  // axis_point is p + bis*offset, so inverting it needs the opposite
  // sign from FilletConvexEdge's own p - bis*offset.
  const double sign = cf.outward ? 1.0 : -1.0;
  const Point3d edge_p0 = cf.frame.origin + sign * bis * offset;
  const Point3d edge_p1 = edge_p0 + cf.length * cf.frame.zaxis;

  // Reject a FilletConvexEdges-built spherical vertex blend's own corner
  // cylinder: such a cylinder is set back so its end rail corners are
  // EXACTLY the two rail corners a SphericalFace shares with it (see
  // FilletConvexEdges' own doc comment) - i.e. both live on that sphere's
  // own surface, at that sphere's own radius. A plain m==1 end (whether
  // built by FilletConvexEdge or by FilletConvexEdges - the two use
  // IDENTICAL math for that case) has no such sphere and is unaffected.
  auto end_is_spherical_corner = [&](const Point3d& rail_i_end, const Point3d& rail_j_end) {
    for (const Brep::SphericalFace& sf : mf.spherical) {
      if (std::fabs(sf.radius - cf.radius) > std::max(tol, 1e-9)) continue;
      const double da = rail_i_end.DistanceTo(sf.frame.origin) - sf.radius;
      const double db = rail_j_end.DistanceTo(sf.frame.origin) - sf.radius;
      if (std::fabs(da) <= std::max(tol * 10.0, 1e-6) && std::fabs(db) <= std::max(tol * 10.0, 1e-6)) return true;
    }
    return false;
  };
  if (end_is_spherical_corner(R0, S0) || end_is_spherical_corner(R1, S1)) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: this cylindrical face has a spherical vertex-blend corner at one of its own "
        "ends (from FilletConvexEdges) - out of scope, see this function's own doc comment");
  }

  std::vector<Brep::PlanarFace> mixed_planar = faces;
  ReplaceLoopEdge(mixed_planar[static_cast<size_t>(idx_i)].loop, R0, R1, edge_p0, edge_p1, tol);
  ReplaceLoopEdge(mixed_planar[static_cast<size_t>(idx_j)].loop, S0, S1, edge_p0, edge_p1, tol);

  std::vector<Brep::PlanarFace> others;
  std::vector<size_t> others_idx;
  for (size_t f = 0; f < mixed_planar.size(); ++f) {
    if (static_cast<int>(f) == idx_i || static_cast<int>(f) == idx_j) continue;
    others.push_back(mixed_planar[f]);
    others_idx.push_back(f);
  }
  CollapseNotchRun(others, R0, S0, edge_p0, tol);
  CollapseNotchRun(others, R1, S1, edge_p1, tol);
  for (size_t o = 0; o < others.size(); ++o) mixed_planar[others_idx[o]] = std::move(others[o]);

  std::vector<Brep::CylindricalFace> remaining_cyl;
  for (size_t c = 0; c < mf.cylindrical.size(); ++c) {
    if (static_cast<int>(c) != best) remaining_cyl.push_back(mf.cylindrical[c]);
  }
  return Brep::FromMixedFaces(mixed_planar, remaining_cyl, mf.conical, mf.spherical);
}

// The genuine inverse of FilletConvexEdgeTapered's own construction -
// removes `mf.conical[best]` and restores the sharp edge (or, for one
// segment of a multi-station taper, that segment's own straight span)
// it rounded off.
//
// The rolling-ball radii r_lo/r_hi at the segment's own two ends follow
// directly from the cone's own TRUE radii: radius0_true = r_lo*c,
// radius1_true = r_hi*c where c = 1/sqrt(1 + tan_half_angle^2) (the
// exact algebraic inverse of BuildTaperedConeSegment's own radius0_true
// = r_lo*c derivation, itself already known in closed form here since
// tan_half_angle = (radius1_true - radius0_true) / length is read
// straight off the ConicalFace's own fields, no unknowns).
//
// Rather than also inverting the apex/axis construction (m, Umag, c) to
// recover edge_p0/edge_p1 directly, this uses the SAME fact
// FilletConvexEdge's own inverse does: k_i = n_i - bis/cosb is a FIXED
// vector (no unknowns, computed from the two recovered face normals
// alone), and the cone's own rail corner at (v0, angle 0) is EXACTLY
// edge_p0 + r_lo*k_i (FilletConvexEdgeTapered's own rail_i(0) - see its
// doc comment) - so edge_p0 = (that rail corner) - r_lo*k_i, and
// likewise edge_p1 from the (v1, angle 0) corner and r_hi*k_i. This
// needs no separate recovery of the taper's own apex/axis geometry at
// all. The SAME two points, reconstructed independently from face j's
// own k_j instead of face i's own k_i, are then a genuine, discriminating
// checked invariant (not vacuous, since k_i != k_j) - not merely trusted.
Brep RemoveConicalBlend(const Brep::MixedFacesResult& mf, int best, const std::vector<Brep::PlanarFace>& faces,
                        double tol) {
  const Brep::ConicalFace& cf = mf.conical[static_cast<size_t>(best)];
  if (!(cf.length > 0.0) || std::fabs(cf.radius1 - cf.radius0) < 1e-300) {
    throw std::invalid_argument("dino8::kernel::RemoveBlend: degenerate conical face (please report this as a bug)");
  }
  const double tan_half_angle = (cf.radius1 - cf.radius0) / cf.length;
  const double c = 1.0 / std::sqrt(1.0 + tan_half_angle * tan_half_angle);
  const double r_lo = cf.radius0 / c;
  const double r_hi = cf.radius1 / c;
  const double v0 = cf.radius0 / tan_half_angle;
  const double v1 = cf.radius1 / tan_half_angle;

  const Point3d R0 = cf.frame.origin + v0 * cf.frame.zaxis + cf.radius0 * cf.frame.xaxis;
  const Point3d R1 = cf.frame.origin + v1 * cf.frame.zaxis + cf.radius1 * cf.frame.xaxis;
  const Point3d S0 =
      cf.frame.origin + v0 * cf.frame.zaxis + cf.radius0 * (std::cos(cf.angle) * cf.frame.xaxis + std::sin(cf.angle) * cf.frame.yaxis);
  const Point3d S1 =
      cf.frame.origin + v1 * cf.frame.zaxis + cf.radius1 * (std::cos(cf.angle) * cf.frame.xaxis + std::sin(cf.angle) * cf.frame.yaxis);

  const int idx_i = FindFaceWithEdge(faces, R0, R1, tol);
  const int idx_j = FindFaceWithEdge(faces, S0, S1, tol);
  if (idx_i < 0 || idx_j < 0 || idx_i == idx_j) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: could not find the two planar faces sharing this cone's own two straight "
        "rails - is this really a FilletConvexEdgeTapered-built face?");
  }

  const Vector3d n_i = faces[static_cast<size_t>(idx_i)].plane.zaxis;
  const Vector3d n_j = faces[static_cast<size_t>(idx_j)].plane.zaxis;
  Vector3d bis = n_i + n_j;
  if (!bis.Unitize()) {
    throw std::invalid_argument("dino8::kernel::RemoveBlend: degenerate (near-180-degree) adjacent-face dihedral");
  }
  const double cosb = bis * n_i;
  if (cosb < 1e-9) {
    throw std::invalid_argument("dino8::kernel::RemoveBlend: degenerate bisector geometry (cosb too small)");
  }
  const Vector3d k_i = n_i - bis * (1.0 / cosb);
  const Vector3d k_j = n_j - bis * (1.0 / cosb);

  const Point3d edge_p0 = R0 - r_lo * k_i;
  const Point3d edge_p1 = R1 - r_hi * k_i;
  // Checked invariant, genuinely discriminating (k_i != k_j): face j's
  // own rail corners must reconstruct the SAME two edge points.
  const Point3d edge_p0_check = S0 - r_lo * k_j;
  const Point3d edge_p1_check = S1 - r_hi * k_j;
  const double check_tol = std::max(tol * 100.0, 1e-6);
  if (edge_p0.DistanceTo(edge_p0_check) > check_tol || edge_p1.DistanceTo(edge_p1_check) > check_tol) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: this conical face's own two rails do not reconstruct the same sharp edge - it "
        "was not built by FilletConvexEdgeTapered's own construction (please report this as a bug if it was)");
  }
  if (!(r_lo > 0.0) || !(r_hi > 0.0)) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: reconstructed a non-positive rolling-ball radius - not a "
        "FilletConvexEdgeTapered-built face");
  }

  std::vector<Brep::PlanarFace> mixed_planar = faces;
  ReplaceLoopEdge(mixed_planar[static_cast<size_t>(idx_i)].loop, R0, R1, edge_p0, edge_p1, tol);
  ReplaceLoopEdge(mixed_planar[static_cast<size_t>(idx_j)].loop, S0, S1, edge_p0, edge_p1, tol);

  std::vector<Brep::PlanarFace> others;
  std::vector<size_t> others_idx;
  for (size_t f = 0; f < mixed_planar.size(); ++f) {
    if (static_cast<int>(f) == idx_i || static_cast<int>(f) == idx_j) continue;
    others.push_back(mixed_planar[f]);
    others_idx.push_back(f);
  }
  // CollapseNotchRun works purely by matching 3D points, agnostic to
  // whether the dense run it finds is a plain circle-arc (a perpendicular
  // third face on a constant-radius fillet) or the tapered cone's own
  // ellipse notch (EllipseNotchCornerAtVertex, fillet.h) - no separate
  // oblique-rejection branch is needed here the way the cylindrical case
  // needs one, since EVERY notched end of a ConicalFace already uses that
  // same dense-run splice regardless of the third face's own orientation.
  CollapseNotchRun(others, R0, S0, edge_p0, tol);
  CollapseNotchRun(others, R1, S1, edge_p1, tol);
  for (size_t o = 0; o < others.size(); ++o) mixed_planar[others_idx[o]] = std::move(others[o]);

  std::vector<Brep::ConicalFace> remaining_cone;
  for (size_t c = 0; c < mf.conical.size(); ++c) {
    if (static_cast<int>(c) != best) remaining_cone.push_back(mf.conical[c]);
  }
  return Brep::FromMixedFaces(mixed_planar, mf.cylindrical, remaining_cone, mf.spherical);
}

}  // namespace

Brep RemoveBlend(const Brep& solid, Point3d point_on_fillet) {
  const Brep::MixedFacesResult mf = solid.MixedFaces();
  if (mf.cylindrical.empty() && mf.conical.empty()) {
    throw std::invalid_argument("dino8::kernel::RemoveBlend: `solid` has no cylindrical or conical face to remove");
  }
  // Use MixedFaces()' own planar records (NOT PlanarFaces(), which throws
  // outright on any non-planar face - exactly the fillet face this
  // function exists to remove).
  const std::vector<Brep::PlanarFace>& faces = mf.planar;
  const double tol = RelativeTol(faces);

  // Locate the closest fillet face to `point_on_fillet` - cylindrical or
  // conical - measured against its own TRIMMED extent (axial/height
  // position and angular position both clamped to the patch's own real
  // domain), not the infinite surface, so a point near a DIFFERENT
  // fillet's own patch (sharing axis/radius by coincidence) is never
  // mismatched.
  int best_cyl = -1;
  double best_cyl_d = std::numeric_limits<double>::infinity();
  for (size_t c = 0; c < mf.cylindrical.size(); ++c) {
    const Brep::CylindricalFace& cf = mf.cylindrical[c];
    const Vector3d d = point_on_fillet - cf.frame.origin;
    const double h = std::max(0.0, std::min(cf.length, d * cf.frame.zaxis));
    double phi = std::atan2(d * cf.frame.yaxis, d * cf.frame.xaxis);
    if (phi < 0.0) phi += 2.0 * ON_PI;
    phi = std::max(0.0, std::min(cf.angle, phi));
    const Point3d on_surface =
        cf.frame.origin + h * cf.frame.zaxis + cf.radius * (std::cos(phi) * cf.frame.xaxis + std::sin(phi) * cf.frame.yaxis);
    const double dist = on_surface.DistanceTo(point_on_fillet);
    if (dist < best_cyl_d) {
      best_cyl_d = dist;
      best_cyl = static_cast<int>(c);
    }
  }
  int best_cone = -1;
  double best_cone_d = std::numeric_limits<double>::infinity();
  for (size_t c = 0; c < mf.conical.size(); ++c) {
    const Brep::ConicalFace& cf = mf.conical[c];
    if (std::fabs(cf.radius1 - cf.radius0) < 1e-300 || !(cf.length > 0.0)) continue;  // degenerate, skip
    const double tan_half_angle = (cf.radius1 - cf.radius0) / cf.length;
    const double v0 = cf.radius0 / tan_half_angle;
    const double v1 = cf.radius1 / tan_half_angle;
    const double h_lo = std::min(v0, v1), h_hi = std::max(v0, v1);
    const Vector3d d = point_on_fillet - cf.frame.origin;
    const double h = std::max(h_lo, std::min(h_hi, d * cf.frame.zaxis));
    double phi = std::atan2(d * cf.frame.yaxis, d * cf.frame.xaxis);
    if (phi < 0.0) phi += 2.0 * ON_PI;
    phi = std::max(0.0, std::min(cf.angle, phi));
    const double radius_at_h = std::fabs(h * tan_half_angle);
    const Point3d on_surface =
        cf.frame.origin + h * cf.frame.zaxis + radius_at_h * (std::cos(phi) * cf.frame.xaxis + std::sin(phi) * cf.frame.yaxis);
    const double dist = on_surface.DistanceTo(point_on_fillet);
    if (dist < best_cone_d) {
      best_cone_d = dist;
      best_cone = static_cast<int>(c);
    }
  }

  const double accept_tol = std::max(tol * 100.0, 1e-4);
  if ((best_cyl < 0 || best_cyl_d > accept_tol) && (best_cone < 0 || best_cone_d > accept_tol)) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveBlend: `point_on_fillet` is not near any cylindrical or conical face of `solid`");
  }
  if (best_cyl >= 0 && (best_cone < 0 || best_cyl_d <= best_cone_d)) {
    return RemoveCylindricalBlend(mf, best_cyl, faces, tol);
  }
  return RemoveConicalBlend(mf, best_cone, faces, tol);
}


namespace {

// A candidate rail pair for RemoveChamfer: the quad's own edge indices
// (k, k1) and (k2, k3) - two OPPOSITE edges of the 4-point loop.
struct ChamferRailCandidate {
  size_t k = 0, k1 = 0, k2 = 0, k3 = 0;
};

// Attempts to reconstruct the sharp edge from ONE candidate rail pair
// (see RemoveChamfer's own doc comment, step 3). Returns true and fills
// edge_p0/edge_p1/idx_a/idx_b on success; false (no throw) if this
// specific candidate simply doesn't check out - RemoveChamfer itself
// decides what "zero or two candidates succeeded" means.
bool TryReconstructChamferRails(const std::vector<Brep::PlanarFace>& faces, const std::vector<Point3d>& quad,
                                const ChamferRailCandidate& cand, double tol, int exclude_face, Point3d& edge_p0_out,
                                Point3d& edge_p1_out, int& idx_a_out, int& idx_b_out) {
  const Point3d& A0 = quad[cand.k];
  const Point3d& A1 = quad[cand.k1];
  const Point3d& B1 = quad[cand.k2];
  const Point3d& B0 = quad[cand.k3];
  const int idx_a = FindFaceWithEdge(faces, A0, A1, tol, exclude_face);
  const int idx_b = FindFaceWithEdge(faces, B0, B1, tol, exclude_face);
  if (idx_a < 0 || idx_b < 0) return false;  // a real chamfer's own rails are never free boundaries

  const ON_Plane& plane_a = faces[static_cast<size_t>(idx_a)].plane;
  const ON_Plane& plane_b = faces[static_cast<size_t>(idx_b)].plane;
  const Vector3d& n_a = plane_a.zaxis;
  const Vector3d& n_b = plane_b.zaxis;
  Vector3d e = ON_CrossProduct(n_a, n_b);
  // Vector3d::Unitize() alone is not a reliable degeneracy test here: for
  // two EXACTLY parallel unit normals (the common "wrong pairing treats a
  // solid's own two parallel side faces as if they were rails" case -
  // e.g. a box's own +x/-x faces), the cross product is mathematically
  // exactly zero but can carry a tiny nonzero floating-point residual
  // (observed directly: ~1e-17, not exactly 0.0) that Unitize() happily
  // normalizes into an ARBITRARY unit direction instead of failing -
  // caught by comparing e's own raw length against an explicit
  // tolerance BEFORE unitizing, not by trusting Unitize()'s own success
  // flag.
  if (e.Length() < 1e-9) return false;  // candidate faces are parallel - the wrong pairing, or a degenerate one
  e.Unitize();

  const double d_a = n_a * (plane_a.origin - Point3d(0, 0, 0));
  const double d_b = n_b * (plane_b.origin - Point3d(0, 0, 0));
  const Vector3d cross_term = ON_CrossProduct(d_a * n_b - d_b * n_a, e);
  const double e_len2 = e * e;  // == 1.0 (e already unitized), kept explicit to match the doc comment's formula
  const Point3d P0 = Point3d(0, 0, 0) + (1.0 / e_len2) * cross_term;

  auto project = [&](const Point3d& p) { return P0 + ((p - P0) * e) * e; };
  const Point3d edge_p0 = project(A0);
  const Point3d edge_p1 = project(A1);
  // Genuinely discriminating cross-check: the OTHER pair's own corners
  // (from face b's own rail) must independently project to the SAME two
  // points - not merely restating the pairing's own construction, since
  // B0/B1 were never used to build P0/e above.
  if (project(B0).DistanceTo(edge_p0) > std::max(tol * 100.0, 1e-6) ||
      project(B1).DistanceTo(edge_p1) > std::max(tol * 100.0, 1e-6)) {
    return false;
  }
  edge_p0_out = edge_p0;
  edge_p1_out = edge_p1;
  idx_a_out = idx_a;
  idx_b_out = idx_b;
  return true;
}

// Genuine euclidean distance from a 3D point to a planar polygon (its
// own boundary AND interior, not just the infinite plane it lies in) -
// needed here because plane-distance alone is not a reliable "nearest
// face" proxy once a solid has several planar faces whose OWN infinite
// planes all happen to pass close to a given point while only one of
// them actually has that point over its own real, trimmed extent
// (confirmed directly: on a solid with two chamfers, plane-distance
// alone picked an unrelated face whose plane merely passed nearby,
// silently reconstructing garbage rather than throwing - caught by a
// two-chamfer regression, not assumed). Projects `p` onto the face's own
// plane using its (xaxis, yaxis) basis, and either returns the plain
// perpendicular distance (the projection lies inside the polygon, via
// the same PointInPolygon2d this codebase's own clipping code already
// trusts) or the true 3D distance to the polygon's nearest boundary
// point (projection outside) - a real Pythagorean combination of the
// perpendicular and in-plane distances, not an approximation of either.
double DistanceToPlanarFace(const Brep::PlanarFace& f, const Point3d& p) {
  const ON_Plane& pl = f.plane;
  const double perp = pl.DistanceTo(p);
  std::vector<Point2d> poly2d;
  poly2d.reserve(f.loop.size());
  for (const Point3d& v : f.loop) {
    const Vector3d d = v - pl.origin;
    poly2d.emplace_back(d * pl.xaxis, d * pl.yaxis);
  }
  const Vector3d dp = p - pl.origin;
  const Point2d p2d(dp * pl.xaxis, dp * pl.yaxis);
  if (dino8::kernel::detail::circle_clip_detail::PointInPolygon2d(p2d.x, p2d.y, poly2d)) {
    return std::fabs(perp);
  }
  double best_edge_d2 = std::numeric_limits<double>::infinity();
  const size_t n = poly2d.size();
  for (size_t k = 0; k < n; ++k) {
    const Point2d& a = poly2d[k];
    const Point2d& b = poly2d[(k + 1) % n];
    const double ex = b.x - a.x, ey = b.y - a.y;
    const double len2 = ex * ex + ey * ey;
    double t = len2 > 0.0 ? ((p2d.x - a.x) * ex + (p2d.y - a.y) * ey) / len2 : 0.0;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = a.x + t * ex, cy = a.y + t * ey;
    const double dx = p2d.x - cx, dy = p2d.y - cy;
    best_edge_d2 = std::min(best_edge_d2, dx * dx + dy * dy);
  }
  return std::sqrt(perp * perp + best_edge_d2);
}

}  // namespace

Brep RemoveChamfer(const Brep& solid, Point3d point_on_chamfer) {
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  // Nearest planar face by genuine point-to-polygon distance (see
  // DistanceToPlanarFace's own doc comment for why plane distance alone
  // is not enough).
  int best = -1;
  double best_d = std::numeric_limits<double>::infinity();
  for (size_t f = 0; f < faces.size(); ++f) {
    const double d = DistanceToPlanarFace(faces[f], point_on_chamfer);
    if (d < best_d) {
      best_d = d;
      best = static_cast<int>(f);
    }
  }
  if (best < 0 || best_d > std::max(tol * 100.0, 1e-4)) {
    throw std::invalid_argument("dino8::kernel::RemoveChamfer: `point_on_chamfer` is not near any planar face of `solid`");
  }
  const std::vector<Point3d>& quad = faces[static_cast<size_t>(best)].loop;
  if (quad.size() != 4) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveChamfer: the nearest face to `point_on_chamfer` is not a quad - a chamfer built by "
        "ChamferConvexEdge/ChamferConvexEdgeAngle is always exactly 4 points");
  }

  const ChamferRailCandidate cand_a{0, 1, 2, 3};
  const ChamferRailCandidate cand_b{1, 2, 3, 0};
  Point3d edge_p0, edge_p1;
  int idx_i = -1, idx_j = -1;
  const bool ok_a = TryReconstructChamferRails(faces, quad, cand_a, tol, best, edge_p0, edge_p1, idx_i, idx_j);
  Point3d edge_p0_b, edge_p1_b;
  int idx_i_b = -1, idx_j_b = -1;
  const bool ok_b = TryReconstructChamferRails(faces, quad, cand_b, tol, best, edge_p0_b, edge_p1_b, idx_i_b, idx_j_b);
  if (ok_a == ok_b) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveChamfer: the nearest quad face does not reconstruct as a chamfer (neither, or both, "
        "of its two opposite-edge pairings check out) - is this really a ChamferConvexEdge-built face?");
  }
  if (ok_b) {
    edge_p0 = edge_p0_b;
    edge_p1 = edge_p1_b;
    idx_i = idx_i_b;
    idx_j = idx_j_b;
  }
  if (idx_i == idx_j) {
    throw std::invalid_argument("dino8::kernel::RemoveChamfer: both rails resolve to the same face - degenerate geometry");
  }

  // Rail corners, in the SAME face-i-first order TryReconstructChamferRails
  // used to build edge_p0/edge_p1 (A0->edge_p0, A1->edge_p1 on face i's
  // own rail).
  const ChamferRailCandidate& winner = ok_a ? cand_a : cand_b;
  const Point3d R_i0 = quad[winner.k], R_i1 = quad[winner.k1];
  const Point3d R_j1 = quad[winner.k2], R_j0 = quad[winner.k3];

  std::vector<Brep::PlanarFace> mixed_planar = faces;
  ReplaceLoopEdge(mixed_planar[static_cast<size_t>(idx_i)].loop, R_i0, R_i1, edge_p0, edge_p1, tol);
  ReplaceLoopEdge(mixed_planar[static_cast<size_t>(idx_j)].loop, R_j0, R_j1, edge_p0, edge_p1, tol);

  std::vector<Brep::PlanarFace> others;
  std::vector<size_t> others_idx;
  for (size_t f = 0; f < mixed_planar.size(); ++f) {
    if (static_cast<int>(f) == idx_i || static_cast<int>(f) == idx_j || static_cast<int>(f) == best) continue;
    others.push_back(mixed_planar[f]);
    others_idx.push_back(f);
  }
  // End conditions: the two OTHER edges of the chamfer quad (R_j0-R_i0
  // near edge_p0, R_i1-R_j1 near edge_p1) - if a third face was
  // chamfered against there, its own loop has that exact 2-point edge
  // (ChamferEndAtVertex's own splice, no dense polyline); collapse it
  // back to the single restored vertex. A free end (no matching edge on
  // any other face) is a silent no-op, exactly like NotchCornerAtVertex's
  // own contract.
  CollapseNotchRun(others, R_j0, R_i0, edge_p0, tol);
  CollapseNotchRun(others, R_i1, R_j1, edge_p1, tol);
  for (size_t o = 0; o < others.size(); ++o) mixed_planar[others_idx[o]] = std::move(others[o]);

  std::vector<Brep::PlanarFace> result_faces;
  result_faces.reserve(mixed_planar.size() - 1);
  for (size_t f = 0; f < mixed_planar.size(); ++f) {
    if (static_cast<int>(f) == best) continue;
    result_faces.push_back(std::move(mixed_planar[f]));
  }
  return Brep::FromMixedFaces(result_faces, {});
}


namespace {

// One step around `loop` from `at`, landing on whichever of `at`'s own 2
// neighbors is NOT `away_from` - direction-agnostic (works regardless of
// which of pred/succ happens to be which), used by RemoveChamferVertex to
// walk past a chamfer facet's own corner to the ORIGINAL corner's own far
// neighbor along that edge, a point chamfering never touches.
Point3d StepPast(const std::vector<Point3d>& loop, const Point3d& at, const Point3d& away_from, double tol) {
  const size_t n = loop.size();
  for (size_t k = 0; k < n; ++k) {
    if (!PointsEqual(loop[k], at, tol)) continue;
    const Point3d& succ = loop[(k + 1) % n];
    const Point3d& pred = loop[(k + n - 1) % n];
    if (!PointsEqual(succ, away_from, tol)) return succ;
    if (!PointsEqual(pred, away_from, tol)) return pred;
    throw std::runtime_error(
        "dino8::kernel::RemoveChamferVertex: StepPast found `at` with both neighbors equal to `away_from` - "
        "degenerate loop, please report this as a bug");
  }
  throw std::runtime_error(
      "dino8::kernel::RemoveChamferVertex: StepPast could not find `at` on the given loop - please report this as "
      "a bug");
}

// Replaces the 2 CONSECUTIVE loop points {a, b} (in either walk order,
// wraparound included) with the single point `restored` - the genuine
// inverse of ChamferVertexCore's own single-vertex-to-2-point clip.
// Deliberately NOT CollapseNotchRun (this file's own general "run of N
// points between two known endpoints" splice, which RemoveBlend/
// RemoveChamfer already use): that search walks FORWARD from whichever of
// its two target points it meets first in loop order, and - confirmed
// directly here, not a hypothetical - mishandles a pair that is adjacent
// via WRAPAROUND in the order that makes the forward search cross almost
// the WHOLE rest of the loop before reaching the other point, collapsing
// far more of the loop than intended instead of just the 2 points. Every
// pair this function is ever called with is a literal, adjacent 2-point
// edge (never a longer dense run), so checking direct (k, k+1 mod n)
// adjacency directly sidesteps that ambiguity entirely rather than fixing
// it in the shared, more general primitive 3 other established functions
// already depend on.
void CollapseChamferVertexEdge(Brep::PlanarFace& f, const Point3d& a, const Point3d& b, const Point3d& restored,
                               double tol) {
  std::vector<Point3d>& loop = f.loop;
  const size_t n = loop.size();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    const bool fwd = PointsEqual(loop[k], a, tol) && PointsEqual(loop[k1], b, tol);
    const bool bwd = PointsEqual(loop[k], b, tol) && PointsEqual(loop[k1], a, tol);
    if (!fwd && !bwd) continue;
    // Walk the remaining n - 2 points starting right after k1, wrapping
    // via modulo - correct regardless of whether (k, k1) themselves
    // wrap around the array end (an earlier version unrolled this as two
    // plain ranges [k1+1, n) and [0, k), which is only correct when
    // k1 < k; when the pair itself straddles the wraparound (k1 < k does
    // NOT hold, e.g. k == n-1, k1 == 0), that unrolling wrongly re-included
    // one of the two collapsed points itself - caught directly by a
    // round-trip regression test producing a non-manifold result, not
    // assumed).
    std::vector<Point3d> new_loop;
    new_loop.reserve(n - 1);
    new_loop.push_back(restored);
    for (size_t step = 0; step + 2 < n; ++step) new_loop.push_back(loop[(k1 + 1 + step) % n]);
    loop = std::move(new_loop);
    return;
  }
  throw std::runtime_error(
      "dino8::kernel::RemoveChamferVertex: CollapseChamferVertexEdge could not find the expected 2-point edge - "
      "please report this as a bug");
}

}  // namespace

Brep RemoveChamferVertex(const Brep& solid, Point3d point_on_facet) {
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  const double tol = RelativeTol(faces);

  int best = -1;
  double best_d = std::numeric_limits<double>::infinity();
  for (size_t f = 0; f < faces.size(); ++f) {
    const double d = DistanceToPlanarFace(faces[f], point_on_facet);
    if (d < best_d) {
      best_d = d;
      best = static_cast<int>(f);
    }
  }
  if (best < 0 || best_d > std::max(tol * 100.0, 1e-4)) {
    throw std::invalid_argument("dino8::kernel::RemoveChamferVertex: `point_on_facet` is not near any planar face of `solid`");
  }
  const std::vector<Point3d>& corner = faces[static_cast<size_t>(best)].loop;
  if (corner.size() != 3) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveChamferVertex: the nearest face to `point_on_facet` is not a triangle - a vertex "
        "chamfer built by ChamferConvexVertex/ChamferConcaveVertex is always exactly 3 points");
  }
  const Point3d P0 = corner[0], P1 = corner[1], P2 = corner[2];

  // The 3 adjacent faces, one per triangle edge, walked OPPOSITELY on
  // their own loops - the same shared-boundary-edge topology every other
  // function in this file already relies on.
  auto find_adjacent = [&](const Point3d& a, const Point3d& b) {
    for (size_t f = 0; f < faces.size(); ++f) {
      if (static_cast<int>(f) == best) continue;
      const std::vector<Point3d>& loop = faces[f].loop;
      const size_t n = loop.size();
      for (size_t k = 0; k < n; ++k) {
        if (PointsEqual(loop[k], b, tol) && PointsEqual(loop[(k + 1) % n], a, tol)) return static_cast<int>(f);
      }
    }
    return -1;
  };
  const int adj01 = find_adjacent(P0, P1);
  const int adj12 = find_adjacent(P1, P2);
  const int adj20 = find_adjacent(P2, P0);
  if (adj01 < 0 || adj12 < 0 || adj20 < 0 || adj01 == adj12 || adj12 == adj20 || adj01 == adj20) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveChamferVertex: the nearest triangular face does not have 3 distinct adjacent faces - "
        "not a genuine vertex chamfer facet");
  }

  // V: the exact intersection of the 3 adjacent faces' own (unclipped)
  // planes - the standard 3-plane-intersection closed form.
  const Vector3d na = faces[static_cast<size_t>(adj01)].plane.zaxis;
  const Vector3d nb = faces[static_cast<size_t>(adj12)].plane.zaxis;
  const Vector3d nc = faces[static_cast<size_t>(adj20)].plane.zaxis;
  const double det = na * ON_CrossProduct(nb, nc);
  if (std::fabs(det) < 1e-9) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveChamferVertex: the 3 adjacent faces' own planes are (nearly) parallel/coplanar - not "
        "a genuine trihedral corner");
  }
  const double ra = na * faces[static_cast<size_t>(adj01)].plane.origin;
  const double rb = nb * faces[static_cast<size_t>(adj12)].plane.origin;
  const double rc = nc * faces[static_cast<size_t>(adj20)].plane.origin;
  const Vector3d numer =
      ON_CrossProduct(nb, nc) * ra + ON_CrossProduct(nc, na) * rb + ON_CrossProduct(na, nb) * rc;
  const Point3d V(numer.x / det, numer.y / det, numer.z / det);
  for (const std::pair<Vector3d, double> plane_eq : {std::make_pair(na, ra), {nb, rb}, {nc, rc}}) {
    if (std::fabs(plane_eq.first * (V - Point3d(0, 0, 0)) - plane_eq.second) > 1e3 * tol) {
      throw std::runtime_error(
          "dino8::kernel::RemoveChamferVertex: 3-plane intersection solve failed - please report this as a bug");
    }
  }

  // VALIDATION: each triangle corner's own far neighbor, found
  // independently from both of its adjacent faces, must agree - and the
  // corner itself must lie exactly on the ray from V through it.
  const Point3d Na1 = StepPast(faces[static_cast<size_t>(adj01)].loop, P0, P1, tol);
  const Point3d Na2 = StepPast(faces[static_cast<size_t>(adj20)].loop, P0, P2, tol);
  const Point3d Nb1 = StepPast(faces[static_cast<size_t>(adj01)].loop, P1, P0, tol);
  const Point3d Nb2 = StepPast(faces[static_cast<size_t>(adj12)].loop, P1, P2, tol);
  const Point3d Nc1 = StepPast(faces[static_cast<size_t>(adj12)].loop, P2, P1, tol);
  const Point3d Nc2 = StepPast(faces[static_cast<size_t>(adj20)].loop, P2, P0, tol);
  if (!PointsEqual(Na1, Na2, tol) || !PointsEqual(Nb1, Nb2, tol) || !PointsEqual(Nc1, Nc2, tol)) {
    throw std::invalid_argument(
        "dino8::kernel::RemoveChamferVertex: the nearest triangular face does not reconstruct as a genuine vertex "
        "chamfer facet - its own corners' far neighbors disagree between adjacent faces");
  }
  auto check_on_ray = [&](const Point3d& P, const Point3d& N) {
    Vector3d full = N - V;
    const double L = full.Length();
    Vector3d dir = P - V;
    const double d = dir.Length();
    if (!(L > tol) || !(d > tol) || !(d < L - tol) || !full.Unitize() || !dir.Unitize() ||
        (dir - full).Length() > 1e3 * tol) {
      throw std::invalid_argument(
          "dino8::kernel::RemoveChamferVertex: the nearest triangular face does not reconstruct as a genuine "
          "vertex chamfer facet - a corner does not lie on the ray from the reconstructed vertex through its own "
          "far neighbor");
    }
  };
  check_on_ray(P0, Na1);
  check_on_ray(P1, Nb1);
  check_on_ray(P2, Nc1);

  std::vector<Brep::PlanarFace> mixed = faces;
  CollapseChamferVertexEdge(mixed[static_cast<size_t>(adj01)], P0, P1, V, tol);
  CollapseChamferVertexEdge(mixed[static_cast<size_t>(adj12)], P1, P2, V, tol);
  CollapseChamferVertexEdge(mixed[static_cast<size_t>(adj20)], P2, P0, V, tol);

  std::vector<Brep::PlanarFace> result;
  result.reserve(mixed.size() - 1);
  for (size_t f = 0; f < mixed.size(); ++f) {
    if (static_cast<int>(f) == best) continue;
    result.push_back(std::move(mixed[f]));
  }
  return Brep::FromPlanarFaces(result);
}

}  // namespace dino8::kernel
