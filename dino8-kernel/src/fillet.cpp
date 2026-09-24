#include "dino8/kernel/fillet.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

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
  Brep::CylindricalFace fillet_face;
  fillet_face.frame.origin = axis_point(edge_p0);
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
  fillet_face.length = edge_p0.DistanceTo(edge_p1);

  // --- (4) assemble: all untouched faces, then the two re-trimmed ones,
  // then the one new CylindricalFace.
  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) {
      others.push_back(faces[f]);
    }
  }

  // Close the two ends of the fillet, where it meets a face perpendicular
  // to the edge at edge_p0/edge_p1 - see NotchCornerAtVertex's own doc
  // comment for why this is needed for a genuinely watertight,
  // volume-correct result whenever the filleted edge runs all the way to
  // such a face (as it always does at both of its own endpoints, unless
  // some other face there happens to be non-planar or oblique, in which
  // case NotchCornerAtVertex leaves that corner untouched).
  NotchCornerAtVertex(others, edge_p0, e, plane_i, plane_j, fillet_face.frame.origin, n_i, frame_yaxis,
                      radius, sweep_angle, tol);
  NotchCornerAtVertex(others, edge_p1, e, plane_i, plane_j,
                      fillet_face.frame.origin + fillet_face.length * e, n_i, frame_yaxis, radius,
                      sweep_angle, tol);

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

}  // namespace dino8::kernel
