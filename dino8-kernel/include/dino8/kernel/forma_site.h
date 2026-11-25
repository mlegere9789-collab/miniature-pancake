// Site and building metrics (Forma-style): ground analysis grids with validity masks and a bounding circle, plot constraints (setbacks,
// height, coverage, area ratio), level stacks and area programs, unit mix, and street cross-sections with intersections.
//
// Header-only, standard library only. Plane coordinates x east, y north (metres); polygons are simple, either orientation.
#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "dino8/kernel/forma_solar.h"

namespace dino8 {
namespace kernel {
namespace forma {

inline double PolyArea(const std::vector<P2>& p) { return std::fabs(PolygonArea2(p)); }

// ------------------------------------------------------------------------------------------------------------------------------
// 2D predicates
// ------------------------------------------------------------------------------------------------------------------------------
inline double PointSegmentDistance(P2 p, P2 a, P2 b) {
  const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
  double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.0;
  t = std::max(0.0, std::min(1.0, t));
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// True when the open segments a-b and c-d cross at a single interior point of both.
inline bool SegmentsCrossProperly(P2 a, P2 b, P2 c, P2 d) {
  auto orient = [](P2 p, P2 q, P2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
  const double o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
  return ((o1 > 1e-12 && o2 < -1e-12) || (o1 < -1e-12 && o2 > 1e-12)) && ((o3 > 1e-12 && o4 < -1e-12) || (o3 < -1e-12 && o4 > 1e-12));
}

inline double SegmentSegmentDistance(P2 a, P2 b, P2 c, P2 d) {
  if (SegmentsCrossProperly(a, b, c, d)) return 0.0;
  return std::min(std::min(PointSegmentDistance(a, c, d), PointSegmentDistance(b, c, d)),
                  std::min(PointSegmentDistance(c, a, b), PointSegmentDistance(d, a, b)));
}

// Inside or on the boundary (within eps).
inline bool PointInPolygon(P2 p, const std::vector<P2>& poly, double eps = 1e-9) {
  const size_t n = poly.size();
  bool in = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    if (PointSegmentDistance(p, poly[j], poly[i]) <= eps) return true;
    if (((poly[i].y > p.y) != (poly[j].y > p.y)) && (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) in = !in;
  }
  return in;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Ground analysis grids
// ------------------------------------------------------------------------------------------------------------------------------
struct GroundGrid {
  double x0 = 0, y0 = 0, spacing = 1;
  int nx = 0, ny = 0;
  std::vector<char> valid;   // nx * ny, row by row from y0, 1 = the point can be evaluated
  P2 Point(int i, int j) const { return {x0 + i * spacing, y0 + j * spacing}; }
  int Count() const { return nx * ny; }
  int ValidCount() const {
    int n = 0;
    for (char c : valid) n += c ? 1 : 0;
    return n;
  }
  bool Valid(int i, int j) const { return valid[static_cast<size_t>(j) * nx + i] != 0; }
};

// Grid points x0 + i s <= xmax, y0 + j s <= ymax (the bounding box edges are included when they fall on a step).
inline GroundGrid MakeGroundGrid(double xmin, double ymin, double xmax, double ymax, double spacing) {
  GroundGrid g;
  g.x0 = xmin; g.y0 = ymin; g.spacing = spacing;
  g.nx = static_cast<int>(std::floor((xmax - xmin) / spacing + 1e-9)) + 1;
  g.ny = static_cast<int>(std::floor((ymax - ymin) / spacing + 1e-9)) + 1;
  g.valid.assign(static_cast<size_t>(g.nx) * g.ny, 1);
  return g;
}

// A point inside or on a building footprint cannot be evaluated; with a site polygon, a point outside it cannot either.
inline void MaskGrid(GroundGrid& g, const std::vector<std::vector<P2>>& footprints, const std::vector<P2>* site) {
  for (int j = 0; j < g.ny; ++j)
    for (int i = 0; i < g.nx; ++i) {
      const P2 p = g.Point(i, j);
      bool ok = true;
      if (site && !PointInPolygon(p, *site)) ok = false;
      for (const auto& f : footprints)
        if (ok && PointInPolygon(p, f)) ok = false;
      if (!ok) g.valid[static_cast<size_t>(j) * g.nx + i] = 0;
    }
}

struct Circle {
  double cx = 0, cy = 0, r = 0;
  bool Contains(P2 p) const { return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r * (1 + 1e-12); }
};

// Points outside the circle become invalid (the analysis area of wind, noise and microclimate).
inline void ClipGridToCircle(GroundGrid& g, const Circle& c) {
  for (int j = 0; j < g.ny; ++j)
    for (int i = 0; i < g.nx; ++i)
      if (!c.Contains(g.Point(i, j))) g.valid[static_cast<size_t>(j) * g.nx + i] = 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Plot constraints
// ------------------------------------------------------------------------------------------------------------------------------
struct Plot {
  std::string id;
  std::vector<P2> boundary;
  std::vector<double> setback;   // per boundary edge i (from boundary[i] to boundary[i + 1]); a single value applies to every edge
  double maxHeight = 0;          // 0 = no limit
  double maxCoverage = 0;        // footprint area / plot area; 0 = no limit
  double maxAreaRatio = 0;       // gross floor area / plot area; 0 = no limit
};
struct PlotBuilding {
  std::string id;
  std::vector<P2> footprint;
  double height = 0;
  double gfa = 0;
};
struct Violation {
  std::string plot, building, rule;   // rule: outside, setback, height, coverage, area_ratio
  int edge = -1;                      // boundary edge of a setback violation
  double value = 0, limit = 0;
};

inline std::vector<Violation> CheckPlot(const Plot& plot, const std::vector<PlotBuilding>& buildings) {
  std::vector<Violation> out;
  const size_t n = plot.boundary.size();
  const double plotArea = PolyArea(plot.boundary);
  double footprintSum = 0, gfaSum = 0;
  for (const PlotBuilding& b : buildings) {
    footprintSum += PolyArea(b.footprint);
    gfaSum += b.gfa;
    bool inside = true;
    for (const P2& v : b.footprint) inside = inside && PointInPolygon(v, plot.boundary);
    for (size_t i = 0; i < b.footprint.size() && inside; ++i)
      for (size_t j = 0; j < n && inside; ++j)
        if (SegmentsCrossProperly(b.footprint[i], b.footprint[(i + 1) % b.footprint.size()], plot.boundary[j], plot.boundary[(j + 1) % n])) inside = false;
    if (!inside) {
      out.push_back({plot.id, b.id, "outside", -1, 0, 0});
    } else {
      for (size_t j = 0; j < n; ++j) {
        const double need = plot.setback.empty() ? 0.0 : (plot.setback.size() == 1 ? plot.setback[0] : plot.setback[j]);
        if (need <= 0) continue;
        double d = 1e300;
        for (size_t i = 0; i < b.footprint.size(); ++i)
          d = std::min(d, SegmentSegmentDistance(b.footprint[i], b.footprint[(i + 1) % b.footprint.size()], plot.boundary[j], plot.boundary[(j + 1) % n]));
        if (d < need - 1e-9) out.push_back({plot.id, b.id, "setback", static_cast<int>(j), d, need});
      }
    }
    if (plot.maxHeight > 0 && b.height > plot.maxHeight + 1e-9) out.push_back({plot.id, b.id, "height", -1, b.height, plot.maxHeight});
  }
  if (plot.maxCoverage > 0 && plotArea > 0 && footprintSum / plotArea > plot.maxCoverage + 1e-12)
    out.push_back({plot.id, "", "coverage", -1, footprintSum / plotArea, plot.maxCoverage});
  if (plot.maxAreaRatio > 0 && plotArea > 0 && gfaSum / plotArea > plot.maxAreaRatio + 1e-12)
    out.push_back({plot.id, "", "area_ratio", -1, gfaSum / plotArea, plot.maxAreaRatio});
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Levels and area program
// ------------------------------------------------------------------------------------------------------------------------------
struct LevelRun {
  double height = 3.0;               // floor to floor
  int count = 1;                     // typical floor repetition
  std::string function;
  std::vector<P2> footprint;         // own outline; empty = the building footprint
};
struct UnitSpec {
  std::string type;
  double area = 0;
  int count = 1;
};
struct BuildingSpec {
  std::string id;
  std::vector<P2> footprint;
  std::vector<LevelRun> basements;   // first entry directly below the ground floor, then deeper
  std::vector<LevelRun> floors;      // first entry is the ground floor (elevation 0), then upwards
  std::vector<UnitSpec> units;
  int parkingSpots = 0;
  double parkingArea = 0;
};
struct Level {
  int index = 0;                     // ..., -2, -1 basements, 0 ground, 1, 2, ...
  double elevation = 0;              // of the floor slab, relative to the ground floor
  double height = 0, area = 0;
  std::string function;
  bool basement = false;
};

inline std::vector<Level> BuildLevels(const BuildingSpec& b) {
  std::vector<Level> out;
  double e = 0;
  int idx = 0;
  for (const LevelRun& r : b.floors)
    for (int k = 0; k < r.count; ++k) {
      Level l;
      l.index = idx++;
      l.elevation = e;
      l.height = r.height;
      l.area = PolyArea(r.footprint.empty() ? b.footprint : r.footprint);
      l.function = r.function;
      out.push_back(l);
      e += r.height;
    }
  double depth = 0;
  idx = -1;
  std::vector<Level> below;
  for (const LevelRun& r : b.basements)
    for (int k = 0; k < r.count; ++k) {
      depth += r.height;
      Level l;
      l.index = idx--;
      l.elevation = -depth;
      l.height = r.height;
      l.area = PolyArea(r.footprint.empty() ? b.footprint : r.footprint);
      l.function = r.function;
      l.basement = true;
      below.push_back(l);
    }
  std::reverse(below.begin(), below.end());   // deepest first
  below.insert(below.end(), out.begin(), out.end());
  return below;
}

inline double BuildingTopElevation(const std::vector<Level>& levels) {
  double top = 0;
  for (const Level& l : levels) top = std::max(top, l.elevation + l.height);
  return top;
}

struct Metrics {
  double siteArea = 0, groundFootprint = 0, coverage = 0;
  double gfaTotal = 0, gfaAbove = 0, gfaBelow = 0;
  std::map<std::string, double> gfaByFunction;
  std::map<std::string, double> zoneAreaByType;
  int unitCount = 0, parkingSpots = 0;
  double parkingArea = 0;
  std::map<std::string, int> unitsByType;
  std::map<std::string, double> unitAreaByType;   // summed area of the units of the type
};
struct Zone {
  std::string type;
  std::vector<P2> poly;
};

inline Metrics ProposalMetrics(const std::vector<P2>& site, const std::vector<BuildingSpec>& buildings, const std::vector<Zone>& zones = {}) {
  Metrics m;
  m.siteArea = PolyArea(site);
  for (const BuildingSpec& b : buildings) {
    const std::vector<Level> lv = BuildLevels(b);
    for (const Level& l : lv) {
      m.gfaTotal += l.area;
      (l.basement ? m.gfaBelow : m.gfaAbove) += l.area;
      m.gfaByFunction[l.function] += l.area;
      if (l.index == 0) m.groundFootprint += l.area;
    }
    for (const UnitSpec& u : b.units) {
      m.unitCount += u.count;
      m.unitsByType[u.type] += u.count;
      m.unitAreaByType[u.type] += u.area * u.count;
    }
    m.parkingSpots += b.parkingSpots;
    m.parkingArea += b.parkingArea;
  }
  for (const Zone& z : zones) m.zoneAreaByType[z.type] += PolyArea(z.poly);
  m.coverage = m.siteArea > 0 ? m.groundFootprint / m.siteArea : 0;
  return m;
}

// A named built-in metric by key: gfa, gfa_above, gfa_below, site_area, footprint, coverage, units, parking_spots, parking_area.
inline double MetricValue(const Metrics& m, const std::string& key) {
  if (key == "gfa") return m.gfaTotal;
  if (key == "gfa_above") return m.gfaAbove;
  if (key == "gfa_below") return m.gfaBelow;
  if (key == "site_area") return m.siteArea;
  if (key == "footprint") return m.groundFootprint;
  if (key == "coverage") return m.coverage;
  if (key == "units") return m.unitCount;
  if (key == "parking_spots") return m.parkingSpots;
  if (key == "parking_area") return m.parkingArea;
  return 0;
}
// A user-defined metric as the ratio of two built-in ones (denominator zero gives zero).
inline double CustomRatio(const Metrics& m, const std::string& numerator, const std::string& denominator) {
  const double d = MetricValue(m, denominator);
  return d != 0 ? MetricValue(m, numerator) / d : 0.0;
}

// Building functions: a name and a colour. One row per definition in definition order (area 0 when no level has that function); areas of
// functions without a definition are collected in one extra row named "(undefined)" with colour 0.
struct FunctionDef {
  std::string name;
  unsigned rgb = 0;
};
struct FunctionTotal {
  std::string name;
  unsigned rgb = 0;
  double area = 0, share = 0;   // share of the total gross floor area
};
inline std::vector<FunctionTotal> FunctionTotals(const Metrics& m, const std::vector<FunctionDef>& defs) {
  std::vector<FunctionTotal> out;
  std::map<std::string, double> left = m.gfaByFunction;
  for (const FunctionDef& d : defs) {
    FunctionTotal t;
    t.name = d.name;
    t.rgb = d.rgb;
    const auto it = left.find(d.name);
    if (it != left.end()) {
      t.area = it->second;
      left.erase(it);
    }
    out.push_back(t);
  }
  double other = 0;
  for (const auto& kv : left) other += kv.second;
  if (other > 0) {
    FunctionTotal t;
    t.name = "(undefined)";
    t.area = other;
    out.push_back(t);
  }
  for (FunctionTotal& t : out) t.share = m.gfaTotal > 0 ? t.area / m.gfaTotal : 0;
  return out;
}

// Unit mix: share of each type in the unit count, and the deviation from a target mix (actual - target, in share points).
struct MixRow {
  std::string type;
  int count = 0;
  double share = 0, meanArea = 0, target = 0, deviation = 0;
};
inline std::vector<MixRow> UnitMix(const Metrics& m, const std::map<std::string, double>& targetShare) {
  std::vector<MixRow> out;
  for (const auto& kv : m.unitsByType) {
    MixRow r;
    r.type = kv.first;
    r.count = kv.second;
    r.share = m.unitCount > 0 ? static_cast<double>(kv.second) / m.unitCount : 0;
    r.meanArea = kv.second > 0 ? m.unitAreaByType.at(kv.first) / kv.second : 0;
    const auto t = targetShare.find(kv.first);
    r.target = t == targetShare.end() ? 0 : t->second;
    r.deviation = r.share - r.target;
    out.push_back(r);
  }
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Streets
// ------------------------------------------------------------------------------------------------------------------------------
struct Strip {
  std::string kind;     // lane, median, sidewalk, ...
  double width = 0;
};
struct StreetSection {
  std::vector<Strip> strips;   // left to right when travelling along the centreline
  double Width() const {
    double w = 0;
    for (const Strip& s : strips) w += s.width;
    return w;
  }
  // Lateral offsets [a, b] of every strip from the centreline (positive to the left of travel); the section is centred on the centreline.
  std::vector<std::pair<double, double>> Offsets() const {
    std::vector<std::pair<double, double>> o;
    double x = Width() / 2;
    for (const Strip& s : strips) {
      o.push_back({x - s.width, x});   // the first strip is the leftmost: offsets +W/2 down to +W/2 - w
      x -= s.width;
    }
    return o;
  }
};

// Polygon of the band between lateral offsets a < b (positive to the left of travel) along a polyline with mitred joins.
inline std::vector<P2> OffsetBand(const std::vector<P2>& line, double a, double b) {
  auto side = [&](double d) {
    std::vector<P2> o;
    const size_t n = line.size();
    for (size_t i = 0; i < n; ++i) {
      V3 t0{0, 0, 0}, t1{0, 0, 0};
      if (i > 0) { t0 = {line[i].x - line[i - 1].x, line[i].y - line[i - 1].y, 0}; t0 = t0 * (1.0 / Len(t0)); }
      if (i + 1 < n) { t1 = {line[i + 1].x - line[i].x, line[i + 1].y - line[i].y, 0}; t1 = t1 * (1.0 / Len(t1)); }
      if (i == 0) t0 = t1;
      if (i + 1 == n) t1 = t0;
      const V3 n0{-t0.y, t0.x, 0}, n1{-t1.y, t1.x, 0};
      const double k = 1.0 + Dot(n0, n1);
      o.push_back({line[i].x + (n0.x + n1.x) / k * d, line[i].y + (n0.y + n1.y) / k * d});
    }
    return o;
  };
  std::vector<P2> poly = side(b);   // left edge forward
  std::vector<P2> back = side(a);
  std::reverse(back.begin(), back.end());
  poly.insert(poly.end(), back.begin(), back.end());
  return poly;
}

// Outline of two crossing straight streets (E-W of width w1 from x = -L to L, N-S of width w2 from y = -M to M) with a curb return of
// radius r at each of the four corners, every return approximated by `arcSegments` chords. Counter-clockwise.
inline std::vector<P2> IntersectionOutline(double w1, double w2, double L, double M, double r, int arcSegments) {
  std::vector<P2> o;
  const double hx = w2 / 2, hy = w1 / 2;
  auto arc = [&](double cx, double cy, double a0, double a1) {   // from angle a0 to a1 (degrees) around (cx, cy), r
    for (int k = 0; k <= arcSegments; ++k) {
      const double a = Rad(a0 + (a1 - a0) * k / arcSegments);
      o.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
  };
  o.push_back({L, -hy});
  o.push_back({L, hy});
  arc(hx + r, hy + r, 270, 180);      // (hx + r, hy) to (hx, hy + r)
  o.push_back({hx, M});
  o.push_back({-hx, M});
  arc(-hx - r, hy + r, 360, 270);     // (-hx, hy + r) to (-hx - r, hy)
  o.push_back({-L, hy});
  o.push_back({-L, -hy});
  arc(-hx - r, -hy - r, 90, 0);       // (-hx - r, -hy) to (-hx, -hy - r)
  o.push_back({-hx, -M});
  o.push_back({hx, -M});
  arc(hx + r, -hy - r, 180, 90);      // (hx, -hy - r) to (hx + r, -hy)
  return o;
}

}  // namespace forma
}  // namespace kernel
}  // namespace dino8
