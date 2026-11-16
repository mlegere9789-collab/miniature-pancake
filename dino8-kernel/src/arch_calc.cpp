#include "dino8/kernel/arch_calc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dino8::kernel {

namespace {
constexpr double kEps = 1e-9;

std::string Num(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.4g", v);
  return buf;
}
}  // namespace

// --- stairs ----------------------------------------------------------------------------------------------------------------

StairCheck CheckStair(double riser, double tread, double width, const StairRules& r) {
  StairCheck c;
  c.blondel = 2.0 * riser + tread;
  if (riser > r.max_riser + kEps) {
    c.riser_ok = false;
    c.violations.push_back("riser " + Num(riser) + " exceeds the maximum " + Num(r.max_riser));
  }
  if (riser < r.min_riser - kEps) {
    c.riser_ok = false;
    c.violations.push_back("riser " + Num(riser) + " is below the minimum " + Num(r.min_riser));
  }
  if (tread < r.min_tread - kEps) {
    c.tread_ok = false;
    c.violations.push_back("tread " + Num(tread) + " is below the minimum " + Num(r.min_tread));
  }
  if (width < r.min_width - kEps) {
    c.width_ok = false;
    c.violations.push_back("run width " + Num(width) + " is below the minimum " + Num(r.min_width));
  }
  if (c.blondel < r.blondel_min - kEps || c.blondel > r.blondel_max + kEps) {
    c.blondel_ok = false;
    c.violations.push_back("2*riser + tread = " + Num(c.blondel) + " is outside [" + Num(r.blondel_min) + ", " + Num(r.blondel_max) + "]");
  }
  c.ok = c.riser_ok && c.tread_ok && c.width_ok && c.blondel_ok;
  return c;
}

std::vector<StairOption> ProposeStairOptions(double total_rise, const StairRules& r, double preferred_tread, int reference_risers) {
  std::vector<StairOption> out;
  if (!(total_rise > 0) || !(r.min_riser > 0)) return out;
  const double centre = 0.5 * (r.blondel_min + r.blondel_max);
  const int n_max = static_cast<int>(std::floor(total_rise / r.min_riser + kEps));
  for (int n = 1; n <= n_max; ++n) {
    const double riser = total_rise / n;
    if (riser > r.max_riser + kEps || riser < r.min_riser - kEps) continue;
    auto fits = [&](double t) {
      const double b = 2.0 * riser + t;
      return t >= r.min_tread - kEps && b >= r.blondel_min - kEps && b <= r.blondel_max + kEps;
    };
    double tread = std::max(centre - 2.0 * riser, r.min_tread);
    if (preferred_tread > 0 && fits(preferred_tread)) tread = preferred_tread;
    if (!fits(tread)) continue;
    StairOption o;
    o.risers = n;
    o.riser = riser;
    o.tread = tread;
    o.run = (n - 1) * tread;
    out.push_back(o);
  }
  std::stable_sort(out.begin(), out.end(), [&](const StairOption& a, const StairOption& b) {
    if (reference_risers > 0) {
      const int da = std::abs(a.risers - reference_risers), db = std::abs(b.risers - reference_risers);
      if (da != db) return da < db;
      return a.risers < b.risers;
    }
    return a.risers < b.risers;  // tallest riser first == fewest risers first
  });
  return out;
}

StairRelink RelinkStairToStory(int risers, double tread, double new_total_rise, const StairRules& rules) {
  StairRelink r;
  if (risers < 1 || !(new_total_rise > 0)) {
    r.reasons.push_back("a stair needs at least one riser and a positive story height");
    return r;
  }
  r.riser = new_total_rise / risers;
  StairCheck c = CheckStair(r.riser, tread, rules.min_width, rules);  // the run width is not affected by a story change
  r.blondel = c.blondel;
  r.valid = c.ok;
  r.reasons = c.violations;
  if (!r.valid) r.fixes = ProposeStairOptions(new_total_rise, rules, tread, risers);
  return r;
}

// --- ramps -----------------------------------------------------------------------------------------------------------------

RampPlan PlanRamp(double total_rise, double max_slope, double max_flight_run, double landing_length) {
  RampPlan p;
  if (!(total_rise > 0) || !(max_slope > 0) || !(max_flight_run > 0) || landing_length < 0) return p;
  const double min_run = total_rise / max_slope;
  p.flights = std::max(1, static_cast<int>(std::ceil(min_run / max_flight_run - 1e-9)));
  p.landings = p.flights - 1;
  p.run_per_flight = min_run / p.flights;
  p.total_run = min_run;
  p.total_length = p.total_run + p.landings * landing_length;
  p.slope = total_rise / p.total_run;
  p.ok = true;
  return p;
}

RampCheck CheckRamp(double rise_per_flight, double run_per_flight, int flights, double max_slope, double max_flight_run) {
  RampCheck c;
  if (!(run_per_flight > 0) || flights < 1) {
    c.ok = false;
    c.violations.push_back("a ramp needs a positive run and at least one flight");
    return c;
  }
  c.slope = rise_per_flight / run_per_flight;
  if (c.slope > max_slope + 1e-12) {
    c.ok = false;
    c.violations.push_back("slope " + Num(c.slope) + " exceeds the maximum " + Num(max_slope));
  }
  if (run_per_flight > max_flight_run + kEps) {
    c.ok = false;
    c.violations.push_back("flight run " + Num(run_per_flight) + " exceeds the maximum " + Num(max_flight_run) + " before a landing");
  }
  return c;
}

// --- roof pitch ------------------------------------------------------------------------------------------------------------

namespace {
constexpr double kPi = 3.14159265358979323846;
RoofPitch FromRadians(double rad) {
  RoofPitch p;
  p.valid = std::isfinite(rad) && std::fabs(rad) < 0.5 * kPi - 1e-12;
  if (!p.valid) return p;
  p.degrees = rad * 180.0 / kPi;
  p.rise_per_run = std::tan(rad);
  p.percent = 100.0 * p.rise_per_run;
  return p;
}
}  // namespace

RoofPitch PitchFromDegrees(double degrees) { return FromRadians(degrees * kPi / 180.0); }
RoofPitch PitchFromPercent(double percent) { return FromRadians(std::atan(percent / 100.0)); }
RoofPitch PitchFromRiseRun(double rise, double run) {
  if (!(run > 0)) return RoofPitch{};
  return FromRadians(std::atan2(rise, run));
}
RoofPitch PitchFromElevation(double z_ref, double z_point, double distance) { return PitchFromRiseRun(z_point - z_ref, distance); }

// --- polygons and zones ----------------------------------------------------------------------------------------------------

double PolygonAreaSigned(const std::vector<ArchPt2>& poly) {
  double s = 0;
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const ArchPt2& a = poly[i];
    const ArchPt2& b = poly[(i + 1) % n];
    s += a.x * b.y - b.x * a.y;
  }
  return 0.5 * s;
}
double PolygonArea(const std::vector<ArchPt2>& poly) { return std::fabs(PolygonAreaSigned(poly)); }

ZoneQuantities ComputeZone(const std::vector<ArchPt2>& poly, double height, const std::vector<ZoneElement>& elements,
                           double min_element_area) {
  ZoneQuantities q;
  q.gross_area = PolygonArea(poly);
  q.net_area = q.gross_area;
  double volume_area = q.gross_area;
  for (const ZoneElement& e : elements) {
    if (e.effect == ZoneEffect::None || e.area < min_element_area || !(e.area > 0)) continue;
    q.net_area -= e.area;
    if (e.effect == ZoneEffect::Subtract) volume_area -= e.area;
    ++q.elements_counted;
  }
  q.volume = volume_area * height;
  return q;
}

double RecessAddedArea(const std::vector<WallRecess>& recesses, double min_depth, double min_area, RecessRule rule) {
  double sum = 0;
  for (const WallRecess& r : recesses) {
    const bool deep = r.depth >= min_depth - kEps, big = r.area >= min_area - kEps;
    const bool add = rule == RecessRule::DepthAndArea ? (deep && big) : (deep || big);
    if (add) sum += r.area;
  }
  return sum;
}

std::vector<ArchPt2> ClipPolygonHalfPlane(const std::vector<ArchPt2>& poly, double a, double b, double c) {
  std::vector<ArchPt2> out;
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const ArchPt2& p = poly[i];
    const ArchPt2& q = poly[(i + 1) % n];
    const double fp = a * p.x + b * p.y + c, fq = a * q.x + b * q.y + c;
    const bool in_p = fp >= 0, in_q = fq >= 0;
    if (in_p) out.push_back(p);
    if (in_p != in_q) {
      const double t = fp / (fp - fq);
      out.push_back({p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)});
    }
  }
  return out;
}

LowCeilingResult ZoneAreaUnderLowCeilings(const std::vector<ArchPt2>& poly, double h0, double gx, double gy, const LowCeilingRule& rule) {
  LowCeilingResult r;
  r.full_area = PolygonArea(poly);
  double l1 = rule.limit1, p1 = rule.pct1, l2 = rule.limit2, p2 = rule.pct2;
  if (rule.two_limits && l2 > l1) {
    std::swap(l1, l2);
    std::swap(p1, p2);
  }
  // h(x, y) <= L  <=>  L - h0 - gx x - gy y >= 0
  r.area_below1 = PolygonArea(ClipPolygonHalfPlane(poly, -gx, -gy, l1 - h0));
  if (rule.two_limits) r.area_below2 = PolygonArea(ClipPolygonHalfPlane(poly, -gx, -gy, l2 - h0));
  r.counted_area = r.full_area - p1 * (r.area_below1 - r.area_below2) - (rule.two_limits ? p2 * r.area_below2 : 0.0);
  return r;
}

std::vector<ArchPt2> OffsetPolygonEdges(const std::vector<ArchPt2>& poly, const std::vector<double>& offsets) {
  const size_t n = poly.size();
  std::vector<ArchPt2> out;
  if (n < 3 || offsets.size() != n) return out;
  const double side = PolygonAreaSigned(poly) >= 0 ? 1.0 : -1.0;  // +1: interior on the left of each edge
  struct Line {
    ArchPt2 p, d;
  };
  std::vector<Line> lines(n);
  for (size_t i = 0; i < n; ++i) {
    const ArchPt2 &a = poly[i], &b = poly[(i + 1) % n];
    double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (!(len > 0)) return {};
    dx /= len;
    dy /= len;
    const double nx = -dy * side, ny = dx * side;  // unit normal pointing into the room
    lines[i] = {{a.x + nx * offsets[i], a.y + ny * offsets[i]}, {dx, dy}};
  }
  for (size_t i = 0; i < n; ++i) {
    const Line& la = lines[(i + n - 1) % n];
    const Line& lb = lines[i];
    const double cr = la.d.x * lb.d.y - la.d.y * lb.d.x;
    if (std::fabs(cr) < 1e-12) {
      out.push_back(lb.p);  // collinear neighbours: the vertex slides along the line
    } else {
      const double wx = lb.p.x - la.p.x, wy = lb.p.y - la.p.y;
      const double s = (wx * lb.d.y - wy * lb.d.x) / cr;
      out.push_back({la.p.x + s * la.d.x, la.p.y + s * la.d.y});
    }
  }
  return out;
}

double RoomBoundaryOffset(const LayeredWall& w, RoomBoundaryLocation where) {
  const double total = w.finish_room + w.core + w.finish_far;
  double pos = 0;  // distance of the boundary from the room-side face
  switch (where) {
    case RoomBoundaryLocation::WallFinish: pos = 0; break;
    case RoomBoundaryLocation::WallCenter: pos = 0.5 * total; break;
    case RoomBoundaryLocation::CoreCenter: pos = w.finish_room + 0.5 * w.core; break;
    case RoomBoundaryLocation::CoreBoundary: pos = w.finish_room; break;
  }
  return w.location_from_room - pos;
}

RoomQuantities ComputeRoom(const std::vector<ArchPt2>& loc, const std::vector<LayeredWall>& walls, RoomBoundaryLocation where, double height) {
  RoomQuantities q;
  if (walls.size() != loc.size()) return q;
  std::vector<double> off;
  for (const LayeredWall& w : walls) off.push_back(RoomBoundaryOffset(w, where));
  q.area = PolygonArea(OffsetPolygonEdges(loc, off));
  q.volume = q.area * height;
  return q;
}

// --- wall quantities -------------------------------------------------------------------------------------------------------

namespace {
// Integral over x in [a, b] of max(0, min(c, top(x)) - lo), top linear from h0 (x = 0) to h1 (x = length): piecewise linear with
// breaks only where top = c or top = lo, so the trapezoid rule on those pieces is exact.
double OpeningArea(double length, double h0, double h1, double a, double b, double lo, double c) {
  if (!(b > a) || !(c > lo)) return 0;
  auto top = [&](double x) { return h0 + (h1 - h0) * x / length; };
  auto f = [&](double x) { return std::max(0.0, std::min(c, top(x)) - lo); };
  std::vector<double> xs = {a, b};
  if (h1 != h0) {
    for (double level : {c, lo}) {
      const double x = (level - h0) * length / (h1 - h0);
      if (x > a && x < b) xs.push_back(x);
    }
  }
  std::sort(xs.begin(), xs.end());
  double s = 0;
  for (size_t i = 0; i + 1 < xs.size(); ++i) s += 0.5 * (f(xs[i]) + f(xs[i + 1])) * (xs[i + 1] - xs[i]);
  return s;
}
}  // namespace

WallQuantitiesResult ComputeWallQuantities(double length, double h0, double h1, double thickness, const std::vector<WallOpening>& openings) {
  WallQuantitiesResult q;
  if (!(length > 0)) return q;
  q.gross_side_area = 0.5 * (h0 + h1) * length;
  struct R {
    double xa, xb, lo, hi;
  };
  std::vector<R> rects;
  for (const WallOpening& o : openings) {
    if (!(o.width > 0) || !(o.height > 0)) continue;
    const double xa = std::max(0.0, o.x0), xb = std::min(length, o.x0 + o.width);
    const double lo = std::max(0.0, o.sill), hi = o.sill + o.height;
    const double a = OpeningArea(length, h0, h1, xa, xb, lo, hi);
    if (a > 0) {
      q.openings_area += a;
      ++q.openings_counted;
    }
    rects.push_back({xa, xb, lo, hi});
  }
  for (size_t i = 0; i < rects.size(); ++i)
    for (size_t j = i + 1; j < rects.size(); ++j) {
      const double dx = std::min(rects[i].xb, rects[j].xb) - std::max(rects[i].xa, rects[j].xa);
      const double dy = std::min(rects[i].hi, rects[j].hi) - std::max(rects[i].lo, rects[j].lo);
      if (dx > kEps && dy > kEps) q.ok = false;
    }
  q.net_side_area = q.gross_side_area - q.openings_area;
  q.gross_volume = q.gross_side_area * thickness;
  q.net_volume = q.net_side_area * thickness;
  return q;
}

}  // namespace dino8::kernel
