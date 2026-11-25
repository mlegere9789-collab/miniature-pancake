// Pure OpenNURBS geometry for the AutoCAD LENGTHEN command (cmd_edit.cpp): change the length of an open line, polyline or arc at one
// end. No document, no kernel wrapper: the function takes and returns ON_NurbsCurve so it can be checked on its own.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "opennurbs.h"

namespace dino8::app {

// Returns the curve `in` with its length changed to `new_length`, the far end staying where it is. `at_start` says which end moves
// (the start, or the end). Lines and polylines (degree 1, polygonal, weights 1): the last segment is lengthened along its own direction
// or the chain is cut back to the new length. Arcs (circular, open): the included angle changes about the same centre and radius, so
// the arc length becomes new_length exactly. Anything else, and closed curves, are refused with a reason in `why`.
inline bool LengthenCurveEnd(const ON_NurbsCurve& in, bool at_start, double new_length, ON_NurbsCurve& out, std::string& why) {
  if (!(new_length > 0) || !std::isfinite(new_length)) { why = "the new length must be greater than zero"; return false; }
  if (in.IsClosed()) { why = "closed objects cannot be lengthened"; return false; }
  bool polygonal = in.Degree() == 1 && in.CVCount() >= 2;
  if (polygonal && in.IsRational()) {
    for (int i = 0; i < in.CVCount() && polygonal; ++i) polygonal = std::fabs(in.Weight(i) - 1.0) < 1e-12;
  }
  if (polygonal) {
    std::vector<ON_3dPoint> pts;
    for (int i = 0; i < in.CVCount(); ++i) { ON_3dPoint p; in.GetCV(i, p); pts.push_back(p); }
    if (at_start) std::reverse(pts.begin(), pts.end());  // always work at the back of the list
    double total = 0;
    for (size_t i = 0; i + 1 < pts.size(); ++i) total += pts[i].DistanceTo(pts[i + 1]);
    if (!(total > 0)) { why = "the curve has no length"; return false; }
    const double delta = new_length - total;
    if (delta >= 0) {
      size_t k = pts.size() - 1;
      while (k > 0 && pts[k].DistanceTo(pts[k - 1]) < 1e-12) --k;  // a zero-length last segment has no direction
      if (k == 0) { why = "the curve has no direction at that end"; return false; }
      ON_3dVector d = pts[k] - pts[k - 1];
      d.Unitize();
      pts.back() = pts[k] + d * delta;
    } else {
      double cut = -delta;
      while (pts.size() > 2) {
        const double seg = pts[pts.size() - 1].DistanceTo(pts[pts.size() - 2]);
        if (cut < seg) break;
        cut -= seg;
        pts.pop_back();
      }
      const ON_3dPoint a = pts[pts.size() - 2], b = pts[pts.size() - 1];
      const double seg = a.DistanceTo(b);
      if (!(seg > cut)) { why = "the new length is too small"; return false; }
      pts.back() = a + (b - a) * ((seg - cut) / seg);
    }
    if (at_start) std::reverse(pts.begin(), pts.end());
    ON_Polyline pl;
    for (const ON_3dPoint& p : pts) pl.Append(p);
    ON_PolylineCurve pc(pl);
    return pc.GetNurbForm(out) > 0;
  }
  ON_Arc arc;
  if (in.IsArc(nullptr, &arc, 1e-9) && arc.IsValid() && !in.IsLinear(1e-12)) {
    const double r = arc.Radius();
    const double ang = new_length / r;
    if (!(ang < 2.0 * ON_PI - 1e-9)) { why = "the arc would close into a full circle"; return false; }
    ON_Arc a2 = arc;
    const ON_Interval dom = arc.DomainRadians();
    const double lo = dom[0], hi = dom[1];
    if (!a2.SetAngleIntervalRadians(at_start ? ON_Interval(hi - ang, hi) : ON_Interval(lo, lo + ang))) { why = "the arc could not be rebuilt"; return false; }
    ON_NurbsCurve tmp;
    if (a2.GetNurbForm(tmp) <= 0) { why = "the arc could not be rebuilt"; return false; }
    // keep the direction of the original curve: the fixed end must stay the fixed end
    const ON_3dPoint fixed_in = at_start ? in.PointAtEnd() : in.PointAtStart();
    const ON_3dPoint fixed_tmp = at_start ? tmp.PointAtEnd() : tmp.PointAtStart();
    if (fixed_in.DistanceTo(fixed_tmp) > 1e-7 * std::max(1.0, r)) tmp.Reverse();
    out = tmp;
    return true;
  }
  why = "only lines, polylines and arcs can be lengthened";
  return false;
}

}  // namespace dino8::app
