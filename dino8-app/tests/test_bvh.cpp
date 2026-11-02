// BVH vs brute force. The path tracer's BVH (src/render/Bvh.cpp) must return
// exactly what a scan over every triangle returns. It once did not: the
// builder put a node's right child after the whole LEFT SUBTREE while
// traversal (and the GPU export) read it at `left + 1`, so any scene with
// more than a few triangles lost most of its right-hand subtrees and a
// primary ray aimed straight at a triangle hit it about 1 time in 70.
// Deterministic (fixed LCG); prints the mismatch counts and exits non-zero on
// any difference.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "render/Bvh.h"

using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
using dino8::render::Bvh;
using dino8::render::BvhHit;
using dino8::render::BvhTriangle;

namespace {
struct Lcg {
  std::uint64_t s = 0x9e3779b97f4a7c15ull;
  double Next() {  // [0, 1)
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<double>(s >> 11) / static_cast<double>(1ull << 53);
  }
  double Range(double a, double b) { return a + (b - a) * Next(); }
};

// Independent Moller-Trumbore (written here, not shared with Bvh.cpp).
bool Hit(const BvhTriangle& t, const Point3d& o, const Vector3d& d, double tmin, double tmax, double& out_t) {
  const Vector3d e1 = t.v1 - t.v0, e2 = t.v2 - t.v0;
  const Vector3d p(d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x);
  const double det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
  if (std::fabs(det) < 1e-12) return false;
  const double inv = 1.0 / det;
  const Vector3d tv = o - t.v0;
  const double u = (tv.x * p.x + tv.y * p.y + tv.z * p.z) * inv;
  if (u < -1e-6 || u > 1.0 + 1e-6) return false;
  const Vector3d q(tv.y * e1.z - tv.z * e1.y, tv.z * e1.x - tv.x * e1.z, tv.x * e1.y - tv.y * e1.x);
  const double v = (d.x * q.x + d.y * q.y + d.z * q.z) * inv;
  if (v < -1e-6 || u + v > 1.0 + 1e-6) return false;
  const double tt = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv;
  if (tt < tmin || tt > tmax) return false;
  out_t = tt;
  return true;
}
}  // namespace

int main() {
  Lcg rng;
  int failures = 0;
  for (int count : {3, 9, 40, 500, 4000}) {
    std::vector<BvhTriangle> tris;
    for (int i = 0; i < count; ++i) {
      BvhTriangle t;
      const Point3d c(rng.Range(-50, 50), rng.Range(-50, 50), rng.Range(-50, 50));
      auto jitter = [&]() { return Vector3d(rng.Range(-2, 2), rng.Range(-2, 2), rng.Range(-2, 2)); };
      t.v0 = c + jitter();
      t.v1 = c + jitter();
      t.v2 = c + jitter();
      t.material = 0;
      t.object = 0;
      tris.push_back(t);
    }
    const std::vector<BvhTriangle> original = tris;
    Bvh bvh;
    bvh.Build(tris);

    int rays = 0, hits = 0, hit_mismatch = 0, t_mismatch = 0, any_mismatch = 0;
    for (int r = 0; r < 3000; ++r) {
      const Point3d o(rng.Range(-120, 120), rng.Range(-120, 120), rng.Range(-120, 120));
      Vector3d d;
      if (r % 2 == 0) {  // aimed at a triangle's centroid: must hit something
        const BvhTriangle& t = original[static_cast<std::size_t>(rng.Next() * count) % original.size()];
        d = Point3d((t.v0.x + t.v1.x + t.v2.x) / 3, (t.v0.y + t.v1.y + t.v2.y) / 3, (t.v0.z + t.v1.z + t.v2.z) / 3) - o;
      } else {
        d = Vector3d(rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(-1, 1));
      }
      if (!d.Unitize()) continue;
      ++rays;
      double best = 1e30;
      bool brute = false;
      for (const BvhTriangle& t : bvh.Triangles()) {
        double tt;
        if (Hit(t, o, d, 1e-4, best, tt)) { best = tt; brute = true; }
      }
      BvhHit h;
      const bool found = bvh.Intersect(o, d, 1e-4f, 1e30f, h);
      const bool any = bvh.IntersectAny(o, d, 1e-4f, 1e30f);
      if (found != brute) ++hit_mismatch;
      else if (found && std::fabs(static_cast<double>(h.t) - best) > 1e-3 * (1.0 + best)) ++t_mismatch;
      if (any != brute) ++any_mismatch;
      if (brute) ++hits;
    }
    std::printf("triangles=%d rays=%d brute_force_hits=%d hit/miss mismatches=%d closest-t mismatches=%d any-hit mismatches=%d\n", count, rays,
                hits, hit_mismatch, t_mismatch, any_mismatch);
    if (hit_mismatch || t_mismatch || any_mismatch) ++failures;
    if (count >= 40 && hits < rays / 4) {
      std::printf("FAIL: only %d of %d rays hit anything in the brute-force scan (the test scene is wrong)\n", hits, rays);
      ++failures;
    }
  }
  if (failures) {
    std::printf("FAIL: %d BVH configuration(s) disagree with the brute-force scan\n", failures);
    return 1;
  }
  std::printf("OK: the BVH matches a scan over every triangle for 3..4000 triangles\n");
  return 0;
}
