#include "drafting/SectionView.h"

namespace dino8::app::drafting {

// Mirrors cmd_viewtools.cpp's file-local SliceMesh (segments -> chains);
// duplicated here so this module has no dependency on that command file.
std::vector<std::vector<kernel::Point3d>> SliceMeshToChains(const ON_Mesh& m, const ON_Plane& plane, double tol) {
  std::vector<std::pair<kernel::Point3d, kernel::Point3d>> segs;
  const int fc = m.FaceCount();
  for (int fi = 0; fi < fc; ++fi) {
    const ON_MeshFace& f = m.m_F[fi];
    const int n = f.IsTriangle() ? 3 : 4;
    for (int tri = 0; tri < (n == 4 ? 2 : 1); ++tri) {
      int idx[3] = {f.vi[0], f.vi[tri + 1], f.vi[tri + 2]};
      kernel::Point3d p[3];
      double d[3];
      for (int k = 0; k < 3; ++k) { p[k] = m.Vertex(idx[k]); d[k] = plane.DistanceTo(p[k]); }
      std::vector<kernel::Point3d> hits;
      for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        if ((d[k] < 0 && d[j] >= 0) || (d[k] >= 0 && d[j] < 0)) {
          const double t = d[k] / (d[k] - d[j]);
          hits.push_back(p[k] + (p[j] - p[k]) * t);
        }
      }
      if (hits.size() == 2 && hits[0].DistanceTo(hits[1]) > tol) segs.emplace_back(hits[0], hits[1]);
    }
  }
  std::vector<std::vector<kernel::Point3d>> out;
  std::vector<bool> used(segs.size(), false);
  for (size_t i = 0; i < segs.size(); ++i) {
    if (used[i]) continue;
    used[i] = true;
    std::vector<kernel::Point3d> pl = {segs[i].first, segs[i].second};
    bool grew = true;
    while (grew) {
      grew = false;
      for (size_t j = 0; j < segs.size(); ++j) {
        if (used[j]) continue;
        if (segs[j].first.DistanceTo(pl.back()) <= tol) { pl.push_back(segs[j].second); used[j] = true; grew = true; }
        else if (segs[j].second.DistanceTo(pl.back()) <= tol) { pl.push_back(segs[j].first); used[j] = true; grew = true; }
        else if (segs[j].second.DistanceTo(pl.front()) <= tol) { pl.insert(pl.begin(), segs[j].first); used[j] = true; grew = true; }
        else if (segs[j].first.DistanceTo(pl.front()) <= tol) { pl.insert(pl.begin(), segs[j].second); used[j] = true; grew = true; }
      }
    }
    out.push_back(std::move(pl));
  }
  return out;
}

}  // namespace dino8::app::drafting
