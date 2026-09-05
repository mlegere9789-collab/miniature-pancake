// Surface creation and editing commands: Sweep1, Sweep2, NetworkSrf, Patch,
// Pipe, OffsetSrf, Shell, ExtrudeCrvAlongCrv, ExtrudeCrvTapered, Project, Pull.
//
// OpenNURBS ships no sweep/network/offset surface fitters, so most of these
// build a grid of transported profile points (rotation-minimizing frames
// along the rail) and turn it into a degree-3 NURBS surface whose control
// points are those samples. That is an approximation of the true sweep:
// exact for planes/lines and for the translational cases that map onto
// ON_SumSurface / ON_NurbsSurface::CreateRuledSurface, close (sub-tolerance
// for reasonable sample counts) elsewhere. Each registration note says so.
#include <algorithm>
#include <limits>

#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

using Row = std::vector<Point3d>;

constexpr int kRailSamples = 48;     // rows along a curved rail/path
constexpr int kProfileSamples = 24;  // columns along a cross-section
constexpr int kRingSegments = 24;    // Pipe cross-section resolution

kernel::Brep WrapBrep(ON_Brep* b) {
  kernel::Brep k;
  if (b) { k.raw() = *b; delete b; }
  return k;
}

// Parameters spaced evenly by arc length. `wrap` omits the final (== first)
// point of a closed curve so the samples can feed a periodic surface.
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

// A moving frame along a rail: origin, unit tangent, normal, binormal.
struct Frame {
  Point3d o;
  Vector3d t, n, b;
  Point3d Place(Vector3d local) const { return o + t * local.x + n * local.y + b * local.z; }
  Vector3d Local(Vector3d d) const { return Vector3d(ON_DotProduct(d, t), ON_DotProduct(d, n), ON_DotProduct(d, b)); }
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
  // Rodrigues for v perpendicular-or-not to a unit axis.
  return v * std::cos(angle) + ON_CrossProduct(axis, v) * std::sin(angle) + axis * (ON_DotProduct(axis, v) * (1 - std::cos(angle)));
}

// Rotation-minimizing frames (double-reflection method, Wang et al. 2008).
// When `close` is set the parameter list ends where it starts and the
// accumulated twist is spread over the loop so the last frame equals the first.
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

// Degree-3 NURBS surface whose control points are rows[v][u]. A periodic
// direction wraps its control points (degree extra copies) and uses a
// periodic knot vector, so closed sweeps have no seam crease.
kernel::NurbsSurface SurfaceFromRows(std::vector<Row> rows, bool periodic_u = false, bool periodic_v = false) {
  const int deg = 3;
  if (periodic_u) for (Row& r : rows) { const Row copy = r; for (int k = 0; k < deg; ++k) r.push_back(copy[static_cast<size_t>(k) % copy.size()]); }
  if (periodic_v) { const std::vector<Row> copy = rows; for (int k = 0; k < deg; ++k) rows.push_back(copy[static_cast<size_t>(k) % copy.size()]); }
  const int nv = static_cast<int>(rows.size()), nu = static_cast<int>(rows[0].size());
  std::vector<Point3d> grid(static_cast<size_t>(nu) * static_cast<size_t>(nv));
  for (int u = 0; u < nu; ++u) for (int v = 0; v < nv; ++v) grid[static_cast<size_t>(u) * nv + v] = rows[static_cast<size_t>(v)][static_cast<size_t>(u)];
  const int du = periodic_u ? deg : std::min(deg, nu - 1), dv = periodic_v ? deg : std::min(deg, nv - 1);
  kernel::NurbsSurface s = kernel::NurbsSurface::FromControlGrid(grid, nu, nv, du, dv);
  if (periodic_u) s.raw().MakePeriodicUniformKnotVector(0);
  if (periodic_v) s.raw().MakePeriodicUniformKnotVector(1);
  return s;
}

// Quad mesh over rows[v][u]; optionally wrapped in u and/or v; optionally
// capped (first/last ring fans, for tubes) — used for Pipe and OffsetSrf.
kernel::Mesh MeshFromRows(const std::vector<Row>& rows, bool wrap_u, bool wrap_v) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const int nv = static_cast<int>(rows.size()), nu = static_cast<int>(rows[0].size());
  for (int v = 0; v < nv; ++v) for (int u = 0; u < nu; ++u) r.SetVertex(v * nu + u, rows[static_cast<size_t>(v)][static_cast<size_t>(u)]);
  int fi = 0;
  const int vu = wrap_u ? nu : nu - 1, vv = wrap_v ? nv : nv - 1;
  for (int v = 0; v < vv; ++v) {
    for (int u = 0; u < vu; ++u) {
      const int u1 = (u + 1) % nu, v1 = (v + 1) % nv;
      r.SetQuad(fi++, v * nu + u, v * nu + u1, v1 * nu + u1, v1 * nu + u);
    }
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

kernel::Mesh Outward(kernel::Mesh m) {
  if (m.IsClosedManifold() && m.Volume() < 0) return m.FlipNormals();
  return m;
}

// Offsets a mesh so that every face plane moves by `d`: each vertex solves
// n_k . v = d over its distinct adjacent face normals (least squares when
// more than three meet), so box corners and creases stay sharp instead of
// shrinking along the averaged vertex normal.
kernel::Mesh OffsetMesh(const kernel::Mesh& in, double d) {
  kernel::Mesh m = in;
  ON_Mesh& r = m.raw();
  r.ComputeFaceNormals();
  std::vector<std::vector<ON_3dVector>> normals(static_cast<size_t>(r.VertexCount()));
  for (int f = 0; f < r.FaceCount(); ++f) {
    const ON_MeshFace& face = r.m_F[f];
    const ON_3dVector n(r.m_FN[f]);
    for (int k = 0; k < (face.IsTriangle() ? 3 : 4); ++k) {
      std::vector<ON_3dVector>& list = normals[static_cast<size_t>(face.vi[k])];
      bool dup = false;
      for (const ON_3dVector& e : list) if (ON_DotProduct(e, n) > 0.9995) { dup = true; break; }
      if (!dup) list.push_back(n);
    }
  }
  const double cap = 3 * std::fabs(d);
  for (int i = 0; i < r.VertexCount(); ++i) {
    const std::vector<ON_3dVector>& ns = normals[static_cast<size_t>(i)];
    if (ns.empty()) continue;
    ON_3dVector v = ON_3dVector::ZeroVector;
    bool solved = false;
    if (ns.size() == 1) { v = ns[0] * d; solved = true; }
    else if (ns.size() == 2) {
      const double c = ON_DotProduct(ns[0], ns[1]);
      if (1 + c > 0.05) { v = (ns[0] + ns[1]) * (d / (1 + c)); solved = true; }
    } else {
      double a[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, rhs[3] = {0, 0, 0};
      for (const ON_3dVector& n : ns) {
        const double c[3] = {n.x, n.y, n.z};
        for (int p = 0; p < 3; ++p) { rhs[p] += d * c[p]; for (int q = 0; q < 3; ++q) a[p][q] += c[p] * c[q]; }
      }
      const double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
      if (std::fabs(det) > 1e-9) {
        auto det3 = [&](int col) {
          double t[3][3];
          for (int p = 0; p < 3; ++p) for (int q = 0; q < 3; ++q) t[p][q] = (q == col) ? rhs[p] : a[p][q];
          return t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) + t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0]);
        };
        v = ON_3dVector(det3(0) / det, det3(1) / det, det3(2) / det);
        solved = true;
      }
    }
    if (!solved) {
      for (const ON_3dVector& n : ns) v += n;
      if (v.Unitize()) v = v * d;
    }
    if (v.Length() > cap) { v.Unitize(); v = v * cap; }
    r.SetVertex(i, r.Vertex(i) + v);
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

// Closed shell between an open mesh and its offset: bottom (flipped), top and
// side quads along every boundary edge. Vertex i of `top` must correspond to
// vertex i of `bottom`.
kernel::Mesh ShellBetween(const kernel::Mesh& bottom, const kernel::Mesh& top) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const ON_Mesh& a = bottom.raw();
  const ON_Mesh& b = top.raw();
  const int n = a.VertexCount();
  for (int i = 0; i < n; ++i) r.SetVertex(i, a.Vertex(i));       // SetVertex only appends in order,
  for (int i = 0; i < n; ++i) r.SetVertex(n + i, b.Vertex(i));   // so fill the two halves separately
  int fi = 0;
  for (int f = 0; f < a.FaceCount(); ++f) {
    const ON_MeshFace& fa = a.m_F[f];
    if (fa.IsTriangle()) { r.SetTriangle(fi++, fa.vi[2], fa.vi[1], fa.vi[0]); r.SetTriangle(fi++, n + fa.vi[0], n + fa.vi[1], n + fa.vi[2]); }
    else { r.SetQuad(fi++, fa.vi[3], fa.vi[2], fa.vi[1], fa.vi[0]); r.SetQuad(fi++, n + fa.vi[0], n + fa.vi[1], n + fa.vi[2], n + fa.vi[3]); }
  }
  // Boundary edges: directed edges used by exactly one face (in face winding).
  std::vector<std::pair<int, int>> edges;
  for (int f = 0; f < a.FaceCount(); ++f) {
    const ON_MeshFace& fa = a.m_F[f];
    const int k = fa.IsTriangle() ? 3 : 4;
    for (int e = 0; e < k; ++e) edges.push_back({fa.vi[e], fa.vi[(e + 1) % k]});
  }
  std::sort(edges.begin(), edges.end());
  for (const auto& [p, q] : edges) {
    if (p == q) continue;
    if (std::binary_search(edges.begin(), edges.end(), std::make_pair(q, p))) continue;  // interior
    r.SetQuad(fi++, p, q, n + q, n + p);
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return Outward(kernel::Mesh::MergeAndWeld({m}, 1e-9));
}

// Least-squares plane through points (PCA via Jacobi on the 3x3 covariance).
ON_Plane FitPlane(const Row& pts) {
  const Point3d c = Centroid(pts);
  double a[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  for (const Point3d& p : pts) {
    const double d[3] = {p.x - c.x, p.y - c.y, p.z - c.z};
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) a[i][j] += d[i] * d[j];
  }
  double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int sweep = 0; sweep < 50; ++sweep) {
    double off = 0;
    for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j) off += a[i][j] * a[i][j];
    if (off < 1e-24) break;
    for (int p = 0; p < 3; ++p) for (int q = p + 1; q < 3; ++q) {
      if (std::fabs(a[p][q]) < 1e-30) continue;
      const double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
      const double t = (theta >= 0 ? 1 : -1) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
      const double cs = 1 / std::sqrt(t * t + 1), sn = t * cs;
      for (int k = 0; k < 3; ++k) { const double akp = a[k][p], akq = a[k][q]; a[k][p] = cs * akp - sn * akq; a[k][q] = sn * akp + cs * akq; }
      for (int k = 0; k < 3; ++k) { const double apk = a[p][k], aqk = a[q][k]; a[p][k] = cs * apk - sn * aqk; a[q][k] = sn * apk + cs * aqk; }
      for (int k = 0; k < 3; ++k) { const double vkp = v[k][p], vkq = v[k][q]; v[k][p] = cs * vkp - sn * vkq; v[k][q] = sn * vkp + cs * vkq; }
    }
  }
  int imin = 0, imax = 0;
  for (int i = 1; i < 3; ++i) { if (a[i][i] < a[imin][imin]) imin = i; if (a[i][i] > a[imax][imax]) imax = i; }
  Vector3d normal(v[0][imin], v[1][imin], v[2][imin]), xaxis(v[0][imax], v[1][imax], v[2][imax]);
  if (imin == imax || !normal.Unitize()) normal = ON_zaxis;
  xaxis = Perpendicular(normal, xaxis);
  return ON_Plane(c, xaxis, ON_CrossProduct(normal, xaxis));
}

bool ParseNumber(const std::string& t, double& v) {
  char* e = nullptr;
  v = std::strtod(t.c_str(), &e);
  return e && e != t.c_str() && *e == 0;
}

std::optional<kernel::NurbsCurve> CurveOf(CommandContext& ctx, ObjectId id) {
  const SceneObject* o = ctx.Doc().Find(id);
  if (o && o->kind == ObjectKind::Curve && o->curve) return *o->curve;
  return std::nullopt;
}

std::vector<kernel::NurbsCurve> CurvesOf(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<kernel::NurbsCurve> out;
  for (ObjectId id : ids) if (std::optional<kernel::NurbsCurve> c = CurveOf(ctx, id)) out.push_back(*c);
  return out;
}

void DeselectAll(CommandContext& ctx, const std::vector<ObjectId>& ids) { for (ObjectId id : ids) ctx.Doc().Select(id, false); }

// Blends the local-coordinate profiles anchored at row indices `anchor[k]`
// (sorted) for row j: piecewise-linear between neighbours, constant outside.
std::vector<Vector3d> BlendProfiles(const std::vector<std::vector<Vector3d>>& locals, const std::vector<int>& anchor, int j) {
  if (locals.size() == 1 || j <= anchor.front()) return locals.front();
  if (j >= anchor.back()) return locals.back();
  size_t k = 0;
  while (k + 1 < anchor.size() && anchor[k + 1] < j) ++k;
  const double span = static_cast<double>(anchor[k + 1] - anchor[k]);
  const double w = span > 0 ? (j - anchor[k]) / span : 0;
  std::vector<Vector3d> out(locals[k].size());
  for (size_t i = 0; i < out.size(); ++i) out[i] = locals[k][i] * (1 - w) + locals[k + 1][i] * w;
  return out;
}

int NearestIndex(const std::vector<double>& params, double t) {
  int best = 0;
  for (size_t i = 1; i < params.size(); ++i) if (std::fabs(params[i] - t) < std::fabs(params[static_cast<size_t>(best)] - t)) best = static_cast<int>(i);
  return best;
}

// ---------------------------------------------------------------------------
// Sweep1: rail, then cross-section curves, transported with RMF frames.
// ---------------------------------------------------------------------------

class Sweep1Command : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select rail"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!rail_) {
      std::vector<kernel::NurbsCurve> c = CurvesOf(ctx, ids);
      if (c.empty()) { ctx.Warn("Select a rail curve"); Finish(); return; }
      rail_ = c.front();
      DeselectAll(ctx, ids);
      closed_ = rail_->IsClosed();
      options = {{"Closed", closed_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
      WantObjects("Select cross section curves");
      accept_preselection = false;
      return;
    }
    std::vector<kernel::NurbsCurve> profiles = CurvesOf(ctx, ids);
    if (profiles.empty()) { ctx.Warn("Select cross section curves"); Finish(); return; }
    Build(ctx, profiles);
    Finish();
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Closed") { closed_ = (v == "Yes"); options[0].value = closed_ ? "Yes" : "No"; }
  }
  void Build(CommandContext& ctx, std::vector<kernel::NurbsCurve> profiles) {
    const kernel::NurbsCurve& rail = *rail_;
    const bool wrap = closed_ && rail.IsClosed();
    const int nrows = rail.IsLinear() && !wrap ? 2 : kRailSamples;
    const std::vector<double> params = ArcLengthParams(rail, wrap ? nrows + 1 : nrows, false);
    const bool closed_profile = profiles.front().IsClosed();
    const int ncols = kProfileSamples;
    Row first = SampleCurve(profiles.front(), 3, false);
    const std::vector<Frame> frames = RmfFrames(rail, params, first[1] - first[0], wrap);

    std::vector<std::vector<Vector3d>> locals;
    std::vector<int> anchors;
    std::optional<Vector3d> first_dir;
    for (kernel::NurbsCurve& c : profiles) {
      Row pts = SampleCurve(c, ncols, closed_profile);
      const Point3d p0 = pts.front();
      const int a = NearestIndex(params, rail.ClosestPointParameter(p0));
      const Frame& f = frames[static_cast<size_t>(a)];
      std::vector<Vector3d> local;
      for (const Point3d& p : pts) local.push_back(f.Local(p - p0));
      const Vector3d dir = local[local.size() / 2];
      if (first_dir && ON_DotProduct(dir, *first_dir) < 0 && !closed_profile) {
        c.Reverse();
        pts = SampleCurve(c, ncols, closed_profile);
        local.clear();
        for (const Point3d& p : pts) local.push_back(f.Local(p - pts.front()));
      }
      if (!first_dir) first_dir = dir;
      // Keep the list sorted by anchor row.
      size_t pos = 0;
      while (pos < anchors.size() && anchors[pos] <= a) ++pos;
      anchors.insert(anchors.begin() + static_cast<long>(pos), a);
      locals.insert(locals.begin() + static_cast<long>(pos), local);
    }
    std::vector<Row> rows;
    for (int j = 0; j < nrows; ++j) {
      const std::vector<Vector3d> local = BlendProfiles(locals, anchors, j);
      Row row;
      for (const Vector3d& l : local) row.push_back(frames[static_cast<size_t>(j)].Place(l));
      rows.push_back(row);
    }
    ctx.Doc().BeginChange("Sweep1");
    ctx.Doc().Add(SceneObject::MakeSurface(SurfaceFromRows(rows, closed_profile, wrap)));
    ctx.Print("Sweep1: " + std::to_string(profiles.size()) + " section(s) along " + std::to_string(nrows) + " rail stations" + (wrap ? ", closed" : ""));
  }
  std::optional<kernel::NurbsCurve> rail_;
  bool closed_ = false;
};

// ---------------------------------------------------------------------------
// Sweep2: two rails, then cross-sections scaled to span the rails.
// ---------------------------------------------------------------------------

class Sweep2Command : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select first rail"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (rails_.size() < 2) {
      std::vector<kernel::NurbsCurve> c = CurvesOf(ctx, ids);
      if (c.empty()) { ctx.Warn("Select a rail curve"); Finish(); return; }
      rails_.push_back(c.front());
      if (rails_.size() == 1 && c.size() >= 2) rails_.push_back(c[1]);
      DeselectAll(ctx, ids);
      accept_preselection = false;
      WantObjects(rails_.size() < 2 ? "Select second rail" : "Select cross section curves");
      return;
    }
    std::vector<kernel::NurbsCurve> profiles = CurvesOf(ctx, ids);
    if (profiles.empty()) { ctx.Warn("Select cross section curves"); Finish(); return; }
    Build(ctx, profiles);
    Finish();
  }
  void Build(CommandContext& ctx, std::vector<kernel::NurbsCurve> profiles) {
    kernel::NurbsCurve a = rails_[0], b = rails_[1];
    const Point3d a0 = a.PointAt(a.Domain().min), b0 = b.PointAt(b.Domain().min), b1 = b.PointAt(b.Domain().max);
    if (a0.DistanceTo(b1) < a0.DistanceTo(b0)) b.Reverse();
    const bool wrap = a.IsClosed() && b.IsClosed();
    const int nrows = a.IsLinear() && b.IsLinear() && !wrap ? 2 : kRailSamples;
    const std::vector<double> pa = ArcLengthParams(a, wrap ? nrows + 1 : nrows, false), pb = ArcLengthParams(b, wrap ? nrows + 1 : nrows, false);
    struct RailFrame { Point3d o; Vector3d x, y, z; double w; };
    std::vector<RailFrame> frames;
    for (int j = 0; j < nrows; ++j) {
      RailFrame f;
      f.o = a.PointAt(pa[static_cast<size_t>(j)]);
      const Point3d q = b.PointAt(pb[static_cast<size_t>(j)]);
      f.x = q - f.o;
      f.w = f.x.Length();
      if (!f.x.Unitize()) f.x = frames.empty() ? Vector3d(1, 0, 0) : frames.back().x;
      Vector3d t = a.TangentAt(pa[static_cast<size_t>(j)]) + b.TangentAt(pb[static_cast<size_t>(j)]);
      if (!t.Unitize()) t = a.TangentAt(pa[static_cast<size_t>(j)]);
      f.z = ON_CrossProduct(f.x, t);
      if (!f.z.Unitize()) f.z = frames.empty() ? Perpendicular(f.x, ON_zaxis) : frames.back().z;
      f.y = ON_CrossProduct(f.z, f.x);
      frames.push_back(f);
    }
    const bool closed_profile = profiles.front().IsClosed();
    std::vector<std::vector<Vector3d>> locals;
    std::vector<int> anchors;
    for (kernel::NurbsCurve& c : profiles) {
      const Point3d s = c.PointAt(c.Domain().min), e = c.PointAt(c.Domain().max);
      if (!closed_profile && a.ClosestPoint(e).DistanceTo(e) < a.ClosestPoint(s).DistanceTo(s)) c.Reverse();
      Row pts = SampleCurve(c, kProfileSamples, closed_profile);
      const int j = NearestIndex(pa, a.ClosestPointParameter(pts.front()));
      const RailFrame& f = frames[static_cast<size_t>(j)];
      const double w = f.w > 1e-12 ? f.w : 1;
      std::vector<Vector3d> local;
      for (const Point3d& p : pts) { const Vector3d d = p - f.o; local.push_back(Vector3d(ON_DotProduct(d, f.x), ON_DotProduct(d, f.y), ON_DotProduct(d, f.z)) / w); }
      size_t pos = 0;
      while (pos < anchors.size() && anchors[pos] <= j) ++pos;
      anchors.insert(anchors.begin() + static_cast<long>(pos), j);
      locals.insert(locals.begin() + static_cast<long>(pos), local);
    }
    std::vector<Row> rows;
    for (int j = 0; j < nrows; ++j) {
      const RailFrame& f = frames[static_cast<size_t>(j)];
      Row row;
      for (const Vector3d& l : BlendProfiles(locals, anchors, j)) row.push_back(f.o + (f.x * l.x + f.y * l.y + f.z * l.z) * f.w);
      rows.push_back(row);
    }
    ctx.Doc().BeginChange("Sweep2");
    ctx.Doc().Add(SceneObject::MakeSurface(SurfaceFromRows(rows, closed_profile, wrap)));
    ctx.Print("Sweep2: " + std::to_string(profiles.size()) + " section(s) along " + std::to_string(nrows) + " rail stations");
  }
  std::vector<kernel::NurbsCurve> rails_;
};

// ---------------------------------------------------------------------------
// NetworkSrf: 2 curves -> exact ruled surface; 3/4 curves -> Coons patch.
// ---------------------------------------------------------------------------

void NetworkSrf(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<kernel::NurbsCurve> curves = CurvesOf(ctx, ids);
  if (curves.size() < 2) { ctx.Warn("Select 2, 3 or 4 curves"); return; }
  if (curves.size() > 4) { ctx.Warn("NetworkSrf handles up to 4 boundary curves here; using the first 4"); curves.resize(4); }
  if (curves.size() == 2) {
    kernel::NurbsCurve a = curves[0], b = curves[1];
    const Point3d a0 = a.PointAt(a.Domain().min), b0 = b.PointAt(b.Domain().min), b1 = b.PointAt(b.Domain().max);
    if (a0.DistanceTo(b1) < a0.DistanceTo(b0)) b.Reverse();
    ON_NurbsSurface ns;
    if (!ns.CreateRuledSurface(a.raw(), b.raw())) { ctx.Warn("NetworkSrf: ruled surface failed"); return; }
    kernel::NurbsSurface k;
    k.raw() = ns;
    ctx.Doc().BeginChange("NetworkSrf");
    ctx.Doc().Add(SceneObject::MakeSurface(k));
    ctx.Print("NetworkSrf: ruled surface between 2 curves");
    return;
  }
  // Chain the curves into a loop by endpoint proximity.
  std::vector<kernel::NurbsCurve> loop = {curves[0]};
  std::vector<bool> used(curves.size(), false);
  used[0] = true;
  Point3d end = curves[0].PointAt(curves[0].Domain().max);
  while (loop.size() < curves.size()) {
    size_t best = 0;
    bool best_reverse = false;
    double best_d = std::numeric_limits<double>::max();
    for (size_t i = 0; i < curves.size(); ++i) {
      if (used[i]) continue;
      const double ds = end.DistanceTo(curves[i].PointAt(curves[i].Domain().min)), de = end.DistanceTo(curves[i].PointAt(curves[i].Domain().max));
      if (ds < best_d) { best_d = ds; best = i; best_reverse = false; }
      if (de < best_d) { best_d = de; best = i; best_reverse = true; }
    }
    used[best] = true;
    kernel::NurbsCurve c = curves[best];
    if (best_reverse) c.Reverse();
    end = c.PointAt(c.Domain().max);
    loop.push_back(c);
  }
  const int n = kProfileSamples + 8;
  const Row bottom = SampleCurve(loop[0], n, false), right = SampleCurve(loop[1], n, false);
  Row top = SampleCurve(loop[2], n, false);
  std::reverse(top.begin(), top.end());
  Row left;
  if (loop.size() == 4) { left = SampleCurve(loop[3], n, false); std::reverse(left.begin(), left.end()); }
  else left.assign(static_cast<size_t>(n), bottom.front());  // degenerate edge: the 3-sided patch closes at a point
  const Point3d p00 = bottom.front(), p10 = bottom.back(), p01 = top.front(), p11 = top.back();
  std::vector<Row> rows;
  for (int j = 0; j < n; ++j) {
    const double v = static_cast<double>(j) / (n - 1);
    Row row;
    for (int i = 0; i < n; ++i) {
      const double u = static_cast<double>(i) / (n - 1);
      const Point3d lc = bottom[static_cast<size_t>(i)] * (1 - v) + top[static_cast<size_t>(i)] * v;
      const Point3d ld = left[static_cast<size_t>(j)] * (1 - u) + right[static_cast<size_t>(j)] * u;
      const Point3d bl = p00 * ((1 - u) * (1 - v)) + p10 * (u * (1 - v)) + p01 * ((1 - u) * v) + p11 * (u * v);
      row.push_back(Point3d(lc.x + ld.x - bl.x, lc.y + ld.y - bl.y, lc.z + ld.z - bl.z));
    }
    rows.push_back(row);
  }
  ctx.Doc().BeginChange("NetworkSrf");
  ctx.Doc().Add(SceneObject::MakeSurface(SurfaceFromRows(rows)));
  ctx.Print("NetworkSrf: Coons patch through " + std::to_string(curves.size()) + " curves");
}

// ---------------------------------------------------------------------------
// Patch: planar fit through curves and points.
// ---------------------------------------------------------------------------

void Patch(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  Row pts;
  std::vector<kernel::NurbsCurve> closed;
  int npts = 0, ncrv = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Point) { pts.push_back(o->point); ++npts; }
    else if (o->kind == ObjectKind::Curve && o->curve) {
      Row s = SampleCurve(*o->curve, 32, o->curve->IsClosed());
      pts.insert(pts.end(), s.begin(), s.end());
      if (o->curve->IsClosed()) closed.push_back(*o->curve);
      ++ncrv;
    }
  }
  if (pts.size() < 3) { ctx.Warn("Select curves and/or at least three points"); return; }
  const ON_Plane fit = FitPlane(pts);
  ctx.Doc().BeginChange("Patch");
  if (closed.size() == 1) {
    ON_Plane own;
    ON_Brep* b = nullptr;
    if (closed[0].raw().IsPlanar(&own, ctx.Settings().absolute_tolerance)) b = ON_BrepTrimmedPlane(own, closed[0].raw());
    if (!b) {
      // Non-planar boundary: project its samples onto the fitted plane.
      Row loop = SampleCurve(closed[0], 64, true);
      for (Point3d& p : loop) p = fit.ClosestPointTo(p);
      loop.push_back(loop.front());
      ON_Polyline pl;
      for (const Point3d& p : loop) pl.Append(p);
      ON_PolylineCurve pc(pl);
      b = ON_BrepTrimmedPlane(fit, pc);
    }
    if (b) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ctx.Print("Patch: planar face bounded by the closed curve"); return; }
  }
  double u0 = 0, u1 = 0, v0 = 0, v1 = 0;
  bool first = true;
  for (const Point3d& p : pts) {
    double u, v;
    fit.ClosestPointTo(p, &u, &v);
    if (first) { u0 = u1 = u; v0 = v1 = v; first = false; }
    u0 = std::min(u0, u); u1 = std::max(u1, u); v0 = std::min(v0, v); v1 = std::max(v1, v);
  }
  if (u1 - u0 < 1e-9 || v1 - v0 < 1e-9) { ctx.Warn("Patch: the input is collinear"); return; }
  std::vector<Point3d> grid = {fit.PointAt(u0, v0), fit.PointAt(u0, v1), fit.PointAt(u1, v0), fit.PointAt(u1, v1)};
  ctx.Doc().Add(SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)));
  ctx.Print("Patch: least-squares plane through " + std::to_string(ncrv) + " curve(s) and " + std::to_string(npts) + " point(s)");
}

// ---------------------------------------------------------------------------
// Pipe: circle swept along curves with RMF frames.
// ---------------------------------------------------------------------------

class PipeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to create pipe around"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    curves_ = CurvesOf(ctx, ids);
    if (curves_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    options = {{"Radius", FormatNumber(radius_), {}, true, false}, {"Cap", cap_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
    WantNumber("Radius", radius_);
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Cap") { cap_ = (v == "Yes"); options[1].value = cap_ ? "Yes" : "No"; }
    if (n == "Radius") { double d; if (ParseNumber(v, d) && d > 0) { radius_ = d; options[0].value = FormatNumber(d); default_number = d; } else ctx.Warn("Radius must be positive"); }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNumber(t, v)) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double r) override {
    if (r <= 0) { ctx.Warn("Radius must be positive"); return; }
    radius_ = r;
    ctx.Doc().BeginChange("Pipe");
    int made = 0;
    for (const kernel::NurbsCurve& c : curves_) {
      const bool wrap = c.IsClosed();
      const int nrows = c.IsLinear() && !wrap ? 2 : kRailSamples;
      const std::vector<double> params = ArcLengthParams(c, wrap ? nrows + 1 : nrows, false);
      const std::vector<Frame> frames = RmfFrames(c, params, ON_zaxis, wrap);
      const bool as_mesh = cap_ || wrap;
      // A cubic periodic B-spline through CVs on a circle runs inside it:
      // enlarge the CV circle so the surface has the requested radius.
      const double rr = as_mesh ? radius_ : radius_ * 3.0 / (2.0 + std::cos(2 * ON_PI / kRingSegments));
      std::vector<Row> rows;
      for (int j = 0; j < nrows; ++j) {
        Row ring;
        for (int i = 0; i < kRingSegments; ++i) {
          const double a = 2 * ON_PI * i / kRingSegments;
          ring.push_back(frames[static_cast<size_t>(j)].Place(Vector3d(0, rr * std::cos(a), rr * std::sin(a))));
        }
        rows.push_back(ring);
      }
      if (as_mesh) {
        kernel::Mesh m;
        if (wrap) m = MeshFromRows(rows, true, true);
        else m = kernel::Mesh::LoftClosedRings(rows);
        m = Outward(m);
        ctx.Doc().Add(SceneObject::MakeMesh(m));
      } else {
        ctx.Doc().Add(SceneObject::MakeSurface(SurfaceFromRows(rows, true, false)));
      }
      ++made;
    }
    ctx.Print("Pipe: radius " + FormatNumber(radius_) + ", " + std::to_string(made) + " pipe(s)" + (cap_ ? " (capped mesh)" : ""));
    Finish();
  }
  std::vector<kernel::NurbsCurve> curves_;
  double radius_ = 1;
  bool cap_ = true;
};

// ---------------------------------------------------------------------------
// OffsetSrf / Shell
// ---------------------------------------------------------------------------

// NURBS surface offset: each control point moves along the surface normal
// at its Greville point. Exact for planes, approximate elsewhere.
kernel::NurbsSurface OffsetNurbs(const kernel::NurbsSurface& in, double d) {
  kernel::NurbsSurface out = in;
  ON_NurbsSurface& s = out.raw();
  const ON_NurbsSurface& src = in.raw();
  for (int i = 0; i < s.CVCount(0); ++i) {
    for (int j = 0; j < s.CVCount(1); ++j) {
      const double u = src.GrevilleAbcissa(0, i), v = src.GrevilleAbcissa(1, j);
      ON_3dVector n = src.NormalAt(u, v);
      if (!n.Unitize()) continue;
      ON_3dPoint p;
      src.GetCV(i, j, p);
      s.SetCV(i, j, p + n * d);
    }
  }
  return out;
}

std::vector<Row> SurfaceRows(const kernel::NurbsSurface& s, int nu, int nv) {
  const kernel::Interval du = s.Domain(0), dv = s.Domain(1);
  std::vector<Row> rows;
  for (int j = 0; j < nv; ++j) {
    Row row;
    for (int i = 0; i < nu; ++i) row.push_back(s.PointAt(du.min + (du.max - du.min) * i / (nu - 1.0), dv.min + (dv.max - dv.min) * j / (nv - 1.0)));
    rows.push_back(row);
  }
  return rows;
}

class OffsetSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select surfaces or polysurfaces to offset"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind != ObjectKind::Curve && o->kind != ObjectKind::Point) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select surfaces, polysurfaces or meshes"); Finish(); return; }
    options = {{"Distance", FormatNumber(distance_), {}, true, false}, {"Solid", "No", {"Yes", "No"}, false, true}, {"FlipAll", "No", {"Yes", "No"}, false, true}};
    WantNumber("Offset distance", distance_);
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Distance") { double d; if (ParseNumber(v, d)) { distance_ = d; options[0].value = FormatNumber(d); default_number = d; } else ctx.Warn("Invalid distance"); }
    if (n == "Solid") { solid_ = (v == "Yes"); options[1].value = solid_ ? "Yes" : "No"; }
    if (n == "FlipAll") { flip_ = (v == "Yes"); options[2].value = flip_ ? "Yes" : "No"; }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNumber(t, v)) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double d) override {
    if (d == 0) { ctx.Warn("Distance must be non-zero"); return; }
    if (flip_) d = -d;
    ctx.Doc().BeginChange("OffsetSrf");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const int layer = o->layer_index;
      SceneObject n;
      if (o->kind == ObjectKind::Surface && o->surface) {
        const kernel::NurbsSurface off = OffsetNurbs(*o->surface, d);
        if (solid_) {
          const int nu = std::max(8, 2 * o->surface->CVCountU()), nv = std::max(8, 2 * o->surface->CVCountV());
          n = SceneObject::MakeMesh(ShellBetween(MeshFromRows(SurfaceRows(*o->surface, nu, nv), false, false), MeshFromRows(SurfaceRows(off, nu, nv), false, false)));
        } else {
          n = SceneObject::MakeSurface(off);
        }
      } else {
        std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
        if (!m || m->FaceCount() == 0) { ctx.Warn("Object " + std::to_string(id) + " could not be meshed; skipped"); continue; }
        kernel::Mesh base = *m;
        if (base.IsClosedManifold() && base.Volume() < 0) base = base.FlipNormals();
        kernel::Mesh off = OffsetMesh(base, d);
        if (solid_) {
          if (base.IsClosedManifold()) off = Outward(kernel::Mesh::MergeAndWeld({d > 0 ? base.FlipNormals() : base, d > 0 ? off : off.FlipNormals()}, 1e-9));
          else off = ShellBetween(base, off);
        }
        n = SceneObject::MakeMesh(off);
      }
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ++made;
    }
    ctx.Print("OffsetSrf: distance " + FormatNumber(d) + ", " + std::to_string(made) + " object(s)" + (solid_ ? ", solid" : ""));
    Finish();
  }
  std::vector<ObjectId> ids_;
  double distance_ = 1;
  bool solid_ = false, flip_ = false;
};

class ShellCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select closed solids to shell"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    options = {{"Thickness", FormatNumber(thickness_), {}, true, false}};
    WantNumber("Thickness", thickness_);
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Thickness") { double d; if (ParseNumber(v, d) && d > 0) { thickness_ = d; options[0].value = FormatNumber(d); default_number = d; } else ctx.Warn("Thickness must be positive"); }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNumber(t, v)) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double t) override {
    if (t <= 0) { ctx.Warn("Thickness must be positive"); return; }
    std::vector<std::pair<ObjectId, kernel::Mesh>> results;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
      if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(id) + " is not a closed solid; skipped"); continue; }
      kernel::Mesh outer = Outward(*m);
      kernel::Mesh inner = OffsetMesh(outer, -t);
      try {
        kernel::Mesh r = kernel::BooleanCombine(outer, inner, kernel::BooleanOp::Difference);
        if (r.FaceCount() == 0) { ctx.Warn("Shell: object " + std::to_string(id) + " is thinner than the thickness; skipped"); continue; }
        results.push_back({id, r});
      } catch (const std::exception& ex) {
        ctx.Warn("Shell: object " + std::to_string(id) + " rejected by the mesh kernel (" + ex.what() + "); skipped");
      }
    }
    if (results.empty()) { Finish(); return; }
    ctx.Doc().BeginChange("Shell");
    for (auto& [id, r] : results) {
      int layer = 0;
      if (const SceneObject* o = ctx.Doc().Find(id)) layer = o->layer_index;
      ctx.Doc().Remove(id);
      SceneObject n = SceneObject::MakeMesh(r);
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ctx.Print("Shell: thickness " + FormatNumber(t) + ", volume " + FormatNumber(std::fabs(r.Volume())));
    }
    Finish();
  }
  std::vector<ObjectId> ids_;
  double thickness_ = 1;
};

// ---------------------------------------------------------------------------
// ExtrudeCrvAlongCrv (exact sum surface) / ExtrudeCrvTapered (ruled surface)
// ---------------------------------------------------------------------------

class ExtrudeAlongCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to extrude"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (profiles_.empty()) {
      profiles_ = CurvesOf(ctx, ids);
      if (profiles_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
      DeselectAll(ctx, ids);
      accept_preselection = false;
      WantObjects("Select path curve");
      return;
    }
    std::vector<kernel::NurbsCurve> path = CurvesOf(ctx, ids);
    if (path.empty()) { ctx.Warn("Select a path curve"); Finish(); return; }
    ctx.Doc().BeginChange("ExtrudeCrvAlongCrv");
    int made = 0;
    for (const kernel::NurbsCurve& c : profiles_) {
      ON_SumSurface ss;
      kernel::NurbsSurface k;
      if (ss.Create(c.raw(), path[0].raw()) && SurfaceFromON(ss, k)) { ctx.Doc().Add(SceneObject::MakeSurface(k)); ++made; }
    }
    ctx.Print("ExtrudeCrvAlongCrv: " + std::to_string(made) + " surface(s)");
    Finish();
  }
  std::vector<kernel::NurbsCurve> profiles_;
};

class ExtrudeTaperedCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to extrude"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    profiles_ = CurvesOf(ctx, ids);
    if (profiles_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    Row all;
    for (const kernel::NurbsCurve& c : profiles_) { Row s = SampleCurve(c, 8, false); all.insert(all.end(), s.begin(), s.end()); }
    center_ = Centroid(all);
    options = {{"DraftAngle", FormatNumber(angle_), {}, true, false}, {"Solid", solid_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
    WantPoint("Extrusion distance");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "DraftAngle") { double d; if (ParseNumber(v, d) && std::fabs(d) < 90) { angle_ = d; options[0].value = FormatNumber(d); } else ctx.Warn("Draft angle must be between -90 and 90 degrees"); }
    if (n == "Solid") { solid_ = (v == "Yes"); options[1].value = solid_ ? "Yes" : "No"; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override { OnNumber(ctx, ON_DotProduct(p - center_, ActiveNormal(ctx))); }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNumber(t, v)) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double d) override {
    ctx.ClearPreview();
    if (d == 0) { ctx.Warn("Distance must be non-zero"); Finish(); return; }
    const Vector3d n = ActiveNormal(ctx);
    ctx.Doc().BeginChange("ExtrudeCrvTapered");
    int made = 0;
    for (const kernel::NurbsCurve& c : profiles_) {
      const Row pts = SampleCurve(c, 32, c.IsClosed());
      const Point3d center = Centroid(pts);
      double radius = 0;
      for (const Point3d& p : pts) { const Vector3d v = p - center; radius += (v - n * ON_DotProduct(v, n)).Length(); }
      radius /= static_cast<double>(pts.size());
      const double inset = std::fabs(d) * std::tan(angle_ * ON_PI / 180.0);
      double scale = radius > 1e-12 ? (radius - inset) / radius : 1;
      if (scale < 0.01) { ctx.Warn("Draft angle closes the profile before the full distance; clamped"); scale = 0.01; }
      ON_Xform xf = ON_Xform::TranslationTransformation(n * d) * ON_Xform::ScaleTransformation(center, scale);
      ON_NurbsCurve top = c.raw();
      top.Transform(xf);
      ON_NurbsSurface rs;
      if (!rs.CreateRuledSurface(c.raw(), top)) continue;
      ON_Plane plane;
      if (solid_ && c.IsClosed() && c.raw().IsPlanar(&plane, ctx.Settings().absolute_tolerance)) {
        ON_Brep* b = ON_Brep::New();
        ON_NurbsSurface* face_srf = new ON_NurbsSurface(rs);
        b->Create(face_srf);
        ON_Plane top_plane = plane;
        top_plane.SetOrigin(plane.origin + n * d);
        ON_Brep* cap0 = ON_BrepTrimmedPlane(plane, c.raw());
        ON_Brep* cap1 = ON_BrepTrimmedPlane(top_plane, top);
        if (cap0) { b->Append(*cap0); delete cap0; }
        if (cap1) { b->Append(*cap1); delete cap1; }
        JoinNakedEdges(*b, ctx.Settings().absolute_tolerance * 10);
        if (b->IsSolid()) {
          BrepMeshOptions opt;
          opt.chord_tolerance = 0.05;
          if (MeshBrepClosed(*b, opt).Volume() < 0) b->Flip();  // outward normals
        } else {
          ctx.Warn("ExtrudeCrvTapered: caps could not be joined; result is an open polysurface");
        }
        ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b)));
      } else {
        kernel::NurbsSurface k;
        k.raw() = rs;
        ctx.Doc().Add(SceneObject::MakeSurface(k));
      }
      ++made;
    }
    ctx.Print("ExtrudeCrvTapered: distance " + FormatNumber(d) + ", draft " + FormatNumber(angle_) + " deg, " + std::to_string(made) + " object(s)");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    const Vector3d n = ActiveNormal(ctx);
    ctx.AddPreviewLine(center_, center_ + n * ON_DotProduct(h - center_, n));
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<kernel::NurbsCurve> profiles_;
  Point3d center_;
  double angle_ = 10;
  bool solid_ = true;
};

// ---------------------------------------------------------------------------
// Project / Pull
// ---------------------------------------------------------------------------

struct Target {
  std::optional<kernel::NurbsSurface> surface;  // exact closest point for Pull
  kernel::Mesh mesh;                            // ray target for Project
};

bool RayTriangle(Point3d o, Vector3d d, Point3d a, Point3d b, Point3d c, double& t) {
  const Vector3d e1 = b - a, e2 = c - a, p = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::fabs(det) < 1e-14) return false;
  const double inv = 1 / det;
  const Vector3d s = o - a;
  const double u = ON_DotProduct(s, p) * inv;
  if (u < -1e-9 || u > 1 + 1e-9) return false;
  const Vector3d q = ON_CrossProduct(s, e1);
  const double v = ON_DotProduct(d, q) * inv;
  if (v < -1e-9 || u + v > 1 + 1e-9) return false;
  t = ON_DotProduct(e2, q) * inv;
  return true;
}

// Nearest intersection of the line p + t*d (either direction) with the mesh.
std::optional<Point3d> LineHit(const kernel::Mesh& m, Point3d p, Vector3d d) {
  const ON_Mesh& r = m.raw();
  double best = std::numeric_limits<double>::max();
  bool hit = false;
  for (int f = 0; f < r.FaceCount(); ++f) {
    const ON_MeshFace& face = r.m_F[f];
    const Point3d v0 = r.Vertex(face.vi[0]), v1 = r.Vertex(face.vi[1]), v2 = r.Vertex(face.vi[2]);
    double t;
    if (RayTriangle(p, d, v0, v1, v2, t) && std::fabs(t) < std::fabs(best)) { best = t; hit = true; }
    if (!face.IsTriangle() && RayTriangle(p, d, v0, v2, r.Vertex(face.vi[3]), t) && std::fabs(t) < std::fabs(best)) { best = t; hit = true; }
  }
  if (!hit) return std::nullopt;
  return p + d * best;
}

class ProjectCommand : public Command {
 public:
  explicit ProjectCommand(bool pull) : pull_(pull) {}
  void Begin(CommandContext&) override { WantObjects(pull_ ? "Select curves and points to pull" : "Select curves and points to project"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!have_sources_) {
      for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && (o->kind == ObjectKind::Curve || o->kind == ObjectKind::Point)) sources_.push_back(id); }
      if (sources_.empty()) { ctx.Warn("Select curves or points"); Finish(); return; }
      have_sources_ = true;
      DeselectAll(ctx, ids);
      accept_preselection = false;
      WantObjects(pull_ ? "Select surfaces, polysurfaces or meshes to pull to" : "Select surfaces, polysurfaces or meshes to project onto");
      return;
    }
    std::vector<Target> targets;
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind == ObjectKind::Curve || o->kind == ObjectKind::Point) continue;
      Target t;
      if (o->kind == ObjectKind::Surface && o->surface) t.surface = *o->surface;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.002);
      if (!m) continue;
      t.mesh = *m;
      targets.push_back(std::move(t));
    }
    if (targets.empty()) { ctx.Warn("Select surfaces, polysurfaces or meshes as targets"); Finish(); return; }
    const Vector3d dir = ActiveNormal(ctx);
    auto map = [&](Point3d p) -> std::optional<Point3d> {
      std::optional<Point3d> best;
      for (const Target& t : targets) {
        std::optional<Point3d> q;
        if (pull_) q = t.surface ? t.surface->ClosestPoint(p, 40, 40) : t.mesh.ClosestPoint(p);
        else q = LineHit(t.mesh, p, dir);
        if (q && (!best || q->DistanceTo(p) < best->DistanceTo(p))) best = q;
      }
      return best;
    };
    // Snapshot the sources before adding anything (Add() may reallocate).
    struct Source { std::optional<kernel::NurbsCurve> curve; Point3d point; int layer; };
    std::vector<Source> sources;
    for (ObjectId id : sources_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      Source s;
      s.layer = o->layer_index;
      if (o->kind == ObjectKind::Curve) s.curve = *o->curve; else s.point = o->point;
      sources.push_back(s);
    }
    const char* label = pull_ ? "Pull" : "Project";
    ctx.Doc().BeginChange(label);
    int curves = 0, points = 0;
    for (const Source& s : sources) {
      if (!s.curve) {
        if (std::optional<Point3d> q = map(s.point)) { SceneObject n = SceneObject::MakePoint(*q); n.layer_index = s.layer; ctx.Doc().Add(std::move(n)); ++points; }
        continue;
      }
      const bool polyline = s.curve->Degree() == 1;
      Row samples;
      if (polyline) { for (int i = 0; i < s.curve->ControlPointCount(); ++i) samples.push_back(s.curve->ControlPointAt(i)); }
      else samples = SampleCurve(*s.curve, std::min(64, std::max(16, 4 * s.curve->ControlPointCount())), false);
      // Runs of consecutive hits each become one curve.
      Row run;
      auto flush = [&]() {
        if (run.size() >= 2) {
          kernel::NurbsCurve c = (polyline || run.size() < 4) ? PolylineCurve(run) : kernel::NurbsCurve::FromControlPoints(run, 3);
          SceneObject n = SceneObject::MakeCurve(c);
          n.layer_index = s.layer;
          ctx.Doc().Add(std::move(n));
          ++curves;
        }
        run.clear();
      };
      for (const Point3d& p : samples) { if (std::optional<Point3d> q = map(p)) run.push_back(*q); else flush(); }
      flush();
    }
    ctx.Print(std::string(label) + ": " + std::to_string(curves) + " curve(s), " + std::to_string(points) + " point(s)");
    Finish();
  }
  bool pull_;
  bool have_sources_ = false;
  std::vector<ObjectId> sources_;
};

}  // namespace

void RegisterSurfaceCommands(CommandEngine& e) {
  Reg(e, "Sweep1", Make<Sweep1Command>(), CommandStatus::Implemented, "Approximated by a lofted sweep: sections are transported along the rail with rotation-minimizing frames and fitted as a degree-3 surface.");
  Reg(e, "Sweep2", Make<Sweep2Command>(), CommandStatus::Implemented, "Approximate: sections are scaled between the rails (matched by arc length) and fitted as a degree-3 surface.");
  Reg(e, "NetworkSrf", OnSelection("Select curves in network (2, 3 or 4)", NetworkSrf, 2), CommandStatus::Implemented, "Two curves give an exact ruled surface; three or four give a bilinear Coons patch fitted as a degree-3 surface.");
  Reg(e, "Patch", OnSelection("Select curves and points to fit a surface through", Patch), CommandStatus::Partial, "Planar patch only: a least-squares plane trimmed by the single closed curve, or a fitted rectangle.");
  Reg(e, "Pipe", Make<PipeCommand>(), CommandStatus::Implemented, "Single radius. Cap=Yes gives a closed mesh solid; Cap=No a periodic NURBS surface (circle approximated by a cubic).");
  Reg(e, "OffsetSrf", Make<OffsetSrfCommand>(), CommandStatus::Partial, "Surfaces: control points offset along Greville normals (exact for planes). Polysurfaces and meshes are offset as meshes along vertex normals; Solid=Yes closes the shell as a mesh.");
  Reg(e, "Shell", Make<ShellCommand>(), CommandStatus::Partial, "Hollows a closed solid as a mesh (outer minus inward vertex-normal offset); face removal to open the shell is planned.");
  Reg(e, "ExtrudeCrvAlongCrv", Make<ExtrudeAlongCommand>(), CommandStatus::Implemented, "Exact translational sweep (sum surface); the profile is not rotated along the path.");
  Reg(e, "ExtrudeCrvTapered", Make<ExtrudeTaperedCommand>(), CommandStatus::Implemented, "Ruled surface to a copy of the profile scaled about its centroid by the draft angle (exact for circles, approximate corners).");
  Reg(e, "Project", Make<ProjectCommand>(false), CommandStatus::Implemented, "Projects along the CPlane normal onto the target's render mesh; result curves are refit through the projected samples.");
  Reg(e, "Pull", Make<ProjectCommand>(true), CommandStatus::Implemented, "Pulls to the closest point on the target; result curves are refit through the pulled samples.");
}

}  // namespace dino8::app
