#include "dino8/kernel/brep.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dino8/kernel/detail/arc_schedule3d.h"
#include "dino8/kernel/detail/polygon2d.h"
#include "dino8/kernel/mesh.h"

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
  double u_min = uv[0].x, u_max = uv[0].x, v_min = uv[0].y, v_max = uv[0].y;
  for (const Point2d& p : uv) {
    u_min = std::min(u_min, p.x);
    u_max = std::max(u_max, p.x);
    v_min = std::min(v_min, p.y);
    v_max = std::max(v_max, p.y);
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
// wherever two faces' own loops meet at "the same" 3D point. tol = 1e-6
// matches Mesh::MergeAndWeld's own proven default exactly, not a newly
// invented tolerance; see brep.h's FromMixedFaces doc comment for the
// real, disclosed limit this implies (features smaller than that mis-weld).
constexpr double kBrepWeldTolerance = 1e-6;

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
};

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
void BuildFaceLoop(ON_Brep& brep, ON_BrepFace& face, const FaceTopology& topo,
                    std::unordered_map<uint64_t, int>& edge_of_vertex_pair) {
  ON_BrepLoop& loop = brep.NewLoop(ON_BrepLoop::outer, face);
  const size_t n = topo.vids.size();
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    const int vid_from = topo.vids[k];
    const int vid_to = topo.vids[k1];

    const bool is_cap = topo.curved_surface != nullptr && (k == 0 || k == 2);
    const bool has_notch_interior = k < topo.notch_interior_uv.size() && !topo.notch_interior_uv[k].empty();
    const bool iso_reversed = is_cap && !has_notch_interior && k == 2;

    const uint32_t lo = static_cast<uint32_t>(std::min(vid_from, vid_to));
    const uint32_t hi = static_cast<uint32_t>(std::max(vid_from, vid_to));
    const uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;

    int edge_index;
    const auto it = edge_of_vertex_pair.find(key);
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
    const bool bRev3d = (edge.m_vi[0] != vid_from);

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
    const ON_Cylinder cyl(circle, cf.length);
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
    const std::vector<Point2d> trim = {Point2d(0.0, 0.0), Point2d(u_max, 0.0),
                                        Point2d(u_max, cf.length), Point2d(0.0, cf.length)};
    result.face_trim_loops_.push_back(trim);
    // exact_clip=true for the same reason FromPlanarFaces()'s own faces
    // use it above: this trim rectangle IS the patch's exact boundary
    // (the two straight rails at u=0/u=u_max and the two circular arcs at
    // v=0/v=length), not an approximation of one.
    result.face_exact_clip_.push_back(true);
    result.face_hole_loops_.emplace_back();
    result.face_arc_runs_.emplace_back();  // meaningless for a non-planar face

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
    t.vids = {welder.Weld(surface->PointAt(0.0, 0.0)), welder.Weld(surface->PointAt(u_max, 0.0)),
              welder.Weld(surface->PointAt(u_max, cf.length)), welder.Weld(surface->PointAt(0.0, cf.length))};
    t.curved_surface = surface;
    t.curved_u_max = u_max;
    t.curved_v0 = 0.0;
    t.curved_length = cf.length;
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
    auto notch_uv = [&](const std::vector<Point3d>& pts3d) {
      std::vector<Point2d> uv;
      uv.reserve(pts3d.size());
      for (const Point3d& p : pts3d) {
        const Vector3d d = p - cf.frame.origin;
        const double height = d * cf.frame.zaxis;
        const double x = d * cf.frame.xaxis, y = d * cf.frame.yaxis;
        double phi = std::atan2(y, x);
        if (phi < 0.0) phi += 2.0 * ON_PI;
        double u = 0.0;
        if (!u_ref_circle.GetNurbFormParameterFromRadian(phi, &u)) {
          throw std::runtime_error(
              "dino8::kernel::Brep::FromMixedFaces: a ConicalFace's own cap "
              "notch point's angle is out of ON_Circle's own NURBS-"
              "parameterization domain");
        }
        // Checked invariant, not merely trusted: this (u, height) point,
        // evaluated back through the REAL surface, must reproduce the
        // same 3D point this whole notch is built from.
        const Point3d check = surface->PointAt(u, height);
        const double tol = std::max(1e-6, (cf.radius0 + cf.radius1) * 1e-6);
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
      cap0_full_uv = notch_uv(cf.cap0_notch_points);
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
      const std::vector<Point2d> full_uv = notch_uv(cf.cap1_notch_points);
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
  for (size_t fi = 0; fi < topo.size(); ++fi) {
    if (topo[fi].curved_surface != nullptr) {
      BuildFaceLoop(brep, brep.m_F[static_cast<int>(fi)], topo[fi], edge_of_vertex_pair);
    }
  }
  for (size_t fi = 0; fi < topo.size(); ++fi) {
    if (topo[fi].curved_surface == nullptr) {
      BuildFaceLoop(brep, brep.m_F[static_cast<int>(fi)], topo[fi], edge_of_vertex_pair);
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

BoundingBox Brep::GetTightBoundingBox() const {
  ON_BoundingBox box;
  if (!brep_.GetTightBoundingBox(box)) {
    throw std::runtime_error(
        "dino8::kernel::Brep::GetTightBoundingBox: ON_Brep::"
        "GetTightBoundingBox failed");
  }
  return BoundingBox{box.Min(), box.Max()};
}

std::vector<Mesh> Brep::Tessellate(int u_divisions, int v_divisions) const {
  std::vector<Mesh> result;
  result.reserve(static_cast<size_t>(brep_.m_F.Count()));
  for (int i = 0; i < brep_.m_F.Count(); ++i) {
    FaceGeometry fg;
    if (!ResolveFace(brep_, i, face_trim_loops_, face_exact_clip_, face_hole_loops_, fg)) continue;
    NurbsSurface wrapper;
    wrapper.raw() = fg.surface;
    if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExact(u_divisions, v_divisions, fg.outer));
    } else {
      const std::vector<std::vector<Point2d>>* holes = fg.holes.empty() ? nullptr : &fg.holes;
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions, &fg.outer, holes));
    }
    if (brep_.m_F[i].m_bRev) result.back() = result.back().FlipNormals();
  }
  return result;
}

Mesh Brep::TessellateToClosedMesh(int u_divisions, int v_divisions) const {
  return Mesh::MergeAndWeld(Tessellate(u_divisions, v_divisions));
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
Mesh BuildConformingCylinderMesh(const NurbsSurface& wrapper, const std::vector<Point2d>& trim_uv, int u_divisions,
                                  int v_divisions, const std::vector<ConformingMatch>& matches) {
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

// One shared boundary sample forced onto a plain quad face's own tensor
// grid, at parameter `t` (in [0, 1]) along whichever of the quad's own
// two axes (see BuildConformingPlainQuadMesh's own doc comment for the
// `a`/`b` convention) the corresponding edge runs along.
struct EdgeForce {
  double t = 0.0;
  Point3d point;
};

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

  struct Break {
    double t = 0.0;
    const Point3d* at_lo = nullptr;  // forced point at this axis's own "0" extreme
    const Point3d* at_hi = nullptr;  // forced point at this axis's own "1" extreme
  };
  // Shared "collect forced breakpoints, then fill the remaining gaps at
  // roughly `divisions`-uniform spacing" logic - the same shape
  // BuildConformingCylinderMesh's own u-breakpoint construction already
  // uses (see its own doc comment), applied once per axis here since
  // EITHER axis (not just `u`) may carry forced points for a plain quad
  // face (see this function's own doc comment for why).
  auto build_axis = [](const std::vector<EdgeForce>& forces_lo, const std::vector<EdgeForce>& forces_hi,
                        int divisions) {
    const double tol = 1e-9;
    std::vector<Break> breaks;
    auto add_break = [&](double t, const Point3d* lo, const Point3d* hi) {
      for (Break& b : breaks) {
        if (std::fabs(b.t - t) <= tol) {
          if (lo) b.at_lo = lo;
          if (hi) b.at_hi = hi;
          return;
        }
      }
      Break b;
      b.t = t;
      b.at_lo = lo;
      b.at_hi = hi;
      breaks.push_back(b);
    };
    add_break(0.0, nullptr, nullptr);
    add_break(1.0, nullptr, nullptr);
    for (const EdgeForce& f : forces_lo) add_break(f.t, &f.point, nullptr);
    for (const EdgeForce& f : forces_hi) add_break(f.t, nullptr, &f.point);
    std::sort(breaks.begin(), breaks.end(), [](const Break& x, const Break& y) { return x.t < y.t; });

    const double target_spacing = 1.0 / static_cast<double>(std::max(divisions, 1));
    std::vector<Break> filled;
    filled.reserve(breaks.size() * 2);
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
      filled.push_back(breaks[i]);
      const double gap = breaks[i + 1].t - breaks[i].t;
      if (gap > target_spacing * 1.5) {
        const int extra = static_cast<int>(std::ceil(gap / target_spacing)) - 1;
        for (int e = 1; e <= extra; ++e) {
          Break b;
          b.t = breaks[i].t + gap * static_cast<double>(e) / static_cast<double>(extra + 1);
          filled.push_back(b);
        }
      }
    }
    if (!breaks.empty()) filled.push_back(breaks.back());
    return filled;
  };

  const std::vector<Break> a_breaks = build_axis(a_forces_b0, a_forces_b1, u_divisions);
  const std::vector<Break> b_breaks = build_axis(b_forces_a0, b_forces_a1, v_divisions);

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

}  // namespace

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

  const double tol = 1e-9;

  std::unordered_map<int, std::vector<std::pair<SubRange, std::vector<Point3d>>>> wedge_subs;
  std::unordered_map<int, std::vector<ConformingMatch>> cyl_matches;

  for (int i = 0; i < n; ++i) {
    if (static_cast<size_t>(i) >= face_arc_runs_.size()) continue;
    const std::vector<PlanarFace::ArcRun>& runs = face_arc_runs_[static_cast<size_t>(i)];
    if (runs.empty()) continue;
    for (size_t k = 0; k < runs.size(); ++k) {
      const PlanarFace::ArcRun& run = runs[k];
      Vector3d normal = ON_CrossProduct(run.plane_xaxis, run.plane_yaxis);
      if (!normal.Unitize()) continue;  // degenerate stored basis - skip, falls through to ordinary path

      const CylEntry* matched = nullptr;
      for (const CylEntry& ce : cyls) {
        if (SameCircleAsCylinder(run.center, run.radius, normal, ce.cf, tol)) {
          matched = &ce;
          break;
        }
      }
      if (matched == nullptr) continue;

      const double height = ON_DotProduct(run.center - matched->cf.frame.origin, matched->cf.frame.zaxis);
      const double len_tol = std::max(tol, matched->cf.length * 1e-6);
      bool at_v0 = false;
      if (std::fabs(height) <= len_tol) {
        at_v0 = true;
      } else if (std::fabs(height - matched->cf.length) <= len_tol) {
        at_v0 = false;
      } else {
        continue;  // doesn't land at either end of the matched cylinder - not a case this targets
      }

      ON_Plane wedge_plane;
      wedge_plane.origin = run.center;
      wedge_plane.xaxis = run.plane_xaxis;
      wedge_plane.yaxis = run.plane_yaxis;
      wedge_plane.zaxis = normal;
      wedge_plane.UpdateEquation();

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

    if (cyl_it != cyl_matches.end()) {
      result.push_back(BuildConformingCylinderMesh(wrapper, fg.outer, u_divisions, v_divisions, cyl_it->second));
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
      result.push_back(BuildConformingPlainQuadMesh(corner, u_divisions, v_divisions, plain_it->second[0],
                                                      plain_it->second[2], plain_it->second[3], plain_it->second[1]));
    } else if (fg.outer.empty()) {
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
    } else if (fg.exact_clip) {
      result.push_back(wrapper.TessellateGridClippedExact(u_divisions, v_divisions, fg.outer));
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

}  // namespace dino8::kernel
