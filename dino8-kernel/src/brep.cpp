#include "dino8/kernel/brep.h"

#include <stdexcept>

#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

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
    const ON_Surface* face_surface = brep_.m_F[i].SurfaceOf();
    const auto* nurbs_surface = ON_NurbsSurface::Cast(face_surface);
    if (nurbs_surface == nullptr) {
      // Every face this chunk's Brep::FromSurface constructs stores a
      // genuine ON_NurbsSurface, so this only trips if Brep grows a way
      // to hold other surface types without updating the tessellator.
      continue;
    }

    NurbsSurface wrapper;
    wrapper.raw() = *nurbs_surface;

    const auto& trim_loop = face_trim_loops_[static_cast<size_t>(i)];
    if (trim_loop.empty()) {
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions));
    } else if (face_exact_clip_[static_cast<size_t>(i)]) {
      result.push_back(
          wrapper.TessellateGridClippedExact(u_divisions, v_divisions, trim_loop));
    } else {
      const auto& hole_loops = face_hole_loops_[static_cast<size_t>(i)];
      const std::vector<std::vector<Point2d>>* holes =
          hole_loops.empty() ? nullptr : &hole_loops;
      result.push_back(wrapper.TessellateGrid(u_divisions, v_divisions, &trim_loop, holes));
    }
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
    const ON_Surface* face_surface = brep_.m_F[i].SurfaceOf();
    const auto* nurbs_surface = ON_NurbsSurface::Cast(face_surface);
    if (nurbs_surface == nullptr) {
      continue;
    }

    NurbsSurface wrapper;
    wrapper.raw() = *nurbs_surface;

    const auto& trim_loop = face_trim_loops_[static_cast<size_t>(i)];
    if (trim_loop.empty()) {
      result.push_back(wrapper.TessellateGridAdaptive(chord_tolerance));
    } else if (face_exact_clip_[static_cast<size_t>(i)]) {
      result.push_back(
          wrapper.TessellateGridClippedExactAdaptive(chord_tolerance, trim_loop));
    } else {
      const auto& hole_loops = face_hole_loops_[static_cast<size_t>(i)];
      const std::vector<std::vector<Point2d>>* holes =
          hole_loops.empty() ? nullptr : &hole_loops;
      result.push_back(wrapper.TessellateGridAdaptive(chord_tolerance, &trim_loop, holes));
    }
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
    const ON_Surface* face_surface = brep_.m_F[i].SurfaceOf();
    const auto* nurbs_surface = ON_NurbsSurface::Cast(face_surface);
    if (nurbs_surface == nullptr) {
      continue;
    }

    NurbsSurface wrapper;
    wrapper.raw() = *nurbs_surface;

    const auto& trim_loop = face_trim_loops_[static_cast<size_t>(i)];
    if (trim_loop.empty()) {
      result.push_back(wrapper.TessellateGridNonUniformAdaptive(chord_tolerance));
    } else if (face_exact_clip_[static_cast<size_t>(i)]) {
      // No non-uniform exact-clip tessellator exists yet - fall back to
      // the uniform adaptive one for this face, a real narrower scope
      // documented on this method's own declaration.
      result.push_back(
          wrapper.TessellateGridClippedExactAdaptive(chord_tolerance, trim_loop));
    } else {
      const auto& hole_loops = face_hole_loops_[static_cast<size_t>(i)];
      const std::vector<std::vector<Point2d>>* holes =
          hole_loops.empty() ? nullptr : &hole_loops;
      result.push_back(
          wrapper.TessellateGridNonUniformAdaptive(chord_tolerance, &trim_loop, holes));
    }
  }

  return result;
}

Mesh Brep::TessellateToClosedMeshNonUniformAdaptive(double chord_tolerance) const {
  return Mesh::MergeAndWeld(TessellateNonUniformAdaptive(chord_tolerance));
}

}  // namespace dino8::kernel
