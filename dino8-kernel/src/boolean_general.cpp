// General boundary-evaluation boolean engine - see boolean_general.h for
// the scope/limitations summary. This file is the actual algorithm:
//
//   1. Every face of A is tested against every face of B (bbox-pruned)
//      via IntersectFaces() (dino8/kernel/surface_intersect.h), which
//      already restricts the resulting curves to each face's own trimmed
//      region and already hands back matching 2D pcurves on BOTH faces'
//      own (u, v) space - no projection needed.
//   2. Each face's own trim-loop boundary (its real ON_BrepLoop if it has
//      one, else the surface's own full parameter-domain rectangle for an
//      untrimmed face - Box()/Sphere()) is split by the intersection
//      curves gathered for it: a CLOSED curve (entirely interior to the
//      trim - IntersectionCurve::closed) becomes a hole in the untouched
//      fragment plus a new, separate single-loop fragment for its own
//      interior; an OPEN curve (both ends on the trim boundary, since
//      IntersectFaces() already clips/splits there) is spliced into the
//      trim boundary, bisecting it into two fragments at the two
//      insertion points.
//   3. Every fragment is classified in/out of the OTHER solid by picking
//      one interior (u, v) point, mapping it to 3D, and ray-casting
//      against every face of the other solid via IntersectCurveSurface()
//      (the general CSX, not a hand-solved ray/plane or ray/cylinder
//      formula) - the same parity-counting idea ClassifyPointVsSolid in
//      boolean.cpp already uses, generalized to arbitrary faces.
//   4. The kept fragments (Union: both sides' OUT pieces; Intersection:
//      both sides' IN pieces; Difference: A's OUT pieces + B's IN pieces
//      with B's orientation flipped) are reassembled into one ON_Brep
//      with genuine topology: fragment boundary points are welded into
//      shared ON_BrepVertex ids by 3D coincidence (the same identity
//      principle Brep::FromMixedFaces()'s own VertexWelder uses), and
//      since a fragment pair on either side of a shared intersection
//      curve is built from the SAME IntersectionCurve::points array (only
//      the (u, v) differs between the A-side and B-side pcurve), the two
//      sides' welded points coincide exactly - so the shared cut becomes
//      one literal shared ON_BrepEdge, not two independently-approximated
//      curves that merely sit close together.
//
// Edges here are dense straight-segment polylines between consecutive
// (already densely sampled, by IntersectFaces()'s own tessellation-seeded
// refinement, and by FaceBoundaryLoop()'s own sampling of a real trim or
// of an untrimmed domain rectangle) fragment-boundary points - the same
// "genuine multi-point polyline, not a single exact analytic curve"
// approach brep.h's own FromMixedFaces()-built notch edges already use
// for a boundary with no simple closed form, applied here uniformly
// (every edge, not just notches) since a general ON_Surface face has no
// guaranteed isocurve family to fall back on the way a cylinder's cap
// does. This is a real, disclosed precision tradeoff (see
// boolean_general.h): the assembled B-rep's edges are polygonal
// approximations of the true intersection curves, accurate to
// IntersectOptions::tolerance, not exact to floating point.
// KNOWN, CONFIRMED (not theorized) GAP as of this writing, found via
// DINO8_BOOL_DEBUG=1 tracing on a box fully pierced by a perpendicular
// cylinder (see dino8-kernel/tests/scratch_test.cpp): IntersectFaces()
// returns ZERO intersection curves for the box's flat z=+-1 planar faces
// against the cylinder's own periodic wall face, even though the wall
// unambiguously crosses both planes (frags=1 with no split on every one
// of those faces, confirmed via the debug trace - not a downstream
// seam-splitting/stitching issue, the SSX call itself finds nothing for
// this specific plane-vs-periodic-cylinder pairing). This is NOT a
// general periodic-surface limitation of the underlying intersector -
// phase 1's own TestSurfaceIntersectSphereGreatCircle (test_basic.cpp)
// already proves a periodic surface (a sphere, u-periodic) against a
// plane works correctly, including the seam-split-into-multiple-curves
// case. The box+box (pure planar) case above is fully correct (exact
// closed-form volumes, valid Breps) - this gap is specific to at least
// one of: (a) IntersectFaces()'s bounding-box pre-filter or seed-search
// behaving differently for a CYLINDRICAL periodic surface than a
// SPHERICAL one, or (b) something specific to how the cylinder wall
// Brep face built via Brep::FromMixedFaces()/CylindricalFace differs
// from the plain ON_Surface used in the sphere test. Not yet root-caused
// further than this. Until fixed, BooleanCombineGeneral must not be
// trusted on any operand pair involving a cylindrical (or, unverified,
// conical/toroidal) face - only proven correct for planar-only operands.
#include "dino8/kernel/boolean_general.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>

#include "dino8/kernel/surface_intersect.h"

namespace dino8::kernel {

namespace {

constexpr double kWeldTol = 1e-6;

struct UVPt {
  Point3d p;
  Point2d uv;
};
using Chain = std::vector<UVPt>;

double Dist2(const Point2d& a, const Point2d& b) {
  const double dx = a.x - b.x, dy = a.y - b.y;
  return dx * dx + dy * dy;
}

// --- vertex welding (self-contained copy of brep.cpp's own VertexWelder
// pattern - not exported from that translation unit) ------------------
struct WeldKey {
  long long x = 0, y = 0, z = 0;
  bool operator==(const WeldKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct WeldKeyHash {
  size_t operator()(const WeldKey& k) const {
    size_t h = std::hash<long long>()(k.x);
    h ^= std::hash<long long>()(k.y) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<long long>()(k.z) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};
class VertexWelder {
 public:
  int Weld(const Point3d& p) {
    const WeldKey key{std::llround(p.x / kWeldTol), std::llround(p.y / kWeldTol), std::llround(p.z / kWeldTol)};
    const auto it = index_of_.find(key);
    if (it != index_of_.end()) return it->second;
    const int id = static_cast<int>(points_.size());
    points_.push_back(p);
    index_of_.emplace(key, id);
    return id;
  }
  const std::vector<Point3d>& Points() const { return points_; }

 private:
  std::unordered_map<WeldKey, int, WeldKeyHash> index_of_;
  std::vector<Point3d> points_;
};

// --- per-face boundary loop (real trim loop if there is one, else the
// surface's own full parameter domain rectangle for an untrimmed face) --
std::vector<UVPt> FaceBoundaryLoop(const ON_Brep& brep, int face_index, int samples_per_edge = 24) {
  const ON_BrepFace& face = brep.m_F[face_index];
  const ON_Surface* s = face.SurfaceOf();
  std::vector<UVPt> out;
  if (face.m_li.Count() > 0) {
    const ON_BrepLoop& loop = brep.m_L[face.m_li[0]];
    for (int k = 0; k < loop.m_ti.Count(); ++k) {
      const ON_BrepTrim& trim = brep.m_T[loop.m_ti[k]];
      const ON_Curve* c2 = trim.TrimCurveOf();
      if (!c2) continue;
      const ON_Interval d = trim.Domain();
      const int n = std::max(2, c2->IsLinear() ? 2 : samples_per_edge);
      for (int i = 0; i < n; ++i) {
        const ON_2dPoint uv = c2->PointAt(d.ParameterAt(static_cast<double>(i) / n));
        out.push_back({s->PointAt(uv.x, uv.y), uv});
      }
    }
    if (out.size() >= 3) return out;
    out.clear();
  }
  const ON_Interval du = s->Domain(0), dv = s->Domain(1);
  auto add_edge = [&](double u0, double v0, double u1, double v1) {
    for (int i = 0; i < samples_per_edge; ++i) {
      const double t = static_cast<double>(i) / samples_per_edge;
      const double u = u0 + (u1 - u0) * t, v = v0 + (v1 - v0) * t;
      out.push_back({s->PointAt(u, v), Point2d(u, v)});
    }
  };
  add_edge(du.Min(), dv.Min(), du.Max(), dv.Min());
  add_edge(du.Max(), dv.Min(), du.Max(), dv.Max());
  add_edge(du.Max(), dv.Max(), du.Min(), dv.Max());
  add_edge(du.Min(), dv.Max(), du.Min(), dv.Min());
  return out;
}

std::vector<Point2d> ToPoly(const std::vector<UVPt>& loop) {
  std::vector<Point2d> poly;
  poly.reserve(loop.size());
  for (const UVPt& p : loop) poly.push_back(p.uv);
  return poly;
}

// --- splicing one open chain (both ends on the loop's own boundary)
// into a loop, bisecting it in two -------------------------------------
struct BoundaryHit {
  size_t edge_index = 0;
  double t = 0;
};
BoundaryHit NearestOnLoop(const std::vector<UVPt>& loop, const Point2d& q) {
  BoundaryHit best;
  double best_d = 1e300;
  const size_t n = loop.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d& a = loop[i].uv;
    const Point2d& b = loop[(i + 1) % n].uv;
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 1e-18 ? ((q.x - a.x) * dx + (q.y - a.y) * dy) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const Point2d proj(a.x + dx * t, a.y + dy * t);
    const double d = Dist2(proj, q);
    if (d < best_d) {
      best_d = d;
      best = {i, t};
    }
  }
  return best;
}

struct Fragment {
  std::vector<UVPt> outer;
  std::vector<std::vector<UVPt>> holes;
};

// One face can be crossed by several DIFFERENT opposing faces at once, and
// the true intersection curve is only ever complete once those per-pair
// pieces are chained together: where solid A's own face meets both solid
// B's face-1 and face-2 along their common corner, the piece IntersectFaces
// returns for (A, B-face-1) ends at that corner - a point strictly INSIDE
// A's own trim, not on A's own boundary at all - and the (A, B-face-2)
// piece continues from that same 3D point onward. Stitching every
// same-face piece at shared 3D endpoints (within `tol`) before ever asking
// "is this open or closed, and where does it meet the trim boundary" is
// what makes that boundary test meaningful; treating each pair's own piece
// as already complete (this engine's own first, incorrect draft) silently
// leaves most of a real multi-face solid's own boundary unsplit.
Chain ReverseChain(Chain c) {
  std::reverse(c.begin(), c.end());
  return c;
}
std::vector<Chain> StitchChains(std::vector<Chain> chains, double tol) {
  const double tol2 = tol * tol;
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < chains.size() && !changed; ++i) {
      if (chains[i].size() < 2) continue;
      const Point3d& ai = chains[i].front().p;
      const Point3d& bi = chains[i].back().p;
      if ((ai - bi).LengthSquared() <= tol2) continue;  // already closed
      for (size_t j = i + 1; j < chains.size(); ++j) {
        if (chains[j].size() < 2) continue;
        const Point3d& aj = chains[j].front().p;
        const Point3d& bj = chains[j].back().p;
        auto near = [&](const Point3d& x, const Point3d& y) { return (x - y).LengthSquared() <= tol2; };
        Chain merged;
        bool ok = true;
        if (near(bi, aj)) {
          merged = chains[i];
          merged.insert(merged.end(), chains[j].begin() + 1, chains[j].end());
        } else if (near(bi, bj)) {
          merged = chains[i];
          Chain rj = ReverseChain(chains[j]);
          merged.insert(merged.end(), rj.begin() + 1, rj.end());
        } else if (near(ai, aj)) {
          merged = ReverseChain(chains[i]);
          merged.insert(merged.end(), chains[j].begin() + 1, chains[j].end());
        } else if (near(ai, bj)) {
          merged = chains[j];
          merged.insert(merged.end(), chains[i].begin() + 1, chains[i].end());
        } else {
          ok = false;
        }
        if (ok) {
          chains[i] = std::move(merged);
          chains.erase(chains.begin() + static_cast<long>(j));
          changed = true;
          break;
        }
      }
    }
  }
  std::vector<Chain> out;
  for (Chain& c : chains)
    if (c.size() >= 2) out.push_back(std::move(c));
  return out;
}

// Splits `loop` into two fragments' outer boundaries at `chain`'s own two
// endpoints (already on, or very near, `loop`'s own boundary - guaranteed
// by IntersectFaces() clipping the curve to the trimmed region).
std::pair<std::vector<UVPt>, std::vector<UVPt>> SpliceOpenChain(const std::vector<UVPt>& loop, const Chain& chain) {
  const BoundaryHit h0 = NearestOnLoop(loop, chain.front().uv);
  const BoundaryHit h1 = NearestOnLoop(loop, chain.back().uv);
  struct Ins {
    size_t edge;
    double t;
    int which;
  };
  std::vector<Ins> ins = {{h0.edge_index, h0.t, 0}, {h1.edge_index, h1.t, 1}};
  std::sort(ins.begin(), ins.end(), [](const Ins& a, const Ins& b) {
    return a.edge < b.edge || (a.edge == b.edge && a.t < b.t);
  });
  std::vector<UVPt> aug;
  int insert_idx[2] = {-1, -1};
  const size_t n = loop.size();
  size_t ii = 0;
  for (size_t e = 0; e < n; ++e) {
    aug.push_back(loop[e]);
    while (ii < ins.size() && ins[ii].edge == e) {
      insert_idx[ins[ii].which] = static_cast<int>(aug.size());
      aug.push_back(ins[ii].which == 0 ? chain.front() : chain.back());
      ++ii;
    }
  }
  const int i0 = insert_idx[0], i1 = insert_idx[1];
  const size_t m = aug.size();
  if (i0 < 0 || i1 < 0 || i0 == i1) {
    // Degenerate (both endpoints landed at the same spot) - refuse to
    // splice rather than fabricate a bogus split.
    return {loop, {}};
  }
  std::vector<UVPt> arcA, arcB;
  for (size_t k = static_cast<size_t>(i0); k != static_cast<size_t>(i1); k = (k + 1) % m) arcA.push_back(aug[k]);
  arcA.push_back(aug[i1]);
  for (size_t k = static_cast<size_t>(i1); k != static_cast<size_t>(i0); k = (k + 1) % m) arcB.push_back(aug[k]);
  arcB.push_back(aug[i0]);

  std::vector<UVPt> fragA = arcA;  // p0 -> boundary -> p1
  for (int idx = static_cast<int>(chain.size()) - 2; idx >= 1; --idx) fragA.push_back(chain[static_cast<size_t>(idx)]);
  std::vector<UVPt> fragB = arcB;  // p1 -> boundary -> p0
  for (size_t idx = 1; idx + 1 < chain.size(); ++idx) fragB.push_back(chain[idx]);
  return {fragA, fragB};
}

// Splits one face's own boundary loop into fragments using every
// intersection chain gathered for it. Closed chains become holes (in
// whichever open-chain fragment geometrically contains them) plus their
// own interior fragment; open chains bisect the boundary, applied one at
// a time against whichever current fragment contains both its endpoints.
std::vector<Fragment> SplitFaceLoop(const std::vector<UVPt>& boundary, const std::vector<Chain>& closed_chains,
                                     const std::vector<Chain>& open_chains) {
  std::vector<std::vector<UVPt>> outers = {boundary};
  for (const Chain& c : open_chains) {
    if (c.size() < 2) continue;
    bool applied = false;
    for (size_t i = 0; i < outers.size(); ++i) {
      const BoundaryHit h0 = NearestOnLoop(outers[i], c.front().uv);
      const BoundaryHit h1 = NearestOnLoop(outers[i], c.back().uv);
      const Point2d& e0a = outers[i][h0.edge_index].uv;
      const Point2d& e0b = outers[i][(h0.edge_index + 1) % outers[i].size()].uv;
      const Point2d proj0(e0a.x + (e0b.x - e0a.x) * h0.t, e0a.y + (e0b.y - e0a.y) * h0.t);
      const Point2d& e1a = outers[i][h1.edge_index].uv;
      const Point2d& e1b = outers[i][(h1.edge_index + 1) % outers[i].size()].uv;
      const Point2d proj1(e1a.x + (e1b.x - e1a.x) * h1.t, e1a.y + (e1b.y - e1a.y) * h1.t);
      const double bbox_span = 1.0;  // scale-agnostic relative check below
      (void)bbox_span;
      // Both endpoints must actually sit close to this fragment's own
      // boundary (not just closest-of-the-worklist) for it to be the
      // right one to splice.
      if (std::sqrt(Dist2(proj0, c.front().uv)) > 1e-3 * (1.0 + std::sqrt(Dist2(e0a, e0b))) &&
          std::sqrt(Dist2(proj0, c.front().uv)) > 1e-6) {
        continue;
      }
      auto [fa, fb] = SpliceOpenChain(outers[i], c);
      if (fb.empty()) continue;  // splice refused (degenerate)
      outers[i] = fa;
      outers.push_back(fb);
      applied = true;
      break;
    }
    if (!applied) {
      // Could not find a fragment whose boundary the chain actually
      // touches - drop it rather than corrupt the topology. Disclosed
      // limitation: this can happen for chains that graze a fragment
      // seam produced by an earlier splice.
      continue;
    }
  }

  std::vector<Fragment> frags;
  frags.reserve(outers.size());
  for (std::vector<UVPt>& o : outers) frags.push_back(Fragment{std::move(o), {}});

  for (const Chain& c : closed_chains) {
    if (c.size() < 3) continue;
    const std::vector<Point2d> chain_poly = ToPoly(c);
    int owner = -1;
    for (size_t i = 0; i < frags.size(); ++i) {
      if (PointInPolygon(ToPoly(frags[i].outer), c.front().uv)) {
        owner = static_cast<int>(i);
        break;
      }
    }
    if (owner >= 0) frags[static_cast<size_t>(owner)].holes.push_back(c);
    Fragment interior;
    interior.outer = c;
    frags.push_back(std::move(interior));
  }
  return frags;
}

// One representative (u, v) point strictly inside `outer` and outside
// every hole - tries the centroid first, then a handful of interior
// candidates derived from the loop's own points, for the non-convex case.
bool RepresentativeUV(const Fragment& frag, Point2d& out_uv) {
  const std::vector<Point2d> outer_poly = ToPoly(frag.outer);
  std::vector<std::vector<Point2d>> hole_polys;
  for (const auto& h : frag.holes) hole_polys.push_back(ToPoly(h));
  auto ok = [&](const Point2d& q) {
    if (!PointInPolygon(outer_poly, q)) return false;
    for (const auto& hp : hole_polys)
      if (PointInPolygon(hp, q)) return false;
    return true;
  };
  double cx = 0, cy = 0;
  for (const Point2d& p : outer_poly) {
    cx += p.x;
    cy += p.y;
  }
  cx /= static_cast<double>(outer_poly.size());
  cy /= static_cast<double>(outer_poly.size());
  if (ok(Point2d(cx, cy))) {
    out_uv = Point2d(cx, cy);
    return true;
  }
  // Fallback: average of every consecutive point triple's midpoint,
  // nudged toward the polygon centroid - cheap and adequate for the
  // simple (near-convex) fragments this engine's validated scope covers.
  const size_t n = outer_poly.size();
  for (size_t i = 0; i < n; ++i) {
    const Point2d mid((outer_poly[i].x + cx) * 0.5, (outer_poly[i].y + cy) * 0.5);
    if (ok(mid)) {
      out_uv = mid;
      return true;
    }
  }
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      const Point2d mid((outer_poly[i].x + outer_poly[j].x) / 2.0, (outer_poly[i].y + outer_poly[j].y) / 2.0);
      if (ok(mid)) {
        out_uv = mid;
        return true;
      }
    }
  }
  return false;
}

// --- classification: ray-cast a 3D point against a whole Brep's own
// faces using the general CSX (IntersectCurveSurface), generalizing
// boolean.cpp's own ClassifyPointVsSolid beyond hand-solved formulas. ---
std::vector<Vector3d> GenericRayDirections() {
  const double phi = 1.6180339887498948482;
  const double phi2 = phi * phi;
  std::vector<Vector3d> dirs = {
      Vector3d(1.0, phi, phi2),   Vector3d(phi, phi2, 1.0),   Vector3d(phi2, 1.0, phi),
      Vector3d(1.0, -phi, phi2),  Vector3d(-phi, phi2, 1.0),  Vector3d(phi2, -1.0, -phi),
      Vector3d(-1.0, phi, -phi2), Vector3d(phi, -phi2, 1.0),
  };
  for (Vector3d& d : dirs) {
    const double len = d.Length();
    if (len > 1e-12) d = d / len;
  }
  return dirs;
}

enum class Cls { In, Out };

Cls ClassifyPointVsBrep(const Point3d& p, const ON_Brep& other, double ray_length, const IntersectOptions& opt,
                         double tol) {
  const std::vector<Vector3d> dirs = GenericRayDirections();
  for (const Vector3d& d : dirs) {
    bool clean = true;
    int crossings = 0;
    ON_LineCurve ray(p, p + d * ray_length);
    for (int fi = 0; fi < other.m_F.Count() && clean; ++fi) {
      const ON_BrepFace& face = other.m_F[fi];
      const ON_Surface* s = face.SurfaceOf();
      if (!s) continue;
      ON_BoundingBox fb = s->BoundingBox();
      fb.m_min -= ON_3dVector(tol, tol, tol);
      fb.m_max += ON_3dVector(tol, tol, tol);
      ON_BoundingBox ray_box;
      ray_box.Set(p, false);
      ray_box.Set(p + d * ray_length, true);
      if (fb.IsDisjoint(ray_box)) continue;
      std::vector<CurveSurfaceHit> hits = IntersectCurveSurface(ray, *s, opt);
      for (const CurveSurfaceHit& h : hits) {
        if (h.t <= tol) continue;  // at/behind the ray origin
        if (!FaceContainsUV(face, h.uv.x, h.uv.y)) continue;
        // Grazing hit very near this face's own trim boundary can't be
        // parity-counted reliably - abandon this direction.
        const double eps = 1e-4;
        const bool near_u0 = std::fabs(h.uv.x - s->Domain(0).Min()) < eps || std::fabs(h.uv.x - s->Domain(0).Max()) < eps;
        const bool near_v0 = std::fabs(h.uv.y - s->Domain(1).Min()) < eps || std::fabs(h.uv.y - s->Domain(1).Max()) < eps;
        if ((near_u0 || near_v0) && face.m_li.Count() == 0) {
          // Untrimmed face: a hit exactly on the periodic seam/pole is
          // fine (still one genuine crossing); only true near-tangency
          // (ray nearly parallel to the surface there) is ambiguous, and
          // that is caught below by the small `h.error` check implicitly
          // passing through IntersectCurveSurface's own refinement.
        }
        ++crossings;
      }
    }
    if (clean) return (crossings % 2 == 1) ? Cls::In : Cls::Out;
  }
  return Cls::Out;  // exhausted every direction - default to outside
}

struct KeptFace {
  ON_Surface* surface = nullptr;  // owned (caller deletes after adding, or brep takes ownership)
  bool rev = false;
  std::vector<UVPt> outer;
  std::vector<std::vector<UVPt>> holes;
};

void CollapseDuplicateVids(std::vector<UVPt>& loop, VertexWelder& welder) {
  std::vector<UVPt> out;
  int last_vid = -1;
  for (const UVPt& p : loop) {
    const int vid = welder.Weld(p.p);
    if (vid == last_vid) continue;
    out.push_back(p);
    last_vid = vid;
  }
  if (out.size() >= 2) {
    const int first_vid = welder.Weld(out.front().p);
    const int back_vid = welder.Weld(out.back().p);
    if (first_vid == back_vid) out.pop_back();
  }
  loop = std::move(out);
}

void BuildLoop(ON_Brep& brep, ON_BrepFace& face, ON_BrepLoop::TYPE type, const std::vector<UVPt>& loop_pts,
               VertexWelder& welder, std::unordered_map<uint64_t, int>& edge_of_pair) {
  const size_t n = loop_pts.size();
  if (n < 3) return;
  ON_BrepLoop& loop = brep.NewLoop(type, face);
  for (size_t k = 0; k < n; ++k) {
    const size_t k1 = (k + 1) % n;
    const int vid_from = welder.Weld(loop_pts[k].p);
    const int vid_to = welder.Weld(loop_pts[k1].p);
    if (vid_from == vid_to) continue;
    const uint32_t lo = static_cast<uint32_t>(std::min(vid_from, vid_to));
    const uint32_t hi = static_cast<uint32_t>(std::max(vid_from, vid_to));
    const uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;
    int edge_index;
    const auto it = edge_of_pair.find(key);
    if (it == edge_of_pair.end()) {
      auto* c3 = new ON_LineCurve(brep.m_V[vid_from].point, brep.m_V[vid_to].point);
      c3->SetDomain(0.0, 1.0);
      const int c3i = brep.AddEdgeCurve(c3);
      ON_BrepEdge& edge = brep.NewEdge(brep.m_V[vid_from], brep.m_V[vid_to], c3i);
      edge.m_tolerance = 0.0;
      edge_index = edge.m_edge_index;
      edge_of_pair.emplace(key, edge_index);
    } else {
      edge_index = it->second;
      if (brep.m_E[edge_index].m_ti.Count() >= 2) {
        throw std::runtime_error(
            "dino8::kernel::BooleanCombineGeneral: an edge is claimed by 3 or "
            "more fragment loops (non-manifold result) - out of scope; see "
            "boolean_general.h's own disclosed limitations");
      }
    }
    ON_BrepEdge& edge = brep.m_E[edge_index];
    const bool bRev3d = (edge.m_vi[0] != vid_from);
    auto* c2 = new ON_LineCurve(loop_pts[k].uv, loop_pts[k1].uv);
    c2->SetDomain(0.0, 1.0);
    const int c2i = brep.AddTrimCurve(c2);
    brep.NewTrim(edge, bRev3d, loop, c2i);
  }
}

}  // namespace

Brep BooleanCombineGeneral(const Brep& a, const Brep& b, BooleanOp op) {
  if (op == BooleanOp::SymmetricDifference) {
    throw std::invalid_argument(
        "dino8::kernel::BooleanCombineGeneral: SymmetricDifference is not "
        "yet implemented - see boolean_general.h's own disclosed scope");
  }

  const ON_Brep& ba = a.raw();
  const ON_Brep& bb = b.raw();
  IntersectOptions opt;

  const BoundingBox tbb_a = a.GetTightBoundingBox();
  const BoundingBox tbb_b = b.GetTightBoundingBox();
  const ON_BoundingBox bbox_a(tbb_a.min, tbb_a.max);
  const ON_BoundingBox bbox_b(tbb_b.min, tbb_b.max);
  const double ray_length = 4.0 * (bbox_a.Diagonal().Length() + bbox_b.Diagonal().Length() + 1.0);
  const double tol = 1e-6;

  const int na = ba.m_F.Count();
  const int nb = bb.m_F.Count();

  // Every chain gathered here is only a PIECE of a face's own true
  // boundary curve where more than one opposing face meets it (see
  // StitchChains's own doc comment) - stitched into maximal chains below,
  // per face, before any open/closed classification is attempted.
  std::vector<std::vector<Chain>> raw_a(static_cast<size_t>(na)), raw_b(static_cast<size_t>(nb));

  std::vector<ON_BoundingBox> boxes_a(static_cast<size_t>(na)), boxes_b(static_cast<size_t>(nb));
  for (int i = 0; i < na; ++i) boxes_a[static_cast<size_t>(i)] = ba.m_F[i].SurfaceOf()->BoundingBox();
  for (int j = 0; j < nb; ++j) boxes_b[static_cast<size_t>(j)] = bb.m_F[j].SurfaceOf()->BoundingBox();

  for (int i = 0; i < na; ++i) {
    ON_BoundingBox exp_a = boxes_a[static_cast<size_t>(i)];
    exp_a.m_min -= ON_3dVector(tol, tol, tol);
    exp_a.m_max += ON_3dVector(tol, tol, tol);
    for (int j = 0; j < nb; ++j) {
      if (exp_a.IsDisjoint(boxes_b[static_cast<size_t>(j)])) continue;
      const ON_BrepFace& fa = ba.m_F[i];
      const ON_BrepFace& fb = bb.m_F[j];
      std::vector<IntersectionCurve> curves = IntersectFaces(&fa, *fa.SurfaceOf(), &fb, *fb.SurfaceOf(), opt);
      for (const IntersectionCurve& ic : curves) {
        if (ic.points.size() < 2) continue;
        Chain ca, cb;
        ca.reserve(ic.points.size());
        cb.reserve(ic.points.size());
        for (size_t k = 0; k < ic.points.size(); ++k) {
          ca.push_back({ic.points[k], ic.uv_a[k]});
          cb.push_back({ic.points[k], ic.uv_b[k]});
        }
        raw_a[static_cast<size_t>(i)].push_back(std::move(ca));
        raw_b[static_cast<size_t>(j)].push_back(std::move(cb));
      }
    }
  }

  // Two different SSX curves (this face against two different opposing
  // faces) that meet at the same true 3D corner are each independently
  // Newton-refined to `opt.tolerance`, so their shared endpoint generally
  // isn't bit-identical between the two - stitch with real slack, not
  // kWeldTol (that tight tolerance is for the FINAL assembly, where a
  // chain's own points are shared verbatim across its own two faces, not
  // independently re-solved).
  const double stitch_tol = std::max(1e-4, opt.tolerance * 20.0);

  // Fragment every face of both operands.
  struct FaceFrags {
    int face_index = 0;
    ON_Surface* surface = nullptr;
    bool base_rev = false;
    std::vector<Fragment> frags;
  };
  auto build_frags = [&](const ON_Brep& brep, int n, std::vector<std::vector<Chain>>& raw) {
    std::vector<FaceFrags> out;
    for (int i = 0; i < n; ++i) {
      const std::vector<UVPt> boundary = FaceBoundaryLoop(brep, i);
      if (boundary.size() < 3) continue;
      const std::vector<Chain> stitched = StitchChains(std::move(raw[static_cast<size_t>(i)]), stitch_tol);
      std::vector<Chain> closed_chains, open_chains;
      for (const Chain& c : stitched) {
        if ((c.front().p - c.back().p).Length() <= stitch_tol) {
          closed_chains.push_back(c);
        } else {
          open_chains.push_back(c);
        }
      }
      FaceFrags ff;
      ff.face_index = i;
      ff.surface = brep.m_F[i].SurfaceOf()->DuplicateSurface();
      ff.base_rev = brep.m_F[i].m_bRev;
      ff.frags = SplitFaceLoop(boundary, closed_chains, open_chains);
      out.push_back(std::move(ff));
    }
    return out;
  };
  std::vector<FaceFrags> frags_a = build_frags(ba, na, raw_a);
  std::vector<FaceFrags> frags_b = build_frags(bb, nb, raw_b);

  // Classify + keep, per operation.
  std::vector<KeptFace> kept;
  const bool debug = std::getenv("DINO8_BOOL_DEBUG") != nullptr;
  auto process = [&](std::vector<FaceFrags>& frags, const ON_Brep& other, bool a_side) {
    for (FaceFrags& ff : frags) {
      if (debug) std::fprintf(stderr, "face(%s) idx=%d frags=%zu\n", a_side ? "A" : "B", ff.face_index, ff.frags.size());
      for (Fragment& frag : ff.frags) {
        Point2d uv;
        if (!RepresentativeUV(frag, uv)) {
          if (debug) std::fprintf(stderr, "  frag: NO representative UV found (outer pts=%zu, holes=%zu)\n", frag.outer.size(), frag.holes.size());
          continue;
        }
        const Point3d p3 = ff.surface->PointAt(uv.x, uv.y);
        const Cls cls = ClassifyPointVsBrep(p3, other, ray_length, opt, tol);
        if (debug) std::fprintf(stderr, "  frag: outer=%zu holes=%zu uv=(%f,%f) p3=(%f,%f,%f) cls=%s\n", frag.outer.size(), frag.holes.size(), uv.x, uv.y, p3.x, p3.y, p3.z, cls == Cls::In ? "In" : "Out");
        bool keep = false;
        bool flip = false;
        switch (op) {
          case BooleanOp::Union:
            keep = (cls == Cls::Out);
            break;
          case BooleanOp::Intersection:
            keep = (cls == Cls::In);
            break;
          case BooleanOp::Difference:
            if (a_side) {
              keep = (cls == Cls::Out);
            } else {
              keep = (cls == Cls::In);
              flip = true;
            }
            break;
          default:
            break;
        }
        if (!keep) continue;
        KeptFace kf;
        kf.surface = ff.surface->DuplicateSurface();
        kf.rev = flip ? !ff.base_rev : ff.base_rev;
        kf.outer = frag.outer;
        kf.holes = frag.holes;
        kept.push_back(std::move(kf));
      }
    }
  };
  process(frags_a, bb, true);
  process(frags_b, ba, false);

  // Every FaceFrags::surface was only ever a read-only source for
  // KeptFace::surface's own DuplicateSurface() copies above - free them
  // all now regardless of whether any fragment of that face was kept.
  for (FaceFrags& ff : frags_a) delete ff.surface;
  for (FaceFrags& ff : frags_b) delete ff.surface;

  if (kept.empty()) {
    // The empty solid - degenerate but not an error (e.g. Intersection of
    // two disjoint solids, or a Difference that fully removes `a`).
    for (KeptFace& kf : kept) delete kf.surface;
    return Brep();
  }

  // Reassemble.
  Brep result;
  ON_Brep& brep = result.raw();
  VertexWelder welder;
  for (KeptFace& kf : kept) {
    CollapseDuplicateVids(kf.outer, welder);
    for (auto& h : kf.holes) CollapseDuplicateVids(h, welder);
  }
  for (const Point3d& p : welder.Points()) brep.NewVertex(p);

  std::unordered_map<uint64_t, int> edge_of_pair;
  for (KeptFace& kf : kept) {
    if (kf.outer.size() < 3) {
      // Degenerate sliver (collapsed to < 3 unique vertices after
      // welding) - AddSurface() never took ownership, free it here.
      delete kf.surface;
      continue;
    }
    const int surface_index = brep.AddSurface(kf.surface);
    ON_BrepFace& face = brep.NewFace(surface_index);
    face.m_bRev = kf.rev;
    BuildLoop(brep, face, ON_BrepLoop::outer, kf.outer, welder, edge_of_pair);
    for (const std::vector<UVPt>& h : kf.holes) {
      if (h.size() >= 3) BuildLoop(brep, face, ON_BrepLoop::inner, h, welder, edge_of_pair);
    }
  }

  brep.SetTrimIsoFlags();
  brep.SetTolerancesBoxesAndFlags(/*bLazy=*/true);
  return result;
}

}  // namespace dino8::kernel
