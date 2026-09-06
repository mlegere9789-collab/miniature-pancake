// The last catalogue commands that were still help-only placeholders:
// curve conversion/matching, tween/developable/extruded-surface solids,
// thickness and continuity analysis, mesh clean-up, layer utilities,
// window layouts and a handful of honest Partial stubs for features that
// depend on subsystems Dino 8 does not have (IGES/STEP readers, a script
// editor, UV editing). Registered right before the curve-edit commands.
#include "commands/cmd_common.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include "app/Settings.h"
#include "imgui.h"
#include "io/File3dm.h"
#include "ui/Panels.h"

namespace dino8::app {
namespace {

using Row = std::vector<Point3d>;
using Face = std::array<int, 4>;

// ---------------------------------------------------------------------------
// Small shared helpers (copies of the ones the other cmd_*.cpp files keep
// in their own anonymous namespaces).
// ---------------------------------------------------------------------------

std::string Lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

bool ParseNum(const std::string& t, double& v) {
  if (t.empty()) return false;
  char* end = nullptr;
  v = std::strtod(t.c_str(), &end);
  return end && *end == 0;
}

bool YesNo(const std::string& v, bool current) {
  const std::string l = Lower(v);
  if (l.empty()) return !current;
  return l == "yes" || l == "y" || l == "true" || l == "1" || l == "on";
}

// Drains the "Name=Value" tokens left on the command line into a map
// (lower-case names). Tokens without '=' are returned in `plain`.
using Opts = std::map<std::string, std::string>;
Opts TakeOptions(CommandContext& ctx, std::vector<std::string>* plain = nullptr) {
  Opts o;
  while (auto tok = ctx.Engine().TakePendingInput()) {
    const size_t eq = tok->find('=');
    if (eq == std::string::npos) { if (plain) plain->push_back(*tok); continue; }
    o[Lower(tok->substr(0, eq))] = tok->substr(eq + 1);
  }
  return o;
}
double OptNum(const Opts& o, const char* name, double def) { auto it = o.find(Lower(name)); double v; return it != o.end() && ParseNum(it->second, v) ? v : def; }
std::string OptStr(const Opts& o, const char* name, const std::string& def) { auto it = o.find(Lower(name)); return it != o.end() ? it->second : def; }
bool OptYes(const Opts& o, const char* name, bool def) { auto it = o.find(Lower(name)); return it != o.end() ? YesNo(it->second, def) : def; }

kernel::Brep WrapBrep(ON_Brep* b) {
  kernel::Brep k;
  if (b) { k.raw() = *b; delete b; }
  return k;
}

std::vector<double> ArcLengthParams(const kernel::NurbsCurve& c, int n, bool wrap) {
  std::vector<double> t;
  const kernel::Interval d = c.Domain();
  const double len = c.Length();
  const int segs = wrap ? n : n - 1;
  for (int i = 0; i < n; ++i) {
    if (i == 0) t.push_back(d.min);
    else if (i == n - 1 && !wrap) t.push_back(d.max);
    else if (len <= 0) t.push_back(d.min + (d.max - d.min) * i / segs);
    else t.push_back(c.ParameterAtArcLength(len * i / segs));
  }
  return t;
}

Row SampleCurve(const kernel::NurbsCurve& c, int n, bool wrap) {
  Row pts;
  for (double t : ArcLengthParams(c, n, wrap)) pts.push_back(c.PointAt(t));
  return pts;
}

Point3d Centroid(const Row& pts) {
  Point3d c(0, 0, 0);
  if (pts.empty()) return c;
  for (const Point3d& p : pts) c = c + p;
  return c / static_cast<double>(pts.size());
}

// Cubic curve interpolating `pts` (chord-length parameters, relaxation solve).
kernel::NurbsCurve InterpolateCubic(const std::vector<Point3d>& pts, bool closed = false) {
  if (pts.size() <= 2) return PolylineCurve(pts);
  ON_3dPointArray arr;
  for (const Point3d& p : pts) arr.Append(p);
  if (closed) arr.Append(pts.front());
  ON_NurbsCurve nc;
  if (!nc.CreateClampedUniformNurbs(3, 3, arr.Count(), arr.Array())) return PolylineCurve(pts);
  kernel::NurbsCurve k;
  k.raw() = nc;
  for (int iter = 0; iter < 40; ++iter) {
    for (int i = 0; i < arr.Count(); ++i) {
      double t = k.raw().Domain().ParameterAt(static_cast<double>(i) / (arr.Count() - 1));
      Point3d on = k.raw().PointAt(t);
      Point3d cv;
      k.raw().GetCV(i, cv);
      k.raw().SetCV(i, cv + (arr[i] - on));
    }
  }
  return k;
}

// Degree-3 surface through a grid of sample rows (rows[i][j], i along U).
kernel::NurbsSurface SurfaceThroughRows(const std::vector<Row>& rows) {
  const int nu = static_cast<int>(rows.size()), nv = static_cast<int>(rows[0].size());
  std::vector<Point3d> grid;
  grid.reserve(static_cast<size_t>(nu) * nv);
  for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) grid.push_back(rows[i][j]);
  int du = std::min(3, nu - 1), dv = std::min(3, nv - 1);
  kernel::NurbsSurface s = kernel::NurbsSurface::FromControlGrid(grid, nu, nv, du, dv);
  for (int it = 0; it < 12; ++it) {
    for (int i = 0; i < nu; ++i)
      for (int j = 0; j < nv; ++j) {
        double u = s.raw().Domain(0).ParameterAt(nu == 1 ? 0 : static_cast<double>(i) / (nu - 1));
        double v = s.raw().Domain(1).ParameterAt(nv == 1 ? 0 : static_cast<double>(j) / (nv - 1));
        Point3d on = s.raw().PointAt(u, v);
        ON_3dPoint cv;
        s.raw().GetCV(i, j, cv);
        s.raw().SetCV(i, j, cv + (rows[i][j] - on));
      }
  }
  return s;
}

// Degree-3 NURBS surface whose control points are rows[v][u].
kernel::NurbsSurface SurfaceFromRows(std::vector<Row> rows, bool periodic_u = false) {
  const int deg = 3;
  if (periodic_u) for (Row& r : rows) { const Row copy = r; for (int k = 0; k < deg; ++k) r.push_back(copy[static_cast<size_t>(k) % copy.size()]); }
  const int nv = static_cast<int>(rows.size()), nu = static_cast<int>(rows[0].size());
  std::vector<Point3d> grid(static_cast<size_t>(nu) * static_cast<size_t>(nv));
  for (int u = 0; u < nu; ++u) for (int v = 0; v < nv; ++v) grid[static_cast<size_t>(u) * nv + v] = rows[static_cast<size_t>(v)][static_cast<size_t>(u)];
  const int du = periodic_u ? deg : std::min(deg, nu - 1), dv = std::min(deg, nv - 1);
  kernel::NurbsSurface s = kernel::NurbsSurface::FromControlGrid(grid, nu, nv, du, dv);
  if (periodic_u) s.raw().MakePeriodicUniformKnotVector(0);
  return s;
}

// A moving frame along a rail: origin, unit tangent, normal, binormal.
struct Frame {
  Point3d o;
  Vector3d t, n, b;
  Point3d Place(Vector3d local) const { return o + t * local.x + n * local.y + b * local.z; }
};

Vector3d Perpendicular(Vector3d t, Vector3d hint) {
  Vector3d n = hint - t * ON_DotProduct(hint, t);
  if (!n.Unitize()) {
    n = ON_CrossProduct(t, ON_zaxis);
    if (!n.Unitize()) { n = ON_CrossProduct(t, ON_xaxis); n.Unitize(); }
  }
  return n;
}

Vector3d Rotate(Vector3d v, Vector3d axis, double angle) {
  return v * std::cos(angle) + ON_CrossProduct(axis, v) * std::sin(angle) + axis * (ON_DotProduct(axis, v) * (1 - std::cos(angle)));
}

// Rotation-minimizing frames (double-reflection method).
std::vector<Frame> RmfFrames(const kernel::NurbsCurve& rail, const std::vector<double>& params, Vector3d normal_hint, bool close) {
  std::vector<Frame> f;
  for (double t : params) {
    Frame fr;
    fr.o = rail.PointAt(t);
    fr.t = rail.TangentAt(t);
    if (!fr.t.Unitize()) fr.t = f.empty() ? Vector3d(0, 0, 1) : f.back().t;
    f.push_back(fr);
  }
  f[0].n = Perpendicular(f[0].t, normal_hint);
  f[0].b = ON_CrossProduct(f[0].t, f[0].n);
  for (size_t i = 1; i < f.size(); ++i) {
    const Vector3d v1 = f[i].o - f[i - 1].o;
    const double c1 = ON_DotProduct(v1, v1);
    Vector3d rl = f[i - 1].n, tl = f[i - 1].t;
    if (c1 > 1e-20) { rl = rl - v1 * (2 / c1 * ON_DotProduct(v1, rl)); tl = tl - v1 * (2 / c1 * ON_DotProduct(v1, tl)); }
    const Vector3d v2 = f[i].t - tl;
    const double c2 = ON_DotProduct(v2, v2);
    Vector3d r = rl;
    if (c2 > 1e-20) r = r - v2 * (2 / c2 * ON_DotProduct(v2, r));
    r = Perpendicular(f[i].t, r);
    f[i].n = r;
    f[i].b = ON_CrossProduct(f[i].t, r);
  }
  if (close && f.size() > 2) {
    const Frame& last = f.back();
    const double twist = std::atan2(ON_DotProduct(f[0].t, ON_CrossProduct(last.n, f[0].n)), ON_DotProduct(last.n, f[0].n));
    const double count = static_cast<double>(f.size() - 1);
    for (size_t i = 1; i < f.size(); ++i) {
      f[i].n = Rotate(f[i].n, f[i].t, twist * static_cast<double>(i) / count);
      f[i].n = Perpendicular(f[i].t, f[i].n);
      f[i].b = ON_CrossProduct(f[i].t, f[i].n);
    }
    f.back() = f[0];
  }
  return f;
}

// Plain editable mesh (faces store four indices; a triangle repeats its last).
struct RawMesh {
  std::vector<Point3d> v;
  std::vector<Face> f;
  static bool IsTri(const Face& f) { return f[2] == f[3]; }
  static int Corners(const Face& f) { return IsTri(f) ? 3 : 4; }
  static Face Tri(int a, int b, int c) { return {a, b, c, c}; }
  static Face Quad(int a, int b, int c, int d) { return {a, b, c, d}; }
  static Face Flipped(const Face& f) { return IsTri(f) ? Tri(f[0], f[2], f[1]) : Quad(f[0], f[3], f[2], f[1]); }
  double FaceArea(const Face& f) const {
    double a = ON_CrossProduct(v[f[1]] - v[f[0]], v[f[2]] - v[f[0]]).Length() / 2;
    if (!IsTri(f)) a += ON_CrossProduct(v[f[2]] - v[f[0]], v[f[3]] - v[f[0]]).Length() / 2;
    return a;
  }
  Point3d FaceCenter(const Face& f) const {
    const int n = Corners(f);
    Vector3d s(0, 0, 0);
    for (int i = 0; i < n; ++i) s += v[f[i]] - Point3d::Origin;
    return Point3d::Origin + s / n;
  }
};

RawMesh Unpack(const ON_Mesh& m) {
  RawMesh r;
  r.v.reserve(m.VertexCount());
  for (int i = 0; i < m.VertexCount(); ++i) r.v.push_back(m.Vertex(i));
  r.f.reserve(m.FaceCount());
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    r.f.push_back({f.vi[0], f.vi[1], f.vi[2], f.vi[3]});
  }
  return r;
}

kernel::Mesh Pack(const RawMesh& r) {
  kernel::Mesh km;
  ON_Mesh& m = km.raw();
  std::vector<int> remap(r.v.size(), -1);
  int nv = 0;
  std::vector<Face> faces;
  for (const Face& f0 : r.f) {
    Face f = f0;
    bool ok = true;
    for (int k = 0; k < 4; ++k) if (f[k] < 0 || f[k] >= static_cast<int>(r.v.size())) ok = false;
    if (!ok) continue;
    if (!RawMesh::IsTri(f)) {
      if (f[0] == f[1]) f = RawMesh::Tri(f[1], f[2], f[3]);
      else if (f[1] == f[2]) f = RawMesh::Tri(f[0], f[1], f[3]);
      else if (f[0] == f[3]) f = RawMesh::Tri(f[0], f[1], f[2]);
    }
    if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2]) continue;
    for (int k = 0; k < 4; ++k) if (remap[f[k]] < 0) { remap[f[k]] = nv; m.SetVertex(nv, r.v[f[k]]); ++nv; }
    faces.push_back({remap[f[0]], remap[f[1]], remap[f[2]], remap[f[3]]});
  }
  int fi = 0;
  for (const Face& f : faces) {
    if (RawMesh::IsTri(f)) m.SetTriangle(fi++, f[0], f[1], f[2]);
    else m.SetQuad(fi++, f[0], f[1], f[2], f[3]);
  }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  return km;
}

// Union-find over vertex indices, used by the welding/collapsing commands.
struct UnionFind {
  std::vector<int> p;
  explicit UnionFind(size_t n) : p(n) { for (size_t i = 0; i < n; ++i) p[i] = static_cast<int>(i); }
  int Find(int a) { while (p[a] != a) { p[a] = p[p[a]]; a = p[a]; } return a; }
  void Unite(int a, int b) { a = Find(a); b = Find(b); if (a != b) p[b] = a; }
};

// Rewrites faces through a vertex map; merged groups take the average position.
RawMesh ApplyMerge(const RawMesh& in, UnionFind& uf) {
  RawMesh out = in;
  std::vector<Vector3d> sum(in.v.size(), Vector3d(0, 0, 0));
  std::vector<int> cnt(in.v.size(), 0);
  for (size_t i = 0; i < in.v.size(); ++i) { const int r = uf.Find(static_cast<int>(i)); sum[r] += in.v[i] - Point3d::Origin; ++cnt[r]; }
  for (size_t i = 0; i < in.v.size(); ++i) { const int r = uf.Find(static_cast<int>(i)); out.v[i] = Point3d::Origin + sum[r] / cnt[r]; }
  for (Face& f : out.f) for (int k = 0; k < 4; ++k) f[k] = uf.Find(f[k]);
  return out;
}

// Directed boundary edges (used by exactly one face, in that face's direction).
std::vector<std::pair<int, int>> BoundaryEdges(const RawMesh& r) {
  std::map<std::pair<int, int>, int> uses;
  for (const Face& f : r.f) {
    const int n = RawMesh::Corners(f);
    for (int k = 0; k < n; ++k) { const int a = f[k], b = f[(k + 1) % n]; if (a != b) uses[{std::min(a, b), std::max(a, b)}]++; }
  }
  std::vector<std::pair<int, int>> out;
  for (const Face& f : r.f) {
    const int n = RawMesh::Corners(f);
    for (int k = 0; k < n; ++k) { const int a = f[k], b = f[(k + 1) % n]; if (a != b && uses[{std::min(a, b), std::max(a, b)}] == 1) out.emplace_back(a, b); }
  }
  return out;
}

kernel::Mesh Outward(kernel::Mesh m) {
  if (m.IsClosedManifold() && m.Volume() < 0) return m.FlipNormals();
  return m;
}

// Sweeps a cap mesh through `stations` (transforms of the cap; the first is
// applied to the bottom) into a closed solid: bottom cap (flipped), top cap,
// and wall quads from the boundary edges. With `apex` set the walls are
// triangles to that point and there is no top cap.
kernel::Mesh SweepCap(const RawMesh& cap, const std::vector<ON_Xform>& stations, const Point3d* apex) {
  RawMesh out;
  const int nv = static_cast<int>(cap.v.size());
  const std::vector<std::pair<int, int>> bnd = BoundaryEdges(cap);
  const size_t ns = apex ? 1 : stations.size();
  for (size_t s = 0; s < ns; ++s) for (const Point3d& p : cap.v) { Point3d q = p; q.Transform(stations[s]); out.v.push_back(q); }
  for (const Face& f : cap.f) out.f.push_back(RawMesh::Flipped(f));  // bottom
  if (apex) {
    out.v.push_back(*apex);
    const int ai = static_cast<int>(out.v.size()) - 1;
    for (const auto& e : bnd) out.f.push_back(RawMesh::Tri(e.first, e.second, ai));
  } else {
    const int top = static_cast<int>(ns - 1) * nv;
    for (const Face& f : cap.f) out.f.push_back(RawMesh::IsTri(f) ? RawMesh::Tri(f[0] + top, f[1] + top, f[2] + top) : RawMesh::Quad(f[0] + top, f[1] + top, f[2] + top, f[3] + top));
    for (size_t s = 0; s + 1 < ns; ++s) {
      const int o0 = static_cast<int>(s) * nv, o1 = o0 + nv;
      for (const auto& e : bnd) out.f.push_back(RawMesh::Quad(e.first + o0, e.second + o0, e.second + o1, e.first + o1));
    }
  }
  return Outward(Pack(out));
}

// Slices a mesh with a plane, returning joined polylines.
std::vector<Row> SliceMesh(const ON_Mesh& m, const ON_Plane& plane, double tol) {
  std::vector<std::pair<Point3d, Point3d>> segs;
  const int fc = m.FaceCount();
  for (int fi = 0; fi < fc; ++fi) {
    const ON_MeshFace& f = m.m_F[fi];
    const int n = f.IsTriangle() ? 3 : 4;
    for (int tri = 0; tri < (n == 4 ? 2 : 1); ++tri) {
      int idx[3] = {f.vi[0], f.vi[tri + 1], f.vi[tri + 2]};
      Point3d p[3];
      double d[3];
      for (int k = 0; k < 3; ++k) { p[k] = m.Vertex(idx[k]); d[k] = plane.DistanceTo(p[k]); }
      Row hits;
      for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        if ((d[k] < 0 && d[j] >= 0) || (d[k] >= 0 && d[j] < 0)) hits.push_back(p[k] + (p[j] - p[k]) * (d[k] / (d[k] - d[j])));
      }
      if (hits.size() == 2 && hits[0].DistanceTo(hits[1]) > tol) segs.emplace_back(hits[0], hits[1]);
    }
  }
  std::vector<Row> out;
  std::vector<bool> used(segs.size(), false);
  for (size_t i = 0; i < segs.size(); ++i) {
    if (used[i]) continue;
    used[i] = true;
    Row pl = {segs[i].first, segs[i].second};
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

// ---- document helpers -----------------------------------------------------

// Snapshot of an object's attributes (never keep a SceneObject* across Add/Remove).
struct Attrs {
  int layer = -1;
  Color color;
  bool by_layer = true;
  std::string material, linetype = "ByLayer", name;
  int group = -1;
  std::map<std::string, std::string> user_text;
  static Attrs Of(const SceneObject& o) { return {o.layer_index, o.color, o.color_by_layer, o.material_name, o.linetype, o.name, o.group_id, o.user_text}; }
  void Apply(SceneObject& o) const { o.layer_index = layer; o.color = color; o.color_by_layer = by_layer; o.material_name = material; o.linetype = linetype; o.group_id = group; o.user_text = user_text; }
};

ObjectId AddLike(CommandContext& ctx, SceneObject obj, const Attrs& like) { like.Apply(obj); return ctx.Doc().Add(std::move(obj)); }

// Replaces object `id` with a new curve carrying the same attributes.
ObjectId ReplaceCurve(CommandContext& ctx, ObjectId id, const kernel::NurbsCurve& c) {
  const SceneObject* o = ctx.Doc().Find(id);
  if (!o) return kNoObject;
  const Attrs a = Attrs::Of(*o);
  ctx.Doc().Remove(id);
  SceneObject n = SceneObject::MakeCurve(c);
  n.name = a.name;
  return AddLike(ctx, std::move(n), a);
}

ObjectId ReplaceMesh(CommandContext& ctx, ObjectId id, const kernel::Mesh& m) {
  const SceneObject* o = ctx.Doc().Find(id);
  if (!o) return kNoObject;
  const Attrs a = Attrs::Of(*o);
  ctx.Doc().Remove(id);
  SceneObject n = SceneObject::MakeMesh(m);
  n.name = a.name;
  return AddLike(ctx, std::move(n), a);
}

// The NURBS surface of a surface object or a single-face brep.
std::optional<kernel::NurbsSurface> SurfaceOf(const SceneObject& o) {
  if (o.kind == ObjectKind::Surface && o.surface) return *o.surface;
  if (o.kind == ObjectKind::Brep && o.brep && o.brep->raw().m_F.Count() >= 1) {
    const ON_Surface* s = o.brep->raw().m_F[0].SurfaceOf();
    kernel::NurbsSurface k;
    if (s && SurfaceFromON(*s, k)) return k;
  }
  return std::nullopt;
}

// The curve of a curve object; for a brep/surface the naked/first edge nearest `near`.
std::optional<kernel::NurbsCurve> CurveOf(const SceneObject& o, const Point3d* near_pt = nullptr) {
  if (o.kind == ObjectKind::Curve && o.curve) return *o.curve;
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    double best = std::numeric_limits<double>::max();
    std::optional<kernel::NurbsCurve> out;
    for (int i = 0; i < b.m_E.Count(); ++i) {
      const ON_BrepEdge& e = b.m_E[i];
      if (e.m_edge_index < 0) continue;
      kernel::NurbsCurve k;
      if (!CurveFromON(e, k)) continue;
      const double d = near_pt ? k.ClosestPoint(*near_pt, 40).DistanceTo(*near_pt) : 0;
      if (d < best) { best = d; out = k; if (!near_pt) break; }
    }
    return out;
  }
  if (o.kind == ObjectKind::Surface && o.surface) {
    // Border edges of an untrimmed surface: the four isocurves.
    const ON_NurbsSurface& s = o.surface->raw();
    double best = std::numeric_limits<double>::max();
    std::optional<kernel::NurbsCurve> out;
    for (int side = 0; side < 4; ++side) {
      const int dir = side < 2 ? 1 : 0;
      const double c = side % 2 == 0 ? s.Domain(1 - dir).Min() : s.Domain(1 - dir).Max();
      ON_Curve* iso = s.IsoCurve(dir, c);
      if (!iso) continue;
      kernel::NurbsCurve k;
      const bool ok = CurveFromON(*iso, k);
      delete iso;
      if (!ok) continue;
      const double d = near_pt ? k.ClosestPoint(*near_pt, 40).DistanceTo(*near_pt) : 0;
      if (d < best) { best = d; out = k; if (!near_pt) break; }
    }
    return out;
  }
  return std::nullopt;
}

std::vector<std::pair<ObjectId, kernel::NurbsCurve>> CurvesIn(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<std::pair<ObjectId, kernel::NurbsCurve>> out;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Curve && o->curve) out.emplace_back(id, *o->curve);
  return out;
}

// Ray/mesh helpers for the point-driven commands.
Point3d BoxCenter(const kernel::BoundingBox& b) { return Point3d((b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, (b.min.z + b.max.z) / 2); }

// ---------------------------------------------------------------------------
// Reusable command shapes
// ---------------------------------------------------------------------------

// Select objects, then a number (typed, or the distance to a picked point
// from the selection's centre); options are Name=Value tokens.
class ObjectsNumberCommand : public Command {
 public:
  using Build = std::function<void(CommandContext&, const std::vector<ObjectId>&, double, const Opts&)>;
  ObjectsNumberCommand(std::string prompt, std::string number_prompt, double def, Build build, std::vector<OptionSpec> opts = {}, bool allow_zero = false)
      : prompt_(std::move(prompt)), nprompt_(std::move(number_prompt)), def_(def), build_(std::move(build)), specs_(std::move(opts)), allow_zero_(allow_zero) {}
  void Begin(CommandContext&) override { options = specs_; WantObjects(prompt_); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (ids_.empty()) { ctx.Warn("Nothing selected"); Finish(); return; }
    ctx.Doc().BoundingBoxOf(ids_, bbox_);
    WantNumber(nprompt_ + " <" + FormatNumber(def_) + ">", def_);
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    for (OptionSpec& o : options) if (o.name == n) { o.value = o.toggle ? (YesNo(v, Lower(o.value) == "yes") ? "Yes" : "No") : v; opts_[Lower(n)] = o.value; }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNum(t, v)) OnNumber(ctx, v); else ctx.Warn("Expected a number"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { OnNumber(ctx, p.DistanceTo(BoxCenter(bbox_))); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!allow_zero_ && v == 0) { ctx.Warn("Value must be non-zero"); return; }
    ctx.ClearPreview();
    build_(ctx, ids_, v, opts_);
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Number) OnNumber(ctx, def_); }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  std::string prompt_, nprompt_;
  double def_;
  Build build_;
  std::vector<OptionSpec> specs_;
  bool allow_zero_;
  std::vector<ObjectId> ids_;
  Opts opts_;
  kernel::BoundingBox bbox_{};
};

// Select objects, then type a text (a layer name, "u,v"...).
class ObjectsTextCommand : public Command {
 public:
  using Build = std::function<void(CommandContext&, const std::vector<ObjectId>&, const std::string&)>;
  ObjectsTextCommand(std::string prompt, std::string text_prompt, Build build, int min = 1) : prompt_(std::move(prompt)), tprompt_(std::move(text_prompt)), build_(std::move(build)), min_(min) {}
  void Begin(CommandContext&) override { WantObjects(prompt_, min_); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (ids_.empty()) { ctx.Warn("Nothing selected"); Finish(); return; }
    WantText(tprompt_);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { build_(ctx, ids_, t); Finish(); }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Text) { build_(ctx, ids_, ""); Finish(); } }

 private:
  std::string prompt_, tprompt_;
  Build build_;
  int min_;
  std::vector<ObjectId> ids_;
};

// Select objects, then pick one or more points (Enter ends).
class ObjectsPointsCommand : public Command {
 public:
  using Build = std::function<void(CommandContext&, const std::vector<ObjectId>&, const std::vector<Point3d>&)>;
  ObjectsPointsCommand(std::string prompt, std::string point_prompt, int count, Build build) : prompt_(std::move(prompt)), pprompt_(std::move(point_prompt)), count_(count), build_(std::move(build)) {}
  void Begin(CommandContext&) override { WantObjects(prompt_); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (ids_.empty()) { ctx.Warn("Nothing selected"); Finish(); return; }
    WantPoint(pprompt_);
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (count_ > 0 && static_cast<int>(pts_.size()) >= count_) { build_(ctx, ids_, pts_); Finish(); return; }
    WantPoint(pprompt_ + " (Enter when done)");
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Point && !pts_.empty()) { build_(ctx, ids_, pts_); Finish(); } }

 private:
  std::string prompt_, pprompt_;
  int count_;
  Build build_;
  std::vector<ObjectId> ids_;
  std::vector<Point3d> pts_;
};

// Two selection stages (targets, then a second set), like ExtrudeCrvAlongCrv.
class TwoSelectionsCommand : public Command {
 public:
  using Build = std::function<void(CommandContext&, const std::vector<ObjectId>&, const std::vector<ObjectId>&)>;
  TwoSelectionsCommand(std::string p1, std::string p2, Build build) : p1_(std::move(p1)), p2_(std::move(p2)), build_(std::move(build)) {}
  void Begin(CommandContext&) override { WantObjects(p1_); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!first_done_) {
      first_ = ids;
      if (first_.empty()) { ctx.Warn("Nothing selected"); Finish(); return; }
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      first_done_ = true;
      accept_preselection = false;
      WantObjects(p2_);
      return;
    }
    build_(ctx, first_, ids);
    Finish();
  }

 private:
  std::string p1_, p2_;
  Build build_;
  std::vector<ObjectId> first_;
  bool first_done_ = false;
};

// ---------------------------------------------------------------------------
// Curve commands
// ---------------------------------------------------------------------------

// Convert: curves to polylines (Output=Lines) or arc chains (Output=Arcs).
void ConvertCurves(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  const Opts o = TakeOptions(ctx);
  const bool arcs = Lower(OptStr(o, "Output", "Lines")) == "arcs";
  const double tol = OptNum(o, "Tolerance", std::max(ctx.Settings().absolute_tolerance * 10, 0.01));
  ctx.Doc().BeginChange("Convert");
  int made = 0, segments = 0;
  for (const auto& [id, c] : CurvesIn(ctx, ids)) {
    std::vector<double> params = c.SuggestedParameterValues(tol, 12);
    if (params.size() < 2) continue;
    if (!arcs) {
      Row pts;
      for (double t : params) pts.push_back(c.PointAt(t));
      if (ReplaceCurve(ctx, id, PolylineCurve(pts)) != kNoObject) { ++made; segments += static_cast<int>(pts.size()) - 1; }
      continue;
    }
    // Arcs: every pair of consecutive spans becomes one arc through three points.
    ON_PolyCurve pc;
    for (size_t i = 0; i + 1 < params.size(); i += 2) {
      const size_t k = std::min(i + 2, params.size() - 1);
      const Point3d p0 = c.PointAt(params[i]), p2 = c.PointAt(params[k]);
      const Point3d p1 = c.PointAt(k == i + 1 ? 0.5 * (params[i] + params[k]) : params[i + 1]);
      ON_Arc arc(p0, p1, p2);
      ON_Curve* seg = nullptr;
      if (arc.IsValid() && arc.radius > tol) seg = new ON_ArcCurve(arc);
      else seg = new ON_LineCurve(p0, p2);
      pc.Append(seg);
    }
    kernel::NurbsCurve k;
    if (pc.Count() > 0 && CurveFromON(pc, k) && ReplaceCurve(ctx, id, k) != kNoObject) { ++made; segments += pc.Count(); }
  }
  ctx.Print("Convert: " + std::to_string(made) + " curve(s) to " + (arcs ? "arcs" : "lines") + ", " + std::to_string(segments) + " segment(s), tolerance " + FormatNumber(tol));
}

// MatchCrvDir: flips curves so they run the same way as the last selected one.
void MatchCrvDir(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  auto curves = CurvesIn(ctx, ids);
  if (curves.size() < 2) { ctx.Warn("Select at least two curves; the last one is the reference"); return; }
  const kernel::NurbsCurve ref = curves.back().second;
  curves.pop_back();
  ctx.Doc().BeginChange("MatchCrvDir");
  int flipped = 0;
  for (auto& [id, c] : curves) {
    // Compare the tangent at the curve start with the reference tangent at its closest point.
    const Point3d s = c.PointAt(c.Domain().min);
    Vector3d tr = ref.TangentAt(ref.ClosestPointParameter(s));
    Vector3d tc = c.TangentAt(c.Domain().min);
    if (!c.IsClosed()) {
      // Open curves: also use the end-to-end direction (robust for curved pairs).
      const Vector3d dr = ref.PointAt(ref.Domain().max) - ref.PointAt(ref.Domain().min);
      const Vector3d dc = c.PointAt(c.Domain().max) - s;
      if (dr.Length() > 1e-9 && dc.Length() > 1e-9) { tr = dr; tc = dc; }
    }
    if (ON_DotProduct(tr, tc) < 0) { c.Reverse(); ReplaceCurve(ctx, id, c); ++flipped; }
  }
  ctx.Print("MatchCrvDir: " + std::to_string(flipped) + " of " + std::to_string(curves.size()) + " curve(s) reversed to match the reference");
}

// OffsetNormal: offsets a curve lying on a surface along the surface normal.
void OffsetNormal(CommandContext& ctx, const std::vector<ObjectId>& ids, double d, const Opts&) {
  std::optional<kernel::NurbsSurface> srf;
  std::vector<std::pair<ObjectId, kernel::NurbsCurve>> curves;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Curve && o->curve) curves.emplace_back(id, *o->curve);
    else if (!srf) srf = SurfaceOf(*o);
  }
  if (!srf || curves.empty()) { ctx.Warn("Select a curve and the surface it lies on"); return; }
  ctx.Doc().BeginChange("OffsetNormal");
  int made = 0;
  for (const auto& [id, c] : curves) {
    const bool closed = c.IsClosed();
    Row pts;
    for (const Point3d& p : SampleCurve(c, closed ? 48 : 40, closed)) {
      const kernel::Point2d uv = srf->ClosestPointParameter(p, 24, 24);
      Vector3d n = srf->NormalAt(uv.x, uv.y);
      if (!n.Unitize()) continue;
      pts.push_back(p + n * d);
    }
    if (pts.size() < 2) continue;
    const SceneObject* o = ctx.Doc().Find(id);
    AddLike(ctx, SceneObject::MakeCurve(c.Degree() == 1 && !closed ? PolylineCurve(pts) : InterpolateCubic(pts, closed)), Attrs::Of(*o));
    ++made;
  }
  ctx.Print("OffsetNormal: " + std::to_string(made) + " curve(s) offset " + FormatNumber(d) + " along the surface normal");
}

// PointCloudContour / PointCloudSection: points within a band around a
// plane, ordered around their centroid into a closed polyline.
int ContourPoints(CommandContext& ctx, const Row& pts, const ON_Plane& plane, double band, const Attrs& like) {
  Row in;
  for (const Point3d& p : pts) if (std::fabs(plane.DistanceTo(p)) <= band) in.push_back(plane.ClosestPointTo(p));
  if (in.size() < 3) return 0;
  const Point3d c = Centroid(in);
  std::sort(in.begin(), in.end(), [&](const Point3d& a, const Point3d& b) {
    double ua, va, ub, vb;
    plane.ClosestPointTo(a, &ua, &va); plane.ClosestPointTo(b, &ub, &vb);
    double uc, vc;
    plane.ClosestPointTo(c, &uc, &vc);
    return std::atan2(va - vc, ua - uc) < std::atan2(vb - vc, ub - uc);
  });
  in.push_back(in.front());
  SceneObject n = SceneObject::MakeCurve(PolylineCurve(in));
  n.name = "Contour";
  AddLike(ctx, std::move(n), like);
  return 1;
}

Row PointsOf(CommandContext& ctx, const std::vector<ObjectId>& ids, Attrs* like) {
  Row pts;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) { if (like && pts.empty()) *like = Attrs::Of(*o); pts.push_back(o->point); }
  return pts;
}

void PointCloudContour(CommandContext& ctx, const std::vector<ObjectId>& ids, double spacing, const Opts& o) {
  Attrs like;
  const Row pts = PointsOf(ctx, ids, &like);
  if (pts.size() < 3) { ctx.Warn("Select the points of the cloud (at least three)"); return; }
  const Vector3d n = ActiveNormal(ctx);
  double lo = std::numeric_limits<double>::max(), hi = -lo;
  for (const Point3d& p : pts) { const double d = ON_DotProduct(p - Point3d::Origin, n); lo = std::min(lo, d); hi = std::max(hi, d); }
  const double band = OptNum(o, "Band", spacing / 2);
  ctx.Doc().BeginChange("PointCloudContour");
  int made = 0, planes = 0;
  for (double h = lo + band; h <= hi + 1e-9; h += spacing) {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(Point3d::Origin + n * h);
    made += ContourPoints(ctx, pts, pl, band, like);
    ++planes;
  }
  ctx.Print("PointCloudContour: " + std::to_string(made) + " contour(s) from " + std::to_string(planes) + " plane(s) " + FormatNumber(spacing) + " apart (band " + FormatNumber(band) + ")");
}

void PointCloudSection(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<Point3d>& picks) {
  Attrs like;
  const Row pts = PointsOf(ctx, ids, &like);
  if (pts.size() < 3) { ctx.Warn("Select the points of the cloud (at least three)"); return; }
  ON_Plane pl = ActivePlane(ctx);
  pl.SetOrigin(picks.front());
  double band = ctx.Settings().absolute_tolerance * 100;
  // Band: a tenth of the cloud's extent along the normal when the cloud is sparse.
  double lo = std::numeric_limits<double>::max(), hi = -lo;
  for (const Point3d& p : pts) { const double d = ON_DotProduct(p - Point3d::Origin, pl.zaxis); lo = std::min(lo, d); hi = std::max(hi, d); }
  band = std::max(band, (hi - lo) * 0.05);
  ctx.Doc().BeginChange("PointCloudSection");
  const int made = ContourPoints(ctx, pts, pl, band, like);
  ctx.Print("PointCloudSection: " + std::to_string(made) + " section curve(s) through " + FormatPoint(picks.front()) + " (band " + FormatNumber(band) + ")");
}

// RebuildCrvNonUniform: refits a curve with knots placed where it bends.
void RebuildCrvNonUniform(CommandContext& ctx, const std::vector<ObjectId>& ids, double tol, const Opts&) {
  ctx.Doc().BeginChange("RebuildCrvNonUniform");
  int made = 0;
  for (const auto& [id, c] : CurvesIn(ctx, ids)) {
    std::vector<double> params = c.SuggestedParameterValues(tol, 10);
    if (params.size() < 2) continue;
    const bool closed = c.IsClosed();
    Row pts;
    for (double t : params) pts.push_back(c.PointAt(t));
    // Non-uniform clamped cubic: knots at the chord-length positions of the samples.
    const int n = static_cast<int>(pts.size());
    kernel::NurbsCurve k;
    if (n < 4) k = PolylineCurve(pts);
    else {
      std::vector<double> chord(pts.size(), 0.0);
      for (size_t i = 1; i < pts.size(); ++i) chord[i] = chord[i - 1] + pts[i].DistanceTo(pts[i - 1]);
      ON_NurbsCurve nc;
      nc.Create(3, false, 4, n);
      for (int i = 0; i < n; ++i) nc.SetCV(i, pts[static_cast<size_t>(i)]);
      // Knot vector (order 4, n CVs): n+2 knots; interior knots average the chord parameters.
      const int nk = nc.KnotCount();
      for (int i = 0; i < 3; ++i) { nc.SetKnot(i, 0.0); nc.SetKnot(nk - 1 - i, chord.back()); }
      for (int i = 3; i < nk - 3; ++i) {
        const int j = i - 2;  // averaging of consecutive chord parameters
        nc.SetKnot(i, (chord[static_cast<size_t>(j)] + chord[static_cast<size_t>(j + 1)] + chord[static_cast<size_t>(std::min(j + 2, n - 1))]) / 3.0);
      }
      k.raw() = nc;
      for (int it = 0; it < 40; ++it)
        for (int i = 0; i < n; ++i) {
          const double t = chord[static_cast<size_t>(i)];
          const Point3d on = k.raw().PointAt(t);
          Point3d cv;
          k.raw().GetCV(i, cv);
          k.raw().SetCV(i, cv + (pts[static_cast<size_t>(i)] - on));
        }
    }
    double dev = 0;
    for (const Point3d& p : SampleCurve(c, 50, closed)) dev = std::max(dev, k.ClosestPoint(p, 200).DistanceTo(p));
    if (ReplaceCurve(ctx, id, k) != kNoObject) { ++made; ctx.Print("RebuildCrvNonUniform: " + std::to_string(k.ControlPointCount()) + " control points, max deviation " + FormatNumber(dev)); }
  }
  ctx.Print("RebuildCrvNonUniform: " + std::to_string(made) + " curve(s) rebuilt (tolerance " + FormatNumber(tol) + ")");
}

// RibbonOffset: offset in the CPlane plus a ruled surface between the curves.
void RibbonOffset(CommandContext& ctx, const std::vector<ObjectId>& ids, double d, const Opts&) {
  const Vector3d n = ActiveNormal(ctx);
  ctx.Doc().BeginChange("RibbonOffset");
  int made = 0;
  for (const auto& [id, c] : CurvesIn(ctx, ids)) {
    const bool closed = c.IsClosed();
    const std::vector<double> params = ArcLengthParams(c, closed ? 64 : 48, closed);
    Row pts;
    for (double t : params) {
      Vector3d tg = c.TangentAt(t);
      Vector3d side = ON_CrossProduct(n, tg);
      if (!side.Unitize()) continue;
      pts.push_back(c.PointAt(t) + side * d);
    }
    if (pts.size() < 2) continue;
    kernel::NurbsCurve off = c.Degree() == 1 && !closed ? PolylineCurve(pts) : InterpolateCubic(pts, closed);
    const Attrs a = Attrs::Of(*ctx.Doc().Find(id));
    AddLike(ctx, SceneObject::MakeCurve(off), a);
    ON_NurbsSurface rs;
    if (rs.CreateRuledSurface(c.raw(), off.raw())) { kernel::NurbsSurface k; k.raw() = rs; AddLike(ctx, SceneObject::MakeSurface(k), a); }
    ++made;
  }
  ctx.Print("RibbonOffset: " + std::to_string(made) + " ribbon(s) of width " + FormatNumber(std::fabs(d)));
}

// ShortPath: Dijkstra over the surface's tessellation between the closest
// points to two picks, then smoothed and re-projected onto the surface.
void ShortPath(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<Point3d>& picks) {
  std::optional<kernel::NurbsSurface> srf;
  Attrs like;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if ((srf = SurfaceOf(*o))) { like = Attrs::Of(*o); break; }
  if (!srf || picks.size() < 2) { ctx.Warn("Select a surface and pick two points on it"); return; }
  const int N = 40;
  kernel::Mesh grid = srf->TessellateGrid(N, N);
  const ON_Mesh& m = grid.raw();
  const int nv = m.VertexCount();
  if (nv < 4) { ctx.Warn("ShortPath: could not tessellate the surface"); return; }
  std::vector<std::vector<int>> adj(static_cast<size_t>(nv));
  for (int fi = 0; fi < m.FaceCount(); ++fi) {
    const ON_MeshFace& f = m.m_F[fi];
    const int n = f.IsTriangle() ? 3 : 4;
    for (int k = 0; k < n; ++k) { const int a = f.vi[k], b = f.vi[(k + 1) % n]; adj[static_cast<size_t>(a)].push_back(b); adj[static_cast<size_t>(b)].push_back(a); }
  }
  auto nearest = [&](Point3d p) { int best = 0; double bd = 1e300; for (int i = 0; i < nv; ++i) { const double d = m.Vertex(i).DistanceTo(p); if (d < bd) { bd = d; best = i; } } return best; };
  const int s = nearest(srf->ClosestPoint(picks[0], 30, 30)), t = nearest(srf->ClosestPoint(picks[1], 30, 30));
  std::vector<double> dist(static_cast<size_t>(nv), 1e300);
  std::vector<int> prev(static_cast<size_t>(nv), -1);
  std::vector<bool> done(static_cast<size_t>(nv), false);
  dist[static_cast<size_t>(s)] = 0;
  for (int it = 0; it < nv; ++it) {
    int u = -1; double bd = 1e300;
    for (int i = 0; i < nv; ++i) if (!done[static_cast<size_t>(i)] && dist[static_cast<size_t>(i)] < bd) { bd = dist[static_cast<size_t>(i)]; u = i; }
    if (u < 0 || u == t) break;
    done[static_cast<size_t>(u)] = true;
    for (int w : adj[static_cast<size_t>(u)]) {
      const double nd = bd + m.Vertex(u).DistanceTo(m.Vertex(w));
      if (nd < dist[static_cast<size_t>(w)]) { dist[static_cast<size_t>(w)] = nd; prev[static_cast<size_t>(w)] = u; }
    }
  }
  if (prev[static_cast<size_t>(t)] < 0 && s != t) { ctx.Warn("ShortPath: no path found"); return; }
  Row path;
  for (int v = t; v >= 0; v = prev[static_cast<size_t>(v)]) { path.push_back(m.Vertex(v)); if (v == s) break; }
  std::reverse(path.begin(), path.end());
  path.front() = srf->ClosestPoint(picks[0], 30, 30);
  path.back() = srf->ClosestPoint(picks[1], 30, 30);
  // Smooth the interior (Laplacian with re-projection) to shorten the polyline.
  for (int it = 0; it < 8 && path.size() > 2; ++it)
    for (size_t i = 1; i + 1 < path.size(); ++i) path[i] = srf->ClosestPoint((path[i - 1] + path[i + 1]) * 0.5, 20, 20);
  double len = 0;
  for (size_t i = 1; i < path.size(); ++i) len += path[i].DistanceTo(path[i - 1]);
  ctx.Doc().BeginChange("ShortPath");
  AddLike(ctx, SceneObject::MakeCurve(path.size() > 3 ? InterpolateCubic(path) : PolylineCurve(path)), like);
  ctx.Print("ShortPath: " + std::to_string(path.size()) + " point(s), length " + FormatNumber(len) + " (straight-line distance " + FormatNumber(path.front().DistanceTo(path.back())) + ")");
}

// AlignProfiles: reverses / re-seams curves so a Loft through them does not twist.
void AlignProfiles(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  auto curves = CurvesIn(ctx, ids);
  if (curves.size() < 2) { ctx.Warn("Select at least two profile curves"); return; }
  ctx.Doc().BeginChange("AlignProfiles");
  kernel::NurbsCurve ref = curves.front().second;
  int reversed = 0, reseamed = 0;
  const double tol = ctx.Settings().absolute_tolerance;
  for (size_t i = 1; i < curves.size(); ++i) {
    kernel::NurbsCurve& c = curves[i].second;
    bool changed = false;
    const Point3d rs = ref.PointAt(ref.Domain().min);
    if (c.IsClosed()) {
      const double t = c.ClosestPointParameter(rs, 200);
      if (c.PointAt(t).DistanceTo(c.PointAt(c.Domain().min)) > tol * 10) { if (c.raw().ChangeClosedCurveSeam(t)) { ++reseamed; changed = true; } }
      // Direction: compare tangents at the (new) seam with the reference start tangent.
      if (ON_DotProduct(c.TangentAt(c.Domain().min), ref.TangentAt(ref.Domain().min)) < 0) { c.Reverse(); ++reversed; changed = true; }
    } else {
      const Point3d cs = c.PointAt(c.Domain().min), ce = c.PointAt(c.Domain().max);
      if (ce.DistanceTo(rs) < cs.DistanceTo(rs)) { c.Reverse(); ++reversed; changed = true; }
    }
    if (changed) ReplaceCurve(ctx, curves[i].first, c);
    ref = c;
  }
  ctx.Print("AlignProfiles: " + std::to_string(reversed) + " curve(s) reversed, " + std::to_string(reseamed) + " seam(s) moved to match the first profile");
}

// ---------------------------------------------------------------------------
// Surface commands
// ---------------------------------------------------------------------------

Row SurfaceGridRow(const kernel::NurbsSurface& s, int i, int nu, int nv) {
  Row r;
  for (int j = 0; j < nv; ++j) {
    const double u = s.Domain(0).min + (s.Domain(0).max - s.Domain(0).min) * i / (nu - 1);
    const double v = s.Domain(1).min + (s.Domain(1).max - s.Domain(1).min) * j / (nv - 1);
    r.push_back(s.PointAt(u, v));
  }
  return r;
}

void TweenSurfaces(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  const Opts o = TakeOptions(ctx);
  const int count = std::max(1, static_cast<int>(OptNum(o, "Count", 1)));
  std::vector<kernel::NurbsSurface> srfs;
  Attrs like;
  for (ObjectId id : ids) if (const SceneObject* oo = ctx.Doc().Find(id)) if (auto s = SurfaceOf(*oo)) { if (srfs.empty()) like = Attrs::Of(*oo); srfs.push_back(*s); }
  if (srfs.size() != 2) { ctx.Warn("Select exactly two surfaces"); return; }
  const int nu = 12, nv = 12;
  // Orient the second surface's grid to match the first (flip U/V/transposed as needed).
  std::vector<Row> a, b;
  for (int i = 0; i < nu; ++i) { a.push_back(SurfaceGridRow(srfs[0], i, nu, nv)); b.push_back(SurfaceGridRow(srfs[1], i, nu, nv)); }
  auto cost = [&](const std::vector<Row>& g) { double c = 0; for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) c += a[i][j].DistanceTo(g[i][j]); return c; };
  std::vector<std::vector<Row>> variants = {b};
  { std::vector<Row> t = b; std::reverse(t.begin(), t.end()); variants.push_back(t); }
  { std::vector<Row> t = b; for (Row& r : t) std::reverse(r.begin(), r.end()); variants.push_back(t); }
  { std::vector<Row> t = b; std::reverse(t.begin(), t.end()); for (Row& r : t) std::reverse(r.begin(), r.end()); variants.push_back(t); }
  { std::vector<Row> t(nu, Row(nv)); for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) t[i][j] = b[j][i]; variants.push_back(t); }
  { std::vector<Row> t(nu, Row(nv)); for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) t[i][j] = b[j][nv - 1 - i]; variants.push_back(t); }
  { std::vector<Row> t(nu, Row(nv)); for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) t[i][j] = b[nu - 1 - j][i]; variants.push_back(t); }
  { std::vector<Row> t(nu, Row(nv)); for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) t[i][j] = b[nu - 1 - j][nv - 1 - i]; variants.push_back(t); }
  size_t best = 0;
  for (size_t k = 1; k < variants.size(); ++k) if (cost(variants[k]) < cost(variants[best])) best = k;
  b = variants[best];
  ctx.Doc().BeginChange("TweenSurfaces");
  for (int k = 1; k <= count; ++k) {
    const double f = static_cast<double>(k) / (count + 1);
    std::vector<Row> rows(nu, Row(nv));
    for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) rows[i][j] = a[i][j] + (b[i][j] - a[i][j]) * f;
    AddLike(ctx, SceneObject::MakeSurface(SurfaceThroughRows(rows)), like);
  }
  ctx.Print("TweenSurfaces: " + std::to_string(count) + " surface(s) between the two inputs");
}

// DevLoft: ruled surface between two curves whose rulings are chosen so
// the tangent planes match along each ruling (developable criterion).
void DevLoft(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  auto curves = CurvesIn(ctx, ids);
  if (curves.size() != 2) { ctx.Warn("Select two rail curves"); return; }
  const kernel::NurbsCurve& A = curves[0].second;
  kernel::NurbsCurve B = curves[1].second;
  // Match directions.
  if (A.PointAt(A.Domain().min).DistanceTo(B.PointAt(B.Domain().max)) < A.PointAt(A.Domain().min).DistanceTo(B.PointAt(B.Domain().min))) B.Reverse();
  const int na = 40, nb = 200;
  const std::vector<double> ta = ArcLengthParams(A, na, false), tb = ArcLengthParams(B, nb, false);
  Row ra, rb;
  int j = 0;
  double twist_sum = 0;
  for (int i = 0; i < na; ++i) {
    const Point3d pa = A.PointAt(ta[static_cast<size_t>(i)]);
    const Vector3d tga = A.TangentAt(ta[static_cast<size_t>(i)]);
    // Monotone search on B: the ruling minimising |(tA x tB) . (pB - pA)| / |pB - pA|.
    int bestj = j;
    double best = 1e300;
    const int jmax = std::min(nb - 1, j + nb / 4 + 1);
    for (int jj = j; jj <= jmax; ++jj) {
      const Point3d pb = B.PointAt(tb[static_cast<size_t>(jj)]);
      Vector3d r = pb - pa;
      const double len = r.Length();
      if (len < 1e-12) continue;
      r /= len;
      const double twist = std::fabs(ON_DotProduct(ON_CrossProduct(tga, B.TangentAt(tb[static_cast<size_t>(jj)])), r));
      // Slight pull towards the proportional match so the ruling stays well distributed.
      const double prop = std::fabs(static_cast<double>(jj) / (nb - 1) - static_cast<double>(i) / (na - 1));
      const double score = twist + 0.02 * prop;
      if (score < best) { best = score; bestj = jj; }
    }
    if (i == na - 1) bestj = nb - 1;
    j = bestj;
    twist_sum += best;
    ra.push_back(pa);
    rb.push_back(B.PointAt(tb[static_cast<size_t>(j)]));
  }
  kernel::NurbsSurface s = SurfaceFromRows({ra, rb});
  ctx.Doc().BeginChange("DevLoft");
  AddLike(ctx, SceneObject::MakeSurface(s), Attrs::Of(*ctx.Doc().Find(curves[0].first)));
  ctx.Print("DevLoft: ruled surface with " + std::to_string(na) + " adjusted rulings (mean twist " + FormatNumber(twist_sum / na) + ")");
}

// Cap mesh of a surface / brep object for the solid extrusions.
std::optional<RawMesh> CapMeshOf(CommandContext& ctx, const SceneObject& o) {
  const double tol = std::min(ctx.App().surface_display_tolerance, 0.05);
  if (o.kind == ObjectKind::Surface && o.surface) return Unpack(o.surface->TessellateGridAdaptive(tol).raw());
  if (o.kind == ObjectKind::Brep && o.brep) {
    BrepMeshOptions opt;
    opt.chord_tolerance = tol;
    const std::vector<kernel::Mesh> faces = MeshBrepFaces(o.brep->raw(), opt);
    if (faces.empty()) return std::nullopt;
    return Unpack(kernel::Mesh::MergeAndWeld(faces, std::max(ctx.Settings().absolute_tolerance, 1e-6)).raw());
  }
  if (o.kind == ObjectKind::Mesh && o.mesh) return Unpack(o.mesh->raw());
  return std::nullopt;
}

// ExtrudeSrfAlongCrv / ExtrudeSrfTapered / ExtrudeSrfToPoint: the surface's
// tessellation is the cap of a swept mesh solid.
class ExtrudeSrfCommand : public Command {
 public:
  enum class Kind { AlongCrv, Tapered, ToPoint };
  explicit ExtrudeSrfCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override { WantObjects("Select surfaces to extrude"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_srfs_) {
      for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Surface || o->kind == ObjectKind::Brep || o->kind == ObjectKind::Mesh) srfs_.push_back(id);
      if (srfs_.empty()) { ctx.Warn("Select surfaces"); Finish(); return; }
      have_srfs_ = true;
      ctx.Doc().BoundingBoxOf(srfs_, bbox_);
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      if (kind_ == Kind::AlongCrv) { WantObjects("Select path curve"); return; }
      if (kind_ == Kind::ToPoint) { WantPoint("Point to extrude to"); return; }
      options = {{"DraftAngle", FormatNumber(angle_), {}, true, false}};
      WantNumber("Extrusion distance <" + FormatNumber(10.0) + ">", 10.0);
      return;
    }
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Curve && o->curve) { BuildAlong(ctx, *o->curve); Finish(); return; }
    ctx.Warn("Select a path curve");
    Finish();
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "DraftAngle") { double d; if (ParseNum(v, d) && std::fabs(d) < 90) { angle_ = d; options[0].value = FormatNumber(d); } else ctx.Warn("Draft angle must be between -90 and 90 degrees"); }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNum(t, v)) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double d) override {
    if (kind_ != Kind::Tapered) return;
    if (d == 0) { ctx.Warn("Distance must be non-zero"); Finish(); return; }
    BuildTapered(ctx, d);
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (kind_ == Kind::ToPoint) { BuildToPoint(ctx, p); Finish(); return; }
    if (kind_ == Kind::Tapered) OnNumber(ctx, ON_DotProduct(p - BoxCenter(bbox_), ActiveNormal(ctx)));
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (kind_ != Kind::ToPoint) return;
    ctx.ClearPreview();
    for (ObjectId id : srfs_) if (const SceneObject* o = ctx.Doc().Find(id)) { const DisplayCache& d = o->Display(); for (size_t i = 0; i + 2 < d.lines.size(); i += 12) ctx.AddPreviewLine(Point3d(d.lines[i], d.lines[i + 1], d.lines[i + 2]), h); }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  int Emit(CommandContext& ctx, const char* label, const std::function<std::optional<kernel::Mesh>(const RawMesh&, const SceneObject&)>& make) {
    ctx.Doc().BeginChange(label);
    int made = 0;
    for (ObjectId id : srfs_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<RawMesh> cap = CapMeshOf(ctx, *o);
      if (!cap || cap->f.empty()) { ctx.Warn(std::string(label) + ": object " + std::to_string(id) + " has no mesh; skipped"); continue; }
      const Attrs a = Attrs::Of(*o);
      std::optional<kernel::Mesh> m;
      try { m = make(*cap, *o); } catch (const std::exception& e) { ctx.Warn(std::string(label) + ": " + e.what()); }
      if (!m) continue;
      AddLike(ctx, SceneObject::MakeMesh(*m), a);
      ++made;
      ctx.Print(std::string(label) + ": " + (m->IsClosedManifold() ? "closed" : "open") + " mesh solid, volume " + FormatNumber(std::fabs(m->Volume())));
    }
    return made;
  }
  void BuildAlong(CommandContext& ctx, const kernel::NurbsCurve& path) {
    const std::vector<double> params = ArcLengthParams(path, path.IsLinear() ? 2 : 32, false);
    const Point3d p0 = path.PointAt(params.front());
    std::vector<ON_Xform> stations;
    for (double t : params) stations.push_back(ON_Xform::TranslationTransformation(path.PointAt(t) - p0));
    const int made = Emit(ctx, "ExtrudeSrfAlongCrv", [&](const RawMesh& cap, const SceneObject&) -> std::optional<kernel::Mesh> { return SweepCap(cap, stations, nullptr); });
    ctx.Print("ExtrudeSrfAlongCrv: " + std::to_string(made) + " solid(s) along a path of length " + FormatNumber(path.Length()));
  }
  void BuildTapered(CommandContext& ctx, double d) {
    ctx.ClearPreview();
    const Vector3d n = ActiveNormal(ctx);
    const int made = Emit(ctx, "ExtrudeSrfTapered", [&](const RawMesh& cap, const SceneObject&) -> std::optional<kernel::Mesh> {
      const Point3d c = Centroid(cap.v);
      double radius = 0;
      for (const Point3d& p : cap.v) { const Vector3d v = p - c; radius = std::max(radius, (v - n * ON_DotProduct(v, n)).Length()); }
      const double inset = std::fabs(d) * std::tan(angle_ * ON_PI / 180.0);
      double scale = radius > 1e-12 ? (radius - inset) / radius : 1;
      if (scale < 0.01) { ctx.Warn("Draft angle closes the surface before the full distance; clamped"); scale = 0.01; }
      std::vector<ON_Xform> stations = {ON_Xform::IdentityTransformation, ON_Xform::TranslationTransformation(n * d) * ON_Xform::ScaleTransformation(c, scale)};
      return SweepCap(cap, stations, nullptr);
    });
    ctx.Print("ExtrudeSrfTapered: " + std::to_string(made) + " solid(s), distance " + FormatNumber(d) + ", draft angle " + FormatNumber(angle_));
  }
  void BuildToPoint(CommandContext& ctx, Point3d apex) {
    ctx.ClearPreview();
    const int made = Emit(ctx, "ExtrudeSrfToPoint", [&](const RawMesh& cap, const SceneObject&) -> std::optional<kernel::Mesh> {
      std::vector<ON_Xform> stations = {ON_Xform::IdentityTransformation};
      return SweepCap(cap, stations, &apex);
    });
    ctx.Print("ExtrudeSrfToPoint: " + std::to_string(made) + " solid(s) to " + FormatPoint(apex));
  }
  Kind kind_;
  std::vector<ObjectId> srfs_;
  bool have_srfs_ = false;
  double angle_ = 10;
  kernel::BoundingBox bbox_{};
};

// MultiPipe: capped pipes along several curves, unioned into one mesh.
void MultiPipe(CommandContext& ctx, const std::vector<ObjectId>& ids, double radius, const Opts&) {
  auto curves = CurvesIn(ctx, ids);
  if (curves.empty()) { ctx.Warn("Select curves"); return; }
  const int segs = 20;
  std::vector<kernel::Mesh> pipes;
  for (const auto& [id, c] : curves) {
    const bool wrap = c.IsClosed();
    const int nrows = c.IsLinear() && !wrap ? 2 : 32;
    const std::vector<double> params = ArcLengthParams(c, wrap ? nrows + 1 : nrows, false);
    const std::vector<Frame> frames = RmfFrames(c, params, ON_zaxis, wrap);
    RawMesh r;
    const int rings = wrap ? nrows : nrows;
    for (int j = 0; j < rings; ++j)
      for (int i = 0; i < segs; ++i) { const double a = 2 * ON_PI * i / segs; r.v.push_back(frames[static_cast<size_t>(j)].Place(Vector3d(0, radius * std::cos(a), radius * std::sin(a)))); }
    const int last = wrap ? rings : rings - 1;
    for (int j = 0; j < last; ++j)
      for (int i = 0; i < segs; ++i) { const int i1 = (i + 1) % segs, j1 = (j + 1) % rings; r.f.push_back(RawMesh::Quad(j * segs + i, j * segs + i1, j1 * segs + i1, j1 * segs + i)); }
    if (!wrap) {
      const int c0 = static_cast<int>(r.v.size()); r.v.push_back(frames.front().o);
      const int c1 = static_cast<int>(r.v.size()); r.v.push_back(frames.back().o);
      for (int i = 0; i < segs; ++i) { const int i1 = (i + 1) % segs; r.f.push_back(RawMesh::Tri(c0, i1, i)); r.f.push_back(RawMesh::Tri(c1, (rings - 1) * segs + i, (rings - 1) * segs + i1)); }
    }
    pipes.push_back(Outward(Pack(r)));
  }
  kernel::Mesh result = pipes.front();
  int united = 1;
  for (size_t i = 1; i < pipes.size(); ++i) {
    try { result = kernel::BooleanCombine(result, pipes[i], kernel::BooleanOp::Union); ++united; }
    catch (const std::exception& e) { ctx.Warn(std::string("MultiPipe: union failed (") + e.what() + "); pipes merged unjoined"); result = kernel::Mesh::MergeAndWeld({result, pipes[i]}, 1e-6); }
  }
  ctx.Doc().BeginChange("MultiPipe");
  AddLike(ctx, SceneObject::MakeMesh(result), Attrs::Of(*ctx.Doc().Find(curves.front().first)));
  ctx.Print("MultiPipe: " + std::to_string(pipes.size()) + " pipe(s) of radius " + FormatNumber(radius) + " joined into one " + (result.IsClosedManifold() ? "closed" : "open") + " mesh (" + std::to_string(united) + " unioned), volume " + FormatNumber(std::fabs(result.Volume())));
}

// ---------------------------------------------------------------------------
// Analysis commands
// ---------------------------------------------------------------------------

bool SurfaceLike(const SceneObject& o) { return o.kind == ObjectKind::Surface || o.kind == ObjectKind::Brep || o.kind == ObjectKind::Mesh || o.kind == ObjectKind::SubD; }

void ThicknessAnalysisOn(CommandContext& ctx) {
  AnalysisSettings s = ctx.App().analysis_defaults;
  s.mode = AnalysisMode::Thickness;
  const Opts o = TakeOptions(ctx);
  s.thickness_min = OptNum(o, "Min", OptNum(o, "MinThickness", s.thickness_min));
  s.thickness_max = OptNum(o, "Max", OptNum(o, "MaxThickness", s.thickness_max));
  if (s.thickness_max <= s.thickness_min) s.thickness_max = s.thickness_min + 1;
  ctx.App().analysis_defaults = s;
  std::vector<SceneObject*> targets;
  for (ObjectId id : ctx.Selected()) if (SceneObject* obj = ctx.Doc().Find(id)) if (SurfaceLike(*obj)) targets.push_back(obj);
  if (targets.empty()) {
    ctx.App().analysis_fallback = s;
    for (SceneObject& obj : ctx.Doc().Objects()) { obj.analysis.mode = AnalysisMode::None; if (SurfaceLike(obj)) targets.push_back(&obj); }
  } else {
    for (SceneObject* obj : targets) obj->analysis = s;
  }
  if (targets.empty()) { ctx.Warn("No surfaces, polysurfaces or meshes to analyze"); return; }
  // Report the measured range of the first object.
  double lo = 1e300, hi = 0;
  const SceneObject& first = *targets.front();
  first.EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
  first.EnsureAnalysisColors(s);
  {
    // Recover thickness values from the colour ramp is lossy; measure directly instead.
    const std::vector<float>& tris = first.Display().triangles;
    // Sample every vertex: reuse the colouring's ray cast by evaluating a few rays here.
    const size_t n = tris.size() / 6;
    for (size_t i = 0; i < n; i += std::max<size_t>(1, n / 400)) {
      Vector3d nv(tris[i * 6 + 3], tris[i * 6 + 4], tris[i * 6 + 5]);
      if (!nv.Unitize()) continue;
      const Point3d p(tris[i * 6], tris[i * 6 + 1], tris[i * 6 + 2]);
      std::optional<kernel::Mesh> m = MeshOf(first, 0.05);
      if (!m) break;
      // March along -normal to the far side using the mesh distance field.
      double t = 1e-3, best = -1;
      for (int step = 0; step < 400 && t < s.thickness_max * 4; ++step) {
        const Point3d q = p - nv * t;
        const double d = std::fabs(m->SignedDistance(q));
        if (step > 2 && d < 1e-4) { best = t; break; }
        t += std::max(d, 1e-3);
      }
      if (best > 0) { lo = std::min(lo, best); hi = std::max(hi, best); }
    }
  }
  std::string msg = "ThicknessAnalysis on " + std::to_string(targets.size()) + " object(s)  Min=" + FormatNumber(s.thickness_min) + " Max=" + FormatNumber(s.thickness_max) + " (red = thin, blue = thick)";
  if (hi > 0) msg += "  measured " + FormatNumber(lo) + " .. " + FormatNumber(hi);
  ctx.Print(msg);
  ctx.RequestRedraw();
}

void ThicknessAnalysisOff(CommandContext& ctx) {
  size_t count = 0;
  if (ctx.App().analysis_fallback.mode == AnalysisMode::Thickness) { ctx.App().analysis_fallback.mode = AnalysisMode::None; ++count; }
  for (SceneObject& o : ctx.Doc().Objects()) if (o.analysis.mode == AnalysisMode::Thickness) { o.analysis.mode = AnalysisMode::None; ++count; }
  ctx.Print(std::string("ThicknessAnalysis off") + (count ? "" : " (was not on)"));
  ctx.RequestRedraw();
}

struct Continuity { double gap = 0, angle = 0, curvature = 0; };

const char* Grade(const Continuity& c, double tol, double ang_tol) {
  if (c.gap > tol) return "not continuous (G0 gap)";
  if (c.angle > ang_tol) return "G0 (position only)";
  if (c.curvature > 0.05) return "G1 (tangent)";
  return "G2 (curvature)";
}

// EdgeContinuity: samples one edge against the closest points of the other.
void EdgeContinuity(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<kernel::NurbsCurve> edges;
  Point3d hint(0, 0, 0);
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) { hint = BoxCenter(o->BoundingBox()); break; }
  for (size_t i = 0; i < ids.size() && edges.size() < 2; ++i) {
    const SceneObject* o = ctx.Doc().Find(ids[i]);
    if (!o) continue;
    // For breps/surfaces: the edge nearest the other object's centre.
    Point3d other = hint;
    for (ObjectId oid : ids) if (oid != ids[i]) if (const SceneObject* oo = ctx.Doc().Find(oid)) { other = BoxCenter(oo->BoundingBox()); break; }
    if (auto c = CurveOf(*o, &other)) edges.push_back(*c);
  }
  if (edges.size() < 2) { ctx.Warn("Select two curves or edges (surfaces contribute their nearest border edge)"); return; }
  const int n = 25;
  Continuity worst;
  ctx.App().overlay_lines.clear();
  const std::vector<double> ta = ArcLengthParams(edges[0], n, false);
  for (double t : ta) {
    const Point3d pa = edges[0].PointAt(t);
    const double tb = edges[1].ClosestPointParameter(pa, 300);
    const Point3d pb = edges[1].PointAt(tb);
    Vector3d na = edges[0].TangentAt(t), nb = edges[1].TangentAt(tb);
    double ang = ON_3dVector::Angle(na, nb) * 180.0 / ON_PI;
    ang = std::min(ang, 180.0 - ang);
    const double ka = edges[0].CurvatureAt(t).Length(), kb = edges[1].CurvatureAt(tb).Length();
    const double kd = std::fabs(ka - kb) / std::max(1e-9, std::max(ka, kb));
    worst.gap = std::max(worst.gap, pa.DistanceTo(pb));
    worst.angle = std::max(worst.angle, ang);
    worst.curvature = std::max(worst.curvature, kd);
    ctx.App().overlay_lines.insert(ctx.App().overlay_lines.end(), {static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z), static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z)});
  }
  ctx.Print("EdgeContinuity: max gap " + FormatNumber(worst.gap) + ", max tangent angle " + FormatNumber(worst.angle) + " deg, max curvature difference " + FormatNumber(worst.curvature * 100) + "% over " + std::to_string(n) + " samples: " + Grade(worst, ctx.Settings().absolute_tolerance, ctx.Settings().angle_tolerance_degrees));
  ctx.RequestRedraw();
}

// GCon: continuity between the nearest ends of two curves.
void GCon(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  auto curves = CurvesIn(ctx, ids);
  if (curves.size() < 2) { ctx.Warn("Select two curves"); return; }
  const kernel::NurbsCurve &a = curves[0].second, &b = curves[1].second;
  const double ends[2][2] = {{a.Domain().min, a.Domain().max}, {b.Domain().min, b.Domain().max}};
  double best = 1e300; int ia = 0, ib = 0;
  for (int i = 0; i < 2; ++i) for (int j = 0; j < 2; ++j) { const double d = a.PointAt(ends[0][i]).DistanceTo(b.PointAt(ends[1][j])); if (d < best) { best = d; ia = i; ib = j; } }
  Continuity c;
  c.gap = best;
  Vector3d ta = a.TangentAt(ends[0][ia]), tb = b.TangentAt(ends[1][ib]);
  if (ia == 1) ta = -ta;  // both tangents pointing away from the joint
  if (ib == 1) tb = -tb;
  double ang = ON_3dVector::Angle(ta, -tb) * 180.0 / ON_PI;
  c.angle = ang;
  const double ka = a.CurvatureAt(ends[0][ia]).Length(), kb = b.CurvatureAt(ends[1][ib]).Length();
  c.curvature = std::fabs(ka - kb) / std::max(1e-9, std::max(ka, kb));
  if (ka < 1e-9 && kb < 1e-9) c.curvature = 0;
  ctx.Print("GCon: end gap " + FormatNumber(c.gap) + ", tangent angle " + FormatNumber(c.angle) + " deg, curvature " + FormatNumber(ka) + " vs " + FormatNumber(kb) + ": " + Grade(c, ctx.Settings().absolute_tolerance, ctx.Settings().angle_tolerance_degrees));
}

bool ParseUV(const std::string& t, double& u, double& v) { return std::sscanf(t.c_str(), "%lf,%lf", &u, &v) == 2; }

// EvaluateUVPt / PointsFromUV: surface points at typed parameters.
class UVPointsCommand : public Command {
 public:
  explicit UVPointsCommand(bool many) : many_(many) {}
  void Begin(CommandContext&) override { options = {{"Normalized", "No", {"Yes", "No"}, false, true}}; WantObjects("Select a surface"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if ((srf_ = SurfaceOf(*o))) { like_ = Attrs::Of(*o); break; }
    if (!srf_) { ctx.Warn("Select a surface"); Finish(); return; }
    WantText(many_ ? "Surface parameters u,v (Enter when done)" : "Surface parameters u,v");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Normalized") { normalized_ = YesNo(v, normalized_); options[0].value = normalized_ ? "Yes" : "No"; } }
  void OnText(CommandContext& ctx, const std::string& t) override {
    double u, v;
    if (!ParseUV(t, u, v)) { ctx.Warn("Expected u,v"); return; }
    if (normalized_) { u = srf_->Domain(0).min + (srf_->Domain(0).max - srf_->Domain(0).min) * u; v = srf_->Domain(1).min + (srf_->Domain(1).max - srf_->Domain(1).min) * v; }
    const Point3d p = srf_->PointAt(u, v);
    pts_.push_back(p);
    ctx.Print((many_ ? "PointsFromUV" : "EvaluateUVPt") + std::string(": uv ") + FormatNumber(u) + "," + FormatNumber(v) + " -> " + FormatPoint(p));
    if (!many_) { Commit(ctx); Finish(); return; }
    WantText("Surface parameters u,v (Enter when done)");
  }
  void OnEnter(CommandContext& ctx) override { if (want == Want::Text) { Commit(ctx); Finish(); } }
  void Commit(CommandContext& ctx) {
    if (pts_.empty()) return;
    ctx.Doc().BeginChange(many_ ? "PointsFromUV" : "EvaluateUVPt");
    for (const Point3d& p : pts_) AddLike(ctx, SceneObject::MakePoint(p), like_);
    if (many_) ctx.Print("PointsFromUV: " + std::to_string(pts_.size()) + " point(s)");
  }

 private:
  bool many_, normalized_ = false;
  std::optional<kernel::NurbsSurface> srf_;
  Attrs like_;
  std::vector<Point3d> pts_;
};

void DraftAnglePoint(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<Point3d>& picks) {
  std::optional<kernel::NurbsSurface> srf;
  Attrs like;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if ((srf = SurfaceOf(*o))) { like = Attrs::Of(*o); break; }
  if (!srf) { ctx.Warn("Select a surface"); return; }
  const kernel::Point2d uv = srf->ClosestPointParameter(picks.front(), 40, 40);
  const Point3d p = srf->PointAt(uv.x, uv.y);
  Vector3d n = srf->NormalAt(uv.x, uv.y);
  n.Unitize();
  const Vector3d pull = ctx.App().analysis_defaults.draft_direction.Length() > 0 ? ctx.App().analysis_defaults.draft_direction : Vector3d(0, 0, 1);
  const double draft = 90.0 - ON_3dVector::Angle(n, pull) * 180.0 / ON_PI;
  ctx.Doc().BeginChange("DraftAnglePoint");
  SceneObject pt = SceneObject::MakePoint(p);
  pt.name = "Draft " + FormatNumber(draft);
  AddLike(ctx, std::move(pt), like);
  ctx.Print("DraftAnglePoint: " + FormatNumber(draft) + " degrees at " + FormatPoint(p) + " (pull direction " + FormatPoint(Point3d(pull.x, pull.y, pull.z)) + ")");
}

// CurvatureGraph: on-screen comb (persistent overlay); ExtractCurvatureGraph makes curves.
std::vector<std::pair<Point3d, Point3d>> CurvatureHairs(const kernel::NurbsCurve& c, double scale, int density) {
  std::vector<std::pair<Point3d, Point3d>> hairs;
  const kernel::Interval d = c.Domain();
  for (int i = 0; i <= density; ++i) {
    const double t = d.min + (d.max - d.min) * i / density;
    const Point3d p = c.PointAt(t);
    hairs.emplace_back(p, p - c.CurvatureAt(t) * scale);
  }
  return hairs;
}

void CurvatureGraph(CommandContext& ctx, const std::vector<ObjectId>& ids, bool extract) {
  const Opts o = TakeOptions(ctx);
  const double scale = OptNum(o, "Scale", 20.0);
  const int density = std::max(4, static_cast<int>(OptNum(o, "Density", 60)));
  auto curves = CurvesIn(ctx, ids);
  if (curves.empty()) { ctx.Warn("Select curves"); return; }
  if (extract) ctx.Doc().BeginChange("ExtractCurvatureGraph");
  else ctx.App().overlay_lines.clear();
  int made = 0;
  double kmax = 0;
  for (const auto& [id, c] : curves) {
    const auto hairs = CurvatureHairs(c, scale, density);
    Row comb;
    for (const auto& h : hairs) { comb.push_back(h.second); kmax = std::max(kmax, h.first.DistanceTo(h.second) / scale); }
    if (extract) {
      const Attrs a = Attrs::Of(*ctx.Doc().Find(id));
      SceneObject g = SceneObject::MakeCurve(PolylineCurve(comb));
      g.name = "CurvatureGraph";
      g.color = Color::FromBytes(255, 120, 40); g.color_by_layer = false;
      AddLike(ctx, std::move(g), a);
      for (size_t i = 0; i < hairs.size(); i += std::max<size_t>(1, hairs.size() / 20)) { SceneObject h = SceneObject::MakeCurve(PolylineCurve({hairs[i].first, hairs[i].second})); h.name = "CurvatureGraph"; h.color = Color::FromBytes(255, 120, 40); h.color_by_layer = false; AddLike(ctx, std::move(h), a); }
    } else {
      std::vector<float>& ov = ctx.App().overlay_lines;
      auto put = [&](Point3d a, Point3d b) { ov.insert(ov.end(), {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z), static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.z)}); };
      for (const auto& h : hairs) put(h.first, h.second);
      for (size_t i = 1; i < comb.size(); ++i) put(comb[i - 1], comb[i]);
    }
    ++made;
  }
  ctx.Print(std::string(extract ? "ExtractCurvatureGraph" : "CurvatureGraph") + ": " + std::to_string(made) + " curve(s), scale " + FormatNumber(scale) + ", density " + std::to_string(density) + ", max curvature " + FormatNumber(kmax) + (extract ? "" : " (CurvatureGraphOff clears the display)"));
  ctx.RequestRedraw();
}

// ---------------------------------------------------------------------------
// Mesh commands
// ---------------------------------------------------------------------------

struct MeshTarget { ObjectId id; kernel::Mesh mesh; };
std::vector<MeshTarget> MeshesIn(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<MeshTarget> out;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Mesh && o->mesh) out.push_back({id, *o->mesh});
  return out;
}

void AlignVertices(CommandContext& ctx, const std::vector<ObjectId>& ids, double dist, const Opts&) {
  auto meshes = MeshesIn(ctx, ids);
  if (meshes.empty()) { ctx.Warn("Select meshes"); return; }
  ctx.Doc().BeginChange("AlignVertices");
  int merged_total = 0;
  for (MeshTarget& t : meshes) {
    RawMesh r = Unpack(t.mesh.raw());
    UnionFind uf(r.v.size());
    std::vector<int> order(r.v.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return r.v[a].x < r.v[b].x; });
    int merged = 0;
    for (size_t i = 0; i < order.size(); ++i)
      for (size_t j = i + 1; j < order.size() && r.v[order[j]].x - r.v[order[i]].x <= dist; ++j)
        if (r.v[order[i]].DistanceTo(r.v[order[j]]) <= dist && uf.Find(order[i]) != uf.Find(order[j])) { uf.Unite(order[i], order[j]); ++merged; }
    if (merged) ReplaceMesh(ctx, t.id, Pack(ApplyMerge(r, uf)));
    merged_total += merged;
  }
  ctx.Print("AlignVertices: " + std::to_string(merged_total) + " vertex pair(s) within " + FormatNumber(dist) + " merged in " + std::to_string(meshes.size()) + " mesh(es)");
}

void MeshPolyline(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("MeshPolyline");
  int made = 0;
  for (const auto& [id, c] : CurvesIn(ctx, ids)) {
    ON_Plane pl;
    if (!c.IsClosed() || !c.raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance * 10)) { ctx.Warn("MeshPolyline: object " + std::to_string(id) + " is not a closed planar curve; skipped"); continue; }
    ON_Brep* b = ON_BrepTrimmedPlane(pl, c.raw());
    if (!b) continue;
    BrepMeshOptions opt;
    opt.chord_tolerance = 0.01;
    const std::vector<kernel::Mesh> faces = MeshBrepFaces(*b, opt);
    delete b;
    if (faces.empty()) continue;
    kernel::Mesh m = kernel::Mesh::MergeAndWeld(faces, 1e-6);
    AddLike(ctx, SceneObject::MakeMesh(m), Attrs::Of(*ctx.Doc().Find(id)));
    ctx.Print("MeshPolyline: mesh with " + std::to_string(m.FaceCount()) + " face(s), area " + FormatNumber(m.Area()));
    ++made;
  }
  ctx.Print("MeshPolyline: " + std::to_string(made) + " mesh(es)");
}

// WeldEdge: merges the coincident vertices at both ends of the mesh edge
// nearest the picked point.
void WeldEdge(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<Point3d>& picks) {
  auto meshes = MeshesIn(ctx, ids);
  if (meshes.empty()) { ctx.Warn("Select a mesh"); return; }
  const Point3d pick = picks.front();
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-6);
  ctx.Doc().BeginChange("WeldEdge");
  int welded = 0;
  for (MeshTarget& t : meshes) {
    RawMesh r = Unpack(t.mesh.raw());
    double best = 1e300;
    std::pair<int, int> edge{-1, -1};
    for (const Face& f : r.f) {
      const int n = RawMesh::Corners(f);
      for (int k = 0; k < n; ++k) {
        const int a = f[k], b = f[(k + 1) % n];
        const ON_Line l(r.v[a], r.v[b]);
        const double d = l.DistanceTo(pick);
        if (d < best) { best = d; edge = {a, b}; }
      }
    }
    if (edge.first < 0) continue;
    UnionFind uf(r.v.size());
    int merged = 0;
    for (int end : {edge.first, edge.second})
      for (size_t i = 0; i < r.v.size(); ++i)
        if (static_cast<int>(i) != end && r.v[i].DistanceTo(r.v[end]) <= tol && uf.Find(static_cast<int>(i)) != uf.Find(end)) { uf.Unite(end, static_cast<int>(i)); ++merged; }
    if (merged) { ReplaceMesh(ctx, t.id, Pack(ApplyMerge(r, uf))); welded += merged; }
  }
  ctx.Print("WeldEdge: " + std::to_string(welded) + " duplicate vertex(es) welded at the edge nearest " + FormatPoint(pick));
}

// Connected face components by shared vertices.
std::vector<int> FaceComponents(const RawMesh& r, int& count) {
  UnionFind uf(r.v.size());
  for (const Face& f : r.f) for (int k = 1; k < RawMesh::Corners(f); ++k) uf.Unite(f[0], f[k]);
  std::map<int, int> label;
  std::vector<int> comp(r.f.size());
  for (size_t i = 0; i < r.f.size(); ++i) { const int root = uf.Find(r.f[i][0]); auto it = label.find(root); if (it == label.end()) it = label.emplace(root, static_cast<int>(label.size())).first; comp[i] = it->second; }
  count = static_cast<int>(label.size());
  return comp;
}

// Picks the closest face across all candidate meshes and returns its mesh,
// component label, component count and face count in that component.
void SelConnectedMeshFaces(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<Point3d>& picks) {
  auto meshes = MeshesIn(ctx, ids);
  if (meshes.empty()) { ctx.Warn("Select a mesh"); return; }
  const Point3d pick = picks.front();
  ObjectId best_id = kNoObject;
  double best = 1e300;
  int faces = 0, parts = 0, best_label = -1;
  RawMesh best_raw;
  std::vector<int> best_comp;
  for (MeshTarget& t : meshes) {
    RawMesh r = Unpack(t.mesh.raw());
    int count = 0;
    const std::vector<int> comp = FaceComponents(r, count);
    for (size_t i = 0; i < r.f.size(); ++i) {
      const double d = r.FaceCenter(r.f[i]).DistanceTo(pick);
      if (d < best) {
        best = d; best_id = t.id; parts = count; best_label = comp[i];
        faces = static_cast<int>(std::count(comp.begin(), comp.end(), comp[i]));
        best_raw = r; best_comp = comp;
      }
    }
  }
  if (best_id == kNoObject) return;
  // A single connected mesh: nothing to isolate, select the whole object.
  if (parts <= 1) {
    ctx.Doc().SelectNone();
    ctx.Doc().Select(best_id, true);
    ctx.Print("SelConnectedMeshFaces: " + std::to_string(faces) + " face(s), the whole mesh (it has a single connected part)");
    return;
  }
  // Several disconnected parts: split off the picked one into its own object
  // (there is no per-face sub-object selection in this app) and select it,
  // leaving the rest of the mesh behind as the original object.
  RawMesh picked_part, remainder;
  picked_part.v = remainder.v = best_raw.v;
  for (size_t i = 0; i < best_raw.f.size(); ++i) (best_comp[i] == best_label ? picked_part : remainder).f.push_back(best_raw.f[i]);
  const Attrs attrs = Attrs::Of(*ctx.Doc().Find(best_id));
  ctx.Doc().BeginChange("SelConnectedMeshFaces");
  ctx.Doc().Remove(best_id);
  AddLike(ctx, SceneObject::MakeMesh(Pack(remainder)), attrs);
  const ObjectId new_id = AddLike(ctx, SceneObject::MakeMesh(Pack(picked_part)), attrs);
  ctx.Doc().SelectNone();
  ctx.Doc().Select(new_id, true);
  ctx.Print("SelConnectedMeshFaces: " + std::to_string(faces) + " connected face(s) split off into object " + std::to_string(new_id) + " and selected (mesh had " + std::to_string(parts) + " disconnected part(s))");
}

// CollapseMeshFacesBy*: faces failing the criterion collapse to their centre
// (mode 0 area, 1 aspect ratio, 2 edge length).
void CollapseFaces(CommandContext& ctx, const std::vector<ObjectId>& ids, double threshold, int mode) {
  auto meshes = MeshesIn(ctx, ids);
  if (meshes.empty()) { ctx.Warn("Select meshes"); return; }
  const char* label = mode == 0 ? "CollapseMeshFacesByArea" : mode == 1 ? "CollapseMeshFacesByAspectRatio" : "CollapseMeshFacesByEdgeLength";
  ctx.Doc().BeginChange(label);
  int collapsed = 0;
  for (MeshTarget& t : meshes) {
    RawMesh r = Unpack(t.mesh.raw());
    UnionFind uf(r.v.size());
    int n_collapsed = 0;
    for (const Face& f : r.f) {
      const int n = RawMesh::Corners(f);
      double shortest = 1e300, longest = 0;
      int short_a = f[0], short_b = f[1];
      for (int k = 0; k < n; ++k) { const double l = r.v[f[k]].DistanceTo(r.v[f[(k + 1) % n]]); if (l < shortest) { shortest = l; short_a = f[k]; short_b = f[(k + 1) % n]; } longest = std::max(longest, l); }
      bool hit = false;
      if (mode == 0) hit = r.FaceArea(f) < threshold;
      else if (mode == 1) hit = shortest > 1e-12 && longest / shortest > threshold;
      else hit = shortest < threshold;
      if (!hit) continue;
      ++n_collapsed;
      if (mode == 0) { for (int k = 1; k < n; ++k) uf.Unite(f[0], f[k]); }
      else uf.Unite(short_a, short_b);
    }
    if (n_collapsed) {
      const int before = static_cast<int>(r.f.size());
      const kernel::Mesh m = Pack(ApplyMerge(r, uf));
      ReplaceMesh(ctx, t.id, m);
      ctx.Print(std::string(label) + ": " + std::to_string(n_collapsed) + " face(s) collapsed, " + std::to_string(before) + " -> " + std::to_string(m.FaceCount()) + " faces");
    }
    collapsed += n_collapsed;
  }
  if (!collapsed) ctx.Print(std::string(label) + ": no faces below the threshold " + FormatNumber(threshold));
}

void ExtractDuplicateMeshFaces(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  auto meshes = MeshesIn(ctx, ids);
  if (meshes.empty()) { ctx.Warn("Select meshes"); return; }
  ctx.Doc().BeginChange("ExtractDuplicateMeshFaces");
  int total = 0;
  for (MeshTarget& t : meshes) {
    RawMesh r = Unpack(t.mesh.raw());
    // Key faces by their sorted vertex positions so welded and unwelded duplicates both count.
    std::map<std::vector<std::array<double, 3>>, int> seen;
    RawMesh keep, dup;
    keep.v = dup.v = r.v;
    for (const Face& f : r.f) {
      std::vector<std::array<double, 3>> key;
      for (int k = 0; k < RawMesh::Corners(f); ++k) { const Point3d& p = r.v[f[k]]; key.push_back({std::round(p.x * 1e6) / 1e6, std::round(p.y * 1e6) / 1e6, std::round(p.z * 1e6) / 1e6}); }
      std::sort(key.begin(), key.end());
      if (seen[key]++ == 0) keep.f.push_back(f); else dup.f.push_back(f);
    }
    if (dup.f.empty()) continue;
    const Attrs a = Attrs::Of(*ctx.Doc().Find(t.id));
    ReplaceMesh(ctx, t.id, Pack(keep));
    SceneObject d = SceneObject::MakeMesh(Pack(dup));
    d.name = "Duplicate faces";
    AddLike(ctx, std::move(d), a);
    total += static_cast<int>(dup.f.size());
  }
  ctx.Print("ExtractDuplicateMeshFaces: " + std::to_string(total) + " duplicate face(s) extracted");
}

// Splits a raw mesh by a plane into the two sides (triangles crossing the
// plane are clipped into pieces).
std::pair<RawMesh, RawMesh> SplitRawByPlane(const RawMesh& in, const ON_Plane& plane, double tol) {
  RawMesh a, b;
  a.v = b.v = in.v;
  auto add = [&](RawMesh& m, const Point3d& p) { m.v.push_back(p); return static_cast<int>(m.v.size()) - 1; };
  for (const Face& f : in.f) {
    std::vector<std::array<int, 3>> tris;
    if (RawMesh::IsTri(f)) tris.push_back({f[0], f[1], f[2]});
    else { tris.push_back({f[0], f[1], f[2]}); tris.push_back({f[0], f[2], f[3]}); }
    for (const auto& t : tris) {
      double d[3];
      int pos = 0, neg = 0;
      for (int k = 0; k < 3; ++k) { d[k] = plane.DistanceTo(in.v[t[k]]); if (d[k] > tol) ++pos; else if (d[k] < -tol) ++neg; }
      if (neg == 0) { a.f.push_back(RawMesh::Tri(t[0], t[1], t[2])); continue; }
      if (pos == 0) { b.f.push_back(RawMesh::Tri(t[0], t[1], t[2])); continue; }
      // Clip: walk the polygon, inserting crossing points.
      std::vector<int> pa, pb;
      for (int k = 0; k < 3; ++k) {
        const int i0 = t[k], i1 = t[(k + 1) % 3];
        const double d0 = d[k], d1 = d[(k + 1) % 3];
        if (d0 >= -tol) pa.push_back(i0);
        if (d0 <= tol) pb.push_back(i0);
        if ((d0 > tol && d1 < -tol) || (d0 < -tol && d1 > tol)) {
          const Point3d x = in.v[i0] + (in.v[i1] - in.v[i0]) * (d0 / (d0 - d1));
          pa.push_back(add(a, x)); pb.push_back(add(b, x));
          // Keep both vertex arrays aligned so indices stay valid in each.
          if (a.v.size() != b.v.size()) { while (a.v.size() < b.v.size()) a.v.push_back(x); while (b.v.size() < a.v.size()) b.v.push_back(x); }
        }
      }
      for (size_t k = 1; k + 1 < pa.size(); ++k) a.f.push_back(RawMesh::Tri(pa[0], pa[k], pa[k + 1]));
      for (size_t k = 1; k + 1 < pb.size(); ++k) b.f.push_back(RawMesh::Tri(pb[0], pb[k], pb[k + 1]));
    }
  }
  return {a, b};
}

void SplitMeshWithCurve(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  auto meshes = MeshesIn(ctx, ids);
  auto curves = CurvesIn(ctx, ids);
  if (meshes.empty() || curves.empty()) { ctx.Warn("Select a mesh and a cutting curve"); return; }
  const kernel::NurbsCurve& c = curves.front().second;
  ON_Plane plane;
  bool planar = true;
  if (c.IsLinear(ctx.Settings().absolute_tolerance)) {
    const Point3d p0 = c.PointAt(c.Domain().min), p1 = c.PointAt(c.Domain().max);
    Vector3d n = ON_CrossProduct(p1 - p0, ActiveNormal(ctx));
    if (!n.Unitize()) { ctx.Warn("SplitMeshWithCurve: the line is parallel to the CPlane normal"); return; }
    plane = ON_Plane(p0, n);
  } else if (!c.raw().IsPlanar(&plane, ctx.Settings().absolute_tolerance * 10)) {
    planar = false;
    const Row pts = SampleCurve(c, 32, c.IsClosed());
    // Least-squares plane through the samples.
    const Point3d cen = Centroid(pts);
    double xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0;
    for (const Point3d& p : pts) { const Vector3d d = p - cen; xx += d.x * d.x; xy += d.x * d.y; xz += d.x * d.z; yy += d.y * d.y; yz += d.y * d.z; zz += d.z * d.z; }
    const double dx = yy * zz - yz * yz, dy = xx * zz - xz * xz, dz = xx * yy - xy * xy;
    Vector3d n = dx >= dy && dx >= dz ? Vector3d(dx, xz * yz - xy * zz, xy * yz - xz * yy) : dy >= dz ? Vector3d(xz * yz - xy * zz, dy, xy * xz - yz * xx) : Vector3d(xy * yz - xz * yy, xy * xz - yz * xx, dz);
    if (!n.Unitize()) n = ActiveNormal(ctx);
    plane = ON_Plane(cen, n);
  }
  ctx.Doc().BeginChange("SplitMeshWithCurve");
  int made = 0;
  for (MeshTarget& t : meshes) {
    const RawMesh r = Unpack(t.mesh.raw());
    auto [a, b] = SplitRawByPlane(r, plane, ctx.Settings().absolute_tolerance);
    if (a.f.empty() || b.f.empty()) { ctx.Warn("SplitMeshWithCurve: the curve's plane does not cross mesh " + std::to_string(t.id)); continue; }
    const Attrs attrs = Attrs::Of(*ctx.Doc().Find(t.id));
    ctx.Doc().Remove(t.id);
    AddLike(ctx, SceneObject::MakeMesh(Pack(a)), attrs);
    AddLike(ctx, SceneObject::MakeMesh(Pack(b)), attrs);
    made += 2;
  }
  ctx.Print("SplitMeshWithCurve: " + std::to_string(made) + " mesh piece(s)" + (planar ? " (split by the curve's plane)" : " (non-planar curve: split by its best-fit plane)"));
}

void ExtractUVMesh(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("ExtractUVMesh");
  int made = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    const Attrs a = Attrs::Of(*o);
    if (o->kind == ObjectKind::Mesh && o->mesh && o->mesh->HasTextureCoordinates()) {
      RawMesh r = Unpack(o->mesh->raw());
      for (int i = 0; i < static_cast<int>(r.v.size()); ++i) { const kernel::Point2d uv = o->mesh->TextureCoordinateAt(i); r.v[static_cast<size_t>(i)] = Point3d(uv.x, uv.y, 0); }
      AddLike(ctx, SceneObject::MakeMesh(Pack(r)), a);
      ++made;
      continue;
    }
    std::optional<kernel::NurbsSurface> s = SurfaceOf(*o);
    if (!s) { ctx.Warn("ExtractUVMesh: object " + std::to_string(id) + " is not a surface or textured mesh; skipped"); continue; }
    // Lay the grid out at the surface's approximate U/V size from the world origin.
    const kernel::SurfaceSize size = s->GetApproximateSize();
    const int n = 24;
    RawMesh r;
    for (int i = 0; i <= n; ++i) for (int j = 0; j <= n; ++j) r.v.emplace_back(size.width * i / n, size.height * j / n, 0);
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) r.f.push_back(RawMesh::Quad(i * (n + 1) + j, (i + 1) * (n + 1) + j, (i + 1) * (n + 1) + j + 1, i * (n + 1) + j + 1));
    kernel::Mesh m = Pack(r);
    std::vector<kernel::Point2d> uvs;
    for (int i = 0; i <= n; ++i) for (int j = 0; j <= n; ++j) uvs.emplace_back(static_cast<double>(i) / n, static_cast<double>(j) / n);
    m.SetTextureCoordinates(uvs);
    AddLike(ctx, SceneObject::MakeMesh(m), a);
    ctx.Print("ExtractUVMesh: " + FormatNumber(size.width) + " x " + FormatNumber(size.height) + " UV rectangle at the origin (" + std::to_string(n * n) + " faces)");
    ++made;
  }
  ctx.Print("ExtractUVMesh: " + std::to_string(made) + " mesh(es)");
}

// ---------------------------------------------------------------------------
// Layer / edit commands
// ---------------------------------------------------------------------------

// DupLayer takes its source layer as a Layer= option (default: current
// layer) so "DupLayer Name=Copies Objects=No" works as a one-shot macro
// line without an extra text prompt for the source name.
void DupLayer(CommandContext& ctx) {
  const Opts o = TakeOptions(ctx);
  const std::string typed = OptStr(o, "Layer", "");
  const int src = typed.empty() ? ctx.Doc().CurrentLayer() : ctx.Doc().FindLayer(typed);
  if (src < 0) { ctx.Warn("No layer named '" + typed + "'"); return; }
  const Layer L = ctx.Doc().Layers()[static_cast<size_t>(src)];
  std::string name = OptStr(o, "Name", L.name + " copy");
  int suffix = 2;
  while (ctx.Doc().FindLayer(name) >= 0) name = L.name + " copy " + std::to_string(suffix++);
  ctx.Doc().BeginChange("DupLayer");
  const int idx = ctx.Doc().AddLayer(name, L.color, L.parent);
  Layer& n = ctx.Doc().Layers()[static_cast<size_t>(idx)];
  n.visible = L.visible; n.locked = L.locked; n.linetype = L.linetype; n.material = L.material; n.description = L.description;
  int copied = 0;
  if (OptYes(o, "Objects", true)) {
    std::vector<SceneObject> copies;
    for (const SceneObject& obj : ctx.Doc().Objects()) if (obj.layer_index == src) { SceneObject c = obj; c.id = kNoObject; c.layer_index = idx; c.selected = false; copies.push_back(std::move(c)); }
    for (SceneObject& c : copies) { ctx.Doc().Add(std::move(c)); ++copied; }
  }
  ctx.Print("DupLayer: " + L.name + " -> " + name + " with " + std::to_string(copied) + " object(s)");
}

void CopyToLayer(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& name) {
  if (name.empty()) { ctx.Warn("Type the layer name"); return; }
  int idx = ctx.Doc().FindLayer(name);
  ctx.Doc().BeginChange("CopyToLayer");
  if (idx < 0) idx = ctx.Doc().AddLayer(name);
  std::vector<SceneObject> copies;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) { SceneObject c = *o; c.id = kNoObject; c.layer_index = idx; c.selected = false; copies.push_back(std::move(c)); }
  for (SceneObject& c : copies) ctx.Doc().Add(std::move(c));
  ctx.Print("CopyToLayer: " + std::to_string(copies.size()) + " object(s) copied to " + ctx.Doc().LayerFullPath(idx));
}

void HighlightObjectLayers(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::set<int> layers;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) layers.insert(o->layer_index);
  ctx.App().highlight_layers.assign(layers.begin(), layers.end());
  for (int i : layers) for (int p = ctx.Doc().Layers()[static_cast<size_t>(i)].parent; p >= 0; p = ctx.Doc().Layers()[static_cast<size_t>(p)].parent) ctx.Doc().Layers()[static_cast<size_t>(p)].expanded = true;
  ctx.App().Panels().layers = true;
  std::string names;
  for (int i : layers) names += (names.empty() ? "" : ", ") + ctx.Doc().LayerFullPath(i);
  ctx.Print("HighlightObjectLayers: " + std::to_string(layers.size()) + " layer(s) highlighted in the Layers panel: " + names);
}

void LayerBook(CommandContext& ctx) {
  std::vector<std::string> plain;
  const Opts o = TakeOptions(ctx, &plain);
  std::string dir = Lower(OptStr(o, "Page", plain.empty() ? "Next" : plain.front()));
  std::vector<Layer>& L = ctx.Doc().Layers();
  int& page = ctx.App().State().layer_book_page;
  const int n = static_cast<int>(L.size());
  if (dir == "all" || dir == "off") {
    for (Layer& l : L) l.visible = true;
    page = -1;
    ctx.Print("LayerBook: all " + std::to_string(n) + " layer(s) on");
    return;
  }
  if (dir == "previous" || dir == "prev") page = page <= 0 ? n - 1 : page - 1;
  else if (dir == "first") page = 0;
  else if (dir == "last") page = n - 1;
  else { double v; if (ParseNum(dir, v)) page = std::clamp(static_cast<int>(v) - 1, 0, n - 1); else page = (page + 1) % n; }
  for (int i = 0; i < n; ++i) L[static_cast<size_t>(i)].visible = (i == page);
  ctx.Print("LayerBook: page " + std::to_string(page + 1) + " of " + std::to_string(n) + ": " + ctx.Doc().LayerFullPath(page) + " shown alone (Next/Previous/All)");
}

void IsolateLock(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("IsolateLock");
  int locked = 0;
  for (SceneObject& o : ctx.Doc().Objects()) {
    if (std::find(ids.begin(), ids.end(), o.id) != ids.end() || o.locked || !o.visible) continue;
    o.locked = true; o.selected = false; o.user_text["IsolateLock"] = "1"; ++locked;
  }
  ctx.Print("IsolateLock: " + std::to_string(locked) + " object(s) locked; " + std::to_string(ids.size()) + " left editable (UnisolateLock restores)");
}

void UnisolateLock(CommandContext& ctx) {
  ctx.Doc().BeginChange("UnisolateLock");
  int unlocked = 0;
  for (SceneObject& o : ctx.Doc().Objects()) if (o.user_text.count("IsolateLock")) { o.locked = false; o.user_text.erase("IsolateLock"); ++unlocked; }
  ctx.Print("UnisolateLock: " + std::to_string(unlocked) + " object(s) unlocked");
}

// JoinCopy: joins copies of the selected curves (end to end) / surfaces
// and breps (naked-edge join) and leaves the originals untouched.
void JoinCopy(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<SceneObject> curves, breps, meshes;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) {
    if (o->kind == ObjectKind::Curve) curves.push_back(*o);
    else if (o->kind == ObjectKind::Brep || o->kind == ObjectKind::Surface) breps.push_back(*o);
    else if (o->kind == ObjectKind::Mesh) meshes.push_back(*o);
  }
  ctx.Doc().BeginChange("JoinCopy");
  int made = 0;
  const double tol = ctx.Settings().absolute_tolerance * 10;
  if (curves.size() >= 2) {
    ON_PolyCurve pc;
    std::vector<size_t> remaining;
    for (size_t i = 1; i < curves.size(); ++i) remaining.push_back(i);
    pc.Append(new ON_NurbsCurve(curves[0].curve->raw()));
    bool progress = true;
    while (progress && !remaining.empty()) {
      progress = false;
      for (size_t i = 0; i < remaining.size(); ++i) {
        ON_NurbsCurve c = curves[remaining[i]].curve->raw();
        if (c.PointAtStart().DistanceTo(pc.PointAtEnd()) <= tol) pc.Append(new ON_NurbsCurve(c));
        else if (c.PointAtEnd().DistanceTo(pc.PointAtEnd()) <= tol) { c.Reverse(); pc.Append(new ON_NurbsCurve(c)); }
        else if (c.PointAtEnd().DistanceTo(pc.PointAtStart()) <= tol) pc.Prepend(new ON_NurbsCurve(c));
        else if (c.PointAtStart().DistanceTo(pc.PointAtStart()) <= tol) { c.Reverse(); pc.Prepend(new ON_NurbsCurve(c)); }
        else continue;
        remaining.erase(remaining.begin() + static_cast<long>(i));
        progress = true;
        break;
      }
    }
    kernel::NurbsCurve k;
    if (pc.Count() >= 2 && CurveFromON(pc, k)) { AddLike(ctx, SceneObject::MakeCurve(k), Attrs::Of(curves[0])); ++made; ctx.Print("JoinCopy: " + std::to_string(pc.Count()) + " curve copies joined into one"); }
    else ctx.Warn("JoinCopy: curve ends do not meet");
  }
  if (breps.size() >= 2) {
    ON_Brep* b = new ON_Brep();
    for (const SceneObject& o : breps) {
      if (o.kind == ObjectKind::Brep) b->Append(o.brep->raw());
      else { ON_Brep tmp; ON_NurbsSurface* ns = new ON_NurbsSurface(o.surface->raw()); tmp.Create(ns); b->Append(tmp); }
    }
    const int joined = JoinNakedEdges(*b, tol);
    AddLike(ctx, SceneObject::MakeBrep(WrapBrep(b)), Attrs::Of(breps[0]));
    ++made;
    ctx.Print("JoinCopy: " + std::to_string(breps.size()) + " surface copies joined (" + std::to_string(joined) + " shared edge(s))");
  }
  if (meshes.size() >= 2) {
    std::vector<kernel::Mesh> ms;
    for (const SceneObject& o : meshes) ms.push_back(*o.mesh);
    AddLike(ctx, SceneObject::MakeMesh(kernel::Mesh::MergeAndWeld(ms, tol)), Attrs::Of(meshes[0]));
    ++made;
    ctx.Print("JoinCopy: " + std::to_string(meshes.size()) + " mesh copies merged");
  }
  if (!made) ctx.Warn("JoinCopy: select at least two curves, surfaces or meshes");
}

void MatchProperties(CommandContext& ctx, const std::vector<ObjectId>& targets, const std::vector<ObjectId>& sources) {
  const Opts o = TakeOptions(ctx);
  const SceneObject* src = sources.empty() ? nullptr : ctx.Doc().Find(sources.front());
  if (!src) { ctx.Warn("Select the source object"); return; }
  const Attrs a = Attrs::Of(*src);
  const bool layer = OptYes(o, "Layer", true), color = OptYes(o, "Color", true), material = OptYes(o, "Material", true), linetype = OptYes(o, "Linetype", true), name = OptYes(o, "Name", false), user = OptYes(o, "UserText", false);
  ctx.Doc().BeginChange("MatchProperties");
  int n = 0;
  for (ObjectId id : targets) {
    SceneObject* t = ctx.Doc().Find(id);
    if (!t || id == src->id) continue;
    if (layer) t->layer_index = a.layer;
    if (color) { t->color = a.color; t->color_by_layer = a.by_layer; }
    if (material) t->material_name = a.material;
    if (linetype) t->linetype = a.linetype;
    if (name) t->name = a.name;
    if (user) t->user_text = a.user_text;
    t->InvalidateDisplay();
    ++n;
  }
  std::string what;
  for (const auto& [on, label] : std::vector<std::pair<bool, const char*>>{{layer, "layer"}, {color, "color"}, {material, "material"}, {linetype, "linetype"}, {name, "name"}, {user, "user text"}}) if (on) what += (what.empty() ? "" : ", ") + std::string(label);
  ctx.Print("MatchProperties: " + what + " of object " + std::to_string(src->id) + " applied to " + std::to_string(n) + " object(s)");
}

// ---------------------------------------------------------------------------
// View / window commands
// ---------------------------------------------------------------------------

void MoveTargetToObjects(CommandContext& ctx) {
  Viewport* vp = ctx.ActiveViewport();
  if (!vp) return;
  kernel::BoundingBox box;
  std::vector<ObjectId> sel = ctx.Selected();
  const bool ok = sel.empty() ? ctx.Doc().VisibleBoundingBox(box) : ctx.Doc().BoundingBoxOf(sel, box);
  if (!ok) { ctx.Warn("Nothing to target"); return; }
  const Point3d c = BoxCenter(box);
  CameraState& s = vp->GetCamera().State();
  s.target = c;
  ctx.Print("MoveTargetToObjects: " + vp->Name() + " target moved to " + FormatPoint(c) + (sel.empty() ? " (all visible objects)" : " (" + std::to_string(sel.size()) + " selected object(s))"));
  ctx.RequestRedraw();
}

void SynchronizeViews(CommandContext& ctx) {
  TakeOptions(ctx);
  Viewport* active = ctx.ActiveViewport();
  if (!active) return;
  const CameraState src = active->GetCamera().State();
  int n = 0;
  for (auto& vp : ctx.Viewports()) {
    if (vp.get() == active) continue;
    CameraState& s = vp->GetCamera().State();
    if (s.perspective) continue;  // like Rhino, perspective views are left alone
    const Vector3d back = s.eye - s.target;
    s.target = src.target;
    s.eye = src.target + back;
    s.ortho_height = src.ortho_height;
    ++n;
  }
  ctx.Print("SynchronizeViews: " + std::to_string(n) + " parallel viewport(s) set to the centre and scale of " + active->Name());
  ctx.RequestRedraw();
}

void ClearAllObjectDisplayModes(CommandContext& ctx) {
  int n = 0;
  for (SceneObject& o : ctx.Doc().Objects()) {
    if (o.analysis.mode == AnalysisMode::None && !o.highlight_edges && !o.show_control_points && !o.show_control_net) continue;
    o.analysis.mode = AnalysisMode::None; o.highlight_edges = false; o.show_control_points = false; o.show_control_net = false;
    o.InvalidateDisplay();
    ++n;
  }
  ctx.Print("ClearAllObjectDisplayModes: " + std::to_string(n) + " object(s) reset to the viewport display mode");
  ctx.RequestRedraw();
}

// Window layouts: the ImGui docking ini plus the panel visibility flags.
struct PanelFlag { const char* name; bool PanelState::*flag; };
const PanelFlag kPanelFlags[] = {
    {"layers", &PanelState::layers}, {"properties", &PanelState::properties}, {"command_history", &PanelState::command_history},
    {"command_list", &PanelState::command_list}, {"help", &PanelState::help}, {"notifications", &PanelState::notifications},
    {"named_views", &PanelState::named_views}, {"notes", &PanelState::notes}, {"document_user_text", &PanelState::document_user_text},
    {"materials", &PanelState::materials}, {"display", &PanelState::display}, {"object_snaps", &PanelState::object_snaps},
    {"toolbars", &PanelState::toolbars}, {"calculator", &PanelState::calculator}, {"layer_state_manager", &PanelState::layer_state_manager},
    {"lights", &PanelState::lights}, {"rendering", &PanelState::rendering}, {"linetypes", &PanelState::linetypes},
    {"clipping_planes", &PanelState::clipping_planes}, {"layouts", &PanelState::layouts}, {"named_cplanes", &PanelState::named_cplanes},
};

std::filesystem::path LayoutDir() { return std::filesystem::path(ConfigDirectory()) / "window_layouts"; }

void SaveWindowLayout(CommandContext& ctx, const std::string& name) {
  if (name.empty()) { ctx.Warn("Type a layout name"); return; }
  std::error_code ec;
  std::filesystem::create_directories(LayoutDir(), ec);
  const std::filesystem::path base = LayoutDir() / name;
  std::ofstream out(base.string() + ".panels");
  if (!out) { ctx.Warn("SaveWindowLayout: could not write " + base.string() + ".panels"); return; }
  const PanelState& p = ctx.App().Panels();
  for (const PanelFlag& f : kPanelFlags) out << f.name << "=" << (p.*f.flag ? 1 : 0) << "\n";
  out << "left_sidebar=" << (ctx.App().show_left_sidebar ? 1 : 0) << "\n";
  out << "toolbar_tab=" << ctx.App().toolbar_tab << "\n";
  out.close();
  bool ini = false;
  if (ImGui::GetCurrentContext()) { ImGui::SaveIniSettingsToDisk((base.string() + ".ini").c_str()); ini = std::filesystem::exists(base.string() + ".ini"); }
  ctx.Print("SaveWindowLayout: saved " + name + " to " + LayoutDir().string() + (ini ? " (panels + docking)" : " (panels)"));
}

void WindowLayout(CommandContext& ctx, const std::string& name) {
  std::vector<std::string> plain;
  const Opts o = TakeOptions(ctx, &plain);
  std::string n = name.empty() && !plain.empty() ? plain.front() : name;
  if (n.empty() || Lower(n) == "list") {
    std::string list;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(LayoutDir(), ec)) if (e.path().extension() == ".panels") list += (list.empty() ? "" : ", ") + e.path().stem().string();
    ctx.Print("WindowLayout: saved layouts: " + (list.empty() ? std::string("(none; use SaveWindowLayout)") : list));
    return;
  }
  const std::filesystem::path base = LayoutDir() / n;
  std::ifstream in(base.string() + ".panels");
  if (!in) { ctx.Warn("WindowLayout: no saved layout named '" + n + "'"); return; }
  PanelState& p = ctx.App().Panels();
  std::string line;
  int applied = 0;
  while (std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
    const bool on = val == "1";
    bool found = false;
    for (const PanelFlag& f : kPanelFlags) if (key == f.name) { p.*f.flag = on; found = true; break; }
    if (key == "left_sidebar") { ctx.App().show_left_sidebar = on; found = true; }
    if (key == "toolbar_tab") { ctx.App().toolbar_tab = std::atoi(val.c_str()); found = true; }
    if (found) ++applied;
  }
  bool ini = false;
  if (ImGui::GetCurrentContext() && std::filesystem::exists(base.string() + ".ini")) { ImGui::LoadIniSettingsFromDisk((base.string() + ".ini").c_str()); ctx.App().has_saved_layout = true; ini = true; }
  ctx.Print("WindowLayout: restored " + n + " (" + std::to_string(applied) + " panel setting(s)" + (ini ? ", docking layout" : "") + ")");
}

// ---------------------------------------------------------------------------
// Drafting / files / misc
// ---------------------------------------------------------------------------

void ChangeSpace(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  const Layout* layout = ctx.App().ActiveLayout();
  ctx.Doc().BeginChange("ChangeSpace");
  int n = 0;
  if (layout) {
    std::vector<SceneObject> copies;
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) { SceneObject c = *o; c.id = kNoObject; c.selected = false; c.user_text["Space"] = layout->name; copies.push_back(std::move(c)); }
    for (SceneObject& c : copies) { ctx.Doc().Add(std::move(c)); ++n; }
    ctx.Print("ChangeSpace: " + std::to_string(n) + " object(s) copied to layout " + layout->name + " (tagged Space=" + layout->name + ")");
  } else {
    for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) if (o->user_text.erase("Space")) ++n;
    ctx.Print("ChangeSpace: " + std::to_string(n) + " object(s) moved back to model space (Space tag removed)");
  }
}

void DecimalPoint(CommandContext& ctx) {
  std::vector<std::string> plain;
  const Opts o = TakeOptions(ctx, &plain);
  bool& comma = ctx.App().State().decimal_comma;
  std::string v = Lower(OptStr(o, "Separator", plain.empty() ? "" : plain.front()));
  if (v == "comma" || v == ",") comma = true;
  else if (v == "point" || v == "period" || v == ".") comma = false;
  else comma = !comma;
  SetDecimalComma(comma);
  ctx.Print(std::string("DecimalPoint: numbers print with a decimal ") + (comma ? "comma (1,5)" : "point (1.5)"));
}

void Rescue3dmFile(CommandContext& ctx) {
  std::vector<std::string> plain;
  TakeOptions(ctx, &plain);
  auto run = [&ctx](const std::string& path) {
    Document tmp;
    std::string err;
    const bool ok = Load3dm(tmp, path, err);
    if (tmp.ObjectCount() == 0) { ctx.Warn("Rescue3dmFile: nothing could be recovered from " + path + (err.empty() ? "" : " (" + err + ")")); return; }
    ctx.Doc().BeginChange("Rescue3dmFile");
    int n = 0;
    for (const SceneObject& o : tmp.Objects()) {
      SceneObject c = o;
      c.id = kNoObject; c.selected = false;
      const std::string lname = o.layer_index >= 0 && o.layer_index < static_cast<int>(tmp.Layers().size()) ? tmp.Layers()[static_cast<size_t>(o.layer_index)].name : "Default";
      int idx = ctx.Doc().FindLayer(lname);
      if (idx < 0) idx = ctx.Doc().AddLayer(lname);
      c.layer_index = idx;
      ctx.Doc().Add(std::move(c));
      ++n;
    }
    ctx.Print("Rescue3dmFile: recovered " + std::to_string(n) + " object(s) from " + path + (ok ? "" : " (the file reported errors: " + err + ")"));
    ctx.ZoomExtentsAll();
  };
  if (!plain.empty()) { run(plain.front()); return; }
  ctx.App().ShowFileDialog("Rescue a damaged .3dm file", {".3dm"}, false, run);
}

void ExportRuiFile(CommandContext& ctx) {
  std::vector<std::string> plain;
  TakeOptions(ctx, &plain);
  auto run = [&ctx](std::string path) {
    if (std::filesystem::path(path).extension().empty()) path += ".rui";
    std::ofstream out(path);
    if (!out) { ctx.Warn("ExportRuiFile: could not write " + path); return; }
    out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<RhinoUI major_ver=\"3\" minor_ver=\"0\" application=\"Dino 8\">\n  <tool_bar_collection>\n";
    int buttons = 0;
    for (int t = 0; t < ToolbarTabCount(); ++t) {
      out << "    <tool_bar name=\"" << ToolbarTabName(t) << "\">\n";
      for (const std::string& c : ToolbarTabCommands(ctx.App(), t)) {
        if (c == "|") { out << "      <separator/>\n"; continue; }
        const char* label = ToolbarButtonLabel(c);
        out << "      <tool_bar_item command=\"" << c << "\" text=\"" << (label ? label : c.c_str()) << "\" macro=\"_" << c << "\"/>\n";
        ++buttons;
      }
      out << "    </tool_bar>\n";
    }
    out << "  </tool_bar_collection>\n  <sidebar>\n";
    out << "  </sidebar>\n</RhinoUI>\n";
    ctx.Print("ExportRuiFile: wrote " + std::to_string(ToolbarTabCount()) + " toolbar(s), " + std::to_string(buttons) + " button(s) to " + path);
  };
  if (!plain.empty()) { run(plain.front()); return; }
  ctx.App().ShowFileDialog("Export toolbar layout", {".rui"}, true, run);
}

void ExportBitmaps(CommandContext& ctx) {
  std::vector<std::string> plain;
  TakeOptions(ctx, &plain);
  auto run = [&ctx](const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int copied = 0, missing = 0;
    for (const Material& m : ctx.Doc().Materials()) {
      if (m.texture_path.empty()) continue;
      const std::filesystem::path src = m.texture_path;
      if (!std::filesystem::exists(src)) { ++missing; continue; }
      std::filesystem::copy_file(src, std::filesystem::path(dir) / src.filename(), std::filesystem::copy_options::overwrite_existing, ec);
      if (!ec) ++copied;
    }
    ctx.Print("ExportBitmaps: " + std::to_string(copied) + " texture file(s) copied to " + dir + (missing ? " (" + std::to_string(missing) + " missing)" : "") + ". Textures are referenced by path, not embedded, so this is the complete bitmap set.");
  };
  if (!plain.empty()) { run(plain.front()); return; }
  ctx.App().ShowFileDialog("Folder for the bitmaps (pick any file name in it)", {".png"}, true, [run](const std::string& p) { run(std::filesystem::path(p).parent_path().string()); });
}

CommandFactory Say(const char* text) { return Immediate([text](CommandContext& ctx) { TakeOptions(ctx); ctx.Print(text); }); }

}  // namespace

void RegisterRemainingCommands(CommandEngine& e) {
  // ---- curves ----
  Reg(e, "Convert", OnSelection("Select curves to convert", ConvertCurves), CommandStatus::Implemented,
      "Output=Lines makes a polyline, Output=Arcs an arc chain (Tolerance= sets the fit).");
  Reg(e, "MatchCrvDir", OnSelection("Select curves; the last one is the reference direction", MatchCrvDir, 2), CommandStatus::Implemented,
      "Reverses curves whose direction opposes the last selected curve.");
  Reg(e, "OffsetNormal", Make<ObjectsNumberCommand>("Select a curve on a surface and the surface", "Offset distance", 1.0, OffsetNormal), CommandStatus::Implemented,
      "Samples the curve and moves each sample along the surface normal at its closest point.");
  Reg(e, "PointCloudContour", Make<ObjectsNumberCommand>("Select the points of the cloud", "Contour spacing", 1.0, PointCloudContour, std::vector<OptionSpec>{OptionSpec{"Band", "", {}, true, false}}), CommandStatus::Implemented,
      "Points in a band around each CPlane-parallel slice become a closed polyline ordered around their centroid (star-shaped sections only).");
  Reg(e, "PointCloudSection", Make<ObjectsPointsCommand>("Select the points of the cloud", "Point on the section plane (parallel to the CPlane)", 1, PointCloudSection), CommandStatus::Implemented,
      "One CPlane-parallel section through the picked point; star-shaped sections only.");
  Reg(e, "RebuildCrvNonUniform", Make<ObjectsNumberCommand>("Select curves to rebuild", "Tolerance", 0.01, RebuildCrvNonUniform), CommandStatus::Implemented,
      "Refits a cubic with knots where the curve bends (adaptive samples within the tolerance).");
  Reg(e, "RibbonOffset", Make<ObjectsNumberCommand>("Select curves", "Ribbon width", 1.0, RibbonOffset), CommandStatus::Implemented,
      "Offsets in the CPlane and adds the ruled surface between the curve and its offset.");
  Reg(e, "ShortPath", Make<ObjectsPointsCommand>("Select a surface", "Path start / end on the surface", 2, ShortPath), CommandStatus::Implemented,
      "Shortest path over the surface's 40 x 40 tessellation, smoothed and re-projected; not an exact geodesic.");
  Reg(e, "AlignProfiles", OnSelection("Select profile curves in loft order", AlignProfiles, 2), CommandStatus::Implemented,
      "Reverses curves and moves closed-curve seams to match the first profile before Loft; no interactive seam dragging.");
  Reg(e, "ReducePointCloud", Make<ObjectsNumberCommand>("Select the points of the cloud", "Percentage of points to remove", 50, [](CommandContext& ctx, const std::vector<ObjectId>& ids, double pct, const Opts&) {
        std::vector<ObjectId> pts;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) pts.push_back(id);
        if (pts.empty()) { ctx.Warn("Select point objects"); return; }
        const double frac = std::clamp(pct, 0.0, 100.0) / 100.0;
        ctx.Doc().BeginChange("ReducePointCloud");
        int removed = 0;
        double acc = 0;
        for (ObjectId id : pts) { acc += frac; if (acc >= 1.0) { acc -= 1.0; ctx.Doc().Remove(id); ++removed; } }
        ctx.Print("ReducePointCloud: removed " + std::to_string(removed) + " of " + std::to_string(pts.size()) + " point(s) (" + FormatNumber(pct) + "%)");
      }, std::vector<OptionSpec>{}, true), CommandStatus::Implemented, "Removes an evenly spread percentage of the selected point objects.");

  // ---- surfaces ----
  Reg(e, "TweenSurfaces", OnSelection("Select two surfaces", TweenSurfaces, 2), CommandStatus::Implemented,
      "Count= surfaces interpolated between 12 x 12 sample grids of the two inputs (grids auto-aligned).");
  Reg(e, "DevLoft", OnSelection("Select two rail curves", DevLoft, 2), CommandStatus::Implemented,
      "Ruled surface whose rulings are chosen to minimise the twist between the rails; approximately developable.");
  Reg(e, "ExtrudeSrfAlongCrv", Make<ExtrudeSrfCommand>(ExtrudeSrfCommand::Kind::AlongCrv), CommandStatus::Implemented,
      "Closed mesh solid: the surface's render mesh swept along the path (translation only).");
  Reg(e, "ExtrudeSrfTapered", Make<ExtrudeSrfCommand>(ExtrudeSrfCommand::Kind::Tapered), CommandStatus::Implemented,
      "Closed mesh solid: the surface's render mesh extruded along the CPlane normal and scaled by DraftAngle=.");
  Reg(e, "ExtrudeSrfToPoint", Make<ExtrudeSrfCommand>(ExtrudeSrfCommand::Kind::ToPoint), CommandStatus::Implemented,
      "Closed mesh solid: the surface's render mesh coned to the picked apex.");
  Reg(e, "MultiPipe", Make<ObjectsNumberCommand>("Select curves to pipe", "Radius", 1.0, MultiPipe), CommandStatus::Implemented,
      "Capped mesh pipes along every curve, unioned into one mesh (merged unjoined if a union fails).");

  // ---- analysis ----
  Reg(e, "ThicknessAnalysis", Immediate(ThicknessAnalysisOn), CommandStatus::Implemented,
      "Colours each display vertex by the distance through the object along -normal (Min= Max= range; red thin, blue thick).");
  Reg(e, "ThicknessAnalysisOff", Immediate(ThicknessAnalysisOff));
  Reg(e, "EdgeContinuity", OnSelection("Select two curves or surface edges", EdgeContinuity, 2), CommandStatus::Implemented,
      "Samples one edge against the closest points of the other: gap, tangent angle, curvature difference; hairs drawn as an overlay.");
  Reg(e, "GCon", OnSelection("Select two curves", GCon, 2), CommandStatus::Implemented, "Continuity (G0/G1/G2) at the nearest pair of curve ends.");
  Reg(e, "EvaluateUVPt", Make<UVPointsCommand>(false), CommandStatus::Implemented, "Surface point at typed u,v (Normalized=Yes for 0..1).");
  Reg(e, "PointsFromUV", Make<UVPointsCommand>(true), CommandStatus::Implemented, "Points at a typed list of u,v parameters (Enter ends).");
  Reg(e, "DraftAnglePoint", Make<ObjectsPointsCommand>("Select a surface", "Point on the surface", 1, DraftAnglePoint), CommandStatus::Implemented,
      "Draft angle at the picked surface point relative to the DraftAngleAnalysis pull direction (world Z by default).");
  Reg(e, "CurvatureGraph", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { CurvatureGraph(ctx, ids, false); }), CommandStatus::Implemented,
      "On-screen curvature comb (Scale= Density=); stays until CurvatureGraphOff.");
  Reg(e, "CurvatureGraphOff", Immediate([](CommandContext& ctx) { const bool was = !ctx.App().overlay_lines.empty(); ctx.App().overlay_lines.clear(); ctx.Print(std::string("CurvatureGraph off") + (was ? "" : " (nothing was displayed)")); ctx.RequestRedraw(); }));
  Reg(e, "ExtractCurvatureGraph", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { CurvatureGraph(ctx, ids, true); }), CommandStatus::Implemented,
      "The curvature comb as curve objects (Scale= Density=).");
  Reg(e, "CheckNewObjects", Immediate([](CommandContext& ctx) {
        std::vector<std::string> plain;
        const Opts o = TakeOptions(ctx, &plain);
        bool& on = ctx.App().State().check_new_objects;
        const std::string v = Lower(OptStr(o, "Enable", plain.empty() ? "" : plain.front()));
        on = v.empty() ? !on : YesNo(v, on);
        ctx.Print(std::string("CheckNewObjects ") + (on ? "on: every object a command adds is validated and invalid ones are reported" : "off"));
      }), CommandStatus::Implemented);
  Reg(e, "ClearAnalysisMeshes", Immediate([](CommandContext& ctx) {
        int n = 0;
        for (SceneObject& o : ctx.Doc().Objects()) if (SurfaceLike(o)) { o.InvalidateDisplay(); ++n; }
        ctx.Print("ClearAnalysisMeshes: display and analysis meshes of " + std::to_string(n) + " object(s) discarded; they rebuild on the next redraw");
        ctx.RequestRedraw();
      }));

  // ---- meshes ----
  Reg(e, "AlignVertices", Make<ObjectsNumberCommand>("Select meshes", "Distance", 0.01, AlignVertices), CommandStatus::Implemented,
      "Merges vertices closer than the distance (averaged position).");
  Reg(e, "MeshPolyline", OnSelection("Select closed planar polylines", MeshPolyline), CommandStatus::Implemented, "Mesh face(s) filling a closed planar curve.");
  Reg(e, "WeldEdge", Make<ObjectsPointsCommand>("Select a mesh", "Point near the edge to weld", 1, WeldEdge), CommandStatus::Implemented,
      "Welds the duplicate vertices at both ends of the mesh edge nearest the pick.");
  Reg(e, "SelConnectedMeshFaces", Make<ObjectsPointsCommand>("Select a mesh", "Point near a face", 1, SelConnectedMeshFaces), CommandStatus::Implemented,
      "No per-face sub-selection exists, so a mesh with several disconnected parts has the picked part split off into its own object and selected; a single-part mesh is selected whole.");
  Reg(e, "CollapseMeshFacesByArea", Make<ObjectsNumberCommand>("Select meshes", "Collapse faces with area below", 0.01, [](CommandContext& ctx, const std::vector<ObjectId>& ids, double v, const Opts&) { CollapseFaces(ctx, ids, v, 0); }), CommandStatus::Implemented,
      "Faces smaller than the area collapse to their centre.");
  Reg(e, "CollapseMeshFacesByAspectRatio", Make<ObjectsNumberCommand>("Select meshes", "Collapse faces with aspect ratio above", 10, [](CommandContext& ctx, const std::vector<ObjectId>& ids, double v, const Opts&) { CollapseFaces(ctx, ids, v, 1); }), CommandStatus::Implemented,
      "Faces whose longest/shortest edge ratio exceeds the value lose their shortest edge.");
  Reg(e, "CollapseMeshFacesByEdgeLength", Make<ObjectsNumberCommand>("Select meshes", "Collapse edges shorter than", 0.01, [](CommandContext& ctx, const std::vector<ObjectId>& ids, double v, const Opts&) { CollapseFaces(ctx, ids, v, 2); }), CommandStatus::Implemented,
      "Edges shorter than the value collapse to their midpoint.");
  Reg(e, "ExtractDuplicateMeshFaces", OnSelection("Select meshes", ExtractDuplicateMeshFaces), CommandStatus::Implemented,
      "Faces sharing the same vertex positions are moved into a new mesh.");
  Reg(e, "SplitMeshWithCurve", OnSelection("Select a mesh and a cutting curve", SplitMeshWithCurve, 2), CommandStatus::Implemented,
      "Splits by the curve's plane (a line is extruded along the CPlane normal); non-planar curves use their best-fit plane.");
  Reg(e, "ExtractUVMesh", OnSelection("Select surfaces or textured meshes", ExtractUVMesh), CommandStatus::Implemented,
      "A flat mesh in the surface's UV space (scaled to its approximate size) or at a mesh's texture coordinates.");
  Reg(e, "CreaseSplitting", Immediate([](CommandContext& ctx) {
        std::vector<std::string> plain;
        const Opts o = TakeOptions(ctx, &plain);
        bool& on = ctx.App().State().crease_splitting;
        const std::string v = Lower(OptStr(o, "Enable", plain.empty() ? "" : plain.front()));
        on = v.empty() ? !on : YesNo(v, on);
        ctx.Print(std::string("CreaseSplitting ") + (on ? "on: AutomaticSubDFromMesh creases edges sharper than its Angle" : "off: AutomaticSubDFromMesh makes smooth SubDs"));
      }), CommandStatus::Implemented, "Toggles whether AutomaticSubDFromMesh creases sharp edges; surfaces are not split at kinks.");

  // ---- layers / editing ----
  Reg(e, "DupLayer", Immediate(DupLayer), CommandStatus::Implemented,
      "Copies the current layer (or Layer=name) under a new name (Name=); Objects=No skips copying its objects.");
  Reg(e, "CopyToLayer", Make<ObjectsTextCommand>("Select objects to copy", "Layer name", CopyToLayer), CommandStatus::Implemented, "Copies the objects onto the named layer (created if missing).");
  Reg(e, "HighlightObjectLayers", OnSelection("Select objects", HighlightObjectLayers), CommandStatus::Implemented, "Highlights the selected objects' layers in the Layers panel.");
  Reg(e, "LayerBook", Immediate(LayerBook), CommandStatus::Implemented, "Shows one layer at a time (LayerBook Next/Previous/First/Last/N/All); no slide-show window.");
  Reg(e, "IsolateLock", OnSelection("Select objects to keep editable", IsolateLock), CommandStatus::Implemented, "Locks every other visible object; UnisolateLock unlocks them.");
  Reg(e, "UnisolateLock", Immediate(UnisolateLock));
  Reg(e, "JoinCopy", OnSelection("Select objects to join copies of", JoinCopy, 2), CommandStatus::Implemented, "Joins copies of the selection; the originals stay.");
  Reg(e, "MatchProperties", Make<TwoSelectionsCommand>("Select objects to change", "Select the source object", MatchProperties), CommandStatus::Implemented,
      "Copies layer, colour, material and linetype (Layer= Color= Material= Linetype= Name= UserText= Yes/No).");

  // ---- view / window ----
  Reg(e, "MoveTargetToObjects", Immediate(MoveTargetToObjects), CommandStatus::Implemented, "Camera target to the centre of the selection (or of everything visible).");
  Reg(e, "Synchronize Views", Immediate(SynchronizeViews), CommandStatus::Implemented, "Parallel viewports take the active view's centre and scale.");
  Reg(e, "SynchronizeViews", Immediate(SynchronizeViews), CommandStatus::Implemented, "Parallel viewports take the active view's centre and scale.");
  Reg(e, "DisplayProperties", Immediate([](CommandContext& ctx) { ctx.App().Panels().display = true; ctx.Print("DisplayProperties: Display panel opened"); }));
  Reg(e, "ClearAllObjectDisplayModes", Immediate(ClearAllObjectDisplayModes), CommandStatus::Implemented,
      "Clears per-object analysis, edge and control-point display; per-object shading modes are not stored in this build.");
  Reg(e, "SaveWindowLayout", [] () -> std::unique_ptr<Command> {
        class C : public Command {
          void Begin(CommandContext& ctx) override { if (auto t = ctx.Engine().TakePendingInput()) { SaveWindowLayout(ctx, *t); Finish(); return; } WantText("Layout name"); }
          void OnText(CommandContext& ctx, const std::string& t) override { SaveWindowLayout(ctx, t); Finish(); }
        };
        return std::make_unique<C>();
      }, CommandStatus::Implemented, "Saves the panel visibility and docking layout under a name in the config folder.");
  Reg(e, "WindowLayout", [] () -> std::unique_ptr<Command> {
        class C : public Command {
          void Begin(CommandContext& ctx) override { WindowLayout(ctx, ""); Finish(); }
        };
        return std::make_unique<C>();
      }, CommandStatus::Implemented, "Restores a saved window layout by name (WindowLayout List shows the saved ones).");
  Reg(e, "PopupToolbar", Immediate([](CommandContext& ctx) { ctx.App().OpenPopupToolbar(); ctx.Print("PopupToolbar: popup toolbar opened at the cursor"); }));

  // ---- drafting / files / misc ----
  Reg(e, "ChangeSpace", OnSelection("Select objects to move between model and layout space", ChangeSpace), CommandStatus::Implemented,
      "With a layout active, copies the objects tagged with the layout name; in model space removes the tag. Objects are not drawn per page.");
  Reg(e, "DecimalPoint", Immediate(DecimalPoint), CommandStatus::Implemented, "Toggles the decimal separator of printed numbers (Separator=Comma/Point).");
  Reg(e, "AcadSchemes", Say("AcadSchemes: DWG/DXF export schemes are not available; Dino 8 exports .3dm, OBJ, STL, PLY, SVG and PDF (see Export)."), CommandStatus::Partial);
  Reg(e, "Rescue3dmFile", Immediate(Rescue3dmFile), CommandStatus::Implemented,
      "Reads what OpenNURBS can still parse from a damaged .3dm and adds the recovered objects; no chunk-level repair.");
  Reg(e, "ExportBitmaps", Immediate(ExportBitmaps), CommandStatus::Implemented, "Copies every referenced material texture into a folder (textures are never embedded).");
  Reg(e, "ExportRuiFile", Immediate(ExportRuiFile), CommandStatus::Implemented, "Writes the toolbar tabs and buttons as a small .rui-style XML file.");
  Reg(e, "AttachGHSData", Say("AttachGHSData: GHS hydrostatics data is not supported; use Hydrostatics for volume and centroid."), CommandStatus::Partial);
  Reg(e, "Unwrap", Say("Unwrap: UV unwrapping is not available; use ExtractUVMesh for the surface's UV layout and ApplyPlanarMapping/ApplyBoxMapping for textures."), CommandStatus::Partial);
  Reg(e, "UVEditor", Say("UVEditor: there is no UV editor; mapping is set per object with ApplyPlanarMapping, ApplyBoxMapping, ApplyCylindricalMapping and ApplySphericalMapping."), CommandStatus::Partial);
  Reg(e, "ApplyOcsMapping", Say("ApplyOcsMapping: object-coordinate-system mapping is not available; ApplyPlanarMapping uses the object's bounding box."), CommandStatus::Partial);
  Reg(e, "ExtractCustomMappingObject", Say("ExtractCustomMappingObject: custom mapping objects do not exist in this build; mappings are bounding-box projections."), CommandStatus::Partial);
  for (const char* n : {"IgesImportOptions", "IGESStudy", "ReadEveryIGESEntity", "SetIgesLayerLevelMap"})
    Reg(e, n, Say("IGES is not supported: Dino 8 has no IGES reader or writer. Exchange geometry as .3dm, OBJ, STL or PLY."), CommandStatus::Partial);
  for (const char* n : {"STEPTree", "StepUnitsAndTolerance"})
    Reg(e, n, Say("STEP is not supported: Dino 8 has no STEP reader or writer. Exchange geometry as .3dm, OBJ, STL or PLY."), CommandStatus::Partial);
  for (const char* n : {"EditPythonScript", "EditScript", "LoadScript"})
    Reg(e, n, Immediate([n](CommandContext& ctx) { TakeOptions(ctx); ctx.App().Panels().macro_editor = true; ctx.Print(std::string(n) + ": there is no Python/RhinoScript engine; the Macro editor runs command scripts (see Macro, ReadCommandFile)."); }), CommandStatus::Partial);
}

}  // namespace dino8::app
