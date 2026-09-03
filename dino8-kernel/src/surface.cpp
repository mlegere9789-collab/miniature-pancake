#include "dino8/kernel/surface.h"

#include "dino8/kernel/mesh.h"

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

Mesh NurbsSurface::TessellateGrid(int u_divisions, int v_divisions) const {
  Mesh mesh;
  ON_Mesh& raw = mesh.mesh_;

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);

  const int u_points = u_divisions + 1;
  const int v_points = v_divisions + 1;

  raw.m_V.Reserve(u_points * v_points);
  for (int i = 0; i < u_points; ++i) {
    const double u = u_domain.ParameterAt(static_cast<double>(i) / u_divisions);
    for (int j = 0; j < v_points; ++j) {
      const double v = v_domain.ParameterAt(static_cast<double>(j) / v_divisions);
      const Point3d pt = PointAt(u, v);
      raw.m_V.Append(ON_3fPoint(pt));
    }
  }

  auto vertex_index = [v_points](int i, int j) { return i * v_points + j; };

  raw.m_F.Reserve(u_divisions * v_divisions * 2);
  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      const int v00 = vertex_index(i, j);
      const int v10 = vertex_index(i + 1, j);
      const int v11 = vertex_index(i + 1, j + 1);
      const int v01 = vertex_index(i, j + 1);

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

}  // namespace dino8::kernel
