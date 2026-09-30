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

// One candidate full-cylinder face, before it's known whether either of
// its own two ends is actually open to the outside, or - for
// RecognizeBosses()/RecognizeCounterboreHoles() below, which both share
// this same scan - anything about how it composes with another
// candidate. `concave` is RecognizeHoles()'s own bore-vs-boss test:
// true for a wall whose material sits OUTSIDE the cylinder (a bore),
// false for one whose material sits INSIDE it (a boss/pin).
struct CylindricalFaceCandidate {
  int face_index = -1;
  double radius = 0.0;
  Point3d axis_ref;   // ON_Cylinder::Center() - an arbitrary point ON the axis line, not necessarily either rim
  Vector3d axis_dir;  // unit
  double t_min = 0.0, t_max = 0.0;  // this face's own axial extent, in dot(point - axis_ref, axis_dir)
  bool concave = false;
};

// Shared first pass RecognizeHoles(), RecognizeBosses() and
// RecognizeCounterboreHoles() all build on: every face of `solid` whose
// underlying surface fits a full (closed, 2*pi) cylinder (`ON_Surface::
// IsCylinder()` - a PARTIAL cylindrical patch, e.g. a fillet's own
// rolling-ball wall, is never a candidate here, hole or boss), tagged
// with its own axial extent and concave/convex orientation. Doesn't
// filter on concavity itself - each caller below picks the half it
// wants - so this is one mesh-independent geometry scan shared by all
// three, not three separate near-identical loops over `brep.m_F`.
std::vector<CylindricalFaceCandidate> ScanFullCylinderFaces(const Brep& solid) {
  const ON_Brep& brep = solid.raw();
  std::vector<CylindricalFaceCandidate> candidates;

  // Same loose-enough-for-a-NURBS-fit's-own-noise, tight-enough-not-to-
  // misclassify tolerance Brep::MixedFaces() uses for its own IsCylinder()
  // call (brep.cpp).
  constexpr double kCylTol = 1e-4;

  for (int fi = 0; fi < brep.m_F.Count(); ++fi) {
    const ON_BrepFace& face = brep.m_F[fi];
    const ON_Surface* srf = face.SurfaceOf();
    if (!srf) continue;

    ON_Cylinder cyl;
    if (!srf->IsCylinder(&cyl, kCylTol)) continue;
    // A partial cylindrical patch (a fillet's own rolling-ball wall, a
    // cylindrical CylindricalFace trimmed to less than the full sweep)
    // isn't itself a hole or boss, whatever solid it's attached to - only
    // a full 2*pi wall is a candidate.
    if (!srf->IsClosed(0)) continue;

    Vector3d axis_dir = cyl.Axis();
    if (!axis_dir.Unitize()) continue;
    const Point3d axis_ref = cyl.Center();
    const double radius = cyl.circle.Radius();
    if (!(radius > 0.0)) continue;

    // Concavity: this face's own outward normal (flipped per m_bRev, the
    // same convention every other orientation-aware read in this kernel
    // uses, e.g. dino8-app's BossRibCommand::ProjectToBase) points TOWARD
    // the axis for a bore cut INTO material (concave), or AWAY from it
    // for a boss/pin sticking OUT of it (convex).
    const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
    const Point3d p_mid = srf->PointAt(du.Mid(), dv.Mid());
    Vector3d n_mid = srf->NormalAt(du.Mid(), dv.Mid());
    if (face.m_bRev) n_mid = -n_mid;
    const Point3d axis_at_mid = axis_ref + ON_DotProduct(p_mid - axis_ref, axis_dir) * axis_dir;
    Vector3d radial_out = p_mid - axis_at_mid;
    if (!radial_out.Unitize()) continue;  // p_mid landed exactly on the axis - degenerate
    const bool concave = ON_DotProduct(n_mid, radial_out) <= 0.0;

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

    candidates.push_back({fi, radius, axis_ref, axis_dir, t_min, t_max, concave});
  }

  return candidates;
}

// Whether the point a small margin PAST each of `c`'s own two ends (ON
// its axis, radius 0 - see RecognizeHoles()'s own doc comment for why
// that specific probe point sidesteps MakeHole()'s disclosed entry-rim
// mesh gap) lies inside `mesh` - the single boolean pair RecognizeHoles(),
// RecognizeBosses() and RecognizeCounterboreHoles() each read with their
// own, different meaning (capped-vs-open for a bore, attached-vs-free for
// a boss).
struct EndOccupancy {
  bool near_inside = false;
  bool far_inside = false;
};

EndOccupancy ClassifyEndOccupancy(const Mesh& mesh, const CylindricalFaceCandidate& c) {
  const double span = c.t_max - c.t_min;
  const double margin = std::min(std::max(tolerance::kDistance * 100.0, c.radius * 1e-3), span * 0.25);
  const Point3d p_near = c.axis_ref + (c.t_min - margin) * c.axis_dir;
  const Point3d p_far = c.axis_ref + (c.t_max + margin) * c.axis_dir;
  return {mesh.ContainsPoint(p_near), mesh.ContainsPoint(p_far)};
}

}  // namespace

std::vector<HoleFeature> RecognizeHoles(const Brep& solid) {
  std::vector<HoleFeature> out;

  std::vector<CylindricalFaceCandidate> candidates;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (c.concave) candidates.push_back(c);
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

  for (const CylindricalFaceCandidate& c : candidates) {
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, c);
    const bool near_open = !occ.near_inside;
    const bool far_open = !occ.far_inside;
    if (!near_open && !far_open) continue;  // both ends capped: an enclosed cavity, not a hole feature

    HoleFeature hf;
    hf.radius = c.radius;
    hf.face_index = c.face_index;
    hf.depth = c.t_max - c.t_min;
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

std::vector<BossFeature> RecognizeBosses(const Brep& solid) {
  std::vector<BossFeature> out;

  std::vector<CylindricalFaceCandidate> candidates;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (!c.concave) candidates.push_back(c);
  }
  if (candidates.empty()) return out;

  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  for (const CylindricalFaceCandidate& c : candidates) {
    // Same two on-axis probes RecognizeHoles() uses, but read with the
    // opposite (boss) meaning: a probe point that lands INSIDE `solid`
    // means this end is still backed by more material (ATTACHED, e.g.
    // the base a boss emerges from); a probe point OUTSIDE means this
    // end is exposed to open air (FREE, e.g. a boss's own tip).
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, c);
    const bool near_attached = occ.near_inside;
    const bool far_attached = occ.far_inside;
    if (near_attached && far_attached) continue;  // both ends embedded: not a visible boss feature

    BossFeature bf;
    bf.radius = c.radius;
    bf.face_index = c.face_index;
    bf.height = c.t_max - c.t_min;
    if (!near_attached && !far_attached) {
      // Free on both ends - NOT a peg embedded partway through a wall and
      // protruding out both sides (that shape's own wall splits into two
      // disjoint faces at the embedding point, each independently
      // through=false - see BossFeature's own doc comment). This is a
      // candidate genuinely unbacked by material at either end within the
      // probe margin, e.g. a free-standing rod. Mirrors HoleFeature's own
      // through case: pin one end arbitrarily (face-index order).
      bf.through = true;
      bf.origin = c.axis_ref + c.t_min * c.axis_dir;
      bf.axis = c.axis_dir;
    } else if (far_attached) {  // near end free (the tip), far end attached (the base)
      bf.through = false;
      bf.origin = c.axis_ref + c.t_max * c.axis_dir;
      bf.axis = -c.axis_dir;
    } else {  // near end attached (the base), far end free (the tip)
      bf.through = false;
      bf.origin = c.axis_ref + c.t_min * c.axis_dir;
      bf.axis = c.axis_dir;
    }
    out.push_back(bf);
  }

  return out;
}

std::vector<CounterboreFeature> RecognizeCounterboreHoles(const Brep& solid) {
  std::vector<CounterboreFeature> out;

  std::vector<CylindricalFaceCandidate> candidates;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (c.concave) candidates.push_back(c);
  }
  if (candidates.size() < 2) return out;

  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  std::vector<bool> consumed(candidates.size(), false);
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (consumed[i]) continue;
    for (size_t j = 0; j < candidates.size(); ++j) {
      if (i == j || consumed[j]) continue;
      const CylindricalFaceCandidate& a = candidates[i];
      const CylindricalFaceCandidate& b = candidates[j];

      // Same axis LINE: parallel directions (either sign - `b`'s own
      // direction gets flipped below once we know which of the two
      // faces is the shallower, entry-side one) and `b`'s own axis
      // reference point sitting ON `a`'s axis line, not merely parallel
      // to it (two independent holes drilled on parallel axes must not
      // merge).
      const double align = std::fabs(ON_DotProduct(a.axis_dir, b.axis_dir));
      if (align < 1.0 - tolerance::kAlignment) continue;
      const Vector3d to_b = b.axis_ref - a.axis_ref;
      const Vector3d off_axis = to_b - ON_DotProduct(to_b, a.axis_dir) * a.axis_dir;
      if (off_axis.Length() > tolerance::kDistance * 100.0) continue;  // not the same line

      // The wider face must be the shallower (entry-side) one: its own
      // far end, projected into `a`'s own coordinate frame, must
      // coincide with the narrower face's own near end - an adjacent,
      // non-overlapping step, CounterboreHole()'s/MakeCounterboreHole()'s
      // own construction, not two overlapping or gapped cylinders.
      const CylindricalFaceCandidate* wide = nullptr;
      const CylindricalFaceCandidate* narrow = nullptr;
      if (a.radius > b.radius) {
        wide = &a;
        narrow = &b;
      } else if (b.radius > a.radius) {
        wide = &b;
        narrow = &a;
      } else {
        continue;  // same radius - not a counterbore step at all
      }

      // `narrow`'s own axis may point either way relative to `wide`'s, so
      // don't assume which of its own two ends (t_min or t_max, in its
      // own independently-scanned, arbitrary-sign frame) is the one
      // touching `wide` - compute BOTH of narrow's own end points in 3D
      // and pick whichever actually sits at `wide`'s own far end, rather
      // than guessing from the two axes' relative sign (a sign-based
      // guess here was tried and got the projection backwards for the
      // anti-parallel case - comparing the two candidate 3D points
      // directly has no sign to get wrong).
      const Point3d wide_far = wide->axis_ref + wide->t_max * wide->axis_dir;
      const Point3d narrow_end_lo = narrow->axis_ref + narrow->t_min * narrow->axis_dir;
      const Point3d narrow_end_hi = narrow->axis_ref + narrow->t_max * narrow->axis_dir;
      const bool lo_is_near = wide_far.DistanceTo(narrow_end_lo) <= wide_far.DistanceTo(narrow_end_hi);
      const Point3d narrow_near_pt = lo_is_near ? narrow_end_lo : narrow_end_hi;
      const Point3d narrow_far_pt = lo_is_near ? narrow_end_hi : narrow_end_lo;
      if (wide_far.DistanceTo(narrow_near_pt) > tolerance::kDistance * 100.0) continue;

      // Confirmed: `wide`'s far end is exactly `narrow`'s near end.
      // Project `narrow`'s own far end onto `wide`'s own axis (a plain
      // dot product, immune to whichever sign `narrow`'s own axis_dir
      // happened to come back as) to get its position in `wide`'s own t
      // coordinate, then build the merged feature in `wide`'s own frame -
      // `wide`'s near end is the counterbore's own entry point, this
      // projected value is the pilot bore's own far end.
      const double wide_span = wide->t_max - wide->t_min;
      const double far_t = ON_DotProduct(narrow_far_pt - wide->axis_ref, wide->axis_dir);
      const double narrow_span = far_t - wide->t_max;
      if (!(narrow_span > 0.0)) continue;  // degenerate: narrow's far end doesn't extend past the step

      CylindricalFaceCandidate merged = *wide;
      merged.t_max = far_t;
      const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, merged);
      const bool near_open = !occ.near_inside;
      const bool far_open = !occ.far_inside;
      if (!near_open) continue;  // a counterbore's own entry must be open to the outside

      CounterboreFeature cf;
      cf.origin = wide->axis_ref + wide->t_min * wide->axis_dir;
      cf.axis = wide->axis_dir;
      cf.counterbore_radius = wide->radius;
      cf.counterbore_depth = wide_span;
      cf.drill_radius = narrow->radius;
      cf.drill_depth = wide_span + narrow_span;
      cf.through = far_open;
      cf.counterbore_face_index = wide->face_index;
      cf.drill_face_index = narrow->face_index;
      out.push_back(cf);

      consumed[i] = true;
      consumed[j] = true;
      break;
    }
  }

  return out;
}

}  // namespace dino8::kernel
