#pragma once

// Building-design calculations that need no geometry kernel (header only, namespace dino8::kernel::bim3): sloped floors, roofs and
// ceilings, railing and handrail-support layout, spiral and winder treads, room regions / areas / volumes, opening schedules and
// door swing, duct and pipe sizing with documented equations, cable tray fill, wire size by voltage drop, panel demand loads,
// run splitting, structural load combinations and line / area load resultants.
//
// Units: SI throughout (metres, seconds, pascals, volt-amperes, kilograms); angles in degrees at the API surface. Every limit
// (maximum spacing, maximum velocity, fill limit, voltage-drop limit, ...) is an INPUT: nothing here is a code-compliance
// engine, and no national building code value is built in. The equations are cited next to each function.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dino8::kernel::bim3 {

constexpr double kPi = 3.14159265358979323846;

struct P2 {
  double x = 0, y = 0;
};

// ===========================================================================================================================
// Sloped planes: floor slope arrow / parallel offset lines / slope-defining line, roof slope arrow, ceiling per-edge heights.
// A one-way plane z(x, y) = z0 + slope * ((x - x0) * dx + (y - y0) * dy) with (dx, dy) a unit vector (the direction of rise).
struct SlopePlane {
  double x0 = 0, y0 = 0, z0 = 0;
  double dx = 1, dy = 0;
  double slope = 0; // rise / run
  double ZAt(double x, double y) const { return z0 + slope * ((x - x0) * dx + (y - y0) * dy); }
  double AngleDeg() const { return std::atan(slope) * 180.0 / kPi; }
  double Percent() const { return slope * 100.0; }
};

// Slope arrow: the plane rises from `tail` (height z_tail) to `head` (height z_head). False when tail == head.
inline bool SlopeFromArrow(P2 tail, P2 head, double z_tail, double z_head, SlopePlane& out) {
  const double ex = head.x - tail.x, ey = head.y - tail.y, d = std::hypot(ex, ey);
  if (d < 1e-12) return false;
  out = SlopePlane{tail.x, tail.y, z_tail, ex / d, ey / d, (z_head - z_tail) / d};
  return true;
}

// Two parallel boundary lines carrying different offsets from the base: line A (a0-a1) at height za, line B (through b0) at zb.
// False when the lines are not parallel (cross product of directions above 1e-9) or coincide.
inline bool SlopeFromParallelLines(P2 a0, P2 a1, double za, P2 b0, P2 b1, double zb, SlopePlane& out) {
  const double ax = a1.x - a0.x, ay = a1.y - a0.y, bx = b1.x - b0.x, by = b1.y - b0.y;
  const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
  if (la < 1e-12 || lb < 1e-12) return false;
  if (std::fabs(ax * by - ay * bx) / (la * lb) > 1e-9) return false;
  const double nx = -ay / la, ny = ax / la;                       // left normal of line A
  const double dist = (b0.x - a0.x) * nx + (b0.y - a0.y) * ny;   // signed distance of line B from line A
  if (std::fabs(dist) < 1e-12) return false;
  const double sgn = dist > 0 ? 1.0 : -1.0;
  out = SlopePlane{a0.x, a0.y, za, nx * sgn, ny * sgn, (zb - za) / std::fabs(dist)};
  return true;
}

// One boundary line (a0-a1) marked slope-defining: height z_line on the line, `slope` per unit run, rising to the left of the
// direction a0 -> a1 when side > 0 and to the right when side < 0.
inline bool SlopeFromDefiningLine(P2 a0, P2 a1, double z_line, double slope, int side, SlopePlane& out) {
  const double ax = a1.x - a0.x, ay = a1.y - a0.y, la = std::hypot(ax, ay);
  if (la < 1e-12) return false;
  const double s = side >= 0 ? 1.0 : -1.0;
  out = SlopePlane{a0.x, a0.y, z_line, -ay / la * s, ax / la * s, slope};
  return true;
}

// ---- compound layer stack with one variable-thickness layer (the shape edit moves only the top surface).
struct Layer {
  std::string name;
  double thickness = 0;
  bool variable = false;
};
struct LayerStack {
  bool valid = false;
  std::string error;
  std::vector<double> bottom, top; // elevation of each layer's lower and upper face
  double total = 0;
};
// `top_shift` = how far the top surface sits above (+) or below (-) its nominal elevation at the point of interest. Layers are
// listed bottom to top; the underside stays at z_base. Exactly one layer must be variable and its thickness t + top_shift
// must stay non-negative; layers above it move with the top surface.
inline LayerStack StackAtPoint(const std::vector<Layer>& layers, double z_base, double top_shift) {
  LayerStack r;
  int nvar = 0;
  for (const Layer& l : layers) nvar += l.variable ? 1 : 0;
  if (nvar != 1) {
    r.error = "exactly one layer must be flagged as variable thickness";
    return r;
  }
  double z = z_base;
  for (const Layer& l : layers) {
    const double t = l.thickness + (l.variable ? top_shift : 0.0);
    if (t < -1e-12) {
      r.error = "variable layer thickness would become negative";
      r.bottom.clear();
      r.top.clear();
      return r;
    }
    r.bottom.push_back(z);
    z += t;
    r.top.push_back(z);
  }
  r.total = z - z_base;
  r.valid = true;
  return r;
}

// ===========================================================================================================================
// Polygons (also used by rooms and areas).
inline double SignedArea(const std::vector<P2>& p) {
  double a = 0;
  for (size_t i = 0, n = p.size(); i < n; ++i) {
    const P2& u = p[i];
    const P2& v = p[(i + 1) % n];
    a += u.x * v.y - v.x * u.y;
  }
  return 0.5 * a;
}
inline double PolygonArea(const std::vector<P2>& p) { return std::fabs(SignedArea(p)); }
inline double PolygonPerimeter(const std::vector<P2>& p) {
  double l = 0;
  for (size_t i = 0, n = p.size(); i < n; ++i) l += std::hypot(p[(i + 1) % n].x - p[i].x, p[(i + 1) % n].y - p[i].y);
  return l;
}
inline P2 PolygonCentroid(const std::vector<P2>& p) {
  double a = 0, cx = 0, cy = 0;
  for (size_t i = 0, n = p.size(); i < n; ++i) {
    const P2& u = p[i];
    const P2& v = p[(i + 1) % n];
    const double c = u.x * v.y - v.x * u.y;
    a += c;
    cx += (u.x + v.x) * c;
    cy += (u.y + v.y) * c;
  }
  if (std::fabs(a) < 1e-300) return P2{};
  return P2{cx / (3.0 * a), cy / (3.0 * a)};
}
inline double AreaWithHoles(const std::vector<P2>& outer, const std::vector<std::vector<P2>>& holes) {
  double a = PolygonArea(outer);
  for (const auto& h : holes) a -= PolygonArea(h);
  return a;
}

// Volume of one variable-thickness layer over a plan polygon when its top surface follows a plane: the mean of a linear function
// over a polygon is its value at the centroid, so V = A * (t + shift(centroid)).
inline double VariableLayerVolume(double nominal_thickness, const std::vector<P2>& plan, const SlopePlane& top_shift_plane) {
  const P2 c = PolygonCentroid(plan);
  return PolygonArea(plan) * (nominal_thickness + top_shift_plane.ZAt(c.x, c.y));
}

// ===========================================================================================================================
// Roof from a rectangular footprint with per-edge slope definitions and an overhang.
// Footprint: x from 0 to `length`, y from 0 to `width`. Edges: 0 south (y = 0), 1 east (x = length), 2 north, 3 west. The
// sloping facets all share `slope` and pass through the footprint outline at the eave elevation (0); the overhang extends the
// same planes outward by `overhang` on every edge, dropping the roof edge by slope * overhang.
struct RoofRect {
  double width = 0, length = 0, overhang = 0, slope = 0;
  bool edge_sloped[4] = {true, true, true, true};
};
struct RoofResult {
  bool valid = false;
  std::string kind;          // "hip", "half-hip", "gable-x" (ridge along x, south/north edges slope), "gable-y"
  int facets = 0;
  double ridge_height = 0;   // above the eave elevation
  double ridge_length = 0;
  double eave_drop = 0;      // slope * overhang
  double surface_area = 0;   // roof surface including the overhang
  double volume = 0;         // between the eave plane and the roof, over the footprint
};
inline RoofResult AnalyseRoof(const RoofRect& r) {
  RoofResult o;
  const bool all = r.edge_sloped[0] && r.edge_sloped[1] && r.edge_sloped[2] && r.edge_sloped[3];
  const bool gx = r.edge_sloped[0] && r.edge_sloped[2] && !r.edge_sloped[1] && !r.edge_sloped[3];
  const bool gy = r.edge_sloped[1] && r.edge_sloped[3] && !r.edge_sloped[0] && !r.edge_sloped[2];
  const int n_sloped = (int)r.edge_sloped[0] + (int)r.edge_sloped[1] + (int)r.edge_sloped[2] + (int)r.edge_sloped[3];
  if (r.width <= 0 || r.length <= 0 || r.overhang < 0) return o;
  const double plan = (r.width + 2 * r.overhang) * (r.length + 2 * r.overhang);
  o.surface_area = plan * std::sqrt(1.0 + r.slope * r.slope);
  o.eave_drop = r.slope * r.overhang;
  if (all) {
    const double a = std::min(r.width, r.length), b = std::max(r.width, r.length);
    o.kind = "hip";
    o.facets = 4;
    o.ridge_height = r.slope * a / 2.0;
    o.ridge_length = b - a;
    o.volume = o.ridge_height * a * (3.0 * b - a) / 6.0;
  } else if (gx || gy) {
    const double span = gx ? r.width : r.length, along = gx ? r.length : r.width;
    o.kind = gx ? "gable-x" : "gable-y";
    o.facets = 2;
    o.ridge_height = r.slope * span / 2.0;
    o.ridge_length = along + 2 * r.overhang;
    o.volume = 0.5 * span * o.ridge_height * along;
  } else if (n_sloped == 3) {
    // Hip at one end, gable at the other: the unsloped edge is the gable end. The prism of the gable roof loses a pyramid of
    // volume span^2 h / 12 at the hipped end (base: the triangular end face, height span/2).
    int gable_edge = 0;
    for (int i = 0; i < 4; ++i)
      if (!r.edge_sloped[i]) gable_edge = i;
    const bool ridge_x = gable_edge == 1 || gable_edge == 3;
    const double span = ridge_x ? r.width : r.length, along = ridge_x ? r.length : r.width;
    if (along < span / 2.0) return o;
    o.kind = "half-hip";
    o.facets = 3;
    o.ridge_height = r.slope * span / 2.0;
    o.ridge_length = along - span / 2.0 + r.overhang;
    o.volume = 0.5 * span * o.ridge_height * along - span * span * o.ridge_height / 12.0;
  } else {
    return o;
  }
  o.valid = true;
  return o;
}

// ===========================================================================================================================
// Railings: post stations, baluster count by maximum clear gap, path planning, stair railings, handrail support layout.

// Posts on a run of length L: one at each end and equal bays no longer than max_spacing.
inline std::vector<double> PostStations(double length, double max_spacing) {
  std::vector<double> s;
  if (length <= 0 || max_spacing <= 0) return s;
  const int bays = std::max(1, (int)std::ceil(length / max_spacing - 1e-9));
  for (int i = 0; i <= bays; ++i) s.push_back(length * i / bays);
  return s;
}
// Fewest balusters of width b between two posts a bay apart so that every clear gap is at most max_clear:
// gap(n) = (bay - n b) / (n + 1) <= max_clear  <=>  n >= (bay - max_clear) / (b + max_clear).
inline int BalustersPerBay(double bay, double baluster_width, double max_clear) {
  if (bay <= max_clear) return 0;
  return (int)std::ceil((bay - max_clear) / (baluster_width + max_clear) - 1e-12);
}
inline double BalusterGap(double bay, double baluster_width, int n) { return (bay - n * baluster_width) / (n + 1); }

struct RailingPlan {
  double length = 0;
  std::vector<double> posts;                      // stations along the path
  std::vector<double> balusters;                  // centre stations along the path
  int bays = 0;
};
// Railing along a polyline path: a post at every vertex and equal bays (<= max_post_spacing) inside each segment.
inline RailingPlan PlanRailing(const std::vector<P2>& path, double max_post_spacing, double baluster_width, double max_clear) {
  RailingPlan r;
  double base = 0;
  for (size_t i = 0; i + 1 < path.size(); ++i) {
    const double len = std::hypot(path[i + 1].x - path[i].x, path[i + 1].y - path[i].y);
    const std::vector<double> ps = PostStations(len, max_post_spacing);
    for (size_t k = 0; k < ps.size(); ++k) {
      if (i > 0 && k == 0) continue; // shared vertex post
      r.posts.push_back(base + ps[k]);
    }
    for (size_t k = 0; k + 1 < ps.size(); ++k) {
      const double bay = ps[k + 1] - ps[k];
      const int n = BalustersPerBay(bay, baluster_width, max_clear);
      const double gap = BalusterGap(bay, baluster_width, n);
      for (int j = 0; j < n; ++j) r.balusters.push_back(base + ps[k] + gap * (j + 1) + baluster_width * (j + 0.5));
      ++r.bays;
    }
    base += len;
  }
  r.length = base;
  return r;
}

// Stair railing: the vertical profile (horizontal distance s, elevation z) of the nosing line, flights joined by level landings,
// then the same railing planner on that profile. A flight of n risers has (n - 1) treads: run = (n - 1) * tread, rise = n * riser.
struct StairFlight {
  int risers = 0;
  double riser = 0, tread = 0;
};
inline std::vector<P2> StairNosingProfile(const std::vector<StairFlight>& flights, double landing_length, double z0 = 0) {
  std::vector<P2> p;
  double s = 0, z = z0;
  p.push_back({s, z});
  for (size_t i = 0; i < flights.size(); ++i) {
    s += (flights[i].risers - 1) * flights[i].tread;
    z += flights[i].risers * flights[i].riser;
    p.push_back({s, z});
    if (i + 1 < flights.size() && landing_length > 0) {
      s += landing_length;
      p.push_back({s, z});
    }
  }
  return p;
}
// Top rail elevation above the profile: "follows host" = profile z + height, "level" = start z + height (a railing that does
// not follow the slope).
inline double TopRailElevation(const std::vector<P2>& profile, double s, double height, bool follow_host) {
  if (profile.empty()) return height;
  if (!follow_host) return profile.front().y + height;
  if (s <= profile.front().x) return profile.front().y + height;
  for (size_t i = 0; i + 1 < profile.size(); ++i) {
    if (s <= profile[i + 1].x + 1e-12) {
      const double ds = profile[i + 1].x - profile[i].x;
      const double t = ds < 1e-12 ? 0.0 : (s - profile[i].x) / ds;
      return profile[i].y + t * (profile[i + 1].y - profile[i].y) + height;
    }
  }
  return profile.back().y + height;
}

enum class SupportRule { FixedDistance, MaxSpacing, FixedNumber };
enum class Justify { Beginning, Center, End };
// Handrail supports along a rail of length L.
//  FixedDistance(d): stations k*d from the justified end (Beginning, End) or symmetric about the middle (Center), k = 0..floor(L/d).
//  MaxSpacing(d): equal bays no longer than d, supports at both ends.
//  FixedNumber(n): n supports at equal bays, the first and last at the ends (n = 1: the middle).
inline std::vector<double> SupportStations(double length, SupportRule rule, double value, Justify j) {
  std::vector<double> s;
  if (length <= 0 || value <= 0) return s;
  if (rule == SupportRule::FixedDistance) {
    const int m = (int)std::floor(length / value + 1e-9);
    const double start = j == Justify::Beginning ? 0.0 : j == Justify::Center ? (length - m * value) / 2.0 : length - m * value;
    for (int k = 0; k <= m; ++k) s.push_back(start + k * value);
  } else if (rule == SupportRule::MaxSpacing) {
    s = PostStations(length, value);
  } else {
    const int n = (int)std::lround(value);
    if (n == 1) s.push_back(length / 2.0);
    for (int i = 0; n > 1 && i < n; ++i) s.push_back(length * i / (n - 1));
  }
  return s;
}
// An unpinned support moved by hand: its station is replaced, the others stay.
inline std::vector<double> OverrideSupport(std::vector<double> stations, size_t index, double new_station) {
  if (index < stations.size()) stations[index] = new_station;
  return stations;
}
inline bool HandrailCountAllowed(int n) { return n >= 0 && n <= 2; }

// ===========================================================================================================================
// Stair treads on a circle sector (full-step spiral, winder corners): n treads share the total sweep angle, each a sector of
// angle theta between inner radius r and outer radius R. Straight edges are radial, so the depth at radius rho is the arc rho * theta.
struct SectorTread {
  double angle_rad = 0, area = 0, depth_inner = 0, depth_walk = 0, depth_outer = 0;
};
inline SectorTread SectorTreadGeometry(double total_angle_deg, int treads, double inner_r, double outer_r, double walk_r) {
  SectorTread t;
  if (treads <= 0) return t;
  t.angle_rad = total_angle_deg * kPi / 180.0 / treads;
  t.area = 0.5 * t.angle_rad * (outer_r * outer_r - inner_r * inner_r);
  t.depth_inner = inner_r * t.angle_rad;
  t.depth_walk = walk_r * t.angle_rad;
  t.depth_outer = outer_r * t.angle_rad;
  return t;
}
// Straight run of n risers: n - 1 treads (the upper floor is the last tread).
inline int StraightRunTreads(int risers) { return std::max(0, risers - 1); }

// ===========================================================================================================================
// Rooms: regions of a plan bounded by line work. Segments are split at every crossing and T-junction, the planar graph is built
// and its bounded faces are traced (each face keeps the interior on its left). Faces enclosed by an outer boundary that is not
// connected to the segment set are not nested-aware: a closed loop floating inside another room is reported as a separate region
// and the outer region keeps its full area.
struct Segment2 {
  P2 a, b;
};
struct Region {
  std::vector<P2> loop;
  double area = 0;
  P2 centroid;
};
inline std::vector<Region> BoundedRegions(const std::vector<Segment2>& segs, double tol = 1e-9) {
  std::vector<P2> verts;
  auto vid = [&](P2 p) {
    for (size_t i = 0; i < verts.size(); ++i)
      if (std::hypot(verts[i].x - p.x, verts[i].y - p.y) <= tol) return (int)i;
    verts.push_back(p);
    return (int)verts.size() - 1;
  };
  std::set<std::pair<int, int>> edges;
  for (size_t i = 0; i < segs.size(); ++i) {
    const P2 a = segs[i].a, b = segs[i].b;
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    if (len2 < tol * tol) continue;
    std::vector<std::pair<double, P2>> cuts;
    cuts.push_back({0.0, a});
    cuts.push_back({1.0, b});
    for (size_t j = 0; j < segs.size(); ++j) {
      if (j == i) continue;
      const P2 c = segs[j].a, d = segs[j].b;
      const double ex = d.x - c.x, ey = d.y - c.y;
      const double den = dx * ey - dy * ex;
      if (std::fabs(den) > 1e-12 * std::sqrt(len2 * (ex * ex + ey * ey))) {
        const double t = ((c.x - a.x) * ey - (c.y - a.y) * ex) / den;
        const double u = ((c.x - a.x) * dy - (c.y - a.y) * dx) / den;
        const double te = tol / std::sqrt(len2), ue = tol / std::sqrt(ex * ex + ey * ey);
        if (t > -te && t < 1 + te && u > -ue && u < 1 + ue) cuts.push_back({std::min(1.0, std::max(0.0, t)), P2{a.x + t * dx, a.y + t * dy}});
      } else {
        for (const P2& q : {c, d}) { // parallel: an endpoint of j lying on i splits i
          const double t = ((q.x - a.x) * dx + (q.y - a.y) * dy) / len2;
          const double px = a.x + t * dx, py = a.y + t * dy;
          if (t > 0 && t < 1 && std::hypot(px - q.x, py - q.y) <= tol) cuts.push_back({t, q});
        }
      }
    }
    std::sort(cuts.begin(), cuts.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
    int prev = vid(cuts[0].second);
    for (size_t k = 1; k < cuts.size(); ++k) {
      const int cur = vid(cuts[k].second);
      if (cur != prev) edges.insert({std::min(prev, cur), std::max(prev, cur)});
      prev = cur;
    }
  }
  std::vector<std::vector<int>> adj(verts.size());
  for (const auto& e : edges) {
    adj[e.first].push_back(e.second);
    adj[e.second].push_back(e.first);
  }
  for (size_t v = 0; v < adj.size(); ++v)
    std::sort(adj[v].begin(), adj[v].end(), [&](int p, int q) {
      return std::atan2(verts[p].y - verts[v].y, verts[p].x - verts[v].x) < std::atan2(verts[q].y - verts[v].y, verts[q].x - verts[v].x);
    });
  std::set<std::pair<int, int>> seen;
  std::vector<Region> out;
  for (size_t u0 = 0; u0 < adj.size(); ++u0)
    for (int v0 : adj[u0]) {
      if (seen.count({(int)u0, v0})) continue;
      std::vector<P2> loop;
      int u = (int)u0, v = v0;
      for (size_t guard = 0; guard < 4 * edges.size() + 8; ++guard) {
        if (seen.count({u, v})) break;
        seen.insert({u, v});
        loop.push_back(verts[u]);
        const std::vector<int>& nb = adj[v];
        size_t idx = 0;
        while (idx < nb.size() && nb[idx] != u) ++idx;
        const int w = nb[(idx + nb.size() - 1) % nb.size()];
        u = v;
        v = w;
      }
      const double a = SignedArea(loop);
      if (a > tol * 10) out.push_back(Region{loop, a, PolygonCentroid(loop)});
    }
  std::sort(out.begin(), out.end(), [](const Region& l, const Region& r) {
    return l.centroid.y != r.centroid.y ? l.centroid.y < r.centroid.y : l.centroid.x < r.centroid.x;
  });
  return out;
}

// Room height from a base level plus offset up to an upper-limit level plus offset; volume is area times height (zero when the
// limit lies at or below the base).
struct RoomLimits {
  double base_level = 0, base_offset = 0, limit_level = 0, limit_offset = 0;
  double Height() const { return (limit_level + limit_offset) - (base_level + base_offset); }
};
inline double RoomVolume(double area, const RoomLimits& l) { return l.Height() > 0 ? area * l.Height() : 0.0; }

// ===========================================================================================================================
// Openings (door / window schedule) and door swing.
struct OpeningRow {
  std::string mark, type;
  double width = 0, height = 0, sill = 0;
  double Head() const { return sill + height; }
  double Area() const { return width * height; }
};
struct OpeningTypeTotal {
  std::string type;
  int count = 0;
  double total_area = 0, min_sill = 0, max_head = 0;
};
inline std::vector<OpeningTypeTotal> ScheduleByType(const std::vector<OpeningRow>& rows) {
  std::map<std::string, OpeningTypeTotal> m;
  for (const OpeningRow& r : rows) {
    auto it = m.find(r.type);
    if (it == m.end()) {
      OpeningTypeTotal t;
      t.type = r.type;
      t.min_sill = r.sill;
      t.max_head = r.Head();
      it = m.emplace(r.type, t).first;
    }
    OpeningTypeTotal& t = it->second;
    ++t.count;
    t.total_area += r.Area();
    t.min_sill = std::min(t.min_sill, r.sill);
    t.max_head = std::max(t.max_head, r.Head());
  }
  std::vector<OpeningTypeTotal> v;
  for (auto& kv : m) v.push_back(kv.second);
  return v;
}
// Glazing area of a zone's windows over the zone's net floor area.
inline double GlazingRatio(const std::vector<double>& glazing_areas, double zone_net_area) {
  double s = 0;
  for (double a : glazing_areas) s += a;
  return zone_net_area > 0 ? s / zone_net_area : 0.0;
}

enum class Hinge { Left, Right };
// A door in a wall along +x from wall_x0 to wall_x0 + width; swing_side +1 opens toward +y, -1 toward -y.
struct DoorLeaf {
  double wall_x0 = 0, width = 0;
  Hinge hinge = Hinge::Left;
  int swing_side = 1;
};
inline P2 DoorHinge(const DoorLeaf& d) { return P2{d.hinge == Hinge::Left ? d.wall_x0 : d.wall_x0 + d.width, 0.0}; }
inline P2 DoorLeafTip(const DoorLeaf& d, double open_deg) {
  const double th = open_deg * kPi / 180.0, dir = d.hinge == Hinge::Left ? 1.0 : -1.0;
  const P2 h = DoorHinge(d);
  return P2{h.x + d.width * dir * std::cos(th), h.y + d.width * d.swing_side * std::sin(th)};
}
inline double DoorSwingArea(const DoorLeaf& d, double open_deg) { return 0.5 * (open_deg * kPi / 180.0) * d.width * d.width; }
inline DoorLeaf FlipHand(DoorLeaf d) {
  d.hinge = d.hinge == Hinge::Left ? Hinge::Right : Hinge::Left;
  return d;
}
inline DoorLeaf FlipFacing(DoorLeaf d) {
  d.swing_side = -d.swing_side;
  return d;
}

// ===========================================================================================================================
// Air and water distribution.
struct Fluid {
  double rho = 1.204;   // kg/m3
  double mu = 1.825e-5; // Pa s
};
inline double CircleArea(double d) { return kPi * d * d / 4.0; }
inline double VelocityRound(double q, double d) { return q / CircleArea(d); }
inline double DiameterForVelocity(double q, double v) { return std::sqrt(4.0 * q / (kPi * v)); }
inline double DynamicPressure(double rho, double v) { return 0.5 * rho * v * v; }
// Rectangular duct a x b: hydraulic diameter 4A/P, and the equal-friction circular equivalent of Huebscher (ASHRAE Fundamentals,
// duct design chapter): De = 1.30 (ab)^0.625 / (a+b)^0.25.
inline double HydraulicDiameterRect(double a, double b) { return 2.0 * a * b / (a + b); }
inline double EquivalentDiameterRect(double a, double b) { return 1.30 * std::pow(a * b, 0.625) / std::pow(a + b, 0.25); }

// Darcy friction factor: laminar f = 64 / Re (Re < 2300), otherwise the Colebrook-White equation
// 1/sqrt(f) = -2 log10( eps/(3.7 D) + 2.51 / (Re sqrt(f)) ) solved by fixed-point iteration.
inline double DarcyFriction(double re, double rel_roughness) {
  if (re <= 0) return 0;
  if (re < 2300.0) return 64.0 / re;
  double x = 1.0 / std::sqrt(0.02); // 1/sqrt(f)
  for (int i = 0; i < 200; ++i) {
    const double nx = -2.0 * std::log10(rel_roughness / 3.7 + 2.51 * x / re);
    if (std::fabs(nx - x) < 1e-13) {
      x = nx;
      break;
    }
    x = nx;
  }
  return 1.0 / (x * x);
}
// Pressure gradient in a round duct or pipe, Pa per metre: f / D * rho V^2 / 2.
inline double FrictionGradient(double q, double d, const Fluid& fl, double roughness) {
  const double v = VelocityRound(q, d), re = fl.rho * v * d / fl.mu;
  return DarcyFriction(re, roughness / d) / d * DynamicPressure(fl.rho, v);
}

struct DuctSize {
  bool ok = false;
  double exact = 0;    // diameter that meets the criterion exactly (equal friction only)
  double selected = 0; // chosen size
  double velocity = 0, friction = 0;
};
// Equal friction: the smallest listed diameter whose friction gradient does not exceed `target` (Pa/m). The exact diameter is found
// by bisection (the gradient falls monotonically with D).
inline DuctSize SizeRoundDuctEqualFriction(double q, double target, const Fluid& fl, double roughness, const std::vector<double>& sizes) {
  DuctSize r;
  double lo = 0.01, hi = 5.0;
  if (FrictionGradient(q, hi, fl, roughness) > target) return r;
  for (int i = 0; i < 200; ++i) {
    const double mid = 0.5 * (lo + hi);
    (FrictionGradient(q, mid, fl, roughness) > target ? lo : hi) = mid;
  }
  r.exact = hi;
  for (double s : sizes)
    if (s >= r.exact - 1e-12 && (r.selected == 0 || s < r.selected)) r.selected = s;
  if (r.selected == 0) return r;
  r.velocity = VelocityRound(q, r.selected);
  r.friction = FrictionGradient(q, r.selected, fl, roughness);
  r.ok = true;
  return r;
}
// Velocity criterion: the smallest listed diameter with V <= vmax.
inline DuctSize SizeRoundDuctVelocity(double q, double vmax, const Fluid& fl, double roughness, const std::vector<double>& sizes) {
  DuctSize r;
  for (double s : sizes)
    if (VelocityRound(q, s) <= vmax + 1e-12 && (r.selected == 0 || s < r.selected)) r.selected = s;
  if (r.selected == 0) return r;
  r.exact = DiameterForVelocity(q, vmax);
  r.velocity = VelocityRound(q, r.selected);
  r.friction = FrictionGradient(q, r.selected, fl, roughness);
  r.ok = true;
  return r;
}

// Section pressure loss: straight length by Darcy-Weisbach with the given friction factor, plus fittings K * rho V^2 / 2.
struct DuctSection {
  double q = 0, d = 0, length = 0, sum_k = 0;
};
inline double SectionLoss(const DuctSection& s, double darcy_f, const Fluid& fl) {
  const double dyn = DynamicPressure(fl.rho, VelocityRound(s.q, s.d));
  return darcy_f * s.length / s.d * dyn + s.sum_k * dyn;
}
// Sections form a tree (parent index, -1 for the root section). The system loss is the largest cumulative loss over any
// root-to-terminal path; `path` lists the sections of that worst path from the root.
struct WorstPath {
  double total = 0;
  std::vector<int> path;
};
inline WorstPath WorstPathLoss(const std::vector<int>& parent, const std::vector<double>& loss) {
  WorstPath w;
  const int n = (int)parent.size();
  std::vector<bool> has_child(n, false);
  for (int i = 0; i < n; ++i)
    if (parent[i] >= 0) has_child[parent[i]] = true;
  for (int i = 0; i < n; ++i) {
    if (has_child[i]) continue;
    double sum = 0;
    std::vector<int> p;
    for (int k = i; k >= 0; k = parent[k]) {
      sum += loss[k];
      p.push_back(k);
    }
    if (sum > w.total || w.path.empty()) {
      w.total = sum;
      w.path.assign(p.rbegin(), p.rend());
    }
  }
  return w;
}

// Hazen-Williams head loss per metre of pipe (m of water per m), SI form:  hf/L = 10.67 Q^1.852 / (C^1.852 D^4.87).
inline double HazenWilliamsGradient(double q, double d, double c) { return 10.67 * std::pow(q, 1.852) / (std::pow(c, 1.852) * std::pow(d, 4.87)); }
enum class CriteriaLogic { And, Or };
struct PipeCriteria {
  bool use_velocity = false, use_friction = false;
  double max_velocity = 0;  // m/s
  double max_gradient = 0;  // m of water per m (Hazen-Williams)
  CriteriaLogic logic = CriteriaLogic::And;
};
// Index of the smallest listed inside diameter meeting the criteria (AND: both active criteria; OR: either), or -1.
inline int SelectPipeSize(double q, double c, const std::vector<double>& inside_diameters, const PipeCriteria& cr) {
  int best = -1;
  for (size_t i = 0; i < inside_diameters.size(); ++i) {
    const double d = inside_diameters[i];
    const bool v_ok = !cr.use_velocity || VelocityRound(q, d) <= cr.max_velocity + 1e-12;
    const bool f_ok = !cr.use_friction || HazenWilliamsGradient(q, d, c) <= cr.max_gradient + 1e-15;
    bool ok;
    if (cr.use_velocity && cr.use_friction) ok = cr.logic == CriteriaLogic::And ? (v_ok && f_ok) : (v_ok || f_ok);
    else ok = v_ok && f_ok;
    if (ok && (best < 0 || d < inside_diameters[best])) best = (int)i;
  }
  return best;
}

// ---- cable tray and conduit fill: sum of cable cross-sections over the inside cross-section.
inline double CableFillRatio(const std::vector<double>& cable_diameters, double inside_area) {
  double s = 0;
  for (double d : cable_diameters) s += CircleArea(d);
  return inside_area > 0 ? s / inside_area : 0.0;
}
inline double TrayFillRatio(const std::vector<double>& cable_diameters, double width, double depth) { return CableFillRatio(cable_diameters, width * depth); }

// ---- wire size by voltage drop. R' = rho / A per conductor; single phase drop = 2 I R' L, three phase = sqrt(3) I R' L.
struct WireChoice {
  bool ok = false;
  size_t index = 0;
  double drop_volts = 0, drop_percent = 0;
};
inline WireChoice SizeWireByDrop(double current, double length, double volts, int phases, double limit_percent, const std::vector<double>& areas_ascending, double resistivity) {
  WireChoice w;
  const double k = phases == 3 ? std::sqrt(3.0) : 2.0;
  for (size_t i = 0; i < areas_ascending.size(); ++i) {
    const double drop = k * current * (resistivity / areas_ascending[i]) * length;
    if (drop / volts * 100.0 <= limit_percent + 1e-12) {
      w.ok = true;
      w.index = i;
      w.drop_volts = drop;
      w.drop_percent = drop / volts * 100.0;
      return w;
    }
  }
  return w;
}

// ---- panel connected and demand load. Demand rule per load classification: a constant factor, or incremental tiers (the portion
// of the classification total up to `limit` is multiplied by `factor`, the next portion by the next tier, ...).
struct DemandTier {
  double limit = 0, factor = 1;
};
struct DemandRule {
  double constant = 1;
  std::vector<DemandTier> tiers; // empty: constant factor; the last tier's limit is ignored (it takes the remainder)
};
inline double DemandLoad(double connected, const DemandRule& r) {
  if (r.tiers.empty()) return connected * r.constant;
  double rest = connected, prev = 0, demand = 0;
  for (size_t i = 0; i < r.tiers.size(); ++i) {
    const double span = i + 1 == r.tiers.size() ? rest : std::min(rest, r.tiers[i].limit - prev);
    demand += std::max(0.0, span) * r.tiers[i].factor;
    rest -= std::max(0.0, span);
    prev = r.tiers[i].limit;
  }
  return demand;
}
struct LoadItem {
  std::string classification;
  double va = 0;
};
struct PanelTotals {
  std::map<std::string, double> connected, demand;
  double total_connected = 0, total_demand = 0;
};
inline PanelTotals PanelLoadTotals(const std::vector<LoadItem>& items, const std::map<std::string, DemandRule>& rules) {
  PanelTotals t;
  for (const LoadItem& i : items) t.connected[i.classification] += i.va;
  for (const auto& kv : t.connected) {
    auto it = rules.find(kv.first);
    const double d = it == rules.end() ? kv.second : DemandLoad(kv.second, it->second);
    t.demand[kv.first] = d;
    t.total_connected += kv.second;
    t.total_demand += d;
  }
  return t;
}
inline double ThreePhaseCurrent(double va, double line_to_line_volts) { return va / (std::sqrt(3.0) * line_to_line_volts); }

// ---- run splitting between a minimum and a maximum piece length. Full pieces of max length; a leftover shorter than min is
// stretched to min and the last full piece shortened by the same amount. ok = false when that is impossible (run shorter than
// min, or the shortened piece would fall below min); the pieces are then empty, except for a run shorter than min, which is
// returned as one flagged piece.
struct SplitResult {
  bool ok = false;
  std::vector<double> pieces;
};
inline SplitResult SplitRun(double length, double min_len, double max_len) {
  SplitResult s;
  if (length <= 0 || min_len <= 0 || max_len < min_len) return s;
  if (length <= max_len) {
    s.pieces.push_back(length);
    s.ok = length >= min_len;
    return s;
  }
  const int k = (int)std::floor(length / max_len + 1e-12);
  const double rest = length - k * max_len;
  for (int i = 0; i < k; ++i) s.pieces.push_back(max_len);
  if (rest < 1e-9) {
    s.ok = true;
    return s;
  }
  if (rest >= min_len) {
    s.pieces.push_back(rest);
    s.ok = true;
    return s;
  }
  const double shortened = max_len - (min_len - rest);
  if (shortened < min_len) {
    s.pieces.clear(); // not splittable by one adjustment: no layout is proposed
    return s;
  }
  s.pieces.back() = shortened;
  s.pieces.push_back(min_len);
  s.ok = true;
  return s;
}

// ===========================================================================================================================
// Structural load combinations.
struct LoadTerm {
  std::string load_case;
  double factor = 0;
};
struct LoadCombo {
  std::string name;
  std::vector<LoadTerm> terms;
};
inline std::string FormatFactor(double f) {
  char b[32];
  std::snprintf(b, sizeof b, "%g", f);
  return b;
}
inline double EvaluateCombo(const LoadCombo& c, const std::map<std::string, double>& values) {
  double s = 0;
  for (const LoadTerm& t : c.terms) {
    auto it = values.find(t.load_case);
    if (it != values.end()) s += t.factor * it->second;
  }
  return s;
}
// Expansion helper: a combination is a list of slots; a slot is a list of alternatives (a term each). A required slot whose
// alternatives are all unavailable drops the whole combination; an optional one simply contributes nothing.
struct ComboSlot {
  bool required = true;
  std::vector<LoadTerm> alternatives;
};
inline void ExpandSlots(const std::vector<ComboSlot>& slots, const std::set<std::string>& avail, std::vector<LoadCombo>& out) {
  std::vector<std::vector<LoadTerm>> acc(1);
  for (const ComboSlot& s : slots) {
    std::vector<LoadTerm> ok;
    for (const LoadTerm& t : s.alternatives)
      if (avail.count(t.load_case)) ok.push_back(t);
    if (ok.empty()) {
      if (s.required) return;
      continue;
    }
    std::vector<std::vector<LoadTerm>> next;
    for (const auto& a : acc)
      for (const LoadTerm& t : ok) {
        auto b = a;
        b.push_back(t);
        next.push_back(b);
      }
    acc.swap(next);
  }
  for (const auto& a : acc) {
    LoadCombo c;
    c.terms = a;
    for (size_t i = 0; i < a.size(); ++i) c.name += (i ? "+" : "") + FormatFactor(a[i].factor) + a[i].load_case;
    bool dup = false;
    for (const LoadCombo& o : out) dup = dup || o.name == c.name;
    if (!dup) out.push_back(c);
  }
}
// ASCE 7-16 section 2.3.1 basic combinations for strength design (load cases D, L, Lr, S, R, W, E), expanded over the "or" groups.
inline std::vector<LoadCombo> Asce7Lrfd(const std::set<std::string>& avail) {
  auto one = [](const char* c, double f) { return ComboSlot{true, {LoadTerm{c, f}}}; };
  auto opt = [](const char* c, double f) { return ComboSlot{false, {LoadTerm{c, f}}}; };
  auto roof = [](double f, bool required) { return ComboSlot{required, {LoadTerm{"Lr", f}, LoadTerm{"S", f}, LoadTerm{"R", f}}}; };
  std::vector<LoadCombo> out;
  ExpandSlots({one("D", 1.4)}, avail, out);
  ExpandSlots({one("D", 1.2), one("L", 1.6), roof(0.5, false)}, avail, out);
  ExpandSlots({one("D", 1.2), roof(1.6, true), ComboSlot{false, {LoadTerm{"L", 1.0}, LoadTerm{"W", 0.5}}}}, avail, out);
  ExpandSlots({one("D", 1.2), one("W", 1.0), opt("L", 1.0), roof(0.5, false)}, avail, out);
  ExpandSlots({one("D", 1.2), one("E", 1.0), opt("L", 1.0), opt("S", 0.2)}, avail, out);
  ExpandSlots({one("D", 0.9), one("W", 1.0)}, avail, out);
  ExpandSlots({one("D", 0.9), one("E", 1.0)}, avail, out);
  return out;
}
// EN 1990 expression 6.10 for persistent situations: sum gamma_G G + gamma_Q1 Q1 + sum gamma_Q,i psi0,i Q_i, one combination per
// choice of the leading variable action.
struct VariableAction {
  std::string load_case;
  double psi0 = 0;
};
inline std::vector<LoadCombo> En1990_610(const std::vector<std::string>& permanent, const std::vector<VariableAction>& variable, double gamma_g = 1.35, double gamma_q = 1.5) {
  std::vector<LoadCombo> out;
  for (size_t lead = 0; lead < std::max<size_t>(1, variable.size()); ++lead) {
    LoadCombo c;
    for (const std::string& g : permanent) c.terms.push_back({g, gamma_g});
    for (size_t i = 0; i < variable.size(); ++i) c.terms.push_back({variable[i].load_case, i == lead ? gamma_q : gamma_q * variable[i].psi0});
    for (size_t i = 0; i < c.terms.size(); ++i) c.name += (i ? "+" : "") + FormatFactor(c.terms[i].factor) + c.terms[i].load_case;
    out.push_back(c);
  }
  return out;
}

// ---- line and area load resultants.
struct LineLoadResultant {
  double total = 0;
  double centroid = 0; // distance from the start
};
// Trapezoidal intensity w1 at the start to w2 at the end over length L.
inline LineLoadResultant TrapezoidLineLoad(double w1, double w2, double length) {
  LineLoadResultant r;
  r.total = (w1 + w2) * length / 2.0;
  r.centroid = std::fabs(w1 + w2) < 1e-300 ? length / 2.0 : length * (w1 + 2.0 * w2) / (3.0 * (w1 + w2));
  return r;
}
enum class AreaLoadBasis { PerSurfaceArea, PerProjectedArea, NormalPerSurfaceArea };
struct AreaLoadForce {
  double vertical = 0, horizontal = 0; // magnitudes (horizontal acts along the direction of fall of the surface)
};
// Uniform area load w on a plane of the given slope whose plan outline is `plan`:
//  PerSurfaceArea (a gravity load per area of surface): F_v = w A_plan sqrt(1+s^2).
//  PerProjectedArea (e.g. a snow load per plan area): F_v = w A_plan.
//  NormalPerSurfaceArea (pressure normal to the surface): F_v = w A_surf cos(theta) = w A_plan, F_h = w A_plan s.
inline AreaLoadForce AreaLoadOnPlane(const std::vector<P2>& plan, double slope, double w, AreaLoadBasis basis) {
  const double ap = PolygonArea(plan), k = std::sqrt(1.0 + slope * slope);
  AreaLoadForce f;
  if (basis == AreaLoadBasis::PerSurfaceArea) f.vertical = w * ap * k;
  else if (basis == AreaLoadBasis::PerProjectedArea) f.vertical = w * ap;
  else {
    f.vertical = w * ap;
    f.horizontal = w * ap * slope;
  }
  return f;
}

} // namespace dino8::kernel::bim3
