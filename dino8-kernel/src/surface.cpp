#include "dino8/kernel/surface.h"

#include <cmath>
#include <stdexcept>

#include "dino8/kernel/mesh.h"

namespace dino8::kernel {

namespace {

double Cross2d(const Point2d& a, const Point2d& b) { return a.x * b.y - a.y * b.x; }

double SignedArea(const std::vector<Point2d>& polygon) {
  double area = 0.0;
  const size_t n = polygon.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = polygon[i];
    const Point2d& b = polygon[(i + 1) % n];
    area += Cross2d(a, b);
  }
  return 0.5 * area;
}

bool IsConvexPolygon(const std::vector<Point2d>& polygon) {
  const size_t n = polygon.size();
  if (n < 3) {
    return false;
  }
  double sign = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = polygon[i];
    const Point2d& b = polygon[(i + 1) % n];
    const Point2d& c = polygon[(i + 2) % n];
    const Point2d edge1(b.x - a.x, b.y - a.y);
    const Point2d edge2(c.x - b.x, c.y - b.y);
    const double turn = Cross2d(edge1, edge2);
    if (std::abs(turn) < 1e-12) {
      continue;  // collinear vertex, doesn't affect convexity either way
    }
    const double turn_sign = turn > 0 ? 1.0 : -1.0;
    if (sign == 0.0) {
      sign = turn_sign;
    } else if (turn_sign != sign) {
      return false;
    }
  }
  return true;
}

Point2d LineIntersection(const Point2d& p1, const Point2d& p2, const Point2d& a,
                          const Point2d& b) {
  const Point2d ab(b.x - a.x, b.y - a.y);
  const double d1 = Cross2d(ab, Point2d(p1.x - a.x, p1.y - a.y));
  const double d2 = Cross2d(ab, Point2d(p2.x - a.x, p2.y - a.y));
  const double t = d1 / (d1 - d2);
  return Point2d(p1.x + t * (p2.x - p1.x), p1.y + t * (p2.y - p1.y));
}

// Sutherland-Hodgman: clips `subject` against the convex polygon `clip`.
// `orientation_sign` is +1 if `clip`'s vertices are CCW (positive signed
// area), -1 if CW - lets the "inside" test work regardless of `clip`'s
// winding direction.
std::vector<Point2d> ClipConvex(std::vector<Point2d> subject,
                                 const std::vector<Point2d>& clip,
                                 double orientation_sign) {
  const size_t clip_n = clip.size();
  for (size_t i = 0; i < clip_n && !subject.empty(); ++i) {
    const Point2d& a = clip[i];
    const Point2d& b = clip[(i + 1) % clip_n];
    const std::vector<Point2d> input = std::move(subject);
    subject.clear();
    const size_t n = input.size();
    for (size_t j = 0; j < n; ++j) {
      const Point2d& curr = input[j];
      const Point2d& prev = input[(j + n - 1) % n];
      const double curr_side = Cross2d(Point2d(b.x - a.x, b.y - a.y),
                                        Point2d(curr.x - a.x, curr.y - a.y)) *
                                orientation_sign;
      const double prev_side = Cross2d(Point2d(b.x - a.x, b.y - a.y),
                                        Point2d(prev.x - a.x, prev.y - a.y)) *
                                orientation_sign;
      const bool curr_inside = curr_side >= 0.0;
      const bool prev_inside = prev_side >= 0.0;
      if (curr_inside) {
        if (!prev_inside) {
          subject.push_back(LineIntersection(prev, curr, a, b));
        }
        subject.push_back(curr);
      } else if (prev_inside) {
        subject.push_back(LineIntersection(prev, curr, a, b));
      }
    }
  }
  return subject;
}

// Standard even-odd ray-casting point-in-polygon test. Boundary behavior
// is not guaranteed either way (ordinary floating-point ray-casting
// caveat) - callers relying on an exact result should keep test points
// off the polygon boundary.
bool PointInPolygon(double u, double v, const std::vector<Point2d>& polygon) {
  bool inside = false;
  const size_t n = polygon.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const Point2d& pi = polygon[i];
    const Point2d& pj = polygon[j];
    const bool crosses = (pi.y > v) != (pj.y > v);
    if (crosses) {
      const double u_at_crossing = (pj.x - pi.x) * (v - pi.y) / (pj.y - pi.y) + pi.x;
      if (u < u_at_crossing) {
        inside = !inside;
      }
    }
  }
  return inside;
}

}  // namespace

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

Mesh NurbsSurface::TessellateGrid(int u_divisions, int v_divisions,
                                   const std::vector<Point2d>* trim_polygon,
                                   const std::vector<std::vector<Point2d>>* hole_polygons) const {
  Mesh mesh;
  ON_Mesh& raw = mesh.mesh_;

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);

  const int u_points = u_divisions + 1;
  const int v_points = v_divisions + 1;

  std::vector<double> u_values(static_cast<size_t>(u_points));
  std::vector<double> v_values(static_cast<size_t>(v_points));
  for (int i = 0; i < u_points; ++i) {
    u_values[static_cast<size_t>(i)] =
        u_domain.ParameterAt(static_cast<double>(i) / u_divisions);
  }
  for (int j = 0; j < v_points; ++j) {
    v_values[static_cast<size_t>(j)] =
        v_domain.ParameterAt(static_cast<double>(j) / v_divisions);
  }

  auto grid_index = [v_points](int i, int j) { return i * v_points + j; };

  if (trim_polygon == nullptr) {
    raw.m_V.Reserve(u_points * v_points);
    for (int i = 0; i < u_points; ++i) {
      for (int j = 0; j < v_points; ++j) {
        raw.m_V.Append(ON_3fPoint(PointAt(u_values[static_cast<size_t>(i)],
                                           v_values[static_cast<size_t>(j)])));
      }
    }

    raw.m_F.Reserve(u_divisions * v_divisions * 2);
    for (int i = 0; i < u_divisions; ++i) {
      for (int j = 0; j < v_divisions; ++j) {
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

  // Trimmed path: only emit cells whose four corners are all inside the
  // trim polygon, and only the vertices those emitted cells actually use
  // (lazily assigned, so unused grid points aren't left dangling in the
  // output mesh).
  std::vector<bool> inside(static_cast<size_t>(u_points) * static_cast<size_t>(v_points));
  for (int i = 0; i < u_points; ++i) {
    for (int j = 0; j < v_points; ++j) {
      const double u = u_values[static_cast<size_t>(i)];
      const double v = v_values[static_cast<size_t>(j)];
      bool point_inside = PointInPolygon(u, v, *trim_polygon);
      if (point_inside && hole_polygons != nullptr) {
        for (const auto& hole : *hole_polygons) {
          if (PointInPolygon(u, v, hole)) {
            point_inside = false;
            break;
          }
        }
      }
      inside[static_cast<size_t>(grid_index(i, j))] = point_inside;
    }
  }

  std::vector<int> compacted_index(static_cast<size_t>(u_points) * static_cast<size_t>(v_points),
                                    -1);
  auto emit_vertex = [&](int i, int j) {
    const int raw_index = grid_index(i, j);
    if (compacted_index[static_cast<size_t>(raw_index)] == -1) {
      compacted_index[static_cast<size_t>(raw_index)] = raw.m_V.Count();
      raw.m_V.Append(ON_3fPoint(
          PointAt(u_values[static_cast<size_t>(i)], v_values[static_cast<size_t>(j)])));
    }
    return compacted_index[static_cast<size_t>(raw_index)];
  };

  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      const bool cell_inside = inside[static_cast<size_t>(grid_index(i, j))] &&
                                inside[static_cast<size_t>(grid_index(i + 1, j))] &&
                                inside[static_cast<size_t>(grid_index(i + 1, j + 1))] &&
                                inside[static_cast<size_t>(grid_index(i, j + 1))];
      if (!cell_inside) {
        continue;
      }

      const int v00 = emit_vertex(i, j);
      const int v10 = emit_vertex(i + 1, j);
      const int v11 = emit_vertex(i + 1, j + 1);
      const int v01 = emit_vertex(i, j + 1);

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

Mesh NurbsSurface::TessellateGridClippedConvex(int u_divisions, int v_divisions,
                                                const std::vector<Point2d>& trim_polygon) const {
  if (!IsConvexPolygon(trim_polygon)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGridClippedConvex: trim_polygon "
        "must be convex - use TessellateGrid()'s whole-cell trim_polygon for "
        "non-convex boundaries");
  }
  const double orientation_sign = SignedArea(trim_polygon) >= 0.0 ? 1.0 : -1.0;

  Mesh mesh;
  ON_Mesh& raw = mesh.mesh_;

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);
  auto u_at = [&](int i) { return u_domain.ParameterAt(static_cast<double>(i) / u_divisions); };
  auto v_at = [&](int j) { return v_domain.ParameterAt(static_cast<double>(j) / v_divisions); };

  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      const double u0 = u_at(i);
      const double u1 = u_at(i + 1);
      const double v0 = v_at(j);
      const double v1 = v_at(j + 1);
      // Same corner order as TessellateGrid's cell winding (v00,v10,v11,v01).
      const std::vector<Point2d> cell = {Point2d(u0, v0), Point2d(u1, v0), Point2d(u1, v1),
                                          Point2d(u0, v1)};

      auto clipped = ClipConvex(cell, trim_polygon, orientation_sign);
      if (clipped.size() < 3) {
        continue;  // cell entirely outside the trim
      }

      // Sutherland-Hodgman can emit a vertex right at (or numerically
      // indistinguishable from) the polygon's start/end when a clip edge
      // passes through/near a cell corner. Left in, this produces
      // zero-area "sliver" triangles below whose two non-shared vertices
      // are the same point - not just visually negligible, but a real
      // topological defect: that degenerate edge doesn't have a proper
      // partner, corrupting ExtrudeCappedSolid()'s boundary-edge
      // extraction (verified: without this, a real cylinder cap measured
      // a mesh with an impossible edge count for its own vertex/face
      // count, and Manifold correctly rejected the resulting solid as
      // non-manifold rather than silently accepting it).
      constexpr double kDuplicatePointEpsilon = 1e-9;
      std::vector<Point2d> deduped;
      deduped.reserve(clipped.size());
      for (const Point2d& p : clipped) {
        if (deduped.empty() ||
            std::abs(p.x - deduped.back().x) > kDuplicatePointEpsilon ||
            std::abs(p.y - deduped.back().y) > kDuplicatePointEpsilon) {
          deduped.push_back(p);
        }
      }
      if (deduped.size() > 1 &&
          std::abs(deduped.front().x - deduped.back().x) <= kDuplicatePointEpsilon &&
          std::abs(deduped.front().y - deduped.back().y) <= kDuplicatePointEpsilon) {
        deduped.pop_back();
      }
      if (deduped.size() < 3) {
        continue;
      }

      std::vector<int> indices;
      indices.reserve(deduped.size());
      for (const Point2d& p : deduped) {
        indices.push_back(raw.m_V.Count());
        raw.m_V.Append(ON_3fPoint(PointAt(p.x, p.y)));
      }
      // Fan triangulation from indices[0]: valid because clipping a convex
      // cell against a convex trim polygon always yields a convex result.
      // Also skip any triangle that's still degenerate (near-zero area in
      // parameter space) even after deduping - e.g. three
      // near-collinear points - for the same reason as above.
      for (size_t k = 1; k + 1 < indices.size(); ++k) {
        const Point2d& p0 = deduped[0];
        const Point2d& p1 = deduped[k];
        const Point2d& p2 = deduped[k + 1];
        const double area2 = Cross2d(Point2d(p1.x - p0.x, p1.y - p0.y),
                                      Point2d(p2.x - p0.x, p2.y - p0.y));
        if (std::abs(area2) <= 1e-15) {
          continue;
        }
        ON_MeshFace face;
        face.vi[0] = indices[0];
        face.vi[1] = indices[k];
        face.vi[2] = indices[k + 1];
        face.vi[3] = face.vi[2];
        raw.m_F.Append(face);
      }
    }
  }

  // Adjacent cells independently compute the same boundary-intersection
  // points as separate vertices; weld them into one consistent mesh.
  return Mesh::MergeAndWeld({mesh});
}

}  // namespace dino8::kernel
