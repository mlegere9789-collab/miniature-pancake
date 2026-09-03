#include "dino8/kernel/surface.h"

namespace dino8::kernel {

NurbsSurface NurbsSurface::FromControlGrid(const std::vector<Point3d>& control_grid,
                                            int u_count, int v_count, int u_degree,
                                            int v_degree) {
  NurbsSurface result;
  const int u_order = u_degree + 1;
  const int v_order = v_degree + 1;
  result.surface_.Create(/*dimension=*/3, /*is_rational=*/false, u_order, v_order,
                          u_count, v_count);

  for (int u = 0; u < u_count; ++u) {
    for (int v = 0; v < v_count; ++v) {
      const size_t idx = static_cast<size_t>(u) * static_cast<size_t>(v_count) +
                          static_cast<size_t>(v);
      result.surface_.SetCV(u, v, control_grid[idx]);
    }
  }

  result.surface_.MakeClampedUniformKnotVector(0);
  result.surface_.MakeClampedUniformKnotVector(1);

  return result;
}

int NurbsSurface::DegreeU() const { return surface_.Degree(0); }
int NurbsSurface::DegreeV() const { return surface_.Degree(1); }

Result NurbsSurface::ElevateDegree(int direction, int new_degree) {
  if (new_degree <= surface_.Degree(direction)) {
    return Result::NoOpAlreadySatisfied;
  }
  const bool ok = surface_.IncreaseDegree(direction, new_degree);
  return ok ? Result::Ok : Result::Failed;
}

Point3d NurbsSurface::PointAt(double u, double v) const {
  ON_3dPoint pt;
  surface_.EvPoint(u, v, pt);
  return pt;
}

}  // namespace dino8::kernel
