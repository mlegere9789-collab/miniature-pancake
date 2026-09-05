// Solid tools: holes (MakeHole, PlaceHole, RoundHole, RevolvedHole, hole
// arrays), planar curve booleans (CurveBoolean, PlanarUnion,
// PlanarDifference, CreateRegions), CreateSolid, Merge, Clash, cage editing
// (Cage / CageEdit / ReleaseFromCage / SelCaptives / SelControls with a
// Bernstein (Bezier) free-form deformation lattice), Flow / FlowAlongSrf,
// ScaleByPlane / ScalePositions and the OrientOnCrv / OrientOnSrf family.
//
// OpenNURBS ships no B-rep booleans, so every hole and region operation
// runs on closed meshes through the Manifold-backed kernel booleans and the
// results are mesh objects; each command says so on the command line.
#include "commands/cmd_common.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace dino8::app {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool ParseNumber(const std::string& t, double& v) {
  char* e = nullptr;
  v = std::strtod(t.c_str(), &e);
  return e && e != t.c_str() && *e == 0;
}

OptionSpec Toggle(const char* name, bool on) { return {name, on ? "Yes" : "No", {"Yes", "No"}, false, true}; }
OptionSpec Numeric(const char* name, double v) { return {name, FormatNumber(v), {}, true, false}; }
OptionSpec Choice(const char* name, const char* value, std::vector<std::string> choices) { return {name, value, std::move(choices), false, false}; }

std::string Id(ObjectId id) { return std::to_string(id); }

// Outward-oriented copy of a closed mesh (Manifold wants positive volume).
kernel::Mesh Outward(kernel::Mesh m) {
  if (m.FaceCount() > 0 && m.Volume() < 0) m = m.FlipNormals();
  return m;
}

double Diagonal(const kernel::Mesh& m) {
  const kernel::BoundingBox bb = m.GetBoundingBox();
  return (bb.max - bb.min).Length();
}

// Closed mesh of a brep built on the fly (deletes the brep).
std::optional<kernel::Mesh> ClosedMeshOfBrep(ON_Brep* b, double tol) {
  if (!b) return std::nullopt;
  BrepMeshOptions opt;
  opt.chord_tolerance = tol;
  kernel::Mesh m = MeshBrepClosed(*b, opt);
  delete b;
  if (m.FaceCount() == 0 || !m.IsClosedManifold()) return std::nullopt;
  return Outward(m);
}

// Closed slab: the planar region bounded by `c` (lying in `plane`) swept by
// `offset`. The face is meshed with the app's own trimmed-face mesher and
// closed with ExtrudeCappedSolid; ON_BrepExtrudeFace is the fallback.
std::optional<kernel::Mesh> RegionSlab(const ON_Plane& plane, const ON_Curve& c, Vector3d offset, double tol) {
  ON_Brep* b = ON_BrepTrimmedPlane(plane, c);
  if (!b) return std::nullopt;
  BrepMeshOptions opt;
  opt.chord_tolerance = tol;
  try {
    std::vector<kernel::Mesh> faces = MeshBrepFaces(*b, opt);
    if (!faces.empty() && faces[0].FaceCount() > 0) {
      kernel::Mesh cap = faces.size() == 1 ? faces[0] : kernel::Mesh::MergeAndWeld(faces, tol);
      kernel::Mesh m = kernel::Mesh::ExtrudeCappedSolid(cap, offset);
      if (m.FaceCount() > 0 && m.IsClosedManifold()) { delete b; return Outward(m); }
    }
  } catch (const std::exception&) {
    // fall through to the brep extrusion
  }
  ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + offset));
  if (ON_BrepExtrudeFace(*b, 0, path, true) < 0) { delete b; return std::nullopt; }
  return ClosedMeshOfBrep(b, tol);
}

struct Solid {
  ObjectId id;
  kernel::Mesh mesh;
};

// Closed meshes of the selected solids (breps, surfaces, meshes, SubDs).
std::vector<Solid> Solids(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label) {
  std::vector<Solid> out;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
    if (!m || m->FaceCount() == 0 || !m->IsClosedManifold()) { ctx.Warn(label + ": object " + Id(id) + " is not a closed solid; skipped"); continue; }
    out.push_back({id, Outward(*m)});
  }
  if (out.empty()) ctx.Warn(label + ": no closed solids selected");
  return out;
}

// Replaces object `id` with mesh `m` (attributes kept). Returns the holder's id.
ObjectId ReplaceWithMesh(CommandContext& ctx, ObjectId id, const kernel::Mesh& m) {
  SceneObject* o = ctx.Doc().Find(id);
  if (!o) return kNoObject;
  if (o->kind == ObjectKind::Mesh) { *o->mesh = m; o->InvalidateDisplay(); return id; }
  SceneObject n = SceneObject::MakeMesh(m);
  n.layer_index = o->layer_index;
  n.name = o->name;
  n.color = o->color;
  n.color_by_layer = o->color_by_layer;
  n.user_text = o->user_text;
  n.selected = o->selected;
  ctx.Doc().Remove(id);
  return ctx.Doc().Add(std::move(n));
}

std::optional<kernel::Mesh> Combine(CommandContext& ctx, const kernel::Mesh& a, const kernel::Mesh& b, kernel::BooleanOp op, const std::string& label) {
  try {
    return kernel::BooleanCombine(a, b, op);
  } catch (const std::exception& ex) {
    ctx.Warn(label + ": boolean failed: " + ex.what());
    return std::nullopt;
  }
}

std::string MeshSummary(const kernel::Mesh& m) {
  return std::to_string(m.FaceCount()) + " faces, volume " + FormatNumber(m.Volume());
}

// Closest point on a triangle (Ericson, Real-Time Collision Detection).
Point3d ClosestOnTriangle(Point3d p, Point3d a, Point3d b, Point3d c) {
  const Vector3d ab = b - a, ac = c - a, ap = p - a;
  const double d1 = ON_DotProduct(ab, ap), d2 = ON_DotProduct(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const Vector3d bp = p - b;
  const double d3 = ON_DotProduct(ab, bp), d4 = ON_DotProduct(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) { const double v = d1 / (d1 - d3); return a + ab * v; }
  const Vector3d cp = p - c;
  const double d5 = ON_DotProduct(ab, cp), d6 = ON_DotProduct(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) { const double w = d2 / (d2 - d6); return a + ac * w; }
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return b + (c - b) * w; }
  const double denom = 1.0 / (va + vb + vc);
  const double v = vb * denom, w = vc * denom;
  return a + ab * v + ac * w;
}

struct Tri {
  Point3d p[3];
  ON_BoundingBox box;
};

std::vector<Tri> Triangles(const ON_Mesh& m) {
  std::vector<Tri> out;
  for (int fi = 0; fi < m.FaceCount(); ++fi) {
    const ON_MeshFace& f = m.m_F[fi];
    auto add = [&](int a, int b, int c) {
      Tri t{{m.Vertex(a), m.Vertex(b), m.Vertex(c)}, ON_BoundingBox()};
      t.box.Set(t.p[0], false); t.box.Set(t.p[1], true); t.box.Set(t.p[2], true);
      out.push_back(t);
    };
    add(f.vi[0], f.vi[1], f.vi[2]);
    if (!f.IsTriangle()) add(f.vi[0], f.vi[2], f.vi[3]);
  }
  return out;
}

bool BoxesTouch(const ON_BoundingBox& a, const ON_BoundingBox& b, double tol) {
  return !(a.m_max.x + tol < b.m_min.x || b.m_max.x + tol < a.m_min.x || a.m_max.y + tol < b.m_min.y || b.m_max.y + tol < a.m_min.y || a.m_max.z + tol < b.m_min.z || b.m_max.z + tol < a.m_min.z);
}

// Nearest point on a mesh and the (outward) normal there.
struct SurfaceHit {
  Point3d point;
  Vector3d normal;
  double distance;
};

SurfaceHit NearestOnMesh(const kernel::Mesh& km, Point3d p) {
  SurfaceHit best{p, Vector3d(0, 0, 1), 1e300};
  for (const Tri& t : Triangles(km.raw())) {
    const Point3d q = ClosestOnTriangle(p, t.p[0], t.p[1], t.p[2]);
    const double d = (q - p).Length();
    if (d < best.distance) {
      Vector3d n = ON_CrossProduct(t.p[1] - t.p[0], t.p[2] - t.p[0]);
      if (!n.Unitize()) continue;
      best = {q, n, d};
    }
  }
  // Make sure the normal points out of the solid.
  const double probe = std::max(Diagonal(km) * 0.01, 1e-4);
  if (km.IsClosedManifold() && km.ContainsPoint(best.point + best.normal * probe)) best.normal = -best.normal;
  return best;
}

// Where triangle `t` crosses the plane (n, d): 0 or 2 points.
int ClipTriangle(const Point3d t[3], Vector3d n, double d, Point3d out[2]) {
  double s[3];
  for (int i = 0; i < 3; ++i) s[i] = ON_DotProduct(n, t[i] - Point3d::Origin) - d;
  const double eps = 1e-9;
  int count = 0;
  for (int i = 0; i < 3 && count < 2; ++i) {
    const int j = (i + 1) % 3;
    if (std::fabs(s[i]) <= eps) { out[count++] = t[i]; continue; }
    if ((s[i] > 0) != (s[j] > 0) && std::fabs(s[j]) > eps) {
      const double f = s[i] / (s[i] - s[j]);
      out[count++] = t[i] + (t[j] - t[i]) * f;
    }
  }
  if (count == 2 && (out[0] - out[1]).Length() <= eps) return 0;
  return count == 2 ? 2 : 0;
}

bool TriTri(const Point3d a[3], const Point3d b[3]) {
  Vector3d na = ON_CrossProduct(a[1] - a[0], a[2] - a[0]), nb = ON_CrossProduct(b[1] - b[0], b[2] - b[0]);
  if (!na.Unitize() || !nb.Unitize()) return false;
  Vector3d line = ON_CrossProduct(na, nb);
  if (!line.Unitize()) return false;  // coplanar or parallel
  Point3d pa[2], pb[2];
  if (ClipTriangle(a, nb, ON_DotProduct(nb, b[0] - Point3d::Origin), pa) != 2) return false;
  if (ClipTriangle(b, na, ON_DotProduct(na, a[0] - Point3d::Origin), pb) != 2) return false;
  double ta0 = ON_DotProduct(line, pa[0] - Point3d::Origin), ta1 = ON_DotProduct(line, pa[1] - Point3d::Origin);
  double tb0 = ON_DotProduct(line, pb[0] - Point3d::Origin), tb1 = ON_DotProduct(line, pb[1] - Point3d::Origin);
  if (ta0 > ta1) std::swap(ta0, ta1);
  if (tb0 > tb1) std::swap(tb0, tb1);
  return std::min(ta1, tb1) - std::max(ta0, tb0) > 1e-9;
}

bool RayTriangle(Point3d o, Vector3d d, const Point3d t[3], double& out_t) {
  const Vector3d e1 = t[1] - t[0], e2 = t[2] - t[0];
  const Vector3d p = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::fabs(det) < 1e-12) return false;
  const double inv = 1 / det;
  const Vector3d s = o - t[0];
  const double u = ON_DotProduct(s, p) * inv;
  if (u < 0 || u > 1) return false;
  const Vector3d q = ON_CrossProduct(s, e1);
  const double v = ON_DotProduct(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  out_t = ON_DotProduct(e2, q) * inv;
  return out_t > 1e-9;
}

// Cubic curve interpolating `pts` (chord-length parameters, relaxation solve).
kernel::NurbsCurve InterpolateCubic(const std::vector<Point3d>& pts, bool closed = false) {
  if (pts.size() < 3) return PolylineCurve(pts);
  ON_3dPointArray arr;
  for (const Point3d& p : pts) arr.Append(p);
  if (closed) arr.Append(pts.front());
  ON_NurbsCurve nc;
  if (!nc.CreateClampedUniformNurbs(3, 4, arr.Count(), arr.Array())) return PolylineCurve(pts);
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

// The plane of a closed planar curve object, oriented like the CPlane.
std::optional<ON_Plane> ClosedPlanarCurvePlane(CommandContext& ctx, const SceneObject& o) {
  if (o.kind != ObjectKind::Curve || !o.curve || !o.curve->raw().IsClosed()) return std::nullopt;
  ON_Plane pl;
  if (!o.curve->raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance * 10)) return std::nullopt;
  if (ON_DotProduct(pl.zaxis, ActiveNormal(ctx)) < 0) pl.Flip();
  return pl;
}

// NURBS surface of a surface object or a single-face polysurface.
std::optional<kernel::NurbsSurface> SurfaceOfObject(const SceneObject& o) {
  if (o.kind == ObjectKind::Surface && o.surface) return *o.surface;
  if (o.kind == ObjectKind::Brep && o.brep && o.brep->raw().m_F.Count() >= 1) {
    const ON_Surface* s = o.brep->raw().m_F[0].SurfaceOf();
    kernel::NurbsSurface k;
    if (s && SurfaceFromON(*s, k)) return k;
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// Moving frames along a curve (rotation-minimizing, as in cmd_surface.cpp)
// ---------------------------------------------------------------------------

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

std::vector<Frame> RmfFrames(const kernel::NurbsCurve& rail, const std::vector<double>& params, Vector3d normal_hint) {
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
  return f;
}

// Frames sampled evenly by arc length along a curve, with the arc length of each sample.
struct CurveFrames {
  std::vector<Frame> f;
  std::vector<double> len;
  double total = 0;
};

CurveFrames SampleFrames(const kernel::NurbsCurve& c, Vector3d hint, int n = 200) {
  CurveFrames cf;
  const kernel::Interval d = c.Domain();
  cf.total = c.Length();
  std::vector<double> params;
  for (int i = 0; i < n; ++i) {
    const double frac = static_cast<double>(i) / (n - 1);
    double t = d.min + (d.max - d.min) * frac;
    if (i > 0 && i < n - 1 && cf.total > 0) t = c.ParameterAtArcLength(cf.total * frac);
    params.push_back(t);
    cf.len.push_back(cf.total * frac);
  }
  cf.f = RmfFrames(c, params, hint);
  return cf;
}

// Frame interpolated at arc-length fraction `s` (0..1).
Frame FrameAtFraction(const CurveFrames& cf, double s) {
  const size_t n = cf.f.size();
  const double x = std::max(0.0, std::min(1.0, s)) * static_cast<double>(n - 1);
  size_t i = static_cast<size_t>(x);
  if (i >= n - 1) i = n - 2;
  const double u = x - static_cast<double>(i);
  const Frame& a = cf.f[i];
  const Frame& b = cf.f[i + 1];
  Frame r;
  r.o = a.o + (b.o - a.o) * u;
  r.t = a.t * (1 - u) + b.t * u;
  if (!r.t.Unitize()) r.t = a.t;
  r.n = Perpendicular(r.t, a.n * (1 - u) + b.n * u);
  r.b = ON_CrossProduct(r.t, r.n);
  return r;
}

// Arc-length fraction and local frame coordinates of `p` relative to the curve.
void CurveLocal(const CurveFrames& cf, Point3d p, double& s, Vector3d& local) {
  double best = 1e300;
  s = 0;
  for (size_t i = 0; i + 1 < cf.f.size(); ++i) {
    const Point3d a = cf.f[i].o, b = cf.f[i + 1].o;
    const Vector3d ab = b - a;
    const double l2 = ab.LengthSquared();
    double t = l2 > 0 ? ON_DotProduct(p - a, ab) / l2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    const double d = (a + ab * t - p).LengthSquared();
    if (d < best) { best = d; s = (static_cast<double>(i) + t) / static_cast<double>(cf.f.size() - 1); }
  }
  const Frame fr = FrameAtFraction(cf, s);
  local = fr.Local(p - fr.o);
}

// ---------------------------------------------------------------------------
// Per-point deformation of any object (control points for curves and
// surfaces, vertices for meshes; polysurfaces and SubDs become meshes).
// ---------------------------------------------------------------------------

using PointMap = std::function<Point3d(Point3d)>;

std::vector<Point3d> ObjectPoints(const SceneObject& o) {
  std::vector<Point3d> pts;
  switch (o.kind) {
    case ObjectKind::Point: pts.push_back(o.point); break;
    case ObjectKind::Curve: for (int i = 0; i < o.curve->ControlPointCount(); ++i) pts.push_back(o.curve->ControlPointAt(i)); break;
    case ObjectKind::Surface:
      for (int i = 0; i < o.surface->CVCountU(); ++i) for (int j = 0; j < o.surface->CVCountV(); ++j) pts.push_back(o.surface->ControlPointAt(i, j));
      break;
    case ObjectKind::Mesh: for (int i = 0; i < o.mesh->raw().VertexCount(); ++i) pts.push_back(o.mesh->raw().Vertex(i)); break;
    default: break;
  }
  return pts;
}

void SetObjectPoints(SceneObject& o, const std::vector<Point3d>& pts) {
  size_t k = 0;
  switch (o.kind) {
    case ObjectKind::Point: if (!pts.empty()) o.point = pts[0]; break;
    case ObjectKind::Curve: for (int i = 0; i < o.curve->ControlPointCount() && k < pts.size(); ++i) o.curve->SetControlPointAt(i, pts[k++]); break;
    case ObjectKind::Surface:
      for (int i = 0; i < o.surface->CVCountU(); ++i) for (int j = 0; j < o.surface->CVCountV() && k < pts.size(); ++j) o.surface->SetControlPointAt(i, j, pts[k++]);
      break;
    case ObjectKind::Mesh: {
      ON_Mesh& m = o.mesh->raw();
      for (int i = 0; i < m.VertexCount() && k < pts.size(); ++i) m.SetVertex(i, pts[k++]);
      m.DestroyRuntimeCache(true);
      m.ComputeFaceNormals();
      m.ComputeVertexNormals();
      break;
    }
    default: break;
  }
  o.InvalidateDisplay();
}

// Curves with few control points are re-sampled before a non-affine map so
// a straight line can actually bend (a two-point line would stay straight).
bool ResampleCurve(SceneObject& o) {
  if (o.kind != ObjectKind::Curve || o.curve->ControlPointCount() >= 12) return false;
  const kernel::NurbsCurve& c = *o.curve;
  const bool closed = c.IsClosed();
  const int n = 24;
  std::vector<Point3d> pts;
  const double len = c.Length();
  const kernel::Interval d = c.Domain();
  for (int i = 0; i < (closed ? n : n); ++i) {
    const double frac = closed ? static_cast<double>(i) / n : static_cast<double>(i) / (n - 1);
    double t = d.min + (d.max - d.min) * frac;
    if (len > 0 && i > 0 && (closed || i < n - 1)) t = c.ParameterAtArcLength(len * frac);
    pts.push_back(c.PointAt(t));
  }
  *o.curve = InterpolateCubic(pts, closed);
  return true;
}

int DeformObjects(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label, const PointMap& fn, bool copy, bool resample = true) {
  ctx.Doc().BeginChange(label);
  int done = 0, converted = 0;
  for (ObjectId id : ids) {
    SceneObject* src = ctx.Doc().Find(id);
    if (!src) continue;
    SceneObject work = *src;
    work.id = kNoObject;
    work.selected = false;
    if (work.kind == ObjectKind::Brep || work.kind == ObjectKind::SubD) {
      std::optional<kernel::Mesh> m = MeshOf(work, ctx.App().surface_display_tolerance);
      if (!m || m->FaceCount() == 0) { ctx.Warn(label + ": object " + Id(id) + " cannot be deformed; skipped"); continue; }
      SceneObject n = SceneObject::MakeMesh(*m);
      n.layer_index = work.layer_index; n.name = work.name; n.color = work.color; n.color_by_layer = work.color_by_layer; n.user_text = work.user_text;
      work = std::move(n);
      ++converted;
    }
    if (resample) ResampleCurve(work);
    std::vector<Point3d> pts = ObjectPoints(work);
    for (Point3d& p : pts) p = fn(p);
    SetObjectPoints(work, pts);
    if (copy) {
      ctx.Doc().Add(std::move(work));
    } else {
      const bool replace = work.kind != src->kind;
      if (replace) { ctx.Doc().Remove(id); ctx.Doc().Add(std::move(work)); }
      else {
        work.id = src->id;
        work.selected = src->selected;
        *src = std::move(work);
        src->InvalidateDisplay();
      }
    }
    ++done;
  }
  std::string msg = label + ": " + (copy ? "copied and deformed " : "deformed ") + std::to_string(done) + " object(s)";
  if (converted) msg += " (" + std::to_string(converted) + " converted to meshes)";
  ctx.Print(msg);
  return done;
}

void ApplyXform(CommandContext& ctx, const std::vector<ObjectId>& ids, const ON_Xform& xf, bool copy, const std::string& label) {
  ctx.Doc().BeginChange(label);
  int n = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (copy) { SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf); ctx.Doc().Add(std::move(dup)); }
    else o->Transform(xf);
    ++n;
  }
  ctx.Print(label + ": " + (copy ? "copied " : "transformed ") + std::to_string(n) + " object(s)");
}

// ---------------------------------------------------------------------------
// A generic "answer a fixed list of prompts" command: object selections,
// points, numbers (typed, or picked as a distance from the previous point)
// and repeated point picks ended with Enter. Works in the UI and in scripts.
// ---------------------------------------------------------------------------

enum class StepKind { Objects, Point, Number, Points };

struct Step {
  StepKind kind;
  std::string prompt;
  double def = 0;
  int min = 1;
};

Step ObjectsStep(const char* prompt, int min = 1) { return {StepKind::Objects, prompt, 0, min}; }
Step PointStep(const char* prompt) { return {StepKind::Point, prompt, 0, 0}; }
Step NumberStep(const char* prompt, double def) { return {StepKind::Number, prompt, def, 0}; }
Step PointsStep(const char* prompt) { return {StepKind::Points, prompt, 0, 0}; }

struct Input {
  std::vector<std::vector<ObjectId>> objs;
  std::vector<std::optional<Point3d>> pts;
  std::vector<std::optional<double>> nums;
  std::vector<std::vector<Point3d>> multi;
  std::map<std::string, std::string> opts;  // lowercase name -> value
  const std::vector<ObjectId>& O(size_t i) const { return objs[i]; }
  bool HasP(size_t i) const { return i < pts.size() && pts[i].has_value(); }
  Point3d P(size_t i) const { return HasP(i) ? *pts[i] : Point3d::Origin; }
  double N(size_t i, double fallback = 0) const { return i < nums.size() && nums[i] ? *nums[i] : fallback; }
  const std::vector<Point3d>& M(size_t i) const { return multi[i]; }
  std::string Opt(const std::string& name, const std::string& def = "") const { auto it = opts.find(Lower(name)); return it == opts.end() ? def : it->second; }
  bool Yes(const std::string& name) const { return Lower(Opt(name)) == "yes"; }
  double OptNum(const std::string& name, double def) const { double v; return ParseNumber(Opt(name), v) ? v : def; }
};

using Action = std::function<void(CommandContext&, const Input&)>;

class ToolCommand : public Command {
 public:
  ToolCommand(std::vector<Step> steps, std::vector<OptionSpec> opts, Action action)
      : steps_(std::move(steps)), opts_(std::move(opts)), action_(std::move(action)) {
    in_.objs.resize(steps_.size());
    in_.pts.resize(steps_.size());
    in_.nums.resize(steps_.size());
    in_.multi.resize(steps_.size());
    for (const OptionSpec& o : opts_) in_.opts[Lower(o.name)] = o.value;
  }
  void Begin(CommandContext& ctx) override { options = opts_; Next(ctx); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (step_ >= steps_.size() || steps_[step_].kind != StepKind::Objects) return;
    if (ids.empty() && steps_[step_].min > 0) { ctx.Warn("Nothing selected"); Finish(); return; }
    in_.objs[step_] = ids;
    // The next object prompt starts from an empty selection.
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    ++step_;
    Next(ctx);
  }
  void OnEnter(CommandContext& ctx) override {
    if (step_ >= steps_.size()) return;
    const Step& s = steps_[step_];
    if (s.kind == StepKind::Objects) { if (s.min == 0) { ++step_; Next(ctx); } else { ctx.Warn("Nothing selected"); Finish(); } return; }
    if (s.kind == StepKind::Number) { OnNumber(ctx, s.def); return; }
    if (s.kind == StepKind::Points) { ++step_; Next(ctx); return; }
    ctx.ClearPreview();
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (step_ >= steps_.size()) return;
    ctx.SetLastPoint(p);
    const Step& s = steps_[step_];
    if (s.kind == StepKind::Points) { in_.multi[step_].push_back(p); return; }
    if (s.kind == StepKind::Objects) return;
    in_.pts[step_] = p;
    if (s.kind == StepKind::Number) in_.nums[step_] = (p - PreviousPoint(ctx)).Length();
    ++step_;
    Next(ctx);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (step_ >= steps_.size() || steps_[step_].kind != StepKind::Number) return;
    in_.nums[step_] = v;
    ++step_;
    Next(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    double v;
    if (ParseNumber(t, v)) { OnNumber(ctx, v); return; }
    ctx.Warn("Unknown option: " + t);
  }
  void OnOption(CommandContext&, const std::string& name, const std::string& value) override {
    for (OptionSpec& o : options) {
      if (o.name != name) continue;
      if (o.toggle) o.value = value.empty() ? (Lower(o.value) == "yes" ? "No" : "Yes") : (Lower(value) == "yes" || value == "1" ? "Yes" : "No");
      else if (o.numeric) { double v; if (ParseNumber(value, v)) o.value = FormatNumber(v); }
      else if (!o.choices.empty()) { for (const std::string& c : o.choices) if (Lower(c) == Lower(value)) o.value = c; }
      else if (!value.empty()) o.value = value;
      in_.opts[Lower(o.name)] = o.value;
    }
    opts_ = options;
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (step_ >= steps_.size() || steps_[step_].kind == StepKind::Objects) return;
    ctx.ClearPreview();
    if (steps_[step_].kind == StepKind::Number) ctx.AddPreviewLine(PreviousPoint(ctx), h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  Point3d PreviousPoint(CommandContext& ctx) const {
    for (size_t i = step_; i-- > 0;) {
      if (in_.pts[i]) return *in_.pts[i];
      if (!in_.multi[i].empty()) return in_.multi[i].back();
    }
    return ActivePlane(ctx).origin;
  }
  void Next(CommandContext& ctx) {
    if (step_ >= steps_.size()) {
      ctx.ClearPreview();
      action_(ctx, in_);
      Finish();
      return;
    }
    const Step& s = steps_[step_];
    switch (s.kind) {
      case StepKind::Objects:
        WantObjects(s.prompt, s.min);
        if (step_ > 0) accept_preselection = false;
        break;
      case StepKind::Point: WantPoint(s.prompt); break;
      case StepKind::Number: WantPoint(s.prompt + " <" + FormatNumber(s.def) + ">"); break;
      case StepKind::Points: WantPoint(s.prompt + ". Press Enter when done"); break;
    }
    options = opts_;
  }
  std::vector<Step> steps_;
  std::vector<OptionSpec> opts_;
  Action action_;
  Input in_;
  size_t step_ = 0;
};

CommandFactory Tool(std::vector<Step> steps, std::vector<OptionSpec> opts, Action action) {
  return [=]() -> std::unique_ptr<Command> { return std::make_unique<ToolCommand>(steps, opts, action); };
}

// Runs `fn` inside a guard so a kernel failure becomes a warning.
Action Guarded(const char* label, Action fn) {
  return [=](CommandContext& ctx, const Input& in) {
    try { fn(ctx, in); } catch (const std::exception& ex) { ctx.Warn(std::string(label) + " failed: " + ex.what()); }
  };
}

// ---------------------------------------------------------------------------
// Holes
// ---------------------------------------------------------------------------

// Subtracts `cutter` from every solid, replacing each with the mesh result.
int CutSolids(CommandContext& ctx, std::vector<Solid>& solids, const kernel::Mesh& cutter, const std::string& label) {
  int cut = 0;
  for (Solid& s : solids) {
    std::optional<kernel::Mesh> r = Combine(ctx, s.mesh, cutter, kernel::BooleanOp::Difference, label);
    if (!r || r->FaceCount() == 0) { ctx.Warn(label + ": object " + Id(s.id) + " was not cut"); continue; }
    s.mesh = *r;
    ++cut;
  }
  return cut;
}

void CommitSolids(CommandContext& ctx, const std::vector<Solid>& solids, const std::string& label) {
  for (const Solid& s : solids) {
    const SceneObject* o = ctx.Doc().Find(s.id);
    const bool was_mesh = o && o->kind == ObjectKind::Mesh;
    ReplaceWithMesh(ctx, s.id, s.mesh);
    ctx.Print(label + ": object " + Id(s.id) + (was_mesh ? " is now " : " replaced by a mesh solid with ") + MeshSummary(s.mesh));
  }
}

// Cylinder cutter through/into the solid at a picked point.
std::optional<kernel::Mesh> CylinderCutter(CommandContext& ctx, const kernel::Mesh& solid, Point3d center, double radius, double depth, bool through, bool cplane_dir, double tol) {
  const SurfaceHit hit = NearestOnMesh(solid, center);
  Vector3d n = cplane_dir ? ActiveNormal(ctx) : hit.normal;
  if (!n.Unitize()) n = Vector3d(0, 0, 1);
  const double diag = Diagonal(solid);
  const double lift = std::max(depth * 0.05, tol * 10);
  Point3d base = through ? hit.point + n * (diag + radius) : hit.point + n * lift;
  const double h = through ? 2 * (diag + radius) : depth + lift;
  ON_Plane pl(base, -n);
  ON_Cylinder cyl(ON_Circle(pl, radius), h);
  return ClosedMeshOfBrep(ON_BrepCylinder(cyl, true, true), tol);
}

void RoundHole(CommandContext& ctx, const Input& in) {
  const double radius = std::fabs(in.N(2, 2)), depth = std::fabs(in.N(3, 10));
  const bool through = in.Yes("Through");
  if (radius <= 0 || (!through && depth <= 0)) { ctx.Warn("RoundHole: radius and depth must be positive"); return; }
  std::vector<Solid> solids = Solids(ctx, in.O(0), "RoundHole");
  if (solids.empty()) return;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  ctx.Doc().BeginChange("RoundHole");
  int cut = 0;
  for (Solid& s : solids) {
    std::optional<kernel::Mesh> cutter = CylinderCutter(ctx, s.mesh, in.P(1), radius, depth, through, Lower(in.Opt("Direction")) == "cplane", tol);
    if (!cutter) { ctx.Warn("RoundHole: could not build the cutter"); continue; }
    std::optional<kernel::Mesh> r = Combine(ctx, s.mesh, *cutter, kernel::BooleanOp::Difference, "RoundHole");
    if (!r || r->FaceCount() == 0) continue;
    s.mesh = *r;
    ++cut;
  }
  ctx.Print("RoundHole: radius " + FormatNumber(radius) + (through ? ", through" : ", depth " + FormatNumber(depth)) + ", cut " + std::to_string(cut) + " solid(s) (mesh boolean; results are meshes)");
  CommitSolids(ctx, solids, "RoundHole");
}

// Hole from closed planar profile curves, extruded along their plane normal.
void MakeHole(CommandContext& ctx, const Input& in) {
  const double depth = std::fabs(in.N(2, 10));
  const bool through = in.Yes("Through");
  std::vector<Solid> solids = Solids(ctx, in.O(1), "MakeHole");
  if (solids.empty()) return;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  std::vector<std::pair<ObjectId, kernel::Mesh>> cutters;
  for (ObjectId id : in.O(0)) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<ON_Plane> pl = ClosedPlanarCurvePlane(ctx, *o);
    if (!pl) { ctx.Warn("MakeHole: object " + Id(id) + " is not a closed planar curve; skipped"); continue; }
    // Extrude towards the solids (into the material).
    Point3d target(0, 0, 0);
    for (const Solid& s : solids) target += s.mesh.GetCentroid();
    target = target / static_cast<double>(solids.size());
    double diag = 0;
    for (const Solid& s : solids) diag = std::max(diag, Diagonal(s.mesh));
    Vector3d n = pl->zaxis;
    if (ON_DotProduct(target - pl->origin, n) < 0) n = -n;
    ON_NurbsCurve c = o->curve->raw();
    Vector3d offset;
    if (through) { c.Translate(-n * diag); offset = n * (2 * diag); }
    else { const double lift = std::max(depth * 0.05, tol * 10); c.Translate(-n * lift); offset = n * (depth + lift); }
    ON_Plane plane = *pl;
    plane.SetOrigin(plane.origin + (through ? -n * diag : -n * std::max(depth * 0.05, tol * 10)));
    std::optional<kernel::Mesh> slab = RegionSlab(plane, c, offset, tol);
    if (!slab) { ctx.Warn("MakeHole: could not extrude curve " + Id(id)); continue; }
    cutters.push_back({id, *slab});
  }
  if (cutters.empty()) { ctx.Warn("MakeHole: no usable profile curves"); return; }
  ctx.Doc().BeginChange("MakeHole");
  int cut = 0;
  for (const auto& [cid, cutter] : cutters) cut += CutSolids(ctx, solids, cutter, "MakeHole");
  if (in.Yes("DeleteInput")) for (const auto& [cid, cutter] : cutters) ctx.Doc().Remove(cid);
  ctx.Print("MakeHole: " + std::to_string(cutters.size()) + " profile(s), " + (through ? "through" : "depth " + FormatNumber(depth)) + ", " + std::to_string(cut) + " cut(s) (mesh boolean; results are meshes)");
  CommitSolids(ctx, solids, "MakeHole");
}

// Move a closed planar profile onto a picked point of a solid, then cut.
void PlaceHole(CommandContext& ctx, const Input& in) {
  const double depth = std::fabs(in.N(3, 10));
  const bool through = in.Yes("Through");
  std::vector<Solid> solids = Solids(ctx, in.O(1), "PlaceHole");
  if (solids.empty()) return;
  const SceneObject* prof = in.O(0).empty() ? nullptr : ctx.Doc().Find(in.O(0)[0]);
  std::optional<ON_Plane> pl = prof ? ClosedPlanarCurvePlane(ctx, *prof) : std::nullopt;
  if (!pl) { ctx.Warn("PlaceHole: select a closed planar profile curve"); return; }
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  const kernel::BoundingBox cb = prof->curve->GetTightBoundingBox();
  ON_Plane from = *pl;
  from.SetOrigin((cb.min + cb.max) * 0.5);
  ctx.Doc().BeginChange("PlaceHole");
  int cut = 0;
  for (Solid& s : solids) {
    const SurfaceHit hit = NearestOnMesh(s.mesh, in.P(2));
    const double diag = Diagonal(s.mesh);
    const double lift = through ? diag : std::max(depth * 0.05, tol * 10);
    ON_Plane to(hit.point + hit.normal * lift, hit.normal);
    ON_Xform xf;
    xf.Rotation(from, to);
    ON_NurbsCurve c = prof->curve->raw();
    c.Transform(xf);
    std::optional<kernel::Mesh> slab = RegionSlab(to, c, -hit.normal * (through ? 2 * diag : depth + lift), tol);
    if (!slab) { ctx.Warn("PlaceHole: could not extrude the profile"); continue; }
    std::optional<kernel::Mesh> r = Combine(ctx, s.mesh, *slab, kernel::BooleanOp::Difference, "PlaceHole");
    if (!r || r->FaceCount() == 0) continue;
    s.mesh = *r;
    ++cut;
  }
  ctx.Print("PlaceHole: profile " + Id(prof->id) + " placed at " + FormatPoint(in.P(2)) + ", " + (through ? "through" : "depth " + FormatNumber(depth)) + ", cut " + std::to_string(cut) + " solid(s) (mesh boolean; results are meshes)");
  CommitSolids(ctx, solids, "PlaceHole");
}

void RevolvedHole(CommandContext& ctx, const Input& in) {
  std::vector<Solid> solids = Solids(ctx, in.O(1), "RevolvedHole");
  if (solids.empty()) return;
  const SceneObject* prof = in.O(0).empty() ? nullptr : ctx.Doc().Find(in.O(0)[0]);
  if (!prof || prof->kind != ObjectKind::Curve) { ctx.Warn("RevolvedHole: select a profile curve"); return; }
  const Point3d a = in.P(2), b = in.P(3);
  if ((b - a).Length() <= 0) { ctx.Warn("RevolvedHole: degenerate axis"); return; }
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  ON_RevSurface* rs = ON_RevSurface::New();
  rs->m_curve = new ON_NurbsCurve(prof->curve->raw());
  rs->m_axis = ON_Line(a, b);
  rs->m_angle = ON_Interval(0, 2 * ON_PI);
  rs->m_t = rs->m_curve->Domain();
  std::optional<kernel::Mesh> cutter = ClosedMeshOfBrep(ON_BrepRevSurface(rs, true, true), tol);
  if (!cutter) { ctx.Warn("RevolvedHole: the revolved profile is not a closed solid (close the profile or end it on the axis)"); return; }
  ctx.Doc().BeginChange("RevolvedHole");
  const int cut = CutSolids(ctx, solids, *cutter, "RevolvedHole");
  if (in.Yes("DeleteInput")) ctx.Doc().Remove(prof->id);
  ctx.Print("RevolvedHole: profile revolved about " + FormatPoint(a) + " -> " + FormatPoint(b) + ", cut " + std::to_string(cut) + " solid(s) (mesh boolean; results are meshes)");
  CommitSolids(ctx, solids, "RevolvedHole");
}

// RoundHole repeated at a set of centres.
void HoleArray(CommandContext& ctx, const Input& in, const std::vector<Point3d>& centers, double radius, double depth, const std::string& label) {
  std::vector<Solid> solids = Solids(ctx, in.O(0), label);
  if (solids.empty()) return;
  const bool through = in.Yes("Through");
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  ctx.Doc().BeginChange(label);
  int cut = 0;
  for (Solid& s : solids) {
    for (const Point3d& c : centers) {
      std::optional<kernel::Mesh> cutter = CylinderCutter(ctx, s.mesh, c, radius, depth, through, false, tol);
      if (!cutter) continue;
      std::optional<kernel::Mesh> r = Combine(ctx, s.mesh, *cutter, kernel::BooleanOp::Difference, label);
      if (!r || r->FaceCount() == 0) continue;
      s.mesh = *r;
      ++cut;
    }
  }
  ctx.Print(label + ": " + std::to_string(centers.size()) + " hole position(s), radius " + FormatNumber(radius) + ", " + std::to_string(cut) + " cut(s) (mesh boolean; results are meshes)");
  CommitSolids(ctx, solids, label);
}

void ArrayHole(CommandContext& ctx, const Input& in) {
  const double radius = std::fabs(in.N(2, 2)), depth = std::fabs(in.N(3, 10));
  const int nx = std::max(1, static_cast<int>(in.N(4, 3))), ny = std::max(1, static_cast<int>(in.N(5, 2)));
  const double sx = in.N(6, 10), sy = in.N(7, 10);
  const ON_Plane pl = ActivePlane(ctx);
  std::vector<Point3d> centers;
  for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) centers.push_back(in.P(1) + pl.xaxis * (sx * i) + pl.yaxis * (sy * j));
  HoleArray(ctx, in, centers, radius, depth, "ArrayHole");
}

void ArrayHolePolar(CommandContext& ctx, const Input& in) {
  const double radius = std::fabs(in.N(2, 2)), depth = std::fabs(in.N(3, 10));
  const int count = std::max(1, static_cast<int>(in.N(5, 6)));
  std::vector<Point3d> centers;
  for (int i = 0; i < count; ++i) {
    ON_Xform rot;
    rot.Rotation(2 * ON_PI * i / count, ActiveNormal(ctx), in.P(4));
    centers.push_back(rot * in.P(1));
  }
  HoleArray(ctx, in, centers, radius, depth, "ArrayHolePolar");
}

CommandFactory HoleFeatureStub(const char* name) {
  return Immediate([name](CommandContext& ctx) {
    ctx.Print(std::string(name) + ": holes cut in Dino 8 are part of the mesh solid, not editable features. Cut another one with RoundHole, PlaceHole or ArrayHole/ArrayHolePolar, or Undo and re-run the hole command at the new position.");
  });
}

// Volume of the solids inside the extrusion of closed planar curves.
void CutVolume(CommandContext& ctx, const Input& in) {
  std::vector<Solid> solids = Solids(ctx, in.O(1), "CutVolume");
  if (solids.empty()) return;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  ctx.Doc().BeginChange("CutVolume");
  int made = 0;
  double total = 0;
  for (ObjectId id : in.O(0)) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<ON_Plane> pl = ClosedPlanarCurvePlane(ctx, *o);
    if (!pl) { ctx.Warn("CutVolume: object " + Id(id) + " is not a closed planar curve; skipped"); continue; }
    for (const Solid& s : solids) {
      const double diag = Diagonal(s.mesh) + (pl->origin - s.mesh.GetCentroid()).Length();
      ON_NurbsCurve c = o->curve->raw();
      c.Translate(-pl->zaxis * diag);
      ON_Plane plane = *pl;
      plane.SetOrigin(plane.origin - pl->zaxis * diag);
      std::optional<kernel::Mesh> slab = RegionSlab(plane, c, pl->zaxis * (2 * diag), tol);
      if (!slab) continue;
      std::optional<kernel::Mesh> r = Combine(ctx, s.mesh, *slab, kernel::BooleanOp::Intersection, "CutVolume");
      if (!r || r->FaceCount() == 0) continue;
      SceneObject n = SceneObject::MakeMesh(*r);
      if (const SceneObject* src = ctx.Doc().Find(s.id)) n.layer_index = src->layer_index;
      total += r->Volume();
      ctx.Doc().Add(std::move(n));
      ++made;
    }
  }
  ctx.Print("CutVolume: " + std::to_string(made) + " cut volume(s) as meshes, total volume " + FormatNumber(total));
}

// ---------------------------------------------------------------------------
// CreateSolid / Merge / Clash
// ---------------------------------------------------------------------------

void CreateSolid(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<kernel::Mesh> parts;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
    if (!m || m->FaceCount() == 0) { ctx.Warn("CreateSolid: object " + Id(id) + " has no surface; skipped"); continue; }
    parts.push_back(*m);
  }
  if (parts.empty()) { ctx.Warn("CreateSolid: select surfaces, polysurfaces or meshes"); return; }
  const double tol = std::max(ctx.Settings().absolute_tolerance * 10, 1e-4);
  kernel::Mesh joined = kernel::Mesh::MergeAndWeld(parts, tol);
  if (!joined.IsClosedManifold()) {
    int naked = 0;
    const ON_MeshTopology& top = joined.raw().Topology();
    for (int i = 0; i < top.m_tope.Count(); ++i) if (top.m_tope[i].m_topf_count == 1) ++naked;
    ctx.Warn("CreateSolid: the selected surfaces do not enclose a volume (" + std::to_string(naked) + " naked edge(s) after joining within " + FormatNumber(tol) + ")");
    return;
  }
  joined = Outward(joined);
  ctx.Doc().BeginChange("CreateSolid");
  int layer = -1;
  for (ObjectId id : ids) { if (const SceneObject* o = ctx.Doc().Find(id)) { if (layer < 0) layer = o->layer_index; ctx.Doc().Remove(id); } }
  SceneObject n = SceneObject::MakeMesh(joined);
  n.layer_index = layer;
  ctx.Doc().Add(std::move(n));
  ctx.Print("CreateSolid: " + std::to_string(parts.size()) + " surface(s) joined into a closed mesh solid, " + MeshSummary(joined));
}

void Merge(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Solid> solids = Solids(ctx, ids, "Merge");
  if (solids.size() < 2) { if (!solids.empty()) ctx.Warn("Merge: select at least two closed solids"); return; }
  kernel::Mesh result = solids[0].mesh;
  for (size_t i = 1; i < solids.size(); ++i) {
    std::optional<kernel::Mesh> r = Combine(ctx, result, solids[i].mesh, kernel::BooleanOp::Union, "Merge");
    if (!r) return;
    result = *r;
  }
  ctx.Doc().BeginChange("Merge");
  for (size_t i = 1; i < solids.size(); ++i) ctx.Doc().Remove(solids[i].id);
  ReplaceWithMesh(ctx, solids[0].id, result);
  ctx.Print("Merge: " + std::to_string(solids.size()) + " solids merged into one mesh solid, " + MeshSummary(result));
}

void Clash(CommandContext& ctx, const Input& in) {
  const double clearance = std::max(0.0, in.OptNum("Clearance", 0));
  struct Item { ObjectId id; std::vector<Tri> tris; ON_BoundingBox box; };
  std::vector<Item> items;
  for (ObjectId id : in.O(0)) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
    if (!m || m->FaceCount() == 0) { ctx.Warn("Clash: object " + Id(id) + " has no surface; skipped"); continue; }
    Item it{id, Triangles(m->raw()), ON_BoundingBox()};
    for (const Tri& t : it.tris) it.box.Union(t.box);
    items.push_back(std::move(it));
  }
  if (items.size() < 2) { ctx.Warn("Clash: select at least two surfaces, polysurfaces or meshes"); return; }
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-6);
  std::set<ObjectId> clashing;
  int pairs = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    for (size_t j = i + 1; j < items.size(); ++j) {
      const Item& a = items[i];
      const Item& b = items[j];
      if (!BoxesTouch(a.box, b.box, clearance + tol)) continue;
      bool hit = false;
      for (const Tri& ta : a.tris) {
        for (const Tri& tb : b.tris) {
          if (!BoxesTouch(ta.box, tb.box, tol)) continue;
          if (TriTri(ta.p, tb.p)) { hit = true; break; }
        }
        if (hit) break;
      }
      double dist = 0;
      if (!hit) {
        // Closest approach: vertices of each against the triangles of the other.
        dist = 1e300;
        auto probe = [&](const Item& from, const Item& to) {
          for (const Tri& t : from.tris)
            for (const Point3d& p : t.p) {
              ON_BoundingBox pb(p, p);
              for (const Tri& u : to.tris) {
                if (!BoxesTouch(pb, u.box, clearance + tol)) continue;
                dist = std::min(dist, (ClosestOnTriangle(p, u.p[0], u.p[1], u.p[2]) - p).Length());
              }
            }
        };
        probe(a, b);
        probe(b, a);
        if (dist > clearance + tol) continue;
      }
      ++pairs;
      clashing.insert(a.id);
      clashing.insert(b.id);
      if (hit) ctx.Print("Clash: objects " + Id(a.id) + " and " + Id(b.id) + " intersect (distance 0)");
      else ctx.Print("Clash: objects " + Id(a.id) + " and " + Id(b.id) + " are " + FormatNumber(dist) + " apart (within clearance " + FormatNumber(clearance) + ")");
    }
  }
  ctx.Doc().SelectNone();
  for (ObjectId id : clashing) ctx.Doc().Select(id, true);
  ctx.Print("Clash: " + std::to_string(pairs) + " clashing pair(s) among " + std::to_string(items.size()) + " object(s)" + (clearance > 0 ? ", clearance " + FormatNumber(clearance) : ""));
}

// ---------------------------------------------------------------------------
// Planar curve booleans: regions as thin mesh slabs, outlines by slicing.
// ---------------------------------------------------------------------------

struct Region {
  ObjectId id;
  kernel::Mesh slab;
};

struct RegionSet {
  ON_Plane plane;
  double height = 1;
  std::vector<Region> regions;
};

std::optional<RegionSet> Regions(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label) {
  RegionSet set;
  bool have_plane = false;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  kernel::BoundingBox all{};
  bool any = false;
  std::vector<const SceneObject*> curves;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<ON_Plane> pl = ClosedPlanarCurvePlane(ctx, *o);
    if (!pl) { ctx.Warn(label + ": object " + Id(id) + " is not a closed planar curve; skipped"); continue; }
    if (!have_plane) { set.plane = *pl; have_plane = true; }
    const kernel::BoundingBox bb = o->curve->GetTightBoundingBox();
    if (!any) { all = bb; any = true; }
    else { all.min.x = std::min(all.min.x, bb.min.x); all.min.y = std::min(all.min.y, bb.min.y); all.min.z = std::min(all.min.z, bb.min.z); all.max.x = std::max(all.max.x, bb.max.x); all.max.y = std::max(all.max.y, bb.max.y); all.max.z = std::max(all.max.z, bb.max.z); }
    curves.push_back(o);
  }
  if (!have_plane) { ctx.Warn(label + ": select closed planar curves"); return std::nullopt; }
  set.height = std::max((all.max - all.min).Length() * 0.1, 1e-3);
  // Outlines come back as polylines, so mesh the regions at a display-like chord tolerance.
  const double mesh_tol = std::max((all.max - all.min).Length() * 0.002, tol);
  ON_Xform proj = ON_Xform::IdentityTransformation;
  proj.PlanarProjection(set.plane);
  for (const SceneObject* o : curves) {
    ON_NurbsCurve c = o->curve->raw();
    c.Transform(proj);
    std::optional<kernel::Mesh> slab = RegionSlab(set.plane, c, set.plane.zaxis * set.height, mesh_tol);
    if (!slab) { ctx.Warn(label + ": could not build a region from curve " + Id(o->id)); continue; }
    set.regions.push_back({o->id, *slab});
  }
  if (set.regions.empty()) return std::nullopt;
  return set;
}

// Closed outline curves of a slab, sliced at mid-height.
std::vector<kernel::NurbsCurve> Outlines(const RegionSet& set, const kernel::Mesh& slab, double tol) {
  std::vector<kernel::NurbsCurve> out;
  ON_Plane mid = set.plane;
  mid.SetOrigin(set.plane.origin + set.plane.zaxis * (set.height * 0.5));
  // Slice: segments of every triangle crossing the plane, chained into loops.
  std::vector<std::pair<Point3d, Point3d>> segs;
  const Vector3d n = mid.zaxis;
  const double d = ON_DotProduct(n, mid.origin - Point3d::Origin);
  for (const Tri& t : Triangles(slab.raw())) {
    Point3d hits[2];
    if (ClipTriangle(t.p, n, d, hits) == 2 && (hits[0] - hits[1]).Length() > tol) segs.emplace_back(hits[0], hits[1]);
  }
  std::vector<bool> used(segs.size(), false);
  for (size_t i = 0; i < segs.size(); ++i) {
    if (used[i]) continue;
    used[i] = true;
    std::vector<Point3d> pl = {segs[i].first, segs[i].second};
    bool grew = true;
    while (grew) {
      grew = false;
      for (size_t j = 0; j < segs.size(); ++j) {
        if (used[j]) continue;
        if ((segs[j].first - pl.back()).Length() <= tol * 10) { pl.push_back(segs[j].second); used[j] = true; grew = true; }
        else if ((segs[j].second - pl.back()).Length() <= tol * 10) { pl.push_back(segs[j].first); used[j] = true; grew = true; }
      }
    }
    if (pl.size() < 3) continue;
    // Back onto the curve plane, dropping (nearly) collinear vertices that
    // the slab's side-wall triangulation adds.
    std::vector<Point3d> clean;
    for (size_t k = 0; k < pl.size(); ++k) {
      const Point3d p = pl[k] - set.plane.zaxis * ON_DotProduct(pl[k] - set.plane.origin, set.plane.zaxis);
      if (!clean.empty() && (p - clean.back()).Length() <= tol) continue;
      if (clean.size() >= 2) {
        Vector3d a = clean.back() - clean[clean.size() - 2], b = p - clean.back();
        if (a.Unitize() && b.Unitize() && ON_CrossProduct(a, b).Length() < 2e-3 && ON_DotProduct(a, b) > 0) clean.pop_back();
      }
      clean.push_back(p);
    }
    while (clean.size() > 2 && (clean.back() - clean.front()).Length() <= tol * 10) clean.pop_back();
    if (clean.size() < 3) continue;
    clean.push_back(clean.front());
    out.push_back(PolylineCurve(clean));
  }
  return out;
}

enum class RegionOp { Union, Difference, Intersection, Regions };

void RegionBoolean(CommandContext& ctx, const std::vector<ObjectId>& ids, RegionOp op, bool delete_input, const std::string& label) {
  std::optional<RegionSet> set = Regions(ctx, ids, label);
  if (!set) return;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-4);
  std::vector<kernel::Mesh> results;
  std::string what;
  if (op == RegionOp::Regions) {
    // Every atomic region: inside the curves of subset S, outside the rest.
    const size_t n = std::min<size_t>(set->regions.size(), 6);
    if (set->regions.size() > 6) ctx.Warn(label + ": using the first 6 curves");
    for (unsigned mask = 1; mask < (1u << n); ++mask) {
      std::optional<kernel::Mesh> r;
      bool ok = true;
      for (size_t i = 0; i < n && ok; ++i) {
        if (!(mask & (1u << i))) continue;
        if (!r) r = set->regions[i].slab;
        else { r = Combine(ctx, *r, set->regions[i].slab, kernel::BooleanOp::Intersection, label); ok = r.has_value(); }
      }
      for (size_t i = 0; i < n && ok && r && r->FaceCount() > 0; ++i) {
        if (mask & (1u << i)) continue;
        r = Combine(ctx, *r, set->regions[i].slab, kernel::BooleanOp::Difference, label);
        ok = r.has_value();
      }
      if (ok && r && r->FaceCount() > 0 && r->Volume() > tol * tol * set->height) results.push_back(*r);
    }
    what = "regions";
  } else {
    kernel::Mesh r = set->regions[0].slab;
    const kernel::BooleanOp bop = op == RegionOp::Union ? kernel::BooleanOp::Union : op == RegionOp::Difference ? kernel::BooleanOp::Difference : kernel::BooleanOp::Intersection;
    for (size_t i = 1; i < set->regions.size(); ++i) {
      std::optional<kernel::Mesh> c = Combine(ctx, r, set->regions[i].slab, bop, label);
      if (!c) return;
      r = *c;
    }
    if (r.FaceCount() > 0) results.push_back(r);
    what = op == RegionOp::Union ? "Union" : op == RegionOp::Difference ? "Difference" : "Intersection";
  }
  ctx.Doc().BeginChange(label);
  int made = 0;
  const SceneObject* like = ctx.Doc().Find(set->regions[0].id);
  const int layer = like ? like->layer_index : -1;
  for (const kernel::Mesh& m : results) {
    for (const kernel::NurbsCurve& c : Outlines(*set, m, tol)) {
      SceneObject n = SceneObject::MakeCurve(c);
      n.layer_index = layer;
      ctx.Doc().Add(std::move(n));
      ++made;
    }
  }
  if (delete_input) for (const Region& r : set->regions) ctx.Doc().Remove(r.id);
  ctx.Print(label + ": " + what + " of " + std::to_string(set->regions.size()) + " region(s) -> " + std::to_string(made) + " closed curve(s)" + (delete_input ? ", input deleted" : ""));
}

// ---------------------------------------------------------------------------
// Cage editing: Bernstein (Bezier) free-form deformation
// ---------------------------------------------------------------------------

constexpr const char* kCageTag = "Cage";
constexpr const char* kCageDivisionsTag = "CageDivisions";
constexpr const char* kCaptiveTag = "CageCaptive";

struct Captive {
  ObjectId id;
  std::vector<Vector3d> local;  // (s, t, u) lattice coordinates per point
  SceneObject original;
};

struct CageBinding {
  ObjectId cage = kNoObject;
  int nx = 2, ny = 2, nz = 2;
  std::vector<Point3d> lattice;  // positions the captives were last evaluated with
  std::vector<Captive> captives;
};

std::map<ObjectId, CageBinding>& Bindings() {
  static std::map<ObjectId, CageBinding> b;
  return b;
}

int LatticeIndex(int nx, int ny, int i, int j, int k) { return i + (nx + 1) * (j + (ny + 1) * k); }

// Lattice mesh: every lattice point is a vertex (in LatticeIndex order); the
// six outer faces are quads so the divisions show as mesh edges.
kernel::Mesh LatticeMesh(const ON_Plane& pl, double sx, double sy, double sz, int nx, int ny, int nz) {
  kernel::Mesh km;
  ON_Mesh& m = km.raw();
  for (int k = 0; k <= nz; ++k) for (int j = 0; j <= ny; ++j) for (int i = 0; i <= nx; ++i)
    m.SetVertex(LatticeIndex(nx, ny, i, j, k), pl.PointAt(sx * i / nx, sy * j / ny) + pl.zaxis * (sz * k / nz));
  int f = 0;
  auto quad = [&](int a, int b, int c, int d) { m.SetQuad(f++, a, b, c, d); };
  for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
    quad(LatticeIndex(nx, ny, i, j, 0), LatticeIndex(nx, ny, i, j + 1, 0), LatticeIndex(nx, ny, i + 1, j + 1, 0), LatticeIndex(nx, ny, i + 1, j, 0));
    quad(LatticeIndex(nx, ny, i, j, nz), LatticeIndex(nx, ny, i + 1, j, nz), LatticeIndex(nx, ny, i + 1, j + 1, nz), LatticeIndex(nx, ny, i, j + 1, nz));
  }
  for (int k = 0; k < nz; ++k) for (int i = 0; i < nx; ++i) {
    quad(LatticeIndex(nx, ny, i, 0, k), LatticeIndex(nx, ny, i + 1, 0, k), LatticeIndex(nx, ny, i + 1, 0, k + 1), LatticeIndex(nx, ny, i, 0, k + 1));
    quad(LatticeIndex(nx, ny, i, ny, k), LatticeIndex(nx, ny, i, ny, k + 1), LatticeIndex(nx, ny, i + 1, ny, k + 1), LatticeIndex(nx, ny, i + 1, ny, k));
  }
  for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) {
    quad(LatticeIndex(nx, ny, 0, j, k), LatticeIndex(nx, ny, 0, j, k + 1), LatticeIndex(nx, ny, 0, j + 1, k + 1), LatticeIndex(nx, ny, 0, j + 1, k));
    quad(LatticeIndex(nx, ny, nx, j, k), LatticeIndex(nx, ny, nx, j + 1, k), LatticeIndex(nx, ny, nx, j + 1, k + 1), LatticeIndex(nx, ny, nx, j, k + 1));
  }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  return km;
}

bool CageDivisions(const SceneObject& o, int& nx, int& ny, int& nz) {
  auto it = o.user_text.find(kCageDivisionsTag);
  if (it == o.user_text.end()) return false;
  return std::sscanf(it->second.c_str(), "%d,%d,%d", &nx, &ny, &nz) == 3 && nx >= 1 && ny >= 1 && nz >= 1;
}

bool IsCage(const SceneObject& o) {
  int nx, ny, nz;
  return o.kind == ObjectKind::Mesh && o.mesh && o.user_text.count(kCageTag) && CageDivisions(o, nx, ny, nz) && o.mesh->raw().VertexCount() == (nx + 1) * (ny + 1) * (nz + 1);
}

std::vector<Point3d> CageLattice(const SceneObject& cage) {
  std::vector<Point3d> pts;
  for (int i = 0; i < cage.mesh->raw().VertexCount(); ++i) pts.push_back(cage.mesh->raw().Vertex(i));
  return pts;
}

// Lattice (s, t, u) coordinates of a world point, from the cage's corner axes.
bool LatticeLocal(const std::vector<Point3d>& lat, int nx, int ny, int nz, Point3d p, Vector3d& out) {
  const Point3d o = lat[LatticeIndex(nx, ny, 0, 0, 0)];
  const Vector3d ex = lat[LatticeIndex(nx, ny, nx, 0, 0)] - o, ey = lat[LatticeIndex(nx, ny, 0, ny, 0)] - o, ez = lat[LatticeIndex(nx, ny, 0, 0, nz)] - o;
  const double det = ON_DotProduct(ex, ON_CrossProduct(ey, ez));
  if (std::fabs(det) < 1e-18) return false;
  const Vector3d d = p - o;
  out.x = ON_DotProduct(d, ON_CrossProduct(ey, ez)) / det;
  out.y = ON_DotProduct(ex, ON_CrossProduct(d, ez)) / det;
  out.z = ON_DotProduct(ex, ON_CrossProduct(ey, d)) / det;
  return true;
}

std::vector<double> Bernstein(int n, double t) {
  std::vector<double> b(static_cast<size_t>(n) + 1, 0.0);
  b[0] = 1;
  for (int i = 1; i <= n; ++i) {
    for (int j = i; j > 0; --j) b[static_cast<size_t>(j)] = b[static_cast<size_t>(j)] * (1 - t) + b[static_cast<size_t>(j) - 1] * t;
    b[0] *= (1 - t);
  }
  return b;
}

Point3d Ffd(const std::vector<Point3d>& lat, int nx, int ny, int nz, Vector3d l) {
  const std::vector<double> bx = Bernstein(nx, l.x), by = Bernstein(ny, l.y), bz = Bernstein(nz, l.z);
  Vector3d sum(0, 0, 0);
  for (int k = 0; k <= nz; ++k) for (int j = 0; j <= ny; ++j) for (int i = 0; i <= nx; ++i)
    sum += (lat[LatticeIndex(nx, ny, i, j, k)] - Point3d::Origin) * (bx[static_cast<size_t>(i)] * by[static_cast<size_t>(j)] * bz[static_cast<size_t>(k)]);
  // "+ 0.0" folds a negative zero from the products into a plain zero.
  return Point3d(sum.x + 0.0, sum.y + 0.0, sum.z + 0.0);
}

ObjectId AddCage(CommandContext& ctx, const ON_Plane& pl, double sx, double sy, double sz, int nx, int ny, int nz) {
  SceneObject cage = SceneObject::MakeMesh(LatticeMesh(pl, sx, sy, sz, nx, ny, nz));
  cage.name = "Cage";
  cage.user_text[kCageTag] = "box";
  cage.user_text[kCageDivisionsTag] = std::to_string(nx) + "," + std::to_string(ny) + "," + std::to_string(nz);
  return ctx.Doc().Add(std::move(cage));
}

int Divisions(const Input& in, const char* name) { return std::max(1, std::min(10, static_cast<int>(in.OptNum(name, 2)))); }

void Cage(CommandContext& ctx, const Input& in) {
  const ON_Plane base = ActivePlane(ctx);
  double u0, v0, u1, v1;
  base.ClosestPointTo(in.P(0), &u0, &v0);
  base.ClosestPointTo(in.P(1), &u1, &v1);
  if (u0 > u1) std::swap(u0, u1);
  if (v0 > v1) std::swap(v0, v1);
  double h = in.N(2, 10);
  if (in.HasP(2) && in.pts[2] != in.pts[1]) h = ON_DotProduct(in.P(2) - in.P(1), base.zaxis);
  if (u1 - u0 <= 0 || v1 - v0 <= 0 || h == 0) { ctx.Warn("Cage: the cage box must have a non-zero size"); return; }
  ON_Plane pl = base;
  pl.SetOrigin(base.PointAt(u0, v0) + (h < 0 ? base.zaxis * h : Vector3d(0, 0, 0)));
  const int nx = Divisions(in, "XDivisions"), ny = Divisions(in, "YDivisions"), nz = Divisions(in, "ZDivisions");
  ctx.Doc().BeginChange("Cage");
  const ObjectId id = AddCage(ctx, pl, u1 - u0, v1 - v0, std::fabs(h), nx, ny, nz);
  ctx.Print("Cage: object " + Id(id) + ", " + std::to_string(nx) + " x " + std::to_string(ny) + " x " + std::to_string(nz) + " divisions (" + std::to_string((nx + 1) * (ny + 1) * (nz + 1)) + " control points). Use CageEdit to bind objects to it.");
}

void CageEdit(CommandContext& ctx, const Input& in) {
  std::vector<ObjectId> captives;
  for (ObjectId id : in.O(0)) if (const SceneObject* o = ctx.Doc().Find(id)) if (!IsCage(*o)) captives.push_back(id);
  if (captives.empty()) { ctx.Warn("CageEdit: select objects to deform"); return; }
  ObjectId cage_id = kNoObject;
  for (ObjectId id : in.O(1)) if (const SceneObject* o = ctx.Doc().Find(id)) { if (IsCage(*o)) { cage_id = id; break; } ctx.Warn("CageEdit: object " + Id(id) + " is not a cage (run Cage first, or use BoundingBox=Yes)"); }
  ctx.Doc().BeginChange("CageEdit");
  if (cage_id == kNoObject) {
    if (!in.O(1).empty() && !in.Yes("BoundingBox")) return;
    kernel::BoundingBox bb;
    if (!ctx.Doc().BoundingBoxOf(captives, bb)) { ctx.Warn("CageEdit: cannot compute the bounding box"); return; }
    Vector3d size = bb.max - bb.min;
    const double pad = std::max(size.Length() * 0.02, 1e-3);
    ON_Plane pl(bb.min - Vector3d(pad, pad, pad), ON_xaxis, ON_yaxis);
    cage_id = AddCage(ctx, pl, size.x + 2 * pad, size.y + 2 * pad, size.z + 2 * pad, Divisions(in, "XDivisions"), Divisions(in, "YDivisions"), Divisions(in, "ZDivisions"));
    ctx.Print("CageEdit: bounding-box cage " + Id(cage_id) + " created");
  }
  // Polysurfaces and SubDs are deformed as meshes.
  std::vector<ObjectId> bound;
  int converted = 0;
  for (ObjectId id : captives) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Brep || o->kind == ObjectKind::SubD) {
      std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
      if (!m || m->FaceCount() == 0) { ctx.Warn("CageEdit: object " + Id(id) + " cannot be deformed; skipped"); continue; }
      const ObjectId nid = ReplaceWithMesh(ctx, id, *m);
      ctx.Print("CageEdit: object " + Id(id) + " converted to mesh " + Id(nid));
      bound.push_back(nid);
      ++converted;
    } else {
      bound.push_back(id);
    }
  }
  SceneObject* cage = ctx.Doc().Find(cage_id);
  if (!cage) return;
  CageBinding& b = Bindings()[cage_id];
  b.cage = cage_id;
  CageDivisions(*cage, b.nx, b.ny, b.nz);
  b.lattice = CageLattice(*cage);
  cage->user_text["CageEdit"] = "bound";
  int points = 0;
  for (ObjectId id : bound) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    Captive c;
    c.id = id;
    c.original = *o;
    bool ok = true;
    for (const Point3d& p : ObjectPoints(*o)) { Vector3d l; if (!LatticeLocal(b.lattice, b.nx, b.ny, b.nz, p, l)) { ok = false; break; } c.local.push_back(l); }
    if (!ok) { ctx.Warn("CageEdit: the cage is degenerate"); continue; }
    points += static_cast<int>(c.local.size());
    // Replace an older binding of the same object.
    for (auto& kv : Bindings()) kv.second.captives.erase(std::remove_if(kv.second.captives.begin(), kv.second.captives.end(), [&](const Captive& x) { return x.id == id; }), kv.second.captives.end());
    b.captives.push_back(std::move(c));
    o->user_text[kCaptiveTag] = Id(cage_id);
  }
  std::string msg = "CageEdit: " + std::to_string(b.captives.size()) + " object(s) (" + std::to_string(points) + " points) bound to cage " + Id(cage_id) + " with a " + std::to_string(b.nx) + " x " + std::to_string(b.ny) + " x " + std::to_string(b.nz) + " Bezier lattice; move, scale or edit the cage to deform them";
  if (converted) msg += " (" + std::to_string(converted) + " converted to meshes)";
  ctx.Print(msg);
}

// Re-evaluates the captives of every cage whose lattice changed. Called once
// per frame by the application.
void UpdateCages(Document& doc) {
  std::map<ObjectId, CageBinding>& all = Bindings();
  for (auto it = all.begin(); it != all.end();) {
    CageBinding& b = it->second;
    SceneObject* cage = doc.Find(b.cage);
    if (!cage || !IsCage(*cage) || cage->mesh->raw().VertexCount() != static_cast<int>(b.lattice.size())) { it = all.erase(it); continue; }
    const std::vector<Point3d> now = CageLattice(*cage);
    bool changed = false;
    for (size_t i = 0; i < now.size() && !changed; ++i) if (now[i] != b.lattice[i]) changed = true;
    if (changed) {
      b.lattice = now;
      for (Captive& c : b.captives) {
        SceneObject* o = doc.Find(c.id);
        if (!o || o->user_text.count(kCaptiveTag) == 0) continue;
        std::vector<Point3d> pts;
        pts.reserve(c.local.size());
        for (const Vector3d& l : c.local) pts.push_back(Ffd(now, b.nx, b.ny, b.nz, l));
        if (pts.size() != ObjectPoints(*o).size()) continue;
        SetObjectPoints(*o, pts);
      }
      doc.Touch();
    }
    ++it;
  }
}

void ReleaseFromCage(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("ReleaseFromCage");
  int released = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (IsCage(*o)) {
      auto it = Bindings().find(id);
      if (it != Bindings().end()) {
        for (const Captive& c : it->second.captives) if (SceneObject* co = ctx.Doc().Find(c.id)) { co->user_text.erase(kCaptiveTag); ++released; }
        Bindings().erase(it);
      }
      o->user_text.erase("CageEdit");
      continue;
    }
    if (o->user_text.erase(kCaptiveTag)) ++released;
    for (auto& kv : Bindings()) kv.second.captives.erase(std::remove_if(kv.second.captives.begin(), kv.second.captives.end(), [&](const Captive& x) { return x.id == id; }), kv.second.captives.end());
  }
  ctx.Print("ReleaseFromCage: " + std::to_string(released) + " object(s) released");
}

void ExtractOriginalCaptives(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("ExtractOriginalCaptives");
  int made = 0;
  for (ObjectId id : ids) {
    for (auto& kv : Bindings()) {
      for (const Captive& c : kv.second.captives) {
        if (c.id != id) continue;
        SceneObject dup = c.original;
        dup.id = kNoObject;
        dup.selected = false;
        dup.user_text.erase(kCaptiveTag);
        ctx.Doc().Add(std::move(dup));
        ++made;
      }
    }
  }
  if (made == 0) ctx.Warn("ExtractOriginalCaptives: no captive with a stored original selected (originals are kept for this session only)");
  else ctx.Print("ExtractOriginalCaptives: " + std::to_string(made) + " original(s) restored as copies");
}

// ---------------------------------------------------------------------------
// Flow / FlowAlongSrf / Splop / Bounce
// ---------------------------------------------------------------------------

std::optional<kernel::NurbsCurve> CurveOf(CommandContext& ctx, const std::vector<ObjectId>& ids, const char* label, const char* what) {
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Curve && o->curve) return *o->curve;
  ctx.Warn(std::string(label) + ": select a " + what);
  return std::nullopt;
}

std::optional<kernel::NurbsSurface> SurfaceOf(CommandContext& ctx, const std::vector<ObjectId>& ids, const char* label, const char* what) {
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (std::optional<kernel::NurbsSurface> s = SurfaceOfObject(*o)) return s;
  ctx.Warn(std::string(label) + ": select a " + what + " (a surface or a single-face polysurface)");
  return std::nullopt;
}

void Flow(CommandContext& ctx, const Input& in) {
  std::optional<kernel::NurbsCurve> base = CurveOf(ctx, in.O(1), "Flow", "base curve");
  std::optional<kernel::NurbsCurve> target = CurveOf(ctx, in.O(2), "Flow", "target curve");
  if (!base || !target) return;
  const Vector3d hint = ActiveNormal(ctx);
  const CurveFrames bf = SampleFrames(*base, hint), tf = SampleFrames(*target, hint);
  if (bf.total <= 0 || tf.total <= 0) { ctx.Warn("Flow: degenerate curve"); return; }
  const bool stretch = in.Yes("Stretch");
  const double ratio = tf.total / bf.total;
  PointMap fn = [=](Point3d p) {
    double s;
    Vector3d local;
    CurveLocal(bf, p, s, local);
    if (!stretch) s = std::min(1.0, s / ratio);
    return FrameAtFraction(tf, s).Place(local);
  };
  ctx.Print("Flow: base length " + FormatNumber(bf.total) + " -> target length " + FormatNumber(tf.total) + (stretch ? " (stretched to fit)" : ""));
  DeformObjects(ctx, in.O(0), "Flow", fn, in.Yes("Copy"));
}

void FlowAlongSrf(CommandContext& ctx, const Input& in) {
  std::optional<kernel::NurbsSurface> base = SurfaceOf(ctx, in.O(1), "FlowAlongSrf", "base surface");
  std::optional<kernel::NurbsSurface> target = SurfaceOf(ctx, in.O(2), "FlowAlongSrf", "target surface");
  if (!base || !target) return;
  const kernel::Interval bu = base->Domain(0), bv = base->Domain(1), tu = target->Domain(0), tv = target->Domain(1);
  const kernel::NurbsSurface b = *base, t = *target;
  PointMap fn = [=](Point3d p) {
    const kernel::Point2d uv = b.ClosestPointParameter(p, 40, 40);
    const Point3d on = b.PointAt(uv.x, uv.y);
    Vector3d n = b.NormalAt(uv.x, uv.y);
    const double d = ON_DotProduct(p - on, n);
    const double fu = (uv.x - bu.min) / (bu.max - bu.min), fv = (uv.y - bv.min) / (bv.max - bv.min);
    const double u2 = tu.min + (tu.max - tu.min) * fu, v2 = tv.min + (tv.max - tv.min) * fv;
    return t.PointAt(u2, v2) + t.NormalAt(u2, v2) * d;
  };
  ctx.Print("FlowAlongSrf: (u, v, normal offset) re-mapped from the base surface to the target surface");
  DeformObjects(ctx, in.O(0), "FlowAlongSrf", fn, in.Yes("Copy"));
}

// Base frame of a set of objects: bottom centre of their bounding box on the CPlane.
ON_Plane ObjectsBaseFrame(CommandContext& ctx, const std::vector<ObjectId>& ids, std::optional<Point3d> base) {
  ON_Plane pl = ActivePlane(ctx);
  if (base) { pl.SetOrigin(*base); return pl; }
  kernel::BoundingBox bb;
  if (ctx.Doc().BoundingBoxOf(ids, bb)) pl.SetOrigin(Point3d((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, bb.min.z));
  return pl;
}

void PlaceCopies(CommandContext& ctx, const std::vector<ObjectId>& ids, const ON_Plane& from, const std::vector<ON_Plane>& targets, bool copy, const std::string& label) {
  ctx.Doc().BeginChange(label);
  int made = 0;
  for (size_t k = 0; k < targets.size(); ++k) {
    ON_Xform xf;
    xf.Rotation(from, targets[k]);
    for (ObjectId id : ids) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (copy) { SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf); ctx.Doc().Add(std::move(dup)); }
      else o->Transform(xf);
      ++made;
    }
    if (!copy) break;
  }
  ctx.Print(label + ": " + (copy ? "placed " + std::to_string(made) + " copy(ies) at " + std::to_string(targets.size()) + " point(s)" : "oriented " + std::to_string(made) + " object(s)"));
}

void Splop(CommandContext& ctx, const Input& in) {
  std::optional<kernel::NurbsSurface> srf = SurfaceOf(ctx, in.O(1), "Splop", "target surface");
  if (!srf) return;
  if (in.M(2).empty()) { ctx.Warn("Splop: pick points on the surface"); return; }
  std::vector<ON_Plane> targets;
  for (const Point3d& p : in.M(2)) {
    const kernel::Point2d uv = srf->ClosestPointParameter(p, 40, 40);
    targets.emplace_back(srf->PointAt(uv.x, uv.y), srf->NormalAt(uv.x, uv.y));
  }
  PlaceCopies(ctx, in.O(0), ObjectsBaseFrame(ctx, in.O(0), std::nullopt), targets, true, "Splop");
}

void OrientOnSrf(CommandContext& ctx, const Input& in) {
  std::optional<kernel::NurbsSurface> srf = SurfaceOf(ctx, in.O(2), "OrientOnSrf", "target surface");
  if (!srf) return;
  if (in.M(3).empty()) { ctx.Warn("OrientOnSrf: pick points on the surface"); return; }
  std::vector<ON_Plane> targets;
  for (const Point3d& p : in.M(3)) {
    const kernel::Point2d uv = srf->ClosestPointParameter(p, 40, 40);
    targets.emplace_back(srf->PointAt(uv.x, uv.y), srf->NormalAt(uv.x, uv.y));
  }
  PlaceCopies(ctx, in.O(0), ObjectsBaseFrame(ctx, in.O(0), in.P(1)), targets, in.Yes("Copy"), "OrientOnSrf");
}

void OrientOnCrv(CommandContext& ctx, const Input& in) {
  std::optional<kernel::NurbsCurve> crv = CurveOf(ctx, in.O(2), "OrientOnCrv", "target curve");
  if (!crv) return;
  if (in.M(3).empty()) { ctx.Warn("OrientOnCrv: pick points on the curve"); return; }
  const Vector3d up = ActiveNormal(ctx);
  std::vector<ON_Plane> targets;
  for (const Point3d& p : in.M(3)) {
    const double t = crv->ClosestPointParameter(p);
    const Point3d pt = crv->PointAt(t);
    Vector3d tan = crv->TangentAt(t);
    if (!tan.Unitize()) tan = ON_xaxis;
    if (in.Yes("Perpendicular")) {
      const Vector3d x = Perpendicular(tan, up);
      targets.emplace_back(pt, x, ON_CrossProduct(tan, x));  // z axis = tangent
    } else {
      Vector3d y = ON_CrossProduct(up, tan);
      if (!y.Unitize()) y = ON_yaxis;
      targets.emplace_back(pt, tan, y);  // x axis = tangent, z stays up
    }
  }
  PlaceCopies(ctx, in.O(0), ObjectsBaseFrame(ctx, in.O(0), in.P(1)), targets, in.Yes("Copy"), "OrientOnCrv");
}

void Bounce(CommandContext& ctx, const Input& in) {
  Point3d o = in.P(0);
  Vector3d d = in.P(1) - o;
  if (!d.Unitize()) { ctx.Warn("Bounce: degenerate direction"); return; }
  const int bounces = std::max(1, static_cast<int>(in.N(2, 10)));
  std::vector<Tri> tris;
  for (const SceneObject& so : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(so)) continue;
    std::optional<kernel::Mesh> m = MeshOf(so, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) continue;
    for (const Tri& t : Triangles(m->raw())) tris.push_back(t);
  }
  if (tris.empty()) { ctx.Warn("Bounce: no visible surfaces or meshes to bounce off"); return; }
  std::vector<Point3d> pts = {o};
  for (int i = 0; i < bounces; ++i) {
    double best = -1;
    const Tri* hit = nullptr;
    for (const Tri& t : tris) { double s; if (RayTriangle(o, d, t.p, s) && (best < 0 || s < best)) { best = s; hit = &t; } }
    if (!hit) break;
    o = o + d * best;
    pts.push_back(o);
    Vector3d n = ON_CrossProduct(hit->p[1] - hit->p[0], hit->p[2] - hit->p[0]);
    if (!n.Unitize()) break;
    d = d - n * (2 * ON_DotProduct(d, n));
  }
  if (pts.size() < 2) { ctx.Warn("Bounce: the ray hits nothing"); return; }
  AddCurve(ctx, PolylineCurve(pts), "Bounce");
  ctx.Print("Bounce: polyline with " + std::to_string(pts.size() - 1) + " bounce(s)");
}

// ---------------------------------------------------------------------------
// ScaleByPlane / ScalePositions
// ---------------------------------------------------------------------------

void ScaleByPlane(CommandContext& ctx, const Input& in) {
  Vector3d n = in.P(2) - in.P(1);
  if (!n.Unitize()) n = ActiveNormal(ctx);
  const double f = in.N(3, 2);
  if (f == 0) { ctx.Warn("ScaleByPlane: the scale factor must be non-zero"); return; }
  ON_Plane pl(in.P(1), n);
  ctx.Print("ScaleByPlane: factor " + FormatNumber(f) + " along the normal of the plane through " + FormatPoint(in.P(1)));
  ApplyXform(ctx, in.O(0), ON_Xform::ScaleTransformation(pl, 1.0, 1.0, f), in.Yes("Copy"), "ScaleByPlane");
}

void ScalePositions(CommandContext& ctx, const Input& in) {
  const double f = in.N(2, 2);
  const Point3d base = in.P(1);
  ctx.Doc().BeginChange("ScalePositions");
  int n = 0;
  for (ObjectId id : in.O(0)) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    const kernel::BoundingBox bb = o->BoundingBox();
    const Point3d c = (bb.min + bb.max) * 0.5;
    const ON_Xform xf = ON_Xform::TranslationTransformation((c - base) * (f - 1));
    if (in.Yes("Copy")) { SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf); ctx.Doc().Add(std::move(dup)); }
    else o->Transform(xf);
    ++n;
  }
  ctx.Print("ScalePositions: distances from " + FormatPoint(base) + " scaled by " + FormatNumber(f) + " for " + std::to_string(n) + " object(s)");
}

}  // namespace

void UpdateCageCaptives(Document& doc) { UpdateCages(doc); }

void RegisterSolidToolsCommands(CommandEngine& e) {
  // ---- holes ------------------------------------------------------------------
  Reg(e, "RoundHole", Tool({ObjectsStep("Select solids to cut"), PointStep("Center of hole on the solid"), NumberStep("Radius", 2), NumberStep("Depth", 10)},
                           {Toggle("Through", true), Choice("Direction", "Normal", {"Normal", "CPlane"})}, Guarded("RoundHole", RoundHole)));
  Reg(e, "MakeHole", Tool({ObjectsStep("Select closed planar curves for the hole"), ObjectsStep("Select solids to cut"), NumberStep("Depth", 10)},
                          {Toggle("Through", true), Toggle("DeleteInput", false)}, Guarded("MakeHole", MakeHole)));
  Reg(e, "PlaceHole", Tool({ObjectsStep("Select a closed planar profile curve"), ObjectsStep("Select solids to cut"), PointStep("Point on the solid to place the hole"), NumberStep("Depth", 10)},
                           {Toggle("Through", false)}, Guarded("PlaceHole", PlaceHole)));
  Reg(e, "RevolvedHole", Tool({ObjectsStep("Select the profile curve"), ObjectsStep("Select solids to cut"), PointStep("Start of revolve axis"), PointStep("End of revolve axis")},
                              {Toggle("DeleteInput", false)}, Guarded("RevolvedHole", RevolvedHole)));
  Reg(e, "ArrayHole", Tool({ObjectsStep("Select solids to cut"), PointStep("Center of first hole"), NumberStep("Radius", 2), NumberStep("Depth", 10), NumberStep("Number in X direction", 3), NumberStep("Number in Y direction", 2), NumberStep("Spacing in X", 10), NumberStep("Spacing in Y", 10)},
                           {Toggle("Through", true)}, Guarded("ArrayHole", ArrayHole)),
      CommandStatus::Partial, "Rectangular grid of round holes along the CPlane axes; profile holes are planned.");
  Reg(e, "ArrayHolePolar", Tool({ObjectsStep("Select solids to cut"), PointStep("Center of first hole"), NumberStep("Radius", 2), NumberStep("Depth", 10), PointStep("Center of polar array"), NumberStep("Number of holes", 6)},
                                {Toggle("Through", true)}, Guarded("ArrayHolePolar", ArrayHolePolar)),
      CommandStatus::Partial, "Polar array of round holes about the CPlane normal; profile holes are planned.");
  for (const char* n : {"CopyHole", "MirrorHole", "MoveHole", "RotateHole"}) Reg(e, n, HoleFeatureStub(n), CommandStatus::Partial, "Holes are not feature objects in this build; prints guidance.");
  Reg(e, "CutVolume", Tool({ObjectsStep("Select closed planar curves"), ObjectsStep("Select solids")}, {}, Guarded("CutVolume", CutVolume)),
      CommandStatus::Partial, "Intersects the curves' extrusion with the solids and reports the volume (mesh result).");
  Reg(e, "CreateSolid", OnSelection("Select surfaces, polysurfaces or meshes that enclose a volume", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        try { CreateSolid(ctx, ids); } catch (const std::exception& ex) { ctx.Warn(std::string("CreateSolid failed: ") + ex.what()); }
      }), CommandStatus::Partial, "Joins and welds the surface meshes into a closed mesh solid; overlapping surfaces are not trimmed.");
  Reg(e, "Merge", OnSelection("Select closed solids to merge", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Merge(ctx, ids); }, 2));
  Reg(e, "NonmanifoldMerge", Immediate([](CommandContext& ctx) { ctx.Print("NonmanifoldMerge: joining the selection (non-manifold polysurfaces are not supported; coincident faces stay separate)"); ctx.Engine().Execute("Join"); }),
      CommandStatus::Partial, "Runs Join.");
  Reg(e, "Clash", Tool({ObjectsStep("Select objects to check for clashes", 2)}, {Numeric("Clearance", 0)}, Guarded("Clash", Clash)));

  // ---- planar curve booleans ------------------------------------------------
  Reg(e, "CurveBoolean", Tool({ObjectsStep("Select closed planar curves", 1)}, {Choice("Operation", "Union", {"Union", "Difference", "Intersection", "Regions"}), Toggle("DeleteInput", false)},
                              Guarded("CurveBoolean", [](CommandContext& ctx, const Input& in) {
                                const std::string op = Lower(in.Opt("Operation"));
                                RegionBoolean(ctx, in.O(0), op == "difference" ? RegionOp::Difference : op == "intersection" ? RegionOp::Intersection : op == "regions" ? RegionOp::Regions : RegionOp::Union, in.Yes("DeleteInput"), "CurveBoolean");
                              })));
  Reg(e, "PlanarUnion", Tool({ObjectsStep("Select closed planar curves", 2)}, {Toggle("DeleteInput", false)},
                             Guarded("PlanarUnion", [](CommandContext& ctx, const Input& in) { RegionBoolean(ctx, in.O(0), RegionOp::Union, in.Yes("DeleteInput"), "PlanarUnion"); })));
  Reg(e, "PlanarDifference", Tool({ObjectsStep("Select closed planar curves (first minus the rest)", 2)}, {Toggle("DeleteInput", false)},
                                  Guarded("PlanarDifference", [](CommandContext& ctx, const Input& in) { RegionBoolean(ctx, in.O(0), RegionOp::Difference, in.Yes("DeleteInput"), "PlanarDifference"); })));
  Reg(e, "CreateRegions", Tool({ObjectsStep("Select closed planar curves", 1)}, {Toggle("DeleteInput", false)},
                               Guarded("CreateRegions", [](CommandContext& ctx, const Input& in) { RegionBoolean(ctx, in.O(0), RegionOp::Regions, in.Yes("DeleteInput"), "CreateRegions"); })),
      CommandStatus::Partial, "Every region of up to 6 overlapping closed curves; open-curve networks are planned.");

  // ---- cage editing ----------------------------------------------------------------
  Reg(e, "Cage", Tool({PointStep("First corner of cage"), PointStep("Other corner of cage"), NumberStep("Height", 10)},
                      {Numeric("XDivisions", 2), Numeric("YDivisions", 2), Numeric("ZDivisions", 2)}, Guarded("Cage", Cage)));
  Reg(e, "CageEdit", Tool({ObjectsStep("Select captive objects"), ObjectsStep("Select control cage (Enter or BoundingBox=Yes for a bounding-box cage)", 0)},
                          {Toggle("BoundingBox", false), Numeric("XDivisions", 2), Numeric("YDivisions", 2), Numeric("ZDivisions", 2)}, Guarded("CageEdit", CageEdit)));
  Reg(e, "ReleaseFromCage", OnSelection("Select captives or cages to release", ReleaseFromCage));
  Reg(e, "ExtractOriginalCaptives", OnSelection("Select captive objects", ExtractOriginalCaptives), CommandStatus::Partial, "Originals are kept for the session only, not in the file.");
  Reg(e, "SelCaptives", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([](const SceneObject& o) { return o.user_text.count(kCaptiveTag) > 0; }); ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " captive(s) selected"); }));
  Reg(e, "SelControls", Immediate([](CommandContext& ctx) { ctx.Doc().SelectWhere([](const SceneObject& o) { return o.user_text.count(kCageTag) > 0; }); ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " cage(s) selected"); }));

  // ---- deformations ----------------------------------------------------------------
  Reg(e, "Flow", Tool({ObjectsStep("Select objects to flow"), ObjectsStep("Select base curve"), ObjectsStep("Select target curve")},
                      {Toggle("Copy", false), Toggle("Stretch", false)}, Guarded("Flow", Flow)));
  Reg(e, "FlowAlongSrf", Tool({ObjectsStep("Select objects to flow"), ObjectsStep("Select base surface"), ObjectsStep("Select target surface")},
                              {Toggle("Copy", false)}, Guarded("FlowAlongSrf", FlowAlongSrf)));
  Reg(e, "Splop", Tool({ObjectsStep("Select objects to splop"), ObjectsStep("Select target surface"), PointsStep("Points on the surface")}, {}, Guarded("Splop", Splop)),
      CommandStatus::Partial, "Places oriented copies at the picked surface points; the spherical mapping is planned.");
  Reg(e, "Bounce", Tool({PointStep("Start of ray"), PointStep("Direction"), NumberStep("Number of bounces", 10)}, {}, Guarded("Bounce", Bounce)),
      CommandStatus::Partial, "Bounces a ray off the visible meshes and surfaces (as meshes).");
  Reg(e, "Radiate", Immediate([](CommandContext& ctx) { ctx.Print("Radiate: paints diffuse and specular vertex colours on meshes in Rhino; the display does not show vertex colours yet."); }), CommandStatus::Partial, "Prints guidance.");
  Reg(e, "RadiateFind", Immediate([](CommandContext& ctx) { ctx.Print("RadiateFind: finds Radiate light sources; Radiate is not available in this build."); }), CommandStatus::Partial, "Prints guidance.");
  Reg(e, "Reflect", Immediate([](CommandContext& ctx) { ctx.Print("Reflect: use Mirror to reflect objects across a plane; the symmetric SubD editing mode is planned."); }), CommandStatus::Partial, "Prints guidance.");
  Reg(e, "ScaleByPlane", Tool({ObjectsStep("Select objects to scale"), PointStep("Origin of the scaling plane"), PointStep("Point on the plane normal"), NumberStep("Scale factor", 2)},
                              {Toggle("Copy", false)}, Guarded("ScaleByPlane", ScaleByPlane)));
  Reg(e, "ScalePositions", Tool({ObjectsStep("Select objects"), PointStep("Base point"), NumberStep("Scale factor", 2)}, {Toggle("Copy", false)}, Guarded("ScalePositions", ScalePositions)));
  Reg(e, "OrientOnCrv", Tool({ObjectsStep("Select objects to orient"), PointStep("Base point"), ObjectsStep("Select target curve"), PointsStep("Points on the curve")},
                             {Toggle("Copy", true), Toggle("Perpendicular", false)}, Guarded("OrientOnCrv", OrientOnCrv)));
  Reg(e, "OrientOnSrf", Tool({ObjectsStep("Select objects to orient"), PointStep("Base point"), ObjectsStep("Select target surface"), PointsStep("Points on the surface")},
                             {Toggle("Copy", true)}, Guarded("OrientOnSrf", OrientOnSrf)));
  Reg(e, "OrientCrvToEdge", Immediate([](CommandContext& ctx) { ctx.Print("OrientCrvToEdge: use OrientOnCrv with the edge duplicated by DupEdge as the target curve; direct edge picking is planned."); }), CommandStatus::Partial, "Prints guidance.");
}

}  // namespace dino8::app
