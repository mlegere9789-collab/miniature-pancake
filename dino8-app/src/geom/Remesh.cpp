#include "geom/Remesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

#include "opennurbs.h"

namespace dino8::app::remesh {

namespace {

double Dot(Vector3d a, Vector3d b) { return ON_DotProduct(a, b); }
Vector3d Cross(Vector3d a, Vector3d b) { return ON_CrossProduct(a, b); }
Vector3d Sub(Point3d a, Point3d b) { return a - b; }

Vector3d SafeUnit(Vector3d v) {
  if (!v.Unitize()) return Vector3d(0, 0, 1);
  return v;
}

// ---------------------------------------------------------------------------
// Closest point on a triangle (Ericson, Real-Time Collision Detection),
// returning barycentric weights so the caller can classify which feature
// (face / edge / vertex) the closest point landed on.
// ---------------------------------------------------------------------------
Point3d ClosestOnTriangle(Point3d p, const Point3d t[3], double bary[3]) {
  const Vector3d ab = Sub(t[1], t[0]), ac = Sub(t[2], t[0]), ap = Sub(p, t[0]);
  const double d1 = Dot(ab, ap), d2 = Dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) { bary[0] = 1; bary[1] = 0; bary[2] = 0; return t[0]; }
  const Vector3d bp = Sub(p, t[1]);
  const double d3 = Dot(ab, bp), d4 = Dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) { bary[0] = 0; bary[1] = 1; bary[2] = 0; return t[1]; }
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) {
    const double v = d1 / (d1 - d3);
    bary[0] = 1 - v; bary[1] = v; bary[2] = 0;
    return t[0] + ab * v;
  }
  const Vector3d cp = Sub(p, t[2]);
  const double d5 = Dot(ab, cp), d6 = Dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) { bary[0] = 0; bary[1] = 0; bary[2] = 1; return t[2]; }
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) {
    const double w = d2 / (d2 - d6);
    bary[0] = 1 - w; bary[1] = 0; bary[2] = w;
    return t[0] + ac * w;
  }
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    bary[0] = 0; bary[1] = 1 - w; bary[2] = w;
    return t[1] + Sub(t[2], t[1]) * w;
  }
  const double denom = 1.0 / (va + vb + vc);
  const double v = vb * denom, w = vc * denom;
  bary[0] = 1 - v - w; bary[1] = v; bary[2] = w;
  return t[0] + ab * v + ac * w;
}

Point3d ClosestOnSegment(Point3d p, Point3d a, Point3d b, double* t_out = nullptr) {
  const Vector3d d = Sub(b, a);
  const double l2 = d.LengthSquared();
  double t = l2 > 1e-18 ? Dot(Sub(p, a), d) / l2 : 0.0;
  t = std::max(0.0, std::min(1.0, t));
  if (t_out) *t_out = t;
  return a + d * t;
}

// Pseudonormal at the closest point, given the barycentric weights on triangle prim.
Vector3d PseudoNormalAt(const Primitive& tri, const double bary[3]) {
  const int nz = (bary[0] > 1e-9) + (bary[1] > 1e-9) + (bary[2] > 1e-9);
  if (nz >= 3) return tri.face_n;
  if (nz == 1) {
    for (int i = 0; i < 3; ++i) if (bary[i] > 1e-9) return tri.vert_n[i];
  }
  // Exactly two non-zero weights: on the edge between those two corners.
  if (bary[2] < 1e-9) return tri.edge_n[0];       // edge 0-1
  if (bary[0] < 1e-9) return tri.edge_n[1];       // edge 1-2
  return tri.edge_n[2];                            // edge 2-0
}

}  // namespace

// ---------------------------------------------------------------------------
// Source construction: welds, triangulates, and builds pseudonormals for a
// closed mesh so BuildSdf can sign the field with the pseudonormal test.
// ---------------------------------------------------------------------------
Source SourceFromMesh(const kernel::Mesh& m, double weld_tol) {
  Source s;
  s.has_faces = true;
  kernel::Mesh welded = kernel::Mesh::MergeAndWeld({m}, std::max(weld_tol, 1e-9));
  const ON_Mesh& raw = welded.raw();
  const int nv = raw.VertexCount();
  std::vector<Point3d> verts(static_cast<size_t>(nv));
  for (int i = 0; i < nv; ++i) verts[static_cast<size_t>(i)] = raw.Vertex(i);
  std::vector<std::array<int, 3>> tris;
  tris.reserve(static_cast<size_t>(raw.FaceCount()) * 2);
  for (int i = 0; i < raw.FaceCount(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    if (f.vi[0] < 0 || f.vi[0] >= nv || f.vi[1] < 0 || f.vi[1] >= nv || f.vi[2] < 0 || f.vi[2] >= nv) continue;
    tris.push_back({f.vi[0], f.vi[1], f.vi[2]});
    if (f.vi[3] != f.vi[2] && f.vi[3] >= 0 && f.vi[3] < nv) tris.push_back({f.vi[0], f.vi[2], f.vi[3]});
  }
  // Drop degenerate triangles (zero area / repeated index) up front.
  tris.erase(std::remove_if(tris.begin(), tris.end(), [&](const std::array<int, 3>& t) {
               if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) return true;
               return Cross(Sub(verts[static_cast<size_t>(t[1])], verts[static_cast<size_t>(t[0])]),
                            Sub(verts[static_cast<size_t>(t[2])], verts[static_cast<size_t>(t[0])])).LengthSquared() < 1e-24;
             }),
             tris.end());
  const size_t nt = tris.size();
  std::vector<Vector3d> faceN(nt);
  for (size_t i = 0; i < nt; ++i) {
    const auto& t = tris[i];
    faceN[i] = SafeUnit(Cross(Sub(verts[static_cast<size_t>(t[1])], verts[static_cast<size_t>(t[0])]),
                              Sub(verts[static_cast<size_t>(t[2])], verts[static_cast<size_t>(t[0])])));
  }
  // Undirected-edge -> incident triangle list, for closedness + edge pseudonormals.
  auto edgeKey = [](int a, int b) -> int64_t {
    if (a > b) std::swap(a, b);
    return (static_cast<int64_t>(a) << 32) | static_cast<uint32_t>(b);
  };
  std::unordered_map<int64_t, std::vector<int>> edgeFaces;
  edgeFaces.reserve(nt * 3);
  for (size_t i = 0; i < nt; ++i) {
    const auto& t = tris[i];
    for (int k = 0; k < 3; ++k) edgeFaces[edgeKey(t[static_cast<size_t>(k)], t[static_cast<size_t>((k + 1) % 3)])].push_back(static_cast<int>(i));
  }
  int naked = 0;
  for (const auto& kv : edgeFaces) if (kv.second.size() == 1) ++naked;
  s.closed = naked == 0 && nt > 0;
  // Vertex pseudonormals: angle-weighted sum of incident face normals.
  std::vector<Vector3d> vertN(static_cast<size_t>(nv), Vector3d(0, 0, 0));
  for (size_t i = 0; i < nt; ++i) {
    const auto& t = tris[i];
    for (int k = 0; k < 3; ++k) {
      const Point3d& a = verts[static_cast<size_t>(t[static_cast<size_t>(k)])];
      const Point3d& b = verts[static_cast<size_t>(t[static_cast<size_t>((k + 1) % 3)])];
      const Point3d& c = verts[static_cast<size_t>(t[static_cast<size_t>((k + 2) % 3)])];
      Vector3d u = Sub(b, a), v = Sub(c, a);
      const double lu = u.Length(), lv = v.Length();
      double ang = ON_PI / 3;
      if (lu > 1e-12 && lv > 1e-12) {
        double cs = Dot(u, v) / (lu * lv);
        cs = std::max(-1.0, std::min(1.0, cs));
        ang = std::acos(cs);
      }
      vertN[static_cast<size_t>(t[static_cast<size_t>(k)])] += faceN[i] * ang;
    }
  }
  for (Vector3d& n : vertN) SafeUnit(n);
  s.prims.reserve(nt);
  for (size_t i = 0; i < nt; ++i) {
    const auto& t = tris[i];
    Primitive p;
    p.kind = 0;
    for (int k = 0; k < 3; ++k) p.p[k] = verts[static_cast<size_t>(t[static_cast<size_t>(k)])];
    p.face_n = faceN[i];
    for (int k = 0; k < 3; ++k) p.vert_n[k] = vertN[static_cast<size_t>(t[static_cast<size_t>(k)])];
    if (s.closed) {
      for (int k = 0; k < 3; ++k) {
        const auto& adj = edgeFaces[edgeKey(t[static_cast<size_t>(k)], t[static_cast<size_t>((k + 1) % 3)])];
        Vector3d en(0, 0, 0);
        for (int fi : adj) en += faceN[static_cast<size_t>(fi)];
        p.edge_n[k] = SafeUnit(en);
      }
    }
    s.prims.push_back(p);
  }
  return s;
}

Source SourceFromPolyline(const std::vector<Point3d>& pts) {
  Source s;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    Primitive p;
    p.kind = 1;
    p.p[0] = pts[i];
    p.p[1] = pts[i + 1];
    s.prims.push_back(p);
  }
  return s;
}

Source SourceFromPoint(Point3d pt) {
  Source s;
  Primitive p;
  p.kind = 2;
  p.p[0] = pt;
  s.prims.push_back(p);
  return s;
}

double SourceArea(const Source& s) {
  double area = 0;
  for (const Primitive& p : s.prims)
    if (p.kind == 0) area += 0.5 * Cross(Sub(p.p[1], p.p[0]), Sub(p.p[2], p.p[0])).Length();
  return area;
}

void SourceBounds(const std::vector<Source>& sources, Point3d& lo, Point3d& hi) {
  lo = Point3d(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
  hi = Point3d(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
  for (const Source& s : sources)
    for (const Primitive& p : s.prims) {
      const int n = p.kind == 0 ? 3 : (p.kind == 1 ? 2 : 1);
      for (int i = 0; i < n; ++i) {
        lo.x = std::min(lo.x, p.p[i].x); lo.y = std::min(lo.y, p.p[i].y); lo.z = std::min(lo.z, p.p[i].z);
        hi.x = std::max(hi.x, p.p[i].x); hi.y = std::max(hi.y, p.p[i].y); hi.z = std::max(hi.z, p.p[i].z);
      }
    }
}

// ---------------------------------------------------------------------------
// Bvh: a uniform spatial hash over the primitives' bounding boxes, queried by
// expanding-ring search. Simple, but a real acceleration structure - turns
// the O(voxels * primitives) naive SDF cost into ~O(voxels * primitives-near-
// surface), which is what makes a 100^3 voxelisation of a few thousand
// triangles tractable.
// ---------------------------------------------------------------------------
struct Bvh::Impl {
  std::vector<Primitive> prims;
  double cell = 1;
  Point3d origin;
  std::unordered_map<int64_t, std::vector<uint32_t>> grid;

  static int64_t Key(int i, int j, int k) {
    // 21 bits per axis (+/- ~1e6 cells), offset to stay non-negative.
    const int64_t bias = 1 << 20;
    return ((static_cast<int64_t>(i + bias) & 0x1FFFFF) << 42) |
           ((static_cast<int64_t>(j + bias) & 0x1FFFFF) << 21) |
           (static_cast<int64_t>(k + bias) & 0x1FFFFF);
  }
  void CellOf(Point3d p, int& i, int& j, int& k) const {
    i = static_cast<int>(std::floor((p.x - origin.x) / cell));
    j = static_cast<int>(std::floor((p.y - origin.y) / cell));
    k = static_cast<int>(std::floor((p.z - origin.z) / cell));
  }
};

Bvh::Bvh(std::vector<Primitive> prims) : impl_(std::make_unique<Impl>()) {
  impl_->prims = std::move(prims);
  if (impl_->prims.empty()) return;
  Point3d lo(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
  Point3d hi(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());
  for (const Primitive& p : impl_->prims) {
    const int n = p.kind == 0 ? 3 : (p.kind == 1 ? 2 : 1);
    for (int i = 0; i < n; ++i) {
      lo.x = std::min(lo.x, p.p[i].x); lo.y = std::min(lo.y, p.p[i].y); lo.z = std::min(lo.z, p.p[i].z);
      hi.x = std::max(hi.x, p.p[i].x); hi.y = std::max(hi.y, p.p[i].y); hi.z = std::max(hi.z, p.p[i].z);
    }
  }
  const double diag = (hi - lo).Length();
  impl_->cell = std::max(diag / std::max<double>(8.0, std::cbrt(static_cast<double>(impl_->prims.size()) * 4)), 1e-6);
  impl_->origin = lo;
  for (uint32_t idx = 0; idx < impl_->prims.size(); ++idx) {
    const Primitive& p = impl_->prims[idx];
    const int n = p.kind == 0 ? 3 : (p.kind == 1 ? 2 : 1);
    int lo_i = std::numeric_limits<int>::max(), lo_j = lo_i, lo_k = lo_i;
    int hi_i = std::numeric_limits<int>::min(), hi_j = hi_i, hi_k = hi_i;
    for (int i = 0; i < n; ++i) {
      int ci, cj, ck;
      impl_->CellOf(p.p[i], ci, cj, ck);
      lo_i = std::min(lo_i, ci); lo_j = std::min(lo_j, cj); lo_k = std::min(lo_k, ck);
      hi_i = std::max(hi_i, ci); hi_j = std::max(hi_j, cj); hi_k = std::max(hi_k, ck);
    }
    for (int i = lo_i; i <= hi_i; ++i)
      for (int j = lo_j; j <= hi_j; ++j)
        for (int k = lo_k; k <= hi_k; ++k) impl_->grid[Impl::Key(i, j, k)].push_back(idx);
  }
}

Bvh::~Bvh() = default;
Bvh::Bvh(Bvh&&) noexcept = default;
Bvh& Bvh::operator=(Bvh&&) noexcept = default;
size_t Bvh::Size() const { return impl_->prims.size(); }
const Primitive& Bvh::PrimAt(size_t i) const { return impl_->prims[i]; }

bool Bvh::Closest(Point3d p, double max_dist, Point3d& out, double& dist, size_t* prim_index) const {
  if (impl_->prims.empty()) return false;
  int ci, cj, ck;
  impl_->CellOf(p, ci, cj, ck);
  double best = max_dist > 0 ? max_dist : std::numeric_limits<double>::max();
  Point3d bestPt = p;
  size_t bestIdx = static_cast<size_t>(-1);
  // Ring r's true surface has O(r^2) cells; visiting the whole (2r+1)^3 cube
  // and filtering (as this used to) is O(r^3) per ring, so the cap below -
  // reached whenever the grid's own cell size is small relative to
  // max_dist, e.g. a finely-tessellated source - turned an already-bounded
  // search into an O(max_ring^4) blowup (some 1.6e9 iterations). Visiting
  // only the 6 true shell faces keeps every ring O(r^2), so the whole
  // search is a harmless O(max_ring^3) even in the worst case.
  auto visit = [&](int i, int j, int k) {
    auto it = impl_->grid.find(Impl::Key(i, j, k));
    if (it == impl_->grid.end()) return;
    for (uint32_t idx : it->second) {
      const Primitive& prim = impl_->prims[idx];
      Point3d cp;
      double bary[3];
      if (prim.kind == 0) cp = ClosestOnTriangle(p, prim.p, bary);
      else if (prim.kind == 1) cp = ClosestOnSegment(p, prim.p[0], prim.p[1]);
      else cp = prim.p[0];
      const double d = (cp - p).Length();
      if (d < best) { best = d; bestPt = cp; bestIdx = idx; }
    }
  };
  const int max_ring = 200;
  for (int r = 0; r <= max_ring; ++r) {
    if (r == 0) {
      visit(ci, cj, ck);
    } else {
      for (int k : {ck - r, ck + r})
        for (int i = ci - r; i <= ci + r; ++i)
          for (int j = cj - r; j <= cj + r; ++j) visit(i, j, k);
      for (int j : {cj - r, cj + r})
        for (int i = ci - r; i <= ci + r; ++i)
          for (int k = ck - r + 1; k <= ck + r - 1; ++k) visit(i, j, k);
      for (int i : {ci - r, ci + r})
        for (int j = cj - r + 1; j <= cj + r - 1; ++j)
          for (int k = ck - r + 1; k <= ck + r - 1; ++k) visit(i, j, k);
    }
    // Any primitive in a farther ring is at least r*cell away, so stop once
    // that bound exceeds `best` - which starts at max_dist even when nothing
    // has been found yet, so a query with no primitive within max_dist stops
    // at the ring where the search radius first exceeds it, instead of
    // scanning every ring up to max_ring regardless of `max_dist` (a
    // previous `bestIdx != -1 &&` guard here defeated the whole point of the
    // max_dist cutoff whenever a source had nothing nearby - the common case
    // for a multi-source ShrinkWrap grid point far from a particular
    // source).
    if (static_cast<double>(r) * impl_->cell > best) break;
  }
  if (bestIdx == static_cast<size_t>(-1)) return false;
  out = bestPt;
  dist = best;
  if (prim_index) *prim_index = bestIdx;
  return true;
}

bool Bvh::Ray(Point3d p, Vector3d dir, double& t) const {
  bool hit = false;
  double best = std::numeric_limits<double>::max();
  dir.Unitize();
  for (const Primitive& prim : impl_->prims) {
    if (prim.kind != 0) continue;
    const Vector3d e1 = Sub(prim.p[1], prim.p[0]), e2 = Sub(prim.p[2], prim.p[0]);
    const Vector3d h = Cross(dir, e2);
    const double det = Dot(e1, h);
    if (std::fabs(det) < 1e-12) continue;
    const double inv = 1.0 / det;
    const Vector3d s = Sub(p, prim.p[0]);
    const double u = Dot(s, h) * inv;
    if (u < 0 || u > 1) continue;
    const Vector3d q = Cross(s, e1);
    const double v = Dot(dir, q) * inv;
    if (v < 0 || u + v > 1) continue;
    const double tt = Dot(e2, q) * inv;
    if (tt >= 0 && tt < best) { best = tt; hit = true; }
  }
  if (hit) t = best;
  return hit;
}

// ---------------------------------------------------------------------------
// BuildSdf
// ---------------------------------------------------------------------------
SdfGrid BuildSdf(const std::vector<Source>& sources, const SdfOptions& opt, SdfReport& report) {
  Point3d lo, hi;
  SourceBounds(sources, lo, hi);
  Vector3d ext = hi - lo;
  double longest = std::max({ext.x, ext.y, ext.z, 1e-6});
  double voxel = opt.voxel > 0 ? opt.voxel : longest / 100.0;
  report.requested_voxel = voxel;
  // Pad by a handful of voxels so the surface has room to close/offset.
  const double pad = voxel * 4;
  lo -= Vector3d(pad, pad, pad);
  hi += Vector3d(pad, pad, pad);
  ext = hi - lo;
  auto counts = [&](double v) {
    return std::array<int64_t, 3>{static_cast<int64_t>(std::ceil(ext.x / v)) + 1, static_cast<int64_t>(std::ceil(ext.y / v)) + 1,
                                  static_cast<int64_t>(std::ceil(ext.z / v)) + 1};
  };
  auto total = [&](double v) { auto c = counts(v); return c[0] * c[1] * c[2]; };
  report.capped = false;
  while (total(voxel) > static_cast<int64_t>(std::max(64, opt.max_voxels))) {
    voxel *= 1.26;  // ~2x volume growth per step
    report.capped = true;
  }
  auto c = counts(voxel);
  SdfGrid g;
  g.nx = static_cast<int>(c[0]);
  g.ny = static_cast<int>(c[1]);
  g.nz = static_cast<int>(c[2]);
  g.h = voxel;
  g.origin = lo;
  g.d.assign(static_cast<size_t>(g.nx) * g.ny * g.nz, static_cast<float>(longest));
  report.nx = g.nx; report.ny = g.ny; report.nz = g.nz; report.voxel = voxel;

  struct Band { Bvh bvh; bool closed; double shell; };
  std::vector<Band> bands;
  bands.reserve(sources.size());
  const double default_shell = opt.shell > 0 ? opt.shell : voxel * 1.5;
  for (const Source& s : sources) {
    if (s.prims.empty()) continue;
    const bool closed = s.has_faces && s.closed;
    if (!closed) ++report.shells;
    bands.push_back(Band{Bvh(s.prims), closed, default_shell});
  }
  if (bands.empty()) return g;

  const double band = voxel * 6 + std::fabs(opt.offset) + default_shell * 2;
  for (int k = 0; k < g.nz; ++k)
    for (int j = 0; j < g.ny; ++j)
      for (int i = 0; i < g.nx; ++i) {
        const Point3d pos = g.Pos(i, j, k);
        double best = longest;
        for (const Band& b : bands) {
          Point3d cp;
          double d;
          size_t idx = 0;
          if (!b.bvh.Closest(pos, band, cp, d, &idx)) continue;
          double signedD;
          if (b.closed) {
            // Baerentzen & Aanaes pseudonormal sign test: negative (inside)
            // when the query point is on the far side of the surface from
            // the pseudonormal at the closest feature.
            const Primitive& prim = b.bvh.PrimAt(idx);
            double bary[3];
            ClosestOnTriangle(pos, prim.p, bary);
            const Vector3d n = PseudoNormalAt(prim, bary);
            signedD = Dot(Sub(pos, cp), n) < 0 ? -d : d;
          } else {
            // Open mesh / curve / point source: an unsigned double-sided
            // shell of thickness `b.shell` around the geometry.
            signedD = d - b.shell;
          }
          best = std::min(best, signedD);
        }
        g.At(i, j, k) = static_cast<float>(best);
      }
  return g;
}

void MirrorSdf(SdfGrid& g, int axis) {
  if (axis < 0 || axis > 2) return;
  const int n[3] = {g.nx, g.ny, g.nz};
  const int mid = n[axis] / 2;
  for (int k = 0; k < g.nz; ++k)
    for (int j = 0; j < g.ny; ++j)
      for (int i = 0; i < g.nx; ++i) {
        int idx[3] = {i, j, k};
        if (idx[axis] >= mid) continue;
        int midx[3] = {i, j, k};
        midx[axis] = n[axis] - 1 - idx[axis];
        const float a = g.At(i, j, k);
        const float b = g.At(midx[0], midx[1], midx[2]);
        const float m = std::min(a, b);
        g.At(i, j, k) = m;
        g.At(midx[0], midx[1], midx[2]) = m;
      }
}

double SdfGrid::Sample(Point3d p) const {
  double fx = (p.x - origin.x) / h, fy = (p.y - origin.y) / h, fz = (p.z - origin.z) / h;
  int i0 = static_cast<int>(std::floor(fx)), j0 = static_cast<int>(std::floor(fy)), k0 = static_cast<int>(std::floor(fz));
  i0 = std::max(0, std::min(nx - 2, i0));
  j0 = std::max(0, std::min(ny - 2, j0));
  k0 = std::max(0, std::min(nz - 2, k0));
  const double tx = std::max(0.0, std::min(1.0, fx - i0)), ty = std::max(0.0, std::min(1.0, fy - j0)), tz = std::max(0.0, std::min(1.0, fz - k0));
  auto v = [&](int di, int dj, int dk) { return static_cast<double>(At(i0 + di, j0 + dj, k0 + dk)); };
  const double c00 = v(0, 0, 0) * (1 - tx) + v(1, 0, 0) * tx;
  const double c10 = v(0, 1, 0) * (1 - tx) + v(1, 1, 0) * tx;
  const double c01 = v(0, 0, 1) * (1 - tx) + v(1, 0, 1) * tx;
  const double c11 = v(0, 1, 1) * (1 - tx) + v(1, 1, 1) * tx;
  const double c0 = c00 * (1 - ty) + c10 * ty;
  const double c1 = c01 * (1 - ty) + c11 * ty;
  return c0 * (1 - tz) + c1 * tz;
}

Vector3d SdfGrid::Gradient(Point3d p) const {
  const double e = h * 0.5;
  // Central difference df/dx ~= (f(x+e) - f(x-e)) / (2e) - the /(2e) here is
  // not optional: without it this returns the un-normalized numerator, off
  // from the true (unit-ish, for a real signed-distance field) gradient by
  // a factor of h. ProjectToIso's Newton step divides its step size by
  // gl*gl, so an undersized `gl` (h~0.25 gives a ~16x gl^2 shrink) turns a
  // one-voxel correction into a many-voxel overshoot that compounds every
  // iteration - confirmed by a concave ShrinkWrap regression test whose
  // wrap mesh exploded from a ~20-unit bounding box to several thousand
  // units the moment Smooth (which always runs ProjectToIso afterward)
  // was greater than zero, i.e. on every default-settings ShrinkWrap.
  const double inv2e = 1.0 / (2.0 * e);
  const double dx = (Sample(p + Vector3d(e, 0, 0)) - Sample(p - Vector3d(e, 0, 0))) * inv2e;
  const double dy = (Sample(p + Vector3d(0, e, 0)) - Sample(p - Vector3d(0, e, 0))) * inv2e;
  const double dz = (Sample(p + Vector3d(0, 0, e)) - Sample(p - Vector3d(0, 0, e))) * inv2e;
  Vector3d g(dx, dy, dz);
  if (g.Length() < 1e-12) return Vector3d(0, 0, 1);
  return g;
}

// ---------------------------------------------------------------------------
// MarchingCubes: each cube is decomposed into 6 tetrahedra sharing the main
// diagonal (corner 0 - corner 6); each tetrahedron is resolved by its own
// sign pattern (1, 2 or 3 of its 4 corners inside). This is the standard
// "marching tetrahedra" variant of marching cubes: it walks every one of the
// 2^8 = 256 corner-sign combinations a cube can have (each decomposes into
// six of the 16 tetrahedron cases), has no face ambiguity to resolve (unlike
// the original 15-case cube table), and produces a topologically closed,
// manifold triangle soup that MergeAndWeld() below turns into a shared-
// vertex watertight mesh.
// ---------------------------------------------------------------------------
namespace {

Point3d EdgeCross(Point3d pa, double va, Point3d pb, double vb, double iso) {
  double t = (vb - va) != 0 ? (iso - va) / (vb - va) : 0.5;
  t = std::max(0.02, std::min(0.98, t));
  return pa + (pb - pa) * t;
}

void EmitTri(std::vector<Point3d>& out, Point3d a, Point3d b, Point3d c, Vector3d outward) {
  Vector3d n = Cross(b - a, c - a);
  if (Dot(n, outward) < 0) { out.push_back(a); out.push_back(c); out.push_back(b); }
  else { out.push_back(a); out.push_back(b); out.push_back(c); }
}

void MarchTet(const Point3d pos[4], const double val[4], double iso, std::vector<Point3d>& out) {
  int inside = 0;
  bool isIn[4];
  for (int i = 0; i < 4; ++i) { isIn[i] = val[i] < iso; inside += isIn[i] ? 1 : 0; }
  if (inside == 0 || inside == 4) return;
  Point3d insideCentroid(0, 0, 0), outsideCentroid(0, 0, 0);
  int nin = 0, nout = 0;
  for (int i = 0; i < 4; ++i) {
    if (isIn[i]) { insideCentroid = insideCentroid + (pos[i] - Point3d::Origin); ++nin; }
    else { outsideCentroid = outsideCentroid + (pos[i] - Point3d::Origin); ++nout; }
  }
  insideCentroid = nin ? Point3d::Origin + (insideCentroid - Point3d::Origin) / static_cast<double>(nin) : Point3d::Origin;
  outsideCentroid = nout ? Point3d::Origin + (outsideCentroid - Point3d::Origin) / static_cast<double>(nout) : Point3d::Origin;
  const Vector3d outward = SafeUnit(outsideCentroid - insideCentroid);
  if (inside == 1 || inside == 3) {
    const bool wantIn = inside == 1;
    int lone = -1, others[3], oc = 0;
    for (int i = 0; i < 4; ++i) { if (isIn[i] == wantIn) lone = i; else others[oc++] = i; }
    Point3d e0 = EdgeCross(pos[lone], val[lone], pos[others[0]], val[others[0]], iso);
    Point3d e1 = EdgeCross(pos[lone], val[lone], pos[others[1]], val[others[1]], iso);
    Point3d e2 = EdgeCross(pos[lone], val[lone], pos[others[2]], val[others[2]], iso);
    EmitTri(out, e0, e1, e2, outward);
  } else {
    // 2-2 split: the four crossing edges connect the two "inside" corners
    // to the two "outside" corners, forming a quad.
    int in2[2], out2[2], ii = 0, oo = 0;
    for (int i = 0; i < 4; ++i) { if (isIn[i]) in2[ii++] = i; else out2[oo++] = i; }
    Point3d a = EdgeCross(pos[in2[0]], val[in2[0]], pos[out2[0]], val[out2[0]], iso);
    Point3d b = EdgeCross(pos[in2[0]], val[in2[0]], pos[out2[1]], val[out2[1]], iso);
    Point3d c = EdgeCross(pos[in2[1]], val[in2[1]], pos[out2[1]], val[out2[1]], iso);
    Point3d d = EdgeCross(pos[in2[1]], val[in2[1]], pos[out2[0]], val[out2[0]], iso);
    EmitTri(out, a, b, c, outward);
    EmitTri(out, a, c, d, outward);
  }
}

}  // namespace

kernel::Mesh MarchingCubes(const SdfGrid& g, double iso) {
  static const int tets[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
  std::vector<Point3d> soup;
  for (int k = 0; k + 1 < g.nz; ++k)
    for (int j = 0; j + 1 < g.ny; ++j)
      for (int i = 0; i + 1 < g.nx; ++i) {
        const Point3d cp[8] = {g.Pos(i, j, k), g.Pos(i + 1, j, k), g.Pos(i + 1, j + 1, k), g.Pos(i, j + 1, k),
                                g.Pos(i, j, k + 1), g.Pos(i + 1, j, k + 1), g.Pos(i + 1, j + 1, k + 1), g.Pos(i, j + 1, k + 1)};
        const double cv[8] = {g.At(i, j, k), g.At(i + 1, j, k), g.At(i + 1, j + 1, k), g.At(i, j + 1, k),
                               g.At(i, j, k + 1), g.At(i + 1, j, k + 1), g.At(i + 1, j + 1, k + 1), g.At(i, j + 1, k + 1)};
        double lo = cv[0], hi = cv[0];
        for (double v : cv) { lo = std::min(lo, v); hi = std::max(hi, v); }
        if (lo >= iso || hi < iso) continue;
        for (const int (&t)[4] : tets) {
          Point3d pos[4] = {cp[t[0]], cp[t[1]], cp[t[2]], cp[t[3]]};
          double val[4] = {cv[t[0]], cv[t[1]], cv[t[2]], cv[t[3]]};
          MarchTet(pos, val, iso, soup);
        }
      }
  kernel::Mesh raw;
  ON_Mesh& m = raw.raw();
  for (size_t i = 0; i < soup.size(); ++i) m.SetVertex(static_cast<int>(i), soup[i]);
  for (size_t i = 0; i + 2 < soup.size(); i += 3) m.SetTriangle(static_cast<int>(i / 3), static_cast<int>(i), static_cast<int>(i + 1), static_cast<int>(i + 2));
  if (soup.empty()) return raw;
  kernel::Mesh welded = kernel::Mesh::MergeAndWeld({raw}, g.h * 1e-4);
  welded.raw().ComputeFaceNormals();
  welded.raw().ComputeVertexNormals();
  return welded;
}

// ---------------------------------------------------------------------------
// DualContouring: one vertex per active grid cell (a cell whose 8 corners do
// not all share the same sign), placed at the crossing points of its 12
// edges (Hermite data) refined by a few Gauss-Seidel passes that project the
// point onto each crossing's tangent plane in turn - a lightweight iterative
// stand-in for the full QEF SVD solve that still pulls the vertex toward
// sharp features instead of just averaging (Surface Nets would stop at the
// plain average; this goes one step further when `sharp` is requested).
// One quad is emitted per interior grid edge that changes sign, connecting
// the four cells around it - the classic dual-contouring topology, which is
// exactly-manifold and all-quad by construction.
// ---------------------------------------------------------------------------
kernel::Mesh DualContouring(const SdfGrid& g, double iso, bool sharp, std::vector<char>* features) {
  const int cx = std::max(1, g.nx - 1), cy = std::max(1, g.ny - 1), cz = std::max(1, g.nz - 1);
  auto cellIndex = [&](int i, int j, int k) { return (static_cast<size_t>(k) * cy + j) * cx + i; };
  std::vector<int> vertOf(static_cast<size_t>(cx) * cy * cz, -1);
  std::vector<Point3d> verts;
  std::vector<char> sharpFlag;
  static const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (int k = 0; k < cz; ++k)
    for (int j = 0; j < cy; ++j)
      for (int i = 0; i < cx; ++i) {
        double cv[8];
        Point3d cp[8];
        bool any = false, first = false;
        for (int c = 0; c < 8; ++c) {
          cp[c] = g.Pos(i + corner[c][0], j + corner[c][1], k + corner[c][2]);
          cv[c] = g.At(i + corner[c][0], j + corner[c][1], k + corner[c][2]);
        }
        first = cv[0] < iso;
        for (int c = 1; c < 8; ++c) if ((cv[c] < iso) != first) any = true;
        if (!any) continue;
        std::vector<Point3d> xs;
        std::vector<Vector3d> ns;
        for (const auto& e : edges) {
          const bool a = cv[e[0]] < iso, b = cv[e[1]] < iso;
          if (a == b) continue;
          Point3d cross = EdgeCross(cp[e[0]], cv[e[0]], cp[e[1]], cv[e[1]], iso);
          xs.push_back(cross);
          ns.push_back(SafeUnit(g.Gradient(cross)));
        }
        if (xs.empty()) continue;
        Point3d p(0, 0, 0);
        for (const Point3d& x : xs) p = p + (x - Point3d::Origin);
        p = Point3d::Origin + (p - Point3d::Origin) / static_cast<double>(xs.size());
        // Clamp inside the cell so a stray gradient can't push it out.
        Point3d clo = cp[0], chi = cp[6];
        bool sharpHere = false;
        if (sharp && xs.size() >= 2) {
          double spread = 0;
          for (size_t a = 0; a < ns.size(); ++a)
            for (size_t b = a + 1; b < ns.size(); ++b) spread = std::max(spread, 1.0 - Dot(ns[a], ns[b]));
          sharpHere = spread > 0.35;  // normals disagree -> a real edge/corner in this cell
          for (int it = 0; it < 6; ++it) {
            Point3d np(0, 0, 0);
            for (size_t s = 0; s < xs.size(); ++s) {
              const double d = Dot(ns[s], p - xs[s]);
              np = np + (p - ns[s] * d - Point3d::Origin);
            }
            p = Point3d::Origin + (np - Point3d::Origin) / static_cast<double>(xs.size());
            p.x = std::max(clo.x, std::min(chi.x, p.x));
            p.y = std::max(clo.y, std::min(chi.y, p.y));
            p.z = std::max(clo.z, std::min(chi.z, p.z));
          }
        }
        vertOf[cellIndex(i, j, k)] = static_cast<int>(verts.size());
        verts.push_back(p);
        sharpFlag.push_back(sharpHere ? 1 : 0);
      }
  std::vector<std::array<int, 4>> quads;
  auto emitQuadForEdge = [&](int i, int j, int k, int axis, bool a_in) {
    // The four cells sharing this grid edge, in the winding order that
    // keeps the quad's normal pointing from inside to outside along `axis`
    // when a_in (corner at the edge start) is inside.
    int cells[4][3];
    if (axis == 0) { cells[0][0]=i;cells[0][1]=j-1;cells[0][2]=k-1; cells[1][0]=i;cells[1][1]=j;cells[1][2]=k-1; cells[2][0]=i;cells[2][1]=j;cells[2][2]=k; cells[3][0]=i;cells[3][1]=j-1;cells[3][2]=k; }
    else if (axis == 1) { cells[0][0]=i-1;cells[0][1]=j;cells[0][2]=k-1; cells[1][0]=i;cells[1][1]=j;cells[1][2]=k-1; cells[2][0]=i;cells[2][1]=j;cells[2][2]=k; cells[3][0]=i-1;cells[3][1]=j;cells[3][2]=k; }
    else { cells[0][0]=i-1;cells[0][1]=j-1;cells[0][2]=k; cells[1][0]=i;cells[1][1]=j-1;cells[1][2]=k; cells[2][0]=i;cells[2][1]=j;cells[2][2]=k; cells[3][0]=i-1;cells[3][1]=j;cells[3][2]=k; }
    int vi[4];
    for (int c = 0; c < 4; ++c) {
      if (cells[c][0] < 0 || cells[c][0] >= cx || cells[c][1] < 0 || cells[c][1] >= cy || cells[c][2] < 0 || cells[c][2] >= cz) return;
      vi[c] = vertOf[cellIndex(cells[c][0], cells[c][1], cells[c][2])];
      if (vi[c] < 0) return;
    }
    if (!a_in) std::swap(vi[1], vi[3]);
    quads.push_back({vi[0], vi[1], vi[2], vi[3]});
  };
  for (int k = 0; k < g.nz; ++k)
    for (int j = 0; j < g.ny; ++j)
      for (int i = 0; i + 1 < g.nx; ++i) {
        const bool a = g.At(i, j, k) < iso, b = g.At(i + 1, j, k) < iso;
        if (a != b && j > 0 && k > 0) emitQuadForEdge(i, j, k, 0, a);
      }
  for (int k = 0; k < g.nz; ++k)
    for (int j = 0; j + 1 < g.ny; ++j)
      for (int i = 0; i < g.nx; ++i) {
        const bool a = g.At(i, j, k) < iso, b = g.At(i, j + 1, k) < iso;
        if (a != b && i > 0 && k > 0) emitQuadForEdge(i, j, k, 1, a);
      }
  for (int k = 0; k + 1 < g.nz; ++k)
    for (int j = 0; j < g.ny; ++j)
      for (int i = 0; i < g.nx; ++i) {
        const bool a = g.At(i, j, k) < iso, b = g.At(i, j, k + 1) < iso;
        if (a != b && i > 0 && j > 0) emitQuadForEdge(i, j, k, 2, a);
      }
  kernel::Mesh out;
  ON_Mesh& m = out.raw();
  for (size_t i = 0; i < verts.size(); ++i) m.SetVertex(static_cast<int>(i), verts[i]);
  int fi = 0;
  for (const auto& q : quads) m.SetQuad(fi++, q[0], q[1], q[2], q[3]);
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  if (features) *features = sharpFlag;
  return out;
}

// ---------------------------------------------------------------------------
// Smoothing / projection
// ---------------------------------------------------------------------------
namespace {
std::vector<std::vector<int>> VertexNeighbors(const ON_Mesh& m) {
  std::vector<std::set<int>> nb(static_cast<size_t>(m.VertexCount()));
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    int idx[4] = {f.vi[0], f.vi[1], f.vi[2], f.vi[3]};
    const int n = idx[3] == idx[2] ? 3 : 4;
    for (int k = 0; k < n; ++k) {
      nb[static_cast<size_t>(idx[k])].insert(idx[(k + 1) % n]);
      nb[static_cast<size_t>(idx[k])].insert(idx[(k + n - 1) % n]);
    }
  }
  std::vector<std::vector<int>> out(nb.size());
  for (size_t i = 0; i < nb.size(); ++i) out[i].assign(nb[i].begin(), nb[i].end());
  return out;
}
}  // namespace

void SmoothTaubin(kernel::Mesh& m, int iterations, const std::vector<char>* fixed) {
  if (iterations <= 0) return;
  ON_Mesh& raw = m.raw();
  const int nv = raw.VertexCount();
  std::vector<std::vector<int>> nb = VertexNeighbors(raw);
  std::vector<Point3d> pos(static_cast<size_t>(nv));
  for (int i = 0; i < nv; ++i) pos[static_cast<size_t>(i)] = raw.Vertex(i);
  const double factors[2] = {0.5, -0.53};
  for (int it = 0; it < iterations; ++it)
    for (double lambda : factors) {
      std::vector<Point3d> next = pos;
      for (int i = 0; i < nv; ++i) {
        if (fixed && i < static_cast<int>(fixed->size()) && (*fixed)[static_cast<size_t>(i)]) continue;
        if (nb[static_cast<size_t>(i)].empty()) continue;
        Vector3d avg(0, 0, 0);
        for (int j : nb[static_cast<size_t>(i)]) avg += Sub(pos[static_cast<size_t>(j)], pos[static_cast<size_t>(i)]);
        avg = avg / static_cast<double>(nb[static_cast<size_t>(i)].size());
        next[static_cast<size_t>(i)] = pos[static_cast<size_t>(i)] + avg * lambda;
      }
      pos.swap(next);
    }
  for (int i = 0; i < nv; ++i) raw.SetVertex(i, pos[static_cast<size_t>(i)]);
  raw.ComputeFaceNormals();
  raw.ComputeVertexNormals();
}

void ProjectToIso(kernel::Mesh& m, const SdfGrid& g, double iso, const std::vector<char>* fixed) {
  ON_Mesh& raw = m.raw();
  for (int i = 0; i < raw.VertexCount(); ++i) {
    if (fixed && i < static_cast<int>(fixed->size()) && (*fixed)[static_cast<size_t>(i)]) continue;
    Point3d p = raw.Vertex(i);
    for (int it = 0; it < 4; ++it) {
      const double s = g.Sample(p) - iso;
      Vector3d grad = g.Gradient(p);
      const double gl = grad.Length();
      if (gl < 1e-9) break;
      p = p - grad * (s / (gl * gl));
    }
    raw.SetVertex(i, p);
  }
  raw.ComputeFaceNormals();
  raw.ComputeVertexNormals();
}

void ProjectToSources(kernel::Mesh& m, const std::vector<Source>& sources, const std::vector<char>* fixed) {
  std::vector<Bvh> bvhs;
  for (const Source& s : sources) if (!s.prims.empty()) bvhs.emplace_back(s.prims);
  if (bvhs.empty()) return;
  ON_Mesh& raw = m.raw();
  for (int i = 0; i < raw.VertexCount(); ++i) {
    if (fixed && i < static_cast<int>(fixed->size()) && (*fixed)[static_cast<size_t>(i)]) continue;
    Point3d p = raw.Vertex(i);
    Point3d best = p;
    double bestD = std::numeric_limits<double>::max();
    for (const Bvh& b : bvhs) {
      Point3d cp;
      double d;
      if (b.Closest(p, std::numeric_limits<double>::max(), cp, d) && d < bestD) { bestD = d; best = cp; }
    }
    raw.SetVertex(i, best);
  }
  raw.ComputeFaceNormals();
  raw.ComputeVertexNormals();
}

// ---------------------------------------------------------------------------
// Decimate: Garland-Heckbert quadric error edge collapse.
// ---------------------------------------------------------------------------
namespace {
struct Quadric {
  double a[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // q11 q12 q13 q14 q22 q23 q24 q33 q34 q44
  void AddPlane(double A, double B, double C, double D, double w = 1.0) {
    a[0] += w * A * A; a[1] += w * A * B; a[2] += w * A * C; a[3] += w * A * D;
    a[4] += w * B * B; a[5] += w * B * C; a[6] += w * B * D;
    a[7] += w * C * C; a[8] += w * C * D;
    a[9] += w * D * D;
  }
  Quadric& operator+=(const Quadric& o) { for (int i = 0; i < 10; ++i) a[i] += o.a[i]; return *this; }
  double Cost(Point3d v) const {
    const double x = v.x, y = v.y, z = v.z;
    return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z + 2 * a[6] * y +
           a[7] * z * z + 2 * a[8] * z + a[9];
  }
  bool Optimum(Point3d& out) const {
    // Solve [[q11 q12 q13],[q12 q22 q23],[q13 q23 q33]] v = -[q14,q24,q34].
    double A[3][3] = {{a[0], a[1], a[2]}, {a[1], a[4], a[5]}, {a[2], a[5], a[7]}};
    double b[3] = {-a[3], -a[6], -a[8]};
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                 A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (std::fabs(det) < 1e-9) return false;
    double x[3];
    for (int col = 0; col < 3; ++col) {
      double M[3][3];
      for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) M[r][c] = c == col ? b[r] : A[r][c];
      double d = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                 M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
      x[col] = d / det;
    }
    out = Point3d(x[0], x[1], x[2]);
    return true;
  }
};
}  // namespace

kernel::Mesh Decimate(const kernel::Mesh& src, int target_faces, bool preserve_boundary) {
  kernel::Mesh welded = kernel::Mesh::MergeAndWeld({src}, 1e-9);
  const ON_Mesh& raw0 = welded.raw();
  std::vector<Point3d> V(static_cast<size_t>(raw0.VertexCount()));
  for (int i = 0; i < raw0.VertexCount(); ++i) V[static_cast<size_t>(i)] = raw0.Vertex(i);
  std::vector<std::array<int, 3>> F;
  for (int i = 0; i < raw0.FaceCount(); ++i) {
    const ON_MeshFace& f = raw0.m_F[i];
    F.push_back({f.vi[0], f.vi[1], f.vi[2]});
    if (f.vi[3] != f.vi[2]) F.push_back({f.vi[0], f.vi[2], f.vi[3]});
  }
  const size_t nv = V.size();
  std::vector<char> alive_v(nv, 1), alive_f(F.size(), 1);
  std::vector<std::vector<int>> vTris(nv);
  for (size_t fi = 0; fi < F.size(); ++fi) for (int vi : F[fi]) vTris[static_cast<size_t>(vi)].push_back(static_cast<int>(fi));
  std::vector<Quadric> Q(nv);
  auto facePlane = [&](const std::array<int, 3>& f, double& A, double& B, double& C, double& D) {
    Vector3d n = Cross(Sub(V[static_cast<size_t>(f[1])], V[static_cast<size_t>(f[0])]), Sub(V[static_cast<size_t>(f[2])], V[static_cast<size_t>(f[0])]));
    const double len = n.Length();
    if (len < 1e-15) { A = B = C = D = 0; return; }
    n = n / len;
    A = n.x; B = n.y; C = n.z; D = -Dot(n, V[static_cast<size_t>(f[0])] - Point3d::Origin);
  };
  for (size_t fi = 0; fi < F.size(); ++fi) {
    double A, B, C, D;
    facePlane(F[fi], A, B, C, D);
    for (int vi : F[fi]) Q[static_cast<size_t>(vi)].AddPlane(A, B, C, D);
  }
  std::map<std::pair<int, int>, int> edgeUse;
  for (const auto& f : F)
    for (int k = 0; k < 3; ++k) {
      int a = f[static_cast<size_t>(k)], b = f[static_cast<size_t>((k + 1) % 3)];
      if (a > b) std::swap(a, b);
      ++edgeUse[{a, b}];
    }
  auto isBoundaryVertex = [&](int v) {
    for (const auto& kv : edgeUse) if (kv.second == 1 && (kv.first.first == v || kv.first.second == v)) return true;
    return false;
  };
  std::vector<char> boundary(nv, 0);
  if (preserve_boundary) for (int i = 0; i < static_cast<int>(nv); ++i) boundary[static_cast<size_t>(i)] = isBoundaryVertex(i) ? 1 : 0;

  std::vector<int> version(nv, 0);
  struct Cand { double cost; int a, b; Point3d pos; int va, vb; };
  struct Cmp { bool operator()(const Cand& x, const Cand& y) const { return x.cost > y.cost; } };
  std::priority_queue<Cand, std::vector<Cand>, Cmp> pq;
  auto isBoundaryEdge = [&](int a, int b) { int x = a, y = b; if (x > y) std::swap(x, y); auto it = edgeUse.find({x, y}); return it != edgeUse.end() && it->second == 1; };
  auto pushEdge = [&](int a, int b) {
    if (preserve_boundary && (boundary[static_cast<size_t>(a)] || boundary[static_cast<size_t>(b)]) && isBoundaryEdge(a, b)) return;
    Quadric q = Q[static_cast<size_t>(a)]; q += Q[static_cast<size_t>(b)];
    Point3d opt;
    if (!q.Optimum(opt)) opt = Point3d::Origin + ((V[static_cast<size_t>(a)] - Point3d::Origin) + (V[static_cast<size_t>(b)] - Point3d::Origin)) * 0.5;
    pq.push(Cand{q.Cost(opt), a, b, opt, version[static_cast<size_t>(a)], version[static_cast<size_t>(b)]});
  };
  std::set<std::pair<int, int>> seen;
  for (const auto& kv : edgeUse) { pushEdge(kv.first.first, kv.first.second); seen.insert(kv.first); }

  int faceCount = static_cast<int>(F.size());
  while (faceCount > std::max(4, target_faces) && !pq.empty()) {
    Cand c = pq.top();
    pq.pop();
    if (!alive_v[static_cast<size_t>(c.a)] || !alive_v[static_cast<size_t>(c.b)] || c.a == c.b) continue;
    if (c.va != version[static_cast<size_t>(c.a)] || c.vb != version[static_cast<size_t>(c.b)]) continue;
    const int a = c.a, b = c.b;
    V[static_cast<size_t>(a)] = c.pos;
    Q[static_cast<size_t>(a)] += Q[static_cast<size_t>(b)];
    alive_v[static_cast<size_t>(b)] = 0;
    ++version[static_cast<size_t>(a)];
    std::set<int> touched;
    for (int fi : vTris[static_cast<size_t>(b)]) {
      if (!alive_f[static_cast<size_t>(fi)]) continue;
      auto& f = F[static_cast<size_t>(fi)];
      for (int& vi : f) if (vi == b) vi = a;
      if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2]) { alive_f[static_cast<size_t>(fi)] = 0; --faceCount; continue; }
      vTris[static_cast<size_t>(a)].push_back(fi);
      for (int vi : f) touched.insert(vi);
    }
    for (int t : touched) if (t != a) pushEdge(a, t);
  }
  kernel::Mesh out;
  ON_Mesh& m = out.raw();
  std::vector<int> remap(nv, -1);
  int nvOut = 0;
  for (size_t i = 0; i < nv; ++i) if (alive_v[i]) { remap[i] = nvOut; m.SetVertex(nvOut, V[i]); ++nvOut; }
  int fiOut = 0;
  for (size_t fi = 0; fi < F.size(); ++fi) {
    if (!alive_f[fi]) continue;
    const auto& f = F[fi];
    if (remap[static_cast<size_t>(f[0])] < 0 || remap[static_cast<size_t>(f[1])] < 0 || remap[static_cast<size_t>(f[2])] < 0) continue;
    m.SetTriangle(fiOut++, remap[static_cast<size_t>(f[0])], remap[static_cast<size_t>(f[1])], remap[static_cast<size_t>(f[2])]);
  }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  return out;
}

double MaxDeviation(const kernel::Mesh& a, const kernel::Mesh& b) {
  auto oneWay = [](const kernel::Mesh& from, const kernel::Mesh& to) {
    Source s = SourceFromMesh(to, 1e-9);
    if (s.prims.empty()) return 0.0;
    Bvh bvh(s.prims);
    double worst = 0;
    const ON_Mesh& raw = from.raw();
    for (int i = 0; i < raw.VertexCount(); ++i) {
      Point3d cp;
      double d;
      if (bvh.Closest(raw.Vertex(i), std::numeric_limits<double>::max(), cp, d)) worst = std::max(worst, d);
    }
    return worst;
  };
  return std::max(oneWay(a, b), oneWay(b, a));
}

}  // namespace dino8::app::remesh
