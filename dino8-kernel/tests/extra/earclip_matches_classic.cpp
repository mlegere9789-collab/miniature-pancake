// EarClipTriangulate() (detail/polygon2d.h) caches each vertex's "is an ear" verdict and recomputes it only where a clip can have changed it, so
// the 64- and 100-hole plates (outlines of 3000+ vertices) triangulate in seconds instead of minutes. The ears it clips must be exactly the
// ones the classic loop clips - the first ear of the current order, every time - so the triangle lists are compared index by index against a
// verbatim copy of that loop on polygons with reflex vertices, collinear runs, duplicated vertices (the coincident ends of a keyhole bridge) and
// both windings.
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

#include "dino8/kernel/detail/polygon2d.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Point2d;
using dino8::kernel::detail::Cross2d;
using dino8::kernel::detail::PointInTriangle;
using dino8::kernel::detail::SignedArea;

std::vector<std::array<int, 3>> ClassicEarClip(const std::vector<Point2d>& poly) {
  std::vector<std::array<int, 3>> triangles;
  std::vector<int> order(poly.size());
  for (size_t i = 0; i < poly.size(); ++i) {
    order[i] = static_cast<int>(i);
  }
  if (SignedArea(poly) < 0.0) {
    std::reverse(order.begin(), order.end());
  }

  while (order.size() > 3) {
    bool ear_found = false;
    const size_t n = order.size();
    for (size_t i = 0; i < n; ++i) {
      const int ia = order[(i + n - 1) % n];
      const int ib = order[i];
      const int ic = order[(i + 1) % n];
      const Point2d& a = poly[static_cast<size_t>(ia)];
      const Point2d& b = poly[static_cast<size_t>(ib)];
      const Point2d& c = poly[static_cast<size_t>(ic)];
      const double turn = Cross2d(Point2d(b.x - a.x, b.y - a.y), Point2d(c.x - b.x, c.y - b.y));
      if (turn <= 1e-15) {
        continue;  // reflex or degenerate vertex - not a valid ear tip
      }
      bool contains_other = false;
      for (size_t k = 0; k < n; ++k) {
        const int idx = order[k];
        if (idx == ia || idx == ib || idx == ic) {
          continue;
        }
        if (PointInTriangle(poly[static_cast<size_t>(idx)], a, b, c)) {
          contains_other = true;
          break;
        }
      }
      if (contains_other) {
        continue;
      }
      triangles.push_back({ia, ib, ic});
      order.erase(order.begin() + static_cast<long>(i));
      ear_found = true;
      break;
    }
    if (!ear_found) {
      break;
    }
  }
  if (order.size() == 3) {
    // A real, checked-directly discovery (not merely a defensive extra):
    // when the input polygon carries a genuinely COLLINEAR vertex (turn
    // exactly/near 0 relative to its own neighbors, always failing the
    // `turn <= 1e-15` ear-tip test above no matter how the OTHER vertices
    // around it get stripped away), that vertex can never be clipped as
    // an ordinary ear and is instead left stranded here, among the "final
    // 3" this fallback always used to emit UNCONDITIONALLY - producing a
    // spurious, ZERO-AREA triangle (three collinear points) rather than
    // correctly recognizing that a collinear vertex contributes no extra
    // area on its own. Confirmed directly by the parallel-axis cylinder
    // Difference increment's own lens-cap end faces (boolean.cpp's own
    // BuildLensEndCap): its own `chord_mid` vertex - deliberately placed
    // EXACTLY on the straight line between the arc's own two endpoints
    // (needed there so Brep::FromMixedFaces() sees two sub-edges instead
    // of one, not a shape requirement) - is precisely this case: every
    // OTHER (convex, arc) vertex gets clipped away first, stranding
    // [arc_endpoint_a, arc_endpoint_b, chord_mid] as the final 3, an
    // exactly-degenerate triangle whose own "diagonal" edge
    // (arc_endpoint_a <-> arc_endpoint_b) then corrupts downstream
    // mesh-edge accounting - a genuinely NEW failure mode this codebase's
    // own generic triangulator had never been asked to handle before,
    // since every PRIOR consumer only ever fed truly non-degenerate
    // polygons. Skipping a degenerate final triangle here is safe and
    // lossless: its own area is (by definition) ~0, so omitting it drops
    // no real polygon area, and the polygon's own BOUNDARY edges (built
    // separately, from the ORIGINAL loop points, not from this function's
    // own triangle list) are completely unaffected either way - this
    // function's own callers only ever use its triangles for AREA/VOLUME/
    // MESH purposes, never to reconstruct the boundary.
    const double area2 = std::fabs(Cross2d(Point2d(poly[static_cast<size_t>(order[1])].x - poly[static_cast<size_t>(order[0])].x,
                                                     poly[static_cast<size_t>(order[1])].y - poly[static_cast<size_t>(order[0])].y),
                                            Point2d(poly[static_cast<size_t>(order[2])].x - poly[static_cast<size_t>(order[0])].x,
                                                     poly[static_cast<size_t>(order[2])].y - poly[static_cast<size_t>(order[0])].y)));
    if (area2 > 1e-15) {
      triangles.push_back({order[0], order[1], order[2]});
    }
  }
  return triangles;
}


}  // namespace

D8_EXTRA_TEST(BooleanEarClipCachedVerdictsMatchTheClassicLoop) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> radius(0.3, 1.0);
  int differ = 0, total = 0;
  for (int t = 0; t < 1500; ++t) {
    const int n = 4 + static_cast<int>(rng() % 60);
    std::vector<Point2d> p;
    for (int i = 0; i < n; ++i) {
      const double a = 2.0 * 3.14159265358979323846 * i / n;
      const double r = (t % 3 == 0) ? radius(rng) : ((i % 2) ? 1.0 : 0.5 + 0.4 * radius(rng));
      p.push_back(Point2d(r * std::cos(a), r * std::sin(a)));
    }
    if (t % 5 == 0) {
      for (int k = 0; k < 3; ++k) {  // collinear vertices
        const size_t i = rng() % p.size(), j = (i + 1) % p.size();
        p.insert(p.begin() + static_cast<long>(i) + 1, Point2d(0.5 * (p[i].x + p[j].x), 0.5 * (p[i].y + p[j].y)));
      }
    }
    if (t % 7 == 0) std::reverse(p.begin(), p.end());
    if (t % 11 == 0) {  // a duplicated vertex
      const size_t i = rng() % p.size();
      p.insert(p.begin() + static_cast<long>(i), p[i]);
    }
    ++total;
    if (ClassicEarClip(p) != dino8::kernel::detail::EarClipTriangulate(p)) ++differ;
  }
  for (const int n : {400, 1200}) {  // many convex tips each blocked or not by a neighbour's spike
    std::vector<Point2d> p;
    for (int i = 0; i < n; ++i) {
      const double a = 2.0 * 3.14159265358979323846 * i / n;
      const double r = (i % 2) ? 1.0 : 0.93 + 0.02 * ((i / 2) % 5);
      p.push_back(Point2d(r * std::cos(a), r * std::sin(a)));
    }
    ++total;
    if (ClassicEarClip(p) != dino8::kernel::detail::EarClipTriangulate(p)) ++differ;
  }
  d8test::Check(differ == 0, "every polygon triangulates to the same triangle list as the classic ear-clipping loop");
  d8test::Check(total > 1000, "fixture: more than a thousand polygons compared");
}
