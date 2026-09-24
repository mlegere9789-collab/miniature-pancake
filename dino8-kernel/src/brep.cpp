#include "dino8/kernel/brep.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dino8/kernel/detail/arc_schedule3d.h"
#include "dino8/kernel/detail/ellipse_clip3d.h"
#include "dino8/kernel/detail/polygon2d.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/tolerance.h"

namespace dino8::kernel {

namespace {

// Resolved geometry for one face: its NURBS form plus, for a face whose
// trim loops come from genuine ON_Brep topology (a box from ON_BrepBox, a
// file loaded from .3dm, a boolean result), the outer loop and hole loops
// sampled into (u, v) polygons. Breps built by this class's own
// constructors carry their trim polygons in the side tables instead.
struct FaceGeometry {
  ON_NurbsSurface surface;
  std::vector<Point2d> outer;              // empty => untrimmed
  std::vector<std::vector<Point2d>> holes;
  bool exact_clip = false;
};

// Samples a loop's 2D trim curves into a closed (u, v) polygon.
std::vector<Point2d> SampleLoop(const ON_Brep& brep, const ON_BrepLoop& loop) {
  std::vector<Point2d> poly;
  for (int k = 0; k < loop.m_ti.Count(); ++k) {
    const int ti = loop.m_ti[k];
    if (ti < 0 || ti >= brep.m_T.Count()) continue;
    const ON_BrepTrim& trim = brep.m_T[ti];
    const ON_Curve* c2 = trim.TrimCurveOf();
    if (!c2) continue;
    const ON_Interval d = trim.Domain();
    int samples = 1;
    if (!c2->IsLinear()) {
      // Curved trims (circle seams, fillets): sample by span count.
      samples = std::max(8, 4 * c2->SpanCount());
    }
    for (int i = 0; i < samples; ++i) {
      const ON_3dPoint p = c2->PointAt(d.ParameterAt(static_cast<double>(i) / samples));
      poly.emplace_back(p.x, p.y);
    }
  }
  // Drop duplicate closing vertex if the sampling produced one.
  if (poly.size() > 1) {
    const Point2d& a = poly.front();
    const Point2d& b = poly.back();
    if (std::fabs(a.x - b.x) < 1e-12 && std::fabs(a.y - b.y) < 1e-12) poly.pop_back();
  }
  return poly;
}

// True when the loop is just the surface's full rectangular domain.
bool LoopIsFullDomain(const std::vector<Point2d>& poly, const ON_NurbsSurface& srf) {
  if (poly.size() != 4) return false;
  const ON_Interval du = srf.Domain(0), dv = srf.Domain(1);
  double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
  for (const Point2d& p : poly) {
    umin = std::min(umin, p.x); umax = std::max(umax, p.x);
    vmin = std::min(vmin, p.y); vmax = std::max(vmax, p.y);
    // Every vertex must sit on a domain corner.
    const bool on_u = std::fabs(p.x - du.Min()) < 1e-9 * (1 + std::fabs(du.Min())) || std::fabs(p.x - du.Max()) < 1e-9 * (1 + std::fabs(du.Max()));
    const bool on_v = std::fabs(p.y - dv.Min()) < 1e-9 * (1 + std::fabs(dv.Min())) || std::fabs(p.y - dv.Max()) < 1e-9 * (1 + std::fabs(dv.Max()));
    if (!on_u || !on_v) return false;
  }
  return std::fabs(umin - du.Min()) < 1e-9 && std::fabs(umax - du.Max()) < 1e-9 &&
         std::fabs(vmin - dv.Min()) < 1e-9 && std::fabs(vmax - dv.Max()) < 1e-9;
}

bool ResolveFace(const ON_Brep& brep, int face_index,
                 const std::vector<std::vector<Point2d>>& side_trims,
                 const std::vector<bool>& side_exact,
                 const std::vector<std::vector<std::vector<Point2d>>>& side_holes,
                 FaceGeometry& out) {
  const ON_BrepFace& face = brep.m_F[face_index];
  const ON_Surface* face_surface = face.SurfaceOf();
  if (!face_surface) return false;
  if (const auto* ns = ON_NurbsSurface::Cast(face_surface)) {
    out.surface = *ns;
  } else if (face_surface->GetNurbForm(out.surface) <= 0) {
    return false;
  }
  // Side tables win when this Brep built the face itself.
  const size_t fi = static_cast<size_t>(face_index);
  if (fi < side_trims.size()) {
    out.outer = side_trims[fi];
    out.exact_clip = fi < side_exact.size() ? side_exact[fi] : false;
    if (fi < side_holes.size()) out.holes = side_holes[fi];
    return true;
  }
  // Otherwise derive trims from the brep's own loops.
  for (int li = 0; li < face.m_li.Count(); ++li) {
    const int loop_index = face.m_li[li];
    if (loop_index < 0 || loop_index >= brep.m_L.Count()) continue;
    const ON_BrepLoop& loop = brep.m_L[loop_index];
    std::vector<Point2d> poly = SampleLoop(brep, loop);
    if (poly.size() < 3) continue;
    if (loop.m_type == ON_BrepLoop::outer) {
      if (!LoopIsFullDomain(poly, out.surface)) out.outer = std::move(poly);
    } else if (loop.m_type == ON_BrepLoop::inner) {
      out.holes.push_back(std::move(poly));
    }
  }
  if (!out.holes.empty() && out.outer.empty()) {
    // Holes in an otherwise-untrimmed face: use the full domain as outer.
    const ON_Interval du = out.surface.Domain(0), dv = out.surface.Domain(1);
    out.outer = {Point2d(du.Min(), dv.Min()), Point2d(du.Max(), dv.Min()), Point2d(du.Max(), dv.Max()), Point2d(du.Min(), dv.Max())};
  }
  out.exact_clip = !out.outer.empty() && out.holes.empty();
  return true;
}

}  // namespace

Brep Brep::FromSurface(const NurbsSurface& surface) {
  Brep result;
  ON_Brep& brep = result.brep_;

  auto* surface_copy = new ON_NurbsSurface(surface.raw());
  const int surface_index = brep.AddSurface(surface_copy);

  ON_BrepFace& face = brep.NewFace(surface_index);
  (void)face;
  result.face_trim_loops_.emplace_back();  // untrimmed
  result.face_exact_clip_.push_back(false);
  result.face_hole_loops_.emplace_back();
  result.face_arc_runs_.emplace_back();
  result.face_notch_rows_.emplace_back();
  result.face_records_.emplace_back();

  brep.SetTrimIsoFlags();

  return result;
}

Brep Brep::Box(double x0, double y0, double z0, double x1, double y1,
               double z1) {
  Brep result;
  ON_Brep& brep = result.brep_;

  const Point3d v0(x0, y0, z0);
  const Point3d v1(x1, y0, z0);
  const Point3d v2(x1, y1, z0);
  const Point3d v3(x0, y1, z0);
  const Point3d v4(x0, y0, z1);
  const Point3d v5(x1, y0, z1);
  const Point3d v6(x1, y1, z1);
  const Point3d v7(x0, y1, z1);

  // Each grid is [P(u=0,v=0), P(u=0,v=1), P(u=1,v=0), P(u=1,v=1)] -
  // NurbsSurface::FromControlGrid indexes a u_count=v_count=2 grid as
  // u*v_count+v, so this is the order that produces exactly those four
  // corners. Per-face corner order is chosen so u_dir x v_dir (the
  // tessellator's triangle-winding normal - see NurbsSurface's own
  // TessellateGrid comment) points outward for that face.
  const std::vector<std::vector<Point3d>> face_grids = {
      {v0, v1, v3, v2},  // bottom (-z)
      {v4, v7, v5, v6},  // top (+z)
      {v0, v4, v1, v5},  // front (-y)
      {v3, v2, v7, v6},  // back (+y)
      {v0, v3, v4, v7},  // left (-x)
      {v1, v5, v2, v6},  // right (+x)
  };

  for (const auto& grid : face_grids) {
    const NurbsSurface surface =
        NurbsSurface::FromControlGrid(grid, /*u_count=*/2, /*v_count=*/2,
                                       /*u_degree=*/1, /*v_degree=*/1);
    auto* surface_copy = new ON_NurbsSurface(surface.raw());
    const int surface_index = brep.AddSurface(surface_copy);
    brep.NewFace(surface_index);
    result.face_trim_loops_.emplace_back();  // untrimmed
    result.face_exact_clip_.push_back(false);
    result.face_hole_loops_.emplace_back();
    result.face_arc_runs_.emplace_back();
    result.face_notch_rows_.emplace_back();
    result.face_records_.emplace_back();
  }

  brep.SetTrimIsoFlags();
  return result;
}

Brep Brep::Sphere(Point3d center, double radius) {
  Brep result;
  ON_Brep& brep = result.brep_;

  ON_Sphere sphere(center, radius);
  auto* surface = new ON_NurbsSurface();
  const int rc = sphere.GetNurbForm(*surface);
  if (rc == 0) {
    delete surface;
    throw std::runtime_error(
        "dino8::kernel::Brep::Sphere: ON_Sphere::GetNurbForm failed");
  }

  const int surface_index = brep.AddSurface(surface);
  brep.NewFace(surface_index);
  result.face_trim_loops_.emplace_back();  // untrimmed
  result.face_exact_clip_.push_back(false);
  result.face_hole_loops_.emplace_back();
  result.face_arc_runs_.emplace_back();
  result.face_notch_rows_.emplace_back();
  result.face_records_.emplace_back();

  brep.SetTrimIsoFlags();
  return result;
}

Brep Brep::TrimmedPlanarFace(const NurbsSurface& surface,
                              const std::vector<Point2d>& trim_loop_uv,
                              bool exact_clip,
                              std::vector<std::vector<Point2d>> hole_loops_uv) {
  if (trim_loop_uv.size() < 3) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::TrimmedPlanarFace: trim_loop_uv must have at "
        "least 3 points (fewer isn't a closed polygon at all - and, before "
        "this check, an empty trim_loop_uv silently meant \"no trim at "
        "all\" to Tessellate(), a genuine footgun this closes)");
  }
  if (exact_clip && !hole_loops_uv.empty()) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::TrimmedPlanarFace: hole_loops_uv is only "
        "supported with exact_clip=false (whole-cell tessellation)");
  }

  Brep result;
  ON_Brep& brep = result.brep_;

  auto* surface_copy = new ON_NurbsSurface(surface.raw());
  const int surface_index = brep.AddSurface(surface_copy);
  brep.NewFace(surface_index);
  result.face_trim_loops_.push_back(trim_loop_uv);
  result.face_exact_clip_.push_back(exact_clip);
  result.face_hole_loops_.push_back(std::move(hole_loops_uv));
  result.face_arc_runs_.emplace_back();
  result.face_notch_rows_.emplace_back();
  result.face_records_.emplace_back();

  brep.SetTrimIsoFlags();
  return result;
}

int Brep::FaceCount() const { return brep_.m_F.Count(); }

namespace {

// Newell's method: robust to a slightly non-planar or noisy polygon
// (unlike a two-edge cross product), and its sign follows the loop's own
// winding directly - the polygon and the plane it returns are always
// mutually consistent, which is exactly what a half-space boolean needs.
ON_3dVector NewellNormal(const std::vector<Point3d>& loop) {
  ON_3dVector n(0, 0, 0);
  const size_t k = loop.size();
  for (size_t i = 0; i < k; ++i) {
    const Point3d& p = loop[i];
    const Point3d& q = loop[(i + 1) % k];
    n.x += (p.y - q.y) * (p.z + q.z);
    n.y += (p.z - q.z) * (p.x + q.x);
    n.z += (p.x - q.x) * (p.y + q.y);
  }
  n.Unitize();
  return n;
}

// The trim polygon in (u, v) space to walk for `fg`, in increasing-
// parameter order when `fg` carries no explicit outer loop of its own -
// shared by PlanarFaces() and MixedFaces() (below) so "what UV rectangle
// does an untrimmed face fall back to" is answered in exactly one place.
std::vector<Point2d> FaceOuterUv(const FaceGeometry& fg) {
  if (!fg.outer.empty()) return fg.outer;
  const ON_Interval du = fg.surface.Domain(0), dv = fg.surface.Domain(1);
  // Increasing-parameter order around the rectangle - matches every
  // planar-face factory's own "u_dir x v_dir points outward" winding.
  return {Point2d(du[0], dv[0]), Point2d(du[1], dv[0]), Point2d(du[1], dv[1]), Point2d(du[0], dv[1])};
}

// Builds a PlanarFace from already-resolved face geometry known to be
// planar - the one conversion PlanarFaces() and MixedFaces() (below)
// share verbatim, so a planar face's own extraction never has two
// independently-maintained copies that could quietly drift apart.
Brep::PlanarFace ExtractPlanarFace(const FaceGeometry& fg) {
  const std::vector<Point2d> uv = FaceOuterUv(fg);
  Brep::PlanarFace face;
  face.loop.reserve(uv.size());
  for (const Point2d& p : uv) face.loop.push_back(fg.surface.PointAt(p.x, p.y));
  const ON_3dVector n = NewellNormal(face.loop);
  face.plane = ON_Plane(face.loop[0], n);
  return face;
}

// Recovers a ConicalFace from a face already known to be non-planar and
// non-cylindrical - the direct sibling of MixedFaces()'s own inline
// cylinder-recovery code (see that method's own doc comment for the
// worked description this mirrors), split into its own function purely
// because a cone's apex/two-radius recovery has enough of its own steps
// to be worth reading on its own. Throws std::invalid_argument if the
// face is neither cylindrical nor conical (mirrors PlanarFaces()' own
// honest narrowing for a non-planar face) or std::runtime_error if the
// recovered geometry is internally inconsistent (should not happen for a
// face this kernel itself built).
void ExtractConicalFace(const ON_Brep& brep, int face_index, const FaceGeometry& fg,
                         Brep::MixedFacesResult& result) {
  ON_Cone cone;
  // Same tolerance scale as MixedFaces()'s own IsCylinder() check just
  // above - loose enough for NURBS-fit noise, tight enough not to
  // misclassify a genuinely free-form face.
  const double cone_tol = 1e-4;
  if (!fg.surface.IsCone(&cone, cone_tol)) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        " is neither planar, cylindrical, nor conical - a genuinely free-form "
        "face is out of scope here, the same honest narrowing PlanarFaces() "
        "uses for a non-planar face (see this method's own doc comment)");
  }

  const std::vector<Point2d> uv = FaceOuterUv(fg);
  double u_min = uv[0].x, u_max = uv[0].x;
  for (const Point2d& p : uv) {
    u_min = std::min(u_min, p.x);
    u_max = std::max(u_max, p.x);
  }

  // v_min/v_max: NOT a plain global min/max of every trim-polygon point's
  // v (that would also sweep in a notched cap's own dense splice, see
  // ConicalFace::cap0_notch_points/cap1_notch_points' own doc comment -
  // every interior notch sample sits at a true height-from-apex strictly
  // different from v0/v1, so a global scan silently picks up the notch's
  // own dip instead of the true rail-corner v). Restricted instead to only
  // the points that sit exactly at u_min or u_max: FromMixedFaces() never
  // subdivides the two straight rail segments (u == 0 and u == u_max in
  // its own trim rectangle - see that method's own cap-splice comment),
  // so those two points are always, and ONLY, the genuine rail corners
  // (u_min, v0)/(u_min, v1) and (u_max, v0)/(u_max, v1) - whether or not
  // either cap is notched. A notch's own interior samples always have u
  // strictly between u_min and u_max, by construction of
  // EllipseNotchCornerAtVertex's monotone-in-phi sampling (fillet.cpp) -
  // so they can never masquerade as a rail-corner point here. Works
  // identically whether `uv` came from the side-table fast path or from
  // genuinely sampling a resolved ON_Brep's own loop (ResolveFace above),
  // since both paths preserve the same rail-vs-cap trim structure.
  const double u_tol = 1e-9 * std::max(1.0, std::max(std::fabs(u_min), std::fabs(u_max)));
  double v_min = std::numeric_limits<double>::infinity();
  double v_max = -std::numeric_limits<double>::infinity();
  for (const Point2d& p : uv) {
    if (std::fabs(p.x - u_min) <= u_tol || std::fabs(p.x - u_max) <= u_tol) {
      v_min = std::min(v_min, p.y);
      v_max = std::max(v_max, p.y);
    }
  }
  if (!(v_min < v_max)) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        "'s trim polygon has no two distinct points at its own u_min/u_max - "
        "cannot recover the true rail-corner v-range");
  }

  // The actual 3D points at the trim rectangle's own (u_min, v_min) and
  // (u_min, v_max) corners, evaluated on the REAL surface - exactly the
  // same "don't trust the fitted primitive's own arbitrary reference
  // direction" principle MixedFaces()'s own cylinder recovery uses.
  const Point3d p_near = fg.surface.PointAt(u_min, v_min);
  const Point3d p_far = fg.surface.PointAt(u_min, v_max);

  Vector3d axis_dir = cone.Axis();
  if (!axis_dir.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        ": ON_Surface::IsCone returned a degenerate (zero-length) axis");
  }
  const Point3d apex = cone.ApexPoint();

  // Orient zaxis so v increases in the +zaxis direction, matching
  // FromMixedFaces' own "v == true axial height from the apex" convention
  // - found from the REAL surface, not assumed from ON_Cone::Axis()'s own
  // arbitrary sign (the same defensive check MixedFaces()'s own cylinder
  // path applies to ON_Cylinder::Axis()).
  Vector3d zaxis = axis_dir;
  double h_near = ON_DotProduct(p_near - apex, zaxis);
  double h_far = ON_DotProduct(p_far - apex, zaxis);
  if (h_far < h_near) {
    zaxis = -zaxis;
    h_near = -h_near;
    h_far = -h_far;
  }

  const Point3d proj_near = apex + h_near * zaxis;
  const Point3d proj_far = apex + h_far * zaxis;
  const double radius0 = p_near.DistanceTo(proj_near);
  const double radius1 = p_far.DistanceTo(proj_far);

  Vector3d xaxis = p_near - proj_near;
  if (!xaxis.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        "'s trim corner sits exactly on the fitted cone's own axis - cannot "
        "recover a consistent reference frame");
  }

  // Self-consistency check: both ends of a genuine right circular cone
  // satisfy radius / height-from-apex = the SAME constant (tan of the
  // cone's own half-angle) - cross-multiplied to avoid instability if
  // either height happens to be near zero.
  const double lhs = radius1 * h_near;
  const double rhs = radius0 * h_far;
  const double scale = std::max({std::fabs(lhs), std::fabs(rhs), 1e-9});
  if (std::fabs(lhs - rhs) > scale * 1e-6) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        "'s two trim corners are not consistent with a single right circular "
        "cone (radius/height-from-apex ratio disagrees between them)");
  }

  Brep::ConicalFace cf;
  cf.frame.origin = apex;
  cf.frame.xaxis = xaxis;
  cf.frame.zaxis = zaxis;
  cf.frame.yaxis = ON_CrossProduct(zaxis, xaxis);
  cf.frame.yaxis.Unitize();
  cf.frame.UpdateEquation();
  cf.radius0 = radius0;
  cf.radius1 = radius1;
  cf.length = h_far - h_near;
  cf.outward = !brep.m_F[face_index].m_bRev;

  // True radian sweep between the trim's own u_min and u_max, via the
  // SAME ON_Circle::GetRadianFromNurbFormParameter conversion the
  // cylinder path above uses - called on cone.CircleAt(cone.height), the
  // literal circle ON_Cone::GetNurbForm's own u-knots are copied from
  // (verified directly against opennurbs_cone.cpp - see this class'
  // MixedFacesResult doc comment), so this is exact, not an
  // approximation carried over from the cylinder case.
  const ON_Circle u_ref_circle = cone.CircleAt(cone.height);
  double r_min = 0.0, r_max = 0.0;
  if (!u_ref_circle.GetRadianFromNurbFormParameter(u_min, &r_min) ||
      !u_ref_circle.GetRadianFromNurbFormParameter(u_max, &r_max)) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        ": ON_Circle::GetRadianFromNurbFormParameter failed converting the "
        "trim's own u-domain to true angle");
  }
  cf.angle = r_max - r_min;

  result.conical.push_back(cf);
}

// Recovers a CylindricalFace from a face already confirmed cylindrical
// (`cyl` is that face's own already-fitted ON_Cylinder) - the direct
// sibling of ExtractConicalFace() above, factored out of MixedFaces()'s
// own inline cylinder-recovery code (see that method's own doc comment
// for the worked description this mirrors) purely so
// Brep::TessellateConforming() can recover the SAME CylindricalFace
// geometry for a given face index without a second, independently-
// maintained copy of this recovery logic - not a behavior change to
// MixedFaces() itself (this is a pure code-motion refactor: every line
// below is unchanged from MixedFaces()'s own prior inline version).
Brep::CylindricalFace ExtractCylindricalFace(const ON_Brep& brep, int face_index, const FaceGeometry& fg,
                                              const ON_Cylinder& cyl) {
  const std::vector<Point2d> uv = FaceOuterUv(fg);
  double u_min = uv[0].x, u_max = uv[0].x, v_min = uv[0].y, v_max = uv[0].y;
  for (const Point2d& p : uv) {
    u_min = std::min(u_min, p.x);
    u_max = std::max(u_max, p.x);
    v_min = std::min(v_min, p.y);
    v_max = std::max(v_max, p.y);
  }

  const Point3d p_corner = fg.surface.PointAt(u_min, v_min);
  const Point3d p_far = fg.surface.PointAt(u_min, v_max);

  Vector3d axis_dir = cyl.Axis();
  if (!axis_dir.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        ": ON_Surface::IsCylinder returned a degenerate (zero-length) axis");
  }
  const Point3d& axis_ref = cyl.Center();
  const Point3d frame_origin = axis_ref + ON_DotProduct(p_corner - axis_ref, axis_dir) * axis_dir;

  Vector3d xaxis = p_corner - frame_origin;
  const double radius = cyl.circle.Radius();
  const double radius_tol = std::max(1e-9, radius * 1e-6);
  if (std::fabs(xaxis.Length() - radius) > radius_tol || !xaxis.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        "'s trim corner does not lie at the fitted cylinder's own radius "
        "from its axis - cannot recover a consistent reference frame");
  }

  Vector3d zaxis = axis_dir;
  if (ON_DotProduct(p_far - frame_origin, zaxis) < 0.0) zaxis = -zaxis;

  Brep::CylindricalFace cf;
  cf.frame.origin = frame_origin;
  cf.frame.xaxis = xaxis;
  cf.frame.zaxis = zaxis;
  cf.frame.yaxis = ON_CrossProduct(zaxis, xaxis);
  cf.frame.yaxis.Unitize();
  cf.frame.UpdateEquation();
  cf.radius = radius;
  cf.length = v_max - v_min;
  cf.outward = !brep.m_F[face_index].m_bRev;

  double r_min = 0.0, r_max = 0.0;
  if (!cyl.circle.GetRadianFromNurbFormParameter(u_min, &r_min) ||
      !cyl.circle.GetRadianFromNurbFormParameter(u_max, &r_max)) {
    throw std::runtime_error(
        "dino8::kernel::Brep::MixedFaces: face " + std::to_string(face_index) +
        ": ON_Circle::GetRadianFromNurbFormParameter failed converting the "
        "trim's own u-domain to true angle");
  }
  cf.angle = r_max - r_min;
  return cf;
}

// Staleness self-checks for the verbatim face records MixedFaces()
// returns in place of the geometric extraction above (see that method's
// own doc comment in brep.h): true iff the record still describes the
// face's REAL surface. Three surface evaluations per face, compared at
// 1e-6 of the face's own coordinate scale - far above the ~1e-15 relative
// noise of the surface construction (a planar face's margined bilinear
// grid, ON_Cylinder::GetNurbForm) and far below any transform a caller
// would apply to a raw() ON_Brep behind this class's back, which is the
// one way a record goes stale.
double RecordScale(const Point3d& a, const Point3d& b, const Point3d& c) {
  double scale = 1.0;
  for (const Point3d& p : {a, b, c}) {
    scale = std::max({scale, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
  }
  return scale;
}

// A planar face's trim polygon has one (u, v) vertex per input loop point,
// in the same order (FromMixedFaces' own planar loop), so the surface
// evaluated at the first three trim vertices must land on the record's
// first three loop points.
bool PlanarRecordMatchesFace(const Brep::PlanarFace& rec, const FaceGeometry& fg) {
  if (rec.loop.size() < 3 || fg.outer.size() != rec.loop.size()) return false;
  const double tol = 1e-6 * RecordScale(rec.loop[0], rec.loop[1], rec.loop[2]);
  for (size_t k = 0; k < 3; ++k) {
    const Point3d on_surface = fg.surface.PointAt(fg.outer[k].x, fg.outer[k].y);
    if (on_surface.DistanceTo(rec.loop[k]) > tol) return false;
  }
  return true;
}

// A cylindrical face's surface is ON_Cylinder(ON_Circle(frame, radius),
// ...).GetNurbForm(): u=0 is angle 0 (the frame.xaxis rail) and v is true
// axial height, so (0, 0) and (0, length) are the two angle-0 rail corners
// (one point for a length-0 eye), and a mid-domain u sample at v=0 must sit
// at height 0 and distance `radius` from the record's own axis - the third
// probe that pins a rotation about the rail line, which leaves both rail
// corners fixed but moves the axis. `outward` maps to !m_bRev.
bool CylindricalRecordMatchesFace(const Brep::CylindricalFace& rec, const FaceGeometry& fg, bool face_rev) {
  if (rec.outward == face_rev) return false;
  const Point3d rail0 = rec.frame.origin + rec.radius * rec.frame.xaxis;
  const Point3d rail1 = rail0 + rec.length * rec.frame.zaxis;
  const double tol = 1e-6 * RecordScale(rec.frame.origin, rail0, rail1);
  if (fg.surface.PointAt(0.0, 0.0).DistanceTo(rail0) > tol) return false;
  if (fg.surface.PointAt(0.0, rec.length).DistanceTo(rail1) > tol) return false;
  const Point3d mid = fg.surface.PointAt(fg.surface.Domain(0).Mid(), 0.0);
  const Vector3d rel = mid - rec.frame.origin;
  const double h = ON_DotProduct(rel, rec.frame.zaxis);
  const double radial = (rel - h * rec.frame.zaxis).Length();
  return std::fabs(h) <= tol && std::fabs(radial - rec.radius) <= tol;
}

}  // namespace

std::vector<Brep::PlanarFace> Brep::PlanarFaces() const {
  std::vector<PlanarFace> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (!wrapper.IsPlanar()) {
      throw std::invalid_argument(
          "dino8::kernel::Brep::PlanarFaces: face " + std::to_string(i) +
          " is not planar - this is a planar-only B-rep boolean, see its own doc comment");
    }
    result.push_back(ExtractPlanarFace(fg));
  }
  return result;
}

Brep::MixedFacesResult Brep::MixedFaces() const {
  MixedFacesResult result;
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    // The verbatim face record first (see this method's own doc comment
    // in brep.h): the exact PlanarFace/CylindricalFace FromMixedFaces()
    // built this face from, returned as-is while it still matches the
    // face's real surface. An absent record (any other factory, a cone
    // by contract) or a stale one (a raw() ON_Brep transformed behind
    // this class's back) takes the geometric extraction below, exactly
    // as every face did before records existed.
    if (static_cast<size_t>(i) < face_records_.size()) {
      const FaceRecord& rec = face_records_[static_cast<size_t>(i)];
      if (rec.kind == FaceRecord::kPlanar && PlanarRecordMatchesFace(rec.planar, fg)) {
        result.planar.push_back(rec.planar);
        continue;
      }
      if (rec.kind == FaceRecord::kCylindrical && CylindricalRecordMatchesFace(rec.cyl, fg, brep_.m_F[i].m_bRev)) {
        result.cylindrical.push_back(rec.cyl);
        continue;
      }
    }
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (wrapper.IsPlanar()) {
      result.planar.push_back(ExtractPlanarFace(fg));
      continue;
    }

    ON_Cylinder cyl;
    // Same tolerance scale cmd_fillet.cpp's own BuildPlaneCylinderVariableFillet
    // uses for this exact IsCylinder() call - loose enough to accept a
    // NURBS fit's own float-ish surface-construction noise, tight enough
    // not to misclassify a genuinely non-cylindrical face.
    const double cyl_tol = 1e-4;
    if (!fg.surface.IsCylinder(&cyl, cyl_tol)) {
      // Not a cylinder - try a cone next (see this method's own doc
      // comment for the ConicalFace recovery this mirrors from
      // FromMixedFaces()'s own cone-building code below).
      ExtractConicalFace(brep_, i, fg, result);
      continue;
    }

    result.cylindrical.push_back(ExtractCylindricalFace(brep_, i, fg, cyl));
  }
  return result;
}

namespace {

// Coincident-point vertex welding for FromMixedFaces()'s own genuine
// ON_Brep topology (real vertices/edges/trims/loops instead of just
// NewFace(surface_index)): the same principle Mesh::MergeAndWeld() already
// relies on for welding a tessellation's own seams shut, reused here as
// the identity test that gives PlanarFace/CylindricalFace loop points -
// which carry no vertex identity of their own - a shared ON_BrepVertex
// wherever two faces' own loops meet at "the same" 3D point. The value
// is the kernel's own weld distance (tolerance::kWeld, 1e-6 - the same
// number Mesh::MergeAndWeld's default reads), not a newly invented
// tolerance; see brep.h's FromMixedFaces doc comment for the real,
// disclosed limit this implies (features smaller than that mis-weld).
constexpr double kBrepWeldTolerance = tolerance::kWeld;

struct WeldKey {
  long long x = 0, y = 0, z = 0;
  bool operator==(const WeldKey& other) const {
    return x == other.x && y == other.y && z == other.z;
  }
};

struct WeldKeyHash {
  size_t operator()(const WeldKey& k) const {
    size_t h = std::hash<long long>()(k.x);
    h ^= std::hash<long long>()(k.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<long long>()(k.z) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};

// Welds coincident 3D points into canonical global vertex ids, local and
// temporary to one FromMixedFaces() call (never stored on Brep itself -
// see brep.h's own comment on why PlanarFace/CylindricalFace need no
// struct changes for this).
class VertexWelder {
 public:
  int Weld(const Point3d& p) {
    const WeldKey key{std::llround(p.x / kBrepWeldTolerance), std::llround(p.y / kBrepWeldTolerance),
                       std::llround(p.z / kBrepWeldTolerance)};
    const auto it = index_of_.find(key);
    if (it != index_of_.end()) return it->second;
    const int id = static_cast<int>(points_.size());
    points_.push_back(p);
    index_of_.emplace(key, id);
    return id;
  }
  const std::vector<Point3d>& Points() const { return points_; }

 private:
  std::unordered_map<WeldKey, int, WeldKeyHash> index_of_;
  std::vector<Point3d> points_;
};

// Per-face bookkeeping needed to build genuine loop/trim/edge topology,
// gathered alongside each face's existing surface-building code below
// without changing any of that math. `vids`/`trim_uv` are parallel arrays,
// one welded global vertex id and one (u, v) trim point per loop point, in
// loop order (already CCW-outward per every planar-face factory's own
// "u_dir x v_dir points outward" convention - see PlanarFaces()'s own
// comment - so this reuses that winding rather than re-deriving it).
struct FaceTopology {
  std::vector<int> vids;
  std::vector<Point2d> trim_uv;
  // Only set for a CylindricalFace's or ConicalFace's own 4-point (u, v)
  // rectangle loop (see BuildFaceLoop's own comment for why its two cap
  // segments, index 0 and 2, need this instead of a plain straight edge).
  // Borrowed - owned by brep.m_S, valid for this whole FromMixedFaces()
  // call. `curved_v0` is the TRUE v-value of the index-0 cap: always 0.0
  // for a CylindricalFace (whose own frame.origin sits ON the patch, at
  // v=0 by construction - see CylindricalFace's own doc comment), but
  // generally nonzero for a ConicalFace (whose own frame.origin is the
  // cone's APEX, outside the trimmed patch - see ConicalFace's own doc
  // comment), so the two cap isocurves live at v=curved_v0 and
  // v=curved_v0+curved_length rather than always at v=0 and v=length.
  ON_NurbsSurface* curved_surface = nullptr;
  double curved_u_max = 0.0;
  double curved_v0 = 0.0;
  double curved_length = 0.0;
  // Parallel to vids/trim_uv (same length, or empty when nothing on this
  // face has been collapsed - the overwhelmingly common case). Non-empty
  // at index k means the segment vids[k]->vids[k+1] is a PlanarFace's own
  // collapsed notch run (see PlanarFace::notch_begin/notch_count's own
  // doc comment): these are the run's own INTERIOR (u, v) points, in
  // order, excluding the two endpoints already at trim_uv[k]/trim_uv[k1]
  // - so BuildFaceLoop can thread them into that one trim's own 2D curve
  // as a genuine dense polyline instead of collapsing the visible
  // boundary itself, even though the real topology now has only ONE edge
  // there (shared with the adjacent CylindricalFace's own cap - see
  // FromMixedFaces' own comment for exactly why this two-level
  // (collapsed topology, dense trim curve) split is what's needed).
  std::vector<std::vector<Point2d>> notch_interior_uv;
  // Parallel to notch_interior_uv (same length when non-empty): the
  // genuinely computed sagitta-style tolerance for THAT segment's own
  // notch (see ConicalFace::cap0_notch_tolerance/cap1_notch_tolerance's
  // own doc comment) - meaningless (0.0) at any index where
  // notch_interior_uv is empty. Only ever populated for a ConicalFace's
  // own cap segments (index 0/2); a PlanarFace's own notch keeps using
  // the shared edge built by whichever CylindricalFace/ConicalFace visits
  // first, so it never needs its own entry here.
  std::vector<double> cap_notch_tolerance;
  // Parallel to notch_interior_uv (same length when non-empty): for a
  // CylindricalFace's/ConicalFace's own notched cap segment, the midpoint
  // of the LITERAL cap*_notch_points list that segment was built from
  // (NotchListMidpoint below) - the one datum BuildFaceLoop uses to tell
  // two genuinely different notch polylines joining the same two vertices
  // apart. Default-valued at any index whose notch_interior_uv is empty.
  std::vector<Point3d> notch_midpoint_3d;
};

// The midpoint of a dense notch sample list, as the average of its two
// central points (the same point twice when the count is odd), so the
// value is identical whichever direction the list is walked - the two
// faces sharing a notched edge walk it in opposite directions.
Point3d NotchListMidpoint(const std::vector<Point3d>& pts) {
  const Point3d& a = pts[(pts.size() - 1) / 2];
  const Point3d& b = pts[pts.size() / 2];
  return 0.5 * (a + b);
}

// Builds one face's genuine ON_BrepLoop plus its edges/trims (spec
// sections 2-3): an edge is created the first time its own {min(vid),
// max(vid)} welded vertex pair is seen and shared automatically the
// second time (the same pair from the adjacent face's own loop) via
// `edge_of_vertex_pair`; a third use is a non-manifold edge, out of scope
// exactly like every other planar-only/convex-only note already in this
// codebase (boolean.h/fillet.h), so it throws rather than silently
// misbuilding a third trim onto it.
//
// The 3D edge curve is a straight ON_LineCurve between the two welded
// points in every case except a CylindricalFace's or ConicalFace's own
// two circular cap segments (index 0 at v=curved_v0, index 2 at
// v=curved_v0+curved_length, of its 4-point [u:0..curved_u_max,
// v:curved_v0..curved_v0+curved_length] rectangle loop - see brep.h's
// FromMixedFaces comment for that rectangle's own construction), which
// instead use the surface's own isocurve (ON_Surface::IsoCurve(0, v)) so
// the edge's C3 curve and the trim's 2D-to-surface composition are
// identical by construction, not independently reconstructed and merely
// close - UNLESS that cap segment is itself notched (see
// ConicalFace::cap0_notch_points/cap1_notch_points' own doc comment), in
// which case the true boundary there is a curve (generally an ellipse)
// with no simple isocurve form, so the edge is instead a dense
// ON_PolylineCurve through the notch's own sample points (see below). The
// rectangle's other two segments (index 1 at u=curved_u_max, index 3 at
// u=0) - the fillet's two straight "rail" lines - need no such
// special-casing: they're genuinely straight, so the plain ON_LineCurve
// path already welds them against FilletConvexEdge's own re-trimmed
// planar faces with zero extra work, exactly as that function's own doc
// comment states.
//
// ON_Surface::IsoCurve(0, c)'s own natural direction is increasing-u
// (point at parameter t is srf(t, c)): segment 0 (u: 0 -> u_max) walks
// that same direction, but segment 2 (u: u_max -> 0, per the rectangle's
// own CCW order) walks it backwards - `iso_reversed` below accounts for
// that so the new edge's own v0/v1 vertex assignment always matches its
// own 3D curve's real start/end point, which every other edge here (and
// ON_Brep's own topology in general) requires.
//
// A cap segment (index 0 or 2) that ALSO carries a `notch_interior_uv`
// entry (see ConicalFace::cap0_notch_points/cap1_notch_points' own doc
// comment) is a genuinely DIFFERENT case from the plain isocurve above:
// its own true boundary curve is not a fixed-v isocurve at all (a plain
// circle), so its own C3 edge curve is instead built as a dense
// ON_PolylineCurve through the SAME (u, v) points this segment's own 2D
// trim curve threads through below (topo.trim_uv[k], every
// notch_interior_uv[k] entry, topo.trim_uv[k1]) - already in this
// segment's own k->k1 walk order by construction (see
// EllipseNotchCornerAtVertex's own doc comment for how that order is
// guaranteed), so unlike the plain isocurve branch, no `iso_reversed`
// correction is needed here at all.
//
// A curved face's RAIL segment (index 1 or 3) whose two endpoint vertices
// welded to the SAME vertex is a zero-length rail and is skipped - no
// trim, no edge. Only a CylindricalFace with length == 0 and both caps
// notched (a Steinmetz "eye", see CylindricalFace's own doc comment) ever
// has one: its loop is then the two notched cap polylines alone, a bigon
// between the two pinch vertices. Deliberately restricted to rails: a
// full-sweep face's CAP segment legitimately self-loops (its two rail
// corners at angle 0 and 2*pi are the same welded vertex) and must keep
// being built.
//
// `notched_edges_of_vertex_pair` disambiguates NOTCHED cap edges that
// reduce to the same two endpoint vertices but are genuinely different
// curves - the notched analogue of `cap_arc_midpoint_of_edge` below,
// needed once the same two vertices can carry several distinct notch
// polylines: the four half-ellipses of a Steinmetz crossing all join the
// same two pinch vertices, and an eye's own two caps are two of them.
// Keyed by the plain vertex-pair key, it lists every edge a notched cap
// segment CREATED under that pair (its polyline's midpoint, and the key
// it was stored under). It intervenes ONLY when a notched segment's plain
// key collides with an EXISTING edge that was itself created by a notched
// segment with a DIFFERENT midpoint: the segment then reuses a listed
// edge whose midpoint matches its own, or is stored under a salted key of
// its own. Every other pattern is exactly as it always was - a notched
// segment finding no edge creates one under the plain key, one finding a
// plain is-cap arc or a straight edge reuses it (the fillet corner-notch
// mechanism), and one finding a notched edge with the SAME midpoint
// (a shared cut between two fragments) reuses it. The midpoint is taken
// from the LITERAL cap*_notch_points list the segment was built from
// (FaceTopology::notch_midpoint_3d), never re-evaluated through this
// face's own surface: two faces sharing a curve share that list verbatim
// (a Steinmetz half-ellipse handed to both cylinders; a tapered fillet's
// interior-station join, whose borrowed points deliberately do NOT lie on
// the later cone's surface - see ConicalFace::cap0_surface_fit_tolerance),
// so their midpoints agree exactly, while genuinely different curves
// between the same two vertices are separated by a physical distance
// (the four Steinmetz half-ellipses' midpoints sit at least 2r apart).
// Compared by distance, not by a quantized hash, so no rounding boundary
// can split a shared curve in two.
//
// The third identity gap, closed the same collision-only way: a STRAIGHT
// segment (a curved face's rail, or a planar loop edge) whose plain key
// names an EXISTING edge that a plain is-cap ARC created. A circular arc
// and the chord between its endpoints are never the same curve unless
// the arc is degenerate (its midpoint on the chord), so the two cannot
// legitimately share an edge - yet before this refinement the straight
// segment silently reused the arc's edge, and with the arc already
// shared by two faces that meant "shared by 3 or more faces". The one
// producer of this pattern is the unequal-radius cylinder/cylinder
// Difference at a right angle (boolean.cpp, SplitCylindricalByUnequalCylinder):
// the smaller cylinder's middle band has a straight rail between two
// pinch vertices, and the larger cylinder's plain piece, cut at the
// crossing height, has its cut ARC between the same two vertices. The
// segment is salted by its own chord midpoint (quantized, FNV-mixed, the
// same recipe the is-cap salt uses) - the same key the other half-band's
// identical rail then computes, so a rail shared by two faces stays one
// edge - and only when the existing arc's midpoint is farther than
// 10 * kBrepWeldTolerance from that chord midpoint, so a degenerate arc
// keeps matching its chord exactly as before. No sharing pattern the
// suite exercises changes: an arc arriving AFTER a straight edge was
// already salted by the is-cap block below (same_cap_arc_midpoint
// answers false for any non-is-cap edge), a straight segment finding a
// straight or notched edge still reuses it, and a straight segment
// creating an edge first is untouched.
//
// The fourth, the notched analogue of the third: a STRAIGHT segment whose
// plain key names an EXISTING edge that a NOTCHED cap segment created
// under that same plain key, with the polyline's midpoint off this
// segment's chord. A chord and a non-degenerate polyline are never one
// curve either. The producer is the same cylinder/cylinder Difference at
// a general axis angle: the larger cylinder's plain piece is then cut by
// a HELIX between the two pinch vertices (a notched polyline, not a cap
// arc), and the smaller cylinder's middle band's straight rail joins the
// same two vertices. Salted exactly as the third (by the quantized chord
// midpoint, so the other half-band's identical rail lands on the same
// key), only when the listed polyline's midpoint is farther than
// 10 * kBrepWeldTolerance from the chord midpoint (a degenerate polyline
// keeps matching its chord), and only against an edge stored under the
// plain key itself. The mirror order - a notched segment arriving after
// a straight edge - deliberately keeps reusing that edge, the fillet
// corner-notch mechanism above; the helix always arrives first (a
// Difference lists the first operand's faces before the second's, and
// curved faces' loops are built before planar ones).
void BuildFaceLoop(ON_Brep& brep, ON_BrepFace& face, const FaceTopology& topo,
                    std::unordered_map<uint64_t, int>& edge_of_vertex_pair,
                    std::unordered_map<int, Point3d>& cap_arc_midpoint_of_edge,
                    std::unordered_map<uint64_t, std::vector<std::pair<Point3d, uint64_t>>>& notched_edges_of_vertex_pair) {
  ON_BrepLoop& loop = brep.NewLoop(ON_BrepLoop::outer, face);
  const size_t n = topo.vids.size();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    const int vid_from = topo.vids[k];
    const int vid_to = topo.vids[k1];

    const bool is_cap = topo.curved_surface != nullptr && (k == 0 || k == 2);
    const bool has_notch_interior = k < topo.notch_interior_uv.size() && !topo.notch_interior_uv[k].empty();
    const bool iso_reversed = is_cap && !has_notch_interior && k == 2;

    if (topo.curved_surface != nullptr && (k == 1 || k == 3) && vid_from == vid_to) {
      continue;  // zero-length rail of a length == 0 eye - see this function's own doc comment
    }

    const uint32_t lo = static_cast<uint32_t>(std::min(vid_from, vid_to));
    const uint32_t hi = static_cast<uint32_t>(std::max(vid_from, vid_to));
    const uint64_t plain_key = (static_cast<uint64_t>(lo) << 32) | hi;
    uint64_t key = plain_key;

    // A real edge-identity refinement, needed once a single circle's own
    // cap boundary can legitimately exist as TWO DIFFERENT arcs sharing
    // the exact same two endpoint vertices - found and fixed by direct
    // counterexample in the later parallel-axis cylinder/cylinder
    // Intersection/Difference increment (boolean.cpp's own
    // SplitCylindricalByOtherCylinderAxialExtent doc comment has the full
    // worked scenario): a genuinely CROSSING pair's two angular wedge
    // children share their own two crossing-point vertices, so the SHORT
    // arc (one wedge's own cap boundary) and the LONG arc (the OTHER
    // wedge's own cap boundary, or that same wedge's own two axially-
    // adjacent pieces reconnecting after an axial split) both reduce to
    // the identical (lo, hi) vertex-pair key above despite being
    // genuinely DIFFERENT curves - silently welding the wrong pair, or
    // (once a THIRD claimant of the same key exists) throwing the "shared
    // by 3 or more faces" refusal below for two faces that were never
    // actually adjacent at all.
    //
    // Deliberately checked ONLY against an EXISTING edge that was ITSELF
    // created by a plain (non-notched) is_cap segment - never against a
    // straight edge or a notched-polyline edge - and deliberately applied
    // AFTER the ordinary plain-key lookup below finds a collision, not
    // folded into the key up front: an earlier version of this fix
    // computed an arc-identity-augmented key UNCONDITIONALLY for every
    // is_cap segment, which broke every PRE-EXISTING case where a
    // CylindricalFace's/ConicalFace's own is_cap cap edge is legitimately
    // shared with a DIFFERENT kind of segment reducing to the same 2
    // vertices by a DIFFERENT construction (a fillet's own corner-notch
    // splice against an adjacent wall's dense notch polyline chief among
    // them, task #50/#52's own already-verified mechanism) - a real,
    // checked-directly regression (FilletConvexEdge's, FilletConvexEdgeTapered's,
    // and the .3dm round-trip's own corner-notch tests all newly failed),
    // since the two sides of that SAME shared edge compute DIFFERENT
    // augmented keys (one is_cap, one not) and can never find each other
    // again. This narrower form only ever intervenes at the ONE genuine
    // ambiguity this codebase has - two is_cap arcs, same 2 endpoints,
    // different curves - leaving every other sharing pattern (is_cap vs.
    // straight, is_cap vs. notched-polyline, is_cap vs. the SAME arc)
    // exactly as it always matched.
    auto same_cap_arc_midpoint = [&](int existing_edge_index) {
      const auto it2 = cap_arc_midpoint_of_edge.find(existing_edge_index);
      if (it2 == cap_arc_midpoint_of_edge.end()) return false;  // not an is_cap-created edge at all
      const double v_const_for_check = (k == 0) ? topo.curved_v0 : topo.curved_v0 + topo.curved_length;
      const Point3d mid = topo.curved_surface->PointAt(topo.curved_u_max * 0.5, v_const_for_check);
      return mid.DistanceTo(it2->second) <= kBrepWeldTolerance * 10.0;
    };
    if (is_cap && !has_notch_interior) {
      const auto plain_it = edge_of_vertex_pair.find(plain_key);
      if (plain_it != edge_of_vertex_pair.end() && !same_cap_arc_midpoint(plain_it->second)) {
        // A genuinely different arc claims the same 2 vertices as an
        // EXISTING is_cap-created edge - salt the key so this arc gets
        // (or finds) its own separate edge instead of colliding with the
        // wrong one.
        const double v_const_for_hash = (k == 0) ? topo.curved_v0 : topo.curved_v0 + topo.curved_length;
        const Point3d mid = topo.curved_surface->PointAt(topo.curved_u_max * 0.5, v_const_for_hash);
        auto quant = [](double x) { return std::llround(x / kBrepWeldTolerance); };
        uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
        auto mix = [&](int64_t v) {
          h ^= static_cast<uint64_t>(v);
          h *= 1099511628211ull;  // FNV-1a prime
        };
        mix(quant(mid.x));
        mix(quant(mid.y));
        mix(quant(mid.z));
        key = plain_key ^ h;
      }
    }

    // Straight-vs-arc disambiguation - see this function's own doc
    // comment. Collision-only: `key` stays the plain key unless the plain
    // key already names an edge a plain is-cap arc created whose midpoint
    // is off this segment's chord.
    if (!is_cap && !has_notch_interior) {
      const auto plain_it = edge_of_vertex_pair.find(plain_key);
      if (plain_it != edge_of_vertex_pair.end()) {
        const auto arc_it = cap_arc_midpoint_of_edge.find(plain_it->second);
        if (arc_it != cap_arc_midpoint_of_edge.end()) {
          const Point3d chord_mid = 0.5 * (brep.m_V[vid_from].point + brep.m_V[vid_to].point);
          if (chord_mid.DistanceTo(arc_it->second) > kBrepWeldTolerance * 10.0) {
            auto quant = [](double x) { return std::llround(x / kBrepWeldTolerance); };
            uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
            auto mix = [&](int64_t v) {
              h ^= static_cast<uint64_t>(v);
              h *= 1099511628211ull;  // FNV-1a prime
            };
            mix(quant(chord_mid.x));
            mix(quant(chord_mid.y));
            mix(quant(chord_mid.z));
            key = plain_key ^ h;
          }
        }
      }
    }

    // Straight-vs-notched disambiguation - see this function's own doc
    // comment. Collision-only: `key` stays the plain key unless the plain
    // key already names an edge a notched cap segment created under that
    // very key whose polyline midpoint is off this segment's chord.
    if (!is_cap && !has_notch_interior && key == plain_key) {
      const auto plain_it = edge_of_vertex_pair.find(plain_key);
      if (plain_it != edge_of_vertex_pair.end()) {
        const auto listed = notched_edges_of_vertex_pair.find(plain_key);
        if (listed != notched_edges_of_vertex_pair.end()) {
          for (const std::pair<Point3d, uint64_t>& entry : listed->second) {
            if (entry.second != plain_key) continue;  // only the edge actually stored under the plain key
            const Point3d chord_mid = 0.5 * (brep.m_V[vid_from].point + brep.m_V[vid_to].point);
            if (chord_mid.DistanceTo(entry.first) > kBrepWeldTolerance * 10.0) {
              auto quant = [](double x) { return std::llround(x / kBrepWeldTolerance); };
              uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
              auto mix = [&](int64_t v) {
                h ^= static_cast<uint64_t>(v);
                h *= 1099511628211ull;  // FNV-1a prime
              };
              mix(quant(chord_mid.x));
              mix(quant(chord_mid.y));
              mix(quant(chord_mid.z));
              key = plain_key ^ h;
            }
            break;
          }
        }
      }
    }

    // Notched-vs-notched disambiguation - see this function's own doc
    // comment. Collision-only: `key` stays the plain key unless the plain
    // key already names an edge that a notched segment with a DIFFERENT
    // midpoint created.
    Point3d notched_mid;
    if (is_cap && has_notch_interior) {
      notched_mid = k < topo.notch_midpoint_3d.size() ? topo.notch_midpoint_3d[k] : Point3d();
      if (edge_of_vertex_pair.find(plain_key) != edge_of_vertex_pair.end()) {
        const auto listed = notched_edges_of_vertex_pair.find(plain_key);
        if (listed != notched_edges_of_vertex_pair.end() && !listed->second.empty()) {
          bool matched = false;
          for (const std::pair<Point3d, uint64_t>& entry : listed->second) {
            if (entry.first.DistanceTo(notched_mid) <= kBrepWeldTolerance * 10.0) {
              key = entry.second;
              matched = true;
              break;
            }
          }
          if (!matched) {
            uint64_t h = 1469598103934665603ull;  // FNV-1a offset basis
            h ^= static_cast<uint64_t>(listed->second.size());
            h *= 1099511628211ull;  // FNV-1a prime
            key = plain_key ^ h;
          }
        }
      }
    }

    int edge_index;
    const auto it = edge_of_vertex_pair.find(key);
    const bool edge_freshly_created = (it == edge_of_vertex_pair.end());
    if (it == edge_of_vertex_pair.end()) {
      ON_Curve* c3 = nullptr;
      int curve_start_vid = vid_from;
      int curve_end_vid = vid_to;
      // See ConicalFace::cap0_notch_tolerance/cap1_notch_tolerance's own
      // doc comment for why this, unlike every other edge here, isn't
      // always honestly 0.0.
      double edge_tolerance = 0.0;
      if (is_cap && has_notch_interior) {
        ON_3dPointArray pts3d;
        pts3d.Append(topo.curved_surface->PointAt(topo.trim_uv[k].x, topo.trim_uv[k].y));
        for (const Point2d& p : topo.notch_interior_uv[k]) {
          pts3d.Append(topo.curved_surface->PointAt(p.x, p.y));
        }
        pts3d.Append(topo.curved_surface->PointAt(topo.trim_uv[k1].x, topo.trim_uv[k1].y));
        auto* poly = new ON_PolylineCurve(pts3d);
        poly->SetDomain(0.0, 1.0);
        c3 = poly;
        edge_tolerance = k < topo.cap_notch_tolerance.size() ? topo.cap_notch_tolerance[k] : 0.0;
      } else if (is_cap) {
        const double v_const = (k == 0) ? topo.curved_v0 : topo.curved_v0 + topo.curved_length;
        ON_Curve* iso = topo.curved_surface->IsoCurve(/*dir=*/0, v_const);
        if (!iso) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: ON_Surface::IsoCurve failed "
              "building a CylindricalFace's/ConicalFace's own cap edge");
        }
        if (!iso->Trim(ON_Interval(0.0, topo.curved_u_max))) {
          delete iso;
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: trimming a "
              "CylindricalFace's/ConicalFace's own cap isocurve to its real "
              "sweep angle failed");
        }
        iso->SetDomain(0.0, 1.0);
        c3 = iso;
        if (iso_reversed) {
          curve_start_vid = vid_to;
          curve_end_vid = vid_from;
        }
      } else {
        c3 = new ON_LineCurve(brep.m_V[vid_from].point, brep.m_V[vid_to].point);
        c3->SetDomain(0.0, 1.0);
      }
      const int c3i = brep.AddEdgeCurve(c3);
      ON_BrepEdge& edge = brep.NewEdge(brep.m_V[curve_start_vid], brep.m_V[curve_end_vid], c3i);
      // A real, checked-directly discovery (not assumed from the spec's
      // own text): the PUBLIC OpenNURBS build's own ON_Brep::
      // SetEdgeTolerance is a deliberate stub for any edge with a trim -
      // its own comment says so verbatim ("TL_Brep::SetEdgeTolerance
      // overrides ON_Brep::SetEdgeTolerance and sets the tolerance
      // correctly") - TL_Brep being Rhino's own closed-source topology
      // library, not part of the public SDK this kernel is built on. Left
      // alone, every edge here would keep ON_UNSET_VALUE forever and
      // IsValid() would report every single one as invalid, regardless of
      // how correct the actual topology is. The honest fix, matching
      // example_brep.cpp's own MakeTwistedCubeEdge (see its own "this
      // simple example is exact" comment): every edge this function
      // builds genuinely IS exact - a straight line between the same two
      // points its own endpoint vertices store, or a CylindricalFace
      // cap's own true isocurve - so 0.0 is the real answer, not a
      // plugged-in default - EXCEPT a notched ConicalFace cap edge (see
      // above), which is honestly a polygonal approximation of a curve
      // with no simple isocurve form, carrying its own genuinely computed
      // `edge_tolerance` instead of a false claim of exactness. Pass 5
      // below calls SetTolerancesBoxesAndFlags with bLazy=true
      // specifically so it leaves this alone instead of overwriting it.
      edge.m_tolerance = edge_tolerance;
      edge_index = edge.m_edge_index;
      edge_of_vertex_pair.emplace(key, edge_index);
      if (is_cap && !has_notch_interior) {
        const double v_const_for_record = (k == 0) ? topo.curved_v0 : topo.curved_v0 + topo.curved_length;
        cap_arc_midpoint_of_edge.emplace(edge_index,
                                          topo.curved_surface->PointAt(topo.curved_u_max * 0.5, v_const_for_record));
      } else if (is_cap && has_notch_interior) {
        notched_edges_of_vertex_pair[plain_key].emplace_back(notched_mid, key);
      }
    } else {
      edge_index = it->second;
      if (brep.m_E[edge_index].m_ti.Count() >= 2) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: an edge is shared by 3 or "
            "more faces (non-manifold) - out of scope here, matching every "
            "other planar-only/convex-only scope note already in this "
            "codebase (boolean.h/fillet.h)");
      }
    }

    const ON_BrepEdge& edge = brep.m_E[edge_index];
    // For a freshly-created edge, the true "is this trim's own walk
    // direction reversed relative to the 3D edge curve's own
    // parametrization" fact is already known exactly from `iso_reversed`
    // (decided above, at the same point `curve_start_vid`/`curve_end_vid`
    // were chosen) - use it directly instead of re-deriving it from vertex
    // identity. The vertex-identity comparison below
    // (`edge.m_vi[0] != vid_from`) is lossy exactly when this segment's own
    // two endpoint vertices are the SAME vertex - a full 2*pi-sweep closed
    // cap edge, where vid_from == vid_to identically regardless of the true
    // sweep direction - and it silently evaluates to `false` for every such
    // edge no matter what `iso_reversed` says, producing a spurious
    // ON_Brep::IsValid() "closed curve directions are opposite" report.
    // For the reuse path (a second face sharing an already-built edge),
    // this segment's own `iso_reversed`/`is_cap` facts describe the OTHER
    // face's trim, not this one, so they don't apply here; the
    // vertex-identity comparison remains the only signal available and is
    // correct for every non-closed shared edge (the only kind that reaches
    // this branch in practice - see the doc comment above `is_cap` on why a
    // closed cap edge is never genuinely shared with `has_notch_interior ==
    // false` semantics changing between the two uses).
    const bool bRev3d = edge_freshly_created ? (is_cap && iso_reversed) : (edge.m_vi[0] != vid_from);

    ON_Curve* c2 = nullptr;
    if (has_notch_interior) {
      // This segment is a PlanarFace's own collapsed notch run (see
      // PlanarFace::notch_begin/notch_count's own doc comment): the real
      // topology has collapsed it to ONE edge (shared with the adjacent
      // CylindricalFace's own true-arc cap, per this method's own
      // reordered Pass 3/4 above), but this face's own 2D trim curve for
      // it is still the full dense polyline through every original notch
      // point - an ON_PolylineCurve, not a 2-point ON_LineCurve - so this
      // face's own visible boundary (and any consumer deriving it purely
      // from stored topology, e.g. a .3dm reload) is exactly the fine
      // polygonal notch, not a chord cutting straight across the corner.
      ON_3dPointArray pts;
      pts.Append(ON_3dPoint(topo.trim_uv[k].x, topo.trim_uv[k].y, 0.0));
      for (const Point2d& p : topo.notch_interior_uv[k]) pts.Append(ON_3dPoint(p.x, p.y, 0.0));
      pts.Append(ON_3dPoint(topo.trim_uv[k1].x, topo.trim_uv[k1].y, 0.0));
      auto* poly = new ON_PolylineCurve(pts);
      poly->ChangeDimension(2);
      poly->SetDomain(0.0, 1.0);
      c2 = poly;
    } else {
      c2 = new ON_LineCurve(topo.trim_uv[k], topo.trim_uv[k1]);
      c2->SetDomain(0.0, 1.0);
    }
    const int c2i = brep.AddTrimCurve(c2);
    brep.NewTrim(brep.m_E[edge_index], bRev3d, loop, c2i);
  }
}

}  // namespace

Brep Brep::FromMixedFaces(const std::vector<Brep::PlanarFace>& faces,
                           const std::vector<Brep::CylindricalFace>& cylindrical_faces,
                           const std::vector<Brep::ConicalFace>& conical_faces) {
  Brep result;
  ON_Brep& brep = result.brep_;
  VertexWelder welder;
  std::vector<FaceTopology> topo;
  for (const PlanarFace& f : faces) {
    if (f.loop.size() < 3) continue;  // degenerate slice - nothing left of this face
    const ON_Plane& pl = f.plane;
    double min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    std::vector<Point2d> local;
    local.reserve(f.loop.size());
    for (size_t k = 0; k < f.loop.size(); ++k) {
      const ON_3dVector d = f.loop[k] - pl.origin;
      const double x = d * pl.xaxis, y = d * pl.yaxis;
      local.emplace_back(x, y);
      if (k == 0) { min_x = max_x = x; min_y = max_y = y; }
      else { min_x = std::min(min_x, x); max_x = std::max(max_x, x); min_y = std::min(min_y, y); max_y = std::max(max_y, y); }
    }
    // A small margin so the trim loop's own extremal points never sit
    // exactly on the surface's own domain edge (a real, if rare, source
    // of clipping-boundary ambiguity in TessellateGridClippedExact).
    const double mx = std::max(1e-9, (max_x - min_x) * 0.05), my = std::max(1e-9, (max_y - min_y) * 0.05);
    min_x -= mx; max_x += mx; min_y -= my; max_y += my;
    const std::vector<Point3d> grid = {
        pl.origin + min_x * pl.xaxis + min_y * pl.yaxis,
        pl.origin + min_x * pl.xaxis + max_y * pl.yaxis,
        pl.origin + max_x * pl.xaxis + min_y * pl.yaxis,
        pl.origin + max_x * pl.xaxis + max_y * pl.yaxis,
    };
    const NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
    // FromControlGrid's clamped-uniform knot vector puts the domain at
    // [0,1] regardless of the grid's real-world span, so rescale each
    // local (x, y) into that normalized domain to get the trim loop.
    std::vector<Point2d> trim;
    trim.reserve(local.size());
    for (const Point2d& p : local) trim.emplace_back((p.x - min_x) / (max_x - min_x), (p.y - min_y) / (max_y - min_y));
    auto* surface_copy = new ON_NurbsSurface(surface.raw());
    const int surface_index = brep.AddSurface(surface_copy);
    brep.NewFace(surface_index);
    result.face_trim_loops_.push_back(trim);
    // exact_clip=true: this face's trim polygon IS its exact boundary
    // (not an approximation of a curved one), so tessellation should
    // clip to it exactly rather than approximate via whole-cell in/out -
    // otherwise a caller measuring volume at low division counts would
    // see grid-approximation error on a shape that has none to begin with.
    result.face_exact_clip_.push_back(true);
    result.face_hole_loops_.emplace_back();
    // Carried through verbatim - see PlanarFace::ArcRun's own doc comment
    // and TessellateConforming()'s own doc comment for the one place
    // this is actually read.
    result.face_arc_runs_.push_back(f.arc_runs);
    result.face_notch_rows_.emplace_back();  // a planar face never has notch rows
    // The input record itself, verbatim, for MixedFaces() to hand back
    // (see FaceRecord's own comment in brep.h).
    result.face_records_.emplace_back();
    result.face_records_.back().kind = FaceRecord::kPlanar;
    result.face_records_.back().planar = f;

    // Genuine topology (see BuildFaceLoop above): weld this face's own
    // loop points - the exact same 3D points the side tables above just
    // recorded - into canonical global vertex ids, reusing f.loop/trim
    // rather than re-deriving either.
    FaceTopology t;
    t.vids.reserve(f.loop.size());
    if (f.notch_count > 1 && f.notch_begin >= 0 &&
        static_cast<size_t>(f.notch_begin + f.notch_count) <= f.loop.size()) {
      // See PlanarFace::notch_begin/notch_count's own doc comment: collapse
      // this run's own STRICTLY INTERIOR points out of the topology-only
      // vids/trim_uv (so the run becomes ONE loop segment, able to share
      // ONE real edge with the adjacent CylindricalFace's own cap - see
      // FromMixedFaces' own reordered Pass 3/4 below), while folding those
      // same interior points into notch_interior_uv at the run's own first
      // KEPT index so BuildFaceLoop can still thread them into that one
      // segment's own 2D trim curve as a dense polyline - the visible
      // boundary this face presents (face_trim_loops_ below, and hence
      // this kernel's own Tessellate()) is entirely untouched either way.
      const int begin = f.notch_begin;
      const int end = f.notch_begin + f.notch_count - 1;  // last run index, inclusive
      for (size_t k = 0; k < f.loop.size(); ++k) {
        const int ik = static_cast<int>(k);
        if (ik > begin && ik < end) continue;  // strictly-interior notch point
        t.vids.push_back(welder.Weld(f.loop[k]));
        t.trim_uv.push_back(trim[k]);
        if (ik == begin) {
          t.notch_interior_uv.emplace_back(trim.begin() + begin + 1, trim.begin() + end);
        } else {
          t.notch_interior_uv.emplace_back();
        }
      }
    } else {
      t.trim_uv = trim;
      for (const Point3d& p : f.loop) t.vids.push_back(welder.Weld(p));
    }
    topo.push_back(std::move(t));
  }

  for (const CylindricalFace& cf : cylindrical_faces) {
    const ON_Circle circle(cf.frame, cf.radius);
    ON_Cylinder cyl(circle, cf.length);
    // A notched cap (cap0_notch_points/cap1_notch_points, see that field's
    // own doc comment) is generally NOT confined to the [0, length] band
    // the two rail corners span: an oblique cut's ellipse swings both
    // above and below the single scalar height the rail corners are
    // anchored at (SplitCylindricalByObliquePlane, boolean.cpp, anchors
    // both children at h(0) while the ellipse itself ranges over
    // h(0) -+ amplitude around the sweep), so the kept region of the
    // "hi" child dips below its own v=0 and the "lo" child's rises above
    // its own v=length. ON_Cylinder::GetNurbForm sets the surface's
    // v-knots to literally [height[0], height[1]], and every grid
    // tessellator here (NurbsSurface::TessellateGridClippedExact chief
    // among them) lays its cells over the SURFACE's own domain - so any
    // trimmed area outside that domain would simply never be covered,
    // silently dropping the out-of-band sliver from every Tessellate()
    // result with no error (a real, measured defect: a notched wall
    // came out at exactly the UN-notched band's area). Widening the
    // cylinder's own height span here, BEFORE GetNurbForm, to cover
    // every notch point's true height is the whole fix: v is true axial
    // height in this parameterization (see the comment just below), so
    // no (u, v) coordinate computed anywhere downstream changes - only
    // the domain the grid spans. Gated on a notch actually being present
    // so the overwhelmingly common un-notched face is built bit-for-bit
    // exactly as before (height[0] == 0, height[1] == length).
    if (!cf.cap0_notch_points.empty() || !cf.cap1_notch_points.empty()) {
      double h_min = std::min(0.0, cf.length), h_max = std::max(0.0, cf.length);
      auto widen_to = [&](const std::vector<Point3d>& pts) {
        for (const Point3d& p : pts) {
          const double h = (p - cf.frame.origin) * cf.frame.zaxis;
          h_min = std::min(h_min, h);
          h_max = std::max(h_max, h);
        }
      };
      widen_to(cf.cap0_notch_points);
      widen_to(cf.cap1_notch_points);
      constexpr double kMinHeightSpan = 1e-9;
      if (!(h_max - h_min > kMinHeightSpan)) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: a notched CylindricalFace's "
            "own height span (its [0, length] rail band widened to cover every "
            "cap notch point's true height) is degenerate (zero or NaN) - "
            "there is no surface to build");
      }
      cyl.height[0] = h_min;
      cyl.height[1] = h_max;
    }
    auto* surface = new ON_NurbsSurface();
    const int rc = cyl.GetNurbForm(*surface);
    if (rc == 0) {
      delete surface;
      throw std::runtime_error(
          "dino8::kernel::Brep::FromMixedFaces: ON_Cylinder::GetNurbForm failed "
          "(invalid frame/radius/length)");
    }
    // The cylinder's own NURBS surface parameterizes u by the base
    // circle's NURBS-curve parameter (NOT true radian angle - see this
    // method's own doc comment) and v linearly by true height (v == the
    // real distance along `frame.zaxis`, since ON_Cylinder::GetNurbForm
    // sets the v-knots directly to [height[0], height[1]] with no
    // reparameterization). So v = 0 and v = cf.length are exactly right,
    // but the u-bound for `cf.angle` of true sweep has to be found via
    // the real NURBS<->radian conversion ON_Circle itself provides.
    double u_max = 0.0;
    if (!circle.GetNurbFormParameterFromRadian(cf.angle, &u_max)) {
      delete surface;
      throw std::invalid_argument(
          "dino8::kernel::Brep::FromMixedFaces: CylindricalFace::angle is out "
          "of ON_Circle's own [0, 2*pi] NURBS-parameterization domain");
    }
    const int surface_index = brep.AddSurface(surface);
    ON_BrepFace& face = brep.NewFace(surface_index);
    // Same increasing-parameter corner order PlanarFaces()'s own
    // untrimmed-domain fallback uses - and, per this method's own doc
    // comment, u_dir x v_dir already points radially outward for
    // ON_Cylinder::GetNurbForm's natural parameterization (verified
    // directly: at u=0 the tangent in u is r*(local +y) and dP/dv is
    // frame.zaxis, whose cross product is r*frame.xaxis - the true
    // outward radial direction at angle 0). `cf.outward == false` (see
    // CylindricalFace's own doc comment) flips that via m_bRev, exactly
    // the flag Tessellate()/TessellateAdaptive()/
    // TessellateNonUniformAdaptive() already check generically for any
    // face; Sphere()'s own precedent of never setting it is unaffected
    // (this is the same field, just actually used here for the first
    // time).
    face.m_bRev = !cf.outward;
    // A notched cap's LAST point may sit on the angle-`angle` rail at a
    // height other than the flat corner's - a SLOPED cut chain (see
    // CylindricalFace::cap0_notch_points' own doc comment: the unequal-
    // radius cylinder/cylinder split's helical cut across a plain piece
    // at a general axis angle, whose two ends are at the two loops' pinch
    // heights). That rail corner is then the chain's own last point: the
    // trim rectangle's corner moves to its height, the rail between the
    // two corners stays the straight iso-u edge it always was, and the
    // rail-corner check below, the (u, v) conversion, the side tables and
    // the topology are untouched. Gated on the chain's last height
    // differing from the flat corner by more than the 1e-6 the rail-
    // corner check tolerates, so every face whose chain ends at the flat
    // corner within that check - every face built before sloped chains
    // existed - keeps the identical (u_max, 0) / (u_max, length) corner.
    double v_corner_u0 = 0.0, v_corner_u1 = cf.length;
    if (!cf.cap0_notch_points.empty()) {
      const double h = (cf.cap0_notch_points.back() - cf.frame.origin) * cf.frame.zaxis;
      if (std::fabs(h) > 1e-6) v_corner_u0 = h;
    }
    if (!cf.cap1_notch_points.empty()) {
      const double h = (cf.cap1_notch_points.back() - cf.frame.origin) * cf.frame.zaxis;
      if (std::fabs(h - cf.length) > 1e-6) v_corner_u1 = h;
    }
    const std::vector<Point2d> trim = {Point2d(0.0, 0.0), Point2d(u_max, v_corner_u0),
                                        Point2d(u_max, v_corner_u1), Point2d(0.0, cf.length)};

    const Point3d corner00 = surface->PointAt(0.0, 0.0);
    const Point3d corner_u0 = surface->PointAt(u_max, v_corner_u0);
    const Point3d corner_u1 = surface->PointAt(u_max, v_corner_u1);
    const Point3d corner01 = surface->PointAt(0.0, cf.length);

    // See CylindricalFace::cap0_notch_points/cap1_notch_points' own doc
    // comment: converts a dense list of 3D points already known to lie on
    // this cylinder (in fixed increasing-angle order) into their own
    // (u, v) coordinates on THIS specific surface - the exact same
    // "evaluate the REAL surface, don't trust an independently
    // reconstructed parameter" principle ConicalFace's own notch_uv lambda
    // (below) already uses, mirrored here for the cylinder case (no
    // apex-relative height recovery needed - a cylinder's true axial
    // height already equals v directly).
    auto notch_uv = [&](const std::vector<Point3d>& pts3d) {
      std::vector<Point2d> uv;
      uv.reserve(pts3d.size());
      for (size_t i = 0; i < pts3d.size(); ++i) {
        const Point3d& p = pts3d[i];
        const Vector3d d = p - cf.frame.origin;
        const double height = d * cf.frame.zaxis;
        const double x = d * cf.frame.xaxis, y = d * cf.frame.yaxis;
        double phi;
        // The first and last points of a notch sample list are REQUIRED
        // (by this field's own documented contract - see
        // CylindricalFace::cap0_notch_points' own doc comment) to sit at
        // angle exactly 0 and exactly cf.angle respectively - forced here
        // directly rather than re-derived via atan2, because atan2 cannot
        // distinguish "angle 0" from "angle 2*pi" (the SAME physical
        // direction) at all, and for a FULL 2*pi sweep specifically (the
        // only case this increment's own SplitCylindricalByObliquePlane
        // ever produces) that ambiguity is not a remote corner case but
        // the literal boundary EVERY notch touches at both its own
        // endpoints. A real, checked-directly numerical subtlety found
        // during this increment's own development (not assumed): with
        // atan2 alone, floating-point rounding at that exact boundary
        // (y coming out as a tiny negative number instead of exactly 0)
        // intermittently wrapped the FIRST point to just-under-2*pi
        // instead of 0, corrupting the cap's own visible-trim winding
        // into a self-intersecting polygon - confirmed directly by
        // reverting just this fix and reproducing the exact failure
        // (NurbsSurface::TessellateGridClippedExact's own "trim_polygon
        // must be simple" rejection) again. Every INTERIOR point (i
        // strictly between 0 and the last index) still uses the genuine
        // atan2-recovered angle, since those have no such contractual
        // anchor and are honestly the geometry's own computed angle.
        if (i == 0) {
          phi = 0.0;
        } else if (i + 1 == pts3d.size()) {
          phi = cf.angle;
        } else {
          phi = std::atan2(y, x);
          if (phi < 0.0) phi += 2.0 * ON_PI;
        }
        double u = 0.0;
        if (!circle.GetNurbFormParameterFromRadian(phi, &u)) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: a CylindricalFace's own cap "
              "notch point's angle is out of ON_Circle's own NURBS-"
              "parameterization domain");
        }
        // Checked invariant, not merely trusted: this (u, height) point,
        // evaluated back through the REAL surface, must reproduce the same
        // 3D point this whole notch is built from.
        const Point3d check = surface->PointAt(u, height);
        const double check_tol = std::max(1e-6, cf.radius * 1e-6);
        if (check.DistanceTo(p) > check_tol) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: a CylindricalFace's own cap "
              "notch point does not lie on the cylinder's own real surface "
              "within tolerance - please report this as a bug");
        }
        uv.emplace_back(u, height);
      }
      return uv;
    };

    std::vector<Point2d> cap0_interior_uv;  // notch_interior_uv[0], empty unless notched
    std::vector<Point2d> cap1_interior_uv;  // notch_interior_uv[2] (already REVERSED), empty unless notched
    double cap0_tol = 0.0, cap1_tol = 0.0;
    std::vector<Point2d> cap0_full_uv, cap1_full_uv_reversed;  // for the visible trim, below

    if (!cf.cap0_notch_points.empty()) {
      if (cf.cap0_notch_points.size() < 2) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: CylindricalFace::cap0_notch_points "
            "must have at least 2 points (the two rail corners) when non-empty");
      }
      if (cf.cap0_notch_points.front().DistanceTo(corner00) > 1e-6 ||
          cf.cap0_notch_points.back().DistanceTo(corner_u0) > 1e-6) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: CylindricalFace::cap0_notch_points's "
            "own first/last points must exactly match this face's own two rail "
            "corners at v=0 (angle 0 and angle `angle` respectively) - see that "
            "field's own doc comment for why this check already accommodates a "
            "full 2*pi sweep with no separate branch");
      }
      cap0_full_uv = notch_uv(cf.cap0_notch_points);
      cap0_interior_uv.assign(cap0_full_uv.begin() + 1, cap0_full_uv.end() - 1);
      cap0_tol = cf.cap0_notch_tolerance;
    }
    if (!cf.cap1_notch_points.empty()) {
      if (cf.cap1_notch_points.size() < 2) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: CylindricalFace::cap1_notch_points "
            "must have at least 2 points (the two rail corners) when non-empty");
      }
      if (cf.cap1_notch_points.front().DistanceTo(corner01) > 1e-6 ||
          cf.cap1_notch_points.back().DistanceTo(corner_u1) > 1e-6) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: CylindricalFace::cap1_notch_points's "
            "own first/last points must exactly match this face's own two rail "
            "corners at v=length (angle 0 and angle `angle` respectively)");
      }
      const std::vector<Point2d> full_uv = notch_uv(cf.cap1_notch_points);
      // Segment index 2 walks u_max->0 (decreasing angle) - the reverse of
      // cap1_notch_points' own fixed increasing-angle convention - so both
      // the topology-only interior list and the visible-trim splice below
      // need it reversed, exactly mirroring ConicalFace's own handling.
      cap1_full_uv_reversed.assign(full_uv.rbegin(), full_uv.rend());
      cap1_interior_uv.assign(cap1_full_uv_reversed.begin() + 1, cap1_full_uv_reversed.end() - 1);
      cap1_tol = cf.cap1_notch_tolerance;
    }

    // The VISIBLE (tessellated) trim boundary: the plain rectangle's own 4
    // corners, with either notched segment's straight corner-to-corner
    // edge replaced by its own dense chain - mirrors ConicalFace's own
    // construction exactly (see its own comment above for why each piece
    // appears exactly once).
    std::vector<Point2d> visible_trim;
    if (!cap0_full_uv.empty()) {
      visible_trim.insert(visible_trim.end(), cap0_full_uv.begin(), cap0_full_uv.end());
    } else {
      visible_trim.push_back(trim[0]);
      visible_trim.push_back(trim[1]);
    }
    visible_trim.push_back(trim[2]);
    if (!cap1_full_uv_reversed.empty()) {
      visible_trim.insert(visible_trim.end(), cap1_full_uv_reversed.begin() + 1, cap1_full_uv_reversed.end());
    } else {
      visible_trim.push_back(trim[3]);
    }
    // A face notched at BOTH ends with length == 0 (a Steinmetz eye - see
    // CylindricalFace's own doc comment) has no rails: the splice above
    // then repeats a pinch point twice in a row (cap0's last point, then
    // trim[2] at the same (u_max, 0)) and once more across the wraparound
    // (cap1's first point at (0, 0) closing onto cap0's first). A zero-
    // length polygon edge is not a shape the exact-clip tessellator's own
    // simple-polygon check is meant for, so consecutive coincident points
    // (wraparound included) are collapsed. Gated on both caps being
    // notched, and inert for the positive-length doubly-notched case too
    // (its rails keep every consecutive pair apart), so no face built
    // before eyes existed changes.
    if (!cap0_full_uv.empty() && !cap1_full_uv_reversed.empty()) {
      const double uv_tol = 1e-9 * std::max({1.0, u_max, cyl.height[1] - cyl.height[0]});
      auto same_uv = [uv_tol](const Point2d& p, const Point2d& q) {
        return std::fabs(p.x - q.x) <= uv_tol && std::fabs(p.y - q.y) <= uv_tol;
      };
      std::vector<Point2d> deduped;
      deduped.reserve(visible_trim.size());
      for (const Point2d& p : visible_trim) {
        if (!deduped.empty() && same_uv(deduped.back(), p)) continue;
        deduped.push_back(p);
      }
      while (deduped.size() > 1 && same_uv(deduped.front(), deduped.back())) deduped.pop_back();
      visible_trim = std::move(deduped);
    }

    result.face_trim_loops_.push_back(visible_trim);
    // exact_clip=true for the same reason FromPlanarFaces()'s own faces
    // use it above: this trim rectangle IS the patch's exact boundary
    // (the two straight rails at u=0/u=u_max and the two circular arcs -
    // or, once notched, an ellipse - at v=0/v=length), not an
    // approximation of one.
    result.face_exact_clip_.push_back(true);
    result.face_hole_loops_.emplace_back();
    result.face_arc_runs_.emplace_back();  // meaningless for a non-planar face
    // The literal notch rows for TessellateConforming()'s per-row strip
    // mesher (see CylinderNotchRows' own doc comment in brep.h): the
    // input's own point lists verbatim, paired with the SAME (u, v)
    // notch_uv() just computed for the visible trim above (cap1's in its
    // own increasing-angle order, i.e. the reverse of the trim splice's
    // walk), so the mesher's notch row and the trim polygon are one
    // computation, not two. Left `present == false` for the ordinary
    // un-notched face, which never reaches that mesher on this account.
    result.face_notch_rows_.emplace_back();
    if (!cf.cap0_notch_points.empty() || !cf.cap1_notch_points.empty()) {
      CylinderNotchRows& rows = result.face_notch_rows_.back();
      rows.present = true;
      rows.cap0_points = cf.cap0_notch_points;
      rows.cap0_uv = cap0_full_uv;
      rows.cap1_points = cf.cap1_notch_points;
      rows.cap1_uv.assign(cap1_full_uv_reversed.rbegin(), cap1_full_uv_reversed.rend());
      rows.v_end0 = 0.0;
      rows.v_end1 = cf.length;
    }
    // The input record itself, verbatim - notch lists, tolerances, end
    // flags and all - for MixedFaces() to hand back (see FaceRecord's own
    // comment in brep.h).
    result.face_records_.emplace_back();
    result.face_records_.back().kind = FaceRecord::kCylindrical;
    result.face_records_.back().cyl = cf;

    // Genuine topology (see BuildFaceLoop above): the same 4 corner
    // points the trim rectangle's own UV corners map to through this
    // exact surface - welded into the same global vertex space as every
    // PlanarFace loop above, which is precisely what lets a fillet's two
    // straight rails share real ON_BrepEdge objects with the adjacent
    // re-trimmed planar faces' own matching corners, with no special
    // casing (see FilletConvexEdge's own doc comment for why those rail
    // points are exact to floating-point precision, not merely close).
    FaceTopology t;
    t.trim_uv = trim;
    t.vids = {welder.Weld(corner00), welder.Weld(corner_u0), welder.Weld(corner_u1), welder.Weld(corner01)};
    t.curved_surface = surface;
    t.curved_u_max = u_max;
    t.curved_v0 = 0.0;
    t.curved_length = cf.length;
    t.notch_interior_uv.resize(4);
    t.notch_interior_uv[0] = std::move(cap0_interior_uv);
    t.notch_interior_uv[2] = std::move(cap1_interior_uv);
    t.cap_notch_tolerance.assign(4, 0.0);
    t.cap_notch_tolerance[0] = cap0_tol;
    t.cap_notch_tolerance[2] = cap1_tol;
    t.notch_midpoint_3d.assign(4, Point3d());
    if (!cf.cap0_notch_points.empty()) t.notch_midpoint_3d[0] = NotchListMidpoint(cf.cap0_notch_points);
    if (!cf.cap1_notch_points.empty()) t.notch_midpoint_3d[2] = NotchListMidpoint(cf.cap1_notch_points);
    topo.push_back(std::move(t));
  }

  for (const ConicalFace& cf : conical_faces) {
    // See ConicalFace's own doc comment for the closed-form derivation:
    // radius0/radius1 (the TRUE cone cross-section radii at the patch's
    // own two ends) and length (the TRUE axial distance between them)
    // determine the cone's own half-angle and, from there, the true
    // axial height of each end AS MEASURED FROM THE APEX (frame.origin) -
    // a third quantity neither radius0/radius1 nor length store directly,
    // recovered here by similar triangles, the same relationship
    // ON_Cone::PointAt's own construction uses (radius = tan(half_angle)
    // * height-from-apex, confirmed directly against opennurbs_cone.cpp).
    if (!(cf.length > 0.0)) {
      throw std::invalid_argument(
          "dino8::kernel::Brep::FromMixedFaces: ConicalFace::length must be "
          "strictly positive");
    }
    const double tan_half_angle = (cf.radius1 - cf.radius0) / cf.length;
    if (std::fabs(tan_half_angle) < 1e-300) {
      throw std::invalid_argument(
          "dino8::kernel::Brep::FromMixedFaces: ConicalFace::radius0 and "
          "radius1 are equal - this is a degenerate (zero half-angle) cone, "
          "i.e. genuinely a cylinder; build a CylindricalFace instead");
    }
    const double v0 = cf.radius0 / tan_half_angle;  // true height-from-apex, end 0
    const double v1 = cf.radius1 / tan_half_angle;  // true height-from-apex, end 1
    // ON_Cone::GetNurbForm builds its own NURBS surface's v-domain as
    // [0, height] (or [height, 0] if height<0 - confirmed directly against
    // opennurbs_cone.cpp/PointAt's own height>=0 branch, not assumed), so
    // whichever of v0/v1 has the LARGER magnitude is what has to be passed
    // as ON_Cone's own `height` for that domain to fully contain both -
    // the other one, closer to the apex, then lies strictly inside it.
    const bool use_v1_as_height = std::fabs(v1) >= std::fabs(v0);
    const double cone_height = use_v1_as_height ? v1 : v0;
    const double cone_radius = use_v1_as_height ? cf.radius1 : cf.radius0;

    const ON_Cone cone(cf.frame, cone_height, cone_radius);
    auto* surface = new ON_NurbsSurface();
    const int rc = cone.GetNurbForm(*surface);
    if (rc == 0) {
      delete surface;
      throw std::runtime_error(
          "dino8::kernel::Brep::FromMixedFaces: ON_Cone::GetNurbForm failed "
          "(invalid frame/height/radius)");
    }
    // Same NURBS-parameter-is-not-radian-angle correction as the cylinder
    // path above, confirmed to apply identically to a cone: ON_Cone::
    // GetNurbForm's own u-knots are a direct copy of the base circle's own
    // ON_Circle::GetNurbForm knots (verified against opennurbs_cone.cpp),
    // so the same ON_Circle::GetNurbFormParameterFromRadian conversion is
    // exact here too, not an approximation borrowed from the cylinder
    // case. Built on `cone.CircleAt(cone.height)` - the literal circle
    // GetNurbForm's own construction uses - matching MixedFaces()'s own
    // inverse conversion (see that method's own doc comment).
    const ON_Circle u_ref_circle = cone.CircleAt(cone.height);
    double u_max = 0.0;
    if (!u_ref_circle.GetNurbFormParameterFromRadian(cf.angle, &u_max)) {
      delete surface;
      throw std::invalid_argument(
          "dino8::kernel::Brep::FromMixedFaces: ConicalFace::angle is out of "
          "ON_Circle's own [0, 2*pi] NURBS-parameterization domain");
    }
    const int surface_index = brep.AddSurface(surface);
    ON_BrepFace& face = brep.NewFace(surface_index);
    // v0 < v1 always (see ConicalFace's own doc comment: the true
    // height-from-apex is monotonically increasing along +frame.zaxis by
    // construction of FilletConvexEdgeTapered's own derivation), so this
    // is the same increasing-(u, v)-parameter corner order the cylinder
    // path above uses, just offset to [v0, v1] instead of [0, length].
    face.m_bRev = !cf.outward;
    const std::vector<Point2d> trim = {Point2d(0.0, v0), Point2d(u_max, v0), Point2d(u_max, v1),
                                        Point2d(0.0, v1)};

    const Point3d corner00 = surface->PointAt(0.0, v0);
    const Point3d corner_u0 = surface->PointAt(u_max, v0);
    const Point3d corner_u1 = surface->PointAt(u_max, v1);
    const Point3d corner01 = surface->PointAt(0.0, v1);

    // See ConicalFace::cap0_notch_points/cap1_notch_points' own doc
    // comment: converts a dense list of 3D points already known to lie on
    // this cone (in fixed increasing-angle order) into their own (u, v)
    // coordinates on THIS specific surface - the same "evaluate the REAL
    // surface, don't trust an independently-reconstructed parameter"
    // principle MixedFaces()'s own cylinder/cone recovery already uses,
    // applied here in the forward (build) direction instead.
    auto notch_uv = [&](const std::vector<Point3d>& pts3d, double surface_fit_tol) {
      std::vector<Point2d> uv;
      uv.reserve(pts3d.size());
      for (size_t i = 0; i < pts3d.size(); ++i) {
        const Point3d& p = pts3d[i];
        const Vector3d d = p - cf.frame.origin;
        const double height = d * cf.frame.zaxis;
        const double x = d * cf.frame.xaxis, y = d * cf.frame.yaxis;
        // The first and last points of a notch sample list are REQUIRED
        // (cap0_notch_points/cap1_notch_points' own documented contract)
        // to sit at angle exactly 0 and exactly cf.angle respectively -
        // forced here directly rather than re-derived via atan2, exactly
        // mirroring CylindricalFace's own notch_uv fix above (see its own
        // comment for the full, checked-directly reason): atan2 cannot
        // distinguish "angle 0" from "angle 2*pi" at all, and floating-
        // point rounding at that exact boundary (y coming out as a tiny
        // negative number instead of exactly 0.0 - a real, observed
        // failure mode for a notch point that sits exactly ON this cone's
        // own xaxis, e.g. a multi-station interior-join's own borrowed
        // v1 circle sample at phi=0, not merely a theoretical corner
        // case) can wrap the FIRST point to just-under-2*pi instead of 0,
        // corrupting this cap's own visible-trim winding into a
        // self-intersecting polygon. Every INTERIOR point still uses the
        // genuine atan2-recovered angle, since those have no such
        // contractual anchor.
        double phi;
        if (i == 0) {
          phi = 0.0;
        } else if (i + 1 == pts3d.size()) {
          phi = cf.angle;
        } else {
          phi = std::atan2(y, x);
          if (phi < 0.0) phi += 2.0 * ON_PI;
        }
        double u = 0.0;
        if (!u_ref_circle.GetNurbFormParameterFromRadian(phi, &u)) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: a ConicalFace's own cap "
              "notch point's angle is out of ON_Circle's own NURBS-"
              "parameterization domain");
        }
        // Checked invariant, not merely trusted: this (u, height) point,
        // evaluated back through the REAL surface, must reproduce the
        // same 3D point this whole notch is built from - within the
        // ordinary near-zero bound for every notch this kernel has ever
        // built before (see ConicalFace::cap0_surface_fit_tolerance/
        // cap1_surface_fit_tolerance's own doc comment), or within that
        // genuinely computed, larger, non-shrinking bound for exactly the
        // one new provenance (an interior taper-station join) that field
        // exists for.
        const Point3d check = surface->PointAt(u, height);
        const double tol = std::max({1e-6, (cf.radius0 + cf.radius1) * 1e-6, surface_fit_tol});
        if (check.DistanceTo(p) > tol) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: a ConicalFace's own cap "
              "notch point does not lie on the cone's own real surface "
              "within tolerance - please report this as a bug");
        }
        uv.emplace_back(u, height);
      }
      return uv;
    };

    std::vector<Point2d> cap0_interior_uv;  // notch_interior_uv[0], empty unless notched
    std::vector<Point2d> cap1_interior_uv;  // notch_interior_uv[2] (already REVERSED), empty unless notched
    double cap0_tol = 0.0, cap1_tol = 0.0;
    std::vector<Point2d> cap0_full_uv, cap1_full_uv_reversed;  // for the visible trim, below

    if (!cf.cap0_notch_points.empty()) {
      if (cf.cap0_notch_points.size() < 2) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: ConicalFace::cap0_notch_points "
            "must have at least 2 points (the two rail corners) when non-empty");
      }
      if (cf.cap0_notch_points.front().DistanceTo(corner00) > 1e-6 ||
          cf.cap0_notch_points.back().DistanceTo(corner_u0) > 1e-6) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: ConicalFace::cap0_notch_points's "
            "own first/last points must exactly match this face's own two rail "
            "corners at v0 (angle 0 and angle `angle` respectively)");
      }
      cap0_full_uv = notch_uv(cf.cap0_notch_points, cf.cap0_surface_fit_tolerance);
      cap0_interior_uv.assign(cap0_full_uv.begin() + 1, cap0_full_uv.end() - 1);
      cap0_tol = cf.cap0_notch_tolerance;
    }
    if (!cf.cap1_notch_points.empty()) {
      if (cf.cap1_notch_points.size() < 2) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: ConicalFace::cap1_notch_points "
            "must have at least 2 points (the two rail corners) when non-empty");
      }
      if (cf.cap1_notch_points.front().DistanceTo(corner01) > 1e-6 ||
          cf.cap1_notch_points.back().DistanceTo(corner_u1) > 1e-6) {
        throw std::invalid_argument(
            "dino8::kernel::Brep::FromMixedFaces: ConicalFace::cap1_notch_points's "
            "own first/last points must exactly match this face's own two rail "
            "corners at v1 (angle 0 and angle `angle` respectively)");
      }
      const std::vector<Point2d> full_uv = notch_uv(cf.cap1_notch_points, cf.cap1_surface_fit_tolerance);
      // Segment index 2 walks u_max->0 (j->i, decreasing angle) - the
      // REVERSE of cap1_notch_points' own fixed i->j convention (see that
      // field's own doc comment) - so both the topology-only interior list
      // and the visible-trim splice below need it reversed.
      cap1_full_uv_reversed.assign(full_uv.rbegin(), full_uv.rend());
      cap1_interior_uv.assign(cap1_full_uv_reversed.begin() + 1, cap1_full_uv_reversed.end() - 1);
      cap1_tol = cf.cap1_notch_tolerance;
    }

    // The VISIBLE (tessellated) trim boundary: the plain rectangle's own 4
    // corners, with either notched segment's straight corner-to-corner
    // edge replaced by its own dense chain - this is what actually changes
    // this face's own TESSELLATED shape (see ConicalFace::cap0_notch_points'
    // own doc comment), independent of the topology-only collapse above.
    std::vector<Point2d> visible_trim;
    if (!cap0_full_uv.empty()) {
      visible_trim.insert(visible_trim.end(), cap0_full_uv.begin(), cap0_full_uv.end());
    } else {
      visible_trim.push_back(trim[0]);
      visible_trim.push_back(trim[1]);
    }
    // trim[2] (corner_u1) always comes next, whether from the straight
    // rail-j segment (B: c1->c2) landing there or as cap0's own chain's
    // implicit successor - either way it's the start of segment C
    // (c2->c3), so it's added exactly once here; cap1_full_uv_reversed's
    // OWN first point is that same corner_u1 (see above), so it's skipped
    // (begin() + 1) to avoid duplicating it.
    visible_trim.push_back(trim[2]);
    if (!cap1_full_uv_reversed.empty()) {
      visible_trim.insert(visible_trim.end(), cap1_full_uv_reversed.begin() + 1, cap1_full_uv_reversed.end());
    } else {
      visible_trim.push_back(trim[3]);
    }

    result.face_trim_loops_.push_back(visible_trim);
    result.face_exact_clip_.push_back(true);
    result.face_hole_loops_.emplace_back();
    result.face_arc_runs_.emplace_back();  // meaningless for a non-planar face
    result.face_notch_rows_.emplace_back();  // a cone never reaches the cylinder strip mesher
    // Deliberately NO verbatim record for a cone: MixedFaces() keeps the
    // geometric extraction (notches come back empty) by documented
    // contract - see that method's own doc comment in brep.h.
    result.face_records_.emplace_back();

    FaceTopology t;
    t.trim_uv = trim;
    t.vids = {welder.Weld(corner00), welder.Weld(corner_u0), welder.Weld(corner_u1), welder.Weld(corner01)};
    t.curved_surface = surface;
    t.curved_u_max = u_max;
    t.curved_v0 = v0;
    t.curved_length = v1 - v0;
    t.notch_interior_uv.resize(4);
    t.notch_interior_uv[0] = std::move(cap0_interior_uv);
    t.notch_interior_uv[2] = std::move(cap1_interior_uv);
    t.cap_notch_tolerance.assign(4, 0.0);
    t.cap_notch_tolerance[0] = cap0_tol;
    t.cap_notch_tolerance[2] = cap1_tol;
    t.notch_midpoint_3d.assign(4, Point3d());
    if (!cf.cap0_notch_points.empty()) t.notch_midpoint_3d[0] = NotchListMidpoint(cf.cap0_notch_points);
    if (!cf.cap1_notch_points.empty()) t.notch_midpoint_3d[2] = NotchListMidpoint(cf.cap1_notch_points);
    topo.push_back(std::move(t));
  }

  // Pass 2 (see this feature's own spec): materialize one real
  // ON_BrepVertex per canonical welded point, in weld-id order - `brep`
  // starts with an empty m_V, and NewVertex() always appends at the next
  // index, so this makes brep.m_V's own indices exactly match every
  // `vids` entry recorded above.
  for (const Point3d& p : welder.Points()) brep.NewVertex(p);

  // Passes 3 + 4: one genuine ON_BrepLoop plus its edges/trims per face,
  // sharing an edge automatically wherever two faces' own welded vertex
  // pairs match (see BuildFaceLoop's own doc comment for exactly how).
  //
  // Visited in TWO passes - every CylindricalFace's/ConicalFace's own topo
  // entry first, then every PlanarFace's - rather than one pass in
  // `topo`'s own (planar-then-cylindrical-then-conical) order. `brep.m_F`'s
  // own face indices are completely unaffected (those were already fixed
  // by Pass 1's own NewFace() call order above; this only changes which
  // face's BuildFaceLoop call runs first for a given shared vertex pair).
  // This is what guarantees a CylindricalFace's own true-arc cap edge
  // always exists BEFORE a PlanarFace's own collapsed corner-notch segment
  // (see PlanarFace::notch_begin/notch_count's own doc comment) could
  // reach the same welded vertex pair: whichever face's BuildFaceLoop call
  // visits a vertex pair FIRST wins the right to build that edge's real
  // 3D curve, and the second visitor merely reuses it - so curved faces
  // must go first for a notch to ever get the arc, not a straight line.
  // (A ConicalFace's own two cap arcs are never notch-shared in v1 - see
  // FilletConvexEdgeTapered's own doc comment for why - but visiting it in
  // this same first pass is still correct and harmless: nothing else ever
  // reaches its own welded vertex pairs first regardless.) Straight-rail
  // sharing (fillet.h) is completely unaffected by this reordering,
  // exactly as it was unaffected by which of the two ORIGINAL pass-1 loops
  // (planar, cylindrical) ran first: a straight ON_LineCurve between the
  // same two points is identical regardless of which face happens to
  // build it.
  std::unordered_map<uint64_t, int> edge_of_vertex_pair;
  // Records, for every edge BuildFaceLoop() creates via a plain
  // (non-notched) is_cap segment, the physical midpoint of the arc it was
  // built from - see BuildFaceLoop's own doc comment on this map's one
  // use: disambiguating a genuinely different arc from a legitimately
  // shared one when both reduce to the same 2 endpoint vertices.
  std::unordered_map<int, Point3d> cap_arc_midpoint_of_edge;
  // Records, per plain vertex-pair key, every edge BuildFaceLoop() creates
  // via a NOTCHED cap segment (polyline midpoint + the key it was stored
  // under) - see BuildFaceLoop's own doc comment for its one use: telling
  // apart genuinely different notch polylines that join the same two
  // vertices, without touching how any other kind of segment matches.
  std::unordered_map<uint64_t, std::vector<std::pair<Point3d, uint64_t>>> notched_edges_of_vertex_pair;
  for (size_t fi = 0; fi < topo.size(); ++fi) {
    if (topo[fi].curved_surface != nullptr) {
      BuildFaceLoop(brep, brep.m_F[static_cast<int>(fi)], topo[fi], edge_of_vertex_pair, cap_arc_midpoint_of_edge,
                    notched_edges_of_vertex_pair);
    }
  }
  for (size_t fi = 0; fi < topo.size(); ++fi) {
    if (topo[fi].curved_surface == nullptr) {
      BuildFaceLoop(brep, brep.m_F[static_cast<int>(fi)], topo[fi], edge_of_vertex_pair, cap_arc_midpoint_of_edge,
                    notched_edges_of_vertex_pair);
    }
  }

  // Pass 5: NewVertex()/NewEdge()/NewTrim() above all leave m_tolerance at
  // ON_UNSET_VALUE - a sentinel ON_Brep::IsValid() rejects outright - so
  // this real geometry-derived tolerance/flag pass (replacing the old
  // bare SetTrimIsoFlags() call FromSurface()/Box()/Sphere()/
  // TrimmedPlanarFace() still use, since they don't build this topology)
  // is required here, not optional polish. bLazy=true so this leaves
  // BuildFaceLoop's own already-correct edge.m_tolerance=0.0 alone
  // (see its own comment for why: the public build's SetEdgeTolerance is
  // a stub that would otherwise reset it right back to unset) while still
  // genuinely computing every vertex/trim tolerance, loop type, iso flag,
  // and trim bounding box left at their own NewVertex()/NewTrim() sentinel.
  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  return result;
}

Brep Brep::FromPlanarFaces(const std::vector<Brep::PlanarFace>& faces) {
  return FromMixedFaces(faces, {});
}

Brep Brep::Compound(const std::vector<Brep>& lumps) {
  Brep result;
  for (const Brep& lump : lumps) {
    const int lump_faces = lump.brep_.m_F.Count();
    if (lump_faces == 0) continue;  // the empty set: contributes nothing (see brep.h)
    const size_t n = static_cast<size_t>(lump_faces);
    if (lump.face_trim_loops_.size() != n || lump.face_exact_clip_.size() != n ||
        lump.face_hole_loops_.size() != n || lump.face_arc_runs_.size() != n ||
        lump.face_notch_rows_.size() != n || lump.face_records_.size() != n) {
      throw std::invalid_argument(
          "dino8::kernel::Brep::Compound: a lump's face side tables are not in "
          "lockstep with its faces (a raw()-assigned ON_Brep, not a Brep this "
          "class's own factories built) - refused rather than letting a "
          "following lump's tables slide onto its faces; see Compound's own "
          "doc comment in brep.h");
    }
    // Record this lump's face range(s) BEFORE appending, offset by the
    // faces already present; a lump that is itself a compound flattens.
    const int offset = result.brep_.m_F.Count();
    for (const std::pair<int, int>& range : lump.LumpFaceRanges()) {
      result.lump_face_ranges_.emplace_back(offset + range.first, offset + range.second);
    }
    // "appends a copy of brep to this and updates indices of appended
    // brep parts. Duplicates are not removed." (opennurbs_brep.h) - the
    // lumps stay unwelded, by design (see brep.h).
    result.brep_.Append(lump.brep_);
    auto concatenate = [](auto& dst, const auto& src) { dst.insert(dst.end(), src.begin(), src.end()); };
    concatenate(result.face_trim_loops_, lump.face_trim_loops_);
    concatenate(result.face_exact_clip_, lump.face_exact_clip_);
    concatenate(result.face_hole_loops_, lump.face_hole_loops_);
    concatenate(result.face_arc_runs_, lump.face_arc_runs_);
    concatenate(result.face_notch_rows_, lump.face_notch_rows_);
    concatenate(result.face_records_, lump.face_records_);
  }
  return result;
}

std::vector<std::pair<int, int>> Brep::LumpFaceRanges() const {
  const int face_count = brep_.m_F.Count();
  if (!lump_face_ranges_.empty()) {
    // Self-check (see brep.h): the recorded ranges must tile
    // [0, face_count) exactly, else the record is stale and this is one
    // lump for every purpose.
    bool consistent = lump_face_ranges_.front().first == 0 && lump_face_ranges_.back().second == face_count;
    for (size_t k = 0; consistent && k < lump_face_ranges_.size(); ++k) {
      const std::pair<int, int>& range = lump_face_ranges_[k];
      consistent = range.first <= range.second && (k == 0 || lump_face_ranges_[k - 1].second == range.first);
    }
    if (consistent) return lump_face_ranges_;
  }
  return {{0, face_count}};
}

BoundingBox Brep::GetTightBoundingBox() const {
  ON_BoundingBox box;
  if (!brep_.GetTightBoundingBox(box)) {
    throw std::runtime_error(
        "dino8::kernel::Brep::GetTightBoundingBox: ON_Brep::"
        "GetTightBoundingBox failed");
  }
  return BoundingBox{box.Min(), box.Max()};
}

std::vector<Mesh> Brep::TessellateAdaptive(double chord_tolerance) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGridAdaptive(chord_tolerance));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExactAdaptive(chord_tolerance, fg.outer));
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGridAdaptive(chord_tolerance, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMeshAdaptive(double chord_tolerance) const {
  return Mesh::MergeAndWeld(TessellateAdaptive(chord_tolerance));
}

std::vector<Mesh> Brep::TessellateNonUniformAdaptive(double chord_tolerance) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGridNonUniformAdaptive(chord_tolerance));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExactAdaptive(chord_tolerance, fg.outer));
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGridNonUniformAdaptive(chord_tolerance, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMeshNonUniformAdaptive(double chord_tolerance) const {
  return Mesh::MergeAndWeld(TessellateNonUniformAdaptive(chord_tolerance));
}

namespace {

// A run of a wedge PlanarFace's own trim loop being substituted with a
// fresh set of replacement points - the same "begin/count consecutive
// original indices, wrapping around the loop's own start/end if needed"
// shape as PlanarFace::ArcRun (see its own doc comment), but decoupled
// from that struct so BuildResampledWedgeLoop/BuildConformingWedgeMesh
// (below) can host BOTH an arc substitution (the curved-boundary fix)
// AND a straight-edge substitution (this one, see
// TessellateConforming()'s own doc comment for the planar/planar
// perimeter gap this closes) without either mechanism knowing about the
// other's own source struct.
struct SubRange {
  int begin = 0;
  int count = 0;
};

// One shared boundary sample set: `points` is the LITERAL
// detail::ArcSchedule3d() output computed from a wedge PlanarFace's own
// PlanarFace::ArcRun - used verbatim (never re-derived, never
// re-evaluated through a NURBS surface) as BOTH the wedge's own
// substituted boundary vertices AND the matching row of the adjacent
// CylindricalFace's own tessellated grid. This identity - literally the
// same std::vector<Point3d>, not a second, independently-computed
// approximation of it - is the actual mechanism that makes
// Brep::TessellateConforming()'s two sides share their boundary exactly,
// not just closely; see that method's own doc comment.
struct ConformingMatch {
  size_t run_index = 0;
  int cyl_face_index = 0;
  bool at_v0 = false;  // true: the cylindrical face's own v=0 end; false: v=length
  std::vector<double> raw_u;    // the cylindrical face's own raw NURBS-u per shared point (grid layout only)
  std::vector<Point3d> points;  // == detail::ArcSchedule3d(...) - the actual shared vertex positions
};

// True when a wedge's own recorded arc (center/radius/plane-normal)
// physically matches `cf`'s own lateral surface - the "which
// CylindricalFace does this arc_run belong to" test
// Brep::TessellateConforming() needs (see that method's own doc
// comment). Reuses boolean.cpp's own same_plane-style pattern (a
// distance-to-axis-line check plus a radius check plus a normal-
// alignment check), independently re-derived here rather than shared
// across translation units for a three-line predicate.
bool SameCircleAsCylinder(const Point3d& center, double radius, const Vector3d& normal,
                           const Brep::CylindricalFace& cf, double tol) {
  const double rtol = std::max(tol, cf.radius * 1e-6);
  const Vector3d d = center - cf.frame.origin;
  const double height = ON_DotProduct(d, cf.frame.zaxis);
  const Point3d axis_point = cf.frame.origin + height * cf.frame.zaxis;
  if (center.DistanceTo(axis_point) > rtol) return false;
  if (std::fabs(radius - cf.radius) > rtol) return false;
  const double align = std::fabs(ON_DotProduct(normal, cf.frame.zaxis));
  if (align < 1.0 - 1e-6) return false;
  return true;
}

// True when two CylindricalFace records describe the SAME physical
// wedge of the SAME physical cylinder - same axis line, radius, u=0
// angular reference direction (`frame.xaxis`), and angular sweep - as
// opposed to SameCircleAsCylinder above, which only identifies "the same
// physical circle" (axis + radius + normal) and is deliberately blind to
// angular reference and sweep, since the arc-matching pass above needs
// exactly that laxer test to pair a cap's own arc against ANY
// axially-adjacent wall fragment, regardless of that fragment's own
// wedge. This stricter test is for a DIFFERENT job (see
// BuildConformingCylinderMesh's own dispatch site below): reusing one
// wedge's OWN axially-adjacent, already-`cyl_matches`-populated sibling's
// raw-u breakpoints as another, "friendless" sibling's own fallback
// schedule requires the SAME angular reference, not merely the same
// circle - two DIFFERENT wedges of one physical cylinder (e.g. this
// file's own parallel-cylinder-Difference fixture's A-outer vs. A-inner
// wedge) generally have DIFFERENT `frame.xaxis` directions, so reusing
// one wedge's raw-u values for the other would silently apply the wrong
// angular reference and reproduce exactly the seam this is meant to fix.
// `SplitCylindricalByOtherCylinderAxialExtent` (boolean.cpp) is confirmed
// (by direct inspection of its own implementation) to copy an axial
// sibling's `frame`/`angle`/`radius` verbatim, only ever trimming
// `length` and shifting `frame.origin` along the shared axis - so two
// genuine axial siblings of one wedge always satisfy this predicate
// (mirroring the SAME re-derivation `ExtractCylindricalFace` performs
// per fragment, which reproduces that same frame up to ordinary
// floating-point fit noise), while two different wedges of the same
// cylinder never do.
// True when a face's outer trim polygon, in its own (u, v) domain, is a
// plain axis-aligned rectangle: every vertex lies on the polygon's own
// bounding box boundary (extra collinear vertices along a side are
// fine). This is the exact precondition BuildConformingCylinderMesh's
// tensor grid relies on - it grids the trim's full (u, v) bounding box
// and never clips to the polygon - so it is what gates that mesher's
// use for a "friendless" cylindrical band (see TessellateConforming()'s
// own dispatch below). A NOTCHED CylindricalFace (cap0_notch_points/
// cap1_notch_points, e.g. the oblique path's own surviving hole-wall
// fragment, whose only ends are ellipse notches with no cap anywhere)
// has genuinely interior trim vertices and must keep the trim-clipping
// path, or its notch would be silently filled back in.
bool IsRectangularTrimUv(const std::vector<Point2d>& outer) {
  if (outer.size() < 4) return false;
  double u_min = outer[0].x, u_max = outer[0].x, v_min = outer[0].y, v_max = outer[0].y;
  for (const Point2d& p : outer) {
    u_min = std::min(u_min, p.x);
    u_max = std::max(u_max, p.x);
    v_min = std::min(v_min, p.y);
    v_max = std::max(v_max, p.y);
  }
  const double tol_u = 1e-9 * (1.0 + std::fabs(u_min) + std::fabs(u_max));
  const double tol_v = 1e-9 * (1.0 + std::fabs(v_min) + std::fabs(v_max));
  for (const Point2d& p : outer) {
    const bool on_u_side = std::fabs(p.x - u_min) <= tol_u || std::fabs(p.x - u_max) <= tol_u;
    const bool on_v_side = std::fabs(p.y - v_min) <= tol_v || std::fabs(p.y - v_max) <= tol_v;
    if (!on_u_side && !on_v_side) return false;
  }
  return true;
}

bool SameWedgeAsCylinder(const Brep::CylindricalFace& a, const Brep::CylindricalFace& b, double tol) {
  const double rtol = std::max(tol, a.radius * 1e-6);
  const Vector3d d = b.frame.origin - a.frame.origin;
  const double height = ON_DotProduct(d, a.frame.zaxis);
  const Point3d axis_point = a.frame.origin + height * a.frame.zaxis;
  if (b.frame.origin.DistanceTo(axis_point) > rtol) return false;
  if (std::fabs(a.radius - b.radius) > rtol) return false;
  if (ON_DotProduct(a.frame.zaxis, b.frame.zaxis) < 1.0 - 1e-6) return false;
  if (ON_DotProduct(a.frame.xaxis, b.frame.xaxis) < 1.0 - 1e-6) return false;
  if (std::fabs(a.angle - b.angle) > 1e-6) return false;
  return true;
}

// Rebuilds a wedge PlanarFace's own trim polygon (`trim_uv`, on `wrapper`
// - the SAME bilinear surface Tessellate() already trims to) as a 3D
// polygon with every SubRange in `subs` substituted for its own fresh
// replacement points, and every OTHER vertex copied verbatim (via
// `wrapper.PointAt`, the same evaluation Tessellate() itself already
// performs for this face's boundary). Each `subs` entry pairs a
// SubRange (which consecutive original indices it replaces - see
// SubRange's own doc comment for why that range may wrap around this
// loop's own start/end) with its own fresh replacement points; a
// SubRange's own source is irrelevant here - it may come from a
// PlanarFace::ArcRun (the curved-boundary fix) or from a straight-edge
// match against an adjacent plain quad face (this one), and both kinds
// may coexist in the same `subs` list for the same wedge.
//
// Walking `trim_uv`'s own indices in order and either skipping (a point
// strictly inside some range's own span), substituting (that range's
// full replacement, emitted once at the range's own `begin` index), or
// copying verbatim reproduces the SAME cyclic polygon (same winding,
// same overall shape) as the original loop, merely rotated to start
// wherever index 0 happens to fall relative to the ranges - a rotation
// of a closed cyclic polygon changes nothing about the shape or winding
// it represents. Two ADJACENT ranges that share an original endpoint
// (e.g. a wedge's own two straight rails on either side of one box
// corner, each independently matched against a different wall) would
// otherwise each emit that shared point once, producing a duplicate,
// zero-length-edge vertex; the caller is responsible for dropping the
// later range's own leading point in that case (see
// TessellateConforming()'s own straight-edge matching pass) - this
// function itself does no such deduplication, since it has no way to
// tell a genuine duplicate from two ranges that happen to share a
// numeric position coincidentally.
std::vector<Point3d> BuildResampledWedgeLoop(const NurbsSurface& wrapper, const std::vector<Point2d>& trim_uv,
                                              const std::vector<std::pair<SubRange, std::vector<Point3d>>>& subs) {
  const size_t n = trim_uv.size();
  std::vector<bool> excluded(n, false);
  std::unordered_map<size_t, const std::vector<Point3d>*> insert_at;
  for (const auto& sub : subs) {
    const size_t begin = static_cast<size_t>(sub.first.begin);
    const size_t count = static_cast<size_t>(sub.first.count);
    for (size_t k = 0; k < count && k < n; ++k) excluded[(begin + k) % n] = true;
    insert_at[begin] = &sub.second;
  }
  std::vector<Point3d> result;
  result.reserve(n + 64);
  for (size_t idx = 0; idx < n; ++idx) {
    const auto it = insert_at.find(idx);
    if (it != insert_at.end()) {
      for (const Point3d& p : *it->second) result.push_back(p);
      continue;
    }
    if (excluded[idx]) continue;
    result.push_back(wrapper.PointAt(trim_uv[idx].x, trim_uv[idx].y));
  }
  return result;
}

// Triangulates a wedge PlanarFace's own resampled boundary (see
// BuildResampledWedgeLoop above) via the existing, proven, boundary-only
// detail::EarClipTriangulate - the same triangulator RepresentativeInteriorPoint()
// and NurbsSurface's own concave exact-clip path already rely on
// elsewhere in this kernel. Projects into a LOCAL 2D basis derived via
// Newell's method from the resampled loop itself (NOT the run's own
// stored plane_xaxis/plane_yaxis - any consistent orthonormal basis of
// the same plane triangulates identically; reusing NewellNormal here,
// the same helper ExtractPlanarFace already uses, keeps this independent
// of how many arc_runs a given face has, including zero).
Mesh BuildConformingWedgeMesh(const NurbsSurface& wrapper, const std::vector<Point2d>& trim_uv,
                               const std::vector<std::pair<SubRange, std::vector<Point3d>>>& subs) {
  const std::vector<Point3d> loop3d = BuildResampledWedgeLoop(wrapper, trim_uv, subs);
  Mesh mesh;
  if (loop3d.size() < 3) return mesh;
  const Vector3d normal = NewellNormal(loop3d);
  const ON_Plane proj_plane(loop3d[0], normal);
  std::vector<Point2d> loop2d;
  loop2d.reserve(loop3d.size());
  for (const Point3d& p : loop3d) {
    const Vector3d d = p - proj_plane.origin;
    loop2d.emplace_back(ON_DotProduct(d, proj_plane.xaxis), ON_DotProduct(d, proj_plane.yaxis));
  }
  ON_Mesh& raw = mesh.raw();
  raw.m_V.Reserve(static_cast<int>(loop3d.size()));
  for (const Point3d& p : loop3d) raw.m_V.Append(ON_3fPoint(p));
  for (const std::array<int, 3>& tri : dino8::kernel::detail::EarClipTriangulate(loop2d)) {
    ON_MeshFace face;
    face.vi[0] = tri[0];
    face.vi[1] = tri[1];
    face.vi[2] = tri[2];
    face.vi[3] = tri[2];
    raw.m_F.Append(face);
  }
  return mesh;
}

// Builds a CylindricalFace's own tensor-product (u, v) mesh with the
// EXACT shared points from every ConformingMatch injected at their own
// (matched u breakpoint, v=0-or-length row) grid position - every other
// vertex still comes from evaluating the real NURBS surface via
// `wrapper.PointAt`, exactly as Tessellate() already does. This is a
// bespoke tensor-grid assembly (not a call to
// NurbsSurface::TessellateGridNonUniform()) specifically so the shared-
// boundary vertices are the LITERAL Point3d values
// Brep::TessellateConforming() already computed for the wedge side - a
// plain "pass matching u_values into TessellateGridNonUniform" would
// still independently re-evaluate the surface at those parameters,
// which is NOT guaranteed bit-identical to a wedge's own closed-form
// detail::ArcSchedule3d() point even when both represent the same
// physical point to full floating-point precision (a rational NURBS
// surface evaluation and a direct trig formula are different
// computations) - seeTessellateConforming()'s own doc comment for why
// bit-identical (not merely close) boundary vertices is the actual
// point of this whole mechanism.
// LAST-RESORT angle-uniform raw-NURBS-u breakpoint schedule for a
// CylindricalFace's own FULL [0, cf.angle] sweep, used ONLY when a
// "friendless" axial band (see BuildConformingCylinderMesh's own dispatch
// site below) has NO already-`cyl_matches`-populated sibling of the SAME
// wedge (per SameWedgeAsCylinder above) to borrow real breakpoints from -
// an edge case that should not arise for any boolean-produced middle
// band in this kernel today (every such band has at least one capped
// axial sibling somewhere in the result), but is kept as a defensive
// fallback so a face this branch cannot fully reconcile still gets SOME
// angle-uniform schedule (better than NurbsSurface::TessellateGrid's own
// raw-u-uniform one) rather than silently regressing to today's bug the
// moment that assumption is ever violated. IMPORTANT: unlike reusing a
// real sibling's own `raw_u` (see below), this does NOT generally
// reproduce the same breakpoints a capped sibling's own ArcRun match(es)
// would compute - a cap built as several quadrant-sized ArcRuns (see
// BuildEndCap's own "always 4 quadrant pieces" convention) samples EACH
// quadrun uniformly across only ITS OWN angular sub-range, not the
// wedge's full sweep in one pass, so the resulting breakpoint set is
// generally NOT the same as sampling `sample_count` steps uniformly
// across the ENTIRE [0, cf.angle] sweep in one go. This helper exists
// purely as a not-worse-than-before safety net for that no-sibling edge
// case, not as the mechanism that actually closes the seam.
std::vector<double> CanonicalCylinderUBreakpoints(const Brep::CylindricalFace& cf, int sample_count) {
  const ON_Circle ref_circle(cf.frame, cf.radius);
  std::vector<double> raw_u(static_cast<size_t>(std::max(sample_count, 1)) + 1);
  for (int s = 0; s <= sample_count; ++s) {
    const double t = static_cast<double>(s) / static_cast<double>(std::max(sample_count, 1));
    const double theta = cf.angle * t;
    double u = 0.0;
    if (!ref_circle.GetNurbFormParameterFromRadian(theta, &u)) {
      throw std::runtime_error(
          "dino8::kernel::Brep::TessellateConforming: ON_Circle::GetNurbFormParameterFromRadian failed "
          "converting a cylindrical face's own canonical angle-uniform breakpoint to raw NURBS-u");
    }
    raw_u[static_cast<size_t>(s)] = u;
  }
  return raw_u;
}

// `fallback_u_breakpoints`, when non-empty, seeds a set of EXTRA u
// breakpoints (no forced 3D point at either row, unlike a genuine
// ConformingMatch - see `matches` below) into the same `add_break`
// dedup-by-`u_tol` machinery `matches` already feeds. It exists for
// exactly one caller (see the dispatch site in TessellateConforming()
// below): a CylindricalFace fragment with ZERO entries in `matches` at
// all (a "friendless" middle axial band produced by splitting a cylinder
// against another cylinder - see boolean.h's own disclosure of this gap)
// - such a fragment previously fell through to a plain, raw-u-uniform
// grid (NurbsSurface::TessellateGrid) at BOTH its v=0 and v=length rows,
// which samples genuinely different physical angular locations than an
// axially-adjacent, ArcRun-matched sibling's own angle-uniform
// breakpoints along the SAME shared circle - an unwelded seam at every
// interior column. The caller populates this by directly reusing an
// axially-adjacent, already-matched sibling's OWN `raw_u` values (the
// SAME `ConformingMatch::raw_u` this function's own `matches` parameter
// already forces breakpoints from, just gathered from a DIFFERENT face's
// entry in `cyl_matches`, identified via SameWedgeAsCylinder as sharing
// this face's own wedge) - not an independently re-derived schedule -
// since a cap's own ArcRun match set can span SEVERAL quadrant-sized
// runs (see BuildEndCap's own "always 4 quadrant pieces" convention),
// each sampled uniformly only across ITS OWN angular sub-range, and only
// the sibling's own actual accumulated breakpoint set (not a fresh
// single uniform-over-the-full-sweep resampling - see
// CanonicalCylinderUBreakpoints' own doc comment for why that
// alternative is NOT equivalent) is guaranteed to reproduce the exact
// values that sibling's own shared boundary row already committed to. An
// empty `fallback_u_breakpoints` (the default for every pre-existing
// caller) is a complete no-op in the loop below.
Mesh BuildConformingCylinderMesh(const NurbsSurface& wrapper, const std::vector<Point2d>& trim_uv, int u_divisions,
                                  int v_divisions, const std::vector<ConformingMatch>& matches,
                                  const std::vector<double>& fallback_u_breakpoints = {}) {
  double u_min = trim_uv[0].x, u_max = trim_uv[0].x, v_min = trim_uv[0].y, v_max = trim_uv[0].y;
  for (const Point2d& p : trim_uv) {
    u_min = std::min(u_min, p.x);
    u_max = std::max(u_max, p.x);
    v_min = std::min(v_min, p.y);
    v_max = std::max(v_max, p.y);
  }
  const double u_range = std::max(u_max - u_min, 1e-300);
  const double u_tol = std::max(1e-12, u_range * 1e-9);

  struct UBreak {
    double u = 0.0;
    const Point3d* v0_point = nullptr;
    const Point3d* v1_point = nullptr;
  };
  std::vector<UBreak> breaks;
  auto add_break = [&](double u, const Point3d* v0p, const Point3d* v1p) {
    for (UBreak& b : breaks) {
      if (std::fabs(b.u - u) <= u_tol) {
        if (v0p) b.v0_point = v0p;
        if (v1p) b.v1_point = v1p;
        return;
      }
    }
    UBreak b;
    b.u = u;
    b.v0_point = v0p;
    b.v1_point = v1p;
    breaks.push_back(b);
  };
  add_break(u_min, nullptr, nullptr);
  add_break(u_max, nullptr, nullptr);
  for (double u : fallback_u_breakpoints) add_break(u, nullptr, nullptr);
  for (const ConformingMatch& m : matches) {
    for (size_t s = 0; s < m.raw_u.size(); ++s) {
      add_break(m.raw_u[s], m.at_v0 ? &m.points[s] : nullptr, m.at_v0 ? nullptr : &m.points[s]);
    }
  }
  std::sort(breaks.begin(), breaks.end(), [](const UBreak& a, const UBreak& b) { return a.u < b.u; });

  // Fill remaining interior u breakpoints uniformly wherever the
  // boundary-derived breakpoints above leave a gap wider than roughly
  // what u_divisions alone would have produced - see
  // TessellateConforming()'s own doc comment.
  const double target_spacing = u_range / static_cast<double>(std::max(u_divisions, 1));
  std::vector<UBreak> filled;
  filled.reserve(breaks.size() * 2);
  for (size_t i = 0; i + 1 < breaks.size(); ++i) {
    filled.push_back(breaks[i]);
    const double gap = breaks[i + 1].u - breaks[i].u;
    if (gap > target_spacing * 1.5) {
      const int extra = static_cast<int>(std::ceil(gap / target_spacing)) - 1;
      for (int e = 1; e <= extra; ++e) {
        UBreak b;
        b.u = breaks[i].u + gap * static_cast<double>(e) / static_cast<double>(extra + 1);
        filled.push_back(b);
      }
    }
  }
  if (!breaks.empty()) filled.push_back(breaks.back());

  std::vector<double> v_values(static_cast<size_t>(v_divisions) + 1);
  for (int j = 0; j <= v_divisions; ++j) {
    v_values[static_cast<size_t>(j)] = v_min + (v_max - v_min) * static_cast<double>(j) / static_cast<double>(v_divisions);
  }
  const double v_tol = std::max(1e-12, (v_max - v_min) * 1e-9);

  Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  const size_t u_points = filled.size();
  const size_t v_points = v_values.size();
  raw.m_V.Reserve(static_cast<int>(u_points * v_points));
  auto grid_index = [v_points](size_t i, size_t j) { return static_cast<int>(i * v_points + j); };
  for (size_t i = 0; i < u_points; ++i) {
    for (size_t j = 0; j < v_points; ++j) {
      const bool is_v0_row = std::fabs(v_values[j] - v_min) <= v_tol;
      const bool is_v1_row = std::fabs(v_values[j] - v_max) <= v_tol;
      const Point3d* forced = is_v0_row ? filled[i].v0_point : (is_v1_row ? filled[i].v1_point : nullptr);
      const Point3d p = forced != nullptr ? *forced : wrapper.PointAt(filled[i].u, v_values[j]);
      raw.m_V.Append(ON_3fPoint(p));
    }
  }
  if (u_points >= 2) {
    for (size_t i = 0; i + 1 < u_points; ++i) {
      for (size_t j = 0; j + 1 < v_points; ++j) {
        const int v00 = grid_index(i, j);
        const int v10 = grid_index(i + 1, j);
        const int v11 = grid_index(i + 1, j + 1);
        const int v01 = grid_index(i, j + 1);
        ON_MeshFace tri1;
        tri1.vi[0] = v00;
        tri1.vi[1] = v10;
        tri1.vi[2] = v11;
        tri1.vi[3] = v11;
        raw.m_F.Append(tri1);
        ON_MeshFace tri2;
        tri2.vi[0] = v00;
        tri2.vi[1] = v11;
        tri2.vi[2] = v01;
        tri2.vi[3] = v01;
        raw.m_F.Append(tri2);
      }
    }
  }
  return mesh;
}

// ---------------------------------------------------------------------
// Per-row chain strip mesher for a CylindricalFace whose two v-rows carry
// genuinely DIFFERENT breakpoint schedules - see Brep::TessellateConforming()'s
// own doc comment (brep.h, the "SIXTH gap" entry) for the diagnosis this
// answers. BuildConformingCylinderMesh above forces every ConformingMatch
// sample onto ONE shared u-breakpoint list used by both its v=0 and its
// v=length row, which is exactly right when the two ends' caps are
// sampled identically (the common case, kept on that function
// unchanged) and a genuine T-junction otherwise: the denser end's
// breakpoints become unforced extra columns on the sparser end's row,
// which that end's own cap loop never received. Here each row is its
// OWN chain - a flat row carries only the matches at THAT end (plus the
// rails and the same uniform gap fill), a notched row is the literal
// cap-notch polyline - and the band between two consecutive rows is
// triangulated as the u-monotone polygon it is.
// ---------------------------------------------------------------------

// One vertex of a row chain: its (u, v) on the face's own surface and,
// when it is a LITERAL shared point (a ConformingMatch sample or a cap-
// notch point), a pointer to that exact Point3d, used verbatim and never
// re-evaluated through the surface - the same "same std::vector<Point3d>
// on both sides" identity BuildConformingCylinderMesh's forced points
// rely on. A null `forced` means "evaluate wrapper.PointAt(u, v)".
struct StripChainVertex {
  double u = 0.0;
  double v = 0.0;
  const Point3d* forced = nullptr;
};
// Strictly increasing in u; front at the u_min rail, back at the u_max
// rail.
using RowChain = std::vector<StripChainVertex>;

// The flat (un-notched) row at v = `v_flat`: the two rails plus the
// forced raw_u samples of every ConformingMatch at THIS end only
// (`at_v0`), deduplicated within `u_tol` (a later forced point wins the
// slot, exactly as BuildConformingCylinderMesh's add_break does) and
// sorted, then the same "fill any gap wider than 1.5x the u_divisions
// spacing uniformly" rule that function applies to its shared list -
// restricted to one row, so a row with a single 65-point lens arc gets
// exactly those 65 columns and a row with four 65-point quadrant arcs
// exactly their 257-point union.
RowChain BuildFlatRowChain(double u_min, double u_max, double v_flat, double u_tol, int u_divisions,
                           const std::vector<ConformingMatch>& matches, bool at_v0) {
  struct Break {
    double u = 0.0;
    const Point3d* point = nullptr;
  };
  std::vector<Break> breaks;
  auto add_break = [&](double u, const Point3d* point) {
    for (Break& b : breaks) {
      if (std::fabs(b.u - u) <= u_tol) {
        if (point) b.point = point;
        return;
      }
    }
    breaks.push_back({u, point});
  };
  add_break(u_min, nullptr);
  add_break(u_max, nullptr);
  for (const ConformingMatch& m : matches) {
    if (m.at_v0 != at_v0) continue;
    for (size_t s = 0; s < m.raw_u.size(); ++s) add_break(m.raw_u[s], &m.points[s]);
  }
  std::sort(breaks.begin(), breaks.end(), [](const Break& a, const Break& b) { return a.u < b.u; });

  const double u_range = std::max(u_max - u_min, 1e-300);
  const double target_spacing = u_range / static_cast<double>(std::max(u_divisions, 1));
  RowChain chain;
  chain.reserve(breaks.size() * 2);
  for (size_t i = 0; i + 1 < breaks.size(); ++i) {
    chain.push_back({breaks[i].u, v_flat, breaks[i].point});
    const double gap = breaks[i + 1].u - breaks[i].u;
    if (gap > target_spacing * 1.5) {
      const int extra = static_cast<int>(std::ceil(gap / target_spacing)) - 1;
      for (int e = 1; e <= extra; ++e) {
        const double u = breaks[i].u + gap * static_cast<double>(e) / static_cast<double>(extra + 1);
        chain.push_back({u, v_flat, nullptr});
      }
    }
  }
  if (!breaks.empty()) chain.push_back({breaks.back().u, v_flat, breaks.back().point});
  return chain;
}

// A notched row: the side table's literal points, verbatim, at the (u, v)
// FromMixedFaces() already computed for them (see CylinderNotchRows' own
// doc comment in brep.h). The table's contract is increasing angle, hence
// increasing u - the reversal below is a guard, not an expected path. No
// gap fill: a producer's notch list is dense by construction (200-odd
// samples over at least a quarter sweep), and adding unforced columns
// to a row that must match a neighbour's literal polyline vertex-for-
// vertex would reintroduce exactly the T-junction this mesher removes.
RowChain BuildNotchRowChain(const std::vector<Point3d>& points, const std::vector<Point2d>& uv) {
  RowChain chain;
  chain.reserve(points.size());
  for (size_t i = 0; i < points.size() && i < uv.size(); ++i) chain.push_back({uv[i].x, uv[i].y, &points[i]});
  if (chain.size() >= 2 && chain.front().u > chain.back().u) std::reverse(chain.begin(), chain.end());
  return chain;
}

// Piecewise-linear v(u) along a chain (binary search on u), clamped to
// the chain's end values outside its u range.
double ChainVAt(const RowChain& chain, double u) {
  if (u <= chain.front().u) return chain.front().v;
  if (u >= chain.back().u) return chain.back().v;
  size_t lo = 0, hi = chain.size() - 1;
  while (hi - lo > 1) {
    const size_t mid = (lo + hi) / 2;
    if (chain[mid].u <= u) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  const double du = chain[hi].u - chain[lo].u;
  const double t = du > 0.0 ? (u - chain[lo].u) / du : 0.0;
  return chain[lo].v + (chain[hi].v - chain[lo].v) * t;
}

// One already-appended mesh vertex of a row: its (u, v) and its index in
// the mesh being built.
struct StripRowVertex {
  double u = 0.0;
  double v = 0.0;
  int index = 0;
};

// Triangulates the band between two consecutive rows, `lower` and
// `upper` (each strictly increasing in u, both spanning the same
// [u_min, u_max]), as the u-monotone polygon they bound - the standard
// stack sweep for monotone polygons (de Berg, Cheong, van Kreveld,
// Overmars, Computational Geometry, section 3.3): walk the merged
// sequence of both rows by increasing u, keeping on a stack the vertices
// that still need diagonals; a vertex on the OTHER chain than the stack
// top fans a triangle to every consecutive stack pair; a vertex on the
// SAME chain pops while the diagonal to the vertex below the top lies
// inside the polygon (a left turn seen from the lower chain, a right
// turn from the upper one), emitting one triangle per pop. Every
// diagonal is tested for being on the interior side, so no two triangles
// overlap and none is left out, for ANY simple u-monotone strip - no
// convexity is assumed, which matters here: a naive two-pointer "advance
// the smaller u, emit one triangle" merge is only valid when both rows
// are straight (the tensor case) and can silently overlap when one row
// is a curved notch sampled at different u than the other.
//
// A tie in u puts the UPPER row's vertex first; for two rows with the
// identical u set that reproduces BuildConformingCylinderMesh's own
// v00-v11 cell diagonal exactly. Every emitted triangle is ordered CCW
// in (u, v) - the same orientation as that function's own tri1/tri2, so
// the surface's u_dir x v_dir outward normal and the dispatch loop's
// m_bRev flip apply to both meshers unchanged. A triangle with a repeated
// vertex index (a pinched rail, see BuildConformingCylinderStripMesh) is
// skipped rather than emitted degenerate.
void TriangulateStrip(const std::vector<StripRowVertex>& lower, const std::vector<StripRowVertex>& upper,
                      ON_Mesh& raw) {
  struct SweepVertex {
    double u = 0.0;
    double v = 0.0;
    int index = 0;
    int chain = 0;  // 0 = lower row, 1 = upper row
  };
  std::vector<SweepVertex> seq;
  seq.reserve(lower.size() + upper.size());
  {
    size_t i = 0, j = 0;
    while (i < lower.size() || j < upper.size()) {
      const bool take_upper = (i >= lower.size()) || (j < upper.size() && upper[j].u <= lower[i].u);
      if (take_upper) {
        seq.push_back({upper[j].u, upper[j].v, upper[j].index, 1});
        ++j;
      } else {
        seq.push_back({lower[i].u, lower[i].v, lower[i].index, 0});
        ++i;
      }
    }
  }
  // A vertex shared by both rows (a pinched rail) appears twice in a row
  // in the merged sequence; the polygon has it once.
  {
    std::vector<SweepVertex> unique;
    unique.reserve(seq.size());
    for (const SweepVertex& s : seq) {
      if (!unique.empty() && unique.back().index == s.index) continue;
      unique.push_back(s);
    }
    seq.swap(unique);
  }
  if (seq.size() < 3) return;

  auto emit = [&](const SweepVertex& p, const SweepVertex& q, const SweepVertex& r) {
    if (p.index == q.index || q.index == r.index || p.index == r.index) return;
    const double twice_area = (q.u - p.u) * (r.v - p.v) - (q.v - p.v) * (r.u - p.u);
    ON_MeshFace f;
    f.vi[0] = p.index;
    if (twice_area >= 0.0) {
      f.vi[1] = q.index;
      f.vi[2] = r.index;
    } else {
      f.vi[1] = r.index;
      f.vi[2] = q.index;
    }
    f.vi[3] = f.vi[2];
    raw.m_F.Append(f);
  };
  auto cross = [](const SweepVertex& o, const SweepVertex& p, const SweepVertex& q) {
    return (p.u - o.u) * (q.v - o.v) - (p.v - o.v) * (q.u - o.u);
  };

  std::vector<SweepVertex> stack;
  stack.push_back(seq[0]);
  stack.push_back(seq[1]);
  for (size_t k = 2; k + 1 < seq.size(); ++k) {
    const SweepVertex& v = seq[k];
    if (v.chain != stack.back().chain) {
      for (size_t s = 0; s + 1 < stack.size(); ++s) emit(v, stack[s], stack[s + 1]);
      const SweepVertex previous_top = stack.back();
      stack.clear();
      stack.push_back(previous_top);
      stack.push_back(v);
    } else {
      SweepVertex last = stack.back();
      stack.pop_back();
      while (!stack.empty()) {
        const double c = cross(stack.back(), last, v);
        const bool diagonal_inside = v.chain == 0 ? (c > 0.0) : (c < 0.0);
        if (!diagonal_inside) break;
        emit(v, stack.back(), last);
        last = stack.back();
        stack.pop_back();
      }
      stack.push_back(last);
      stack.push_back(v);
    }
  }
  const SweepVertex& final_vertex = seq.back();
  for (size_t s = 0; s + 1 < stack.size(); ++s) emit(final_vertex, stack[s], stack[s + 1]);
}

// Builds a CylindricalFace's mesh from two independent boundary row
// chains: row 0 is `bottom` (the v=0 end), row `v_divisions` is `top`
// (the v=length end), and each interior row j lies on the UNION of both
// chains' u breakpoints (deduplicated within `u_tol`) at
// v = lerp(bottom's v(u), top's v(u), j / v_divisions) - so for two flat
// rows the interior is exactly BuildConformingCylinderMesh's own uniform
// v progression, and for a notched row the interior rows follow the
// notch's own dip smoothly out to the flat end. Every chain vertex with
// a literal `forced` point is appended verbatim; everything else is
// wrapper.PointAt. The two rails: where the two chains meet at the same
// (u, v) within tolerance (the pinch of a length-0 "eye" strip, whose
// two rows are two half-curves sharing their endpoints) every row reuses
// the bottom chain's endpoint vertex instead of stacking coincident
// vertices there; otherwise the rail column is PointAt(u_rail, v_j),
// the same samples an axially-adjacent sibling's own tensor rail
// carries, so rail welding is unchanged.
Mesh BuildConformingCylinderStripMesh(const NurbsSurface& wrapper, const RowChain& bottom, const RowChain& top,
                                      int v_divisions, double u_tol, double v_tol) {
  Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  if (bottom.size() < 2 || top.size() < 2 || v_divisions < 1) return mesh;

  std::vector<double> u_all;
  u_all.reserve(bottom.size() + top.size());
  for (const StripChainVertex& c : bottom) u_all.push_back(c.u);
  for (const StripChainVertex& c : top) u_all.push_back(c.u);
  std::sort(u_all.begin(), u_all.end());
  std::vector<double> u_interior;
  for (double u : u_all) {
    if (!u_interior.empty() && std::fabs(u - u_interior.back()) <= u_tol) continue;
    u_interior.push_back(u);
  }

  const bool pinch0 = std::fabs(bottom.front().u - top.front().u) <= u_tol &&
                      std::fabs(bottom.front().v - top.front().v) <= v_tol;
  const bool pinch1 =
      std::fabs(bottom.back().u - top.back().u) <= u_tol && std::fabs(bottom.back().v - top.back().v) <= v_tol;

  auto append = [&](const StripChainVertex& c) {
    const Point3d p = c.forced != nullptr ? *c.forced : wrapper.PointAt(c.u, c.v);
    raw.m_V.Append(ON_3fPoint(p));
    return raw.m_V.Count() - 1;
  };

  std::vector<std::vector<StripRowVertex>> rows(static_cast<size_t>(v_divisions) + 1);
  for (const StripChainVertex& c : bottom) rows[0].push_back({c.u, c.v, append(c)});
  const int first_index = rows[0].front().index;
  const int last_index = rows[0].back().index;
  for (int j = 1; j < v_divisions; ++j) {
    const double t = static_cast<double>(j) / static_cast<double>(v_divisions);
    std::vector<StripRowVertex>& row = rows[static_cast<size_t>(j)];
    row.reserve(u_interior.size());
    for (size_t k = 0; k < u_interior.size(); ++k) {
      const double u = u_interior[k];
      const double v = (1.0 - t) * ChainVAt(bottom, u) + t * ChainVAt(top, u);
      if (pinch0 && k == 0) {
        row.push_back({u, v, first_index});
      } else if (pinch1 && k + 1 == u_interior.size()) {
        row.push_back({u, v, last_index});
      } else {
        row.push_back({u, v, append({u, v, nullptr})});
      }
    }
  }
  for (size_t k = 0; k < top.size(); ++k) {
    const StripChainVertex& c = top[k];
    if (pinch0 && k == 0) {
      rows.back().push_back({c.u, c.v, first_index});
    } else if (pinch1 && k + 1 == top.size()) {
      rows.back().push_back({c.u, c.v, last_index});
    } else {
      rows.back().push_back({c.u, c.v, append(c)});
    }
  }
  for (size_t j = 0; j + 1 < rows.size(); ++j) TriangulateStrip(rows[j], rows[j + 1], raw);
  return mesh;
}

// True when two u-breakpoint sets, each taken as a sorted set
// deduplicated within `u_tol`, coincide element-wise within `u_tol` -
// the "would BuildConformingCylinderMesh force the same columns on both
// rows" question the dispatch below asks. Callers pass the EFFECTIVE
// sets (forced samples plus the two rails), not the raw forced samples:
// see the dispatch site for why that distinction decides which faces are
// re-routed.
bool SameEffectiveRowSet(std::vector<double> a, std::vector<double> b, double u_tol) {
  auto normalize = [u_tol](std::vector<double>& s) {
    std::sort(s.begin(), s.end());
    std::vector<double> d;
    d.reserve(s.size());
    for (double u : s) {
      if (!d.empty() && std::fabs(u - d.back()) <= u_tol) continue;
      d.push_back(u);
    }
    s.swap(d);
  };
  normalize(a);
  normalize(b);
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::fabs(a[i] - b[i]) > u_tol) return false;
  }
  return true;
}

// One shared boundary sample forced onto a plain quad face's own tensor
// grid, at parameter `t` (in [0, 1]) along whichever of the quad's own
// two axes (see BuildConformingPlainQuadMesh's own doc comment for the
// `a`/`b` convention) the corresponding edge runs along.
struct EdgeForce {
  double t = 0.0;
  Point3d point;
};

// One breakpoint of a plain quad's own a or b axis (see
// BuildConformingPlainQuadMesh below): its parameter, plus the forced
// literal point (if any) at each of that axis's two extremes.
struct QuadAxisBreak {
  double t = 0.0;
  const Point3d* at_lo = nullptr;  // forced point at this axis's own "0" extreme
  const Point3d* at_hi = nullptr;  // forced point at this axis's own "1" extreme
};

// Shared "collect forced breakpoints, then fill the remaining gaps at
// roughly `divisions`-uniform spacing" logic - the same shape
// BuildConformingCylinderMesh's own u-breakpoint construction already
// uses (see its own doc comment), applied once per axis by
// BuildConformingPlainQuadMesh (both extremes' forces unioned into one
// list, since a tensor grid needs one column set for every row) and
// once per ROW by BuildConformingPlainQuadStripMesh (one extreme's
// forces at a time, the other passed empty). `forces_lo`/`forces_hi`
// are the forced points at the axis's "0"/"1" extreme; two forces within
// 1e-9 in t share one breakpoint, the later one winning that extreme's
// pointer. The result is sorted by t, always starting at 0 and ending at
// 1.
std::vector<QuadAxisBreak> BuildQuadAxisBreaks(const std::vector<EdgeForce>& forces_lo,
                                               const std::vector<EdgeForce>& forces_hi, int divisions) {
  const double tol = 1e-9;
  std::vector<QuadAxisBreak> breaks;
  auto add_break = [&](double t, const Point3d* lo, const Point3d* hi) {
    for (QuadAxisBreak& b : breaks) {
      if (std::fabs(b.t - t) <= tol) {
        if (lo) b.at_lo = lo;
        if (hi) b.at_hi = hi;
        return;
      }
    }
    QuadAxisBreak b;
    b.t = t;
    b.at_lo = lo;
    b.at_hi = hi;
    breaks.push_back(b);
  };
  add_break(0.0, nullptr, nullptr);
  add_break(1.0, nullptr, nullptr);
  for (const EdgeForce& f : forces_lo) add_break(f.t, &f.point, nullptr);
  for (const EdgeForce& f : forces_hi) add_break(f.t, nullptr, &f.point);
  std::sort(breaks.begin(), breaks.end(), [](const QuadAxisBreak& x, const QuadAxisBreak& y) { return x.t < y.t; });

  const double target_spacing = 1.0 / static_cast<double>(std::max(divisions, 1));
  std::vector<QuadAxisBreak> filled;
  filled.reserve(breaks.size() * 2);
  for (size_t i = 0; i + 1 < breaks.size(); ++i) {
    filled.push_back(breaks[i]);
    const double gap = breaks[i + 1].t - breaks[i].t;
    if (gap > target_spacing * 1.5) {
      const int extra = static_cast<int>(std::ceil(gap / target_spacing)) - 1;
      for (int e = 1; e <= extra; ++e) {
        QuadAxisBreak b;
        b.t = breaks[i].t + gap * static_cast<double>(e) / static_cast<double>(extra + 1);
        filled.push_back(b);
      }
    }
  }
  if (!breaks.empty()) filled.push_back(breaks.back());
  return filled;
}

// Builds one un-cut, 4-corner planar face's own tensor-product mesh via
// PLAIN BILINEAR interpolation of its own 4 corners `q[0..3]` (CCW, the
// SAME order `fg.outer` - or the implicit domain-corner rectangle for an
// untrimmed face - already presents them in, per FaceOuterUv's own doc
// comment), with any of `a_forces_b0`/`a_forces_b1`/`b_forces_a0`/
// `b_forces_a1` overriding grid rows/columns at their own recorded `t`
// position with the LITERAL Point3d already computed for the matching
// straight edge of some adjacent wedge PlanarFace - the plain-quad
// counterpart to BuildConformingCylinderMesh's own "inject the wedge's
// own literal arc points into the matching grid row" mechanism just
// above, generalized from "always forced along u at fixed v" (the only
// shape a CylindricalFace's own cap match ever needs) to "forced along
// EITHER of this quad's own two axes, at either of that axis's own two
// extremes" (the shape a Box() wall's own cap-level edge needs, since
// which physical direction - x, y, or z - plays which role varies per
// wall; see TessellateConforming()'s own doc comment for why).
//
// Deliberately does NOT evaluate the real underlying NURBS surface at
// all (unlike TessellateGridClippedExact, which this replaces for a
// face that has at least one forced edge) - `q[0..3]` are evaluated
// once by the caller (via wrapper.PointAt on the face's own 4 trim
// corners) and every other grid point is a direct bilinear blend of
// them. Exactly matches TessellateGridClippedExact's own physical shape
// for the faces this is used on (a genuine planar quadrilateral, where
// bilinear interpolation and the degree-(1,1) NURBS surface's own
// PointAt are the same map to floating-point precision) while making
// the forced-point injection tractable at all: TessellateGridClippedExact
// tessellates by clipping independent grid CELLS against a trim
// polygon, which has no notion of "this specific boundary vertex must
// be exactly this literal point" - a plain tensor grid does.
//
// `a` runs from `q[0]` (a=0) to `q[1]` (a=1) at b=0, and from `q[3]` to
// `q[2]` at b=1; `b` runs from `q[0]` (b=0) to `q[3]` (b=1) at a=0, and
// from `q[1]` to `q[2]` at a=1 - i.e. `a`/`b` are just this function's
// own name for whichever of the real surface's u/v (or a transposition
// of them) makes `q[0]->q[1]` and `q[0]->q[3]` the two edges out of
// `q[0]`, matching TessellateGrid's own (v00,v10,v11)/(v00,v11,v01)
// triangle winding exactly (with `a` playing the role TessellateGrid's
// own `u` plays, `b` playing `v`'s) - since `q[0..3]` is a CCW loop as
// seen from outside (the same invariant every planar-face factory in
// this kernel already maintains), `(q[1]-q[0]) x (q[3]-q[0])` points
// outward exactly as "u_dir x v_dir points outward" already promises
// elsewhere in this file, so this reproduces the correct orientation
// with no separate flip/transpose logic needed regardless of which real
// axis `a`/`b` happen to correspond to for any one particular face.
Mesh BuildConformingPlainQuadMesh(const std::array<Point3d, 4>& q, int u_divisions, int v_divisions,
                                   const std::vector<EdgeForce>& a_forces_b0,
                                   const std::vector<EdgeForce>& a_forces_b1,
                                   const std::vector<EdgeForce>& b_forces_a0,
                                   const std::vector<EdgeForce>& b_forces_a1) {
  auto bilinear = [&](double a, double b) {
    return (1.0 - a) * (1.0 - b) * q[0] + a * (1.0 - b) * q[1] + a * b * q[2] + (1.0 - a) * b * q[3];
  };

  // Once per axis (see BuildQuadAxisBreaks above), since EITHER axis
  // (not just `u`) may carry forced points for a plain quad face (see
  // this function's own doc comment for why). Both extremes' forces are
  // unioned into ONE list per axis - a tensor grid needs the same column
  // set on every row - which is exactly why a quad whose two opposite
  // edges carry genuinely DIFFERENT forced sets is routed to
  // BuildConformingPlainQuadStripMesh instead (see the dispatch in
  // TessellateConforming).
  const std::vector<QuadAxisBreak> a_breaks = BuildQuadAxisBreaks(a_forces_b0, a_forces_b1, u_divisions);
  const std::vector<QuadAxisBreak> b_breaks = BuildQuadAxisBreaks(b_forces_a0, b_forces_a1, v_divisions);

  Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  const size_t a_points = a_breaks.size();
  const size_t b_points = b_breaks.size();
  raw.m_V.Reserve(static_cast<int>(a_points * b_points));
  auto grid_index = [b_points](size_t i, size_t j) { return static_cast<int>(i * b_points + j); };
  for (size_t i = 0; i < a_points; ++i) {
    for (size_t j = 0; j < b_points; ++j) {
      const Point3d* forced = nullptr;
      if (j == 0) {
        forced = a_breaks[i].at_lo;
      } else if (j + 1 == b_points) {
        forced = a_breaks[i].at_hi;
      }
      if (forced == nullptr) {
        if (i == 0) {
          forced = b_breaks[j].at_lo;
        } else if (i + 1 == a_points) {
          forced = b_breaks[j].at_hi;
        }
      }
      const Point3d p = forced != nullptr ? *forced : bilinear(a_breaks[i].t, b_breaks[j].t);
      raw.m_V.Append(ON_3fPoint(p));
    }
  }
  for (size_t i = 0; i + 1 < a_points; ++i) {
    for (size_t j = 0; j + 1 < b_points; ++j) {
      const int v00 = grid_index(i, j);
      const int v10 = grid_index(i + 1, j);
      const int v11 = grid_index(i + 1, j + 1);
      const int v01 = grid_index(i, j + 1);
      ON_MeshFace tri1;
      tri1.vi[0] = v00;
      tri1.vi[1] = v10;
      tri1.vi[2] = v11;
      tri1.vi[3] = v11;
      raw.m_F.Append(tri1);
      ON_MeshFace tri2;
      tri2.vi[0] = v00;
      tri2.vi[1] = v11;
      tri2.vi[2] = v01;
      tri2.vi[3] = v01;
      raw.m_F.Append(tri2);
    }
  }
  return mesh;
}

// Per-ROW variant of BuildConformingPlainQuadMesh for a plain quad whose
// two OPPOSITE a-edges (b=0 and b=1) carry genuinely DIFFERENT forced
// t-sets - the planar counterpart of BuildConformingCylinderStripMesh,
// and the same design: row 0 is EXACTLY the b=0 edge's own chain (its
// forced points plus uniform gap-fill, nothing else), the last row
// EXACTLY the b=1 edge's own chain, every interior row lies on the union
// of both chains' t values (so the mesh stays a clean loft between the
// two edges), and consecutive rows are joined by TriangulateStrip. The
// b axis (rows) is built exactly as the tensor mesher builds it and must
// agree on its own two edges (b_forces_a0 vs b_forces_a1) - when it does
// not, the dispatch transposes the quad so the mismatched pair becomes
// the a-edges; a quad mismatched on BOTH axes has no such rescue and
// stays on the tensor mesher (see TessellateConforming's own doc
// comment for that disclosed residual). Corner forcing follows the
// tensor mesher's rule verbatim: an a-edge force at t=0/t=1 wins,
// otherwise the b-edge's own force at that row.
//
// Why the tensor mesher cannot do this: BuildQuadAxisBreaks unions the
// two opposite edges' t-sets into one column list for the WHOLE grid, so
// a t forced only on the b=0 edge still appears on the b=1 row as an
// unforced bilinear point - a T-junction against whatever neighbor
// shares that b=1 edge (and vice versa). Measured on the oblique-drilled
// box (a tilted hole's two cap ellipses pierce a long wall's top and
// bottom edges at different positions): 756 such perimeter edges stayed
// open at 64/64 with the ellipse seam itself already closed, all on the
// two long walls, and every one closes under this mesher.
Mesh BuildConformingPlainQuadStripMesh(const std::array<Point3d, 4>& q, int u_divisions, int v_divisions,
                                        const std::vector<EdgeForce>& a_forces_b0,
                                        const std::vector<EdgeForce>& a_forces_b1,
                                        const std::vector<EdgeForce>& b_forces_a0,
                                        const std::vector<EdgeForce>& b_forces_a1) {
  auto bilinear = [&](double a, double b) {
    return (1.0 - a) * (1.0 - b) * q[0] + a * (1.0 - b) * q[1] + a * b * q[2] + (1.0 - a) * b * q[3];
  };
  const std::vector<EdgeForce> none;
  const std::vector<QuadAxisBreak> a_lo = BuildQuadAxisBreaks(a_forces_b0, none, u_divisions);
  const std::vector<QuadAxisBreak> a_hi = BuildQuadAxisBreaks(none, a_forces_b1, u_divisions);
  const std::vector<QuadAxisBreak> b_breaks = BuildQuadAxisBreaks(b_forces_a0, b_forces_a1, v_divisions);

  // Interior rows: the union of both chains' t values, deduplicated
  // within the same 1e-9 BuildQuadAxisBreaks itself uses.
  std::vector<double> a_all;
  a_all.reserve(a_lo.size() + a_hi.size());
  for (const QuadAxisBreak& b : a_lo) a_all.push_back(b.t);
  for (const QuadAxisBreak& b : a_hi) a_all.push_back(b.t);
  std::sort(a_all.begin(), a_all.end());
  {
    std::vector<double> distinct;
    distinct.reserve(a_all.size());
    for (double t : a_all) {
      if (!distinct.empty() && std::fabs(t - distinct.back()) <= 1e-9) continue;
      distinct.push_back(t);
    }
    a_all.swap(distinct);
  }

  Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  auto append = [&](const Point3d& p) {
    raw.m_V.Append(ON_3fPoint(p));
    return raw.m_V.Count() - 1;
  };
  std::vector<std::vector<StripRowVertex>> rows(b_breaks.size());
  for (size_t j = 0; j < b_breaks.size(); ++j) {
    const double bt = b_breaks[j].t;
    const bool first = j == 0;
    const bool last = j + 1 == b_breaks.size();
    if (first || last) {
      const std::vector<QuadAxisBreak>& chain = first ? a_lo : a_hi;
      for (size_t i = 0; i < chain.size(); ++i) {
        const Point3d* forced = first ? chain[i].at_lo : chain[i].at_hi;
        if (forced == nullptr) {
          if (i == 0) {
            forced = b_breaks[j].at_lo;
          } else if (i + 1 == chain.size()) {
            forced = b_breaks[j].at_hi;
          }
        }
        rows[j].push_back({chain[i].t, bt, append(forced != nullptr ? *forced : bilinear(chain[i].t, bt))});
      }
    } else {
      for (size_t i = 0; i < a_all.size(); ++i) {
        const Point3d* forced = nullptr;
        if (i == 0) {
          forced = b_breaks[j].at_lo;
        } else if (i + 1 == a_all.size()) {
          forced = b_breaks[j].at_hi;
        }
        rows[j].push_back({a_all[i], bt, append(forced != nullptr ? *forced : bilinear(a_all[i], bt))});
      }
    }
  }
  // TriangulateStrip emits CCW-in-(a, b) triangles, the same orientation
  // as the tensor mesher's own tri1/tri2, so `(q[1]-q[0]) x (q[3]-q[0])`
  // pointing outward gives outward-facing triangles here too.
  for (size_t j = 0; j + 1 < rows.size(); ++j) TriangulateStrip(rows[j], rows[j + 1], raw);
  return mesh;
}

// `q[e]`'s own "from" corner (BuildConformingPlainQuadMesh's a/b
// convention: edge 0 is q0->q1 at b=0, edge 1 is q1->q2 at a=1, edge 2 is
// q3->q2 at b=1 - walked "backward" in q's own trim order so a=0 still
// anchors at q3 and a=1 at q2 - edge 3 is q0->q3 at a=0). Factored out
// (as a template, so it works identically on a face's own 3D corners and
// its 2D (u, v) corners) because Tessellate()'s own new plain-quad seam
// pass (below) needs this exact convention twice - once to test whether
// a face's own edge is a constant-u or constant-v line at all
// (IsAxisAlignedQuadUv), once to build the shared 3D boundary
// (ComputePlainQuadSeamForces) - TessellateConforming()'s own
// straight-edge pass above keeps its own inline copy of this same
// convention unchanged (see its own implementation comments), since
// sharing one helper between the two is a natural follow-up, not
// required by this fix's own scope (see Tessellate()'s own doc comment).
template <typename P>
P PlainQuadEdgeFrom(const std::array<P, 4>& q, int e) {
  return (e == 0) ? q[0] : (e == 1) ? q[1] : (e == 2) ? q[3] : q[0];
}
template <typename P>
P PlainQuadEdgeTo(const std::array<P, 4>& q, int e) {
  return (e == 0) ? q[1] : (e == 1) ? q[2] : (e == 2) ? q[2] : q[3];
}

// True when every one of a "plain quad" face's own 4 edges (see
// CollectPlainQuadFaces's own doc comment for that shape's definition)
// is, in the face's OWN (u, v) domain, either a constant-u or a
// constant-v line - the actual condition NurbsSurface::TessellateGrid/
// TessellateGridClippedExact's own per-edge sample count (u_divisions+1
// along a constant-v edge, v_divisions+1 along a constant-u edge)
// depends on. This is NOT guaranteed merely by "this face's outer loop
// has exactly 4 points": ExtractPlanarFace's own ON_Plane(origin,
// normal) picks an in-plane (x, y) basis from the normal ALONE, with no
// relationship to the polygon's own edge directions - confirmed directly
// (not merely derived): hulling a box's 8 corners after rotating them by
// an arbitrary angle produces quad faces whose own local (x, y), and
// hence (after FromMixedFaces()'s own rescale) (u, v), corners share
// NEITHER an x/u NOR a y/v value pairwise - a genuinely rotated
// quadrilateral in its own parameter domain, not an axis-aligned
// rectangle. A face shaped that way is excluded from the ENTIRE seam
// pass (CollectPlainQuadFaces never adds it to `quad_faces` at all) -
// this is a real, PRE-EXISTING, SEPARATE gap (confirmed directly: even
// at u_divisions == v_divisions, such a face's own tessellated boundary
// does not reliably coincide with its neighbor's either, since each
// side's own vertex count along the shared edge comes from however many
// grid lines its own diagonal trim edge happens to cross - not from
// u_divisions+1 or v_divisions+1 alone), already covered by this
// kernel's own existing disclosure that "a genuinely arbitrary pair of
// adjacent PlanarFaces... is not attempted" (see
// TessellateConforming()'s own doc comment) - not something this fix
// touches, and not safe to misapply this pass's own u/v-axis labeling to
// (doing so could inject wrong force points instead of correctly doing
// nothing).
bool IsAxisAlignedQuadUv(const std::array<Point2d, 4>& uv) {
  for (int e = 0; e < 4; ++e) {
    const Point2d from = PlainQuadEdgeFrom(uv, e);
    const Point2d to = PlainQuadEdgeTo(uv, e);
    const double tol_u = 1e-9 * (1.0 + std::fabs(from.x) + std::fabs(to.x));
    const double tol_v = 1e-9 * (1.0 + std::fabs(from.y) + std::fabs(to.y));
    const bool u_constant = std::fabs(to.x - from.x) <= tol_u;
    const bool v_constant = std::fabs(to.y - from.y) <= tol_v;
    if (!u_constant && !v_constant) return false;  // a genuinely oblique edge
  }
  return true;
}

// One "plain quad" planar face's own 4 outward-CCW corners, in both 3D
// (`corner` - what BuildConformingPlainQuadMesh actually builds a mesh
// from) and the SAME corners' own (u, v) values (`corner_uv` - what
// ComputePlainQuadSeamForces needs to tell a constant-u edge from a
// constant-v one, per IsAxisAlignedQuadUv's own doc comment for why that
// can't be assumed from edge index alone the way
// TessellateConforming()'s own narrower-scoped pass safely does).
struct PlainQuadFace {
  int face_index = 0;
  std::array<Point3d, 4> corner;
  std::array<Point2d, 4> corner_uv;
};

// Collects every resolved face that is a "plain quad" in the sense
// TessellateConforming()'s own straight-edge pass already defines
// (planar, and either untrimmed - using the implicit domain-corner
// rectangle, per FaceOuterUv's own doc comment - or trimmed to an
// explicit 4-point outer loop, what FromMixedFaces() always builds, even
// for an untouched input face), further narrowed by two guards that pass
// does not need:
//
// - `fg.holes` must be empty. TessellateConforming()'s own inputs
//   (Box()-derived walls, FromMixedFaces()-built quads) never carry
//   holes by construction, so its own filter gets away without checking;
//   Tessellate() is reached by a broader input surface, including a
//   .3dm-round-tripped Brep whose face happens to present a literal
//   4-point outer loop AND an inner hole loop (ResolveFace's own
//   fallback path only synthesizes a domain-rectangle outer when holes
//   are present and outer was ALREADY empty - a face with its own real
//   4-point outer plus a hole keeps both, and its own exact_clip is then
//   false - see ResolveFace's own implementation). Routing such a face
//   through BuildConformingPlainQuadMesh (which has no hole-clipping
//   logic at all) would silently drop the hole, so this filter excludes
//   it instead - that face keeps its exact prior tessellation via
//   Tessellate()'s own ordinary TessellateGrid(..., &fg.outer, holes)
//   path, unaffected.
// - The face's own 4 corners must be axis-aligned in its own (u, v)
//   domain (see IsAxisAlignedQuadUv's own doc comment for exactly why,
//   and the separate, pre-existing gap this guard deliberately leaves
//   untouched rather than misapplying itself to).
std::vector<PlainQuadFace> CollectPlainQuadFaces(const std::vector<FaceGeometry>& fgs,
                                                  const std::vector<bool>& resolved) {
  std::vector<PlainQuadFace> quad_faces;
  for (size_t i = 0; i < fgs.size(); ++i) {
    if (!resolved[i]) continue;
    const FaceGeometry& fg = fgs[i];
    if (!fg.holes.empty()) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (!wrapper.IsPlanar()) continue;
    std::array<Point2d, 4> corners_uv;
    if (fg.outer.size() == 4) {
      for (int c = 0; c < 4; ++c) corners_uv[static_cast<size_t>(c)] = fg.outer[static_cast<size_t>(c)];
    } else if (fg.outer.empty()) {
      const ON_Interval du = fg.surface.Domain(0), dv = fg.surface.Domain(1);
      corners_uv = {Point2d(du.Min(), dv.Min()), Point2d(du.Max(), dv.Min()), Point2d(du.Max(), dv.Max()),
                    Point2d(du.Min(), dv.Max())};
    } else {
      continue;  // a genuinely trimmed (non-4-corner) planar face - not this pass's target shape
    }
    if (!IsAxisAlignedQuadUv(corners_uv)) continue;
    PlainQuadFace qf;
    qf.face_index = static_cast<int>(i);
    qf.corner_uv = corners_uv;
    for (int c = 0; c < 4; ++c) {
      qf.corner[static_cast<size_t>(c)] =
          wrapper.PointAt(corners_uv[static_cast<size_t>(c)].x, corners_uv[static_cast<size_t>(c)].y);
    }
    // A planar face whose 4 domain corners are not 4 DISTINCT points is
    // not a quadrilateral at all - a planar fan cap from the sweep-class
    // factories (Brep::Extrude() et al.: a singular apex side plus a
    // curved boundary) is planar and untrimmed, so it reaches this point,
    // and building it as a bilinear patch of its "corners" (apex, apex,
    // B(b), B(a)) would tessellate it as a zero-width sliver - a silently
    // wrong result. Such a face takes the ordinary TessellateGrid path.
    {
      bool distinct = true;
      double scale = 0.0;
      for (int c = 0; c < 4; ++c) scale = std::max(scale, qf.corner[static_cast<size_t>(c)].MaximumCoordinate());
      const double tol = 1e-9 * (1.0 + scale);
      for (int c = 0; c < 4 && distinct; ++c) {
        for (int d = c + 1; d < 4; ++d) {
          if (qf.corner[static_cast<size_t>(c)].DistanceTo(qf.corner[static_cast<size_t>(d)]) <= tol) {
            distinct = false;
            break;
          }
        }
      }
      if (!distinct) continue;
    }
    quad_faces.push_back(qf);
  }
  return quad_faces;
}

// The actual fix for the gap TessellateConforming()'s own doc comment
// discloses (and Tessellate()'s own doc comment now discloses too - see
// there for the full falsifiable before/after claim): for two DIFFERENT
// "plain quad" faces (see CollectPlainQuadFaces's own doc comment) that
// share a boundary edge, NurbsSurface::TessellateGrid/
// TessellateGridClippedExact samples that edge independently on each
// side - u_divisions+1 points along a constant-v edge, v_divisions+1
// along a constant-u edge - and because which PHYSICAL axis (x, y, or z)
// plays u vs v is NOT the same for every face (confirmed directly:
// Box()'s own front and back walls assign x/z to u/v oppositely - see
// Brep::Box()'s own comment), two faces sharing one physical edge can
// each apply a DIFFERENT one of the two division counts to it whenever
// u_divisions != v_divisions, leaving that edge's two independently-
// tessellated copies with different point counts - genuinely open, not
// just approximately open.
//
// Only ever called when u_divisions != v_divisions (see Tessellate()'s
// own doc comment for why the two counts being equal is a full
// structural guarantee, not a heuristic, that NONE of this runs at all):
// every edge in the whole Brep has exactly one of two possible natural
// sample counts, u_divisions or v_divisions, so a mismatch between two
// sides can only ever be exactly {u_divisions, v_divisions} in some
// order - making the shared, closing count simply max(u_divisions,
// v_divisions), the same choice TessellateConforming()'s own
// boundary_samples default already makes for the same reason (dense
// enough that a caller asking for a fine grid on either axis also gets a
// fine shared boundary).
//
// Compares every DISTINCT pair of quad faces' 4 edges by their two 3D
// endpoints (in either order - two faces sharing a physical edge
// normally walk it in OPPOSITE order, since their outward normals point
// opposite ways across it; a same-direction coincidence is also accepted
// rather than silently missed, though it would mean inconsistent winding
// between the two faces - not expected from any Brep this kernel's own
// factories build) rather than either face's own u/v domain, for exactly
// the reason above. A pair whose natural counts already agree (6 of a
// Box()'s own 12 edges, for instance - every edge where both walls
// happen to use the SAME one of the two division counts) is left
// completely untouched: both sides already compute the identical count
// and hence the identical shared corner-to-corner points on their own,
// so forcing anything there would be redundant, not merely harmless.
// For a genuine mismatch, the shared points are built via PLAIN LINEAR
// interpolation between the two matched corner points (never a second,
// independently-evaluated approximation through either face's own NURBS
// surface - the same "share the literal points" mechanism
// TessellateConforming()'s own straight-edge pass already uses) and
// pushed into BOTH faces' own force lists at their own local edge index
// - always spanning that edge's FULL t in [0, 1] on both sides (unlike
// TessellateConforming()'s own wedge-vs-quad pass, a quad-vs-quad match
// here is always a complete shared edge between two faces of the same
// solid, never a wedge's own partial rail, so no fractional-span
// rounding/tie-breaking is needed).
//
// `already_forced`, if non-null, is the SAME `plain_forces` map the
// wedge/straight-edge passes above have already populated - i.e. it both
// names which face indices are already guaranteed to dispatch through
// BuildConformingPlainQuadMesh's own bilinear grid builder (see
// TessellateConforming()'s own dispatch loop) rather than that face's
// natural default (TessellateGrid or, for a BooleanCombineMixed-
// reconstructed face, TessellateGridClippedExact), AND carries the exact
// point lists already forced onto each such face's own 4 edges.
// `natural_count` agreeing between two sides is only proof they'll
// tessellate identically when BOTH sides also use the SAME dispatch
// path - true for every caller of Tessellate() (which has no forcing
// pass to begin with, so `already_forced` is always null there) and for
// every TessellateConforming() pair where neither or BOTH sides are
// already forced, but false the one place TessellateConforming()'s own
// wedge/straight-edge passes above force exactly ONE side of a seam
// (e.g. a wedge-forced Box() wall's own top edge) while leaving its
// OTHER edge's plain-quad neighbor (e.g. an untouched z-cap, reconstructed
// with `exact_clip = true` by BooleanCombineMixed's own FromMixedFaces())
// on its natural, possibly-diverging default - confirmed directly: at
// EQUAL u_divisions/v_divisions, a wedge-forced wall's unclaimed bottom
// edge samples a uniform bilinear row while its untouched-cap neighbor's
// own TessellateGridClippedExact dispatch samples a DIFFERENT, margined
// partition of the same physical edge at the identical division count -
// see TestBooleanCombineMixedUnionBossFlushBaseVolumeAndCapSeamIsClosed's
// own comment for the exact measurement. So a pair is now also forced
// (bypassing the `count_a == count_b` skip below) whenever `already_forced`
// says EXACTLY ONE of the two sides is already a forced face - the other
// side's own dispatch is not yet pinned down, so nothing is redundant
// about forcing it too; a pair where NEITHER or BOTH sides are already
// forced keeps the original count-agreement skip untouched.
//
// When that new trigger fires, `shared_count` is NOT simply
// max(u_divisions, v_divisions) the way the pre-existing count-mismatch
// trigger uses - BuildConformingPlainQuadMesh's own `build_axis` (see its
// doc comment) unions the t-breakpoints of a quad's TWO OPPOSITE edges
// (edge 0 with edge 2, or edge 1 with edge 3) into ONE shared per-axis
// breakpoint list for the WHOLE grid, since a tensor-product mesh needs
// the same column count on every row. If the already-forced side's
// OPPOSITE edge (its own already-forced top edge, in the wall-vs-cap
// example) carries a DIFFERENT point count than whatever this pass would
// otherwise pick, that union silently grows to include BOTH sets - adding
// extra, spurious bilinear-interpolated points to the very edge meant to
// match the neighbor, corrupting the match instead of completing it
// (confirmed directly: an early version of this fix used a flat
// max(u_divisions, v_divisions) here unconditionally and produced a
// mesh with MORE open boundary edges than before at an asymmetric
// divisions pair, all still exactly at the same physical seam). So:
// when the already-forced side's own opposite edge (`opposite_edge`)
// already carries points, reuse ITS point count as `shared_count` instead
// - keeping that face's own two opposite (same-axis) edges consistent,
// which is what `build_axis` actually needs - falling back to
// max(u_divisions, v_divisions) only when there is no such competing
// opposite-edge force to stay consistent with (the pre-existing
// count-mismatch case, and the common case where neither side was
// already forced at all).
std::unordered_map<int, std::array<std::vector<EdgeForce>, 4>> ComputePlainQuadSeamForces(
    const std::vector<PlainQuadFace>& quad_faces, int u_divisions, int v_divisions,
    const std::unordered_map<int, std::array<std::vector<EdgeForce>, 4>>* already_forced = nullptr) {
  std::unordered_map<int, std::array<std::vector<EdgeForce>, 4>> forces;

  auto natural_count = [&](const PlainQuadFace& qf, int e) {
    const Point2d from = PlainQuadEdgeFrom(qf.corner_uv, e);
    const Point2d to = PlainQuadEdgeTo(qf.corner_uv, e);
    const double tol_v = 1e-9 * (1.0 + std::fabs(from.y) + std::fabs(to.y));
    const bool v_constant = std::fabs(to.y - from.y) <= tol_v;
    return v_constant ? u_divisions : v_divisions;
  };
  // Edge 0's own opposite edge is edge 2 (both constant-b); edge 1's is
  // edge 3 (both constant-a) - see BuildConformingPlainQuadMesh's own
  // doc comment for this a/b convention.
  auto opposite_edge = [](int e) { return (e + 2) % 4; };

  for (size_t a = 0; a < quad_faces.size(); ++a) {
    for (size_t b = a + 1; b < quad_faces.size(); ++b) {
      const PlainQuadFace& qa = quad_faces[a];
      const PlainQuadFace& qb = quad_faces[b];
      for (int ea = 0; ea < 4; ++ea) {
        const Point3d a_from = PlainQuadEdgeFrom(qa.corner, ea);
        const Point3d a_to = PlainQuadEdgeTo(qa.corner, ea);
        const double edge_len = a_from.DistanceTo(a_to);
        if (edge_len < 1e-12) continue;  // degenerate - nothing to match
        const double lin_tol = std::max(1e-9, edge_len * 1e-6);
        for (int eb = 0; eb < 4; ++eb) {
          const Point3d b_from = PlainQuadEdgeFrom(qb.corner, eb);
          const Point3d b_to = PlainQuadEdgeTo(qb.corner, eb);
          const bool reversed = a_from.DistanceTo(b_to) <= lin_tol && a_to.DistanceTo(b_from) <= lin_tol;
          const bool forward = !reversed && a_from.DistanceTo(b_from) <= lin_tol && a_to.DistanceTo(b_to) <= lin_tol;
          if (!reversed && !forward) continue;  // not the same physical edge

          const int count_a = natural_count(qa, ea);
          const int count_b = natural_count(qb, eb);
          // Restricted to u_divisions == v_divisions (see this function's
          // own doc comment for the full reasoning): at unequal divisions,
          // which physical axis (x/y/z) a wall assigns to "u" vs "v" is
          // NOT the same for every wall (confirmed directly - Box()'s own
          // front and back walls assign it oppositely), so two DIFFERENT
          // already-forced walls bordering the SAME plain-quad neighbor
          // (e.g. a box's untouched cap, bordered by all 4 walls) can
          // legitimately want that neighbor's own OPPOSITE edges forced to
          // two DIFFERENT counts - a conflict no single per-pair choice of
          // `shared_count` can resolve, since BuildConformingPlainQuadMesh
          // needs BOTH of a quad's own opposite edges internally consistent
          // (see its own doc comment). At EQUAL divisions this conflict is
          // structurally impossible: u_divisions and v_divisions are the
          // same NUMBER, so every wall's own wedge-forced count is
          // identical regardless of which axis convention that wall uses -
          // confirmed directly, not merely assumed (an earlier version of
          // this fix applied the same trigger unconditionally and produced
          // a genuinely WORSE-open mesh than before at an asymmetric
          // divisions pair, from exactly this cap-vs-multiple-walls
          // conflict). Asymmetric divisions combined with a one-sided
          // wedge is nonetheless closed today - not by widening this
          // trigger, but downstream of it: a quad whose two opposite
          // edges end up with different forced sets is meshed per row by
          // BuildConformingPlainQuadStripMesh, each edge row exactly its
          // own chain, so the two counts never need reconciling at all -
          // see TessellateConforming()'s own doc comment in brep.h.
          const bool a_already_forced =
              u_divisions == v_divisions && already_forced != nullptr && already_forced->count(qa.face_index) != 0;
          const bool b_already_forced =
              u_divisions == v_divisions && already_forced != nullptr && already_forced->count(qb.face_index) != 0;
          const bool exactly_one_already_forced = a_already_forced != b_already_forced;
          if (count_a == count_b && !exactly_one_already_forced) {
            continue;  // already agrees, and neither side's dispatch is pinned to a differing path
          }

          int shared_count = std::max(u_divisions, v_divisions);
          if (exactly_one_already_forced) {
            const int forced_face = a_already_forced ? qa.face_index : qb.face_index;
            const int forced_edge = a_already_forced ? ea : eb;
            const std::vector<EdgeForce>& opposite =
                already_forced->at(forced_face)[static_cast<size_t>(opposite_edge(forced_edge))];
            // Count DISTINCT t-values, not the raw vector size: a wedge's
            // own straight-edge match (see the second matching pass above)
            // can push the SAME t twice - once as the last point of one
            // matched rail segment, once as the first point of the next -
            // whenever two adjacent rails were not immediately consecutive
            // in trim-loop order (so the leading-point dedup that pass
            // applies to itself never saw them as neighbors). Using the
            // raw size here would silently pick the wrong shared_count
            // (confirmed directly: it produced a 9-division row from an
            // opposite edge that is genuinely only 8 divisions with one
            // duplicated midpoint).
            std::vector<double> distinct_t;
            distinct_t.reserve(opposite.size());
            for (const EdgeForce& f : opposite) distinct_t.push_back(f.t);
            std::sort(distinct_t.begin(), distinct_t.end());
            distinct_t.erase(std::unique(distinct_t.begin(), distinct_t.end(),
                                          [](double x, double y) { return std::fabs(x - y) <= 1e-9; }),
                              distinct_t.end());
            if (distinct_t.size() >= 2) shared_count = static_cast<int>(distinct_t.size()) - 1;
          }
          std::vector<EdgeForce>& fa = forces[qa.face_index][static_cast<size_t>(ea)];
          std::vector<EdgeForce>& fb = forces[qb.face_index][static_cast<size_t>(eb)];
          for (int s = 0; s <= shared_count; ++s) {
            const double t = static_cast<double>(s) / static_cast<double>(shared_count);
            const Point3d p = a_from + t * (a_to - a_from);
            fa.push_back({t, p});
            fb.push_back({reversed ? 1.0 - t : t, p});
          }
        }
      }
    }
  }
  return forces;
}

}  // namespace

// Tessellates each face into a triangle mesh via NurbsSurface's grid
// tessellator (see its comment for why this doesn't go through
// OpenNURBS' own CreateMesh). One Mesh per face, in face order.
// `u_divisions`/`v_divisions` apply to every face's own parameter
// domain. Faces built by TrimmedPlanarFace() are tessellated against
// their trim loop; every other face here is untrimmed.
//
// When u_divisions != v_divisions, two adjacent "plain quad" planar
// faces (see CollectPlainQuadFaces's own doc comment for the exact
// shape - every wall of Box(), and every rectangular-in-its-own-(u,v)
// quad built by FromMixedFaces()/FromPlanarFaces()/ExactConvexHull())
// that share a boundary edge but would otherwise sample it at two
// DIFFERENT point counts - because which physical axis (x, y, or z)
// plays u vs v differs per face; see ComputePlainQuadSeamForces's own
// doc comment for the exact mechanism and Brep::Box()'s own comment for
// a directly-confirmed example - now share the LITERAL same boundary
// points instead, closing that seam: Mesh::MergeAndWeld(Tessellate(u,
// v)) (i.e. TessellateToClosedMesh(u, v)) is a genuine
// Mesh::IsClosedManifold() for such a Brep at ANY u_divisions/
// v_divisions pair, not only when the two happen to be equal (falsified,
// were it untrue, by TestBoxAsymmetricDivisionsIsClosedManifold and its
// siblings in this kernel's own test file).
//
// This is a full structural bypass, not a heuristic: when u_divisions ==
// v_divisions, every edge in the whole Brep has the SAME natural sample
// count on both sides by construction (there is only one division count
// to disagree about), so the new matching pass above finds zero
// mismatches and leaves EVERY face's tessellation exactly as it was -
// bit-for-bit identical to this method's own pre-fix behavior (verified
// directly: TestTessellateSymmetricDivisionsUnaffectedByAsymmetricFix
// compares raw mesh vertex/face data float-for-float against a
// same-shape asymmetric call's own untouched faces).
//
// Scope limits, honestly: only a face whose own visible boundary is
// EXACTLY 4 points (explicit or the implicit domain rectangle), with no
// hole loops, and whose own (u, v) corners form an axis-aligned
// rectangle in that face's own parameter domain, ever participates (see
// CollectPlainQuadFaces's own doc comment for the concrete gaps this
// deliberately leaves for a face shaped otherwise - a genuinely trimmed
// non-4-corner planar face, a 4-corner face that ALSO has a hole, or a
// quad face that is only a rectangle in 3D but NOT in its own (u, v)
// domain, e.g. one face of a hulled, arbitrarily-ROTATED box - confirmed
// directly to be a real, PRE-EXISTING, SEPARATE gap unaffected by this
// fix either way). A non-planar face (cylindrical, conical, spherical)
// is never part of this pass either. This fix is Tessellate()-only: it
// does not touch TessellateConforming() at all - TessellateConforming()
// had its own, separately-disclosed version of this exact same gap for
// its own quad-vs-quad case (two adjacent plain quads, neither a wedge),
// left open by this change. A LATER, separate fix closed it there too,
// by reusing this same CollectPlainQuadFaces()/ComputePlainQuadSeamForces()
// machinery directly from inside TessellateConforming() itself - see that
// method's own doc comment (brep.h) for the exact mechanism and
// falsifiable claim.
std::vector<Mesh> Brep::Tessellate(int u_divisions, int v_divisions) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));

  const int n = brep_.m_F.Count();
  std::vector<FaceGeometry> fgs(static_cast<size_t>(n));
  std::vector<bool> resolved(static_cast<size_t>(n), false);
  for (int i = 0; i < n; ++i) {
    resolved[static_cast<size_t>(i)] =
        ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fgs[static_cast<size_t>(i)]);
  }

  std::vector<PlainQuadFace> quad_faces;
  std::unordered_map<int, std::array<std::vector<EdgeForce>, 4>> plain_forces;
  if (u_divisions != v_divisions) {
    quad_faces = CollectPlainQuadFaces(fgs, resolved);
    plain_forces = ComputePlainQuadSeamForces(quad_faces, u_divisions, v_divisions);
  }

  for (int i = 0; i < n; ++i) {
    if (!resolved[static_cast<size_t>(i)]) continue;
    FaceGeometry& fg = fgs[static_cast<size_t>(i)];
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    const auto plain_it = plain_forces.find(i);
    if (plain_it != plain_forces.end()) {
      std::array<Point3d, 4> corner{};
      for (const PlainQuadFace& qf : quad_faces) {
        if (qf.face_index == i) {
          corner = qf.corner;
          break;
        }
      }
      result.push_back(BuildConformingPlainQuadMesh(corner, u_divisions, v_divisions, plain_it->second[0],
                                                      plain_it->second[2], plain_it->second[3],
                                                      plain_it->second[1]));
    } else if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExact(u_divisions, v_divisions, fg.outer));
      if (std::getenv("DINO8_BOOL_DEBUG_VERBOSE")) {
        std::fprintf(stderr, "  Tessellate: face_index=%d exact_clip outer.size()=%zu m_bRev=%d area=%.6f\n", i,
                     fg.outer.size(), (int)brep_.m_F[i].m_bRev, result.back().Area());
      }
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

// Tessellate() followed by Mesh::MergeAndWeld() - the combination that
// actually produces a single closed, boolean-ready mesh from a closed
// Brep like Box(). Tessellate() alone leaves each face's tessellation as
// a separate mesh with its own copy of shared-edge vertices; this is
// what welds those seams shut - including, now, the u_divisions !=
// v_divisions plain-quad seams Tessellate() itself closes (see its own
// doc comment): the literal shared boundary points that fix forces are
// exactly what lets this weld succeed at ANY divisions pair, not just a
// symmetric one.
Mesh Brep::TessellateToClosedMesh(int u_divisions, int v_divisions) const {
  return Mesh::MergeAndWeld(Tessellate(u_divisions, v_divisions));
}

std::vector<Mesh> Brep::TessellateConforming(int u_divisions, int v_divisions, int boundary_samples) const {
  if (u_divisions < 1 || v_divisions < 1) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::TessellateConforming: u_divisions and v_divisions must be at least 1");
  }
  if (boundary_samples < 0) boundary_samples = std::max(u_divisions, v_divisions);
  if (boundary_samples < 1) {
    throw std::invalid_argument("dino8::kernel::Brep::TessellateConforming: boundary_samples must be at least 1");
  }

  const int n = brep_.m_F.Count();
  std::vector<FaceGeometry> fgs(static_cast<size_t>(n));
  std::vector<bool> resolved(static_cast<size_t>(n), false);
  for (int i = 0; i < n; ++i) {
    resolved[static_cast<size_t>(i)] =
        ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fgs[static_cast<size_t>(i)]);
  }

  // Every face that resolves to a CylindricalFace, recovered exactly as
  // MixedFaces() already does (same ExtractCylindricalFace() helper) -
  // see this method's own doc comment.
  struct CylEntry {
    int face_index = 0;
    Brep::CylindricalFace cf;
  };
  std::vector<CylEntry> cyls;
  // Indexes `cyls` by its own `face_index`, for the fallback-breakpoint
  // lookup in the dispatch loop below (a CylindricalFace fragment with
  // zero `cyl_matches` entries needs its OWN CylindricalFace record -
  // frame, radius, angle - to compute CanonicalCylinderUBreakpoints;
  // see that helper's own doc comment above BuildConformingCylinderMesh).
  std::unordered_map<int, const Brep::CylindricalFace*> cyl_by_face;
  for (int i = 0; i < n; ++i) {
    if (!resolved[static_cast<size_t>(i)]) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fgs[static_cast<size_t>(i)].surface;
    if (wrapper.IsPlanar()) continue;
    ON_Cylinder cyl;
    const double cyl_tol = 1e-4;  // same scale MixedFaces() itself uses
    if (fgs[static_cast<size_t>(i)].surface.IsCylinder(&cyl, cyl_tol)) {
      CylEntry entry;
      entry.face_index = i;
      entry.cf = ExtractCylindricalFace(brep_, i, fgs[static_cast<size_t>(i)], cyl);
      cyls.push_back(std::move(entry));
    }
    // A non-cylindrical curved face (e.g. a ConicalFace) is simply never
    // matched below - it falls through to today's ordinary path, per
    // this method's own doc comment ("does not attempt to generalize").
  }
  for (const CylEntry& ce : cyls) cyl_by_face[ce.face_index] = &ce.cf;

  const double tol = 1e-9;

  std::unordered_map<int, std::vector<std::pair<SubRange, std::vector<Point3d>>>> wedge_subs;
  std::unordered_map<int, std::vector<ConformingMatch>> cyl_matches;

  for (int i = 0; i < n; ++i) {
    if (static_cast<size_t>(i) >= face_arc_runs_.size()) continue;
    const std::vector<PlanarFace::ArcRun>& runs = face_arc_runs_[static_cast<size_t>(i)];
    if (runs.empty()) continue;
    for (size_t k = 0; k < runs.size(); ++k) {
      const PlanarFace::ArcRun& run = runs[k];
      // A LITERAL run (PlanarFace::ArcRun::literal_points - the oblique
      // plane+cylinder ellipse pieces) needs no cylinder match and no
      // resampling: its points ARE the shared boundary, the very same
      // EllipsePointAt values the adjoining notched cylindrical fragment
      // meshes as its own literal notch row (BuildConformingCylinderStripMesh).
      // Registering it in `wedge_subs` verbatim routes this face to
      // BuildConformingWedgeMesh (ear-clip of the substituted loop) and
      // into the straight-edge pass below, exactly like an arc wedge -
      // `arc_excluded` there already treats the run's indices as
      // non-straight. Nothing is pushed into `cyl_matches`: the
      // cylindrical side's row is the notch side table's, not a
      // resampled cap arc.
      if (!run.literal_points.empty()) {
        if (run.count > 0 && static_cast<size_t>(run.count) == run.literal_points.size()) {
          wedge_subs[i].push_back({SubRange{run.begin, run.count}, run.literal_points});
        }
        continue;
      }
      Vector3d normal = ON_CrossProduct(run.plane_xaxis, run.plane_yaxis);
      if (!normal.Unitize()) continue;  // degenerate stored basis - skip, falls through to ordinary path

      ON_Plane wedge_plane;
      wedge_plane.origin = run.center;
      wedge_plane.xaxis = run.plane_xaxis;
      wedge_plane.yaxis = run.plane_yaxis;
      wedge_plane.zaxis = normal;
      wedge_plane.UpdateEquation();

      // SameCircleAsCylinder (above) matches purely by circle IDENTITY -
      // same axis line, radius, and normal - with no reference to angular
      // sweep at all, which was completely unambiguous for every producer
      // of a CylindricalFace boolean operand before the parallel-axis
      // cylinder/cylinder increment (case (iv) of
      // SplitMixedAgainstAllFaces, boolean.cpp): every one of those only
      // ever emitted a SINGLE, full 2*pi wall fragment per physical
      // circle. That increment's own SplitCylindricalByParallelCylinder
      // can legitimately produce TWO separate, genuinely PARTIAL-angle
      // wall fragments sharing the exact same circle in one result (two
      // angular children of the same original cylinder) - so multiple
      // `cyls` entries can now pass SameCircleAsCylinder for the same
      // wedge run, and picking the FIRST one unconditionally (this
      // method's own prior behavior) can pair a cap wedge with the WRONG
      // co-circular wall fragment, silently corrupting the shared
      // boundary re-sampling with an out-of-range angle. Confirmed
      // directly during that increment's own development (not a
      // theoretical worry): a two-wedge parallel-cylinder Union measured
      // a wildly wrong tessellated volume and a non-manifold mesh before
      // this disambiguation was added, traced to exactly this ambiguity.
      // Fixed here by additionally requiring the run's own MIDPOINT angle
      // (converted into the candidate's own local frame via
      // ConvertAngleBetweenFrames, the same conversion this method
      // already performs per-sample below) to genuinely fall inside that
      // candidate's own local [0, cf.angle] sweep - trivially satisfied
      // for every PRE-EXISTING full 2*pi candidate (so every prior,
      // single-wall scenario's own behavior is completely unchanged; see
      // TestTessellateConformingSymmetricDivisionsUnaffectedByQuadQuadFix's
      // own sibling precedent for this kind of "gated, provably inert on
      // old callers" claim), while correctly refusing a genuinely
      // partial-angle candidate whose own sweep does not contain this
      // particular run.
      //
      // A SECOND disambiguation axis, alongside the angular one above,
      // found and fixed by direct counterexample during the later
      // parallel-axis Intersection/Difference increment (not anticipated
      // by the angular-only fix's own original scoping): once
      // SplitCylindricalByOtherCylinderAxialExtent (boolean.cpp) can
      // legitimately produce TWO OR MORE cylindrical fragments that share
      // not just the same circle (axis + radius) but the EXACT SAME
      // rotated angular frame too (the SAME angular wedge, cut into
      // several axial bands at different heights), the angle-containment
      // check alone can no longer tell them apart - EVERY axial sibling's
      // own local sweep [0, ce.cf.angle] identically contains the SAME
      // run's own midpoint angle, since they share the identical rotation.
      // Confirmed directly: an end cap's own run at height h, matched
      // against the FIRST such axial sibling found (regardless of that
      // sibling's own actual height range), silently computed a bogus
      // `height` relative to the WRONG sibling's own frame.origin -
      // landing at neither that wrong sibling's v=0 nor v=length, so the
      // run was skipped entirely (falling through to this cap's own
      // un-reconciled, independently-sampled polygon boundary) instead of
      // being resampled to match its own TRUE adjoining wall band, leaving
      // a genuine (if visually small) crack of unwelded near-duplicate
      // vertices along that whole arc - not a full open hole, but still a
      // real non-manifold seam, caught by this increment's own
      // IsClosedManifold() checks, not by inspection. Fixed by checking
      // height/at_v0 compatibility INSIDE this same search loop (moved up
      // from after it), so a candidate that matches on angle but not on
      // height is skipped in favor of a LATER candidate, rather than
      // wrongly committing to the first angle-only match and then
      // discarding the run entirely when its height doesn't fit.
      const CylEntry* matched = nullptr;
      double matched_height = 0.0;
      bool matched_at_v0 = false;
      for (const CylEntry& ce : cyls) {
        if (!SameCircleAsCylinder(run.center, run.radius, normal, ce.cf, tol)) continue;
        const double mid_theta = 0.5 * (run.angle_begin + run.angle_end);
        double cyl_mid = detail::ConvertAngleBetweenFrames(mid_theta, wedge_plane, ce.cf.frame);
        cyl_mid = std::fmod(cyl_mid, 2.0 * ON_PI);
        if (cyl_mid < 0.0) cyl_mid += 2.0 * ON_PI;
        constexpr double kAngleContainTol = 1e-6;
        if (cyl_mid < -kAngleContainTol || cyl_mid > ce.cf.angle + kAngleContainTol) continue;

        const double height = ON_DotProduct(run.center - ce.cf.frame.origin, ce.cf.frame.zaxis);
        const double len_tol = std::max(tol, ce.cf.length * 1e-6);
        if (std::fabs(height) <= len_tol) {
          matched_at_v0 = true;
        } else if (std::fabs(height - ce.cf.length) <= len_tol) {
          matched_at_v0 = false;
        } else {
          continue;  // angle-compatible but wrong axial band - keep searching
        }
        matched = &ce;
        matched_height = height;
        break;
      }
      if (matched == nullptr) continue;

      const double height = matched_height;
      const bool at_v0 = matched_at_v0;

      const std::vector<Point3d> shared_points = detail::ArcSchedule3d(
          run.center, run.radius, run.plane_xaxis, run.plane_yaxis, run.angle_begin, run.angle_end, boundary_samples);

      const ON_Circle ref_circle(matched->cf.frame, matched->cf.radius);
      std::vector<double> raw_u(shared_points.size());
      for (int s = 0; s <= boundary_samples; ++s) {
        const double t = static_cast<double>(s) / static_cast<double>(boundary_samples);
        const double theta = run.angle_begin + (run.angle_end - run.angle_begin) * t;
        double cyl_theta = detail::ConvertAngleBetweenFrames(theta, wedge_plane, matched->cf.frame);
        // ON_Circle::GetNurbFormParameterFromRadian only accepts an angle
        // within the circle's own [0, 2*pi] radian domain (see
        // ON_Arc::GetNurbFormParameterFromRadian's own DomainRadians()
        // check) - ConvertAngleBetweenFrames has no reason to already
        // land in that range (it's a plain angle subtraction/negation),
        // so normalize here, at the one call site that actually needs
        // the NURBS-form convention, rather than inside
        // ConvertAngleBetweenFrames itself (which stays a pure, general
        // "true angle in frame A -> true angle in frame B" conversion,
        // useful regardless of any one target's own parameterization
        // convention).
        cyl_theta = std::fmod(cyl_theta, 2.0 * ON_PI);
        if (cyl_theta < 0.0) cyl_theta += 2.0 * ON_PI;
        // A PARTIAL-sweep face's own seam sample (the run endpoint sitting
        // exactly on the face's angle-0 rail) can convert to -epsilon
        // rather than +epsilon depending on the frames' relative
        // orientation, which the normalization above turns into 2*pi -
        // epsilon: a raw u far beyond the face's own trimmed sweep, that
        // would drag the row's uniform gap-fill columns across the entire
        // untrimmed far side of the cylinder. Measured directly on a
        // 60-degree Steinmetz half-band (fine at 90 degrees, where the
        // same sample converts to +epsilon). For a full 2*pi sweep both
        // readings name the same seam and nothing changes here.
        constexpr double kSeamSnapTol = 1e-6;
        if (cyl_theta > matched->cf.angle + kSeamSnapTol && cyl_theta >= 2.0 * ON_PI - kSeamSnapTol) {
          cyl_theta = 0.0;
        }
        double u = 0.0;
        if (!ref_circle.GetNurbFormParameterFromRadian(cyl_theta, &u)) {
          throw std::runtime_error(
              "dino8::kernel::Brep::TessellateConforming: ON_Circle::GetNurbFormParameterFromRadian failed "
              "converting a shared arc angle to the cylindrical face's own raw NURBS-u parameter");
        }
        raw_u[static_cast<size_t>(s)] = u;
      }

      wedge_subs[i].push_back({SubRange{run.begin, run.count}, shared_points});

      ConformingMatch match;
      match.run_index = k;
      match.cyl_face_index = matched->face_index;
      match.at_v0 = at_v0;
      match.raw_u = std::move(raw_u);
      match.points = shared_points;
      cyl_matches[matched->face_index].push_back(std::move(match));
    }
  }

  // Second matching pass: every wedge PlanarFace's own STRAIGHT (i.e.
  // non-arc) boundary segments against every "plain quad" planar face's
  // own 4 edges - the planar/planar counterpart to the arc-matching pass
  // just above, closing the SEPARATE gap that one leaves open (see this
  // method's own doc comment): where a wedge's own straight rail lies
  // exactly along an untouched Box() side wall's own cap-level edge,
  // force both sides to share the LITERAL same boundary points, exactly
  // as the arc pass already does for the wedge-cap/cylinder-wall seam.
  //
  // A "plain quad" target is any resolved PLANAR face whose own visible
  // boundary is exactly 4 points (either an explicit 4-point trim - what
  // FromMixedFaces() always builds, even for an UNTOUCHED input face,
  // per its own doc comment - or the implicit domain-corner rectangle of
  // a genuinely untrimmed face) that isn't itself already a wedge or a
  // matched CylindricalFace: a Box() side wall the drilling cylinder
  // never reaches is exactly this shape. Matching against `fg.outer`'s
  // own 4 corners directly (rather than the real surface's own u/v
  // domain) sidesteps a real subtlety a prior investigation of this gap
  // found: which physical direction (x, y, or z) plays "u" vs "v" is NOT
  // the same for every wall (confirmed directly - Box()'s own front and
  // back walls assign x/z to u/v oppositely) - operating purely on the
  // 4 corner points themselves in their own already-CCW-as-seen-from-
  // outside order needs no per-wall axis convention at all, see
  // BuildConformingPlainQuadMesh's own doc comment for exactly how.
  struct QuadFace {
    int face_index = 0;
    std::array<Point3d, 4> corner;
  };
  std::vector<QuadFace> quad_faces;
  for (int i = 0; i < n; ++i) {
    if (!resolved[static_cast<size_t>(i)]) continue;
    if (cyl_matches.count(i) != 0 || wedge_subs.count(i) != 0) continue;
    const FaceGeometry& fg = fgs[static_cast<size_t>(i)];
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (!wrapper.IsPlanar()) continue;
    std::vector<Point2d> corners_uv;
    if (fg.outer.size() == 4) {
      corners_uv = fg.outer;
    } else if (fg.outer.empty()) {
      const ON_Interval du = fg.surface.Domain(0), dv = fg.surface.Domain(1);
      corners_uv = {Point2d(du.Min(), dv.Min()), Point2d(du.Max(), dv.Min()), Point2d(du.Max(), dv.Max()),
                    Point2d(du.Min(), dv.Max())};
    } else {
      continue;  // a genuinely trimmed (non-4-corner) planar face - not this pass's target shape
    }
    QuadFace qf;
    qf.face_index = i;
    for (int c = 0; c < 4; ++c) qf.corner[static_cast<size_t>(c)] = wrapper.PointAt(corners_uv[static_cast<size_t>(c)].x, corners_uv[static_cast<size_t>(c)].y);
    quad_faces.push_back(qf);
  }

  // Per-quad-face, per-edge (0: q0->q1 at b=0, 1: q1->q2 at a=1, 2:
  // q2->q3 at b=1, 3: q3->q0 at a=0 - see BuildConformingPlainQuadMesh's
  // own doc comment for this a/b convention) forced points, keyed the
  // same way BuildConformingPlainQuadMesh itself organizes them (two
  // lists per axis, one per extreme).
  std::unordered_map<int, std::array<std::vector<EdgeForce>, 4>> plain_forces;  // index 0..3 == edge index above

  for (const auto& wsub : wedge_subs) {
    const int wedge_index = wsub.first;
    if (static_cast<size_t>(wedge_index) >= face_arc_runs_.size()) continue;
    const FaceGeometry& wfg = fgs[static_cast<size_t>(wedge_index)];
    NurbsSurface wwrapper;
    wwrapper.raw() = wfg.surface;
    const std::vector<PlanarFace::ArcRun>& runs = face_arc_runs_[static_cast<size_t>(wedge_index)];
    const size_t wn = wfg.outer.size();
    if (wn < 3) continue;
    std::vector<bool> arc_excluded(wn, false);
    for (const PlanarFace::ArcRun& run : runs) {
      const size_t begin = static_cast<size_t>(run.begin);
      const size_t count = static_cast<size_t>(run.count);
      for (size_t k = 0; k < count && k < wn; ++k) arc_excluded[(begin + k) % wn] = true;
    }

    // Every straight (both endpoints non-arc) segment of this wedge's
    // own trim loop, matched (if at all) against exactly one quad
    // face's own edge - collected in trim-loop order (increasing
    // `begin`) so the duplicate-shared-corner fix-up below (mirroring
    // this method's own arc-matching pass fix-up in spirit, see
    // BuildResampledWedgeLoop's own doc comment) can compare each
    // segment to its immediate predecessor.
    struct StraightMatch {
      int begin = 0;
      std::vector<Point3d> points;  // this wedge's own P(begin) .. P(begin+1), inclusive of both ends
    };
    std::vector<StraightMatch> straight_matches;

    for (size_t k = 0; k < wn; ++k) {
      const size_t k1 = (k + 1) % wn;
      if (arc_excluded[k] || arc_excluded[k1]) continue;  // a radial rail, not a perimeter segment - see above

      const Point3d p0 = wwrapper.PointAt(wfg.outer[k].x, wfg.outer[k].y);
      const Point3d p1 = wwrapper.PointAt(wfg.outer[k1].x, wfg.outer[k1].y);
      const double seg_len = p0.DistanceTo(p1);
      if (seg_len < 1e-12) continue;  // degenerate - nothing to match

      for (const QuadFace& qf : quad_faces) {
        bool matched_this_segment = false;
        for (int e = 0; e < 4 && !matched_this_segment; ++e) {
          // Edge e's own "from" (a=0 or b=0 end) and "to" (a=1 or b=1
          // end) reference corners, per this function's own a/b
          // convention (see BuildConformingPlainQuadMesh's doc comment):
          // edge 0 (q0->q1, b=0) and edge 2 (q3->q2, b=1) both measure
          // `a`; edge 3 (q0->q3, a=0) and edge 1 (q1->q2, a=1) both
          // measure `b`. Edge 2 and edge 3 are walked "backward" in
          // `corner`'s own trim order (q2->q3, q3->q0) relative to the
          // `a`/`b` value they carry, so their own reference pair is
          // chosen accordingly (from=q3,to=q2 for edge 2; from=q0,to=q3
          // for edge 3) rather than following `corner`'s own walk
          // direction - this is exactly what keeps a=0 anchored at
          // q0/q3 and a=1 at q1/q2 (and b symmetrically) for EVERY edge,
          // matching BuildConformingPlainQuadMesh's own convention
          // regardless of which edge a given wedge segment lands on.
          const Point3d& from = (e == 0) ? qf.corner[0] : (e == 1) ? qf.corner[1] : (e == 2) ? qf.corner[3] : qf.corner[0];
          const Point3d& to = (e == 0) ? qf.corner[1] : (e == 1) ? qf.corner[2] : (e == 2) ? qf.corner[2] : qf.corner[3];
          const Vector3d edge_vec = to - from;
          const double edge_len2 = edge_vec.LengthSquared();
          if (edge_len2 < 1e-18) continue;
          const double edge_len = std::sqrt(edge_len2);
          const double lin_tol = std::max(1e-9, edge_len * 1e-6);

          auto project = [&](const Point3d& p, double* t_out) {
            const Vector3d d = p - from;
            const double t = ON_DotProduct(d, edge_vec) / edge_len2;
            const Point3d on_line = from + t * edge_vec;
            if (p.DistanceTo(on_line) > lin_tol) return false;
            *t_out = t;
            return true;
          };
          double t0 = 0.0, t1 = 0.0;
          if (!project(p0, &t0) || !project(p1, &t1)) continue;
          const double edge_tol = lin_tol / edge_len;
          if (t0 < -edge_tol || t0 > 1.0 + edge_tol || t1 < -edge_tol || t1 > 1.0 + edge_tol) continue;
          t0 = std::clamp(t0, 0.0, 1.0);
          t1 = std::clamp(t1, 0.0, 1.0);

          // A genuine match: build this segment's own shared points by
          // PLAIN LINEAR interpolation of p0/p1 directly (never
          // re-evaluated through either face's own NURBS surface) - the
          // straight-edge analogue of detail::ArcSchedule3d, used
          // verbatim as both this wedge's own substituted boundary and
          // the matching quad face's own forced grid row/column, so the
          // two sides share their boundary EXACTLY, not just closely
          // (the same guarantee the arc-matching pass above already
          // makes for the wedge-cap/cylinder-wall seam).
          // Rounded with a tiny fixed epsilon nudge (not plain
          // std::lround) so that a wedge segment whose own true span is
          // EXACTLY a half-integer multiple of 1/divisions - the common
          // case for a straight-through hole centered (or evenly split)
          // on a wall's own edge, where two DIFFERENT wedges (e.g. the
          // top-cap and bottom-cap wedge touching the same wall) each
          // independently compute a span that SHOULD be identical but
          // differs by a few ULPs of floating-point noise from their own
          // independent constructions - rounds the SAME way on both
          // sides instead of landing on opposite sides of the tie. Both
          // wedges' own spans still agree to noise-level precision
          // (~1e-13 relative for this kernel's own coordinate scales),
          // so a fixed epsilon many orders of magnitude looser than that
          // noise, but far tighter than "the next integer's own gap",
          // resolves the tie deterministically without masking a
          // genuine (non-tied) span. A genuinely asymmetric per-wedge
          // span (e.g. a straight hole whose own top and bottom cap
          // footprints differ) is not this increment's own test scope -
          // see this method's own doc comment.
          const int divisions = (e == 0 || e == 2) ? u_divisions : v_divisions;
          const double raw_count = std::fabs(t1 - t0) * divisions;
          const int count = std::max(1, static_cast<int>(std::floor(raw_count + 0.5 + 1e-7)));
          std::vector<Point3d> shared(static_cast<size_t>(count) + 1);
          for (int s = 0; s <= count; ++s) {
            const double f = static_cast<double>(s) / static_cast<double>(count);
            shared[static_cast<size_t>(s)] = p0 + f * (p1 - p0);
          }

          straight_matches.push_back({static_cast<int>(k), shared});

          std::vector<EdgeForce>& target_list = plain_forces[qf.face_index][static_cast<size_t>(e)];
          for (int s = 0; s <= count; ++s) {
            const double f = static_cast<double>(s) / static_cast<double>(count);
            target_list.push_back({t0 + f * (t1 - t0), shared[static_cast<size_t>(s)]});
          }
          matched_this_segment = true;
        }
      }
    }

    if (straight_matches.empty()) continue;
    std::sort(straight_matches.begin(), straight_matches.end(),
              [](const StraightMatch& a, const StraightMatch& b) { return a.begin < b.begin; });
    // Drop the leading point of any match whose own segment starts
    // exactly where the PREVIOUS (already-processed) match's own
    // segment ends - both would otherwise independently include that
    // shared corner, producing a duplicate, zero-length-edge vertex in
    // the resampled loop (see BuildResampledWedgeLoop's own doc comment
    // for why this function, not that one, is responsible for avoiding
    // it).
    for (size_t m = 1; m < straight_matches.size(); ++m) {
      if (straight_matches[m].begin == straight_matches[m - 1].begin + 1 && straight_matches[m].points.size() > 1) {
        straight_matches[m].points.erase(straight_matches[m].points.begin());
      }
    }
    for (StraightMatch& sm : straight_matches) {
      wedge_subs[wedge_index].push_back({SubRange{sm.begin, 2}, std::move(sm.points)});
    }
  }

  // Third matching pass: two DIFFERENT "plain quad" planar faces (see
  // CollectPlainQuadFaces's own doc comment) that share a boundary edge
  // but where NEITHER side is a wedge or a matched CylindricalFace - the
  // pair the two passes above never look at, since both of them only
  // ever walk a wedge's own trim loop as their SOURCE side (see this
  // method's own doc comment for the concrete example: two untouched
  // Box() wall faces the drilling hole never reaches). This is exactly
  // the gap Tessellate()'s own doc comment discloses as this method's
  // "separately-disclosed, NOT-YET-closed" follow-up - now closed, by
  // reusing CollectPlainQuadFaces/ComputePlainQuadSeamForces directly
  // (both already live in this same anonymous namespace, are already
  // generic over any (fgs, resolved) pair, and already carry the
  // IsAxisAlignedQuadUv/holes guards the local `quad_faces` collection
  // above doesn't need for its own narrower wedge-vs-quad job) rather
  // than a second, hand-adapted local reimplementation that would risk a
  // second, subtly different definition of "natural count" or
  // "axis-aligned" drifting out of sync with Tessellate()'s own over
  // time - see ComputePlainQuadSeamForces's own doc comment for the
  // exact matching mechanism, shared verbatim here, not reimplemented.
  //
  // Gated on u_divisions != v_divisions, OR on the straight-edge pass
  // above having already forced at least one face into `plain_forces` -
  // a SECOND, narrower trigger closing a SEPARATE gap than the one the
  // paragraph above describes (see ComputePlainQuadSeamForces's own doc
  // comment for the full mechanism and the direct measurement that
  // surfaced it): even at EQUAL u_divisions/v_divisions, where every
  // edge's natural sample count agrees on both sides by construction, a
  // wedge-forced face (e.g. a Box() wall whose TOP edge the straight-edge
  // pass above just claimed) is now guaranteed to dispatch through
  // BuildConformingPlainQuadMesh's own bilinear grid builder for its
  // ENTIRE tessellation - including its other, still-unclaimed edges -
  // while a plain-quad neighbor across one of those unclaimed edges that
  // is NOT itself forced still dispatches through its own natural
  // default, which for a BooleanCombineMixed-reconstructed face is
  // TessellateGridClippedExact, not TessellateGrid/BuildConformingPlainQuadMesh.
  // "Natural sample count agrees" is not "produces identical points" once
  // the two sides can land on genuinely different tessellation
  // algorithms - the one-sided-boss case
  // (TestBooleanCombineMixedUnionBossFlushBaseVolumeAndCapSeamIsClosed:
  // only the box's TOP z-cap is wedge-split, its BOTTOM z-cap stays an
  // untouched, exact-clip-dispatched quad) is the first Brep in this
  // kernel's own test suite where that combination occurs. When NEITHER
  // trigger applies (u_divisions == v_divisions AND the straight-edge
  // pass forced nothing - e.g. a plain, undrilled Box()), this whole
  // block is still a complete no-op, exactly as before.
  if (u_divisions != v_divisions || !plain_forces.empty()) {
    // Candidate set: every resolved face NOT already claimed by the arc-
    // matching or straight-edge-matching passes above - i.e. the same
    // "not a wedge, not a matched cylinder" criterion the local
    // `quad_faces` collection above already applies (line-for-line: a
    // face is excluded here iff it would have been excluded there),
    // routed through CollectPlainQuadFaces so it additionally gets the
    // uv-based IsAxisAlignedQuadUv guard and holes check that pass
    // doesn't need for its own narrower job. Because those two extra
    // guards only ever narrow (never widen) CollectPlainQuadFaces's
    // result relative to the local `quad_faces` list above - both start
    // from the identical resolved-and-not-wedge/cylinder base, and both
    // require the same "planar, 4-corner-or-untrimmed" shape - every
    // face this pass can add to `plain_forces` is guaranteed to already
    // be present in `quad_faces`, so the final dispatch loop's own
    // corner lookup below always finds it.
    std::vector<bool> quad_pass_resolved = resolved;
    for (int i = 0; i < n; ++i) {
      if (cyl_matches.count(i) != 0 || wedge_subs.count(i) != 0) {
        quad_pass_resolved[static_cast<size_t>(i)] = false;
      }
    }
    const std::vector<PlainQuadFace> quad_quad_candidates = CollectPlainQuadFaces(fgs, quad_pass_resolved);
    // `plain_forces` itself (as already populated by the straight-edge
    // pass above) is passed directly as the `already_forced` map, so
    // ComputePlainQuadSeamForces can both detect the "exactly one side
    // already forced" case its own doc comment above describes AND reuse
    // an already-forced face's own opposite-edge point count to stay
    // consistent with it (see that function's own doc comment for why).
    const auto quad_quad_forces =
        ComputePlainQuadSeamForces(quad_quad_candidates, u_divisions, v_divisions, &plain_forces);

    // Merge additively: fill only edge slots the wedge/cylinder passes
    // above left empty. Never overwrite a slot that's already non-empty -
    // per the dispatch priority below (cyl_it > wedge_it > plain_it), a
    // face already claimed as a wedge or matched cylinder is excluded
    // from `quad_quad_candidates` above and so can never appear as a key
    // here; a face that IS an existing `plain_forces` target from the
    // straight-edge pass keeps its wedge-forced edges exactly as they
    // are - only its OTHER, still-unclaimed edges (if any) gain new
    // forces here. Physically this should never collide with an
    // already-forced edge (each physical edge has exactly one neighbor;
    // if that neighbor is a wedge, no other quad candidate can also
    // report matching 3D endpoints on that same edge), but the check is
    // defense-in-depth: it costs nothing and guarantees a pass-2 force
    // can never be silently clobbered even if that assumption were ever
    // violated.
    for (const auto& [face_index, edges] : quad_quad_forces) {
      std::array<std::vector<EdgeForce>, 4>& dst = plain_forces[face_index];
      for (int e = 0; e < 4; ++e) {
        if (!dst[e].empty()) continue;
        dst[e] = edges[e];
      }
    }
  }

  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    if (!resolved[static_cast<size_t>(i)]) continue;
    FaceGeometry& fg = fgs[static_cast<size_t>(i)];
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;

    const auto cyl_it = cyl_matches.find(i);
    const auto wedge_it = wedge_subs.find(i);
    const auto plain_it = plain_forces.find(i);

    const auto self_cyl_it = cyl_by_face.find(i);

    // Per-row strip routing (see this method's own doc comment, the
    // "SIXTH gap" entry, and BuildConformingCylinderStripMesh's own): a
    // hole-free cylindrical fragment takes the strip mesher iff it is
    // NOTCHED (its side-table entry is present - the literal notch
    // polyline becomes that row, closing the latent "an ArcRun match on
    // the flat end routed the whole face to the bounding-box tensor
    // mesher, which filled the notch back in" gap) or it has matches at
    // BOTH ends whose EFFECTIVE per-row column sets differ. "Effective"
    // means the forced raw_u samples PLUS the two rails, deduplicated
    // within the mesher's own u tolerance - exactly the columns
    // BuildConformingCylinderMesh would force on each row. Comparing the
    // RAW forced sets instead is wrong in a way that only a suite-wide
    // trace exposed: a full-sweep face whose two caps sample the seam at
    // u=0 on one row and at u=u_max on the other has different raw sets
    // but identical effective columns, and treating those as "different"
    // re-routed ~15 faces that the shared-list tensor mesher already
    // closed. With the effective comparison the only re-routed faces are
    // the ones that genuinely could not have been closed before: an
    // unforced column on a matched row is a T-junction against that
    // cap's literal loop, which Mesh::IsClosedManifold() rejects.
    const CylinderNotchRows* notch_rows = nullptr;
    if (static_cast<size_t>(i) < face_notch_rows_.size() && face_notch_rows_[static_cast<size_t>(i)].present) {
      notch_rows = &face_notch_rows_[static_cast<size_t>(i)];
    }
    bool rows_differ = false;
    double u_min = 0.0, u_max = 0.0, v_min = 0.0, v_max = 0.0, u_tol = 0.0, v_tol = 0.0;
    if (self_cyl_it != cyl_by_face.end() && !fg.outer.empty()) {
      u_min = u_max = fg.outer[0].x;
      v_min = v_max = fg.outer[0].y;
      for (const Point2d& p : fg.outer) {
        u_min = std::min(u_min, p.x);
        u_max = std::max(u_max, p.x);
        v_min = std::min(v_min, p.y);
        v_max = std::max(v_max, p.y);
      }
      u_tol = std::max(1e-12, std::max(u_max - u_min, 1e-300) * 1e-9);
      v_tol = std::max(1e-12, (v_max - v_min) * 1e-9);
      if (cyl_it != cyl_matches.end()) {
        std::vector<double> row0, row1;
        for (const ConformingMatch& m : cyl_it->second) {
          std::vector<double>& row = m.at_v0 ? row0 : row1;
          row.insert(row.end(), m.raw_u.begin(), m.raw_u.end());
        }
        if (!row0.empty() && !row1.empty()) {
          for (std::vector<double>* row : {&row0, &row1}) {
            row->push_back(u_min);
            row->push_back(u_max);
          }
          rows_differ = !SameEffectiveRowSet(row0, row1, u_tol);
        }
      }
    }
    const bool take_strip =
        self_cyl_it != cyl_by_face.end() && !fg.outer.empty() && fg.holes.empty() && (notch_rows != nullptr || rows_differ);
    if (take_strip) {
      static const std::vector<ConformingMatch> kNoMatches;
      const std::vector<ConformingMatch>& matches = cyl_it != cyl_matches.end() ? cyl_it->second : kNoMatches;
      // A flat row sits at the side table's own recorded end height when
      // the face is notched (its trim bounding box may have been widened
      // by the OTHER end's notch), else at the trim's own v extreme -
      // the same value BuildConformingCylinderMesh's v_min/v_max are.
      const double v_end0 = notch_rows != nullptr ? notch_rows->v_end0 : v_min;
      const double v_end1 = notch_rows != nullptr ? notch_rows->v_end1 : v_max;
      const RowChain bottom = (notch_rows != nullptr && !notch_rows->cap0_points.empty())
                                  ? BuildNotchRowChain(notch_rows->cap0_points, notch_rows->cap0_uv)
                                  : BuildFlatRowChain(u_min, u_max, v_end0, u_tol, u_divisions, matches, /*at_v0=*/true);
      const RowChain top = (notch_rows != nullptr && !notch_rows->cap1_points.empty())
                               ? BuildNotchRowChain(notch_rows->cap1_points, notch_rows->cap1_uv)
                               : BuildFlatRowChain(u_min, u_max, v_end1, u_tol, u_divisions, matches, /*at_v0=*/false);
      result.push_back(BuildConformingCylinderStripMesh(wrapper, bottom, top, v_divisions, u_tol, v_tol));
    } else if (cyl_it != cyl_matches.end()) {
      result.push_back(BuildConformingCylinderMesh(wrapper, fg.outer, u_divisions, v_divisions, cyl_it->second));
    } else if (self_cyl_it != cyl_by_face.end() && fg.holes.empty() && IsRectangularTrimUv(fg.outer)) {
      // A CylindricalFace fragment with NO ArcRun match on either end at
      // all (see boolean.h's own disclosure) - a "friendless" middle
      // axial band. Gated on the trim being a plain (u, v) rectangle with
      // no holes (IsRectangularTrimUv - see its own doc comment): the
      // mesher below grids the bounding box, so a NOTCHED, cap-less
      // fragment (the oblique path's surviving hole-wall) must instead
      // keep the trim-clipping path further down, exactly as
      // Tessellate() treats it. Rather than falling back to NurbsSurface::TessellateGrid's
      // raw-u-uniform division (this branch's own prior behavior, and
      // exactly the mismatch that left an unwelded seam against any
      // axially-adjacent, ArcRun-matched sibling sharing this same
      // wedge), gather every OTHER face's own `cyl_matches` entries whose
      // CylindricalFace record is the SAME wedge as this one
      // (SameWedgeAsCylinder - see its own doc comment for why "same
      // wedge", not merely "same circle", is the right identity here) and
      // reuse their literal `raw_u` breakpoints as this face's own
      // fallback schedule (see BuildConformingCylinderMesh's own doc
      // comment for `fallback_u_breakpoints` for why reusing a real
      // sibling's own accumulated breakpoints, rather than independently
      // resampling this face's own [0, cf.angle] sweep uniformly, is the
      // part that actually reproduces the exact values a capped sibling's
      // shared boundary row already committed to). Every genuine
      // boolean-produced middle band has at least one such sibling
      // somewhere in the result (it was axially split FROM one, by
      // SplitCylindricalByOtherCylinderAxialExtent); CanonicalCylinderUBreakpoints
      // (see its own doc comment) is kept purely as a defensive fallback
      // for the case that assumption is ever violated.
      std::vector<double> fallback;
      for (const auto& [other_face, other_matches] : cyl_matches) {
        const auto other_cf_it = cyl_by_face.find(other_face);
        if (other_cf_it == cyl_by_face.end()) continue;
        if (!SameWedgeAsCylinder(*self_cyl_it->second, *other_cf_it->second, tol)) continue;
        for (const ConformingMatch& m : other_matches) {
          fallback.insert(fallback.end(), m.raw_u.begin(), m.raw_u.end());
        }
      }
      if (fallback.empty()) {
        fallback = CanonicalCylinderUBreakpoints(*self_cyl_it->second, boundary_samples);
      }
      result.push_back(
          BuildConformingCylinderMesh(wrapper, fg.outer, u_divisions, v_divisions, {}, fallback));
    } else if (wedge_it != wedge_subs.end()) {
      result.push_back(BuildConformingWedgeMesh(wrapper, fg.outer, wedge_it->second));
    } else if (plain_it != plain_forces.end()) {
      std::array<Point3d, 4> corner;
      for (const QuadFace& qf : quad_faces) {
        if (qf.face_index == i) {
          corner = qf.corner;
          break;
        }
      }
      // Per-row strip routing for a plain quad (the planar twin of the
      // cylindrical dispatch above): a quad whose two OPPOSITE edges
      // carry different EFFECTIVE forced t-sets (forced t plus the two
      // corners, deduplicated within the mesher's own 1e-9) goes to
      // BuildConformingPlainQuadStripMesh - the tensor mesher's shared
      // per-axis column list would put one edge's forced columns on the
      // other edge's row as unforced T-junctions, so a differing pair is
      // never closed by it today and re-routing cannot un-close anything.
      // Edge 0 vs 2 (both constant-b, measuring a) differing is the
      // mesher's native shape; edge 3 vs 1 (constant-a, measuring b)
      // differing is the same shape on the transposed quad {q0,q3,q2,q1}
      // (whose edge 0 is the original edge 3, edge 2 the original edge 1,
      // edge 3 the original edge 0 and edge 1 the original edge 2, each
      // keeping its own from/to convention), which is wound clockwise as
      // seen from outside, so its triangles are flipped back. Both axes
      // differing (no fixture in this kernel produces it) or neither:
      // the tensor mesher, bit-for-bit as before.
      const std::array<std::vector<EdgeForce>, 4>& forces = plain_it->second;
      auto effective_t = [](const std::vector<EdgeForce>& f) {
        std::vector<double> t{0.0, 1.0};
        for (const EdgeForce& e : f) t.push_back(e.t);
        return t;
      };
      const bool a_differs = !SameEffectiveRowSet(effective_t(forces[0]), effective_t(forces[2]), 1e-9);
      const bool b_differs = !SameEffectiveRowSet(effective_t(forces[3]), effective_t(forces[1]), 1e-9);
      if (a_differs && !b_differs) {
        result.push_back(BuildConformingPlainQuadStripMesh(corner, u_divisions, v_divisions, forces[0], forces[2],
                                                           forces[3], forces[1]));
      } else if (b_differs && !a_differs) {
        const std::array<Point3d, 4> transposed = {corner[0], corner[3], corner[2], corner[1]};
        result.push_back(BuildConformingPlainQuadStripMesh(transposed, v_divisions, u_divisions, forces[3], forces[1],
                                                           forces[0], forces[2])
                             .FlipNormals());
      } else {
        result.push_back(BuildConformingPlainQuadMesh(corner, u_divisions, v_divisions, forces[0], forces[2],
                                                        forces[3], forces[1]));
      }
    } else if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExact(u_divisions, v_divisions, fg.outer));
      if (std::getenv("DINO8_BOOL_DEBUG_VERBOSE")) {
        std::fprintf(stderr, "  Tessellate: face_index=%d exact_clip outer.size()=%zu m_bRev=%d area=%.6f\n", i,
                     fg.outer.size(), (int)brep_.m_F[i].m_bRev, result.back().Area());
      }
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMeshConforming(int u_divisions, int v_divisions, int boundary_samples) const {
  return Mesh::MergeAndWeld(TessellateConforming(u_divisions, v_divisions, boundary_samples));
}

// ---------------------------------------------------------------------------
// MergeCoplanarFaces / ReplaceEdgeCurve / UnjoinEdge - real B-rep topology
// surgery, operating directly on this class' own ON_Brep member (m_E/m_T/
// m_L/m_F), never through Manifold (see brep.h's own doc comment on each
// method for the full rationale/contract).
// ---------------------------------------------------------------------------

namespace {

// ON_Brep::SetEdgeTolerance() (the plain OpenNURBS base-class version this
// kernel links against, not Rhino's own internal TL_Brep override) is
// documented, directly in its own vendored source, to do nothing useful
// for an edge that has any trims: it unconditionally sets
// edge.m_tolerance to ON_UNSET_VALUE and leaves an actual computation to
// TL_Brep, which isn't part of open-source OpenNURBS at all. So calling
// ON_Brep::SetTolerancesBoxesAndFlags() - needed here for its OTHER
// bookkeeping (vertex/trim tolerances, trim iso/type flags, loop types,
// trim boxes, all of which the base class computes for real) - silently
// RESETS every trimmed edge's own tolerance to invalid on the way, even
// one this class itself had already set correctly (e.g. FromPlanarFaces()'s
// own exact 0.0 - see BuildFaceLoop's own comment). Every edge this
// class's own topology surgery touches is built from an EXACT duplicate
// or exact substitute curve (DuplicateCurve(), or ReplaceEdgeCurve()'s own
// caller-supplied curve), so 0.0 - "this edge's curve IS the boundary,
// not an approximation of it" - is the same honest, exact claim this
// class already makes elsewhere, not a guess. Called after
// SetTolerancesBoxesAndFlags(), never before, so it's this call that has
// the final say.
void FixUnsetEdgeTolerances(ON_Brep& b) {
  for (int i = 0; i < b.m_E.Count(); ++i) {
    ON_BrepEdge& e = b.m_E[i];
    if (e.m_edge_index >= 0 && !(e.m_tolerance >= 0.0)) e.m_tolerance = 0.0;
  }
}

// Joins coincident naked (single-trim) edges of `b` within `tol` by
// position (or, for a closed edge, by tangent direction at its start) -
// the same technique dino8-app's own JoinNakedEdges (cmd_common.h)
// already uses, duplicated here rather than shared because the app layer
// is a separate target this kernel library cannot depend on.
// Brep::MergeCoplanarFaces() uses this to re-weld a freshly-merged face's
// own boundary onto whatever naked edges deleting its two source faces
// exposed on their other, untouched neighbors.
int WeldCoincidentNakedEdges(ON_Brep& b, double tol) {
  int joined = 0;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    ON_BrepEdge& e0 = b.m_E[i];
    if (e0.m_edge_index < 0 || e0.TrimCount() != 1) continue;
    const ON_3dPoint a0 = e0.PointAtStart(), a1 = e0.PointAtEnd();
    for (int j = i + 1; j < b.m_E.Count(); ++j) {
      ON_BrepEdge& e1 = b.m_E[j];
      if (e1.m_edge_index < 0 || e1.TrimCount() != 1) continue;
      const ON_3dPoint p0 = e1.PointAtStart(), p1 = e1.PointAtEnd();
      bool forward = a0.DistanceTo(p0) <= tol && a1.DistanceTo(p1) <= tol;
      bool reversed = !forward && a0.DistanceTo(p1) <= tol && a1.DistanceTo(p0) <= tol;
      if (forward && a0.DistanceTo(a1) <= tol) {
        // Closed edges: endpoints alone say nothing about direction.
        forward = ON_DotProduct(e0.TangentAt(e0.Domain().Min()), e1.TangentAt(e1.Domain().Min())) > 0;
        reversed = !forward;
      }
      if (!forward && !reversed) continue;
      const ON_3dPoint m0 = e0.PointAt(e0.Domain().Mid()), m1 = e1.PointAt(e1.Domain().Mid());
      if (m0.DistanceTo(m1) > tol * 10) continue;
      if (reversed && !e1.Reverse()) continue;
      for (int k = 0; k < 2; ++k) {
        if (e0.m_vi[k] == e1.m_vi[k]) continue;
        if (!b.CombineCoincidentVertices(b.m_V[e0.m_vi[k]], b.m_V[e1.m_vi[k]])) break;
      }
      if (b.CombineCoincidentEdges(e0, e1)) { ++joined; break; }
    }
  }
  return joined;
}

// Attempts to merge faces `fa`/`fb` of `b`, already confirmed coplanar and
// sharing EXACTLY one edge (`shared_edge_index`, with exactly two trims),
// into a single face on `plane`. On success, deletes both source faces,
// appends the merged one, re-welds any naked edges the deletion exposed
// on their other neighbors, and returns true. Returns false (leaving `b`
// completely untouched) if the merge boundary can't be spliced into one
// simple closed loop or ON_BrepTrimmedPlane refuses it - the caller
// treats that the same as "not eligible", not an error: a face pair that
// merely LOOKS mergeable (coplanar, one shared 2-trim edge) can still
// fail here, e.g. if the two loops' own stored trim directions aren't the
// standard opposite pair a valid 2-manifold edge is expected to have.
bool TryMergeCoplanarPair(ON_Brep& b, int fa, int fb, int shared_edge_index, const ON_Plane& plane, double tol) {
  const ON_BrepFace& face_a = b.m_F[fa];
  const ON_BrepFace& face_b = b.m_F[fb];
  if (face_a.LoopCount() != 1 || face_b.LoopCount() != 1) return false;
  const ON_BrepLoop& loop_a = *face_a.Loop(0);
  const ON_BrepLoop& loop_b = *face_b.Loop(0);

  std::vector<std::unique_ptr<ON_Curve>> owned;

  // Builds the open boundary path that remains once the trim using
  // `shared_edge_index` is removed from `loop`, walked in the loop's own
  // stored order starting right after that trim - i.e. from the shared
  // edge's own "end" (in this loop's own direction) around to its own
  // "start". Returns an empty vector if the shared edge isn't found in
  // `loop` exactly once, the loop has fewer than 2 trims, or any trim
  // along the way has no edge (a singular/seam trim - out of scope here).
  auto build_path = [&](const ON_BrepLoop& loop) -> std::vector<ON_Curve*> {
    std::vector<ON_Curve*> path;
    const int n = loop.TrimCount();
    if (n < 2) return path;
    int pos = -1;
    for (int k = 0; k < n; ++k) {
      const ON_BrepTrim* t = loop.Trim(k);
      if (t && t->m_ei == shared_edge_index) {
        if (pos >= 0) return {};  // shared edge appears twice in this loop
        pos = k;
      }
    }
    if (pos < 0) return path;
    for (int step = 1; step < n; ++step) {
      const ON_BrepTrim* t = loop.Trim((pos + step) % n);
      const ON_BrepEdge* e = t ? t->Edge() : nullptr;
      if (!e) return {};
      ON_Curve* c = e->DuplicateCurve();
      if (!c) return {};
      if (t->m_bRev3d) c->Reverse();
      owned.emplace_back(c);
      path.push_back(c);
    }
    return path;
  };

  const std::vector<ON_Curve*> path_a = build_path(loop_a);
  const std::vector<ON_Curve*> path_b = build_path(loop_b);
  if (path_a.empty() || path_b.empty()) return false;

  // The standard two-manifold-edge invariant (the two faces traverse
  // their shared edge in opposite directions) means path_a's own end
  // meets path_b's own start, and vice versa - if it doesn't, this pair's
  // own topology isn't the ordinary case this merge handles, so back out
  // rather than guess.
  const double jt = std::max(tol, 1e-6);
  if (path_a.back()->PointAtEnd().DistanceTo(path_b.front()->PointAtStart()) > jt ||
      path_b.back()->PointAtEnd().DistanceTo(path_a.front()->PointAtStart()) > jt) {
    return false;
  }

  ON_SimpleArray<ON_Curve*> boundary;
  for (ON_Curve* c : path_a) boundary.Append(c);
  for (ON_Curve* c : path_b) boundary.Append(c);

  ON_Brep* merged_raw = ON_BrepTrimmedPlane(plane, boundary, /*bDuplicateCurves=*/true);
  if (!merged_raw) return false;
  ON_Brep merged = *merged_raw;
  delete merged_raw;
  if (merged.m_F.Count() != 1) return false;

  // Defensive: ON_BrepTrimmedPlane() is documented to decide the new
  // face's own m_bRev from the boundary's winding, which is not
  // guaranteed (by that function's own contract) to line up with
  // `plane.zaxis` - fa's own real outward normal, by construction above -
  // in every case. Checked here directly against the surface's own true
  // evaluated normal (NormalAt(), corrected for m_bRev) rather than
  // trusted blindly, and flipped if it disagrees: a silent mismatch here
  // would still pass IsValid()/IsManifold()/IsSolid() (orientability is a
  // topological property; it says nothing about which of the two
  // consistent orientations is actually stored) while silently reversing
  // this one face's own contribution to any divergence-theorem volume
  // integral over the whole solid.
  {
    const ON_Surface* srf = merged.m_F[0].SurfaceOf();
    const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
    ON_3dVector actual_normal = srf->NormalAt(du.Mid(), dv.Mid());
    if (merged.m_F[0].m_bRev) actual_normal = -actual_normal;
    if (ON_DotProduct(actual_normal, plane.zaxis) < 0.0) {
      merged.FlipFace(merged.m_F[0]);
    }
  }

  const int hi = std::max(fa, fb), lo = std::min(fa, fb);
  b.DeleteFace(b.m_F[hi], true);
  b.DeleteFace(b.m_F[lo], true);
  b.Compact();
  b.Append(merged);
  WeldCoincidentNakedEdges(b, std::max(tol * 20, tolerance::kEdgeJoin));
  b.Compact();
  b.SetTolerancesBoxesAndFlags();
  FixUnsetEdgeTolerances(b);
  return true;
}

}  // namespace

int Brep::MergeCoplanarFaces(double tolerance) {
  // This method mutates brep_'s own real ON_Brep topology directly
  // (ON_Brep::DeleteFace()/Append()/Compact()), which invalidates every
  // per-face side table this class otherwise carries as an exact_clip/
  // arc_runs/notch fast path (face_trim_loops_ and its five siblings
  // below, all indexed parallel to brep_.m_F - see their own doc
  // comments) - those are just parallel vectors with no knowledge of a
  // face index being deleted, appended, or renumbered by Compact(), so
  // after any of that they would silently describe the WRONG face at a
  // given index. Checked directly, not assumed: without this clear, a
  // later Tessellate() (e.g. TessellateToClosedMesh()) read a stale trim
  // polygon at a still-in-range index for whichever face now sits there,
  // corrupting that face's own tessellated shape and, through it, the
  // whole solid's divergence-theorem volume - while every topology check
  // (IsValid()/IsManifold()/IsSolid()) still passed, since those never
  // look at this class's own side tables at all. Cleared unconditionally,
  // up front, rather than patched face-by-face: every remaining face's
  // own real ON_Brep loop already describes its trim correctly (a
  // straight or simple curved 2D trim samples the same shape whether
  // read from a side-table shortcut or derived generically - see
  // ResolveFace's own fallback path), so losing the fast path costs only
  // some OTHER, unrelated curved face's verbatim record elsewhere in this
  // same Brep (a real but narrow precision-only trade-off, not a
  // correctness one) - never a wrong shape.
  face_trim_loops_.clear();
  face_exact_clip_.clear();
  face_hole_loops_.clear();
  face_arc_runs_.clear();
  face_notch_rows_.clear();
  face_records_.clear();

  const double tol = std::max(tolerance, 1e-9);
  int merges = 0;
  bool changed = true;
  while (changed) {
    changed = false;
    for (int fa = 0; fa < brep_.m_F.Count() && !changed; ++fa) {
      const ON_BrepFace& face_a = brep_.m_F[fa];
      if (face_a.m_face_index < 0 || face_a.LoopCount() != 1) continue;
      FaceGeometry fga;
      if (!ResolveFace(brep_, fa, face_trim_loops_, face_exact_clip_, face_hole_loops_, fga)) continue;
      if (!fga.holes.empty()) continue;
      NurbsSurface wa;
      wa.raw() = fga.surface;
      if (!wa.IsPlanar()) continue;
      const PlanarFace pa = ExtractPlanarFace(fga);

      const ON_BrepLoop& loop_a = *face_a.Loop(0);
      for (int k = 0; k < loop_a.TrimCount() && !changed; ++k) {
        const ON_BrepTrim* trim = loop_a.Trim(k);
        const ON_BrepEdge* edge = trim ? trim->Edge() : nullptr;
        if (!edge || edge->TrimCount() != 2) continue;  // not a manifold-safe boundary
        const int other_ti = edge->m_ti[0] == trim->m_trim_index ? edge->m_ti[1] : edge->m_ti[0];
        const ON_BrepTrim& other_trim = brep_.m_T[other_ti];
        const int fb = other_trim.FaceIndexOf();
        if (fb < 0 || fb == fa) continue;
        const ON_BrepFace& face_b = brep_.m_F[fb];
        if (face_b.LoopCount() != 1) continue;

        // fa/fb must share EXACTLY this one edge - a pair also touching
        // along a second, separate edge would not merge into one simple
        // polygon by the splice below.
        int shared_edges = 0;
        for (int m = 0; m < loop_a.TrimCount(); ++m) {
          const ON_BrepTrim* tm = loop_a.Trim(m);
          const ON_BrepEdge* em = tm ? tm->Edge() : nullptr;
          if (!em) continue;
          for (int q = 0; q < em->TrimCount(); ++q) {
            if (em->m_ti[q] == tm->m_trim_index) continue;
            if (brep_.m_T[em->m_ti[q]].FaceIndexOf() == fb) { ++shared_edges; break; }
          }
        }
        if (shared_edges != 1) continue;

        FaceGeometry fgb;
        if (!ResolveFace(brep_, fb, face_trim_loops_, face_exact_clip_, face_hole_loops_, fgb)) continue;
        if (!fgb.holes.empty()) continue;
        NurbsSurface wb;
        wb.raw() = fgb.surface;
        if (!wb.IsPlanar()) continue;
        const PlanarFace pb = ExtractPlanarFace(fgb);

        // Coplanar AND coincident: same-direction normal, and fb's plane
        // origin lies in fa's own plane.
        if (ON_DotProduct(pa.plane.zaxis, pb.plane.zaxis) < 1.0 - 1e-6) continue;
        if (std::fabs(ON_DotProduct(pa.plane.zaxis, pb.plane.origin - pa.plane.origin)) > tol) continue;

        if (TryMergeCoplanarPair(brep_, fa, fb, edge->m_edge_index, pa.plane, tol)) {
          ++merges;
          changed = true;
        }
      }
    }
  }
  return merges;
}

void Brep::ReplaceEdgeCurve(int edge_index, const NurbsCurve& new_curve, double tolerance) {
  if (edge_index < 0 || edge_index >= brep_.m_E.Count()) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::ReplaceEdgeCurve: edge_index " + std::to_string(edge_index) +
        " is out of range (this Brep has " + std::to_string(brep_.m_E.Count()) + " edge slot(s))");
  }
  ON_BrepEdge& edge = brep_.m_E[edge_index];
  if (edge.m_edge_index < 0) {
    throw std::invalid_argument("dino8::kernel::Brep::ReplaceEdgeCurve: edge_index " +
                                 std::to_string(edge_index) + " refers to a deleted edge");
  }

  const double tol = std::max(tolerance, 1e-9);
  const double vtol = std::max(tol, 1e-4);

  const ON_NurbsCurve& src = new_curve.raw();
  const ON_3dPoint new_start = src.PointAtStart();
  const ON_3dPoint new_end = src.PointAtEnd();
  const ON_3dPoint v0 = brep_.m_V[edge.m_vi[0]].point;
  const ON_3dPoint v1 = brep_.m_V[edge.m_vi[1]].point;
  const bool same_dir = new_start.DistanceTo(v0) <= vtol && new_end.DistanceTo(v1) <= vtol;
  const bool rev_dir = !same_dir && new_start.DistanceTo(v1) <= vtol && new_end.DistanceTo(v0) <= vtol;
  if (!same_dir && !rev_dir) {
    throw std::invalid_argument(
        "dino8::kernel::Brep::ReplaceEdgeCurve: new_curve's own endpoints don't land within "
        "tolerance of edge " +
        std::to_string(edge_index) +
        "'s own two vertices - a substitute curve must at least start and end where the edge it "
        "replaces does (this reshapes an edge between its own fixed endpoints; it does not "
        "re-point the topology to new ones)");
  }

  ON_NurbsCurve fitted_curve = src;
  if (rev_dir) fitted_curve.Reverse();
  const ON_Interval curve_dom = fitted_curve.Domain();

  // Phase 1: for every trim sharing this edge, re-derive its own 2D trim
  // curve via closest-point projection of `fitted_curve` onto that
  // trim's own face surface, validating as we go - WITHOUT mutating this
  // Brep at all yet, so a thrown exception here leaves every existing
  // face's trim exactly as valid as it was before this call.
  struct PendingTrim {
    int trim_index;
    std::unique_ptr<ON_NurbsCurve> curve;
  };
  std::vector<PendingTrim> pending;
  constexpr int kSamples = 24;
  for (int k = 0; k < edge.m_ti.Count(); ++k) {
    const int ti = edge.m_ti[k];
    const ON_BrepTrim& trim = brep_.m_T[ti];
    const int face_index = trim.FaceIndexOf();
    if (face_index < 0) continue;
    const ON_BrepFace& face = brep_.m_F[face_index];
    const ON_Surface* srf = face.SurfaceOf();
    if (!srf) {
      throw std::invalid_argument("dino8::kernel::Brep::ReplaceEdgeCurve: face " +
                                   std::to_string(face_index) + " sharing edge " +
                                   std::to_string(edge_index) + " has no surface");
    }
    ON_NurbsSurface ns;
    if (const auto* cast = ON_NurbsSurface::Cast(srf)) {
      ns = *cast;
    } else if (srf->GetNurbForm(ns) <= 0) {
      throw std::invalid_argument("dino8::kernel::Brep::ReplaceEdgeCurve: face " +
                                   std::to_string(face_index) + "'s surface has no NURBS form");
    }
    NurbsSurface wrapper;
    wrapper.raw() = ns;
    const ON_Interval du = ns.Domain(0), dv = ns.Domain(1);
    const double u_pad = std::max(du.Length(), 1e-9) * 1e-4;
    const double v_pad = std::max(dv.Length(), 1e-9) * 1e-4;

    std::vector<ON_3dPoint> uv_points;
    uv_points.reserve(kSamples + 1);
    double max_residual = 0.0;
    // A trim's own 2D curve always runs in ITS OWN loop-consistent
    // direction (PointAtStart() == the 3D point at trim.m_vi[0]), which
    // is the EDGE's direction only when m_bRev3d is false - reversed
    // (m_bRev3d true), it runs opposite the edge's own fitted_curve.
    // Sampling forward regardless (the previous, unconditional i=0..
    // kSamples order below) silently built the trim's new 2D curve
    // backwards for a reversed trim: same 2D shape, wrong direction, so
    // trim.PointAtStart()/PointAtEnd() swapped without trim.m_vi[]
    // knowing it - a loop whose neighbor still expects the OLD start to
    // be there sees a real discontinuity (caught running this against a
    // reversed naked trim, via RemoveNakedMicroEdge, for the first time).
    for (int i = 0; i <= kSamples; ++i) {
      const double s = trim.m_bRev3d ? static_cast<double>(kSamples - i) : static_cast<double>(i);
      const double t = curve_dom.ParameterAt(s / kSamples);
      const ON_3dPoint p3 = fitted_curve.PointAt(t);
      const Point2d uv = wrapper.ClosestPointParameter(Point3d(p3.x, p3.y, p3.z), 40, 40);
      const Point3d back = wrapper.PointAt(uv.x, uv.y);
      max_residual = std::max(max_residual, back.DistanceTo(p3));
      if (uv.x < du.Min() - u_pad || uv.x > du.Max() + u_pad || uv.y < dv.Min() - v_pad ||
          uv.y > dv.Max() + v_pad) {
        throw std::runtime_error(
            "dino8::kernel::Brep::ReplaceEdgeCurve: new_curve's own point at t=" +
            std::to_string(t) + " projects outside face " + std::to_string(face_index) +
            "'s own surface domain - this substitute curve does not reasonably fit this face");
      }
      uv_points.emplace_back(uv.x, uv.y, 0.0);
    }
    const double fit_tol = std::max(vtol * 10.0, tol * 100.0);
    if (max_residual > fit_tol) {
      throw std::runtime_error(
          "dino8::kernel::Brep::ReplaceEdgeCurve: new_curve strays " + std::to_string(max_residual) +
          " from face " + std::to_string(face_index) + "'s own surface (tolerance " +
          std::to_string(fit_tol) +
          ") - this substitute curve does not reasonably fit this face's geometry");
    }

    auto trim_curve = std::make_unique<ON_NurbsCurve>();
    if (!trim_curve->CreateClampedUniformNurbs(2, 2, static_cast<int>(uv_points.size()), uv_points.data())) {
      throw std::runtime_error("dino8::kernel::Brep::ReplaceEdgeCurve: failed to build face " +
                                std::to_string(face_index) + "'s new trim curve");
    }
    pending.push_back({ti, std::move(trim_curve)});
  }

  // Phase 2: every validation above passed, so commit - replace the
  // edge's own 3D curve, then re-point every affected trim onto its
  // freshly-built (and already-validated) 2D curve.
  auto* new_curve_heap = new ON_NurbsCurve(fitted_curve);
  const int c3i = brep_.AddEdgeCurve(new_curve_heap);
  if (!edge.ChangeEdgeCurve(c3i)) {
    throw std::runtime_error("dino8::kernel::Brep::ReplaceEdgeCurve: ON_BrepEdge::ChangeEdgeCurve "
                              "failed for edge " +
                              std::to_string(edge_index));
  }
  for (PendingTrim& pt : pending) {
    const int c2i = brep_.AddTrimCurve(pt.curve.release());
    ON_BrepTrim& trim = brep_.m_T[pt.trim_index];
    if (!trim.ChangeTrimCurve(c2i)) {
      throw std::runtime_error(
          "dino8::kernel::Brep::ReplaceEdgeCurve: ON_BrepTrim::ChangeTrimCurve failed for trim " +
          std::to_string(pt.trim_index));
    }
  }

  brep_.SetTolerancesBoxesAndFlags();
  FixUnsetEdgeTolerances(brep_);

  // The face(s) sharing this edge just had their own real trim curve
  // replaced, but this class's own per-face side tables (face_trim_loops_
  // and its siblings - see MergeCoplanarFaces()'s own doc comment for the
  // full explanation of why these go stale and what that costs) still
  // hold whatever UV polygon that face had BEFORE this edit, at the same
  // still-valid index - so a later Tessellate() would silently keep
  // showing the OLD boundary instead of the one just set here. Cleared
  // unconditionally (not just for the affected face indices) for the same
  // reason MergeCoplanarFaces() does: simple and always correct, at the
  // cost of losing an unrelated curved face's own verbatim fast-path
  // record elsewhere in this same Brep, never a wrong shape.
  face_trim_loops_.clear();
  face_exact_clip_.clear();
  face_hole_loops_.clear();
  face_arc_runs_.clear();
  face_notch_rows_.clear();
  face_records_.clear();
}

Result Brep::UnjoinEdge(int edge_index) {
  if (edge_index < 0 || edge_index >= brep_.m_E.Count()) {
    throw std::out_of_range("dino8::kernel::Brep::UnjoinEdge: edge_index " +
                             std::to_string(edge_index) + " is out of range (this Brep has " +
                             std::to_string(brep_.m_E.Count()) + " edge slot(s))");
  }
  ON_BrepEdge& edge = brep_.m_E[edge_index];
  if (edge.m_edge_index < 0) {
    throw std::invalid_argument("dino8::kernel::Brep::UnjoinEdge: edge_index " +
                                 std::to_string(edge_index) + " refers to a deleted edge");
  }
  if (edge.TrimCount() != 2) return Result::Failed;

  ON_Curve* dup = edge.DuplicateCurve();
  if (!dup) return Result::Failed;
  const int c3i = brep_.AddEdgeCurve(dup);
  ON_BrepVertex& v0 = brep_.m_V[edge.m_vi[0]];
  ON_BrepVertex& v1 = brep_.m_V[edge.m_vi[1]];
  // Cache what's still needed from `edge` before calling NewEdge: NewEdge
  // appends to brep_.m_E internally, which can reallocate that array and
  // invalidate the `edge` reference into it (a real, ASan-caught
  // heap-use-after-free when the two both continued to be read below).
  const double edge_tolerance = edge.m_tolerance;
  const int edge_ti1 = edge.m_ti[1];
  ON_BrepEdge& new_edge = brep_.NewEdge(v0, v1, c3i);
  new_edge.m_tolerance = edge_tolerance;

  // Move the SECOND of the original edge's two trims onto the new,
  // duplicate edge - AttachToEdge() is the OpenNURBS "expert user" API
  // that correctly updates both edges' own m_ti[] bookkeeping (removing
  // the trim from the old edge's list, adding it to the new edge's),
  // rather than hand-editing those arrays. The result: two edges, each
  // with exactly one trim (a naked edge, by the same TrimCount()==1 test
  // this kernel's SelNakedEdges-style detection already uses), occupying
  // the same 3D location - both faces stay in this SAME ON_Brep.
  const int ti = edge_ti1;
  ON_BrepTrim& trim = brep_.m_T[ti];
  const bool rev = trim.m_bRev3d;
  if (!trim.AttachToEdge(new_edge.m_edge_index, rev)) {
    new_edge.m_edge_index = -1;  // roll back the unused edge so a failed
    brep_.Compact();             // attempt leaves this Brep untouched
    return Result::Failed;
  }

  brep_.SetTolerancesBoxesAndFlags();
  FixUnsetEdgeTolerances(brep_);
  // Unlike MergeCoplanarFaces()/ReplaceEdgeCurve() (see their own doc
  // comments), this class's own per-face side tables do NOT need
  // invalidating here: no face was added, removed, or renumbered, and
  // both faces' own VISIBLE boundary is bit-identical to before (the
  // duplicated edge carries the exact same 3D curve content - only which
  // ON_BrepEdge object underlies each of the two now-separate trims
  // changed, not the shape either face presents).
  return Result::Ok;
}

Result Brep::RemoveNakedMicroEdge(int edge_index, double tolerance) {
  if (edge_index < 0 || edge_index >= brep_.m_E.Count()) {
    throw std::out_of_range("dino8::kernel::Brep::RemoveNakedMicroEdge: edge_index " +
                             std::to_string(edge_index) + " is out of range (this Brep has " +
                             std::to_string(brep_.m_E.Count()) + " edge slot(s))");
  }
  const ON_BrepEdge& micro = brep_.m_E[edge_index];
  if (micro.m_edge_index < 0) {
    throw std::invalid_argument("dino8::kernel::Brep::RemoveNakedMicroEdge: edge_index " +
                                 std::to_string(edge_index) + " refers to a deleted edge");
  }
  if (micro.TrimCount() != 1) return Result::Failed;  // not naked - out of scope

  // Must actually BE a micro edge - same length measure (GetNurbForm +
  // a 20-sample polyline length) the app layer's own detection already
  // uses (cmd_srfedit.cpp's RemoveAllNakedMicroEdges).
  {
    ON_NurbsCurve nc;
    if (micro.GetNurbForm(nc) <= 0) return Result::Failed;
    NurbsCurve len_check;
    len_check.raw() = nc;
    if (len_check.Length(20) >= tolerance) return Result::Failed;
  }

  const int ti = micro.m_ti[0];
  if (ti < 0 || ti >= brep_.m_T.Count()) return Result::Failed;
  const ON_BrepTrim& trim = brep_.m_T[ti];
  if (trim.m_li < 0 || trim.m_li >= brep_.m_L.Count()) return Result::Failed;

  // The two edges flanking this one in its own loop - the "faces on
  // either side" this re-trims (both happen to be the SAME face here,
  // since this is scoped to a naked edge's own single-face loop).
  const int ti_prev = brep_.PrevTrim(ti);
  const int ti_next = brep_.NextTrim(ti);
  if (ti_prev < 0 || ti_next < 0 || ti_prev == ti || ti_next == ti || ti_prev == ti_next) return Result::Failed;
  const ON_BrepTrim& trim_prev = brep_.m_T[ti_prev];
  const ON_BrepTrim& trim_next = brep_.m_T[ti_next];
  const int ei_prev = trim_prev.m_ei;
  const int ei_next = trim_next.m_ei;
  if (ei_prev < 0 || ei_next < 0 || ei_prev == edge_index || ei_next == edge_index) return Result::Failed;
  const ON_BrepEdge& edge_prev = brep_.m_E[ei_prev];
  const ON_BrepEdge& edge_next = brep_.m_E[ei_next];
  // Scoped to naked neighbors only (see this method's own doc comment):
  // a neighbor shared with a second face, or itself non-manifold, is left
  // for a caller to handle by hand rather than guessed at here.
  if (edge_prev.TrimCount() != 1 || edge_next.TrimCount() != 1) return Result::Failed;

  const int v_start = trim.m_vi[0];
  const int v_end = trim.m_vi[1];
  if (v_start < 0 || v_end < 0 || v_start == v_end) return Result::Failed;
  if (v_start >= brep_.m_V.Count() || v_end >= brep_.m_V.Count()) return Result::Failed;

  // Isolated-sliver check: each endpoint may touch nothing in this WHOLE
  // Brep besides the micro edge itself and its own one loop-neighbor - a
  // vertex a third edge (another face, a non-manifold junction, ...) also
  // depends on is left alone rather than risked.
  auto only_touches = [&](int vi, int allowed_other_edge) {
    const ON_BrepVertex& v = brep_.m_V[vi];
    for (int k = 0; k < v.m_ei.Count(); ++k) {
      const int e = v.m_ei[k];
      if (e != edge_index && e != allowed_other_edge) return false;
    }
    return true;
  };
  if (!only_touches(v_start, ei_prev) || !only_touches(v_end, ei_next)) return Result::Failed;

  const ON_3dPoint p_start = brep_.m_V[v_start].point;
  const ON_3dPoint p_end = brep_.m_V[v_end].point;
  const ON_3dPoint merged((p_start.x + p_end.x) / 2.0, (p_start.y + p_end.y) / 2.0, (p_start.z + p_end.z) / 2.0);

  // Phase 1 (no mutation yet): for each neighbor, duplicate its own curve
  // and nudge the ONE end that touches the micro edge over to `merged`
  // via ON_Curve::SetStartPoint()/SetEndPoint() - the standard OpenNURBS
  // "close a small gap without reshaping the rest of the curve" primitive.
  // If either can't be moved this way, bail before touching this Brep.
  auto nudge = [&](const ON_BrepEdge& e, int vi) -> std::optional<NurbsCurve> {
    ON_Curve* dup = e.DuplicateCurve();
    if (!dup) return std::nullopt;
    const bool at_end = (e.m_vi[1] == vi);
    const bool moved = at_end ? dup->SetEndPoint(merged) : dup->SetStartPoint(merged);
    if (!moved) { delete dup; return std::nullopt; }
    ON_NurbsCurve nc;
    const bool has_nurbs_form = dup->GetNurbForm(nc) > 0;
    delete dup;
    if (!has_nurbs_form) return std::nullopt;
    NurbsCurve out;
    out.raw() = nc;
    return out;
  };
  std::optional<NurbsCurve> new_prev = nudge(edge_prev, v_start);
  std::optional<NurbsCurve> new_next = nudge(edge_next, v_end);
  if (!new_prev || !new_next) return Result::Failed;

  // Phase 2: commit. Move the two vertices to their shared merged point
  // FIRST, so ReplaceEdgeCurve()'s own "new curve endpoints must land
  // near the edge's EXISTING vertices" check (see its doc comment) passes
  // with near-zero residual rather than needing a loosened tolerance.
  brep_.m_V[v_start].point = merged;
  brep_.m_V[v_end].point = merged;
  const double retrim_tolerance = std::max(tolerance, p_start.DistanceTo(p_end));
  try {
    ReplaceEdgeCurve(ei_prev, *new_prev, retrim_tolerance);
    ReplaceEdgeCurve(ei_next, *new_next, retrim_tolerance);
  } catch (const std::exception&) {
    // Leaves this Brep with, at most, one neighbor's curve nudged by the
    // same micro-scale amount this whole operation is trying to close (a
    // no-op-sized change, never a structural one) and the micro edge
    // itself untouched - a safe, honest "couldn't", not a corrupted Brep.
    return Result::Failed;
  }

  // Both neighbors now reach the shared `merged` point; weld the micro
  // edge's own two vertices into one (ON_Brep's own "expert user"
  // primitive for exactly this - re-points every trim referencing the
  // second vertex onto the first) and delete the now fully degenerate
  // micro edge and its lone trim, then physically cull them.
  brep_.CombineCoincidentVertices(brep_.m_V[v_start], brep_.m_V[v_end]);
  brep_.m_E[edge_index].m_edge_index = -1;
  brep_.m_T[ti].m_trim_index = -1;
  brep_.Compact();

  brep_.SetTolerancesBoxesAndFlags();
  FixUnsetEdgeTolerances(brep_);
  // Same reasoning as ReplaceEdgeCurve()/MergeCoplanarFaces() above: the
  // affected face's own trim loop just changed shape (one fewer edge),
  // so this class's own per-face side tables would otherwise silently
  // keep describing the pre-edit boundary.
  face_trim_loops_.clear();
  face_exact_clip_.clear();
  face_hole_loops_.clear();
  face_arc_runs_.clear();
  face_notch_rows_.clear();
  face_records_.clear();
  return Result::Ok;
}

}  // namespace dino8::kernel
