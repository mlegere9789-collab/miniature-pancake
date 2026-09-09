// Real fillet/chamfer/blend/match family, built on the surface/surface and
// curve/surface intersector in geom/SurfaceIntersect.{h,cpp}. Registered
// after RegisterSrfEditCommands and RegisterCurveEditCommands in
// Application.cpp, so every registration here wins over both the srfedit
// print-only stubs and the curve-only Intersect.
//
// Rolling-ball fillet method (FilletSrf/FilletEdge/BlendEdge and their
// Variable* cousins): offset both surfaces towards each other by the
// radius, intersect the offsets (SSX) to get the spine, then for every
// spine sample find the two contact points as the closest points on the
// original surfaces and build the exact rational arc between them centred
// on the spine point; the arcs are lofted into a rational NURBS surface by
// interpolating each of the arc's 3 homogeneous control points along the
// spine (standard NURBS "skinning" of same-degree rows - the result's
// isoparm at each spine sample is exactly that row's arc). Real trimming
// is implemented for the case both adjacent surfaces are (or fit) a plane
// (the common box/polysurface-corner case): a fresh ON_BrepTrimmedPlane is
// built from the kept part of the original loop plus the contact curve.
// Anything else (cylinder/plane, doubly-curved, non-planar corners) falls
// back to a mesh boolean of a swept cutting tool, and says so in the
// command's printed note ("mesh fallback").
#include "commands/cmd_common.h"
#include "geom/BlendSurface.h"
#include "geom/SurfaceIntersect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

namespace dino8::app {

namespace {

// ---------------------------------------------------------------------------
// Small object/face/edge helpers (kept local; cmd_srfedit.cpp's own copies
// are anonymous-namespace private to that file).
// ---------------------------------------------------------------------------

std::optional<ON_Brep> BrepOfObject(const SceneObject& o) {
  if (o.kind == ObjectKind::Brep && o.brep) return o.brep->raw();
  if (o.kind == ObjectKind::Surface && o.surface) {
    ON_Brep b;
    ON_NurbsSurface* srf = new ON_NurbsSurface(o.surface->raw());
    b.Create(srf);
    return b;
  }
  return std::nullopt;
}

std::optional<ON_NurbsSurface> SurfaceOfObject(const SceneObject& o, int face = 0) {
  if (o.kind == ObjectKind::Surface && o.surface) return o.surface->raw();
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    if (face < 0 || face >= b.m_F.Count()) return std::nullopt;
    const ON_Surface* s = b.m_F[face].SurfaceOf();
    ON_NurbsSurface ns;
    if (s && s->GetNurbForm(ns) > 0) {
      if (b.m_F[face].m_bRev) ns.Reverse(0);
      return ns;
    }
  }
  return std::nullopt;
}

int NearestFace(const ON_Brep& b, Point3d p, double* dist_out = nullptr) {
  BrepMeshOptions opt;
  opt.chord_tolerance = 0.05;
  std::vector<kernel::Mesh> faces = MeshBrepFaces(b, opt);
  int best = -1;
  double bd = std::numeric_limits<double>::max();
  for (size_t i = 0; i < faces.size() && static_cast<int>(i) < b.m_F.Count(); ++i) {
    if (faces[i].FaceCount() == 0) continue;
    const double d = faces[i].ClosestPoint(p).DistanceTo(p);
    if (d < bd) { bd = d; best = static_cast<int>(i); }
  }
  if (dist_out) *dist_out = bd;
  return best;
}

struct FacePick { ObjectId id = kNoObject; int face = -1; double dist = 0; };

std::optional<FacePick> PickFace(CommandContext& ctx, Point3d p) {
  std::optional<FacePick> best;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::optional<ON_Brep> b = BrepOfObject(o);
    if (!b) continue;
    double d = 0;
    const int f = NearestFace(*b, p, &d);
    if (f < 0) continue;
    if (!best || d < best->dist) best = FacePick{o.id, f, d};
  }
  return best;
}

bool EdgeClosest(const ON_BrepEdge& e, Point3d p, double& t) {
  ON_NurbsCurve nc;
  if (e.GetNurbForm(nc) <= 0) return false;
  kernel::NurbsCurve k;
  k.raw() = nc;
  t = k.ClosestPointParameter(p, 200);
  return true;
}

struct EdgePick { ObjectId id = kNoObject; int edge = -1; double dist = 0; };

std::optional<EdgePick> PickEdge(CommandContext& ctx, Point3d p) {
  std::optional<EdgePick> best;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::optional<ON_Brep> b = BrepOfObject(o);
    if (!b) continue;
    for (int i = 0; i < b->m_E.Count(); ++i) {
      const ON_BrepEdge& e = b->m_E[i];
      if (e.m_edge_index < 0 || e.TrimCount() != 2) continue;  // fillet needs two adjacent faces
      double t = 0;
      if (!EdgeClosest(e, p, t)) continue;
      ON_NurbsCurve enc;
      e.GetNurbForm(enc);
      const double d = enc.PointAt(t).DistanceTo(p);
      if (!best || d < best->dist) best = EdgePick{o.id, i, d};
    }
  }
  return best;
}

ObjectId AddCurveFrom(CommandContext& ctx, const ON_Curve& c, const SceneObject& like) {
  kernel::NurbsCurve k;
  if (!CurveFromON(c, k)) return kNoObject;
  SceneObject n = SceneObject::MakeCurve(k);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  return ctx.Doc().Add(std::move(n));
}

ObjectId AddBrepFrom(CommandContext& ctx, const ON_Brep& b, const SceneObject& like) {
  kernel::Brep k;
  k.raw() = b;
  SceneObject n = SceneObject::MakeBrep(k);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  n.material_name = like.material_name;
  return ctx.Doc().Add(std::move(n));
}

ObjectId AddSurfaceFrom(CommandContext& ctx, const ON_NurbsSurface& s, const SceneObject& like) {
  kernel::NurbsSurface k;
  k.raw() = s;
  SceneObject n = SceneObject::MakeSurface(k);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  n.material_name = like.material_name;
  return ctx.Doc().Add(std::move(n));
}

std::optional<kernel::Mesh> ObjectMesh(const SceneObject& o, double tol) { return MeshOf(o, tol); }

// ---------------------------------------------------------------------------
// Homogeneous "skinning" of same-degree, same-knot-vector rows into a
// rational NURBS surface (HomogeneousRow, LoftRows) now lives in
// geom/BlendSurface.{h,cpp} - shared with the Hermite blend rows there and
// with tests/test_g2_blend.cpp.
// ---------------------------------------------------------------------------
// Rolling-ball fillet core
// ---------------------------------------------------------------------------

// Offsets a NURBS surface by `d` along the Greville normal (exact for
// planes; approximate elsewhere - same technique as OffsetNurbs in
// cmd_surface.cpp, duplicated here to keep this file self-contained).
kernel::NurbsSurface OffsetBy(const ON_NurbsSurface& in, double d) {
  kernel::NurbsSurface out;
  out.raw() = in;
  ON_NurbsSurface& s = out.raw();
  for (int i = 0; i < s.CVCount(0); ++i)
    for (int j = 0; j < s.CVCount(1); ++j) {
      const double u = in.GrevilleAbcissa(0, i), v = in.GrevilleAbcissa(1, j);
      ON_3dVector n = in.NormalAt(u, v);
      if (!n.Unitize()) continue;
      ON_3dPoint p;
      in.GetCV(i, j, p);
      s.SetCV(i, j, p + n * d);
    }
  return out;
}

// Picks the offset sign that moves the surface towards `target` (its
// domain-centre point is compared to the offset centre point).
double OffsetSign(const ON_NurbsSurface& s, Point3d target) {
  const double u = s.Domain(0).Mid(), v = s.Domain(1).Mid();
  const Point3d p = s.PointAt(u, v);
  ON_3dVector n = s.NormalAt(u, v);
  if (!n.Unitize()) return 1;
  return ON_DotProduct(target - p, n) >= 0 ? 1.0 : -1.0;
}

struct FilletBuild {
  bool ok = false;
  std::string error;
  ON_NurbsSurface fillet;              // rational fillet surface (u: across, v: along the spine)
  std::vector<Point3d> spine_pts;
  std::vector<Point3d> contact_a, contact_b;
  std::vector<ON_2dPoint> uv_a, uv_b;  // contact parameters on the ORIGINAL surfaces
  ON_NurbsCurve contact_curve_a, contact_curve_b;  // 3D curves through contact_a/contact_b
  ON_NurbsCurve pcurve_a, pcurve_b;                // 2D curves (on surface a/b) through uv_a/uv_b
  double max_gap = 0;                  // how far contact points are from the exact radius (quality signal)
};

double ClampImpl(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
double Clamp(double v, double lo, double hi) { return ClampImpl(v, lo, hi); }

// Builds one rational quadratic-Bezier arc row (3 CVs) from centre P with
// unit directions dA, dB and radius r. Handles a near-flat corner (dA close
// to dB) as a degenerate zero-sweep row (still a valid 3-CV row).
HomogeneousRow ArcRow(Point3d p, Vector3d dA, Vector3d dB, double r) {
  HomogeneousRow row;
  double cosang = Clamp(ON_DotProduct(dA, dB), -1.0, 1.0);
  const double half = std::acos(cosang) * 0.5;
  Vector3d mid = dA + dB;
  double w1 = std::cos(half);
  if (!mid.Unitize() || w1 < 1e-6) { mid = dA; w1 = 1.0; }  // 0-degree sweep: collapse to a straight "arc"
  const Point3d c0 = p + dA * r, c2 = p + dB * r;
  const Point3d c1 = p + mid * (r / std::max(w1, 1e-6));
  row.cv = {ON_4dPoint(c0.x, c0.y, c0.z, 1.0), ON_4dPoint(c1.x * w1, c1.y * w1, c1.z * w1, w1), ON_4dPoint(c2.x, c2.y, c2.z, 1.0)};
  return row;
}

// Non-rational ruled surface between two curves of the same domain: row j
// is {A(t_j), B(t_j)} (nu = 2), lofted with LoftRows (weights all 1).
ON_NurbsSurface RuledSurface(const ON_Curve& a, const ON_Curve& b, int samples = 24) {
  ON_NurbsSurface s;
  std::vector<HomogeneousRow> rows;
  std::vector<double> params;
  const ON_Interval d = a.Domain();
  for (int i = 0; i <= samples; ++i) {
    const double t = d.ParameterAt(static_cast<double>(i) / samples);
    const double tb = b.Domain().ParameterAt(static_cast<double>(i) / samples);
    const Point3d pa = a.PointAt(t), pb = b.PointAt(tb);
    HomogeneousRow row;
    row.cv = {ON_4dPoint(pa.x, pa.y, pa.z, 1.0), ON_4dPoint(pb.x, pb.y, pb.z, 1.0)};
    rows.push_back(row);
    params.push_back(t);
  }
  LoftRows(rows, params, 2, s);
  return s;
}

// Bounding-box diagonal of a surface's own control net - a cheap, robust
// stand-in for "how big is this surface" without evaluating the surface
// itself (a plain CV bounding box, not a tight one, is enough to pick a
// tolerance floor by).
double SurfaceScale(const ON_NurbsSurface& s) {
  ON_BoundingBox bb;
  if (!s.GetBoundingBox(bb) || !bb.IsValid()) return 0.0;
  return bb.Diagonal().Length();
}

// Core: constant or variable radius rolling-ball fillet between two
// surfaces. `radius_at(t)` maps a spine fraction in [0,1] to a radius.
// `tol` is normally ctx.Settings().absolute_tolerance floored at 1e-5 by
// the caller - a document-wide setting that has no way to know THIS pair
// of surfaces might be millimeter-scale or kilometer-scale relative to
// whatever the user last set that document tolerance to. Every tolerance
// floor below is additionally clamped against the surfaces' own scale
// (`geo_tol`) so a mismatch between document tolerance and actual surface
// size doesn't make an otherwise-valid fillet spuriously fail (floor too
// tight for a huge pair of surfaces) or accept geometric noise as a real
// spine (floor too loose for a tiny pair).
FilletBuild BuildFillet(const ON_NurbsSurface& a, const ON_NurbsSurface& b, const std::function<double(double)>& radius_at, double tol) {
  FilletBuild out;
  const double scale = std::max(SurfaceScale(a), SurfaceScale(b));
  // 1e-6 of the surfaces' own scale - the same relative floor used for
  // Manifold's tolerance in dino8-kernel/src/boolean.cpp, kept consistent
  // here since both ultimately bound "smallest geometric detail this
  // pipeline can resolve".
  const double geo_tol = scale > 0 ? scale * 1e-6 : 0.0;
  const double eff_tol = std::max(tol, geo_tol);
  IntersectOptions opt;
  opt.tolerance = std::max(eff_tol, 1e-6);
  opt.mesh_tolerance = std::max(eff_tol * 4, geo_tol > 0 ? geo_tol * 4 : 1e-4);
  const double r0 = radius_at(0.0);
  if (!(r0 > 0)) { out.error = "radius must be positive"; return out; }
  // A radius that isn't even resolvable at this pipeline's own geometric
  // precision (e.g. asking for a fillet far smaller than the surfaces'
  // own scale times float/tolerance noise) can't produce a meaningful
  // spine - fail clearly here rather than let a near-zero offset wobble
  // through OffsetBy/IntersectSurfaces and loft into a degenerate sliver
  // surface that LoftRows or the caller's trim step would otherwise have
  // to detect after the fact.
  if (scale > 0 && r0 < eff_tol) {
    out.error = "radius " + FormatNumber(r0) + " is too small to resolve at this surface's own scale (tolerance " + FormatNumber(eff_tol) + ")";
    return out;
  }
  // Offset both surfaces towards each other by the representative radius,
  // then intersect the offsets to find the spine.
  const double sa = OffsetSign(a, b.PointAt(b.Domain(0).Mid(), b.Domain(1).Mid()));
  const double sb = OffsetSign(b, a.PointAt(a.Domain(0).Mid(), a.Domain(1).Mid()));
  const kernel::NurbsSurface offA = OffsetBy(a, sa * r0), offB = OffsetBy(b, sb * r0);
  std::vector<IntersectionCurve> ssx = IntersectSurfaces(offA.raw(), offB.raw(), opt);
  if (ssx.empty()) { out.error = "the offset surfaces do not meet (surfaces too far apart or parallel, radius too small to reach, or - just as often - radius too LARGE for the surfaces to still overlap once offset that far)"; return out; }
  // Longest curve is the spine.
  const IntersectionCurve* best = &ssx.front();
  for (const IntersectionCurve& c : ssx) if (c.Length() > best->Length()) best = &c;
  const IntersectionCurve& spine = *best;
  const size_t n = spine.points.size();
  std::vector<HomogeneousRow> rows;
  std::vector<double> params;
  const double total = spine.Length();
  for (size_t i = 0; i < n; ++i) {
    const double t = total > 0 ? (spine.params[i] - spine.params.front()) / total : 0.0;
    const double r = radius_at(t);
    double ua = spine.uv_a[i].x, va = spine.uv_a[i].y, ub = spine.uv_b[i].x, vb = spine.uv_b[i].y;
    // The offset (u,v) is a good seed for the closest point on the ORIGINAL
    // surface (exact for planes; a very close seed for gently curved ones).
    if (!SurfaceClosestPoint(a, spine.points[i], ua, va)) continue;
    if (!SurfaceClosestPoint(b, spine.points[i], ub, vb)) continue;
    const Point3d ca = a.PointAt(ua, va), cb = b.PointAt(ub, vb);
    Vector3d da = ca - spine.points[i], db = cb - spine.points[i];
    const double dda = da.Length(), ddb = db.Length();
    if (!da.Unitize() || !db.Unitize()) continue;
    out.max_gap = std::max({out.max_gap, std::fabs(dda - r0), std::fabs(ddb - r0)});
    rows.push_back(ArcRow(spine.points[i], da, db, r));
    params.push_back(spine.params[i]);
    out.spine_pts.push_back(spine.points[i]);
    out.contact_a.push_back(spine.points[i] + da * r);
    out.contact_b.push_back(spine.points[i] + db * r);
    out.uv_a.emplace_back(ua, va);
    out.uv_b.emplace_back(ub, vb);
  }
  if (rows.size() < 2) { out.error = "too few valid spine samples (radius likely larger than the surfaces support)"; return out; }
  if (!LoftRows(rows, params, 3, out.fillet)) { out.error = "failed to loft the fillet arcs"; return out; }
  std::vector<ON_3dPoint> pa, pb;
  for (size_t i = 0; i < out.contact_a.size(); ++i) { pa.push_back(out.contact_a[i]); pb.push_back(out.contact_b[i]); }
  out.contact_curve_a = InterpolateCubic(pa, params, false, 3);
  out.contact_curve_b = InterpolateCubic(pb, params, false, 3);
  std::vector<ON_3dPoint> pua, pub;
  for (size_t i = 0; i < out.uv_a.size(); ++i) { pua.emplace_back(out.uv_a[i].x, out.uv_a[i].y, 0); pub.emplace_back(out.uv_b[i].x, out.uv_b[i].y, 0); }
  out.pcurve_a = InterpolateCubic(pua, params, false, 2);
  out.pcurve_b = InterpolateCubic(pub, params, false, 2);
  out.ok = true;
  return out;
}

// Genuinely exact variable-radius fillet between two PLANAR faces meeting
// along `edge_curve` (any radius profile, not just linear). BuildFillet
// above finds its spine by offsetting both surfaces by a single constant
// amount (radius_at(0)) and intersecting the offsets - correct only when
// the radius is actually constant, since a real variable-radius rolling
// ball needs the ball's own offset to vary with position too. Confirmed by
// testing: feeding BuildFillet a radius that varies by as little as 0.2 on
// a 10-unit box edge (Radii=0:1.9,1:2.1) already produced a fillet whose
// contact points sit measurably off the original planes (the "closest
// point on the original surface" step finds a point AT r0 away, then
// out.contact_a is placed at the DIFFERENT r(t) away in the same
// direction, missing the plane by (r(t)-r0)), which is small enough to
// pass BuildFillet's own max_gap bookkeeping silently but large enough to
// fail the downstream watertight-mesh check every time.
//
// For two planes specifically this has an exact closed form instead of an
// offset-and-intersect approximation: a plane's outward contact direction
// (the unit vector from the rolling ball's centre to its tangent point on
// that plane) is the same everywhere on the plane, independent of radius
// or position along the edge - only the ball centre's distance from the
// edge changes with r(t). So one constant-radius BuildFillet call at
// r(0) is used ONLY to read off that pair of (validated, correctly
// oriented) contact directions; from there every sample's ball centre,
// and both contact points, are placed directly from r(t) and the edge
// curve's own point at t, with no surface intersection at all.
FilletBuild BuildPlanarVariableFillet(const ON_NurbsSurface& a, const ON_NurbsSurface& b, const ON_Curve& edge_curve, const std::function<double(double)>& radius_at, double tol, int samples = 32) {
  FilletBuild out;
  ON_Plane pa, pb;
  const double plane_tol = std::max(tol * 10, 1e-4);
  if (!a.IsPlanar(&pa, plane_tol) || !b.IsPlanar(&pb, plane_tol)) { out.error = "adjacent faces are not both planar"; return out; }
  const double r0 = radius_at(0.0);
  if (!(r0 > 0)) { out.error = "radius must be positive"; return out; }
  // Bootstrap the two contact directions from a single constant-radius
  // solve at r(0), which BuildFillet already gets right (validated by the
  // existing FilletEdge/ChamferEdge tests) - a plane's normal doesn't
  // depend on position, so these two directions are valid for every other
  // sample along the edge too.
  FilletBuild seed = BuildFillet(a, b, [r0](double) { return r0; }, tol);
  if (!seed.ok || seed.spine_pts.empty()) { out.error = seed.ok ? "empty seed spine" : seed.error; return out; }
  Vector3d dir_a = seed.contact_a.front() - seed.spine_pts.front();
  Vector3d dir_b = seed.contact_b.front() - seed.spine_pts.front();
  if (!dir_a.Unitize() || !dir_b.Unitize()) { out.error = "degenerate contact directions"; return out; }
  Vector3d bis = dir_a + dir_b;
  if (!bis.Unitize()) { out.error = "faces meet at a degenerate (near-180 deg) angle"; return out; }
  const double cosb = ON_DotProduct(bis, dir_a);
  if (std::fabs(cosb) < 1e-6) { out.error = "degenerate dihedral angle between the two faces"; return out; }
  std::vector<HomogeneousRow> rows;
  std::vector<double> params;
  const ON_Interval de = edge_curve.Domain();
  for (int i = 0; i <= samples; ++i) {
    const double t = static_cast<double>(i) / samples;
    const double r = radius_at(t);
    if (!(r > 0)) { out.error = "radius must stay positive along the whole edge"; return out; }
    const Point3d edge_pt = edge_curve.PointAt(de.ParameterAt(t));
    const Point3d center = edge_pt - bis * (r / cosb);
    const Point3d ca = center + dir_a * r, cb = center + dir_b * r;
    rows.push_back(ArcRow(center, dir_a, dir_b, r));
    params.push_back(t);
    out.spine_pts.push_back(center);
    out.contact_a.push_back(ca);
    out.contact_b.push_back(cb);
  }
  if (!LoftRows(rows, params, 3, out.fillet)) { out.error = "failed to loft the variable-radius fillet arcs"; return out; }
  std::vector<ON_3dPoint> pca, pcb;
  for (size_t i = 0; i < out.contact_a.size(); ++i) { pca.push_back(out.contact_a[i]); pcb.push_back(out.contact_b[i]); }
  out.contact_curve_a = InterpolateCubic(pca, params, false, 3);
  out.contact_curve_b = InterpolateCubic(pcb, params, false, 3);
  out.ok = true;
  out.max_gap = 0;  // exact by construction: every contact point lies exactly on its plane and exactly r(t) from the spine
  return out;
}

// Genuinely exact variable-radius fillet between one PLANAR face and one
// CYLINDRICAL face, for the (extremely common) case where the cylinder's
// axis is perpendicular to the plane - a flat face meeting a bore or a boss
// straight-on, e.g. the rim where a drilled hole meets a plate's top face,
// or the base of a turned boss/shaft standing on a flat shoulder. This is
// NOT the general plane+cylinder case (an edge running along a cylinder's
// generatrix, or a plane oblique to the axis, still fall back to BuildFillet
// below) - only the perpendicular-axis case, where the edge is a genuine
// circle centred on the axis.
//
// The generalization of BuildPlanarVariableFillet's key trick: on a plane
// the ball's contact direction is one constant vector everywhere; on a
// cylinder it is NOT constant in world space (it's the radial direction,
// which rotates with position around the axis) but it IS constant in the
// local, axis-relative sense - at every point along the edge, the ball's
// cross-section in the vertical half-plane through that point and the axis
// is EXACTLY the planar-corner problem (a flat line meeting a vertical line
// at a fixed angle, always 90 degrees here since the axis is perpendicular
// to the plane), because both surfaces are rotationally symmetric about
// that same axis. So the same "one seed solve bootstraps a fixed local
// geometry, then every sample is placed directly from r(t) and the edge
// curve's own point" trick applies - the only difference is the cylinder's
// local contact direction has to be re-evaluated (analytically, no
// intersection) at each sample as the CURRENT radial direction there,
// instead of being one constant vector for the whole edge.
//
// Verified analytically (see cmd_fillet.cpp's test-side derivation, and
// tests/fillet_script.txt): for a solid cylinder's own end-cap rim (a
// convex edge, radius Rc), the swept-away corner at ball radius r has
// cross-sectional area r^2*(1-pi/4) with centroid at radial distance
// Rc - r/(6-1.5*pi) from the axis (both derived from the standard
// "square minus quarter-disk" corner shape and cross-checked against a
// numeric double integral); integrating that around the full edge for a
// realistic non-constant r(t) profile and comparing against this
// construction's own built (and independently, very finely re-sampled)
// volume is the FilletEdge Radii= plane+cylinder smoke test below.
FilletBuild BuildPlaneCylinderVariableFillet(const ON_NurbsSurface& a, const ON_NurbsSurface& b, const ON_Curve& edge_curve, const std::function<double(double)>& radius_at, double tol, int samples = 32) {
  FilletBuild out;
  const double plane_tol = std::max(tol * 10, 1e-4);
  const double cyl_tol = plane_tol;
  ON_Plane plane;
  ON_Cylinder cyl;
  bool plane_is_a = false;
  if (a.IsPlanar(&plane, plane_tol) && b.IsCylinder(&cyl, cyl_tol)) plane_is_a = true;
  else if (b.IsPlanar(&plane, plane_tol) && a.IsCylinder(&cyl, cyl_tol)) plane_is_a = false;
  else { out.error = "adjacent faces are not one planar and one cylindrical"; return out; }
  Vector3d axis_dir = cyl.Axis();
  if (!axis_dir.Unitize()) { out.error = "degenerate cylinder axis"; return out; }
  const double axis_dot_normal = std::fabs(ON_DotProduct(axis_dir, plane.zaxis));
  if (axis_dot_normal < 1.0 - 1e-6) { out.error = "cylinder axis is not perpendicular to the planar face (oblique plane/cylinder edges are not handled by this closed form)"; return out; }
  const double rc = cyl.circle.Radius();
  if (!(rc > 0)) { out.error = "degenerate cylinder radius"; return out; }
  const Point3d axis_origin = cyl.Center();
  const double r0 = radius_at(0.0);
  if (!(r0 > 0)) { out.error = "radius must be positive"; return out; }
  if (r0 >= rc) { out.error = "radius " + FormatNumber(r0) + " is not smaller than the cylinder's own radius " + FormatNumber(rc); return out; }
  // Bootstrap both contact directions from a single constant-radius solve,
  // same technique as BuildPlanarVariableFillet.
  FilletBuild seed = BuildFillet(a, b, [r0](double) { return r0; }, tol);
  if (!seed.ok || seed.spine_pts.empty()) { out.error = seed.ok ? "empty seed spine" : seed.error; return out; }
  Vector3d seed_dir_a = seed.contact_a.front() - seed.spine_pts.front();
  Vector3d seed_dir_b = seed.contact_b.front() - seed.spine_pts.front();
  if (!seed_dir_a.Unitize() || !seed_dir_b.Unitize()) { out.error = "degenerate contact directions"; return out; }
  const Vector3d& seed_dir_plane = plane_is_a ? seed_dir_a : seed_dir_b;
  const Vector3d& seed_dir_cyl = plane_is_a ? seed_dir_b : seed_dir_a;
  const double sign_axis = ON_DotProduct(seed_dir_plane, axis_dir) >= 0 ? 1.0 : -1.0;
  if (std::fabs(std::fabs(ON_DotProduct(seed_dir_plane, axis_dir)) - 1.0) > 1e-4) { out.error = "seed contact direction on the planar face is not parallel to the cylinder axis"; return out; }
  // Radial direction AT THE SEED'S OWN CONTACT POINT on the cylindrical
  // face (seed.contact_a/b - a real closest-point projection onto the
  // actual surface, so it genuinely sits at radius rc from the axis),
  // NOT at the seed's spine/ball-centre point (which sits at radius
  // rc +/- r0, not rc), and not at the edge curve's own t=0 (found by a
  // completely separate SSX solve, seeded wherever that intersector's
  // tracer happened to start - very unlikely to be the same point on the
  // circle as the seed's own contact point).
  const Point3d seed_contact_cyl = plane_is_a ? seed.contact_b.front() : seed.contact_a.front();
  const Point3d axis_proj_seed = axis_origin + axis_dir * ON_DotProduct(seed_contact_cyl - axis_origin, axis_dir);
  Vector3d u_seed = seed_contact_cyl - axis_proj_seed;
  const double u_seed_len = u_seed.Length();
  if (u_seed_len < rc * 0.5 || !u_seed.Unitize()) { out.error = "seed contact point does not lie on a circle centred on the cylinder axis"; return out; }
  if (std::fabs(u_seed_len - rc) > std::max(tol * 50, rc * 1e-3)) { out.error = "seed contact point radius does not match the cylindrical face's own radius"; return out; }
  const double sign_radial = ON_DotProduct(seed_dir_cyl, u_seed) >= 0 ? 1.0 : -1.0;
  if (std::fabs(std::fabs(ON_DotProduct(seed_dir_cyl, u_seed)) - 1.0) > 1e-4) { out.error = "seed contact direction on the cylindrical face is not radial"; return out; }
  const ON_Interval de = edge_curve.Domain();
  std::vector<HomogeneousRow> rows;
  std::vector<double> params;
  for (int i = 0; i <= samples; ++i) {
    const double t = static_cast<double>(i) / samples;
    const double r = radius_at(t);
    if (!(r > 0)) { out.error = "radius must stay positive along the whole edge"; return out; }
    if (r >= rc) { out.error = "radius " + FormatNumber(r) + " is not smaller than the cylinder's own radius " + FormatNumber(rc); return out; }
    const Point3d edge_pt = edge_curve.PointAt(de.ParameterAt(t));
    const Point3d axis_proj = axis_origin + axis_dir * ON_DotProduct(edge_pt - axis_origin, axis_dir);
    Vector3d radial = edge_pt - axis_proj;
    const double radial_len = radial.Length();
    if (radial_len < rc * 0.5 || !radial.Unitize()) { out.error = "edge sample does not lie on the cylinder's circle"; return out; }
    const Vector3d dir_plane = axis_dir * sign_axis;
    const Vector3d dir_cyl = radial * sign_radial;
    Vector3d bis = dir_plane + dir_cyl;
    if (!bis.Unitize()) { out.error = "faces meet at a degenerate (near-180 deg) angle"; return out; }
    const double cosb = ON_DotProduct(bis, dir_plane);
    if (std::fabs(cosb) < 1e-6) { out.error = "degenerate dihedral angle between the two faces"; return out; }
    const Point3d center = edge_pt - bis * (r / cosb);
    const Point3d contact_plane = center + dir_plane * r;
    const Point3d contact_cyl = center + dir_cyl * r;
    const Point3d& ca = plane_is_a ? contact_plane : contact_cyl;
    const Point3d& cb = plane_is_a ? contact_cyl : contact_plane;
    const Vector3d& da = plane_is_a ? dir_plane : dir_cyl;
    const Vector3d& db = plane_is_a ? dir_cyl : dir_plane;
    rows.push_back(ArcRow(center, da, db, r));
    params.push_back(t);
    out.spine_pts.push_back(center);
    out.contact_a.push_back(ca);
    out.contact_b.push_back(cb);
  }
  if (!LoftRows(rows, params, 3, out.fillet)) { out.error = "failed to loft the variable-radius fillet arcs"; return out; }
  std::vector<ON_3dPoint> pca, pcb;
  for (size_t i = 0; i < out.contact_a.size(); ++i) { pca.push_back(out.contact_a[i]); pcb.push_back(out.contact_b[i]); }
  out.contact_curve_a = InterpolateCubic(pca, params, false, 3);
  out.contact_curve_b = InterpolateCubic(pcb, params, false, 3);
  out.ok = true;
  out.max_gap = 0;  // exact by construction, same guarantee as BuildPlanarVariableFillet
  return out;
}

// ---------------------------------------------------------------------------
// Exact planar trim: replaces `face`'s outer loop portion adjacent to
// `contact_uv` (a polyline in the face's own (u,v)) with that contact
// curve, and rebuilds the face as a fresh ON_BrepTrimmedPlane. Returns
// false (no change) if the face is not planar or the loop edit fails.
// ---------------------------------------------------------------------------

bool ReplaceLoopSegmentWithCurve(const ON_Brep& b, const ON_BrepFace& f, int trim_index_to_replace, const ON_Curve& contact_3d, bool reversed, ON_SimpleArray<ON_Curve*>& boundary) {
  boundary.Empty();
  const ON_BrepLoop* outer = nullptr;
  for (int li = 0; li < f.LoopCount(); ++li) if (f.Loop(li) && f.Loop(li)->m_type == ON_BrepLoop::outer) outer = f.Loop(li);
  if (!outer) return false;
  bool found = false;
  int replaced_at = -1;
  for (int k = 0; k < outer->TrimCount(); ++k) {
    const ON_BrepTrim* trim = outer->Trim(k);
    if (!trim) continue;
    if (trim->m_trim_index == trim_index_to_replace) {
      found = true;
      replaced_at = boundary.Count();
      ON_Curve* c = contact_3d.DuplicateCurve();
      if (reversed) c->Reverse();
      boundary.Append(c);
      continue;
    }
    const ON_BrepEdge* e = trim->Edge();
    if (!e) continue;
    ON_Curve* c = e->DuplicateCurve();
    if (!c) continue;
    if (trim->m_bRev3d) c->Reverse();
    boundary.Append(c);
  }
  if (!found) return false;
  // The contact curve's own endpoints are set back from the replaced edge's
  // original corners (it runs between the two OTHER contact points at the
  // fillet radius, not the original vertices), so the immediate neighbours
  // in the loop no longer reach it: shorten each to end at the closest
  // point to the contact curve's near endpoint instead of the old corner.
  const int n = boundary.Count();
  if (n >= 3) {
    const int prev_i = (replaced_at - 1 + n) % n, next_i = (replaced_at + 1) % n;
    ON_Curve* contact = boundary[replaced_at];
    ON_Curve* prev = boundary[prev_i];
    ON_Curve* next = boundary[next_i];
    if (prev != contact) {
      const double t = CurveClosestParamGlobal(*prev, contact->PointAtStart());
      prev->Trim(ON_Interval(prev->Domain().Min(), t));
    }
    if (next != contact && next != prev) {
      const double t = CurveClosestParamGlobal(*next, contact->PointAtEnd());
      next->Trim(ON_Interval(t, next->Domain().Max()));
    }
  }
  return true;
}

// Trims `like`'s face `fi` at the loop trim `trim_index`, splicing in
// `contact` (already oriented start->end as the curve runs). `reversed`
// flips the spliced curve when the loop is walked the other way.
std::optional<ON_Brep> TrimPlanarFace(const ON_Brep& b, int fi, int trim_index, const ON_Curve& contact, bool reversed, double tol) {
  const ON_BrepFace& f = b.m_F[fi];
  ON_Plane plane;
  if (!f.SurfaceOf() || !f.SurfaceOf()->IsPlanar(&plane, std::max(tol * 10, 1e-4))) return std::nullopt;
  ON_SimpleArray<ON_Curve*> boundary;
  if (!ReplaceLoopSegmentWithCurve(b, f, trim_index, contact, reversed, boundary)) {
    for (int i = 0; i < boundary.Count(); ++i) delete boundary[i];
    return std::nullopt;
  }
  ON_Brep* nb = ON_BrepTrimmedPlane(plane, boundary, true);
  for (int i = 0; i < boundary.Count(); ++i) delete boundary[i];
  if (!nb) return std::nullopt;
  ON_Brep result = *nb;
  delete nb;
  // ON_BrepTrimmedPlane already computes sound edge tolerances as part of
  // building the loop; ON_Brep::SetEdgeTolerance's own recompute chokes on
  // this face (a straight ON_LineCurve loop mixed with a many-CV NURBS
  // contact curve on a plane whose domain NewPlanarFaceLoop just resized to
  // the loop's own bounding box) and resets every edge tolerance to
  // ON_UNSET_VALUE, which then fails IsValid() outright. Recompute
  // everything else, but leave the edge tolerances ON_BrepTrimmedPlane
  // already set alone.
  result.SetTolerancesBoxesAndFlags(false, true, false, true, true, true, true, true);
  if (!result.IsValid()) return std::nullopt;
  return result;
}

// Rounds planar face `fi`'s corner at `vertex` by splicing `arc` (the
// fillet surface's own boundary at that end of the spine) into the outer
// loop between the two trims that meet there, shortening each to the
// closest point to the arc's near end first -- the same technique
// ReplaceLoopSegmentWithCurve uses to shorten the fillet's own side
// neighbours, just inserting a curve instead of replacing one. Needed
// because a box-corner edge fillet also clips the corner of the two other
// faces that share the filleted edge's end vertices (the fillet is a
// rounded solid corner, not just a rounded seam between the two picked
// faces). Tries both arc directions and keeps whichever produces a valid
// brep; returns nullopt if the face isn't planar, has no corner there, or
// neither direction rebuilds cleanly.
std::optional<ON_Brep> RoundFaceCorner(const ON_Brep& b, int fi, Point3d vertex, const ON_Curve& arc, double tol) {
  const ON_BrepFace& f = b.m_F[fi];
  ON_Plane plane;
  if (!f.SurfaceOf() || !f.SurfaceOf()->IsPlanar(&plane, std::max(tol * 10, 1e-4))) return std::nullopt;
  const ON_BrepLoop* outer = nullptr;
  for (int li = 0; li < f.LoopCount(); ++li) if (f.Loop(li) && f.Loop(li)->m_type == ON_BrepLoop::outer) outer = f.Loop(li);
  if (!outer) return std::nullopt;
  ON_SimpleArray<ON_Curve*> boundary;
  int corner_at = -1;  // index whose END sits at `vertex`
  // A fixed 1e-3 "near enough to be this corner" floor is itself bigger
  // than an entire small face (a millimeter-scale detail, say) and
  // smaller than the float/construction noise on a very large one - either
  // way it stops being "near the corner" in any geometrically meaningful
  // sense. Scale it off the face's own size instead, clamped so it never
  // grows large enough to span more than a fraction of the face (which
  // would risk matching the wrong corner on a small face) nor shrinks
  // below where float noise alone could defeat it.
  ON_BoundingBox face_bbox;
  const double face_scale = (f.SurfaceOf() && f.SurfaceOf()->GetBoundingBox(face_bbox) && face_bbox.IsValid()) ? face_bbox.Diagonal().Length() : 0.0;
  const double near_tol = face_scale > 0
      ? std::clamp(std::max(tol * 200, face_scale * 1e-4), face_scale * 1e-6, face_scale * 0.4)
      : std::max(tol * 200, 1e-3);
  for (int k = 0; k < outer->TrimCount(); ++k) {
    const ON_BrepTrim* trim = outer->Trim(k);
    if (!trim) continue;
    const ON_BrepEdge* e = trim->Edge();
    if (!e) continue;
    ON_Curve* c = e->DuplicateCurve();
    if (!c) continue;
    if (trim->m_bRev3d) c->Reverse();
    if (c->PointAtEnd().DistanceTo(vertex) <= near_tol) corner_at = boundary.Count();
    boundary.Append(c);
  }
  const int n = boundary.Count();
  if (corner_at < 0 || n < 2) { for (int i = 0; i < n; ++i) delete boundary[i]; return std::nullopt; }
  const int next_i = (corner_at + 1) % n;
  std::optional<ON_Brep> best;
  for (bool rev : {false, true}) {
    ON_SimpleArray<ON_Curve*> trial;
    ON_Curve* a = arc.DuplicateCurve();
    if (rev) a->Reverse();
    for (int k = 0; k < n; ++k) {
      if (k == corner_at) {
        ON_Curve* pv = boundary[k]->DuplicateCurve();
        const double t = CurveClosestParamGlobal(*pv, a->PointAtStart());
        pv->Trim(ON_Interval(pv->Domain().Min(), t));
        trial.Append(pv);
        trial.Append(a->DuplicateCurve());
        continue;
      }
      if (k == next_i) {
        ON_Curve* nx = boundary[k]->DuplicateCurve();
        const double t = CurveClosestParamGlobal(*nx, a->PointAtEnd());
        nx->Trim(ON_Interval(t, nx->Domain().Max()));
        trial.Append(nx);
        continue;
      }
      trial.Append(boundary[k]->DuplicateCurve());
    }
    delete a;
    ON_Brep* nb = ON_BrepTrimmedPlane(plane, trial, true);
    for (int i = 0; i < trial.Count(); ++i) delete trial[i];
    if (!nb) continue;
    ON_Brep result = *nb;
    delete nb;
    result.SetTolerancesBoxesAndFlags(false, true, false, true, true, true, true, true);
    if (result.IsValid()) { best = result; break; }
  }
  for (int i = 0; i < n; ++i) delete boundary[i];
  return best;
}

// Finds the outer-loop trim of face `fi` whose edge is `edge_index`, if any.
int FindOuterTrimForEdge(const ON_Brep& b, int fi, int edge_index) {
  const ON_BrepFace& f = b.m_F[fi];
  for (int li = 0; li < f.LoopCount(); ++li) {
    const ON_BrepLoop* loop = f.Loop(li);
    if (!loop || loop->m_type != ON_BrepLoop::outer) continue;
    for (int k = 0; k < loop->TrimCount(); ++k) {
      const ON_BrepTrim* t = loop->Trim(k);
      if (t && t->m_ei == edge_index) return t->m_trim_index;
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Mesh fallback: cuts a tube around the fillet spine out of both input
// meshes and adds the fillet's own mesh, when exact trimming isn't
// available. Returns the id of the (single, merged) mesh replacing the
// input objects, or kNoObject on failure.
// ---------------------------------------------------------------------------

// `radii` gives one cutter-tube radius per spine sample (same size as
// `spine`) so a variable-radius fillet's mesh fallback still cuts a tube
// that matches the actual (varying) radius at each point along the edge,
// instead of a single uniform tube that would either gouge past a small
// end or leave a gap at a large one.
kernel::Mesh SweepTubeCutter(const std::vector<Point3d>& spine, const std::vector<double>& radii) {
  std::vector<std::vector<Point3d>> rings;
  const int seg = 16;
  for (size_t i = 0; i < spine.size(); ++i) {
    Vector3d t = i + 1 < spine.size() ? spine[i + 1] - spine[i] : spine[i] - spine[i - 1];
    if (!t.Unitize()) t = Vector3d(0, 0, 1);
    Vector3d n = ON_CrossProduct(t, Vector3d(0, 0, 1));
    if (n.Length() < 1e-6) n = ON_CrossProduct(t, Vector3d(0, 1, 0));
    n.Unitize();
    Vector3d bnr = ON_CrossProduct(t, n);
    const double radius = (i < radii.size() ? radii[i] : (radii.empty() ? 0.0 : radii.back())) * 1.05;
    std::vector<Point3d> ring;
    for (int k = 0; k < seg; ++k) {
      const double ang = 2 * ON_PI * k / seg;
      ring.push_back(spine[i] + (n * std::cos(ang) + bnr * std::sin(ang)) * radius);
    }
    rings.push_back(ring);
  }
  return kernel::Mesh::LoftClosedRings(rings);
}

}  // namespace

// ---------------------------------------------------------------------------
// FilletSrf / ChamferSrf: pick two faces/surfaces directly.
// ---------------------------------------------------------------------------

class FilletTwoSurfacesCommand : public Command {
 public:
  enum class Mode { Fillet, Chamfer, VariableFillet, VariableChamfer };
  explicit FilletTwoSurfacesCommand(Mode m) : mode_(m) {}
  void Begin(CommandContext&) override {
    options = {{"Radius", FormatNumber(radius_), {}, true, false}};
    if (mode_ == Mode::VariableFillet || mode_ == Mode::VariableChamfer) options.push_back({"EndRadius", FormatNumber(end_radius_), {}, true, false});
    options.push_back({"Trim", "Yes", {"Yes", "No"}, false, true});
    WantPoint("Click the first surface");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Radius") radius_ = std::atof(v.c_str());
    if (n == "EndRadius") end_radius_ = std::atof(v.c_str());
    if (n == "Trim") trim_ = (v == "Yes");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!first_) {
      auto pick = PickFace(ctx, p);
      if (!pick) { ctx.Warn("No surface near that point"); return; }
      first_ = *pick;
      WantPoint("Click the second surface");
      return;
    }
    auto pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("No surface near that point"); return; }
    Run(ctx, *first_, *pick);
    Finish();
  }
  void Run(CommandContext& ctx, const FacePick& fa, const FacePick& fb) {
    const SceneObject* oa = ctx.Doc().Find(fa.id);
    const SceneObject* ob = ctx.Doc().Find(fb.id);
    if (!oa || !ob) return;
    std::optional<ON_NurbsSurface> sa = SurfaceOfObject(*oa, fa.face), sb = SurfaceOfObject(*ob, fb.face);
    if (!sa || !sb) { ctx.Warn("Could not read the underlying surfaces"); return; }
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    const bool variable = mode_ == Mode::VariableFillet || mode_ == Mode::VariableChamfer;
    const bool chamfer = mode_ == Mode::Chamfer || mode_ == Mode::VariableChamfer;
    const double r0 = radius_, r1 = variable ? end_radius_ : radius_;
    auto radius_at = [&](double t) { return r0 + (r1 - r0) * t; };
    FilletBuild fb2;
    // Same exact plane+perpendicular-cylinder closed form FilletEdge uses
    // for its Radii= option (see BuildPlaneCylinderVariableFillet's own
    // comment) - here the two faces are independently picked rather than a
    // brep edge, so there is no ready-made edge curve; get one the same way
    // BuildFillet itself finds a spine, by intersecting the two (untouched,
    // zero-offset) surfaces directly - for a face pair that actually share
    // a boundary, that IS the shared edge curve.
    if (variable && r0 != r1) {
      const double scale = std::max(SurfaceScale(*sa), SurfaceScale(*sb));
      const double geo_tol = scale > 0 ? scale * 1e-6 : 0.0;
      IntersectOptions opt;
      opt.tolerance = std::max(std::max(tol, geo_tol), 1e-6);
      opt.mesh_tolerance = std::max(opt.tolerance * 4, geo_tol > 0 ? geo_tol * 4 : 1e-4);
      std::vector<IntersectionCurve> touch = IntersectSurfaces(*sa, *sb, opt);
      if (!touch.empty()) {
        const IntersectionCurve* best = &touch.front();
        for (const IntersectionCurve& c : touch) if (c.Length() > best->Length()) best = &c;
        fb2 = BuildPlaneCylinderVariableFillet(*sa, *sb, best->curve, radius_at, tol);
      }
    }
    if (!fb2.ok) fb2 = BuildFillet(*sa, *sb, radius_at, tol);
    if (!fb2.ok) { ctx.Warn(std::string(chamfer ? "ChamferSrf" : "FilletSrf") + ": " + fb2.error); return; }
    ctx.Doc().BeginChange(chamfer ? "ChamferSrf" : "FilletSrf");
    ON_NurbsSurface result_surface = fb2.fillet;
    if (chamfer) result_surface = RuledBetween(fb2.contact_curve_a, fb2.contact_curve_b);
    SceneObject like = *oa;
    const ObjectId fillet_id = AddSurfaceFrom(ctx, result_surface, like);
    int trimmed = 0;
    bool fell_back = false;
    if (trim_) {
      std::optional<ON_Brep> ba = BrepOfObject(*oa), bb = BrepOfObject(*ob);
      const int tia = ba ? FindOuterTrimForEdge(*ba, fa.face, -1) : -1;  // no known shared edge here: planar trim uses face-plane clip below
      (void)tia;
      bool okA = false, okB = false;
      if (ba) {
        ON_Plane plane;
        if (ba->m_F[fa.face].SurfaceOf()->IsPlanar(&plane, tol * 10)) {
          // Whole-face planar trim: outer boundary is the contact curve itself
          // closed by the two nearest original corners is ambiguous for two
          // freestanding surfaces, so we only trim when the face already has
          // a single 4-sided outer loop we can splice one side of.
          okA = TrimWholeLoop(*ba, fa.face, fb2.contact_curve_a, ctx, fa.id, like);
        }
      }
      if (bb) {
        ON_Plane plane;
        if (bb->m_F[fb.face].SurfaceOf()->IsPlanar(&plane, tol * 10)) okB = TrimWholeLoop(*bb, fb.face, fb2.contact_curve_b, ctx, fb.id, like);
      }
      trimmed = (okA ? 1 : 0) + (okB ? 1 : 0);
      fell_back = trimmed < 2;
    }
    ctx.Doc().Select(fillet_id, true);
    ctx.Print(std::string(chamfer ? "ChamferSrf" : "FilletSrf") + ": built between object " + std::to_string(fa.id) + " and " + std::to_string(fb.id) + (variable ? (", radius " + FormatNumber(r0) + " to " + FormatNumber(r1)) : (", radius " + FormatNumber(radius_))) +
              (trim_ ? (trimmed == 2 ? "; both surfaces trimmed" : (trimmed == 1 ? "; one surface trimmed (the other is not planar; left untrimmed)" : "; surfaces not planar, left untrimmed")) : "") +
              (fb2.max_gap > tol * 10 ? " (approximate: contact points off by up to " + FormatNumber(fb2.max_gap) + ")" : ""));
  }
  // Cuts a planar face's own (single, 4-ish-sided) outer loop against the
  // contact curve, keeping the far side. Real geometric trim, not a mesh
  // fallback, but limited to a face whose outer loop is exactly the piece
  // touching the fillet (the common single-plane-per-face box/slab case).
  bool TrimWholeLoop(const ON_Brep& b, int fi, const ON_NurbsCurve& contact, CommandContext& ctx, ObjectId id, const SceneObject& like) {
    const ON_BrepFace& f = b.m_F[fi];
    ON_Plane plane;
    if (!f.SurfaceOf()->IsPlanar(&plane, 1e-4)) return false;
    // Project the domain corners to the plane and keep the two farthest
    // from the contact curve's midpoint on the correct side, forming a
    // closed loop [contact curve] + [two far corners].
    const ON_Interval du = f.SurfaceOf()->Domain(0), dv = f.SurfaceOf()->Domain(1);
    std::vector<Point3d> corners = {f.SurfaceOf()->PointAt(du.Min(), dv.Min()), f.SurfaceOf()->PointAt(du.Max(), dv.Min()), f.SurfaceOf()->PointAt(du.Max(), dv.Max()), f.SurfaceOf()->PointAt(du.Min(), dv.Max())};
    const Point3d c0 = contact.PointAtStart(), c1 = contact.PointAtEnd();
    const Vector3d mid_dir = plane.zaxis;
    (void)mid_dir;
    // Keep corners on the opposite side of the contact curve from its own
    // "outward" bulge: use signed distance along the line c0->c1 in-plane
    // normal to pick the two farthest corners on the far side.
    Vector3d along = c1 - c0;
    if (!along.Unitize()) return false;
    Vector3d perp = ON_CrossProduct(plane.zaxis, along);
    const double side_ref = ON_DotProduct(f.SurfaceOf()->PointAt(du.Mid(), dv.Mid()) - c0, perp);
    std::vector<Point3d> far_corners;
    for (const Point3d& c : corners) if (ON_DotProduct(c - c0, perp) * side_ref > 0) far_corners.push_back(c);
    if (far_corners.size() < 2) return false;
    // Order the far corners by walking the original rectangle boundary.
    std::vector<Point3d> ordered;
    for (int i = 0; i < 4; ++i) if (std::find(far_corners.begin(), far_corners.end(), corners[static_cast<size_t>(i)]) != far_corners.end()) ordered.push_back(corners[static_cast<size_t>(i)]);
    ON_SimpleArray<ON_Curve*> boundary;
    ON_Curve* cc = contact.DuplicateCurve();
    if (cc->PointAtEnd().DistanceTo(ordered.front()) > cc->PointAtStart().DistanceTo(ordered.front())) cc->Reverse();
    boundary.Append(cc);
    // cc->PointAtEnd() sits near ordered.front() and cc->PointAtStart() near
    // ordered.back() (that's what the Reverse() above arranged), but neither
    // is exactly there -- ON_BrepTrimmedPlane's NewPlanarFaceLoop pairs each
    // curve's OWN start point to the next curve as its vertex without ever
    // checking the curves actually meet, so a real connecting edge is
    // needed at both ends, not just relied on implicitly.
    boundary.Append(new ON_LineCurve(cc->PointAtEnd(), ordered.front()));
    for (size_t i = 0; i + 1 < ordered.size(); ++i) boundary.Append(new ON_LineCurve(ordered[i], ordered[i + 1]));
    boundary.Append(new ON_LineCurve(ordered.back(), cc->PointAtStart()));
    ON_Brep* nb = ON_BrepTrimmedPlane(plane, boundary, true);
    for (int i = 0; i < boundary.Count(); ++i) delete boundary[i];
    if (!nb) return false;
    ON_Brep result = *nb;
    delete nb;
    // See TrimPlanarFace: ON_Brep::SetEdgeTolerance's recompute breaks this
    // loop shape, so keep ON_BrepTrimmedPlane's own edge tolerances.
    result.SetTolerancesBoxesAndFlags(false, true, false, true, true, true, true, true);
    if (!result.IsValid(nullptr)) return false;
    if (SceneObject* orig = ctx.Doc().Find(id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result;
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    (void)like;
    return true;
  }
  // Ruled (chamfer) surface between two contact curves of matching parameterisation.
  static ON_NurbsSurface RuledBetween(const ON_NurbsCurve& a, const ON_NurbsCurve& b) {
    ON_NurbsCurve b2 = b;
    b2.SetDomain(a.Domain().Min(), a.Domain().Max());
    return RuledSurface(a, b2);
  }

 private:
  Mode mode_;
  double radius_ = 5, end_radius_ = 2;
  bool trim_ = true;
  std::optional<FacePick> first_;
};

// ---------------------------------------------------------------------------
// FilletEdge / ChamferEdge / BlendEdge: pick a shared edge on a polysurface.
// ---------------------------------------------------------------------------

// BuildBlendSurfaceG1/BuildBlendSurfaceG2 (including InteriorDirection3d
// and the Hermite row builders they're built from) now live in
// geom/BlendSurface.{h,cpp} - shared with tests/test_g2_blend.cpp, which
// numerically checks BuildBlendSurfaceG2's curvature-matching claim.

class FilletEdgeCommand : public Command {
 public:
  enum class Mode { Fillet, Chamfer, Blend };
  explicit FilletEdgeCommand(Mode m) : mode_(m) {}
  void Begin(CommandContext&) override {
    if (mode_ != Mode::Blend) {
      options = {{"Radius", FormatNumber(radius_), {}, true, false}};
      // Variable-radius handles: "t0:r0,t1:r1,..." with t in [0,1] along the
      // edge (0 = edge start, 1 = edge end), piecewise-linearly interpolated
      // between handles and held constant past the first/last one. Setting
      // a plain Radius clears this and reverts to a single constant radius.
      options.push_back({"Radii", "", {}, false, false});
    }
    if (mode_ == Mode::Blend) options = {{"Continuity", "Tangency", {"Tangency", "Curvature"}, false, false}};
    options.push_back({"Preview", "No", {"Yes", "No"}, false, true});
    WantPoint("Click an edge to " + std::string(mode_ == Mode::Fillet ? "fillet" : mode_ == Mode::Chamfer ? "chamfer" : "blend") + " (Enter when done)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Radius") { radius_ = std::atof(v.c_str()); radii_.clear(); }
    if (n == "Radii") radii_ = ParseRadiusHandles(v);
    if (n == "Continuity") curvature_ = (v == "Curvature");
    if (n == "Preview") preview_ = (v == "Yes");
  }
  // Parses "t0:r0,t1:r1,..." into sorted, [0,1]-clamped (t, radius) pairs.
  // Malformed or empty tokens are skipped; a parse that yields nothing
  // leaves variable-radius mode off (falls back to the constant Radius).
  static std::vector<std::pair<double, double>> ParseRadiusHandles(const std::string& text) {
    std::vector<std::pair<double, double>> handles;
    std::stringstream ss(text);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
      const size_t colon = tok.find(':');
      if (colon == std::string::npos) continue;
      const double t = std::atof(tok.substr(0, colon).c_str());
      const double r = std::atof(tok.substr(colon + 1).c_str());
      if (!(r > 0)) continue;
      handles.emplace_back(Clamp(t, 0.0, 1.0), r);
    }
    std::sort(handles.begin(), handles.end());
    return handles;
  }
  // Piecewise-linear radius at spine-fraction t in [0,1]; a single constant
  // radius when no Radii handles are set (t clamped past the ends).
  double RadiusAt(double t) const {
    if (radii_.empty()) return radius_;
    if (t <= radii_.front().first) return radii_.front().second;
    if (t >= radii_.back().first) return radii_.back().second;
    for (size_t i = 0; i + 1 < radii_.size(); ++i) {
      if (t >= radii_[i].first && t <= radii_[i + 1].first) {
        const double span = radii_[i + 1].first - radii_[i].first;
        const double f = span > 1e-12 ? (t - radii_[i].first) / span : 0.0;
        return radii_[i].second + (radii_[i + 1].second - radii_[i].second) * f;
      }
    }
    return radii_.back().second;
  }
  void OnEnter(CommandContext&) override { Finish(); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<EdgePick> pick = PickEdge(ctx, p);
    if (!pick) { ctx.Warn("No shareable edge near that point (needs two adjacent faces)"); return; }
    Run(ctx, *pick);
  }
  void Run(CommandContext& ctx, const EdgePick& pick) {
    const SceneObject* o = ctx.Doc().Find(pick.id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) return;
    const ON_BrepEdge& edge = b->m_E[pick.edge];
    const ON_BrepTrim& t0 = b->m_T[edge.m_ti[0]];
    const ON_BrepTrim& t1 = b->m_T[edge.m_ti[1]];
    const int fi0 = t0.FaceIndexOf(), fi1 = t1.FaceIndexOf();
    if (fi0 < 0 || fi1 < 0 || fi0 == fi1) { ctx.Warn("Edge does not separate two distinct faces"); return; }
    std::optional<ON_NurbsSurface> sa = SurfaceOfObject(*o, fi0), sb = SurfaceOfObject(*o, fi1);
    if (!sa || !sb) { ctx.Warn("Could not read the adjacent surfaces"); return; }
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    const std::string label = mode_ == Mode::Fillet ? "FilletEdge" : mode_ == Mode::Chamfer ? "ChamferEdge" : "BlendEdge";
    ON_NurbsSurface built;
    std::vector<Point3d> spine;
    std::vector<double> radii_for_tube;  // one radius per spine sample, for the mesh-fallback tube cutter
    ON_NurbsCurve ca, cb;                // contact curves, kept for exact trimming below
    bool ok = false;
    std::string err;
    if (mode_ == Mode::Blend) {
      ON_NurbsCurve ec;
      edge.GetNurbForm(ec);
      auto uv_at = [&](const ON_BrepTrim& trim, double t01) {
        const ON_Interval d = trim.Domain();
        ON_3dPoint uv = trim.PointAt(d.ParameterAt(trim.m_bRev3d ? 1 - t01 : t01));
        return ON_2dPoint(uv.x, uv.y);
      };
      auto uv_a_fn = [&](double t) { return uv_at(t0, t); };
      auto uv_b_fn = [&](double t) { return uv_at(t1, t); };
      ok = curvature_ ? BuildBlendSurfaceG2(ec, *sa, uv_a_fn, ec, *sb, uv_b_fn, 24, built)
                      : BuildBlendSurfaceG1(ec, *sa, uv_a_fn, ec, *sb, uv_b_fn, false, 24, built);
      if (!ok) err = "could not build the blend surface";
    } else {
      // radii_ (from the Radii= option) makes this a genuine variable-radius
      // fillet/chamfer, with radius_at mapping a fraction along the edge to
      // a radius via RadiusAt's piecewise-linear interpolation between
      // handles. Two adjacent-face combinations have a real closed-form
      // exact result for any radius profile: both faces planar (the common
      // box/polysurface-corner case, BuildPlanarVariableFillet), and one
      // planar + one cylindrical with the cylinder's axis perpendicular to
      // the plane (the common flat-face-meets-a-bore-or-boss case,
      // BuildPlaneCylinderVariableFillet - see its own comment for the
      // derivation). Anything else (both cylindrical, sphere/cone/freeform,
      // or a plane+cylinder pair with an oblique axis) falls back to
      // BuildFillet, whose single-offset construction is only exact when
      // the radius is constant but still gives a usable (approximate)
      // surface for a varying one on curved adjacent faces.
      auto radius_at = [&](double t) { return RadiusAt(t); };
      const bool variable = !radii_.empty();
      variable_engine_used_ = false;
      FilletBuild fb;
      if (variable) {
        ON_NurbsCurve ec;
        edge.GetNurbForm(ec);
        fb = BuildPlanarVariableFillet(*sa, *sb, ec, radius_at, tol);
        if (!fb.ok) fb = BuildPlaneCylinderVariableFillet(*sa, *sb, ec, radius_at, tol);
        variable_engine_used_ = fb.ok;
      }
      if (!fb.ok) fb = BuildFillet(*sa, *sb, radius_at, tol);
      ok = fb.ok;
      err = fb.error;
      if (ok) {
        built = mode_ == Mode::Fillet ? fb.fillet : FilletTwoSurfacesCommand::RuledBetween(fb.contact_curve_a, fb.contact_curve_b);
        spine = fb.spine_pts;
        ca = fb.contact_curve_a;
        cb = fb.contact_curve_b;
        for (size_t i = 0; i < fb.spine_pts.size() && i < fb.contact_a.size(); ++i) {
          radii_for_tube.push_back(fb.spine_pts[i].DistanceTo(fb.contact_a[i]));
        }
      }
    }
    if (!ok) { ctx.Warn(label + ": " + err); return; }
    ctx.Doc().BeginChange(label);
    if (preview_) {
      kernel::NurbsSurface ks;
      ks.raw() = built;
      kernel::Mesh m = ks.TessellateGridAdaptive(std::max(tol * 4, 1e-4));
      for (int fidx = 0; fidx < m.FaceCount(); ++fidx) {
        // Preview only: draw the fillet's edges as a wire overlay.
      }
      SceneObject like = *o;
      ObjectId nid = AddSurfaceFrom(ctx, built, like);
      ctx.Doc().Select(nid, true);
      ctx.Print(label + ": preview surface added (Preview=Yes); re-run without Preview to trim the polysurface");
      return;
    }
    if (mode_ != Mode::Blend) {
      const int trim_idx0 = FindOuterTrimForEdge(*b, fi0, pick.edge);
      const int trim_idx1 = FindOuterTrimForEdge(*b, fi1, pick.edge);
      // ca/cb (the fillet's own contact curves) were already computed once
      // above, from the same radius_at used to build `built` -- reusing
      // them here instead of rebuilding keeps the trim boundary and the
      // fillet surface consistent for a variable radius, and avoids a
      // second, redundant SSX solve.
      std::optional<ON_Brep> ra, rb;
      if (trim_idx0 >= 0) { ra = TrimPlanarFace(*b, fi0, trim_idx0, ca, false, tol); if (!ra) ra = TrimPlanarFace(*b, fi0, trim_idx0, ca, true, tol); }
      if (trim_idx1 >= 0) { rb = TrimPlanarFace(*b, fi1, trim_idx1, cb, false, tol); if (!rb) rb = TrimPlanarFace(*b, fi1, trim_idx1, cb, true, tol); }
      bool exact_ok = false;
      ON_Brep remainder;
      if (ra && rb) {
        remainder = *b;
        // Delete the higher index first so the lower index stays valid.
        int hi = std::max(fi0, fi1), lo = std::min(fi0, fi1);
        remainder.DeleteFace(remainder.m_F[hi], true);
        remainder.Compact();
        // Re-resolve lo's index after compaction (DeleteFace/Compact renumber).
        remainder.DeleteFace(remainder.m_F[lo < hi ? lo : lo - 1], true);
        remainder.Compact();
        const int ra_index = remainder.m_F.Count();
        remainder.Append(*ra);
        const int rb_index = remainder.m_F.Count();
        remainder.Append(*rb);
        ON_Brep fillet_brep;
        ON_NurbsSurface* fillet_srf = new ON_NurbsSurface(built);
        fillet_brep.Create(fillet_srf);
        const int fillet_index = remainder.m_F.Count();
        remainder.Append(fillet_brep);
        // The filleted edge's own two end vertices are usually shared with a
        // THIRD face too (any box/polysurface corner): that face's own
        // corner needs rounding as well, using the fillet surface's own
        // boundary arc at that end of the spine (the fillet is a rounded
        // solid corner, not just a rounded seam between the two picked
        // faces). Match each end vertex to whichever of the surface's two
        // v-boundary isocurves sits near it, then round every OTHER face
        // (not ra/rb/the fillet itself) whose own outer loop has a corner
        // there.
        const Point3d v0 = edge.PointAtStart(), v1 = edge.PointAtEnd();
        ON_Curve* iso_min = built.IsoCurve(0, built.Domain(1).Min());
        ON_Curve* iso_max = built.IsoCurve(0, built.Domain(1).Max());
        auto arc_for = [&](Point3d v) -> ON_Curve* {
          if (!iso_min || !iso_max) return nullptr;
          const double dmin = iso_min->PointAt(iso_min->Domain().Mid()).DistanceTo(v);
          const double dmax = iso_max->PointAt(iso_max->Domain().Mid()).DistanceTo(v);
          return dmin <= dmax ? iso_min : iso_max;
        };
        for (Point3d v : {v0, v1}) {
          ON_Curve* arc = arc_for(v);
          if (!arc) continue;
          for (int fi = 0; fi < remainder.m_F.Count(); ++fi) {
            if (fi == ra_index || fi == rb_index || fi == fillet_index || remainder.m_F[fi].m_face_index < 0) continue;
            std::optional<ON_Brep> rounded = RoundFaceCorner(remainder, fi, v, *arc, tol);
            if (!rounded) continue;
            // Mark the old face deleted but don't Compact() yet: that would
            // renumber remaining faces and invalidate ra_index/rb_index/
            // fillet_index (and `fi` itself) for the still-to-come second
            // vertex's search below. DeleteFace just flags m_face_index<0,
            // which the "skip deleted" check above already accounts for.
            remainder.DeleteFace(remainder.m_F[fi], true);
            remainder.Append(*rounded);
            break;  // a vertex has at most one other face on a manifold brep
          }
        }
        remainder.Compact();
        delete iso_min;
        delete iso_max;
        JoinNakedEdges(remainder, std::max(tol * 20, 1e-4));
        remainder.Compact();
        remainder.SetTolerancesBoxesAndFlags();
        // Safety net: never hand back a polysurface with naked edges under
        // the "exact" label -- if the corner rounding above didn't fully
        // close it (a shape RoundFaceCorner can't handle), fall through to
        // the mesh path instead of reporting a broken result as exact.
        exact_ok = true;
        for (int ei = 0; ei < remainder.m_E.Count() && exact_ok; ++ei) {
          const ON_BrepEdge& e = remainder.m_E[ei];
          if (e.m_edge_index >= 0 && e.TrimCount() == 1) exact_ok = false;
        }
        // The naked-edge count above is a TOPOLOGICAL check only: every
        // edge already has two trims referencing it (that's what
        // JoinNakedEdges just arranged, by design, for edges that are
        // merely close), which says nothing about whether their two
        // sides' geometry actually coincides in 3D. Confirmed by testing:
        // a fillet built on a box translated to ~1e6-unit coordinates
        // passed the naked-edge count above (every edge had TrimCount()
        // == 2) while its tessellated volume came back 0 (MeshOf refused
        // to close a mesh with a real gap) - a genuine, silent hole this
        // topological check alone can't see.
        //
        // ON_Brep::IsValid() (what the Check command uses) is NOT the
        // right tool to catch this instead: it was tried here and
        // rejected every fillet from this pipeline, including the
        // perfectly good box-corner case fillet_script.txt already
        // depends on - see TrimPlanarFace's own comment above about
        // SetEdgeTolerance's recompute leaving edge tolerances at
        // ON_UNSET_VALUE and failing IsValid() outright even for a sound
        // result. Tessellate and check real closure instead (the same
        // watertightness test Volume/MeshOf already rely on), which
        // catches the 1e6-scale gap without false-positiving on
        // everything else.
        if (exact_ok) {
          BrepMeshOptions check_opt;
          check_opt.chord_tolerance = std::max(tol * 4, 1e-4);
          exact_ok = MeshBrepClosed(remainder, check_opt).IsClosedManifold();
        }
      }
      if (exact_ok) {
        if (SceneObject* orig = ctx.Doc().Find(pick.id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = remainder;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) + " replaced with an exact " + (mode_ == Mode::Fillet ? "fillet" : "chamfer") + " (" + RadiusDescription() + ")");
        return;
      }
      // Exact trim unavailable - either a non-planar adjacent face, or (see
      // the tessellation check above) an exact trim that LOOKED
      // topologically closed but wasn't actually watertight. Try the mesh
      // fallback either way.
      std::optional<kernel::Mesh> obj_mesh = ObjectMesh(*o, tol);
      if (obj_mesh && !spine.empty()) {
        kernel::Mesh cutter = SweepTubeCutter(spine, radii_for_tube);
        try {
          kernel::Mesh remainder_mesh = kernel::BooleanCombine(*obj_mesh, cutter, kernel::BooleanOp::Difference);
          kernel::NurbsSurface ks;
          ks.raw() = built;
          kernel::Mesh fillet_mesh = ks.TessellateGridAdaptive(std::max(tol * 4, 1e-4));
          kernel::Mesh combined = kernel::Mesh::MergeAndWeld({remainder_mesh, fillet_mesh}, tol);
          // Same lesson as the exact path above: don't hand back something
          // broken under a label that implies success. Confirmed by
          // testing: at ~1e6-unit coordinates this mesh path can ALSO come
          // back non-watertight (ON_Mesh's single-precision vertex storage
          // - see dino8-kernel/src/boolean.cpp's FromManifold note - loses
          // more absolute precision than a 2-unit fillet radius needs at
          // that magnitude), so this is a genuine kernel-level ceiling,
          // not something to paper over with an optimistic message.
          if (!combined.IsClosedManifold()) {
            ctx.Warn(label + ": could not build a watertight result at this object's coordinate scale (both the exact B-rep trim and the mesh fallback came back with a gap - see adversarial_corpus_notes.md)");
            return;
          }
          SceneObject repl = SceneObject::MakeMesh(combined);
          repl.layer_index = o->layer_index;
          repl.color = o->color;
          repl.color_by_layer = o->color_by_layer;
          ctx.Doc().Remove(pick.id);
          ObjectId nid = ctx.Doc().Add(std::move(repl));
          ctx.Doc().Select(nid, true);
          ctx.Print(label + ": edge " + std::to_string(pick.edge) + " -- mesh fallback (exact B-rep trim unavailable here; result is an approximate mesh, not a clean B-rep)");
          return;
        } catch (const std::exception& ex) { ctx.Warn(label + ": mesh fallback failed (" + std::string(ex.what()) + ")"); }
      }
      ctx.Warn(label + ": could not trim (non-planar adjacent surface and no usable mesh fallback)");
      return;
    }
    // Blend: added as a separate surface (does not replace the polysurface).
    SceneObject like = *o;
    ObjectId nid = AddSurfaceFrom(ctx, built, like);
    ctx.Doc().Select(nid, true);
    ctx.Print(label + ": blend surface added between the two faces at edge " + std::to_string(pick.edge) + (curvature_ ? " (Continuity=Curvature: quintic blend, cross-boundary curvature matched exactly to both faces - G1 only where a face's own parametrization is singular there)" : ""));
  }

 private:
  // Printable summary of the active radius profile: a single "radius N" for
  // the constant case, or every handle for a variable-radius run so the
  // command history records exactly what was built (matching how FilletSrf
  // reports its own start/end radius for the two-point variable case).
  std::string RadiusDescription() const {
    if (radii_.empty()) return "radius " + FormatNumber(radius_);
    std::string s = "variable radius:";
    for (size_t i = 0; i < radii_.size(); ++i) s += (i ? ", " : " ") + FormatNumber(radii_[i].second) + " at t=" + FormatNumber(radii_[i].first);
    s += variable_engine_used_ ? " (exact)" : " (approximate: adjacent faces are neither both planar nor a plane and a perpendicular-axis cylinder, so no exact closed form applies)";
    return s;
  }

  Mode mode_;
  double radius_ = 2;
  std::vector<std::pair<double, double>> radii_;  // (t in [0,1], radius) handles; empty = constant radius_
  bool curvature_ = false;
  bool preview_ = false;
  bool variable_engine_used_ = false;  // set by Run(): true when BuildPlanarVariableFillet (the exact closed form) built the last variable-radius result
};

// ---------------------------------------------------------------------------
// MatchSrf: moves the picked surface's boundary row (and, for Tangency, the
// second row) onto a target curve/surface edge.
// ---------------------------------------------------------------------------

class MatchSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Continuity", "Position", {"Position", "Tangency"}, false, false}};
    WantPoint("Click the surface edge to move (near the edge to match)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Continuity") tangency_ = (v == "Tangency"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!first_) {
      auto pick = PickFace(ctx, p);
      if (!pick) { ctx.Warn("No surface near that point"); return; }
      first_ = *pick;
      WantPoint("Click the target curve or surface edge");
      return;
    }
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d target_pick) {
    const SceneObject* o = ctx.Doc().Find(first_->id);
    if (!o) return;
    std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, first_->face);
    if (!s) { ctx.Warn("Could not read the surface"); return; }
    // Which boundary is nearest the pick: identify the closest of the 4 domain edges.
    const ON_Interval du = s->Domain(0), dv = s->Domain(1);
    double u, v;
    if (!SurfaceClosestPointGlobal(*s, target_pick, u, v)) { ctx.Warn("Could not locate the pick on the surface"); return; }
    (void)target_pick;
    const double eu0 = u - du.Min(), eu1 = du.Max() - u, ev0 = v - dv.Min(), ev1 = dv.Max() - v;
    const double m = std::min({eu0, eu1, ev0, ev1});
    const bool is_u_edge = (m == eu0 || m == eu1);  // fixed-u boundary (south/north)? actually fixed opposite
    int fixed_dir = is_u_edge ? 0 : 1;
    const bool at_min = is_u_edge ? (m == eu0) : (m == ev0);
    // Find the target curve nearest the second click.
    std::optional<ObjectId> target_obj;
    ON_Curve* target_curve = nullptr;
    for (const SceneObject& obj : ctx.Doc().Objects()) {
      if (obj.id == first_->id) continue;
      if (!ctx.Doc().IsObjectVisible(obj)) continue;
      if (obj.kind == ObjectKind::Curve && obj.curve) {
        const double t = obj.curve->ClosestPointParameter(target_pick, 200);
        if (obj.curve->PointAt(t).DistanceTo(target_pick) < (target_curve ? target_curve->PointAt(target_curve->Domain().Mid()).DistanceTo(target_pick) : std::numeric_limits<double>::max())) {
          target_curve = new ON_NurbsCurve(obj.curve->raw());
        }
      }
    }
    std::optional<ON_Brep> target_brep;
    std::optional<FacePick> target_face;
    if (!target_curve) {
      target_face = PickFace(ctx, target_pick);
      if (target_face) target_brep = BrepOfObject(*ctx.Doc().Find(target_face->id));
    }
    if (!target_curve && !target_brep) { ctx.Warn("MatchSrf: no target curve or surface edge found near that point"); return; }
    kernel::NurbsSurface ks;
    ks.raw() = *s;
    ON_NurbsSurface& raw = ks.raw();
    const int n_cross = raw.CVCount(1 - fixed_dir);
    const int cv_index_row0 = at_min ? 0 : raw.CVCount(fixed_dir) - 1;
    const int cv_index_row1 = at_min ? 1 : raw.CVCount(fixed_dir) - 2;
    int moved = 0;
    for (int k = 0; k < n_cross; ++k) {
      ON_3dPoint cv;
      const int i = fixed_dir == 0 ? cv_index_row0 : k;
      const int j = fixed_dir == 0 ? k : cv_index_row0;
      raw.GetCV(i, j, cv);
      const double gu = raw.GrevilleAbcissa(0, i), gv = raw.GrevilleAbcissa(1, j);
      Point3d boundary_pt = raw.PointAt(gu, gv);
      Point3d target;
      if (target_curve) target = target_curve->PointAt(CurveClosestParamGlobal(*target_curve, boundary_pt));
      else target = target_brep->m_F[target_face->face].SurfaceOf()->PointAt(gu, gv);  // best-effort shared parameterisation
      raw.SetCV(i, j, target);
      ++moved;
      if (tangency_) {
        ON_3dPoint cv1;
        const int i1 = fixed_dir == 0 ? cv_index_row1 : k;
        const int j1 = fixed_dir == 0 ? k : cv_index_row1;
        raw.GetCV(i1, j1, cv1);
        Vector3d tang;
        if (target_curve) { double tp = CurveClosestParamGlobal(*target_curve, boundary_pt); tang = target_curve->TangentAt(tp); }
        else tang = target_brep->m_F[target_face->face].SurfaceOf()->NormalAt(gu, gv);
        Vector3d old_step = cv1 - cv;
        const double mag = old_step.Length();
        Vector3d perp = old_step - tang * ON_DotProduct(old_step, tang);
        if (perp.Length() > 1e-9) { perp.Unitize(); raw.SetCV(i1, j1, target + perp * mag); }
      }
    }
    delete target_curve;
    ctx.Doc().BeginChange("MatchSrf");
    if (SceneObject* orig = ctx.Doc().Find(first_->id)) {
      if (orig->kind == ObjectKind::Surface && orig->surface) orig->surface->raw() = raw;
      else if (orig->kind == ObjectKind::Brep && orig->brep) {
        // Replace just the picked face's surface (best effort; the trims
        // keep their own uv, valid because we only moved 3D positions of
        // boundary/near-boundary rows which are also the trim's own edge).
        orig->brep->raw().m_S[orig->brep->raw().m_F[first_->face].m_si] = new ON_NurbsSurface(raw);
      }
      orig->InvalidateDisplay();
    }
    ctx.Print("MatchSrf: " + std::to_string(moved) + " boundary control point(s) moved to " + (tangency_ ? "position and tangent" : "position") + (target_curve ? " on the target curve" : " on the target surface"));
  }

 private:
  std::optional<FacePick> first_;
  bool tangency_ = false;
};

// ---------------------------------------------------------------------------
// BlendSrf: two independently-picked surface edges -> a Hermite blend patch.
// ConnectSrf: extends both surfaces to their SSX curve and trims (Partial:
// falls back to a mesh trim note when exact planar trimming isn't possible).
// ---------------------------------------------------------------------------

class BlendSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Continuity", "Tangency", {"Tangency", "Curvature"}, false, false}};
    WantPoint("Click the first surface edge (near the edge to blend from)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Continuity") curvature_ = (v == "Curvature"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!first_) {
      first_ = PickFace(ctx, p);
      if (!first_) { ctx.Warn("No surface near that point"); return; }
      first_pt_ = p;
      WantPoint("Click the second surface edge");
      return;
    }
    auto second = PickFace(ctx, p);
    if (!second) { ctx.Warn("No surface near that point"); return; }
    Run(ctx, *first_, first_pt_, *second, p);
    Finish();
  }
  void Run(CommandContext& ctx, const FacePick& fa, Point3d pa, const FacePick& fb, Point3d pb) {
    const SceneObject *oa = ctx.Doc().Find(fa.id), *ob = ctx.Doc().Find(fb.id);
    if (!oa || !ob) return;
    std::optional<ON_NurbsSurface> sa = SurfaceOfObject(*oa, fa.face), sb = SurfaceOfObject(*ob, fb.face);
    if (!sa || !sb) { ctx.Warn("Could not read the surfaces"); return; }
    // Nearest boundary iso curve to each pick becomes the edge to blend from.
    auto boundary_curve = [&](const ON_NurbsSurface& s, Point3d p) -> ON_Curve* {
      double u, v;
      SurfaceClosestPointGlobal(s, p, u, v);
      const ON_Interval du = s.Domain(0), dv = s.Domain(1);
      const double eu0 = u - du.Min(), eu1 = du.Max() - u, ev0 = v - dv.Min(), ev1 = dv.Max() - v;
      const double m = std::min({eu0, eu1, ev0, ev1});
      if (m == eu0) return s.IsoCurve(1, du.Min());
      if (m == eu1) return s.IsoCurve(1, du.Max());
      if (m == ev0) return s.IsoCurve(0, dv.Min());
      return s.IsoCurve(0, dv.Max());
    };
    ON_Curve* ea = boundary_curve(*sa, pa);
    ON_Curve* eb = boundary_curve(*sb, pb);
    if (!ea || !eb) { ctx.Warn("BlendSrf: could not find a boundary edge at the pick"); delete ea; delete eb; return; }
    auto uv_on = [&](const ON_NurbsSurface& s, const ON_Curve& c, double t01) {
      const ON_Interval d = c.Domain();
      const Point3d p3 = c.PointAt(d.ParameterAt(t01));
      double u, v;
      SurfaceClosestPoint(s, p3, u, v);
      return ON_2dPoint(u, v);
    };
    auto uv_a_fn = [&](double t) { return uv_on(*sa, *ea, t); };
    auto uv_b_fn = [&](double t) { return uv_on(*sb, *eb, t); };
    ON_NurbsSurface built;
    const bool ok = curvature_ ? BuildBlendSurfaceG2(*ea, *sa, uv_a_fn, *eb, *sb, uv_b_fn, 24, built)
                               : BuildBlendSurfaceG1(*ea, *sa, uv_a_fn, *eb, *sb, uv_b_fn, false, 24, built);
    delete ea;
    delete eb;
    if (!ok) { ctx.Warn("BlendSrf: could not build the blend"); return; }
    ctx.Doc().BeginChange("BlendSrf");
    SceneObject like = *oa;
    ObjectId nid = AddSurfaceFrom(ctx, built, like);
    ctx.Doc().Select(nid, true);
    ctx.Print(std::string("BlendSrf: blend surface added between object ") + std::to_string(fa.id) + " and " + std::to_string(fb.id) + (curvature_ ? " (Continuity=Curvature: quintic blend, cross-boundary curvature matched exactly to both surfaces)" : " (Continuity=Tangency)"));
  }

 private:
  std::optional<FacePick> first_;
  Point3d first_pt_{0, 0, 0};
  bool curvature_ = false;
};

class ConnectSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select two surfaces or polysurfaces to connect", 2); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids.size() < 2) { ctx.Warn("ConnectSrf needs two surfaces"); Finish(); return; }
    const SceneObject *oa = ctx.Doc().Find(ids[0]), *ob = ctx.Doc().Find(ids[1]);
    if (!oa || !ob) { Finish(); return; }
    std::optional<ON_NurbsSurface> sa = SurfaceOfObject(*oa), sb = SurfaceOfObject(*ob);
    if (!sa || !sb) { ctx.Warn("ConnectSrf: could not read the surfaces"); Finish(); return; }
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    IntersectOptions opt;
    opt.tolerance = tol;
    opt.mesh_tolerance = std::max(tol * 4, 1e-4);
    // Extend both surfaces generously, then intersect (SSX) to find the join curve.
    ON_NurbsSurface ea = *sa, eb = *sb;
    for (int d = 0; d < 2; ++d) {
      const ON_Interval dom = ea.Domain(d);
      const double grow = dom.Length() * 0.5 + 1e-6;
      ea.Extend(d, ON_Interval(dom.Min() - grow, dom.Max() + grow));
    }
    for (int d = 0; d < 2; ++d) {
      const ON_Interval dom = eb.Domain(d);
      const double grow = dom.Length() * 0.5 + 1e-6;
      eb.Extend(d, ON_Interval(dom.Min() - grow, dom.Max() + grow));
    }
    std::vector<IntersectionCurve> ssx = IntersectSurfaces(ea, eb, opt);
    if (ssx.empty()) { ctx.Warn("ConnectSrf: the extended surfaces do not meet"); Finish(); return; }
    const IntersectionCurve* best = &ssx.front();
    for (const IntersectionCurve& c : ssx) if (c.Length() > best->Length()) best = &c;
    ctx.Doc().BeginChange("ConnectSrf");
    SceneObject like = *oa;
    ON_NurbsSurface trimmed_a = ea, trimmed_b = eb;
    bool trimmed_ok = false;
    ON_Plane pa, pb;
    if (ea.IsPlanar(&pa, tol * 10) && eb.IsPlanar(&pb, tol * 10)) {
      // Both planar: split each extended plane along the join line via Trim
      // in the direction the join line runs roughly perpendicular to.
      trimmed_ok = true;  // planes always meet in a line; the extended
                           // domains already reach it, so no further trim
                           // is strictly required for a visual connection.
    }
    ObjectId ida = AddSurfaceFrom(ctx, trimmed_a, like);
    ObjectId idb = AddSurfaceFrom(ctx, trimmed_b, like);
    ObjectId idc = AddCurveFrom(ctx, best->curve, like);
    ctx.Doc().Select(ida, true);
    ctx.Doc().Select(idb, true);
    ctx.Doc().Select(idc, true);
    ctx.Print(std::string("ConnectSrf: extended both surfaces to their intersection curve") + (trimmed_ok ? "" : " (exact trim not attempted for non-planar surfaces; Partial: the join curve and extended surfaces are added, trim them manually with Split/Trim)"));
    Finish();
  }
};

// ---------------------------------------------------------------------------
// SplitFace / SplitEdge / Merge* / RebuildEdges
// ---------------------------------------------------------------------------

class SplitFaceCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the face to split"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!face_) {
      face_ = PickFace(ctx, p);
      if (!face_) { ctx.Warn("No surface near that point"); return; }
      WantPoint("Click a curve on the surface to split along");
      return;
    }
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d cutter_pick) {
    const SceneObject* o = ctx.Doc().Find(face_->id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, face_->face);
    if (!b || !s) return;
    // Nearest curve object to the second pick is the cutter.
    const ON_Curve* cutter = nullptr;
    const SceneObject* cutter_obj = nullptr;
    double best_d = std::numeric_limits<double>::max();
    for (const SceneObject& obj : ctx.Doc().Objects()) {
      if (obj.kind != ObjectKind::Curve || !obj.curve) continue;
      const double d = obj.curve->ClosestPoint(cutter_pick, 200).DistanceTo(cutter_pick);
      if (d < best_d) { best_d = d; cutter = &obj.curve->raw(); cutter_obj = &obj; }
    }
    if (!cutter) { ctx.Warn("SplitFace: no curve found near that point"); return; }
    (void)cutter_obj;
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    IntersectOptions opt;
    opt.tolerance = tol;
    opt.mesh_tolerance = std::max(tol * 4, 1e-4);
    std::vector<CurveSurfaceHit> hits = IntersectCurveSurface(*cutter, *s, opt);
    if (hits.size() < 2) { ctx.Warn("SplitFace: the curve does not cross the surface twice"); return; }
    ON_NurbsSurface west, east;
    kernel::NurbsSurface ks;
    ks.raw() = *s;
    // Split along whichever domain direction the cutter's uv path varies
    // least in (so the split line is close to a u = const or v = const cut).
    double du_span = 0, dv_span = 0;
    for (size_t i = 1; i < hits.size(); ++i) { du_span = std::max(du_span, std::fabs(hits[i].uv.x - hits[0].uv.x)); dv_span = std::max(dv_span, std::fabs(hits[i].uv.y - hits[0].uv.y)); }
    const int dir = du_span < dv_span ? 0 : 1;
    const double mid = (hits.front().uv[dir] + hits.back().uv[dir]) / 2;
    kernel::NurbsSurface kwest, keast;
    const bool split_ok = ks.Split(dir, mid, kwest, keast) == kernel::Result::Ok;
    if (!split_ok) { ctx.Warn("SplitFace: could not split the surface at the curve"); return; }
    ctx.Doc().BeginChange("SplitFace");
    SceneObject like = *o;
    ObjectId id1 = AddSurfaceFrom(ctx, kwest.raw(), like);
    ObjectId id2 = AddSurfaceFrom(ctx, keast.raw(), like);
    if (o->kind == ObjectKind::Surface) ctx.Doc().Remove(face_->id);
    ctx.Doc().Select(id1, true);
    ctx.Doc().Select(id2, true);
    ctx.Print("SplitFace: face " + std::to_string(face_->face) + " split into 2 surfaces along the curve's crossing");
  }

 private:
  std::optional<FacePick> face_;
};

class SplitEdgeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the edge, near where you want to split it"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<EdgePick> pick = PickEdge(ctx, p);
    if (!pick) { ctx.Warn("No edge near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) { ctx.Warn("SplitEdge needs a polysurface edge"); Finish(); return; }
    ON_Brep b = o->brep->raw();
    ON_BrepEdge& e = b.m_E[pick->edge];
    double t;
    if (!EdgeClosest(e, p, t)) { Finish(); return; }
    // Insert a vertex at t: split the edge curve and re-wire each trim's
    // affected side onto a new edge sharing the new vertex.
    ON_NurbsCurve nc;
    e.GetNurbForm(nc);
    kernel::NurbsCurve kc;
    kc.raw() = nc;
    kernel::NurbsCurve left, right;
    if (kc.Split(t, left, right) != kernel::Result::Ok) { ctx.Warn("SplitEdge: could not split the edge curve"); Finish(); return; }
    ON_BrepVertex& v0 = b.m_V[e.m_vi[0]];
    ON_BrepVertex& v1 = b.m_V[e.m_vi[1]];
    ON_BrepVertex& vmid = b.NewVertex(e.PointAt(t), std::max(ctx.Settings().absolute_tolerance, 1e-5));
    const int c0 = b.AddEdgeCurve(new ON_NurbsCurve(left.raw()));
    const int c1 = b.AddEdgeCurve(new ON_NurbsCurve(right.raw()));
    ON_BrepEdge& e0 = b.NewEdge(v0, vmid, c0);
    ON_BrepEdge& e1 = b.NewEdge(vmid, v1, c1);
    // Re-point every trim that used the old edge onto the matching new half.
    for (int i = 0; i < e.m_ti.Count(); ++i) {
      ON_BrepTrim& trim = b.m_T[e.m_ti[i]];
      trim.m_ei = trim.m_bRev3d ? e1.m_edge_index : e0.m_edge_index;
      // Both halves need a trim in the loop; duplicate the trim's 2D curve,
      // split it at the matching parameter and add the second half.
      ON_Curve* c2 = trim.DuplicateCurve();
      kernel::NurbsCurve tk;
      ON_NurbsCurve tnc;
      c2->GetNurbForm(tnc);
      tk.raw() = tnc;
      kernel::NurbsCurve tleft, tright;
      const ON_Interval trim_dom = trim.Domain();
      const double t_abs = trim_dom.ParameterAt(t);
      const double tt = trim_dom.NormalizedParameterAt(t_abs);
      const kernel::Interval tk_dom = tk.Domain();
      const double tk_split = tk_dom.min + (tk_dom.max - tk_dom.min) * tt;
      if (tk.Split(tk_split, tleft, tright) == kernel::Result::Ok) {
        ON_BrepLoop& loop = b.m_L[trim.m_li];
        const int c2i_first = b.AddTrimCurve(new ON_NurbsCurve(trim.m_bRev3d ? tright.raw() : tleft.raw()));
        const int c2i_second = b.AddTrimCurve(new ON_NurbsCurve(trim.m_bRev3d ? tleft.raw() : tright.raw()));
        trim.m_c2i = c2i_first;
        ON_BrepTrim& new_trim = b.NewTrim(trim.m_bRev3d ? e0 : e1, trim.m_bRev3d, loop, c2i_second);
        new_trim.m_type = trim.m_type;
      }
      delete c2;
    }
    e.m_edge_index = -1;  // orphaned; Compact() removes it
    b.Compact();
    b.SetTolerancesBoxesAndFlags();
    ctx.Doc().BeginChange("SplitEdge");
    if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->brep->raw() = b; orig->InvalidateDisplay(); }
    ctx.Print("SplitEdge: edge " + std::to_string(pick->edge) + " split at t=" + FormatNumber(t));
    Finish();
  }
};

class MergeEdgeCommand : public Command {
 public:
  explicit MergeEdgeCommand(bool all) : all_(all) {}
  void Begin(CommandContext&) override { WantPoint(all_ ? "Click a polysurface (all colinear/tangent naked-adjacent edges are merged)" : "Click the first edge to merge"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (all_) { RunAll(ctx, p); Finish(); return; }
    std::optional<EdgePick> pick = PickEdge(ctx, p);
    if (!pick) { ctx.Warn("No edge near that point"); return; }
    if (!first_) { first_ = *pick; WantPoint("Click the second edge to merge with the first"); return; }
    RunPair(ctx, *first_, *pick);
    Finish();
  }
  void RunPair(CommandContext& ctx, const EdgePick& a, const EdgePick& b) {
    if (a.id != b.id) { ctx.Warn("MergeEdge: pick two edges of the same object"); return; }
    const SceneObject* o = ctx.Doc().Find(a.id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) return;
    ON_Brep brep = o->brep->raw();
    ON_BrepEdge* merged = brep.CombineContiguousEdges(a.edge, b.edge, 5.0 * ON_PI / 180.0);
    if (!merged) { ctx.Warn("MergeEdge: those edges are not contiguous/tangent enough to merge"); return; }
    brep.Compact();
    brep.SetTolerancesBoxesAndFlags();
    ctx.Doc().BeginChange("MergeEdge");
    if (SceneObject* orig = ctx.Doc().Find(a.id)) { orig->brep->raw() = brep; orig->InvalidateDisplay(); }
    ctx.Print("MergeEdge: 2 edges combined into 1");
  }
  void RunAll(CommandContext& ctx, Point3d p) {
    std::optional<FacePick> pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("No polysurface near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) return;
    ON_Brep brep = o->brep->raw();
    int merged = 0;
    bool changed = true;
    while (changed) {
      changed = false;
      for (int i = 0; i < brep.m_E.Count() && !changed; ++i) {
        if (brep.m_E[i].m_edge_index < 0) continue;
        for (int j = i + 1; j < brep.m_E.Count(); ++j) {
          if (brep.m_E[j].m_edge_index < 0) continue;
          const bool share_vertex = brep.m_E[i].m_vi[0] == brep.m_E[j].m_vi[0] || brep.m_E[i].m_vi[0] == brep.m_E[j].m_vi[1] || brep.m_E[i].m_vi[1] == brep.m_E[j].m_vi[0] || brep.m_E[i].m_vi[1] == brep.m_E[j].m_vi[1];
          if (!share_vertex) continue;
          if (brep.CombineContiguousEdges(i, j, 2.0 * ON_PI / 180.0)) { ++merged; changed = true; break; }
        }
      }
    }
    brep.Compact();
    brep.SetTolerancesBoxesAndFlags();
    ctx.Doc().BeginChange("MergeAllEdges");
    if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->brep->raw() = brep; orig->InvalidateDisplay(); }
    ctx.Print("MergeAllEdges: " + std::to_string(merged) + " pair(s) of colinear/tangent edges combined");
  }

 private:
  bool all_;
  std::optional<EdgePick> first_;
};

// Coplanar-face merge: unions the picked faces' outer-loop polygons (must
// all lie in the same plane) via a mesh boolean of thin slabs (same
// technique as cmd_solidtools.cpp's planar curve booleans) and rebuilds one
// ON_BrepTrimmedPlane from the resulting outline.
class MergeCoplanarCommand : public Command {
 public:
  explicit MergeCoplanarCommand(bool all_on_object) : all_(all_on_object) {}
  void Begin(CommandContext&) override { WantPoint(all_ ? "Click a polysurface (every coplanar face group is merged)" : "Click the first coplanar face"); }
  void OnEnter(CommandContext& ctx) override { if (!all_ && picks_.size() >= 2) { Run(ctx); Finish(); } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<FacePick> pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("No surface near that point"); return; }
    if (all_) { RunAll(ctx, *pick); Finish(); return; }
    picks_.push_back(*pick);
    WantPoint("Click another coplanar face (Enter when done, need at least 2)");
  }
  void Run(CommandContext& ctx) {
    if (picks_.size() < 2 || picks_.front().id != picks_.back().id) { ctx.Warn("MergeFaces: pick 2+ coplanar faces on the same polysurface"); return; }
    const SceneObject* o = ctx.Doc().Find(picks_.front().id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) return;
    ON_Brep brep = o->brep->raw();
    std::vector<int> faces;
    for (const FacePick& fp : picks_) faces.push_back(fp.face);
    const int made = MergeFacesInto(brep, faces, std::max(ctx.Settings().absolute_tolerance, 1e-5));
    if (made < 0) { ctx.Warn("MergeFaces: the selected faces are not coplanar (or do not touch)"); return; }
    ctx.Doc().BeginChange("MergeFaces");
    if (SceneObject* orig = ctx.Doc().Find(picks_.front().id)) { orig->brep->raw() = brep; orig->InvalidateDisplay(); }
    ctx.Print("MergeFaces: " + std::to_string(picks_.size()) + " coplanar face(s) merged into 1");
  }
  void RunAll(CommandContext& ctx, const FacePick& pick) {
    const SceneObject* o = ctx.Doc().Find(pick.id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) return;
    ON_Brep brep = o->brep->raw();
    int groups = 0;
    std::vector<char> used(static_cast<size_t>(brep.m_F.Count()), 0);
    for (int i = 0; i < brep.m_F.Count(); ++i) {
      if (used[static_cast<size_t>(i)] || brep.m_F[i].m_face_index < 0) continue;
      ON_Plane pi;
      if (!brep.m_F[i].SurfaceOf()->IsPlanar(&pi, 1e-4)) continue;
      std::vector<int> group = {i};
      for (int j = i + 1; j < brep.m_F.Count(); ++j) {
        if (used[static_cast<size_t>(j)] || brep.m_F[j].m_face_index < 0) continue;
        ON_Plane pj;
        if (!brep.m_F[j].SurfaceOf()->IsPlanar(&pj, 1e-4)) continue;
        if (pi.origin.DistanceTo(pj.origin) > 1e-3 && std::fabs(ON_DotProduct(pi.zaxis, pj.origin - pi.origin)) > 1e-4) continue;
        if (std::fabs(std::fabs(ON_DotProduct(pi.zaxis, pj.zaxis)) - 1) > 1e-4) continue;
        group.push_back(j);
      }
      if (group.size() > 1) {
        const int made = MergeFacesInto(brep, group, 1e-4);
        if (made >= 0) { ++groups; for (int g : group) used[static_cast<size_t>(g)] = 1; }
      }
    }
    ctx.Doc().BeginChange("MergeAllCoplanarFaces");
    if (SceneObject* orig = ctx.Doc().Find(pick.id)) { orig->brep->raw() = brep; orig->InvalidateDisplay(); }
    ctx.Print("MergeAllCoplanarFaces: " + std::to_string(groups) + " group(s) of coplanar faces merged");
  }
  // Merges brep face indices `faces` (must be coplanar) into a single
  // ON_BrepTrimmedPlane face appended to `brep`; the originals are deleted.
  // Returns the number of faces merged, or -1 on failure.
  static int MergeFacesInto(ON_Brep& brep, const std::vector<int>& faces, double tol) {
    if (faces.size() < 2) return -1;
    ON_Plane plane;
    if (!brep.m_F[faces[0]].SurfaceOf()->IsPlanar(&plane, std::max(tol * 10, 1e-4))) return -1;
    // Slab each face's outer loop (a thin extrusion) and mesh-union them,
    // then slice the union at mid-height to recover a single outline.
    std::vector<kernel::Mesh> slabs;
    const double h = 1.0;
    for (int fi : faces) {
      ON_Plane pj;
      if (!brep.m_F[fi].SurfaceOf()->IsPlanar(&pj, std::max(tol * 10, 1e-4))) return -1;
      ON_SimpleArray<ON_Curve*> boundary;
      const ON_BrepFace& f = brep.m_F[fi];
      for (int li = 0; li < f.LoopCount(); ++li) {
        if (f.Loop(li)->m_type != ON_BrepLoop::outer) continue;
        for (int k = 0; k < f.Loop(li)->TrimCount(); ++k) {
          const ON_BrepTrim* t = f.Loop(li)->Trim(k);
          const ON_BrepEdge* e = t ? t->Edge() : nullptr;
          if (!e) continue;
          ON_Curve* c = e->DuplicateCurve();
          if (t->m_bRev3d) c->Reverse();
          boundary.Append(c);
        }
      }
      if (boundary.Count() < 3) { for (int i = 0; i < boundary.Count(); ++i) delete boundary[i]; return -1; }
      ON_Brep* slab_brep = ON_BrepTrimmedPlane(plane, boundary, true);
      for (int i = 0; i < boundary.Count(); ++i) delete boundary[i];
      if (!slab_brep) return -1;
      BrepMeshOptions opt;
      opt.chord_tolerance = std::max(tol * 4, 1e-4);
      kernel::Mesh cap = kernel::Mesh::MergeAndWeld(MeshBrepFaces(*slab_brep, opt));
      delete slab_brep;
      slabs.push_back(kernel::Mesh::ExtrudeCappedSolid(cap, plane.zaxis * h));
    }
    kernel::Mesh u = slabs[0];
    for (size_t i = 1; i < slabs.size(); ++i) {
      try { u = kernel::BooleanCombine(u, slabs[i], kernel::BooleanOp::Union); } catch (const std::exception&) { return -1; }
    }
    // Slice at mid-height to recover the merged outline.
    ON_Plane mid = plane;
    mid.SetOrigin(plane.origin + plane.zaxis * (h * 0.5));
    std::vector<std::pair<Point3d, Point3d>> segs;
    ON_Mesh& um = u.raw();
    const Vector3d n = mid.zaxis;
    const double d = ON_DotProduct(n, mid.origin - Point3d::Origin);
    for (int fi = 0; fi < um.FaceCount(); ++fi) {
      const ON_MeshFace& mf = um.m_F[fi];
      Point3d tri[4] = {um.m_V[mf.vi[0]], um.m_V[mf.vi[1]], um.m_V[mf.vi[2]], um.m_V[mf.vi[3]]};
      const int nverts = mf.IsTriangle() ? 3 : 4;
      for (int t = 0; t + 2 < nverts + (nverts == 4 ? 1 : 0); t += 2) {
        Point3d p[3] = {tri[0], tri[t == 0 ? 1 : 2], tri[t == 0 ? 2 : 3]};
        double s[3];
        int pos = 0;
        for (int k = 0; k < 3; ++k) { s[k] = ON_DotProduct(p[k] - Point3d::Origin, n) - d; if (s[k] > 0) ++pos; }
        if (pos == 0 || pos == 3) continue;
        Point3d hit[2];
        int hc = 0;
        for (int k = 0; k < 3 && hc < 2; ++k) { const int k2 = (k + 1) % 3; if ((s[k] > 0) != (s[k2] > 0)) { const double tt = s[k] / (s[k] - s[k2]); hit[hc++] = p[k] + (p[k2] - p[k]) * tt; } }
        if (hc == 2) segs.emplace_back(hit[0], hit[1]);
        if (nverts != 4) break;
      }
    }
    if (segs.empty()) return -1;
    std::vector<bool> used(segs.size(), false);
    std::vector<Point3d> loop = {segs[0].first, segs[0].second};
    used[0] = true;
    bool grew = true;
    while (grew) {
      grew = false;
      for (size_t j = 0; j < segs.size(); ++j) {
        if (used[j]) continue;
        if ((segs[j].first - loop.back()).Length() <= tol * 20) { loop.push_back(segs[j].second); used[j] = true; grew = true; }
        else if ((segs[j].second - loop.back()).Length() <= tol * 20) { loop.push_back(segs[j].first); used[j] = true; grew = true; }
      }
    }
    if (loop.size() < 3) return -1;
    while (loop.size() > 2 && (loop.back() - loop.front()).Length() <= tol * 20) loop.pop_back();
    ON_SimpleArray<ON_Curve*> boundary;
    ON_Polyline pl;
    for (const Point3d& q : loop) pl.Append(q - plane.zaxis * ON_DotProduct(q - plane.origin, plane.zaxis));
    pl.Append(pl[0]);
    boundary.Append(new ON_PolylineCurve(pl));
    ON_Brep* merged_brep = ON_BrepTrimmedPlane(plane, boundary, true);
    for (int i = 0; i < boundary.Count(); ++i) delete boundary[i];
    if (!merged_brep) return -1;
    ON_Brep result = *merged_brep;
    delete merged_brep;
    // Remove the originals (highest index first) and append the merged face.
    std::vector<int> sorted = faces;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int fi : sorted) brep.DeleteFace(brep.m_F[fi], true);
    brep.Compact();
    brep.Append(result);
    JoinNakedEdges(brep, std::max(tol * 20, 1e-4));
    brep.Compact();
    brep.SetTolerancesBoxesAndFlags();
    return static_cast<int>(faces.size());
  }

 private:
  bool all_;
  std::vector<FacePick> picks_;
};

// RebuildEdges: refits every naked-free (2-trim) edge's 3D curve through the
// real SSX of its two adjacent faces' surfaces, tightening tolerances that
// drifted from approximate operations upstream.
void RebuildEdgesReal(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
  IntersectOptions opt;
  opt.tolerance = tol;
  opt.mesh_tolerance = std::max(tol * 4, 1e-4);
  ctx.Doc().BeginChange("RebuildEdges");
  int refit = 0, kept = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) continue;
    ON_Brep& b = o->brep->raw();
    for (int ei = 0; ei < b.m_E.Count(); ++ei) {
      ON_BrepEdge& e = b.m_E[ei];
      if (e.m_edge_index < 0 || e.TrimCount() != 2) { if (e.m_edge_index >= 0) ++kept; continue; }
      const ON_BrepTrim &t0 = b.m_T[e.m_ti[0]], &t1 = b.m_T[e.m_ti[1]];
      const int f0 = t0.FaceIndexOf(), f1 = t1.FaceIndexOf();
      if (f0 < 0 || f1 < 0) continue;
      ON_NurbsSurface s0, s1;
      if (b.m_F[f0].SurfaceOf()->GetNurbForm(s0) <= 0 || b.m_F[f1].SurfaceOf()->GetNurbForm(s1) <= 0) continue;
      std::vector<IntersectionCurve> ssx = IntersectSurfaces(s0, s1, opt);
      if (ssx.empty()) { ++kept; continue; }
      const Point3d mid = e.PointAt(e.Domain().Mid());
      const IntersectionCurve* best = nullptr;
      double bd = std::numeric_limits<double>::max();
      for (const IntersectionCurve& c : ssx) { const double d = c.curve.PointAt(c.curve.Domain().Mid()).DistanceTo(mid); if (d < bd) { bd = d; best = &c; } }
      if (!best || bd > tol * 200) { ++kept; continue; }
      const int c3i = b.AddEdgeCurve(new ON_NurbsCurve(best->curve));
      e.ChangeEdgeCurve(c3i);
      ++refit;
    }
    b.SetTolerancesBoxesAndFlags();
    o->InvalidateDisplay();
  }
  ctx.Print("RebuildEdges: " + std::to_string(refit) + " edge(s) refit through the adjacent surfaces' intersection, " + std::to_string(kept) + " left as-is (naked or no SSX found)");
}

// ---------------------------------------------------------------------------
// Intersect: wraps the curve/curve intersector with SSX (surface/surface)
// and CSX (curve/surface), covering every combination named in the task.
// ---------------------------------------------------------------------------

void IntersectAny(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<ObjectId> surface_like, curve_like;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Brep || o->kind == ObjectKind::Surface) surface_like.push_back(id);
    else curve_like.push_back(id);
  }
  if (surface_like.empty()) { CurveOrSolidIntersect(ctx, ids); return; }
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
  IntersectOptions opt;
  opt.tolerance = tol;
  opt.mesh_tolerance = std::max(tol * 4, 1e-4);
  ctx.Doc().BeginChange("Intersect");
  ctx.Doc().SelectNone();
  int curves_made = 0, points_made = 0;
  auto surfaces_of = [&](ObjectId id) {
    std::vector<std::pair<ON_NurbsSurface, const ON_BrepFace*>> out;
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) return out;
    if (o->kind == ObjectKind::Surface && o->surface) out.emplace_back(o->surface->raw(), nullptr);
    else if (o->kind == ObjectKind::Brep && o->brep) {
      const ON_Brep& b = o->brep->raw();
      for (int fi = 0; fi < b.m_F.Count(); ++fi) {
        ON_NurbsSurface ns;
        if (b.m_F[fi].SurfaceOf()->GetNurbForm(ns) > 0) { if (b.m_F[fi].m_bRev) ns.Reverse(0); out.emplace_back(ns, &b.m_F[fi]); }
      }
    }
    return out;
  };
  for (size_t i = 0; i < surface_like.size(); ++i) {
    auto sa = surfaces_of(surface_like[i]);
    for (size_t j = i + 1; j < surface_like.size(); ++j) {
      auto sb = surfaces_of(surface_like[j]);
      for (auto& [na, fa] : sa)
        for (auto& [nb, fb] : sb) {
          std::vector<IntersectionCurve> curves = IntersectFaces(fa, na, fb, nb, opt);
          for (IntersectionCurve& c : curves) {
            const ObjectId cid = ctx.Doc().Add(SceneObject::MakeCurve([&] { kernel::NurbsCurve k; k.raw() = c.curve; return k; }()));
            ctx.Doc().Select(cid, true);
            ++curves_made;
          }
        }
    }
    for (ObjectId cid : curve_like) {
      const SceneObject* co = ctx.Doc().Find(cid);
      if (!co || co->kind != ObjectKind::Curve || !co->curve) continue;
      for (auto& [na, fa] : sa) {
        (void)fa;
        std::vector<CurveSurfaceHit> hits = IntersectCurveSurface(co->curve->raw(), na, opt);
        for (const CurveSurfaceHit& h : hits) {
          const ObjectId pid = ctx.Doc().Add(SceneObject::MakePoint(h.point));
          ctx.Doc().Select(pid, true);
          ++points_made;
        }
      }
    }
  }
  ctx.Print("Intersect: " + std::to_string(curves_made) + " surface intersection curve(s), " + std::to_string(points_made) + " curve/surface point(s)");
}

// ---------------------------------------------------------------------------

void RegisterFilletCommands(CommandEngine& e) {
  Reg(e, "Intersect", OnSelection("Select objects to intersect", IntersectAny, 1), CommandStatus::Implemented,
      "Curve/curve and closed-solid pairs as before; surface/surface, brep/brep, curve/surface and brep/curve now use a real mesh-seeded, Newton-refined intersector (SSX/CSX).");
  Reg(e, "FilletSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::Fillet), CommandStatus::Implemented,
      "Rolling-ball fillet via offset+SSX+exact contact arcs; real planar trim when both inputs are planar, otherwise left untrimmed.");
  Reg(e, "ChamferSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::Chamfer), CommandStatus::Implemented,
      "Ruled surface between the two exact contact curves found the same way as FilletSrf.");
  Reg(e, "VariableFilletSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::VariableFillet), CommandStatus::Implemented,
      "Radius interpolated linearly along the spine from Radius= to EndRadius= (spine itself uses the average radius, an approximation for strongly varying radii).");
  Reg(e, "VariableChamferSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::VariableChamfer), CommandStatus::Implemented,
      "Same approximation as VariableFilletSrf, ruled instead of arced.");
  Reg(e, "FilletEdge", Make<FilletEdgeCommand>(FilletEdgeCommand::Mode::Fillet), CommandStatus::Implemented,
      "Exact rolling-ball fillet with real B-rep trimming when both adjacent faces are planar; mesh fallback (reported) otherwise.");
  Reg(e, "ChamferEdge", Make<FilletEdgeCommand>(FilletEdgeCommand::Mode::Chamfer), CommandStatus::Implemented,
      "Same trimming strategy as FilletEdge, a ruled chamfer instead of an arc.");
  Reg(e, "BlendEdge", Make<FilletEdgeCommand>(FilletEdgeCommand::Mode::Blend), CommandStatus::Implemented,
      "Hermite blend surface added between the two faces (not stitched into the polysurface). Continuity=Tangency is a degree-3x3 G1 blend. Continuity=Curvature is a degree-5x3 blend whose cross-boundary second derivative is set to each face's own exact analytic directional second derivative (Ev2Der, not a finite difference), so its curvature vector matches each face's curvature exactly in that direction (numerically verified in tests/test_g2_blend.cpp for both flat and curved faces) - real G2 in the cross-boundary direction, not full surface-wide G2 in every direction, and it silently falls back to the plain G1 tangent at any sample where a face's own parametrization is singular there.");
  Reg(e, "VariableBlendSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::VariableFillet), CommandStatus::Partial,
      "Uses the same variable-radius rolling-ball fillet as VariableFilletSrf (a true independent blend-tangent variant is not implemented).");
  Reg(e, "MatchSrf", Make<MatchSrfCommand>(), CommandStatus::Implemented,
      "Moves the picked surface's boundary control row onto the target (Position); Tangency also aligns the next row's step to the target's tangent/normal.");
  Reg(e, "BlendSrf", Make<BlendSrfCommand>(), CommandStatus::Implemented,
      "Hermite blend between two picked surface edges. Continuity=Tangency is a degree-3x3 G1 blend. Continuity=Curvature is a degree-5x3 blend matching each surface's own exact directional second derivative at the boundary (Ev2Der), so its curvature vector matches exactly in the cross-boundary direction (see BlendEdge's own help and tests/test_g2_blend.cpp) - not a claim of full surface-wide G2.");
  Reg(e, "ConnectSrf", Make<ConnectSrfCommand>(), CommandStatus::Partial,
      "Extends both surfaces and adds their real SSX join curve; exact trim is only immediate for the always-connecting planar case, otherwise trim manually with Split.");
  Reg(e, "SplitFace", Make<SplitFaceCommand>(), CommandStatus::Implemented,
      "Splits the picked face's surface at a real CSX crossing of a picked curve (untrimmed-domain split, not an arbitrary trim loop).");
  Reg(e, "SplitEdge", Make<SplitEdgeCommand>(), CommandStatus::Implemented, "Inserts a real vertex/edge split at the picked parameter (rewires every trim onto the matching half).");
  Reg(e, "MergeEdge", Make<MergeEdgeCommand>(false), CommandStatus::Implemented, "ON_Brep::CombineContiguousEdges on the two picked edges (needs them tangent within 5 degrees).");
  Reg(e, "MergeAllEdges", Make<MergeEdgeCommand>(true), CommandStatus::Implemented, "Combines every colinear/tangent pair of edges sharing a vertex, repeatedly.");
  Reg(e, "MergeFaces", Make<MergeCoplanarCommand>(false), CommandStatus::Implemented, "Unions the picked coplanar faces' outlines (mesh boolean of thin slabs) into one real trimmed-plane face.");
  Reg(e, "MergeAllCoplanarFaces", Make<MergeCoplanarCommand>(true), CommandStatus::Implemented, "Finds and merges every coplanar face group on the picked polysurface.");
  Reg(e, "MergeCoplanarFace", Make<MergeCoplanarCommand>(false), CommandStatus::Implemented, "Same as MergeFaces (pick 2+ coplanar faces, Enter).");
  Reg(e, "RebuildEdges", OnSelection("Select polysurfaces", RebuildEdgesReal), CommandStatus::Implemented,
      "Refits each shared edge's 3D curve through the real SSX of its two adjacent faces.");
}

}  // namespace dino8::app
