// Hatch pattern generation shared by Hatch (cmd_drafting.cpp) and the hatch
// editing commands (HatchScale / HatchBase in cmd_annotate2.cpp).
//
// A hatch is a group of line curves clipped to a closed planar boundary.
// Every member carries user text so the hatch can be rebuilt later:
//   Hatch          pattern name (Solid, Hatch1, Grid, Hatch2)
//   HatchSpacing   line spacing in model units
//   HatchRotation  pattern angle in degrees
//   HatchBoundary  id of the boundary curve object (if it still exists)
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "commands/cmd_common.h"

namespace dino8::app {

inline const char* const kHatchPatternNames[] = {"Solid", "Hatch1", "Grid", "Hatch2"};

inline int HatchPatternIndex(const std::string& name) {
  for (int i = 0; i < 4; ++i) if (ToLower(name) == ToLower(kHatchPatternNames[i])) return i;
  return 1;
}

// Clips an infinite family of parallel lines to a closed polygon (even-odd).
// The line family passes through `base` (projected onto the plane), so
// hatches with the same base line up across neighbouring boundaries.
inline void HatchLines(const std::vector<Point3d>& poly, const ON_Plane& pl, double angle_deg, double spacing,
                       std::vector<kernel::NurbsCurve>& out, Point3d base = Point3d(0, 0, 0)) {
  if (poly.size() < 3 || spacing <= 0) return;
  const double a = angle_deg * ON_PI / 180.0;
  const Vector3d dir = pl.xaxis * std::cos(a) + pl.yaxis * std::sin(a);
  const Vector3d perp = ON_CrossProduct(pl.zaxis, dir);
  const Point3d origin = pl.ClosestPointTo(base);
  // Polygon in (s, t) coordinates: s along dir, t along perp.
  std::vector<std::pair<double, double>> p2;
  double tmin = 1e300, tmax = -1e300;
  for (const Point3d& p : poly) {
    const Vector3d d = p - origin;
    p2.push_back({ON_DotProduct(d, dir), ON_DotProduct(d, perp)});
    tmin = std::min(tmin, p2.back().second);
    tmax = std::max(tmax, p2.back().second);
  }
  if ((tmax - tmin) / spacing > 200000) return;  // absurd density: refuse rather than hang
  for (double t = std::floor(tmin / spacing) * spacing; t <= tmax; t += spacing) {
    std::vector<double> xs;
    for (size_t i = 0; i < p2.size(); ++i) {
      const auto& p = p2[i];
      const auto& q = p2[(i + 1) % p2.size()];
      if ((p.second > t) != (q.second > t)) xs.push_back(p.first + (q.first - p.first) * (t - p.second) / (q.second - p.second));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {
      const Point3d a0 = origin + dir * xs[i] + perp * t, a1 = origin + dir * xs[i + 1] + perp * t;
      if (a0.DistanceTo(a1) > 1e-9) out.push_back(PolylineCurve({a0, a1}));
    }
  }
}

// Samples a closed curve into a polygon (no repeated closing point).
inline std::vector<Point3d> BoundaryPolygon(const kernel::NurbsCurve& c) {
  std::vector<Point3d> poly;
  for (double t : c.SuggestedParameterValues(0.005)) poly.push_back(c.PointAt(t));
  if (poly.size() > 1 && poly.front().DistanceTo(poly.back()) < 1e-9) poly.pop_back();
  return poly;
}

// Line pattern for a boundary polygon: pattern 1 = one family, 2 = grid,
// 3 = family + a sparser perpendicular family.
inline std::vector<kernel::NurbsCurve> HatchPatternLines(const std::vector<Point3d>& poly, const ON_Plane& pl, int pattern,
                                                        double spacing, double rotation, Point3d base) {
  std::vector<kernel::NurbsCurve> lines;
  HatchLines(poly, pl, rotation, spacing, lines, base);
  if (pattern == 2) HatchLines(poly, pl, rotation + 90, spacing, lines, base);
  if (pattern == 3) HatchLines(poly, pl, rotation + 90, spacing * 2, lines, base);
  return lines;
}

// Builds one hatch (a group) inside `boundary`; returns the number of objects made.
inline int AddHatch(CommandContext& ctx, const kernel::NurbsCurve& boundary, ObjectId boundary_id, int layer, int pattern,
                    double spacing, double rotation) {
  ON_Plane pl;
  if (!boundary.raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance)) return 0;
  std::vector<ObjectId> ids_out;
  auto tag = [&](SceneObject& s) {
    s.layer_index = layer;
    s.user_text["Hatch"] = kHatchPatternNames[std::clamp(pattern, 0, 3)];
    s.user_text["HatchSpacing"] = FormatNumber(spacing);
    s.user_text["HatchRotation"] = FormatNumber(rotation);
    if (boundary_id != kNoObject) s.user_text["HatchBoundary"] = std::to_string(boundary_id);
  };
  if (pattern == 0) {
    if (ON_Brep* b = ON_BrepTrimmedPlane(pl, boundary.raw())) {
      kernel::Brep k; k.raw() = *b; delete b;
      SceneObject s = SceneObject::MakeBrep(k);
      s.name = "Hatch Solid";
      tag(s);
      ids_out.push_back(ctx.Doc().Add(std::move(s)));
    }
  } else {
    for (const kernel::NurbsCurve& l : HatchPatternLines(BoundaryPolygon(boundary), pl, pattern, spacing, rotation, ctx.Settings().hatch_base)) {
      SceneObject s = SceneObject::MakeCurve(l);
      tag(s);
      ids_out.push_back(ctx.Doc().Add(std::move(s)));
    }
  }
  if (!ids_out.empty()) ctx.Doc().CreateGroup(ids_out, "Hatch");
  return static_cast<int>(ids_out.size());
}

}  // namespace dino8::app
