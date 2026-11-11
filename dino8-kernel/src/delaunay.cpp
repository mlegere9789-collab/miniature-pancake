#include "dino8/kernel/delaunay.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace dino8::kernel {

namespace {

using P = Point2d;

double Orient(const P& a, const P& b, const P& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

double LoopArea(const std::vector<P>& l) {
  double a = 0.0;
  for (size_t i = 0; i < l.size(); ++i) {
    const P& p = l[i];
    const P& q = l[(i + 1) % l.size()];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5 * a;
}

// > 0 when d lies strictly inside the circumcircle of the counter-clockwise triangle abc.
double InCircle(const P& a, const P& b, const P& c, const P& d) {
  const double adx = a.x - d.x, ady = a.y - d.y, bdx = b.x - d.x, bdy = b.y - d.y, cdx = c.x - d.x, cdy = c.y - d.y;
  const double ad = adx * adx + ady * ady, bd = bdx * bdx + bdy * bdy, cd = cdx * cdx + cdy * cdy;
  return adx * (bdy * cd - bd * cdy) - ady * (bdx * cd - bd * cdx) + ad * (bdx * cdy - bdy * cdx);
}

bool OnSegment(const P& p, const P& a, const P& b, double tol) {
  const double cross = Orient(a, b, p);
  const double len = std::hypot(b.x - a.x, b.y - a.y);
  if (len <= 0.0 || std::fabs(cross) > tol * len) return false;
  const double dot = (p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y);
  return dot >= -tol * len && dot <= len * len + tol * len;
}

// Closed segments ab and cd share a point.
bool SegmentsMeet(const P& a, const P& b, const P& c, const P& d, double tol) {
  const double o1 = Orient(a, b, c), o2 = Orient(a, b, d), o3 = Orient(c, d, a), o4 = Orient(c, d, b);
  if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0))) return true;
  return OnSegment(c, a, b, tol) || OnSegment(d, a, b, tol) || OnSegment(a, c, d, tol) || OnSegment(b, c, d, tol);
}

double DistanceToSegment(const P& p, const P& a, const P& b) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double l2 = dx * dx + dy * dy;
  double t = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

bool InsideLoop(const P& p, const std::vector<P>& l) {
  bool in = false;
  for (size_t i = 0, j = l.size() - 1; i < l.size(); j = i++) {
    if (((l[i].y > p.y) != (l[j].y > p.y)) && (p.x < (l[j].x - l[i].x) * (p.y - l[i].y) / (l[j].y - l[i].y) + l[i].x)) in = !in;
  }
  return in;
}

}  // namespace

Triangulation2D TriangulateConstrainedDelaunay(const std::vector<Point2d>& outer_in, const std::vector<std::vector<Point2d>>& holes_in,
                                               const std::vector<Point2d>& steiner) {
  // --- validation and orientation: outer counter-clockwise, holes clockwise
  std::vector<std::vector<P>> loops;
  loops.push_back(outer_in);
  for (const auto& h : holes_in) loops.push_back(h);
  double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
  for (const auto& l : loops) {
    if (l.size() < 3) throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: every loop needs at least 3 points");
    for (const P& p : l) {
      if (!std::isfinite(p.x) || !std::isfinite(p.y)) throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: a point is not finite");
      xmin = std::min(xmin, p.x); xmax = std::max(xmax, p.x); ymin = std::min(ymin, p.y); ymax = std::max(ymax, p.y);
    }
  }
  const double scale = std::max({xmax - xmin, ymax - ymin, 1e-300});
  const double tol = 1e-9 * scale;
  for (size_t li = 0; li < loops.size(); ++li) {
    const double a = LoopArea(loops[li]);
    if (std::fabs(a) <= 1e-12 * scale * scale) throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: a loop has zero area");
    if ((li == 0) != (a > 0)) std::reverse(loops[li].begin(), loops[li].end());
  }
  // every edge of every loop against every other edge: no crossing, touching or overlap (adjacent edges of one loop share only their end)
  struct Edge { size_t loop; size_t i; };
  std::vector<Edge> edges;
  for (size_t li = 0; li < loops.size(); ++li) for (size_t i = 0; i < loops[li].size(); ++i) edges.push_back({li, i});
  for (size_t e = 0; e < edges.size(); ++e) {
    const auto& le = loops[edges[e].loop];
    const P &a = le[edges[e].i], &b = le[(edges[e].i + 1) % le.size()];
    if (a.x == b.x && a.y == b.y) throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: a loop has a repeated point");
    for (size_t f = e + 1; f < edges.size(); ++f) {
      const auto& lf = loops[edges[f].loop];
      const size_t i = edges[f].i;
      const P &c = lf[i], &d = lf[(i + 1) % lf.size()];
      const bool adjacent = edges[e].loop == edges[f].loop &&
                            (edges[f].i == (edges[e].i + 1) % le.size() || edges[e].i == (edges[f].i + 1) % le.size());
      if (adjacent) {
        // only the shared end may touch: a fold-back along the same line is an overlap
        const P& shared = (edges[f].i == (edges[e].i + 1) % le.size()) ? b : a;
        const P& far_e = (&shared == &b) ? a : b;
        const P& far_f = (&shared == &b) ? d : c;
        if (std::fabs(Orient(shared, far_e, far_f)) <= tol * std::hypot(far_e.x - shared.x, far_e.y - shared.y) &&
            (far_e.x - shared.x) * (far_f.x - shared.x) + (far_e.y - shared.y) * (far_f.y - shared.y) > 0) {
          throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: two consecutive edges of a loop overlap");
        }
        continue;
      }
      if (SegmentsMeet(a, b, c, d, tol)) {
        throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: loops intersect, touch or self-intersect");
      }
    }
  }
  for (size_t hi = 1; hi < loops.size(); ++hi) {
    if (!InsideLoop(loops[hi][0], loops[0])) throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: a hole is not inside the outer loop");
    for (size_t hj = 1; hj < loops.size(); ++hj) {
      if (hj != hi && InsideLoop(loops[hi][0], loops[hj])) throw std::invalid_argument("dino8::kernel::TriangulateConstrainedDelaunay: a hole lies inside another hole");
    }
  }

  Triangulation2D out;
  std::vector<int> start;  // first index of each loop in out.points
  for (const auto& l : loops) {
    start.push_back(static_cast<int>(out.points.size()));
    for (const P& p : l) out.points.push_back(p);
  }
  out.outer_count = static_cast<int>(loops[0].size());
  for (size_t hi = 1; hi < loops.size(); ++hi) out.hole_sizes.push_back(static_cast<int>(loops[hi].size()));
  const auto& pts = out.points;
  const auto inside_domain = [&](const P& p) {
    if (!InsideLoop(p, loops[0])) return false;
    for (size_t hi = 1; hi < loops.size(); ++hi) if (InsideLoop(p, loops[hi])) return false;
    return true;
  };
  // constraint edges (polygon edges) by index pair
  std::map<std::pair<int, int>, bool> constraint;
  const auto key = [](int a, int b) { return std::make_pair(std::min(a, b), std::max(a, b)); };
  for (size_t li = 0; li < loops.size(); ++li) {
    const int n = static_cast<int>(loops[li].size());
    for (int i = 0; i < n; ++i) constraint[key(start[li] + i, start[li] + (i + 1) % n)] = true;
  }

  // --- bridge the holes into the outer loop (rightmost hole first)
  std::vector<int> poly;
  for (int i = 0; i < out.outer_count; ++i) poly.push_back(i);
  std::vector<size_t> order;
  for (size_t hi = 1; hi < loops.size(); ++hi) order.push_back(hi);
  const auto max_x = [&](size_t hi) {
    double m = -1e300;
    for (const P& p : loops[hi]) m = std::max(m, p.x);
    return m;
  };
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return max_x(a) > max_x(b); });
  std::vector<bool> merged(loops.size(), false);
  for (const size_t hi : order) {
    const int m = static_cast<int>(loops[hi].size());
    int hpos = 0;
    for (int i = 1; i < m; ++i) if (loops[hi][i].x > loops[hi][hpos].x || (loops[hi][i].x == loops[hi][hpos].x && loops[hi][i].y > loops[hi][hpos].y)) hpos = i;
    const int H = start[hi] + hpos;
    std::vector<size_t> cand(poly.size());
    for (size_t k = 0; k < poly.size(); ++k) cand[k] = k;
    std::sort(cand.begin(), cand.end(), [&](size_t a, size_t b) {
      return std::hypot(pts[poly[a]].x - pts[H].x, pts[poly[a]].y - pts[H].y) < std::hypot(pts[poly[b]].x - pts[H].x, pts[poly[b]].y - pts[H].y);
    });
    long chosen = -1;
    for (const size_t k : cand) {
      const int V = poly[k];
      const P mid((pts[V].x + pts[H].x) * 0.5, (pts[V].y + pts[H].y) * 0.5);
      if (!inside_domain(mid)) continue;
      bool clear = true;
      const auto test_edge = [&](int e0, int e1) {
        if (e0 == V || e1 == V || e0 == H || e1 == H) return;
        if (SegmentsMeet(pts[H], pts[V], pts[e0], pts[e1], tol)) clear = false;
      };
      for (size_t q = 0; q < poly.size() && clear; ++q) test_edge(poly[q], poly[(q + 1) % poly.size()]);
      for (size_t hj = 1; hj < loops.size() && clear; ++hj) {
        if (merged[hj] && hj != hi) continue;
        const int mj = static_cast<int>(loops[hj].size());
        for (int q = 0; q < mj && clear; ++q) test_edge(start[hj] + q, start[hj] + (q + 1) % mj);
      }
      if (clear) { chosen = static_cast<long>(k); break; }
    }
    if (chosen < 0) throw std::runtime_error("dino8::kernel::TriangulateConstrainedDelaunay: no visible outer vertex found to bridge a hole");
    std::vector<int> next(poly.begin(), poly.begin() + chosen + 1);
    for (int t = 0; t <= m; ++t) next.push_back(start[hi] + (hpos + t) % m);
    next.push_back(poly[static_cast<size_t>(chosen)]);
    next.insert(next.end(), poly.begin() + chosen + 1, poly.end());
    poly = std::move(next);
    merged[hi] = true;
  }

  // --- ear clipping of the weakly simple polygon `poly`
  const double domain_area = [&] {
    double a = LoopArea(loops[0]);
    for (size_t hi = 1; hi < loops.size(); ++hi) a += LoopArea(loops[hi]);  // hole areas are negative after the reversal
    return a;
  }();
  std::vector<std::array<int, 3>>& tris = out.triangles;
  {
    std::vector<int> ring = poly;
    size_t guard = 0;
    const auto is_ear = [&](size_t i, bool strict) {
      const size_t n = ring.size();
      const int a = ring[(i + n - 1) % n], b = ring[i], c = ring[(i + 1) % n];
      if (Orient(pts[a], pts[b], pts[c]) <= 0.0) return false;
      if (!strict) return true;
      for (size_t k = 0; k < n; ++k) {
        const int q = ring[k];
        if (q == a || q == b || q == c) continue;
        if (Orient(pts[a], pts[b], pts[q]) >= 0.0 && Orient(pts[b], pts[c], pts[q]) >= 0.0 && Orient(pts[c], pts[a], pts[q]) >= 0.0) return false;
      }
      return true;
    };
    while (ring.size() > 3) {
      if (++guard > 4 * poly.size() * poly.size() + 16) throw std::runtime_error("dino8::kernel::TriangulateConstrainedDelaunay: ear clipping did not terminate");
      bool clipped = false;
      for (const bool strict : {true, false}) {
        for (size_t i = 0; i < ring.size() && !clipped; ++i) {
          if (!is_ear(i, strict)) continue;
          const size_t n = ring.size();
          tris.push_back({ring[(i + n - 1) % n], ring[i], ring[(i + 1) % n]});
          ring.erase(ring.begin() + static_cast<long>(i));
          clipped = true;
        }
        if (clipped) break;
      }
      if (!clipped) throw std::runtime_error("dino8::kernel::TriangulateConstrainedDelaunay: the polygon has no ear (not weakly simple)");
    }
    if (Orient(pts[ring[0]], pts[ring[1]], pts[ring[2]]) > 0.0) tris.push_back({ring[0], ring[1], ring[2]});
  }
  {
    double a = 0.0;
    for (const auto& t : tris) a += 0.5 * Orient(pts[t[0]], pts[t[1]], pts[t[2]]);
    if (std::fabs(a - domain_area) > 1e-9 * std::max(domain_area, 1e-300)) {
      throw std::runtime_error("dino8::kernel::TriangulateConstrainedDelaunay: the ear-clipped triangles do not cover the domain");
    }
  }

  // --- Steiner points: split the containing triangle (or the two sharing the edge the point lies on)
  for (const P& s : steiner) {
    if (!std::isfinite(s.x) || !std::isfinite(s.y) || !inside_domain(s)) continue;
    bool near = false;
    for (const P& p : out.points) if (std::hypot(p.x - s.x, p.y - s.y) <= tol) near = true;
    for (size_t li = 0; li < loops.size() && !near; ++li) {
      for (size_t i = 0; i < loops[li].size(); ++i) if (DistanceToSegment(s, loops[li][i], loops[li][(i + 1) % loops[li].size()]) <= tol) near = true;
    }
    if (near) continue;
    const int sid = static_cast<int>(out.points.size());
    bool placed = false;
    const double on_edge = 1e-12 * scale * scale;
    for (size_t t = 0; t < tris.size() && !placed; ++t) {
      const auto tri = tris[t];
      const double o[3] = {Orient(out.points[tri[0]], out.points[tri[1]], s), Orient(out.points[tri[1]], out.points[tri[2]], s), Orient(out.points[tri[2]], out.points[tri[0]], s)};
      if (o[0] < -on_edge || o[1] < -on_edge || o[2] < -on_edge) continue;
      int edge = -1;
      for (int k = 0; k < 3; ++k) if (o[k] <= on_edge) edge = k;
      out.points.push_back(s);
      if (edge < 0) {
        tris[t] = {tri[0], tri[1], sid};
        tris.push_back({tri[1], tri[2], sid});
        tris.push_back({tri[2], tri[0], sid});
      } else {
        const int a = tri[static_cast<size_t>(edge)], b = tri[static_cast<size_t>((edge + 1) % 3)], c = tri[static_cast<size_t>((edge + 2) % 3)];
        // the neighbour across edge (a, b): the triangle holding (b, a)
        long nb = -1;
        for (size_t u = 0; u < tris.size(); ++u) {
          if (u == t) continue;
          for (int k = 0; k < 3; ++k) if (tris[u][static_cast<size_t>(k)] == b && tris[u][static_cast<size_t>((k + 1) % 3)] == a) nb = static_cast<long>(u);
        }
        if (nb < 0) { out.points.pop_back(); break; }  // on the boundary: skipped (cannot happen past the edge-distance test)
        int d = -1;
        for (int k = 0; k < 3; ++k) { const int v = tris[static_cast<size_t>(nb)][static_cast<size_t>(k)]; if (v != a && v != b) d = v; }
        tris[t] = {a, sid, c};
        tris[static_cast<size_t>(nb)] = {b, sid, d};
        tris.push_back({sid, b, c});
        tris.push_back({a, d, sid});
      }
      ++out.steiner_accepted;
      placed = true;
    }
    if (!placed && static_cast<int>(out.points.size()) > sid) out.points.pop_back();
  }

  // --- Lawson flips until every non-constraint edge is locally Delaunay
  const double circle_eps = 1e-10 * scale * scale * scale * scale;
  const size_t max_passes = 64 + 4 * tris.size();
  for (size_t pass = 0; pass < max_passes; ++pass) {
    std::map<std::pair<int, int>, std::vector<std::pair<size_t, int>>> adj;
    for (size_t t = 0; t < tris.size(); ++t) {
      for (int k = 0; k < 3; ++k) adj[key(tris[t][static_cast<size_t>(k)], tris[t][static_cast<size_t>((k + 1) % 3)])].push_back({t, k});
    }
    std::vector<bool> touched(tris.size(), false);
    int flipped = 0;
    for (const auto& [e, uses] : adj) {
      if (uses.size() != 2 || constraint.count(e)) continue;
      const size_t t1 = uses[0].first, t2 = uses[1].first;
      if (touched[t1] || touched[t2]) continue;
      // orient so that t1 holds (a, b) and t2 holds (b, a)
      int ia = uses[0].second;
      const int a = tris[t1][static_cast<size_t>(ia)], b = tris[t1][static_cast<size_t>((ia + 1) % 3)], c = tris[t1][static_cast<size_t>((ia + 2) % 3)];
      int d = -1;
      for (int k = 0; k < 3; ++k) { const int v = tris[t2][static_cast<size_t>(k)]; if (v != a && v != b) d = v; }
      if (InCircle(out.points[a], out.points[b], out.points[c], out.points[d]) <= circle_eps) continue;
      if (Orient(out.points[a], out.points[d], out.points[c]) <= 0.0 || Orient(out.points[d], out.points[b], out.points[c]) <= 0.0) continue;  // not convex
      tris[t1] = {a, d, c};
      tris[t2] = {d, b, c};
      touched[t1] = touched[t2] = true;
      ++flipped;
    }
    out.flips += flipped;
    if (flipped == 0) return out;
  }
  throw std::runtime_error("dino8::kernel::TriangulateConstrainedDelaunay: edge flipping did not converge");
}

Mesh TessellateTrimmedDelaunay(const NurbsSurface& surface, const std::vector<Point2d>& outer_uv, const std::vector<std::vector<Point2d>>& hole_uv,
                               int u_divisions, int v_divisions) {
  if (u_divisions < 1 || v_divisions < 1) throw std::invalid_argument("dino8::kernel::TessellateTrimmedDelaunay: at least 1 division per direction");
  double umin = 1e300, umax = -1e300, vmin = 1e300, vmax = -1e300;
  for (const Point2d& p : outer_uv) { umin = std::min(umin, p.x); umax = std::max(umax, p.x); vmin = std::min(vmin, p.y); vmax = std::max(vmax, p.y); }
  const double du = (umax - umin) / u_divisions, dv = (vmax - vmin) / v_divisions;
  const double keep = 0.3 * std::min(du, dv);
  std::vector<P> steiner;
  const auto near_loop = [&](const P& s, const std::vector<P>& l) {
    for (size_t i = 0; i < l.size(); ++i) if (DistanceToSegment(s, l[i], l[(i + 1) % l.size()]) < keep) return true;
    return false;
  };
  for (int i = 0; i <= u_divisions; ++i) {
    for (int j = 0; j <= v_divisions; ++j) {
      const P s(umin + du * i, vmin + dv * j);
      if (near_loop(s, outer_uv)) continue;
      bool bad = false;
      for (const auto& h : hole_uv) if (near_loop(s, h)) bad = true;
      if (!bad) steiner.push_back(s);
    }
  }
  const Triangulation2D tri = TriangulateConstrainedDelaunay(outer_uv, hole_uv, steiner);
  Mesh mesh;
  ON_Mesh& raw = mesh.raw();
  for (const P& p : tri.points) {
    const Point3d q = surface.PointAt(p.x, p.y);
    raw.m_V.Append(ON_3fPoint(static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)));
  }
  for (const auto& t : tri.triangles) {
    ON_MeshFace f;
    f.vi[0] = t[0];
    f.vi[1] = t[1];
    f.vi[2] = t[2];
    f.vi[3] = t[2];
    raw.m_F.Append(f);
  }
  return mesh;
}

}  // namespace dino8::kernel
