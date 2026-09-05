#include "dino8/kernel/brep.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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

  brep.SetTrimIsoFlags();
  return result;
}

int Brep::FaceCount() const { return brep_.m_F.Count(); }

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

}  // namespace dino8::kernel
