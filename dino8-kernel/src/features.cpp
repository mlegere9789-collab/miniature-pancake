#include "dino8/kernel/features.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/tolerance.h"

namespace dino8::kernel {

namespace {

// An arbitrary orthonormal frame with `axis` (unitized) as its z-axis -
// a full 2*pi CylindricalFace sweep doesn't care where the angle-0 seam
// sits, so any perpendicular x/y pair works. Same manual field-setting +
// UpdateEquation() convention CylindricalFace's own producers already
// use (see dino8-kernel/tests/test_basic.cpp's BuildDrilledBoxInputs),
// generalized from an axis-aligned z to an arbitrary one via a plain
// Gram-Schmidt step.
ON_Plane MakeAxisFrame(Point3d origin, Vector3d axis) {
  Vector3d z = axis;
  if (!z.Unitize()) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: axis must be nonzero");
  }
  const Vector3d seed = (std::fabs(z.x) < 0.9) ? Vector3d(1, 0, 0) : Vector3d(0, 1, 0);
  Vector3d x = seed - z * ON_DotProduct(seed, z);
  x.Unitize();
  Vector3d y = ON_CrossProduct(z, x);
  y.Unitize();

  ON_Plane frame;
  frame.origin = origin;
  frame.xaxis = x;
  frame.yaxis = y;
  frame.zaxis = z;
  frame.UpdateEquation();
  return frame;
}

}  // namespace

Brep CounterboreHole(const Brep& solid, Point3d origin, Vector3d axis, double drill_radius, double drill_depth,
                      double counterbore_radius, double counterbore_depth) {
  if (!(drill_radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: drill_radius must be positive");
  }
  if (!(drill_depth > 0.0)) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: drill_depth must be positive");
  }
  if (!(counterbore_radius > drill_radius)) {
    throw std::invalid_argument(
        "dino8::kernel::CounterboreHole: counterbore_radius must be strictly greater than drill_radius");
  }
  if (!(counterbore_depth > 0.0)) {
    throw std::invalid_argument("dino8::kernel::CounterboreHole: counterbore_depth must be positive");
  }
  if (!(counterbore_depth < drill_depth)) {
    throw std::invalid_argument(
        "dino8::kernel::CounterboreHole: counterbore_depth must be strictly less than drill_depth - a counterbore "
        "needs an actual narrower bore beyond its own recess");
  }

  const ON_Plane frame = MakeAxisFrame(origin, axis);

  // A single compound cutter Brep with TWO CylindricalFaces covering
  // ADJACENT, NON-OVERLAPPING axial ranges - the wide counterbore wall
  // over [0, counterbore_depth], the narrow drill wall over
  // [counterbore_depth, drill_depth] - fed into ONE
  // BooleanCombineMixed(..., BooleanOp::Difference) pass against `solid`.
  //
  // This is deliberately NOT built as two full-length, axially-OVERLAPPING
  // cylinders (one for the whole bore, one for the recess) composed via
  // two sequential Difference passes against `solid`, nor via a prior
  // Union of two such overlapping bare cylinders: both were tried and
  // both fail. Two sequential Difference passes against `solid` (in
  // either order) eventually clip some face that already has a circular
  // boundary (either `solid`'s own starting face, hit by both the drill's
  // and the counterbore's circle in turn, or the counterbore's own
  // freshly-cut recess-bottom disc, itself circular, later re-clipped by
  // the drill's circle) against a SECOND, different circle -
  // ClipPolygonByCircle3d's own doc comment (boolean.h) explicitly
  // disclaims exactly this ("a once-already-clipped, non-rectangular poly
  // losing convexity for a second interacting circle" is out of scope),
  // confirmed directly: it throws std::invalid_argument in practice.
  // Unioning two full-length overlapping bare (uncapped) cylinders first -
  // the seemingly obvious alternative - was also tried and confirmed
  // directly to produce a wrong volume and a non-manifold result: that
  // pairing is not one of BooleanCombineMixed's own documented
  // CYLINDER/CYLINDER cases (those are all stated for a cylinder against
  // an otherwise-real solid, not two bare open tubes against each other).
  //
  // Adjacent, non-overlapping segments sidestep the whole issue: `solid`'s
  // own starting face is only ever crossed by the counterbore's own
  // (outer, wider) circle, `solid`'s own far face (if the drill reaches
  // it) is only ever crossed by the drill's own (inner, narrower) circle,
  // and the step between the two cutter radii is an entirely NEW face
  // this call itself synthesizes - not a second clip of any pre-existing
  // one. Confirmed directly (see this feature's own tests): the result is
  // `raw().IsValid()`, its tessellated volume matches the hand-derived
  // value, and `TessellateToClosedMeshConforming()`'s own mesh is a
  // genuine `IsClosedManifold()`.
  Brep::CylindricalFace counterbore_cf;
  counterbore_cf.frame = frame;
  counterbore_cf.radius = counterbore_radius;
  counterbore_cf.angle = 2.0 * ON_PI;
  counterbore_cf.length = counterbore_depth;

  Brep::CylindricalFace drill_cf;
  drill_cf.frame = frame;
  drill_cf.frame.origin = frame.PointAt(0, 0, counterbore_depth);
  drill_cf.radius = drill_radius;
  drill_cf.angle = 2.0 * ON_PI;
  drill_cf.length = drill_depth - counterbore_depth;

  const Brep tool = Brep::FromMixedFaces({}, {counterbore_cf, drill_cf});
  return BooleanCombineMixed(solid, tool, BooleanOp::Difference);
}

namespace {

// One candidate concave cylindrical face, before it's known whether
// either of its own two ends is actually open to the outside.
struct CylindricalHoleCandidate {
  int face_index = -1;
  double radius = 0.0;
  Point3d axis_ref;   // ON_Cylinder::Center() - an arbitrary point ON the axis line, not necessarily either rim
  Vector3d axis_dir;  // unit
  double t_min = 0.0, t_max = 0.0;  // this face's own axial extent, in dot(point - axis_ref, axis_dir)
};

}  // namespace

std::vector<HoleFeature> RecognizeHoles(const Brep& solid) {
  const ON_Brep& brep = solid.raw();
  std::vector<HoleFeature> out;

  // Same loose-enough-for-a-NURBS-fit's-own-noise, tight-enough-not-to-
  // misclassify tolerance Brep::MixedFaces() uses for its own IsCylinder()
  // call (brep.cpp).
  constexpr double kCylTol = 1e-4;

  std::vector<CylindricalHoleCandidate> candidates;

  for (int fi = 0; fi < brep.m_F.Count(); ++fi) {
    const ON_BrepFace& face = brep.m_F[fi];
    const ON_Surface* srf = face.SurfaceOf();
    if (!srf) continue;

    ON_Cylinder cyl;
    if (!srf->IsCylinder(&cyl, kCylTol)) continue;
    // A partial cylindrical patch (a fillet's own rolling-ball wall, a
    // cylindrical CylindricalFace trimmed to less than the full sweep)
    // isn't itself a hole, whatever solid it's attached to - only a full
    // 2*pi bore is a candidate.
    if (!srf->IsClosed(0)) continue;

    Vector3d axis_dir = cyl.Axis();
    if (!axis_dir.Unitize()) continue;
    const Point3d axis_ref = cyl.Center();
    const double radius = cyl.circle.Radius();
    if (!(radius > 0.0)) continue;

    // Concavity: this face's own outward normal (flipped per m_bRev, the
    // same convention every other orientation-aware read in this kernel
    // uses, e.g. dino8-app's BossRibCommand::ProjectToBase) must point
    // TOWARD the axis for this to be a bore cut INTO material rather than
    // a boss/pin sticking OUT of it.
    const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
    const Point3d p_mid = srf->PointAt(du.Mid(), dv.Mid());
    Vector3d n_mid = srf->NormalAt(du.Mid(), dv.Mid());
    if (face.m_bRev) n_mid = -n_mid;
    const Point3d axis_at_mid = axis_ref + ON_DotProduct(p_mid - axis_ref, axis_dir) * axis_dir;
    Vector3d radial_out = p_mid - axis_at_mid;
    if (!radial_out.Unitize()) continue;  // p_mid landed exactly on the axis - degenerate
    if (ON_DotProduct(n_mid, radial_out) > 0.0) continue;  // convex (a boss) - not a hole

    // This face's own axial extent: the min/max, over every point of
    // every edge in every loop this face has, of t = dot(point - axis_ref,
    // axis_dir). Deliberately NOT derived from this face's own two rim
    // EDGES specifically (which BooleanCombineGeneral's own fragmentation
    // - see boolean_general.h's own top-of-file scope note - can leave as
    // dozens of short polyline segments per rim, sometimes genuinely
    // naked at the entry rim's own disclosed "tool's far end floats
    // entirely inside the target" gap, MakeHole()'s own doc comment):
    // a plain global min/max over every sampled point needs no loop/trim
    // ADJACENCY reasoning at all, so it's immune to exactly that
    // fragmentation and to the entry-rim gap alike.
    double t_min = std::numeric_limits<double>::infinity();
    double t_max = -std::numeric_limits<double>::infinity();
    for (int li = 0; li < face.m_li.Count(); ++li) {
      const int loop_index = face.m_li[li];
      if (loop_index < 0 || loop_index >= brep.m_L.Count()) continue;
      const ON_BrepLoop& loop = brep.m_L[loop_index];
      for (int k = 0; k < loop.m_ti.Count(); ++k) {
        const int ti = loop.m_ti[k];
        if (ti < 0 || ti >= brep.m_T.Count()) continue;
        const ON_BrepTrim& trim = brep.m_T[ti];
        if (trim.m_ei < 0 || trim.m_ei >= brep.m_E.Count()) continue;
        const ON_BrepEdge& edge = brep.m_E[trim.m_ei];
        const ON_Interval ed = edge.Domain();
        constexpr int kSamples = 4;
        for (int s = 0; s <= kSamples; ++s) {
          const Point3d p = edge.PointAt(ed.ParameterAt(static_cast<double>(s) / kSamples));
          const double t = ON_DotProduct(p - axis_ref, axis_dir);
          t_min = std::min(t_min, t);
          t_max = std::max(t_max, t);
        }
      }
    }
    if (!(t_max > t_min)) continue;  // no real axial extent found - degenerate/unreadable face

    candidates.push_back({fi, radius, axis_ref, axis_dir, t_min, t_max});
  }

  if (candidates.empty()) return out;

  // Tessellated once and reused for every candidate below: whether a
  // candidate's own end is open (continues into open air) or capped
  // (blocked by more of `solid`'s own material) is answered by sampling
  // a point just past that end, ON THE AXIS, and asking whether it lies
  // inside this mesh (Mesh::ContainsPoint(), mesh.h) - a point at radius
  // 0 from the bore's own axis is never close to the specific "entry rim"
  // topology gap MakeHole()'s own doc comment discloses (that gap sits at
  // radius == the hole's own radius, not on the centerline), so this
  // sidesteps it entirely rather than working around it.
  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  for (const CylindricalHoleCandidate& c : candidates) {
    const double span = c.t_max - c.t_min;
    const double margin = std::min(std::max(tolerance::kDistance * 100.0, c.radius * 1e-3), span * 0.25);
    const Point3d p_near = c.axis_ref + (c.t_min - margin) * c.axis_dir;
    const Point3d p_far = c.axis_ref + (c.t_max + margin) * c.axis_dir;
    const bool near_open = !solid_mesh.ContainsPoint(p_near);
    const bool far_open = !solid_mesh.ContainsPoint(p_far);
    if (!near_open && !far_open) continue;  // both ends capped: an enclosed cavity, not a hole feature

    HoleFeature hf;
    hf.radius = c.radius;
    hf.face_index = c.face_index;
    hf.depth = span;
    if (near_open && far_open) {
      hf.through = true;
      hf.origin = c.axis_ref + c.t_min * c.axis_dir;
      hf.axis = c.axis_dir;
    } else if (near_open) {  // far end capped
      hf.through = false;
      hf.origin = c.axis_ref + c.t_min * c.axis_dir;
      hf.axis = c.axis_dir;
    } else {  // near end capped, far end open
      hf.through = false;
      hf.origin = c.axis_ref + c.t_max * c.axis_dir;
      hf.axis = -c.axis_dir;
    }
    out.push_back(hf);
  }

  return out;
}

}  // namespace dino8::kernel
