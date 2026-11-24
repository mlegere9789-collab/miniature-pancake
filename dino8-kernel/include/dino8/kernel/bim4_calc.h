#pragma once

// Second batch of building-design calculations (header only, namespace dino8::kernel::bim4), built on bim3_calc.h:
//  * multi-plane roofs over a convex footprint with a slope and an overhang per edge, wall tops attached to a roof or plane,
//    roofs from a planar mass face, openings through sloped roofs (vertical or perpendicular), soffit bands;
//  * rooms with nested loops (holes), room-bounding flags, room boundaries offset from wall centre lines, room numbering,
//    sloped ceilings with inner loops;
//  * stair run sketches and multi-storey stairs;
//  * slanted columns, braces, sloped beams, sloped foundation slabs;
//  * insulation take-off, pipe slope application, slope units, static-regain duct sizing.
// Units are SI and angles degrees at the API surface. Every limit is an input: no building code value is built in.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "dino8/kernel/bim3_calc.h"

namespace dino8::kernel::bim4 {

using bim3::kPi;
using bim3::P2;

// ===========================================================================================================================
// Convex polygon helpers.
inline std::vector<P2> ClipHalfPlane(const std::vector<P2>& poly, double a, double b, double c) { // keep a x + b y + c >= 0
  std::vector<P2> out;
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const P2 u = poly[i], v = poly[(i + 1) % n];
    const double fu = a * u.x + b * u.y + c, fv = a * v.x + b * v.y + c;
    if (fu >= 0) out.push_back(u);
    if ((fu > 0 && fv < 0) || (fu < 0 && fv > 0)) {
      const double t = fu / (fu - fv);
      out.push_back({u.x + t * (v.x - u.x), u.y + t * (v.y - u.y)});
    }
  }
  return out;
}

// ===========================================================================================================================
// Multi-plane roof over a convex footprint (counter-clockwise). Edge i runs from vertex i to vertex i + 1. A sloped edge carries
// a roof plane through the footprint edge at the eave elevation (0) rising inward at `slope`; an unsloped edge has no plane (a
// gable end). `overhang` extends every plane outward past its edge (the edge of the roof drops by slope * overhang; at an
// unsloped edge the roof simply continues by the overhang as a rake). The roof surface is the lower envelope of the planes:
// h(p) = min over sloped edges of slope_i * d_i(p), d_i = distance from p to the footprint edge line, positive inside.
struct RoofEdgeSpec {
  bool sloped = true;
  double slope = 0;
  double overhang = 0;
};
struct RoofFacet {
  int edge = 0;
  std::vector<P2> plan; // plan polygon of the facet inside the roof outline
  double plan_area = 0;
};
class ConvexRoof {
public:
  bool valid = false;
  std::vector<RoofFacet> facets;
  double outline_area = 0;     // plan area of the roof outline (footprint grown by the overhangs)
  double surface_area = 0;     // true area of all facets
  double volume = 0;           // between the eave plane and the roof over the footprint
  double ridge_height = 0;

  ConvexRoof(const std::vector<P2>& footprint, const std::vector<RoofEdgeSpec>& edges) : fp_(footprint), es_(edges) {
    const size_t n = fp_.size();
    if (n < 3 || edges.size() != n || bim3::SignedArea(fp_) <= 0) return;
    int sloped = 0;
    for (size_t i = 0; i < n; ++i) {
      const P2 u = fp_[i], v = fp_[(i + 1) % n];
      const double len = std::hypot(v.x - u.x, v.y - u.y);
      if (len < 1e-12) return;
      nx_.push_back(-(v.y - u.y) / len);
      ny_.push_back((v.x - u.x) / len);
      c_.push_back(nx_[i] * u.x + ny_[i] * u.y);
      if (es_[i].sloped) {
        ++sloped;
        if (es_[i].slope <= 0 || es_[i].overhang < 0) return;
      }
    }
    if (sloped == 0) return;
    // roof outline: footprint grown by the overhangs (intersection of the offset half-planes)
    std::vector<P2> outline = {{-1e6, -1e6}, {1e6, -1e6}, {1e6, 1e6}, {-1e6, 1e6}};
    for (size_t i = 0; i < n; ++i) outline = ClipHalfPlane(outline, nx_[i], ny_[i], -c_[i] + es_[i].overhang);
    outline_area = bim3::PolygonArea(outline);
    for (size_t i = 0; i < n; ++i) {
      if (!es_[i].sloped) continue;
      std::vector<P2> poly = outline;
      for (size_t j = 0; j < n; ++j) {
        if (j == i || !es_[j].sloped) continue;
        // keep s_j d_j - s_i d_i >= 0
        const double sj = es_[j].slope, si = es_[i].slope;
        poly = ClipHalfPlane(poly, sj * nx_[j] - si * nx_[i], sj * ny_[j] - si * ny_[i], -sj * c_[j] + si * c_[i]);
      }
      const double a = bim3::PolygonArea(poly);
      if (poly.size() < 3 || a < 1e-12) continue;
      facets.push_back({(int)i, poly, a});
      surface_area += a * std::sqrt(1.0 + es_[i].slope * es_[i].slope);
      // volume over the footprint: clip the facet by the footprint, integral of s d_i = s * A * d_i(centroid)
      std::vector<P2> in = poly;
      for (size_t k = 0; k < n; ++k) in = ClipHalfPlane(in, nx_[k], ny_[k], -c_[k]);
      if (in.size() >= 3 && bim3::PolygonArea(in) > 1e-12) {
        const P2 ce = bim3::PolygonCentroid(in);
        volume += es_[i].slope * bim3::PolygonArea(in) * Dist(i, ce);
      }
      for (const P2& p : poly) ridge_height = std::max(ridge_height, es_[i].slope * Dist(i, p));
    }
    valid = !facets.empty();
  }
  double Dist(size_t i, P2 p) const { return nx_[i] * p.x + ny_[i] * p.y - c_[i]; }
  // Roof height above the eave elevation at a plan point (negative under an overhang).
  double HeightAt(P2 p) const {
    double h = 1e300;
    for (size_t j = 0; j < es_.size(); ++j)
      if (es_[j].sloped) h = std::min(h, es_[j].slope * Dist(j, p));
    return h;
  }
  // Exact integral of (z_eave + h - z_base) along the plan segment a-b: the height is the minimum of linear functions of the
  // parameter, so it is linear between the crossings of any two planes.
  double WallAreaToRoof(P2 a, P2 b, double z_eave, double z_base) const {
    const size_t n = es_.size();
    std::vector<double> ts = {0.0, 1.0};
    std::vector<double> c0(n), m(n);
    for (size_t j = 0; j < n; ++j) {
      c0[j] = es_[j].slope * Dist(j, a);
      m[j] = es_[j].slope * (nx_[j] * (b.x - a.x) + ny_[j] * (b.y - a.y));
    }
    for (size_t i = 0; i < n; ++i)
      for (size_t j = i + 1; j < n; ++j) {
        if (!es_[i].sloped || !es_[j].sloped || std::fabs(m[i] - m[j]) < 1e-14) continue;
        const double t = (c0[j] - c0[i]) / (m[i] - m[j]);
        if (t > 0 && t < 1) ts.push_back(t);
      }
    std::sort(ts.begin(), ts.end());
    const double len = std::hypot(b.x - a.x, b.y - a.y);
    double area = 0;
    for (size_t k = 0; k + 1 < ts.size(); ++k) {
      const P2 p0 = {a.x + ts[k] * (b.x - a.x), a.y + ts[k] * (b.y - a.y)}, p1 = {a.x + ts[k + 1] * (b.x - a.x), a.y + ts[k + 1] * (b.y - a.y)};
      area += 0.5 * ((z_eave + HeightAt(p0) - z_base) + (z_eave + HeightAt(p1) - z_base)) * (ts[k + 1] - ts[k]) * len;
    }
    return area;
  }

private:
  std::vector<P2> fp_;
  std::vector<RoofEdgeSpec> es_;
  std::vector<double> nx_, ny_, c_;
};

// Wall top (or base) attached to a plane (floor, ceiling, reference plane): the area of the wall elevation between a flat base
// and the plane over the segment a-b, i.e. length * (mean plane height - base). The plane is z = z0 + slope * along.
inline double WallAreaToPlane(const bim3::SlopePlane& plane, P2 a, P2 b, double z_base) {
  const double len = std::hypot(b.x - a.x, b.y - a.y);
  return 0.5 * ((plane.ZAt(a.x, a.y) - z_base) + (plane.ZAt(b.x, b.y) - z_base)) * len;
}

// ---- roof from a planar mass face given by 3D vertices (counter-clockwise seen from outside).
struct MassFaceRoof {
  bool valid = false;
  double area = 0, plan_area = 0, slope = 0, tilt_deg = 0;
  double normal[3] = {0, 0, 0}; // unit normal
};
inline MassFaceRoof RoofFromMassFace(const std::vector<std::array<double, 3>>& v) {
  MassFaceRoof r;
  if (v.size() < 3) return r;
  double nx = 0, ny = 0, nz = 0;
  for (size_t i = 0; i < v.size(); ++i) { // Newell's method
    const auto& p = v[i];
    const auto& q = v[(i + 1) % v.size()];
    nx += (p[1] - q[1]) * (p[2] + q[2]);
    ny += (p[2] - q[2]) * (p[0] + q[0]);
    nz += (p[0] - q[0]) * (p[1] + q[1]);
  }
  const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
  if (len < 1e-12) return r;
  r.area = 0.5 * len;
  r.normal[0] = nx / len;
  r.normal[1] = ny / len;
  r.normal[2] = nz / len;
  r.plan_area = r.area * std::fabs(r.normal[2]);
  if (std::fabs(r.normal[2]) < 1e-12) return r; // vertical face: no roof
  r.slope = std::hypot(nx, ny) / std::fabs(nz);
  r.tilt_deg = std::atan(r.slope) * 180.0 / kPi;
  r.valid = true;
  return r;
}

// ---- openings through a plane of slope s and normal thickness t.
struct SlopedOpening {
  double face_area = 0;    // opening measured in the plane of the face
  double plan_area = 0;    // opening seen in plan on the face where it is sketched
  double volume = 0;       // material removed
  double bottom_shift = 0; // horizontal shift of the lower outline against the upper one (0 for a vertical cut)
};
// Cut perpendicular to the face: the sketch is drawn in the face plane.
inline SlopedOpening OpeningPerpendicular(double face_area, double slope, double t_normal) {
  const double k = std::sqrt(1.0 + slope * slope);
  return {face_area, face_area / k, face_area * t_normal, t_normal * slope / k};
}
// Cut along the vertical direction: the sketch is drawn in plan.
inline SlopedOpening OpeningVertical(double plan_area, double slope, double t_normal) {
  const double k = std::sqrt(1.0 + slope * slope);
  return {plan_area * k, plan_area, plan_area * (t_normal * k), 0.0};
}

// ---- soffit under a rectangular overhang: the plan band between the footprint and the footprint grown by per-edge
// overhangs (south, east, north, west); a sloped soffit has the band area times sqrt(1 + s^2). Layered type: volume = area * sum of
// layer thicknesses (normal thickness).
inline double SoffitBandPlanArea(double length, double width, const double overhang[4]) {
  return (length + overhang[1] + overhang[3]) * (width + overhang[0] + overhang[2]) - length * width;
}

// ===========================================================================================================================
// Rooms with nested loops.
struct RoomRegion {
  std::vector<P2> loop;
  double gross_area = 0; // area inside the outer loop
  double net_area = 0;   // minus the regions nested directly inside it
  int holes = 0;
};
inline bool StrictlyInside(const std::vector<P2>& poly, P2 p, double tol) {
  bool in = false;
  const size_t n = poly.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const P2 a = poly[i], b = poly[j];
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    const double t = l2 < 1e-300 ? 0 : std::max(0.0, std::min(1.0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2));
    if (std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y) <= tol) return false; // on the boundary
    if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) in = !in;
  }
  return in;
}
// Every bounded face of the line work, with the faces nested inside it (loops that touch nothing else) removed from its net area.
inline std::vector<RoomRegion> RoomRegions(const std::vector<bim3::Segment2>& segs, double tol = 1e-9) {
  const std::vector<bim3::Region> faces = bim3::BoundedRegions(segs, tol);
  std::vector<RoomRegion> out;
  for (const auto& f : faces) out.push_back({f.loop, f.area, f.area, 0});
  for (size_t j = 0; j < out.size(); ++j) {
    int best = -1;
    for (size_t i = 0; i < out.size(); ++i) {
      if (i == j || out[i].gross_area <= out[j].gross_area) continue;
      bool all_in = true;
      for (const P2& p : out[j].loop) all_in = all_in && StrictlyInside(out[i].loop, p, tol * 10);
      if (all_in && (best < 0 || out[i].gross_area < out[best].gross_area)) best = (int)i;
    }
    if (best >= 0) {
      out[best].net_area -= out[j].gross_area;
      ++out[best].holes;
    }
  }
  return out;
}
// Walls with a room-bounding flag: a wall that does not bound rooms is ignored when regions are found.
struct WallLine {
  bim3::Segment2 seg;
  bool room_bounding = true;
};
inline std::vector<RoomRegion> RoomRegionsFromWalls(const std::vector<WallLine>& walls, double tol = 1e-9) {
  std::vector<bim3::Segment2> s;
  for (const WallLine& w : walls)
    if (w.room_bounding) s.push_back(w.seg);
  return RoomRegions(s, tol);
}

// Room boundary of a closed counter-clockwise wall centre-line polygon, moved inward by `dist[i]` per edge (the lines are
// offset and adjacent offset lines intersected). False if two adjacent edges are parallel.
inline bool OffsetPolygonInward(const std::vector<P2>& poly, const std::vector<double>& dist, std::vector<P2>& out) {
  const size_t n = poly.size();
  out.clear();
  if (n < 3 || dist.size() != n) return false;
  std::vector<double> nx(n), ny(n), c(n);
  for (size_t i = 0; i < n; ++i) {
    const P2 u = poly[i], v = poly[(i + 1) % n];
    const double len = std::hypot(v.x - u.x, v.y - u.y);
    if (len < 1e-12) return false;
    nx[i] = -(v.y - u.y) / len;
    ny[i] = (v.x - u.x) / len;
    c[i] = nx[i] * u.x + ny[i] * u.y + dist[i]; // offset line: n . p = c
  }
  for (size_t k = 0; k < n; ++k) {
    const size_t a = (k + n - 1) % n, b = k;
    const double det = nx[a] * ny[b] - ny[a] * nx[b];
    if (std::fabs(det) < 1e-12) return false;
    out.push_back({(c[a] * ny[b] - ny[a] * c[b]) / det, (nx[a] * c[b] - c[a] * nx[b]) / det});
  }
  return true;
}
enum class RoomBoundaryAt { WallFinish, WallCenter, CoreFace, CoreCenter };
// Distance from the wall centre line to the room boundary on the room side, for a wall of interior finish `fin`, core `core` and
// exterior finish `fout` (location line at the centre of the whole wall).
inline double RoomBoundaryOffset(RoomBoundaryAt at, double fin, double core, double fout) {
  const double t = fin + core + fout;
  switch (at) {
    case RoomBoundaryAt::WallFinish: return t / 2.0;
    case RoomBoundaryAt::WallCenter: return 0.0;
    case RoomBoundaryAt::CoreFace: return t / 2.0 - fin;
    case RoomBoundaryAt::CoreCenter: return t / 2.0 - fin - core / 2.0;
  }
  return 0.0;
}

// Automatic room numbers: prefix + zero-padded counter + suffix. With a partition (e.g. by level) each group has its own start;
// numbers already in use (typed by hand) are skipped.
struct NumberingRule {
  std::string prefix, suffix;
  int start = 1, step = 1, digits = 1;
};
inline std::string FormatNumber(const NumberingRule& r, int n) {
  char b[32];
  std::snprintf(b, sizeof b, "%0*d", r.digits, n);
  return r.prefix + b + r.suffix;
}
inline std::vector<std::string> NumberRooms(size_t count, const NumberingRule& r, const std::set<std::string>& in_use) {
  std::vector<std::string> out;
  int n = r.start;
  while (out.size() < count) {
    const std::string s = FormatNumber(r, n);
    n += r.step > 0 ? r.step : 1;
    if (in_use.count(s)) continue;
    out.push_back(s);
  }
  return out;
}

// Sloped ceiling over a boundary with inner loops: surface area = plan area (holes removed) * sqrt(1 + s^2).
inline double CeilingSurfaceArea(const std::vector<P2>& boundary, const std::vector<std::vector<P2>>& inner, double slope) {
  return bim3::AreaWithHoles(boundary, inner) * std::sqrt(1.0 + slope * slope);
}

// ===========================================================================================================================
// Stairs.
struct StairRunPlan {
  bool valid = false;
  int risers = 0;
  double riser = 0, tread = 0;
};
// Sketch of a run: total rise H, largest riser, horizontal distance `run` from the first to the last riser line; riser lines are
// equally spaced: n = ceil(H / max_riser) risers and n - 1 treads of run / (n - 1).
inline StairRunPlan StairFromRunSketch(double total_rise, double max_riser, double run) {
  StairRunPlan p;
  if (total_rise <= 0 || max_riser <= 0 || run <= 0) return p;
  p.risers = (int)std::ceil(total_rise / max_riser - 1e-9);
  if (p.risers < 2) return p;
  p.riser = total_rise / p.risers;
  p.tread = run / (p.risers - 1);
  p.valid = true;
  return p;
}
// A stair repeated on several levels keeps the riser count of the base stair while that gives a riser inside [min_riser, max_riser];
// otherwise the count is recomputed as ceil(H / max_riser).
inline std::vector<std::pair<int, double>> MultistoryRisers(const std::vector<double>& level_heights, int base_risers, double min_riser, double max_riser) {
  std::vector<std::pair<int, double>> out;
  for (double h : level_heights) {
    int n = base_risers;
    const double r = h / n;
    if (r > max_riser + 1e-12 || r < min_riser - 1e-12) n = (int)std::ceil(h / max_riser - 1e-9);
    out.push_back({n, h / n});
  }
  return out;
}

// ===========================================================================================================================
// Structure.
struct SlantedColumn {
  double height = 0, length = 0, slant_deg = 0, offset = 0; // vertical height, member length, angle from vertical, plan offset
};
// Angle driven: base, top elevation and the slant angle from vertical.
inline SlantedColumn ColumnFromAngle(double base_level, double base_offset, double top_level, double top_offset, double slant_deg) {
  SlantedColumn c;
  c.height = (top_level + top_offset) - (base_level + base_offset);
  c.slant_deg = slant_deg;
  const double t = std::tan(slant_deg * kPi / 180.0);
  c.offset = c.height * t;
  c.length = c.height / std::cos(slant_deg * kPi / 180.0);
  return c;
}
// End-point driven: the plan offset between the two ends is given.
inline SlantedColumn ColumnFromEnds(double height, double plan_offset) {
  SlantedColumn c;
  c.height = height;
  c.offset = plan_offset;
  c.length = std::hypot(height, plan_offset);
  c.slant_deg = std::atan2(plan_offset, height) * 180.0 / kPi;
  return c;
}
struct Member3 {
  double length = 0, angle_from_horizontal_deg = 0, slope = 0;
};
// A member between two end points given as (plan distance along the run, elevation).
inline Member3 MemberBetween(double run, double z0, double z1) {
  Member3 m;
  const double dz = z1 - z0;
  m.length = std::hypot(run, dz);
  m.angle_from_horizontal_deg = std::atan2(std::fabs(dz), run) * 180.0 / kPi;
  m.slope = run > 0 ? dz / run : 0.0;
  return m;
}
// Sloped slab: plan area from the boundary offset by `edge_offset` (mitred), thickness measured vertically or normal to the face.
struct SlabVolume {
  double plan_area = 0, surface_area = 0, volume = 0;
};
inline SlabVolume SlopedSlab(double plan_area, double thickness, double slope, bool thickness_is_normal) {
  const double k = std::sqrt(1.0 + slope * slope);
  SlabVolume v;
  v.plan_area = plan_area;
  v.surface_area = plan_area * k;
  v.volume = thickness_is_normal ? v.surface_area * thickness : plan_area * thickness;
  return v;
}

// ===========================================================================================================================
// Pipes and ducts.
inline double InsulationVolumeRound(double outer_diameter, double thickness, double length) {
  return kPi * thickness * (outer_diameter + thickness) * length;
}
inline double InsulationOuterSurface(double outer_diameter, double thickness, double length) { return kPi * (outer_diameter + 2 * thickness) * length; }
struct InsulationLine {
  std::string size_label; // e.g. "DN50"
  double diameter = 0, thickness = 0, length = 0;
};
struct InsulationRow {
  std::string size_label;
  double thickness = 0, length = 0, volume = 0, surface = 0;
  int count = 0;
};
// Take-off grouped by (size, thickness), sorted by size label then thickness.
inline std::vector<InsulationRow> InsulationSchedule(const std::vector<InsulationLine>& lines) {
  std::map<std::pair<std::string, double>, InsulationRow> m;
  for (const InsulationLine& l : lines) {
    InsulationRow& r = m[{l.size_label, l.thickness}];
    r.size_label = l.size_label;
    r.thickness = l.thickness;
    r.length += l.length;
    r.volume += InsulationVolumeRound(l.diameter, l.thickness, l.length);
    r.surface += InsulationOuterSurface(l.diameter, l.thickness, l.length);
    ++r.count;
  }
  std::vector<InsulationRow> v;
  for (auto& kv : m) v.push_back(kv.second);
  return v;
}

// Slope notation: rise over run as a ratio, percent, degrees.
inline double SlopeFromPercent(double pct) { return pct / 100.0; }
inline double SlopeFromDegrees(double deg) { return std::tan(deg * kPi / 180.0); }
inline double SlopeFromRiseRun(double rise, double run) { return rise / run; }
inline double SlopeToPercent(double s) { return s * 100.0; }
inline double SlopeToDegrees(double s) { return std::atan(s) * 180.0 / kPi; }
// A route segment keeps its slope when an end elevation changes: the other end follows.
inline double EndElevationKeepingSlope(double z_fixed, double slope, double length_along_plan, bool fixed_is_start) {
  return fixed_is_start ? z_fixed + slope * length_along_plan : z_fixed - slope * length_along_plan;
}

// Apply a slope to a pipe network given as a tree of plan points. The slope rises with distance from the reference node,
// which keeps its elevation: z_i = z_ref + slope * (path length from the reference). `edges` are index pairs.
inline std::vector<double> ApplySlopeFromReference(const std::vector<P2>& pts, const std::vector<double>& z, const std::vector<std::pair<int, int>>& edges, int reference, double slope) {
  const size_t n = pts.size();
  std::vector<double> out = z, dist(n, -1.0);
  if (reference < 0 || (size_t)reference >= n) return out;
  std::vector<std::vector<int>> adj(n);
  for (const auto& e : edges) {
    adj[e.first].push_back(e.second);
    adj[e.second].push_back(e.first);
  }
  std::vector<int> stack = {reference};
  dist[reference] = 0;
  while (!stack.empty()) {
    const int u = stack.back();
    stack.pop_back();
    for (int v : adj[u]) {
      if (dist[v] >= 0) continue;
      dist[v] = dist[u] + std::hypot(pts[v].x - pts[u].x, pts[v].y - pts[u].y);
      stack.push_back(v);
    }
  }
  for (size_t i = 0; i < n; ++i)
    if (dist[i] >= 0) out[i] = z[reference] + slope * dist[i];
  return out;
}
inline int LowestPoint(const std::vector<double>& z) {
  int k = 0;
  for (size_t i = 1; i < z.size(); ++i)
    if (z[i] < z[k]) k = (int)i;
  return k;
}
// Static-regain duct sizing: the velocity reduction after a branch regains static pressure R * rho / 2 * (V1^2 - V2^2), set equal
// to the friction loss `dp_friction` of the downstream section: V2 = sqrt(V1^2 - 2 dp / (R rho)). Returns -1 when no real velocity
// exists (the regain cannot pay for the friction at this R).
inline double StaticRegainDownstreamVelocity(double v1, double rho, double regain_factor, double dp_friction) {
  if (regain_factor <= 0 || regain_factor > 1 || rho <= 0) return -1;
  const double v2sq = v1 * v1 - 2.0 * dp_friction / (regain_factor * rho);
  return v2sq > 0 ? std::sqrt(v2sq) : -1.0;
}
inline double StaticRegain(double v1, double v2, double rho, double regain_factor) { return regain_factor * rho / 2.0 * (v1 * v1 - v2 * v2); }

} // namespace dino8::kernel::bim4
