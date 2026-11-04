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
#include <functional>
#include <limits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "commands/cmd_common.h"
#include "commands/continuity_request.h"
#include "commands/history_rebuild.h"
#include "commands/revolved_solid.h"
#include "commands/prism_solid.h"
#include "commands/section_fit.h"

namespace dino8::app {

namespace {

using revolved::PlainRevolved;
using revolved::RecognizeConeLike;
using revolved::OffsetProfile;
using revolved::RevolveProfileBrep;
using revolved::RevolveOutward;

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

// A measured deviation for a printed claim: two significant digits, scientific
// when tiny (FormatNumber's "%.4g" would round a 3e-16 to "3e-16" too, but
// prints a whole-number-looking "0" for anything under 1e-9).
std::string FormatDeviation(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2g", v);
  return buf;
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

// Offsets a mesh so that every face plane moves by its own distance: each
// vertex solves n_k . v = d_k over its distinct adjacent face normals
// (least squares when more than three meet), so box corners and creases
// stay sharp instead of shrinking along the averaged vertex normal. `region`
// gives each mesh face (aligned 1:1 with `in`'s own face order) an integer
// id; `d_for(id)` returns that region's own signed offset distance, so
// different original faces of the same solid can move by different
// amounts. At a vertex shared by two faces with *different* distances this
// solves the exact intersection of the two offset planes - a real mitered
// corner between a thick and a thin wall, not an average of the two - and
// at a corner where three (or more) faces meet it is the same least-squares
// plane-intersection the single-thickness code always used, just with a
// per-constraint right-hand side instead of one shared scalar. This is
// exact only when every face meeting at a given vertex is itself planar
// (its own tessellation triangles all share one normal); a curved face's
// triangle normals vary continuously, so "one distance per triangle" there
// is at best the same kind of approximation the single-thickness version
// already made - callers that care about a numerically verified result
// should check planarity themselves (see `AllFacesPlanar` below) before
// handing in a `region`/`d_for` pair that assigns different faces
// different distances.
kernel::Mesh OffsetMeshPerFace(const kernel::Mesh& in, const std::vector<int>& region, const std::function<double(int)>& d_for) {
  kernel::Mesh m = in;
  ON_Mesh& r = m.raw();
  r.ComputeFaceNormals();
  struct NormalD { ON_3dVector n; double d; };
  std::vector<std::vector<NormalD>> normals(static_cast<size_t>(r.VertexCount()));
  for (int f = 0; f < r.FaceCount(); ++f) {
    const ON_MeshFace& face = r.m_F[f];
    const ON_3dVector n(r.m_FN[f]);
    const double d = d_for(static_cast<size_t>(f) < region.size() ? region[static_cast<size_t>(f)] : 0);
    for (int k = 0; k < (face.IsTriangle() ? 3 : 4); ++k) {
      std::vector<NormalD>& list = normals[static_cast<size_t>(face.vi[k])];
      bool dup = false;
      for (const NormalD& e : list) if (ON_DotProduct(e.n, n) > 0.9995) { dup = true; break; }
      if (!dup) list.push_back({n, d});
    }
  }
  for (int i = 0; i < r.VertexCount(); ++i) {
    const std::vector<NormalD>& ns = normals[static_cast<size_t>(i)];
    if (ns.empty()) continue;
    double cap = 0;
    for (const NormalD& e : ns) cap = std::max(cap, std::fabs(e.d));
    cap *= 3;
    ON_3dVector v = ON_3dVector::ZeroVector;
    bool solved = false;
    if (ns.size() == 1) { v = ns[0].n * ns[0].d; solved = true; }
    else if (ns.size() == 2) {
      // Exact solve of n0.v=d0, n1.v=d1 in the (n0,n1) basis; reduces to
      // the old single-thickness shortcut (n0+n1)*(d/(1+c)) when d0==d1.
      const double c = ON_DotProduct(ns[0].n, ns[1].n);
      if (1 + c > 0.05) {
        const double det = 1 - c * c;
        if (std::fabs(det) > 1e-12) {
          const double x = (ns[0].d - ns[1].d * c) / det;
          const double y = (ns[1].d - ns[0].d * c) / det;
          v = ns[0].n * x + ns[1].n * y;
          solved = true;
        }
      }
    } else {
      double a[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, rhs[3] = {0, 0, 0};
      for (const NormalD& e : ns) {
        const double c[3] = {e.n.x, e.n.y, e.n.z};
        for (int p = 0; p < 3; ++p) { rhs[p] += e.d * c[p]; for (int q = 0; q < 3; ++q) a[p][q] += c[p] * c[q]; }
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
      double dmean = 0;
      for (const NormalD& e : ns) { v += e.n; dmean += e.d; }
      dmean /= static_cast<double>(ns.size());
      if (v.Unitize()) v = v * dmean;
    }
    if (v.Length() > cap) { v.Unitize(); v = v * cap; }
    r.SetVertex(i, r.Vertex(i) + v);
  }
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

// Single-thickness offset: every face gets the same distance `d`. Defers
// to OffsetMeshPerFace with one shared region so the two never drift apart
// numerically - a mesh with no per-face overrides goes through exactly the
// same arithmetic it always has.
kernel::Mesh OffsetMesh(const kernel::Mesh& in, double d) {
  const std::vector<int> region(static_cast<size_t>(in.raw().FaceCount()), 0);
  return OffsetMeshPerFace(in, region, [d](int) { return d; });
}

// True if every one of `faces`' own per-brep-face triangle normals agrees
// (within `cos_tol`) with that face's own first triangle - i.e. every
// entry is genuinely flat. OffsetMeshPerFace's per-vertex plane solve is an
// exact mitered offset only when every face meeting at a shared vertex is
// planar (see its own comment); this is the gate that decides whether a
// requested set of per-face thickness overrides is honoured (a solid
// composed entirely of planar faces - a box, a prism, any polyhedron) or
// silently would not be - in which case the caller falls back to the
// single verified-correct uniform thickness instead of guessing.
bool AllFacesPlanar(const std::vector<kernel::Mesh>& faces, double cos_tol) {
  for (const kernel::Mesh& fm : faces) {
    ON_Mesh r = fm.raw();
    if (r.FaceCount() == 0) continue;
    r.ComputeFaceNormals();
    const ON_3dVector n0(r.m_FN[0]);
    for (int f = 1; f < r.FaceCount(); ++f) {
      if (ON_DotProduct(n0, ON_3dVector(r.m_FN[f])) < cos_tol) return false;
    }
  }
  return true;
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

// Per-axis scale of Sweep1 (the ScaleX / ScaleY options): "start,end" - the scale along the section's X (resp. Y) axis at the
// rail's start and end, linear in the rail's arc length in between (a single number is that value all along). Both must be
// positive. The pair is what History records (num sx0 sx1 sy0 sy1); the kernel's two-point schedule {(0, start), (1, end)}.
bool ParseScalePair(const std::string& text, double& a, double& b) {
  std::string t = text;
  for (char& c : t) {
    if (c == ';' || c == ' ') c = ',';
  }
  const size_t comma = t.find(',');
  const std::string first = t.substr(0, comma), second = comma == std::string::npos ? first : t.substr(comma + 1);
  char* e1 = nullptr;
  char* e2 = nullptr;
  a = std::strtod(first.c_str(), &e1);
  b = std::strtod(second.c_str(), &e2);
  if (first.empty() || second.empty() || *e1 != 0 || *e2 != 0 || !std::isfinite(a) || !std::isfinite(b)) return false;
  return a > 0.0 && b > 0.0;
}

std::string ScalePairText(double a, double b) { return FormatNumber(a) + "," + FormatNumber(b); }

bool HasAxisScale(const HistoryRecord& rec) { return rec.num.count("sx0") || rec.num.count("sy0"); }

class Sweep1Command : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Cap", "No", {"Yes", "No"}, false, true}, {"ScaleX", "1,1", {}, false, false}, {"ScaleY", "1,1", {}, false, false}};
    WantObjects("Select rail");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!rail_) {
      // Id kept alongside the curve copy (not just CurvesOf's plain copy)
      // so a single-profile sweep built while History On is set can
      // record its rail's id for UpdateHistory - same reason PipeCommand
      // keeps its rail id (cmd_surface.cpp's PipeCommand::OnObjects).
      for (ObjectId id : ids) {
        if (std::optional<kernel::NurbsCurve> c = CurveOf(ctx, id)) { rail_ = c; rail_id_ = id; break; }
      }
      if (!rail_) { ctx.Warn("Select a rail curve"); Finish(); return; }
      DeselectAll(ctx, ids);
      closed_ = rail_->IsClosed();
      options = {{"Closed", closed_ ? "Yes" : "No", {"Yes", "No"}, false, true}, {"Cap", cap_ ? "Yes" : "No", {"Yes", "No"}, false, true},
                 {"ScaleX", ScalePairText(sx_[0], sx_[1]), {}, false, false}, {"ScaleY", ScalePairText(sy_[0], sy_[1]), {}, false, false}};
      WantObjects("Select cross section curves");
      accept_preselection = false;
      return;
    }
    std::vector<kernel::NurbsCurve> profiles;
    profile_ids_.clear();
    for (ObjectId id : ids) {
      if (std::optional<kernel::NurbsCurve> c = CurveOf(ctx, id)) { profiles.push_back(*c); profile_ids_.push_back(id); }
    }
    if (profiles.empty()) { ctx.Warn("Select cross section curves"); Finish(); return; }
    Build(ctx, profiles);
    Finish();
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    for (OptionSpec& o : options) {
      if (o.name == "Closed" && n == "Closed") { closed_ = (v == "Yes"); o.value = closed_ ? "Yes" : "No"; }
      if (o.name == "Cap" && n == "Cap") { cap_ = (v == "Yes"); o.value = cap_ ? "Yes" : "No"; }
      if (o.name == n && (n == "ScaleX" || n == "ScaleY")) {
        double a = 1.0, b = 1.0;
        if (ParseScalePair(v, a, b)) {
          double* dst = n == "ScaleX" ? sx_ : sy_;
          dst[0] = a;
          dst[1] = b;
          o.value = ScalePairText(a, b);
          scale_refusal_[n == "ScaleX" ? 0 : 1].clear();  // only this axis's own refusal: a valid ScaleY must not forgive a refused ScaleX
          ctx.Print("Sweep1: " + n + " = " + o.value + " (scale along the section's " + (n == "ScaleX" ? "X" : "Y") + " axis at the rail start, at the rail end)");
        } else {
          // A refused scale must never turn into a silently UNSCALED sweep: the command builds nothing until a valid one is given.
          std::string& refusal = scale_refusal_[n == "ScaleX" ? 0 : 1];
          refusal = n + " must be 'start,end' - two positive numbers (or one), got '" + v + "'; zero, negative and non-numeric scales are refused";
          ctx.Warn("Sweep1: " + refusal);
        }
      }
    }
  }
  // The per-axis scale pairs as History numbers (an axis left at 1,1 records nothing, so a plain sweep records exactly what it did before).
  void RecordAxisScale(HistoryRecord& rec) const {
    if (sx_[0] != 1.0 || sx_[1] != 1.0) { rec.num["sx0"] = sx_[0]; rec.num["sx1"] = sx_[1]; }
    if (sy_[0] != 1.0 || sy_[1] != 1.0) { rec.num["sy0"] = sy_[0]; rec.num["sy1"] = sy_[1]; }
  }
  void Build(CommandContext& ctx, std::vector<kernel::NurbsCurve> profiles) {
    const kernel::NurbsCurve& rail = *rail_;
    for (const std::string& refusal : scale_refusal_) {
      if (refusal.empty()) continue;
      ctx.Print("Sweep1: no surface created - " + refusal);
      return;
    }
    ctx.Doc().BeginChange("Sweep1");
    // A single cross-section is the kernel's own exact Brep::Sweep1 case
    // (rotation-minimizing-frame transport + skin) - use it rather than
    // this file's own approximate RMF-loft-and-fit construction below
    // whenever it applies. Multiple cross-sections (the anchor-blend case
    // right below) and any case the kernel refuses (a degenerate rail or
    // section) still fall back to the existing construction unchanged, so
    // this can only add capability, never remove it. See RebuildSweep1's
    // own comment (below the anonymous namespace this file closes further
    // down) for why this stays uncapped/a Surface even for a closed
    // section, same as before.
    if (profiles.size() == 1) {
      HistoryRecord rec;
      rec.command = "Sweep1";
      if (cap_) rec.num["cap"] = 1.0;
      RecordAxisScale(rec);
      if (std::optional<SceneObject> exact = RebuildSweep1(ctx, profiles.front(), rail, rec)) {
        const ObjectId new_id = ctx.Doc().Add(std::move(*exact));
        if (rail_id_ && !profile_ids_.empty()) RecordHistoryIfEnabled(ctx, new_id, "Sweep1", {*rail_id_, profile_ids_.front()}, rec.num);
        ctx.Print("Sweep1: exact kernel sweep (rotation-minimizing frames)");
        return;
      }
      if (HasAxisScale(rec)) {
        ctx.Print("Sweep1: no surface created - the per-axis scale (ScaleX / ScaleY) is only applied by the exact kernel sweep, and it is not silently dropped for the sampled approximation");
        return;
      }
    }
    // Several cross-sections: the kernel's exact Brep::Sweep1Sections first -
    // every section anchored on the rail (a closed planar one where the rail
    // crosses its plane), decoded in the rotation-minimizing frame there,
    // blended between neighbours and skinned through the stations, so each
    // section is reproduced exactly. The kernel measures the wall against the
    // exact blended sections between stations; that figure is held to the
    // DOCUMENT tolerance here (retried at 64, 128 and 256 stations when 32 are
    // not enough). Anything the kernel refuses (a closed rail, a plane the rail
    // never crosses, two sections at one station, seams that do not line up) or
    // that misses tolerance falls through, with the reason printed, to the
    // older sampled construction below unchanged. Always an uncapped Surface,
    // like every other result of this command; no history is recorded.
    if (profiles.size() >= 2) {
      std::string multi_reason;
      HistoryRecord rec;
      rec.command = "Sweep1";
      if (cap_) rec.num["cap"] = 1.0;
      RecordAxisScale(rec);
      std::vector<const kernel::NurbsCurve*> ptrs;
      for (const kernel::NurbsCurve& c : profiles) ptrs.push_back(&c);
      if (std::optional<SceneObject> exact = RebuildSweep1Sections(ctx, ptrs, rail, rec, multi_reason)) {
        const ObjectId new_id = ctx.Doc().Add(std::move(*exact));
        // History: {rail, section 1, section 2, ...} - the same order UpdateHistory rebuilds from.
        if (rail_id_ && profile_ids_.size() == profiles.size()) {
          std::vector<ObjectId> srcs{*rail_id_};
          srcs.insert(srcs.end(), profile_ids_.begin(), profile_ids_.end());
          RecordHistoryIfEnabled(ctx, new_id, "Sweep1", srcs, rec.num);
        }
        return;
      }
      if (HasAxisScale(rec)) {
        ctx.Print("Sweep1: no surface created - the per-axis scale (ScaleX / ScaleY) was refused by the exact multi-section sweep (" + multi_reason + ") and is not silently dropped for the sampled approximation");
        return;
      }
      ctx.Print("Sweep1: the exact multi-section sweep was not used (" + multi_reason + "); using the sampled approximation");
    }
    if (cap_) ctx.Print("Sweep1: Cap=Yes not applied - the sampled approximation is an uncapped surface (only the kernel's exact sweeps are capped)");
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
    ctx.Doc().Add(SceneObject::MakeSurface(SurfaceFromRows(rows, closed_profile, wrap)));
    ctx.Print("Sweep1: " + std::to_string(profiles.size()) + " section(s) along " + std::to_string(nrows) + " rail stations" + (wrap ? ", closed" : ""));
  }
  std::optional<kernel::NurbsCurve> rail_;
  std::optional<ObjectId> rail_id_;
  std::vector<ObjectId> profile_ids_;
  bool closed_ = false;
  bool cap_ = false;
  double sx_[2] = {1.0, 1.0};  // ScaleX: scale along the section's X axis at the rail start / end
  double sy_[2] = {1.0, 1.0};  // ScaleY
  std::string scale_refusal_[2];  // [0] ScaleX, [1] ScaleY: set by a refused value of that axis; Build then creates nothing until it is replaced by a valid one
};

// ---------------------------------------------------------------------------
// Sweep2: two rails, then cross-sections scaled to span the rails.
// ---------------------------------------------------------------------------

class Sweep2Command : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Cap", "No", {"Yes", "No"}, false, true}};
    WantObjects("Select first rail");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Cap") { cap_ = (v == "Yes"); if (!options.empty()) options[0].value = cap_ ? "Yes" : "No"; }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (rails_.size() < 2) {
      // Ids kept alongside each rail curve copy (not just CurvesOf's plain
      // copy) so a single-cross-section sweep built while History On is
      // set can record both rails' ids for UpdateHistory - same reason
      // Sweep1Command keeps its rail id.
      std::vector<kernel::NurbsCurve> c;
      std::vector<ObjectId> found_ids;
      for (ObjectId id : ids) {
        if (std::optional<kernel::NurbsCurve> cc = CurveOf(ctx, id)) { c.push_back(*cc); found_ids.push_back(id); }
      }
      if (c.empty()) { ctx.Warn("Select a rail curve"); Finish(); return; }
      rails_.push_back(c.front());
      rail_ids_.push_back(found_ids.front());
      if (rails_.size() == 1 && c.size() >= 2) { rails_.push_back(c[1]); rail_ids_.push_back(found_ids[1]); }
      DeselectAll(ctx, ids);
      accept_preselection = false;
      WantObjects(rails_.size() < 2 ? "Select second rail" : "Select cross section curves");
      return;
    }
    std::vector<kernel::NurbsCurve> profiles;
    profile_ids_.clear();
    for (ObjectId id : ids) {
      if (std::optional<kernel::NurbsCurve> c = CurveOf(ctx, id)) { profiles.push_back(*c); profile_ids_.push_back(id); }
    }
    if (profiles.empty()) { ctx.Warn("Select cross section curves"); Finish(); return; }
    Build(ctx, profiles);
    Finish();
  }
  void Build(CommandContext& ctx, std::vector<kernel::NurbsCurve> profiles) {
    // A single cross-section is the kernel's own exact Brep::Sweep2 case
    // (two-rail frame transport + skin, with rail2's direction matched to
    // rail1's the exact same way this file's own approximate construction
    // below already does, replicated inside Brep::Sweep2 itself) - use it
    // rather than this file's own scaled-frame-and-fit construction
    // whenever it applies. Multiple cross-sections try the kernel's
    // Brep::Sweep2Sections just below (the anchor-blend case after that is
    // its fail-open fallback), and any case the kernel refuses (e.g. a
    // degenerate rail/section, or the rails touching) still falls back to the
    // existing construction unchanged, so this can only add capability, never
    // remove it - same shape as Sweep1Command::Build's own fast path
    // above. Built uncapped for the same reason RebuildSweep1 is: this
    // command has no Cap option of its own.
    if (profiles.size() == 1) {
      HistoryRecord rec;
      rec.command = "Sweep2";
      if (cap_) rec.num["cap"] = 1.0;
      if (std::optional<SceneObject> exact = RebuildSweep2(ctx, profiles.front(), rails_[0], rails_[1], rec)) {
        ctx.Doc().BeginChange("Sweep2");
        const ObjectId new_id = ctx.Doc().Add(std::move(*exact));
        if (rail_ids_.size() == 2 && !profile_ids_.empty()) {
          RecordHistoryIfEnabled(ctx, new_id, "Sweep2", {rail_ids_[0], rail_ids_[1], profile_ids_.front()}, rec.num);
        }
        ctx.Print("Sweep2: exact kernel sweep (two-rail frame transport)");
        return;
      }
    }
    // Several cross-sections: the kernel's exact Brep::Sweep2Sections first -
    // every section placed in the two-rail frame of its own station, the
    // rail-local shapes blended between neighbouring sections and skinned
    // through the stations, so each section is reproduced and the wall edges
    // follow the rails. Its own report is measured against the DOCUMENT
    // tolerance here: the sections must be reproduced within it, and when the
    // sections touch the rails the wall's edges must stay within it of the
    // rails (retried at 64, 128, 256 and 512 stations if the first 32 are not enough).
    // Closed rails (both closed: a cyclic sweep, the wall closed round the ring)
    // and rails that meet at an end (a wall pinched to a point there) are the
    // kernel's own cases now. Anything the kernel refuses (anchors that cross,
    // rails that touch at an interior station) or that misses tolerance falls through, with the
    // reason printed, to the original scaled-frame fit below unchanged.
    if (profiles.size() >= 2) {
      std::string multi_reason;
      HistoryRecord rec;
      rec.command = "Sweep2";
      if (cap_) rec.num["cap"] = 1.0;
      std::vector<const kernel::NurbsCurve*> ptrs;
      for (const kernel::NurbsCurve& c : profiles) ptrs.push_back(&c);
      if (std::optional<SceneObject> exact = RebuildSweep2Sections(ctx, ptrs, rails_[0], rails_[1], rec, multi_reason)) {
        ctx.Doc().BeginChange("Sweep2");
        const ObjectId new_id = ctx.Doc().Add(std::move(*exact));
        // History: {rail 1, rail 2, section 1, section 2, ...}.
        if (rail_ids_.size() == 2 && profile_ids_.size() == profiles.size()) {
          std::vector<ObjectId> srcs{rail_ids_[0], rail_ids_[1]};
          srcs.insert(srcs.end(), profile_ids_.begin(), profile_ids_.end());
          RecordHistoryIfEnabled(ctx, new_id, "Sweep2", srcs, rec.num);
        }
        return;
      }
      ctx.Print("Sweep2: the exact multi-section sweep was not used (" + multi_reason + "); using the scaled-frame fit");
    }
    if (cap_) ctx.Print("Sweep2: Cap=Yes not applied - the scaled-frame fit is an uncapped surface (only the kernel's exact sweeps are capped)");
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
  std::vector<ObjectId> rail_ids_;
  std::vector<ObjectId> profile_ids_;
  bool cap_ = false;
};

// ---------------------------------------------------------------------------
// NetworkSrf: 2 curves -> exact ruled surface; 3/4 curves -> Coons patch.
// ---------------------------------------------------------------------------

// RebuildNetworkSrf2 (shared with UpdateHistory, cmd_history.cpp) is
// defined below, after this anonymous namespace closes - same placement
// as RebuildPipe/RebuildPatch/RebuildSweep1/RebuildSweep2, which need
// external linkage to match history_rebuild.h's own declaration (already
// visible here via that header's #include at the top of this file, so the
// call below resolves against it without needing a local redeclaration).

// Moves the end control point of a clamped NURBS curve (its first or last
// control point IS the curve's own start/end point) to `p`, keeping its
// weight. Returns false (curve untouched) for an unclamped curve, whose end
// point is not a control point.
bool MoveCurveEnd(kernel::NurbsCurve& c, bool at_start, const Point3d& p) {
  ON_NurbsCurve& r = c.raw();
  if (!r.IsClamped(2)) return false;
  const int i = at_start ? 0 : r.CVCount() - 1;
  if (r.IsRational()) {
    const double w = r.Weight(i);
    return r.SetCV(i, ON_4dPoint(p.x * w, p.y * w, p.z * w, w));
  }
  return r.SetCV(i, p);
}

// Largest distance from a sampled point of any curve in `inputs` to the four
// boundary isocurves of `s` (the collapsed isocurve of a 3-sided patch is a
// point). Each distance is the kernel's closest-point search on the boundary
// isocurve as a NURBS curve, not a distance to a polyline, so a result that
// reproduces the inputs exactly reads at rounding level (~1e-15) instead of at
// a polyline's own chord error.
double BoundaryDeviation(const kernel::NurbsSurface& s, const std::vector<kernel::NurbsCurve>& inputs) {
  std::vector<kernel::NurbsCurve> edges;
  for (int dir = 0; dir < 2; ++dir) {
    const kernel::Interval d = s.Domain(dir == 0 ? 1 : 0);  // the constant parameter's own domain
    for (double c : {d.min, d.max}) {
      std::unique_ptr<ON_Curve> iso(s.raw().IsoCurve(dir, c));
      const ON_NurbsCurve* nc = iso ? ON_NurbsCurve::Cast(iso.get()) : nullptr;
      if (!nc) continue;
      kernel::NurbsCurve k;
      k.raw() = *nc;
      edges.push_back(k);
    }
  }
  double worst = 0.0;
  for (const kernel::NurbsCurve& in : inputs) {
    const kernel::Interval d = in.Domain();
    for (int k = 0; k <= 64; ++k) {
      const Point3d p = in.PointAt(d.min + (d.max - d.min) * k / 64.0);
      double best = std::numeric_limits<double>::max();
      for (const kernel::NurbsCurve& e : edges) best = std::min(best, p.DistanceTo(e.PointAt(e.ClosestPointParameter(p, 400))));
      worst = std::max(worst, best);
    }
  }
  return worst;
}


// The 3- and 4-curve NetworkSrf/EdgeSrf construction (the kernel's exact
// Coons patch, its triangular variant, or the fitted bilinear fallback), shared
// by the live command below and UpdateHistory (RebuildNetworkSrfLoop). `note`
// receives the one-line description the live command prints.
std::optional<SceneObject> BuildNetworkSrfLoop(CommandContext& ctx, const std::vector<kernel::NurbsCurve>& curves, std::string& note) {
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
  // Curves whose knot vectors are not clamped (an open uniform / periodic-style B-spline: its ends are not control
  // points) can neither have an open corner closed by moving an end control point nor be trusted to unify inside
  // the kernel's Coons patch. Re-express each as a clamped NURBS first (ON_NurbsCurve::ClampEnd, a knot-insertion
  // that leaves the shape alone) and MEASURE the result against the original at 129 parameters; a clamp that moves
  // the curve by more than 1e-9 of its size is not used and the curve is left as it was.
  std::string clamp_note;
  {
    int clamped = 0;
    double worst = 0.0;
    for (kernel::NurbsCurve& c : loop) {
      if (c.raw().IsClamped(2)) continue;
      kernel::NurbsCurve k = c;
      if (!k.raw().ClampEnd(2)) continue;
      const kernel::Interval d0 = c.Domain(), d1 = k.Domain();
      double dev = 0.0;
      for (int i = 0; i <= 128; ++i) {
        const double f = i / 128.0;
        dev = std::max(dev, c.PointAt(d0.min + f * (d0.max - d0.min)).DistanceTo(k.PointAt(d1.min + f * (d1.max - d1.min))));
      }
      if (dev <= 1e-9 * std::max(sectionfit::CurveSize(c), 1e-9)) { c = k; ++clamped; worst = std::max(worst, dev); }
    }
    if (clamped > 0) clamp_note = ", " + std::to_string(clamped) + " unclamped curve(s) re-expressed as clamped NURBS first (measured shape change " + FormatDeviation(worst) + ")";
  }
  // Loop closure against the DOCUMENT tolerance. The chain above joined the
  // curves head to tail by proximity, so the corner between loop[i] and
  // loop[i+1] (and the last-to-first corner) can still be open by up to the
  // model's absolute tolerance. A loop open by no more than that is the same
  // loop as far as this document is concerned: the two ends at each corner are
  // moved to their midpoint (each curve's end control point, which is its end
  // point, so the curve moves by half the gap at most), after which the kernel's
  // exact construction sees a truly closed loop. A larger gap is not a loop at
  // all and is reported as the reason for the approximate path below.
  const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  const size_t nloop = loop.size();
  double max_gap = 0.0;
  for (size_t i = 0; i < nloop; ++i) {
    const kernel::NurbsCurve& a = loop[i];
    const kernel::NurbsCurve& b = loop[(i + 1) % nloop];
    max_gap = std::max(max_gap, a.PointAt(a.Domain().max).DistanceTo(b.PointAt(b.Domain().min)));
  }
  std::string closure_note = clamp_note;
  std::string fallback_reason;
  if (max_gap > tol) {
    fallback_reason = "the " + std::to_string(nloop) + " curves do not close into a loop within the document tolerance " + FormatNumber(tol) +
                      " (largest corner gap " + FormatNumber(max_gap) + ")";
  } else if (max_gap > 1e-12) {
    std::vector<kernel::NurbsCurve> snapped = loop;
    bool all_moved = true;
    for (size_t i = 0; i < nloop && all_moved; ++i) {
      const size_t j = (i + 1) % nloop;
      const Point3d mid = (snapped[i].PointAt(snapped[i].Domain().max) + snapped[j].PointAt(snapped[j].Domain().min)) * 0.5;
      all_moved = MoveCurveEnd(snapped[i], false, mid) && MoveCurveEnd(snapped[j], true, mid);
    }
    if (all_moved) {
      loop = snapped;
      closure_note += ", corners closed within the document tolerance " + FormatNumber(tol) + " (largest gap " + FormatNumber(max_gap) + " split between the two ends)";
    } else {
      fallback_reason = "a curve is not clamped, so its open corner (gap " + FormatNumber(max_gap) + ") cannot be closed exactly";
    }
  }
  // Exactly 4 curves: try the kernel's exact NurbsSurface::CoonsPatch()
  // first - real NURBS control-point algebra that reproduces all 4
  // boundary curves exactly, rather than the sample-then-refit
  // approximation below (self-checks its own result and fails closed,
  // so falling through to that approximation on Result::Failed - e.g.
  // curves whose degrees/knots can't be unified within tolerance -
  // never leaves this command worse off than before this swap).
  if (loop.size() == 4 && fallback_reason.empty()) {
    kernel::NurbsSurface exact;
    if (kernel::NurbsSurface::CoonsPatch(loop[0], loop[2], loop[3], loop[1], exact, tol) == kernel::Result::Ok) {
      note = "NetworkSrf: exact Coons patch through 4 curves (all 4 boundaries reproduced exactly)" + closure_note +
             "; measured boundary deviation from the closed loop " + FormatDeviation(BoundaryDeviation(exact, loop));
      return SceneObject::MakeSurface(exact);
    }
    fallback_reason = "the kernel's exact 4-curve Coons patch refused these curves (their degrees/knots could not be unified, or the self-check missed a boundary)";
  }
  // Exactly 3 curves: the kernel's exact NurbsSurface::CoonsPatchTriangular()
  // (the 4-curve patch with its fourth edge collapsed to a point). The three
  // curves are handed over in the order the chain above found them; the kernel
  // itself resolves which of the last two is the patch's "top" and which its
  // "right", and each one's direction, by trying both orders against the first
  // curve's own direction. Same exact-or-fall-through contract as the 4-curve
  // case above, with the reason printed when it falls through.
  if (loop.size() == 3 && fallback_reason.empty()) {
    kernel::NurbsSurface exact;
    if (kernel::NurbsSurface::CoonsPatchTriangular(loop[0], loop[1], loop[2], exact, tol) == kernel::Result::Ok) {
      note = "NetworkSrf: exact 3-sided Coons patch through 3 curves (all 3 boundaries reproduced exactly)" + closure_note +
             "; measured boundary deviation from the closed loop " + FormatDeviation(BoundaryDeviation(exact, loop));
      return SceneObject::MakeSurface(exact);
    }
    fallback_reason = "the kernel's exact 3-sided Coons patch refused these curves (their degrees/knots could not be unified, or the self-check missed a boundary)";
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
  note = "NetworkSrf: fitted bilinear Coons patch through " + std::to_string(curves.size()) + " curves (approximate fallback" +
         (fallback_reason.empty() ? std::string() : ": " + fallback_reason) + ")";
  return SceneObject::MakeSurface(SurfaceFromRows(rows));
}

// NetworkSrf through FIVE OR MORE boundary curves. No exact multi-sided Coons
// patch exists in the kernel (3 and 4 sides do), and the command used to keep
// only the first 4 curves of a longer selection. Here the curves are chained
// head to tail by endpoint proximity, each corner is closed within the
// DOCUMENT tolerance (moved to the midpoint, as the 3/4-curve case does), the
// chain is joined into ONE closed NURBS curve (exact NurbsCurve::Join), a
// least-squares NURBS surface is fitted through the boundary samples
// (NurbsSurface::FitPatch) and trimmed by the joined loop
// (Brep::TrimmedPatchFace, one real B-rep face). This is NOT an interpolating
// surface: the worst deviation of the fit from the sampled curves and the
// kernel-measured deviation of the trimmed edge from the loop are printed, and
// a fit missing the document tolerance is flagged. Returns nullopt with the
// reason when the curves do not close, the loop folds over its best-fit plane
// (a height-field fit cannot represent it) or the kernel refuses.
std::optional<SceneObject> BuildNetworkSrfMany(CommandContext& ctx, const std::vector<kernel::NurbsCurve>& curves, std::string& note,
                                               std::string& reason) {
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
  const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  const size_t n = loop.size();
  double max_gap = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const kernel::NurbsCurve& a = loop[i];
    const kernel::NurbsCurve& b = loop[(i + 1) % n];
    max_gap = std::max(max_gap, a.PointAt(a.Domain().max).DistanceTo(b.PointAt(b.Domain().min)));
  }
  if (max_gap > tol) {
    reason = "the " + std::to_string(n) + " curves do not close into a loop within the document tolerance " + FormatNumber(tol) + " (largest corner gap " + FormatNumber(max_gap) + ")";
    return std::nullopt;
  }
  if (max_gap > 1e-12) {
    for (size_t i = 0; i < n; ++i) {
      const size_t j = (i + 1) % n;
      const Point3d mid = (loop[i].PointAt(loop[i].Domain().max) + loop[j].PointAt(loop[j].Domain().min)) * 0.5;
      if (!MoveCurveEnd(loop[i], false, mid) || !MoveCurveEnd(loop[j], true, mid)) {
        reason = "a curve is not clamped, so its open corner (gap " + FormatNumber(max_gap) + ") cannot be closed exactly";
        return std::nullopt;
      }
    }
  }
  kernel::NurbsCurve joined = loop[0];
  try {
    for (size_t i = 1; i < n; ++i) {
      if (joined.Join(loop[i], std::max(tol, 1e-6)) != kernel::Result::Ok) { reason = "the kernel could not join curve " + std::to_string(i + 1) + " onto the chain"; return std::nullopt; }
    }
  } catch (const std::exception& e) {
    reason = std::string("joining the curves into one loop failed: ") + e.what();
    return std::nullopt;
  }
  if (!joined.IsClosed()) { reason = "the joined loop is not closed"; return std::nullopt; }
  Row boundary;
  for (const kernel::NurbsCurve& c : loop) {
    const Row smp = SampleCurve(c, 96, false);
    boundary.insert(boundary.end(), smp.begin(), smp.end());
  }
  kernel::NurbsSurface patch;
  kernel::NurbsSurface::PatchFitReport rep;
  kernel::Result fr = kernel::Result::Failed;
  try {
    fr = kernel::NurbsSurface::FitPatch(boundary, {}, tol, patch, &rep);
  } catch (const std::exception& e) {
    reason = std::string("the kernel patch fit threw: ") + e.what();
    return std::nullopt;
  }
  if (fr != kernel::Result::Ok) { reason = "the kernel could not fit a surface through these curves (collinear or singular)"; return std::nullopt; }
  // The fit is a height field over its plane: a loop that crosses itself when
  // projected onto that plane cannot be trimmed out of it.
  std::vector<std::pair<double, double>> poly;
  for (const Point3d& q : SampleCurve(joined, 128, true)) {
    const ON_3dVector d = q - rep.plane_origin;
    poly.emplace_back(ON_DotProduct(d, rep.plane_x), ON_DotProduct(d, rep.plane_y));
  }
  auto cross = [](double ax, double ay, double bx, double by, double cx, double cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
  for (size_t i = 0; i < poly.size(); ++i) {
    const size_t i2 = (i + 1) % poly.size();
    for (size_t j = i + 2; j < poly.size(); ++j) {
      const size_t j2 = (j + 1) % poly.size();
      if (j2 == i) continue;
      const double d1 = cross(poly[i].first, poly[i].second, poly[i2].first, poly[i2].second, poly[j].first, poly[j].second);
      const double d2 = cross(poly[i].first, poly[i].second, poly[i2].first, poly[i2].second, poly[j2].first, poly[j2].second);
      const double d3 = cross(poly[j].first, poly[j].second, poly[j2].first, poly[j2].second, poly[i].first, poly[i].second);
      const double d4 = cross(poly[j].first, poly[j].second, poly[j2].first, poly[j2].second, poly[i2].first, poly[i2].second);
      if (d1 * d2 < 0 && d3 * d4 < 0) { reason = "the loop folds over its best-fit plane (it crosses itself when projected), which a height-field fit cannot represent"; return std::nullopt; }
    }
  }
  try {
    double edge_dev = 0.0;
    kernel::Brep face = kernel::Brep::TrimmedPatchFace(patch, rep, joined, &edge_dev);
    note = "NetworkSrf: " + std::to_string(n) + " curves chained into a closed loop" +
           (max_gap > 1e-12 ? " (corners closed within the document tolerance " + FormatNumber(tol) + ", largest gap " + FormatNumber(max_gap) + ")" : std::string()) +
           "; kernel least-squares NURBS patch (" + std::to_string(rep.u_count) + " x " + std::to_string(rep.v_count) + " net, worst deviation from the curves " +
           FormatDeviation(rep.max_deviation) + " vs tolerance " + FormatNumber(tol) + (rep.met_tolerance ? ": met" : ": NOT met") +
           ") trimmed by the joined loop, trimmed edge within " + FormatDeviation(edge_dev) +
           " of the loop (approximate: fitted through the boundary samples, not an interpolating Coons patch)";
    return SceneObject::MakeBrep(std::move(face));
  } catch (const std::exception& e) {
    reason = std::string("the loop could not be trimmed out of the fitted surface: ") + e.what();
    return std::nullopt;
  }
}

void NetworkSrf(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  // Ids kept in lockstep with CurvesOf's own curve-only filter (not just
  // ids itself) so the 2-curve case's recorded history sources line up
  // with curves[0]/curves[1] exactly, even if a non-curve object was also
  // selected - the same reason Sweep1Command/Sweep2Command/PipeCommand
  // above track ids alongside their own curve copies rather than the raw
  // selection list.
  std::vector<kernel::NurbsCurve> curves;
  std::vector<ObjectId> curve_ids;
  for (ObjectId id : ids) {
    if (std::optional<kernel::NurbsCurve> c = CurveOf(ctx, id)) { curves.push_back(*c); curve_ids.push_back(id); }
  }
  if (curves.size() < 2) { ctx.Warn("Select 2, 3 or 4 curves"); return; }
  // A CURVE NETWORK (4 or more curves, some crossing in their interiors - e.g.
  // 3 curves one way crossing 3 the other): the kernel sorts the curves into
  // the two directions by where they meet, requires every curve to meet every
  // curve of the other direction within the document tolerance, fits one
  // bicubic NURBS surface through the curves and the meeting points and MEASURES
  // every input curve's distance to it, refusing above the tolerance. A set that
  // only meets end to end (a boundary loop) is left to the loop paths below; a
  // refusal prints its reason and the old behaviour carries on.
  if (curves.size() >= 4) {
    const double net_tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
    kernel::NurbsSurface net;
    kernel::NurbsSurface::CurveNetworkReport rep;
    kernel::Result nr = kernel::Result::Failed;
    try {
      nr = kernel::NurbsSurface::FitCurveNetwork(curves, net_tol, net, rep);
    } catch (const std::exception& e) {
      rep.reason = e.what();
    }
    if (nr == kernel::Result::Ok && rep.interior_crossings > 0) {
      ctx.Doc().BeginChange("NetworkSrf");
      ctx.Doc().Add(SceneObject::MakeSurface(net));
      std::string per;
      for (size_t i = 0; i < rep.curve_deviation.size(); ++i) per += (i ? ", " : "") + FormatDeviation(rep.curve_deviation[i]);
      ctx.Print("NetworkSrf: curve network of " + std::to_string(rep.u_curves) + " x " + std::to_string(rep.v_curves) + " curves (" +
                std::to_string(rep.intersections) + " meetings, " + std::to_string(rep.interior_crossings) + " crossing in their interiors, worst meeting gap " +
                FormatDeviation(rep.max_intersection_gap) + "); NURBS surface (cubic where the net allows) fitted through the curves and the meetings (" + std::to_string(rep.net_u) + " x " +
                std::to_string(rep.net_v) + " net); measured distance of each input curve from the surface: " + per + " (worst " + FormatDeviation(rep.max_deviation) +
                ", document tolerance " + FormatNumber(net_tol) + ": met). Approximate least-squares fit over the curves' bounding rectangle, not an interpolating or trimmed surface.");
      return;
    }
    if (nr != kernel::Result::Ok && rep.any_interior_meeting) {
      ctx.Warn("NetworkSrf: these curves cross in their interiors but cannot be fitted as a curve network (" + rep.reason + "); using the older loop behaviour");
    }
  }
  if (curves.size() > 4) {
    std::string many_note, many_reason;
    std::optional<SceneObject> many = BuildNetworkSrfMany(ctx, curves, many_note, many_reason);
    if (many) {
      ctx.Doc().BeginChange("NetworkSrf");
      ctx.Doc().Add(std::move(*many));
      ctx.Print(many_note);
      return;
    }
    ctx.Warn("NetworkSrf: " + std::to_string(curves.size()) + " curves could not be fitted (" + many_reason + "); using the first 4 curves only");
    curves.resize(4);
    curve_ids.resize(4);
  }
  if (curves.size() == 2) {
    std::optional<SceneObject> fresh = RebuildNetworkSrf2(ctx, curves[0], curves[1], HistoryRecord{});
    if (!fresh) { ctx.Warn("NetworkSrf: ruled surface failed"); return; }
    ctx.Doc().BeginChange("NetworkSrf");
    const ObjectId new_id = ctx.Doc().Add(std::move(*fresh));
    // History (UpdateHistory, cmd_history.cpp's RebuildNetworkSrf2
    // dispatch): deterministic from these two source curves alone, the
    // same "simple, unambiguous rebuild" shape Pipe/Sweep1/Sweep2/Patch
    // above already qualify for - which of the two needs reversing to
    // match head-to-tail is re-derived fresh from the sources' *current*
    // geometry on rebuild, not itself recorded, the same way Pipe's own
    // `wrap` is read fresh rather than frozen at creation time.
    RecordHistoryIfEnabled(ctx, new_id, "NetworkSrf2", {curve_ids[0], curve_ids[1]}, {});
    ctx.Print("NetworkSrf: ruled surface between 2 curves");
    return;
  }
  std::string note;
  std::optional<SceneObject> built = BuildNetworkSrfLoop(ctx, curves, note);
  if (!built) { ctx.Warn("NetworkSrf: could not build a surface through these curves"); return; }
  ctx.Doc().BeginChange("NetworkSrf");
  const ObjectId new_id = ctx.Doc().Add(std::move(*built));
  // History: the loop order/orientation is re-derived from the curves' current
  // geometry on every rebuild, so only the source ids (in selection order) are
  // recorded.
  RecordHistoryIfEnabled(ctx, new_id, "NetworkSrf", curve_ids, {});
  ctx.Print(note);
}

// ---------------------------------------------------------------------------
// Patch: planar fit through curves and points.
// ---------------------------------------------------------------------------

// The non-planar Patch: the kernel's least-squares NurbsSurface::FitPatch through the boundary samples of every
// selected curve and the selected point objects, trimmed by the largest closed curve (others becoming holes) when
// there is one. Shared by Patch (live) and RebuildPatchFit (UpdateHistory). Returns the object (notes printed), or
// nullopt with `planar_reason` set when the data are not better served by the fitted surface than by the flat plane
// (the live command then runs its planar construction, the rebuild refuses). `pts` holds every curve sample (32 per
// curve) and point, `flat_dev` the worst distance of those from their own best-fit plane.
std::optional<SceneObject> FitNonPlanarPatch(CommandContext& ctx, const Row& pts, const std::vector<kernel::NurbsCurve>& all_curves,
                                              const Row& point_objects, double flat_dev, double patch_tol, int ncrv, int npts,
                                              std::string& planar_reason) {
  (void)pts;
  std::vector<kernel::NurbsCurve> closed;
  for (const kernel::NurbsCurve& c : all_curves) if (c.IsClosed()) closed.push_back(c);
  {
    Row boundary_samples;
    for (const kernel::NurbsCurve& c : all_curves) {
      const Row smp = SampleCurve(c, 96, c.IsClosed());
      boundary_samples.insert(boundary_samples.end(), smp.begin(), smp.end());
    }
    kernel::NurbsSurface patch;
    kernel::NurbsSurface::PatchFitReport rep;
    kernel::Result fr = kernel::Result::Failed;
    try {
      fr = kernel::NurbsSurface::FitPatch(boundary_samples, point_objects, patch_tol, patch, &rep);
    } catch (const std::exception& e) {
      planar_reason = std::string("the kernel patch fit threw: ") + e.what();
    }
    if (fr == kernel::Result::Ok && rep.max_deviation < flat_dev) {
      const std::string net = std::to_string(rep.u_count) + " x " + std::to_string(rep.v_count) + " net, worst data deviation " +
                              FormatDeviation(rep.max_deviation) + " vs tolerance " + FormatNumber(patch_tol) +
                              (rep.met_tolerance ? ": met" : ": NOT met") + "; the best-fit plane misses the data by " + FormatDeviation(flat_dev);
      if (!rep.met_tolerance) ctx.Warn("Patch: the least-squares surface misses the data by " + FormatDeviation(rep.max_deviation) + ", more than the document tolerance " + FormatNumber(patch_tol) + " (still better than the flat plane's " + FormatDeviation(flat_dev) + ")");
      if (!closed.empty()) {
        // One closed curve trims the fitted surface; with several, the one
        // enclosing the largest area (in the fit's own plane coordinates) is the
        // outer boundary and every other closed curve must lie wholly inside it
        // and clear of the other holes to become a hole of the same face.
        // Anything else (a curve crossing another, two disjoint islands) is
        // reported and gives the untrimmed surface, as before.
        using Poly = std::vector<std::pair<double, double>>;
        std::vector<Poly> polys;
        for (const kernel::NurbsCurve& c : closed) {
          Poly poly;
          for (const Point3d& q : SampleCurve(c, 96, true)) {
            const ON_3dVector d = q - rep.plane_origin;
            poly.emplace_back(ON_DotProduct(d, rep.plane_x), ON_DotProduct(d, rep.plane_y));
          }
          polys.push_back(std::move(poly));
        }
        auto signed_area = [](const Poly& pl) {
          double a = 0.0;
          for (size_t i = 0; i < pl.size(); ++i) { const auto& p0 = pl[i]; const auto& p1 = pl[(i + 1) % pl.size()]; a += p0.first * p1.second - p1.first * p0.second; }
          return 0.5 * a;
        };
        auto inside = [](const Poly& pl, double x, double y) {
          bool in = false;
          for (size_t i = 0, j = pl.size() - 1; i < pl.size(); j = i++) {
            if (((pl[i].second > y) != (pl[j].second > y)) && (x < (pl[j].first - pl[i].first) * (y - pl[i].second) / (pl[j].second - pl[i].second) + pl[i].first)) in = !in;
          }
          return in;
        };
        auto all_inside = [&](const Poly& pl, const Poly& outer) {
          for (const auto& q : pl) if (!inside(outer, q.first, q.second)) return false;
          return true;
        };
        auto any_inside = [&](const Poly& pl, const Poly& outer) {
          for (const auto& q : pl) if (inside(outer, q.first, q.second)) return true;
          return false;
        };
        size_t outer_i = 0;
        for (size_t i = 1; i < closed.size(); ++i) if (std::fabs(signed_area(polys[i])) > std::fabs(signed_area(polys[outer_i]))) outer_i = i;
        std::string hole_problem;
        for (size_t i = 0; i < closed.size() && hole_problem.empty(); ++i) {
          if (i == outer_i) continue;
          if (!all_inside(polys[i], polys[outer_i])) hole_problem = "closed curve " + std::to_string(i + 1) + " is not wholly inside the outer boundary";
          for (size_t j = i + 1; j < closed.size() && hole_problem.empty(); ++j) {
            if (j == outer_i) continue;
            if (any_inside(polys[i], polys[j]) || any_inside(polys[j], polys[i])) hole_problem = "closed curves " + std::to_string(i + 1) + " and " + std::to_string(j + 1) + " overlap";
          }
        }
        if (hole_problem.empty()) {
          try {
            std::vector<kernel::NurbsCurve> holes;
            for (size_t i = 0; i < closed.size(); ++i) if (i != outer_i) holes.push_back(closed[i]);
            double edge_dev = 0.0;
            kernel::Brep face = kernel::Brep::TrimmedPatchFace(patch, rep, closed[outer_i], holes, &edge_dev);
            SceneObject made = SceneObject::MakeBrep(std::move(face));
            ctx.Print("Patch: non-planar data - kernel least-squares NURBS patch (" + net + ") trimmed by the closed curve" +
                      (holes.empty() ? "; the face's edge is within " + FormatDeviation(edge_dev) + " of the curve"
                                     : " with " + std::to_string(holes.size()) + " hole(s) cut by the other closed curve(s); the worst of the " +
                                           std::to_string(holes.size() + 1) + " trimmed edges is within " + FormatDeviation(edge_dev) + " of its curve"));
            return made;
          } catch (const std::exception& e) {
            ctx.Print(std::string("Patch: the closed curve(s) could not be trimmed out of the fitted surface (") + e.what() + "); keeping the untrimmed fitted surface");
          }
        } else {
          ctx.Print("Patch: " + hole_problem + "; keeping the untrimmed fitted surface");
        }
      }
      SceneObject made_srf = SceneObject::MakeSurface(patch);
      ctx.Print("Patch: non-planar data - kernel least-squares NURBS surface through " + std::to_string(ncrv) + " curve(s) and " + std::to_string(npts) +
                " point(s) (" + net + "); untrimmed, spanning the data's extent");
      return made_srf;
    }
    if (planar_reason.empty()) {
      planar_reason = fr != kernel::Result::Ok ? "the kernel could not fit a surface to this data (fewer than three points, collinear data, or a singular solve)"
                                               : "the fitted surface (worst deviation " + FormatDeviation(rep.max_deviation) + ") is no better than the flat plane (" + FormatDeviation(flat_dev) + ")";
    }
  }
  return std::nullopt;
}

void Patch(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  Row pts;
  Row point_objects;                          // the selected Point objects (through-points for the non-planar fit)
  std::vector<kernel::NurbsCurve> all_curves;  // every selected curve, open or closed (boundary constraints for the fit)
  std::vector<kernel::NurbsCurve> closed;
  std::vector<ObjectId> closed_ids;
  std::vector<ObjectId> fit_ids;               // every curve and point, in selection order (the history sources)
  int npts = 0, ncrv = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Point) { pts.push_back(o->point); point_objects.push_back(o->point); fit_ids.push_back(id); ++npts; }
    else if (o->kind == ObjectKind::Curve && o->curve) {
      all_curves.push_back(*o->curve);
      fit_ids.push_back(id);
      Row s = SampleCurve(*o->curve, 32, o->curve->IsClosed());
      pts.insert(pts.end(), s.begin(), s.end());
      if (o->curve->IsClosed()) { closed.push_back(*o->curve); closed_ids.push_back(id); }
      ++ncrv;
    }
  }
  if (pts.size() < 3) { ctx.Warn("Select curves and/or at least three points"); return; }
  const ON_Plane fit = FitPlane(pts);
  ctx.Doc().BeginChange("Patch");
  // Non-planar input: data that stray from their own best-fit plane by more
  // than the document tolerance used to be flattened onto that plane (a closed
  // curve's boundary projected onto it, or a flat rectangle). The kernel's
  // least-squares NurbsSurface::FitPatch fits a real NURBS surface through the
  // curve samples (boundary constraints) and the point objects instead, and
  // reports its own measured deviation; a single closed curve is then trimmed
  // out of that surface by Brep::TrimmedPatchFace so the result is bounded by
  // the curve. It is used only when it beats the flat plane at the data (a
  // smaller worst deviation); otherwise, or if the kernel refuses (fewer than
  // three points, collinear data), the reason is printed and the original
  // planar construction below runs unchanged.
  const double patch_tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  double flat_dev = 0.0;
  for (const Point3d& p : pts) flat_dev = std::max(flat_dev, std::fabs(fit.DistanceTo(p)));
  std::string planar_reason;
  if (flat_dev > patch_tol) {
    std::optional<SceneObject> fitted = FitNonPlanarPatch(ctx, pts, all_curves, point_objects, flat_dev, patch_tol, ncrv, npts, planar_reason);
    if (fitted) {
      const ObjectId new_id = ctx.Doc().Add(std::move(*fitted));
      // History: {every selected curve and point, in selection order}; RebuildPatchFit re-runs the same fit.
      if (fit_ids.size() == static_cast<size_t>(ncrv + npts)) RecordHistoryIfEnabled(ctx, new_id, "Patch", fit_ids, {{"fit", 1.0}});
      return;
    }
    ctx.Print("Patch: non-planar data (best-fit plane misses it by " + FormatDeviation(flat_dev) + ") but " + planar_reason + "; using the planar construction");
  }
  if (closed.size() == 1) {
    ON_Plane own;
    ON_Brep* b = nullptr;
    const bool exact_plane = closed[0].raw().IsPlanar(&own, ctx.Settings().absolute_tolerance);
    if (exact_plane) b = ON_BrepTrimmedPlane(own, closed[0].raw());
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
    if (b) {
      const ObjectId new_id = ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b)));
      // History (UpdateHistory, cmd_history.cpp's RebuildPatch dispatch):
      // only the exact-planar-trim sub-case is recorded, since that is the
      // only one RebuildPatch above reproduces - the non-planar-boundary
      // fallback just above depends on `fit`, derived from every point and
      // curve in the whole selection, not just this one curve, so it is
      // deliberately left unrecorded rather than recorded with a
      // misleadingly narrow source list.
      if (exact_plane) RecordHistoryIfEnabled(ctx, new_id, "Patch", {closed_ids[0]}, {});
      ctx.Print("Patch: planar face bounded by the closed curve");
      return;
    }
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
// Continuity (G0 / G1 / G2) to an adjacent surface edge: NetworkSrf and Patch
// (Loft's is in cmd_solids.cpp). Options and the edge pick: continuity_request.h.
// ---------------------------------------------------------------------------

// Index of the curve that lies best on `adj` (smallest worst distance of its samples to the surface), with that worst
// distance. The user picked a surface EDGE; the curve that runs along it is the one whose boundary is to continue it.
int CurveLyingOn(const std::vector<kernel::NurbsCurve>& curves, const kernel::NurbsSurface& adj, double& worst_out) {
  int best = 0;
  double best_worst = std::numeric_limits<double>::max();
  for (size_t i = 0; i < curves.size(); ++i) {
    double worst = 0.0;
    for (const Point3d& p : SampleCurve(curves[i], 17, curves[i].IsClosed())) worst = std::max(worst, adj.ClosestPoint(p, 40, 40).DistanceTo(p));
    if (worst < best_worst) { best_worst = worst; best = static_cast<int>(i); }
  }
  worst_out = best_worst;
  return best;
}

void NetworkSrfContinuity(CommandContext& ctx, const std::vector<ObjectId>& ids, const ContinuityRequest& req, const AdjacentEdge& edge) {
  std::vector<kernel::NurbsCurve> curves;
  for (ObjectId id : ids) if (std::optional<kernel::NurbsCurve> c = CurveOf(ctx, id)) curves.push_back(*c);
  const std::string head = std::string("NetworkSrf: ") + req.Name() + " continuity to " + edge.Describe();
  if (curves.size() < 4) {
    ctx.Warn(head + " refused: continuity is built by the kernel's curve-network fit (FitCurveNetwork), which needs 4 or more curves crossing in two directions; " +
             std::to_string(curves.size()) + " curve(s) selected (a 2-curve ruled surface or a 3/4-curve boundary loop has no continuity option); nothing was built");
    return;
  }
  const double net_tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  double lying = 0.0;
  const int idx = CurveLyingOn(curves, edge.surface, lying);
  kernel::NurbsSurface::NetworkEdgeContinuity ne;
  ne.curve_index = idx;
  ne.adjacent = &edge.surface;
  ne.continuity = req.continuity;
  ne.angle_tolerance = req.angle_tol;
  ne.curvature_tolerance = req.curv_tol;
  kernel::NurbsSurface net;
  kernel::NurbsSurface::CurveNetworkReport rep;
  kernel::Result nr = kernel::Result::Failed;
  try {
    nr = kernel::NurbsSurface::FitCurveNetwork(curves, net_tol, net, rep, {ne});
  } catch (const std::exception& e) {
    rep.reason = e.what();
  }
  if (nr != kernel::Result::Ok || rep.edge_continuity.empty()) {
    ctx.Warn(head + " refused by the kernel (curve " + std::to_string(idx + 1) + " is the input curve nearest that edge, worst distance " + Sig3(lying) + "): " +
             (rep.reason.empty() ? std::string("no reason given") : rep.reason) + "; nothing was built");
    return;
  }
  ctx.Doc().BeginChange("NetworkSrf");
  ctx.Doc().Add(SceneObject::MakeSurface(net));
  std::string per;
  for (size_t i = 0; i < rep.curve_deviation.size(); ++i) per += (i ? ", " : "") + Sig3(rep.curve_deviation[i]);
  ctx.Print(head + ": input curve " + std::to_string(idx + 1) + " lies on the edge (worst distance " + Sig3(lying) + "); curve network of " + std::to_string(rep.u_curves) +
            " x " + std::to_string(rep.v_curves) + " curves fitted with the edge's " + ContinuityWhat(req.continuity) +
            " as a weighted constraint (" + std::to_string(rep.net_u) + " x " + std::to_string(rep.net_v) + " net, approximate least squares, untrimmed). " +
            ContinuityMeasuredNote(req, rep.edge_continuity[0]) + ". Distance of each input curve from the surface: " + per + " (worst " + Sig3(rep.max_deviation) +
            ", document tolerance " + FormatNumber(net_tol) + ")");
  NoHistoryNote(ctx, "NetworkSrf", req);
}

void PatchContinuity(CommandContext& ctx, const std::vector<ObjectId>& ids, const ContinuityRequest& req, const AdjacentEdge& edge) {
  Row point_objects;
  std::vector<kernel::NurbsCurve> curves;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Point) point_objects.push_back(o->point);
    else if (o->kind == ObjectKind::Curve && o->curve) curves.push_back(*o->curve);
  }
  const std::string head = std::string("Patch: ") + req.Name() + " continuity to " + edge.Describe();
  if (curves.empty()) {
    ctx.Warn(head + " refused: select at least one curve lying on the adjacent surface edge (the boundary that continues it); nothing was built");
    return;
  }
  const double patch_tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  double lying = 0.0;
  const int idx = CurveLyingOn(curves, edge.surface, lying);
  kernel::NurbsSurface::FitEdgeContinuity fe;
  fe.points = SampleCurve(curves[static_cast<size_t>(idx)], 33, curves[static_cast<size_t>(idx)].IsClosed());
  fe.adjacent = &edge.surface;
  fe.continuity = req.continuity;
  fe.angle_tolerance = req.angle_tol;
  fe.curvature_tolerance = req.curv_tol;
  Row boundary;
  for (size_t i = 0; i < curves.size(); ++i) {
    if (static_cast<int>(i) == idx) continue;
    const Row smp = SampleCurve(curves[i], 96, curves[i].IsClosed());
    boundary.insert(boundary.end(), smp.begin(), smp.end());
  }
  kernel::NurbsSurface patch;
  kernel::NurbsSurface::PatchFitReport rep;
  kernel::Result fr = kernel::Result::Failed;
  try {
    fr = kernel::NurbsSurface::FitPatch(boundary, point_objects, patch_tol, patch, &rep, 16, {fe});
  } catch (const std::exception& e) {
    rep.reason = e.what();
  }
  if (fr != kernel::Result::Ok || rep.edge_continuity.empty()) {
    ctx.Warn(head + " refused by the kernel (curve " + std::to_string(idx + 1) + " is the selected curve nearest that edge, worst distance " + Sig3(lying) + "): " +
             (rep.reason.empty() ? std::string("the kernel could not fit a surface to this data (fewer than three points, collinear data, or a singular solve)") : rep.reason) +
             "; nothing was built");
    return;
  }
  ctx.Doc().BeginChange("Patch");
  ctx.Doc().Add(SceneObject::MakeSurface(patch));
  ctx.Print(head + ": selected curve " + std::to_string(idx + 1) + " lies on the edge (worst distance " + Sig3(lying) + "); kernel least-squares NURBS surface through " +
            std::to_string(curves.size()) + " curve(s) and " + std::to_string(point_objects.size()) + " point(s) with the edge's " + ContinuityWhat(req.continuity) +
            " as a weighted constraint (" + std::to_string(rep.u_count) + " x " + std::to_string(rep.v_count) + " net, worst data deviation " + Sig3(rep.max_deviation) +
            " vs tolerance " + FormatNumber(patch_tol) + (rep.met_tolerance ? ": met" : ": NOT met") + "; untrimmed, spanning the data's extent). " +
            ContinuityMeasuredNote(req, rep.edge_continuity[0]));
  NoHistoryNote(ctx, "Patch", req);
}

// ---------------------------------------------------------------------------
// Pipe: circle swept along curves with RMF frames.
// ---------------------------------------------------------------------------

class PipeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to create pipe around"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    // Ids kept (not just CurvesOf's plain curve copies) so a pipe built
    // while History On is set can record its rail curve's id for
    // UpdateHistory (cmd_history.cpp's RebuildPipe dispatch) - see
    // RebuildPipe's own comment below for exactly what it reproduces.
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
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
    HistoryRecord rec;
    rec.command = "Pipe";
    rec.num = {{"radius", radius_}, {"cap", cap_ ? 1.0 : 0.0}};
    int made = 0, made_brep = 0, made_closed_brep = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      std::optional<SceneObject> built = RebuildPipe(ctx, *o->curve, rec);
      if (!built) continue;
      if (built->kind == ObjectKind::Brep) { ++made_brep; if (o->curve->IsClosed()) ++made_closed_brep; }
      // Tag with the rail curve (see PipeFeature, Document.h) regardless of
      // Cap/mesh-vs-surface/B-rep, as real geometry (a value copy, not a
      // reference to the source object, which may since have been
      // deleted) - so ExtractPipedCurve works either way. Copied before
      // Add() moves `built`.
      const kernel::NurbsCurve rail = *o->curve;
      const ObjectId new_id = ctx.Doc().Add(std::move(*built));
      ctx.Doc().SetPipeFeature(new_id, rail);
      RecordHistoryIfEnabled(ctx, new_id, "Pipe", {id}, rec.num);
      ++made;
    }
    // An open rail's capped pipe is now the kernel's own exact Brep::Pipe
    // (see RebuildPipe above); a closed rail, or an open rail whose
    // Brep::Pipe call threw on a degenerate station, still falls back to
    // the hand-rolled mesh - report honestly which one this batch actually
    // built rather than always claiming "mesh".
    std::string kind_note;
    if (cap_) kind_note = made == 0 ? "" : made_brep == made ? (made_closed_brep == made ? " (exact closed B-rep tube)" : made_closed_brep > 0 ? " (exact B-rep, closed rails as closed tubes)" : " (exact capped B-rep)") : made_brep == 0 ? " (capped mesh)" : " (mixed exact B-rep/mesh)";
    ctx.Print("Pipe: radius " + FormatNumber(radius_) + ", " + std::to_string(made) + " pipe(s)" + kind_note);
    Finish();
  }
  std::vector<ObjectId> ids_;
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

// A PLAIN CAPPED CYLINDER recognised from an ON_Brep: exactly one cylindrical
// wall face and two planar caps perpendicular to its axis (what the Cylinder
// command makes). `wall` / `bottom` / `top` are brep face indices.
struct PlainCylinder {
  ON_3dPoint c0;   // centre of the bottom cap
  ON_3dVector ax;  // unit axis, bottom -> top
  ON_3dVector u;   // unit direction perpendicular to ax
  double R = 0, H = 0;
  int wall = -1, bottom = -1, top = -1;
};

bool RecognizePlainCylinder(const ON_Brep& b, PlainCylinder& pc) {
  if (!b.IsSolid() || b.m_F.Count() != 3) return false;
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double tol = std::max(1e-6, bb.Diagonal().Length() * 1e-6);
  ON_Cylinder cyl;
  int caps[2] = {-1, -1}, ncaps = 0;
  ON_Plane cap_plane[2];
  pc.wall = -1;
  for (int f = 0; f < 3; ++f) {
    const ON_Surface* surf = b.m_F[f].SurfaceOf();
    if (!surf) return false;
    ON_Plane pl;
    if (pc.wall < 0 && surf->IsCylinder(&cyl, tol)) pc.wall = f;
    else if (ncaps < 2 && surf->IsPlanar(&pl, tol)) { caps[ncaps] = f; cap_plane[ncaps] = pl; ++ncaps; }
    else return false;
  }
  if (pc.wall < 0 || ncaps != 2) return false;
  ON_3dVector ax = cyl.Axis();
  if (!ax.Unitize()) return false;
  double h[2];
  for (int k = 0; k < 2; ++k) {
    if (std::fabs(cap_plane[k].zaxis * ax) < 1.0 - 1e-9) return false;
    h[k] = (cap_plane[k].origin - cyl.circle.plane.origin) * ax;
  }
  pc.bottom = h[0] <= h[1] ? caps[0] : caps[1];
  pc.top = h[0] <= h[1] ? caps[1] : caps[0];
  const double h0 = std::min(h[0], h[1]), h1 = std::max(h[0], h[1]);
  if (!(h1 - h0 > tol)) return false;
  pc.c0 = cyl.circle.plane.origin + h0 * ax;
  pc.ax = ax;
  pc.u = cyl.circle.plane.xaxis;
  pc.R = cyl.circle.radius;
  pc.H = h1 - h0;
  return true;
}

// Two closed shells in one Brep: `outer` as built and `inner` reversed (its normals
// point into the cavity) - the hollow solid between them. Both are the app's own
// primitives (ON_BrepSphere / ON_BrepTorus / ON_BrepCylinder), so the result
// tessellates exactly like those do. nullopt if the assembly is not a valid solid.
std::optional<kernel::Brep> HollowSolid(ON_Brep* outer, ON_Brep* inner) {
  if (!outer || !inner) { delete outer; delete inner; return std::nullopt; }
  inner->Flip();
  ON_Brep all;
  all.Append(*outer);
  all.Append(*inner);
  delete outer;
  delete inner;
  kernel::Brep k;
  k.raw() = all;
  if (!k.raw().IsSolid() || !k.raw().IsValid()) return std::nullopt;
  return k;
}

// Exact B-rep offset of a closed solid by `d` (positive grows, negative
// shrinks) - every kernel construction that exists, tried before OffsetSrf's
// mesh offset:
//   Solid=No
//     * a solid whose faces are ALL planar and whose shape is convex:
//       kernel::OffsetSolidConvexPlanar (every plane moved by d, re-clipped);
//     * a WHOLE sphere (radius r -> r + d) or WHOLE torus (minor radius ->
//       minor + d): the exact offset of an analytic primitive is the same
//       primitive with the radius moved, built by kernel::Brep::Sphere/Torus;
//     * a plain capped right circular CYLINDER (wall + two flat caps):
//       radius r + d, each cap moved d outward (ON_BrepCylinder);
//   Solid=Yes (the hollow wall between the original and the offset)
//     * convex planar: kernel::ShellConvexPlanar of the OUTER body with
//       thickness |d| (outer = offset solid for d > 0, the original for d < 0);
//     * whole sphere / torus: kernel::ShellClosedSphere / ShellClosedTorus;
//     * a plain capped cylinder: the offset and original cylinders as two shells (HollowSolid).
// nullopt (with the kernel's reason in `why`) for everything else - the caller
// then keeps the mesh offset and prints the reason.
std::optional<kernel::Brep> TryExactOffsetSolid(const kernel::Brep& kb, double d, bool solid, std::string& why) {
  const ON_Brep& b = kb.raw();
  if (!b.IsSolid()) { why = "the polysurface is not a closed solid"; return std::nullopt; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = std::max(1e-9, bb.Diagonal().Length());
  const double tol = diag * 1e-6;
  try {
    if (b.m_F.Count() == 1) {
      const ON_Surface* surf = b.m_F[0].SurfaceOf();
      ON_Sphere sph;
      ON_Torus tor;
      if (surf && surf->IsSphere(&sph, tol)) {
        const double r = sph.Radius();
        const ON_3dPoint c = sph.Center();
        if (!solid) {
          if (!(r + d > 0)) { why = "the sphere would collapse (offset distance <= -radius)"; return std::nullopt; }
          return WrapBrep(ON_BrepSphere(ON_Sphere(c, r + d)));
        }
        if (!(r + std::min(d, 0.0) > 0)) { why = "the sphere would collapse"; return std::nullopt; }
        if (std::optional<kernel::Brep> h = d > 0 ? HollowSolid(ON_BrepSphere(ON_Sphere(c, r + d)), ON_BrepSphere(ON_Sphere(c, r)))
                                                  : HollowSolid(ON_BrepSphere(ON_Sphere(c, r)), ON_BrepSphere(ON_Sphere(c, r + d)))) return h;
        why = "the hollow sphere did not assemble into a valid solid";
        return std::nullopt;
      }
      if (surf && surf->IsTorus(&tor, tol)) {
        const double mn = tor.minor_radius, mj = tor.major_radius;
        auto tb = [&](double minor) { return ON_BrepTorus(ON_Torus(tor.plane, mj, minor)); };
        if (!solid) {
          if (!(mn + d > 0) || !(mn + d < mj)) { why = "the torus tube would collapse or self-intersect at that offset"; return std::nullopt; }
          return WrapBrep(tb(mn + d));
        }
        if (!(mn + d > 0) || !(mn + d < mj) || !(mn - std::fabs(d) > 0)) { why = "the torus tube would collapse or self-intersect at that offset"; return std::nullopt; }
        if (std::optional<kernel::Brep> h = d > 0 ? HollowSolid(tb(mn + d), tb(mn)) : HollowSolid(tb(mn), tb(mn + d))) return h;
        why = "the hollow torus did not assemble into a valid solid";
        return std::nullopt;
      }
    }
    {
      PlainRevolved pr;
      if (RecognizeConeLike(b, pr)) {
        // A plain cone / frustum: offset the (radius, height) profile with sharp corners and revolve it.
        std::optional<std::vector<std::pair<double, double>>> off = OffsetProfile(pr.prof, std::vector<double>(pr.prof.size() - 1, d));
        if (!off) { why = "the cone would collapse or self-intersect at that offset"; return std::nullopt; }
        if (!solid) {
          if (std::optional<kernel::Brep> k = RevolveOutward(pr, *off)) return k;
          why = "the revolved offset cone did not join into a valid closed solid";
          return std::nullopt;
        }
        const std::vector<std::pair<double, double>>& outer = d > 0 ? *off : pr.prof;
        const std::vector<std::pair<double, double>>& inner = d > 0 ? pr.prof : *off;
        std::optional<kernel::Brep> ko = RevolveOutward(pr, outer), ki = RevolveOutward(pr, inner);
        if (ko && ki) if (std::optional<kernel::Brep> h = HollowSolid(new ON_Brep(ko->raw()), new ON_Brep(ki->raw()))) return h;
        why = "the hollow cone did not assemble into a valid solid";
        return std::nullopt;
      }
    }
    // A right prism (extruded L / U, a box with parallel edges rounded by FilletEdge): the exact
    // analytic offset of its profile (prism_solid.h). Tried for the three-face wall prisms and,
    // after the convex planar construction declines, for every other shape.
    auto try_prism = [&](std::string& reason) -> std::optional<kernel::Brep> {
      // A prism with one round through-hole (box minus cylinder): read from the exact faces.
      {
        prism::HolePrism hp;
        bool not_hole = true;
        std::string hw;
        if (prism::RecognizeHole(b, hp, hw, not_hole)) {
          std::string w;
          std::optional<kernel::Brep> o = prism::OffsetHoleSolid(hp, d, w);
          if (!o) { reason = w; return std::nullopt; }
          if (!solid) return o;
          std::optional<kernel::Brep> orig = prism::OffsetHoleSolid(hp, 0.0, w);
          if (!orig) { reason = w; return std::nullopt; }
          std::optional<kernel::Brep> h = d > 0 ? HollowSolid(new ON_Brep(o->raw()), new ON_Brep(orig->raw()))
                                                : HollowSolid(new ON_Brep(orig->raw()), new ON_Brep(o->raw()));
          if (!h) reason = "the hollow prism with its hole did not assemble into a valid solid";
          return h;
        }
        if (!not_hole) { reason = hw; return std::nullopt; }
      }
      prism::Prism pr;
      bool not_prism = true;
      std::string r;
      if (!prism::Recognize(b, pr, r, not_prism)) { reason = r; return std::nullopt; }
      std::string w;
      if (!solid) {
        std::optional<kernel::Brep> k = prism::OffsetSolid(pr, d, w);
        if (!k) reason = w;
        return k;
      }
      std::optional<kernel::Brep> o = prism::OffsetSolid(pr, d, w);
      if (!o) { reason = w; return std::nullopt; }
      std::optional<kernel::Brep> h = d > 0 ? HollowSolid(new ON_Brep(o->raw()), new ON_Brep(b)) : HollowSolid(new ON_Brep(b), new ON_Brep(o->raw()));
      if (!h) reason = "the hollow prism did not assemble into a valid solid";
      return h;
    };
    if (b.m_F.Count() == 3) {
      PlainCylinder pc;
      if (!RecognizePlainCylinder(b, pc)) {
        if (std::optional<kernel::Brep> k = try_prism(why)) return k;
        if (why.empty() || why.rfind("not a right prism", 0) == 0)
          why = "no exact offset for this solid (needs all-planar convex faces, a whole sphere/torus, a plain capped cylinder, or a right prism)";
        return std::nullopt;
      }
      const double r2 = pc.R + d;
      if (!(r2 > 0) || !(pc.H + 2 * d > 0)) { why = "the cylinder would collapse at that offset"; return std::nullopt; }
      auto cylb = [&](double r, double h, double shift) {
        return ON_BrepCylinder(ON_Cylinder(ON_Circle(ON_Plane(pc.c0 - shift * pc.ax, pc.ax), r), h), true, true);
      };
      if (!solid) return WrapBrep(cylb(r2, pc.H + 2 * d, d));
      // Solid=Yes: the hollow wall between the original and the offset cylinder - two closed
      // shells in one Brep (outer as built, inner reversed), the same assembly as the sphere/torus.
      if (std::optional<kernel::Brep> h = d > 0 ? HollowSolid(cylb(r2, pc.H + 2 * d, d), cylb(pc.R, pc.H, 0.0))
                                                : HollowSolid(cylb(pc.R, pc.H, 0.0), cylb(r2, pc.H + 2 * d, d))) return h;
      why = "the hollow cylinder did not assemble into a valid solid";
      return std::nullopt;
    }
    // All-planar convex solid.
    try {
      if (!solid) return kernel::OffsetSolidConvexPlanar(kb, d);
      if (d > 0) return kernel::ShellConvexPlanar(kernel::OffsetSolidConvexPlanar(kb, d), {}, d);
      return kernel::ShellConvexPlanar(kb, {}, -d);
    } catch (const std::exception& ex) {
      // Not convex-planar: a right prism (L extrusion, rounded box) is still exact.
      std::string pw;
      if (std::optional<kernel::Brep> k = try_prism(pw)) return k;
      why = pw.empty() || pw.rfind("not a right prism", 0) == 0 ? std::string(ex.what()) : pw;
      return std::nullopt;
    }
  } catch (const std::exception& ex) {
    why = ex.what();
    return std::nullopt;
  }
}

class OffsetSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select surfaces or polysurfaces to offset"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind != ObjectKind::Curve && o->kind != ObjectKind::Point) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select surfaces, polysurfaces or meshes"); Finish(); return; }
    options = {{"Distance", FormatNumber(distance_), {}, true, false}, {"Solid", "No", {"Yes", "No"}, false, true}, {"FlipAll", "No", {"Yes", "No"}, false, true},
               {"Exact", exact_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
    WantNumber("Offset distance", distance_);
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Distance") { double d; if (ParseNumber(v, d)) { distance_ = d; options[0].value = FormatNumber(d); default_number = d; } else ctx.Warn("Invalid distance"); }
    if (n == "Solid") { solid_ = (v == "Yes"); options[1].value = solid_ ? "Yes" : "No"; }
    if (n == "FlipAll") { flip_ = (v == "Yes"); options[2].value = flip_ ? "Yes" : "No"; }
    if (n == "Exact") { exact_ = (v == "Yes"); options[3].value = exact_ ? "Yes" : "No"; }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { double v; if (ParseNumber(t, v)) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double d) override {
    if (d == 0) { ctx.Warn("Distance must be non-zero"); return; }
    if (flip_) d = -d;
    ctx.Doc().BeginChange("OffsetSrf");
    int made = 0, exact_made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const int layer = o->layer_index;
      // The per-object construction lives in RebuildOffsetSrf (below the
      // anonymous namespace) so UpdateHistory re-runs exactly what this does.
      HistoryRecord rec;
      rec.command = "OffsetSrf";
      rec.num = {{"d", d}, {"solid", solid_ ? 1.0 : 0.0}, {"exact", exact_ ? 1.0 : 0.0}};
      std::optional<SceneObject> built = RebuildOffsetSrf(ctx, *o, rec);
      if (!built) continue;
      if (built->kind == ObjectKind::Brep && o->kind != ObjectKind::Surface) ++exact_made;
      built->layer_index = layer;
      const ObjectId new_id = ctx.Doc().Add(std::move(*built));
      RecordHistoryIfEnabled(ctx, new_id, "OffsetSrf", {id}, rec.num);
      ++made;
    }
    ctx.Print("OffsetSrf: distance " + FormatNumber(d) + ", " + std::to_string(made) + " object(s)" + (solid_ ? ", solid" : "") +
               (exact_made ? ", " + std::to_string(exact_made) + " exact B-rep" : std::string()));
    Finish();
  }
  std::vector<ObjectId> ids_;
  double distance_ = 1;
  bool solid_ = false, flip_ = false, exact_ = true;
};

// ---------------------------------------------------------------------------
// Shell face picking: mirrors cmd_fillet.cpp's FacePick/PickFace pattern
// (nearest brep face to a clicked point, via MeshBrepFaces) but restricted
// to a caller-supplied set of object ids (only the solids Shell already has
// selected are eligible), so it is its own small copy rather than a shared
// export - the same "kept local" choice cmd_fillet.cpp's own comment makes
// for cmd_srfedit.cpp's copy.
// ---------------------------------------------------------------------------

struct ShellFacePick { ObjectId id = kNoObject; int face = -1; double dist = 0; };

std::optional<ShellFacePick> PickShellFace(CommandContext& ctx, Point3d p, const std::vector<ObjectId>& allowed) {
  std::optional<ShellFacePick> best;
  for (ObjectId id : allowed) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) continue;
    BrepMeshOptions opt;
    opt.chord_tolerance = 0.05;
    std::vector<kernel::Mesh> faces = MeshBrepFaces(o->brep->raw(), opt);
    for (size_t f = 0; f < faces.size(); ++f) {
      if (faces[f].FaceCount() == 0) continue;
      const double d = faces[f].ClosestPoint(p).DistanceTo(p);
      if (!best || d < best->dist) best = ShellFacePick{id, static_cast<int>(f), d};
    }
  }
  return best;
}

// True if every edge of `m` is used by exactly one triangle in each of at
// most two opposite windings (a normal manifold interior edge: once each
// way) or by exactly one triangle total (a boundary edge) - the same
// "well-behaved manifold-with-boundary" requirement Mesh::ExtrudeCappedSolid
// enforces on its own cap argument, checked here directly on the *directed*
// edge multiset so a non-manifold edge (3+ faces) or an inconsistently
// wound pair (the same directed edge appearing twice) is caught rather than
// silently handed to OffsetMesh/ShellBetween, which both assume it.
bool IsSimpleManifoldWithBoundary(const kernel::Mesh& m) {
  const ON_Mesh& r = m.raw();
  if (r.FaceCount() == 0) return false;
  std::map<std::pair<int, int>, int> directed;
  for (int f = 0; f < r.FaceCount(); ++f) {
    const ON_MeshFace& face = r.m_F[f];
    const int k = face.IsTriangle() ? 3 : 4;
    for (int e = 0; e < k; ++e) {
      const int a = face.vi[e], b = face.vi[(e + 1) % k];
      if (a == b) return false;
      if (++directed[{a, b}] > 1) return false;
    }
  }
  for (const auto& [edge, count] : directed) {
    const auto rev = directed.find({edge.second, edge.first});
    const int rev_count = rev == directed.end() ? 0 : rev->second;
    if (count + rev_count > 2) return false;
  }
  return true;
}

// Builds a real *open* shell for the solid `brep`: the picked faces
// (`remove`, brep face indices as returned by PickShellFace/NearestFace)
// are dropped entirely rather than getting an inward offset, so the
// remaining outer surface keeps a real boundary loop where they used to be
// - the wall thickness only applies to what is left. `ShellBetween` (used
// elsewhere in this file for OffsetSrf's solid option) already builds
// exactly "flip one open mesh inward, keep the other outward, wall the gap
// between them along the shared boundary loop", so it is reused verbatim
// rather than writing a second rim-stitcher: inner_open (flipped inward)
// is `bottom`, outer_open (unchanged) is `top`.
//
// Real scope: this only accepts a face selection whose remainder is a
// single well-behaved manifold-with-boundary patch (IsSimpleManifoldWithBoundary
// below) - true for one face, or several mutually-adjacent faces, removed
// from a simple solid (a box, a box-like polysurface); it explicitly
// rejects (rather than silently mis-triangulating) a selection that would
// leave a non-manifold or self-overlapping remainder. Highly curved or
// non-planar picked faces are accepted the same way any face is (the
// removal is purely topological - drop that face's triangles - so no
// planarity is actually required), but a mesh-level rim across a very
// non-planar boundary loop is still a straight-line lip between
// corresponding outer/inner vertices, not a fitted surface - fine for the
// common box/cup/case case this targets, visibly facetted on a sharply
// curved opening edge.
// `remove` empty: builds the ordinary fully-closed shell (outer minus its
// inward offset, combined with a real mesh boolean - same shape of result
// the old MeshOf()+OffsetMesh()+BooleanCombine() closed-shell path always
// produced, just able to source per-face thickness). `remove` non-empty:
// builds the open shell described below.
//
// `thickness_by_face` maps a brep face index (as returned by
// PickShellFace/NearestFace) to its own wall thickness; any kept face not
// present in it uses `default_thickness`. It is only actually honoured
// (`out_overrides_applied` set true) when every kept face is planar
// (AllFacesPlanar) - the case OffsetMeshPerFace's per-vertex plane solve is
// an exact miter for, and the only case this has been numerically verified
// for (see ShellCommand's own per-face test). When a kept face is curved,
// `thickness_by_face` is ignored entirely and every face gets
// `default_thickness` - the same, already-correct uniform behaviour Shell
// always had - rather than applying an unverified per-triangle distance to
// a curved surface.
std::optional<kernel::Mesh> BuildShell(const ON_Brep& brep, const std::vector<int>& remove,
                                        const std::map<int, double>& thickness_by_face, double default_thickness,
                                        bool& out_overrides_applied, std::string& err) {
  out_overrides_applied = false;
  ON_BoundingBox bb;
  brep.GetBoundingBox(bb);
  const double diag = bb.Diagonal().Length();
  BrepMeshOptions opt;
  opt.chord_tolerance = std::isfinite(diag) && diag > 0 ? std::clamp(diag * 5e-4, 1e-5, 0.05) : 0.005;
  std::vector<kernel::Mesh> faces = MeshBrepFaces(brep, opt);
  if (faces.empty()) { err = "could not be meshed"; return std::nullopt; }
  for (int fi : remove) {
    if (fi < 0 || static_cast<size_t>(fi) >= faces.size() || faces[static_cast<size_t>(fi)].FaceCount() == 0) {
      err = "has an invalid face selection";
      return std::nullopt;
    }
  }
  const double size = std::max(1e-9, diag);
  const double weld = std::max(1e-12, size * 1e-8);
  const kernel::Mesh full = kernel::Mesh::MergeAndWeld(faces, weld);
  if (!full.IsClosedManifold()) { err = "is not a closed solid"; return std::nullopt; }
  const bool flip = full.Volume() < 0;
  std::set<int> remove_set(remove.begin(), remove.end());
  std::vector<kernel::Mesh> kept;
  std::vector<int> region;  // per outer-mesh-face original brep face index, aligned to `kept`'s concatenation order
  for (size_t i = 0; i < faces.size(); ++i) {
    if (!remove_set.count(static_cast<int>(i)) && faces[i].FaceCount() > 0) {
      kept.push_back(faces[i]);
      region.insert(region.end(), static_cast<size_t>(faces[i].raw().FaceCount()), static_cast<int>(i));
    }
  }
  if (kept.empty()) { err = "would have no surface left after removing the selected face(s)"; return std::nullopt; }
  kernel::Mesh outer = kernel::Mesh::MergeAndWeld(kept, weld);
  if (flip) outer = outer.FlipNormals();
  const bool is_open = !remove.empty();
  if (is_open && !IsSimpleManifoldWithBoundary(outer)) {
    err = "removing that face selection leaves a non-manifold or multi-piece remainder (not supported - try removing a single face, or a group of mutually-adjacent faces, from a simple box-like solid)";
    return std::nullopt;
  }
  std::map<int, double> effective;
  if (!thickness_by_face.empty() && AllFacesPlanar(kept, 0.999999)) {
    effective = thickness_by_face;
    out_overrides_applied = true;
  }
  const auto d_for = [&](int fid) {
    const auto it = effective.find(fid);
    return -(it != effective.end() ? it->second : default_thickness);
  };
  const kernel::Mesh inner = OffsetMeshPerFace(outer, region, d_for);
  if (is_open) {
    const kernel::Mesh shell = ShellBetween(inner, outer);
    if (shell.FaceCount() == 0) { err = "produced no geometry (thickness may be too large for the remaining wall)"; return std::nullopt; }
    return shell;
  }
  try {
    const kernel::Mesh shell = kernel::BooleanCombine(outer, inner, kernel::BooleanOp::Difference);
    if (shell.FaceCount() == 0) { err = "is thinner than the thickness; skipped"; return std::nullopt; }
    return shell;
  } catch (const std::exception& ex) {
    err = std::string("rejected by the mesh kernel (") + ex.what() + ")";
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// Exact (B-rep) shell: every kernel shell construction that exists, wired in
// ahead of BuildShell's mesh construction. Returns a real, untessellated
// Brep for
//   * a solid whose faces are ALL planar and whose shape is convex (a box or
//     other simple polyhedron): kernel::ShellConvexPlanar - with or without
//     removed (opened) faces and per-face thickness overrides, the two cases
//     the earlier "plain closed shell" wiring did not reach;
//   * a WHOLE sphere or WHOLE torus (a single-face solid): kernel::
//     ShellClosedSphere / ShellClosedTorus (concentric inner copy);
// and returns nullopt, with `why` set to the kernel's own reason, for
// everything else (a curved face among several - cylinder, cone, filleted or
// Boolean-built solids -, a non-convex shape, a thickness beyond the shape's
// offset feasibility): the caller then keeps the mesh construction it always
// had and prints `why`, so a mesh result is never silent.
// `removed` / `overrides` are brep face indices as returned by PickShellFace.
// ---------------------------------------------------------------------------
std::optional<kernel::Brep> TryExactShell(const SceneObject& o, const std::vector<int>& removed,
                                           const std::map<int, double>& overrides, double t, std::string& why) {
  if (o.kind != ObjectKind::Brep || !o.brep) { why = "the object is not a polysurface"; return std::nullopt; }
  const ON_Brep& b = o.brep->raw();
  if (!b.IsSolid()) { why = "the polysurface is not a closed solid"; return std::nullopt; }
  ON_BoundingBox bb;
  b.GetBoundingBox(bb);
  const double diag = std::max(1e-9, bb.Diagonal().Length());
  // A whole sphere / torus: one face, nothing to open or override.
  if (b.m_F.Count() == 1 && removed.empty() && overrides.empty()) {
    const ON_Surface* surf = b.m_F[0].SurfaceOf();
    ON_Sphere sph;
    ON_Torus tor;
    try {
      if (surf && surf->IsSphere(&sph, diag * 1e-6)) {
        if (!(t < sph.Radius())) { why = "the wall thickness is not smaller than the sphere radius"; return std::nullopt; }
        if (std::optional<kernel::Brep> h = HollowSolid(ON_BrepSphere(ON_Sphere(sph.Center(), sph.Radius())), ON_BrepSphere(ON_Sphere(sph.Center(), sph.Radius() - t)))) return h;
        return kernel::ShellClosedSphere(sph.Center(), sph.Radius(), t);
      }
      if (surf && surf->IsTorus(&tor, diag * 1e-6)) {
        if (!(t < tor.minor_radius)) { why = "the wall thickness is not smaller than the tube radius"; return std::nullopt; }
        if (std::optional<kernel::Brep> h = HollowSolid(ON_BrepTorus(ON_Torus(tor.plane, tor.major_radius, tor.minor_radius)),
                                                         ON_BrepTorus(ON_Torus(tor.plane, tor.major_radius, tor.minor_radius - t)))) return h;
        return kernel::ShellClosedTorus(tor.plane, tor.major_radius, tor.minor_radius, t);
      }
    } catch (const std::exception& ex) {
      why = ex.what();
      return std::nullopt;
    }
  }
  // A plain capped cylinder: a closed hollow (outer cylinder + a reversed inner
  // cylinder, two shells of one Brep) or, with one CAP removed, an open cup
  // (outer wall + base disc + inner wall + inner floor + flat rim annulus), every
  // face an exact revolved/cylindrical surface (RevolveProfileBrep).
  // A plain capped cone / frustum: the inner wall is the profile offset inward by t. Closed
  // hollow = outer + reversed inner (two shells of one Brep); with one flat CAP removed, an
  // open cup revolved from outer profile + rim + inner profile. All faces exact revolved surfaces.
  {
    PlainRevolved pr;
    if (RecognizeConeLike(b, pr)) {
      if (!overrides.empty()) { why = "a per-face thickness on a cone has no exact construction"; return std::nullopt; }
      const std::vector<std::pair<double, double>>& P = pr.prof;
      const size_t n = P.size();
      auto is_cap = [&](size_t e) { return std::fabs(P[e + 1].second - P[e].second) < 1e-9; };
      try {
        if (removed.empty()) {
          std::optional<std::vector<std::pair<double, double>>> in = OffsetProfile(P, std::vector<double>(n - 1, -t));
          if (!in) { why = "the wall thickness is too large for this cone"; return std::nullopt; }
          std::optional<kernel::Brep> ko = RevolveOutward(pr, P), ki = RevolveOutward(pr, *in);
          if (ko && ki) if (std::optional<kernel::Brep> h = HollowSolid(new ON_Brep(ko->raw()), new ON_Brep(ki->raw()))) return h;
          why = "the hollow cone did not assemble into a valid solid";
          return std::nullopt;
        }
        int e = -1;
        if (removed.size() == 1) for (size_t k = 0; k + 1 < n; ++k) if (pr.face[k] == removed[0] && is_cap(k)) e = static_cast<int>(k);
        if (e < 0 || (e != 0 && static_cast<size_t>(e) != n - 2)) { why = "only one flat cap of a cone can be removed exactly (not its wall, not several faces)"; return std::nullopt; }
        std::vector<double> dk(n - 1, -t);
        dk[static_cast<size_t>(e)] = 0;
        std::optional<std::vector<std::pair<double, double>>> in = OffsetProfile(P, dk);
        if (!in) { why = "the wall thickness is too large for this cone"; return std::nullopt; }
        std::vector<std::pair<double, double>> prof;
        if (e == 0) {
          for (size_t k = n; k-- > 1;) prof.push_back(P[k]);
          for (size_t k = 1; k < n; ++k) prof.push_back((*in)[k]);
        } else {
          for (size_t k = 0; k + 1 < n; ++k) prof.push_back(P[k]);
          for (size_t k = n - 1; k-- > 0;) prof.push_back((*in)[k]);
        }
        if (std::optional<kernel::Brep> cup = RevolveOutward(pr, prof)) return cup;
        why = "the revolved cone cup did not join into a valid closed solid";
        return std::nullopt;
      } catch (const std::exception& ex) {
        why = ex.what();
        return std::nullopt;
      }
    }
  }
  PlainCylinder pc;
  if (b.m_F.Count() == 3 && RecognizePlainCylinder(b, pc)) {
    if (!overrides.empty()) { why = "a per-face thickness on a cylinder has no exact construction"; return std::nullopt; }
    if (!(t < pc.R)) { why = "the wall thickness is not smaller than the cylinder radius"; return std::nullopt; }
    try {
      if (removed.empty()) {
        if (!(2 * t < pc.H)) { why = "the wall thickness consumes the whole height"; return std::nullopt; }
        ON_Brep all;
        ON_Brep* outer = ON_BrepCylinder(ON_Cylinder(ON_Circle(ON_Plane(pc.c0, pc.ax), pc.R), pc.H), true, true);
        ON_Brep* inner = ON_BrepCylinder(ON_Cylinder(ON_Circle(ON_Plane(pc.c0 + t * pc.ax, pc.ax), pc.R - t), pc.H - 2 * t), true, true);
        if (!outer || !inner) { delete outer; delete inner; why = "could not build the cylinders"; return std::nullopt; }
        inner->Flip();
        all.Append(*outer);
        all.Append(*inner);
        delete outer;
        delete inner;
        kernel::Brep k;
        k.raw() = all;
        if (!k.raw().IsSolid() || !k.raw().IsValid()) { why = "the hollow cylinder did not assemble into a valid solid"; return std::nullopt; }
        return k;
      }
      if (removed.size() == 1 && (removed[0] == pc.top || removed[0] == pc.bottom)) {
        if (!(t < pc.H)) { why = "the wall thickness consumes the whole height"; return std::nullopt; }
        const bool open_top = removed[0] == pc.top;
        const double R = pc.R, H = pc.H;
        const std::vector<std::pair<double, double>> prof =
            open_top ? std::vector<std::pair<double, double>>{{0, 0}, {R, 0}, {R, H}, {R - t, H}, {R - t, t}, {0, t}}
                     : std::vector<std::pair<double, double>>{{0, H}, {R, H}, {R, 0}, {R - t, 0}, {R - t, H - t}, {0, H - t}};
        if (std::optional<kernel::Brep> cup = RevolveProfileBrep(pc.c0, pc.ax, pc.u, prof)) return cup;
        why = "the revolved cup pieces did not join into a valid closed solid";
        return std::nullopt;
      }
      why = "only one cap of a cylinder can be removed exactly (not its wall, not several faces)";
      return std::nullopt;
    } catch (const std::exception& ex) {
      why = ex.what();
      return std::nullopt;
    }
  }
  try {
    const std::vector<kernel::Brep::PlanarFace> pf = o.brep->PlanarFaces();  // throws on any curved face
    // PlanarFaces()/ShellConvexPlanar index faces in the brep's own face
    // order; verify that against the picked indices (each face's bounding-box
    // centre must lie on the matching plane) instead of assuming it.
    if (static_cast<int>(pf.size()) != b.m_F.Count()) { why = "planar face list does not match the polysurface's faces"; return std::nullopt; }
    for (size_t i = 0; i < pf.size(); ++i) {
      ON_BoundingBox fb;
      b.m_F[static_cast<int>(i)].GetBoundingBox(fb);
      if (std::fabs(pf[i].plane.DistanceTo(fb.Center())) > diag * 1e-6) { why = "planar face order does not match the polysurface's face order"; return std::nullopt; }
    }
    std::vector<double> thick(pf.size(), t);
    for (const auto& [fi, ft] : overrides) {
      if (fi < 0 || static_cast<size_t>(fi) >= thick.size()) { why = "invalid face index"; return std::nullopt; }
      thick[static_cast<size_t>(fi)] = ft;
    }
    return kernel::ShellConvexPlanar(*o.brep, removed, thick);
  } catch (const std::exception& ex) {
    // Not a convex planar solid. A closed hollow shell of a right prism (extruded L / U, a box with
    // parallel edges rounded by FilletEdge) is still exact: the inner cavity is the same prism with
    // its profile offset inward by t and both caps moved in by t (prism_solid.h).
    if (removed.empty() && overrides.empty()) {
      {
        // A prism with one round through-hole (box minus cylinder): the cavity is the same prism with the profile
        // inset by t and the hole widened by t, read from the exact faces (prism_solid.h).
        prism::HolePrism hp;
        bool not_hole = true;
        std::string hw;
        if (prism::RecognizeHole(b, hp, hw, not_hole)) {
          std::optional<kernel::Brep> outer = prism::OffsetHoleSolid(hp, 0.0, hw);
          std::optional<kernel::Brep> inner = outer ? prism::OffsetHoleSolid(hp, -t, hw) : std::nullopt;
          if (outer && inner) {
            if (std::optional<kernel::Brep> h = HollowSolid(new ON_Brep(outer->raw()), new ON_Brep(inner->raw()))) return h;
            hw = "the hollow prism with its hole did not assemble into a valid solid";
          }
          why = hw;
          return std::nullopt;
        }
        if (!not_hole) { why = hw; return std::nullopt; }
      }
      prism::Prism pr;
      bool not_prism = true;
      std::string pw;
      if (prism::Recognize(b, pr, pw, not_prism)) {
        std::optional<kernel::Brep> inner = prism::OffsetSolid(pr, -t, pw);
        if (inner) {
          if (std::optional<kernel::Brep> h = HollowSolid(new ON_Brep(b), new ON_Brep(inner->raw()))) return h;
          pw = "the hollow prism did not assemble into a valid solid";
        }
        why = pw;
        return std::nullopt;
      }
      if (!not_prism) { why = pw; return std::nullopt; }
    }
    why = ex.what();
    return std::nullopt;
  }
}

class ShellCommand : public Command {
 public:
  // Picking: the original single face-picking prompt, now doing double
  // duty - a click marks a face to remove/open (Mode=Remove, the default,
  // unchanged from before) or to get its own wall thickness instead of the
  // shared default (Mode=Thickness), and Enter always moves straight on to
  // the closing Thickness prompt exactly as it always did (so an old
  // script's literal "Shell / Enter / 1" - no Mode, no extra picks - runs
  // through the identical two calls it always has: OnEnter then
  // OnNumber(1) at Phase::Thickness). A face picked in Mode=Thickness
  // briefly switches `want` to Number to capture its value
  // (OverrideValue), then returns to Picking on its own - no extra Enter
  // is ever required for that, so it cannot get in the way of the plain
  // "click remove faces, Enter" flow either. This is the "adapt
  // pragmatically" alternative to a wholly separate pick-then-type
  // sub-step: the original code's own single WantPoint moment already had
  // exactly one thing an Enter from it could mean (finish picking), and
  // preserving that meaning for old scripts mattered more than a second
  // dedicated phase would have gained.
  enum class Phase { Picking, OverrideValue, Thickness };

  void Begin(CommandContext&) override { WantObjects("Select closed solids to shell"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    bool any_brep = false;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (o && o->kind == ObjectKind::Brep && o->brep) any_brep = true;
    }
    if (any_brep) {
      DeselectAll(ctx, ids_);
      accept_preselection = false;
      phase_ = Phase::Picking;
      options = {{"Mode", "Remove", {"Remove", "Thickness"}, false, false}, {"Exact", exact_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
      WantPoint("Click face(s) to remove/open, or set Mode=Thickness first to give clicked face(s) their own wall thickness (Enter for the default Thickness)");
    } else {
      StartThickness();
    }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (phase_ != Phase::Picking) return;
    std::optional<ShellFacePick> pick = PickShellFace(ctx, p, ids_);
    if (!pick) { ctx.Warn("No face of the selected solid(s) near that point"); return; }
    if (!override_mode_) {
      std::vector<int>& picked = open_faces_[pick->id];
      if (std::find(picked.begin(), picked.end(), pick->face) != picked.end()) {
        ctx.Warn("That face is already marked to be removed");
        return;
      }
      picked.push_back(pick->face);
      ctx.Print("Shell: face " + std::to_string(pick->face) + " on object " + std::to_string(pick->id) + " marked to open (" +
                 std::to_string(picked.size()) + " on this solid); click more faces or press Enter");
      return;
    }
    const auto rem = open_faces_.find(pick->id);
    if (rem != open_faces_.end() && std::find(rem->second.begin(), rem->second.end(), pick->face) != rem->second.end()) {
      ctx.Warn("That face is marked to be removed and has no wall of its own to thicken");
      return;
    }
    pending_override_ = pick;
    const auto& existing = face_thickness_[pick->id];
    const auto it = existing.find(pick->face);
    const double def = it != existing.end() ? it->second : thickness_;
    phase_ = Phase::OverrideValue;
    WantNumber("Thickness for face " + std::to_string(pick->face) + " on object " + std::to_string(pick->id), def);
  }
  void OnEnter(CommandContext&) override { if (phase_ == Phase::Picking) StartThickness(); }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Thickness") { double d; if (ParseNumber(v, d) && d > 0) { thickness_ = d; options[0].value = FormatNumber(d); default_number = d; } else ctx.Warn("Thickness must be positive"); }
    if (n == "Mode") { override_mode_ = (v == "Thickness"); options[0].value = v; }
    if (n == "Exact") { exact_ = (v == "Yes"); for (auto& op : options) if (op.name == "Exact") op.value = v; }
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (phase_ != Phase::Thickness && phase_ != Phase::OverrideValue) return;
    double v;
    if (ParseNumber(t, v)) OnNumber(ctx, v);
  }
  void OnNumber(CommandContext& ctx, double t) override {
    if (phase_ == Phase::OverrideValue) {
      if (t <= 0) { ctx.Warn("Thickness must be positive"); return; }
      face_thickness_[pending_override_->id][pending_override_->face] = t;
      ctx.Print("Shell: face " + std::to_string(pending_override_->face) + " on object " + std::to_string(pending_override_->id) +
                 " set to thickness " + FormatNumber(t) + "; click more faces or press Enter for the default Thickness");
      pending_override_.reset();
      phase_ = Phase::Picking;
      WantPoint("Click face(s) to remove/open, or set Mode=Thickness first to give clicked face(s) their own wall thickness (Enter for the default Thickness)");
      return;
    }
    if (phase_ != Phase::Thickness) return;
    if (t <= 0) { ctx.Warn("Thickness must be positive"); return; }
    std::vector<std::pair<ObjectId, kernel::Mesh>> results;
    std::vector<std::pair<ObjectId, kernel::Brep>> exact_results;
    std::vector<ObjectId> opened;
    std::vector<ObjectId> per_face;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const auto rem_it = open_faces_.find(id);
      const bool has_remove = rem_it != open_faces_.end() && !rem_it->second.empty();
      const auto fth_it = face_thickness_.find(id);
      const bool has_overrides = fth_it != face_thickness_.end() && !fth_it->second.empty();
      // The plain case - a closed solid, one uniform thickness, no opened or
      // individually-thickened faces - lives in RebuildShell (below the
      // anonymous namespace) so UpdateHistory re-runs exactly what this does,
      // and is the only Shell that records construction history (face indices
      // are not a stable thing to re-run against an edited solid).
      if (!has_remove && !has_overrides) {
        HistoryRecord rec;
        rec.command = "Shell";
        rec.num = {{"t", t}, {"exact", exact_ ? 1.0 : 0.0}};
        std::optional<SceneObject> built = RebuildShell(ctx, *o, rec);
        if (!built) continue;
        plain_.insert(id);
        if (built->kind == ObjectKind::Brep && built->brep) {
          exact_results.push_back({id, *built->brep});
          exact_notes_.push_back({0, 0});
        } else if (built->mesh) {
          results.push_back({id, *built->mesh});
        }
        continue;
      }
      // Exact B-rep first (TryExactShell above): convex planar solids with or
      // without opened faces / per-face thickness, and whole spheres / tori.
      // Fails open to the mesh constructions below, printing the kernel's
      // reason; Exact=No (option) skips this and keeps the mesh path.
      if (exact_ && o->kind == ObjectKind::Brep && o->brep) {
        static const std::vector<int> kNoRemoveExact;
        static const std::map<int, double> kNoOverridesExact;
        std::string why;
        std::optional<kernel::Brep> ex = TryExactShell(*o, has_remove ? rem_it->second : kNoRemoveExact,
                                                        has_overrides ? fth_it->second : kNoOverridesExact, t, why);
        if (ex) {
          exact_results.push_back({id, *ex});
          exact_notes_.push_back({has_remove ? rem_it->second.size() : size_t(0), has_overrides ? fth_it->second.size() : size_t(0)});
          continue;
        }
        if (!why.empty() && (has_remove || has_overrides || o->brep->raw().IsSolid()))
          ctx.Print("Shell: object " + std::to_string(id) + " - exact B-rep shell not used (" + why + "); using the mesh construction");
      }
      if (has_remove || has_overrides) {
        if (o->kind != ObjectKind::Brep || !o->brep) {
          ctx.Warn("Object " + std::to_string(id) + " has picked faces but is not a polysurface; skipped");
          continue;
        }
        static const std::vector<int> kNoRemove;
        static const std::map<int, double> kNoOverrides;
        std::string err;
        bool overrides_applied = false;
        std::optional<kernel::Mesh> r = BuildShell(o->brep->raw(), has_remove ? rem_it->second : kNoRemove,
                                                     has_overrides ? fth_it->second : kNoOverrides, t, overrides_applied, err);
        if (!r) { ctx.Warn("Shell: object " + std::to_string(id) + " " + err); continue; }
        if (has_overrides && !overrides_applied) {
          ctx.Warn("Shell: object " + std::to_string(id) +
                    " has a curved face among those kept, so its per-face thickness override(s) were not numerically verified "
                    "and were skipped; the default Thickness was used everywhere on it instead");
        }
        results.push_back({id, *r});
        if (has_remove) opened.push_back(id);
        if (has_overrides && overrides_applied) per_face.push_back(id);
        continue;
      }
    }
    if (results.empty() && exact_results.empty()) { Finish(); return; }
    ctx.Doc().BeginChange("Shell");
    // History On: the solid a plain Shell hollowed is HIDDEN instead of
    // deleted (as the booleans do), so the result can name it as its recorded
    // source and editing it - or the curve it was extruded from - re-shells it.
    const bool keep_source = ctx.App().State().history_recording;
    auto retire = [&](ObjectId id) {
      if (keep_source && plain_.count(id)) {
        if (SceneObject* s = ctx.Doc().Find(id)) { s->visible = false; s->selected = false; s->InvalidateDisplay(); }
      } else {
        ctx.Doc().Remove(id);
      }
    };
    auto record = [&](ObjectId id, ObjectId new_id) {
      if (plain_.count(id)) RecordHistoryIfEnabled(ctx, new_id, "Shell", {id}, {{"t", t}, {"exact", exact_ ? 1.0 : 0.0}});
    };
    size_t exact_i = 0;
    for (auto& [id, r] : exact_results) {
      const std::pair<size_t, size_t> note = exact_i < exact_notes_.size() ? exact_notes_[exact_i] : std::pair<size_t, size_t>{0, 0};
      ++exact_i;
      int layer = 0;
      if (const SceneObject* o = ctx.Doc().Find(id)) layer = o->layer_index;
      retire(id);
      SceneObject n = SceneObject::MakeBrep(r);
      n.layer_index = layer;
      const ObjectId new_id = ctx.Doc().Add(std::move(n));
      record(id, new_id);
      // Brep::Volume() refuses a FromPlanarFaces()-built face as "trimmed"
      // (it only integrates over a face's full surface parameter domain,
      // which a general polygon loop isn't considered to be, even a plain
      // convex rectangle like every face ShellConvexPlanar builds here) -
      // confirmed by running this exact path end-to-end, not assumed; the
      // same reason cmd_boolean.cpp's own exact-brep boolean result prints
      // a face count rather than a volume. Report volume via the same
      // display-mesh tessellation MeshOf already uses for every other
      // object in this app (including this command's own mesh-fallback
      // path below) - approximate only in the sense any displayed mesh
      // is, not a sign the geometry just added (the exact Brep) is
      // anything but exact.
      double vol = 0;
      if (const SceneObject* added = ctx.Doc().Find(new_id)) {
        if (std::optional<kernel::Mesh> disp = MeshOf(*added, 0.005)) vol = std::fabs(disp->Volume());
      }
      // A shell with removed faces is still a closed solid (the opening is a
      // cavity reachable through empty space, bounded by the flat rim in the
      // removed face's own plane), so its volume is meaningful too.
      ctx.Print("Shell: thickness " + FormatNumber(t) +
                 (note.second ? ", " + std::to_string(note.second) + " face(s) at their own thickness" : std::string()) +
                 (note.first ? ", open (" + std::to_string(note.first) + " face(s) removed)" : std::string(", closed")) +
                 ", exact B-rep, volume " + FormatNumber(vol));
    }
    for (auto& [id, r] : results) {
      int layer = 0;
      if (const SceneObject* o = ctx.Doc().Find(id)) layer = o->layer_index;
      retire(id);
      SceneObject n = SceneObject::MakeMesh(r);
      n.layer_index = layer;
      record(id, ctx.Doc().Add(std::move(n)));
      const bool is_open = std::find(opened.begin(), opened.end(), id) != opened.end();
      const bool is_per_face = std::find(per_face.begin(), per_face.end(), id) != per_face.end();
      const std::string face_note = is_per_face ? (", " + std::to_string(face_thickness_[id].size()) + " face(s) at their own thickness") : "";
      // Volume() assumes a closed, consistently-oriented mesh; an open
      // shell's Volume() is not a meaningful enclosed-volume figure (the
      // divergence-theorem sum is only exact across the missing face's
      // hole), so it is not printed for that case.
      ctx.Print("Shell: thickness " + FormatNumber(t) + face_note +
                 (is_open ? ", open (face(s) removed), area " + FormatNumber(r.Area())
                          : ", closed, volume " + FormatNumber(std::fabs(r.Volume()))));
    }
    Finish();
  }
  void StartThickness() {
    phase_ = Phase::Thickness;
    options = {{"Thickness", FormatNumber(thickness_), {}, true, false}, {"Exact", exact_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
    WantNumber("Thickness", thickness_);
  }
  std::vector<ObjectId> ids_;
  std::set<ObjectId> plain_;  // ids whose result is a plain uniform shell (RebuildShell): the only ones that record history
  std::vector<std::pair<size_t, size_t>> exact_notes_;  // per exact result: (faces removed, per-face overrides)
  bool exact_ = true;  // Exact=No keeps the mesh constructions only
  std::map<ObjectId, std::vector<int>> open_faces_;
  std::map<ObjectId, std::map<int, double>> face_thickness_;
  std::optional<ShellFacePick> pending_override_;
  Phase phase_ = Phase::Picking;
  bool override_mode_ = false;
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
      for (ObjectId id : ids) if (CurveOf(ctx, id)) profile_ids_.push_back(id);  // lockstep with CurvesOf's curve-only filter
      DeselectAll(ctx, ids);
      accept_preselection = false;
      WantObjects("Select path curve");
      return;
    }
    std::vector<kernel::NurbsCurve> path = CurvesOf(ctx, ids);
    if (path.empty()) { ctx.Warn("Select a path curve"); Finish(); return; }
    ObjectId path_id = kNoObject;
    for (ObjectId id : ids) if (CurveOf(ctx, id)) { path_id = id; break; }
    ctx.Doc().BeginChange("ExtrudeCrvAlongCrv");
    int made = 0;
    for (size_t i = 0; i < profiles_.size(); ++i) {
      std::optional<SceneObject> built = RebuildExtrudeAlongCrv(ctx, profiles_[i], path[0], HistoryRecord{});
      if (!built) continue;
      const ObjectId new_id = ctx.Doc().Add(std::move(*built));
      RecordHistoryIfEnabled(ctx, new_id, "ExtrudeCrvAlongCrv", {profile_ids_[i], path_id}, {});
      ++made;
    }
    ctx.Print("ExtrudeCrvAlongCrv: " + std::to_string(made) + " surface(s)");
    Finish();
  }
  std::vector<kernel::NurbsCurve> profiles_;
  std::vector<ObjectId> profile_ids_;  // history sources, in lockstep with profiles_
};

class ExtrudeTaperedCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to extrude"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    profiles_ = CurvesOf(ctx, ids);
    if (profiles_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    profile_ids_.clear();
    for (ObjectId id : ids) if (CurveOf(ctx, id)) profile_ids_.push_back(id);  // lockstep with CurvesOf's curve-only filter
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
    const Vector3d direction = n * d;
    const double draft_rad = angle_ * ON_PI / 180.0;
    for (size_t i = 0; i < profiles_.size(); ++i) {
      // Construction in RebuildExtrudeTapered (below the anonymous namespace),
      // shared with UpdateHistory.
      HistoryRecord rec;
      rec.command = "ExtrudeCrvTapered";
      rec.num = {{"dx", direction.x}, {"dy", direction.y}, {"dz", direction.z}, {"draft", draft_rad}, {"solid", solid_ ? 1.0 : 0.0}};
      std::optional<SceneObject> built = RebuildExtrudeTapered(ctx, profiles_[i], rec);
      if (!built) continue;
      const ObjectId new_id = ctx.Doc().Add(std::move(*built));
      RecordHistoryIfEnabled(ctx, new_id, "ExtrudeCrvTapered", {profile_ids_[i]}, rec.num);
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
  std::vector<ObjectId> profile_ids_;  // history sources, in lockstep with profiles_
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

// Project/Pull's mapping, shared by ProjectCommand and the history rebuild
// (RebuildProject, below the anonymous namespace). `dir` is the projection
// direction (Project) and unused for Pull (always the true nearest point).
std::optional<Target> TargetOf(const SceneObject& o) {
  if (o.kind == ObjectKind::Curve || o.kind == ObjectKind::Point) return std::nullopt;
  Target t;
  if (o.kind == ObjectKind::Surface && o.surface) t.surface = *o.surface;
  std::optional<kernel::Mesh> m = MeshOf(o, 0.002);
  if (!m) return std::nullopt;
  t.mesh = *m;
  return t;
}

std::optional<Point3d> MapToTargets(const std::vector<Target>& targets, bool pull, Vector3d dir, Point3d p) {
  std::optional<Point3d> best;
  for (const Target& t : targets) {
    std::optional<Point3d> q;
    if (pull) q = t.surface ? t.surface->ClosestPoint(p, 40, 40) : t.mesh.ClosestPoint(p);
    else q = LineHit(t.mesh, p, dir);
    if (q && (!best || q->DistanceTo(p) < best->DistanceTo(p))) best = q;
  }
  return best;
}

// The curve pieces a source curve maps to: runs of consecutive hits each become one curve.
std::vector<kernel::NurbsCurve> ProjectCurvePieces(const kernel::NurbsCurve& curve, const std::vector<Target>& targets, bool pull, Vector3d dir) {
  std::vector<kernel::NurbsCurve> out;
  const bool polyline = curve.Degree() == 1;
  Row samples;
  if (polyline) { for (int i = 0; i < curve.ControlPointCount(); ++i) samples.push_back(curve.ControlPointAt(i)); }
  else samples = SampleCurve(curve, std::min(64, std::max(16, 4 * curve.ControlPointCount())), false);
  Row run;
  auto flush = [&]() {
    if (run.size() >= 2) out.push_back((polyline || run.size() < 4) ? PolylineCurve(run) : kernel::NurbsCurve::FromControlPoints(run, 3));
    run.clear();
  };
  for (const Point3d& p : samples) { if (std::optional<Point3d> q = MapToTargets(targets, pull, dir, p)) run.push_back(*q); else flush(); }
  flush();
  return out;
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
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind == ObjectKind::Curve || o->kind == ObjectKind::Point) continue;
      std::optional<Target> t = TargetOf(*o);
      if (!t) continue;
      targets_.push_back(std::move(*t));
      target_ids_.push_back(id);
    }
    if (targets_.empty()) { ctx.Warn("Select surfaces, polysurfaces or meshes as targets"); Finish(); return; }
    // Pull has no direction of its own (always the real nearest point), so
    // it runs immediately. Project used to always sweep along the active
    // CPlane normal with no way to choose otherwise - the real "CPlane
    // sampling only" gap this command's own PARITY_MAP bullet named.
    if (pull_) { DoWork(ctx, Vector3d()); Finish(); return; }
    WantPoint("Direction start point, or Enter for the active CPlane normal");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!have_dir_start_) { dir_start_ = p; have_dir_start_ = true; WantPoint("Direction end point"); return; }
    Vector3d dir = p - dir_start_;
    if (!dir.Unitize()) { ctx.Warn("Project: direction points coincide, using the active CPlane normal instead"); dir = ActiveNormal(ctx); }
    DoWork(ctx, dir);
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    DoWork(ctx, ActiveNormal(ctx));
    Finish();
  }
  void DoWork(CommandContext& ctx, Vector3d dir) {
    // Snapshot the sources before adding anything (Add() may reallocate).
    struct Source { ObjectId id; std::optional<kernel::NurbsCurve> curve; Point3d point; int layer; };
    std::vector<Source> sources;
    for (ObjectId id : sources_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      Source s;
      s.id = id;
      s.layer = o->layer_index;
      if (o->kind == ObjectKind::Curve) s.curve = *o->curve; else s.point = o->point;
      sources.push_back(s);
    }
    const char* label = pull_ ? "Pull" : "Project";
    ctx.Doc().BeginChange(label);
    int curves = 0, points = 0, recorded = 0, split = 0;
    // History On: a source that maps to exactly ONE result is recorded as {source, targets...} so the result follows edits of
    // the source or of a target; a source that splits into several pieces is not (the piece count can change on an edit).
    std::vector<ObjectId> rec_sources;
    std::map<std::string, double> rec_num{{"dx", dir.x}, {"dy", dir.y}, {"dz", dir.z}, {"pull", pull_ ? 1.0 : 0.0}};
    auto record = [&](ObjectId src_id, ObjectId result_id) {
      std::vector<ObjectId> srcs{src_id};
      srcs.insert(srcs.end(), target_ids_.begin(), target_ids_.end());
      RecordHistoryIfEnabled(ctx, result_id, label, srcs, rec_num);
      if (ctx.App().State().history_recording) ++recorded;
    };
    for (const Source& s : sources) {
      if (!s.curve) {
        if (std::optional<Point3d> q = MapToTargets(targets_, pull_, dir, s.point)) {
          SceneObject n = SceneObject::MakePoint(*q);
          n.layer_index = s.layer;
          const ObjectId nid = ctx.Doc().Add(std::move(n));
          record(s.id, nid);
          ++points;
        }
        continue;
      }
      const std::vector<kernel::NurbsCurve> pieces = ProjectCurvePieces(*s.curve, targets_, pull_, dir);
      for (const kernel::NurbsCurve& c : pieces) {
        SceneObject n = SceneObject::MakeCurve(c);
        n.layer_index = s.layer;
        const ObjectId nid = ctx.Doc().Add(std::move(n));
        if (pieces.size() == 1) record(s.id, nid);
        ++curves;
      }
      if (pieces.size() > 1) ++split;
    }
    ctx.Print(std::string(label) + ": " + std::to_string(curves) + " curve(s), " + std::to_string(points) + " point(s)" +
              (ctx.App().State().history_recording
                   ? "; " + std::to_string(recorded) + " result(s) recorded in history" +
                         (split ? ", " + std::to_string(split) + " source(s) split into several pieces and not recorded" : std::string())
                   : std::string()));
  }
  std::vector<ObjectId> target_ids_;
  bool pull_;
  bool have_sources_ = false;
  bool have_dir_start_ = false;
  Point3d dir_start_;
  std::vector<ObjectId> sources_;
  std::vector<Target> targets_;
};

}  // namespace

// Project / Pull re-execution (history_rebuild.h). inputs[0] is the source curve or
// point, inputs[1..] the target surfaces/polysurfaces/meshes. A curve must still map
// to exactly ONE piece (it did when recorded); more, fewer or a miss refuses.
std::optional<SceneObject> RebuildProject(CommandContext&, const HistoryInputs& in, const HistoryRecord& rec, std::string& why) {
  if (in.size() < 2) { why = "the record names no target object"; return std::nullopt; }
  auto num = [&](const char* k) { const auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const bool pull = rec.command == "Pull";
  const Vector3d dir(num("dx"), num("dy"), num("dz"));
  if (!pull && dir.Length() < 1e-9) { why = "the record holds no projection direction"; return std::nullopt; }
  std::vector<Target> targets;
  for (size_t i = 1; i < in.size(); ++i) {
    std::optional<Target> t = TargetOf(*in[i]);
    if (!t) { why = "target object " + std::to_string(in[i]->id) + " is " + HistoryKindName(in[i]->kind) + ", not a surface, polysurface or mesh"; return std::nullopt; }
    targets.push_back(std::move(*t));
  }
  const SceneObject& src = *in[0];
  const std::string verb = pull ? "pulled" : "projected";
  if (src.kind == ObjectKind::Point) {
    std::optional<Point3d> q = MapToTargets(targets, pull, dir, src.point);
    if (!q) { why = "the point no longer hits any target when " + verb; return std::nullopt; }
    return SceneObject::MakePoint(*q);
  }
  if (src.kind != ObjectKind::Curve || !src.curve) { why = "source object " + std::to_string(src.id) + " is " + HistoryKindName(src.kind) + ", not a curve or point"; return std::nullopt; }
  const std::vector<kernel::NurbsCurve> pieces = ProjectCurvePieces(*src.curve, targets, pull, dir);
  if (pieces.size() != 1) {
    why = "the curve now " + verb + " to " + std::to_string(pieces.size()) + " piece(s) instead of the one that was recorded";
    return std::nullopt;
  }
  return SceneObject::MakeCurve(pieces[0]);
}

// Shared by NetworkSrf's own 2-curve case above and UpdateHistory
// (cmd_history.cpp) - same "single source of truth" shape as RebuildPipe/
// RebuildPatch below. `b` is reversed when its tail, not its head, is
// nearer `a`'s own start - re-derived fresh from the two curves' *current*
// geometry every call, never itself a recorded parameter, the same way
// RebuildPipe below reads a rail's closed/open `wrap` fresh rather than
// freezing it at creation time. Returns nullopt only if OpenNURBS' own
// `CreateRuledSurface` refuses the (degenerate) inputs.
std::optional<SceneObject> RebuildNetworkSrf2(CommandContext&, const kernel::NurbsCurve& a_in,
                                               const kernel::NurbsCurve& b_in, const HistoryRecord&) {
  kernel::NurbsCurve a = a_in, b = b_in;
  const Point3d a0 = a.PointAt(a.Domain().min), b0 = b.PointAt(b.Domain().min), b1 = b.PointAt(b.Domain().max);
  if (a0.DistanceTo(b1) < a0.DistanceTo(b0)) b.Reverse();
  ON_NurbsSurface ns;
  if (!ns.CreateRuledSurface(a.raw(), b.raw())) return std::nullopt;
  kernel::NurbsSurface k;
  k.raw() = ns;
  return SceneObject::MakeSurface(k);
}

// Shared by PipeCommand::OnNumber above and UpdateHistory (cmd_history.cpp)
// - same "single source of truth" shape as RebuildExtrude/RebuildRevolve/
// RebuildLoft (cmd_solids.cpp, declared in history_rebuild.h). Reproduces
// PipeCommand::OnNumber's construction exactly (same RMF frames, same CV-
// circle radius correction for the open-surface case, same mesh-vs-
// periodic-surface choice) from rec.num alone: radius (> 0) and cap (1/0,
// Cap=Yes/No). `wrap` (closed rail) is NOT itself a recorded parameter -
// like the live command, it is read fresh from whichever curve is passed
// in, so a rail curve that was opened or closed since the pipe was first
// built is honoured on rebuild, not frozen at creation time. Calls the
// same anonymous-namespace helpers (ArcLengthParams/RmfFrames/
// MeshFromRows/SurfaceFromRows/Outward/kRailSamples/kRingSegments) the
// live command itself used above - anonymous-namespace names have
// internal linkage but are still visible by ordinary unqualified lookup
// throughout this translation unit, so this needs no header for them.
std::optional<SceneObject> RebuildPipe(CommandContext&, const kernel::NurbsCurve& c, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const double radius = num("radius");
  if (radius <= 0) return std::nullopt;
  const bool cap = num("cap") != 0.0;
  const bool wrap = c.IsClosed();
  // An open, capped rail now calls the kernel's own exact Brep::Pipe first
  // (an exact rational cylinder wall along a straight rail, real flat-disc
  // end caps) instead of unconditionally falling through to this file's
  // own hand-rolled ring-loft-and-weld mesh below - the same "exact kernel
  // call first, fail closed to the existing construction" shape
  // RebuildSweep1/RebuildSweep2 above already use for their own single-
  // section case. A closed rail is untouched (Brep::Pipe's own tube has no
  // ends to cap, so there is nothing this call would add over the existing
  // mesh path); the kernel throws std::invalid_argument for a genuinely
  // degenerate rail (zero tangent, touching stations) - caught here so the
  // hand-rolled construction below still runs, the same "degrade
  // gracefully" contract every other Rebuild* function in this file
  // follows.
  // A CLOSED rail with Cap=Yes also takes the kernel's exact Brep::Pipe now
  // ("a closed rail gives a closed tube", brep.h): a closed rail has no ends
  // to cap, but the result is then a real closed solid (an exact rational
  // torus for a circular rail) rather than the hand-rolled ring-loft mesh
  // the earlier code kept for it. A kernel throw still falls through to
  // that mesh construction.
  if (cap) {
    try {
      return SceneObject::MakeBrep(kernel::Brep::Pipe(c, radius, /*cap=*/true, /*stations=*/32, /*round_caps=*/false));
    } catch (const std::exception&) {
      // Fall through to the mesh construction below.
    }
  }
  const int nrows = c.IsLinear() && !wrap ? 2 : kRailSamples;
  const std::vector<double> params = ArcLengthParams(c, wrap ? nrows + 1 : nrows, false);
  const std::vector<Frame> frames = RmfFrames(c, params, ON_zaxis, wrap);
  const bool as_mesh = cap || wrap;
  // A cubic periodic B-spline through CVs on a circle runs inside it:
  // enlarge the CV circle so the surface has the requested radius - same
  // correction the live command applies.
  const double rr = as_mesh ? radius : radius * 3.0 / (2.0 + std::cos(2 * ON_PI / kRingSegments));
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
    return SceneObject::MakeMesh(m);
  }
  return SceneObject::MakeSurface(SurfaceFromRows(rows, true, false));
}

// Shared by the Patch free function below and UpdateHistory
// (cmd_history.cpp) - same "single source of truth" shape as RebuildPipe
// above, scoped to Patch's single-planar-closed-curve case only (Patch's
// own "closed.size() == 1" branch when the curve is already planar, the
// `ON_BrepTrimmedPlane(own, closed[0].raw())` call): that result depends
// only on the one curve's own current geometry, never on any other point
// or curve also selected at build time (those only ever feed the
// non-planar-boundary fallback or the multi-point least-squares branch,
// neither of which this covers), so it is exactly as self-contained as
// RebuildPipe's single rail. Returns nullopt if the curve is no longer
// closed+planar after being edited (a hard failure, not silently
// replaced by the mesh-projection fallback this file's own Patch uses
// live - same "degrade to stale rather than switch construction methods
// underneath the object" contract RebuildExtrude/RebuildPipe already
// have for their own degenerate-after-edit case).
std::optional<SceneObject> RebuildPatch(CommandContext& ctx, const kernel::NurbsCurve& c, const HistoryRecord&) {
  if (!c.IsClosed()) return std::nullopt;
  ON_Plane own;
  if (!c.raw().IsPlanar(&own, ctx.Settings().absolute_tolerance)) return std::nullopt;
  ON_Brep* b = ON_BrepTrimmedPlane(own, c.raw());
  if (!b) return std::nullopt;
  return SceneObject::MakeBrep(WrapBrep(b));
}

// Patch, the non-planar fitted case (Patch above): sources are every selected curve and point in selection order. The
// fit is re-run on their current shape. Refuses (reason in `why`, the object kept) when a source is no longer a curve or
// point, or the data are now within tolerance of a plane (the live command would build a planar patch, a different
// construction), or the fit is no better than the flat plane or fails.
std::optional<SceneObject> RebuildPatchFit(CommandContext& ctx, const std::vector<const SceneObject*>& inputs, const HistoryRecord&, std::string& why) {
  Row pts, point_objects;
  std::vector<kernel::NurbsCurve> all_curves;
  int ncrv = 0, npts = 0;
  for (const SceneObject* o : inputs) {
    if (o->kind == ObjectKind::Point) { pts.push_back(o->point); point_objects.push_back(o->point); ++npts; }
    else if (o->kind == ObjectKind::Curve && o->curve) {
      all_curves.push_back(*o->curve);
      const Row smp = SampleCurve(*o->curve, 32, o->curve->IsClosed());
      pts.insert(pts.end(), smp.begin(), smp.end());
      ++ncrv;
    } else {
      why = "source object " + std::to_string(o->id) + " is " + HistoryKindName(o->kind) + ", not a curve or a point";
      return std::nullopt;
    }
  }
  if (pts.size() < 3) { why = "fewer than three points are left in the sources"; return std::nullopt; }
  const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  const ON_Plane fit = FitPlane(pts);
  double flat_dev = 0.0;
  for (const Point3d& p : pts) flat_dev = std::max(flat_dev, std::fabs(fit.DistanceTo(p)));
  if (flat_dev <= tol) {
    why = "the sources are now flat (within " + FormatNumber(tol) + " of their best-fit plane), so the fitted patch no longer applies";
    return std::nullopt;
  }
  std::string reason;
  std::optional<SceneObject> r = FitNonPlanarPatch(ctx, pts, all_curves, point_objects, flat_dev, tol, ncrv, npts, reason);
  if (!r) why = "the patch fit no longer works on the edited sources: " + reason;
  return r;
}

// ---------------------------------------------------------------------------
// Section-anchored single-section sweeps (RebuildSweep1 / RebuildSweep2).
//
// kernel::Brep::Sweep1/Sweep2 carry the section RIGIDLY from the rail's START
// frame: the section's offset from the rail start is preserved, so the swept
// surface runs from wherever the section is for the rail's whole length
// (a section sitting 40% along a straight rail of length 10 gives a surface
// spanning 40%..140% of the rail, overshooting its end by 4), and on a rail
// that is not a circular arc the transported copies drift off the rail
// altogether. Rhino's semantics are different: the swept surface covers the
// rail and passes through the picked section WHEREVER it sits along it.
//
// So the section is anchored first (sectionfit::AnchorOnRail: the rail point
// closest to the section's reference point). A section at the rail start (to
// 1e-6 of the rail's size) is the kernel call UNCHANGED - it is exactly the
// case that call is right for. Otherwise the rail is cut at the anchor and
// each side is swept from the anchor by the same kernel call (the section is
// station 0 of each side, so it is reproduced exactly), the backward side on
// the reversed rail with its v direction turned back, and the two surfaces are
// spliced with NurbsSurface::Join (they share the section as their common
// boundary row). A section at the rail's end needs only the backward side; a
// closed rail is simply re-seamed at the anchor (a periodic sweep has no ends
// to split). Whatever is built is then MEASURED: the isocurve of the result at
// the section's station is compared with the section, and a result that
// strays by more than 1e-6 of the section's size is refused rather than
// returned - the caller then falls back to the older section-anchored
// approximate sweep and the reason is printed.
// ---------------------------------------------------------------------------
namespace {

constexpr double kAnchorTolerance = 1e-6;  // fraction of the rail's / section's size

// The wall (first face's NURBS surface) of an uncapped swept Brep.
std::optional<kernel::NurbsSurface> SweptWall(const kernel::Brep& b) {
  if (b.raw().m_F.Count() < 1) return std::nullopt;
  const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(b.raw().m_F[0].SurfaceOf());
  if (!ns) return std::nullopt;
  kernel::NurbsSurface k;
  k.raw() = *ns;
  return k;
}

// Fraction of the rail's arc length at which parameter `t` sits.
double RailFraction(const kernel::NurbsCurve& rail, double t) {
  const kernel::Interval d = rail.Domain();
  if (!(t > d.min) || !(t < d.max)) return t <= d.min ? 0.0 : 1.0;
  kernel::NurbsCurve left, right;
  if (rail.Split(t, left, right) != kernel::Result::Ok) return (t - d.min) / (d.max - d.min);
  const double ll = left.Length(), rl = right.Length();
  return ll + rl > 0.0 ? ll / (ll + rl) : 0.0;
}

std::string Percent(double fraction) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f%%", 100.0 * fraction);
  return buf;
}

// The schedules recorded in `rec` (an axis without a record is not scaled).
struct AxisScaleSchedules {
  std::vector<std::pair<double, double>> x, y;
  const std::vector<std::pair<double, double>>* xp = nullptr;
  const std::vector<std::pair<double, double>>* yp = nullptr;
  kernel::Vector3d axis{1.0, 0.0, 0.0};
};

void ReadAxisScale(const HistoryRecord& rec, const kernel::NurbsCurve& rail, AxisScaleSchedules& out) {
  auto get = [&](const char* k) { return rec.num.at(k); };
  if (rec.num.count("sx0")) { out.x = {{0.0, get("sx0")}, {1.0, get("sx1")}}; out.xp = &out.x; }
  if (rec.num.count("sy0")) { out.y = {{0.0, get("sy0")}, {1.0, get("sy1")}}; out.yp = &out.y; }
  // The section's X axis: world X seen in the plane perpendicular to the rail at its start (world Y when the rail starts along X).
  kernel::Vector3d t = rail.TangentAt(rail.Domain().min);
  if (std::fabs(t.x) > 0.99 * t.Length()) out.axis = kernel::Vector3d(0.0, 1.0, 0.0);
}

kernel::NurbsCurve Reversed(const kernel::NurbsCurve& c) {
  kernel::NurbsCurve r = c;
  r.Reverse();
  return r;
}

struct AnchoredSweep {
  std::optional<kernel::NurbsSurface> surface;
  std::string how;      // what was done, for the printed line
  std::string refusal;  // why `surface` is empty
  double deviation = 0.0;  // measured: section vs the result's isocurve at its station
  bool gated = false;      // true when the result was checked against the tolerance
};

// Measures the isocurve of `s` at `v_station` against `section`; false (with
// `a.refusal` set) when a gated result strays by more than the tolerance.
bool VerifyStation(AnchoredSweep& a, const kernel::NurbsSurface& s, double v_station, const kernel::NurbsCurve& section, bool gated) {
  const double limit = kAnchorTolerance * std::max(sectionfit::CurveSize(section), 1e-9);
  a.deviation = sectionfit::CurveDeviation(section, s.IsoCurve(0, v_station));
  a.gated = gated;
  if (gated && !(a.deviation <= limit)) {
    a.refusal = "the combined surface misses the section by " + sectionfit::Sci(a.deviation) + " (limit " + sectionfit::Sci(limit) + ")";
    return false;
  }
  return true;
}

// Sweeps the two sides of an anchored rail pair (`fwd_*` from the anchor to the
// rails' ends, `back_*` from the anchor back toward their starts, already
// reversed so they also run away from the anchor) and splices them. Either
// side may be absent (null), but not both. `sweep` builds one side's wall.
template <typename Sweep>
std::optional<kernel::NurbsSurface> SweepBothSides(const Sweep& sweep, bool have_fwd, bool have_back, double join_tol, double& v_station, std::string& refusal) {
  std::optional<kernel::NurbsSurface> fwd, back;
  if (have_fwd) fwd = sweep(true);
  if (have_back) back = sweep(false);
  if ((have_fwd && !fwd) || (have_back && !back)) { refusal = "the kernel sweep returned no surface"; return std::nullopt; }
  if (fwd && !back) { v_station = fwd->Domain(1).min; return fwd; }
  // The backward side runs anchor -> start; turn its v direction so v runs
  // start -> end like the forward side, leaving the section at its v maximum.
  if (back->Reverse(1) != kernel::Result::Ok) { refusal = "could not turn the backward side's v direction"; return std::nullopt; }
  v_station = back->Domain(1).max;
  if (!fwd) return back;
  kernel::SurfaceJoinReport report;
  const double station = v_station;
  if (back->Join(*fwd, report, join_tol) != kernel::Result::Ok) {
    refusal = "joining the two halves of the sweep failed: " + report.failure_reason;
    return std::nullopt;
  }
  v_station = station;
  return back;
}

// A closed rail that is an exact planar circle (every one of 65 samples within 1e-9 of its size of the circle
// through three of them, and in its plane): rotation-minimizing frames on a circle have no torsion, so they are
// the rigid rotation about the circle's axis through its centre, and the sweep of ANY section placed at the
// rail start is that section revolved once round the axis - an exact rational surface (a torus for a circle
// section), not the kernel's 32-station interpolating skin (about 3e-5 off a torus). Returns nullopt for any
// other rail, or when the kernel's Revolve refuses the section (it touches or crosses the axis); the caller then
// takes the ordinary kernel sweep.
std::optional<kernel::NurbsSurface> RevolveAboutCircularRail(const kernel::NurbsCurve& section, const kernel::NurbsCurve& rail, double& deviation) {
  if (!rail.IsClosed()) return std::nullopt;
  const kernel::Interval d = rail.Domain();
  auto at = [&](double f) { return rail.PointAt(d.min + f * (d.max - d.min)); };
  const ON_Circle circle(at(0.0), at(1.0 / 3.0), at(2.0 / 3.0));
  if (!circle.IsValid() || !(circle.radius > 0.0)) return std::nullopt;
  const double tol = 1e-9 * std::max(circle.radius, 1e-9);
  for (int i = 0; i <= 64; ++i) {
    const Point3d p = at(static_cast<double>(i) / 64.0);
    const Vector3d r = p - circle.plane.origin;
    if (std::fabs(ON_DotProduct(r, circle.plane.zaxis)) > tol || std::fabs(r.Length() - circle.radius) > tol) return std::nullopt;
  }
  // Rotation about `axis` by +theta must carry the rail's start along its start tangent.
  Vector3d axis = ON_CrossProduct(at(0.0) - circle.plane.origin, rail.TangentAt(d.min));
  if (!axis.Unitize()) return std::nullopt;
  try {
    kernel::Brep b = kernel::Brep::Revolve(section, circle.plane.origin, axis, 2.0 * ON_PI, /*cap=*/false);
    std::optional<kernel::NurbsSurface> wall = SweptWall(b);
    if (!wall) return std::nullopt;
    // MEASURED, not assumed: the distance of a point from a surface of revolution is its distance, in the
    // (radius, height) half-plane about the axis, from the section's own (radius, height) profile. Compare a
    // 33 x 65 grid of the wall's points with 4001 samples of the section (refined by a local ternary search).
    const Point3d o = circle.plane.origin;
    auto meridian = [&](const Point3d& p, double& rho, double& z) {
      const Vector3d r = p - o;
      z = ON_DotProduct(r, axis);
      rho = (r - axis * z).Length();
    };
    const kernel::Interval sd = section.Domain();
    constexpr int kProfile = 4000;
    std::vector<std::pair<double, double>> prof(kProfile + 1);
    for (int i = 0; i <= kProfile; ++i) meridian(section.PointAt(sd.min + (sd.max - sd.min) * i / kProfile), prof[static_cast<size_t>(i)].first, prof[static_cast<size_t>(i)].second);
    const kernel::Interval wu = wall->Domain(0), wv = wall->Domain(1);
    double worst = 0.0;
    for (int i = 0; i <= 32; ++i) for (int j = 0; j <= 64; ++j) {
      double rho, z;
      meridian(wall->PointAt(wu.min + (wu.max - wu.min) * i / 32.0, wv.min + (wv.max - wv.min) * j / 64.0), rho, z);
      int best = 0;
      double bd = 1e300;
      for (int k = 0; k <= kProfile; ++k) {
        const double dd = (prof[static_cast<size_t>(k)].first - rho) * (prof[static_cast<size_t>(k)].first - rho) + (prof[static_cast<size_t>(k)].second - z) * (prof[static_cast<size_t>(k)].second - z);
        if (dd < bd) { bd = dd; best = k; }
      }
      double lo = sd.min + (sd.max - sd.min) * std::max(best - 1, 0) / kProfile, hi = sd.min + (sd.max - sd.min) * std::min(best + 1, kProfile) / kProfile;
      auto dist_at = [&](double t) { double r2, z2; meridian(section.PointAt(t), r2, z2); return std::hypot(r2 - rho, z2 - z); };
      for (int it = 0; it < 60; ++it) {
        const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
        if (dist_at(m1) < dist_at(m2)) hi = m2; else lo = m1;
      }
      worst = std::max(worst, dist_at(0.5 * (lo + hi)));
    }
    deviation = worst;
    if (!(worst <= 1e-9 * std::max(sectionfit::CurveSize(section), 1e-9))) return std::nullopt;
    return wall;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

AnchoredSweep AnchoredSweep1(const kernel::NurbsCurve& section, const kernel::NurbsCurve& rail) {
  AnchoredSweep out;
  const kernel::Interval dom = rail.Domain();
  const double rail_tol = kAnchorTolerance * std::max(sectionfit::CurveSize(rail), 1e-9);
  const sectionfit::RailAnchor an = sectionfit::AnchorOnRail(section, rail);
  const Point3d pa = rail.PointAt(an.t);
  const bool closed = rail.IsClosed();
  const bool at_start = pa.DistanceTo(rail.PointAt(dom.min)) <= rail_tol;
  const bool at_end = !closed && pa.DistanceTo(rail.PointAt(dom.max)) <= rail_tol;
  const double join_tol = 1e-8 * std::max(sectionfit::CurveSize(section), 1e-6);

  bool revolved = false;
  double revolve_dev = 0.0;
  auto wall_of = [&](const kernel::NurbsCurve& r) {
    if (std::optional<kernel::NurbsSurface> rev = RevolveAboutCircularRail(section, r, revolve_dev)) { revolved = true; return rev; }
    return SweptWall(kernel::Brep::Sweep1(section, r, /*stations=*/32, /*cap=*/false));
  };

  if (at_start) {
    // The case Brep::Sweep1 is right for: the call, unchanged.
    out.surface = wall_of(rail);
    if (!out.surface) { out.refusal = "the kernel sweep returned no surface"; return out; }
    out.how = revolved ? "the rail is an exact circle, so the sweep is the section revolved once about the rail's axis (exact rational surface, not a skin; measured distance of 2145 wall points from the revolved section " + sectionfit::Sci(revolve_dev) + ")"
                       : "the section is at the rail start";
    VerifyStation(out, *out.surface, out.surface->Domain(1).min, section, /*gated=*/false);
    return out;
  }
  const std::string where = "the section sits " + Percent(RailFraction(rail, an.t)) + " along the rail";
  double v_station = 0.0;
  if (closed) {
    kernel::NurbsCurve reseamed = rail;
    if (reseamed.ChangeSeam(an.t) != kernel::Result::Ok) { out.refusal = "could not move the closed rail's seam to the section"; return out; }
    out.surface = wall_of(reseamed);
    if (!out.surface) { out.refusal = "the kernel sweep returned no surface"; return out; }
    v_station = out.surface->Domain(1).min;
    out.how = where + (revolved ? "; the closed rail is an exact circle, so the sweep is the section revolved once about the rail's axis (exact rational surface, not a skin; measured distance of 2145 wall points from the revolved section " + sectionfit::Sci(revolve_dev) + ")"
                                : "; the closed rail was re-seamed there");
  } else {
    kernel::NurbsCurve left, right;
    if (!at_end && rail.Split(an.t, left, right) != kernel::Result::Ok) { out.refusal = "could not split the rail at the section"; return out; }
    const kernel::NurbsCurve back_rail = Reversed(at_end ? rail : left);
    out.surface = SweepBothSides([&](bool forward) { return forward ? wall_of(right) : wall_of(back_rail); },
                                 /*have_fwd=*/!at_end, /*have_back=*/true, join_tol, v_station, out.refusal);
    if (!out.surface) return out;
    out.how = where + (at_end ? "; swept back along the rail from the section" : "; the rail was split there and both sides swept from the section and joined");
  }
  if (!VerifyStation(out, *out.surface, v_station, section, /*gated=*/true)) out.surface.reset();
  return out;
}

AnchoredSweep AnchoredSweep2(const kernel::NurbsCurve& section, const kernel::NurbsCurve& rail1, const kernel::NurbsCurve& rail2_in) {
  AnchoredSweep out;
  // rail2 runs the way rail1 does (the kernel's own start-to-start test).
  kernel::NurbsCurve rail2 = rail2_in;
  if (rail1.PointAt(rail1.Domain().min).DistanceTo(rail2.PointAt(rail2.Domain().max)) < rail1.PointAt(rail1.Domain().min).DistanceTo(rail2.PointAt(rail2.Domain().min))) rail2.Reverse();
  const kernel::Interval d1 = rail1.Domain(), d2 = rail2.Domain();
  const double tol1 = kAnchorTolerance * std::max(sectionfit::CurveSize(rail1), 1e-9), tol2 = kAnchorTolerance * std::max(sectionfit::CurveSize(rail2), 1e-9);
  const sectionfit::RailAnchor an1 = sectionfit::AnchorOnRail(section, rail1), an2 = sectionfit::AnchorOnRail(section, rail2);
  const Point3d p1 = rail1.PointAt(an1.t), p2 = rail2.PointAt(an2.t);
  const bool closed1 = rail1.IsClosed(), closed2 = rail2.IsClosed();
  const bool start1 = p1.DistanceTo(rail1.PointAt(d1.min)) <= tol1, start2 = p2.DistanceTo(rail2.PointAt(d2.min)) <= tol2;
  const bool end1 = !closed1 && p1.DistanceTo(rail1.PointAt(d1.max)) <= tol1, end2 = !closed2 && p2.DistanceTo(rail2.PointAt(d2.max)) <= tol2;
  const double join_tol = 1e-8 * std::max(sectionfit::CurveSize(section), 1e-6);

  auto wall_of = [&](const kernel::NurbsCurve& a, const kernel::NurbsCurve& b) { return SweptWall(kernel::Brep::Sweep2(section, a, b, /*stations=*/32, /*cap=*/false)); };

  if (start1 && start2) {
    out.surface = wall_of(rail1, rail2_in);
    if (!out.surface) { out.refusal = "the kernel sweep returned no surface"; return out; }
    out.how = "the section is at the start of both rails";
    VerifyStation(out, *out.surface, out.surface->Domain(1).min, section, /*gated=*/false);
    return out;
  }
  const std::string where = "the section sits " + Percent(RailFraction(rail1, an1.t)) + " along rail 1 and " + Percent(RailFraction(rail2, an2.t)) + " along rail 2";
  double v_station = 0.0;
  if (closed1 && closed2) {
    kernel::NurbsCurve r1 = rail1, r2 = rail2;
    if (r1.ChangeSeam(an1.t) != kernel::Result::Ok || r2.ChangeSeam(an2.t) != kernel::Result::Ok) { out.refusal = "could not move the closed rails' seams to the section"; return out; }
    // Two closed rails have no start/end to compare, so match their sense of travel at the new seams.
    if (ON_DotProduct(r1.TangentAt(r1.Domain().min), r2.TangentAt(r2.Domain().min)) < 0.0) r2.Reverse();
    out.surface = wall_of(r1, r2);
    if (!out.surface) { out.refusal = "the kernel sweep returned no surface"; return out; }
    v_station = out.surface->Domain(1).min;
    out.how = where + "; both closed rails were re-seamed there";
  } else {
    if (start1 != start2 || end1 != end2) {
      out.refusal = std::string("the section sits at the ") + (start1 ? "start" : end1 ? "end" : "interior") + " of rail 1 but at the " + (start2 ? "start" : end2 ? "end" : "interior") +
                    " of rail 2, so one rail has nothing to sweep on one side of it";
      return out;
    }
    kernel::NurbsCurve l1, r1, l2, r2;
    if (!end1 && (rail1.Split(an1.t, l1, r1) != kernel::Result::Ok || rail2.Split(an2.t, l2, r2) != kernel::Result::Ok)) { out.refusal = "could not split the rails at the section"; return out; }
    const kernel::NurbsCurve b1 = Reversed(end1 ? rail1 : l1), b2 = Reversed(end2 ? rail2 : l2);
    // Each side's rails must still run the same way from the anchor (the kernel re-matches them by start distance).
    auto same_way = [](const kernel::NurbsCurve& a, const kernel::NurbsCurve& b) {
      const Point3d a0 = a.PointAt(a.Domain().min);
      return a0.DistanceTo(b.PointAt(b.Domain().min)) <= a0.DistanceTo(b.PointAt(b.Domain().max));
    };
    if ((!end1 && !same_way(r1, r2)) || !same_way(b1, b2)) { out.refusal = "the two rails do not run the same way away from the section"; return out; }
    out.surface = SweepBothSides([&](bool forward) { return forward ? wall_of(r1, r2) : wall_of(b1, b2); },
                                 /*have_fwd=*/!end1, /*have_back=*/true, join_tol, v_station, out.refusal);
    if (!out.surface) return out;
    out.how = where + (end1 ? "; swept back along the rails from the section" : "; both rails were split there and both sides swept from the section and joined");
  }
  if (!VerifyStation(out, *out.surface, v_station, section, /*gated=*/true)) out.surface.reset();
  return out;
}

bool CapRequested(const HistoryRecord& rec) {
  const auto it = rec.num.find("cap");
  return it != rec.num.end() && it->second != 0.0;
}

// Cap=Yes for a swept wall: the kernel's own AssembleSweptBody (the capping Brep::Sweep1/Sweep2/Pipe use) turns
// the wall plus a closed planar section's two end isocurves into a closed B-rep solid. The wall may come from a
// section anywhere along the rail (the two sides joined), whose section direction need not be the one the
// kernel's own cap=true call would pick, so a result that is not a positive-volume closed solid is retried with
// the section direction turned. Anything that cannot be capped (an open or non-planar section, a closed rail with
// no ends) keeps the uncapped surface and says why - never a silently wrong solid. Cap=No is the plain surface.
// A capped solid needs a section that is a SIMPLE closed loop, wound once: a figure-eight, a self-crossing loop or a loop
// traversed twice would still close topologically and have a positive signed volume, but not bound a solid.  The wall's
// section at several stations is sampled, flattened onto its own best-fit plane, and tested for self-crossing and for a total
// exterior turn of one revolution.  Returns an empty string when the sections are simple, else the reason.
std::string SectionLoopProblem(const kernel::NurbsSurface& wall) {
  const ON_NurbsSurface& srf = wall.raw();
  const ON_Interval du = srf.Domain(0), dv = srf.Domain(1);
  const int N = 160;
  for (int vi = 0; vi < 3; ++vi) {
    const double v = dv.ParameterAt(vi * 0.5);
    std::vector<ON_3dPoint> pts;
    pts.reserve(N);
    for (int k = 0; k < N; ++k) pts.push_back(srf.PointAt(du.ParameterAt(static_cast<double>(k) / N), v));
    ON_3dVector nrm(0, 0, 0);  // Newell normal
    for (int k = 0; k < N; ++k) {
      const ON_3dPoint &a = pts[k], &b = pts[(k + 1) % N];
      nrm.x += (a.y - b.y) * (a.z + b.z);
      nrm.y += (a.z - b.z) * (a.x + b.x);
      nrm.z += (a.x - b.x) * (a.y + b.y);
    }
    if (!nrm.Unitize()) return "the cross section encloses no area";
    ON_3dPoint c(0, 0, 0);
    for (const ON_3dPoint& q : pts) c += q;
    c = c * (1.0 / N);
    ON_3dVector ex = pts[0] - c;
    ex -= (ex * nrm) * nrm;
    if (!ex.Unitize()) return "the cross section is degenerate";
    const ON_3dVector ey = ON_CrossProduct(nrm, ex);
    std::vector<std::pair<double, double>> q2;
    for (const ON_3dPoint& q : pts) q2.emplace_back((q - c) * ex, (q - c) * ey);
    auto cross = [](double ax, double ay, double bx, double by, double cx, double cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
    for (int i = 0; i < N; ++i) {
      for (int j = i + 2; j < N; ++j) {
        if (i == 0 && j == N - 1) continue;
        const auto &a = q2[i], &b = q2[(i + 1) % N], &c2 = q2[j], &d = q2[(j + 1) % N];
        const double d1 = cross(a.first, a.second, b.first, b.second, c2.first, c2.second), d2 = cross(a.first, a.second, b.first, b.second, d.first, d.second);
        const double d3 = cross(c2.first, c2.second, d.first, d.second, a.first, a.second), d4 = cross(c2.first, c2.second, d.first, d.second, b.first, b.second);
        if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0))) return "the cross section crosses itself";
      }
    }
    double turn = 0;  // sum of exterior angles: +-2 pi for any simple closed loop (convex or not), 0 for a figure-eight, 4 pi for a doubled loop
    for (int k = 0; k < N; ++k) {
      const auto &a = q2[k], &b = q2[(k + 1) % N], &c2 = q2[(k + 2) % N];
      const double ux = b.first - a.first, uy = b.second - a.second, wx = c2.first - b.first, wy = c2.second - b.second;
      turn += std::atan2(ux * wy - uy * wx, ux * wx + uy * wy);
    }
    if (!(std::fabs(std::fabs(turn) - 2.0 * ON_PI) < 0.5)) return "the cross section is not a simple loop wound once (total turn " + std::to_string(turn / (2.0 * ON_PI)) + " revolutions)";
  }
  return std::string();
}

SceneObject CapOrKeepWall(CommandContext& ctx, const char* caller, const kernel::NurbsSurface& wall, bool cap) {
  if (!cap) return SceneObject::MakeSurface(wall);
  std::string why;
  try {
    if (!wall.IsClosed(0)) {
      why = "the cross section is not closed";
    } else if (const std::string loop = SectionLoopProblem(wall); !loop.empty()) {
      why = loop;
    } else {
      // A closed rail has no ends: the wall, closed both ways (a torus-like face), already bounds the solid.
      const bool ends = !wall.IsClosed(1);
      for (int attempt = 0; attempt < 2 && why.empty(); ++attempt) {
        kernel::NurbsSurface w = wall;
        if (attempt == 1 && w.Reverse(0) != kernel::Result::Ok) { why = "could not turn the section direction"; break; }
        kernel::Brep b = kernel::Brep::AssembleSweptBody(new ON_NurbsSurface(w.raw()), ends, ends, false, false, caller);
        const double vol = b.Volume();
        if (b.raw().IsValid() && b.raw().IsSolid() && vol > 0.0) {
          ctx.Print(std::string(caller) + ": Cap=Yes - closed B-rep solid with " + std::to_string(b.raw().m_F.Count()) + (ends ? " faces (wall + two planar end caps)" : " face (the closed rail has no ends: the wall closed both ways bounds the solid)") + ", exact volume " + [&] { char buf[40]; std::snprintf(buf, sizeof(buf), "%.9g", vol); return std::string(buf); }());
          return SceneObject::MakeBrep(b);
        }
        if (attempt == 1) why = "the capped result is not a valid closed solid";
      }
    }
  } catch (const std::exception& ex) {
    why = ex.what();
  }
  ctx.Print(std::string(caller) + ": Cap=Yes not applied (" + why + "); the uncapped surface is kept");
  return SceneObject::MakeSurface(wall);
}

}  // namespace

// Shared by Sweep1Command::Build above and UpdateHistory (cmd_history.cpp) -
// same "single source of truth" shape as RebuildPipe above. Unlike every
// other Rebuild* function here, this does not reproduce one of this file's
// own approximate constructions: it calls the kernel's own exact
// Brep::Sweep1 directly (rotation-minimizing-frame transport and skin), the
// real construction this file's own Sweep1Command previously never reached
// at all. `stations` is fixed at 32 (this command exposes no Style/
// tolerance option to vary it).
//
// The section's position along the rail is honoured (see the block comment
// above AnchoredSweep1): a section at the rail start is the kernel call
// unchanged; a section anywhere else along the rail is swept from where it
// sits, each side of the rail separately, and joined, so the result covers
// the rail and passes through the section exactly. The measured deviation is
// printed on every call. Cases this cannot do exactly (the kernel refuses a
// side, the halves cannot be joined, or the measured result misses the
// section) return nullopt with the reason printed, and the live command then
// takes its older section-anchored approximate path - never a silently wrong
// exact-looking surface.
//
// Always built uncapped (`cap=false`), even when the section is closed and
// planar: capping is a real Brep::Sweep1 feature, but this command has no
// Cap option of its own (unlike Pipe), and this file's existing Sweep1
// result kind is, and stays, a plain Surface (periodic when the rail is
// closed) - other scripted tests rely on that (srfedit_script.txt's SrfSeam
// case sweeps a closed circle down an OPEN rail specifically to get a
// periodic surface with a seam to move; auto-capping would silently turn it
// into a solid and break that case's own premise). So the one-face,
// untrimmed wall the kernel returns is unwrapped back down to the bare
// ON_NurbsSurface its one face already wraps (`ON_NurbsSurface::
// Cast(m_F[0].SurfaceOf())` - the same idiom used throughout this codebase
// to read a built Brep's own surface back out, e.g. dino8-kernel/tests/
// test_basic.cpp) and re-packaged as SceneObject::MakeSurface, matching
// every call site's existing object-kind expectations exactly. The kernel
// throws std::invalid_argument for a genuinely degenerate rail/section
// (zero tangent, invalid NURBS) - caught here and reported as nullopt, the
// same "degrade gracefully" contract every other Rebuild* function in this
// file already follows, so neither the live command nor UpdateHistory ever
// crashes on an input the exact path can't handle.
// Sweep1 with a per-axis scale (ScaleX / ScaleY): the kernel's Brep::Sweep1 with scale_x_schedule / scale_y_schedule, the
// section at the rail start (the kernel carries a section from the rail's start; a section elsewhere is refused with the
// reason rather than swept with a scale schedule anchored in the wrong place). Never falls back to an unscaled sweep.
std::optional<SceneObject> RebuildSweep1Scaled(CommandContext& ctx, const kernel::NurbsCurve& section, const kernel::NurbsCurve& rail,
                                               const HistoryRecord& rec) {
  try {
    const sectionfit::RailAnchor an = sectionfit::AnchorOnRail(section, rail);
    const kernel::Interval dom = rail.Domain();
    const double rail_tol = kAnchorTolerance * std::max(sectionfit::CurveSize(rail), 1e-9);
    if (rail.PointAt(an.t).DistanceTo(rail.PointAt(dom.min)) > rail_tol) {
      ctx.Print("Sweep1: per-axis scale refused - the section sits " + Percent(RailFraction(rail, an.t)) +
                " along the rail; a scaled sweep needs the section at the rail start (the schedule is measured from there)");
      return std::nullopt;
    }
    AxisScaleSchedules sc;
    ReadAxisScale(rec, rail, sc);
    std::optional<kernel::NurbsSurface> wall = SweptWall(kernel::Brep::Sweep1(section, rail, /*stations=*/32, /*cap=*/false, 0.0, 1.0, nullptr, nullptr, nullptr, sc.xp, sc.yp, &sc.axis));
    if (!wall) {
      ctx.Print("Sweep1: per-axis scale refused - the kernel sweep returned no surface");
      return std::nullopt;
    }
    std::string what;
    if (sc.xp) what += "x " + ScalePairText(rec.num.at("sx0"), rec.num.at("sx1"));
    if (sc.yp) what += std::string(what.empty() ? "" : ", ") + "y " + ScalePairText(rec.num.at("sy0"), rec.num.at("sy1"));
    ctx.Print("Sweep1: exact kernel sweep with a per-axis scale along the rail (" + what + "; x axis = " +
              (sc.axis.y == 1.0 ? "world Y" : "world X") + " in the section plane at the rail start, y perpendicular to it)");
    return CapOrKeepWall(ctx, "Sweep1", *wall, CapRequested(rec));
  } catch (const std::exception& ex) {
    ctx.Print(std::string("Sweep1: per-axis scale refused - the kernel refused (") + ex.what() + ")");
    return std::nullopt;
  }
}

std::optional<SceneObject> RebuildSweep1(CommandContext& ctx, const kernel::NurbsCurve& section,
                                          const kernel::NurbsCurve& rail, const HistoryRecord& rec) {
  if (HasAxisScale(rec)) return RebuildSweep1Scaled(ctx, section, rail, rec);
  try {
    AnchoredSweep r = AnchoredSweep1(section, rail);
    if (!r.surface) {
      ctx.Print("Sweep1: exact sweep not used - " + r.refusal + "; falling back to the approximate section-anchored sweep");
      return std::nullopt;
    }
    ctx.Print("Sweep1: " + r.how + "; " + (r.gated ? "the result reproduces the section to " : "the section's isocurve on the result deviates by ") + sectionfit::Sci(r.deviation));
    return CapOrKeepWall(ctx, "Sweep1", *r.surface, CapRequested(rec));
  } catch (const std::exception& ex) {
    ctx.Print(std::string("Sweep1: exact sweep not used - the kernel refused (") + ex.what() + "); falling back to the approximate section-anchored sweep");
    return std::nullopt;
  }
}

// Shared by Sweep2Command::Build above and UpdateHistory (cmd_history.cpp) -
// the exact same "single source of truth" shape RebuildSweep1 above already
// has, mirrored for Sweep2's own kernel::Brep::Sweep2 (two-rail frame
// transport and skin; rail2's direction is matched to rail1's INSIDE
// Brep::Sweep2 itself, so no reversal is needed here - see brep.h's own
// doc comment). Always built uncapped (`cap=false`) for the same reason
// RebuildSweep1 is: this command has no Cap option of its own, and its
// existing result kind is, and stays, a plain Surface. `stations` is fixed
// at 32 (this command exposes no Style/tolerance option to vary it). The
// kernel throws std::invalid_argument for a genuinely degenerate rail/
// section or touching rails - caught here and reported as nullopt, same
// "degrade gracefully" contract every other Rebuild* function follows.
//
// Section placement is handled exactly as in RebuildSweep1: anchored on
// rail 1 and on rail 2 (each at its own closest point to the section), the
// unchanged kernel call when the section is at the start of both rails,
// otherwise both rails are split at their anchors, each side swept and the
// halves joined (closed rail pairs are re-seamed instead). A section at the
// start of one rail but not the other has nothing to sweep on one side of
// the first rail: refused with the reason printed, falling back to the older
// approximate path.
std::optional<SceneObject> RebuildSweep2(CommandContext& ctx, const kernel::NurbsCurve& section,
                                          const kernel::NurbsCurve& rail1, const kernel::NurbsCurve& rail2,
                                          const HistoryRecord& rec) {
  try {
    AnchoredSweep r = AnchoredSweep2(section, rail1, rail2);
    if (!r.surface) {
      ctx.Print("Sweep2: exact sweep not used - " + r.refusal + "; falling back to the approximate section-anchored sweep");
      return std::nullopt;
    }
    ctx.Print("Sweep2: " + r.how + "; " + (r.gated ? "the result reproduces the section to " : "the section's isocurve on the result deviates by ") + sectionfit::Sci(r.deviation));
    return CapOrKeepWall(ctx, "Sweep2", *r.surface, CapRequested(rec));
  } catch (const std::exception& ex) {
    ctx.Print(std::string("Sweep2: exact sweep not used - the kernel refused (") + ex.what() + "); falling back to the approximate section-anchored sweep");
    return std::nullopt;
  }
}


// Sweep1 with SEVERAL cross-sections (Sweep1Command::Build and UpdateHistory):
// the kernel's exact Brep::Sweep1Sections. Every section is anchored on the
// rail, decoded in its rotation-minimizing frame, blended between neighbours and
// skinned through the stations, so each section is reproduced. The kernel's own
// report is held to the DOCUMENT tolerance (sections reproduced; the wall
// against the exact blended sections between stations, retried at up to 256
// stations). Always an uncapped Surface. Returns nullopt with `why` set when the
// kernel refuses (closed rail, a plane the rail never crosses, two sections at
// one station, seams that do not line up) or the tolerance is missed; the live
// command then takes its older sampled construction, UpdateHistory keeps the
// object as it was. Prints one measured line on success.
std::optional<SceneObject> RebuildSweep1Sections(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& sections,
                                                  const kernel::NurbsCurve& rail, const HistoryRecord& rec, std::string& why) {
  if (sections.size() < 2) { why = "needs at least two cross sections"; return std::nullopt; }
  std::vector<kernel::NurbsCurve> profiles;
  for (const kernel::NurbsCurve* c : sections) profiles.push_back(*c);
  const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  AxisScaleSchedules sc;
  ReadAxisScale(rec, rail, sc);
  for (const int stations : {32, 64, 128, 256}) {
    try {
      kernel::Brep::Sweep1SectionsReport rep;
      kernel::Brep body = kernel::Brep::Sweep1Sections(profiles, rail, stations, /*cap=*/false, &rep, sc.xp, sc.yp, (sc.xp || sc.yp) ? &sc.axis : nullptr);
      const ON_NurbsSurface* ns = body.raw().m_F.Count() > 0 ? ON_NurbsSurface::Cast(body.raw().m_F[0].SurfaceOf()) : nullptr;
      if (!ns) { why = "the kernel result has no NURBS wall"; return std::nullopt; }
      if (rep.max_section_deviation > tol) {
        why = "the kernel wall misses an input section by " + FormatDeviation(rep.max_section_deviation) + " (tolerance " + FormatNumber(tol) + ")";
        return std::nullopt;
      }
      if (rep.max_between_station_deviation > tol) {
        why = "between its " + std::to_string(rep.stations) + " stations the wall is " + FormatDeviation(rep.max_between_station_deviation) +
              " from the exact blended sections (tolerance " + FormatNumber(tol) + ")";
        continue;  // more stations
      }
      kernel::NurbsSurface k;
      k.raw() = *ns;
      ctx.Print("Sweep1: exact kernel multi-section sweep (" + std::to_string(rep.sections) + " sections, " + std::to_string(rep.stations) +
                " rail stations); measured: sections reproduced to " + FormatDeviation(rep.max_section_deviation) +
                ", between stations within " + FormatDeviation(rep.max_between_station_deviation) + " of the exact blended sections; document tolerance " +
                FormatNumber(tol));
      if (sc.xp || sc.yp) {
        ctx.Print("Sweep1: per-axis scale applied to the blended sections (" + std::string(sc.xp ? "x " + ScalePairText(rec.num.at("sx0"), rec.num.at("sx1")) : std::string()) +
                  (sc.xp && sc.yp ? ", " : "") + (sc.yp ? "y " + ScalePairText(rec.num.at("sy0"), rec.num.at("sy1")) : std::string()) +
                  "); the reproduced-section figure above is against each input section scaled by the schedule at its own station");
      }
      return CapOrKeepWall(ctx, "Sweep1", k, CapRequested(rec));
    } catch (const std::exception& e) {
      why = e.what();
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Sweep2 with SEVERAL cross-sections: the kernel's Brep::Sweep2Sections, same
// contract as RebuildSweep1Sections (each section decoded in its own station's
// two-rail frame; sections reproduced within the document tolerance and, when
// the sections touch the rails, the wall edges within it of the rails; retried at
// 64, 128, 256 and 512 stations - a long straight run into a tight bend needs 256).
std::optional<SceneObject> RebuildSweep2Sections(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& sections,
                                                  const kernel::NurbsCurve& rail1, const kernel::NurbsCurve& rail2,
                                                  const HistoryRecord& rec, std::string& why) {
  if (sections.size() < 2) { why = "needs at least two cross sections"; return std::nullopt; }
  std::vector<kernel::NurbsCurve> profiles;
  for (const kernel::NurbsCurve* c : sections) profiles.push_back(*c);
  const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
  for (const int stations : {32, 64, 128, 256, 512}) {
    try {
      kernel::Brep::Sweep2SectionsReport rep;
      kernel::Brep body = kernel::Brep::Sweep2Sections(profiles, rail1, rail2, stations, &rep);
      const ON_NurbsSurface* ns = body.raw().m_F.Count() > 0 ? ON_NurbsSurface::Cast(body.raw().m_F[0].SurfaceOf()) : nullptr;
      if (!ns) { why = "the kernel result has no NURBS wall"; return std::nullopt; }
      const bool touches = rep.rails_on_boundary && rep.section_rail_gap <= tol;
      const double rail_dev = std::max(rep.max_rail1_deviation, rep.max_rail2_deviation);
      if (rep.max_section_deviation > tol) {
        why = "the kernel wall misses an input section by " + FormatDeviation(rep.max_section_deviation) + " (tolerance " + FormatNumber(tol) + ")";
        return std::nullopt;
      }
      if (touches && rail_dev > tol) {
        why = "the wall edges are " + FormatDeviation(rail_dev) + " from the rails at " + std::to_string(rep.stations) + " stations (tolerance " + FormatNumber(tol) + ")";
        continue;  // more stations
      }
      kernel::NurbsSurface k;
      k.raw() = *ns;
      ctx.Print("Sweep2: exact kernel multi-section sweep (" + std::to_string(rep.sections) + " sections, " + std::to_string(rep.stations) +
                " rail stations); measured: sections reproduced to " + FormatDeviation(rep.max_section_deviation) +
                (touches ? ", wall edges within " + FormatDeviation(rail_dev) + " of the rails"
                         : ", sections do not touch both rails (largest gap " + FormatDeviation(rep.section_rail_gap) + ") so no rail-edge claim is made") +
                "; document tolerance " + FormatNumber(tol));
      return CapOrKeepWall(ctx, "Sweep2", k, CapRequested(rec));
    } catch (const std::exception& e) {
      why = e.what();
      return std::nullopt;
    }
  }
  return std::nullopt;
}


// OffsetSrf's construction for ONE object - shared by OffsetSrfCommand and
// UpdateHistory. Reads num: d (signed distance, FlipAll already applied) and
// solid (1/0). A surface goes through the kernel's unified NurbsSurface::Offset
// (exact analytic for plane/sphere/cylinder/cone/torus, tolerance-controlled
// refit otherwise; the first-order Greville-normal fit only when a freeform
// refit refuses, with a warning), a convex planar solid through OffsetSolidConvexPlanar (exact
// B-rep), anything else through the mesh offset. Layer is the caller's.
std::optional<SceneObject> RebuildOffsetSrf(CommandContext& ctx, const SceneObject& o, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const double d = num("d");
  const bool solid_ = num("solid") != 0.0;
  if (d == 0) return std::nullopt;
  SceneObject n;
  if (o.kind == ObjectKind::Surface && o.surface) {
    // The kernel's unified NurbsSurface::Offset(): the EXACT rational closed form when the surface is a
    // plane / sphere / cylinder / cone / torus (on the input's own parametrization and orientation), the
    // tolerance-controlled OffsetRefit() otherwise (non-rational, deviation measured against the true
    // parallel-surface locus). The command output states which path ran and a deviation measured HERE,
    // independently of the kernel's own report: the worst distance from the result to S(u,v) + d*N(u,v) on a
    // 9x9 grid of the input's domain. An analytic surface whose true offset folds through its own axis is
    // refused (the kernel never substitutes a fit for it); a freeform refusal keeps the old first-order
    // Greville-normal approximation, with its warning, exactly as before.
    kernel::NurbsSurface off;
    kernel::NurbsSurface::OffsetInfo info;
    const double tol_doc = ctx.Settings().absolute_tolerance;
    bool offset_ok = false;
    try {
      offset_ok = o.surface->Offset(d, off, tol_doc, &info) == kernel::Result::Ok;
    } catch (const std::exception& ex) {
      info.refusal_reason = ex.what();
    }
    if (offset_ok) {
      double worst = 0.0;
      const kernel::Interval du = o.surface->Domain(0), dv = o.surface->Domain(1);
      for (int i = 0; i <= 8; ++i) {
        for (int j = 0; j <= 8; ++j) {
          const double u = du.min + (du.max - du.min) * (0.03 + 0.94 * i / 8.0), v = dv.min + (dv.max - dv.min) * (0.03 + 0.94 * j / 8.0);
          const Point3d want = o.surface->PointAt(u, v) + d * o.surface->NormalAt(u, v);
          worst = std::max(worst, off.PointAt(u, v).DistanceTo(want));
        }
      }
      ctx.Print("OffsetSrf: object " + std::to_string(o.id) + " - " +
                 (info.exact ? std::string("exact analytic offset (closed-form rational surface on the input's own parametrization")
                             : "tolerance-controlled refit (non-rational; kernel-measured deviation " + FormatDeviation(info.achieved_deviation) + " <= tolerance " + FormatNumber(info.tolerance_used)) +
                 "); measured deviation from the true offset locus " + FormatDeviation(worst));
    } else if (info.refusal_reason.rfind("recognised plane", 0) == 0) {
      ctx.Warn("OffsetSrf: object " + std::to_string(o.id) + " not offset - " + info.refusal_reason);
      return std::nullopt;
    } else {
      ctx.Warn("OffsetSrf: object " + std::to_string(o.id) + " could not be refit to tolerance " + FormatNumber(tol_doc) +
                " (fold-through-curvature guard, or this surface's own offset needs more control points than "
                "the fit's own ceiling) - using the first-order Greville-normal approximation instead");
      off = OffsetNurbs(*o.surface, d);
    }
    if (solid_) {
      const int nu = std::max(8, 2 * o.surface->CVCountU()), nv = std::max(8, 2 * o.surface->CVCountV());
      n = SceneObject::MakeMesh(ShellBetween(MeshFromRows(SurfaceRows(*o.surface, nu, nv), false, false), MeshFromRows(SurfaceRows(off, nu, nv), false, false)));
    } else {
      n = SceneObject::MakeSurface(off);
    }
  } else {
    // A closed polysurface: try the kernel's exact offset first
    // (TryExactOffsetSolid - convex planar solids, whole spheres / tori, plain
    // capped cylinders; Solid=Yes as well for convex planar solids and whole
    // spheres / tori). Fails open to the mesh offset below, printing the
    // kernel's reason; Exact=No keeps the mesh path.
    const auto ex_it = rec.num.find("exact");
    const bool exact_ = ex_it == rec.num.end() || ex_it->second != 0.0;
    if (exact_ && o.kind == ObjectKind::Brep && o.brep) {
      std::string why;
      if (std::optional<kernel::Brep> ex = TryExactOffsetSolid(*o.brep, d, solid_, why)) return SceneObject::MakeBrep(*ex);
      if (!why.empty() && o.brep->raw().IsSolid())
        ctx.Print("OffsetSrf: object " + std::to_string(o.id) + " - exact B-rep offset not used (" + why + "); using the mesh offset");
    }
    std::optional<kernel::Mesh> m = MeshOf(o, 0.01);
    if (!m || m->FaceCount() == 0) { ctx.Warn("Object " + std::to_string(o.id) + " could not be meshed; skipped"); return std::nullopt; }
    kernel::Mesh base = *m;
    if (base.IsClosedManifold() && base.Volume() < 0) base = base.FlipNormals();
    kernel::Mesh off = OffsetMesh(base, d);
    if (solid_) {
      if (base.IsClosedManifold()) off = Outward(kernel::Mesh::MergeAndWeld({d > 0 ? base.FlipNormals() : base, d > 0 ? off : off.FlipNormals()}, 1e-9));
      else off = ShellBetween(base, off);
    }
    n = SceneObject::MakeMesh(off);
  }
  return n;
}

// Shell's plain construction (closed solid, one uniform wall thickness, no
// opened or individually-thickened faces), shared by ShellCommand and
// UpdateHistory. Reads num: t (> 0), exact (1/0, absent means 1). The kernel's
// exact ShellConvexPlanar / whole sphere or torus shell first; otherwise the
// mesh construction (outer minus the inward vertex-normal offset). The result
// kind therefore follows the source's current shape. Layer is the caller's.
std::optional<SceneObject> RebuildShell(CommandContext& ctx, const SceneObject& o, const HistoryRecord& rec) {
  const auto t_it = rec.num.find("t");
  const double t = t_it == rec.num.end() ? 0.0 : t_it->second;
  const auto ex_it = rec.num.find("exact");
  const bool exact = ex_it == rec.num.end() || ex_it->second != 0.0;
  if (!(t > 0)) return std::nullopt;
  if (exact && o.kind == ObjectKind::Brep && o.brep) {
    static const std::vector<int> kNoRemoveExact;
    static const std::map<int, double> kNoOverridesExact;
    std::string why;
    if (std::optional<kernel::Brep> ex = TryExactShell(o, kNoRemoveExact, kNoOverridesExact, t, why)) return SceneObject::MakeBrep(*ex);
    if (!why.empty() && o.brep->raw().IsSolid())
      ctx.Print("Shell: object " + std::to_string(o.id) + " - exact B-rep shell not used (" + why + "); using the mesh construction");
  }
  std::optional<kernel::Mesh> m = MeshOf(o, 0.005);
  if (!m || !m->IsClosedManifold()) { ctx.Warn("Object " + std::to_string(o.id) + " is not a closed solid; skipped"); return std::nullopt; }
  kernel::Mesh outer = Outward(*m);
  kernel::Mesh inner = OffsetMesh(outer, -t);
  try {
    kernel::Mesh r = kernel::BooleanCombine(outer, inner, kernel::BooleanOp::Difference);
    if (r.FaceCount() == 0) { ctx.Warn("Shell: object " + std::to_string(o.id) + " is thinner than the thickness; skipped"); return std::nullopt; }
    return SceneObject::MakeMesh(r);
  } catch (const std::exception& ex) {
    ctx.Warn("Shell: object " + std::to_string(o.id) + " rejected by the mesh kernel (" + ex.what() + "); skipped");
    return std::nullopt;
  }
}

// ExtrudeCrvAlongCrv's construction: exact translational sum surface.
std::optional<SceneObject> RebuildExtrudeAlongCrv(CommandContext&, const kernel::NurbsCurve& profile, const kernel::NurbsCurve& path, const HistoryRecord&) {
  ON_SumSurface ss;
  kernel::NurbsSurface k;
  if (ss.Create(profile.raw(), path.raw()) && SurfaceFromON(ss, k)) return SceneObject::MakeSurface(k);
  return std::nullopt;
}

// ExtrudeCrvTapered's construction for ONE profile: the kernel's
// Brep::ExtrudeTapered. Reads num: dx,dy,dz, draft (radians), solid (1/0). The
// caps are wanted only for a closed planar profile (read fresh from the curve).
std::optional<SceneObject> RebuildExtrudeTapered(CommandContext& ctx, const kernel::NurbsCurve& c, const HistoryRecord& rec) {
  auto num = [&](const char* k) { auto it = rec.num.find(k); return it == rec.num.end() ? 0.0 : it->second; };
  const Vector3d direction(num("dx"), num("dy"), num("dz"));
  const double draft_rad = num("draft");
  const bool solid = num("solid") != 0.0;
  ON_Plane plane;
  const bool want_solid = solid && c.IsClosed() && c.raw().IsPlanar(&plane, ctx.Settings().absolute_tolerance);
  try {
    kernel::Brep b = kernel::Brep::ExtrudeTapered(c, direction, draft_rad, /*cap=*/want_solid);
    if (want_solid) {
      if (!b.raw().IsSolid()) {
        ctx.Warn("ExtrudeCrvTapered: caps could not be joined; result is an open polysurface");
      } else {
        BrepMeshOptions opt;
        opt.chord_tolerance = 0.05;
        if (MeshBrepClosed(b.raw(), opt).Volume() < 0) b.raw().Flip();  // outward normals
      }
      return SceneObject::MakeBrep(b);
    }
    const ON_Surface* s = b.raw().m_F.Count() > 0 ? b.raw().m_F[0].SurfaceOf() : nullptr;
    kernel::NurbsSurface k;
    if (!s || !SurfaceFromON(*s, k)) { ctx.Warn("ExtrudeCrvTapered: could not build a surface for this profile"); return std::nullopt; }
    return SceneObject::MakeSurface(k);
  } catch (const std::exception& ex) {
    ctx.Warn(std::string("ExtrudeCrvTapered: ") + ex.what());
    return std::nullopt;
  }
}

// NetworkSrf/EdgeSrf, 3 or 4 curves (history rebuild entry point).
std::optional<SceneObject> RebuildNetworkSrfLoop(CommandContext& ctx, const std::vector<const kernel::NurbsCurve*>& curves, const HistoryRecord&, std::string* note) {
  std::vector<kernel::NurbsCurve> copy;
  for (const kernel::NurbsCurve* c : curves) copy.push_back(*c);
  std::string n;
  std::optional<SceneObject> built = BuildNetworkSrfLoop(ctx, copy, n);
  if (note) *note = n;
  return built;
}

// ---------------------------------------------------------------------------
// continuity_request.h: option parsing, the adjacent-edge pick, and the
// MeasureEdgeContinuity command.
// ---------------------------------------------------------------------------

std::string Sig3(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3g", v);
  return buf;
}

std::string FormatEdgeMeasure(const kernel::NurbsSurface::EdgeContinuityMeasure& m) {
  return "position gap " + Sig3(m.max_position_gap) + ", tangent angle " + Sig3(m.max_tangent_angle) + " rad (" + Sig3(m.max_tangent_angle * 180.0 / ON_PI) +
         " deg), cross-edge curvature difference " + Sig3(m.max_cross_curvature_diff) + ", Gaussian curvature difference " + Sig3(m.max_gaussian_diff);
}

bool ParseContinuityToken(const std::string& token, ContinuityRequest& r, std::string& err) {
  const size_t eq = token.find('=');
  if (eq == std::string::npos) return false;
  std::string name = token.substr(0, eq), value = token.substr(eq + 1);
  for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string lv = value;
  for (char& c : lv) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (name == "continuity") {
    if (lv == "none") r.requested = false;
    else if (lv == "position" || lv == "g0") { r.requested = true; r.continuity = kernel::MatchContinuity::Position; }
    else if (lv == "tangency" || lv == "tangent" || lv == "g1") { r.requested = true; r.continuity = kernel::MatchContinuity::Tangent; }
    else if (lv == "curvature" || lv == "g2") { r.requested = true; r.continuity = kernel::MatchContinuity::Curvature; }
    else err = "Continuity must be None, Position (G0), Tangency (G1) or Curvature (G2), not '" + value + "'";
    return true;
  }
  if (name == "angletol" || name == "curvtol") {
    double d = 0.0;
    if (!ParseNumber(value, d) || !(d > 0.0) || !std::isfinite(d)) {
      err = std::string(name == "angletol" ? "AngleTol" : "CurvTol") + " must be a positive number (" + (name == "angletol" ? "degrees" : "1/length") + "), not '" + value + "'";
    } else if (name == "angletol") {
      r.angle_tol = d * ON_PI / 180.0;
    } else {
      r.curv_tol = d;
    }
    return true;
  }
  return false;
}

std::optional<AdjacentEdge> PickAdjacentEdge(CommandContext& ctx, Point3d p, const std::vector<ObjectId>& exclude, std::string& why) {
  std::optional<AdjacentEdge> best;
  double best_face_d = std::numeric_limits<double>::max();
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (std::find(exclude.begin(), exclude.end(), o.id) != exclude.end()) continue;
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::vector<kernel::NurbsSurface> faces;
    if (o.kind == ObjectKind::Surface && o.surface) {
      faces.push_back(*o.surface);
    } else if (o.kind == ObjectKind::Brep && o.brep) {
      const ON_Brep& b = o.brep->raw();
      for (int f = 0; f < b.m_F.Count(); ++f) {
        kernel::NurbsSurface ks;
        const ON_Surface* s = b.m_F[f].SurfaceOf();
        if (s && SurfaceFromON(*s, ks)) faces.push_back(std::move(ks)); else faces.emplace_back();
      }
    }
    for (size_t f = 0; f < faces.size(); ++f) {
      if (!faces[f].raw().IsValid()) continue;
      const double d = faces[f].ClosestPoint(p, 40, 40).DistanceTo(p);
      if (d < best_face_d) {
        best_face_d = d;
        AdjacentEdge e;
        e.id = o.id;
        e.face = (o.kind == ObjectKind::Brep) ? static_cast<int>(f) : 0;
        e.surface = faces[f];
        best = std::move(e);
      }
    }
  }
  if (!best) { why = "no visible surface or polysurface to take an edge from"; return std::nullopt; }
  // Nearest of the four isoparametric edges of that surface (65 samples each).
  const ON_NurbsSurface& s = best->surface.raw();
  double best_edge_d = std::numeric_limits<double>::max();
  for (int dir = 0; dir < 2; ++dir) {
    for (int mn = 0; mn < 2; ++mn) {
      const ON_Interval fixed = s.Domain(dir), run = s.Domain(1 - dir);
      const double fv = mn ? fixed.Min() : fixed.Max();
      for (int k = 0; k <= 64; ++k) {
        const double t = run.Min() + run.Length() * k / 64.0;
        const ON_3dPoint q = dir == 0 ? s.PointAt(fv, t) : s.PointAt(t, fv);
        const double d = q.DistanceTo(p);
        if (d < best_edge_d) { best_edge_d = d; best->fixed_direction = dir; best->at_min = (mn != 0); }
      }
    }
  }
  best->pick_distance = best_edge_d;
  return best;
}

namespace {

// MeasureEdgeContinuity: pick two surface edges; report the measured G0/G1/G2 between them.
class MeasureEdgeContinuityCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"AngleTol", Sig3(req_.angle_tol * 180.0 / ON_PI), {}, true, false}, {"CurvTol", Sig3(req_.curv_tol), {}, true, false}};
    WantPoint("Click the first surface edge (the surface to measure)");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    std::string err;
    ParseContinuityToken(n + "=" + v, req_, err);
    if (!err.empty()) { ctx.Warn("MeasureEdgeContinuity: " + err); return; }
    options[0].value = Sig3(req_.angle_tol * 180.0 / ON_PI);
    options[1].value = Sig3(req_.curv_tol);
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::string why;
    std::optional<AdjacentEdge> e = PickAdjacentEdge(ctx, p, {}, why);
    if (!e) { ctx.Warn("MeasureEdgeContinuity: " + why); return; }
    if (!first_) {
      first_ = std::move(e);
      ctx.Print("MeasureEdgeContinuity: first edge = " + first_->Describe());
      WantPoint("Click the second surface edge (the edge to measure along)");
      return;
    }
    if (e->id == first_->id && e->face == first_->face && e->fixed_direction == first_->fixed_direction && e->at_min == first_->at_min) {
      ctx.Warn("MeasureEdgeContinuity: that is the first edge again; click a different edge");
      return;
    }
    Run(ctx, *first_, *e);
    Finish();
  }

 private:
  void Run(CommandContext& ctx, const AdjacentEdge& a, const AdjacentEdge& b) {
    kernel::NurbsSurface::EdgeContinuityMeasure m;
    kernel::Result r = kernel::Result::Failed;
    try {
      r = a.surface.MeasureEdgeContinuity(b.surface, b.fixed_direction, b.at_min, 97, m);
    } catch (const std::exception& ex) {
      ctx.Warn(std::string("MeasureEdgeContinuity: ") + ex.what());
      return;
    }
    if (r != kernel::Result::Ok) { ctx.Warn("MeasureEdgeContinuity: the kernel could not evaluate one of the two surfaces at a probe"); return; }
    const double tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 0.001;
    const bool g0 = m.max_position_gap <= tol;
    const bool g1 = g0 && m.max_tangent_angle <= req_.angle_tol;
    const bool g2 = g1 && m.max_cross_curvature_diff <= req_.curv_tol;
    ctx.Print("MeasureEdgeContinuity: " + a.Describe() + " against " + b.Describe() + ", MEASURED at " + std::to_string(m.samples) +
              " probes evenly along the second edge (each against the first surface's own closest point): " + FormatEdgeMeasure(m));
    ctx.Print("MeasureEdgeContinuity: G0 (gap <= " + FormatNumber(tol) + ") " + (g0 ? "met" : "NOT met") + "; G1 (and angle <= " + Sig3(req_.angle_tol) + " rad) " +
              (g1 ? "met" : "NOT met") + "; G2 (and cross-edge curvature difference <= " + Sig3(req_.curv_tol) + ") " + (g2 ? "met" : "NOT met") +
              "; continuity achieved: " + (g2 ? "G2" : g1 ? "G1" : g0 ? "G0" : "none (the edges do not meet within the tolerance)"));
  }
  ContinuityRequest req_;
  std::optional<AdjacentEdge> first_;
};

}  // namespace

void RegisterSurfaceCommands(CommandEngine& e) {
  Reg(e, "Sweep1", Make<Sweep1Command>(), CommandStatus::Implemented, "A single cross section calls the kernel's exact Brep::Sweep1 (rotation-minimizing-frame transport and skin; still an uncapped surface, periodic when the rail is closed, same as before) and records History (History On) for UpdateHistory to rebuild against the rail/section's current shape; two or more cross sections call the kernel's exact Brep::Sweep1Sections (each section anchored on the rail - a closed planar one where the rail crosses its plane - decoded in its rotation-minimizing frame, blended between neighbours and skinned through the stations: every section reproduced exactly, the wall measured against the exact blended sections between stations and held to the document tolerance, retried at up to 256 stations; an uncapped surface; closed cross sections must already share a seam and direction, the kernel refuses seams more than 60 degrees apart; with History On it records {rail, section 1, section 2, ...} and rebuilds when the rail or a section is edited, a rebuild the kernel refuses keeping the old wall with the reason printed); it falls back, with the reason printed, to the old sampled approximation (24 samples per section, fitted to a degree-3 surface) when the kernel refuses (closed rail, a section plane the rail never crosses or contains, two sections at one station, mismatched seams) or misses the document tolerance. Cap=Yes (default No) turns the sweep of a closed planar cross section into a closed B-rep solid with the kernel's own capping (two planar end caps; the section may sit anywhere along the rail and several cross sections are capped too; recorded for History); an open or non-planar section, or a fallback construction, keeps the uncapped surface and prints why. A closed rail that is an exact planar circle is built as the section revolved once about the rail's axis (Brep::Revolve: an exact rational surface, e.g. a true torus, measured against the revolved section and printed), and with Cap=Yes as a closed one-face solid; any other closed rail keeps the kernel's periodic skin.");
  Reg(e, "Sweep2", Make<Sweep2Command>(), CommandStatus::Implemented, "A single cross section calls the kernel's exact Brep::Sweep2 (two-rail frame transport and skin; still an uncapped surface, same as before) and records History (History On) for UpdateHistory to rebuild against the rails/section's current shape. Two or more cross sections call the kernel's Brep::Sweep2Sections (each section decoded in its own station's two-rail frame, blended between neighbours and skinned through the stations: every section reproduced, wall edges within the document tolerance of the rails when the sections touch them, measured and printed; closed cross sections must already share a seam and direction, the kernel does not align them; with History On it records {rail 1, rail 2, section 1, section 2, ...} and rebuilds when a rail or a section is edited, a rebuild the kernel refuses keeping the old wall with the reason printed); it falls back, with the reason printed, to the old approximated two-rail-scaled sweep fitted to a degree-3 surface when the kernel refuses (crossing section anchors, rails that touch at an interior station, a section where the rails touch) or misses the document tolerance; two closed rails give a cyclic sweep whose wall is closed round the ring (seam at the first section, sections ordered round both rails), and rails meeting at an end give a wall pinched to a point there. Cap=Yes (default No) caps a closed planar cross section's sweep into a closed B-rep solid (single section, or the multi-section wall) with the kernel's own capping; anything that cannot be capped keeps the surface and prints why.");
  Reg(e, "NetworkSrf", []() -> std::unique_ptr<Command> {
    return std::make_unique<ContinuityPickCommand>("NetworkSrf", "Select curves in network (2, 3 or 4)", 2,
        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<std::string>&) { NetworkSrf(ctx, ids); },
        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ContinuityRequest& req, const AdjacentEdge& edge, const std::vector<std::string>&) { NetworkSrfContinuity(ctx, ids, req, edge); });
  }, CommandStatus::Implemented, "Two curves give an exact ruled surface. Three or four curves that close into a loop within the document tolerance call the kernel's exact Coons patch (CoonsPatchTriangular / CoonsPatch: all boundaries reproduced exactly, corners within tolerance closed first, any order or direction, measured boundary deviation printed); a set that does not close, or that the kernel refuses, prints why and falls back to the old bilinear Coons patch fitted as a degree-3 surface. Five or more curves that close into a loop within the document tolerance are chained (any order or direction), joined into one loop and trimmed out of the kernel's least-squares NurbsSurface::FitPatch surface as one B-rep face (approximate, not an interpolating Coons patch: the worst deviation from the curves and the trimmed edge's deviation are printed and a miss of the tolerance is flagged); a set that does not close, or whose loop folds over its best-fit plane, prints why and uses the first 4 curves as before. Four or more curves that CROSS in their interiors form a curve network: the kernel (NurbsSurface::FitCurveNetwork) sorts them into two directions by where they meet, requires every curve to meet every curve of the other direction within the document tolerance, fits one NURBS surface (cubic where the net allows) through the curves and the meetings, and prints the measured distance of EVERY input curve from it (approximate least-squares fit over the curves' bounding rectangle, untrimmed); a network that misses the tolerance, whose curves do not all meet, or whose order is not a regular grid prints why and the older behaviour runs. Continuity=Position|Tangency|Curvature (G0/G1/G2, with AngleTol= in degrees and CurvTol= in 1/length; none given = everything above unchanged) asks for a click near an edge of an existing surface and, for a curve network of four or more curves, calls the kernel's continuity-aware FitCurveNetwork: the input curve lying on that edge is fitted with the adjacent surface's tangent plane (and second fundamental form) as a heavily weighted constraint, the finished surface is MEASURED along the edge (position gap, tangent angle, cross-edge and Gaussian curvature difference) and printed, and the request is refused with the kernel's reason when G0 is not met, the adjacent plane is too steep, or the measured angle / curvature / curve deviation exceeds the tolerance. A 2-curve or loop selection has no continuity option (refused with that reason). A continuity result is not recorded in History (the reason is printed).");
  Reg(e, "Patch", []() -> std::unique_ptr<Command> {
    return std::make_unique<ContinuityPickCommand>("Patch", "Select curves and points to fit a surface through", 1,
        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const std::vector<std::string>&) { Patch(ctx, ids); },
        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ContinuityRequest& req, const AdjacentEdge& edge, const std::vector<std::string>&) { PatchContinuity(ctx, ids, req, edge); });
  }, CommandStatus::Implemented, "Data within the document tolerance of their own best-fit plane keep the planar patch (an exact plane trimmed by a single closed planar curve, or a least-squares rectangle). Non-planar data call the kernel's least-squares NurbsSurface::FitPatch (boundary samples of the selected curves and the selected points, a bicubic-or-lower net grown until the worst data deviation is within the document tolerance, measured and printed; a surface that misses tolerance is flagged NOT met) and the closed curve enclosing the most area is trimmed out of it by Brep::TrimmedPatchFace, every other closed curve wholly inside it becoming a hole of the same face (overlapping or crossing curves are reported and leave the surface untrimmed); if the kernel refuses or the fit is no better than the flat plane the reason is printed and the planar construction runs. With History On the non-planar result records every selected curve and point (in selection order) and the fit (and trim) is re-run when one is edited; a rebuild that no longer applies (the data became flat, a source deleted, the fit failing) keeps the old patch with the reason printed. Continuity=Position|Tangency|Curvature (G0/G1/G2, with AngleTol= in degrees and CurvTol= in 1/length; none given = everything above unchanged) asks for a click near an edge of an existing surface and calls the kernel's continuity-aware FitPatch: the selected curve lying on that edge supplies ordered edge samples whose adjacent tangent plane (and second fundamental form) enter the least-squares fit as heavily weighted constraints; the finished UNTRIMMED surface is MEASURED between the samples (position gap, tangent angle, cross-edge and Gaussian curvature difference) and printed, and the request is refused with the kernel's reason (G0 not met, adjacent plane too steep, measured angle / curvature above tolerance, data deviation above the document tolerance). A continuity result is not recorded in History (the reason is printed).");
  Reg(e, "MeasureEdgeContinuity", Make<MeasureEdgeContinuityCommand>(), CommandStatus::Implemented, "Click two surface (or polysurface face) edges; prints the MEASURED position gap, tangent-plane angle, cross-edge normal curvature difference and Gaussian curvature difference at 97 probes evenly spaced along the second edge, each against the first surface's own closest point (kernel NurbsSurface::MeasureEdgeContinuity), then which of G0 (gap within the document tolerance), G1 (and angle within AngleTol, degrees, default 0.0573 = 0.001 rad) and G2 (and curvature difference within CurvTol, default 0.01 per length) are met. Pure measurement: nothing is modified. The edge is the nearest isoparametric edge of the face's underlying surface, so a trimmed face's trim edge is not measured.");
  Reg(e, "Pipe", Make<PipeCommand>(), CommandStatus::Implemented, "Single radius. Cap=Yes on an open rail calls the kernel's exact Brep::Pipe (real flat-disc end caps, exact rational cylinder wall along a straight rail); a closed rail still gives a closed mesh tube. Cap=No gives a periodic NURBS surface (circle approximated by a cubic). Records History (History On, see cmd_history.cpp) for UpdateHistory to rebuild against the rail curve's current shape.");
  Reg(e, "OffsetSrf", Make<OffsetSrfCommand>(), CommandStatus::Implemented, "Surfaces: control points offset along Greville normals (exact for planes). Polysurfaces and meshes are offset as meshes along vertex normals; Solid=Yes closes the shell as a mesh.");
  Reg(e, "Shell", Make<ShellCommand>(), CommandStatus::Implemented, "Hollows a closed solid. With no face clicked to remove/open and no per-face thickness override, a polysurface whose faces are all planar and convex (a box or other simple polyhedron) now calls the kernel's exact ShellConvexPlanar first, giving a real B-rep result (exact volume, no tessellation) instead of a mesh; it fails open to the existing mesh construction (outer minus inward vertex-normal offset) for a curved or non-convex solid, or a thickness beyond that shape's own offset feasibility. Optionally click face(s) of a polysurface solid to remove/open before entering thickness (Enter with none picked keeps the old fully-closed behavior): the picked face(s) are dropped from the outer surface, the remainder gets the inward offset, and a rim mesh connects the two boundary loops - a real open shell (cup/case) for a single face, or a group of mutually-adjacent faces, on a simple box-like solid; a selection that would leave a non-manifold or multi-piece remainder is rejected with a warning rather than producing bad geometry, and mesh-only solids (no polysurface to pick faces on) still only support the fully-closed form. After face removal you can also click additional face(s) and type a thickness for each (repeat, then Enter for the default Thickness on the rest): every kept face's vertices then solve to the exact intersection of its own neighbours' offset planes, so two faces with different thickness meet in a real mitered corner rather than an average - numerically verified for a box (see tests/surface_script.txt) and, by the same plane-intersection algebra, correct for any solid whose kept faces are all planar (prisms and other polyhedra). If any kept face is curved, per-face overrides are detected and dropped for that solid (warned), falling back to the single default Thickness everywhere on it rather than applying an unverified per-triangle offset to a curved surface.");
  Reg(e, "ExtrudeCrvAlongCrv", Make<ExtrudeAlongCommand>(), CommandStatus::Implemented, "Exact translational sweep (sum surface); the profile is not rotated along the path.");
  Reg(e, "ExtrudeCrvTapered", Make<ExtrudeTaperedCommand>(), CommandStatus::Implemented, "Ruled surface to a copy of the profile scaled about its centroid by the draft angle (exact for circles, approximate corners).");
  Reg(e, "Project", Make<ProjectCommand>(false), CommandStatus::Implemented, "Projects onto the target's render mesh along a picked direction (two points), or the active CPlane normal on Enter; result curves are refit through the projected samples.");
  Reg(e, "Pull", Make<ProjectCommand>(true), CommandStatus::Implemented, "Pulls to the closest point on the target; result curves are refit through the pulled samples.");
}

}  // namespace dino8::app
