// Measurement helpers shared by Loft (cmd_solids.cpp) and the single-section
// Sweep1/Sweep2 rebuilds (cmd_surface.cpp): how big a curve is, where along a
// rail a section sits, and how far a section is from an isocurve of a surface
// that is supposed to reproduce it. Header-only so neither command file needs
// a new translation unit.
//
// Everything here is read-only measurement - nothing builds geometry - so a
// caller can use it to REFUSE its own construction when the measured result is
// not what the construction promised (Loft promises to pass through every
// section; a section-anchored sweep promises to pass through the picked
// section), instead of trusting the construction unmeasured.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "commands/cmd_common.h"

namespace dino8::app::sectionfit {

// A measured distance in scientific notation ("3.21e-13"). FormatNumber() would
// print anything under 1e-9 as a bare "0", hiding exactly the number a
// measurement line exists to show.
inline std::string Sci(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2e", v);
  return buf;
}

// Bounding-box diagonal of the curve's control polygon hull, with the curve's
// own length as a floor (a degenerate/collinear box still has a size).
inline double CurveSize(const kernel::NurbsCurve& c) {
  const ON_BoundingBox bb = c.raw().BoundingBox();
  double d = bb.IsValid() ? bb.Diagonal().Length() : 0.0;
  if (!(d > 0.0)) d = c.Length();
  return d;
}

// `n` points evenly spaced in PARAMETER across the curve. A closed curve does
// not repeat its start point at the end.
inline std::vector<Point3d> SamplePoints(const kernel::NurbsCurve& c, int n) {
  std::vector<Point3d> out;
  const kernel::Interval d = c.Domain();
  const bool closed = c.IsClosed();
  for (int i = 0; i < n; ++i) {
    const double f = closed ? static_cast<double>(i) / n : static_cast<double>(i) / (n - 1);
    out.push_back(c.PointAt(d.min + (d.max - d.min) * f));
  }
  return out;
}

// Largest distance from any of `pts` to curve `c`.
inline double MaxDistanceToCurve(const std::vector<Point3d>& pts, const kernel::NurbsCurve& c, int search_samples = 200) {
  double worst = 0.0;
  for (const Point3d& p : pts) worst = std::max(worst, c.ClosestPoint(p, search_samples).DistanceTo(p));
  return worst;
}

// Two-sided deviation between two curves: the larger of (largest distance from
// a sample of `a` to `b`) and (largest distance from a sample of `b` to `a`).
// Zero (to the closest-point search's own ~1e-9 relative resolution) exactly
// when the two are the same point set, whatever their parameterizations.
inline double CurveDeviation(const kernel::NurbsCurve& a, const kernel::NurbsCurve& b, int samples = 64) {
  return std::max(MaxDistanceToCurve(SamplePoints(a, samples), b), MaxDistanceToCurve(SamplePoints(b, samples), a));
}

// Centroid of the curve's length (the mean of its points weighted by arc
// length), by 8-point Gauss-Legendre on every knot span: converges to ~1e-12 of
// the curve's size for a NURBS curve (a sampled mean does not - a rational
// circle sampled evenly in parameter has its mean visibly off the circle's
// centre, since its parameter is far from uniform in angle).
inline Point3d LengthCentroid(const kernel::NurbsCurve& c) {
  static const double x[4] = {0.1834346424956498, 0.5255324099163290, 0.7966664774136267, 0.9602898564975363};
  static const double w[4] = {0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763};
  const ON_NurbsCurve& r = c.raw();
  const kernel::Interval d = c.Domain();
  std::vector<double> knots = {d.min};
  for (int i = 0; i < r.KnotCount(); ++i) {
    const double k = r.Knot(i);
    if (k > knots.back() + 1e-12 * (d.max - d.min) && k < d.max - 1e-12 * (d.max - d.min)) knots.push_back(k);
  }
  knots.push_back(d.max);
  Point3d sum(0, 0, 0);
  double total = 0.0;
  for (size_t s = 0; s + 1 < knots.size(); ++s) {
    const double a = knots[s], b = knots[s + 1];
    for (int i = 0; i < 4; ++i) {
      for (int sign = -1; sign <= 1; sign += 2) {
        const double t = 0.5 * (a + b) + 0.5 * (b - a) * sign * x[i];
        ON_3dPoint p;
        ON_3dVector d1;
        if (!r.Ev1Der(t, p, d1)) continue;
        const double wt = 0.5 * (b - a) * w[i] * d1.Length();
        sum += p * wt;
        total += wt;
      }
    }
  }
  if (!(total > 0.0)) return c.PointAt(d.min);
  return sum / total;
}

// `t` moved onto the curve's nearest knot when it is within `rel` of the
// domain's width of one. A closest-point search lands a few 1e-9 beside a
// knot the seam should sit exactly on (a circle moved a quarter turn); a
// ChangeSeam there inserts a second knot a hair from the first, and
// Brep::Loft then refuses sections whose merged knot vectors carry
// near-duplicates ("knot vectors differ after refinement").
inline double SnapToKnot(const kernel::NurbsCurve& c, double t, double rel = 1e-6) {
  const ON_NurbsCurve& r = c.raw();
  const kernel::Interval d = c.Domain();
  const double window = rel * (d.max - d.min);
  double best = t, best_gap = window;
  for (int i = 0; i < r.KnotCount(); ++i) {
    const double k = r.Knot(i);
    if (k >= d.min && k <= d.max && std::fabs(k - t) <= best_gap) { best = k; best_gap = std::fabs(k - t); }
  }
  return best;
}

// Where a section sits along a rail.
struct RailAnchor {
  double t = 0.0;         // rail parameter of the anchor
  double distance = 0.0;  // distance from the section's reference point to the rail
  Point3d reference;      // the section point the anchor was taken from
};

// The rail point a section is anchored at: the rail's closest point to the
// section's reference point, where the reference point is the section's
// CENTROID when that is at least as close to the rail as the nearest point of
// the section itself (a profile that surrounds the rail, e.g. a circle around
// a pipe axis), otherwise the point of the section nearest the rail (a profile
// that starts on, or crosses, the rail). The centroid is the mean of sampled
// points, so it also works for open sections that cross the rail near their
// middle.
inline RailAnchor AnchorOnRail(const kernel::NurbsCurve& section, const kernel::NurbsCurve& rail, int samples = 96) {
  const std::vector<Point3d> pts = SamplePoints(section, samples);
  const Point3d centroid = LengthCentroid(section);
  int best = 0;
  double best_d = 1e300;
  for (size_t i = 0; i < pts.size(); ++i) {
    const double d = rail.ClosestPoint(pts[i], 200).DistanceTo(pts[i]);
    if (d < best_d) { best_d = d; best = static_cast<int>(i); }
  }
  // Refine the nearest section point by golden-section on the section's own
  // parameter, within the two neighbouring samples.
  const kernel::Interval sd = section.Domain();
  const bool closed = section.IsClosed();
  const double step = (sd.max - sd.min) / (closed ? samples : samples - 1);
  double lo = sd.min + step * (best - 1), hi = sd.min + step * (best + 1);
  if (!closed) { lo = std::max(lo, sd.min); hi = std::min(hi, sd.max); }
  auto dist_at = [&](double s) { const Point3d p = section.PointAt(s); return rail.ClosestPoint(p, 200).DistanceTo(p); };
  const double g = 0.6180339887498949;
  double a = lo, b = hi, c = b - g * (b - a), d = a + g * (b - a);
  double fc = dist_at(c), fd = dist_at(d);
  for (int it = 0; it < 40; ++it) {
    if (fc < fd) { b = d; d = c; fd = fc; c = b - g * (b - a); fc = dist_at(c); }
    else { a = c; c = d; fc = fd; d = a + g * (b - a); fd = dist_at(d); }
  }
  const double s_best = 0.5 * (a + b);
  const Point3d nearest = section.PointAt(s_best);
  const double nearest_d = std::min(best_d, rail.ClosestPoint(nearest, 200).DistanceTo(nearest));
  const double centroid_d = rail.ClosestPoint(centroid, 200).DistanceTo(centroid);
  RailAnchor out;
  const bool use_centroid = centroid_d <= nearest_d + 1e-9 * std::max(CurveSize(section), 1e-12);
  out.reference = use_centroid ? centroid : nearest;
  out.distance = use_centroid ? centroid_d : nearest_d;
  out.t = rail.ClosestPointParameter(out.reference, 400);
  return out;
}

}  // namespace dino8::app::sectionfit
