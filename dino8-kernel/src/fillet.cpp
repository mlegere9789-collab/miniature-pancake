#include "dino8/kernel/fillet.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
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
      f.notch_begin = static_cast<int>(k);
      f.notch_count = static_cast<int>(arc.size());
      break;  // this face's corner is notched; a face shouldn't need it twice at the same vertex
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

Brep FilletConvexEdgeTapered(const Brep& solid, Point3d edge_p0, Point3d edge_p1, double radius0,
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

  const double m = (radius1 - radius0) / L;

  // --- Step 1 (rail exactness, for ANY r(t)): k_i, k_j fixed vectors;
  // rail_i(t) = edge_p0 + t*e + r(t)*k_i always lies exactly in plane i
  // (k_i . n_i == 0, checked directly: n_i.n_i - (bis.n_i)/cosb == 1 - 1
  // == 0), and analogously for rail_j/plane j - see this function's own
  // doc comment for the full derivation.
  const Vector3d k_i = n_i - bis * (1.0 / cosb);
  const Vector3d k_j = n_j - bis * (1.0 / cosb);
  auto r_of = [&](double t) { return radius0 + m * t; };
  auto rail_i = [&](double t) { return edge_p0 + t * e + r_of(t) * k_i; };
  auto rail_j = [&](double t) { return edge_p0 + t * e + r_of(t) * k_j; };

  // --- Step 3 (cone apex/axis): the spine C(t) = edge_p0 + t*e -
  // bis*r(t)/cosb has CONSTANT derivative U = e - (m/cosb)*bis whenever
  // r(t) is linear (e.bis == 0, so this is a real consequence of r(t)
  // being linear, not an assumption) - i.e. C(t) is itself a straight
  // line. r(t) hits exactly zero at t* = -radius0/m, and C(t*) = edge_p0 +
  // t**e exactly (the bisector-offset term vanishes there since it's
  // proportional to r(t*) == 0) - a genuinely checked, not assumed,
  // consequence: the fillet's own apex sits exactly ON the original sharp
  // edge's own infinite line.
  const double t_star = -radius0 / m;
  const Point3d apex = edge_p0 + t_star * e;

  const Vector3d U = e - bis * (m / cosb);
  const double Umag = U.Length();
  // Umag >= 1 always (U = e - (m/cosb)*bis with e.bis==0, |e|=|bis|=1, so
  // Umag = sqrt(1+(m/cosb)^2) >= 1) - never degenerate.
  Vector3d u_hat = U;
  u_hat.Unitize();

  // c = sqrt(1-(m/Umag)^2): the canal-surface characteristic-circle
  // "does it degenerate" factor - here a genuine CONSTANT (not a function
  // of t) because the spine is straight and r(t) is linear. Provably in
  // (0, 1] for any valid convex dihedral (cosb in (0,1)) and ANY m,
  // however large - see this function's own doc comment and
  // FilletConvexEdgeTapered's own verification tests for the closed-form
  // check this was validated against: |m|/Umag == |m|*cosb/sqrt(cosb^2+m^2)
  // < cosb < 1 always, so the classical canal-surface "does the
  // characteristic circle degenerate" condition never fails here - no
  // extra "taper too steep" failure mode beyond radius0, radius1 > 0.
  const double m_over_Umag = m / Umag;
  const double c = std::sqrt(std::max(0.0, 1.0 - m_over_Umag * m_over_Umag));
  if (c < 1e-9) {
    // Should not happen per the proof above for any finite m and
    // cosb in (0,1) - kept as a checked invariant, not silently trusted.
    throw std::runtime_error(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate cone construction "
        "(c too small) - this should be mathematically impossible for a valid "
        "convex dihedral; please report this as a bug");
  }

  // The cone's own true angular sweep, between the two rails' own
  // characteristic-circle angles - NOT simply sweep_angle0 (the plain
  // planar-dihedral angle) once m != 0, because the rails' own directions
  // are no longer perpendicular to the cone's own axis u_hat the way they
  // were perpendicular to e in the m=0 case. Closed form, derived from
  // and cross-checked against the standard canal-surface
  // characteristic-circle formula (see this function's own doc comment
  // and its verification tests): cos(cone_sweep) =
  // (dot_ij - (m/Umag)^2) / c^2, reducing exactly to dot_ij (i.e.
  // sweep_angle0) as m -> 0, confirmed to stay in (0, pi) - never
  // reflex - for every convex dihedral/taper combination tested.
  double cos_cone_sweep = (dot_ij - m_over_Umag * m_over_Umag) / (c * c);
  cos_cone_sweep = std::max(-1.0, std::min(1.0, cos_cone_sweep));
  const double cone_sweep_angle = std::acos(cos_cone_sweep);

  // Cone frame: xaxis is n_i's own component perpendicular to the cone's
  // axis (the projection that makes rail_i sit at angle 0 on the cone's
  // own characteristic circle - verified directly against the standard
  // formula, see this function's own doc comment); yaxis completes a
  // right-handed frame the same way FilletConvexEdge's own frame does.
  Vector3d xaxis = n_i - (n_i * u_hat) * u_hat;
  if (!xaxis.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::FilletConvexEdgeTapered: degenerate cone frame (face "
        "i's own normal is parallel to the cone's own axis) - should not "
        "happen given c > 0 above; please report this as a bug");
  }
  Vector3d yaxis = ON_CrossProduct(u_hat, xaxis);
  yaxis.Unitize();

  // True cone cross-section radii at the patch's own two ends (radius*c,
  // NOT the raw rolling-ball radius - see Brep::ConicalFace's own doc
  // comment for why) and the true axial length between them (L*c^2*Umag,
  // a direct closed form from v(t) = (t - t*) * c^2 * Umag).
  Brep::ConicalFace fillet_face;
  fillet_face.frame.origin = apex;
  fillet_face.frame.xaxis = xaxis;
  fillet_face.frame.yaxis = yaxis;
  fillet_face.frame.zaxis = u_hat;
  fillet_face.frame.UpdateEquation();
  fillet_face.radius0 = radius0 * c;
  fillet_face.radius1 = radius1 * c;
  fillet_face.angle = cone_sweep_angle;
  fillet_face.length = L * c * c * Umag;

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
  // the one new ConicalFace. Deliberately NO NotchCornerAtVertex call -
  // v1's own explicit, honest scope-out (see this function's own doc
  // comment): once m != 0 the cone's own axis is not parallel to e, so a
  // third face perpendicular to e has a true cross-section there that is
  // an ELLIPSE, not the circular arc NotchCornerAtVertex hardcodes -
  // reusing it unchanged would be silently wrong, so any such face's
  // sharp corner is left untouched instead.
  std::vector<Brep::PlanarFace> others;
  others.reserve(faces.size() - 2);
  for (size_t f = 0; f < faces.size(); ++f) {
    if (static_cast<int>(f) != idx_i && static_cast<int>(f) != idx_j) {
      others.push_back(faces[f]);
    }
  }

  std::vector<Brep::PlanarFace> mixed_planar = std::move(others);
  mixed_planar.push_back(std::move(retrimmed_i));
  mixed_planar.push_back(std::move(retrimmed_j));

  return Brep::FromMixedFaces(mixed_planar, {}, {fillet_face});
}

}  // namespace dino8::kernel
