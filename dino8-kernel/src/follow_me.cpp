#include "dino8/kernel/follow_me.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace dino8::kernel {

namespace {

struct V3 {
  double x = 0, y = 0, z = 0;
};

V3 FromPoint(const Point3d& p) { return V3{p.x, p.y, p.z}; }
V3 Add(const V3& a, const V3& b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(const V3& a, const V3& b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Mul(const V3& a, double s) { return V3{a.x * s, a.y * s, a.z * s}; }
double Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(const V3& a, const V3& b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(const V3& a) { return std::sqrt(Dot(a, a)); }
Point3d ToPoint(const V3& a) { return Point3d(a.x, a.y, a.z); }

// Newell normal (not unitized: its length is twice the polygon's area).
V3 Newell(const std::vector<V3>& poly) {
  V3 n;
  for (size_t i = 0; i < poly.size(); ++i) {
    const V3& a = poly[i];
    const V3& b = poly[(i + 1) % poly.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  return n;
}

[[noreturn]] void Refuse(const std::string& why) { throw std::invalid_argument("dino8::kernel::FollowMePolyline: " + why); }

}  // namespace

Brep FollowMePolyline(const std::vector<Point3d>& profile_in, const std::vector<Point3d>& path_in, bool closed_path, double tolerance) {
  if (profile_in.size() < 3) Refuse("the profile needs at least 3 corners");
  if (path_in.size() < 2 || (closed_path && path_in.size() < 3)) Refuse(closed_path ? "a closed path needs at least 3 corners" : "the path needs at least 2 corners");
  std::vector<V3> profile;
  for (const Point3d& p : profile_in) profile.push_back(FromPoint(p));
  std::vector<V3> corner;
  for (const Point3d& p : path_in) corner.push_back(FromPoint(p));

  const int n = static_cast<int>(corner.size());
  const int segments = closed_path ? n : n - 1;
  std::vector<V3> dir(static_cast<size_t>(segments));
  std::vector<double> seg_len(static_cast<size_t>(segments));
  for (int k = 0; k < segments; ++k) {
    const V3 e = Sub(corner[static_cast<size_t>((k + 1) % n)], corner[static_cast<size_t>(k)]);
    const double l = Len(e);
    if (!(l > 1e-12)) Refuse("segment " + std::to_string(k) + " of the path has zero length");
    seg_len[static_cast<size_t>(k)] = l;
    dir[static_cast<size_t>(k)] = Mul(e, 1.0 / l);
  }

  // the profile: planar, its plane perpendicular to the first segment, ordered counter-clockwise about the direction of travel
  V3 pn = Newell(profile);
  const double profile_double_area = Len(pn);
  if (!(profile_double_area > 1e-12)) Refuse("the profile has no area");
  pn = Mul(pn, 1.0 / profile_double_area);
  double size = 0;
  for (const V3& p : profile) size = std::max(size, Len(Sub(p, profile[0])));
  for (const V3& p : profile) {
    if (std::fabs(Dot(Sub(p, profile[0]), pn)) > 1e-9 * std::max(1.0, size)) Refuse("the profile is not planar");
  }
  const V3 d0 = dir[0];
  if (Len(Cross(pn, d0)) > std::max(tolerance, 1e-9)) Refuse("the profile plane is not perpendicular to the first segment of the path");
  if (Dot(pn, d0) < 0) std::reverse(profile.begin(), profile.end());

  // where the profile's plane meets the first segment
  const double t0 = Dot(Sub(profile[0], corner[0]), d0);
  if (t0 < -1e-9 || t0 >= seg_len[0] - 1e-9) Refuse("the profile's plane does not meet the first segment (it is " + std::to_string(t0) + " along a segment of length " + std::to_string(seg_len[0]) + ")");

  // the planes the generating lines are cut by, in travel order: the mitre planes of the corners after the first segment, then (open
  // path) the plane through the end perpendicular to the last segment or (closed path) the corner 0 mitre and the profile's own plane.
  struct Cut {
    V3 point, normal;
    int next_dir;  // the segment the lines follow after this cut (-1: none, the sweep ends)
  };
  std::vector<Cut> cuts;
  auto mitre = [&](int corner_index, int before, int after) {
    V3 m = Add(dir[static_cast<size_t>(before)], dir[static_cast<size_t>(after)]);
    const double l = Len(m);
    if (!(l > 1e-9)) Refuse("the path reverses direction at corner " + std::to_string(corner_index));
    return Cut{corner[static_cast<size_t>(corner_index)], Mul(m, 1.0 / l), after};
  };
  if (!closed_path) {
    for (int k = 1; k < n - 1; ++k) cuts.push_back(mitre(k, k - 1, k));
    cuts.push_back(Cut{corner[static_cast<size_t>(n - 1)], dir[static_cast<size_t>(segments - 1)], -1});
  } else {
    for (int k = 1; k < n; ++k) cuts.push_back(mitre(k, k - 1, k % n));
    cuts.push_back(mitre(0, n - 1, 0));
    cuts.push_back(Cut{profile[0], d0, -1});
  }

  // the sections: section 0 is the profile, section s + 1 is section s carried along its segment to cut s
  std::vector<std::vector<V3>> sections;
  sections.push_back(profile);
  int cur = 0;
  for (const Cut& c : cuts) {
    std::vector<V3> next;
    const double denom = Dot(dir[static_cast<size_t>(cur)], c.normal);
    if (!(denom > 1e-9)) Refuse("a mitre plane is parallel to the direction of travel");
    for (const V3& q : sections.back()) {
      const double t = Dot(Sub(c.point, q), c.normal) / denom;
      // a line that would have to run backwards to reach the cut means the cut crosses the sweep before the profile gets there
      if (t < -1e-9 * std::max(1.0, size)) Refuse("the profile is too large for the corner: a mitre plane is behind part of the profile");
      next.push_back(Add(q, Mul(dir[static_cast<size_t>(cur)], t)));
    }
    // a section whose normal turns against the direction of travel has folded over
    const V3 sn = Newell(next);
    if (!(Dot(sn, dir[static_cast<size_t>(cur)]) > 0.0) || Len(sn) < 0.25 * profile_double_area * 1e-6) {
      Refuse("the profile is too large for the corner: its mitred section folds over");
    }
    sections.push_back(std::move(next));
    if (c.next_dir >= 0) cur = c.next_dir;
  }
  if (closed_path) {
    // the sweep must come back onto the profile
    double worst = 0;
    for (size_t i = 0; i < profile.size(); ++i) worst = std::max(worst, Len(Sub(sections.back()[i], profile[i])));
    if (worst > 1e-7 * std::max(1.0, size)) Refuse("the path does not close: the profile comes back " + std::to_string(worst) + " away from where it started");
    sections.back() = profile;  // share the vertices exactly so that the faces weld
  }

  // faces
  std::vector<Brep::PlanarFace> faces;
  auto add_face = [&](std::vector<V3> loop_v, const V3& normal) {
    Brep::PlanarFace f;
    f.plane = ON_Plane(ToPoint(loop_v[0]), ON_3dVector(normal.x, normal.y, normal.z));
    for (const V3& p : loop_v) f.loop.push_back(ToPoint(p));
    faces.push_back(std::move(f));
  };
  const size_t m = profile.size();
  for (size_t s = 0; s + 1 < sections.size(); ++s) {
    for (size_t i = 0; i < m; ++i) {
      const size_t j = (i + 1) % m;
      const V3& a = sections[s][i];
      const V3& b = sections[s][j];
      const V3& c = sections[s + 1][j];
      const V3& d = sections[s + 1][i];
      V3 out = Cross(Sub(b, a), Sub(d, a));
      const double l = Len(out);
      if (!(l > 1e-14)) Refuse("a side face has no area");
      add_face({a, b, c, d}, Mul(out, 1.0 / l));
    }
  }
  if (!closed_path) {
    std::vector<V3> start(profile.rbegin(), profile.rend());
    add_face(start, Mul(d0, -1.0));
    add_face(sections.back(), dir[static_cast<size_t>(segments - 1)]);
  }
  return Brep::FromPlanarFaces(faces);
}

}  // namespace dino8::kernel
