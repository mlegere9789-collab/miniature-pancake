// Pure dimension geometry: the exact curve/arrow/tag math that turns a
// dimension's measured points (+ a fixed, replay-safe layout) into real
// Dino8 dimension geometry. Shared, line-for-line, by:
//   - cmd_annotate.cpp's BuildLinearDimensionGroup/BuildRadiusDimensionGroup,
//     which wrap these to add the result to the live document via
//     CommandContext (live Dim/DimAligned/DimRadius/DimDiameter commands,
//     and UpdateDimensions rebuilding an edited one), and
//   - DXF/DWG DIMENSION import (io/FileExchange.cpp, which has no
//     CommandContext at all).
// So an imported linear/aligned/radius/diameter dimension gets
// geometrically and tag-for-tag identical curves to one drawn in-app from
// the same measured points - selectable via SelDim (group name) and
// rebuildable via UpdateDimensions (the DimPlaneOrigin/X/Y, DimAligned,
// DimHorizontal, DimOffset / DimIsDiameter, DimDir, DimExtra, DimCenter,
// DimRadiusVal tags below are exactly what cmd_annotate.cpp's
// LoadLinearDimLayout/LoadRadiusDimLayout read back) the same way a dim
// built by hand is.
//
// No Document/CommandContext dependency at all (unlike commands/cmd_common.h,
// which drags in app/Application.h, commands/Command.h, commands/
// CommandEngine.h - see drafting/HatchBuild.h's comment for the exact
// io/FileExchange.cpp CurveFromON collision this avoids by staying
// decoupled), so this header is safe to include from io/FileExchange.cpp.
#pragma once

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/curve.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

// Defined in commands/CommandEngine.cpp; declared here rather than pulled in
// via commands/cmd_common.h (see the collision note above) - same pattern as
// drafting/HatchBuild.h.
std::string FormatNumber(double v);

// Point tag codec (same "%.10g,%.10g,%.10g" format as annotate_common.h's
// PointTag/ParsePointTag, which cmd_annotate.cpp's LoadLinearDimLayout/
// LoadRadiusDimLayout/ResolveLinearDimPoints/ResolveRadiusDimGeom read back
// with) - named distinctly (Dim-prefixed) so this header can be included
// alongside annotate_common.h in the same translation unit without a
// redefinition clash.
inline std::string DimPointTag(Point3d p) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%.10g,%.10g,%.10g", p.x, p.y, p.z);
  return buf;
}

// A DimLinear/DimAligned dimension's fixed layout - see cmd_annotate.cpp's
// original comment on this struct for the full rationale (`horizontal` only
// applies when !aligned; `offset` is the fixed dimension-line coordinate).
struct LinearDimLayout {
  bool aligned = false;
  bool horizontal = true;
  double offset = 0;
  ON_Plane plane;
};

// A DimRadius/DimDiameter dimension's fixed layout - see cmd_annotate.cpp's
// original comment on this struct.
struct RadiusDimLayout {
  bool diameter = false;
  ON_Plane plane;
  Vector3d dir;
  double extra = 0;
};

// A label to place as centred/left-aligned glyph-outline text. Structurally
// identical to annotate_common.h's GlyphSpec, but named distinctly for the
// same reason as DimPointTag above.
struct DimGlyphSpec {
  std::string text;
  double height = 1;
  ON_Plane plane;
  bool center = false;
};

namespace dim_geom_detail {

inline bool ToNurbsCurve(const ON_Curve& c, kernel::NurbsCurve& out) {
  ON_NurbsCurve nc;
  if (c.GetNurbForm(nc) <= 0) return false;
  out.raw() = nc;
  return true;
}

inline kernel::NurbsCurve MakePolyline(const std::vector<Point3d>& pts) {
  ON_Polyline pl;
  for (const Point3d& p : pts) pl.Append(p);
  ON_PolylineCurve pc(pl);
  kernel::NurbsCurve out;
  ToNurbsCurve(pc, out);
  return out;
}

inline void AddLine(std::vector<kernel::NurbsCurve>& out, Point3d a, Point3d b) { out.push_back(MakePolyline({a, b})); }

inline void AddArrow(std::vector<kernel::NurbsCurve>& out, Point3d tip, Vector3d dir, double size, const ON_Plane& pl) {
  dir.Unitize();
  Vector3d side = ON_CrossProduct(pl.zaxis, dir);
  side.Unitize();
  const Point3d a = tip - dir * size + side * (size * 0.3), b = tip - dir * size - side * (size * 0.3);
  out.push_back(MakePolyline({a, tip, b, a}));
}

}  // namespace dim_geom_detail

// Builds the curve list + label text + tag map for one DimLinear/DimAligned
// dimension from its two measured points and fixed layout - identical math
// to what the live Dim/DimAligned command computes from a hand-picked
// dimension-line location, just with the derived layout (aligned/
// horizontal/offset/plane) passed in directly instead of derived from a
// third pick point. Returns false (nothing built) if the two points
// coincide once projected onto the fixed dimension line, the same failure
// as the live command's zero-length pick.
inline bool BuildLinearDimensionGeometry(Point3d p0, Point3d p1, const LinearDimLayout& L, double text_h,
                                         std::vector<kernel::NurbsCurve>& curves, DimGlyphSpec& text,
                                         std::map<std::string, std::string>& tags, double* len_out = nullptr) {
  using namespace dim_geom_detail;
  const ON_Plane& pl = L.plane;
  Point3d a = p0, b = p1;
  Vector3d dir = b - a;
  if (!L.aligned) {
    double ua, va, ub, vb;
    pl.ClosestPointTo(a, &ua, &va); pl.ClosestPointTo(b, &ub, &vb);
    if (L.horizontal) { a = pl.PointAt(ua, L.offset); b = pl.PointAt(ub, L.offset); }
    else { a = pl.PointAt(L.offset, va); b = pl.PointAt(L.offset, vb); }
    dir = b - a;
  } else {
    Vector3d n = ON_CrossProduct(pl.zaxis, dir);
    n.Unitize();
    a = a + n * L.offset; b = b + n * L.offset;
  }
  const double len = dir.Length();
  if (len_out) *len_out = len;
  if (len <= 0) return false;
  curves.clear();
  AddLine(curves, a, b);
  AddLine(curves, p0, a);
  AddLine(curves, p1, b);
  AddArrow(curves, a, a - b, text_h, pl);
  AddArrow(curves, b, b - a, text_h, pl);
  Vector3d up = ON_CrossProduct(pl.zaxis, dir);
  up.Unitize();
  if (ON_DotProduct(up, pl.yaxis) < 0) up = -up;
  text.text = FormatNumber(len);
  text.height = text_h;
  text.plane = pl;
  text.plane.SetOrigin((a + b) / 2.0 + up * (text_h * 0.6));
  text.center = true;
  tags.clear();
  tags["DimAligned"] = L.aligned ? "1" : "0";
  tags["DimHorizontal"] = L.horizontal ? "1" : "0";
  tags["DimOffset"] = FormatNumber(L.offset);
  tags["DimPlaneOrigin"] = DimPointTag(pl.origin);
  tags["DimPlaneX"] = DimPointTag(Point3d(pl.xaxis));
  tags["DimPlaneY"] = DimPointTag(Point3d(pl.yaxis));
  tags["DimP0"] = DimPointTag(p0);
  tags["DimP1"] = DimPointTag(p1);
  return true;
}

// Builds the curve list + label text + tag map for one DimRadius/
// DimDiameter dimension from the measured circle/arc's center+radius and
// fixed layout - identical math to what the live DimRadius/DimDiameter
// command computes. Returns false for a non-positive radius (degenerate;
// never happens from a real selected arc/circle, but import can hand this a
// zero-radius pair of coincident points, so it is checked here rather than
// silently producing a zero-length dimension).
inline bool BuildRadiusDimensionGeometry(Point3d center, double radius, const RadiusDimLayout& L, double text_h,
                                         std::vector<kernel::NurbsCurve>& curves, DimGlyphSpec& text,
                                         std::map<std::string, std::string>& tags, double* val_out = nullptr) {
  using namespace dim_geom_detail;
  if (radius <= 0) return false;
  const ON_Plane& pl = L.plane;
  Vector3d d = L.dir;
  if (!d.Unitize()) d = pl.xaxis;
  const Point3d on = center + d * radius;
  const Point3d p = center + d * (radius + L.extra);
  curves.clear();
  if (L.diameter) { AddLine(curves, center - d * radius, p); AddArrow(curves, center - d * radius, -d, text_h, pl); }
  else AddLine(curves, center, p);
  AddArrow(curves, on, d, text_h, pl);
  const double val = L.diameter ? radius * 2 : radius;
  if (val_out) *val_out = val;
  tags.clear();
  tags["DimIsDiameter"] = L.diameter ? "1" : "0";
  tags["DimPlaneOrigin"] = DimPointTag(pl.origin);
  tags["DimPlaneX"] = DimPointTag(Point3d(pl.xaxis));
  tags["DimPlaneY"] = DimPointTag(Point3d(pl.yaxis));
  tags["DimDir"] = DimPointTag(Point3d(d));
  tags["DimExtra"] = FormatNumber(L.extra);
  tags["DimCenter"] = DimPointTag(center);
  tags["DimRadiusVal"] = FormatNumber(radius);
  text.text = std::string(L.diameter ? "D " : "R ") + FormatNumber(val);
  text.height = text_h;
  text.plane = pl;
  text.plane.SetOrigin(p + d * text_h);
  text.center = true;
  return true;
}

}  // namespace dino8::app
