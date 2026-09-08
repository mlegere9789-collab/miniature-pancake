#include "render/Bvh.h"

#include <algorithm>
#include <cmath>

namespace dino8::render {

using kernel::Point3d;
using kernel::Vector3d;

namespace {

struct Box {
  Point3d bmin{1e30, 1e30, 1e30}, bmax{-1e30, -1e30, -1e30};
  void Grow(const Point3d& p) {
    bmin.x = std::min(bmin.x, p.x); bmin.y = std::min(bmin.y, p.y); bmin.z = std::min(bmin.z, p.z);
    bmax.x = std::max(bmax.x, p.x); bmax.y = std::max(bmax.y, p.y); bmax.z = std::max(bmax.z, p.z);
  }
  void Grow(const Box& o) { Grow(o.bmin); Grow(o.bmax); }
  double Area() const {
    const double dx = std::max(bmax.x - bmin.x, 0.0), dy = std::max(bmax.y - bmin.y, 0.0), dz = std::max(bmax.z - bmin.z, 0.0);
    return 2.0 * (dx * dy + dy * dz + dz * dx);
  }
};

Box TriBox(const BvhTriangle& t) {
  Box b;
  b.Grow(t.v0); b.Grow(t.v1); b.Grow(t.v2);
  return b;
}

Point3d Centroid(const BvhTriangle& t) {
  return Point3d((t.v0.x + t.v1.x + t.v2.x) / 3.0, (t.v0.y + t.v1.y + t.v2.y) / 3.0, (t.v0.z + t.v1.z + t.v2.z) / 3.0);
}

// Ray-box slab test; returns whether it intersects within [tmin, tmax].
bool HitBox(const Point3d& bmin, const Point3d& bmax, const Point3d& o, const Vector3d& inv_d, float tmin, float tmax) {
  double t0 = tmin, t1 = tmax;
  const double* omin = &bmin.x; const double* omax = &bmax.x; const double* org = &o.x; const double* inv = &inv_d.x;
  for (int a = 0; a < 3; ++a) {
    double tn = (omin[a] - org[a]) * inv[a];
    double tf = (omax[a] - org[a]) * inv[a];
    if (tn > tf) std::swap(tn, tf);
    t0 = std::max(t0, tn);
    t1 = std::min(t1, tf);
    if (t0 > t1) return false;
  }
  return true;
}

constexpr int kBins = 12;
constexpr int kMaxLeaf = 4;

}  // namespace

void Bvh::Build(std::vector<BvhTriangle> triangles) {
  tris_ = std::move(triangles);
  nodes_.clear();
  if (tris_.empty()) return;
  nodes_.reserve(tris_.size() * 2);
  BuildRange(0, static_cast<int>(tris_.size()), 0);
}

int Bvh::BuildRange(int start, int count, int depth) {
  const int node_index = static_cast<int>(nodes_.size());
  nodes_.emplace_back();
  Box bounds;
  for (int i = 0; i < count; ++i) bounds.Grow(TriBox(tris_[static_cast<size_t>(start + i)]));

  if (count <= kMaxLeaf || depth > 40) {
    nodes_[static_cast<size_t>(node_index)].bmin = bounds.bmin;
    nodes_[static_cast<size_t>(node_index)].bmax = bounds.bmax;
    nodes_[static_cast<size_t>(node_index)].start = start;
    nodes_[static_cast<size_t>(node_index)].count = count;
    return node_index;
  }

  // Binned SAH: try each of the 3 axes, kBins bins on the centroid extent.
  Box centroid_box;
  for (int i = 0; i < count; ++i) centroid_box.Grow(Centroid(tris_[static_cast<size_t>(start + i)]));
  const double ext[3] = {centroid_box.bmax.x - centroid_box.bmin.x, centroid_box.bmax.y - centroid_box.bmin.y,
                         centroid_box.bmax.z - centroid_box.bmin.z};
  const double cmin[3] = {centroid_box.bmin.x, centroid_box.bmin.y, centroid_box.bmin.z};

  int best_axis = -1, best_split = -1;
  double best_cost = 1e300;
  for (int axis = 0; axis < 3; ++axis) {
    if (ext[axis] < 1e-12) continue;
    struct Bin { Box box; int count = 0; } bins[kBins];
    auto bin_of = [&](const BvhTriangle& t) {
      const auto cen = Centroid(t);
      const double c = (&cen.x)[axis];
      int b = static_cast<int>((c - cmin[axis]) / ext[axis] * kBins);
      return std::clamp(b, 0, kBins - 1);
    };
    for (int i = 0; i < count; ++i) {
      const BvhTriangle& t = tris_[static_cast<size_t>(start + i)];
      Bin& bin = bins[bin_of(t)];
      bin.box.Grow(TriBox(t));
      ++bin.count;
    }
    // Sweep from the left and right to get prefix/suffix SAH costs per split.
    Box left_box[kBins]; int left_count[kBins];
    Box acc; int accn = 0;
    for (int i = 0; i < kBins; ++i) { acc.Grow(bins[i].box); accn += bins[i].count; left_box[i] = acc; left_count[i] = accn; }
    Box right_box[kBins]; int right_count[kBins];
    acc = Box(); accn = 0;
    for (int i = kBins - 1; i >= 0; --i) { acc.Grow(bins[i].box); accn += bins[i].count; right_box[i] = acc; right_count[i] = accn; }
    for (int split = 0; split < kBins - 1; ++split) {
      const int lc = left_count[split], rc = right_count[split + 1];
      if (lc == 0 || rc == 0) continue;
      const double cost = left_box[split].Area() * lc + right_box[split + 1].Area() * rc;
      if (cost < best_cost) { best_cost = cost; best_axis = axis; best_split = split; }
    }
  }

  if (best_axis < 0) {
    // Degenerate centroids (coincident triangles): split by count instead.
    nodes_[static_cast<size_t>(node_index)].bmin = bounds.bmin;
    nodes_[static_cast<size_t>(node_index)].bmax = bounds.bmax;
    nodes_[static_cast<size_t>(node_index)].start = start;
    nodes_[static_cast<size_t>(node_index)].count = count;
    return node_index;
  }

  const double axis_min = cmin[best_axis], axis_ext = ext[best_axis];
  auto bin_of_axis = [&](const BvhTriangle& t) {
    const auto cen = Centroid(t);
    const double c = (&cen.x)[best_axis];
    int b = static_cast<int>((c - axis_min) / axis_ext * kBins);
    return std::clamp(b, 0, kBins - 1);
  };
  auto mid = std::partition(tris_.begin() + start, tris_.begin() + start + count,
                             [&](const BvhTriangle& t) { return bin_of_axis(t) <= best_split; });
  int left_count = static_cast<int>(mid - (tris_.begin() + start));
  if (left_count == 0 || left_count == count) left_count = count / 2;  // partition degenerated; force a split

  const int left = BuildRange(start, left_count, depth + 1);
  const int right = BuildRange(start + left_count, count - left_count, depth + 1);
  nodes_[static_cast<size_t>(node_index)].bmin = bounds.bmin;
  nodes_[static_cast<size_t>(node_index)].bmax = bounds.bmax;
  nodes_[static_cast<size_t>(node_index)].left = left;
  (void)right;  // right child is always left+1
  return node_index;
}

namespace {
// Möller-Trumbore ray-triangle intersection.
bool HitTriangle(const BvhTriangle& t, const Point3d& o, const Vector3d& d, float tmin, float tmax, float& out_t,
                 float& out_u, float& out_v) {
  const Vector3d e1 = t.v1 - t.v0, e2 = t.v2 - t.v0;
  const Vector3d p = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::fabs(det) < 1e-12) return false;
  const double inv_det = 1.0 / det;
  const Vector3d tv = o - t.v0;
  const double u = ON_DotProduct(tv, p) * inv_det;
  if (u < -1e-6 || u > 1.0 + 1e-6) return false;
  const Vector3d q = ON_CrossProduct(tv, e1);
  const double v = ON_DotProduct(d, q) * inv_det;
  if (v < -1e-6 || u + v > 1.0 + 1e-6) return false;
  const double tt = ON_DotProduct(e2, q) * inv_det;
  if (tt < tmin || tt > tmax) return false;
  out_t = static_cast<float>(tt); out_u = static_cast<float>(u); out_v = static_cast<float>(v);
  return true;
}
}  // namespace

bool Bvh::Intersect(const Point3d& origin, const Vector3d& dir, float tmin, float tmax, BvhHit& hit) const {
  if (nodes_.empty()) return false;
  const Vector3d inv_d(1.0 / dir.x, 1.0 / dir.y, 1.0 / dir.z);
  int stack[64]; int sp = 0;
  stack[sp++] = 0;
  bool found = false;
  float closest = tmax;
  while (sp > 0) {
    const Node& n = nodes_[static_cast<size_t>(stack[--sp])];
    if (!HitBox(n.bmin, n.bmax, origin, inv_d, tmin, closest)) continue;
    if (n.count > 0) {
      for (int i = 0; i < n.count; ++i) {
        const BvhTriangle& t = tris_[static_cast<size_t>(n.start + i)];
        float tt, u, v;
        if (HitTriangle(t, origin, dir, tmin, closest, tt, u, v)) {
          closest = tt; hit.t = tt; hit.u = u; hit.v = v; hit.tri = n.start + i; found = true;
        }
      }
    } else {
      stack[sp++] = n.left;
      stack[sp++] = n.left + 1;
    }
  }
  return found;
}

bool Bvh::IntersectAny(const Point3d& origin, const Vector3d& dir, float tmin, float tmax) const {
  if (nodes_.empty()) return false;
  const Vector3d inv_d(1.0 / dir.x, 1.0 / dir.y, 1.0 / dir.z);
  int stack[64]; int sp = 0;
  stack[sp++] = 0;
  while (sp > 0) {
    const Node& n = nodes_[static_cast<size_t>(stack[--sp])];
    if (!HitBox(n.bmin, n.bmax, origin, inv_d, tmin, tmax)) continue;
    if (n.count > 0) {
      for (int i = 0; i < n.count; ++i) {
        const BvhTriangle& t = tris_[static_cast<size_t>(n.start + i)];
        float tt, u, v;
        if (HitTriangle(t, origin, dir, tmin, tmax, tt, u, v)) return true;
      }
    } else {
      stack[sp++] = n.left;
      stack[sp++] = n.left + 1;
    }
  }
  return false;
}

void Bvh::ExportGpuNodes(std::vector<float>& out) const {
  out.clear();
  out.reserve(nodes_.size() * 12);
  for (const Node& n : nodes_) {
    out.push_back(static_cast<float>(n.bmin.x)); out.push_back(static_cast<float>(n.bmin.y));
    out.push_back(static_cast<float>(n.bmin.z)); out.push_back(static_cast<float>(n.left));
    out.push_back(static_cast<float>(n.bmax.x)); out.push_back(static_cast<float>(n.bmax.y));
    out.push_back(static_cast<float>(n.bmax.z)); out.push_back(static_cast<float>(n.count));
    out.push_back(static_cast<float>(n.start)); out.push_back(0.f); out.push_back(0.f); out.push_back(0.f);
  }
}

void Bvh::ExportGpuTriangles(std::vector<float>& out) const {
  out.clear();
  out.reserve(tris_.size() * 24);
  for (const BvhTriangle& t : tris_) {
    out.push_back(static_cast<float>(t.v0.x)); out.push_back(static_cast<float>(t.v0.y));
    out.push_back(static_cast<float>(t.v0.z)); out.push_back(static_cast<float>(t.material));
    out.push_back(static_cast<float>(t.v1.x)); out.push_back(static_cast<float>(t.v1.y));
    out.push_back(static_cast<float>(t.v1.z)); out.push_back(0.f);
    out.push_back(static_cast<float>(t.v2.x)); out.push_back(static_cast<float>(t.v2.y));
    out.push_back(static_cast<float>(t.v2.z)); out.push_back(0.f);
    out.push_back(static_cast<float>(t.n0.x)); out.push_back(static_cast<float>(t.n0.y));
    out.push_back(static_cast<float>(t.n0.z)); out.push_back(0.f);
    out.push_back(static_cast<float>(t.n1.x)); out.push_back(static_cast<float>(t.n1.y));
    out.push_back(static_cast<float>(t.n1.z)); out.push_back(0.f);
    out.push_back(static_cast<float>(t.n2.x)); out.push_back(static_cast<float>(t.n2.y));
    out.push_back(static_cast<float>(t.n2.z)); out.push_back(0.f);
  }
}

}  // namespace dino8::render
