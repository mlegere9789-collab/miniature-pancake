#include "dino8/kernel/features.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
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

// One matched pair of coaxial, adjacent, non-overlapping full-cylinder
// candidates - the geometric "stepped" pattern both
// RecognizeCounterboreHoles() (concave candidates) and
// RecognizeSteppedBosses() (convex candidates) look for, extracted here
// so neither duplicates the other's own matching loop (the same "share
// the scanning geometry, not a second copy of the loop" precedent
// ScanFullCylinderFaces() itself already set for RecognizeHoles()/
// RecognizeBosses()/RecognizeCounterboreHoles()). Deliberately agnostic
// to which candidate is wider: a counterbore's own recess is ALWAYS the
// shallower, entry-side segment (RecognizeCounterboreHoles() itself
// checks that), but a stepped BOSS has no such fixed convention - either
// segment can be the one actually attached to the body (see
// SteppedBossFeature's own doc comment) - so this helper reports
// `first`/`second` purely by which end of `first` genuinely touches
// `second` in 3D, never by radius.
//
// `origin` is `first`'s own OUTER (non-touching) end; `axis` is a unit
// vector from there through the step and on to `second`'s own OUTER end;
// `first_length`/`second_length` are each segment's own axial length.
// Each candidate index is consumed by at most one pair; a chain of 3+
// same-axis steps only ever contributes its first adjacent pair (a
// disclosed, not-yet-closed limitation - see RecognizeCounterboreHoles()'s
// and RecognizeSteppedBosses()'s own doc comments).
struct SteppedPair {
  size_t first_index = 0;
  size_t second_index = 0;
  Point3d origin;
  Vector3d axis;
  double first_length = 0.0;
  double second_length = 0.0;
};

std::vector<SteppedPair> FindAdjacentSteppedPairs(const std::vector<CylindricalFaceCandidate>& candidates) {
  std::vector<SteppedPair> pairs;
  std::vector<bool> consumed(candidates.size(), false);
  const double kTol = tolerance::kDistance * 100.0;

  for (size_t i = 0; i < candidates.size(); ++i) {
    if (consumed[i]) continue;
    for (size_t j = 0; j < candidates.size(); ++j) {
      if (i == j || consumed[j]) continue;
      const CylindricalFaceCandidate& a = candidates[i];
      const CylindricalFaceCandidate& b = candidates[j];
      if (a.radius == b.radius) continue;  // same radius - not a step at all

      // Same axis LINE: parallel directions (either sign) and `b`'s own
      // axis reference point sitting ON `a`'s axis line, not merely
      // parallel to it (two independent features on parallel axes must
      // not merge).
      const double align = std::fabs(ON_DotProduct(a.axis_dir, b.axis_dir));
      if (align < 1.0 - tolerance::kAlignment) continue;
      const Vector3d to_b = b.axis_ref - a.axis_ref;
      const Vector3d off_axis = to_b - ON_DotProduct(to_b, a.axis_dir) * a.axis_dir;
      if (off_axis.Length() > kTol) continue;  // not the same line

      // Each candidate's own two ends, in 3D - deliberately not compared
      // by sign of axis_dir (each candidate's own independent ON_Cylinder
      // fit gives it an arbitrary, unrelated sign - a sign-based guess
      // here was tried and got the projection backwards for the
      // anti-parallel case). Exactly one of `a`'s own two ends must
      // coincide with exactly one of `b`'s own two ends for this to be a
      // genuine adjacent, non-overlapping step; the two OTHER
      // (non-touching) ends become this pair's own two outer termini.
      const Point3d a_lo = a.axis_ref + a.t_min * a.axis_dir;
      const Point3d a_hi = a.axis_ref + a.t_max * a.axis_dir;
      const Point3d b_lo = b.axis_ref + b.t_min * b.axis_dir;
      const Point3d b_hi = b.axis_ref + b.t_max * b.axis_dir;

      Point3d touch, a_outer, b_outer;
      if (a_hi.DistanceTo(b_lo) <= kTol) {
        touch = a_hi;
        a_outer = a_lo;
        b_outer = b_hi;
      } else if (a_lo.DistanceTo(b_hi) <= kTol) {
        touch = a_lo;
        a_outer = a_hi;
        b_outer = b_lo;
      } else if (a_hi.DistanceTo(b_hi) <= kTol) {
        touch = a_hi;
        a_outer = a_lo;
        b_outer = b_lo;
      } else if (a_lo.DistanceTo(b_lo) <= kTol) {
        touch = a_lo;
        a_outer = a_hi;
        b_outer = b_hi;
      } else {
        continue;  // no shared endpoint at all - not adjacent
      }

      const double first_length = a_outer.DistanceTo(touch);
      const double second_length = b_outer.DistanceTo(touch);
      if (!(first_length > 0.0) || !(second_length > 0.0)) continue;  // degenerate

      Vector3d axis = touch - a_outer;
      if (!axis.Unitize()) continue;
      // `axis` must actually continue straight on to `b_outer` (a genuine
      // adjacent step, not an overlap or a fold-back) - confirmed
      // directly here, not assumed from either candidate's own
      // independently-fitted axis.
      const Point3d predicted_b_outer = touch + axis * second_length;
      if (predicted_b_outer.DistanceTo(b_outer) > kTol) continue;

      SteppedPair sp;
      sp.first_index = i;
      sp.second_index = j;
      sp.origin = a_outer;
      sp.axis = axis;
      sp.first_length = first_length;
      sp.second_length = second_length;
      pairs.push_back(sp);
      consumed[i] = true;
      consumed[j] = true;
      break;
    }
  }
  return pairs;
}

// One chain of THREE OR MORE coaxial, pairwise-adjacent, non-overlapping
// full-cylinder candidates on the SAME axis line, each consecutive pair
// differing in radius - the generalization of SteppedPair/
// FindAdjacentSteppedPairs() above from exactly two segments to an
// arbitrary chain length, shared by RecognizeSteppedHoleChains() (concave
// candidates) and RecognizeSteppedBossChains() (convex candidates) below
// the same way FindAdjacentSteppedPairs() itself is already shared by
// RecognizeCounterboreHoles()/RecognizeSteppedBosses() - one chain-walking
// geometry scan, not two near-identical copies of it.
//
// `indices` lists the chain's own constituent candidates (indices into
// the `candidates` vector FindSteppedChains() was called with) in
// physical entry-to-far geometric order. `first_outer_is_min` is the only
// per-candidate sign information a caller needs to turn this ordered
// index list into an absolute 3D origin/axis: it records whether the
// FIRST element's own OUTER (non-touching, i.e. not shared with the next
// element) end is that candidate's own t_min end (true) or t_max end
// (false) - every other step's own length is simply its own t_max - t_min
// (each candidate's own axial extent IS its own segment length, and each
// successive segment is already confirmed, during the walk below, to
// start exactly where the previous one's far end sits), so no further 3D
// touch-point recomputation is needed past this one bit.
struct SteppedChain {
  std::vector<size_t> indices;
  bool first_outer_is_min = false;
};

std::vector<SteppedChain> FindSteppedChains(const std::vector<CylindricalFaceCandidate>& candidates) {
  const size_t n = candidates.size();
  const double kTol = tolerance::kDistance * 100.0;

  // Per-candidate, per-end adjacency: link[i][0] describes candidate i's
  // own t_min end, link[i][1] its own t_max end. `ambiguous` marks an end
  // that more than one OTHER candidate's own end claims to touch - a
  // degenerate, not-actually-a-simple-chain topology this function
  // conservatively refuses to walk through (treated as a chain terminus),
  // rather than picking one of several candidates arbitrarily.
  struct EndLink {
    bool has = false;
    bool ambiguous = false;
    size_t neighbor = 0;
    bool neighbor_is_min = false;
  };
  std::vector<std::array<EndLink, 2>> link(n);

  auto record = [&](size_t idx, int end_idx, size_t other, int other_end_idx) {
    EndLink& l = link[idx][static_cast<size_t>(end_idx)];
    if (!l.has) {
      l.has = true;
      l.neighbor = other;
      l.neighbor_is_min = (other_end_idx == 0);
    } else if (l.neighbor != other) {
      l.ambiguous = true;
    }
  };

  // Same pairwise adjacency test FindAdjacentSteppedPairs() itself uses
  // (same axis line, one end of each genuinely touching in 3D, radii
  // differing) - but recorded into the per-end link table above instead
  // of immediately consuming both candidates into a single pair, so a
  // candidate with a valid link on BOTH of its own ends can still
  // continue a chain past it.
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      const CylindricalFaceCandidate& a = candidates[i];
      const CylindricalFaceCandidate& b = candidates[j];
      if (a.radius == b.radius) continue;  // same radius - not a step at all

      const double align = std::fabs(ON_DotProduct(a.axis_dir, b.axis_dir));
      if (align < 1.0 - tolerance::kAlignment) continue;
      const Vector3d to_b = b.axis_ref - a.axis_ref;
      const Vector3d off_axis = to_b - ON_DotProduct(to_b, a.axis_dir) * a.axis_dir;
      if (off_axis.Length() > kTol) continue;  // not the same axis line

      const Point3d a_lo = a.axis_ref + a.t_min * a.axis_dir;
      const Point3d a_hi = a.axis_ref + a.t_max * a.axis_dir;
      const Point3d b_lo = b.axis_ref + b.t_min * b.axis_dir;
      const Point3d b_hi = b.axis_ref + b.t_max * b.axis_dir;

      if (a_hi.DistanceTo(b_lo) <= kTol) {
        record(i, 1, j, 0);
        record(j, 0, i, 1);
      } else if (a_lo.DistanceTo(b_hi) <= kTol) {
        record(i, 0, j, 1);
        record(j, 1, i, 0);
      } else if (a_hi.DistanceTo(b_hi) <= kTol) {
        record(i, 1, j, 1);
        record(j, 1, i, 1);
      } else if (a_lo.DistanceTo(b_lo) <= kTol) {
        record(i, 0, j, 0);
        record(j, 0, i, 0);
      }
      // else: no shared endpoint at all - not adjacent, no link recorded.
    }
  }

  std::vector<SteppedChain> chains;
  std::vector<bool> visited(n, false);

  auto is_terminal = [&](size_t idx, int end_idx) {
    const EndLink& l = link[idx][static_cast<size_t>(end_idx)];
    return !l.has || l.ambiguous;
  };

  for (size_t i = 0; i < n; ++i) {
    if (visited[i]) continue;
    const bool min_terminal = is_terminal(i, 0);
    const bool max_terminal = is_terminal(i, 1);
    // A chain START is a candidate with EXACTLY one terminal end (the
    // other genuinely, unambiguously linked onward): an interior link of
    // some other chain has both ends linked (skipped here, reached later
    // by walking FROM its own chain's actual start instead); an entirely
    // isolated candidate has both ends terminal (a chain of 1, not a
    // multi-step chain at all - already RecognizeHoles()'s/
    // RecognizeBosses()'s own domain).
    if (min_terminal == max_terminal) continue;

    const int start_end = min_terminal ? 1 : 0;  // this candidate's own outgoing (linked) end
    std::vector<size_t> ordered;
    size_t cur = i;
    int incoming_end = -1;  // -1: `cur` is the chain's own first element, no incoming end yet
    while (!visited[cur]) {
      visited[cur] = true;
      ordered.push_back(cur);
      const int outgoing_end = (incoming_end == -1) ? start_end : (1 - incoming_end);
      const EndLink& l = link[cur][static_cast<size_t>(outgoing_end)];
      if (!l.has || l.ambiguous) break;  // chain ends here
      const size_t next = l.neighbor;
      const int next_incoming_end = l.neighbor_is_min ? 0 : 1;
      cur = next;
      incoming_end = next_incoming_end;
    }
    if (ordered.size() < 3) continue;  // exactly 1 or 2 segments: RecognizeHoles()'s/RecognizeBosses()'s/
                                        // RecognizeCounterboreHoles()'s/RecognizeSteppedBosses()'s own domain

    SteppedChain chain;
    chain.indices = std::move(ordered);
    // The first element's own OUTER end is whichever end is NOT the
    // outgoing one this walk started from.
    chain.first_outer_is_min = (start_end == 1);
    chains.push_back(std::move(chain));
  }

  return chains;
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

  for (const SteppedPair& pair : FindAdjacentSteppedPairs(candidates)) {
    // A counterbore's own recess is ALWAYS the shallower, entry-side
    // segment - unlike a stepped boss (RecognizeSteppedBosses() below),
    // there is no "narrow first" counterbore. FindAdjacentSteppedPairs()
    // itself makes no such assumption (`first`/`second` are assigned
    // purely by which end happens to touch, not by radius), so normalize
    // here: if `second` is actually the wider one, re-express the SAME
    // pair with `wide` at `origin` instead of silently rejecting it.
    const CylindricalFaceCandidate* wide = &candidates[pair.first_index];
    const CylindricalFaceCandidate* narrow = &candidates[pair.second_index];
    Point3d entry = pair.origin;
    Vector3d into_material = pair.axis;
    double wide_len = pair.first_length, narrow_len = pair.second_length;
    if (wide->radius < narrow->radius) {
      std::swap(wide, narrow);
      entry = pair.origin + pair.axis * (pair.first_length + pair.second_length);
      into_material = -pair.axis;
      std::swap(wide_len, narrow_len);
    }

    CylindricalFaceCandidate merged;
    merged.radius = wide->radius;
    merged.axis_ref = entry;
    merged.axis_dir = into_material;
    merged.t_min = 0.0;
    merged.t_max = wide_len + narrow_len;
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, merged);
    const bool near_open = !occ.near_inside;
    const bool far_open = !occ.far_inside;
    if (!near_open) continue;  // a counterbore's own entry must be open to the outside

    CounterboreFeature cf;
    cf.origin = entry;
    cf.axis = into_material;
    cf.counterbore_radius = wide->radius;
    cf.counterbore_depth = wide_len;
    cf.drill_radius = narrow->radius;
    cf.drill_depth = wide_len + narrow_len;
    cf.through = far_open;
    cf.counterbore_face_index = wide->face_index;
    cf.drill_face_index = narrow->face_index;
    out.push_back(cf);
  }

  return out;
}

namespace {

// The conical sibling of CylindricalFaceCandidate above, for
// RecognizeCountersinkHoles() below: one candidate full-cone face, before
// it's known whether its own narrower end actually touches an adjacent
// cylindrical bore. `apex`/`axis_dir` are oriented so `t` (dot(point -
// apex, axis_dir)) comes out POSITIVE and increasing away from the apex
// for every point of this patch - a real conical frustum patch never
// contains its own apex, so exactly one sign of axis_dir makes this true
// (the same "orient so both heights-from-apex are positive" idea
// ExtractConicalFace(), brep.cpp, already applies to exactly two points;
// here to an arbitrary sampled set). `radius_min`/`radius_max` are the
// true cone cross-section radius at `t_min`/`t_max` respectively
// (`radius_max` > `radius_min` always follows from `t_max` > `t_min` > 0
// for a genuine right circular cone), measured directly off the same
// sampled boundary points ScanFullCylinderFaces() itself reads its own
// axial extent from - deliberately NOT derived from ON_Cone's own
// `radius`/`height` fields, which (per ExtractConicalFace's own doc
// comment in brep.cpp) can carry an arbitrary reference scale unrelated to
// this specific trimmed patch's own two true ends.
struct ConeFaceCandidate {
  int face_index = -1;
  Point3d apex;
  Vector3d axis_dir;  // unit, oriented so t increases away from the apex
  double t_min = 0.0, t_max = 0.0;
  double radius_min = 0.0, radius_max = 0.0;
  bool concave = false;
};

std::vector<ConeFaceCandidate> ScanFullConeFaces(const Brep& solid) {
  const ON_Brep& brep = solid.raw();
  std::vector<ConeFaceCandidate> candidates;

  // Same tolerance scale ExtractConicalFace() (brep.cpp) and
  // ScanFullCylinderFaces() above both use for their own analytic-surface
  // fit checks.
  constexpr double kConeTol = 1e-4;

  for (int fi = 0; fi < brep.m_F.Count(); ++fi) {
    const ON_BrepFace& face = brep.m_F[fi];
    const ON_Surface* srf = face.SurfaceOf();
    if (!srf) continue;

    ON_Cone cone;
    if (!srf->IsCone(&cone, kConeTol)) continue;
    // A partial (less than 2*pi) cone sector - e.g. a fillet's own conical
    // rolling-ball wall - isn't itself a countersink, whatever solid it's
    // attached to; only a full sweep is a candidate.
    if (!srf->IsClosed(0)) continue;

    Vector3d axis_dir = cone.Axis();
    if (!axis_dir.Unitize()) continue;
    const Point3d apex = cone.ApexPoint();

    // Concavity: same midpoint-normal-vs-radial-direction test
    // ScanFullCylinderFaces() uses, generalized from a cylinder's fixed
    // radius to a cone's own on-axis closest point at the SAMPLED point's
    // own height - sign-invariant to whichever way axis_dir happens to
    // point, so this is safe to compute before the t_min/t_max
    // orientation fix below.
    const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
    const Point3d p_mid = srf->PointAt(du.Mid(), dv.Mid());
    Vector3d n_mid = srf->NormalAt(du.Mid(), dv.Mid());
    if (face.m_bRev) n_mid = -n_mid;
    const Point3d axis_at_mid = apex + ON_DotProduct(p_mid - apex, axis_dir) * axis_dir;
    Vector3d radial_out = p_mid - axis_at_mid;
    if (!radial_out.Unitize()) continue;  // p_mid landed exactly on the axis - degenerate
    const bool concave = ON_DotProduct(n_mid, radial_out) <= 0.0;

    // Same plain global min/max over every sampled loop/trim/edge point
    // ScanFullCylinderFaces() itself uses for its own axial extent -
    // immune to loop/trim adjacency and fragmentation for the identical
    // reason - but keeping the actual extremal POINT alongside each
    // extreme t, not just the t value, since a cone's own radius varies
    // along its axis (unlike a cylinder's constant radius, there is no
    // single shared "the radius" to fall back on).
    double raw_t_min = std::numeric_limits<double>::infinity();
    double raw_t_max = -std::numeric_limits<double>::infinity();
    Point3d p_at_raw_min, p_at_raw_max;
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
          const double t = ON_DotProduct(p - apex, axis_dir);
          if (t < raw_t_min) {
            raw_t_min = t;
            p_at_raw_min = p;
          }
          if (t > raw_t_max) {
            raw_t_max = t;
            p_at_raw_max = p;
          }
        }
      }
    }
    if (!(raw_t_max > raw_t_min)) continue;  // no real axial extent found - degenerate/unreadable face

    // Orient axis_dir so both true heights-from-apex come out positive - a
    // genuine conical frustum patch always sits entirely on ONE side of
    // its own apex, so exactly one sign works. Flipping axis_dir negates
    // every t, so the point that achieved the OLD raw_t_min becomes the
    // NEW t_max (and vice versa).
    ConeFaceCandidate c;
    c.face_index = fi;
    c.apex = apex;
    Point3d p_at_tmin, p_at_tmax;
    if (raw_t_min < 0.0) {
      c.axis_dir = -axis_dir;
      c.t_min = -raw_t_max;
      c.t_max = -raw_t_min;
      p_at_tmin = p_at_raw_max;
      p_at_tmax = p_at_raw_min;
    } else {
      c.axis_dir = axis_dir;
      c.t_min = raw_t_min;
      c.t_max = raw_t_max;
      p_at_tmin = p_at_raw_min;
      p_at_tmax = p_at_raw_max;
    }
    if (!(c.t_min > 0.0)) continue;  // patch would have to contain the apex itself - degenerate

    const Point3d axis_at_tmin = apex + c.t_min * c.axis_dir;
    const Point3d axis_at_tmax = apex + c.t_max * c.axis_dir;
    c.radius_min = (p_at_tmin - axis_at_tmin).Length();
    c.radius_max = (p_at_tmax - axis_at_tmax).Length();
    if (!(c.radius_max > c.radius_min)) continue;  // not a genuine frustum patch away from the apex

    c.concave = concave;
    candidates.push_back(c);
  }

  return candidates;
}

// One matched cone/cylinder pair - the conical sibling of SteppedPair
// above, for RecognizeCountersinkHoles() below. `open_point`/`open_radius`
// are the cone's own OTHER (non-touching) end - the countersink's own
// entry surface for a real countersink, where MakeCountersinkHole()'s own
// `center` parameter would sit; `far_point` is the matched cylinder's own
// OTHER (non-touching) end - the pilot bore's own far end.
struct ConeCylinderStep {
  size_t cone_index = 0;
  size_t cyl_index = 0;
  Point3d open_point;
  double open_radius = 0.0;
  Point3d far_point;
  Vector3d axis;  // unit, from open_point through the touch point to far_point
  double cone_length = 0.0;
  double cyl_length = 0.0;
};

// Pairs each cone candidate with at most one cylinder candidate, the
// conical/cylindrical sibling of FindAdjacentSteppedPairs() above - same
// axis-line + genuine-3D-touch test, generalized to check BOTH of the
// cone's own two ends against a candidate cylinder (not assumed to always
// be the narrower one - a stepped BOSS's own tapered tip can touch its
// cylinder at either end, see SteppedBossFeature's own doc comment for the
// cylinder/cylinder precedent) and requiring the touching end's own radius
// to actually match the cylinder's radius (a coincidental 3D touch at the
// wrong radius is not a genuine smooth transition, so it's rejected before
// the touch-point check even runs).
std::vector<ConeCylinderStep> FindAdjacentConeCylinderPairs(const std::vector<ConeFaceCandidate>& cones,
                                                             const std::vector<CylindricalFaceCandidate>& cyls) {
  std::vector<ConeCylinderStep> pairs;
  std::vector<bool> cyl_consumed(cyls.size(), false);
  const double kTol = tolerance::kDistance * 100.0;

  for (size_t ci = 0; ci < cones.size(); ++ci) {
    const ConeFaceCandidate& cone = cones[ci];
    const Point3d p_narrow = cone.apex + cone.t_min * cone.axis_dir;
    const Point3d p_wide = cone.apex + cone.t_max * cone.axis_dir;
    struct End {
      Point3d touch, open;
      double touch_radius, open_radius;
    };
    const End ends[2] = {
        {p_narrow, p_wide, cone.radius_min, cone.radius_max},
        {p_wide, p_narrow, cone.radius_max, cone.radius_min},
    };

    for (size_t cyi = 0; cyi < cyls.size(); ++cyi) {
      if (cyl_consumed[cyi]) continue;
      const CylindricalFaceCandidate& cyl = cyls[cyi];

      const double align = std::fabs(ON_DotProduct(cone.axis_dir, cyl.axis_dir));
      if (align < 1.0 - tolerance::kAlignment) continue;
      const Vector3d to_cyl = cyl.axis_ref - cone.apex;
      const Vector3d off_axis = to_cyl - ON_DotProduct(to_cyl, cone.axis_dir) * cone.axis_dir;
      if (off_axis.Length() > kTol) continue;  // not the same axis line

      const Point3d cyl_lo = cyl.axis_ref + cyl.t_min * cyl.axis_dir;
      const Point3d cyl_hi = cyl.axis_ref + cyl.t_max * cyl.axis_dir;

      bool matched = false;
      for (const End& end : ends) {
        if (std::fabs(end.touch_radius - cyl.radius) > std::max(kTol, cyl.radius * 1e-4)) continue;

        Point3d far_point;
        if (end.touch.DistanceTo(cyl_lo) <= kTol) {
          far_point = cyl_hi;
        } else if (end.touch.DistanceTo(cyl_hi) <= kTol) {
          far_point = cyl_lo;
        } else {
          continue;
        }

        Vector3d axis = end.touch - end.open;
        if (!axis.Unitize()) continue;
        const double cyl_length = cyl.t_max - cyl.t_min;
        // `axis` must actually continue straight on to `far_point` (a
        // genuine adjacent step, not an overlap or a fold-back) -
        // confirmed directly, the same check FindAdjacentSteppedPairs()
        // itself applies to its own two candidates.
        const Point3d predicted_far = end.touch + axis * cyl_length;
        if (predicted_far.DistanceTo(far_point) > kTol) continue;

        ConeCylinderStep step;
        step.cone_index = ci;
        step.cyl_index = cyi;
        step.open_point = end.open;
        step.open_radius = end.open_radius;
        step.far_point = far_point;
        step.axis = axis;
        step.cone_length = cone.t_max - cone.t_min;
        step.cyl_length = cyl_length;
        pairs.push_back(step);
        cyl_consumed[cyi] = true;
        matched = true;
        break;
      }
      if (matched) break;
    }
  }

  return pairs;
}

}  // namespace

std::vector<CountersinkFeature> RecognizeCountersinkHoles(const Brep& solid) {
  std::vector<CountersinkFeature> out;

  std::vector<ConeFaceCandidate> cones;
  for (ConeFaceCandidate& c : ScanFullConeFaces(solid)) {
    if (c.concave) cones.push_back(c);
  }
  std::vector<CylindricalFaceCandidate> cyls;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (c.concave) cyls.push_back(c);
  }
  if (cones.empty() || cyls.empty()) return out;

  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  for (const ConeCylinderStep& step : FindAdjacentConeCylinderPairs(cones, cyls)) {
    const ConeFaceCandidate& cone = cones[step.cone_index];
    const CylindricalFaceCandidate& cyl = cyls[step.cyl_index];

    // Same on-axis open/capped probe RecognizeCounterboreHoles() itself
    // uses, applied to the merged cone+cylinder span.
    CylindricalFaceCandidate merged;
    merged.radius = std::max(step.open_radius, cyl.radius);
    merged.axis_ref = step.open_point;
    merged.axis_dir = step.axis;
    merged.t_min = 0.0;
    merged.t_max = step.cone_length + step.cyl_length;
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, merged);
    const bool near_open = !occ.near_inside;
    const bool far_open = !occ.far_inside;
    if (!near_open) continue;  // a countersink's own entry must be open to the outside

    // Full included angle from the cone's own MEASURED slope (radius gap
    // over axial length), not any ON_Cone field - see ConeFaceCandidate's
    // own doc comment for why.
    const double half_angle = std::atan2(cone.radius_max - cone.radius_min, cone.t_max - cone.t_min);

    CountersinkFeature cf;
    cf.origin = step.open_point;
    cf.axis = step.axis;
    cf.countersink_diameter = 2.0 * step.open_radius;
    cf.countersink_angle_degrees = 2.0 * half_angle * 180.0 / ON_PI;
    cf.bore_radius = cyl.radius;
    cf.bore_depth = step.cone_length + step.cyl_length;
    cf.through = far_open;
    cf.countersink_face_index = cone.face_index;
    cf.bore_face_index = cyl.face_index;
    out.push_back(cf);
  }

  return out;
}

// Turns a walked SteppedChain (candidate indices in physical entry-to-far
// order, plus the one first_outer_is_min sign bit) into an absolute 3D
// origin point and unit axis direction pointing from that origin along
// the chain toward its own far end - shared by RecognizeSteppedHoleChains()
// and RecognizeSteppedBossChains() below, since both need exactly this
// same conversion before applying their own (opposite) open/attached
// probe semantics.
std::pair<Point3d, Vector3d> SteppedChainOriginAndAxis(const std::vector<CylindricalFaceCandidate>& candidates,
                                                        const SteppedChain& chain) {
  const CylindricalFaceCandidate& first = candidates[chain.indices.front()];
  if (chain.first_outer_is_min) {
    return {first.axis_ref + first.t_min * first.axis_dir, first.axis_dir};
  }
  return {first.axis_ref + first.t_max * first.axis_dir, -first.axis_dir};
}

std::vector<SteppedHoleChain> RecognizeSteppedHoleChains(const Brep& solid) {
  std::vector<SteppedHoleChain> out;

  std::vector<CylindricalFaceCandidate> candidates;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (c.concave) candidates.push_back(c);
  }
  if (candidates.size() < 3) return out;

  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  for (const SteppedChain& chain : FindSteppedChains(candidates)) {
    std::vector<double> radii, lengths;
    std::vector<int> face_indices;
    double total_length = 0.0;
    double max_radius = 0.0;
    for (size_t idx : chain.indices) {
      const CylindricalFaceCandidate& c = candidates[idx];
      radii.push_back(c.radius);
      lengths.push_back(c.t_max - c.t_min);
      face_indices.push_back(c.face_index);
      total_length += c.t_max - c.t_min;
      max_radius = std::max(max_radius, c.radius);
    }

    const auto [chain_origin, chain_axis] = SteppedChainOriginAndAxis(candidates, chain);

    // Same on-axis open/capped probe RecognizeHoles()/RecognizeCounterboreHoles()
    // themselves use, applied to the whole chain's own merged span.
    CylindricalFaceCandidate merged;
    merged.radius = max_radius;
    merged.axis_ref = chain_origin;
    merged.axis_dir = chain_axis;
    merged.t_min = 0.0;
    merged.t_max = total_length;
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, merged);
    const bool near_open = !occ.near_inside;
    const bool far_open = !occ.far_inside;
    if (!near_open && !far_open) continue;  // fully enclosed chain: not a hole feature

    SteppedHoleChain shc;
    shc.through = near_open && far_open;
    if (near_open) {
      // The chain's own walk order already starts at the open (entry)
      // end - keep it as-is.
      shc.origin = chain_origin;
      shc.axis = chain_axis;
      for (size_t k = 0; k < radii.size(); ++k) shc.steps.push_back({radii[k], lengths[k], face_indices[k]});
    } else {
      // Only the FAR end is open: reverse the whole chain so `origin`
      // sits at the open end instead, matching HoleFeature's/
      // CounterboreFeature's own "origin is the entry" convention.
      shc.origin = chain_origin + chain_axis * total_length;
      shc.axis = -chain_axis;
      for (size_t k = radii.size(); k-- > 0;) shc.steps.push_back({radii[k], lengths[k], face_indices[k]});
    }
    out.push_back(std::move(shc));
  }

  return out;
}

std::vector<SteppedBossFeature> RecognizeSteppedBosses(const Brep& solid) {
  std::vector<SteppedBossFeature> out;

  std::vector<CylindricalFaceCandidate> candidates;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (!c.concave) candidates.push_back(c);
  }
  if (candidates.size() < 2) return out;

  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  for (const SteppedPair& pair : FindAdjacentSteppedPairs(candidates)) {
    const CylindricalFaceCandidate& first = candidates[pair.first_index];
    const CylindricalFaceCandidate& second = candidates[pair.second_index];

    // Same two on-axis probes RecognizeBosses() uses on a single
    // candidate, applied here to the MERGED two-segment span -
    // `near_attached` reads `first`'s own outer end (pair.origin),
    // `far_attached` reads `second`'s own outer end. Unlike a
    // counterbore, a stepped boss has no fixed "wide is always first"
    // convention (see SteppedBossFeature's own doc comment), so this
    // reads BOTH orientations directly off which end is actually
    // attached, rather than assuming one from radius.
    CylindricalFaceCandidate merged;
    merged.radius = std::max(first.radius, second.radius);
    merged.axis_ref = pair.origin;
    merged.axis_dir = pair.axis;
    merged.t_min = 0.0;
    merged.t_max = pair.first_length + pair.second_length;
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, merged);
    const bool near_attached = occ.near_inside;
    const bool far_attached = occ.far_inside;
    if (near_attached && far_attached) continue;  // both ends embedded: not a visible feature

    SteppedBossFeature sf;
    if (!near_attached && !far_attached) {
      // Free on both ends - a free-standing stepped rod (see
      // SteppedBossFeature's own doc comment for why this is NOT a chain
      // embedded partway through a wall). Pin the origin at `first`'s own
      // outer end arbitrarily, mirroring BossFeature's own through case.
      sf.through = true;
      sf.origin = pair.origin;
      sf.axis = pair.axis;
      sf.base_radius = first.radius;
      sf.base_height = pair.first_length;
      sf.tip_radius = second.radius;
      sf.tip_height = pair.second_length;
      sf.base_face_index = first.face_index;
      sf.tip_face_index = second.face_index;
    } else if (far_attached) {
      // `second`'s own outer end is the base (e.g. a narrow post rising
      // from a wide pad at the free end) - axis points from there back
      // toward `first`'s own outer end, the tip.
      sf.through = false;
      sf.origin = pair.origin + pair.axis * (pair.first_length + pair.second_length);
      sf.axis = -pair.axis;
      sf.base_radius = second.radius;
      sf.base_height = pair.second_length;
      sf.tip_radius = first.radius;
      sf.tip_height = pair.first_length;
      sf.base_face_index = second.face_index;
      sf.tip_face_index = first.face_index;
    } else {
      // near_attached: `first`'s own outer end is the base - e.g. a wide
      // shoulder/flange at the base, narrower shaft continuing to the
      // free tip.
      sf.through = false;
      sf.origin = pair.origin;
      sf.axis = pair.axis;
      sf.base_radius = first.radius;
      sf.base_height = pair.first_length;
      sf.tip_radius = second.radius;
      sf.tip_height = pair.second_length;
      sf.base_face_index = first.face_index;
      sf.tip_face_index = second.face_index;
    }
    out.push_back(sf);
  }

  return out;
}

std::vector<SteppedBossChain> RecognizeSteppedBossChains(const Brep& solid) {
  std::vector<SteppedBossChain> out;

  std::vector<CylindricalFaceCandidate> candidates;
  for (CylindricalFaceCandidate& c : ScanFullCylinderFaces(solid)) {
    if (!c.concave) candidates.push_back(c);
  }
  if (candidates.size() < 3) return out;

  const Mesh solid_mesh = solid.TessellateToClosedMesh(16, 32);

  for (const SteppedChain& chain : FindSteppedChains(candidates)) {
    std::vector<double> radii, heights;
    std::vector<int> face_indices;
    double total_length = 0.0;
    double max_radius = 0.0;
    for (size_t idx : chain.indices) {
      const CylindricalFaceCandidate& c = candidates[idx];
      radii.push_back(c.radius);
      heights.push_back(c.t_max - c.t_min);
      face_indices.push_back(c.face_index);
      total_length += c.t_max - c.t_min;
      max_radius = std::max(max_radius, c.radius);
    }

    const auto [chain_origin, chain_axis] = SteppedChainOriginAndAxis(candidates, chain);

    // Same on-axis attached/free probe RecognizeBosses()/RecognizeSteppedBosses()
    // themselves use, applied to the whole chain's own merged span.
    CylindricalFaceCandidate merged;
    merged.radius = max_radius;
    merged.axis_ref = chain_origin;
    merged.axis_dir = chain_axis;
    merged.t_min = 0.0;
    merged.t_max = total_length;
    const EndOccupancy occ = ClassifyEndOccupancy(solid_mesh, merged);
    const bool near_attached = occ.near_inside;
    const bool far_attached = occ.far_inside;
    if (near_attached && far_attached) continue;  // fully embedded chain: not a visible feature

    SteppedBossChain sbc;
    if (!near_attached && !far_attached) {
      // Free on both ends - a free-standing multi-step rod. Pin the base
      // at the chain's own first outer end arbitrarily, mirroring
      // BossFeature's/SteppedBossFeature's own "through" convention.
      sbc.through = true;
      sbc.origin = chain_origin;
      sbc.axis = chain_axis;
      for (size_t k = 0; k < radii.size(); ++k) sbc.steps.push_back({radii[k], heights[k], face_indices[k]});
    } else if (near_attached) {
      // The chain's own walk order already starts at the attached (base)
      // end - keep it as-is.
      sbc.through = false;
      sbc.origin = chain_origin;
      sbc.axis = chain_axis;
      for (size_t k = 0; k < radii.size(); ++k) sbc.steps.push_back({radii[k], heights[k], face_indices[k]});
    } else {
      // Only the FAR end is attached: reverse the whole chain so `origin`
      // sits at the attached (base) end instead.
      sbc.through = false;
      sbc.origin = chain_origin + chain_axis * total_length;
      sbc.axis = -chain_axis;
      for (size_t k = radii.size(); k-- > 0;) sbc.steps.push_back({radii[k], heights[k], face_indices[k]});
    }
    out.push_back(std::move(sbc));
  }

  return out;
}

}  // namespace dino8::kernel
