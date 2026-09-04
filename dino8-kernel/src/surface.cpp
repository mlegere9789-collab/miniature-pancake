#include "dino8/kernel/surface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "dino8/kernel/curve.h"
#include "dino8/kernel/detail/polygon2d.h"
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
// Used only for a convex trim_polygon (TessellateGridClippedExact's fast,
// long-proven path) - ClipPolygon below is the general fallback for a
// concave trim. `orientation_sign` is +1 if `clip`'s vertices are CCW
// (positive signed area), -1 if CW - lets the "inside" test work
// regardless of `clip`'s winding direction.
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

// Greiner-Hormann polygon intersection: clips `subject` against `clip`,
// where both are simple (non-self-intersecting) polygons and either may
// be concave. This is TessellateGridClippedExact's fallback for a concave
// trim_polygon - ClipConvex above stays the path for a convex one, both
// because it's simpler and because it's the long-proven implementation
// Mesh::Cylinder()'s circular trim (and everything else that exercises
// exact clipping so far) already depends on; concave clipping is a newer,
// narrower-tested addition and doesn't need to displace it. Unlike
// Sutherland-Hodgman, this doesn't require the clip region to be convex -
// it works by inserting every boundary-crossing point into both polygons'
// vertex lists, tagging each subject-side crossing "entry" (the subject
// path moves from outside `clip` to inside there) or "exit", then tracing
// the shared boundary starting from each unvisited *entry*, always moving
// forward (never backward - both polygons are normalized to the same CCW
// winding below, which is what makes "always forward" valid for plain
// intersection, unlike the fuller Greiner-Hormann/Foster algorithm's
// forward/backward rule needed for union and difference) and switching
// polygon at every crossing. Starting only from entries matters: starting
// a forward-only trace from an exit instead traces the wrong (much
// larger, effectively union-shaped) loop - a real bug an earlier version
// of this function had, caught by a wildly-too-large measured area, not a
// subtle one. Returns zero or more closed loops - zero if disjoint, one
// for the ordinary case, more than one if `clip` carves `subject` (here,
// always a single grid cell) into disjoint pieces.
std::vector<std::vector<Point2d>> ClipPolygon(const std::vector<Point2d>& subject_in,
                                               const std::vector<Point2d>& clip_in) {
  struct Vertex {
    Point2d p;
    bool intersect = false;
    bool entry = false;
    bool visited = false;
    int neighbor = -1;  // index into the *other* list (or, before fix-up, a shared id)
  };
  struct PendingHit {
    double alpha;
    Point2d p;
    int id;
  };

  std::vector<Point2d> subject = subject_in;
  if (SignedArea(subject) < 0.0) {
    std::reverse(subject.begin(), subject.end());
  }
  std::vector<Point2d> clip = clip_in;
  if (SignedArea(clip) < 0.0) {
    std::reverse(clip.begin(), clip.end());
  }

  const size_t sn = subject.size();
  const size_t cn = clip.size();
  std::vector<std::vector<PendingHit>> subject_hits(sn);
  std::vector<std::vector<PendingHit>> clip_hits(cn);
  int next_id = 0;

  constexpr double kEps = 1e-9;
  for (size_t i = 0; i < sn; ++i) {
    const Point2d& p1 = subject[i];
    const Point2d& p2 = subject[(i + 1) % sn];
    const double d1x = p2.x - p1.x;
    const double d1y = p2.y - p1.y;
    for (size_t j = 0; j < cn; ++j) {
      const Point2d& p3 = clip[j];
      const Point2d& p4 = clip[(j + 1) % cn];
      const double d2x = p4.x - p3.x;
      const double d2y = p4.y - p3.y;
      const double denom = d1x * d2y - d1y * d2x;
      if (std::abs(denom) < 1e-15) {
        continue;  // parallel (or one segment is degenerate)
      }
      const double dx = p3.x - p1.x;
      const double dy = p3.y - p1.y;
      const double t = (dx * d2y - dy * d2x) / denom;
      const double u = (dx * d1y - dy * d1x) / denom;
      if (t < kEps || t > 1.0 - kEps || u < kEps || u > 1.0 - kEps) {
        continue;
      }
      const Point2d hit(p1.x + t * d1x, p1.y + t * d1y);
      const int id = next_id++;
      subject_hits[i].push_back({t, hit, id});
      clip_hits[j].push_back({u, hit, id});
    }
  }

  auto build_list = [](const std::vector<Point2d>& poly,
                        std::vector<std::vector<PendingHit>>& hits) {
    std::vector<Vertex> list;
    for (size_t i = 0; i < poly.size(); ++i) {
      Vertex original;
      original.p = poly[i];
      list.push_back(original);
      std::sort(hits[i].begin(), hits[i].end(),
                [](const PendingHit& a, const PendingHit& b) { return a.alpha < b.alpha; });
      for (const PendingHit& hit : hits[i]) {
        Vertex crossing;
        crossing.p = hit.p;
        crossing.intersect = true;
        crossing.neighbor = hit.id;  // temporary: fixed up to a real index below
        list.push_back(crossing);
      }
    }
    return list;
  };

  std::vector<Vertex> subject_list = build_list(subject, subject_hits);
  std::vector<Vertex> clip_list = build_list(clip, clip_hits);

  if (next_id == 0) {
    // No boundary crossings at all: one polygon is entirely inside the
    // other, or they're disjoint.
    if (PointInPolygon(subject[0].x, subject[0].y, clip)) {
      return {subject};
    }
    if (PointInPolygon(clip[0].x, clip[0].y, subject)) {
      return {clip};
    }
    return {};
  }

  std::vector<int> id_to_subject_index(static_cast<size_t>(next_id), -1);
  std::vector<int> id_to_clip_index(static_cast<size_t>(next_id), -1);
  for (size_t i = 0; i < subject_list.size(); ++i) {
    if (subject_list[i].intersect) {
      id_to_subject_index[static_cast<size_t>(subject_list[i].neighbor)] = static_cast<int>(i);
    }
  }
  for (size_t i = 0; i < clip_list.size(); ++i) {
    if (clip_list[i].intersect) {
      id_to_clip_index[static_cast<size_t>(clip_list[i].neighbor)] = static_cast<int>(i);
    }
  }
  for (Vertex& v : subject_list) {
    if (v.intersect) {
      v.neighbor = id_to_clip_index[static_cast<size_t>(v.neighbor)];
    }
  }
  for (Vertex& v : clip_list) {
    if (v.intersect) {
      v.neighbor = id_to_subject_index[static_cast<size_t>(v.neighbor)];
    }
  }

  // Only subject_list needs entry/exit: a trace always starts at a
  // subject-side "entry" (a crossing where the subject path moves from
  // outside `clip` to inside it) - starting at an "exit" instead and
  // still always moving forward traces the wrong (much larger,
  // effectively union-shaped) loop, not the intersection. Each list's
  // first vertex is always an original (non-crossing) polygon vertex
  // (build_list appends it before that edge's crossings), so it's safe to
  // seed the toggle with a direct point-in-polygon test.
  {
    bool inside = PointInPolygon(subject_list[0].p.x, subject_list[0].p.y, clip);
    for (Vertex& v : subject_list) {
      if (v.intersect) {
        inside = !inside;
        v.entry = inside;
      }
    }
  }

  std::vector<std::vector<Point2d>> result;
  for (size_t s = 0; s < subject_list.size(); ++s) {
    if (!subject_list[s].intersect || !subject_list[s].entry || subject_list[s].visited) {
      continue;
    }
    std::vector<Point2d> polygon;
    std::vector<Vertex>* list = &subject_list;
    std::vector<Vertex>* other = &clip_list;
    size_t idx = s;
    while (true) {
      Vertex& v = (*list)[idx];
      v.visited = true;
      if (v.intersect && v.neighbor >= 0) {
        (*other)[static_cast<size_t>(v.neighbor)].visited = true;
      }
      polygon.push_back(v.p);
      size_t next_idx = (idx + 1) % list->size();
      while (!(*list)[next_idx].intersect) {
        polygon.push_back((*list)[next_idx].p);
        next_idx = (next_idx + 1) % list->size();
      }
      idx = static_cast<size_t>((*list)[next_idx].neighbor);
      std::swap(list, other);
      if (list == &subject_list && idx == s) {
        break;
      }
    }
    if (polygon.size() >= 3) {
      result.push_back(std::move(polygon));
    }
  }
  return result;
}

// EarClipTriangulate itself now lives in detail/polygon2d.h, shared with
// mesh.cpp's loft end-cap triangulation - both need "triangulate a simple,
// possibly-concave 2D polygon" and there's no reason to maintain two
// copies of that logic.
using dino8::kernel::detail::EarClipTriangulate;

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

bool NurbsSurface::IsClosed(int direction) const { return surface_.IsClosed(direction); }

bool NurbsSurface::IsPeriodic(int direction) const { return surface_.IsPeriodic(direction); }

Result NurbsSurface::Reverse(int direction) {
  return surface_.Reverse(direction) ? Result::Ok : Result::Failed;
}

void NurbsSurface::Transpose() { surface_.Transpose(); }

Result NurbsSurface::Trim(int direction, double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  return surface_.Trim(direction, ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::Extend(int direction, double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  if (surface_.IsClosed(direction)) {
    return Result::Failed;
  }
  const ON_Interval current = surface_.Domain(direction);
  if (t0 >= current.Min() && t1 <= current.Max()) {
    return Result::NoOpAlreadySatisfied;
  }
  return surface_.Extend(direction, ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsSurface::Split(int direction, double t, NurbsSurface& out_west_or_south,
                            NurbsSurface& out_east_or_north) const {
  ON_Surface* west_or_south = nullptr;
  ON_Surface* east_or_north = nullptr;
  const bool ok = surface_.Split(direction, t, west_or_south, east_or_north);
  if (!ok) {
    delete west_or_south;
    delete east_or_north;
    return Result::Failed;
  }

  ON_NurbsSurface* west_or_south_nurbs = ON_NurbsSurface::Cast(west_or_south);
  ON_NurbsSurface* east_or_north_nurbs = ON_NurbsSurface::Cast(east_or_north);
  if (west_or_south_nurbs == nullptr || east_or_north_nurbs == nullptr) {
    delete west_or_south;
    delete east_or_north;
    return Result::Failed;
  }

  out_west_or_south.surface_ = *west_or_south_nurbs;
  out_east_or_north.surface_ = *east_or_north_nurbs;
  delete west_or_south;
  delete east_or_north;
  return Result::Ok;
}

Point3d NurbsSurface::PointAt(double u, double v) const {
  ON_3dPoint pt;
  surface_.EvPoint(u, v, pt);
  return pt;
}

Point2d NurbsSurface::ClosestPointParameter(Point3d point, int u_divisions, int v_divisions) const {
  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);
  auto distance_squared = [&](double u, double v) { return (PointAt(u, v) - point).LengthSquared(); };

  double u_lo = u_domain.Min();
  double u_hi = u_domain.Max();
  double v_lo = v_domain.Min();
  double v_hi = v_domain.Max();
  double best_u = u_lo;
  double best_v = v_lo;

  constexpr int kRefinementLevels = 8;
  for (int level = 0; level < kRefinementLevels; ++level) {
    double best_d2 = std::numeric_limits<double>::max();
    for (int i = 0; i <= u_divisions; ++i) {
      const double u = u_lo + (u_hi - u_lo) * static_cast<double>(i) / u_divisions;
      for (int j = 0; j <= v_divisions; ++j) {
        const double v = v_lo + (v_hi - v_lo) * static_cast<double>(j) / v_divisions;
        const double d2 = distance_squared(u, v);
        if (d2 < best_d2) {
          best_d2 = d2;
          best_u = u;
          best_v = v;
        }
      }
    }
    const double u_step = (u_hi - u_lo) / u_divisions;
    const double v_step = (v_hi - v_lo) / v_divisions;
    u_lo = std::max(u_domain.Min(), best_u - u_step);
    u_hi = std::min(u_domain.Max(), best_u + u_step);
    v_lo = std::max(v_domain.Min(), best_v - v_step);
    v_hi = std::min(v_domain.Max(), best_v + v_step);
  }
  return Point2d(best_u, best_v);
}

Point3d NurbsSurface::ClosestPoint(Point3d point, int u_divisions, int v_divisions) const {
  const Point2d uv = ClosestPointParameter(point, u_divisions, v_divisions);
  return PointAt(uv.x, uv.y);
}

Vector3d NurbsSurface::NormalAt(double u, double v) const {
  ON_3dPoint point;
  ON_3dVector normal;
  if (!surface_.EvNormal(u, v, point, normal)) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::NormalAt: OpenNURBS couldn't evaluate "
        "a normal at this (u, v) - likely a singular point where the "
        "surface's two partial derivatives are parallel or zero");
  }
  return normal;
}

SurfaceCurvature NurbsSurface::CurvatureAt(double u, double v) const {
  ON_3dPoint point;
  ON_3dVector du, dv, duu, duv, dvv;
  if (!surface_.Ev2Der(u, v, point, du, dv, duu, duv, dvv)) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::CurvatureAt: OpenNURBS couldn't "
        "evaluate second derivatives at this (u, v)");
  }
  ON_3dVector normal = ON_CrossProduct(du, dv);
  if (!normal.Unitize()) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::CurvatureAt: singular point - the "
        "surface's two partial derivatives are parallel or zero");
  }

  const double e_coeff = du * du;
  const double f_coeff = du * dv;
  const double g_coeff = dv * dv;
  const double l_coeff = duu * normal;
  const double m_coeff = duv * normal;
  const double n_coeff = dvv * normal;

  const double denom = e_coeff * g_coeff - f_coeff * f_coeff;
  if (std::abs(denom) < 1e-15) {
    throw std::runtime_error(
        "dino8::kernel::NurbsSurface::CurvatureAt: degenerate first "
        "fundamental form at this (u, v)");
  }

  const double gaussian = (l_coeff * n_coeff - m_coeff * m_coeff) / denom;
  const double mean =
      (l_coeff * g_coeff - 2.0 * m_coeff * f_coeff + n_coeff * e_coeff) / (2.0 * denom);
  const double discriminant = std::max(0.0, mean * mean - gaussian);
  const double sqrt_discriminant = std::sqrt(discriminant);
  return SurfaceCurvature{gaussian, mean, mean + sqrt_discriminant, mean - sqrt_discriminant};
}

SurfaceDivisions NurbsSurface::SuggestedDivisions(double chord_tolerance,
                                                   int isocurve_samples) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::SuggestedDivisions: chord_tolerance "
        "must be positive");
  }

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);

  // dir=0: first parameter (u) varies, second (v) is held constant - an
  // isocurve running in the U direction. Sampling several of these
  // (at different fixed v) and taking the worst-case suggested sample
  // count accounts for a surface whose U-direction curvature varies
  // across v.
  int u_divisions = 1;
  for (int i = 0; i <= isocurve_samples; ++i) {
    const double v = v_domain.ParameterAt(static_cast<double>(i) / isocurve_samples);
    ON_Curve* iso = surface_.IsoCurve(0, v);
    if (iso == nullptr) {
      continue;
    }
    if (ON_NurbsCurve* nurbs_iso = ON_NurbsCurve::Cast(iso)) {
      NurbsCurve wrapped;
      wrapped.raw() = *nurbs_iso;
      u_divisions = std::max(u_divisions, wrapped.SuggestedSamples(chord_tolerance));
    }
    delete iso;
  }

  // dir=1: second parameter (v) varies, first (u) is held constant - the
  // V-direction counterpart, sampled the same way.
  int v_divisions = 1;
  for (int i = 0; i <= isocurve_samples; ++i) {
    const double u = u_domain.ParameterAt(static_cast<double>(i) / isocurve_samples);
    ON_Curve* iso = surface_.IsoCurve(1, u);
    if (iso == nullptr) {
      continue;
    }
    if (ON_NurbsCurve* nurbs_iso = ON_NurbsCurve::Cast(iso)) {
      NurbsCurve wrapped;
      wrapped.raw() = *nurbs_iso;
      v_divisions = std::max(v_divisions, wrapped.SuggestedSamples(chord_tolerance));
    }
    delete iso;
  }

  return SurfaceDivisions{u_divisions, v_divisions};
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

Mesh NurbsSurface::TessellateGridClippedExact(int u_divisions, int v_divisions,
                                               const std::vector<Point2d>& trim_polygon) const {
  if (!dino8::kernel::detail::IsSimplePolygon(trim_polygon)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsSurface::TessellateGridClippedExact: trim_polygon "
        "must be simple (non-self-intersecting) - a self-intersecting trim "
        "isn't decomposable into a well-defined \"inside\" at all");
  }

  Mesh mesh;
  ON_Mesh& raw = mesh.mesh_;

  const ON_Interval u_domain = surface_.Domain(0);
  const ON_Interval v_domain = surface_.Domain(1);
  auto u_at = [&](int i) { return u_domain.ParameterAt(static_cast<double>(i) / u_divisions); };
  auto v_at = [&](int j) { return v_domain.ParameterAt(static_cast<double>(j) / v_divisions); };

  // Dispatch by convexity: ClipConvex (Sutherland-Hodgman) is the
  // long-proven path every existing caller of exact clipping (in
  // particular Mesh::Cylinder()'s circular trim) already exercises;
  // ClipPolygon (Greiner-Hormann-style) is the newer, general fallback
  // that also handles a concave trim_polygon. Keeping both rather than
  // routing everything through the general path avoids regressing
  // already-proven convex behavior with a less battle-tested one.
  const bool trim_is_convex = IsConvexPolygon(trim_polygon);
  const double orientation_sign = SignedArea(trim_polygon) >= 0.0 ? 1.0 : -1.0;

  // ClipPolygon's own crossing detection deliberately excludes an
  // intersection landing within `kEps` of either segment's endpoint (see
  // its comment) - the standard way to avoid double-registering a
  // crossing at a shared vertex. That same exclusion misfires when a
  // trim_polygon vertex lands exactly on a cell's grid line: the cell
  // edge lying along that line hits the trim edge right at its endpoint,
  // gets excluded as "not a real crossing," and the cell's clipped
  // topology comes out wrong (a documented, previously-unhardened
  // degeneracy). Nudging any trim_polygon vertex that's suspiciously
  // close to a grid line off of it by a tiny fraction of one cell's
  // width - the standard "simulation of simplicity" fix for an exact
  // degeneracy in a numerical geometry algorithm, not a workaround for a
  // wrong algorithm - removes the coincidence with a shape change far
  // below this function's own kDuplicatePointEpsilon, let alone any
  // caller's area/volume tolerance. Convex trims go through ClipConvex
  // instead, which has no such exclusion, so this only applies to the
  // concave path.
  std::vector<Point2d> trim_for_clipping = trim_polygon;
  if (!trim_is_convex) {
    const double u_width = u_domain.Length() / u_divisions;
    const double v_width = v_domain.Length() / v_divisions;
    constexpr double kOnGridLineFraction = 1e-6;
    constexpr double kNudgeFraction = 1e-6;
    auto nudge_onto_grid_line = [](double coord, double origin, double width) {
      if (width == 0.0) {
        return coord;
      }
      const double steps = (coord - origin) / width;
      const double nearest_line = std::round(steps);
      if (std::abs(steps - nearest_line) < kOnGridLineFraction) {
        return origin + (nearest_line + kNudgeFraction) * width;
      }
      return coord;
    };
    for (Point2d& p : trim_for_clipping) {
      p.x = nudge_onto_grid_line(p.x, u_domain.Min(), u_width);
      p.y = nudge_onto_grid_line(p.y, v_domain.Min(), v_width);
    }
  }

  // A crossing point computed independently by two adjacent cells (or by
  // both loops of a split cell) can land at slightly different floating
  // point values; dedupe near-coincident consecutive points in a clipped
  // loop before triangulating it - a zero-area sliver edge is a real
  // topological defect (it corrupts ExtrudeCappedSolid's boundary-edge
  // extraction), not just a rendering nit.
  constexpr double kDuplicatePointEpsilon = 1e-9;

  for (int i = 0; i < u_divisions; ++i) {
    for (int j = 0; j < v_divisions; ++j) {
      const double u0 = u_at(i);
      const double u1 = u_at(i + 1);
      const double v0 = v_at(j);
      const double v1 = v_at(j + 1);
      // Same corner order as TessellateGrid's cell winding (v00,v10,v11,v01).
      const std::vector<Point2d> cell = {Point2d(u0, v0), Point2d(u1, v0), Point2d(u1, v1),
                                          Point2d(u0, v1)};

      // A cell straddling a concave trim boundary can clip into more than
      // one disjoint piece (e.g. the trim polygon's boundary crosses the
      // cell twice, cutting off two separate corners); each is
      // triangulated independently below. A convex trim never splits a
      // convex cell into more than one piece, so that path always
      // produces zero or one.
      std::vector<std::vector<Point2d>> pieces;
      if (trim_is_convex) {
        std::vector<Point2d> clipped = ClipConvex(cell, trim_polygon, orientation_sign);
        if (clipped.size() >= 3) {
          pieces.push_back(std::move(clipped));
        }
      } else {
        pieces = ClipPolygon(cell, trim_for_clipping);
      }

      for (const std::vector<Point2d>& piece : pieces) {
        std::vector<Point2d> deduped;
        deduped.reserve(piece.size());
        for (const Point2d& p : piece) {
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
        for (const std::array<int, 3>& tri : EarClipTriangulate(deduped)) {
          const Point2d& p0 = deduped[static_cast<size_t>(tri[0])];
          const Point2d& p1 = deduped[static_cast<size_t>(tri[1])];
          const Point2d& p2 = deduped[static_cast<size_t>(tri[2])];
          const double area2 = Cross2d(Point2d(p1.x - p0.x, p1.y - p0.y),
                                        Point2d(p2.x - p0.x, p2.y - p0.y));
          if (std::abs(area2) <= 1e-15) {
            continue;  // degenerate (e.g. three near-collinear points)
          }
          ON_MeshFace face;
          face.vi[0] = indices[static_cast<size_t>(tri[0])];
          face.vi[1] = indices[static_cast<size_t>(tri[1])];
          face.vi[2] = indices[static_cast<size_t>(tri[2])];
          face.vi[3] = face.vi[2];
          raw.m_F.Append(face);
        }
      }
    }
  }

  // Adjacent cells independently compute the same boundary-intersection
  // points as separate vertices; weld them into one consistent mesh.
  return Mesh::MergeAndWeld({mesh});
}

}  // namespace dino8::kernel
