// SubD::Subdivide() against an INDEPENDENT reference implementation of Catmull-Clark with the DeRose-Kass-Truong (1998) semi-sharp
// rules, written out here on a plain vertex / face / edge-sharpness structure (no OpenNURBS):
//   face point          F = centroid of the face;
//   edge point          smooth: (v0 + v1 + F0 + F1) / 4,  sharp (s >= 1): (v0 + v1) / 2,  0 < s < 1: the blend (1 - s) smooth + s sharp;
//   vertex point        k = number of incident edges with s > 0:
//                         k <= 1: smooth rule (F + 2 R + (n - 3) v) / n   (F mean of the incident face points, R mean of the incident
//                                 edge midpoints, n the valence),
//                         k == 2: crease vertex: C = (a + 6 v + b) / 8 over the far ends of the two sharp edges, blended with the
//                                 smooth rule by the mean sharpness: (1 - m) smooth + m C with m = min(mean s, 1),
//                         k >= 3: corner, the vertex stays;
//   child sharpness     both halves of an edge of sharpness s get max(s - 1, 0).
// The library's output after three rounds is compared vertex by vertex (nearest point) with this reference.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "dino8/kernel/mesh.h"
#include "dino8/kernel/subd.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Point3d;
using dino8::kernel::SubD;

struct V3 {
  double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(double s, V3 a) { return {s * a.x, s * a.y, s * a.z}; }
double Len(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

using EdgeKey = std::pair<int, int>;
EdgeKey Key(int a, int b) { return a < b ? EdgeKey(a, b) : EdgeKey(b, a); }

struct RefCage {
  std::vector<V3> v;
  std::vector<std::vector<int>> faces;
  std::map<EdgeKey, double> sharp;  // edges with s > 0
};

RefCage RefSubdivide(const RefCage& in) {
  const int nv = static_cast<int>(in.v.size());
  // face points
  std::vector<V3> fp(in.faces.size());
  for (size_t f = 0; f < in.faces.size(); ++f) {
    V3 c;
    for (int i : in.faces[f]) c = c + in.v[i];
    fp[f] = (1.0 / in.faces[f].size()) * c;
  }
  // edge -> faces
  std::map<EdgeKey, std::vector<int>> edge_faces;
  for (size_t f = 0; f < in.faces.size(); ++f) {
    const auto& fv = in.faces[f];
    for (size_t i = 0; i < fv.size(); ++i) edge_faces[Key(fv[i], fv[(i + 1) % fv.size()])].push_back(static_cast<int>(f));
  }
  const auto sharp_of = [&in](const EdgeKey& k) {
    const auto it = in.sharp.find(k);
    return it == in.sharp.end() ? 0.0 : it->second;
  };
  RefCage out;
  out.v = in.v;  // vertex points are overwritten below
  // edge points
  std::map<EdgeKey, int> edge_index;
  for (const auto& kv : edge_faces) {
    const EdgeKey k = kv.first;
    const V3 mid = 0.5 * (in.v[k.first] + in.v[k.second]);
    V3 smooth = mid;
    if (kv.second.size() == 2) smooth = 0.25 * (in.v[k.first] + in.v[k.second] + fp[kv.second[0]] + fp[kv.second[1]]);
    const double s = std::min(sharp_of(k), 1.0);
    edge_index[k] = static_cast<int>(out.v.size());
    out.v.push_back((1.0 - s) * smooth + s * mid);
  }
  // vertex points
  std::vector<std::vector<int>> vfaces(nv);
  std::vector<std::vector<int>> vnbrs(nv);
  for (size_t f = 0; f < in.faces.size(); ++f)
    for (int i : in.faces[f]) vfaces[i].push_back(static_cast<int>(f));
  for (const auto& kv : edge_faces) {
    vnbrs[kv.first.first].push_back(kv.first.second);
    vnbrs[kv.first.second].push_back(kv.first.first);
  }
  for (int i = 0; i < nv; ++i) {
    const int n = static_cast<int>(vnbrs[i].size());
    V3 F, R;
    for (int f : vfaces[i]) F = F + fp[f];
    F = (1.0 / vfaces[i].size()) * F;
    for (int j : vnbrs[i]) R = R + 0.5 * (in.v[i] + in.v[j]);
    R = (1.0 / n) * R;
    const V3 smooth = (1.0 / n) * (F + 2.0 * R + (n - 3.0) * in.v[i]);
    std::vector<int> sharp_far;
    double sum_s = 0.0;
    for (int j : vnbrs[i]) {
      const double s = sharp_of(Key(i, j));
      if (s > 0.0) {
        sharp_far.push_back(j);
        sum_s += s;
      }
    }
    V3 p = smooth;
    if (sharp_far.size() == 2) {
      const V3 crease = (1.0 / 8.0) * (in.v[sharp_far[0]] + 6.0 * in.v[i] + in.v[sharp_far[1]]);
      const double m = std::min(sum_s / 2.0, 1.0);
      p = (1.0 - m) * smooth + m * crease;
    } else if (sharp_far.size() >= 3) {
      p = in.v[i];
    }
    out.v[i] = p;
  }
  // faces and child sharpness
  for (size_t f = 0; f < in.faces.size(); ++f) {
    const auto& fv = in.faces[f];
    const int fi = static_cast<int>(out.v.size());
    out.v.push_back(fp[f]);
    for (size_t i = 0; i < fv.size(); ++i) {
      const int a = fv[i], b = fv[(i + 1) % fv.size()], z = fv[(i + fv.size() - 1) % fv.size()];
      out.faces.push_back({a, edge_index[Key(a, b)], fi, edge_index[Key(z, a)]});
    }
  }
  for (const auto& kv : in.sharp) {
    const double child = std::max(kv.second - 1.0, 0.0);
    if (child <= 0.0) continue;
    const int e = edge_index[kv.first];
    out.sharp[Key(kv.first.first, e)] = child;
    out.sharp[Key(kv.first.second, e)] = child;
  }
  return out;
}

dino8::kernel::Mesh RefBoxMesh() {  // cube [-1, 1]^3, same vertex / face order as RefBoxCage()
  dino8::kernel::Mesh m;
  ON_Mesh& raw = m.raw();
  const double s[2] = {-1.0, 1.0};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) raw.m_V.Append(ON_3fPoint(float(s[x]), float(s[y]), float(s[z])));
  const auto q = [&raw](int a, int b, int c, int d) {
    ON_MeshFace f;
    f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d;
    raw.m_F.Append(f);
  };
  q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(0, 4, 6, 2); q(1, 3, 7, 5); q(4, 5, 7, 6);
  return m;
}

RefCage RefBoxCage() {
  RefCage c;
  const double s[2] = {-1.0, 1.0};
  for (int z = 0; z < 2; ++z)
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) c.v.push_back({s[x], s[y], s[z]});
  c.faces = {{0, 2, 3, 1}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}, {4, 5, 7, 6}};
  return c;
}

struct Comparison {
  size_t reference_vertices = 0, library_vertices = 0;
  double worst = 0.0;       // largest distance from a reference vertex to the nearest library control point
  double worst_x = 0, worst_y = 0, worst_z = 0;
};

Comparison Compare(const RefCage& ref, const SubD& lib) {
  Comparison c;
  std::vector<V3> pts;
  for (const dino8::kernel::SubDLimitPoint& lp : lib.LimitPoints()) pts.push_back({lp.control_point.x, lp.control_point.y, lp.control_point.z});
  c.reference_vertices = ref.v.size();
  c.library_vertices = pts.size();
  for (const V3& r : ref.v) {
    double best = 1e300;
    for (const V3& p : pts) best = std::min(best, Len(r - p));
    if (best > c.worst) {
      c.worst = best;
      c.worst_x = r.x; c.worst_y = r.y; c.worst_z = r.z;
    }
  }
  return c;
}

}  // namespace

namespace {
struct Edge { int a, b; Point3d pa, pb; };
const Edge kE67 = {6, 7, Point3d(-1, 1, 1), Point3d(1, 1, 1)};    // top, back
const Edge kE75 = {7, 5, Point3d(1, 1, 1), Point3d(1, -1, 1)};    // top, right
const Edge kE73 = {7, 3, Point3d(1, 1, 1), Point3d(1, 1, -1)};    // vertical edge at (1, 1)

struct Case {
  const char* name;
  std::vector<std::pair<Edge, double>> edges;
};

void Setup(const Case& cs, RefCage* ref, SubD* lib) {
  *ref = RefBoxCage();
  *lib = SubD::FromControlMesh(RefBoxMesh());
  for (const auto& e : cs.edges) {
    ref->sharp[Key(e.first.a, e.first.b)] = e.second;
    lib->SetEdgeSharpness(e.first.pa, e.first.pb, e.second, 1e-6);
  }
}
}  // namespace

D8_EXTRA_TEST(SubDSubdivideMatchesTheDeRoseSemiSharpReferenceVertexForVertex) {
  const Case cases[] = {
      {"one edge, sharpness 3", {{kE67, 3.0}}},
      {"one edge, sharpness 1 (the crease threshold)", {{kE67, 1.0}}},
      {"one edge, sharpness 0.5 (blend)", {{kE67, 0.5}}},
      {"two-edge chain 3,3", {{kE67, 3.0}, {kE75, 3.0}}},
      {"two-edge chain 2,0.5", {{kE67, 2.0}, {kE75, 0.5}}},
      {"two-edge chain 0.5,0.5", {{kE67, 0.5}, {kE75, 0.5}}},
      {"two-edge chain 1,2", {{kE67, 1.0}, {kE75, 2.0}}},
      {"corner of three sharp edges 2,2,2", {{kE67, 2.0}, {kE75, 2.0}, {kE73, 2.0}}},
      {"corner of three sharp edges 2,1,0.5", {{kE67, 2.0}, {kE75, 1.0}, {kE73, 0.5}}},
  };
  for (const Case& cs : cases) {
    RefCage ref;
    SubD lib = SubD::FromControlMesh(RefBoxMesh());
    Setup(cs, &ref, &lib);
    double worst = 0.0;
    bool counts_equal = true;
    for (int r = 1; r <= 3; ++r) {
      ref = RefSubdivide(ref);
      lib.Subdivide(1);
      const Comparison c = Compare(ref, lib);
      worst = std::max(worst, c.worst);
      counts_equal = counts_equal && c.reference_vertices == c.library_vertices;
    }
    char what[220];
    std::snprintf(what, sizeof what, "%s: after 1, 2 and 3 rounds every reference vertex is matched by a library vertex (worst distance %.2e, 386 vertices at round 3)", cs.name, worst);
    d8test::Check(worst < 1e-9 && counts_equal, what);
  }
}

D8_EXTRA_TEST(SubDSemiSharpEdgeVerticesLeaveTheStraightEdgeBecauseTheChainEndsMove) {
  // A single sharpness-3 vertical edge from (1,1,-1) to (1,1,1). Its two END vertices have only one sharp edge, so the DeRose rule
  // gives them the SMOOTH vertex rule: they move inward in round 1. The chain's middle vertex (at the edge midpoint (1,1,0) after
  // round 1) then uses the crease rule (a + 6 v + b) / 8 over those moved neighbours, so it leaves the straight line from round 2 on.
  // That is the rule, not a defect: the reference implementation reproduces the library's position.
  RefCage ref;
  SubD lib = SubD::FromControlMesh(RefBoxMesh());
  Setup({"", {{kE73, 3.0}}}, &ref, &lib);
  ref = RefSubdivide(ref);
  lib.Subdivide(1);
  V3 best1{};
  for (const V3& p : ref.v) if (std::abs(p.z) < 1e-12 && std::abs(p.x - 1.0) < 1e-12 && std::abs(p.y - 1.0) < 1e-12) best1 = p;
  d8test::Check(Len(best1 - V3{1, 1, 0}) < 1e-12, "round 1: the middle of the sharp edge is exactly the midpoint (1, 1, 0) (sharp edge point = mean of its ends)");
  ref = RefSubdivide(ref);
  lib.Subdivide(1);
  V3 drift{};
  double best = 1e300;
  for (const V3& p : ref.v) {
    if (std::abs(p.z) < 1e-12 && p.x > 0.5 && std::abs(p.x - p.y) < 1e-12 && Len(p - V3{1, 1, 0}) < best) { best = Len(p - V3{1, 1, 0}); drift = p; }
  }
  d8test::Check(drift.x < 0.95 && drift.x > 0.8, "round 2: the reference moves that vertex inward off the straight edge");
  double nearest = 1e300;
  for (const dino8::kernel::SubDLimitPoint& lp : lib.LimitPoints()) nearest = std::min(nearest, Len(V3{lp.control_point.x, lp.control_point.y, lp.control_point.z} - drift));
  char what[200];
  std::snprintf(what, sizeof what, "round 2: the library has a vertex at the same place (%.6f, %.6f, %.6f), distance %.1e", drift.x, drift.y, drift.z, nearest);
  d8test::Check(nearest < 1e-9, what);
}

D8_EXTRA_TEST(SubDSemiSharpReferenceCanTellRightFromWrong) {
  // Detection power of the comparison: a reference that ignores the sharpness is far from the library with it.
  RefCage plain = RefBoxCage();
  SubD lib = SubD::FromControlMesh(RefBoxMesh());
  lib.SetEdgeSharpness(kE67.pa, kE67.pb, 3.0, 1e-6);
  for (int r = 0; r < 3; ++r) {
    plain = RefSubdivide(plain);
    lib.Subdivide(1);
  }
  d8test::Check(Compare(plain, lib).worst > 0.1, "a reference without the semi-sharp edge is more than 0.1 away from the library's three-round result");
  // and a reference whose crease rule is wrong (smooth rule for crease vertices: sharpness 0.4 instead of 3) is also far away
  RefCage wrong = RefBoxCage();
  wrong.sharp[Key(kE67.a, kE67.b)] = 0.4;
  for (int r = 0; r < 3; ++r) wrong = RefSubdivide(wrong);
  d8test::Check(Compare(wrong, lib).worst > 0.05, "a reference with the wrong sharpness (0.4 for 3) is more than 0.05 away");
}
