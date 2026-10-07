// General surface/surface (SSX) and curve/surface (CSX) intersection.
//
// The public OpenNURBS SDK ships no SSX/CCX, so this is a mesh-seeded,
// Newton-polished intersector: both surfaces are tessellated (with the
// (u,v) of every vertex kept), the triangle/triangle intersection segments
// are chained into polylines, every polyline vertex is refined with a
// Gauss-Newton iteration on the exact surfaces until |S1 - S2| is within
// the requested tolerance, and a degree-3 NURBS is interpolated through
// the refined points (plus the 2D parameter-space curves on both
// surfaces, which share the 3D curve's parameterisation). A final pass
// checks the fitted curve between the samples against both surfaces and
// inserts more refined points where it strays past the tolerance.
//
// This works on ANY ON_Surface (plane, cylinder, sphere, cone, torus,
// freeform NURBS, ...) - it is not enumerated per surface-type-pair,
// which is the whole point of having it live in the kernel: it is the
// general building block a boundary-evaluation boolean engine needs, as
// opposed to boolean.cpp's closed-form per-pair special cases.
//
// Also here: the small numerical helpers the fillet family (and this
// file itself) are built on (damped Newton/Gauss-Newton on a residual,
// surface closest point, curve closest parameter, cubic interpolation).
//
// Ported (close to verbatim - only the namespace changed, nothing
// app-specific was ever in here) from dino8-app/src/geom/SurfaceIntersect.
// {h,cpp}, which is now a thin re-export of these functions so existing
// app callers (cmd_fillet.cpp, cmd_srfedit.cpp, BlendSurface.cpp) keep
// working unchanged.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"

class ON_Brep;
class ON_BrepFace;

namespace dino8::kernel {

struct IntersectOptions {
  double tolerance = 0.001;       // final |S1 - S2| per refined point
  double mesh_tolerance = 0.02;   // chord tolerance of the seed meshes
  int max_mesh_divisions = 160;   // per direction
  int min_mesh_divisions = 6;
};

// A surface tessellation that remembers the (u, v) of every vertex. `nu`/`nv`
// are the regular grid's own cell counts (nu * nv cells, 2 triangles each,
// in the exact construction order TessellateWithUV() below uses: cell
// (i, j)'s two triangles are tris[2*(j*nu+i)] and tris[2*(j*nu+i)+1]) -
// exposed so a caller can recover which grid cell a triangle came from
// without re-deriving nu/nv from pts.size() (which cannot be done
// uniquely in general, since (nu+1)*(nv+1) does not factor one way).
struct SurfaceMesh {
  std::vector<Point3d> pts;
  std::vector<ON_2dPoint> uv;
  std::vector<std::array<int, 3>> tris;
  ON_BoundingBox bbox;
  int nu = 0, nv = 0;
};
SurfaceMesh TessellateWithUV(const ON_Surface& s, const IntersectOptions& opt);

struct IntersectionCurve {
  std::vector<Point3d> points;         // refined points (on both surfaces)
  std::vector<ON_2dPoint> uv_a, uv_b;  // their parameters on A and B
  std::vector<double> params;          // chord-length parameters of `points`
  bool closed = false;
  ON_NurbsCurve curve;                 // degree-3 3D curve through `points`
  ON_NurbsCurve pcurve_a, pcurve_b;    // 2D curves on A and B, same parameterisation as `curve`
  double max_error = 0;                // largest |A - B| left after refinement
  double Length() const { return params.empty() ? 0 : params.back() - params.front(); }
};

// All intersection curves of two surfaces (untrimmed). Empty when they do
// not meet (or only touch tangentially within the mesh tolerance).
//
// Also handles the coincident/overlapping case, closing PARITY_MAP.md's own
// "SSX coincident / overlapping surface regions" bullet's remaining gap
// ("IntersectSurfaces still returns nothing for coincident surfaces"): when
// the ordinary mesh-seeded triangle-crossing search finds no crossing
// segment at all (a genuine non-meet, OR two surfaces that coincide over a
// real patch - coincident triangles never produce a clean crossing segment
// either), IntersectSurfacesOverlap() (below) is consulted to tell the two
// apart; a genuine coincident region's own bisection-tightened (u0,v0)-
// (u1,v1) rectangle is then walked and fit into one closed IntersectionCurve
// per region (uv_b found by closest-point projection onto `b`, not by the
// crossing relation this function otherwise solves) rather than silently
// reporting empty. This is the overlap REGION's own rectangular boundary,
// not an exact polygon of the coincident patch - see
// CoincidentOverlapBoundaryCurves()'s own doc comment, surface_intersect.cpp,
// for the full construction and disclosed scope. A genuine non-meet is
// unaffected and still returns empty.
//
// Two real, confirmed limitations, found while testing this rather than
// assumed away:
//
//  - "the ordinary crossing search finds nothing" is NOT a reliable
//    coincidence signal for two CURVED surfaces - confirmed on two different
//    fixtures, not just one: a cylinder wall `b` trimmed to a
//    strictly-interior sub-rectangle of a larger cylinder `a` (361 raw
//    crossing segments from a genuine grid-RESOLUTION mismatch alone -
//    TessellateWithUV()'s own divisions depend on each surface's own
//    domain/arc-length, so differently-domained operands tessellate at
//    different resolutions); AND, more surprisingly, even two FULL-EXTENT
//    walls of the IDENTICAL cylinder from two independent
//    `ON_Cylinder::GetNurbForm()` calls, which DO tessellate at matching
//    divisions (confirmed nu/nv-equal) - still produced 756 raw segments,
//    not zero: matching triangles between the two meshes are exactly
//    coincident, but ADJACENT triangles across one curved facet's own hinge
//    line are only approximately coplanar with each other (unlike a flat
//    surface, where every pair of adjacent facets stays exactly coplanar
//    regardless of tessellation grid), and that per-hinge approximation
//    error alone is enough seam noise to make `segs` nonzero. This
//    function's own coincident-region path is therefore reliable for FLAT/
//    coplanar surfaces of any two domains (confirmed by
//    `TestIntersectSurfacesReturnsBoundaryCurveForCoincidentRegion`'s own
//    two plane fixtures, tests/test_basic.cpp - a strictly-interior
//    sub-rectangle AND an entire-surface match), but NOT for a genuinely
//    curved coincident pair of any shape - disclosed here rather than
//    silently assumed closed.
//  - A pole/singular-point false positive, caught by this round's own
//    full-suite run and fixed before landing, not after: IntersectSurfacesOverlap()'s
//    own grid sampling right at a surface's coordinate pole (where an entire
//    row of (u, v) samples collapses onto nearly the same 3D point -
//    PARITY_MAP.md's own separate, still-`[partial]` "SSX across periodic
//    seams and at singular points (poles)" bullet) can report a small but
//    genuine multi-cell "region" there even for an ordinary tangent TOUCH
//    (e.g. a sphere resting on a plane at its own pole), not a real
//    coincident area - which this function very nearly misreported as one.
//    CoincidentOverlapBoundaryCurves() (surface_intersect.cpp) guards
//    against this directly: a candidate region's own 3D bounding-box
//    diagonal must be at least opt.mesh_tolerance before it is trusted,
//    extending IntersectSurfacesOverlap()'s own existing "a single isolated
//    cell is a transient touch" principle to a pole's multi-cell version of
//    the same trap. `TestFindSurfaceTangentContactsSphereOnPlane`'s own
//    pre-existing `IntersectSurfaces(...).empty()` assertion is the
//    regression guard for this (confirmed it still passes with the guard in
//    place), not duplicated in the new test.
std::vector<IntersectionCurve> IntersectSurfaces(const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt);

// Restricts SSX curves to the trimmed region of the faces (points whose
// (u,v) fall outside a face's trim loops are dropped; a curve is split
// where it leaves a face). `face_a`/`face_b` may be null (= untrimmed).
std::vector<IntersectionCurve> IntersectFaces(const ON_BrepFace* face_a, const ON_Surface& a, const ON_BrepFace* face_b, const ON_Surface& b, const IntersectOptions& opt);

// One SSX result between a specific face of `a` and a specific face of
// `b`, as returned by IntersectBreps() below.
struct BrepBrepIntersection {
  int face_a = -1;
  int face_b = -1;
  IntersectionCurve curve;
};

// B-rep/B-rep intersection as a public kernel API - the Brep-level
// counterpart to IntersectFaces() above, composing it over every
// bounding-box-overlapping face pair of `a` and `b` (the same pruning
// BooleanCombineGeneral()'s own face-pair loop uses, boolean_general.cpp).
// PARITY_MAP.md's own "kernel: Intersections & projections" evidence named
// this gap directly: "only face-level kernel entry points exist
// (IntersectFaces, IntersectCurveSurface); the app composes the B-rep loop
// itself (IntersectAny) ... there is no public Brep-level Intersect." This
// is that function - general to any two B-reps' faces (not enumerated per
// surface-type-pair, the same "general building block" scope this file's
// own top comment states), trimmed to each face's own trim loops exactly
// as IntersectFaces() already does, and returned un-stitched (one entry
// per face-pair-and-curve, `face_a`/`face_b` naming which faces produced
// it) - a caller needing one merged chain per physical intersection would
// stitch these the same way BooleanCombineGeneral()'s own StitchChains
// does, which is deliberately NOT duplicated here since this function's
// job is exposing the raw per-face-pair SSX results, not building a
// specific boolean engine's own topology.
std::vector<BrepBrepIntersection> IntersectBreps(const ON_Brep& a, const ON_Brep& b, const IntersectOptions& opt);

// One exact section curve between a Brep face and an infinite plane, as
// returned by IntersectBrepByPlane() below.
struct BrepPlaneIntersection {
  int face_index = -1;
  IntersectionCurve curve;
};

// Exact plane sections/contours of a whole B-rep - the Brep-level
// counterpart to IntersectFaces()/IntersectBreps() above, but against a
// caller-supplied infinite ON_Plane instead of a second B-rep.
// PARITY_MAP.md's own "Plane sections / contours of surfaces and B-reps"
// evidence named this gap directly: the app only ever slices its own
// render MESH (SliceObjects/SliceMesh); "Kernel SplitByPlane is mesh-only;
// the exact route (IntersectSurfaces per face) is not used for sections."
// This is that exact route: the plane is modeled as a bounded
// ON_PlaneSurface sized generously past the Brep's own bounding box (so no
// genuine crossing is ever missed at the plane's own edge - the same
// "don't let a caller-guessed rectangle silently clip a real result" issue
// IntersectCurvePlane()'s own doc comment raises for a curve), and
// IntersectFaces() is called once per face whose bounding box actually
// comes within tolerance of the plane, trimming the result to that face's
// own trim loops exactly as every other IntersectFaces() caller here
// already does. Returned un-stitched, one entry per face (a caller
// wanting one merged polyline per physical section contour composes these
// the same way a caller of IntersectBreps() would).
std::vector<BrepPlaneIntersection> IntersectBrepByPlane(const ON_Brep& b, const ON_Plane& plane, const IntersectOptions& opt);

struct CurveSurfaceHit {
  double t = 0;           // curve parameter
  ON_2dPoint uv;          // surface parameters
  Point3d point;
  double error = 0;       // |C(t) - S(u,v)| after refinement
};
std::vector<CurveSurfaceHit> IntersectCurveSurface(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt);

// One CSX hit between a curve and a specific (trimmed) face of a B-rep, as
// returned by IntersectCurveBrep() below.
struct CurveBrepHit {
  int face_index = -1;
  CurveSurfaceHit hit;
};

// Curve/B-rep intersection as a public kernel API - runs IntersectCurveSurface()
// against every face of `b` whose surface bounding box can plausibly meet
// `c`, then drops any hit whose (u, v) falls outside that face's own trim
// loops (FaceContainsUV(), the identical trim test IntersectFaces() already
// applies to SSX results). Closes the other half of the same PARITY_MAP.md
// gap IntersectBreps() above closes: "only face-level kernel entry points
// exist ... the app composes the B-rep loop itself (IntersectAny)."
std::vector<CurveBrepHit> IntersectCurveBrep(const ON_Curve& c, const ON_Brep& b, const IntersectOptions& opt);

// A curve/plane crossing - the curve-level counterpart to CurveSurfaceHit
// above, but against a caller-supplied INFINITE ON_Plane rather than a
// bounded ON_Surface. PARITY_MAP.md's own "Curve/plane intersection" gap:
// IntersectCurveSurface() can be handed a bounded ON_PlaneSurface, but "no
// dedicated infinite-plane API" existed (a caller had to first decide how
// big a rectangle to bound the plane with - and any curve point beyond
// that rectangle's edge is silently missed, a real correctness hazard an
// actually-infinite plane doesn't have). This solves the true implicit
// equation directly (signed distance to the plane), not a bounded-surface
// stand-in for it.
struct CurvePlaneHit {
  double t = 0;      // curve parameter
  Point3d point;
  double error = 0;  // |plane.DistanceTo(point)| after refinement
};
std::vector<CurvePlaneHit> IntersectCurvePlane(const ON_Curve& c, const ON_Plane& plane, const IntersectOptions& opt);

struct CurveCurveHit {
  double ta = 0;    // parameter on curve a
  double tb = 0;    // parameter on curve b
  Point3d point;    // Newton-refined point (the midpoint of A(ta) and B(tb), which
                     // coincide to within opt.tolerance once refinement succeeds)
  double error = 0; // |A(ta) - B(tb)| after refinement
};

// All points where two 3D curves meet within opt.tolerance (CCX - the
// curve/curve counterpart to IntersectSurfaces (SSX) and
// IntersectCurveSurface (CSX) above; the public OpenNURBS SDK has no
// curve/curve intersector either, the same gap those two fill). Two
// general space curves only meet at isolated points (never along a shared
// span, barring literal geometric coincidence - see the caveat in
// surface_intersect.cpp), so unlike SSX this returns points, not curves.
//
// Seeded the same way as IntersectCurveSurface: both curves are sampled
// into polylines at a resolution driven by opt.mesh_tolerance (not
// opt.max_mesh_divisions/min_mesh_divisions - those bound the *surface*
// mesher's adaptive refinement, which this doesn't use), every polyline
// segment pair whose padded bounding boxes overlap is checked with an
// exact closest-point-between-two-segments computation, and every
// close-approach pair seeds a Newton refinement (via NewtonSolve below)
// on (ta, tb) minimizing |A(ta) - B(tb)|. Refined hits within
// opt.tolerance * 4 of an already-accepted one are dropped as duplicates
// of the same crossing, the same dedup rule IntersectCurveSurface uses.
//
// Not intended for two curves that are coincident (or partially
// coincident) over a real span - that residual stays near zero along the
// whole overlap, and this seeding/dedup scheme reports whatever handful
// of isolated points its finite sampling happens to converge to, not the
// shared span itself.
std::vector<CurveCurveHit> IntersectCurves(const ON_Curve& a, const ON_Curve& b, const IntersectOptions& opt);

// A single curve's own self-intersections (a figure-eight-style crossing,
// or any other point where the curve genuinely passes through itself at
// two distinct parameters) - PARITY_MAP.md's own "Curve self-intersection"
// gap: "the kernel's own IntersectCurves(c, c) is still not usable for
// this (spurious self-hits on a plain line)" - IntersectCurves(c, c) is
// NOT what this delegates to (see its own "not intended for coincident
// curves" caveat just above: every parameter trivially equals itself,
// which is exactly the "coincident over a real span" case that caveat
// warns about, not a bug this function tries to route around). Instead,
// this is a dedicated self-intersection primitive: seeded the same
// segment-pair way as IntersectCurves(), but ONLY for sample-index pairs
// (i, j) separated by at least 2 segments (wrapping for a closed curve) -
// immediately-adjacent segments always meet at (or near) their shared
// sample point, which is the curve's own ordinary continuity, not a
// self-crossing, so they are never seeded at all rather than relying on
// post-hoc dedup to paper over a flood of trivial adjacent-segment hits.
// A genuine crossing still gets Newton-refined to full opt.tolerance
// precision, exactly like IntersectCurves(); a refined (ta, tb) pair that
// nonetheless converges back within one segment step of the diagonal
// (ta == tb) is discarded as the same "not a real crossing" case, a
// second, post-refinement instance of the same check (Newton is free to
// walk away from its own seed). A straight line, or any other
// non-self-intersecting curve, correctly returns empty - the concrete
// "usability" gap PARITY_MAP.md's own evidence names.
std::vector<CurveCurveHit> IntersectCurveSelfIntersections(const ON_Curve& c, const IntersectOptions& opt);

struct PullbackResult {
  ON_NurbsCurve pcurve;         // 2D curve in the surface's (u, v) parameter space
  ON_NurbsCurve pulled_curve;   // the literal 3D "Pull curve to surface" result: S(pcurve(t)), refit as its own 3D curve
  std::vector<ON_2dPoint> uv;   // the (u, v) found at each sample (same order as `t`)
  std::vector<double> t;        // the curve parameters (in c.Domain()) that were sampled
  std::vector<double> params;   // pcurve's/pulled_curve's own chord-length parameters (same order as `uv`/`t`)
  double max_error = 0;         // largest |S(u, v) - C(t)| at any SAMPLED point
  bool on_surface = false;      // true iff max_error <= opt.tolerance
};

// General-purpose pullback AND pull: projects an ARBITRARY 3D curve (on,
// near, or genuinely far from a surface) onto that surface, returning BOTH
// a 2D pcurve in the surface's own (u, v) parameter space and the literal
// 3D "pull curve to surface" result (Rhino's Pull command semantics -
// `pulled_curve`, the pcurve mapped back through the surface as its own 3D
// curve). This is the general-purpose sibling of two things the kernel
// previously only did as a by-product of something else: the pullback
// ReplaceEdgeCurve()/SplitNakedEdgeAt() already do internally (brep.cpp) as
// part of a topology edit (2D, but only reachable from inside those edits),
// and the plain closest-point projection SurfaceClosestPoint/Global already
// do per POINT (3D, but with no curve-level call to drive it across a whole
// input curve and refit the result as one curve).
//
// The curve is sampled at a resolution driven by opt.mesh_tolerance (same
// formula as IntersectCurveSurface), each sample is closest-point-projected
// onto the surface (SurfaceClosestPointGlobal for the first sample, then
// SurfaceClosestPoint seeded from the PREVIOUS sample's (u, v) for
// continuity - re-seeded globally whenever the local Newton polish fails or
// lands implausibly far from the sample point, so a warm seed that has
// wandered off a disconnected sheet or across a awkward periodic seam
// self-corrects rather than silently drifting), and the resulting (u, v)
// samples are fit with InterpolateCubic(..., dim=2) for `pcurve` and the
// matching S(u, v) samples are separately fit with InterpolateCubic(...,
// dim=3) for `pulled_curve` - the exact same cubic-fit call
// IntersectSurfaces() itself uses to build pcurve_a/pcurve_b (and, for
// `pulled_curve`, its 3D `curve` field).
//
// Honesty notes (read before trusting the result):
//  - max_error is the worst per-SAMPLE closest-point residual; unlike
//    IntersectSurfaces()'s own post-fit pass, neither fitted curve's
//    deviation from the surface/input BETWEEN samples is independently
//    re-checked or refined with inserted points - a caller who needs a
//    tighter guarantee on a highly-curved input should tighten
//    opt.mesh_tolerance (more samples), not rely on this call to notice
//    and self-correct.
//  - `pulled_curve` (the literal "Pull to surface" result) is meaningful
//    for ANY input, on-surface or not - that is the definition of a Pull
//    operation, and on_surface need not be true to trust it. `pcurve` and
//    `on_surface`, by contrast, are about PARAMETER-SPACE correspondence:
//    when the curve does NOT actually lie on the surface within tolerance
//    (on_surface == false), `pcurve` is still returned (it is whatever
//    curve interpolates the raw closest-point (u, v) projections), but
//    treating it as a meaningful pullback of THIS curve's own shape is not
//    warranted - it is closest-point noise reparametrized, not a pullback.
//    Callers MUST check on_surface before trusting `pcurve` as a shape-
//    preserving parametrization; this call does not throw or return an
//    empty curve for an off-surface input, since "far from the surface"
//    has no single correct threshold this general-purpose call can assume
//    for every caller.
//  - Unlike IntersectSurfaces()'s own pcurve_a/pcurve_b, a raw (u, v)
//    sample sequence that crosses a CLOSED surface direction's seam IS
//    unwrapped before `pcurve` is fit: whenever two consecutive samples in
//    a closed direction (IsClosed(dir)) differ by more than half that
//    direction's domain length, the later one is shifted by a whole period
//    first, so the fit keeps moving the way it was already moving instead
//    of swinging through the domain's middle. `pcurve`'s control points
//    can therefore legitimately fall outside the surface's own nominal
//    domain for a seam-crossing stretch, exactly like a seam-crossing trim
//    pcurve elsewhere in OpenNURBS-based kernels - but, unless the
//    direction is also genuinely IsPeriodic() (a strictly stronger
//    condition than IsClosed() - see NurbsSurface::IsPeriodic()'s own doc
//    comment, surface.h), evaluating the surface DIRECTLY at such an
//    out-of-range parameter is not guaranteed to reproduce the in-domain
//    point (a merely-closed, clamped-knot surface - e.g. the standard NURBS
//    form of a plain ON_Cylinder - has matching end curves but no periodic
//    knot structure past either end). A caller mapping `pcurve` back
//    through S(u, v) near a seam-crossing stretch MUST first reduce that
//    coordinate into the surface's own [Domain(dir).Min(),
//    Domain(dir).Max()] by its domain length (always valid for a closed
//    direction, periodic or not, since IsClosed() is exactly the guarantee
//    that both ends already evaluate to the same point) rather than
//    evaluate the raw out-of-range value directly. `uv` (the raw
//    per-sample field) is unaffected by any of this - it stays wrapped to
//    the domain exactly as found, as before.
PullbackResult PullbackCurveToSurface(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt);

// One directional ray/surface projection result, as returned by
// ProjectPointToSurface() below - the point-level sibling of
// ProjectedCurveResult, closing the "points" half of PARITY_MAP.md's own
// "Projection of curves/points onto surfaces along a direction (Project)"
// bullet (ProjectCurveToSurface() below closes the "curves" half).
struct PointProjectionHit {
  bool hit = false;
  double t = 0;    // ray parameter: point + t*direction == this->point (meaningful only if hit)
  ON_2dPoint uv;
  Point3d point;
};

// Directional projection of a single point onto a surface along
// `direction` - Rhino's Project command semantics for a point input. Not a
// closest-point search (see SurfaceClosestPoint/SurfaceClosestPointGlobal
// above for that): `point` moves along the fixed `direction` until it
// meets the surface, the same ray/surface system ProjectCurveToSurface()
// below solves per curve sample, exposed here as its own public,
// single-point entry point rather than requiring a caller to wrap one
// point in a degenerate curve. `direction` of zero length, or a ray that
// genuinely never meets the surface, both return `hit == false` rather
// than throwing or fabricating a point - the same honest-miss semantics
// ProjectCurveToSurface() applies per sample.
PointProjectionHit ProjectPointToSurface(Point3d point, const Vector3d& direction, const ON_Surface& s, const IntersectOptions& opt);

struct ProjectedCurveResult {
  std::vector<Point3d> points;  // projected 3D points, one per sample that hit the surface
  std::vector<ON_2dPoint> uv;   // their (u, v) on the surface, same order as `points`
  std::vector<double> t;        // the curve parameter each entry in `points` came from
  std::vector<bool> hit;        // per-SAMPLE success, in uniform sample order (see ProjectCurveToSurface)
  int sample_count = 0;         // total samples attempted
  int hit_count = 0;            // points.size() == uv.size() == t.size()
  ON_NurbsCurve projected_curve;  // degree-3 curve refit through `points` (empty curve if hit_count < 2)
};

// Directional projection of a curve onto a surface (Rhino's Project
// command semantics: every point moves along a fixed `direction`, unlike
// PullbackCurveToSurface()/Pull above, which moves each point to its own
// nearest point on the surface instead). PARITY_MAP.md's own "Projection
// of curves/points onto surfaces along a direction" evidence named this
// gap directly: "app ProjectCommand samples the curve and ray-casts along
// the CPlane normal onto the render mesh, then refits. No kernel project
// API." This is that kernel API, against the exact surface rather than a
// tessellated stand-in: the curve is sampled the same way
// PullbackCurveToSurface() is (opt.mesh_tolerance-driven, continuity-
// seeded from the previous sample), each sample solves the 3-unknown
// system `sample + t*direction == S(u, v)` by Newton iteration (seeded
// from the previous sample's (u, v) when available, or a global grid scan
// minimizing perpendicular distance from the ray to the surface
// otherwise - the same warm/global-reseed discipline
// PullbackCurveToSurface() applies to its own closest-point search), and
// the resulting 3D points are refit as `projected_curve` via
// InterpolateCubic(..., dim=3).
//
// A `direction` of zero length returns an empty result outright (there is
// no ray to cast). Unlike Pull, a ray cast along an arbitrary direction
// can genuinely miss the surface for some samples (the surface simply
// isn't there in that direction) - those samples are recorded as
// `hit[i] == false` and dropped from `points`/`uv`/`t`/the refit curve
// rather than either aborting the whole call or inventing a point; the
// common "every sample hits" case is `hit_count == sample_count`.
// `projected_curve` is left as a default-constructed (empty) ON_NurbsCurve
// when fewer than 2 samples hit - not enough to fit a curve through.
ProjectedCurveResult ProjectCurveToSurface(const ON_Curve& c, const ON_Surface& s, const Vector3d& direction, const IntersectOptions& opt);

// A span of the curve's own parameter domain that lies ON the surface
// (every sample in [t0, t1] within opt.tolerance of the surface), as
// opposed to the isolated crossing POINTS IntersectCurveSurface() above
// reports. PARITY_MAP.md's own "CSX against trimmed faces and
// curve-on-surface overlap (coincident) detection" bullet named this
// directly: "No overlap detection anywhere." - IntersectCurveSurface()'s
// mesh-seeded triangle crossing search is the wrong tool for this (a
// curve lying IN a surface, not crossing through it, produces no clean
// triangle piercing at all, the same "not intended for coincident curves"
// situation IntersectCurves() already discloses for two coincident 3D
// curves) - so this is a dedicated sampling-based overlap finder instead.
struct CurveSurfaceOverlap {
  double t0 = 0, t1 = 0;   // the curve parameter sub-interval that lies on the surface
  bool entire_curve = false;  // true iff t0/t1 cover the curve's whole domain
};

// Samples `c` at the same opt.mesh_tolerance-driven resolution
// PullbackCurveToSurface() uses, closest-point-projects each sample onto
// `s` (continuity-seeded from the previous sample, globally re-seeded on
// failure or an implausible jump - identical discipline to
// PullbackCurveToSurface()), and reports every maximal run of consecutive
// samples whose closest-point distance is within opt.tolerance as one
// overlap span. A curve nowhere near the surface returns empty; a curve
// lying entirely on the surface returns one span with `entire_curve ==
// true`. Each span's own t0/t1 boundary (where one exists inside the
// curve's own domain, i.e. not already a domain endpoint) is then
// bisection-refined against the identical on/off-surface predicate the
// sampling loop uses, the same way IntersectCurveSurface() Newton-refines
// a seed - narrowing the previously sampling-resolution-only boundary down
// to within double-precision of the true crossing.
std::vector<CurveSurfaceOverlap> IntersectCurveSurfaceOverlap(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt);

// One overlap span between a curve and a specific (trimmed) face of a
// B-rep, as returned by IntersectCurveBrepOverlap() below - the
// coincident-region counterpart to CurveBrepHit above, the same way
// CurveSurfaceOverlap is the coincident-region counterpart to
// CurveSurfaceHit.
struct CurveBrepOverlap {
  int face_index = -1;
  CurveSurfaceOverlap overlap;
};

// Curve/B-rep coincident-region detection - the curve/B-rep counterpart to
// IntersectCurveBrep() above, but for a curve lying ON a face over a real
// span rather than crossing through it. PARITY_MAP.md's own "CSX against
// trimmed faces and curve-on-surface overlap (coincident) detection"
// bullet named this directly as still missing even after
// IntersectCurveSurfaceOverlap() closed the plain curve/surface case:
// "this is curve/surface only (no curve/B-rep ... counterpart)". Runs the
// same per-face bounding-box-pruned loop IntersectCurveBrep() already
// uses, but calls IntersectCurveSurfaceOverlap() against each face's own
// (untrimmed) surface with every on-surface sample ALSO required to pass
// FaceContainsUV() - so a span that runs off one face's trim boundary and
// onto a neighbour's is correctly reported as two separate (face_index,
// overlap) entries, not one that silently ignores the trim.
std::vector<CurveBrepOverlap> IntersectCurveBrepOverlap(const ON_Curve& c, const ON_Brep& b, const IntersectOptions& opt);

// One parallel plane section of a whole B-rep, as returned by
// ContourBrep() below - `offset` is the signed distance from
// `base_plane`, along `base_plane`'s own normal, that this section's
// plane sits at (so `offset == 0` is `base_plane` itself).
struct BrepContourSection {
  double offset = 0;
  std::vector<BrepPlaneIntersection> hits;
};

// A family of parallel plane sections at even intervals - the "Contour"
// half of PARITY_MAP.md's own "Plane sections / contours of surfaces and
// B-reps (Section, Contour, ClippingSections)" bullet, which named this
// directly as still "entirely unaddressed" even after IntersectBrepByPlane()
// closed the single-plane "Section" half. Builds directly on
// IntersectBrepByPlane(): every plane parallel to `base_plane`, stepped
// along `base_plane`'s own normal by a multiple of `spacing`, that could
// plausibly meet the B-rep's own bounding box is sectioned, covering the
// whole B-rep automatically (Rhino's own Contour semantics - the caller
// picks a base plane and a spacing, not a station count). A section whose
// plane produces zero hits (e.g. it only grazes the bounding box, not the
// actual solid) is dropped rather than returned empty. `spacing <= 0` or
// an invalid `base_plane` returns empty outright. This is parallel sections
// of ONE object along ONE fixed direction (`base_plane`'s own normal) -
// "ClippingSections" (multiple live, named, arbitrarily-oriented clip
// planes) is the separate SectionBrepByPlanes() below.
std::vector<BrepContourSection> ContourBrep(const ON_Brep& b, const ON_Plane& base_plane, double spacing, const IntersectOptions& opt);

// One named clip-plane section, as returned by SectionBrepByPlanes() below -
// `plane_index` is this section's position in the caller-supplied `planes`
// list (so a caller can tell which of several independent, differently-
// oriented clip planes produced it, the same way BrepContourSection::offset
// identifies which parallel station ContourBrep() produced one from).
struct BrepMultiPlaneSection {
  int plane_index = -1;
  std::vector<BrepPlaneIntersection> hits;
};

// Multiple independent, arbitrarily-oriented clip-plane sections of a whole
// B-rep in one call - the "ClippingSections" half of PARITY_MAP.md's own
// "Plane sections / contours of surfaces and B-reps (Section, Contour,
// ClippingSections)" bullet, the one gap left after IntersectBrepByPlane()
// closed "Section" (one plane) and ContourBrep() closed "Contour" (a family
// of PARALLEL planes at even spacing). Rhino's own ClippingPlane objects are
// not parallel siblings of one base plane - each is independently placed and
// oriented by the user - so this takes a plain list of planes instead of a
// base plane + spacing: each entry is run through IntersectBrepByPlane()
// completely independently (no shared bounding-box precomputation across
// planes, since two clip planes need not even be close to each other), and a
// plane producing zero hits is dropped rather than returned empty, the same
// convention ContourBrep() already uses. `plane_index` records the entry's
// own position in `planes` (not a compacted output index), so a caller
// matching sections back to named clip-plane objects does not have to
// re-derive which input plane produced which output. An empty `planes` list
// returns empty outright; an individual invalid plane is simply skipped
// (IntersectBrepByPlane()'s own `!plane.IsValid()` guard already returns no
// hits for it) rather than failing the whole call. The returned hatching
// Rhino's own ClippingSections draws is an app-level display concern, not a
// kernel geometry one, and stays out of scope here - this returns exact
// section CURVES only, the same honest curves-not-fills scope
// IntersectBrepByPlane()/ContourBrep() already have.
std::vector<BrepMultiPlaneSection> SectionBrepByPlanes(const ON_Brep& b, const std::vector<ON_Plane>& planes, const IntersectOptions& opt);

// Face-vs-face crossing test WITHIN a single B-rep - not through shared
// topology (that is Brep::Check()'s own SelfIntersectingLoop/
// SelfIntersectingLoop3d job, brep.h), but two faces of the same B-rep
// that do not share an edge yet still physically cross each other in
// space. PARITY_MAP.md's own "Surface / B-rep self-intersection
// detection" bullet named this directly as one of two remaining gaps:
// "no face-interior self-intersection test and no face-vs-face crossing
// test within a B-rep." This is that face-vs-face test: every pair of
// faces NOT already sharing an edge (ordinary adjacency, Check()'s own
// job, not this function's) is run through IntersectFaces() - the exact
// same per-face-pair SSX IntersectBreps() already composes across two
// SEPARATE B-reps, composed here across one B-rep's own faces instead.
// Edge-index adjacency alone cannot be trusted here: several of this
// kernel's own face-construction paths (Box()/FromUntrimmedQuadFaces(),
// per their own doc comments) deliberately build adjacent faces with NO
// shared ON_BrepEdge topology at all even though they genuinely touch in
// 3D - so a per-pair result is additionally checked against both faces'
// own boundary loops (sampled 3D polylines through their trims): a curve
// that lies entirely on BOTH faces' own boundary at once is the ordinary
// seam where two faces border each other, not one face's material
// cutting through the other's, and is NOT reported; only a curve that
// leaves at least one face's boundary (running through that face's
// interior) is a genuine crossing. Returned un-stitched, one entry per
// crossing face pair, the same shape IntersectBreps() already uses.
// This only catches a crossing where it already reaches another face's own
// trimmed region - "no face-interior self-intersection test" (a single face
// folding back onto itself) is the OTHER half of the same bullet's own
// prior evidence, closed separately by FindFaceInteriorSelfIntersections()
// below (point detections rather than curves - see that function's own doc
// comment for why).
std::vector<BrepBrepIntersection> FindBrepSelfIntersections(const ON_Brep& b, const IntersectOptions& opt);

// A single point where a face's own interior crosses itself - the "no
// face-interior self-intersection test" half of PARITY_MAP.md's own
// "Surface / B-rep self-intersection detection" bullet that
// FindBrepSelfIntersections() above deliberately leaves unaddressed (that
// function only catches a crossing that already reaches ANOTHER face's own
// trimmed region; this is the question of one face folding back onto
// itself, entirely within its own domain). `Brep::Check()`'s own
// SelfIntersectingLoop/SelfIntersectingLoop3d (brep.h) only ever examine a
// face's own BOUNDARY; this examines a face's own INTERIOR.
//
// Built on the exact same regular-grid tessellation (TessellateWithUV())
// and triangle/triangle crossing test (TriTri, surface_intersect.cpp) every
// other function in this file already uses, applied to ONE surface's own
// mesh instead of two different surfaces' - but, unlike those, this cannot
// simply reuse IntersectSurfaces(s, s, opt) directly: every point of a
// surface trivially coincides with itself under its OWN parametrization
// (ua == ub, va == vb is always a valid root of the exact same "Sa == Sb"
// system IntersectSurfaces() solves), the identical "not usable for this"
// trap PARITY_MAP.md's own "Curve self-intersection" bullet already named
// for IntersectCurves(c, c) one dimension down - a naive self-SSX would
// report nearly the entire surface as "self-intersecting" against itself.
// The fix is this file's own established answer to that exact trap, run
// one dimension up: IntersectCurveSelfIntersections() seeds ONLY sample
// INDEX pairs separated by at least 2 segments, skipping a curve's own
// ordinary local continuity rather than relying on post-hoc dedup to paper
// over a flood of trivial adjacent-sample hits; this seeds ONLY triangle
// pairs whose own tessellation GRID CELLS (SurfaceMesh::nu/nv above) are
// separated by at least 2 cells in Chebyshev distance - the two-dimensional
// analogue of "at least 2 segments apart" - skipping a smoothly-varying
// patch's own ordinary local neighbourhood (always close together in 3D
// too, not a self-crossing) while still finding a genuine fold-back between
// two topologically distant regions of the domain that happen to cross in
// space. Each surviving candidate triangle pair's own TriTri crossing
// segment seeds a Newton refinement of the true "Sa == Sb" system
// (RefineSurfaceSurfacePoint(), the same one IntersectSurfaces() itself
// uses, called with the SAME surface passed as both `a` and `b` - purely
// mechanical, since that function only ever calls .PointAt() on each
// argument independently) from the seed's own two DISTINCT (u, v)
// candidates; a converged result that nonetheless lands back within
// tolerance of the trivial ua==ub/va==vb diagonal (Newton is free to walk
// away from its own seed, exactly the same caveat
// IntersectCurveSelfIntersections() discloses) is discarded as that trap,
// not a real second preimage.
//
// Reported as isolated POINTS, not stitched into curves, unlike every
// other SSX-shaped function in this file: a continuous self-intersection
// locus (e.g. two parallel strips of the same face folded flat against
// each other along a whole shared line) would need the same seam/chain
// machinery IntersectSurfaces() applies to two DIFFERENT surfaces, which a
// single surface's own trivial-diagonal trap above complicates enough
// (every chain step risks reseeding back onto the diagonal) that this
// deliberately stays at the same honestly-scoped POINT level
// FindSurfaceTangentContacts() above already uses for an analogous reason;
// a genuinely continuous self-intersection line is reported as however
// many isolated points its finite grid of triangle pairs happens to
// converge to along it, not as one connected curve.
struct FaceInteriorSelfIntersection {
  int face_index = -1;
  Point3d point;    // the shared (refined, coincident-to-tolerance) 3D point
  ON_2dPoint uv_a;  // one (u, v) preimage of `point` on the face's surface
  ON_2dPoint uv_b;  // the other, DISTINCT (u, v) preimage of the same point
  double gap = 0;   // |S(uv_a) - S(uv_b)| after refinement
};
std::vector<FaceInteriorSelfIntersection> FindFaceInteriorSelfIntersections(const ON_Brep& b, const IntersectOptions& opt);

// A single point where two surfaces touch WITHOUT crossing - PARITY_MAP.md's
// own "SSX tangent / grazing contact (surfaces touching along a point or
// curve)" bullet, named as entirely `[missing]`: "There is no SSX tangency
// capability to give partial credit for". IntersectSurfaces() is the wrong
// tool for this on purpose - its mesh-seeded triangle/triangle crossing
// search only ever finds a SEGMENT where two triangles actually cross, and a
// pure tangential touch (a sphere resting on a plane, two cylinders touching
// along one ruling line) produces no such crossing at all, however fine the
// seed mesh. This is a dedicated closest-approach search instead: both
// surfaces are sampled on a coarse grid, every grid point of `a` is paired
// with its nearest grid point of `b`, and every pair close enough to
// plausibly be a real touch seeds a Newton solve for a genuine stationary
// point of the squared gap between the surfaces (RefineClosestApproach,
// surface_intersect.cpp - the 4-equation "both gradients vanish" system, not
// RefineSurfaceSurfacePoint()'s 3-equation "coincident point" system that
// IntersectSurfaces() itself uses, which is deliberately under-determined
// along a shared crossing curve and would not distinguish a tangent touch
// from an ordinary point on one). A stationary point with zero gap is not
// automatically a tangent touch, though - a nonnegative function that
// reaches zero has a zero gradient there regardless of whether the
// surfaces merely touch or genuinely cross, so an ordinary transversal
// crossing point satisfies the same 4-equation system too. What actually
// distinguishes the two is the surfaces' own tangent planes at that point:
// coincident (parallel or antiparallel normals) for a tangent touch, at a
// real angle for a transversal crossing - a purely local, sampling-
// independent test (checked from each surface's own normal at the converged
// parameters), deliberately NOT a proximity check against
// IntersectSurfaces()'s own sampled crossing-curve points (that approach
// was tried and is unreliable in both directions: a crossing curve's own
// mesh-driven sample spacing can leave a genuine crossing point farther
// from its nearest recorded sample than a reasonable match tolerance,
// wrongly keeping it; and a real tangent touch could coincidentally fall
// within that same tolerance of an unrelated recorded sample, wrongly
// dropping it). The normal at each side is taken via a nudge-off-the-exact-
// point search (RobustSurfaceNormal, surface_intersect.cpp), not a plain
// cross product of the raw partial derivatives at (ua, va)/(ub, vb) - a
// contact landing exactly on a surface's own coordinate pole (an entire
// row/column of control points collapsed to one point, where d/du or d/dv
// vanishes even though the surface itself has a perfectly well-defined
// limiting tangent plane there) is still classified correctly, which
// matters here more than it might elsewhere: the single most natural touch
// this function exists to find - a sphere resting on a plane directly below
// its own center - lands exactly on that sphere's own south pole under
// ON_Sphere's default parametrization (RobustSurfaceNormal's own nudge
// resolves exactly that case). A contact where even the nudged normal
// degenerates on either side - a genuinely malformed surface there, not
// merely a pole - cannot be classified this way and is skipped rather than
// guessed. Still honestly partial: this finds isolated POINT contacts only -
// the bullet's own "or curve" half (two surfaces tangent along a whole
// shared curve, e.g. two cylinders of equal radius touching along one
// ruling line) is not detected as a curve, only (if at all) as however many
// isolated points the coarse seed grid happens to converge to along it; a
// surface pair with more than one genuinely separate point of tangential
// contact is likewise not guaranteed to find every one of them (only every
// contact whose coarse grid seed converges to it).
struct SurfaceTangentContact {
  ON_2dPoint uv_a;  // parameters on `a` at the contact
  ON_2dPoint uv_b;  // parameters on `b` at the contact
  Point3d point;    // the (refined, shared-to-tolerance) contact point
  double gap = 0;   // |a(uv_a) - b(uv_b)| after refinement
};
std::vector<SurfaceTangentContact> FindSurfaceTangentContacts(const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt);

// A connected region of `a`'s own (u, v) domain, reported in that domain's
// own axis-aligned bounding box, whose points all lie ON `b` within
// tolerance - PARITY_MAP.md's own "SSX coincident / overlapping surface
// regions" bullet, as it stood when this function was written: "IntersectSurfaces
// still returns nothing for coincident surfaces. The only coincidence
// handling is inside planar booleans." (IntersectSurfaces() itself now DOES
// return a real boundary curve for a coincident region, built directly on
// top of this function - see its own doc comment above for that later
// addition.) IntersectSurfaces()'s own ORDINARY crossing-chain search is the wrong tool for a coincident region for the same
// reason IntersectCurveSurfaceOverlap() already exists instead of reusing
// IntersectCurveSurface(): two surfaces that coincide over a real patch
// produce no clean triangle-pair CROSSING there at all (the triangles lie
// in, not athwart, each other), so the mesh-seeded chainer finds nothing to
// chain. This applies that same sampling-overlap idea one dimension up: `a`
// is sampled on a grid sized the same way IntersectCurveSurfaceOverlap()
// sizes its own 1D sampling (opt.mesh_tolerance-driven), each sample is
// closest-point-projected onto `b` (row-continuity-seeded, globally
// re-seeded on failure - identical discipline to
// IntersectCurveSurfaceOverlap()'s own per-sample projection), and every
// maximal 4-connected run of on-`b` grid cells becomes one region, reported
// as that run's own axis-aligned (u, v) bounding box in `a`'s domain - NOT
// an exact boundary polygon (a genuinely concave or multi-lobe coincident
// patch is still only ever reported as its enclosing rectangle). Each of
// the box's own four extents IS bisection-tightened, though, the same
// RefineBoundary() idea IntersectCurveSurfaceOverlap() uses one dimension
// down: 4-connectivity guarantees a genuine off-`b` neighbour just past
// each extent along its own axis (if that neighbour were on-`b` too, it
// would already be 4-connected into this very region, and the extent would
// already have moved past it), so each of u0/u1/v0/v1 bisects against that
// neighbour, along a representative transect at the grid line where the
// extent was reached, down to machine precision rather than stopping at
// the grid's own sampling pitch. `entire_surface` is true when every
// sampled grid point across `a`'s WHOLE domain lies on `b` (the two surfaces
// coincide everywhere `a` is defined, not just within this region's own
// bounding box). A single isolated on-`b` grid cell with no on-`b` neighbour
// is dropped as a transient touch - the same "not an overlap" treatment
// IntersectCurveSurfaceOverlap() already gives an isolated on-surface
// sample - and is left for FindSurfaceTangentContacts() above to report
// instead.
struct SurfaceOverlapRegion {
  double u0 = 0, u1 = 0;  // bounding sub-interval of a.Domain(0) covered by this region
  double v0 = 0, v1 = 0;  // bounding sub-interval of a.Domain(1) covered by this region
  bool entire_surface = false;
};
std::vector<SurfaceOverlapRegion> IntersectSurfacesOverlap(const ON_Surface& a, const ON_Surface& b, const IntersectOptions& opt);

// The exact closed-form plane/sphere SSX - PARITY_MAP.md's own "Analytic/
// analytic SSX closed forms (plane/plane, plane/cylinder, cylinder/cylinder,
// plane/sphere, cone, torus)" bullet named plane/sphere directly as one of
// the still-missing pairs: "No public analytic-SSX API, and no plane/sphere,
// cone or torus closed form (the only general path is the mesh-seeded
// IntersectSurfaces)." A plane and a sphere meet in, at most, one circle -
// this solves that true geometric fact directly (the sphere center's signed
// distance `d` from the plane via ON_Plane::DistanceTo(), the circle's own
// center at the center's own projection onto the plane, radius
// sqrt(r^2 - d^2)) rather than mesh-seeding IntersectSurfaces() and
// Newton-polishing a chain of approximate points through an exact relation
// that already has a one-line closed form. Degenerates honestly at both
// ends: `|d| > r + tolerance` is a genuine miss (`empty == true`); `|d|`
// within `tolerance` of `r` is a single tangent POINT, not a
// zero-or-negative-radius "circle" (`tangent == true`, only `point` is
// meaningful); otherwise the real circle is built directly as an ON_Circle
// in a plane parallel to `plane` (same xaxis/yaxis, origin at the center's
// projection) and converted to its own NURBS form via
// ON_Circle::GetNurbForm(), the same exact-conversion primitive
// Brep::Sphere()/Cone()/Torus() already rely on elsewhere in this kernel.
// Still honestly scoped: only this one analytic pair (plane/sphere) is
// closed by this function - plane/cylinder, cylinder/cylinder, plane/cone,
// and plane/torus remain exactly as unaddressed as this bullet's own prior
// evidence already named them (those closed forms still exist only inside
// BooleanCombineMixed's own private splitters, not as a public API), and
// this does not replace IntersectSurfaces() for a plane/sphere pair that
// arrives as two generic ON_Surface references with no sphere-ness known
// to the caller - a caller has to already know it is holding an ON_Sphere
// to call this at all.
struct PlaneSphereIntersection {
  bool empty = true;     // true: the plane and sphere do not meet at all (|d| > r + tolerance)
  bool tangent = false;  // true: a single tangent point only (|d| within tolerance of r); only `point` is meaningful then
  Point3d point;         // the tangent point - meaningful only when tangent == true
  ON_Circle circle;      // the intersection circle - meaningful only when !empty && !tangent
  ON_NurbsCurve curve;   // circle's own NURBS form (ON_Circle::GetNurbForm) - meaningful only when !empty && !tangent
};
PlaneSphereIntersection IntersectPlaneSphere(const ON_Plane& plane, const ON_Sphere& sphere, double tolerance);

// The exact closed-form plane/cylinder SSX - the "plane/cylinder" half of
// PARITY_MAP.md's own "Analytic/analytic SSX closed forms (plane/plane,
// plane/cylinder, cylinder/cylinder, plane/sphere, cone, torus)" bullet,
// following IntersectPlaneSphere() above as the next of that same named
// list: a true geometric closed form, no mesh seeding, no Newton polish.
//
// A plane and an infinite right-circular cylinder meet in one of two
// shapes, depending on `C = dot(cylinder.Axis(), plane.zaxis)` (the signed
// cosine between the cylinder's own axis and the plane's normal):
//
//  - |C| below an internal angular tolerance (the axis lies IN the plane -
//    the plane is edge-on to the cylinder): the result is zero, one
//    (`tangent`), or two lines, each parallel to the axis. The axis's own
//    signed distance `d` to the plane is CONSTANT along its whole length
//    here (moving along the axis changes distance by `t*C`, and `C` is
//    the very thing that's ~0), so solving `d + radius*cos(phi) = 0` for
//    the radial angle `phi` (measured from the plane's own normal `n`,
//    which already lies entirely in the circular cross-section
//    perpendicular to the axis when `C` is ~0) is immediate, not
//    iterative: zero real `phi` when `|d| > radius`, one when `|d| ==
//    radius` (the tangent line), two (symmetric about the axis/normal
//    plane) when `|d| < radius`.
//  - Otherwise: a true ellipse - a true CIRCLE in the special case the
//    axis is exactly perpendicular to the plane (`|C| == 1`), the same
//    closed form handling both without a separate branch. Built from its
//    exact orthogonal semi-axis pair: the "minor" direction
//    `cross(plane.zaxis, axis)` (perpendicular to the axis's own
//    projected tilt; semi-axis length is exactly `radius`, unaffected by
//    the cut angle) and the perpendicular "major" direction within the
//    plane (semi-axis length `radius / sqrt(1 - C^2) * |C|`... in the
//    form this function actually computes, `radius * sqrt(1 + (B/C)^2)`
//    for the `B`/`C` it derives internally - see the .cpp for the full
//    derivation) - this grows without bound as the cut becomes more
//    edge-on, which is exactly the `|C| -> 0` limit the line-pair branch
//    above takes over from.
//
// Deliberately operates on the cylinder's INFINITE lateral surface along
// its own axis line - the same unbounded scope IntersectPlaneSphere()
// takes for a sphere (which has no comparable bound at all). An
// ON_Cylinder built with a finite `height` is NOT trimmed to that range
// here; a caller holding a finite cylinder gets the line(s)/ellipse of its
// unbounded extension and is responsible for trimming the result to the
// finite height range itself - a strictly smaller, strictly easier,
// follow-up problem (clip a line segment or an ellipse's own NURBS form
// against two parallel end planes) than the SSX this function actually
// solves. Still honestly scoped, matching this bullet's own prior
// evidence: only plane/sphere and now plane/cylinder are closed forms here
// - cylinder/cylinder, plane/cone, and plane/torus remain exactly as
// unaddressed as before (still existing only inside BooleanCombineMixed's
// own private splitters, not as a public API), and this does not replace
// IntersectSurfaces() for a plane/cylinder pair arriving as two generic
// ON_Surface references with no cylinder-ness known to the caller.
struct PlaneCylinderIntersection {
  bool empty = true;
  bool parallel_to_axis = false;  // true: the plane is (within tolerance) edge-on to the axis - the result is line(s), not an ellipse
  bool tangent = false;           // meaningful only when parallel_to_axis: a single tangent line (only line_a is meaningful then)
  ON_Line line_a, line_b;         // meaningful only when parallel_to_axis && !empty; line_b meaningful only when !tangent too
  ON_Ellipse ellipse;              // meaningful only when !parallel_to_axis && !empty
  ON_NurbsCurve curve;             // ellipse's own NURBS form (ON_Ellipse::GetNurbForm) - meaningful only when !parallel_to_axis && !empty
};
PlaneCylinderIntersection IntersectPlaneCylinder(const ON_Plane& plane, const ON_Cylinder& cylinder, double tolerance);

// The exact closed-form cylinder/cylinder SSX for the PARALLEL-AXIS special
// case - narrows the "cylinder/cylinder" half of PARITY_MAP.md's own
// "Analytic/analytic SSX closed forms" bullet (still not fully closed: the
// general, non-parallel-axis pair stays exactly as unaddressed as before -
// see this function's own `not_parallel` refusal). This is a public
// extraction of the SAME closed form `BooleanCombineMixed`'s own private
// `ComputeParallelCylinderCrossing` (boolean.cpp) already uses internally -
// the standard circle/circle intersection (Weisstein/MathWorld; Paul
// Bourke, 1997) of the two cylinders' cross-sectional circles, projected
// into any plane perpendicular to their shared axis direction (valid at
// every such plane identically, since neither axis has a component in
// that projection direction) - but exposed here as a public API returning
// the actual 3D line(s), not merely the angles `BooleanCombineMixed`'s own
// internal angular-split bookkeeping needs.
//
// `axis_a`/`axis_b` must be parallel or antiparallel to within an internal
// angular tolerance, or this refuses outright (`not_parallel == true`) -
// a caller with two cylinders at a genuine angle needs the still-missing
// general closed form (or the mesh-seeded `IntersectSurfaces()` instead).
// Given that, the two circles (radius `r_a`/`r_b`, centers at each
// cylinder's own `Center()`, both projected into a plane perpendicular to
// `a`'s own axis) meet in zero, one (tangent), or two points via the
// standard formula; each point extrudes to its own line, parallel to the
// shared axis, through that point. Two cylinders sharing (to within
// tolerance) the SAME axis line (concentric, including two genuinely
// coincident cylinders) are a disclosed non-result (`empty == true`,
// `not_parallel == false`) rather than a fabricated line pair: a shared
// axis has no well-defined "lens" of 2D circle crossings at all (every
// angle is either always-inside or always-outside the other circle, the
// same structural fact `BooleanCombineMixed`'s own `CylinderCylinderNoInteraction`
// relies on for its own disjoint/nested cases).
struct CylinderCylinderParallelIntersection {
  bool empty = true;
  bool not_parallel = false;  // true: refused outright - the two axes are not (anti)parallel within tolerance; every other field is meaningless
  bool tangent = false;       // a single tangent line (only line_a is meaningful then)
  ON_Line line_a, line_b;     // meaningful only when !empty && !not_parallel; line_b meaningful only when !tangent too
};
CylinderCylinderParallelIntersection IntersectCylinderCylinderParallel(const ON_Cylinder& a, const ON_Cylinder& b, double tolerance);

// The exact closed-form cylinder/cylinder SSX for the EQUAL-RADIUS,
// INTERSECTING-AXES (Steinmetz/bicylinder) special case - a second
// narrowing of the "cylinder/cylinder" half of PARITY_MAP.md's own
// "Analytic/analytic SSX closed forms" bullet, alongside the parallel-axis
// case IntersectCylinderCylinderParallel() above already closes (still not
// fully closed: the general unequal-radius or genuinely skew-axes pair
// stays exactly as unaddressed as before - see this function's own
// `unequal_radius`/`skew` refusals). This is a public extraction of the
// SAME closed form `BooleanCombineMixed`'s own private
// `ComputeSteinmetzCrossing`/`SplitCylindricalBySteinmetzCylinder`
// (boolean.cpp) already rely on internally for their own wall-splitting
// bookkeeping - the classical public-domain Steinmetz fact that two
// equal-radius cylinders whose axes meet at a point Q factor their
// intersection into two PLANAR ELLIPSES: subtracting the two implicit
// cylinder equations `|p-Q|^2 - dot(p-Q, axis_a)^2 == r^2` and
// `|p-Q|^2 - dot(p-Q, axis_b)^2 == r^2` (equal `r` on both sides is what
// makes this work at all - this is NOT available for unequal radii) leaves
// `dot(p-Q, axis_a - axis_b) == 0` or `dot(p-Q, axis_a + axis_b) == 0`,
// i.e. two planes through Q; each one cuts EITHER cylinder in the
// identical 3D ellipse (a point on one of these planes sits at the same
// distance from both axes by construction), so this function computes
// each ellipse only once, as cylinder `a`'s own
// `IntersectPlaneCylinder()` result against that plane - not a fresh
// derivation, and not sampled or Newton-polished.
//
// `a`/`b` must have equal radius (to within an internal relative
// tolerance) or this refuses outright (`unequal_radius == true`): a
// caller with two different-radius cylinders at a genuine angle needs the
// still-missing general closed form (or `IntersectSurfaces()` instead).
// `axis_a`/`axis_b` must also be genuinely non-parallel (otherwise this is
// `IntersectCylinderCylinderParallel()`'s own case, `parallel == true`
// here) AND their infinite axis lines must actually meet at a point
// within `tolerance` (`skew == true` when they are non-parallel but
// genuinely skew, the one configuration this closed form has no answer
// for at all - the general skew case needs a genuine NURBS-NURBS surface
// intersection). Given all of that, `ellipse_a`/`ellipse_b` are the two
// Steinmetz ellipses, each built as `IntersectPlaneCylinder(plane, a,
// tolerance)` against the plane with normal `axis_a - axis_b` (ellipse_a)
// or `axis_a + axis_b` (ellipse_b), both through the axes' own crossing
// point - never the line-pair branch of that function, since neither
// normal can lie in a plane edge-on to `a`'s own axis for any genuine
// angle strictly between 0 and pi (dot(axis_a, axis_a-axis_b) == 1 -
// cos(alpha), dot(axis_a, axis_a+axis_b) == 1 + cos(alpha), both bounded
// away from 0 once the parallel/antiparallel guard above has ruled out
// alpha == 0 or pi).
//
// Deliberately operates on both cylinders' own INFINITE lateral surfaces,
// the same unbounded scope every other closed form in this file already
// takes; a caller holding finite cylinders is responsible for trimming
// the result to each one's own finite height range itself.
struct CylinderCylinderIntersectingIntersection {
  bool empty = true;
  bool unequal_radius = false;  // true: refused outright - `a`/`b` do not have the same radius; every other field is meaningless
  bool parallel = false;        // true: refused outright - the two axes are (anti)parallel within tolerance; use IntersectCylinderCylinderParallel() instead
  bool skew = false;            // true: refused outright - the axes are non-parallel but their infinite lines do not actually meet within tolerance
  ON_Ellipse ellipse_a, ellipse_b;      // the two Steinmetz ellipses - meaningful only when !empty && !unequal_radius && !parallel && !skew
  ON_NurbsCurve curve_a, curve_b;       // each ellipse's own NURBS form - meaningful under the same condition
};
CylinderCylinderIntersectingIntersection IntersectCylinderCylinderIntersecting(const ON_Cylinder& a, const ON_Cylinder& b, double tolerance);

// The exact closed-form plane/plane SSX - closes the "plane/plane" half of
// PARITY_MAP.md's own "Analytic/analytic SSX closed forms" bullet, the
// easiest of its named pairs and, until now, still unaddressed as a public
// API (the bullet's own evidence: "closed forms still exist only inside
// ... the planar boolean's plane/plane path"). Two planes meet in a line
// (the general case), are the SAME plane (coincident), or never meet
// (parallel but distinct) - no mesh seeding, no Newton polish, the exact
// classical formula: writing each plane's own unit normal/offset as
// `n, h = dot(n, plane.origin)` (so the plane's own equation is `dot(n, X)
// == h`), a point common to both planes decomposes uniquely as `alpha*n_a +
// beta*n_b` (the only directions that can affect either dot product) via
// the 2x2 system `alpha + beta*c == h_a`, `alpha*c + beta == h_b` (`c =
// dot(n_a, n_b)`, and `n_a.n_a == n_b.n_b == 1` since both are unit),
// solved directly as `alpha = (h_a - c*h_b)/(1-c^2)`, `beta = (h_b -
// c*h_a)/(1-c^2)` - valid whenever the planes are not parallel (`1-c^2`
// bounded away from 0), giving the line's own point `alpha*n_a + beta*n_b`
// and direction `cross(n_a, n_b)` directly. When `|c|` is within an
// internal angular tolerance of 1 (the planes ARE parallel), that formula's
// own denominator degenerates; this instead checks `a`'s own distance to
// `b`'s plane directly: within `tolerance` means the same plane
// (`coincident == true`, every point shared, no single line to report);
// otherwise two genuinely parallel, disjoint planes (`empty == true`).
struct PlanePlaneIntersection {
  bool empty = true;
  bool coincident = false;  // true: the two planes are the same plane (parallel AND within tolerance of each other) - every point is shared, `line` is meaningless
  ON_Line line;              // meaningful only when !empty && !coincident
};
PlanePlaneIntersection IntersectPlanePlane(const ON_Plane& a, const ON_Plane& b, double tolerance);

// The exact closed-form plane/cone SSX, restricted to the two cases that
// reduce to a construction this kernel already has elsewhere - the "cone"
// half of PARITY_MAP.md's own "Analytic/analytic SSX closed forms (plane/
// plane, plane/cylinder, cylinder/cylinder, plane/sphere, cone, torus)"
// bullet, previously entirely unaddressed: unlike every other pair this
// bullet names, a plane/cone closed form exists nowhere in this kernel,
// not even inside BooleanCombineMixed's own private splitters.
//
// A right circular (double-napped) cone with apex `cone.ApexPoint()`, unit
// axis `cone.Axis()`, and half-angle `alpha = |cone.AngleInRadians()|`
// satisfies the homogeneous relation `dot(V, axis)^2 == cos(alpha)^2 *
// dot(V, V)` for `V = point - apex` (true on either nappe at once, with no
// separate sign case). Restricting this to the plane's own in-plane (s, t)
// coordinates (`point = plane.origin + s*plane.xaxis + t*plane.yaxis`)
// gives a single 2D conic `A*s^2 + B*s*t + C*t^2 + D*s + E*t + F == 0` -
// the classical "quadric meets a plane" reduction, with A/B/C depending
// only on the direction cosines of plane.xaxis/plane.yaxis against the
// cone's own axis (so the SAME A, B, C apply regardless of where the
// plane's own origin sits). This function closes only the two cases that
// follow directly from that reduction without needing a general conic-
// family constructor of its own:
//
//  - The plane does not pass through the apex (`plane.DistanceTo(apex)`
//    outside `tolerance`), and the restricted conic's own discriminant
//    `B^2 - 4*A*C` is negative - the plane is steeper than the cone's own
//    generators relative to the axis, cutting only one nappe in a closed
//    loop: a genuine ELLIPSE (a true circle in the special case the plane
//    is exactly perpendicular to the axis - the same "one formula handles
//    both" shape IntersectPlaneCylinder() already established), built
//    directly from the conic matrix [[A, B/2], [B/2, C]]'s own 2x2 eigen-
//    decomposition (a closed form for a 2x2 symmetric matrix - no general
//    numerical eigensolver needed) rather than sampled or Newton-polished.
//  - The plane passes through the apex (within `tolerance`): every D/E/F
//    term of the restricted conic vanishes identically (the apex itself,
//    (0,0) in apex-centered local coordinates, is always a root), so the
//    conic collapses to the homogeneous `A*ds^2 + B*ds*dt + C*dt^2 == 0`
//    in a direction `(ds, dt)` alone - solved directly via the same
//    discriminant (`< 0`: no real direction, the plane touches the cone
//    at the apex point only; `== 0`: one real direction, a single tangent
//    line through the apex; `> 0`: two real directions, a genuine line
//    pair through the apex) by writing the direction as `cos(theta)*
//    plane.xaxis + sin(theta)*plane.yaxis` and solving `A*cos(theta)^2 +
//    B*cos(theta)*sin(theta) + C*sin(theta)^2 == 0` via its own double-
//    angle closed form - no case split on A or C individually being zero.
//
// Everything else - the plane does not reach the apex but is not steep
// enough relative to the cone's own half-angle (cuts both nappes: a
// hyperbola) or is exactly parallel to one generator (a parabola) - is
// honestly reported as `unsupported == true` rather than guessed at or
// silently misreported as an ellipse: a caller checks `unsupported` FIRST,
// before `empty`/`through_apex`/anything else. Deliberately scoped to the
// cone's INFINITE double nappe along its own axis line, the same unbounded
// scope IntersectPlaneCylinder()/IntersectPlaneSphere() already take; a
// finite `cone.height` is not trimmed to that range here, and this does
// not replace IntersectSurfaces() for a plane/cone pair arriving as two
// generic ON_Surface references with no cone-ness known to the caller.
struct PlaneConeIntersection {
  bool unsupported = false;   // true: this pair's conic is a parabola or hyperbola, not built here - every other field is meaningless; checked FIRST
  bool empty = true;          // true: a genuine miss (only reachable via a guarded-against numerical edge case; kept for symmetry with this file's other closed forms)
  bool through_apex = false;  // true: the plane passes through the cone's own apex - line_count/line_a/line_b are meaningful, not ellipse/curve
  int line_count = 0;         // meaningful only when through_apex: 0 (apex point only), 1 (tangent, only line_a meaningful), or 2
  ON_Line line_a, line_b;     // meaningful only when through_apex; line_b meaningful only when line_count == 2
  ON_Ellipse ellipse;         // meaningful only when !through_apex && !empty && !unsupported
  ON_NurbsCurve curve;        // ellipse's own NURBS form - meaningful under the same condition as `ellipse`
};
PlaneConeIntersection IntersectPlaneCone(const ON_Plane& plane, const ON_Cone& cone, double tolerance);

// The exact closed-form plane/torus SSX, restricted to the two special
// plane orientations that reduce directly to a circle (or circle pair) -
// the "torus" half of PARITY_MAP.md's own "Analytic/analytic SSX closed
// forms" bullet, previously entirely unaddressed: a general oblique plane/
// torus section is a quartic space curve with no simple closed form
// (including the classical Villarceau-circle case at one special oblique
// angle), and this does not attempt either.
//
//  - MERIDIAN: the plane contains the torus's own axis (within
//    tolerance) - the classical "slice a donut straight through the
//    middle" cut. Writing the torus's own defining relation `(hypot(x, y)
//    - R)^2 + z^2 == r^2` (R = major_radius, r = minor_radius, z along the
//    torus axis) restricted to any single half-plane at a fixed angle
//    around that axis gives exactly one circle of radius r centered at
//    distance R from the axis, in that half-plane; the full plane (both
//    half-planes at once, on either side of the axis) always produces
//    the two symmetric copies of that circle, both returned at once.
//  - AXIAL: the plane is perpendicular to the torus's own axis (within
//    tolerance), at signed height `z` from the torus center along that
//    axis - an ordinary horizontal slice. Solving the same defining
//    relation for a fixed z gives `hypot(x, y) == R +/- sqrt(r^2 - z^2)`:
//    two concentric circles when `|z| < r` and the inner radius is
//    genuinely positive, collapsing to a single circle when `|z| == r`
//    (the tangent degeneracy at the very top/bottom of the torus) or when
//    the torus's own central hole does not reach this far in (the inner
//    radius would be non-positive), and a genuine miss when `|z| > r`.
//
// Every other plane orientation is honestly `unsupported == true`, not
// guessed at.
struct PlaneTorusIntersection {
  bool unsupported = false;
  bool empty = true;
  bool meridian = false;     // true: the plane contains the torus axis - circle_a and circle_b (always both) are meaningful
  bool axial = false;        // true: the plane is _|_ the torus axis - circle_a is meaningful whenever !empty; circle_b only when circle_count == 2
  int circle_count = 0;      // meridian: always 2; axial: 1 or 2
  ON_Circle circle_a, circle_b;
  ON_NurbsCurve curve_a, curve_b;  // NURBS forms of circle_a/circle_b - meaningful under the same conditions as those circles
};
PlaneTorusIntersection IntersectPlaneTorus(const ON_Plane& plane, const ON_Torus& torus, double tolerance);

// A single point where a surface's own silhouette for a FIXED, PARALLEL
// (orthographic) viewing direction crosses one edge of a regular sampling
// grid over its (u, v) domain - PARITY_MAP.md's own "Silhouette / outline
// curves" bullet: "still app-only and mesh-based ... No kernel silhouette."
// The silhouette (contour generator) for an orthographic view along
// `view_direction` is, by definition, every point where the surface's own
// tangent plane CONTAINS `view_direction` - equivalently, where the
// surface's normal is exactly perpendicular to it (`dot(normal,
// view_direction) == 0`): the viewing ray grazes the surface tangentially
// there rather than piercing it transversally. This is found by sampling
// `f(u, v) = dot(RobustSurfaceNormal(s, u, v), view_direction)` (the same
// nudge-off-a-pole-safe normal `FindSurfaceTangentContacts()` already
// relies on, here for the identical reason: a contact or a silhouette
// crossing landing exactly on a coordinate pole must not be missed just
// because the raw `d/du`/`d/dv` vanish there) at every vertex of the SAME
// regular `TessellateWithUV()` grid every other function in this file
// already tessellates with, and bisecting along every grid EDGE (both
// horizontal and vertical) whose two endpoints disagree in `f`'s own sign -
// the one-dimensional "on/off" sign test every other sampling-based
// detector in this file already uses (`IntersectCurveSurfaceOverlap`'s own
// on/off-surface predicate, `IntersectSurfacesOverlap`'s own on-`b`/off-`b`
// grid cells), applied here to a scalar tangency test instead. Reported as
// isolated POINTS, not stitched into a curve - the silhouette is, in
// general, a continuous curve in (u, v) space (one equation in two
// unknowns, structurally under-determined the way a single Newton seed
// cannot pin to a unique point the way `FindSurfaceTangentContacts()`'s own
// fully-determined systems do), the same honestly-scoped choice this file's
// other inherently-continuous-locus detectors already make when full
// curve-chaining is out of scope. Duplicate crossings within
// `opt.tolerance * 4` of an already-accepted point are dropped, the same
// dedup radius `IntersectCurveSurface`/`IntersectCurves` already use for
// their own crossing points. A grid edge where `RobustSurfaceNormal`
// degenerates at either endpoint (a genuinely malformed surface there, not
// merely a pole - `RobustSurfaceNormal`'s own nudge already handles an
// ordinary pole) is skipped outright ("cannot verify", not guessed), and a
// zero-length `view_direction` returns empty outright (there is no
// direction to test tangency against). Still honestly scoped: orthographic
// projection only (no perspective eye point - a ray from a finite viewpoint
// needs a per-point VARYING direction, `point - eye`, not this function's
// own single fixed `view_direction`); point detections only, as above; no
// visibility/self-occlusion resolution (a point behind the surface's own
// near side from the viewer, or hidden behind a different object entirely,
// is reported exactly like a visible one); and no `dino8-app` command calls
// it yet (the app's own `Silhouette` stays mesh-based, unchanged).
struct SurfaceSilhouettePoint {
  ON_2dPoint uv;
  Point3d point;
};
std::vector<SurfaceSilhouettePoint> FindSurfaceSilhouettePoints(const ON_Surface& s, const Vector3d& view_direction, const IntersectOptions& opt);

// The perspective sibling of FindSurfaceSilhouettePoints() above - narrows
// the "Silhouette / outline curves" bullet's own "orthographic projection
// only" caveat, which that function's own header doc comment names as a
// real remaining gap ("no perspective eye point, which would need a
// per-point VARYING direction... not this function's own single fixed
// one"). The silhouette (contour generator) for a perspective view from a
// finite `eye` is, by definition, every point where the surface's own
// tangent plane CONTAINS the viewing ray TO that point - the same `dot(
// normal, direction) == 0` tangency test FindSurfaceSilhouettePoints()
// already uses, just with `direction = point - eye` now genuinely varying
// per sample instead of one fixed vector. Shares that same function's own
// sampling/bisection/dedup machinery exactly (both are built on the one
// internal grid-edge tangency-crossing finder this file now shares between
// them) - only the direction a sample tests against differs. A grid sample
// that happens to land exactly at `eye` itself (direction length ~0, no
// ray to test tangency against there) is treated as "cannot verify", the
// same honest skip RobustSurfaceNormal()'s own pole degeneracy already
// gets. Still honestly scoped exactly as FindSurfaceSilhouettePoints() is:
// point detections only, no curve-chaining (see FindSurfaceSilhouetteCurves()
// below for that, which does not yet have a perspective sibling of its
// own), and no visibility/self-occlusion resolution.
std::vector<SurfaceSilhouettePoint> FindSurfaceSilhouettePointsPerspective(const ON_Surface& s, const Point3d& eye, const IntersectOptions& opt);

// Chains FindSurfaceSilhouettePoints()'s own isolated grid-edge crossings
// into actual curves - narrows that function's own "point detections only"
// caveat for the orthographic case. The silhouette is a continuous locus in
// (u, v) (one equation in two unknowns), so a single Newton seed cannot pin
// it to a unique point the way FindSurfaceTangentContacts()'s own fully-
// determined systems do - but the SAME regular TessellateWithUV() grid
// FindSurfaceSilhouettePoints() already samples gives every crossing a
// known HOME EDGE, and two crossings on the four boundary edges of one grid
// CELL are connected directly (the classical "marching squares" contour-
// extraction idea, applied to this scalar tangency field instead of an
// implicit function's own zero level set): a cell with exactly two crossing
// edges links them; a cell with zero has no crossing to link; a cell with
// all four (the "saddle" ambiguity marching squares is already well known
// for, a genuine but measure-zero coincidence of the grid's own placement,
// not a routine occurrence) is honestly skipped rather than guessed at with
// an arbitrary diagonal pairing. Chaining these links across the whole grid
// gives each crossing point degree 0 (dropped - not part of any clean
// 2-crossing cell), 1 (an open chain's own endpoint - typically where the
// silhouette runs off the surface's own parameter-domain boundary), or 2 (an
// ordinary interior pass-through); walking every degree-1 start to its
// matching endpoint recovers the open chains, and whatever links remain
// afterward are closed loops (every remaining node degree 2) walked back to
// their own start. Each chain's own ordered 3D points are fit via the same
// InterpolateCubic()/ChordParams() global-interpolation pair this file
// already uses for every other sampled curve (PullbackCurveToSurface()'s
// `pulled_curve`, ContourAtPlane(), ...), not resampled. Still honestly
// scoped: an isolated degree-0 crossing (dropped here) is still visible via
// FindSurfaceSilhouettePoints() itself; saddle cells are skipped rather than
// resolved; there is no perspective sibling of this function yet; and - the
// one gap this bullet's own text keeps naming regardless of which of these
// functions is asked - no visibility/self-occlusion resolution (a chain on
// the surface's own far side from the viewer, or hidden behind a different
// object, is reported exactly like a visible one).
struct SurfaceSilhouetteCurve {
  ON_NurbsCurve curve;
  bool closed = false;
};
std::vector<SurfaceSilhouetteCurve> FindSurfaceSilhouetteCurves(const ON_Surface& s, const Vector3d& view_direction, const IntersectOptions& opt);

// --- numerical helpers ------------------------------------------------------

// Damped Gauss-Newton on residual(x) (m equations, n unknowns) with box
// bounds. Under-determined systems take the minimal-norm step. Returns
// true when |residual| <= tol.
using Residual = std::function<std::vector<double>(const std::vector<double>&)>;
bool NewtonSolve(const Residual& residual, std::vector<double>& x, const std::vector<double>& lo, const std::vector<double>& hi, double tol, int max_iter = 40, double* final_norm = nullptr);

// Newton polish of a surface/surface point from seed parameters.
bool RefineSurfaceSurfacePoint(const ON_Surface& a, const ON_Surface& b, double& ua, double& va, double& ub, double& vb, double tol, int max_iter = 40);

// Closest point on a surface (Newton from a seed; `global` first scans a grid
// to seed the Newton polish, then reports the SAME convergence status as
// SurfaceClosestPoint() - i.e. `global`'s bool is not automatically true
// just because a grid seed exists; a genuinely pathological surface (e.g.
// wildly-varying rational weights) can still fail the Newton polish and
// this correctly returns false in that case, matching SurfaceClosestPoint's
// own contract).
bool SurfaceClosestPoint(const ON_Surface& s, Point3d p, double& u, double& v, int max_iter = 40);
bool SurfaceClosestPointGlobal(const ON_Surface& s, Point3d p, double& u, double& v, int grid = 24);

// Closest parameter on a curve (Newton from a seed; `global` scans samples first).
double CurveClosestParam(const ON_Curve& c, Point3d p, double seed, int max_iter = 40);
double CurveClosestParamGlobal(const ON_Curve& c, Point3d p, int samples = 256);

// Global cubic interpolation through points (chord-length parameters when
// `params` is empty). Closed input (first != last point, closed = true)
// is wrapped so the seam is smooth. `dim` = 2 or 3.
ON_NurbsCurve InterpolateCubic(const std::vector<ON_3dPoint>& pts, std::vector<double> params, bool closed, int dim = 3);
std::vector<double> ChordParams(const std::vector<ON_3dPoint>& pts, bool closed);

// Is (u, v) inside the face's trim loops? (Untrimmed surface: domain test.)
bool FaceContainsUV(const ON_BrepFace& f, double u, double v);

// 2D polygon of a face loop (sampled trims), for containment tests.
std::vector<ON_2dPoint> LoopPolygon(const ON_Brep& b, int loop_index, int samples_per_trim = 24);
bool PointInPolygon(const std::vector<ON_2dPoint>& poly, ON_2dPoint p);

}  // namespace dino8::kernel
