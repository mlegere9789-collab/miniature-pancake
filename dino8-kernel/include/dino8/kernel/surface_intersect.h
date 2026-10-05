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
// true`. Each boundary that has a genuine off-surface neighbour sample (not
// the curve's own domain end) is then bisection-refined between that
// on/off sample pair - 40 halvings of an already sampling-resolution-wide
// bracket, closing this function's own previously-disclosed "t0/t1 are only
// as precise as the sampling resolution" honesty gap: the reported boundary
// is now the exact (to within opt.tolerance, not the coarser sampling
// pitch) parameter where the curve leaves the surface, always landing on
// the genuinely on-surface side of that transition.
std::vector<CurveSurfaceOverlap> IntersectCurveSurfaceOverlap(const ON_Curve& c, const ON_Surface& s, const IntersectOptions& opt);

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
// regions" bullet: "IntersectSurfaces still returns nothing for coincident
// surfaces. The only coincidence handling is inside planar booleans."
// IntersectSurfaces() is the wrong tool for a coincident region for the same
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
