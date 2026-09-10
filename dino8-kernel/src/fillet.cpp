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

}  // namespace dino8::kernel
