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
#include <array>
#include "commands/cmd_common.h"
#include "commands/history_rebuild.h"
#include "commands/revolved_solid.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/fillet.h"
#include "geom/BlendSurface.h"
#include "geom/BrepTrimFace.h"
#include "geom/SurfaceIntersect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
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

// Finds the ON_BrepEdge shared by two faces of the SAME brep (fi/fj are
// `b`'s own face indices), the mirror-image lookup of PickEdge above: that
// one starts from a picked point on an edge and returns the edge; this
// starts from two already-picked FACES (FilletTwoSurfacesCommand's own
// PickFace, not PickEdge) and returns whether they're actually adjacent -
// needed because ChamferConvexEdge/ChamferConcaveEdge below take the shared
// edge's own endpoints, not a face pair.
bool FindSharedEdgeEndpoints(const ON_Brep& b, int fi, int fj, Point3d& p0, Point3d& p1) {
  for (int i = 0; i < b.m_E.Count(); ++i) {
    const ON_BrepEdge& e = b.m_E[i];
    if (e.m_edge_index < 0 || e.TrimCount() != 2) continue;
    const int a = b.m_T[e.m_ti[0]].FaceIndexOf();
    const int c = b.m_T[e.m_ti[1]].FaceIndexOf();
    if ((a == fi && c == fj) || (a == fj && c == fi)) {
      p0 = e.PointAtStart();
      p1 = e.PointAtEnd();
      return true;
    }
  }
  return false;
}

// Exact conical chamfer on a FULL capped cylinder's rim: wraps
// kernel::ChamferCylinderRim (fillet.h), the only exact chamfer whose two
// faces are not both planar. `point_on_rim` is any point on the rim circle
// (an edge endpoint will do); `distance_wall` is measured along the wall,
// `distance_cap` across the cap. Returns false (leaving `out` untouched,
// reason in `err`) when the kernel refuses - not a full cylinder's rim, a
// concave/countersink rim, a distance that consumes the whole wall or cap -
// so the caller can fall back to its older approximate path unchanged.
//
// The kernel reads a disc cap as a dense polygon and hands the polygon back
// (128 edges here), which no longer shares an edge with the neighbouring
// curved face's exact cap circle: the raw result is a 261-edge OPEN brep
// whose Volume/booleans/Cap all fail. Both polygon caps are therefore
// dropped and re-capped from the circles the curved faces already carry
// (Brep::DeleteFace + CapPlanarHoles' closed-curved-loop cap), which gives
// back a true closed 4-face/7-edge solid with exact circular caps. If that
// re-closing does not produce a valid solid the result is refused, so a
// chamfer never turns a closed cylinder into an open shell.
bool TryCylinderRimChamfer(const ON_Brep& solid, Point3d point_on_rim, double distance_wall, double distance_cap, ON_Brep& out,
                            std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    kernel::Brep res = kernel::ChamferCylinderRim(kb, point_on_rim, distance_wall, distance_cap);
    std::vector<int> polygon_caps;
    for (int fi = 0; fi < res.raw().m_F.Count(); ++fi) {
      ON_Plane plane;
      const ON_Surface* srf = res.raw().m_F[fi].SurfaceOf();
      const ON_BrepLoop* outer = res.raw().m_F[fi].OuterLoop();
      if (srf && outer && srf->IsPlanar(&plane, 1e-9) && outer->TrimCount() > 8) polygon_caps.push_back(fi);
    }
    if (polygon_caps.size() != 2) { err = "ChamferCylinderRim result does not have the two expected disc caps"; return false; }
    for (int k = static_cast<int>(polygon_caps.size()) - 1; k >= 0; --k) res.DeleteFace(polygon_caps[k]);
    res.CapPlanarHoles(1e-6);
    if (!res.raw().IsSolid() || !res.raw().IsValid()) { err = "the chamfered cylinder could not be re-closed with exact disc caps"; return false; }
    out = res.raw();
    return true;
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

// Exact conical chamfer on the rim of a PLAIN CAPPED CONE or CONE FRUSTUM (Cone / TCone): the
// solid's convex (radius, height) profile has the rim circle through `point_on_rim` as a vertex;
// that corner is cut by `distance_wall` along the wall and `distance_cap` across the flat cap and
// the new profile is revolved (revolved_solid.h), so the bevel is an exact cone and every face
// stays an exact revolved surface. Same contract as TryCylinderRimChamfer: false (reason in
// `err`, `out` untouched) when this is not such a solid / rim or a distance does not fit.
bool TryConeRimChamfer(const ON_Brep& solid, Point3d point_on_rim, double distance_wall, double distance_cap, ON_Brep& out,
                       std::string& err) {
  try {
    revolved::PlainRevolved pr;
    if (!revolved::RecognizeConeLike(solid, pr)) { err = "not a plain capped cone or frustum"; return false; }
    ON_BoundingBox bb;
    solid.GetBoundingBox(bb);
    const double tol = std::max(1e-9, bb.Diagonal().Length() * 1e-5);
    const ON_3dVector v = point_on_rim - pr.c0;
    const double h = v * pr.ax;
    const double r = (v - h * pr.ax).Length();
    const auto& P = pr.prof;
    int k = -1;
    for (size_t i = 1; i + 1 < P.size(); ++i) if (std::fabs(P[i].first - r) < tol && std::fabs(P[i].second - h) < tol) k = static_cast<int>(i);
    if (k < 0) { err = "the edge is not a rim circle of this cone"; return false; }
    if (!(distance_wall > 0) || !(distance_cap > 0)) { err = "chamfer distances must be positive"; return false; }
    auto unit = [&](size_t a, size_t b) {
      const double dr = P[b].first - P[a].first, dz = P[b].second - P[a].second, len = std::hypot(dr, dz);
      return std::array<double, 3>{dr / len, dz / len, len};
    };
    const std::array<double, 3> in = unit(static_cast<size_t>(k) - 1, static_cast<size_t>(k)), outd = unit(static_cast<size_t>(k), static_cast<size_t>(k) + 1);
    const bool in_is_cap = std::fabs(in[1]) < 1e-9;  // the edge arriving at the rim vertex is the flat cap
    const double d_in = in_is_cap ? distance_cap : distance_wall, d_out = in_is_cap ? distance_wall : distance_cap;
    if (!(d_in < in[2] - tol) || !(d_out < outd[2] - tol)) { err = "a chamfer distance consumes the whole wall or cap"; return false; }
    std::vector<std::pair<double, double>> prof;
    for (int i = 0; i < static_cast<int>(P.size()); ++i) {
      if (i != k) { prof.push_back(P[static_cast<size_t>(i)]); continue; }
      prof.emplace_back(P[static_cast<size_t>(i)].first - d_in * in[0], P[static_cast<size_t>(i)].second - d_in * in[1]);
      prof.emplace_back(P[static_cast<size_t>(i)].first + d_out * outd[0], P[static_cast<size_t>(i)].second + d_out * outd[1]);
    }
    std::optional<kernel::Brep> res = revolved::RevolveOutward(pr, prof);
    if (!res) { err = "the chamfered cone profile did not revolve into a valid closed solid"; return false; }
    out = res->raw();
    return true;
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

// ---------------------------------------------------------------------------
// Kernel cap-rim wrappers (kernel::FilletCapRim(s) / ChamferCapRim / ChamferCapRimAngle, fillet.h): exact blends on
// a rim of a PLAIN CAPPED CYLINDER or CONE FRUSTUM (acute and obtuse rims alike). The kernel's own classifier refuses
// every other solid with a reason (a hole plate, a boss, a full apex cone, an annular cap, an already-blended solid,
// ...) and each wrapper hands that reason back in `err` with `out` untouched, so the caller falls through to its
// existing path and can print why. A result is accepted only if it is a valid closed solid.
// ---------------------------------------------------------------------------
bool AcceptCapRimResult(const kernel::Brep& res, ON_Brep& out, std::string& err) {
  if (!res.raw().IsValid() || !res.raw().IsSolid()) { err = "the kernel cap-rim result is not a valid closed solid"; return false; }
  out = res.raw();
  return true;
}

bool TryCapRimChamferAngle(const ON_Brep& solid, Point3d point_on_rim, double distance_cap, double angle_rad, ON_Brep& out, std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    return AcceptCapRimResult(kernel::ChamferCapRimAngle(kb, point_on_rim, distance_cap, angle_rad), out, err);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

bool TryCapRimChamfer(const ON_Brep& solid, Point3d point_on_rim, double distance_wall, double distance_cap, ON_Brep& out, std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    return AcceptCapRimResult(kernel::ChamferCapRim(kb, point_on_rim, distance_wall, distance_cap), out, err);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

bool TryCapRimFillet(const ON_Brep& solid, Point3d point_on_rim, double radius, ON_Brep& out, std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    return AcceptCapRimResult(kernel::FilletCapRim(kb, point_on_rim, radius), out, err);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

// ---------------------------------------------------------------------------
// Kernel revolve-rim wrappers (kernel::FilletRevolveRim / ChamferRevolveRim, fillet.h): exact blends on ONE rim of a
// polygonal solid of revolution - a plate with a countersunk / counterbored / plain hole, a stepped shaft (the rim may
// be an INSIDE corner, which adds material), a flanged ring, a cone-capped rim. They are tried only AFTER the capped
// cylinder / frustum wrappers above (and the app's own cylinder / hole classifiers) declined, so every solid those
// accept is built exactly as before. The kernel refuses everything it cannot read back (non-coaxial or non-analytic
// face, void, second shell, an already-blended solid, a point on no rim, tangent faces, a setback consuming a face, a
// chamfer without exactly one planar face) with a stated reason, handed back in `err` with `out` untouched.
// ---------------------------------------------------------------------------
bool TryRevolveRimFillet(const ON_Brep& solid, Point3d point_on_rim, double radius, ON_Brep& out, std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    return AcceptCapRimResult(kernel::FilletRevolveRim(kb, point_on_rim, radius), out, err);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

bool TryRevolveRimChamfer(const ON_Brep& solid, Point3d point_on_rim, double distance_wall, double distance_cap, ON_Brep& out, std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    return AcceptCapRimResult(kernel::ChamferRevolveRim(kb, point_on_rim, distance_wall, distance_cap), out, err);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

// ---------------------------------------------------------------------------
// Kernel pipe-tee wrapper (kernel::FilletPipeTee, fillet.h): the rolling-ball fillet along the SADDLE edge where a
// perpendicular branch cylinder (radius rb) meets a larger main cylinder (radius R) - the solid the app's sealed
// Boolean union (TryGeneralBrepBoolean) makes of two capped cylinders. It is tried only AFTER every other exact attempt
// declined. The kernel reads the tee back from its faces and refuses everything else (a box, a cross, equal radii,
// non-perpendicular or skew axes, a ball that would reach a cap or fold over itself, a fit that misses the tolerance)
// with a stated reason, handed back in `err` with `out` untouched. The cylinder walls, caps and the ball's arcs are
// analytic; the blend is a NURBS surface fitted to the exact rolling-ball surface to `tolerance` (measured at the
// interval midpoints, see PipeTeeFillet::fit_deviation), which is why the messages say "fitted", never "exact".
// The kernel fillets the whole saddle edge, so the picked edge only has to BE a piece of it (EdgeOnPipeTeeSaddle).
// The fit tolerance is 1e-7 of the solid's bounding-box diagonal (never below the kernel's default 1e-6).
// ---------------------------------------------------------------------------
double PipeTeeTolerance(const ON_Brep& solid) {
  ON_BoundingBox bb;
  solid.GetBoundingBox(bb);
  return std::max(1e-6, bb.Diagonal().Length() * 1e-7);
}

bool TryPipeTeeFillet(const ON_Brep& solid, double radius, ON_Brep& out, std::string& err, kernel::PipeTeeFillet* info = nullptr) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    kernel::PipeTeeFillet f = kernel::FilletPipeTee(kb, radius, PipeTeeTolerance(solid));
    if (!AcceptCapRimResult(f.solid, out, err)) return false;
    if (info) *info = std::move(f);
    return true;
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

// True when `p` lies on the main cylinder (radius R about the main axis) AND on the branch cylinder (radius rb about the
// branch axis) of the tee that `f` describes, within `band`: that is the saddle curve and nothing else. The cap rims of
// either cylinder are at least one ball radius off the other cylinder, and a seam of the main wall meets the branch
// cylinder only at its ends, so a `band` below the ball radius separates them.
bool OnPipeTeeSaddle(const kernel::PipeTeeFillet& f, Point3d p, double band) {
  auto dist_to_line = [&](const Vector3d& axis) {
    const Vector3d v = p - f.crossing;
    return (v - (v * axis) * axis).Length();
  };
  return std::fabs(dist_to_line(f.main_axis) - f.main_radius) <= band && std::fabs(dist_to_line(f.branch_axis) - f.branch_radius) <= band;
}

// True when the picked edge is a piece of the saddle: its two ends (vertices of the Boolean's intersection chain) lie on
// both cylinders to a tight band, its middle to a wider one (the chain is a polyline of chords, so a middle is off the true
// curve by up to the chord's sagitta).
bool EdgeOnPipeTeeSaddle(const kernel::PipeTeeFillet& f, const ON_BrepEdge& edge, double ball_radius, double tol) {
  const double tight = std::min(0.1 * ball_radius, std::max(tol, 1e-2 * f.branch_radius));
  const double loose = 0.25 * ball_radius;
  return OnPipeTeeSaddle(f, edge.PointAtStart(), tight) && OnPipeTeeSaddle(f, edge.PointAtEnd(), tight) &&
         OnPipeTeeSaddle(f, edge.PointAt(edge.Domain().ParameterAt(0.5)), loose);
}

// True when `p` lies on an edge of `b` (within tol).
bool PointOnAnyEdge(const ON_Brep& b, Point3d p, double tol) {
  for (int i = 0; i < b.m_E.Count(); ++i) {
    ON_NurbsCurve nc;
    if (b.m_E[i].GetNurbForm(nc) <= 0) continue;
    kernel::NurbsCurve k;
    k.raw() = nc;
    if (k.PointAt(k.ClosestPointParameter(p, 200)).DistanceTo(p) <= tol) return true;
  }
  return false;
}

// Both rims of a cap-rim solid in ONE kernel call. `rims` are (a point on the rim, radius); with two, the kernel's
// low / high assignment (the end with the smaller coordinate along the wall's own axis) is found by probing: filleting
// only the first rim as the "low" one leaves its sharp circle in place exactly when it was really the high one.
bool TryCapRimsFillet(const ON_Brep& solid, const std::vector<std::pair<Point3d, double>>& rims, ON_Brep& out, std::string& err) {
  try {
    kernel::Brep kb;
    kb.raw() = solid;
    if (rims.size() == 1) return AcceptCapRimResult(kernel::FilletCapRim(kb, rims[0].first, rims[0].second), out, err);
    if (rims.size() != 2) { err = "a cap-rim fillet takes one or two rims"; return false; }
    ON_BoundingBox bb;
    solid.GetBoundingBox(bb);
    const double tol = std::max(1e-6, bb.Diagonal().Length() * 1e-6);
    bool first_is_low = false;
    try {
      first_is_low = !PointOnAnyEdge(kernel::FilletCapRims(kb, rims[0].second, 0.0).raw(), rims[0].first, tol);
    } catch (const std::exception&) {  // this radius may not fit the other end of a frustum: probe the other assignment
      first_is_low = PointOnAnyEdge(kernel::FilletCapRims(kb, 0.0, rims[0].second).raw(), rims[0].first, tol);
    }
    const double r_low = first_is_low ? rims[0].second : rims[1].second;
    const double r_high = first_is_low ? rims[1].second : rims[0].second;
    return AcceptCapRimResult(kernel::FilletCapRims(kb, r_low, r_high), out, err);
  } catch (const std::exception& ex) {
    err = ex.what();
    return false;
  }
}

// ---------------------------------------------------------------------------
// Exact fillet of the rim(s) of a PLAIN CAPPED CYLINDER.
//
// The kernel has no fillet between a plane and a cylinder (FilletConvexEdge is
// planar-planar; only the CHAMFER of a cylinder rim, ChamferCylinderRim, is
// exact), so the app used to send a cylinder rim to the mesh fallback - which,
// measured, was not even right (a radius-1 fillet on the top rim of a
// radius-5, height-20 cylinder INCREASED the volume 1570.8 -> 1594). The rolling
// ball between a flat cap and a cylinder wall traces an exact quarter TORUS, so
// the result is assembled from exact pieces: the wall (a shortened cylinder), one
// quarter-torus patch per filleted rim (an exact rational arc revolved about the
// axis, ON_RevSurface), joined along their shared circles
// (Brep::JoinNakedEdges - the circles coincide exactly) and re-capped with
// planar discs (Brep::CapPlanarHoles). The result is a true closed polysurface
// (4 faces / 7 edges for one rim, 5 / 9 for both) whose volume is the closed
// form 2*pi*[R^2 H/2 - ...] (checked in the smoke script), not a tessellation.
// Only a plain cylinder (one cylindrical wall + two flat caps perpendicular to
// its axis) is recognised; anything else keeps the existing paths.
// ---------------------------------------------------------------------------
struct CylinderRimPlan {
  ON_3dPoint c0;   // centre of the bottom cap
  ON_3dVector ax;  // unit axis, bottom -> top
  ON_3dVector u;   // unit direction perpendicular to ax
  double R = 0, H = 0;
  bool top = false;  // which cap the picked edge bounds
};

bool ClassifyPlainCylinderRim(const ON_Brep& b, int edge_index, CylinderRimPlan& plan, std::string& why) {
  if (!b.IsSolid() || b.m_F.Count() != 3) { why = "not a plain capped cylinder (needs a wall and two cap faces)"; return false; }
  if (edge_index < 0 || edge_index >= b.m_E.Count()) { why = "invalid edge"; return false; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double tol = std::max(1e-6, bb.Diagonal().Length() * 1e-6);
  int wall = -1;
  ON_Cylinder cyl;
  std::vector<ON_Plane> caps;
  for (int f = 0; f < 3; ++f) {
    const ON_Surface* surf = b.m_F[f].SurfaceOf();
    if (!surf) { why = "face without a surface"; return false; }
    ON_Plane pl;
    if (wall < 0 && surf->IsCylinder(&cyl, tol)) wall = f;
    else if (surf->IsPlanar(&pl, tol)) caps.push_back(pl);
    else { why = "not a plain capped cylinder"; return false; }
  }
  if (wall < 0 || caps.size() != 2) { why = "not a plain capped cylinder (needs one cylindrical wall and two planar caps)"; return false; }
  ON_3dVector ax = cyl.Axis();
  if (!ax.Unitize()) { why = "degenerate cylinder axis"; return false; }
  double h[2];
  for (int k = 0; k < 2; ++k) {
    if (std::fabs(caps[k].zaxis * ax) < 1.0 - 1e-9) { why = "a cap is not perpendicular to the cylinder axis"; return false; }
    h[k] = (caps[k].origin - cyl.circle.plane.origin) * ax;
  }
  const double h0 = std::min(h[0], h[1]), h1 = std::max(h[0], h[1]);
  if (!(h1 - h0 > tol)) { why = "degenerate cylinder height"; return false; }
  const ON_BrepEdge& e = b.m_E[edge_index];
  const ON_3dPoint p = e.PointAtStart();
  if (p.DistanceTo(e.PointAtEnd()) > 10 * tol) { why = "the picked edge is the cylinder's seam line, not a closed rim circle"; return false; }
  const double hp = (p - cyl.circle.plane.origin) * ax;
  const ON_3dVector radial = (p - cyl.circle.plane.origin) - hp * ax;
  if (std::fabs(radial.Length() - cyl.circle.radius) > 10 * tol) { why = "the picked edge is not a rim circle of the cylinder"; return false; }
  if (std::fabs(hp - h0) <= 10 * tol) plan.top = false;
  else if (std::fabs(hp - h1) <= 10 * tol) plan.top = true;
  else { why = "the picked edge is the cylinder's seam, not a rim"; return false; }
  plan.c0 = cyl.circle.plane.origin + h0 * ax;
  plan.ax = ax;
  plan.u = radial.Length() > tol ? radial / radial.Length() : cyl.circle.plane.xaxis;
  plan.R = cyl.circle.radius;
  plan.H = h1 - h0;
  return true;
}

// rt / rb: fillet radius at the top / bottom rim (0 = leave that rim sharp).
bool BuildFilletedCylinder(const CylinderRimPlan& g, double rt, double rb, ON_Brep& out, std::string& why) {
  if (!(rt >= 0) || !(rb >= 0) || (rt <= 0 && rb <= 0)) { why = "no positive radius"; return false; }
  if (!(rt < g.R) || !(rb < g.R)) { why = "the radius is not smaller than the cylinder radius"; return false; }
  if (!(rt + rb < g.H)) { why = "the radii together consume the whole wall"; return false; }
  auto revolve = [&](const ON_Arc& arc) -> ON_Brep* {
    ON_RevSurface* rev = new ON_RevSurface();
    rev->m_curve = new ON_ArcCurve(arc);
    rev->m_axis = ON_Line(g.c0, g.c0 + g.ax);
    rev->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
    rev->m_t = rev->m_angle;
    rev->m_bTransposed = false;
    return ON_BrepRevSurface(rev, false, false);
  };
  ON_Brep all;
  ON_Cylinder wall(ON_Circle(ON_Plane(g.c0 + rb * g.ax, g.ax), g.R), g.H - rt - rb);
  if (ON_Brep* w = ON_BrepCylinder(wall, false, false)) { all.Append(*w); delete w; }
  else { why = "could not build the cylinder wall"; return false; }
  if (rt > 0) {
    ON_Brep* t = revolve(ON_Arc(ON_Plane(g.c0 + (g.R - rt) * g.u + (g.H - rt) * g.ax, g.u, g.ax), rt, ON_PI / 2));
    if (!t) { why = "could not build the top torus patch"; return false; }
    all.Append(*t);
    delete t;
  }
  if (rb > 0) {
    ON_Brep* t = revolve(ON_Arc(ON_Circle(ON_Plane(g.c0 + (g.R - rb) * g.u + rb * g.ax, g.u, g.ax), rb), ON_Interval(-ON_PI / 2, 0.0)));
    if (!t) { why = "could not build the bottom torus patch"; return false; }
    all.Append(*t);
    delete t;
  }
  kernel::Brep k;
  k.raw() = all;
  try {
    k.JoinNakedEdges(1e-6);
    k.CapPlanarHoles(1e-6);
    k.UnifyNormals();
  } catch (const std::exception& ex) {
    why = ex.what();
    return false;
  }
  if (!k.raw().IsSolid() || !k.raw().IsValid()) { why = "the assembled pieces did not form a valid closed solid"; return false; }
  out = k.raw();
  return true;
}

// ---------------------------------------------------------------------------
// Exact fillet / chamfer of the rim of a CYLINDRICAL THROUGH-HOLE.
//
// The solid a BooleanDifference of a box and a cylinder gives (and, in general, any
// straight-edged prism with ONE cylindrical through-hole whose axis is perpendicular to both end
// faces) has, at a hole rim, a planar end face meeting the concave hole wall. The rolling ball
// between a plane and a cylinder is again an exact torus patch (for a hole it is centred
// OUTSIDE the wall: major radius R + r, minor radius r), and the chamfer is an exact cone, so
// the result is assembled from exact pieces the same way the plain-cylinder rim fillet above is:
// the prism's side faces are the extrusion of its outer polygon (untouched), the end face gets
// a circular inner loop of radius R + (cap setback), the wall is shortened by the blend height,
// and one revolved patch (ON_RevSurface of an exact rational arc or a line) joins the two along
// shared circles (Brep::JoinNakedEdges). Rim circles on the original Boolean result are
// polylines of many short edges, so the plan is read from the faces (the cylinder, the two end
// planes and the end face's outer polygon), never from the picked edge's own geometry.
// ---------------------------------------------------------------------------
// One rim blend: a fillet (d_wall == d_cap == radius) or a chamfer (distances along the wall / across the cap).
struct HoleRimBlend {
  bool chamfer = false;
  double d_wall = 0, d_cap = 0;
};

struct HolePlatePlan {
  ON_3dPoint c0;                 // point of the hole axis on the lower end plane
  ON_3dVector ax;                // unit axis, lower end -> upper end
  ON_3dVector u;                 // unit direction perpendicular to ax
  double R = 0, H = 0;           // hole radius, plate thickness (along ax)
  std::vector<ON_3dPoint> poly;  // outer polygon of the lower end face (straight edges, on the lower plane)
  bool top = false;              // which end the picked edge bounds
  int corner = -1;               // for a picked vertical corner edge: its index in `poly`
  double floor = 0;              // blind hole: height of the hole floor above the lower (closed) end; 0 = a through-hole
  // A blend (exact torus / cone patch) that an earlier FilletEdge / ChamferEdge already put on the OTHER rim of the hole,
  // recovered from the patch's own geometry so a second operation keeps it (mixed fillet + chamfer rims, both rims in one run).
  std::optional<HoleRimBlend> exist_lo, exist_hi;
  // A BOSS (cylinder standing on the plate) instead of a hole: `floor` is then the plate thickness, `H` the height of the boss
  // cap above the plate's bottom, and the picked rim is the base (boss_base, a concave blend that ADDS material) or the top.
  bool boss = false, boss_base = false;
};

// want_corner = false: the picked edge must be a hole rim (cylinder/end face); true: a vertical corner edge between
// two side faces (plan.corner = the index of that corner of plan.poly).
bool ClassifyHolePlateRim(const ON_Brep& b, int edge_index, HolePlatePlan& plan, std::string& why, bool want_corner = false) {
  if (!b.IsSolid()) { why = "not a closed solid"; return false; }
  if (edge_index < 0 || edge_index >= b.m_E.Count()) { why = "invalid edge"; return false; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double tol = std::max(1e-6, bb.Diagonal().Length() * 1e-6);
  int wall = -1;
  ON_Cylinder cyl;
  std::vector<int> blends;
  for (int f = 0; f < b.m_F.Count(); ++f) {
    const ON_Surface* surf = b.m_F[f].SurfaceOf();
    if (!surf) { why = "face without a surface"; return false; }
    ON_Cylinder c;
    ON_Plane pl;
    if (surf->IsPlanar(&pl, tol)) continue;
    if (surf->IsCylinder(&c, tol)) {
      if (wall >= 0) { why = "more than one cylindrical face"; return false; }
      wall = f;
      cyl = c;
    } else if (dynamic_cast<const ON_RevSurface*>(surf) != nullptr) {
      blends.push_back(f);  // a fillet / chamfer patch an earlier operation left on a rim: analysed below
    } else { why = "a face is neither planar nor the one cylindrical wall"; return false; }
  }
  if (wall < 0) { why = "no cylindrical face"; return false; }
  if (blends.size() > 2) { why = "more than two blend faces on the hole"; return false; }
  ON_3dVector ax = cyl.Axis();
  if (!ax.Unitize()) { why = "degenerate cylinder axis"; return false; }
  bool is_boss = false;
  // (a boss versus a blind hole is decided below, once the end faces are known: both have the same face structure)
  // The two end planes (normal parallel to the axis) and the side planes (normal perpendicular to it).
  std::vector<int> ends, sides;
  for (int f = 0; f < b.m_F.Count(); ++f) {
    if (f == wall || std::find(blends.begin(), blends.end(), f) != blends.end()) continue;
    ON_Plane pl;
    b.m_F[f].SurfaceOf()->IsPlanar(&pl, tol);
    const double dn = std::fabs(pl.zaxis * ax);
    if (dn > 1.0 - 1e-9) ends.push_back(f);
    else if (dn < 1e-9) sides.push_back(f);
    else { why = "a planar face is neither an end face nor a side face of a prism along the hole axis"; return false; }
  }
  if ((ends.size() != 2 && ends.size() != 3) || sides.size() < 3) { why = "not a prism with one cylindrical hole (needs two end faces, plus the hole floor if blind, and at least three side faces)"; return false; }
  const bool blind = ends.size() == 3;
  // A boss (a cylinder standing on a plate) has the face structure of a blind hole: the wall meets the plate's top face and
  // the boss cap, the plate's bottom face is untouched. Only its base and top rims are supported, never a corner edge.
  std::vector<double> h(ends.size());
  std::vector<bool> touch(ends.size(), false);  // does the hole wall share an edge with this end plane?
  std::vector<int> blend_end(ends.size(), -1);   // the blend face between the wall and this end plane, if that rim is already blended
  for (size_t k = 0; k < ends.size(); ++k) {
    ON_Plane pl;
    b.m_F[ends[k]].SurfaceOf()->IsPlanar(&pl, tol);
    h[k] = (pl.origin - cyl.circle.plane.origin) * ax;
    for (int ei = 0; ei < b.m_E.Count() && !touch[k]; ++ei) {
      const ON_BrepEdge& ee = b.m_E[ei];
      if (ee.m_edge_index < 0 || ee.TrimCount() != 2) continue;
      const int x = b.m_T[ee.m_ti[0]].FaceIndexOf(), y = b.m_T[ee.m_ti[1]].FaceIndexOf();
      touch[k] = (x == wall && y == ends[k]) || (y == wall && x == ends[k]);
    }
    // an end that is already blended: the wall meets a blend patch and that patch meets this end plane
    for (int bf : blends) {
      bool wall_bf = false, bf_end = false;
      for (int ei = 0; ei < b.m_E.Count(); ++ei) {
        const ON_BrepEdge& ee = b.m_E[ei];
        if (ee.m_edge_index < 0 || ee.TrimCount() != 2) continue;
        const int x = b.m_T[ee.m_ti[0]].FaceIndexOf(), y = b.m_T[ee.m_ti[1]].FaceIndexOf();
        if ((x == wall && y == bf) || (y == wall && x == bf)) wall_bf = true;
        if ((x == ends[k] && y == bf) || (y == ends[k] && x == bf)) bf_end = true;
      }
      if (wall_bf && bf_end) {
        if (blend_end[k] >= 0 && blend_end[k] != bf) { why = "more than one blend patch between the wall and one end face"; return false; }
        blend_end[k] = bf;
      }
    }
    if (blend_end[k] >= 0) touch[k] = true;
  }
  // Every curved non-cylinder face must be a rim blend this analysis reads: rebuilding from the plan alone would silently delete
  // any other revolved feature (a dimple, a dome, an unrelated cone).
  for (int bf : blends) {
    if (std::find(blend_end.begin(), blend_end.end(), bf) == blend_end.end()) { why = "a curved face that is not a blend between the hole wall and an end face"; return false; }
  }
  size_t lower = 0, upper = 0, floor_k = 0;  // indices into ends: the plate's lower end, its upper end (the rim end if blind), the hole floor
  if (!blind) {
    if (!touch[0] || !touch[1]) { why = "the hole is not a through-hole"; return false; }
    lower = h[0] <= h[1] ? 0 : 1;
    upper = 1 - lower;
  } else {
    // a blind hole: the wall touches the open end and the floor; the untouched end is the closed one. Orient the axis
    // from the closed end to the open end.
    int untouched = -1, touched = 0;
    for (size_t k = 0; k < 3; ++k) { if (touch[k]) ++touched; else untouched = static_cast<int>(k); }
    if (touched != 2 || untouched < 0) { why = "not a blind hole in a prism (the wall must meet exactly the open end and the floor)"; return false; }
    lower = static_cast<size_t>(untouched);
    size_t t1 = (lower + 1) % 3, t2 = (lower + 2) % 3;
    const bool t1_nearer = std::fabs(h[t1] - h[lower]) < std::fabs(h[t2] - h[lower]);
    floor_k = t1_nearer ? t1 : t2;
    upper = t1_nearer ? t2 : t1;
    if (h[upper] < h[lower]) {
      ax = -ax;
      for (double& x : h) x = -x;
    }
    if (!(h[floor_k] > h[lower] + tol && h[upper] > h[floor_k] + tol)) { why = "the hole floor is not between the closed end and the open end"; return false; }
    // A BOSS (a cylinder unioned onto a plate) has the face structure of a blind hole: the wall meets the plate's top face (here
    // `floor_k`) and the boss cap (`upper`), the plate's bottom face (`lower`) is untouched. Tell them apart by volume: the
    // bottom face is the full plate outline (area A), so V = A*H - pi R^2 (H - f) for a hole and A*f + pi R^2 (H - f) for a boss.
    {
      const ON_BrepLoop* ol = b.m_F[ends[lower]].OuterLoop();
      double A2 = 0;
      if (ol) {
        for (int ti = 0; ti < ol->TrimCount(); ++ti) {
          const ON_BrepTrim* t = ol->Trim(ti);
          ON_3dPoint pa = t->Edge()->PointAtStart(), pb = t->Edge()->PointAtEnd();
          if (t->m_bRev3d) std::swap(pa, pb);
          A2 += ON_CrossProduct(pa - ON_3dPoint(0, 0, 0), pb - ON_3dPoint(0, 0, 0)) * ax;
        }
      }
      const double A = 0.5 * std::fabs(A2), Rr = cyl.circle.radius, fl = h[floor_k] - h[lower], Hh = h[upper] - h[lower];
      BrepMeshOptions mopt;
      mopt.chord_tolerance = std::max(1e-6, bb.Diagonal().Length() * 2e-4);
      double vol = 0;
      kernel::Mesh closed_mesh;
      bool have_mesh = false;
      try { closed_mesh = MeshBrepClosed(b, mopt); vol = std::fabs(closed_mesh.Volume()); have_mesh = true; } catch (const std::exception&) { vol = 0; }
      const double v_hole = A * Hh - ON_PI * Rr * Rr * (Hh - fl), v_boss = A * fl + ON_PI * Rr * Rr * (Hh - fl);
      // Boss or blind hole is decided by STRUCTURE (a boss wall faces away from the axis, a hole wall toward it); the volume only
      // has to agree with that reading. (Volumes alone tie exactly when the plate area equals 2 pi R^2.)
      const ON_Surface* ws = b.m_F[wall].SurfaceOf();
      const double uu = ws->Domain(0).ParameterAt(0.37), vv = ws->Domain(1).ParameterAt(0.41);
      const ON_3dPoint wp = ws->PointAt(uu, vv);
      ON_3dVector radial = wp - cyl.circle.plane.origin;
      radial -= (radial * ax) * ax;
      if (!radial.Unitize()) { why = "degenerate hole wall"; return false; }
      // A point just OUTSIDE the cylinder's radius lies in the material for a hole (the void is inside) and in the void for a boss.
      const ON_3dPoint probe = wp + 0.01 * cyl.circle.radius * radial;
      if (!have_mesh) { why = "could not mesh the solid to tell a boss from a blind hole"; return false; }
      is_boss = !closed_mesh.ContainsPoint(Point3d(probe.x, probe.y, probe.z));
      const double chosen = std::fabs(vol - (is_boss ? v_boss : v_hole));
      if (!(vol > 0) || !(A > 0) || chosen > 0.02 * vol) { why = std::string("the volume does not match a ") + (is_boss ? "boss" : "blind hole") + " on a straight-edged plate"; return false; }
    }
    if (is_boss && want_corner) { why = "corner edges of a plate carrying a boss are not supported"; return false; }
  }
  const double h0 = h[lower], h1 = h[upper];
  if (!(h1 - h0 > tol)) { why = "degenerate plate thickness"; return false; }
  if (blind && !is_boss && !blends.empty()) { why = "a blind hole that already carries a blend is not supported"; return false; }
  // Blends an earlier operation left: read their (d_wall, d_cap) and kind from the patch geometry. A blend sits between the wall
  // and one end plane: its wall contact is (R, z_end + s*d_wall), its cap contact (R + sigma*d_cap, z_end), with s = +-1 the side
  // of the end plane it lies on and sigma = +1 when it widens the opening (hole rim, boss base), -1 when it narrows the cylinder
  // (boss top). A fillet is the quarter circle centred (R + sigma*r, z_end + s*r), a chamfer the straight cone between them.
  {
    const ON_3dPoint lowpt = cyl.circle.plane.origin + h0 * ax;
    const double Rr = cyl.circle.radius, gt = 10 * tol;
    for (size_t k = 0; k < ends.size(); ++k) {
      if (blend_end[k] < 0) continue;
      const ON_Surface* sf = b.m_F[blend_end[k]].SurfaceOf();
      const ON_Interval du = sf->Domain(0), dv = sf->Domain(1);
      double zmin = 1e300, zmax = -1e300, rmin = 1e300, rmax = -1e300;
      std::vector<std::pair<double, double>> pts;
      for (int i = 0; i <= 8; ++i) {
        for (int j = 0; j <= 8; ++j) {
          const ON_3dVector w = sf->PointAt(du.ParameterAt(i / 8.0), dv.ParameterAt(j / 8.0)) - lowpt;
          const double z = w * ax, rho = (w - z * ax).Length();
          pts.emplace_back(rho, z);
          zmin = std::min(zmin, z); zmax = std::max(zmax, z);
          rmin = std::min(rmin, rho); rmax = std::max(rmax, rho);
        }
      }
      const double ze = h[k] - h0;
      if (std::fabs(zmin - ze) > gt && std::fabs(zmax - ze) > gt) { why = "an existing blend on the cylinder does not lie on an end plane"; return false; }
      const double s = 0.5 * (zmin + zmax) > ze ? 1.0 : -1.0;
      const double sigma = std::fabs(rmin - Rr) <= gt ? 1.0 : (std::fabs(rmax - Rr) <= gt ? -1.0 : 0.0);
      if (sigma == 0.0) { why = "an existing blend on the cylinder is not a fillet or chamfer of a rim"; return false; }
      HoleRimBlend bl;
      bl.d_wall = zmax - zmin;
      bl.d_cap = rmax - rmin;
      if (!(bl.d_wall > gt) || !(bl.d_cap > gt)) { why = "degenerate existing blend"; return false; }
      const double wx = Rr, wy = ze + s * bl.d_wall, cx = Rr + sigma * bl.d_cap, cy = ze;
      const double ll = std::hypot(cx - wx, cy - wy);
      bool on_line = true, on_circle = std::fabs(bl.d_wall - bl.d_cap) <= gt;
      const double r = bl.d_wall, ccx = Rr + sigma * r, ccy = ze + s * r;
      for (const auto& pz : pts) {
        if (std::fabs((pz.first - wx) * (cy - wy) - (pz.second - wy) * (cx - wx)) / std::max(ll, 1e-300) > gt) on_line = false;
        if (std::fabs(std::hypot(pz.first - ccx, pz.second - ccy) - r) > gt) on_circle = false;
      }
      if (on_circle) bl.chamfer = false;
      else if (on_line) bl.chamfer = true;
      else { why = "an existing blend on the cylinder is neither a rolling-ball fillet nor a chamfer cone"; return false; }
      const bool is_lo = blind ? (k == floor_k) : (k == lower);
      (is_lo ? plan.exist_lo : plan.exist_hi) = bl;
    }
  }
  // The picked edge must separate the cylinder from one end face; both end faces must carry the hole (a through-hole).
  const ON_BrepEdge& e = b.m_E[edge_index];
  if (e.TrimCount() != 2) { why = "the picked edge does not separate two faces"; return false; }
  const int fa = b.m_T[e.m_ti[0]].FaceIndexOf(), fb = b.m_T[e.m_ti[1]].FaceIndexOf();
  int endface = -1;
  if (fa == wall) endface = fb;
  else if (fb == wall) endface = fa;
  const bool corner_edge = std::find(sides.begin(), sides.end(), fa) != sides.end() && std::find(sides.begin(), sides.end(), fb) != sides.end() && fa != fb;
  if (corner_edge) {
    if (!want_corner) { why = "the picked edge is a corner edge of the prism, not a hole rim"; return false; }
  } else if (want_corner) {
    why = "the picked edge is not a corner edge between two side faces";
    return false;
  } else if (endface < 0 || std::find(ends.begin(), ends.end(), endface) == ends.end()) {
    why = "the picked edge is not on a hole rim (it must lie between the hole wall and an end face)";
    return false;
  } else if (blind && !is_boss && endface != ends[upper]) {
    why = "the picked edge is the foot of the blind hole (its floor), not the rim of its opening";
    return false;
  } else if (is_boss && endface != ends[upper] && endface != ends[floor_k]) {
    why = "the picked edge is not at the base or the top rim of the boss";
    return false;
  }
  plan.boss = is_boss;
  plan.boss_base = is_boss && endface == ends[floor_k];
  plan.top = !corner_edge && endface == ends[upper];
  plan.ax = ax;
  plan.R = cyl.circle.radius;
  plan.H = h1 - h0;
  plan.floor = blind ? h[floor_k] - h0 : 0.0;
  plan.c0 = cyl.circle.plane.origin + h0 * ax;
  plan.u = cyl.circle.plane.xaxis;
  // Outer polygon: the side planes are exact (the sewn Boolean result's own end-face loop is a keyhole whose
  // vertices were snapped by the sewing tolerance), so the corners are the intersections of consecutive side planes
  // with the lower end plane; consecutive = sharing an edge.
  std::map<int, std::set<int>> adj;
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    const ON_BrepEdge& ee = b.m_E[ei];
    if (ee.m_edge_index < 0 || ee.TrimCount() != 2) continue;
    const int x = b.m_T[ee.m_ti[0]].FaceIndexOf(), y = b.m_T[ee.m_ti[1]].FaceIndexOf();
    if (x == y || std::find(sides.begin(), sides.end(), x) == sides.end() || std::find(sides.begin(), sides.end(), y) == sides.end()) continue;
    adj[x].insert(y);
    adj[y].insert(x);
  }
  for (int sfi : sides) if (adj[sfi].size() != 2) { why = "the side faces do not form a single closed ring"; return false; }
  std::vector<int> ring{sides[0]};
  int prev = -1, cur = sides[0];
  while (true) {
    int nxt = -1;
    for (int cand : adj[cur]) if (cand != prev) { nxt = cand; break; }
    if (nxt < 0 || nxt == sides[0]) break;
    if (ring.size() > sides.size()) { why = "the side faces do not form a single closed ring"; return false; }
    ring.push_back(nxt);
    prev = cur;
    cur = nxt;
  }
  if (ring.size() != sides.size()) { why = "the side faces do not form a single closed ring"; return false; }
  const ON_Plane lower_plane(plan.c0, ax);
  std::vector<ON_3dPoint> poly;
  for (size_t i = 0; i < ring.size(); ++i) {
    ON_Plane pa, pb;
    b.m_F[ring[i]].SurfaceOf()->IsPlanar(&pa, tol);
    b.m_F[ring[(i + 1) % ring.size()]].SurfaceOf()->IsPlanar(&pb, tol);
    ON_3dPoint corner;
    if (!ON_Intersect(pa, pb, lower_plane, corner)) { why = "two adjacent side faces are parallel"; return false; }
    poly.push_back(corner);
  }
  // counter-clockwise about the axis
  double area = 0;
  for (size_t i = 0; i < poly.size(); ++i) area += ON_CrossProduct(poly[i] - plan.c0, poly[(i + 1) % poly.size()] - plan.c0) * ax;
  if (area < 0) std::reverse(poly.begin(), poly.end());
  if (poly.size() < 3) { why = "degenerate outer polygon"; return false; }
  for (size_t i = 0; i < poly.size(); ++i) {
    const ON_3dPoint& p = poly[i];
    const ON_3dPoint& q = poly[(i + 1) % poly.size()];
    ON_3dVector d = q - p;
    if (!d.Unitize()) continue;
    const ON_3dVector w = plan.c0 - p;
    const double dist = (w - (w * d) * d).Length();
    if (!(dist > plan.R + 10 * tol)) { why = "the hole is not inside the end face's outer boundary"; return false; }
  }
  plan.poly = poly;
  if (corner_edge) {
    // which polygon corner is the picked vertical edge? (the sewn Boolean result's vertices are snapped by up to its
    // sewing tolerance, so compare with a generous fraction of the plate size)
    ON_3dPoint mid = e.PointAtStart();
    mid = mid + (e.PointAtEnd() - mid) * 0.5;
    mid = mid - ((mid - plan.c0) * ax) * ax;  // onto the lower end plane
    double best = 1e300;
    for (size_t i = 0; i < poly.size(); ++i) {
      const double d = poly[i].DistanceTo(mid + ((poly[i] - mid) * ax) * ax);
      if (d < best) { best = d; plan.corner = static_cast<int>(i); }
    }
    if (!(best < 0.02 * bb.Diagonal().Length() + 10 * tol)) { why = "the picked corner edge is not a corner of the prism's outer polygon"; return false; }
  }
  return true;
}

bool JoinAndOrientSolid(const ON_Brep& all, double tol, ON_Brep& out, std::string& why);

// Builds the prism-with-hole from `plan` with the given blends at the lower / upper rim (std::nullopt = sharp).
// `corners` (corner index in g.poly -> radius) rounds those vertical prism edges with an exact cylindrical fillet.
bool BuildBlendedHolePlate(const HolePlatePlan& g, const std::optional<HoleRimBlend>& lo, const std::optional<HoleRimBlend>& hi, ON_Brep& out,
                           std::string& why, const std::map<int, double>& corners = {}) {
  if (!lo && !hi && corners.empty()) { why = "no blend requested"; return false; }
  double max_cap = 0;
  const double lw = lo ? lo->d_wall : 0, hw = hi ? hi->d_wall : 0;
  for (const auto* bl : {&lo, &hi}) {
    if (!*bl) continue;
    if (!((*bl)->d_wall > 0) || !((*bl)->d_cap > 0)) { why = "blend distances must be positive"; return false; }
    max_cap = std::max(max_cap, (*bl)->d_cap);
  }
  const bool blind = g.floor > 0;
  if (blind && lo) { why = "the closed end of a blind hole has no rim to blend"; return false; }
  if (!(lw + hw < g.H - g.floor)) { why = "the blends together consume the whole hole wall"; return false; }
  // the widened hole must stay inside the outer polygon
  for (size_t i = 0; i < g.poly.size(); ++i) {
    const ON_3dPoint& p = g.poly[i];
    ON_3dVector d = g.poly[(i + 1) % g.poly.size()] - p;
    if (!d.Unitize()) continue;
    const ON_3dVector w = g.c0 - p;
    if (!((w - (w * d) * d).Length() > g.R + max_cap + 1e-6)) { why = "the blend would run off the end face"; return false; }
  }
  const double tol = 1e-6;
  const ON_3dVector side_axis = ON_CrossProduct(g.ax, g.u);  // plane yaxis: (u, side_axis) is right-handed about ax
  // Outer boundary: the polygon with the requested corners rounded by tangent arcs of the given radius.
  struct BndSeg { ON_3dPoint a, b; bool arc = false; ON_3dPoint mid; };
  std::vector<BndSeg> bnd_segs;
  {
    const size_t n = g.poly.size();
    std::vector<ON_3dPoint> ca(n), cb(n), cm(n);  // per rounded corner: arc start, end, mid
    std::vector<double> tl(n, 0.0);
    for (const auto& kv : corners) {
      const size_t i = static_cast<size_t>(kv.first);
      const double r = kv.second;
      if (kv.first < 0 || i >= n || !(r > 0)) { why = "invalid corner fillet"; return false; }
      const ON_3dPoint &P = g.poly[i], &Pp = g.poly[(i + n - 1) % n], &Pn = g.poly[(i + 1) % n];
      ON_3dVector din = P - Pp, dout = Pn - P;
      if (!din.Unitize() || !dout.Unitize()) { why = "degenerate corner"; return false; }
      if (!(ON_CrossProduct(din, dout) * g.ax > 1e-9)) { why = "the picked corner is not a convex corner of the outer polygon"; return false; }
      const double phi = std::acos(std::max(-1.0, std::min(1.0, din * dout)));
      const double t = r * std::tan(phi / 2);
      tl[i] = t;
      ca[i] = P - t * din;
      cb[i] = P + t * dout;
      const ON_3dPoint C = ca[i] + r * ON_CrossProduct(g.ax, din);
      ON_3dVector toP = P - C;
      toP.Unitize();
      cm[i] = C + r * toP;
      // the rounded corner must keep clear of the hole's widened rim
      for (int k = 0; k <= 16; ++k) {
        const double a = static_cast<double>(k) / 16.0;
        ON_3dVector v = (ca[i] - C) * (1.0 - a) + (cb[i] - C) * a;
        v.Unitize();
        const ON_3dPoint q = C + r * v;
        const ON_3dVector w = q - g.c0;
        if (!((w - (w * g.ax) * g.ax).Length() > g.R + max_cap + 1e-6)) { why = "the corner fillet would run into the hole"; return false; }
      }
    }
    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;
      if (tl[i] + tl[j] > g.poly[i].DistanceTo(g.poly[j]) - 1e-9) { why = "the corner fillet radius is too large for the adjacent edge"; return false; }
      BndSeg line;
      line.a = tl[i] > 0 ? cb[i] : g.poly[i];
      line.b = tl[j] > 0 ? ca[j] : g.poly[j];
      bnd_segs.push_back(line);
      if (tl[j] > 0) { BndSeg arc; arc.a = ca[j]; arc.b = cb[j]; arc.mid = cm[j]; arc.arc = true; bnd_segs.push_back(arc); }
    }
  }
  // The rebuilt outline must still be a simple closed curve with the hole's widened rim well inside it.
  // (The per-corner tests above see only the corner's own neighbours: on a non-convex outline a large
  // rounding can cut across a far edge, and a hole sitting in the removed wedge would lie outside.)
  if (!corners.empty()) {
    auto to2 = [&](const ON_3dPoint& q) {
      const ON_3dVector w = q - g.c0;
      return std::array<double, 2>{w * g.u, w * side_axis};
    };
    std::vector<std::array<double, 2>> bp;  // dense boundary polyline
    for (const BndSeg& sg : bnd_segs) {
      if (!sg.arc) { bp.push_back(to2(sg.a)); continue; }
      ON_Arc arc(sg.a, sg.mid, sg.b);
      for (int k = 0; k < 64; ++k) bp.push_back(to2(arc.PointAt(arc.Domain().ParameterAt(static_cast<double>(k) / 64.0))));
    }
    const size_t m = bp.size();
    auto seg_inter = [](const std::array<double, 2>& a, const std::array<double, 2>& b, const std::array<double, 2>& c, const std::array<double, 2>& d) {
      auto cr = [](const std::array<double, 2>& o, const std::array<double, 2>& p, const std::array<double, 2>& q) {
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0]);
      };
      const double d1 = cr(a, b, c), d2 = cr(a, b, d), d3 = cr(c, d, a), d4 = cr(c, d, b);
      return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
    };
    for (size_t i = 0; i < m; ++i) {
      for (size_t j = i + 2; j < m; ++j) {
        if (i == 0 && j == m - 1) continue;  // adjacent through the closing segment
        if (seg_inter(bp[i], bp[(i + 1) % m], bp[j], bp[(j + 1) % m])) { why = "the rounded outline would cross itself"; return false; }
      }
    }
    const std::array<double, 2> hc2 = to2(g.c0);  // (0,0)
    bool inside = false;
    double mind = 1e300;
    for (size_t i = 0; i < m; ++i) {
      const auto &a = bp[i], &b = bp[(i + 1) % m];
      if (((a[1] > hc2[1]) != (b[1] > hc2[1])) && (hc2[0] < (b[0] - a[0]) * (hc2[1] - a[1]) / (b[1] - a[1]) + a[0])) inside = !inside;
      const double dx = b[0] - a[0], dy = b[1] - a[1], L2 = dx * dx + dy * dy;
      double t = L2 > 0 ? ((hc2[0] - a[0]) * dx + (hc2[1] - a[1]) * dy) / L2 : 0.0;
      t = std::max(0.0, std::min(1.0, t));
      mind = std::min(mind, std::hypot(a[0] + t * dx - hc2[0], a[1] + t * dy - hc2[1]));
    }
    if (!inside) { why = "the rounded outline no longer encloses the hole"; return false; }
    if (!(mind > g.R + max_cap + 1e-6)) { why = "the corner fillet would run into the hole"; return false; }
  }
  // 1. Exact prism with the plain hole: extrusion of (outer polygon, hole circle) along the axis.
  auto make_face = [&](const ON_3dPoint& origin, double hole_r) -> ON_Brep* {
    ON_Plane pl(origin, g.u, side_axis);
    ON_SimpleArray<ON_Curve*> bnd;
    const ON_3dVector shift = origin - g.c0;
    for (const BndSeg& sg : bnd_segs) {
      if (sg.arc) bnd.Append(new ON_ArcCurve(ON_Arc(sg.a + shift, sg.mid + shift, sg.b + shift)));
      else bnd.Append(new ON_LineCurve(sg.a + shift, sg.b + shift));
    }
    ON_Brep* f = ON_BrepTrimmedPlane(pl, bnd, false);
    if (!f) { for (int i = 0; i < bnd.Count(); ++i) delete bnd[i]; return nullptr; }
    if (!(hole_r > 0)) { f->Compact(); return f; }  // a full end face (the closed end of a blind hole)
    ON_ArcCurve* hc = new ON_ArcCurve(ON_Circle(pl, hole_r));
    // the hole runs opposite to the outer boundary
    double outer_area = 0;
    if (const ON_BrepLoop* ol = f->m_F[0].OuterLoop()) {
      for (int ti = 0; ti < ol->TrimCount(); ++ti) {
        const ON_BrepTrim* t = ol->Trim(ti);
        ON_3dPoint a = t->Edge()->PointAtStart(), c = t->Edge()->PointAtEnd();
        if (t->m_bRev3d) std::swap(a, c);
        outer_area += ON_CrossProduct(a - origin, c - origin) * g.ax;
      }
    }
    if (outer_area > 0) hc->Reverse();  // the circle is counter-clockwise about ax; the hole must run opposite to the outer boundary
    ON_SimpleArray<ON_Curve*> loop;
    loop.Append(hc);
    if (!f->NewPlanarFaceLoop(0, ON_BrepLoop::inner, loop, false)) { delete hc; delete f; return nullptr; }
    f->Compact();
    return f;
  };
  ON_Brep* plate = make_face(g.c0, g.R);
  if (!plate) { why = "could not build the end face with its hole"; return false; }
  {
    ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + g.H * g.ax));
    if (ON_BrepExtrudeFace(*plate, 0, path, true) < 0) { delete plate; why = "could not extrude the prism"; return false; }
  }
  kernel::Brep k;
  k.raw() = *plate;
  delete plate;
  // 2. Drop the faces being replaced: the wall, and each blended end.
  std::vector<int> drop;
  for (int f = 0; f < k.raw().m_F.Count(); ++f) {
    ON_Plane pl;
    const ON_Surface* s = k.raw().m_F[f].SurfaceOf();
    if (!s->IsPlanar(&pl, 1e-7)) {
      // the hole wall (the other non-planar faces are the rounded prism corners: their points lie farther from the axis)
      const ON_Interval du = s->Domain(0), dv = s->Domain(1);
      const ON_3dPoint q = s->PointAt(du.ParameterAt(0.37), dv.ParameterAt(0.41));
      const ON_3dVector w = q - g.c0;
      if (std::fabs((w - (w * g.ax) * g.ax).Length() - g.R) < 1e-6) drop.push_back(f);
      continue;
    }
    if (std::fabs(pl.zaxis * g.ax) > 1.0 - 1e-9) {
      const double hh = (pl.origin - g.c0) * g.ax;
      if (((lo || blind) && std::fabs(hh) < 1e-6) || (hi && std::fabs(hh - g.H) < 1e-6)) drop.push_back(f);
    }
  }
  std::sort(drop.rbegin(), drop.rend());
  try {
    for (int f : drop) k.DeleteFace(f, false);
  } catch (const std::exception& ex) { why = ex.what(); return false; }
  ON_Brep all = k.raw();
  // 3. New end faces, shortened wall, revolved blend patches.
  auto add = [&](ON_Brep* p) -> bool {
    if (!p) return false;
    all.Append(*p);
    delete p;
    return true;
  };
  auto revolve = [&](ON_Curve* crv) -> ON_Brep* {
    ON_RevSurface* rev = new ON_RevSurface();
    rev->m_curve = crv;
    rev->m_axis = ON_Line(g.c0, g.c0 + g.ax);
    rev->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
    rev->m_t = rev->m_angle;
    rev->m_bTransposed = false;
    return ON_BrepRevSurface(rev, false, false);
  };
  auto patch = [&](const HoleRimBlend& bl, bool upper) -> ON_Brep* {
    const double z = upper ? g.H : 0.0, dir = upper ? -1.0 : 1.0;  // dir: from the end plane into the plate
    if (!bl.chamfer) {
      const double r = bl.d_wall;
      const ON_3dPoint centre = g.c0 + (g.R + r) * g.u + (z + dir * r) * g.ax;
      const ON_Circle circ(ON_Plane(centre, g.u, g.ax), r);
      return revolve(new ON_ArcCurve(ON_Arc(circ, upper ? ON_Interval(ON_PI / 2, ON_PI) : ON_Interval(ON_PI, 1.5 * ON_PI))));
    }
    const ON_3dPoint on_wall = g.c0 + g.R * g.u + (z + dir * bl.d_wall) * g.ax;
    const ON_3dPoint on_cap = g.c0 + (g.R + bl.d_cap) * g.u + z * g.ax;
    return revolve(new ON_LineCurve(on_wall, on_cap));
  };
  const double wall_lo = blind ? g.floor : (lo ? lo->d_wall : 0.0), wall_hi = hi ? hi->d_wall : 0.0;
  if (blind) {
    // the closed end becomes a full face (no hole) and the hole gets its flat floor
    const ON_Plane fp(g.c0 + g.floor * g.ax, g.u, side_axis);
    const ON_ArcCurve floor_circle{ON_Circle(fp, g.R)};
    if (!add(make_face(g.c0, 0.0)) || !add(ON_BrepTrimmedPlane(fp, floor_circle))) { why = "could not build the closed end / floor of the blind hole"; return false; }
  }
  {
    ON_Cylinder wall(ON_Circle(ON_Plane(g.c0 + wall_lo * g.ax, g.u, side_axis), g.R), g.H - wall_lo - wall_hi);
    if (!add(ON_BrepCylinder(wall, false, false))) { why = "could not build the shortened hole wall"; return false; }
  }
  if (lo) {
    if (!add(make_face(g.c0, g.R + lo->d_cap)) || !add(patch(*lo, false))) { why = "could not build the lower rim blend"; return false; }
  }
  if (hi) {
    if (!add(make_face(g.c0 + g.H * g.ax, g.R + hi->d_cap)) || !add(patch(*hi, true))) { why = "could not build the upper rim blend"; return false; }
  }
  return JoinAndOrientSolid(all, tol, out, why);
}

// Joins the loose exact pieces along their shared circles / lines and orients the shell outward; false (with the reason)
// unless the result is a valid closed solid.
bool JoinAndOrientSolid(const ON_Brep& all, double tol, ON_Brep& out, std::string& why) {
  kernel::Brep res;
  res.raw() = all;
  try {
    res.JoinNakedEdges(tol);
    res.UnifyNormals();
  } catch (const std::exception& ex) {
    why = ex.what();
    return false;
  }
  if (!res.raw().IsSolid() && res.raw().IsManifold() && res.raw().IsValid()) {
    // Face orientations disagree (the revolved patches and the extruded faces come out with their own senses): find the
    // smallest set of face flips that makes the shell consistently oriented, then orient it outward.
    ON_Brep& rb = res.raw();
    const int nf = rb.m_F.Count();
    bool fixed = false;
    // (at most 2^nf subsets, nf <= 10 here; smallest subsets first)
    for (int pop = 1; pop <= nf && !fixed && nf <= 12; ++pop) {
      for (unsigned mask = 1; mask < (1u << nf) && !fixed; ++mask) {
        int bits = 0;
        for (unsigned m = mask; m; m &= m - 1) ++bits;  // portable popcount
        if (bits != pop) continue;
        ON_Brep t = rb;
        for (int i = 0; i < nf; ++i) if (mask & (1u << i)) t.FlipFace(t.m_F[i]);
        if (t.SolidOrientation() != 0) { rb = t; fixed = true; }
      }
    }
    if (fixed && rb.SolidOrientation() < 0) rb.Flip();
  }
  if (!res.raw().IsSolid() || !res.raw().IsValid()) {
    why = "the assembled pieces did not form a valid closed solid";
    return false;
  }
  out = res.raw();
  return true;
}

// Exact fillet / chamfer at the BASE and/or the top rim of a cylindrical BOSS standing on a straight-edged plate (what
// BooleanUnion of a box and a cylinder resting on its top face gives; the plan comes from ClassifyHolePlateRim's boss
// reading of the exact faces, not from the sewn keyhole loop). The base blend is concave and ADDS material: a torus
// (centre outside the wall, major radius R + r, minor r) or a cone, sharing its geometry with the hole rim's patch; the
// plate's top face gets a circular hole of radius R + (base cap setback), the wall is shortened, and the top blend is the
// ordinary convex rim blend (centre inside the wall) closed by a smaller cap disc.
bool BuildBlendedBoss(const HolePlatePlan& g, const std::optional<HoleRimBlend>& base, const std::optional<HoleRimBlend>& top, ON_Brep& out,
                      std::string& why) {
  if (!base && !top) { why = "no blend requested"; return false; }
  for (const auto* bl : {&base, &top}) {
    if (!*bl) continue;
    if (!((*bl)->d_wall > 0) || !((*bl)->d_cap > 0)) { why = "blend distances must be positive"; return false; }
  }
  const double zp = g.floor, Hc = g.H - zp;
  const double bw = base ? base->d_wall : 0.0, tw = top ? top->d_wall : 0.0;
  if (!(zp > 0) || !(Hc > 0)) { why = "degenerate boss"; return false; }
  if (!(bw + tw < Hc)) { why = "the blends together consume the whole boss wall"; return false; }
  if (top && !(top->d_cap < g.R)) { why = "the top blend would consume the whole boss cap"; return false; }
  const double foot = g.R + (base ? base->d_cap : 0.0);
  for (size_t i = 0; i < g.poly.size(); ++i) {
    const ON_3dPoint& p = g.poly[i];
    ON_3dVector d = g.poly[(i + 1) % g.poly.size()] - p;
    if (!d.Unitize()) continue;
    const ON_3dVector w = g.c0 - p;
    if (!((w - (w * d) * d).Length() > foot + 1e-6)) { why = "the base blend would run off the plate"; return false; }
  }
  const double tol = 1e-6;
  const ON_3dVector side_axis = ON_CrossProduct(g.ax, g.u);
  auto make_face = [&](const ON_3dPoint& origin, double hole_r) -> ON_Brep* {
    ON_Plane pl(origin, g.u, side_axis);
    ON_SimpleArray<ON_Curve*> bnd;
    const ON_3dVector shift = origin - g.c0;
    for (size_t i = 0; i < g.poly.size(); ++i) bnd.Append(new ON_LineCurve(g.poly[i] + shift, g.poly[(i + 1) % g.poly.size()] + shift));
    ON_Brep* f = ON_BrepTrimmedPlane(pl, bnd, false);
    if (!f) { for (int i = 0; i < bnd.Count(); ++i) delete bnd[i]; return nullptr; }
    if (!(hole_r > 0)) { f->Compact(); return f; }
    ON_ArcCurve* hc = new ON_ArcCurve(ON_Circle(pl, hole_r));
    double outer_area = 0;
    if (const ON_BrepLoop* ol = f->m_F[0].OuterLoop()) {
      for (int ti = 0; ti < ol->TrimCount(); ++ti) {
        const ON_BrepTrim* t = ol->Trim(ti);
        ON_3dPoint a = t->Edge()->PointAtStart(), c = t->Edge()->PointAtEnd();
        if (t->m_bRev3d) std::swap(a, c);
        outer_area += ON_CrossProduct(a - origin, c - origin) * g.ax;
      }
    }
    if (outer_area > 0) hc->Reverse();
    ON_SimpleArray<ON_Curve*> loop;
    loop.Append(hc);
    if (!f->NewPlanarFaceLoop(0, ON_BrepLoop::inner, loop, false)) { delete hc; delete f; return nullptr; }
    f->Compact();
    return f;
  };
  // the plate: a plain prism (full bottom face), its top face then replaced by one with the circular footing
  ON_Brep* plate = make_face(g.c0, 0.0);
  if (!plate) { why = "could not build the plate"; return false; }
  {
    ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + zp * g.ax));
    if (ON_BrepExtrudeFace(*plate, 0, path, true) < 0) { delete plate; why = "could not extrude the plate"; return false; }
  }
  kernel::Brep k;
  k.raw() = *plate;
  delete plate;
  std::vector<int> drop;
  for (int f = 0; f < k.raw().m_F.Count(); ++f) {
    ON_Plane pl;
    if (k.raw().m_F[f].SurfaceOf()->IsPlanar(&pl, 1e-7) && std::fabs(pl.zaxis * g.ax) > 1.0 - 1e-9 && std::fabs((pl.origin - g.c0) * g.ax - zp) < 1e-6) drop.push_back(f);
  }
  if (drop.size() != 1) { why = "could not find the plate's top face"; return false; }
  try { k.DeleteFace(drop[0], false); } catch (const std::exception& ex) { why = ex.what(); return false; }
  ON_Brep all = k.raw();
  auto add = [&](ON_Brep* p) -> bool {
    if (!p) return false;
    all.Append(*p);
    delete p;
    return true;
  };
  auto revolve = [&](ON_Curve* crv) -> ON_Brep* {
    ON_RevSurface* rev = new ON_RevSurface();
    rev->m_curve = crv;
    rev->m_axis = ON_Line(g.c0, g.c0 + g.ax);
    rev->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
    rev->m_t = rev->m_angle;
    rev->m_bTransposed = false;
    return ON_BrepRevSurface(rev, false, false);
  };
  if (!add(make_face(g.c0 + zp * g.ax, foot))) { why = "could not build the plate's top face"; return false; }
  if (base) {
    if (!base->chamfer) {
      const double r = base->d_wall;
      const ON_3dPoint centre = g.c0 + (g.R + r) * g.u + (zp + r) * g.ax;
      if (!add(revolve(new ON_ArcCurve(ON_Arc(ON_Circle(ON_Plane(centre, g.u, g.ax), r), ON_Interval(ON_PI, 1.5 * ON_PI)))))) { why = "could not build the base fillet"; return false; }
    } else {
      if (!add(revolve(new ON_LineCurve(g.c0 + g.R * g.u + (zp + base->d_wall) * g.ax, g.c0 + (g.R + base->d_cap) * g.u + zp * g.ax)))) { why = "could not build the base chamfer"; return false; }
    }
  }
  {
    ON_Cylinder wall(ON_Circle(ON_Plane(g.c0 + (zp + bw) * g.ax, g.u, side_axis), g.R), Hc - bw - tw);
    if (!add(ON_BrepCylinder(wall, false, false))) { why = "could not build the boss wall"; return false; }
  }
  if (top) {
    if (!top->chamfer) {
      const double r = top->d_wall;
      const ON_3dPoint centre = g.c0 + (g.R - r) * g.u + (g.H - r) * g.ax;
      if (!add(revolve(new ON_ArcCurve(ON_Arc(ON_Circle(ON_Plane(centre, g.u, g.ax), r), ON_Interval(0.0, ON_PI / 2)))))) { why = "could not build the top fillet"; return false; }
    } else {
      if (!add(revolve(new ON_LineCurve(g.c0 + g.R * g.u + (g.H - top->d_wall) * g.ax, g.c0 + (g.R - top->d_cap) * g.u + g.H * g.ax)))) { why = "could not build the top chamfer"; return false; }
    }
  }
  {
    const ON_Plane cp(g.c0 + g.H * g.ax, g.u, side_axis);
    const ON_ArcCurve cap_circle{ON_Circle(cp, g.R - (top ? top->d_cap : 0.0))};
    if (!add(ON_BrepTrimmedPlane(cp, cap_circle))) { why = "could not build the boss cap"; return false; }
  }
  return JoinAndOrientSolid(all, tol, out, why);
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

struct VertexPick { ObjectId id = kNoObject; int vertex = -1; double dist = 0; };

// Nearest ON_BrepVertex to `p` across every visible, unlocked B-rep object
// in the scene - the vertex-level sibling of PickFace/PickEdge above, for
// FilletVertexCommand's own "pick a solid corner" entry point.
std::optional<VertexPick> PickVertex(CommandContext& ctx, Point3d p) {
  std::optional<VertexPick> best;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::optional<ON_Brep> b = BrepOfObject(o);
    if (!b) continue;
    for (int i = 0; i < b->m_V.Count(); ++i) {
      const ON_BrepVertex& v = b->m_V[i];
      if (v.m_vertex_index < 0) continue;
      const double d = v.point.DistanceTo(p);
      if (!best || d < best->dist) best = VertexPick{o.id, i, d};
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

// Offsets a NURBS surface by `d` the same way as OffsetBy above, EXCEPT
// the offset points come from sampling the real surface on a `samples_u`
// x `samples_v` parameter grid (PointAt/NormalAt, always a real point and
// a real normal ON the true surface) instead of translating its control
// points - which can sit well off the true surface (e.g. the extra
// control points any rational representation of a circle always
// carries), so translating THEM along their own local normal does not
// converge to a true offset as the net densifies on its own (confirmed
// empirically: an earlier version of this file tried densifying OffsetBy's
// own control net via knot insertion instead of this sampling approach,
// and it measurably made a real curved-face FilletEdge fixture's own
// result WORSE, not better - see BuildFilletAdaptive's own dated
// PARITY_MAP.md note for the full story). Every sampled point here, by
// contrast, is genuinely ON the surface, so increasing the grid
// resolution genuinely converges towards the true offset surface - the
// same point-sampling convergence BuildBlendSurfaceG1Adaptive/G2Adaptive
// (geom/BlendSurface.cpp) already rely on elsewhere in this file, just
// for an offset instead of a blend row. The grid is reassembled into a
// NURBS surface via the kernel's own `NurbsSurface::InterpolateThroughGrid`
// (surface.h/surface_edit.cpp) - exact tensor-product interpolation
// through every sampled point, not a least-squares fit - so the result
// is exact on a plane regardless of resolution (every sampled point and
// its own constant normal direction stay within the plane, and
// interpolating through in-plane data can't leave it either), matching
// OffsetBy's own exactness there.
bool OffsetBySampled(const ON_NurbsSurface& in, double d, int samples_u, int samples_v, ON_NurbsSurface& out) {
  samples_u = std::max(samples_u, 4);
  samples_v = std::max(samples_v, 4);
  const ON_Interval du = in.Domain(0), dv = in.Domain(1);
  // A CLOSED direction's own parameter max is the SAME physical point as
  // its min (e.g. a cylinder wall's full-circumference seam) - sampling
  // both would hand InterpolateThroughGrid a row/column whose own last
  // two points coincide, a zero-length chord that starves its shared
  // chord-length parameterization (confirmed directly: this is exactly
  // what made an earlier version of this function return a wildly wrong
  // surface on a real solid-cylinder FilletEdge fixture - PARITY_MAP.md's
  // own dated note has the full story). Sampling only up to (not
  // including) the max in a closed direction avoids the duplicate
  // altogether; an open direction keeps both ends included, same as
  // before, so the offset genuinely covers the original surface's own
  // full domain there.
  const bool closed_u = in.IsClosed(0), closed_v = in.IsClosed(1);
  std::vector<Point3d> grid;
  grid.reserve(static_cast<size_t>(samples_u) * static_cast<size_t>(samples_v));
  for (int i = 0; i < samples_u; ++i) {
    const double u = du.ParameterAt(static_cast<double>(i) / (closed_u ? samples_u : samples_u - 1));
    for (int j = 0; j < samples_v; ++j) {
      const double v = dv.ParameterAt(static_cast<double>(j) / (closed_v ? samples_v : samples_v - 1));
      const Point3d p = in.PointAt(u, v);
      ON_3dVector n = in.NormalAt(u, v);
      if (!n.Unitize()) return false;
      grid.push_back(p + n * d);
    }
  }
  kernel::NurbsSurface result;
  if (kernel::NurbsSurface::InterpolateThroughGrid(grid, samples_u, samples_v, result) != kernel::Result::Ok) return false;
  out = result.raw();
  // InterpolateThroughGrid's own chord-length parameterization has no
  // relation at all to `in`'s own domain - left as-is, a caller seeding a
  // closest-point search on the ORIGINAL surface from a (u, v) read off
  // THIS offset result (exactly what BuildFillet's own per-spine-sample
  // loop below does, the same way it already does for OffsetBy's result)
  // would be seeding it with a parameter value from an unrelated domain,
  // landing the search on the wrong part of the surface entirely -
  // confirmed directly: this is exactly what made an earlier version of
  // this function (without the fix on the next two lines) report a
  // wildly wrong max_gap on a real solid-cylinder FilletEdge fixture,
  // even though the offset surface it built was itself already a good,
  // converging approximation of the true offset - see PARITY_MAP.md's own
  // dated note for the full story. Rescaling the result's own domain back
  // onto `in`'s domain fixes this with no shape change at all (`SetDomain`
  // only reparameterizes): the grid above was sampled at `in`-domain
  // fractions in the same order InterpolateThroughGrid lays its own
  // output's (u, v) out in, so a seed point near a given fraction of
  // `in`'s domain is now near the SAME fraction of the offset result's
  // domain too.
  out.SetDomain(0, du.Min(), du.Max());
  out.SetDomain(1, dv.Min(), dv.Max());
  return true;
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
  std::vector<double> v_params;        // fillet.Domain(1) parameter for each row - lets a caller sample fillet.PointAt(u, v_params[i]) and land exactly on row i's own arc (LoftRows builds an interpolating spline through these exact parameter values)
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
// `offset_samples` is 0 by default (OffsetBy's own per-CV translation,
// unchanged behavior for every pre-existing caller) or, when positive,
// the (offset_samples x offset_samples) grid resolution to build the
// offset surfaces with OffsetBySampled instead - the only thing
// BuildFilletAdaptive below varies between its own successive attempts.
FilletBuild BuildFillet(const ON_NurbsSurface& a, const ON_NurbsSurface& b, const std::function<double(double)>& radius_at, double tol,
                        int offset_samples = 0) {
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
  ON_NurbsSurface off_a_raw, off_b_raw;
  if (offset_samples > 0) {
    if (!OffsetBySampled(a, sa * r0, offset_samples, offset_samples, off_a_raw) ||
        !OffsetBySampled(b, sb * r0, offset_samples, offset_samples, off_b_raw)) {
      out.error = "could not build a sampled offset surface at this resolution";
      return out;
    }
  } else {
    off_a_raw = OffsetBy(a, sa * r0).raw();
    off_b_raw = OffsetBy(b, sb * r0).raw();
  }
  std::vector<IntersectionCurve> ssx = IntersectSurfaces(off_a_raw, off_b_raw, opt);
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
    out.v_params.push_back(spine.params[i]);
    out.uv_a.emplace_back(ua, va);
    out.uv_b.emplace_back(ub, vb);
  }
  if (rows.size() < 2) { out.error = "too few valid spine samples (radius likely larger than the surfaces support)"; return out; }
  // For a closed (periodic) edge - e.g. a solid cylinder's own flat-top
  // rim - the spine's first and last samples are the SAME physical
  // location, but IntersectSurfaces' marching tracer doesn't itself detect
  // loop closure, so they routinely land a full sample-spacing or more
  // apart (confirmed by testing: up to several percent of the loop's own
  // size, not numerical noise). Left as-is, that's a real geometric gap in
  // the lofted fillet surface's own two ends, not just a topological one -
  // no amount of downstream vertex-welding can close a gap that size.
  // Snap the last row to an exact copy of the first (not just its
  // position - contact_a/contact_b/uv too, so every derived curve stays
  // consistent) whenever the intersection curve itself is closed.
  if (spine.closed && rows.size() >= 3) {
    rows.back() = rows.front();
    out.spine_pts.back() = out.spine_pts.front();
    out.contact_a.back() = out.contact_a.front();
    out.contact_b.back() = out.contact_b.front();
    out.uv_a.back() = out.uv_a.front();
    out.uv_b.back() = out.uv_b.front();
  }
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

// Adaptive, tolerance-enforcing counterpart to BuildFillet() above -
// closes PARITY_MAP.md's own long-standing "Rolling-ball blend surface
// accuracy on freeform surfaces... the recorded max_gap quality signal is
// never enforced against a tolerance" gap. BuildFillet() already computes
// max_gap (how far each contact point lands from the true radius - the
// one real error the offset step can introduce on a curved face; it is
// exactly zero on a plane), but previously nothing ever compared it
// against anything.
//
// This retries with a progressively finer OffsetBySampled() grid (see its
// own doc comment above for why point sampling, not OffsetBy's per-CV
// translation, is what this needed) whenever the achieved gap is still
// above `max_gap`, tracking whichever attempt reached the SMALLEST gap
// so far - not simply the latest one, since a finer grid is not
// guaranteed to beat a coarser one on every single attempt (sampling
// noise near a difficult region of the surface), only to converge on
// average as the grid densifies; always taking "the latest" could
// silently regress on an unlucky attempt (confirmed possible in exactly
// this way during development - see this function's own git history and
// PARITY_MAP.md's dated note for the real fixture that caught the
// earlier knot-insertion approach doing exactly this). A plane (or a
// pair where the very first, coarsest attempt already certifies) needs
// no retry at all and returns on the first try, same as a bare
// BuildFillet() call would, since level 0 there is already the identical
// unrefined OffsetBy() construction (offset_samples=0) for maximum
// backward compatibility with every other existing caller.
//
// `max_refine_levels` bounds the sampling-grid growth (each level
// doubles the grid resolution in both directions, starting from a
// modest base) - a budget-exhausted certification still returns the
// best (smallest-gap) attempt reached, with `max_gap` honestly reporting
// what was actually achieved rather than silently claiming success, the
// same "best effort, honestly measured" contract
// BuildBlendSurfaceG1Adaptive/G2Adaptive (geom/BlendSurface.cpp) already
// established for the unrelated Hermite-blend construction.
FilletBuild BuildFilletAdaptive(const ON_NurbsSurface& a, const ON_NurbsSurface& b,
                                 const std::function<double(double)>& radius_at, double tol,
                                 double max_gap, int max_refine_levels = 5) {
  FilletBuild best = BuildFillet(a, b, radius_at, tol, /*offset_samples=*/0);
  // An outright failure here (e.g. "the offset surfaces do not meet" for a
  // radius genuinely too large for the geometry) is not something a finer
  // sampling grid should ever be asked to "rescue" - confirmed directly
  // this is a real, not just a theoretical, risk: an earlier version of
  // this function retried unconditionally on any failure too, and on a
  // real adversarial fixture (radius 5 on a box edge only 2 units tall,
  // more than double what the geometry can resolve) the finer
  // OffsetBySampled grid found a spurious near-degenerate "intersection"
  // the unrefined OffsetBy correctly found none of, reporting success on
  // a fillet that removed essentially no material (Volume stayed at the
  // ORIGINAL box's own 1000, not a real cut) instead of the correct,
  // honest failure - see this function's own dated PARITY_MAP.md note.
  // Escalating a genuine accuracy problem (ok already true, gap too
  // large) is this function's own actual job; rescuing an outright
  // infeasibility is not.
  if (!best.ok) return best;
  int grid = 8;
  for (int level = 0; level < max_refine_levels && best.max_gap > max_gap; ++level, grid *= 2) {
    FilletBuild next = BuildFillet(a, b, radius_at, tol, /*offset_samples=*/grid);
    if (next.ok && next.max_gap < best.max_gap) best = next;
  }
  return best;
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
    out.v_params.push_back(t);
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
    out.v_params.push_back(t);
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

// Builds the mesh-fallback cutter as a wedge swept along the spine, with a
// triangular (spine, contact_b, contact_a) cross-section at each sample -
// not an independently-swept circular tube. An earlier, oversized
// (radius * 1.05) circular-tube version gouged past the fillet's own
// contact curves without anything re-trimming the cavity back down to
// them, so the caller's later MergeAndWeld between the cut cavity and the
// analytic fillet ribbon had almost nothing to actually weld (measured: 10
// of ~10,600 vertices on a real test case) - a real, previously-hidden
// bug, not something a bigger weld tolerance should paper over. Using the
// wedge's own two known rail edges - spine-to-contact_a (running along
// face A) and spine-to-contact_b (running along face B) - as the cutter's
// side faces means the cavity this leaves behind is bounded EXACTLY by
// contact_curve_a and contact_curve_b, the very same curves the fillet
// ribbon itself is built from, so the two meshes' boundaries coincide
// pointwise (up to sharing the same t-parameterization) rather than
// approximately.
kernel::Mesh SweepTubeCutter(const std::vector<Point3d>& spine_in, const std::vector<Point3d>& contact_a_in,
                              const std::vector<Point3d>& contact_b_in, bool periodic) {
  // A spine sampled from a closed (periodic) edge curve - e.g. a solid
  // cylinder's own flat-top rim, or the rim where a bore meets a plate -
  // loops back on its own start point. LoftClosedRings() always caps both
  // ends flat, which for a periodic spine leaves two coincident end caps
  // sitting on top of each other right where the loop closes instead of a
  // manifold seam; use LoftPeriodicRings() (no caps, last ring bands
  // straight back to the first) instead whenever the caller says the
  // *edge curve itself* is closed (ON_Curve::IsClosed(), checked once by
  // the caller against the real curve - not inferred here from how close
  // the sampled spine's first and last points happen to land, which can be
  // off by a whole sample spacing or more: BuildFillet's spine comes from
  // marching an SSX curve that doesn't itself special-case detecting its
  // own loop closure, so a periodic spine's two end samples routinely land
  // a full ring-spacing or two apart, not within any tight numeric
  // tolerance of each other).
  std::vector<Point3d> spine = spine_in, ca = contact_a_in, cb = contact_b_in;
  periodic = periodic && spine.size() >= 3;
  if (periodic) { spine.pop_back(); ca.pop_back(); cb.pop_back(); }  // the closing sample duplicates the first ring's location
  const size_t n = std::min({spine.size(), ca.size(), cb.size()});
  std::vector<std::vector<Point3d>> rings;
  rings.reserve(n);
  // Winding: (spine, contact_a, contact_b) - verified empirically to match
  // LoftClosedRings()/LoftPeriodicRings()' "CCW as seen from ahead along
  // the loft direction" convention for a wedge built this way. A wrong
  // order here still produces a topologically closed mesh, just an
  // inside-out one - confirmed on a real cylinder-rim FilletEdge case: the
  // (spine, contact_b, contact_a) order gave a closed cutter with a
  // negative volume (-14.35 on the test case), not assumed correct just
  // because IsClosedManifold() passed.
  for (size_t i = 0; i < n; ++i) rings.push_back({spine[i], ca[i], cb[i]});
  return periodic ? kernel::Mesh::LoftPeriodicRings(rings) : kernel::Mesh::LoftClosedRings(rings);
}

// Builds a real, closed SOLID representing the material between the flat
// wedge SweepTubeCutter() removes and the true rolling-ball fillet
// surface `built` - the "lune" a caller unions onto the cutter's own
// remainder solid instead of trying to weld an open fillet ribbon onto an
// already fully-closed mesh (which two closed solids from a plain boolean
// DIFFERENCE always are - there's no naked boundary left to weld an extra
// patch into, which is the real reason the old weld-based approach could
// never close: `IsClosedManifold()` on the difference result was already
// true *before* the ribbon was ever added, so adding it just produced
// overlapping, non-manifold geometry, not a gap to be welded shut).
// Each cross-section ring is (spine, contact_a, [[arc_samples interior
// points sampled from built's own true arc]], contact_b) - the same
// planar circular arc BuildFillet/BuildPlanarVariableFillet/
// BuildPlaneCylinderVariableFillet already computed contact_a/contact_b
// from, so this ring is genuinely planar and simple (required for
// LoftClosedRings()' end caps). A boolean UNION between this solid and
// the cutter's own remainder is robust to the two meshes' independent
// tessellations not matching vertex-for-vertex - unlike a manual weld,
// Manifold's own CSG algorithm computes the real geometric intersection
// regardless.
kernel::Mesh BuildFilletSolid(const std::vector<Point3d>& spine_in, const std::vector<Point3d>& contact_a_in,
                               const std::vector<Point3d>& contact_b_in, const std::vector<double>& v_params_in,
                               const ON_NurbsSurface& built, bool periodic, int arc_samples = 5) {
  std::vector<Point3d> spine = spine_in, ca = contact_a_in, cb = contact_b_in;
  std::vector<double> v_params = v_params_in;
  periodic = periodic && spine.size() >= 3;
  if (periodic) { spine.pop_back(); ca.pop_back(); cb.pop_back(); if (!v_params.empty()) v_params.pop_back(); }
  const size_t n = std::min({spine.size(), ca.size(), cb.size(), v_params.size()});
  const ON_Interval u_domain = built.Domain(0);
  std::vector<std::vector<Point3d>> rings;
  rings.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    std::vector<Point3d> ring;
    ring.push_back(spine[i]);
    ring.push_back(ca[i]);
    for (int k = 1; k <= arc_samples; ++k) {
      const double u = u_domain.ParameterAt(static_cast<double>(k) / (arc_samples + 1));
      const ON_3dPoint p = built.PointAt(u, v_params[i]);
      ring.emplace_back(p.x, p.y, p.z);
    }
    ring.push_back(cb[i]);
    rings.push_back(std::move(ring));
  }
  return periodic ? kernel::Mesh::LoftPeriodicRings(rings) : kernel::Mesh::LoftClosedRings(rings);
}

}  // namespace

namespace {
// The general (approximate rolling-ball / ruled) construction of FilletSrf/ChamferSrf/VariableFilletSrf/VariableChamferSrf
// between two surfaces, shared by FilletTwoSurfacesCommand::Run and RebuildSurfaceFillet (UpdateHistory). Returns false with
// fb2.error set when no fillet can be built.
bool BuildSurfaceFilletBetween(const ON_NurbsSurface& sa, const ON_NurbsSurface& sb, double r0, double r1, bool variable, bool chamfer,
                               double tol, FilletBuild& fb2, ON_NurbsSurface& result_surface) {
  auto radius_at = [&](double t) { return r0 + (r1 - r0) * t; };
    // Same exact plane+perpendicular-cylinder closed form FilletEdge uses
    // for its Radii= option (see BuildPlaneCylinderVariableFillet's own
    // comment) - here the two faces are independently picked rather than a
    // brep edge, so there is no ready-made edge curve; get one the same way
    // BuildFillet itself finds a spine, by intersecting the two (untouched,
    // zero-offset) surfaces directly - for a face pair that actually share
    // a boundary, that IS the shared edge curve.
    if (variable && r0 != r1) {
      const double scale = std::max(SurfaceScale(sa), SurfaceScale(sb));
      const double geo_tol = scale > 0 ? scale * 1e-6 : 0.0;
      IntersectOptions opt;
      opt.tolerance = std::max(std::max(tol, geo_tol), 1e-6);
      opt.mesh_tolerance = std::max(opt.tolerance * 4, geo_tol > 0 ? geo_tol * 4 : 1e-4);
      std::vector<IntersectionCurve> touch = IntersectSurfaces(sa, sb, opt);
      if (!touch.empty()) {
        const IntersectionCurve* best = &touch.front();
        for (const IntersectionCurve& c : touch) if (c.Length() > best->Length()) best = &c;
        fb2 = BuildPlaneCylinderVariableFillet(sa, sb, best->curve, radius_at, tol);
      }
    }
    if (!fb2.ok) {
      // Adaptive, not a bare fixed-net call - closes PARITY_MAP.md's own
      // "Rolling-ball blend surface accuracy on freeform surfaces...
      // max_gap is never enforced against a tolerance" gap for this
      // command's own general (curved-face) fallback. Same scale-aware
      // floor FilletEdgeCommand's own blend/tapered-fillet wiring already
      // uses elsewhere in this file (SurfaceScale * 1e-3, clamped against
      // the document tolerance), so a tiny or huge surface pair gets a
      // sane target either way.
      const double max_gap = std::max(std::max(SurfaceScale(sa), SurfaceScale(sb)) * 1e-3, tol * 10);
      fb2 = BuildFilletAdaptive(sa, sb, radius_at, tol, max_gap);
    }
  if (!fb2.ok) return false;
  if (chamfer) {  // ruled between the two contact curves (FilletTwoSurfacesCommand::RuledBetween)
    ON_NurbsCurve b2 = fb2.contact_curve_b;
    b2.SetDomain(fb2.contact_curve_a.Domain().Min(), fb2.contact_curve_a.Domain().Max());
    result_surface = RuledSurface(fb2.contact_curve_a, b2);
  } else {
    result_surface = fb2.fillet;
  }
  return true;
}
}  // namespace

// ---------------------------------------------------------------------------
// FilletSrf / ChamferSrf: pick two faces/surfaces directly.
// ---------------------------------------------------------------------------

class FilletTwoSurfacesCommand : public Command {
 public:
  enum class Mode { Fillet, Chamfer, VariableFillet, VariableChamfer };
  // Same three settings as FilletEdgeCommand's own RailType (cmd_fillet.cpp)
  // - a separate enum rather than a shared one purely because this class is
  // defined ahead of FilletEdgeCommand in this file, not a difference in
  // meaning.
  enum class RailType { RollingBall, DistFromEdge, DistBetweenRails };
  explicit FilletTwoSurfacesCommand(Mode m) : mode_(m) {}
  void Begin(CommandContext&) override {
    options = {{"Radius", FormatNumber(radius_), {}, true, false}};
    if (mode_ == Mode::VariableFillet || mode_ == Mode::VariableChamfer) options.push_back({"EndRadius", FormatNumber(end_radius_), {}, true, false});
    // Rho (strictly between 0 and 1): kernel::FilletConvexEdgeConic/
    // FilletConcaveEdgeConic's own exact ellipse/parabola/hyperbola
    // cross-section, the same option FilletEdgeCommand's own single-edge
    // Fillet mode already exposes (cmd_fillet.cpp). Fillet mode only - the
    // approximate RuledBetween chamfer path and the plane+cylinder/general
    // variable-radius lofts have no non-circular cross-section to select
    // between, so there is nothing for Rho to switch there.
    if (mode_ == Mode::Fillet) {
      options.push_back({"Rho", "", {}, true, false});
      // Distance2 (Rho only): the conic's own independent second setback
      // distance_j, the same option FilletEdgeCommand's own single-edge Rho
      // wiring already exposes (cmd_fillet.cpp) - unset defaults to Radius
      // (a symmetric conic), exactly as FilletEdgeCommand's own default
      // does. Closes this command's own previously-disclosed "no Distance2
      // option at all... symmetric-distance only" gap.
      options.push_back({"Distance2", "", {}, true, false});
      // RailType (default RollingBall): kernel::FilletConvexEdgeByDistanceFromEdge/
      // ByDistanceBetweenRails (and their concave mirrors) - the SAME
      // circular rolling-ball fillet as a plain Radius, just specified as a
      // distance instead, mirroring FilletEdgeCommand's own RailType option.
      // Radius supplies that distance under either non-default setting.
      // Mutually exclusive with Rho by construction (Rho's own block in Run()
      // always returns once Rho is set, the same precedence FilletEdgeCommand
      // already establishes between its own Rho and RailType blocks).
      options.push_back({"RailType", "RollingBall", {"RollingBall", "DistFromEdge", "DistBetweenRails"}, false, false});
    }
    options.push_back({"Trim", "Yes", {"Yes", "No"}, false, true});
    WantPoint("Click the first surface");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Radius") radius_ = std::atof(v.c_str());
    if (n == "EndRadius") end_radius_ = std::atof(v.c_str());
    if (n == "Rho") rho_ = v.empty() ? std::nullopt : std::optional<double>(std::atof(v.c_str()));
    if (n == "Distance2") distance2_ = v.empty() ? std::nullopt : std::optional<double>(std::atof(v.c_str()));
    if (n == "RailType") rail_type_ = v == "DistFromEdge" ? RailType::DistFromEdge : v == "DistBetweenRails" ? RailType::DistBetweenRails : RailType::RollingBall;
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
    // Exact planar chamfer, tried FIRST: kernel::ChamferConvexEdge/
    // ChamferConcaveEdge build a genuine flat-bevel B-rep the same way
    // ChamferEdge's own exact path does (cmd_fillet.cpp's FilletEdgeCommand::
    // TryExactChamfer) - the RuledBetween ruled surface below is only ever a
    // faceted approximation between two independently-lofted contact curves,
    // never a real trimmed B-rep. Only reachable here when both picks landed
    // on the SAME solid (fa.id == fb.id): ChamferConvexEdge needs one shared
    // ON_BrepEdge to identify the corner, which only exists when the two
    // faces come from one object - two genuinely independent surfaces (the
    // common FilletSrf/ChamferSrf case) have no such edge, so those always
    // fall through to the approximate path below exactly as before this
    // wiring existed. Also skipped for the tapered (r0 != r1) case: there is
    // no tapered-chamfer kernel construction (unlike FilletConvexEdgeTapered
    // for fillets), so VariableChamfer always uses the approximate path;
    // and for Trim=No, since the exact path always replaces the whole solid
    // with an already-trimmed result, unlike the untrimmed-surface-only
    // result Trim=No asks for.
    if (chamfer && !variable && trim_ && fa.id == fb.id) {
      std::optional<ON_Brep> solid = BrepOfObject(*oa);
      Point3d p0, p1;
      if (solid && FindSharedEdgeEndpoints(*solid, fa.face, fb.face, p0, p1)) {
        kernel::Brep kb;
        kb.raw() = *solid;
        ON_Brep exact;
        bool got = false;
        try {
          exact = kernel::ChamferConvexEdge(kb, p0, p1, radius_, radius_).raw();
          got = true;
        } catch (const std::exception&) {}
        if (!got) {
          try {
            exact = kernel::ChamferConcaveEdge(kb, p0, p1, radius_, radius_).raw();
            got = true;
          } catch (const std::exception&) {}
        }
        // A cylinder's rim (flat cap + curved wall, one closed shared edge):
        // not planar-faced, so both planar constructions above refuse -
        // kernel::ChamferCylinderRim builds the exact conical bevel instead.
        // ChamferSrf has a single distance, so wall and cap setbacks are equal.
        bool rim = false;
        if (!got) {
          std::string rim_err;
          rim = got = TryCylinderRimChamfer(*solid, p0, radius_, radius_, exact, rim_err);
        }
        if (got) {
          ctx.Doc().BeginChange("ChamferSrf");
          if (SceneObject* orig = ctx.Doc().Find(fa.id)) {
            orig->kind = ObjectKind::Brep;
            if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
            orig->brep->raw() = exact;
            orig->surface.reset();
            orig->InvalidateDisplay();
          }
          ctx.Doc().Select(fa.id, true);
          ctx.Print("ChamferSrf: faces " + std::to_string(fa.face) + " and " + std::to_string(fb.face) + " of object " + std::to_string(fa.id) +
                     " replaced with an exact chamfer (distance " + FormatNumber(radius_) + (rim ? "; exact cone on the cylinder rim" : "") + ")");
          return;
        }
      }
      // Exact path unavailable here (not adjacent, curved faces, or a
      // non-manifold third face) - fall through to the approximate path
      // below exactly as before this wiring existed.
    }
    // Exact conic ("Rho") fillet: kernel::FilletConvexEdgeConic/
    // FilletConcaveEdgeConic's own ellipse/parabola/hyperbola cross-section
    // blend, requested via the Rho option above - previously exposed only
    // from FilletEdgeCommand's own single-edge Fillet mode
    // (TryExactConicFillet, cmd_fillet.cpp), never from this independently-
    // picked-faces command at all (there was no Rho option here before this
    // pass). Reachable under the same "same solid, Trim=Yes" condition as
    // the exact chamfer block above, for the identical reason
    // (FilletConvexEdgeConic/FilletConcaveEdgeConic both need one shared
    // ON_BrepEdge, which only exists when both picks land on one object).
    // Like the single-edge Rho case, there is deliberately no fallthrough
    // to the approximate path on failure, for ANY reason (not just a
    // PlanarFaces() rejection): BuildFillet/RuledBetween below can only
    // ever build a circular cross-section, so a Rho request they can't
    // satisfy exactly always warns and returns rather than silently
    // building a plain round fillet that quietly ignores Rho - including
    // when the two picks are genuinely independent surfaces (fa.id !=
    // fb.id, no shared edge to identify at all) or Trim=No (the exact path
    // always replaces the whole solid, unlike the Chamfer/RailType/
    // VariableFillet blocks above and below, which fall through silently
    // for those same two reasons because their own approximate paths CAN
    // still honor a plain Radius/EndRadius under Trim=No or independent
    // surfaces - there is no equivalent honest approximation for Rho).
    // distance_j is Distance2 (above) when set, defaulting to radius_ when
    // not - the same symmetric-unless-specified convention FilletEdgeCommand's
    // own Rho wiring already uses, now genuinely available here too.
    if (mode_ == Mode::Fillet && rho_.has_value()) {
      std::optional<ON_Brep> solid = (fa.id == fb.id) ? BrepOfObject(*oa) : std::nullopt;
      Point3d p0, p1;
      bool got = false;
      std::string convex_err, concave_err, reason;
      ON_Brep exact;
      if (!trim_) {
        reason = "Trim=No (the exact path always replaces the whole solid)";
      } else if (fa.id != fb.id) {
        reason = "the two picks are independent surfaces with no shared edge";
      } else if (!solid || !FindSharedEdgeEndpoints(*solid, fa.face, fb.face, p0, p1)) {
        reason = "the two faces are not adjacent on one solid";
      } else {
        kernel::Brep kb;
        kb.raw() = *solid;
        const double distance_j = distance2_.value_or(radius_);
        try {
          exact = kernel::FilletConvexEdgeConic(kb, p0, p1, radius_, distance_j, *rho_).raw();
          got = true;
        } catch (const std::exception& ex) { convex_err = ex.what(); }
        if (!got) {
          try {
            exact = kernel::FilletConcaveEdgeConic(kb, p0, p1, radius_, distance_j, *rho_).raw();
            got = true;
          } catch (const std::exception& ex) { concave_err = ex.what(); }
        }
        if (!got) reason = "convex attempt: " + convex_err + "; concave attempt: " + concave_err;
      }
      if (got) {
        ctx.Doc().BeginChange("FilletSrf");
        if (SceneObject* orig = ctx.Doc().Find(fa.id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = exact;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Doc().Select(fa.id, true);
        const std::string dist_desc = (distance2_ && std::fabs(*distance2_ - radius_) > 1e-9)
                                           ? "distance1 " + FormatNumber(radius_) + ", distance2 " + FormatNumber(*distance2_)
                                           : "distance " + FormatNumber(radius_);
        ctx.Print("FilletSrf: faces " + std::to_string(fa.face) + " and " + std::to_string(fb.face) + " of object " + std::to_string(fa.id) +
                   " replaced with an exact conic fillet (rho " + FormatNumber(*rho_) + ", " + dist_desc + ")");
        return;
      }
      ctx.Warn("FilletSrf: an exact conic (Rho) fillet needs the two faces to share an edge on one planar-faced solid with Trim=Yes (" + reason +
                "); use a plain Radius for the approximate rolling-ball-derived fillet instead");
      return;
    }
    // Exact RailType (DistFromEdge/DistBetweenRails) fillet: kernel::
    // FilletConvexEdgeByDistanceFromEdge/ByDistanceBetweenRails (and their
    // concave mirrors) - the same circular rolling-ball fillet a plain
    // Radius builds, just specified as a distance via RailType instead of
    // the radius directly, mirroring FilletEdgeCommand's own RailType
    // wiring (TryExactRailFillet, cmd_fillet.cpp) - previously exposed only
    // from that single-edge command, never from this independently-picked-
    // faces command (there was no RailType option here before this pass).
    // Reachable when Rho did NOT already fire (Rho's own block above always
    // returns once Rho is set - the same mutual-exclusivity FilletEdgeCommand's
    // own Rho/RailType blocks already have). Like Rho, there is deliberately
    // no fallthrough to the approximate path on failure, for ANY reason:
    // EdgeDihedralAngleForRailType (the conversion both kernel functions
    // dispatch through) requires the whole solid to be planar-faced, so a
    // request that can't be satisfied exactly always warns and returns
    // rather than silently reinterpreting the typed distance as a literal
    // radius - including for independent surfaces or Trim=No, the same two
    // extra reasons Rho's own block above always refuses rather than
    // silently falling back to a plain-Radius interpretation.
    if (mode_ == Mode::Fillet && rail_type_ != RailType::RollingBall) {
      std::optional<ON_Brep> solid = (fa.id == fb.id) ? BrepOfObject(*oa) : std::nullopt;
      Point3d p0, p1;
      bool got = false;
      std::string convex_err, concave_err, reason;
      ON_Brep exact;
      const bool from_edge = rail_type_ == RailType::DistFromEdge;
      if (!trim_) {
        reason = "Trim=No (the exact path always replaces the whole solid)";
      } else if (fa.id != fb.id) {
        reason = "the two picks are independent surfaces with no shared edge";
      } else if (!solid || !FindSharedEdgeEndpoints(*solid, fa.face, fb.face, p0, p1)) {
        reason = "the two faces are not adjacent on one solid";
      } else {
        kernel::Brep kb;
        kb.raw() = *solid;
        try {
          exact = (from_edge ? kernel::FilletConvexEdgeByDistanceFromEdge(kb, p0, p1, radius_)
                              : kernel::FilletConvexEdgeByDistanceBetweenRails(kb, p0, p1, radius_))
                      .raw();
          got = true;
        } catch (const std::exception& ex) { convex_err = ex.what(); }
        if (!got) {
          try {
            exact = (from_edge ? kernel::FilletConcaveEdgeByDistanceFromEdge(kb, p0, p1, radius_)
                                : kernel::FilletConcaveEdgeByDistanceBetweenRails(kb, p0, p1, radius_))
                        .raw();
            got = true;
          } catch (const std::exception& ex) { concave_err = ex.what(); }
        }
        if (!got) reason = "convex attempt: " + convex_err + "; concave attempt: " + concave_err;
      }
      if (got) {
        ctx.Doc().BeginChange("FilletSrf");
        if (SceneObject* orig = ctx.Doc().Find(fa.id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = exact;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Doc().Select(fa.id, true);
        ctx.Print("FilletSrf: faces " + std::to_string(fa.face) + " and " + std::to_string(fb.face) + " of object " + std::to_string(fa.id) +
                   " replaced with an exact fillet (RailType=" + std::string(from_edge ? "DistFromEdge" : "DistBetweenRails") + ", distance " + FormatNumber(radius_) + ")");
        return;
      }
      ctx.Warn("FilletSrf: an exact RailType=" + std::string(from_edge ? "DistFromEdge" : "DistBetweenRails") +
                " fillet needs the two faces to share an edge on one planar-faced solid with Trim=Yes (" + reason +
                "); use RailType=RollingBall for the approximate rolling-ball-derived fillet instead");
      return;
    }
    // Exact TAPERED fillet, tried FIRST for VariableFillet (r0 != r1):
    // kernel::FilletConvexEdgeTapered's own two-radius overload builds the
    // SAME piecewise-linear rolling-ball taper BuildPlaneCylinderVariableFillet
    // below already claims is exact for a plane+perpendicular-cylinder pair,
    // but as the genuine closed-form kernel construction (real ConicalFace
    // segments, corner-notch splicing included) rather than the app's own
    // separate arc-lofting reimplementation of the same math - the identical
    // "try the exact kernel construction first" pattern FilletEdgeCommand's
    // own TryExactTaperedFillet (cmd_fillet.cpp) already established for a
    // single picked edge. Reachable under the SAME conditions as the exact
    // chamfer block just above (same solid, Trim=Yes - the exact path
    // always replaces the whole solid with an already-trimmed result), plus
    // r0 != r1 (a constant-radius VariableFillet run falls through to the
    // untapered general construction, unchanged - Mode::Fillet's own
    // approximate offset+SSX path already works exactly on planes too, so
    // there is nothing to close for that case the way there was here).
    // On a convex-attempt failure, also tries kernel::FilletConcaveEdgeTapered
    // (its own two-radius overload, the only one that exists - see its own
    // doc comment, fillet.h) before falling through to the approximate
    // cascade - the same convex-then-concave attempt order every other
    // exact block in this file already uses (TryExactFillet/TryExactChamfer/
    // etc.), now genuinely available here too since FilletConcaveEdgeTapered
    // exists.
    if (mode_ == Mode::VariableFillet && trim_ && fa.id == fb.id && r0 != r1) {
      std::optional<ON_Brep> solid = BrepOfObject(*oa);
      Point3d p0, p1;
      if (solid && FindSharedEdgeEndpoints(*solid, fa.face, fb.face, p0, p1)) {
        kernel::Brep kb;
        kb.raw() = *solid;
        bool got = false;
        ON_Brep exact;
        try {
          exact = kernel::FilletConvexEdgeTapered(kb, p0, p1, r0, r1).raw();
          got = true;
        } catch (const std::exception&) {
          // Exact convex path unavailable here - try the concave mirror
          // below before giving up.
        }
        if (!got) {
          try {
            exact = kernel::FilletConcaveEdgeTapered(kb, p0, p1, r0, r1).raw();
            got = true;
          } catch (const std::exception&) {
            // Exact path unavailable either way (a curved adjacent face,
            // etc.) - fall through to the approximate cascade below exactly
            // as before this wiring existed.
          }
        }
        if (got) {
          ctx.Doc().BeginChange("FilletSrf");
          if (SceneObject* orig = ctx.Doc().Find(fa.id)) {
            orig->kind = ObjectKind::Brep;
            if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
            orig->brep->raw() = exact;
            orig->surface.reset();
            orig->InvalidateDisplay();
          }
          ctx.Doc().Select(fa.id, true);
          ctx.Print("FilletSrf: faces " + std::to_string(fa.face) + " and " + std::to_string(fb.face) + " of object " + std::to_string(fa.id) +
                     " replaced with an exact tapered fillet (radius " + FormatNumber(r0) + " to " + FormatNumber(r1) + ")");
          return;
        }
      }
    }
    FilletBuild fb2;
    ON_NurbsSurface result_surface;
    if (!BuildSurfaceFilletBetween(*sa, *sb, r0, r1, variable, chamfer, tol, fb2, result_surface)) {
      ctx.Warn(std::string(chamfer ? "ChamferSrf" : "FilletSrf") + ": " + fb2.error);
      return;
    }
    ctx.Doc().BeginChange(chamfer ? "ChamferSrf" : "FilletSrf");
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
        } else {
          // Non-planar input: TrimWholeLoop's own ON_BrepTrimmedPlane has
          // no analogue here, but the kernel's own SplitFaceByCurve() is
          // general to ANY surface - see TrimBySplit()'s own doc comment.
          okA = TrimBySplit(*ba, fa.face, fb2.contact_curve_a, tol, ctx, fa.id);
        }
      }
      if (bb) {
        ON_Plane plane;
        if (bb->m_F[fb.face].SurfaceOf()->IsPlanar(&plane, tol * 10)) okB = TrimWholeLoop(*bb, fb.face, fb2.contact_curve_b, ctx, fb.id, like);
        else okB = TrimBySplit(*bb, fb.face, fb2.contact_curve_b, tol, ctx, fb.id);
      }
      trimmed = (okA ? 1 : 0) + (okB ? 1 : 0);
      fell_back = trimmed < 2;
    }
    // History On, Trim=No: the fillet is a standalone surface between two UNTOUCHED sources, so it can be re-run when either
    // moves. With Trim=Yes the sources themselves are cut by the fillet's contact curves, which a re-run could not undo: not recorded.
    if (!trim_) {
      RecordHistoryIfEnabled(ctx, fillet_id, chamfer ? "ChamferSrf" : "FilletSrf", {fa.id, fb.id},
                             {{"face_a", static_cast<double>(fa.face)}, {"face_b", static_cast<double>(fb.face)}, {"r0", r0}, {"r1", r1},
                              {"variable", variable ? 1.0 : 0.0}});
    } else if (ctx.App().State().history_recording) {
      ctx.Print(std::string(chamfer ? "ChamferSrf" : "FilletSrf") + ": no history recorded - Trim=Yes cuts the two source surfaces, which a rebuild could not undo (use Trim=No to keep it associative)");
    }
    ctx.Doc().Select(fillet_id, true);
    ctx.Print(std::string(chamfer ? "ChamferSrf" : "FilletSrf") + ": built between object " + std::to_string(fa.id) + " and " + std::to_string(fb.id) + (variable ? (", radius " + FormatNumber(r0) + " to " + FormatNumber(r1)) : (", radius " + FormatNumber(radius_))) +
              (trim_ ? (trimmed == 2 ? "; both surfaces trimmed" : (trimmed == 1 ? "; one surface trimmed (the other could not be split)" : "; neither surface could be split, left untrimmed")) : "") +
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
  // General (non-planar) counterpart to TrimWholeLoop() above - closes the
  // "otherwise the input is left untrimmed" half of this command's own
  // PARITY_MAP.md gap (Blending & chamfering: "Face-face blend between two
  // independently picked surfaces"). TrimWholeLoop's ON_BrepTrimmedPlane
  // only knows how to build a planar trim loop; this uses the kernel's own
  // SplitFaceByCurve() instead, which is general to ANY ON_Surface (see
  // its own doc comment, boolean_general.h) - a real trim-loop split, not
  // a mesh-boolean approximation.
  bool TrimBySplit(const ON_Brep& b, int fi, const ON_NurbsCurve& contact, double tol, CommandContext& ctx, ObjectId id) {
    if (fi < 0 || fi >= b.m_F.Count()) return false;
    const ON_BrepFace& f = b.m_F[fi];
    const ON_Surface* surf = f.SurfaceOf();
    if (surf == nullptr) return false;
    // Same "which side of the contact curve is kept" sign test
    // TrimWholeLoop() uses (a reference interior point vs. a perpendicular
    // in-surface direction), just evaluated off the surface's own normal
    // at the contact curve's midpoint instead of a single shared plane
    // normal, since a general surface has no one normal.
    const ON_Interval du = surf->Domain(0), dv = surf->Domain(1);
    const Point3d far_ref = surf->PointAt(du.Mid(), dv.Mid());
    const Point3d c0 = contact.PointAtStart(), c1 = contact.PointAtEnd();
    Vector3d along = c1 - c0;
    if (!along.Unitize()) return false;
    double mu = 0, mv = 0;
    const Point3d mid3 = contact.PointAt(contact.Domain().Mid());
    if (!SurfaceClosestPointGlobal(*surf, mid3, mu, mv)) return false;
    const Vector3d normal = surf->NormalAt(mu, mv);
    Vector3d perp = ON_CrossProduct(normal, along);
    if (!perp.Unitize()) return false;
    const double side_ref = ON_DotProduct(far_ref - c0, perp);
    if (std::fabs(side_ref) < 1e-9) return false;  // degenerate: contact runs through the face's own middle

    kernel::Brep kb;
    kb.raw() = b;
    kernel::NurbsCurve kc;
    kc.raw() = contact;
    kernel::Brep split;
    try {
      split = kernel::SplitFaceByCurve(kb, fi, kc, std::max(tol, 1e-6));
    } catch (const std::exception&) {
      return false;
    }
    if (split.FaceCount() != kb.FaceCount() + 1) return false;
    // SplitFaceByCurve() keeps every face in original index order, with
    // fi's own single face replaced by its 2 fragments in place - so the
    // two fragments land at fi and fi+1, every later face shifted up by
    // one (see that function's own "every other face carried through
    // unchanged" doc comment and its own index-robust test,
    // TestSplitFaceByCurveBoxTopFaceAsymmetricVSplit, tests/test_basic.cpp).
    const std::vector<kernel::Mesh> meshes = split.Tessellate(16, 16);
    int keep = -1, drop = -1;
    for (int idx : {fi, fi + 1}) {
      if (idx < 0 || idx >= static_cast<int>(meshes.size()) || meshes[static_cast<size_t>(idx)].VertexCount() == 0) continue;
      const kernel::BoundingBox bb = meshes[static_cast<size_t>(idx)].GetBoundingBox();
      const Point3d center((bb.min.x + bb.max.x) * 0.5, (bb.min.y + bb.max.y) * 0.5, (bb.min.z + bb.max.z) * 0.5);
      const double s = ON_DotProduct(center - c0, perp);
      if (s * side_ref > 0) keep = idx; else drop = idx;
    }
    if (keep < 0 || drop < 0) return false;
    try {
      split.DeleteFace(drop, /*heal=*/false);
    } catch (const std::exception&) {
      return false;
    }
    ON_Brep result = split.raw();
    result.SetTolerancesBoxesAndFlags(false, true, false, true, true, true, true, true);
    if (!result.IsValid(nullptr)) return false;
    if (SceneObject* orig = ctx.Doc().Find(id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result;
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    return true;
  }

 private:
  Mode mode_;
  double radius_ = 5, end_radius_ = 2;
  std::optional<double> rho_;  // Fillet only: conic shape parameter in (0,1) for the exact FilletConvexEdgeConic/FilletConcaveEdgeConic path; unset = default rolling-ball circular fillet
  std::optional<double> distance2_;  // Rho only: independent second setback distance_j; unset = symmetric (defaults to radius_)
  RailType rail_type_ = RailType::RollingBall;  // Fillet only: alternate distance-based input for the same circular rolling-ball fillet; RollingBall = plain Radius (default)
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

// ---------------------------------------------------------------------------
// Construction history for FilletEdge / ChamferEdge (exact kernel paths only).
//
// Face and edge indices are not stable across edits of the source solid, so
// each filleted/chamfered edge is recorded by a GEOMETRIC SIGNATURE and
// re-found on the edited source by RebuildEdgeBlend (end of this file): the
// edge's direction (sign-free), the outward normals of its two faces at the
// edge midpoint (an unordered pair) and its midpoint relative to the solid's
// bounding box (centre-relative, divided by the box size, so it survives a Move
// and a Scale). The rebuild accepts an edge only when exactly one edge matches
// direction and both normals, or when the nearest of several matches is
// clearly (2x) nearer than the next; otherwise it refuses with the reason and
// the dependent is kept as it was.
// ---------------------------------------------------------------------------
namespace {

struct EdgeSig {
  ON_3dPoint mid;
  ON_3dPoint rel;  // midpoint relative to the solid's bounding box
  ON_3dVector dir, na, nb;
  // Rotation- and uniform-scale-invariant fallback identity (used only when no edge matches the
  // geometry above, e.g. after the solid was Rotated): the edge's index and the solid's edge count,
  // the cosine of the dihedral angle and the edge length relative to the mean edge length.
  int index = -1, count = 0;
  double dih = 0, lenrel = 0;
};

double MeanEdgeLength(const ON_Brep& b) {
  double sum = 0;
  int n = 0;
  for (int i = 0; i < b.m_E.Count(); ++i) { sum += b.m_E[i].PointAtStart().DistanceTo(b.m_E[i].PointAtEnd()); ++n; }
  return n ? sum / n : 0.0;
}

bool FaceNormalAtEdgeMid(const ON_Brep& b, const ON_BrepEdge& e, int k, ON_3dVector& n) {
  if (k < 0 || k >= e.TrimCount()) return false;
  const ON_BrepTrim& t = b.m_T[e.m_ti[k]];
  const int fi = t.FaceIndexOf();
  if (fi < 0 || fi >= b.m_F.Count()) return false;
  const ON_BrepFace& f = b.m_F[fi];
  const ON_Surface* srf = f.SurfaceOf();
  if (!srf) return false;
  const ON_3dPoint uv = t.PointAt(t.Domain().ParameterAt(0.5));
  n = srf->NormalAt(uv.x, uv.y);
  if (!n.Unitize()) return false;
  if (f.m_bRev) n = -n;
  return true;
}

bool EdgeSignatureOf(const ON_Brep& b, int ei, EdgeSig& s, std::string& why, double mean_len = -1.0) {
  if (ei < 0 || ei >= b.m_E.Count()) { why = "edge index out of range"; return false; }
  const ON_BrepEdge& e = b.m_E[ei];
  if (e.TrimCount() != 2) { why = "the edge does not separate exactly two faces"; return false; }
  const ON_3dPoint p0 = e.PointAtStart(), p1 = e.PointAtEnd();
  ON_3dVector d = p1 - p0;
  const double len = d.Length();
  if (!d.Unitize()) { why = "the edge is degenerate"; return false; }
  s.mid = e.PointAt(e.Domain().ParameterAt(0.5));
  if (s.mid.DistanceTo(ON_3dPoint((p0.x + p1.x) / 2, (p0.y + p1.y) / 2, (p0.z + p1.z) / 2)) > 1e-6 * len + 1e-9) { why = "the edge is not straight"; return false; }
  // Sign-free direction: the first significant component is positive.
  const double* c = &d.x;
  for (int i = 0; i < 3; ++i) { if (std::fabs(c[i]) > 1e-9) { if (c[i] < 0) d = -d; break; } }
  s.dir = d;
  if (!FaceNormalAtEdgeMid(b, e, 0, s.na) || !FaceNormalAtEdgeMid(b, e, 1, s.nb)) { why = "a face normal at the edge midpoint could not be evaluated"; return false; }
  const ON_BoundingBox bb = b.BoundingBox();
  const ON_3dPoint ctr = (bb.m_min + bb.m_max) * 0.5;
  auto rel = [&](double v, double c0, double lo, double hi) { const double sz = hi - lo; return (v - c0) / (sz > 1e-9 ? sz : 1.0); };
  s.rel = ON_3dPoint(rel(s.mid.x, ctr.x, bb.m_min.x, bb.m_max.x), rel(s.mid.y, ctr.y, bb.m_min.y, bb.m_max.y), rel(s.mid.z, ctr.z, bb.m_min.z, bb.m_max.z));
  if (mean_len < 0) mean_len = MeanEdgeLength(b);
  s.index = ei;
  s.count = b.m_E.Count();
  s.dih = ON_DotProduct(s.na, s.nb);
  s.lenrel = mean_len > 1e-12 ? len / mean_len : 0.0;
  return true;
}

std::string EdgeKey(int k, const std::string& f) { return "e" + std::to_string(k) + "_" + f; }

void StoreSig(std::map<std::string, double>& num, int k, int group, const EdgeSig& s) {
  auto put3 = [&](const char* f, double x, double y, double z) {
    num[EdgeKey(k, f) + "x"] = x; num[EdgeKey(k, f) + "y"] = y; num[EdgeKey(k, f) + "z"] = z;
  };
  put3("r", s.rel.x, s.rel.y, s.rel.z);
  put3("m", s.mid.x, s.mid.y, s.mid.z);
  put3("d", s.dir.x, s.dir.y, s.dir.z);
  put3("a", s.na.x, s.na.y, s.na.z);
  put3("b", s.nb.x, s.nb.y, s.nb.z);
  num[EdgeKey(k, "g")] = group;
  num[EdgeKey(k, "i")] = s.index;
  num[EdgeKey(k, "c")] = s.count;
  num[EdgeKey(k, "dh")] = s.dih;
  num[EdgeKey(k, "lr")] = s.lenrel;
}

bool LoadSig(const std::map<std::string, double>& num, int k, EdgeSig& s) {
  auto get3 = [&](const char* f, double& x, double& y, double& z) {
    const auto ix = num.find(EdgeKey(k, f) + "x"), iy = num.find(EdgeKey(k, f) + "y"), iz = num.find(EdgeKey(k, f) + "z");
    if (ix == num.end() || iy == num.end() || iz == num.end()) return false;
    x = ix->second; y = iy->second; z = iz->second;
    return true;
  };
  auto get1 = [&](const char* f, double dflt) { const auto it = num.find(EdgeKey(k, f)); return it == num.end() ? dflt : it->second; };
  s.index = static_cast<int>(get1("i", -1));
  s.count = static_cast<int>(get1("c", 0));
  s.dih = get1("dh", 0);
  s.lenrel = get1("lr", 0);
  return get3("r", s.rel.x, s.rel.y, s.rel.z) && get3("m", s.mid.x, s.mid.y, s.mid.z) && get3("d", s.dir.x, s.dir.y, s.dir.z) &&
         get3("a", s.na.x, s.na.y, s.na.z) && get3("b", s.nb.x, s.nb.y, s.nb.z);
}

std::string P3(const ON_3dPoint& p) { return FormatNumber(p.x) + "," + FormatNumber(p.y) + "," + FormatNumber(p.z); }

// Re-finds the recorded edge on `b`. Returns its index, or -1 with the reason.
int FindEdgeBySignature(const ON_Brep& b, const EdgeSig& want, std::string& why, bool* by_index = nullptr) {
  constexpr double kCos = 0.999;  // about 2.6 degrees
  std::vector<std::pair<double, int>> cand;
  const double mean_len = MeanEdgeLength(b);
  for (int i = 0; i < b.m_E.Count(); ++i) {
    EdgeSig s;
    std::string ignore;
    if (!EdgeSignatureOf(b, i, s, ignore, mean_len)) continue;
    if (std::fabs(ON_DotProduct(s.dir, want.dir)) < kCos) continue;
    const bool straight = ON_DotProduct(s.na, want.na) >= kCos && ON_DotProduct(s.nb, want.nb) >= kCos;
    const bool swapped = ON_DotProduct(s.na, want.nb) >= kCos && ON_DotProduct(s.nb, want.na) >= kCos;
    if (!straight && !swapped) continue;
    cand.emplace_back(s.rel.DistanceTo(want.rel), i);
  }
  const std::string what = "the edge at " + P3(want.mid) + " (direction " + P3(want.dir) + ", between faces with outward normals " + P3(want.na) + " and " + P3(want.nb) + ")";
  if (cand.empty()) {
    // Nothing has that direction and those normals. If the solid kept its topology and the same edge still has the
    // same dihedral angle and the same length relative to the mean edge length, the solid was re-oriented (rotated):
    // accept it by its position in the edge list, and say so.
    if (want.count > 0 && b.m_E.Count() == want.count && want.index >= 0 && want.index < b.m_E.Count()) {
      EdgeSig s;
      std::string ignore;
      if (EdgeSignatureOf(b, want.index, s, ignore, mean_len) && std::fabs(s.dih - want.dih) < 1e-3 && std::fabs(s.lenrel - want.lenrel) < 1e-3) {
        if (by_index) *by_index = true;
        return want.index;
      }
    }
    why = what + " no longer exists on the edited source: no edge has that direction between faces with those normals, and the solid's own edge " +
          std::to_string(want.index) + " no longer has the recorded dihedral angle and length";
    return -1;
  }
  std::sort(cand.begin(), cand.end());
  if (cand.size() > 1 && !(cand[0].first * 2.0 < cand[1].first)) {
    why = what + " is ambiguous on the edited source: " + std::to_string(cand.size()) + " edges match its direction and face normals and none is clearly nearest";
    return -1;
  }
  return cand[0].second;
}

int EdgeIndexByEndpoints(const ON_Brep& b, Point3d p0, Point3d p1, double tol) {
  for (int i = 0; i < b.m_E.Count(); ++i) {
    const ON_BrepEdge& e = b.m_E[i];
    const Point3d a = e.PointAtStart(), c = e.PointAtEnd();
    if ((a.DistanceTo(p0) <= tol && c.DistanceTo(p1) <= tol) || (a.DistanceTo(p1) <= tol && c.DistanceTo(p0) <= tol)) return i;
  }
  return -1;
}

// What a successful exact FilletEdge/ChamferEdge hands to CommitEdgeBlend.
struct EdgeBlendOp {
  bool chamfer = false;
  double d1 = 0;                  // radius (fillet) / distance1 (chamfer)
  std::optional<double> d2;       // chamfer distance2 (unset = symmetric)
  bool recordable = true;         // false: this build is not what the rebuild reproduces (angle chamfer)
  std::vector<std::pair<Point3d, Point3d>> edges;  // endpoints on the PRE-edit solid
};

// Writes an exact result into the document. With History Off (or a build the
// rebuild cannot reproduce) it replaces the solid's geometry in place, as
// before. With History On the source solid is HIDDEN (not deleted), the result
// becomes a new object and records {source, options, edge signatures}; a
// further pick on a result of this same run extends that record (the rebuild
// replays the groups in order, each edge re-found on the intermediate solid).
// Returns the id that now holds the result and appends a note for the message.
ObjectId CommitEdgeBlend(CommandContext& ctx, ObjectId id, const ON_Brep& before, const ON_Brep& result, const std::string& label,
                         const EdgeBlendOp& op, std::set<ObjectId>& made, std::string& note) {
  ctx.Doc().BeginChange(label);
  SceneObject* orig = ctx.Doc().Find(id);
  if (!orig) return id;
  const bool want = ctx.App().State().history_recording && orig->kind == ObjectKind::Brep;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
  const bool extend = made.count(id) > 0 && ctx.Doc().FindHistoryRecord(id) != nullptr;
  std::map<std::string, double> sigs;
  std::string why;
  const int k0 = extend ? static_cast<int>(ctx.Doc().FindHistoryRecord(id)->num.at("n")) : 0;
  const int group = extend ? static_cast<int>(ctx.Doc().FindHistoryRecord(id)->num.at("groups")) : 0;
  bool sig_ok = want && op.recordable;
  if (want && !op.recordable) why = "this chamfer mode (distance and angle) is not one the rebuild reproduces";
  if (sig_ok) {
    int k = k0;
    for (const auto& e : op.edges) {
      const int ei = EdgeIndexByEndpoints(before, e.first, e.second, tol);
      EdgeSig s;
      if (ei < 0) { sig_ok = false; why = "the edge could not be located on the solid"; break; }
      if (!EdgeSignatureOf(before, ei, s, why)) { sig_ok = false; break; }
      StoreSig(sigs, k++, group, s);
    }
  }
  auto replace_in_place = [&]() {
    orig->kind = ObjectKind::Brep;
    if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
    orig->brep->raw() = result;
    orig->surface.reset();
    orig->InvalidateDisplay();
  };
  if (sig_ok && extend) {
    HistoryRecord rec = *ctx.Doc().FindHistoryRecord(id);
    for (const auto& kv : sigs) rec.num[kv.first] = kv.second;
    rec.num["n"] = k0 + static_cast<double>(op.edges.size());
    rec.num["groups"] = group + 1;
    ctx.Doc().SetHistoryRecord(id, std::move(rec));
    replace_in_place();
    note = "; recorded in the history of object " + std::to_string(id) + " (now " + std::to_string(group + 1) + " step(s))";
    return id;
  }
  if (sig_ok) {
    const int layer = orig->layer_index;
    orig->visible = false;
    orig->selected = false;
    orig->InvalidateDisplay();
    kernel::Brep made_brep;
    made_brep.raw() = result;
    SceneObject n = SceneObject::MakeBrep(made_brep);
    n.layer_index = layer;
    const ObjectId new_id = ctx.Doc().Add(std::move(n));
    HistoryRecord rec;
    rec.command = op.chamfer ? "ChamferEdge" : "FilletEdge";
    rec.sources = {id};
    rec.num = sigs;
    rec.num["n"] = static_cast<double>(op.edges.size());
    rec.num["groups"] = 1;
    rec.num["r"] = op.d1;
    rec.num["d2"] = op.d2.value_or(op.d1);
    rec.num["sym"] = op.d2.has_value() ? 0.0 : 1.0;
    ctx.Doc().SetHistoryRecord(new_id, std::move(rec));
    made.insert(new_id);
    note = "; History is On: the original solid " + std::to_string(id) + " is kept hidden as the source and the result is object " + std::to_string(new_id) +
           " (it follows edits of the source; edges are re-found by direction, face normals and position)";
    return new_id;
  }
  if (want && made.count(id)) {
    ctx.Doc().ClearHistoryRecord(id);
    note = "; this step cannot be recorded (" + why + "), so the history of object " + std::to_string(id) + " was dropped";
  } else if (want) {
    note = "; not recorded in history (" + why + ")";
  }
  replace_in_place();
  return id;
}

}  // namespace

class FilletEdgeCommand : public Command {
 public:
  enum class Mode { Fillet, Chamfer, Blend };
  enum class RailType { RollingBall, DistFromEdge, DistBetweenRails };
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
    if (mode_ == Mode::Chamfer) {
      // Distance2 (a second, independent setback -- Rhino's ChamferEdge
      // Distance1/Distance2 mode) and Angle (Rhino's Distance/Angle mode,
      // mutually exclusive with Distance2) only ever feed the EXACT
      // kernel::ChamferConvexEdge/ChamferConcaveEdge(Angle) path below: the
      // rolling-ball-derived approximate fallback has no way to represent
      // an asymmetric or angled chamfer (it's built from one offset
      // radius), so leaving either set falls through to a warning instead
      // of silently building a symmetric result under the caller's asked-
      // for distances.
      options.push_back({"Distance2", "", {}, true, false});
      options.push_back({"Angle", "", {}, true, false});
    }
    if (mode_ == Mode::Fillet) {
      // Rho (strictly between 0 and 1): kernel::FilletConvexEdgeConic/
      // FilletConcaveEdgeConic's own conic cross-section shape parameter -
      // an exact ellipse/parabola/hyperbola blend instead of the default
      // circular rolling-ball arc. Only ever feeds that exact kernel path
      // below (see the Chamfer Distance2/Angle comment just above for why:
      // the approximate rolling-ball construction has no way to represent
      // a non-circular cross-section at all, let alone one with two
      // independent distances). Distance2 (reused from the Chamfer case
      // above) supplies the conic's own independent second setback
      // distance_j; unset defaults to symmetric (distance_j = Radius).
      options.push_back({"Rho", "", {}, true, false});
      options.push_back({"Distance2", "", {}, true, false});
      // RailType (default RollingBall): kernel::FilletConvexEdgeByDistanceFromEdge/
      // ByDistanceBetweenRails (and their concave mirrors) - the SAME
      // circular rolling-ball fillet surface FilletConvexEdge/
      // FilletConcaveEdge already build, just specified by measuring a
      // distance instead of the radius directly (Rhino 8's own FilletEdge
      // RailType option). Radius supplies that distance under either
      // non-default setting - a plain re-use rather than a new option,
      // since both rail types and the default radius input are mutually
      // exclusive ways to specify the same one free parameter. Only ever
      // feeds the exact kernel path below: like Rho, this requires the
      // whole solid to be planar-faced at the edge (the dihedral-angle
      // conversion has no meaning on a curved adjacent face), so a
      // request that can't be satisfied exactly warns and returns rather
      // than silently building a plain radius-based rolling-ball fillet
      // under a different distance interpretation.
      options.push_back({"RailType", "RollingBall", {"RollingBall", "DistFromEdge", "DistBetweenRails"}, false, false});
      // Exact (default Yes): a plain cylinder's rim is filleted as an exact quarter torus
      // (ClassifyPlainCylinderRim / BuildFilletedCylinder above); No keeps the old approximate path.
      options.push_back({"Exact", "Yes", {"Yes", "No"}, false, true});
    }
    if (mode_ == Mode::Blend) options = {{"Continuity", "Tangency", {"Tangency", "Curvature"}, false, false}};
    options.push_back({"Preview", "No", {"Yes", "No"}, false, true});
    WantPoint("Click an edge to " + std::string(mode_ == Mode::Fillet ? "fillet" : mode_ == Mode::Chamfer ? "chamfer" : "blend") + " (Enter when done)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Radius") { radius_ = std::atof(v.c_str()); radii_.clear(); }
    if (n == "Radii") radii_ = ParseRadiusHandles(v);
    if (n == "Distance2") { distance2_ = v.empty() ? std::nullopt : std::optional<double>(std::atof(v.c_str())); if (distance2_) angle_deg_.reset(); }
    if (n == "Angle") { angle_deg_ = v.empty() ? std::nullopt : std::optional<double>(std::atof(v.c_str())); if (angle_deg_) distance2_.reset(); }
    if (n == "Rho") rho_ = v.empty() ? std::nullopt : std::optional<double>(std::atof(v.c_str()));
    if (n == "RailType") rail_type_ = v == "DistFromEdge" ? RailType::DistFromEdge : v == "DistBetweenRails" ? RailType::DistBetweenRails : RailType::RollingBall;
    if (n == "Continuity") curvature_ = (v == "Curvature");
    if (n == "Preview") preview_ = (v == "Yes");
    if (n == "Exact") exact_rim_ = (v == "Yes");
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
  void OnEnter(CommandContext& ctx) override {
    FlushPendingRims(ctx);
    FlushPendingConic(ctx);
    FlushPendingRail(ctx);
    FlushPendingFillet(ctx);
    Finish();
  }
  // Applies every staged plain-radius edge (see pending_fillet_'s own doc
  // comment) in one kernel::FilletConvexEdges/FilletConcaveEdges call
  // against the object's own CURRENT (still entirely untouched) Brep - the
  // same convex-then-concave attempt FlushPendingConic already uses for the
  // conic case, since neither function knows the edges' own convexity in
  // advance. Unlike Rho, a failed batch (a curved face, a vertex
  // configuration FilletConvexEdges/FilletConcaveEdges doesn't support,
  // mismatched convexity, edges sharing a face, etc.) is not refused
  // outright: it replays every staged edge through the EXACT same
  // TryExactFillet-or-approximate cascade this command always used before
  // staging existed (flushing_fillet_ makes Run()'s own staging check step
  // aside for that replay), so a batch that can't help is never worse than
  // the old per-edge-immediate behavior - only a batch that CAN help (the
  // common case this pass actually closes: several edges meeting at a
  // vertex, or a tangent chain FilletConvexEdges/FilletConcaveEdges already
  // know how to merge) changes anything observable. A single staged edge
  // gets the exact original "replaced with an exact fillet" message either
  // way - FilletConvexEdges' own doc comment: a one-edge batch reproduces
  // FilletConvexEdge's own result bit-for-bit, so this is additive, not a
  // behavior change, for the already-ubiquitous single-edge case.
  // Finds the edge of `b` whose two endpoints match (p0, p1), in either
  // order, within `tol` - used both to read fresh per-edge points for the
  // batch call below and, in the replay fallback, to re-resolve each
  // staged edge's own CURRENT index (never assumed stable - see
  // FlushPendingFillet's own doc comment for why). Returns -1 if none
  // matches (the edge was itself consumed by an earlier replay in the same
  // loop, e.g. two staged edges that turned out to share more than a
  // vertex).
  static int FindEdgeIndexByEndpoints(const ON_Brep& b, Point3d p0, Point3d p1, double tol) {
    for (int i = 0; i < b.m_E.Count(); ++i) {
      const ON_BrepEdge& e = b.m_E[i];
      const Point3d a = e.PointAtStart(), c = e.PointAtEnd();
      if ((a.DistanceTo(p0) <= tol && c.DistanceTo(p1) <= tol) || (a.DistanceTo(p1) <= tol && c.DistanceTo(p0) <= tol)) return i;
    }
    return -1;
  }
  // Applies every staged cylinder-rim edge (see Run()) in ONE exact build, so
  // filleting both rims of a cylinder in the same run yields one solid.
  // Fails open: on any refusal the staged edges are replayed through the
  // unchanged approximate path, with the reason printed.
  void FlushPendingRims(CommandContext& ctx) {
    if (pending_rims_.empty()) return;
    const std::vector<PendingRim> rims = std::move(pending_rims_);
    const ObjectId id = pending_rim_id_;
    pending_rims_.clear();
    pending_rim_id_ = kNoObject;
    const std::string label = "FilletEdge";
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    const SceneObject* o = ctx.Doc().Find(id);
    std::optional<ON_Brep> b = o ? BrepOfObject(*o) : std::nullopt;
    std::string why = "object not found";
    if (b && rims.front().hole) {
      // Rims of a cylindrical through-hole in a prism (ClassifyHolePlateRim / BuildBlendedHolePlate above).
      HolePlatePlan plan;
      std::optional<HoleRimBlend> lo, hi;
      std::map<int, double> corners;
      bool ok = true;
      for (const PendingRim& r : rims) {
        const int ei = FindEdgeIndexByEndpoints(*b, r.start, r.end, tol);
        HolePlatePlan p;
        if (ei < 0 || !ClassifyHolePlateRim(*b, ei, p, why, r.corner)) { ok = false; if (ei < 0) why = "staged rim edge no longer exists"; break; }
        plan = p;
        if (r.corner) corners[p.corner] = r.radius;
        else if (p.boss) (p.boss_base ? lo : hi) = HoleRimBlend{false, r.radius, r.radius};  // a boss: lo = base, hi = top rim
        else (p.top ? hi : lo) = HoleRimBlend{false, r.radius, r.radius};
      }
      if (ok) {  // keep a blend an earlier operation put on the other rim
        if (plan.exist_lo && !lo) lo = plan.exist_lo;
        if (plan.exist_hi && !hi) hi = plan.exist_hi;
      }
      ON_Brep out;
      if (ok && (plan.boss ? BuildBlendedBoss(plan, lo, hi, out, why) : BuildBlendedHolePlate(plan, lo, hi, out, why, corners))) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = out;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        if (plan.boss) {
          ctx.Print(label + ": boss of object " + std::to_string(id) + " replaced with an exact fillet (" +
                     (lo ? "base radius " + FormatNumber(lo->d_wall) : std::string()) + (hi && lo ? ", " : "") +
                     (hi ? "top radius " + FormatNumber(hi->d_wall) : std::string()) + "; exact torus patch" + (hi && lo ? "es" : "") +
                     " at the boss, " + std::to_string(out.m_F.Count()) + " faces, closed solid)");
        } else if (lo || hi) {
          ctx.Print(label + ": hole rim(s) of object " + std::to_string(id) + " replaced with an exact fillet (" +
                     (hi ? (hi->chamfer ? "upper chamfer " + FormatNumber(hi->d_wall) + " x " + FormatNumber(hi->d_cap) : "upper radius " + FormatNumber(hi->d_wall)) : std::string()) + (hi && lo ? ", " : "") +
                     (lo ? (lo->chamfer ? "lower chamfer " + FormatNumber(lo->d_wall) + " x " + FormatNumber(lo->d_cap) : "lower radius " + FormatNumber(lo->d_wall)) : std::string()) + "; exact torus patch" + (hi && lo ? "es" : "") +
                     " on the concave hole wall" + (corners.empty() ? "" : ", " + std::to_string(corners.size()) + " exact cylindrical corner fillet(s)") + ", " +
                     std::to_string(out.m_F.Count()) + " faces, closed solid)");
        } else {
          ctx.Print(label + ": " + std::to_string(corners.size()) + " corner edge(s) of object " + std::to_string(id) +
                     " (a prism with a cylindrical hole) replaced with an exact fillet (exact cylindrical corner fillet" + (corners.size() > 1 ? "s" : "") + ", " +
                     std::to_string(out.m_F.Count()) + " faces, closed solid)");
        }
        return;
      }
    } else if (b && rims.front().rev) {
      // One rim of a polygonal solid of revolution through the kernel (FilletRevolveRim); see Run().
      ON_Brep out;
      if (TryRevolveRimFillet(*b, rims.front().start, rims.front().radius, out, why)) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = out;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Print(label + ": rim of solid-of-revolution object " + std::to_string(id) + " replaced with an exact fillet (radius " + FormatNumber(rims.front().radius) +
                   "; exact kernel torus patch tangent to both faces at the rim, " + std::to_string(out.m_F.Count()) + " faces, closed solid)");
        return;
      }
    } else if (b && rims.front().tee) {
      // The saddle edge of a perpendicular pipe tee through the kernel (FilletPipeTee); see Run().
      ON_Brep out;
      kernel::PipeTeeFillet info;
      if (TryPipeTeeFillet(*b, rims.front().radius, out, why, &info)) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = out;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Print(label + ": saddle edge of pipe-tee object " + std::to_string(id) + " replaced with a rolling-ball fillet (radius " + FormatNumber(rims.front().radius) +
                   ", main radius " + FormatNumber(info.main_radius) + ", branch radius " + FormatNumber(info.branch_radius) +
                   "; analytic cylinder walls, blend fitted to the exact rolling-ball surface with sampled deviation " + FormatNumber(info.fit_deviation) +
                   " over " + std::to_string(info.intervals) + " intervals, " + std::to_string(out.m_F.Count()) + " faces, closed solid)");
        return;
      }
    } else if (b && rims.front().cap) {
      // Rims of a plain capped cone frustum / cylinder through the kernel (FilletCapRim / FilletCapRims), one solid.
      std::vector<std::pair<Point3d, double>> kr;
      for (const PendingRim& r : rims) kr.emplace_back(r.start, r.radius);
      ON_Brep out;
      if (TryCapRimsFillet(*b, kr, out, why)) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = out;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        std::string radii;
        for (size_t i = 0; i < rims.size(); ++i) radii += (i ? ", " : "") + FormatNumber(rims[i].radius);
        ctx.Print(label + ": cap rim(s) of object " + std::to_string(id) + " replaced with an exact fillet (radius " + radii +
                   "; exact kernel torus patch" + (rims.size() > 1 ? "es" : "") + " tangent to the cap and the wall, " + std::to_string(out.m_F.Count()) +
                   " faces, closed solid)");
        return;
      }
    } else if (b) {
      double rt = 0, rb = 0;
      CylinderRimPlan plan;
      bool ok = true;
      for (const PendingRim& r : rims) {
        const int ei = FindEdgeIndexByEndpoints(*b, r.start, r.start, tol);
        CylinderRimPlan p;
        if (ei < 0 || !ClassifyPlainCylinderRim(*b, ei, p, why)) { ok = false; if (ei < 0) why = "staged rim edge no longer exists"; break; }
        plan = p;
        (r.top ? rt : rb) = r.radius;
      }
      ON_Brep out;
      if (ok && BuildFilletedCylinder(plan, rt, rb, out, why)) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = out;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Print(label + ": cylinder rim(s) of object " + std::to_string(id) + " replaced with an exact fillet (" +
                   (rt > 0 ? "top radius " + FormatNumber(rt) : std::string()) + (rt > 0 && rb > 0 ? ", " : "") +
                   (rb > 0 ? "bottom radius " + FormatNumber(rb) : std::string()) + "; exact quarter-torus patch" + (rt > 0 && rb > 0 ? "es" : "") +
                   ", " + std::to_string(out.m_F.Count()) + " faces, closed solid)");
        return;
      }
    }
    ctx.Print(label + ": exact cylinder-rim fillet not used (" + why + "); using the approximate (mesh) fillet");
    flushing_fillet_ = true;
    for (const PendingRim& r : rims) {
      const SceneObject* oc = ctx.Doc().Find(id);
      std::optional<ON_Brep> bc = oc ? BrepOfObject(*oc) : std::nullopt;
      if (!bc) continue;
      const int ei = FindEdgeIndexByEndpoints(*bc, r.start, r.hole ? r.end : r.start, tol);
      if (ei < 0) continue;
      EdgePick replay;
      replay.id = id;
      replay.edge = ei;
      Run(ctx, replay);
    }
    flushing_fillet_ = false;
  }
  void FlushPendingFillet(CommandContext& ctx) {
    if (pending_fillet_.empty()) return;
    const std::vector<std::pair<Point3d, Point3d>> edges = std::move(pending_fillet_);
    const ObjectId id = pending_fillet_id_;
    pending_fillet_.clear();
    pending_fillet_id_ = kNoObject;
    const std::string label = "FilletEdge";
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    const SceneObject* o = ctx.Doc().Find(id);
    std::optional<ON_Brep> b = o ? BrepOfObject(*o) : std::nullopt;
    if (b) {
      kernel::Brep kb;
      kb.raw() = *b;
      kernel::Brep result;
      bool ok = false;
      try {
        result = kernel::FilletConvexEdges(kb, edges, radius_);
        ok = true;
      } catch (const std::exception&) {
      }
      if (!ok) {
        try {
          result = kernel::FilletConcaveEdges(kb, edges, radius_);
          ok = true;
        } catch (const std::exception&) {
        }
      }
      if (ok) {
        EdgeBlendOp op;
        op.d1 = radius_;
        op.edges = edges;
        std::string note;
        CommitEdgeBlend(ctx, id, *b, result.raw(), label, op, made_, note);
        if (edges.size() == 1) {
          const int ei = FindEdgeIndexByEndpoints(*b, edges.front().first, edges.front().second, tol);
          ctx.Print(label + ": edge " + std::to_string(ei) + " of object " + std::to_string(id) +
                     " replaced with an exact fillet (" + RadiusDescription() + ")" + note);
        } else {
          ctx.Print(label + ": " + std::to_string(edges.size()) + " staged edges of object " + std::to_string(id) +
                     " replaced with an exact multi-edge fillet (" + RadiusDescription() + ")" + note);
        }
        return;
      }
    }
    // Batch unavailable or failed - replay every staged edge through the
    // unchanged, independent, single-edge TryExactFillet-or-approximate
    // cascade, exactly reproducing this command's own pre-staging behavior.
    // Each edge's own CURRENT index is re-resolved fresh right before its
    // own Run() call, never assumed stable across iterations: an earlier
    // iteration in this same loop can rebuild the object (a new Brep with
    // its own edge numbering), the same "re-resolve from whatever is there
    // now" guarantee a real second click already gave the old, pre-staging
    // sequential behavior this replay reproduces.
    flushing_fillet_ = true;
    for (const std::pair<Point3d, Point3d>& e : edges) {
      const SceneObject* oc = ctx.Doc().Find(id);
      std::optional<ON_Brep> bc = oc ? BrepOfObject(*oc) : std::nullopt;
      if (!bc) continue;
      const int ei = FindEdgeIndexByEndpoints(*bc, e.first, e.second, tol);
      if (ei < 0) continue;
      EdgePick replay;
      replay.id = id;
      replay.edge = ei;
      Run(ctx, replay);
    }
    flushing_fillet_ = false;
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<EdgePick> pick = PickEdge(ctx, p);
    if (!pick) { ctx.Warn("No shareable edge near that point (needs two adjacent faces)"); return; }
    Run(ctx, *pick);
  }
  // Applies every staged Rho edge (see the Run()/Rho block above) in one
  // kernel::FilletConvexEdgesConic/FilletConcaveEdgesConic batch call - the
  // same convex-then-concave cascade TryExactChamfer/TryExactRailFillet/
  // TryExactFillet above already use, since a batch is only ever uniformly
  // convex or uniformly concave (each kernel function validates every
  // edge's own dihedral matches before building anything - a mixed-
  // convexity batch is out of scope, see FilletConvexEdgesConic's own doc
  // comment, and both attempts fail cleanly rather than building half a
  // result). A single staged edge is exact here too, not an approximation
  // of the old single-edge helper: FilletConvexEdgesConic's own doc
  // comment derives every staged edge's own retrim/notch/wall exactly as
  // the single-edge FilletConvexEdgeConic construction does, just batched.
  // A no-op when nothing was staged (Rho never set, or the Fillet+Rho
  // branch never reached this run at all).
  void FlushPendingConic(CommandContext& ctx) {
    if (pending_conic_.empty()) return;
    const std::vector<kernel::ConicEdgeSpec> edges = std::move(pending_conic_);
    const ObjectId id = pending_conic_id_;
    const int first_edge = pending_conic_first_edge_;
    pending_conic_.clear();
    pending_conic_id_ = kNoObject;
    pending_conic_first_edge_ = -1;
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) return;
    kernel::Brep kb;
    kb.raw() = *b;
    kernel::Brep result;
    std::string convex_err, concave_err;
    bool ok = false;
    try {
      result = kernel::FilletConvexEdgesConic(kb, edges);
      ok = true;
    } catch (const std::exception& ex) {
      convex_err = ex.what();
    }
    if (!ok) {
      try {
        result = kernel::FilletConcaveEdgesConic(kb, edges);
        ok = true;
      } catch (const std::exception& ex) {
        concave_err = ex.what();
      }
    }
    const std::string label = "FilletEdge";
    if (!ok) {
      const std::string detail = "convex attempt: " + convex_err + "; concave attempt: " + concave_err;
      if (edges.size() == 1) {
        // Same wording a single staged edge always produced before this
        // batch refactor (the pre-existing adversarial-script expectation:
        // "needs the whole object to be planar-faced at this edge").
        ctx.Warn(label + ": an exact conic (Rho) fillet needs the whole object to be planar-faced at this edge (" +
                  detail + "); Rho has no approximate rolling-ball equivalent, so this cannot silently fall back");
      } else {
        ctx.Warn(label + ": an exact conic (Rho) fillet of the " + std::to_string(edges.size()) +
                  " staged edge(s) on object " + std::to_string(id) + " failed (" + detail +
                  "); Rho has no approximate rolling-ball equivalent, so this cannot silently fall back - edges "
                  "sharing a face, or a mix of convex and concave edges, are out of scope for one FilletEdge run");
      }
      return;
    }
    ctx.Doc().BeginChange(label);
    if (SceneObject* orig = ctx.Doc().Find(id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result.raw();
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    if (edges.size() == 1) {
      const kernel::ConicEdgeSpec& e = edges.front();
      const std::string dist = std::fabs(e.distance_j - e.distance_i) > 1e-9
                                    ? "distance1 " + FormatNumber(e.distance_i) + ", distance2 " + FormatNumber(e.distance_j)
                                    : "distance " + FormatNumber(e.distance_i);
      ctx.Print(label + ": edge " + std::to_string(first_edge) + " of object " + std::to_string(id) +
                 " replaced with an exact conic fillet (rho " + FormatNumber(e.rho) + ", " + dist + ")");
    } else {
      ctx.Print(label + ": " + std::to_string(edges.size()) + " staged edges of object " + std::to_string(id) +
                 " replaced with an exact multi-edge conic fillet");
    }
  }
  // Applies every staged RailType edge (see the Run() RailType block above)
  // in one kernel::FilletConvexEdgesByDistanceFromEdge/ByDistanceBetweenRails
  // (or concave mirror) batch call - the identical convex-then-concave
  // cascade FlushPendingConic/FlushPendingFillet already use, since neither
  // function knows the edges' own convexity in advance. Like Rho (and
  // unlike the plain-radius case), there is deliberately no replay-to-
  // approximate fallback on a failed batch: RailType's own dihedral-angle-
  // to-distance conversion has no meaning on a curved face, so a failure
  // here is reported as-is rather than silently reinterpreted. A no-op
  // when nothing was staged.
  void FlushPendingRail(CommandContext& ctx) {
    if (pending_rail_.empty()) return;
    const std::vector<std::pair<Point3d, Point3d>> edges = std::move(pending_rail_);
    const ObjectId id = pending_rail_id_;
    pending_rail_.clear();
    pending_rail_id_ = kNoObject;
    const std::string label = "FilletEdge";
    const std::string rail_name = rail_type_ == RailType::DistFromEdge ? "DistFromEdge" : "DistBetweenRails";
    const bool from_edge = rail_type_ == RailType::DistFromEdge;
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) return;
    kernel::Brep kb;
    kb.raw() = *b;
    kernel::Brep result;
    std::string convex_err, concave_err;
    bool ok = false;
    try {
      result = from_edge ? kernel::FilletConvexEdgesByDistanceFromEdge(kb, edges, radius_)
                          : kernel::FilletConvexEdgesByDistanceBetweenRails(kb, edges, radius_);
      ok = true;
    } catch (const std::exception& ex) {
      convex_err = ex.what();
    }
    if (!ok) {
      try {
        result = from_edge ? kernel::FilletConcaveEdgesByDistanceFromEdge(kb, edges, radius_)
                            : kernel::FilletConcaveEdgesByDistanceBetweenRails(kb, edges, radius_);
        ok = true;
      } catch (const std::exception& ex) {
        concave_err = ex.what();
      }
    }
    if (!ok) {
      const std::string detail = "convex attempt: " + convex_err + "; concave attempt: " + concave_err;
      if (edges.size() == 1) {
        ctx.Warn(label + ": an exact RailType=" + rail_name + " fillet needs the whole object to be planar-faced at this edge (" +
                  detail + "); RailType has no approximate rolling-ball-by-radius equivalent, so this cannot silently fall back");
      } else {
        ctx.Warn(label + ": an exact RailType=" + rail_name + " fillet of the " + std::to_string(edges.size()) +
                  " staged edge(s) on object " + std::to_string(id) + " failed (" + detail +
                  "); RailType has no approximate equivalent, so this cannot silently fall back - edges sharing a "
                  "face, a mix of convex and concave edges, or edges whose dihedral angles convert this distance to "
                  "genuinely different radii are out of scope for one FilletEdge run");
      }
      return;
    }
    ctx.Doc().BeginChange(label);
    if (SceneObject* orig = ctx.Doc().Find(id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result.raw();
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    if (edges.size() == 1) {
      const int ei = FindEdgeIndexByEndpoints(*b, edges.front().first, edges.front().second,
                                               std::max(ctx.Settings().absolute_tolerance, 1e-5));
      ctx.Print(label + ": edge " + std::to_string(ei) + " of object " + std::to_string(id) +
                 " replaced with an exact fillet (RailType=" + rail_name + ", distance " + FormatNumber(radius_) + ")");
    } else {
      ctx.Print(label + ": " + std::to_string(edges.size()) + " staged edges of object " + std::to_string(id) +
                 " replaced with an exact multi-edge fillet (RailType=" + rail_name + ", distance " + FormatNumber(radius_) + ")");
    }
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
    // Exact fillet of a plain cylinder's rim (an exact quarter torus) - see
    // ClassifyPlainCylinderRim above. Staged like the planar edges so both rims
    // picked in one run become one solid; applied at Enter, failing open to the
    // approximate path below with the reason printed.
    if (mode_ == Mode::Fillet && exact_rim_ && !flushing_fillet_ && !rho_.has_value() && rail_type_ == RailType::RollingBall && radii_.empty() && !preview_) {
      CylinderRimPlan plan;
      std::string why;
      if (ClassifyPlainCylinderRim(*b, pick.edge, plan, why)) {
        if ((!pending_fillet_.empty() && pending_fillet_id_ != pick.id) || (!pending_rims_.empty() && pending_rim_id_ != pick.id)) {
          ctx.Warn(label + ": an edge on a different object is already staged in this FilletEdge run; press Enter to apply it first, then run FilletEdge again for this object");
          return;
        }
        pending_rim_id_ = pick.id;
        pending_rims_.push_back({edge.PointAtStart(), plan.top, radius_});
        ctx.Print(label + ": " + std::string(plan.top ? "top" : "bottom") + " rim of cylinder object " + std::to_string(pick.id) +
                   " staged for an exact torus fillet (radius " + FormatNumber(radius_) + ") - " + std::to_string(pending_rims_.size()) + " staged, Enter to apply");
        return;
      }
      // A rim of a cylindrical through-hole in a prism (e.g. a box minus a cylinder): an exact torus patch on the
      // concave wall (ClassifyHolePlateRim / BuildBlendedHolePlate above), staged like the plain-cylinder rims.
      HolePlatePlan hplan;
      std::string hwhy;
      bool hole_rim = ClassifyHolePlateRim(*b, pick.edge, hplan, hwhy);
      bool hole_corner = false;
      if (!hole_rim) hole_corner = ClassifyHolePlateRim(*b, pick.edge, hplan, hwhy, /*want_corner=*/true);
      if (hole_rim || hole_corner) {
        if ((!pending_fillet_.empty() && pending_fillet_id_ != pick.id) || (!pending_rims_.empty() && pending_rim_id_ != pick.id)) {
          ctx.Warn(label + ": an edge on a different object is already staged in this FilletEdge run; press Enter to apply it first, then run FilletEdge again for this object");
          return;
        }
        pending_rim_id_ = pick.id;
        PendingRim pr{edge.PointAtStart(), hplan.top, radius_};
        pr.end = edge.PointAtEnd();
        pr.hole = true;
        pr.corner = hole_corner;
        pending_rims_.push_back(pr);
        if (hole_corner)
          ctx.Print(label + ": corner edge of object " + std::to_string(pick.id) + " (a prism with a cylindrical hole) staged for an exact cylindrical fillet (radius " +
                     FormatNumber(radius_) + ") - " + std::to_string(pending_rims_.size()) + " staged, Enter to apply");
        else
          ctx.Print(label + ": " + (hplan.boss ? std::string(hplan.boss_base ? "base" : "top") + " rim of the cylindrical boss on object " : std::string(hplan.top ? "upper" : "lower") + " rim of the cylindrical hole in object ") + std::to_string(pick.id) +
                     " staged for an exact torus fillet (radius " + FormatNumber(radius_) + ") - " + std::to_string(pending_rims_.size()) + " staged, Enter to apply");
        return;
      }
      // A rim of a plain capped CONE FRUSTUM (or any plain capped cylinder the app's own classifier above declined):
      // the kernel's exact FilletCapRim (an exact torus patch tangent to the cap and to the cone / cylinder wall).
      // Tried as a dry run now so a refusal (not a plain capped revolve, radius too large for the cap or the wall)
      // is known before anything is staged; staged like the cylinder rims so both rims of one solid are blended in
      // ONE FilletCapRims call at Enter. A refusal falls through to the existing path, with the kernel's reason
      // printed when the picked edge is a closed rim circle.
      if (edge.IsClosed()) {
        ON_Brep cap_probe;
        std::string cap_why;
        if (TryCapRimFillet(*b, edge.PointAtStart(), radius_, cap_probe, cap_why)) {
          if ((!pending_fillet_.empty() && pending_fillet_id_ != pick.id) || (!pending_rims_.empty() && pending_rim_id_ != pick.id)) {
            ctx.Warn(label + ": an edge on a different object is already staged in this FilletEdge run; press Enter to apply it first, then run FilletEdge again for this object");
            return;
          }
          pending_rim_id_ = pick.id;
          bool replaced = false;
          for (PendingRim& pr : pending_rims_)
            if (pr.cap && pr.start.DistanceTo(edge.PointAtStart()) <= tol) { pr.radius = radius_; replaced = true; }
          if (!replaced) {
            PendingRim pr{edge.PointAtStart(), false, radius_};
            pr.cap = true;
            pending_rims_.push_back(pr);
          }
          ctx.Print(label + ": rim of cylinder / cone-frustum object " + std::to_string(pick.id) + " staged for an exact kernel torus fillet (radius " +
                     FormatNumber(radius_) + ") - " + std::to_string(pending_rims_.size()) + " staged, Enter to apply");
          return;
        }
        ctx.Print(label + ": exact kernel cap-rim fillet not used (" + cap_why + ")");
      }
      // Last exact resort, only after every path above declined: the kernel's FilletRevolveRim - ONE rim of a polygonal
      // solid of revolution (a countersunk / counterbored hole plate, a stepped shaft's inside corner, a cone-capped
      // rim). Dry run now so a refusal is known before anything is staged. A rim built by a Boolean is a chain of
      // polyline edges rather than one closed circle, so the pick is not required to be a closed edge: the kernel
      // decides from the point (a refusal is printed with its reason for a closed rim circle, and stays silent for an
      // ordinary open edge, which the planar paths below then handle exactly as before). The kernel blends one rim
      // per solid per run (its result carries a torus, which it does not re-read), so a second revolve rim in the
      // same run is refused with that explanation instead of being silently approximated.
      {
        ON_Brep rev_probe;
        std::string rev_why;
        if (TryRevolveRimFillet(*b, edge.PointAtStart(), radius_, rev_probe, rev_why)) {
          if ((!pending_fillet_.empty() && pending_fillet_id_ != pick.id) || (!pending_rims_.empty() && pending_rim_id_ != pick.id)) {
            ctx.Warn(label + ": an edge on a different object is already staged in this FilletEdge run; press Enter to apply it first, then run FilletEdge again for this object");
            return;
          }
          if (!pending_rims_.empty()) {
            ctx.Warn(label + ": a rim of this solid of revolution is already staged in this FilletEdge run; the exact kernel revolve-rim fillet blends one rim per run - press Enter to apply it, then run FilletEdge again for the next rim");
            return;
          }
          pending_rim_id_ = pick.id;
          PendingRim pr{edge.PointAtStart(), false, radius_};
          pr.rev = true;
          pending_rims_.push_back(pr);
          ctx.Print(label + ": rim of solid-of-revolution object " + std::to_string(pick.id) + " staged for an exact kernel revolve-rim fillet (radius " +
                     FormatNumber(radius_) + ") - " + std::to_string(pending_rims_.size()) + " staged, Enter to apply");
          return;
        }
        if (edge.IsClosed()) ctx.Print(label + ": exact kernel revolve-rim fillet not used (" + rev_why + ")");
      }
      // Last of all: the kernel's FilletPipeTee along the SADDLE edge of a perpendicular pipe tee (the sealed Boolean
      // union of a main cylinder and a thinner branch cylinder). The kernel fillets the whole saddle edge, so the pick
      // only has to lie on it. Dry run now (a refusal is known before anything is staged); one tee edge per run. A
      // refusal is printed with the kernel's reason when the pick has a curved adjacent face (a box edge, whose two
      // faces are planar, stays silent and goes to the planar paths below exactly as before).
      {
        ON_Brep tee_probe;
        std::string tee_why;
        kernel::PipeTeeFillet tee_info;
        const bool curved_pick = !sa->IsPlanar(nullptr, tol) || !sb->IsPlanar(nullptr, tol);
        bool tee_ok = TryPipeTeeFillet(*b, radius_, tee_probe, tee_why, &tee_info);
        if (tee_ok && !EdgeOnPipeTeeSaddle(tee_info, edge, radius_, tol)) {
          tee_ok = false;
          tee_why = "the picked edge is not on the saddle edge of the pipe tee (pick the curve where the branch meets the main cylinder)";
        }
        if (tee_ok) {
          if ((!pending_fillet_.empty() && pending_fillet_id_ != pick.id) || (!pending_rims_.empty() && pending_rim_id_ != pick.id)) {
            ctx.Warn(label + ": an edge on a different object is already staged in this FilletEdge run; press Enter to apply it first, then run FilletEdge again for this object");
            return;
          }
          if (!pending_rims_.empty()) {
            ctx.Warn(label + ": the saddle edge of this pipe tee is already staged in this FilletEdge run (the kernel fillets the whole edge in one go); press Enter to apply it");
            return;
          }
          pending_rim_id_ = pick.id;
          PendingRim pr{edge.PointAtStart(), false, radius_};
          pr.tee = true;
          pending_rims_.push_back(pr);
          ctx.Print(label + ": saddle edge of pipe-tee object " + std::to_string(pick.id) + " staged for a kernel rolling-ball fillet (radius " +
                     FormatNumber(radius_) + ") - " + std::to_string(pending_rims_.size()) + " staged, Enter to apply");
          return;
        }
        if (curved_pick && !edge.IsClosed()) ctx.Print(label + ": kernel pipe-tee fillet not used (" + tee_why + ")");
      }
    }
    // Exact planar chamfer, tried FIRST (ahead of the rolling-ball-derived
    // approximate path below): kernel::ChamferConvexEdge/ChamferConcaveEdge
    // (and their Angle overloads) build a genuine flat-bevel B-rep with
    // real Distance1/Distance2 (or Distance/Angle) support, something the
    // approximate path below can never produce (RuledBetween's ruled
    // surface always runs between two EQUAL-offset contact curves, so
    // it's only ever a symmetric chamfer regardless of what Distance2 or
    // Angle ask for). Only attempted for a constant (non-variable, no
    // Radii=) distance and when Preview isn't requested (the exact path
    // replaces the polysurface directly; there's no separate "preview
    // surface" form for it, unlike the approximate path's AddSurfaceFrom).
    // Requires the WHOLE solid - not just the two adjacent faces - to be
    // planar-faced (kernel::Brep::PlanarFaces()'s own scope); anything
    // else throws and is treated as "exact unavailable" below.
    if (mode_ == Mode::Chamfer && radii_.empty() && !preview_) {
      const bool asymmetric = distance2_.has_value() && std::fabs(*distance2_ - radius_) > 1e-9;
      const bool angled = angle_deg_.has_value();
      ON_Brep exact;
      std::string detail;
      bool rim_chamfer = false;
      if (TryExactChamfer(*b, edge.PointAtStart(), edge.PointAtEnd(), exact, detail, &rim_chamfer)) {
        EdgeBlendOp op;
        op.chamfer = true;
        op.d1 = radius_;
        if (distance2_ && std::fabs(*distance2_ - radius_) > 1e-9) op.d2 = *distance2_;
        op.recordable = !angled && !rim_chamfer;  // the angle and cylinder/cone-rim chamfers are not what the rebuild re-runs
        op.edges.emplace_back(edge.PointAtStart(), edge.PointAtEnd());
        std::string note;
        CommitEdgeBlend(ctx, pick.id, *b, exact, label, op, made_, note);
        ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                   " replaced with an exact chamfer (" + ChamferDistanceDescription() +
                   (rim_chamfer ? rim_note_ : std::string()) + ")" + note);
        return;
      }
      if (asymmetric || angled) {
        ctx.Warn(label + ": an asymmetric Distance1/Distance2 or Distance/Angle chamfer needs the whole object to be planar-faced at this edge (" + detail +
                  "); use a plain Radius for the approximate rolling-ball-derived chamfer instead");
        return;
      }
      // Symmetric distance, exact path unavailable here (a curved adjacent
      // face, a non-planar third face at an end, etc.) - fall through to
      // the approximate rolling-ball-derived chamfer below exactly as
      // before this Distance2/Angle wiring existed.
    }
    // Exact conic ("Rho") fillet: kernel::FilletConvexEdgeConic/
    // FilletConcaveEdgeConic's own ellipse/parabola/hyperbola cross-section
    // blend, requested via the Rho option above. Unlike the Chamfer
    // Distance2/Angle case, there is no symmetric fallthrough here: the
    // rolling-ball approximate path below can ONLY ever build a circular
    // arc, so any rho != the one specific value that happens to make the
    // conic a circle for this edge's own dihedral (never special-cased or
    // detected here) would be silently misrepresented as a plain round
    // fillet - the same "misleading success" bug the Chamfer wiring's own
    // Distance2-on-a-curved-edge case is documented to avoid.
    //
    // Every Rho pick is STAGED (pending_conic_) rather than applied here:
    // applying the first edge immediately, as this used to do, would
    // silently break picking a SECOND independent Rho edge in the same run
    // - the first edge's own committed result already carries a curved
    // conic wall face, and PlanarFaces() (called first by every one of
    // these kernel functions) rejects any solid already carrying one, so
    // the second pick would fail with a confusing "not planar-faced" warning
    // even though it names a perfectly ordinary planar edge. Staging and
    // applying the whole batch together at Enter, via
    // kernel::FilletConvexEdgesConic/FilletConcaveEdgesConic, avoids that
    // entirely (one shared PlanarFaces() snapshot for every staged edge) and
    // closes PARITY_MAP.md's Blending & chamfering entry for that batch
    // API's own previously-zero app reachability. See FlushPendingConic
    // (below OnEnter) for the actual build.
    if (mode_ == Mode::Fillet && rho_.has_value() && radii_.empty() && !preview_) {
      if (!pending_conic_.empty() && pending_conic_id_ != pick.id) {
        ctx.Warn(label + ": a Rho edge on a different object can't be staged in the same FilletEdge run (object " +
                  std::to_string(pending_conic_id_) + " already has " + std::to_string(pending_conic_.size()) +
                  " staged); press Enter to apply those first, then run FilletEdge again for this object");
        return;
      }
      pending_conic_id_ = pick.id;
      if (pending_conic_.empty()) pending_conic_first_edge_ = pick.edge;
      kernel::ConicEdgeSpec spec;
      spec.p0 = edge.PointAtStart();
      spec.p1 = edge.PointAtEnd();
      spec.distance_i = radius_;
      spec.distance_j = distance2_.value_or(radius_);
      spec.rho = *rho_;
      pending_conic_.push_back(spec);
      ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                 " staged for an exact conic fillet (rho " + FormatNumber(*rho_) + ", " + ConicDistanceDescription() +
                 ") - " + std::to_string(pending_conic_.size()) + " staged, Enter to apply");
      return;
    }
    // Exact RailType fillet: kernel::FilletConvexEdgeByDistanceFromEdge/
    // ByDistanceBetweenRails (and their concave mirrors) - the same
    // circular rolling-ball surface the approximate path below can also
    // build, just specified via a distance instead of the radius
    // directly. Like Rho just above, the dihedral-angle-to-radius
    // conversion this depends on only has a meaning on planar-faced
    // solids, so a request that fails here warns and returns rather than
    // falling back to the approximate path under a mismatched distance
    // interpretation (that path only ever takes Radius as a literal
    // radius, not a DistFromEdge/DistBetweenRails distance).
    //
    // Every RailType pick is STAGED (pending_rail_) rather than applied
    // here, for the identical reason pending_conic_ above stages every Rho
    // pick: committing the first edge immediately leaves the object
    // carrying a curved rolling-ball wall face, so PlanarFaces() (called
    // first by every one of these kernel functions) would reject a SECOND
    // independent RailType edge picked in the same run with a confusing
    // "not planar-faced" warning about a perfectly ordinary planar edge.
    // Staging and building the whole batch together at Enter, via the new
    // kernel::FilletConvexEdgesByDistanceFromEdge/ByDistanceBetweenRails
    // (and concave mirrors) - which already existed in the kernel but had
    // zero call sites anywhere in dino8-app - closes that app-reachability
    // gap the same way pending_conic_/pending_fillet_ already did for
    // their own batch functions. See FlushPendingRail (below OnEnter) for
    // the actual build.
    if (mode_ == Mode::Fillet && rail_type_ != RailType::RollingBall && radii_.empty() && !preview_) {
      if (!pending_rail_.empty() && pending_rail_id_ != pick.id) {
        ctx.Warn(label + ": a RailType fillet edge on a different object can't be staged in the same FilletEdge run (object " +
                  std::to_string(pending_rail_id_) + " already has " + std::to_string(pending_rail_.size()) +
                  " staged); press Enter to apply those first, then run FilletEdge again for this object");
        return;
      }
      pending_rail_id_ = pick.id;
      pending_rail_.emplace_back(edge.PointAtStart(), edge.PointAtEnd());
      const std::string rail_name = rail_type_ == RailType::DistFromEdge ? "DistFromEdge" : "DistBetweenRails";
      ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                 " staged for an exact fillet (RailType=" + rail_name + ", distance " + FormatNumber(radius_) + ") - " +
                 std::to_string(pending_rail_.size()) + " staged, Enter to apply");
      return;
    }
    // Exact rolling-ball fillet, tried FIRST for the ordinary case (default
    // RailType=RollingBall, no Rho, a constant Radius): kernel::
    // FilletConvexEdge/FilletConcaveEdge build the SAME circular-arc
    // rolling-ball surface BuildFillet's own generic offset+SSX path below
    // already reaches for two planar faces, but as the genuine closed-form
    // B-rep construction PARITY_MAP.md's Blending & chamfering entry
    // documents as still never called from here - the same "try the exact
    // kernel construction first" pattern TryExactChamfer above already
    // established for ChamferEdge's own plain-Radius case (which now goes
    // through kernel::ChamferConvexEdge/ChamferConcaveEdge unconditionally,
    // not just for Distance2/Angle). Unlike Rho/RailType, this fails open:
    // a curved adjacent face (or any other PlanarFaces() rejection) falls
    // through to the unchanged approximate BuildFillet path below exactly
    // as before this wiring existed, since that path already covers the
    // curved case this exact construction cannot.
    if (mode_ == Mode::Fillet && !rho_.has_value() && rail_type_ == RailType::RollingBall && radii_.empty() && !preview_) {
      // Stage this edge rather than applying it immediately - see
      // pending_fillet_'s own doc comment for why (two edges sharing a
      // vertex, picked in the same run, need one shared
      // kernel::FilletConvexEdges/FilletConcaveEdges call to blend
      // TOGETHER, not two independent single-edge attempts). Bypassed
      // during FlushPendingFillet's own replay loop (flushing_fillet_),
      // which needs this exact TryExactFillet-or-approximate logic to run
      // per edge, unstaged, the same way it always did before batching.
      if (!flushing_fillet_) {
        if (!pending_fillet_.empty() && pending_fillet_id_ != pick.id) {
          ctx.Warn(label + ": a fillet edge on a different object can't be staged in the same FilletEdge run (object " +
                    std::to_string(pending_fillet_id_) + " already has " + std::to_string(pending_fillet_.size()) +
                    " staged); press Enter to apply those first, then run FilletEdge again for this object");
          return;
        }
        pending_fillet_id_ = pick.id;
        pending_fillet_.emplace_back(edge.PointAtStart(), edge.PointAtEnd());
        ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                   " staged for an exact fillet (" + RadiusDescription() + ") - " + std::to_string(pending_fillet_.size()) +
                   " staged, Enter to apply");
        return;
      }
      ON_Brep exact;
      std::string detail;
      if (TryExactFillet(*b, edge.PointAtStart(), edge.PointAtEnd(), exact, detail)) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(pick.id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = exact;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                   " replaced with an exact fillet (" + RadiusDescription() + ")");
        return;
      }
      // Exact path unavailable here (a curved adjacent face, a non-planar
      // third face at an end, etc.) - fall through to the approximate
      // rolling-ball-derived path below exactly as before this wiring
      // existed; unlike Rho/RailType, the plain-Radius case has always had
      // a working approximate equivalent, so there is nothing to warn about.
    }
    // Exact TAPERED fillet, tried FIRST for a variable-radius (Radii=) run:
    // kernel::FilletConvexEdgeTapered builds the SAME piecewise-linear
    // rolling-ball taper BuildPlanarVariableFillet below already claims is
    // exact for two planar faces (see its own "exact by construction"
    // comment), but as the genuine closed-form kernel construction (real
    // ConicalFace segments, corner-notch splicing included) rather than
    // the app's own separate arc-lofting reimplementation of the same
    // math - the same "try the exact kernel construction first" pattern
    // TryExactFillet above already established for the constant-radius
    // case. Convex edges only: no FilletConcaveEdgeTapered exists in the
    // kernel yet, so a concave edge always falls through unchanged.
    // Fails open exactly like TryExactFillet: a concave edge, a curved
    // adjacent face, or any other rejection falls through to the existing
    // BuildPlanarVariableFillet/BuildPlaneCylinderVariableFillet/
    // BuildFillet cascade below, unchanged.
    if (mode_ == Mode::Fillet && !rho_.has_value() && rail_type_ == RailType::RollingBall && !radii_.empty() && !preview_) {
      ON_Brep exact;
      std::string detail;
      if (TryExactTaperedFillet(*b, edge.PointAtStart(), edge.PointAtEnd(), exact, detail)) {
        ctx.Doc().BeginChange(label);
        if (SceneObject* orig = ctx.Doc().Find(pick.id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = exact;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        variable_engine_used_ = true;
        ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                   " replaced with an exact fillet (" + RadiusDescription() + ")");
        return;
      }
      // Exact path unavailable here (a concave edge, a curved adjacent
      // face, etc.) - fall through to the existing approximate cascade
      // below exactly as before this wiring existed.
    }
    ON_NurbsSurface built;
    std::vector<Point3d> spine;
    std::vector<Point3d> contact_pts_a, contact_pts_b;  // one contact point per spine sample, for the mesh-fallback wedge cutter
    std::vector<double> v_params;        // built.Domain(1) parameter per spine sample, for sampling built's own true arc at each row
    ON_NurbsCurve ca, cb;                // contact curves, kept for exact trimming below
    bool ok = false;
    std::string err;
    double achieved_fillet_gap = 0.0;    // BuildFilletAdaptive's own fb.max_gap, kept past its own block's scope for the two prints below
    if (mode_ == Mode::Blend) {
      ON_NurbsCurve ec;
      edge.GetNurbForm(ec);
      auto uv_at = [&](const ON_BrepTrim& trim, double t01) {
        const ON_Interval d = trim.Domain();
        ON_3dPoint uv = trim.PointAt(d.ParameterAt(trim.m_bRev3d ? 1 - t01 : t01));
        return ON_2dPoint(uv.x, uv.y);
      };
      // BuildBlendSurfaceG1/G2's own `pa`/`pb` (each row's boundary
      // position, per their own doc comments) come from `ea.PointAt`/
      // `sb.PointAt(uv_b_at(...))` directly - genuinely correct for
      // BlendSrf (two INDEPENDENTLY picked, already-apart boundaries), but
      // wrong for BlendEdge: `uv_at(t0, t)`/`uv_at(t1, t)` both trace the
      // SAME physical edge (edge.m_ti[0]/[1] are the two trims of ONE
      // shared edge), so the un-offset `uv_a_fn`/`uv_b_fn` this bullet used
      // to pass gave `pa == pb` at every row - confirmed directly (not
      // assumed): the resulting `built` sampled to the IDENTICAL 3D point
      // at every (u, v), a fully degenerate, invisible patch collapsed
      // onto the edge, not a blend surface at all. Offsetting each side's
      // own (u, v) a short distance INTO that face's own interior before
      // handing it to BuildBlendSurfaceG1/G2 is the actual fix - genuinely
      // non-coincident `pa`/`pb`, matching what BlendSrf's own independent
      // picks already give it for free. No user-adjustable bulge handle
      // exists yet (unlike real Rhino's own draggable BlendEdge handles),
      // so this picks one fixed, disclosed-as-heuristic width - the same
      // "visually fair, not a rigorous optimum" spirit
      // BuildBlendSurfaceG1's own 0.35/0.55 tangent-magnitude fractions
      // already use - scaled off the SMALLER adjacent face's own size so
      // it stays sane for both a tiny and a huge edge.
      const double setback = std::max(0.1 * std::min(SurfaceScale(*sa), SurfaceScale(*sb)), tol * 100);
      // The offset direction is kept STRICTLY perpendicular to the edge's
      // own tangent, within the face's own tangent plane (normal x edge
      // tangent) - not `InteriorDirection3d`'s generic "toward the
      // surface's own domain centre" probe, which points diagonally
      // in/along the edge too at a sample exactly AT a corner (t=0 or
      // t=1), shortening the resulting offset curve at both ends instead
      // of spanning the full edge length the way a real rolling-ball
      // fillet's own contact curves already do (confirmed directly: an
      // earlier version of this fix, using `InteriorDirection3d` alone,
      // produced an offset curve running only 1/9 instead of 0/10 along a
      // 10-unit edge, which TrimPlanarFace then correctly refused to
      // splice). Only `perp`'s own SIGN (not its direction) is chosen
      // toward the face's own domain centre, so it stays perpendicular to
      // the edge everywhere, including exactly at its two endpoints.
      auto offset_uv = [&](const ON_BrepTrim& trim, const ON_Surface& s, double t01) {
        const ON_2dPoint uv0 = uv_at(trim, t01);
        const Point3d edge_pt = s.PointAt(uv0.x, uv0.y);
        const Vector3d edge_tan = ec.TangentAt(ec.Domain().ParameterAt(t01));
        Vector3d perp = ON_CrossProduct(s.NormalAt(uv0.x, uv0.y), edge_tan);
        if (!perp.Unitize()) perp = InteriorDirection3d(s, uv0.x, uv0.y);
        else {
          const Point3d center3d = s.PointAt(s.Domain(0).Mid(), s.Domain(1).Mid());
          if (ON_DotProduct(perp, center3d - edge_pt) < 0) perp = -perp;
        }
        const Point3d target = edge_pt + perp * setback;
        double u = uv0.x, v = uv0.y;
        SurfaceClosestPointGlobal(s, target, u, v);
        return ON_2dPoint(u, v);
      };
      auto uv_a_fn = [&](double t) { return offset_uv(t0, *sa, t); };
      auto uv_b_fn = [&](double t) { return offset_uv(t1, *sb, t); };
      // `ea`'s own PointAt (not a surface evaluation) supplies `pa`
      // directly, so it must be a genuine curve through the OFFSET
      // positions above, not the bare shared edge `ec` - built the same
      // way this command already derives a curve from sampled points
      // elsewhere (InterpolateCubic). `eb` is never read for `pb`
      // (BuildBlendSurfaceG1/G2 both discard it, `(void)eb;` in their own
      // source) so `ec` is passed there unchanged, same as before.
      const int offset_samples = 24;
      std::vector<ON_3dPoint> ea_pts;
      std::vector<double> ea_params;
      for (int i = 0; i <= offset_samples; ++i) {
        const double t = static_cast<double>(i) / offset_samples;
        const ON_2dPoint uva = uv_a_fn(t);
        ea_pts.push_back(sa->PointAt(uva.x, uva.y));
        ea_params.push_back(t);
      }
      const ON_NurbsCurve ea_offset = InterpolateCubic(ea_pts, ea_params, false, 3);
      // Adaptive, tolerance-enforcing build (geom/BlendSurface.h) instead
      // of a bare fixed-24-sample call - closes the "tolerance enforcement
      // exists in the geometry library but is not yet reachable from any
      // app command" half of PARITY_MAP.md's own "Surface-to-surface
      // continuity blend" gap for BlendEdge. `max_gap` floors at the same
      // scale-aware bound FilletTwoSurfacesCommand::Run already uses
      // (SurfaceScale * 1e-3, clamped against the document tolerance) so a
      // tiny or huge edge pair gets a sane target either way. A failed
      // CERTIFICATION (achieved_gap finite but above max_gap) still keeps
      // the best surface BuildBlendSurfaceG1Adaptive/G2Adaptive built along
      // the way - only a genuine build failure (too few usable samples,
      // achieved_gap left at +infinity) is treated as this command's own
      // failure, exactly the fixed-sample call's own failure mode.
      const double max_gap = std::max(std::max(SurfaceScale(*sa), SurfaceScale(*sb)) * 1e-3, tol * 10);
      double achieved_gap = std::numeric_limits<double>::infinity();
      curvature_ ? BuildBlendSurfaceG2Adaptive(ea_offset, *sa, uv_a_fn, ec, *sb, uv_b_fn, max_gap, 24, 384, built, &achieved_gap)
                 : BuildBlendSurfaceG1Adaptive(ea_offset, *sa, uv_a_fn, ec, *sb, uv_b_fn, false, max_gap, 24, 384, built, &achieved_gap);
      ok = std::isfinite(achieved_gap);
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
      if (!fb.ok) {
        // Adaptive, not a bare fixed-net call - closes PARITY_MAP.md's own
        // "Rolling-ball blend surface accuracy on freeform surfaces...
        // max_gap is never enforced against a tolerance" gap for this
        // command's own general (curved-face) fallback, the identical
        // floor FilletTwoSurfacesCommand::Run's own FilletSrf/ChamferSrf
        // fallback above already uses.
        const double max_gap = std::max(std::max(SurfaceScale(*sa), SurfaceScale(*sb)) * 1e-3, tol * 10);
        fb = BuildFilletAdaptive(*sa, *sb, radius_at, tol, max_gap);
      }
      ok = fb.ok;
      err = fb.error;
      achieved_fillet_gap = fb.max_gap;
      if (ok) {
        built = mode_ == Mode::Fillet ? fb.fillet : FilletTwoSurfacesCommand::RuledBetween(fb.contact_curve_a, fb.contact_curve_b);
        spine = fb.spine_pts;
        ca = fb.contact_curve_a;
        cb = fb.contact_curve_b;
        contact_pts_a = fb.contact_a;
        contact_pts_b = fb.contact_b;
        v_params = fb.v_params;
      }
    }
    if (!ok) {
      // PARITY_MAP.md's own "Fillet overflow / cliff-edge / notch
      // handling" entry: every exact kernel construction already computes
      // its own "radius too large to fit" bound internally (fillet.cpp's
      // trim_back > extent_i/extent_j check) but used to just throw a
      // generic rejection rather than surfacing it - this still doesn't
      // roll the fillet onto a neighbouring face (genuinely out of scope,
      // see that entry), but a constant-radius Mode::Fillet failure can at
      // least now report the REAL largest radius this edge's own two
      // faces support, via kernel::MaxFilletRadius, instead of leaving the
      // user to guess by trial and error. Guarded to the plain
      // constant-radius case (radii_.empty()) since MaxFilletRadius's own
      // trim_back = radius/tan(theta/2) bound assumes one shared radius,
      // not a variable taper; Mode::Chamfer and Mode::Blend have no
      // equivalent helper yet, so they fall through unchanged.
      if (mode_ == Mode::Fillet && radii_.empty()) {
        try {
          kernel::Brep kb;
          kb.raw() = *b;
          const kernel::FilletRadiusLimit limit = kernel::MaxFilletRadius(kb, edge.PointAtStart(), edge.PointAtEnd());
          if (limit.max_radius > 0.0 && radius_ > limit.max_radius) {
            err += " (the largest radius this edge's own two faces support is " + FormatNumber(limit.max_radius) + ")";
          }
        } catch (const std::exception&) {
          // MaxFilletRadius rejects for a structural reason unrelated to
          // "too large" (e.g. a curved adjacent face already rejected the
          // exact path for a different reason) - leave `err` as it was.
        }
      }
      ctx.Warn(label + ": " + err);
      return;
    }
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
        kernel::Mesh cutter = SweepTubeCutter(spine, contact_pts_a, contact_pts_b, edge.IsClosed());
        try {
          kernel::Mesh remainder_mesh = kernel::BooleanCombine(*obj_mesh, cutter, kernel::BooleanOp::Difference);
          // remainder_mesh is already a fully closed, watertight solid on
          // its own (a boolean DIFFERENCE between two closed solids always
          // is) - it has NO naked boundary left for an added surface patch
          // to weld into. The old approach (MergeAndWeld with a separately
          // tessellated open fillet ribbon) was fighting an impossible
          // topology: welding a patch onto an already-closed mesh cannot
          // produce a valid manifold no matter how tight the tolerance,
          // since there's nothing open to fuse it to. Instead, build the
          // fillet as its OWN closed solid (the "lune" between the flat cut
          // and the true rolling-ball arc) and boolean UNION it onto
          // remainder_mesh - Manifold's CSG algorithm computes the real
          // geometric intersection between the two independently-
          // tessellated solids robustly, with no manual vertex-conformance
          // requirement at all.
          kernel::Mesh fillet_solid = BuildFilletSolid(spine, contact_pts_a, contact_pts_b, v_params, built, edge.IsClosed());
          kernel::Mesh combined = kernel::BooleanCombine(remainder_mesh, fillet_solid, kernel::BooleanOp::Union);
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
          // A closed manifold alone isn't proof this actually cut anything:
          // at extreme coordinate scales (~1e6 units), ON_Mesh's single-
          // precision (ON_3fPoint) vertex storage can quantize the cutter/
          // fillet-solid geometry so coarsely that the boolean union comes
          // back closed but essentially unchanged from the original object
          // - a real, previously-hidden failure mode this specific check
          // exists to catch: cutter.Volume() is this fillet's own expected
          // removed-material magnitude (computed from the exact same
          // coordinates, so it degrades the same way under quantization),
          // and a genuine fillet must remove at least a meaningful fraction
          // of it - not "did the topology check pass," but "did the volume
          // actually move by roughly the right amount." Confirmed this
          // catches a real case: an otherwise-ordinary radius-2 fillet on a
          // 10-unit box translated to 1e6-unit coordinates now clears the
          // watertight check but changes volume by 0 instead of ~8.6 - this
          // rejects that instead of silently handing back the unfilleted
          // box under a label that implies success.
          const double removed = std::fabs(obj_mesh->Volume() - combined.Volume());
          const double expected_removed = std::fabs(cutter.Volume());
          // A second, independent degeneracy check, found by this exact
          // adversarial case (a radius well beyond what a 2-unit-tall
          // edge can resolve): the ratio check just above only catches
          // "removed" and "expected_removed" degrading TOGETHER (the
          // 1e6-scale quantization case its own comment documents) - it
          // says nothing when `cutter` itself is already a degenerate
          // sliver, which is exactly what happens here. The offset-
          // surface intersection in BuildFillet can still return SOME
          // spine for a geometrically infeasible radius (the offset
          // surfaces cross well past where the real edge's own faces
          // end), producing a cutter whose own volume is driven to
          // numerical noise (confirmed directly: radius 5 on this
          // fixture's 2-unit edge measured `cutter.Volume()` at ~3.6e-7,
          // against a real quarter-circle cross-section's own nominal
          // `(1 - pi/4) * radius^2 * spine_length` of ~53.6) - so the
          // ratio check above (comparing two already-degenerate numbers)
          // passed vacuously, and the mesh "fell back" to the unfilleted
          // box relabeled as a success. `nominal_removed` is that same
          // closed-form estimate, scale-correct for any legitimate
          // radius (including a genuinely tiny one, like this file's own
          // radius=0.02 case elsewhere, which this check does not
          // affect: a real cutter there lands close to its own, much
          // smaller, nominal value) - `expected_removed` landing more
          // than 1000x below it, rather than merely below `removed`, is
          // the real tell that the cutter itself never represented a
          // physically meaningful fillet at this radius.
          double spine_len = 0.0;
          for (size_t i = 1; i < spine.size(); ++i) spine_len += spine[i].DistanceTo(spine[i - 1]);
          // The smaller of the two cross-section dimensions, conservatively
          // (an asymmetric Distance1/Distance2 chamfer/conic's own
          // cross-section area scales with BOTH distances, not radius_
          // alone - using the smaller one keeps this check from spuriously
          // firing on a legitimate, very lopsided distance1/distance2
          // pair, while still catching the case both distances collapsed
          // the same way this adversarial fixture's symmetric radius did).
          const double eff_r = std::min(radius_, distance2_.value_or(radius_));
          const double nominal_removed = (1.0 - ON_PI / 4.0) * eff_r * eff_r * std::max(spine_len, 1e-9);
          if (nominal_removed > 1e-9 && expected_removed < 1e-3 * nominal_removed) {
            ctx.Warn(label + ": the offset surfaces do not meet (radius " + FormatNumber(radius_) +
                      " is too large for this edge's own adjacent faces to resolve a real fillet - the mesh fallback's own cutter degenerated to near-zero volume instead of a genuine cut)");
            return;
          }
          if (expected_removed > 1e-9 && removed < 0.1 * expected_removed) {
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
          ctx.Print(label + ": edge " + std::to_string(pick.edge) + " -- mesh fallback (exact B-rep trim unavailable here; result is an approximate mesh, not a clean B-rep)" +
                     (achieved_fillet_gap > tol * 10 ? " (surface quality: contact points off by up to " + FormatNumber(achieved_fillet_gap) + ")" : ""));
          return;
        } catch (const std::exception& ex) { ctx.Warn(label + ": mesh fallback failed (" + std::string(ex.what()) + ")"); }
      }
      ctx.Warn(label + ": could not trim (non-planar adjacent surface and no usable mesh fallback)");
      return;
    }
    // Blend: try the SAME exact trim-and-join pipeline the Fillet/Chamfer
    // block above already uses (TrimPlanarFace/RoundFaceCorner/
    // JoinNakedEdges) - closes PARITY_MAP.md's own "Edge blend trimmed and
    // joined into the polysurface (Rhino BlendEdge TrimAndJoin)" gap for
    // the planar-adjacent-face case. `built`'s own u=0/u=max boundary
    // isocurves ARE the exact curves to trim faces i/j back to (the
    // Hermite blend's own edge lies exactly on each face by construction -
    // HermiteRowG1/G2's own c0/c3 Bezier endpoints, BlendSurface.cpp - so
    // no separate contact-curve derivation is needed the way the rolling-
    // ball fillet's offset+SSX construction above needs one). Still
    // planar-adjacent-face only, the same scope every other exact block in
    // this file shares - a curved adjacent face falls through unchanged to
    // the pre-existing "added as a separate surface" behaviour below.
    if (mode_ == Mode::Blend) {
      const int trim_idx0 = FindOuterTrimForEdge(*b, fi0, pick.edge);
      const int trim_idx1 = FindOuterTrimForEdge(*b, fi1, pick.edge);
      ON_Curve* iso_a = built.IsoCurve(1, built.Domain(0).Min());
      ON_Curve* iso_b = built.IsoCurve(1, built.Domain(0).Max());
      bool exact_ok = false;
      ON_Brep remainder;
      if (iso_a && iso_b && trim_idx0 >= 0 && trim_idx1 >= 0) {
        std::optional<ON_Brep> ra = TrimPlanarFace(*b, fi0, trim_idx0, *iso_a, false, tol);
        if (!ra) ra = TrimPlanarFace(*b, fi0, trim_idx0, *iso_a, true, tol);
        std::optional<ON_Brep> rb = TrimPlanarFace(*b, fi1, trim_idx1, *iso_b, false, tol);
        if (!rb) rb = TrimPlanarFace(*b, fi1, trim_idx1, *iso_b, true, tol);
        if (ra && rb) {
          remainder = *b;
          const int hi = std::max(fi0, fi1), lo = std::min(fi0, fi1);
          remainder.DeleteFace(remainder.m_F[hi], true);
          remainder.Compact();
          remainder.DeleteFace(remainder.m_F[lo < hi ? lo : lo - 1], true);
          remainder.Compact();
          const int ra_index = remainder.m_F.Count();
          remainder.Append(*ra);
          const int rb_index = remainder.m_F.Count();
          remainder.Append(*rb);
          ON_Brep blend_brep;
          ON_NurbsSurface* blend_srf = new ON_NurbsSurface(built);
          blend_brep.Create(blend_srf);
          const int blend_index = remainder.m_F.Count();
          remainder.Append(blend_brep);
          // Same "also round the third face at each end vertex" step the
          // Fillet/Chamfer block above already does (a box-corner edge
          // blend also clips the corner of whichever OTHER face shares
          // that vertex) - `built`'s own cross-section isocurve at v=min/
          // max (the "across the blend" direction, held at each end of
          // the edge) is that corner's own exact replacement boundary,
          // the identical role FilletConvexEdge's own cap arc plays there.
          const Point3d v0 = edge.PointAtStart(), v1 = edge.PointAtEnd();
          ON_Curve* cross_min = built.IsoCurve(0, built.Domain(1).Min());
          ON_Curve* cross_max = built.IsoCurve(0, built.Domain(1).Max());
          auto arc_for = [&](Point3d v) -> ON_Curve* {
            if (!cross_min || !cross_max) return nullptr;
            const double dmin = cross_min->PointAt(cross_min->Domain().Mid()).DistanceTo(v);
            const double dmax = cross_max->PointAt(cross_max->Domain().Mid()).DistanceTo(v);
            return dmin <= dmax ? cross_min : cross_max;
          };
          for (Point3d v : {v0, v1}) {
            ON_Curve* arc = arc_for(v);
            if (!arc) continue;
            for (int fi = 0; fi < remainder.m_F.Count(); ++fi) {
              if (fi == ra_index || fi == rb_index || fi == blend_index || remainder.m_F[fi].m_face_index < 0) continue;
              std::optional<ON_Brep> rounded = RoundFaceCorner(remainder, fi, v, *arc, tol);
              if (!rounded) continue;
              remainder.DeleteFace(remainder.m_F[fi], true);
              remainder.Append(*rounded);
              break;
            }
          }
          remainder.Compact();
          delete cross_min;
          delete cross_max;
          JoinNakedEdges(remainder, std::max(tol * 20, 1e-4));
          remainder.Compact();
          remainder.SetTolerancesBoxesAndFlags();
          exact_ok = true;
          for (int ei = 0; ei < remainder.m_E.Count() && exact_ok; ++ei) {
            const ON_BrepEdge& e = remainder.m_E[ei];
            if (e.m_edge_index >= 0 && e.TrimCount() == 1) exact_ok = false;
          }
          if (exact_ok) {
            BrepMeshOptions check_opt;
            check_opt.chord_tolerance = std::max(tol * 4, 1e-4);
            exact_ok = MeshBrepClosed(remainder, check_opt).IsClosedManifold();
          }
        }
      }
      delete iso_a;
      delete iso_b;
      if (exact_ok) {
        if (SceneObject* orig = ctx.Doc().Find(pick.id)) {
          orig->kind = ObjectKind::Brep;
          if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
          orig->brep->raw() = remainder;
          orig->surface.reset();
          orig->InvalidateDisplay();
        }
        ctx.Print(label + ": edge " + std::to_string(pick.edge) + " of object " + std::to_string(pick.id) +
                   " trimmed and joined into the polysurface (" + std::string(curvature_ ? "Continuity=Curvature" : "Continuity=Tangency") + ")");
        return;
      }
      // Exact trim-and-join unavailable (a non-planar adjacent face, or a
      // join that didn't fully close) - fall through to the pre-existing
      // "added as a separate surface" behaviour below, unchanged.
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

  // Printable summary for the exact-chamfer path only (Run() prints this
  // instead of RadiusDescription() once TryExactChamfer succeeds). Kept as
  // plain "radius N" for the plain symmetric case - matching
  // RadiusDescription()'s own wording, since that's the case this fast
  // path now handles INSTEAD OF the older exact_ok/RoundFaceCorner path
  // further down (same construction, same message, for the same box-
  // corner case fillet_script.txt's own ChamferEdge check already covers).
  std::string ChamferDistanceDescription() const {
    if (angle_deg_) return "distance1 " + FormatNumber(radius_) + ", angle " + FormatNumber(*angle_deg_) + " degrees from face 1";
    if (distance2_ && std::fabs(*distance2_ - radius_) > 1e-9) return "distance1 " + FormatNumber(radius_) + ", distance2 " + FormatNumber(*distance2_);
    return "radius " + FormatNumber(radius_);
  }

  // Tries the exact kernel chamfer (convex, then concave) between p0/p1 on
  // `solid`'s own two adjacent faces, using radius_ as distance_i and
  // either angle_deg_ (Distance/Angle mode) or distance2_.value_or(radius_)
  // (Distance1/Distance2 mode, symmetric when Distance2 is unset) for the
  // second parameter. Returns false (leaving `out` untouched) if neither
  // convexity fits or `solid` isn't fully planar-faced - PlanarFaces()'s
  // own requirement, inherited by ChamferConvexEdge/ChamferConcaveEdge -
  // with both underlying exception messages joined into `detail` so a
  // caller that needs to report a real failure (as opposed to silently
  // falling back) has something concrete to show, not just "didn't work".
  bool TryExactChamfer(const ON_Brep& solid, Point3d p0, Point3d p1, ON_Brep& out, std::string& detail, bool* rim_chamfer = nullptr) const {
    kernel::Brep kb;
    kb.raw() = solid;
    rim_note_ = "; exact cone on the cylinder rim, distance1 along the wall and distance2 across the cap";
    const double d_i = radius_;
    const bool use_angle = angle_deg_.has_value();
    const double angle_from_i = use_angle ? (*angle_deg_) * ON_PI / 180.0 : 0.0;
    const double d_j = distance2_.value_or(d_i);
    std::string convex_err, concave_err;
    auto attempt = [&](bool convex, std::string& err) -> bool {
      try {
        kernel::Brep result = use_angle ? (convex ? kernel::ChamferConvexEdgeAngle(kb, p0, p1, d_i, angle_from_i)
                                                   : kernel::ChamferConcaveEdgeAngle(kb, p0, p1, d_i, angle_from_i))
                                         : (convex ? kernel::ChamferConvexEdge(kb, p0, p1, d_i, d_j)
                                                   : kernel::ChamferConcaveEdge(kb, p0, p1, d_i, d_j));
        out = result.raw();
        return true;
      } catch (const std::exception& ex) {
        err = ex.what();
        return false;
      }
    };
    if (attempt(true, convex_err)) return true;
    if (attempt(false, concave_err)) return true;
    // Third attempt: the edge is a full capped cylinder's rim (neither planar
    // construction can touch a curved wall). Distance1 is the setback along
    // the wall, Distance2 (default: equal) across the cap; the Distance/Angle
    // mode has no cone construction, so it is not tried.
    std::string rim_err = "Distance/Angle mode has no app-level cylinder-rim construction (the kernel cap-rim attempt below handles it)";
    if (!use_angle && TryCylinderRimChamfer(solid, p0, d_i, d_j, out, rim_err)) {
      if (rim_chamfer) *rim_chamfer = true;
      return true;
    }
    // Fourth attempt: the rim of a plain capped cone / frustum (a conical bevel, same distance meaning).
    std::string cone_err = "Distance/Angle mode has no app-level cone-rim construction (the kernel cap-rim attempt below handles it)";
    if (!use_angle && TryConeRimChamfer(solid, p0, d_i, d_j, out, cone_err)) {
      if (rim_chamfer) *rim_chamfer = true;
      return true;
    }
    // Kernel cap-rim chamfer (kernel::ChamferCapRim / ChamferCapRimAngle): a plain capped cylinder or cone frustum,
    // acute and obtuse rims alike. It is the only construction for Distance/Angle there (a closed rim circle has no
    // loop direction to say which side is "face 1", so Distance is measured ACROSS THE CAP and Angle from the cap
    // plane), and the Distance1/Distance2 fallback when the app's own two builders above decline.
    std::string cap_err;
    if (use_angle ? TryCapRimChamferAngle(solid, p0, d_i, angle_from_i, out, cap_err) : TryCapRimChamfer(solid, p0, d_i, d_j, out, cap_err)) {
      if (rim_chamfer) *rim_chamfer = true;
      rim_note_ = use_angle ? "; exact kernel cap-rim bevel (cone), distance " + FormatNumber(d_i) + " across the cap, " + FormatNumber(*angle_deg_) + " degrees from the cap plane"
                            : "; exact kernel cap-rim bevel (cone), distance1 along the wall and distance2 across the cap";
      return true;
    }
    // Fifth attempt: a rim of a cylindrical through-hole in a prism (box minus cylinder): an exact conical bevel
    // on the concave wall; distance1 along the wall, distance2 across the end face.
    std::string hole_err = "Distance/Angle mode has no hole-rim construction";
    if (!use_angle) {
      const int ei = FindEdgeIndexByEndpoints(solid, p0, p1, 1e-6);
      HolePlatePlan hplan;
      if (ei >= 0 && ClassifyHolePlateRim(solid, ei, hplan, hole_err)) {
        std::optional<HoleRimBlend> lo, hi;
        if (hplan.boss) (hplan.boss_base ? lo : hi) = HoleRimBlend{true, d_i, d_j};  // a boss: lo = base, hi = top rim
        else (hplan.top ? hi : lo) = HoleRimBlend{true, d_i, d_j};
        if (hplan.exist_lo && !lo) lo = hplan.exist_lo;  // keep a blend an earlier operation put on the other rim
        if (hplan.exist_hi && !hi) hi = hplan.exist_hi;
        if (hplan.boss ? BuildBlendedBoss(hplan, lo, hi, out, hole_err) : BuildBlendedHolePlate(hplan, lo, hi, out, hole_err)) {
          if (rim_chamfer) *rim_chamfer = true;
          return true;
        }
      } else if (ei < 0) {
        hole_err = "edge not found";
      }
    }
    // Sixth attempt, only when everything above declined: one rim of a polygonal solid of revolution (countersunk /
    // counterbored hole plate, stepped shaft, flanged ring, cone-capped rim) through kernel::ChamferRevolveRim -
    // distance1 along the cone / cylinder wall, distance2 across the planar face at the rim.
    std::string rev_err = "Distance/Angle mode has no revolve-rim construction";
    // (A rim built by a Boolean is a chain of polyline edges, so the pick need not be one closed circle: the kernel
    // reads the rim from the point and refuses a point on no rim.)
    if (!use_angle && TryRevolveRimChamfer(solid, p0, d_i, d_j, out, rev_err)) {
      if (rim_chamfer) *rim_chamfer = true;
      rim_note_ = "; exact kernel revolve-rim bevel (cone / plane), distance1 along the wall and distance2 across the planar face";
      return true;
    }
    detail = "convex attempt: " + convex_err + "; concave attempt: " + concave_err + "; cylinder-rim attempt: " + rim_err + "; cone-rim attempt: " + cone_err +
             "; kernel cap-rim attempt: " + cap_err + "; hole-rim attempt: " + hole_err + "; kernel revolve-rim attempt: " + rev_err;
    return false;
  }

  // Printable summary for the exact-conic-fillet path (mirrors
  // ChamferDistanceDescription's own "radius N"/"distance1 N, distance2 M"
  // split, just under the "distance" wording FilletConvexEdgeConic's own
  // parameters use rather than "radius", since rho != 0.5 has no single
  // radius to report).
  std::string ConicDistanceDescription() const {
    if (distance2_ && std::fabs(*distance2_ - radius_) > 1e-9) return "distance1 " + FormatNumber(radius_) + ", distance2 " + FormatNumber(*distance2_);
    return "distance " + FormatNumber(radius_);
  }

  // Tries the exact kernel rolling-ball fillet (convex, then concave)
  // specified via a RailType distance instead of a direct radius -
  // kernel::FilletConvexEdgeByDistanceFromEdge/ByDistanceBetweenRails (and
  // their concave mirrors) are pure closed-form radius conversions that
  // dispatch straight to FilletConvexEdge/FilletConcaveEdge, so this
  // reuses the identical convex-then-concave/detail-joining structure
  // FlushPendingConic's own batch call above uses, for the identical
  // reason: the caller doesn't know the edge's own convexity in advance.
  bool TryExactRailFillet(const ON_Brep& solid, Point3d p0, Point3d p1, ON_Brep& out, std::string& detail) const {
    kernel::Brep kb;
    kb.raw() = solid;
    const double d = radius_;
    const bool from_edge = rail_type_ == RailType::DistFromEdge;
    std::string convex_err, concave_err;
    auto attempt = [&](bool convex, std::string& err) -> bool {
      try {
        kernel::Brep result = from_edge ? (convex ? kernel::FilletConvexEdgeByDistanceFromEdge(kb, p0, p1, d)
                                                    : kernel::FilletConcaveEdgeByDistanceFromEdge(kb, p0, p1, d))
                                         : (convex ? kernel::FilletConvexEdgeByDistanceBetweenRails(kb, p0, p1, d)
                                                    : kernel::FilletConcaveEdgeByDistanceBetweenRails(kb, p0, p1, d));
        out = result.raw();
        return true;
      } catch (const std::exception& ex) {
        err = ex.what();
        return false;
      }
    };
    if (attempt(true, convex_err)) return true;
    if (attempt(false, concave_err)) return true;
    detail = "convex attempt: " + convex_err + "; concave attempt: " + concave_err;
    return false;
  }

  // Tries the exact kernel rolling-ball fillet (convex, then concave)
  // between p0/p1 on `solid`'s own two adjacent faces, using radius_ for
  // both faces (a plain rolling-ball fillet has one shared radius, unlike
  // Chamfer's independent d_i/d_j). Same convex-then-concave/detail-joining
  // structure as TryExactChamfer/TryExactRailFillet above, for the
  // identical reason: the caller doesn't know the edge's own convexity in
  // advance, and both kernel functions already reject the wrong one
  // cleanly via EdgeConvexity/RequireConcaveEdge.
  bool TryExactFillet(const ON_Brep& solid, Point3d p0, Point3d p1, ON_Brep& out, std::string& detail) const {
    kernel::Brep kb;
    kb.raw() = solid;
    const double r = radius_;
    std::string convex_err, concave_err;
    auto attempt = [&](bool convex, std::string& err) -> bool {
      try {
        kernel::Brep result = convex ? kernel::FilletConvexEdge(kb, p0, p1, r) : kernel::FilletConcaveEdge(kb, p0, p1, r);
        out = result.raw();
        return true;
      } catch (const std::exception& ex) {
        err = ex.what();
        return false;
      }
    };
    if (attempt(true, convex_err)) return true;
    if (attempt(false, concave_err)) return true;
    detail = "convex attempt: " + convex_err + "; concave attempt: " + concave_err;
    return false;
  }

  // Builds the piecewise-linear radius profile from radii_ (RadiusAt's own
  // held-constant-past-the-ends, t-fraction-in-[0,1] convention, the same
  // one BuildPlanarVariableFillet's radius_at(t) callback already uses) as
  // arc-length stations for kernel::FilletConvexEdgeTapered's N-station
  // overload: `stations.front().t == 0` and `stations.back().t == length`
  // are required by that overload, so the first and last stations are
  // always anchored there (via RadiusAt(0)/RadiusAt(1), matching whatever
  // handle - if any - already sits at that end), with every interior
  // handle converted from its t-fraction to an arc-length t = fraction *
  // length and included in between; a handle that already falls on (or
  // within a small tolerance of) t=0 or t=length is skipped to avoid
  // feeding the kernel two stations at the same t.
  std::vector<kernel::FilletRadiusStation> TaperedStations(double length) const {
    std::vector<kernel::FilletRadiusStation> stations;
    stations.push_back({0.0, RadiusAt(0.0)});
    for (const auto& h : radii_) {
      const double t = h.first * length;
      if (t <= 1e-9 || t >= length - 1e-9) continue;
      stations.push_back({t, h.second});
    }
    stations.push_back({length, RadiusAt(1.0)});
    return stations;
  }

  // Tries the exact kernel N-station tapered fillet (kernel::
  // FilletConvexEdgeTapered) for a variable-radius (Radii=) run between
  // p0/p1 on `solid`'s own two adjacent PLANAR faces - the real closed-form
  // construction PARITY_MAP.md's Blending & chamfering entry documents as
  // still never called from here, the same gap TryExactFillet's own
  // plain-Radius case already closed. On a convex-attempt failure, falls
  // through to kernel::FilletConcaveEdgeTapered's own N-station overload -
  // closes the "FilletConcaveEdgeTapered has no N-station overload yet"
  // restriction this used to carry: that overload now exists (fillet.h)
  // and its own doc comment establishes it agrees bit-for-bit with the
  // plain two-radius overload at the 2-station case, so there is no longer
  // any reason to special-case stations.size() == 2 here - a run with an
  // interior Radii= handle now reaches a genuine exact concave fillet too,
  // not just the approximate cascade below.
  bool TryExactTaperedFillet(const ON_Brep& solid, Point3d p0, Point3d p1, ON_Brep& out, std::string& detail) const {
    kernel::Brep kb;
    kb.raw() = solid;
    const double length = p0.DistanceTo(p1);
    if (!(length > 1e-9)) { detail = "degenerate edge"; return false; }
    const std::vector<kernel::FilletRadiusStation> stations = TaperedStations(length);
    std::string convex_err;
    try {
      out = kernel::FilletConvexEdgeTapered(kb, p0, p1, stations).raw();
      return true;
    } catch (const std::exception& ex) {
      convex_err = ex.what();
    }
    try {
      out = kernel::FilletConcaveEdgeTapered(kb, p0, p1, stations).raw();
      return true;
    } catch (const std::exception& ex) {
      detail = "convex attempt: " + convex_err + "; concave attempt: " + ex.what();
      return false;
    }
  }

  Mode mode_;
  double radius_ = 2;
  std::vector<std::pair<double, double>> radii_;  // (t in [0,1], radius) handles; empty = constant radius_
  std::optional<double> distance2_;  // Chamfer/Fillet: second distance (Distance1/Distance2 mode); unset = symmetric
  std::optional<double> angle_deg_;  // Chamfer only: angle from face 1 in degrees (Distance/Angle mode); mutually exclusive with distance2_
  std::optional<double> rho_;  // Fillet only: conic shape parameter in (0,1) for the exact FilletConvexEdgeConic/FilletConcaveEdgeConic path; unset = default rolling-ball circular fillet
  RailType rail_type_ = RailType::RollingBall;  // Fillet only: alternate distance-based input for the same circular rolling-ball fillet; RollingBall = plain Radius (default)
  bool curvature_ = false;
  bool preview_ = false;
  struct PendingRim { Point3d start; bool top; double radius; Point3d end = Point3d(); bool hole = false; bool corner = false; bool cap = false; bool rev = false; bool tee = false; };
  mutable std::string rim_note_;  // TryExactChamfer: what the "; exact ..." suffix of a rim-chamfer message says
  std::vector<PendingRim> pending_rims_;  // staged plain-cylinder rim fillets (see Run()/FlushPendingRims)
  ObjectId pending_rim_id_ = kNoObject;
  bool exact_rim_ = true;  // Exact=No skips the exact cylinder-rim torus fillet
  bool variable_engine_used_ = false;  // set by Run(): true when TryExactTaperedFillet (kernel::FilletConvexEdgeTapered) or BuildPlanarVariableFillet/BuildPlaneCylinderVariableFillet (the app's own exact closed forms) built the last variable-radius result

  // Staged Rho (exact conic) edges for this command run, applied together at
  // Enter via kernel::FilletConvexEdgesConic/FilletConcaveEdgesConic
  // (PARITY_MAP.md's Blending & chamfering entry: that multi-edge batch API
  // previously had no app call site at all). A single FilletConvexEdgeConic/
  // FilletConcaveEdgeConic call can't simply be chained per edge - the first
  // edge's own output already carries a curved conic wall face, which
  // PlanarFaces() (called first by every one of these functions) rejects on
  // the next edge's own attempt - so instead of applying each Rho pick
  // immediately like the plain/Chamfer/RailType branches below, every pick
  // is staged here (its own distance_i/distance_j/rho captured AT PICK TIME,
  // so changing Distance2/Rho between picks still gives each staged edge its
  // own values) and the whole batch is built in one call once Enter is
  // pressed. All staged edges must belong to the SAME object (the kernel
  // call needs one shared PlanarFaces() snapshot); a pick on a different
  // object is refused rather than silently mixed in.
  std::vector<kernel::ConicEdgeSpec> pending_conic_;
  ObjectId pending_conic_id_ = kNoObject;
  int pending_conic_first_edge_ = -1;  // ON_BrepEdge index of the first staged edge, for the single-edge message
  // Same staging idea as pending_conic_ above, for the RailType
  // (DistFromEdge/DistBetweenRails) case - see the Run() RailType block and
  // FlushPendingRail for why staging is needed and how the batch is built.
  std::vector<std::pair<Point3d, Point3d>> pending_rail_;
  ObjectId pending_rail_id_ = kNoObject;
  // Same staging idea as pending_conic_ above, for the PLAIN (no Rho, no
  // RailType, no Radii=) constant-radius case - closes PARITY_MAP.md's
  // Blending & chamfering "no app command surfaces either function's own
  // chain-pick at all" gap for kernel::FilletConvexEdges/FilletConcaveEdges
  // (the plain circular sibling of FilletConvexEdgesConic/
  // FilletConcaveEdgesConic above, which only Rho ever reached). Applying
  // each plain edge immediately, as this command always used to, means two
  // edges sharing a vertex are never filleted TOGETHER - each call only
  // ever sees a single-edge FilletConvexEdge/FilletConcaveEdge, never the
  // batch functions' own m == 1/m == 3 vertex-blend handling or tangent-
  // chain merge. Staged edges are flushed as one kernel::FilletConvexEdges/
  // FilletConcaveEdges call in FlushPendingFillet (below OnEnter); unlike
  // Rho, there IS an approximate fallback here (the existing TryExactFillet-
  // or-BuildFillet cascade), so a failed batch replays each staged edge
  // through that unchanged, sequential, independent path instead of
  // refusing outright - see FlushPendingFillet's own comment.
  std::vector<std::pair<Point3d, Point3d>> pending_fillet_;
  ObjectId pending_fillet_id_ = kNoObject;
  // Set around FlushPendingFillet's own replay loop so the staging check in
  // Run() steps aside and the ordinary TryExactFillet-or-approximate logic
  // underneath it runs instead, exactly as if staging never existed.
  bool flushing_fillet_ = false;
  std::set<ObjectId> made_;  // results of this run that carry a FilletEdge/ChamferEdge history record (later picks extend it)
};

// ---------------------------------------------------------------------------
// MatchSrf: moves the picked surface's boundary row (and, for Tangency, the
// second row) onto a target curve/surface edge.
// ---------------------------------------------------------------------------

class MatchSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Continuity", "Position", {"Position", "Tangency", "Curvature"}, false, false}};
    WantPoint("Click the surface edge to move (near the edge to match)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n != "Continuity") return;
    continuity_ = v == "Curvature" ? kernel::MatchContinuity::Curvature : v == "Tangency" ? kernel::MatchContinuity::Tangent : kernel::MatchContinuity::Position;
  }
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
    // Against a target *surface* edge, try the exact kernel::NurbsSurface::
    // MatchEdge() first: real NURBS algebra (shared edge curve, matched
    // cross derivatives via the clamped end-derivative formula) rather
    // than this command's own older per-control-point loop below, which
    // for a surface target moves each control point onto the target
    // evaluated at *this* surface's own (u, v) - a "shared parameterization"
    // assumption that's only ever true by coincidence between two
    // independently-built surfaces. MatchEdge() self-checks its own result
    // by evaluation and fails closed, so falling through to the per-CV
    // loop below on Result::Failed (e.g. a periodic edge, or continuity
    // beyond what the target's degree supports) never leaves this command
    // worse off than before this swap.
    bool matched_exact = false;
    kernel::MatchEdgeReport exact_report;
    if (target_brep && target_face) {
      std::optional<ON_NurbsSurface> ts = SurfaceOfObject(*ctx.Doc().Find(target_face->id), target_face->face);
      if (ts) {
        kernel::NurbsSurface kt;
        kt.raw() = *ts;
        double tu, tv;
        if (SurfaceClosestPointGlobal(*ts, target_pick, tu, tv)) {
          const ON_Interval tdu = ts->Domain(0), tdv = ts->Domain(1);
          const double teu0 = tu - tdu.Min(), teu1 = tdu.Max() - tu, tev0 = tv - tdv.Min(), tev1 = tdv.Max() - tv;
          const double tm = std::min({teu0, teu1, tev0, tev1});
          const bool t_is_u_edge = (tm == teu0 || tm == teu1);
          const int target_fixed_dir = t_is_u_edge ? 0 : 1;
          const bool target_at_min = t_is_u_edge ? (tm == teu0) : (tm == tev0);
          if (ks.MatchEdge(fixed_dir, at_min, kt, target_fixed_dir, target_at_min, continuity_, &exact_report) == kernel::Result::Ok) {
            matched_exact = true;
            moved = n_cross;
          }
        }
      }
    }
    // Curvature (G2) has no approximate counterpart in the per-CV loop
    // below - that loop only ever moves the boundary row (Position) and,
    // for Tangency, one more row via a hand-derived step/perpendicular
    // construction with no notion of a matched second derivative at all.
    // Rather than silently downgrade a requested G2 match to G1/G0 (or
    // fabricate a curvature match this loop was never built to compute),
    // this fails closed with a clear diagnostic - the same "no approximate
    // equivalent, fails closed" posture BlendSrf's own Continuity=G3
    // already uses when its own exact construction can't apply, rather
    // than silently doing something less than what was asked.
    if (!matched_exact && continuity_ == kernel::MatchContinuity::Curvature) {
      const std::string why = target_curve ? "a target curve has no curvature-match fallback"
                                            : "the exact match failed (periodic edge, or the target's degree is too low to carry a curvature row)";
      delete target_curve;
      ctx.Warn("MatchSrf: Curvature continuity needs an exact edge match against a target SURFACE edge (kernel::NurbsSurface::MatchEdge) - " +
               why + "; nothing was changed");
      return;
    }
    if (!matched_exact) {
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
        // Curvature (G2) matching has no counterpart in this best-effort,
        // no-target-surface-found fallback - only the exact MatchEdge()
        // path above can do that - so a Curvature request still only
        // approximates tangency here, the closest available continuity.
        if (continuity_ != kernel::MatchContinuity::Position) {
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
    const char* continuity_name = continuity_ == kernel::MatchContinuity::Curvature ? "G2" : continuity_ == kernel::MatchContinuity::Tangent ? "G1" : "G0";
    if (matched_exact) {
      std::string msg = "MatchSrf: exact edge match (" + std::string(continuity_name) + ") to the target surface, max position error " + FormatNumber(exact_report.max_position_error);
      if (continuity_ != kernel::MatchContinuity::Position) msg += ", max tangent error " + FormatNumber(exact_report.max_tangent_error);
      if (continuity_ == kernel::MatchContinuity::Curvature) msg += ", max curvature error " + FormatNumber(exact_report.max_curvature_error);
      ctx.Print(msg);
    } else {
      const char* moved_desc = continuity_ != kernel::MatchContinuity::Position ? "position and tangent" : "position";
      ctx.Print("MatchSrf: " + std::to_string(moved) + " boundary control point(s) moved to " + moved_desc + (target_curve ? " on the target curve" : " on the target surface"));
    }
  }

 private:
  std::optional<FacePick> first_;
  kernel::MatchContinuity continuity_ = kernel::MatchContinuity::Position;
};

// ---------------------------------------------------------------------------
// BlendSrf: two independently-picked surface edges -> a Hermite blend patch.
// ConnectSrf: extends both surfaces to their SSX curve and trims (Partial:
// falls back to a mesh trim note when exact planar trimming isn't possible).
// ---------------------------------------------------------------------------

// The nearest-boundary-isocurve edge pick BlendSrfCommand/VariableBlendSrfCommand
// both use, factored out once: it is ALWAYS exactly one of `s`'s own 4
// isoparametric edges (never a trimmed sub-curve), so the caller also gets
// that edge's own (dir, at_max) description for free - the same pair
// kernel::NurbsSurface::BlendSurfaces() itself needs (surface.h) and the
// same convention MatchSrf's own target_fixed_dir/target_at_min derivation
// above already establishes (dir = which parameter VARIES along the edge;
// at_max = whether the OTHER, fixed parameter sits at Domain().Max()).
// Caller owns the returned curve. Internal linkage: a file-local helper,
// the same "each file keeps its own copy" convention this file's own
// BlendCurves()-duplication comment elsewhere in this document describes.
static ON_Curve* NearestBoundaryIsoEdge(const ON_NurbsSurface& s, Point3d p, int* dir_out, bool* at_max_out) {
  double u, v;
  SurfaceClosestPointGlobal(s, p, u, v);
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  const double eu0 = u - du.Min(), eu1 = du.Max() - u, ev0 = v - dv.Min(), ev1 = dv.Max() - v;
  const double m = std::min({eu0, eu1, ev0, ev1});
  if (m == eu0) { *dir_out = 1; *at_max_out = false; return s.IsoCurve(1, du.Min()); }
  if (m == eu1) { *dir_out = 1; *at_max_out = true; return s.IsoCurve(1, du.Max()); }
  if (m == ev0) { *dir_out = 0; *at_max_out = false; return s.IsoCurve(0, dv.Min()); }
  *dir_out = 0; *at_max_out = true;
  return s.IsoCurve(0, dv.Max());
}

// Whether srf1's own rail (as returned by NearestBoundaryIsoEdge) needs to
// run back-to-front (kernel::NurbsSurface::BlendSurfaces()'s own
// `reverse_rail1`) before pairing with srf0's rail at the same t - i.e.
// whether the two independently-parameterized surfaces walk their own
// (possibly facing, possibly just nearby) edge in opposite physical
// directions. Picked, not guessed: compares the SAME-t and swapped-t total
// endpoint distance (the "orient like a" pattern TweenCurvesCommand::OnNumber
// already uses above, generalized from one comparison to a sum over both
// ends so a genuinely skew pair - not just a reversed one - still picks the
// less-twisted pairing).
static bool BlendRailsNeedReversal(const ON_Curve& ea, const ON_Curve& eb) {
  const ON_Interval da = ea.Domain(), db = eb.Domain();
  const Point3d a0 = ea.PointAt(da.Min()), a1 = ea.PointAt(da.Max());
  const Point3d b0 = eb.PointAt(db.Min()), b1 = eb.PointAt(db.Max());
  const double same = a0.DistanceTo(b0) + a1.DistanceTo(b1);
  const double swapped = a0.DistanceTo(b1) + a1.DistanceTo(b0);
  return swapped < same;
}

// Continuity=Tangency/Curvature/G3/G4 -> kernel::NurbsSurface::
// BlendSurfaces()'s own continuity argument (1/2/3/4) - shared by
// BlendSrfCommand and VariableBlendSrfCommand's identical Continuity
// option.
static int ContinuityOptionToInt(const std::string& v) {
  return v == "G4" ? 4 : (v == "G3" ? 3 : (v == "Curvature" ? 2 : 1));
}

// The boundary isoparametric edge of `s` named by (dir, at_max) - the same pair
// NearestBoundaryIsoEdge reports for a pick. Caller owns the returned curve.
static ON_Curve* BoundaryIsoEdge(const ON_NurbsSurface& s, int dir, bool at_max) {
  const ON_Interval du = s.Domain(0), dv = s.Domain(1);
  return dir == 1 ? s.IsoCurve(1, at_max ? du.Max() : du.Min()) : s.IsoCurve(0, at_max ? dv.Max() : dv.Min());
}

// BlendSrf's construction between two boundary edges (shared by BlendSrfCommand and the
// history rebuild RebuildBlendSrf): the exact kernel Hermite skin first, then (G1/G2 only)
// the app's generic Hermite build. `used_exact` says which one produced `built`.
static bool BuildBlendBetween(CommandContext& ctx, const ON_NurbsSurface& sa, int dir_a, bool at_max_a, const ON_NurbsSurface& sb,
                              int dir_b, bool at_max_b, int continuity, ON_NurbsSurface& built, bool& used_exact) {
  std::unique_ptr<ON_Curve> ea(BoundaryIsoEdge(sa, dir_a, at_max_a)), eb(BoundaryIsoEdge(sb, dir_b, at_max_b));
  if (!ea || !eb) return false;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
  const double max_gap = std::max(std::max(SurfaceScale(sa), SurfaceScale(sb)) * 1e-3, tol * 10);
  double achieved_gap = std::numeric_limits<double>::infinity();
  // Try the exact kernel::NurbsSurface::BlendSurfaces() Hermite-skin
  // construction FIRST - reachable here because both rails are genuine
  // isoparametric boundaries (surface.h's own documented scope) - ahead
  // of the app's own generic BuildBlendSurfaceG1/G2Adaptive fallback,
  // the same "exact kernel construction first, fail open" structure
  // TryExactFillet/TryExactChamfer already establish elsewhere in this
  // file. This is also the only path that reaches Continuity=G3 at all.
  kernel::NurbsSurface ksa, ksb;
  ksa.raw() = sa;
  ksb.raw() = sb;
  const bool reverse1 = BlendRailsNeedReversal(*ea, *eb);
  const bool exact_ok = BuildBlendSurfaceKernelAdaptive(dir_a, at_max_a, ksa, dir_b, at_max_b, ksb, reverse1,
                                                         continuity, max_gap, 8, 256, built, &achieved_gap);
  bool ok = exact_ok && std::isfinite(achieved_gap);
  used_exact = ok;
  if (!ok && continuity < 3) {
    // G1/G2 can still fall back to the app's own generic Hermite build
    // (e.g. the exact construction's own degenerate-rail rejection); a
    // caller-requested G3 or G4 has no approximate equivalent to fall
    // back to at all, so it is reported as a failure instead of silently
    // dropping to a G1/G2 result the caller never asked for.
    auto uv_on = [&](const ON_NurbsSurface& s, const ON_Curve& c, double t01) {
      const ON_Interval d = c.Domain();
      const Point3d p3 = c.PointAt(d.ParameterAt(t01));
      double u, v;
      SurfaceClosestPoint(s, p3, u, v);
      return ON_2dPoint(u, v);
    };
    auto uv_a_fn = [&](double t) { return uv_on(sa, *ea, t); };
    auto uv_b_fn = [&](double t) { return uv_on(sb, *eb, t); };
    achieved_gap = std::numeric_limits<double>::infinity();
    continuity == 2 ? BuildBlendSurfaceG2Adaptive(*ea, sa, uv_a_fn, *eb, sb, uv_b_fn, max_gap, 24, 384, built, &achieved_gap)
                    : BuildBlendSurfaceG1Adaptive(*ea, sa, uv_a_fn, *eb, sb, uv_b_fn, false, max_gap, 24, 384, built, &achieved_gap);
    ok = std::isfinite(achieved_gap);
  }
  return ok;
}

class BlendSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Continuity", "Tangency", {"Tangency", "Curvature", "G3", "G4"}, false, false}};
    WantPoint("Click the first surface edge (near the edge to blend from)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Continuity") continuity_ = ContinuityOptionToInt(v); }
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
    // Nearest boundary iso curve to each pick becomes the edge to blend
    // from - always a genuine isoparametric boundary of sa/sb (never a
    // trimmed sub-curve), so dir_a/dir_b/at_max_a/at_max_b below are always
    // meaningful for the exact kernel path tried first.
    int dir_a, dir_b;
    bool at_max_a, at_max_b;
    ON_Curve* ea = NearestBoundaryIsoEdge(*sa, pa, &dir_a, &at_max_a);
    ON_Curve* eb = NearestBoundaryIsoEdge(*sb, pb, &dir_b, &at_max_b);
    if (!ea || !eb) { ctx.Warn("BlendSrf: could not find a boundary edge at the pick"); delete ea; delete eb; return; }
    delete ea;
    delete eb;
    ON_NurbsSurface built;
    bool used_exact = false;
    const bool ok = BuildBlendBetween(ctx, *sa, dir_a, at_max_a, *sb, dir_b, at_max_b, continuity_, built, used_exact);
    if (!ok) { ctx.Warn("BlendSrf: could not build the blend"); return; }
    ctx.Doc().BeginChange("BlendSrf");
    SceneObject like = *oa;
    ObjectId nid = AddSurfaceFrom(ctx, built, like);
    ctx.Doc().Select(nid, true);
    // History On: the blend is recorded as {surface A, surface B} with the boundary edges as (iso direction, at max)
    // and the face indices - intrinsic to each surface, so they survive Move/Rotate/Scale and control-point edits.
    RecordHistoryIfEnabled(ctx, nid, "BlendSrf", {fa.id, fb.id},
                           {{"dir_a", static_cast<double>(dir_a)}, {"max_a", at_max_a ? 1.0 : 0.0}, {"face_a", static_cast<double>(fa.face)},
                            {"dir_b", static_cast<double>(dir_b)}, {"max_b", at_max_b ? 1.0 : 0.0}, {"face_b", static_cast<double>(fb.face)},
                            {"cont", static_cast<double>(continuity_)}});
    const char* cname = continuity_ == 4 ? "G4" : (continuity_ == 3 ? "G3" : (continuity_ == 2 ? "Curvature" : "Tangency"));
    ctx.Print(std::string("BlendSrf: blend surface added between object ") + std::to_string(fa.id) + " and " + std::to_string(fb.id) +
               " (Continuity=" + cname + (used_exact ? ", exact kernel blend" : "") + ")");
  }

 private:
  std::optional<FacePick> first_;
  Point3d first_pt_{0, 0, 0};
  int continuity_ = 1;
};

// ---------------------------------------------------------------------------
// VariableBlendSrf: a genuine independent blend-tangent variant of BlendSrf,
// not the rolling-ball fillet construction VariableFilletSrf uses. It picks
// two surface edges exactly like BlendSrfCommand and builds the same
// BuildBlendSurfaceG1/G2 Hermite/quintic blend, but with the tangent-
// magnitude fraction (the blend's cross-section "width") interpolated
// linearly along the rail from Width= at t=0 to EndWidth= at t=1 via
// width_frac_at - the same piecewise-linear-along-the-rail technique
// FilletTwoSurfacesCommand::Run uses for its own Radius=/EndRadius=
// (radius_at(t) = r0 + (r1-r0)*t), just applied to the blend's width
// instead of a rolling-ball radius. Because this reuses
// BuildBlendSurfaceG1/G2 directly, Continuity=Curvature here gets the exact
// same real, numerically-verified G2 cross-boundary curvature match BlendSrf
// gets (see BuildBlendSurfaceG2's own comment and tests/test_g2_blend.cpp) -
// something no rolling-ball fillet construction (fixed circular
// cross-section) can give except at the one radius that happens to match
// the adjacent surface's own curvature.
class VariableBlendSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Width", FormatNumber(width0_), {}, true, false}, {"EndWidth", FormatNumber(width1_), {}, true, false}, {"Continuity", "Tangency", {"Tangency", "Curvature"}, false, false}};
    WantPoint("Click the first surface edge (near the edge to blend from)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Width") width0_ = std::atof(v.c_str());
    if (n == "EndWidth") width1_ = std::atof(v.c_str());
    if (n == "Continuity") curvature_ = (v == "Curvature");
  }
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
    // Same nearest-boundary-isocurve edge pick as BlendSrfCommand::Run.
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
    if (!ea || !eb) { ctx.Warn("VariableBlendSrf: could not find a boundary edge at the pick"); delete ea; delete eb; return; }
    auto uv_on = [&](const ON_NurbsSurface& s, const ON_Curve& c, double t01) {
      const ON_Interval d = c.Domain();
      const Point3d p3 = c.PointAt(d.ParameterAt(t01));
      double u, v;
      SurfaceClosestPoint(s, p3, u, v);
      return ON_2dPoint(u, v);
    };
    auto uv_a_fn = [&](double t) { return uv_on(*sa, *ea, t); };
    auto uv_b_fn = [&](double t) { return uv_on(*sb, *eb, t); };
    // Width interpolated linearly along the rail, exactly the
    // FilletTwoSurfacesCommand::Run radius_at(t) pattern (r0 + (r1-r0)*t)
    // applied to the blend's own tangent-magnitude fraction instead of a
    // rolling-ball radius - this is the "vary the width along the rail"
    // piece layered on top of the independent blend construction.
    const double w0 = width0_, w1 = width1_;
    auto width_at = [w0, w1](double t) { return w0 + (w1 - w0) * t; };
    ON_NurbsSurface built;
    // Adaptive, tolerance-enforcing build - see FilletEdgeCommand's own
    // Mode::Blend block (cmd_fillet.cpp, above) for the full rationale;
    // same scale-aware max_gap floor.
    const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
    const double max_gap = std::max(std::max(SurfaceScale(*sa), SurfaceScale(*sb)) * 1e-3, tol * 10);
    double achieved_gap = std::numeric_limits<double>::infinity();
    curvature_ ? BuildBlendSurfaceG2Adaptive(*ea, *sa, uv_a_fn, *eb, *sb, uv_b_fn, max_gap, 24, 384, built, &achieved_gap, width_at)
               : BuildBlendSurfaceG1Adaptive(*ea, *sa, uv_a_fn, *eb, *sb, uv_b_fn, false, max_gap, 24, 384, built, &achieved_gap, width_at);
    const bool ok = std::isfinite(achieved_gap);
    delete ea;
    delete eb;
    if (!ok) { ctx.Warn("VariableBlendSrf: could not build the blend"); return; }
    ctx.Doc().BeginChange("VariableBlendSrf");
    SceneObject like = *oa;
    ObjectId nid = AddSurfaceFrom(ctx, built, like);
    ctx.Doc().Select(nid, true);
    ctx.Print(std::string("VariableBlendSrf: blend surface added between object ") + std::to_string(fa.id) + " and " + std::to_string(fb.id) + ", width " + FormatNumber(w0) + " to " + FormatNumber(w1) +
              (curvature_ ? " (Continuity=Curvature: quintic blend, cross-boundary curvature matched exactly to both surfaces)" : " (Continuity=Tangency)"));
  }

 private:
  std::optional<FacePick> first_;
  Point3d first_pt_{0, 0, 0};
  bool curvature_ = false;
  double width0_ = 0.35, width1_ = 0.7;
};

// ---------------------------------------------------------------------------
// ConnectSrf's general (non-planar) trim: given an already-extended surface
// and the real SSX join curve's own pullback onto it (IntersectSurfaces
// already computes one 2D pcurve per input surface as part of finding the
// curve at all - see IntersectionCurve::pcurve_a/pcurve_b in
// surface_intersect.h - so no separate projection pass is needed here),
// builds a real trimmed loop and hands it to geom/BrepTrimFace.h's
// AddTrimmedFace: the same Brep vertex/edge/trim construction the IGES/STEP
// importer uses for every trimmed face it reads from a file. Reused
// wholesale rather than reinvented - connecting two surfaces along an
// arbitrary space curve is exactly "build one trimmed Brep face from a
// surface plus a loop of (2D,3D) curve pairs", the importer's own job.
// ---------------------------------------------------------------------------

// Where a 2D point sits on an axis-aligned rectangle's own perimeter, as a
// value in [0,4): 0..1 along the bottom edge (v=min), 1..2 up the right
// edge (u=max), 2..3 back along the top edge (v=max), 3..4 down the left
// edge (u=min). The four formulas agree exactly at every corner (each
// corner satisfies two of the four edge tests; both give the same integer),
// so which one a corner happens to hit first is never ambiguous.
double RectPerimeterParam(ON_2dPoint p, const ON_Interval& du, const ON_Interval& dv) {
  const double u0 = du.Min(), u1 = du.Max(), v0 = dv.Min(), v1 = dv.Max();
  const double eps = 1e-7 * std::max(1.0, std::max(u1 - u0, v1 - v0));
  if (std::fabs(p.y - v0) <= eps) return std::clamp((p.x - u0) / std::max(u1 - u0, 1e-300), 0.0, 1.0);
  if (std::fabs(p.x - u1) <= eps) return 1.0 + std::clamp((p.y - v0) / std::max(v1 - v0, 1e-300), 0.0, 1.0);
  if (std::fabs(p.y - v1) <= eps) return 2.0 + std::clamp((u1 - p.x) / std::max(u1 - u0, 1e-300), 0.0, 1.0);
  return 3.0 + std::clamp((v1 - p.y) / std::max(v1 - v0, 1e-300), 0.0, 1.0);
}

// True when `p` sits within `tol_uv` of the rectangle's own boundary.
bool OnRectBoundary(ON_2dPoint p, const ON_Interval& du, const ON_Interval& dv, double tol_uv) {
  return std::fabs(p.x - du.Min()) <= tol_uv || std::fabs(p.x - du.Max()) <= tol_uv || std::fabs(p.y - dv.Min()) <= tol_uv ||
         std::fabs(p.y - dv.Max()) <= tol_uv;
}

// Casts a ray from an interior point `p` along `dir` to the rectangle's own
// boundary. `p` is assumed strictly interior, so a real exit always exists
// unless `dir` itself is degenerate.
bool RayToRectExit(ON_2dPoint p, ON_2dVector dir, const ON_Interval& du, const ON_Interval& dv, ON_2dPoint& exit_out) {
  double best_t = -1;
  auto try_edge = [&](double t, double other, double lo, double hi) {
    if (t <= 1e-9 || (best_t >= 0 && t >= best_t)) return;
    const double slack = 1e-6 * std::max(1.0, hi - lo);
    if (other < lo - slack || other > hi + slack) return;
    best_t = t;
  };
  if (std::fabs(dir.x) > 1e-300) {
    double t = (du.Min() - p.x) / dir.x; try_edge(t, p.y + t * dir.y, dv.Min(), dv.Max());
    t = (du.Max() - p.x) / dir.x; try_edge(t, p.y + t * dir.y, dv.Min(), dv.Max());
  }
  if (std::fabs(dir.y) > 1e-300) {
    double t = (dv.Min() - p.y) / dir.y; try_edge(t, p.x + t * dir.x, du.Min(), du.Max());
    t = (dv.Max() - p.y) / dir.y; try_edge(t, p.x + t * dir.x, du.Min(), du.Max());
  }
  if (best_t < 0) return false;
  exit_out = ON_2dPoint(std::clamp(p.x + best_t * dir.x, du.Min(), du.Max()), std::clamp(p.y + best_t * dir.y, dv.Min(), dv.Max()));
  return true;
}

// A straight 2D trim segment from `a` to `b`, paired with the matching EXACT
// 3D edge: when the segment runs along the rectangle's own boundary (the
// only shape AppendRectWalk ever builds), that is a real iso-boundary curve
// of `srf` (u=const or v=const), taken via IsoCurve+Trim rather than
// approximated by a 3D chord.
LoopSeg RectEdgeSeg(const ON_NurbsSurface& srf, ON_2dPoint a, ON_2dPoint b) {
  LoopSeg seg;
  seg.c2.Create(2, false, 2, 2);
  seg.c2.SetCV(0, ON_3dPoint(a.x, a.y, 0));
  seg.c2.SetCV(1, ON_3dPoint(b.x, b.y, 0));
  seg.c2.SetKnot(0, 0);
  seg.c2.SetKnot(1, 1);
  const bool u_const = std::fabs(a.x - b.x) < 1e-9 * std::max(1.0, srf.Domain(0).Length());
  ON_Curve* iso = u_const ? srf.IsoCurve(1, a.x) : srf.IsoCurve(0, a.y);
  ON_NurbsCurve isonc;
  if (iso && iso->GetNurbForm(isonc) > 0) {
    const double t0 = u_const ? a.y : a.x, t1 = u_const ? b.y : b.x;
    isonc.Trim(ON_Interval(std::min(t0, t1), std::max(t0, t1)));
    if (t0 > t1) isonc.Reverse();
    seg.c3 = isonc;
  } else {
    // A real NURBS rectangle's own boundary should never fail to produce an
    // iso-curve; kept only as a last-resort straight chord so a single odd
    // surface can't crash the whole trim.
    ON_LineCurve lc(srf.PointAt(a.x, a.y), srf.PointAt(b.x, b.y));
    lc.GetNurbForm(seg.c3);
  }
  delete iso;
  return seg;
}

// Appends the rectangle-boundary path from `from` to `to` (both ON the
// boundary) as real LoopSegs onto `out`, walking in the increasing-
// perimeter-parameter direction when `forward`, the decreasing direction
// otherwise (`from == to` with `forward` walks the WHOLE boundary once).
void AppendRectWalk(const ON_NurbsSurface& srf, ON_2dPoint from, ON_2dPoint to, bool forward, std::vector<LoopSeg>& out) {
  const ON_Interval du = srf.Domain(0), dv = srf.Domain(1);
  const std::array<ON_2dPoint, 4> corner = {ON_2dPoint(du.Min(), dv.Min()), ON_2dPoint(du.Max(), dv.Min()), ON_2dPoint(du.Max(), dv.Max()),
                                             ON_2dPoint(du.Min(), dv.Max())};
  const double sf = RectPerimeterParam(from, du, dv), st = RectPerimeterParam(to, du, dv);
  const bool full_loop = (from - to).Length() < 1e-9 * std::max(1.0, std::max(du.Length(), dv.Length()));
  const double span = full_loop ? 4.0 : (forward ? std::fmod(st - sf + 4.0, 4.0) : std::fmod(sf - st + 4.0, 4.0));
  std::vector<std::pair<double, ON_2dPoint>> stops;
  for (const ON_2dPoint& c : corner) {
    const double cs = RectPerimeterParam(c, du, dv);
    const double rel = forward ? std::fmod(cs - sf + 4.0, 4.0) : std::fmod(sf - cs + 4.0, 4.0);
    if (rel > 1e-7 && (full_loop ? rel < 4.0 - 1e-7 : rel < span - 1e-7)) stops.emplace_back(rel, c);
  }
  std::sort(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<ON_2dPoint> path = {from};
  for (const auto& [rel, pt] : stops) path.push_back(pt);
  path.push_back(to);
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    if ((path[i] - path[i + 1]).Length() < 1e-12) continue;
    out.push_back(RectEdgeSeg(srf, path[i], path[i + 1]));
  }
}

// Samples a loop's own 2D trim curves into a coarse polygon, for a
// point-in-polygon "which side is this on" test.
std::vector<ON_2dPoint> SampleLoopPolygon(const std::vector<LoopSeg>& segs) {
  std::vector<ON_2dPoint> poly;
  for (const LoopSeg& s : segs) {
    const ON_Interval d = s.c2.Domain();
    const int n = std::clamp(std::max(1, s.c2.SpanCount()) * 6, 8, 200);
    for (int i = 0; i <= n; ++i) {
      const ON_3dPoint q = s.c2.PointAt(d.ParameterAt(static_cast<double>(i) / n));
      poly.emplace_back(q.x, q.y);
    }
  }
  return poly;
}

// Attempts a real general-case trim of `srf` (already extended) along the
// SSX join curve's own pullback `pcurve2d`/`curve3d` pair (both share one
// parametrisation - straight from IntersectSurfaces), keeping the side of
// `keep_uv` (the surface's own PRE-extension domain centre - Extend() only
// grows the domain, leaving the original domain's own parameter values in
// place, so this point's meaning survives the extension unchanged). Returns
// the resulting outer trim loop on success; sets `why_not` to a specific,
// real reason on failure. Not every join curve can be closed into a valid
// loop from one surface's own domain rectangle alone (see the "both ends
// interior" case below) - that is reported honestly rather than forced.
std::optional<std::vector<LoopSeg>> BuildConnectTrimLoop(const ON_NurbsSurface& srf, const ON_NurbsCurve& pcurve2d, const ON_NurbsCurve& curve3d,
                                                          ON_2dPoint keep_uv, std::string& why_not) {
  const ON_Interval du = srf.Domain(0), dv = srf.Domain(1);
  const double tol_uv = 1e-6 * std::max(1.0, std::max(du.Length(), dv.Length()));

  if (pcurve2d.IsClosed()) {
    // The join curve is a closed loop that stays fully inside (or fully
    // outside) this one surface's own domain, rather than crossing it - a
    // genuinely different, multi-loop-face case (e.g. one surface fully
    // piercing the other) this pass does not attempt: a single outer loop
    // has nowhere to open onto the domain's own boundary here.
    why_not = "the join curve forms a closed loop on this surface rather than crossing its domain edge (e.g. one surface fully piercing the "
              "other) - left untrimmed; use Split/Trim manually";
    return std::nullopt;
  }

  const ON_3dPoint p0 = pcurve2d.PointAtStart(), p1 = pcurve2d.PointAtEnd();
  ON_2dPoint uv0(p0.x, p0.y), uv1(p1.x, p1.y);
  const bool start_on = OnRectBoundary(uv0, du, dv, tol_uv);
  const bool end_on = OnRectBoundary(uv1, du, dv, tol_uv);
  if (!start_on && !end_on) {
    why_not = "the join curve does not reach this surface's own domain edge anywhere (it is bounded only by the OTHER surface, floating loose "
              "in the middle of this one) - no unique region to trim to; left untrimmed";
    return std::nullopt;
  }

  std::vector<LoopSeg> loop;
  ON_2dPoint loop_start = uv0, loop_end = uv1;
  const ON_Interval cd = pcurve2d.Domain();
  if (!start_on) {
    ON_3dPoint pt; ON_3dVector tan;
    ON_2dPoint exit;
    if (!pcurve2d.Ev1Der(cd.Min(), pt, tan) || ON_2dVector(tan.x, tan.y).Length() < 1e-12 ||
        !RayToRectExit(uv0, ON_2dVector(-tan.x, -tan.y), du, dv, exit)) {
      why_not = "could not extend the join curve's open start to this surface's own domain edge";
      return std::nullopt;
    }
    loop.push_back(RectEdgeSeg(srf, exit, uv0));
    loop_start = exit;
  }
  LoopSeg main_seg;
  main_seg.c2 = pcurve2d;
  main_seg.c3 = curve3d;
  loop.push_back(main_seg);
  if (!end_on) {
    ON_3dPoint pt; ON_3dVector tan;
    ON_2dPoint exit;
    if (!pcurve2d.Ev1Der(cd.Max(), pt, tan) || ON_2dVector(tan.x, tan.y).Length() < 1e-12 || !RayToRectExit(uv1, ON_2dVector(tan.x, tan.y), du, dv, exit)) {
      why_not = "could not extend the join curve's open end to this surface's own domain edge";
      return std::nullopt;
    }
    loop.push_back(RectEdgeSeg(srf, uv1, exit));
    loop_end = exit;
  }

  // Close loop_end -> loop_start by walking the rectangle boundary; try
  // both directions and keep whichever one actually contains `keep_uv` (the
  // side the surface's own original footprint is on).
  std::vector<LoopSeg> fwd = loop, bwd = loop;
  AppendRectWalk(srf, loop_end, loop_start, true, fwd);
  AppendRectWalk(srf, loop_end, loop_start, false, bwd);
  const std::vector<ON_2dPoint> poly_fwd = SampleLoopPolygon(fwd);
  if (poly_fwd.size() >= 3 && PointInPolygon(poly_fwd, keep_uv)) return fwd;
  if (SampleLoopPolygon(bwd).size() >= 3) return bwd;
  why_not = "neither side of the join curve's rectangle closure contains this surface's own original footprint";
  return std::nullopt;
}

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
    ON_Plane pa, pb;
    ObjectId ida = kNoObject, idb = kNoObject;
    bool trimmed_ok = false;
    std::string note;
    if (ea.IsPlanar(&pa, tol * 10) && eb.IsPlanar(&pb, tol * 10)) {
      // Both planar: two extended planes always meet along their whole
      // shared line, so the extended domains already reach the join with
      // nothing left to cut away for a visual connection - the one case
      // that needed no further trim step to begin with.
      ida = AddSurfaceFrom(ctx, ea, like);
      idb = AddSurfaceFrom(ctx, eb, like);
      trimmed_ok = true;
    } else {
      // General (non-planar) case: real trim, via the SSX curve's own
      // pullback onto each extended surface, using geom/BrepTrimFace.h's
      // Brep-loop construction (see BuildConnectTrimLoop above).
      const ON_2dPoint keep_a(sa->Domain(0).Mid(), sa->Domain(1).Mid());
      const ON_2dPoint keep_b(sb->Domain(0).Mid(), sb->Domain(1).Mid());
      std::string why_a, why_b;
      std::optional<std::vector<LoopSeg>> loop_a = BuildConnectTrimLoop(ea, best->pcurve_a, best->curve, keep_a, why_a);
      std::optional<std::vector<LoopSeg>> loop_b = BuildConnectTrimLoop(eb, best->pcurve_b, best->curve, keep_b, why_b);
      int trimmed_count = 0;
      bool ok_a = false, ok_b = false;
      // Finishes a freshly built single-face trim: FinishBrepTrims's own
      // SetTolerancesBoxesAndFlags() recomputes every edge tolerance from
      // scratch by default, which for an edge shared by only ONE trim (the
      // ordinary case here - this is a single open, naked-boundary face,
      // never the shared-by-two-faces edges FileIgesStep.cpp's own callers
      // always have) can fail internally and leave it ON_UNSET_VALUE - the
      // exact same failure TrimWholeLoop's own SetTolerancesBoxesAndFlags
      // call above already works around by turning edge-tolerance
      // recompute off (bSetEdgeTolerances=false) and keeping the real
      // tolerance set when each edge was built instead.
      auto finish_open_face = [&](ON_Brep& b) {
        FinishBrepTrims(b);
        for (int ei = 0; ei < b.m_E.Count(); ++ei) {
          if (!(b.m_E[ei].m_tolerance >= 0)) b.m_E[ei].m_tolerance = tol;
        }
      };
      if (loop_a) {
        ON_Brep ba;
        const int fi = AddTrimmedFace(ba, new ON_NurbsSurface(ea), {*loop_a}, tol);
        if (fi >= 0) finish_open_face(ba);
        if (fi >= 0 && ba.IsValid()) { ida = AddBrepFrom(ctx, ba, like); ++trimmed_count; ok_a = true; }
        else why_a = "the trimmed result was not a valid B-rep";
      }
      if (ida == kNoObject) ida = AddSurfaceFrom(ctx, ea, like);
      if (loop_b) {
        ON_Brep bb;
        const int fi = AddTrimmedFace(bb, new ON_NurbsSurface(eb), {*loop_b}, tol);
        if (fi >= 0) finish_open_face(bb);
        if (fi >= 0 && bb.IsValid()) { idb = AddBrepFrom(ctx, bb, like); ++trimmed_count; ok_b = true; }
        else why_b = "the trimmed result was not a valid B-rep";
      }
      if (idb == kNoObject) idb = AddSurfaceFrom(ctx, eb, like);
      trimmed_ok = trimmed_count == 2;
      if (!ok_a || !ok_b) {
        note = "; " + std::to_string(trimmed_count) + "/2 surfaces trimmed";
        if (!ok_a) note += " (surface " + std::to_string(ids[0]) + ": " + why_a + ")";
        if (!ok_b) note += " (surface " + std::to_string(ids[1]) + ": " + why_b + ")";
      }
    }
    ObjectId idc = AddCurveFrom(ctx, best->curve, like);
    ctx.Doc().Select(ida, true);
    ctx.Doc().Select(idb, true);
    ctx.Doc().Select(idc, true);
    ctx.Print(std::string("ConnectSrf: extended both surfaces to their real intersection curve") + (trimmed_ok ? "; both surfaces trimmed to the join" : note));
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
    // A polysurface face keeps the body's real topology: the kernel's own
    // general SplitFaceByCurve() (dino8/kernel/boolean_general.h) replaces
    // just face_->face with two faces sharing a genuine new ON_BrepEdge on
    // the SAME ON_Brep, leaving every other face of the body untouched -
    // unlike the surface-only path below, which only ever produces two
    // disconnected floating surfaces with no shared edge and (for a
    // picked polysurface face) doesn't touch the original body at all.
    if (o->kind == ObjectKind::Brep && o->brep) {
      ON_NurbsCurve cnc;
      if (cutter->GetNurbForm(cnc) <= 0) { ctx.Warn("SplitFace: could not read the cutter curve"); return; }
      kernel::NurbsCurve kc;
      kc.raw() = cnc;
      kernel::Brep kb;
      kb.raw() = *b;
      try {
        kernel::Brep split = kernel::SplitFaceByCurve(kb, face_->face, kc, tol);
        ctx.Doc().BeginChange("SplitFace");
        o->brep->raw() = split.raw();
        o->InvalidateDisplay();
        ctx.Doc().Select(face_->id, true);
        ctx.Print("SplitFace: face " + std::to_string(face_->face) + " of object " + std::to_string(face_->id) + " split in place, body topology kept");
      } catch (const std::exception& ex) {
        ctx.Warn("SplitFace: " + std::string(ex.what()));
      }
      return;
    }
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

// Same nearest-edge search as PickEdge() above, but for SplitEdgeCommand
// specifically: inserting a mid-edge vertex has no need of PickEdge()'s
// own "needs two adjacent faces" restriction (that's a genuine
// requirement for the FILLET-family commands sharing PickEdge - a fillet
// needs a real edge between two faces to roll a blend across - not one
// SplitEdge itself has: splitting a naked edge is, if anything, simpler,
// since there's only ever one trim's own 2D curve to also split). Without
// its own picker, SplitEdge would silently skip every naked edge and
// resolve to whatever SHARED edge happens to be nearest instead - often
// nowhere near the click, and one whose closest-point parameter then
// sits right at its own domain boundary (Split()'s own documented
// "fails at an exact endpoint" case), which is exactly the confusing
// "could not split the edge curve" this used to produce for a click
// that was actually right on a valid naked edge.
std::optional<EdgePick> PickEdgeAny(CommandContext& ctx, Point3d p) {
  std::optional<EdgePick> best;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::optional<ON_Brep> b = BrepOfObject(o);
    if (!b) continue;
    for (int i = 0; i < b->m_E.Count(); ++i) {
      const ON_BrepEdge& e = b->m_E[i];
      if (e.m_edge_index < 0 || e.TrimCount() < 1) continue;
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

class SplitEdgeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the edge, near where you want to split it"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<EdgePick> pick = PickEdgeAny(ctx, p);
    if (!pick) { ctx.Warn("No edge near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) { ctx.Warn("SplitEdge needs a polysurface edge"); Finish(); return; }
    ON_Brep b = o->brep->raw();
    const int old_ei = pick->edge;
    double t;
    if (!EdgeClosest(b.m_E[old_ei], p, t)) { Finish(); return; }
    // Insert a vertex at t: split the edge curve and re-wire each trim's
    // affected side onto a new edge sharing the new vertex.
    ON_NurbsCurve nc;
    b.m_E[old_ei].GetNurbForm(nc);
    kernel::NurbsCurve kc;
    kc.raw() = nc;
    kernel::NurbsCurve left, right;
    if (kc.Split(t, left, right) != kernel::Result::Ok) { ctx.Warn("SplitEdge: could not split the edge curve"); Finish(); return; }
    // `t` is the EDGE's own raw domain parameter (whatever that edge's
    // domain actually is - not necessarily [0,1], see the fraction/frac
    // use below) - captured now, before old_ei's own edge is replaced.
    const ON_Interval edge_dom = b.m_E[old_ei].Domain();
    // Everything below is kept by INDEX, never by reference, across any
    // New*()/Add*() call: each one appends to its own ON_Brep array
    // (m_V/m_E/m_T/...), which can reallocate that array's buffer - a
    // reference taken before such a call (the original code's own e/v0/
    // v1/trim references) is left silently dangling by one that
    // reallocates, a real crash this fixes (found running this on a
    // naked edge for the first time - see PickEdgeAny's own doc comment
    // above for why that case was never actually exercised before).
    const int v0i = b.m_E[old_ei].m_vi[0];
    const int v1i = b.m_E[old_ei].m_vi[1];
    const Point3d mid_pt = b.m_E[old_ei].PointAt(t);
    const int vmid_i = b.NewVertex(mid_pt, std::max(ctx.Settings().absolute_tolerance, 1e-5)).m_vertex_index;
    const int c0 = b.AddEdgeCurve(new ON_NurbsCurve(left.raw()));
    const int c1 = b.AddEdgeCurve(new ON_NurbsCurve(right.raw()));
    const int e0i = b.NewEdge(b.m_V[v0i], b.m_V[vmid_i], c0).m_edge_index;
    const int e1i = b.NewEdge(b.m_V[vmid_i], b.m_V[v1i], c1).m_edge_index;
    // Re-point every trim that used the old edge onto the matching new half.
    // The old edge's own trim-index LIST is copied by value up front - its
    // storage lives on the OLD edge object, which NewTrim() below could
    // just as easily leave dangling (b.m_E reallocating) if read fresh
    // from inside the loop.
    const ON_SimpleArray<int> old_ti = b.m_E[old_ei].m_ti;
    for (int i = 0; i < old_ti.Count(); ++i) {
      const int ti = old_ti[i];
      const bool rev = b.m_T[ti].m_bRev3d;
      // AttachToEdge() (the same "expert user" primitive UnjoinEdge() above
      // uses) - not a raw trim.m_ei assignment, which would leave the NEW
      // edge's own m_ti[] never told about this trim at all.
      b.m_T[ti].AttachToEdge(rev ? e1i : e0i, rev);
      // Both halves need a trim in the loop; duplicate the trim's 2D curve,
      // split it at the matching parameter and add the second half.
      ON_Curve* c2 = b.m_T[ti].DuplicateCurve();
      kernel::NurbsCurve tk;
      ON_NurbsCurve tnc;
      c2->GetNurbForm(tnc);
      tk.raw() = tnc;
      kernel::NurbsCurve tleft, tright;
      // The split point as a FRACTION of the edge's own domain (`t` is a
      // raw parameter value on THAT domain, not necessarily [0,1] - the
      // original code's own trim_dom.ParameterAt(t) treated it as if it
      // already were one, which is only ever right by coincidence when
      // the edge's domain happens to itself be [0,1]; on a real box edge,
      // whose domain is closer to real-world arc length, that silently
      // split the trim curve at the WRONG point, sometimes badly enough
      // to leave the loop's own 2D boundary discontinuous - a real,
      // separate bug this fixes). A REVERSED trim's own 2D curve runs
      // opposite the edge's 3D direction, so its fraction is (1 - the
      // edge's).
      const double edge_frac = edge_dom.NormalizedParameterAt(t);
      const double trim_frac = rev ? (1.0 - edge_frac) : edge_frac;
      const kernel::Interval tk_dom = tk.Domain();
      const double tk_split = tk_dom.min + (tk_dom.max - tk_dom.min) * trim_frac;
      if (tk.Split(tk_split, tleft, tright) == kernel::Result::Ok) {
        const int li = b.m_T[ti].m_li;
        const ON_BrepTrim::TYPE trim_type = b.m_T[ti].m_type;
        // `ti` keeps its OWN start (tk_dom.min, i.e. `tleft`) regardless of
        // `rev` - AttachToEdge() above already re-pointed it to whichever
        // of e0i/e1i shares that same real-world span (see the comment on
        // that call), so no rev-based swap belongs here too: doing both
        // would put the LARGE half's own 3D edge together with the SMALL
        // half's own 2D curve (or vice versa) whenever rev is true - a
        // genuine curve/edge-length mismatch that Check() would (rightly)
        // flag as a discontinuous loop, found running this on a reversed
        // naked trim for the first time.
        const int c2i_first = b.AddTrimCurve(new ON_NurbsCurve(tleft.raw()));
        const int c2i_second = b.AddTrimCurve(new ON_NurbsCurve(tright.raw()));
        // ChangeTrimCurve() - not a raw trim.m_c2i assignment, which leaves
        // this trim's OWN curve-proxy (it inherits ON_CurveProxy) still
        // pointing at its old, pre-split curve: m_c2i said "use the new
        // one" but every actual evaluation (PointAtStart(), used right
        // after by SetTolerancesBoxesAndFlags()) kept reading the old,
        // now-wrong-length one - the real cause of a hard-to-place SIGSEGV
        // this fixes (ON_CurveProxy::Dimension() on a curve whose own
        // domain/CV state no longer matched its trim's new, much shorter
        // span).
        b.m_T[ti].ChangeTrimCurve(c2i_first);
        const int new_ti = b.NewTrim(b.m_E[rev ? e0i : e1i], rev, b.m_L[li], c2i_second).m_trim_index;
        b.m_T[new_ti].m_type = trim_type;
        // NewTrim() only APPENDS to loop.m_ti (see its own source) - it
        // has no idea this new trim is really the second half of `ti`,
        // splitting its old position. Splice it back to sit immediately
        // after `ti` instead of at the end, so the loop's own cyclic
        // trim order - what PrevTrim()/NextTrim() (and hence
        // Brep::RemoveNakedMicroEdge()'s own loop-neighbor search) walk
        // - still matches the two halves' real geometric adjacency.
        ON_BrepLoop& loop = b.m_L[li];
        const int last = loop.m_ti.Count() - 1;
        if (last >= 0 && loop.m_ti[last] == new_ti) {
          loop.m_ti.Remove(last);
          int ti_pos = -1;
          for (int k = 0; k < loop.m_ti.Count(); ++k) {
            if (loop.m_ti[k] == ti) { ti_pos = k; break; }
          }
          loop.m_ti.Insert(ti_pos >= 0 ? ti_pos + 1 : loop.m_ti.Count(), new_ti);
        }
      }
      delete c2;
    }
    b.m_E[old_ei].m_edge_index = -1;  // orphaned; Compact() removes it
    b.Compact();
    b.SetTolerancesBoxesAndFlags();
    // ON_Brep::SetEdgeTolerance() (the OPEN-SOURCE base class SetTolerances-
    // BoxesAndFlags() above actually calls) is a documented no-op for any
    // edge with at least one trim - it deliberately leaves edge.m_tolerance
    // at ON_UNSET_VALUE and says so in its own comment ("TL_Brep::
    // SetEdgeTolerance overrides ... and sets the tolerance correctly" -
    // TL_Brep isn't part of this build). Every kernel-side topology method
    // here already works around exactly this gap (see brep.cpp's own
    // FixUnsetEdgeTolerances(), called right after every one of its own
    // SetTolerancesBoxesAndFlags() calls) - this is that same one-line fix,
    // inline, for the one app-layer command that edits real Brep topology
    // directly instead of going through the kernel: an edge tolerance left
    // at ON_UNSET_VALUE fails ON_Brep::IsValid() outright ("should be >=
    // 0.0"), which is otherwise invisible until something actually checks
    // it (found running SplitEdge on a naked edge for the first time).
    for (int i = 0; i < b.m_E.Count(); ++i) {
      ON_BrepEdge& fix_e = b.m_E[i];
      if (fix_e.m_edge_index >= 0 && !(fix_e.m_tolerance >= 0.0)) fix_e.m_tolerance = 0.0;
    }
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
  ctx.Doc().BeginChange("RebuildEdges");
  int refit = 0;
  int total_two_trim_edges = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) continue;
    const ON_Brep& raw = o->brep->raw();
    for (int ei = 0; ei < raw.m_E.Count(); ++ei) {
      if (raw.m_E[ei].m_edge_index >= 0 && raw.m_E[ei].TrimCount() == 2) ++total_two_trim_edges;
    }
    // kernel::Brep::RebuildAllEdgeCurves() is the same real-SSX edge rebuild
    // this command used to reimplement inline (midpoint-nearest curve pick,
    // ChangeEdgeCurve with no 2D-trim refit). Routing through the kernel API
    // instead gets the curve-vs-vertex matching that actually extracts the
    // sub-arc between THIS edge's own endpoints (not just "closest curve by
    // midpoint distance") and ReplaceEdgeCurve's trim re-projection, so the
    // 2D trims stay consistent with the new 3D curve instead of silently
    // drifting out of tolerance.
    refit += o->brep->RebuildAllEdgeCurves(tol);
    o->brep->raw().SetTolerancesBoxesAndFlags();
    o->InvalidateDisplay();
  }
  const int kept = total_two_trim_edges - refit;
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
// Vertex blend (Rhino's FilletEdge extended to a whole solid corner):
// PARITY_MAP.md's Blending & chamfering "Vertex blend" entry already
// documents genuinely exact kernel constructions for the m==3 trihedral
// corner - kernel::FilletConvexEdges/FilletConcaveEdges's own spherical-
// corner dispatch (see fillet.h's own doc comment on FilletConvexEdges for
// the full derivation: the ball center is the unique point equidistant
// from all three face planes, requiring one of the three faces to be
// perpendicular to the other two) - but neither function had ANY app
// caller before this (zero references anywhere in dino8-app/src). This
// command picks a single VERTEX (PickVertex above, the vertex-level
// sibling of PickFace/PickEdge) rather than an edge: at a genuine
// trihedral corner exactly 3 edges meet there, which is exactly the m==3
// case both kernel functions require, so no separate edge-chain selection
// UI (still absent from the app entirely - PARITY_MAP.md's own "tangent
// edge chains" entry) is needed for this one specific, common corner
// shape. Tries FilletConvexEdges first, then FilletConcaveEdges, the same
// convex-then-concave cascade every other command in this file uses,
// since the picked vertex's own convexity isn't known in advance.
class FilletVertexCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Radius", FormatNumber(radius_), {}, true, false}};
    WantPoint("Click a solid's vertex to fillet (rounds the 3 edges meeting there into one spherical corner)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Radius") radius_ = std::atof(v.c_str());
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d p) {
    std::optional<VertexPick> pick = PickVertex(ctx, p);
    if (!pick) { ctx.Warn("FilletVertex: no vertex near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) { ctx.Warn("FilletVertex: picked object has no B-rep"); return; }
    const ON_BrepVertex& v = b->m_V[pick->vertex];
    if (v.m_ei.Count() != 3) {
      ctx.Warn("FilletVertex: needs a vertex where exactly 3 edges meet (found " + std::to_string(v.m_ei.Count()) +
                "); higher-valence and non-trihedral corners have no exact spherical-blend construction yet");
      return;
    }
    std::vector<std::pair<Point3d, Point3d>> edges;
    for (int k = 0; k < 3; ++k) {
      const ON_BrepEdge& e = b->m_E[v.m_ei[k]];
      edges.emplace_back(e.PointAtStart(), e.PointAtEnd());
    }
    kernel::Brep kb;
    kb.raw() = *b;
    std::string convex_err, concave_err;
    ON_Brep result;
    const char* kind = nullptr;
    try {
      result = kernel::FilletConvexEdges(kb, edges, radius_).raw();
      kind = "convex";
    } catch (const std::exception& ex1) {
      convex_err = ex1.what();
      try {
        result = kernel::FilletConcaveEdges(kb, edges, radius_).raw();
        kind = "concave";
      } catch (const std::exception& ex2) {
        concave_err = ex2.what();
      }
    }
    if (!kind) {
      ctx.Warn("FilletVertex: not a supported trihedral corner (needs one of the 3 faces perpendicular to the other "
                "two; convex attempt: " + convex_err + "; concave attempt: " + concave_err + ")");
      return;
    }
    ctx.Doc().BeginChange("FilletVertex");
    if (SceneObject* orig = ctx.Doc().Find(pick->id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result;
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    ctx.Print("FilletVertex: " + std::string(kind) + " spherical corner at vertex " + std::to_string(pick->vertex) +
               " of object " + std::to_string(pick->id) + " filleted (radius " + FormatNumber(radius_) + ")");
  }

 private:
  double radius_ = 2;
};

// ---------------------------------------------------------------------------
// Vertex chamfer (the flat-facet sibling of FilletVertex above): PARITY_MAP.md's
// Blending & chamfering "Vertex blend" entry's own doc comment also names
// kernel::ChamferConvexVertex/ChamferConcaveVertex - the single-new-planar-
// facet cut across a trihedral corner, ChamferConvexEdge's genuine 3D
// analogue for a VERTEX rather than an edge - but, like FilletConvexEdges/
// FilletConcaveEdges before FilletVertexCommand above, neither had any app
// caller (RemoveFilletCommand already reaches their own inverse,
// RemoveChamferVertex, so only the forward construction was unreached).
// Unlike FilletVertex's own spherical corner (which needs one of the 3
// faces perpendicular to the other two, or the blend isn't a sphere), a
// plane always exists through any 3 non-collinear points, so this accepts
// ANY convex (or, mirrored, concave) trihedral corner - see
// ChamferConvexVertex's own doc comment. Same convex-then-concave cascade
// as FilletVertexCommand, for the identical reason (the picked vertex's own
// convexity isn't known in advance).
class ChamferVertexCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Distance", FormatNumber(distance_), {}, true, false}};
    WantPoint("Click a solid's vertex to chamfer (cuts the trihedral corner meeting there with one flat facet)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Distance") distance_ = std::atof(v.c_str());
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d p) {
    std::optional<VertexPick> pick = PickVertex(ctx, p);
    if (!pick) { ctx.Warn("ChamferVertex: no vertex near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) { ctx.Warn("ChamferVertex: picked object has no B-rep"); return; }
    const ON_BrepVertex& v = b->m_V[pick->vertex];
    if (v.m_ei.Count() != 3) {
      ctx.Warn("ChamferVertex: needs a vertex where exactly 3 edges meet (found " + std::to_string(v.m_ei.Count()) +
                "); higher-valence corners have no exact single-facet chamfer construction");
      return;
    }
    kernel::Brep kb;
    kb.raw() = *b;
    std::string convex_err, concave_err;
    ON_Brep result;
    const char* kind = nullptr;
    try {
      result = kernel::ChamferConvexVertex(kb, p, distance_).raw();
      kind = "convex";
    } catch (const std::exception& ex1) {
      convex_err = ex1.what();
      try {
        result = kernel::ChamferConcaveVertex(kb, p, distance_).raw();
        kind = "concave";
      } catch (const std::exception& ex2) {
        concave_err = ex2.what();
      }
    }
    if (!kind) {
      ctx.Warn("ChamferVertex: not a supported trihedral corner (convex attempt: " + convex_err +
                "; concave attempt: " + concave_err + ")");
      return;
    }
    ctx.Doc().BeginChange("ChamferVertex");
    if (SceneObject* orig = ctx.Doc().Find(pick->id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result;
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    ctx.Print("ChamferVertex: " + std::string(kind) + " corner at vertex " + std::to_string(pick->vertex) +
               " of object " + std::to_string(pick->id) + " chamfered (distance " + FormatNumber(distance_) + ")");
  }

 private:
  double distance_ = 2;
};

// ---------------------------------------------------------------------------
// Blend/chamfer removal (Rhino's RemoveFillet): PARITY_MAP.md's Blending &
// chamfering "Blend removal / defeaturing with healing" entry already
// documents genuinely exact kernel inverses - RemoveBlend (fillets, plain
// cylindrical/conical and the m==3 spherical vertex-blend corner),
// RemoveChamfer (two-distance/angle edge chamfers) and RemoveChamferVertex
// (single-facet vertex chamfers) - but "nothing in the app calls any of
// these" was the entry's own remaining app-layer gap. Each kernel function
// already does its own geometric identification purely from a point on the
// candidate face (RemoveBlend/RemoveChamfer search solid.MixedFaces()/
// PlanarFaces() for the closest match and geometrically verify it really is
// one of their own constructions before touching anything; RemoveChamferVertex
// likewise verifies the picked triangular facet's own 3-plane vertex
// reconstruction), so this command needs no separate face-type detection of
// its own: it just tries all three, in the same "narrowest construction
// first" order RemoveChamfer's own doc comment implies (a chamfer quad is a
// PlanarFace RemoveBlend's own MixedFaces() search never considers, and a
// vertex-chamfer facet is a strict subset of RemoveChamfer's own quad check
// that only accepts on a 4-point loop), joining every failure's own
// exception text into one diagnostic if all three decline.
class RemoveFilletCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    WantPoint("Click a fillet, chamfer, or vertex-chamfer face to remove (restores the sharp edge/vertex)");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d p) {
    std::optional<FacePick> pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("RemoveFillet: no face near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o) return;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) { ctx.Warn("RemoveFillet: picked object has no B-rep"); return; }
    kernel::Brep kb;
    kb.raw() = *b;
    std::string fillet_err, chamfer_err, vertex_err;
    ON_Brep result;
    const char* kind = nullptr;
    try {
      result = kernel::RemoveBlend(kb, p).raw();
      kind = "fillet";
    } catch (const std::exception& ex1) {
      fillet_err = ex1.what();
      try {
        result = kernel::RemoveChamfer(kb, p).raw();
        kind = "chamfer";
      } catch (const std::exception& ex2) {
        chamfer_err = ex2.what();
        try {
          result = kernel::RemoveChamferVertex(kb, p).raw();
          kind = "vertex chamfer";
        } catch (const std::exception& ex3) {
          vertex_err = ex3.what();
        }
      }
    }
    if (!kind) {
      ctx.Warn("RemoveFillet: the picked face is not a recognized fillet, chamfer, or vertex-chamfer facet (fillet attempt: " + fillet_err +
                "; chamfer attempt: " + chamfer_err + "; vertex-chamfer attempt: " + vertex_err + ")");
      return;
    }
    ctx.Doc().BeginChange("RemoveFillet");
    if (SceneObject* orig = ctx.Doc().Find(pick->id)) {
      orig->kind = ObjectKind::Brep;
      if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>();
      orig->brep->raw() = result;
      orig->surface.reset();
      orig->InvalidateDisplay();
    }
    ctx.Print(std::string("RemoveFillet: ") + kind + " on object " + std::to_string(pick->id) + " removed, sharp edge/vertex restored");
  }
};

// ---------------------------------------------------------------------------

// FilletEdge / ChamferEdge re-execution (history_rebuild.h): replays the recorded
// steps on the CURRENT source solid. Each recorded edge is re-found by its
// geometric signature on the solid as it stands at that step (so a chamfer
// recorded after an earlier one is searched for on the already-chamfered
// intermediate), then the same exact kernel calls the live command made are
// run: FilletConvexEdges/FilletConcaveEdges for a fillet step, ChamferConvexEdge/
// ChamferConcaveEdge for a chamfer. There is no approximate fallback: a
// refusal names the edge or the kernel's reason and the dependent is kept.
std::optional<SceneObject> RebuildEdgeBlend(CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) {
  if (in.size() != 1) { why = "expected 1 source object, the record has " + std::to_string(in.size()); return std::nullopt; }
  const SceneObject& src = *in[0];
  if (src.kind != ObjectKind::Brep || !src.brep) {
    why = "source object " + std::to_string(src.id) + " is " + HistoryKindName(src.kind) + ", not a polysurface solid";
    return std::nullopt;
  }
  const bool fillet = rec.command == "FilletEdge";
  auto num = [&](const std::string& k, double d = 0.0) { const auto it = rec.num.find(k); return it == rec.num.end() ? d : it->second; };
  const double r = num("r");
  const bool sym = num("sym", 1.0) != 0.0;
  const double d2 = sym ? r : num("d2", r);
  const int n = static_cast<int>(num("n")), groups = static_cast<int>(num("groups", 1));
  if (!(r > 0) || n < 1) { why = "the record holds no usable distance or edge list"; return std::nullopt; }
  ON_Brep cur = src.brep->raw();
  for (int g = 0; g < groups; ++g) {
    std::vector<std::pair<Point3d, Point3d>> es;
    for (int k = 0; k < n; ++k) {
      if (static_cast<int>(num(EdgeKey(k, "g"), 0.0)) != g) continue;
      EdgeSig want;
      if (!LoadSig(rec.num, k, want)) { why = "the record of edge " + std::to_string(k + 1) + " is incomplete"; return std::nullopt; }
      std::string reason;
      bool by_index = false;
      const int ei = FindEdgeBySignature(cur, want, reason, &by_index);
      if (ei < 0) { why = "edge " + std::to_string(k + 1) + " of " + std::to_string(n) + ": " + reason; return std::nullopt; }
      es.emplace_back(Point3d(cur.m_E[ei].PointAtStart()), Point3d(cur.m_E[ei].PointAtEnd()));
      ctx.Print(rec.command + ": edge " + std::to_string(k + 1) + " re-found as edge " + std::to_string(ei) + " of the source (" +
                (by_index ? "the solid was re-oriented: same position in its edge list, same dihedral angle and relative length"
                          : "direction and both face normals match") + ")");
    }
    if (es.empty()) { why = "step " + std::to_string(g + 1) + " has no edges"; return std::nullopt; }
    kernel::Brep kb;
    kb.raw() = cur;
    std::string convex_err, concave_err;
    kernel::Brep out;
    auto attempt = [&](bool convex, std::string& err) {
      try {
        if (fillet) {
          out = convex ? kernel::FilletConvexEdges(kb, es, r) : kernel::FilletConcaveEdges(kb, es, r);
        } else {
          kernel::Brep b = kb;
          for (const auto& e : es) b = convex ? kernel::ChamferConvexEdge(b, e.first, e.second, r, d2) : kernel::ChamferConcaveEdge(b, e.first, e.second, r, d2);
          out = b;
        }
        return true;
      } catch (const std::exception& ex) {
        err = ex.what();
        return false;
      }
    };
    const bool ok = attempt(true, convex_err) || attempt(false, concave_err);
    if (!ok) {
      why = std::string("the exact kernel ") + (fillet ? "fillet" : "chamfer") + " refused the edited source (convex attempt: " + convex_err + "; concave attempt: " + concave_err + ")";
      return std::nullopt;
    }
    cur = out.raw();
  }
  kernel::Brep result;
  result.raw() = cur;
  return SceneObject::MakeBrep(result);
}

// BlendSrf re-execution (history_rebuild.h): the same boundary edges (iso direction,
// min/max, face index) of the sources' CURRENT surfaces, the same continuity.
std::optional<SceneObject> RebuildBlendSrf(CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) {
  if (in.size() != 2) { why = "expected 2 source objects, the record has " + std::to_string(in.size()); return std::nullopt; }
  auto num = [&](const char* k) { const auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  std::optional<ON_NurbsSurface> sa, sb;
  for (int k = 0; k < 2; ++k) {
    const SceneObject& o = *in[k];
    const int face = static_cast<int>(num(k == 0 ? "face_a" : "face_b"));
    if (o.kind != ObjectKind::Surface && o.kind != ObjectKind::Brep) {
      why = "source object " + std::to_string(o.id) + " is " + HistoryKindName(o.kind) + ", not a surface or polysurface";
      return std::nullopt;
    }
    std::optional<ON_NurbsSurface> s = SurfaceOfObject(o, face);
    if (!s) { why = "source object " + std::to_string(o.id) + " has no face " + std::to_string(face) + " any more"; return std::nullopt; }
    (k == 0 ? sa : sb) = s;
  }
  ON_NurbsSurface built;
  bool used_exact = false;
  if (!BuildBlendBetween(ctx, *sa, static_cast<int>(num("dir_a")), num("max_a") != 0.0, *sb, static_cast<int>(num("dir_b")), num("max_b") != 0.0,
                         static_cast<int>(num("cont")), built, used_exact)) {
    why = "the blend could not be built between the sources' current boundary edges";
    return std::nullopt;
  }
  kernel::NurbsSurface k;
  k.raw() = built;
  return SceneObject::MakeSurface(k);
}

// FilletSrf / ChamferSrf / VariableFilletSrf / VariableChamferSrf, general untrimmed case (Trim=No): sources {A, B}; num: face_a, face_b
// (polysurface face indices), r0, r1 (radius at the start/end of the spine; equal unless variable), variable (1/0). Re-runs the
// same BuildSurfaceFilletBetween the live command builds with, so the result follows either source. Chamfer or fillet is the
// record's command name. Refuses with the reason when a source has no such face any more or no fillet can be built.
std::optional<SceneObject> RebuildSurfaceFillet(CommandContext& ctx, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) {
  if (in.size() != 2) { why = "expected 2 source objects, the record has " + std::to_string(in.size()); return std::nullopt; }
  auto num = [&](const char* k) { const auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  std::optional<ON_NurbsSurface> sa, sb;
  for (int k = 0; k < 2; ++k) {
    const SceneObject& o = *in[k];
    const int face = static_cast<int>(num(k == 0 ? "face_a" : "face_b"));
    if (o.kind != ObjectKind::Surface && o.kind != ObjectKind::Brep) {
      why = "source object " + std::to_string(o.id) + " is " + HistoryKindName(o.kind) + ", not a surface or polysurface";
      return std::nullopt;
    }
    std::optional<ON_NurbsSurface> s = SurfaceOfObject(o, face);
    if (!s) { why = "source object " + std::to_string(o.id) + " has no face " + std::to_string(face) + " any more"; return std::nullopt; }
    (k == 0 ? sa : sb) = s;
  }
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-5);
  FilletBuild fb2;
  ON_NurbsSurface built;
  if (!BuildSurfaceFilletBetween(*sa, *sb, num("r0"), num("r1"), num("variable") != 0.0, rec.command == "ChamferSrf", tol, fb2, built)) {
    why = "the fillet could not be built between the sources' current shapes: " + fb2.error;
    return std::nullopt;
  }
  kernel::NurbsSurface k;
  k.raw() = built;
  return SceneObject::MakeSurface(k);
}

void RegisterFilletCommands(CommandEngine& e) {
  Reg(e, "Intersect", OnSelection("Select objects to intersect", IntersectAny, 1), CommandStatus::Implemented,
      "Curve/curve and closed-solid pairs as before; surface/surface, brep/brep, curve/surface and brep/curve now use a real mesh-seeded, Newton-refined intersector (SSX/CSX).");
  Reg(e, "FilletSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::Fillet), CommandStatus::Implemented,
      "Rolling-ball fillet via offset+SSX+exact contact arcs; real planar trim when both inputs are planar, otherwise left untrimmed. With History On and Trim=No (the sources stay untouched) the result is recorded {surface A, surface B} + faces + radii and rebuilt when either surface is edited (EditHistory radius=/endradius=); Trim=Yes cuts the sources and is not recorded (the reason is printed).");
  Reg(e, "ChamferSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::Chamfer), CommandStatus::Implemented,
      "Exact kernel::ChamferConvexEdge/ChamferConcaveEdge flat-bevel B-rep when both picks land on the same solid's two adjacent planar faces (Trim=Yes); otherwise a ruled surface between the two exact contact curves found the same way as FilletSrf.");
  Reg(e, "VariableFilletSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::VariableFillet), CommandStatus::Implemented,
      "Radius interpolated linearly along the spine from Radius= to EndRadius= (spine itself uses the average radius, an approximation for strongly varying radii).");
  Reg(e, "VariableChamferSrf", Make<FilletTwoSurfacesCommand>(FilletTwoSurfacesCommand::Mode::VariableChamfer), CommandStatus::Implemented,
      "Same approximation as VariableFilletSrf, ruled instead of arced.");
  Reg(e, "FilletEdge", Make<FilletEdgeCommand>(FilletEdgeCommand::Mode::Fillet), CommandStatus::Implemented,
      "Exact rolling-ball fillet with real B-rep trimming when both adjacent faces are planar; mesh fallback (reported) otherwise.");
  Reg(e, "ChamferEdge", Make<FilletEdgeCommand>(FilletEdgeCommand::Mode::Chamfer), CommandStatus::Implemented,
      "Same trimming strategy as FilletEdge, a ruled chamfer instead of an arc.");
  Reg(e, "BlendEdge", Make<FilletEdgeCommand>(FilletEdgeCommand::Mode::Blend), CommandStatus::Implemented,
      "Hermite blend surface trimmed and stitched into the polysurface when both adjacent faces are planar; otherwise added between the two faces as a separate, untouched surface (not stitched in). Continuity=Tangency is a degree-3x3 G1 blend. Continuity=Curvature is a degree-5x3 blend whose cross-boundary second derivative is set to each face's own exact analytic directional second derivative (Ev2Der, not a finite difference), so its curvature vector matches each face's curvature exactly in that direction (numerically verified in tests/test_g2_blend.cpp for both flat and curved faces) - real G2 in the cross-boundary direction, not full surface-wide G2 in every direction, and it silently falls back to the plain G1 tangent at any sample where a face's own parametrization is singular there.");
  Reg(e, "VariableBlendSrf", Make<VariableBlendSrfCommand>(), CommandStatus::Implemented,
      "A genuine independent blend-tangent variant of BlendSrf (not the rolling-ball fillet VariableFilletSrf uses): the same BuildBlendSurfaceG1/G2 Hermite/quintic blend between two picked surface edges, with the blend's own cross-section width interpolated linearly along the rail from Width= to EndWidth=, the same piecewise-linear-along-the-rail technique FilletTwoSurfacesCommand uses for Radius=/EndRadius=. Continuity=Curvature gets the same real, numerically-verified G2 cross-boundary curvature match as BlendSrf (see tests/test_g2_blend.cpp and tests/test_variable_blend.cpp), independent of the width, which no fixed-cross-section rolling-ball fillet construction can do except at one coincidental radius.");
  Reg(e, "MatchSrf", Make<MatchSrfCommand>(), CommandStatus::Implemented,
      "Moves the picked surface's boundary control row onto the target (Position); Tangency also aligns the next row's step to the target's tangent/normal. Against a target surface edge, Position/Tangency/Curvature all try the exact kernel::NurbsSurface::MatchEdge() construction first (real NURBS algebra, not moved-and-hoped-for control points) - Curvature additionally matches the cross-boundary second derivative, real G2, not just G1 (see kernel::NurbsSurface::MatchEdge()'s own doc comment). Curvature has no approximate fallback against a target curve or when the exact match itself fails (a periodic edge, or a target whose degree is too low to carry a curvature row) - it fails closed with a diagnostic rather than silently downgrading to G1/G0.");
  Reg(e, "BlendSrf", Make<BlendSrfCommand>(), CommandStatus::Implemented,
      "Hermite blend between two picked surface edges. Continuity=Tangency is a degree-3x3 G1 blend. Continuity=Curvature is a degree-5x3 blend matching each surface's own exact directional second derivative at the boundary (Ev2Der), so its curvature vector matches exactly in the cross-boundary direction (see BlendEdge's own help and tests/test_g2_blend.cpp) - not a claim of full surface-wide G2.");
  Reg(e, "ConnectSrf", Make<ConnectSrfCommand>(), CommandStatus::Implemented,
      "Extends both surfaces, adds their real SSX join curve, and trims each extended surface to it: for two planes that is immediate (the "
      "extended planes already meet along their whole shared line); otherwise each surface is really trimmed by pulling the join curve back "
      "onto its own (u,v) domain and closing it into a loop against that domain's own rectangular boundary (real Brep construction, shared "
      "with the IGES/STEP importer's own trimmed-face builder - see geom/BrepTrimFace.h), keeping the side that holds the surface's own "
      "original footprint. Handles any pair of non-planar surfaces whose join curve crosses at least one of the two surfaces' own domain "
      "edges (the ordinary case - e.g. two extended cylinders or a cylinder and a freeform surface meeting along a simple curve); a join "
      "curve that stays a closed loop fully inside one surface's own domain (e.g. one surface fully piercing the other) is left untrimmed "
      "with an honest note, since a single open trim loop has nowhere to close onto that surface's own boundary.");
  Reg(e, "SplitFace", Make<SplitFaceCommand>(), CommandStatus::Implemented,
      "Splits the picked face's surface at a real CSX crossing of a picked curve (untrimmed-domain split, not an arbitrary trim loop).");
  Reg(e, "SplitEdge", Make<SplitEdgeCommand>(), CommandStatus::Implemented, "Inserts a real vertex/edge split at the picked parameter (rewires every trim onto the matching half) - works on a naked (1-trim) edge exactly as well as a shared (2-trim) one, unlike the fillet-family commands' own edge picker.");
  Reg(e, "MergeEdge", Make<MergeEdgeCommand>(false), CommandStatus::Implemented, "ON_Brep::CombineContiguousEdges on the two picked edges (needs them tangent within 5 degrees).");
  Reg(e, "MergeAllEdges", Make<MergeEdgeCommand>(true), CommandStatus::Implemented, "Combines every colinear/tangent pair of edges sharing a vertex, repeatedly.");
  Reg(e, "MergeFaces", Make<MergeCoplanarCommand>(false), CommandStatus::Implemented, "Unions the picked coplanar faces' outlines (mesh boolean of thin slabs) into one real trimmed-plane face.");
  Reg(e, "MergeAllCoplanarFaces", Make<MergeCoplanarCommand>(true), CommandStatus::Implemented, "Finds and merges every coplanar face group on the picked polysurface.");
  Reg(e, "MergeCoplanarFace", Make<MergeCoplanarCommand>(false), CommandStatus::Implemented, "Same as MergeFaces (pick 2+ coplanar faces, Enter).");
  Reg(e, "RebuildEdges", OnSelection("Select polysurfaces", RebuildEdgesReal), CommandStatus::Implemented,
      "Refits each shared edge's 3D curve through the real SSX of its two adjacent faces.");
  Reg(e, "RemoveFillet", Make<RemoveFilletCommand>(), CommandStatus::Implemented,
      "Exact kernel::RemoveBlend/RemoveChamfer/RemoveChamferVertex inverse of FilletEdge/ChamferEdge/vertex-chamfer, tried in that order from a single picked face - restores the sharp edge or vertex purely from the solid's own geometry, no separate provenance needed.");
  Reg(e, "FilletVertex", Make<FilletVertexCommand>(), CommandStatus::Implemented,
      "Exact kernel::FilletConvexEdges/FilletConcaveEdges spherical-corner blend of the 3 edges meeting at a picked trihedral vertex (needs one of the 3 faces perpendicular to the other two, e.g. any box corner) - tried convex then concave.");
  Reg(e, "ChamferVertex", Make<ChamferVertexCommand>(), CommandStatus::Implemented,
      "Exact kernel::ChamferConvexVertex/ChamferConcaveVertex single-facet cut across the 3 edges meeting at a picked trihedral vertex (any convex or concave corner, no perpendicular-face restriction) - tried convex then concave. RemoveFillet already reaches this construction's own inverse (RemoveChamferVertex).");
}

}  // namespace dino8::app
