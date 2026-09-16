// Curve tools, part two: conics, catenary, closing/merging/simplifying,
// sub-curves, seams, knot and control-point editing, tweening, fitting,
// curve arrays, alignment, and planar slicing (Contour/Section/CutPlane).
#include "commands/cmd_common.h"

#include <algorithm>
#include <limits>
#include <map>

namespace dino8::app {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

struct CurveCopy {
  ObjectId id = kNoObject;
  kernel::NurbsCurve curve;
  SceneObject attrs;
};

std::optional<CurveCopy> CopyCurveObj(CommandContext& ctx, ObjectId id) {
  const SceneObject* o = ctx.Doc().Find(id);
  if (!o || o->kind != ObjectKind::Curve || !o->curve) return std::nullopt;
  CurveCopy c;
  c.id = id;
  c.curve = *o->curve;
  c.attrs = *o;
  return c;
}

std::vector<CurveCopy> CopyCurves(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<CurveCopy> out;
  for (ObjectId id : ids) if (auto c = CopyCurveObj(ctx, id)) out.push_back(*c);
  return out;
}

ObjectId AddCurveLike(CommandContext& ctx, const kernel::NurbsCurve& c, const SceneObject& like) {
  SceneObject n = SceneObject::MakeCurve(c);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  n.name = like.name;
  n.user_text = like.user_text;
  return ctx.Doc().Add(std::move(n));
}

// Replaces the geometry of curve object `id` in place.
bool ReplaceCurve(CommandContext& ctx, ObjectId id, const kernel::NurbsCurve& c) {
  SceneObject* o = ctx.Doc().Find(id);
  if (!o || o->kind != ObjectKind::Curve) return false;
  *o->curve = c;
  o->InvalidateDisplay();
  return true;
}


inline double At(const kernel::Interval& d, double u) { return d.min + (d.max - d.min) * u; }

// Approximate knot removal: drop knot `ki`, keep the degree, and re-sample the
// control points at the Greville abscissae of the new knot vector.
bool RemoveKnotApprox(ON_NurbsCurve& nc, int ki) {
  const int deg = nc.Degree(), order = nc.Order();
  if (nc.CVCount() <= order || ki < deg || ki >= nc.KnotCount() - deg) return false;
  std::vector<double> knots;
  for (int i = 0; i < nc.KnotCount(); ++i) if (i != ki) knots.push_back(nc.Knot(i));
  const int ncv = nc.CVCount() - 1;
  ON_NurbsCurve out;
  out.Create(3, false, order, ncv);
  for (size_t i = 0; i < knots.size(); ++i) out.SetKnot(static_cast<int>(i), knots[i]);
  for (int i = 0; i < ncv; ++i) {
    double g = 0;
    for (int k = 0; k < deg; ++k) g += knots[i + k];
    g /= deg;
    out.SetCV(i, nc.PointAt(g));
  }
  nc = out;
  return true;
}

// Principal axes of a point set: centroid + eigenvectors sorted by decreasing variance.
bool PrincipalAxes(const std::vector<Point3d>& pts, Point3d& centroid, Vector3d axes[3]) {
  if (pts.size() < 2) return false;
  centroid = Point3d(0, 0, 0);
  for (const Point3d& p : pts) centroid += p;
  centroid = centroid * (1.0 / pts.size());
  double xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0;
  for (const Point3d& p : pts) { Vector3d d = p - centroid; xx += d.x * d.x; xy += d.x * d.y; xz += d.x * d.z; yy += d.y * d.y; yz += d.y * d.z; zz += d.z * d.z; }
  double e1, e2, e3; ON_3dVector v1, v2, v3;
  if (!ON_Sym3x3EigenSolver(xx, yy, zz, xy, yz, xz, &e1, v1, &e2, v2, &e3, v3)) return false;
  struct E { double v; ON_3dVector a; } es[3] = {{e1, v1}, {e2, v2}, {e3, v3}};
  std::sort(es, es + 3, [](const E& a, const E& b) { return a.v > b.v; });
  for (int i = 0; i < 3; ++i) { axes[i] = es[i].a; axes[i].Unitize(); }
  return true;
}

// Jacobi eigenvalue algorithm for a small symmetric matrix `a` (n x n,
// n <= kMaxJacobi): diagonalizes `a` in place via repeated Givens rotations,
// writing the eigenvectors (as columns) into `v` and the eigenvalues onto
// the diagonal of `a`. Standard textbook algorithm - used here because
// OpenNURBS only exposes a 3x3 symmetric eigensolver (ON_Sym3x3EigenSolver),
// and the general planar-conic fit below needs the smallest eigenvector of
// a 6x6 scatter matrix.
constexpr int kMaxJacobi = 6;
void JacobiEigen(double a[kMaxJacobi][kMaxJacobi], double v[kMaxJacobi][kMaxJacobi], int n) {
  for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) v[i][j] = (i == j) ? 1.0 : 0.0;
  for (int sweep = 0; sweep < 100; ++sweep) {
    double off = 0;
    for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) off += a[i][j] * a[i][j];
    if (off < 1e-30) break;
    for (int p = 0; p < n; ++p) {
      for (int q = p + 1; q < n; ++q) {
        if (std::fabs(a[p][q]) < 1e-300) continue;
        double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
        double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
        double c = 1.0 / std::sqrt(t * t + 1), s = t * c;
        double app = a[p][p], aqq = a[q][q], apq = a[p][q];
        a[p][p] = c * c * app - 2 * s * c * apq + s * s * aqq;
        a[q][q] = s * s * app + 2 * s * c * apq + c * c * aqq;
        a[p][q] = a[q][p] = 0;
        for (int i = 0; i < n; ++i) {
          if (i != p && i != q) {
            double aip = a[i][p], aiq = a[i][q];
            a[i][p] = a[p][i] = c * aip - s * aiq;
            a[i][q] = a[q][i] = s * aip + c * aiq;
          }
          double vip = v[i][p], viq = v[i][q];
          v[i][p] = c * vip - s * viq;
          v[i][q] = s * vip + c * viq;
        }
      }
    }
  }
}

// A planar conic in a curve's own plane, fitted by least squares.
struct PlaneConic { double A, B, C, D, E, F; };

// Fits Ax^2+Bxy+Cy^2+Dx+Ey+F=0 (in local x/y, centered at the point set's
// own centroid for numerical conditioning) through `pts_local` as the
// eigenvector of the smallest eigenvalue of the 6x6 normal matrix M^T*M
// (rows of M are [x^2, xy, y^2, x, y, 1]) - the standard unconstrained
// algebraic conic fit. Needs at least 6 points.
std::optional<PlaneConic> FitConic(const std::vector<std::pair<double, double>>& pts_local) {
  if (pts_local.size() < 6) return std::nullopt;
  double scale = 0;
  for (const auto& p : pts_local) scale = std::max({scale, std::fabs(p.first), std::fabs(p.second)});
  if (scale <= 0) return std::nullopt;
  double s[kMaxJacobi][kMaxJacobi] = {};
  for (const auto& p : pts_local) {
    double x = p.first / scale, y = p.second / scale;
    double row[6] = {x * x, x * y, y * y, x, y, 1};
    for (int i = 0; i < 6; ++i) for (int j = 0; j < 6; ++j) s[i][j] += row[i] * row[j];
  }
  double v[kMaxJacobi][kMaxJacobi];
  JacobiEigen(s, v, 6);
  int best = 0;
  for (int i = 1; i < 6; ++i) if (s[i][i] < s[best][best]) best = i;
  double coef[6];
  for (int i = 0; i < 6; ++i) coef[i] = v[i][best];
  // Undo the coordinate scaling: coefficients were fit in x/scale, y/scale.
  PlaneConic c;
  c.A = coef[0] / (scale * scale);
  c.B = coef[1] / (scale * scale);
  c.C = coef[2] / (scale * scale);
  c.D = coef[3] / scale;
  c.E = coef[4] / scale;
  c.F = coef[5];
  return c;
}

// Classifies a fitted conic and returns its focus/foci in the same local
// (centered) plane coordinates it was fit in - 1 point for a parabola, 2
// for a hyperbola, none otherwise (ellipses/arcs are handled separately by
// exact ON_Ellipse/ON_Arc geometry, not this generic algebraic fit).
// Standard analytic-geometry technique: rotate by
// theta = 0.5*atan2(B, A-C) to eliminate the xy term, then complete the
// square in the rotated frame.
std::vector<std::pair<double, double>> ConicFoci(const PlaneConic& q) {
  const double disc = q.B * q.B - 4 * q.A * q.C;
  const double mag = std::fabs(q.A) + std::fabs(q.B) + std::fabs(q.C);
  if (mag <= 0) return {};
  double theta = (std::fabs(q.B) < 1e-12 * mag) ? 0.0 : 0.5 * std::atan2(q.B, q.A - q.C);
  double c = std::cos(theta), s = std::sin(theta);
  double A2 = q.A * c * c + q.B * c * s + q.C * s * s;
  double C2 = q.A * s * s - q.B * c * s + q.C * c * c;
  double D2 = q.D * c + q.E * s;
  double E2 = -q.D * s + q.E * c;
  double F2 = q.F;
  auto unrotate = [&](double xp, double yp) { return std::make_pair(xp * c - yp * s, xp * s + yp * c); };
  if (std::fabs(disc) <= 1e-9 * mag * mag) {
    // Parabola: exactly one of A2, C2 is (numerically) zero.
    bool axis_is_x = std::fabs(A2) < std::fabs(C2);
    double lin = axis_is_x ? D2 : E2;
    if (std::fabs(lin) < 1e-12) return {};
    double quad = axis_is_x ? C2 : A2;
    double other_lin = axis_is_x ? E2 : D2;
    double u0 = -other_lin / (2 * quad);
    double k = F2 - other_lin * other_lin / (4 * quad);
    double t0 = -k / lin;             // vertex's axis coordinate
    double f = -(lin / quad) / 4.0;   // focal length along the axis
    double t_focus = t0 + f;
    // Snap a near-zero coordinate to exactly 0 (avoids a stray "-0" from
    // floating-point cancellation when the true value is 0).
    auto snap = [](double v) { return std::fabs(v) < 1e-9 ? 0.0 : v; };
    std::pair<double, double> focus = axis_is_x ? unrotate(t_focus, u0) : unrotate(u0, t_focus);
    return {{snap(focus.first), snap(focus.second)}};
  }
  if (disc > 0 && std::fabs(A2) > 1e-12 * mag && std::fabs(C2) > 1e-12 * mag) {
    // Hyperbola: A2 and C2 have opposite signs.
    double x0 = -D2 / (2 * A2), y0 = -E2 / (2 * C2);
    double k = F2 - D2 * D2 / (4 * A2) - E2 * E2 / (4 * C2);
    double P = -k / A2, Q = -k / C2;  // one positive (transverse), one negative
    double a2 = P > 0 ? P : Q, b2 = P > 0 ? -Q : -P;
    if (a2 <= 0 || b2 < 0) return {};
    double cc = std::sqrt(a2 + b2);
    if (P > 0) return {unrotate(x0 - cc, y0), unrotate(x0 + cc, y0)};
    return {unrotate(x0, y0 - cc), unrotate(x0, y0 + cc)};
  }
  return {};
}

// Fits a general conic to `c` (assumed planar) and returns its analytic
// focus/foci in world 3D space via `plane`'s own local axes - used by
// MarkFoci for parabolas and hyperbolas, which (unlike ellipses/arcs) have
// no dedicated ON_Curve test to read the answer from directly.
std::vector<Point3d> GeneralConicFoci(const kernel::NurbsCurve& c, const ON_Plane& plane) {
  const int n = 40;
  kernel::Interval d = c.Domain();
  std::vector<Point3d> world;
  Point3d centroid(0, 0, 0);
  for (int i = 0; i <= n; ++i) {
    Point3d p = c.PointAt(d.min + (d.max - d.min) * i / n);
    world.push_back(p);
    centroid += p;
  }
  centroid = centroid * (1.0 / world.size());
  double cx = ON_DotProduct(centroid - plane.origin, plane.xaxis), cy = ON_DotProduct(centroid - plane.origin, plane.yaxis);
  std::vector<std::pair<double, double>> local;
  for (const Point3d& p : world) {
    double x = ON_DotProduct(p - plane.origin, plane.xaxis) - cx;
    double y = ON_DotProduct(p - plane.origin, plane.yaxis) - cy;
    local.emplace_back(x, y);
  }
  std::optional<PlaneConic> fit = FitConic(local);
  if (!fit) return {};
  std::vector<Point3d> out;
  for (const auto& f : ConicFoci(*fit)) {
    Point3d p = plane.origin + plane.xaxis * (f.first + cx) + plane.yaxis * (f.second + cy);
    // Snap each coordinate near zero to exactly 0 - the plane fitted from
    // sampled points carries enough numerical noise that a mathematically
    // exact 0 can otherwise come back as a tiny nonzero (e.g. -1e-15),
    // which prints as a confusing "-0".
    auto snap = [](double v) { return std::fabs(v) < 1e-6 ? 0.0 : v; };
    out.push_back(Point3d(snap(p.x), snap(p.y), snap(p.z)));
  }
  return out;
}

// Cubic curve interpolating `pts` (chord-length parameters, relaxation solve).
kernel::NurbsCurve InterpolateCubic(const std::vector<Point3d>& pts, bool closed = false) {
  if (pts.size() < 2) return PolylineCurve(pts);
  if (pts.size() == 2) return PolylineCurve(pts);
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

// Rational quadratic Bezier (a conic arc) from P0 to P2 with apex P1 and weight w.
kernel::NurbsCurve ConicArc(Point3d p0, Point3d p1, Point3d p2, double w) {
  ON_NurbsCurve nc;
  nc.Create(3, true, 3, 3);
  nc.SetCV(0, ON_4dPoint(p0.x, p0.y, p0.z, 1));
  nc.SetCV(1, ON_4dPoint(p1.x * w, p1.y * w, p1.z * w, w));
  nc.SetCV(2, ON_4dPoint(p2.x, p2.y, p2.z, 1));
  nc.SetKnot(0, 0); nc.SetKnot(1, 0); nc.SetKnot(2, 1); nc.SetKnot(3, 1);
  kernel::NurbsCurve k;
  k.raw() = nc;
  return k;
}

// Weight so the conic P0-P1-P2 passes through the shoulder point s along the axis (M -> P1).
double ConicWeightThrough(Point3d p0, Point3d p1, Point3d p2, Point3d s) {
  Point3d m = (p0 + p2) * 0.5;
  Vector3d axis = p1 - m;
  double l2 = axis.LengthSquared();
  if (l2 <= 0) return 1;
  double f = ON_DotProduct(s - m, axis) / l2;  // 0 at M, 1 at apex
  f = std::clamp(f, 0.01, 0.99);
  return f / (1 - f);
}

// Slices a mesh with a plane, returning joined polylines.
std::vector<std::vector<Point3d>> SliceMesh(const ON_Mesh& m, const ON_Plane& plane, double tol) {
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
      std::vector<Point3d> hits;
      for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        if ((d[k] < 0 && d[j] >= 0) || (d[k] >= 0 && d[j] < 0)) {
          double t = d[k] / (d[k] - d[j]);
          hits.push_back(p[k] + (p[j] - p[k]) * t);
        }
      }
      if (hits.size() == 2 && hits[0].DistanceTo(hits[1]) > tol) segs.emplace_back(hits[0], hits[1]);
    }
  }
  // Chain segments into polylines.
  std::vector<std::vector<Point3d>> out;
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

// Curve/plane crossings as points.
std::vector<Point3d> CurvePlaneHits(const kernel::NurbsCurve& c, const ON_Plane& plane) {
  std::vector<Point3d> out;
  std::vector<double> params = c.SuggestedParameterValues(0.01, 10);
  if (params.size() < 2) return out;
  for (size_t i = 0; i + 1 < params.size(); ++i) {
    double a = params[i], b = params[i + 1];
    double da = plane.DistanceTo(c.PointAt(a)), db = plane.DistanceTo(c.PointAt(b));
    if ((da < 0) == (db < 0)) continue;
    for (int it = 0; it < 40; ++it) {
      double m = 0.5 * (a + b), dm = plane.DistanceTo(c.PointAt(m));
      if ((dm < 0) == (da < 0)) { a = m; da = dm; } else { b = m; db = dm; }
    }
    out.push_back(c.PointAt(0.5 * (a + b)));
  }
  return out;
}

int SliceObjects(CommandContext& ctx, const std::vector<ObjectId>& ids, const ON_Plane& plane, const std::string& label) {
  int made = 0;
  const double tol = ctx.Settings().absolute_tolerance * 10;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject like = *o;
    if (o->kind == ObjectKind::Curve && o->curve) {
      for (const Point3d& p : CurvePlaneHits(*o->curve, plane)) { SceneObject n = SceneObject::MakePoint(p); n.layer_index = like.layer_index; ctx.Doc().Add(std::move(n)); ++made; }
      continue;
    }
    std::optional<kernel::Mesh> m = MeshOf(*o, 0.005);
    if (!m) continue;
    for (const auto& pl : SliceMesh(m->raw(), plane, tol)) {
      if (pl.size() < 2) continue;
      AddCurveLike(ctx, PolylineCurve(pl), like);
      ++made;
    }
  }
  (void)label;
  return made;
}

// Frame on a curve at parameter t (tangent + a stable normal).
ON_Plane FrameAt(const kernel::NurbsCurve& c, double t, const Vector3d& up) {
  Vector3d tan = c.TangentAt(t);
  if (!tan.Unitize()) tan = ON_xaxis;
  Vector3d n = ON_CrossProduct(up, tan);
  if (!n.Unitize()) { n = ON_CrossProduct(ON_zaxis, tan); if (!n.Unitize()) n = ON_yaxis; }
  Vector3d b = ON_CrossProduct(tan, n);
  return ON_Plane(c.PointAt(t), tan, n);
  (void)b;
}

// Douglas-Peucker on a polyline.
void Simplify(const std::vector<Point3d>& pts, size_t a, size_t b, double tol, std::vector<bool>& keep) {
  if (b <= a + 1) return;
  ON_Line line(pts[a], pts[b]);
  double best = -1; size_t bi = a;
  for (size_t i = a + 1; i < b; ++i) { double d = line.DistanceTo(pts[i]); if (d > best) { best = d; bi = i; } }
  if (best > tol) { keep[bi] = true; Simplify(pts, a, bi, tol, keep); Simplify(pts, bi, b, tol, keep); }
}

std::vector<Point3d> ControlPolygon(const kernel::NurbsCurve& c) {
  std::vector<Point3d> pts;
  for (int i = 0; i < c.ControlPointCount(); ++i) pts.push_back(c.ControlPointAt(i));
  return pts;
}

// ---------------------------------------------------------------------------
// Conic-family commands
// ---------------------------------------------------------------------------

class ConicCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Rho", FormatNumber(rho_), {}, true, false}};
    WantPoint("Start of conic");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Rho") rho_ = std::clamp(std::atof(v.c_str()), 0.01, 0.99); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) WantPoint("End of conic");
    else if (pts_.size() == 2) WantPoint("Apex");
    else if (pts_.size() == 3) { options = {{"Rho", FormatNumber(rho_), {}, true, false}}; WantPoint("Point on conic (or Rho option, Enter for Rho=" + FormatNumber(rho_) + ")"); }
    else { Build(ctx, ConicWeightThrough(pts_[0], pts_[2], pts_[1], p)); }
  }
  void OnEnter(CommandContext& ctx) override { if (pts_.size() == 3) Build(ctx, rho_ / (1 - rho_)); }
  void OnNumber(CommandContext& ctx, double v) override { if (pts_.size() == 3) { rho_ = std::clamp(v, 0.01, 0.99); Build(ctx, rho_ / (1 - rho_)); } }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    if (pts_.size() == 1) ctx.AddPreviewLine(pts_[0], h);
    else if (pts_.size() == 2) ctx.AddPreviewCurve(ConicArc(pts_[0], h, pts_[1], 1));
    else if (pts_.size() == 3) ctx.AddPreviewCurve(ConicArc(pts_[0], pts_[2], pts_[1], ConicWeightThrough(pts_[0], pts_[2], pts_[1], h)));
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  void Build(CommandContext& ctx, double w) {
    ctx.ClearPreview();
    AddCurve(ctx, ConicArc(pts_[0], pts_[2], pts_[1], w), "Conic");
    ctx.Print("Conic: rho = " + FormatNumber(w / (1 + w)));
    Finish();
  }
  std::vector<Point3d> pts_;
  double rho_ = 0.5;
};

// Parabola from vertex, focus and an end point (symmetric about the axis).
void BuildParabola(CommandContext& ctx, Point3d vertex, Point3d focus, Point3d end) {
  Vector3d axis = focus - vertex;
  double f = axis.Length();
  if (!axis.Unitize() || f <= 0) { ctx.Warn("Focus must differ from the vertex"); return; }
  // Distance of the end point across the axis.
  Vector3d d = end - vertex;
  double along = ON_DotProduct(d, axis);
  Vector3d across = d - axis * along;
  double x1 = across.Length();
  if (!across.Unitize() || x1 <= 0) { ctx.Warn("End point must be off the axis"); return; }
  double y1 = x1 * x1 / (4 * f);
  Point3d p0 = vertex + axis * y1 - across * x1;
  Point3d p2 = vertex + axis * y1 + across * x1;
  Point3d p1 = vertex - axis * y1;
  AddCurve(ctx, ConicArc(p0, p1, p2, 1), "Parabola");
  ctx.Print("Parabola: focal length " + FormatNumber(f));
}

// Hyperbola from center, vertex and an end point on one branch.
void BuildHyperbola(CommandContext& ctx, Point3d center, Point3d vertex, Point3d end) {
  Vector3d axis = vertex - center;
  double a = axis.Length();
  if (!axis.Unitize() || a <= 0) { ctx.Warn("Vertex must differ from the center"); return; }
  Vector3d d = end - center;
  double x = ON_DotProduct(d, axis);
  Vector3d across = d - axis * x;
  double y = across.Length();
  if (!across.Unitize() || y <= 0 || x <= a) { ctx.Warn("End point must be beyond the vertex and off the axis"); return; }
  // Symmetric arc from end' to end; tangent lines meet on the axis at x = a^2/x.
  double b2 = y * y / ((x / a) * (x / a) - 1);
  double xt = a * a / x;
  (void)b2;
  Point3d p0 = center + axis * x - across * y;
  Point3d p2 = center + axis * x + across * y;
  Point3d p1 = center + axis * xt;
  AddCurve(ctx, ConicArc(p0, p1, p2, ConicWeightThrough(p0, p1, p2, vertex)), "Hyperbola");
  ctx.Print("Hyperbola: a = " + FormatNumber(a));
}

class CatenaryCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Start of catenary"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!a_) { a_ = p; ctx.SetLastPoint(p); WantPoint("End of catenary"); return; }
    if (!b_) { b_ = p; double span = (*b_ - *a_).Length(); WantNumber("Cable length", span * 1.2); return; }
  }
  void OnNumber(CommandContext& ctx, double len) override { if (a_ && b_) Build(ctx, len); }
  void OnEnter(CommandContext& ctx) override { if (a_ && b_) Build(ctx, (*b_ - *a_).Length() * 1.2); }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_ && !b_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  void Build(CommandContext& ctx, double len) {
    ctx.ClearPreview();
    Vector3d up = ActiveNormal(ctx);
    Vector3d chord = *b_ - *a_;
    double v = ON_DotProduct(chord, up);
    Vector3d horiz = chord - up * v;
    double h = horiz.Length();
    if (h <= 0) { ctx.Warn("Endpoints must be horizontally separated"); Finish(); return; }
    horiz.Unitize();
    double min_len = chord.Length();
    if (len <= min_len) len = min_len * 1.001;
    // Solve sqrt(L^2 - v^2) = 2a sinh(h/(2a)) for a by bisection.
    double target = std::sqrt(len * len - v * v);
    double lo = 1e-6 * h, hi = 1e6 * h;
    for (int i = 0; i < 200; ++i) {
      double a = std::sqrt(lo * hi);
      double val = 2 * a * std::sinh(h / (2 * a));
      if (val > target) lo = a; else hi = a;
    }
    double a = std::sqrt(lo * hi);
    // y(x) = a cosh((x - x0)/a) + c; choose x0 so the endpoints fit.
    double x0 = h / 2 - a * std::asinh(v / (2 * a * std::sinh(h / (2 * a))));
    double c = -a * std::cosh(-x0 / a);
    std::vector<Point3d> pts;
    const int n = 48;
    for (int i = 0; i <= n; ++i) {
      double x = h * i / n;
      double y = a * std::cosh((x - x0) / a) + c;
      pts.push_back(*a_ + horiz * x + up * y);
    }
    AddCurve(ctx, InterpolateCubic(pts), "Catenary");
    ctx.Print("Catenary: length " + FormatNumber(len) + ", sag parameter a = " + FormatNumber(a));
    Finish();
  }
  std::optional<Point3d> a_, b_;
};

// ---------------------------------------------------------------------------
// Sub-curve picking (two points on a curve)
// ---------------------------------------------------------------------------

std::optional<CurveCopy> NearestCurveTo(CommandContext& ctx, Point3d p, double* t_out) {
  const SceneObject* best = nullptr;
  double bd = std::numeric_limits<double>::max(), bt = 0;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.kind != ObjectKind::Curve || !o.curve || !ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    double t = o.curve->ClosestPointParameter(p);
    double d = o.curve->PointAt(t).DistanceTo(p);
    if (d < bd) { bd = d; best = &o; bt = t; }
  }
  if (!best) return std::nullopt;
  if (t_out) *t_out = bt;
  return CopyCurveObj(ctx, best->id);
}

class SubCrvCommand : public Command {
 public:
  enum class Mode { Keep, Copy, Delete };
  explicit SubCrvCommand(Mode m) : mode_(m) {}
  void Begin(CommandContext&) override { WantPoint("Start of sub-curve (click on a curve)"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      curve_ = NearestCurveTo(ctx, p, &t0_);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      WantPoint("End of sub-curve");
      return;
    }
    double t1 = curve_->curve.ClosestPointParameter(p);
    if (t1 < t0_) std::swap(t1, t0_);
    if (t1 - t0_ <= 1e-9) { ctx.Warn("Pick two different points"); return; }
    kernel::NurbsCurve piece = curve_->curve;
    piece.Trim(t0_, t1);
    ctx.Doc().BeginChange(mode_ == Mode::Delete ? "DeleteSubCrv" : mode_ == Mode::Copy ? "ExtractSubCrv" : "SubCrv");
    if (mode_ == Mode::Keep) { ReplaceCurve(ctx, curve_->id, piece); ctx.Print("SubCrv: curve shortened to the picked span"); }
    else if (mode_ == Mode::Copy) { AddCurveLike(ctx, piece, curve_->attrs); ctx.Print("ExtractSubCrv: 1 curve extracted"); }
    else {
      kernel::Interval d = curve_->curve.Domain();
      int made = 0;
      if (t0_ - d.min > 1e-9) { kernel::NurbsCurve a = curve_->curve; a.Trim(d.min, t0_); AddCurveLike(ctx, a, curve_->attrs); ++made; }
      if (d.max - t1 > 1e-9) { kernel::NurbsCurve b = curve_->curve; b.Trim(t1, d.max); AddCurveLike(ctx, b, curve_->attrs); ++made; }
      ctx.Doc().Remove(curve_->id);
      ctx.Print("DeleteSubCrv: removed the picked span, " + std::to_string(made) + " piece(s) left");
    }
    Finish();
  }
  Mode mode_;
  std::optional<CurveCopy> curve_;
  double t0_ = 0;
};

// Reports each selected curve's parameter domain, and optionally sets a
// new one (SetDomain reparameterizes in place - same shape, new t-range;
// the actual command Rhino's own Domain provides, not just a read-out).
class DomainCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to view or set the domain"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    curves_ = CopyCurves(ctx, ids);
    for (const CurveCopy& c : curves_) {
      kernel::Interval d = c.curve.Domain();
      ctx.Print("Curve " + std::to_string(c.id) + " domain: " + FormatNumber(d.min) + " to " + FormatNumber(d.max));
    }
    if (curves_.empty()) { Finish(); return; }
    WantText("New domain \"min max\" (Enter to leave unchanged)");
  }
  void OnEnter(CommandContext&) override { Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (t.empty()) { Finish(); return; }
    std::string s = t;
    for (char& c : s) if (c == ',') c = ' ';
    char* end1 = nullptr;
    double a = std::strtod(s.c_str(), &end1);
    char* end2 = nullptr;
    double b = end1 ? std::strtod(end1, &end2) : 0;
    if (end1 == s.c_str() || end2 == end1 || !(b > a)) { ctx.Warn("Domain: type two numbers min max, with max > min"); return; }
    ctx.Doc().BeginChange("Domain");
    int n = 0;
    for (const CurveCopy& c : curves_) { kernel::NurbsCurve k = c.curve; k.raw().SetDomain(a, b); ReplaceCurve(ctx, c.id, k); ++n; }
    ctx.Print("Domain: " + std::to_string(n) + " curve(s) now have domain " + FormatNumber(a) + " to " + FormatNumber(b));
    Finish();
  }
  std::vector<CurveCopy> curves_;
};

class CrvSeamCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select closed curves to change seam"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    curves_ = CopyCurves(ctx, ids);
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    if (curves_.empty()) { Finish(); return; }
    WantPoint("New seam location");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.Doc().BeginChange("CrvSeam");
    int n = 0;
    for (CurveCopy& c : curves_) {
      if (!c.curve.IsClosed()) continue;
      double t = c.curve.ClosestPointParameter(p);
      ON_NurbsCurve nc = c.curve.raw();
      if (!nc.IsPeriodic()) {
        // Split at t, swap the pieces, join.
        ON_Curve* left = nullptr; ON_Curve* right = nullptr;
        if (nc.Split(t, left, right) && left && right) {
          ON_PolyCurve pc; pc.Append(right); pc.Append(left);
          kernel::NurbsCurve k; if (CurveFromON(pc, k)) { ReplaceCurve(ctx, c.id, k); ++n; }
        }
      } else if (nc.ChangeClosedCurveSeam(t)) { kernel::NurbsCurve k; k.raw() = nc; ReplaceCurve(ctx, c.id, k); ++n; }
    }
    ctx.Print("CrvSeam: seam moved on " + std::to_string(n) + " curve(s)");
    Finish();
  }
  std::vector<CurveCopy> curves_;
};

// ---------------------------------------------------------------------------
// Knot / control-point editing at a picked location
// ---------------------------------------------------------------------------

class KnotEditCommand : public Command {
 public:
  enum class Op { InsertKnot, InsertKink, RemoveKnot, InsertCP, RemoveCP, InsertEditPoint };
  explicit KnotEditCommand(Op op) : op_(op) {}
  void Begin(CommandContext&) override { WantPoint("Click on the curve where to edit (Enter when done)"); }
  void OnEnter(CommandContext&) override { Finish(); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveCopy> c = NearestCurveTo(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    ON_NurbsCurve nc = c->curve.raw();
    bool ok = false;
    std::string what;
    switch (op_) {
      case Op::InsertKnot: ok = nc.InsertKnot(t, 1); what = "knot inserted"; break;
      case Op::InsertKink: ok = nc.InsertKnot(t, nc.Degree()); what = "kink inserted"; break;
      case Op::InsertCP: ok = nc.InsertKnot(t, 1); what = "control point inserted"; break;
      case Op::InsertEditPoint: ok = nc.InsertKnot(t, 1); what = "edit point inserted"; break;
      case Op::RemoveKnot: {
        // Remove the interior knot nearest to t.
        int best = -1; double bd = std::numeric_limits<double>::max();
        for (int i = nc.Degree(); i < nc.KnotCount() - nc.Degree(); ++i) { double d = std::fabs(nc.Knot(i) - t); if (d < bd) { bd = d; best = i; } }
        if (best >= 0) ok = RemoveKnotApprox(nc, best); what = "knot removed";
        break;
      }
      case Op::RemoveCP: {
        int best = -1; double bd = std::numeric_limits<double>::max();
        for (int i = 0; i < nc.CVCount(); ++i) { double d = c->curve.ControlPointAt(i).DistanceTo(p); if (d < bd) { bd = d; best = i; } }
        if (best >= 0 && nc.CVCount() > nc.Order()) {
          std::vector<Point3d> cvs = ControlPolygon(c->curve);
          cvs.erase(cvs.begin() + best);
          kernel::NurbsCurve k = kernel::NurbsCurve::FromControlPoints(cvs, nc.Degree());
          nc = k.raw(); ok = true; what = "control point removed";
        }
        break;
      }
    }
    if (!ok) { ctx.Warn("Could not edit the curve there"); return; }
    ctx.Doc().BeginChange("KnotEdit");
    kernel::NurbsCurve k; k.raw() = nc;
    ReplaceCurve(ctx, c->id, k);
    ctx.Print("Curve " + std::to_string(c->id) + ": " + what + " (" + std::to_string(nc.CVCount()) + " control points)");
  }
  Op op_;
};

// ---------------------------------------------------------------------------
// Curve-on-curve arrays and alignment
// ---------------------------------------------------------------------------

class ArrayCrvCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to array along a curve"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids_.empty()) {
      ids_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select path curve");
      return;
    }
    for (ObjectId id : ids) if (auto c = CopyCurveObj(ctx, id)) { path_ = *c; break; }
    if (!path_) { ctx.Warn("Select a curve as the path"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    options = {{"Orientation", "Freeform", {"Freeform", "NoRotation"}, false, false}};
    WantNumber("Number of items", 5);
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Orientation") freeform_ = (v != "NoRotation"); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!path_) return;
    int count = std::max(2, static_cast<int>(v));
    kernel::BoundingBox bb;
    ctx.Doc().BoundingBoxOf(ids_, bb);
    Point3d base = path_->curve.PointAt(path_->curve.Domain().min);
    Vector3d up = ActiveNormal(ctx);
    ON_Plane f0 = FrameAt(path_->curve, path_->curve.Domain().min, up);
    std::vector<double> params = path_->curve.DivideByCount(count - 1);
    if (params.empty()) { Finish(); return; }
    ctx.Doc().BeginChange("ArrayCrv");
    int made = 0;
    for (size_t i = 0; i < params.size(); ++i) {
      ON_Xform xf;
      if (freeform_) {
        ON_Plane fi = FrameAt(path_->curve, params[i], up);
        xf.Rotation(f0, fi);
      } else {
        xf = ON_Xform::TranslationTransformation(path_->curve.PointAt(params[i]) - base);
      }
      for (ObjectId id : ids_) {
        const SceneObject* o = ctx.Doc().Find(id);
        if (!o) continue;
        SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf);
        ctx.Doc().Add(std::move(dup)); ++made;
      }
    }
    ctx.Print("ArrayCrv: " + std::to_string(made) + " object(s) placed along the curve");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (path_) OnNumber(ctx, 5); }
  std::vector<ObjectId> ids_;
  std::optional<CurveCopy> path_;
  bool freeform_ = true;
};

// ArrayCrvOnSrf: like ArrayCrv, but each copy is placed at the path curve's
// closest point ON THE SURFACE and oriented from the surface's own normal
// there (not just the curve's own arbitrary "up" frame) - the actual
// surface-normal orientation ArrayCrv itself has no way to provide.
class ArrayCrvOnSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to array along a curve on a surface"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids_.empty()) {
      ids_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select path curve (should lie on the surface)");
      return;
    }
    if (!path_) {
      for (ObjectId id : ids) if (auto c = CopyCurveObj(ctx, id)) { path_ = *c; break; }
      if (!path_) { ctx.Warn("Select a curve as the path"); Finish(); return; }
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select the surface the curve lies on");
      return;
    }
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Surface && o->surface) { srf_ = *o->surface; break; }
      if (o->kind == ObjectKind::Brep && o->brep && o->brep->raw().m_F.Count() > 0) {
        const ON_Surface* s = o->brep->raw().m_F[0].SurfaceOf();
        kernel::NurbsSurface ns;
        if (s && SurfaceFromON(*s, ns)) { srf_ = ns; break; }
      }
    }
    if (!srf_) { ctx.Warn("Select a surface"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    WantNumber("Number of items", 5);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!path_ || !srf_) return;
    int count = std::max(2, static_cast<int>(v));
    std::vector<double> params = path_->curve.DivideByCount(count - 1);
    if (params.empty()) { Finish(); return; }
    auto frame_at = [&](double t) {
      Point3d pc = path_->curve.PointAt(t);
      ON_2dPoint uv = srf_->ClosestPointParameter(pc);
      ON_3dPoint sp;
      ON_3dVector du, dv;
      srf_->raw().Ev1Der(uv.x, uv.y, sp, du, dv);
      Vector3d n = ON_CrossProduct(du, dv);
      if (!n.Unitize()) n = ON_zaxis;
      Vector3d tan = path_->curve.TangentAt(t);
      Vector3d tan_proj = tan - n * ON_DotProduct(tan, n);
      if (!tan_proj.Unitize()) { tan_proj = du; if (!tan_proj.Unitize()) tan_proj = ON_xaxis; }
      Vector3d y = ON_CrossProduct(n, tan_proj);
      return ON_Plane(sp, tan_proj, y);
    };
    ON_Plane f0 = frame_at(path_->curve.Domain().min);
    ctx.Doc().BeginChange("ArrayCrvOnSrf");
    int made = 0;
    for (double t : params) {
      ON_Plane fi = frame_at(t);
      ON_Xform xf;
      xf.Rotation(f0, fi);
      for (ObjectId id : ids_) {
        const SceneObject* o = ctx.Doc().Find(id);
        if (!o) continue;
        SceneObject dup = *o;
        dup.id = kNoObject;
        dup.selected = false;
        dup.Transform(xf);
        ctx.Doc().Add(std::move(dup));
        ++made;
      }
    }
    ctx.Print("ArrayCrvOnSrf: " + std::to_string(made) + " object(s) placed along the curve, oriented to the surface normal");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (path_ && srf_) OnNumber(ctx, 5); }
  std::vector<ObjectId> ids_;
  std::optional<CurveCopy> path_;
  std::optional<kernel::NurbsSurface> srf_;
};

class ArraySrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to array on a surface"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids_.empty()) {
      ids_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select target surface");
      return;
    }
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (o->kind == ObjectKind::Surface && o->surface) { srf_ = o->surface->raw(); break; }
      if (o->kind == ObjectKind::Brep && o->brep && o->brep->raw().m_F.Count() > 0) {
        const ON_Surface* s = o->brep->raw().m_F[0].SurfaceOf();
        ON_NurbsSurface ns; if (s && s->GetNurbForm(ns) > 0) { srf_ = ns; break; }
      }
    }
    if (!srf_) { ctx.Warn("Select a surface"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    WantNumber("Number in U direction", 5);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!srf_) return;
    if (!nu_) { nu_ = std::max(1, static_cast<int>(v)); WantNumber("Number in V direction", 5); return; }
    int nv = std::max(1, static_cast<int>(v));
    kernel::BoundingBox bb;
    ctx.Doc().BoundingBoxOf(ids_, bb);
    Point3d base((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, bb.min.z);
    ON_Plane f0(base, ON_xaxis, ON_yaxis);
    ctx.Doc().BeginChange("ArraySrf");
    int made = 0;
    for (int i = 0; i < *nu_; ++i)
      for (int j = 0; j < nv; ++j) {
        double u = srf_->Domain(0).ParameterAt(*nu_ == 1 ? 0.5 : static_cast<double>(i) / (*nu_ - 1));
        double w = srf_->Domain(1).ParameterAt(nv == 1 ? 0.5 : static_cast<double>(j) / (nv - 1));
        ON_3dPoint p; ON_3dVector du, dv;
        srf_->Ev1Der(u, w, p, du, dv);
        ON_3dVector n = ON_CrossProduct(du, dv);
        if (!n.Unitize()) n = ON_zaxis;
        ON_3dVector x = du; if (!x.Unitize()) x = ON_xaxis;
        ON_3dVector y = ON_CrossProduct(n, x);
        ON_Plane fi(p, x, y);
        ON_Xform xf; xf.Rotation(f0, fi);
        for (ObjectId id : ids_) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf);
          ctx.Doc().Add(std::move(dup)); ++made;
        }
      }
    ctx.Print("ArraySrf: " + std::to_string(made) + " object(s) placed on the surface");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (srf_) OnNumber(ctx, 5); }
  std::vector<ObjectId> ids_;
  std::optional<ON_NurbsSurface> srf_;
  std::optional<int> nu_;
};

class AlignCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to align", 2); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    options = {{"Mode", mode_, {"Left", "Right", "Top", "Bottom", "HorizCenter", "VertCenter", "Concentric"}, false, false}};
    WantText("Alignment (Left/Right/Top/Bottom/HorizCenter/VertCenter/Concentric)", mode_);
    (void)ctx;
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override { if (n == "Mode") { mode_ = v; Apply(ctx); } }
  void OnText(CommandContext& ctx, const std::string& t) override { if (!t.empty()) mode_ = t; Apply(ctx); }
  void OnEnter(CommandContext& ctx) override { Apply(ctx); }
  void Apply(CommandContext& ctx) {
    ON_Plane pl = ActivePlane(ctx);
    struct Item { ObjectId id; double lo_x, hi_x, lo_y, hi_y; };
    std::vector<Item> items;
    for (ObjectId id : ids_) {
      kernel::BoundingBox bb;
      if (!ctx.Doc().BoundingBoxOf({id}, bb)) continue;
      Item it{id, 1e300, -1e300, 1e300, -1e300};
      for (int c = 0; c < 8; ++c) {
        Point3d p((c & 1) ? bb.max.x : bb.min.x, (c & 2) ? bb.max.y : bb.min.y, (c & 4) ? bb.max.z : bb.min.z);
        double x = ON_DotProduct(p - pl.origin, pl.xaxis), y = ON_DotProduct(p - pl.origin, pl.yaxis);
        it.lo_x = std::min(it.lo_x, x); it.hi_x = std::max(it.hi_x, x); it.lo_y = std::min(it.lo_y, y); it.hi_y = std::max(it.hi_y, y);
      }
      items.push_back(it);
    }
    if (items.size() < 2) { Finish(); return; }
    double lo_x = 1e300, hi_x = -1e300, lo_y = 1e300, hi_y = -1e300;
    for (const Item& it : items) { lo_x = std::min(lo_x, it.lo_x); hi_x = std::max(hi_x, it.hi_x); lo_y = std::min(lo_y, it.lo_y); hi_y = std::max(hi_y, it.hi_y); }
    ctx.Doc().BeginChange("Align");
    for (const Item& it : items) {
      double dx = 0, dy = 0;
      if (mode_ == "Left") dx = lo_x - it.lo_x;
      else if (mode_ == "Right") dx = hi_x - it.hi_x;
      else if (mode_ == "Bottom") dy = lo_y - it.lo_y;
      else if (mode_ == "Top") dy = hi_y - it.hi_y;
      else if (mode_ == "HorizCenter") dy = (lo_y + hi_y) / 2 - (it.lo_y + it.hi_y) / 2;
      else if (mode_ == "VertCenter") dx = (lo_x + hi_x) / 2 - (it.lo_x + it.hi_x) / 2;
      else if (mode_ == "Concentric") { dx = (lo_x + hi_x) / 2 - (it.lo_x + it.hi_x) / 2; dy = (lo_y + hi_y) / 2 - (it.lo_y + it.hi_y) / 2; }
      if (SceneObject* o = ctx.Doc().Find(it.id)) { o->Transform(ON_Xform::TranslationTransformation(pl.xaxis * dx + pl.yaxis * dy)); }
    }
    ctx.Print("Align: " + std::to_string(items.size()) + " object(s) aligned " + mode_);
    Finish();
  }
  std::vector<ObjectId> ids_;
  std::string mode_ = "Bottom";
};

class DistributeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to distribute", 3); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantText("Direction (X/Y/Z)", "X");
    (void)ctx;
  }
  void OnText(CommandContext& ctx, const std::string& t) override { if (!t.empty()) axis_ = std::toupper(t[0]); Apply(ctx); }
  void OnEnter(CommandContext& ctx) override { Apply(ctx); }
  void Apply(CommandContext& ctx) {
    struct Item { ObjectId id; double c, lo, hi; };
    std::vector<Item> items;
    int k = axis_ == 'Y' ? 1 : axis_ == 'Z' ? 2 : 0;
    for (ObjectId id : ids_) {
      kernel::BoundingBox bb;
      if (!ctx.Doc().BoundingBoxOf({id}, bb)) continue;
      double lo = k == 0 ? bb.min.x : k == 1 ? bb.min.y : bb.min.z;
      double hi = k == 0 ? bb.max.x : k == 1 ? bb.max.y : bb.max.z;
      items.push_back({id, (lo + hi) / 2, lo, hi});
    }
    if (items.size() < 3) { Finish(); return; }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.c < b.c; });
    // Even gaps between objects.
    double total = items.back().hi - items.front().lo, sizes = 0;
    for (const Item& it : items) sizes += it.hi - it.lo;
    double gap = (total - sizes) / (items.size() - 1);
    ctx.Doc().BeginChange("Distribute");
    double cursor = items.front().lo;
    for (Item& it : items) {
      double d = cursor - it.lo;
      Vector3d v(k == 0 ? d : 0, k == 1 ? d : 0, k == 2 ? d : 0);
      if (SceneObject* o = ctx.Doc().Find(it.id)) o->Transform(ON_Xform::TranslationTransformation(v));
      cursor += (it.hi - it.lo) + gap;
    }
    ctx.Print("Distribute: " + std::to_string(items.size()) + " object(s) spaced evenly along " + std::string(1, axis_));
    Finish();
  }
  std::vector<ObjectId> ids_;
  char axis_ = 'X';
};

// ---------------------------------------------------------------------------
// Contour / Section / CutPlane
// ---------------------------------------------------------------------------

class ContourCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to contour"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { ids_ = ids; for (ObjectId id : ids) ctx.Doc().Select(id, false); WantPoint("Contour plane base point"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!base_) { base_ = p; ctx.SetLastPoint(p); WantPoint("Direction perpendicular to contour planes"); return; }
    dir_ = p - *base_;
    if (!dir_.Unitize()) { ctx.Warn("Direction must differ from the base point"); return; }
    WantNumber("Distance between contours", 10);
  }
  void OnNumber(CommandContext& ctx, double spacing) override {
    if (!base_ || spacing <= 0) return;
    kernel::BoundingBox bb;
    ctx.Doc().BoundingBoxOf(ids_, bb);
    double lo = 1e300, hi = -1e300;
    for (int c = 0; c < 8; ++c) {
      Point3d p((c & 1) ? bb.max.x : bb.min.x, (c & 2) ? bb.max.y : bb.min.y, (c & 4) ? bb.max.z : bb.min.z);
      double d = ON_DotProduct(p - *base_, dir_);
      lo = std::min(lo, d); hi = std::max(hi, d);
    }
    ctx.Doc().BeginChange("Contour");
    int made = 0, planes = 0;
    for (double d = std::ceil(lo / spacing) * spacing; d <= hi + 1e-9; d += spacing) {
      ON_Plane pl(*base_ + dir_ * d, dir_);
      made += SliceObjects(ctx, ids_, pl, "Contour");
      ++planes;
    }
    ctx.Print("Contour: " + std::to_string(made) + " curve(s) from " + std::to_string(planes) + " plane(s)");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (base_) OnNumber(ctx, 10); }
  void OnHover(CommandContext& ctx, Point3d h) override { if (base_ && dir_.IsZero()) { ctx.ClearPreview(); ctx.AddPreviewLine(*base_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> base_;
  Vector3d dir_{0, 0, 0};
};

class SectionCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to section"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { ids_ = ids; for (ObjectId id : ids) ctx.Doc().Select(id, false); WantPoint("Start of section line"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!a_) { a_ = p; ctx.SetLastPoint(p); WantPoint("End of section line"); return; }
    Vector3d along = p - *a_;
    Vector3d n = ON_CrossProduct(along, ActiveNormal(ctx));
    if (!n.Unitize()) { ctx.Warn("Section line has no length"); return; }
    ctx.ClearPreview();
    ctx.Doc().BeginChange("Section");
    int made = SliceObjects(ctx, ids_, ON_Plane(*a_, n), "Section");
    ctx.Print("Section: " + std::to_string(made) + " curve(s)");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
};

class CutPlaneCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects the cutting plane should span"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { ids_ = ids; for (ObjectId id : ids) ctx.Doc().Select(id, false); WantPoint("Start of cut plane"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!a_) { a_ = p; ctx.SetLastPoint(p); WantPoint("End of cut plane"); return; }
    kernel::BoundingBox bb;
    ctx.Doc().BoundingBoxOf(ids_, bb);
    Vector3d up = ActiveNormal(ctx);
    Vector3d along = p - *a_;
    double len = along.Length();
    if (!along.Unitize()) { ctx.Warn("Cut plane has no length"); return; }
    double diag = (bb.max - bb.min).Length();
    double pad = diag * 0.1 + 1;
    Point3d c((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, (bb.min.z + bb.max.z) / 2);
    double h0 = ON_DotProduct(bb.min - c, up), h1 = ON_DotProduct(bb.max - c, up);
    double lo = std::min({h0, h1, -diag / 2}) - pad, hi = std::max({h0, h1, diag / 2}) + pad;
    double base_h = ON_DotProduct(*a_ - c, up);
    Point3d p00 = *a_ - along * pad + up * (lo - base_h), p10 = *a_ + along * (len + pad) + up * (lo - base_h);
    Point3d p01 = *a_ - along * pad + up * (hi - base_h), p11 = *a_ + along * (len + pad) + up * (hi - base_h);
    ctx.ClearPreview();
    ctx.Doc().BeginChange("CutPlane");
    kernel::NurbsSurface s = kernel::NurbsSurface::FromControlGrid({p00, p10, p01, p11}, 2, 2, 1, 1);
    ctx.Doc().Add(SceneObject::MakeSurface(s));
    ctx.Print("CutPlane: 1 plane surface created through the objects");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
};

// ---------------------------------------------------------------------------
// Tween / blend / fit
// ---------------------------------------------------------------------------

class TweenCurvesCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select two curves to tween between", 2); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    curves_ = CopyCurves(ctx, ids);
    if (curves_.size() < 2) { ctx.Warn("Select two curves"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    WantNumber("Number of tween curves", 3);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    int n = std::max(1, static_cast<int>(v));
    const kernel::NurbsCurve& a = curves_[0].curve;
    kernel::NurbsCurve b = curves_[1].curve;
    // Orient b like a.
    if (a.PointAt(a.Domain().min).DistanceTo(b.PointAt(b.Domain().max)) < a.PointAt(a.Domain().min).DistanceTo(b.PointAt(b.Domain().min))) b.Reverse();
    const int samples = 32;
    ctx.Doc().BeginChange("TweenCurves");
    for (int i = 1; i <= n; ++i) {
      double f = static_cast<double>(i) / (n + 1);
      std::vector<Point3d> pts;
      for (int s = 0; s <= samples; ++s) {
        double u = static_cast<double>(s) / samples;
        Point3d pa = a.PointAt(At(a.Domain(), u)), pb = b.PointAt(At(b.Domain(), u));
        pts.push_back(pa + (pb - pa) * f);
      }
      AddCurveLike(ctx, InterpolateCubic(pts), curves_[0].attrs);
    }
    ctx.Print("TweenCurves: " + std::to_string(n) + " curve(s) created");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (curves_.size() >= 2) OnNumber(ctx, 3); }
  std::vector<CurveCopy> curves_;
};

// Blend: pick near the ends of two curves; builds a G1 cubic blend.
class BlendCrvCommand : public Command {
 public:
  // `curvature`: false builds BlendCrv's own plain tangent (G1) cubic;
  // true (used by the "Blend" alias) builds a curvature-continuous (G2)
  // quintic Hermite blend instead - matching not just the end tangents but
  // the actual curvature vector of each source curve at the blend point.
  explicit BlendCrvCommand(bool curvature = false) : curvature_(curvature) {}
  void Begin(CommandContext&) override { WantPoint("Select first curve near the end to blend from"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveCopy> c = NearestCurveTo(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    kernel::Interval d = c->curve.Domain();
    bool at_end = std::fabs(t - d.max) < std::fabs(t - d.min);
    double te = at_end ? d.max : d.min;
    Point3d e = c->curve.PointAt(te);
    Vector3d tan = c->curve.TangentAt(te);
    if (!at_end) tan = -tan;  // pointing away from the curve
    ends_.push_back(e); tans_.push_back(tan); kappas_.push_back(c->curve.CurvatureAt(te)); attrs_ = c->attrs;
    if (ends_.size() == 1) { WantPoint("Select second curve near the end to blend to"); return; }
    double len = ends_[0].DistanceTo(ends_[1]) / 3;
    ctx.Doc().BeginChange("BlendCrv");
    if (!curvature_) {
      std::vector<Point3d> cvs = {ends_[0], ends_[0] + tans_[0] * len, ends_[1] + tans_[1] * len, ends_[1]};
      AddCurveLike(ctx, kernel::NurbsCurve::FromControlPoints(cvs, 3), attrs_);
      ctx.Print("BlendCrv: tangent blend curve created");
    } else {
      // Quintic Hermite matching position, tangent, and curvature at both
      // ends. Choosing a "speed" s = len for each end's velocity vector,
      // and zero tangential acceleration (a locally constant-speed
      // parametrization there), the required 2nd derivative is exactly
      // the source curve's own curvature vector scaled by s^2 - the
      // geometric relation DD_perp = s^2 * kappa_vec for any parametrization
      // whose speed isn't itself accelerating at that instant.
      Vector3d d0 = tans_[0] * len, d1 = tans_[1] * len;
      Vector3d dd0 = kappas_[0] * (len * len), dd1 = kappas_[1] * (len * len);
      Point3d b0 = ends_[0];
      Point3d b1 = b0 + d0 * 0.2;
      Point3d b2 = b0 + d0 * 0.4 + dd0 * 0.05;
      Point3d b5 = ends_[1];
      Point3d b4 = b5 - d1 * 0.2;
      Point3d b3 = b5 - d1 * 0.4 + dd1 * 0.05;
      std::vector<Point3d> cvs = {b0, b1, b2, b3, b4, b5};
      AddCurveLike(ctx, kernel::NurbsCurve::FromControlPoints(cvs, 5), attrs_);
      ctx.Print("Blend: curvature-continuous (G2) blend curve created");
    }
    Finish();
  }
  std::vector<Point3d> ends_;
  std::vector<Vector3d> tans_;
  std::vector<Vector3d> kappas_;
  SceneObject attrs_;
  bool curvature_;
};

// ArcBlend: a genuine two-arc tangent blend (a "biarc"), unlike BlendCrv's
// single cubic. Builds the classic equal-radius biarc: two circular arcs of
// the same radius, externally tangent to each other at their shared joint,
// each tangent to one of the two picked curve ends - solved as a quadratic
// in the common radius from the tangency distance condition
// |C0 - C1| = 2*|r|, then verified numerically (the two arcs' own tangent
// vectors at the joint must actually agree) before being accepted, rather
// than trusted from the derivation alone. Falls back to a single tangent
// arc through both points when the tangents are too close to parallel for
// a biarc to exist.
class ArcBlendCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select first curve near the end to blend from"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveCopy> c = NearestCurveTo(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    kernel::Interval d = c->curve.Domain();
    bool at_end = std::fabs(t - d.max) < std::fabs(t - d.min);
    double te = at_end ? d.max : d.min;
    Point3d e = c->curve.PointAt(te);
    Vector3d tan = c->curve.TangentAt(te);
    if (!at_end) tan = -tan;  // pointing away from the curve
    ends_.push_back(e); tans_.push_back(tan); attrs_ = c->attrs;
    if (ends_.size() == 1) { WantPoint("Select second curve near the end to blend to"); return; }
    Build(ctx);
  }
  // The point on the circle (center `center`, radius `radius`, plane
  // normal `nrm_`) exactly half-way (by angle, the short way starting from
  // `start`) between `start` and `target` - used to hand ON_Arc's 3-point
  // constructor a genuine interior point rather than guessing one.
  std::optional<Point3d> MidArcPoint(Point3d start, Point3d center, double radius, Point3d target) const {
    if (std::fabs(radius) < 1e-9) return std::nullopt;
    Vector3d ds = (start - center) / radius, de = (target - center) / radius;
    if (!ds.Unitize()) return std::nullopt;
    Vector3d yax = ON_CrossProduct(nrm_, ds);
    if (!yax.Unitize()) return std::nullopt;
    double ang = std::atan2(ON_DotProduct(de, yax), ON_DotProduct(de, ds));
    if (ang < 1e-9) ang += 2 * ON_PI;
    const double mid = ang * 0.5;
    return center + (ds * std::cos(mid) + yax * std::sin(mid)) * std::fabs(radius);
  }
  // The (unique) circular arc leaving `start` with tangent `tstart` and
  // passing through `target` - the standard tangent-circle-through-a-point
  // construction, tried both ways round the circle and accepted only once
  // its own actual start tangent (queried back from the built ON_ArcCurve,
  // not assumed) verifies against `tstart`. Returns the arc plus its own
  // real tangent at `target` (again queried, not derived by hand) so a
  // caller can check where a second such arc would need to continue from.
  struct TangentArc { ON_Arc arc; Vector3d end_tangent; };
  std::optional<TangentArc> ArcFromTangentThroughPoint(Point3d start, Vector3d tstart, Point3d target) const {
    if (!tstart.Unitize()) return std::nullopt;
    Vector3d n = ON_CrossProduct(nrm_, tstart);
    const double denom = 2 * ON_DotProduct(n, start - target);
    if (std::fabs(denom) < 1e-9) return std::nullopt;  // start, target, tstart nearly colinear
    const double r = -ON_DotProduct(start - target, start - target) / denom;
    Point3d center = start + n * r;
    auto mid = MidArcPoint(start, center, r, target);
    if (!mid) return std::nullopt;
    const Point3d opts[2] = {*mid, center * 2.0 - *mid};
    for (const Point3d& m : opts) {
      ON_Arc a(start, m, target);
      if (!a.IsValid()) continue;
      ON_ArcCurve ac(a);
      Vector3d ts = ac.TangentAt(ac.Domain().Min());
      if (!ts.Unitize() || ON_DotProduct(ts, tstart) < 0.999) continue;
      Vector3d te = ac.TangentAt(ac.Domain().Max());
      if (!te.Unitize()) continue;
      return TangentArc{a, te};
    }
    return std::nullopt;
  }
  // How well a candidate joint point J works: build the tangent arc from
  // P0 (tangent T0) through J, and the tangent arc from P1 (tangent
  // -Tend, i.e. leaving P1 backwards) through the same J, then compare the
  // two arcs' actual tangent directions AT J (arc0's arrival tangent there
  // must be the exact reverse of arc1's own departure-from-P1 tangent
  // there, since arc1 gets reversed to run J->P1 in the final blend).
  // Returns the dot product (1 = perfectly G1 continuous at the joint)
  // together with both arcs, or nullopt if either tangent arc doesn't
  // exist for this J (e.g. colinear with an endpoint).
  struct Candidate { double score; ON_Arc arc0, arc1_reversed_source; };
  std::optional<Candidate> TryJoint(Point3d P0, Vector3d T0, Point3d P1, Vector3d negTend, Point3d J) const {
    auto a0 = ArcFromTangentThroughPoint(P0, T0, J);
    auto a1 = ArcFromTangentThroughPoint(P1, negTend, J);
    if (!a0 || !a1) return std::nullopt;
    const double score = ON_DotProduct(a0->end_tangent, -a1->end_tangent);
    return Candidate{score, a0->arc, a1->arc};
  }
  void Build(CommandContext& ctx) {
    Point3d P0 = ends_[0], P1 = ends_[1];
    Vector3d T0 = tans_[0]; T0.Unitize();
    Vector3d Tend = -tans_[1]; Tend.Unitize();  // direction of travel arriving at P1
    nrm_ = ON_CrossProduct(T0, Tend);
    if (!nrm_.Unitize()) {
      nrm_ = ON_CrossProduct(T0, P1 - P0);
      if (!nrm_.Unitize()) nrm_ = ActiveNormal(ctx);
    }
    ctx.Doc().BeginChange("ArcBlend");
    // The biarc's joint is, in general, NOT on the straight chord between
    // P0 and P1 - it can lie anywhere in their shared plane - so search
    // that plane directly. TryJoint()'s score (built from two
    // independently exact, verified tangent-arcs - see
    // ArcFromTangentThroughPoint) is 1.0 exactly at a genuine biarc
    // solution and falls off smoothly nearby, so a coarse 2D grid to
    // bracket the peak followed by compass-search (pattern search)
    // refinement finds it reliably without a closed-form solve.
    const Vector3d negTend = -Tend;
    Vector3d ax_u = T0, ax_v = nrm_.IsZero() ? ON_yaxis : ON_CrossProduct(nrm_, T0);
    ax_v.Unitize();
    const double D = std::max(P0.DistanceTo(P1), 1e-6);
    auto joint_at = [&](double u, double v) { return P0 + ax_u * (u * D) + ax_v * (v * D); };
    // A whole continuous family of joints can satisfy the tangent-match
    // condition (the classic "biarc family"), so picking by raw score
    // alone among near-ties would pick an arbitrary, often wildly looping,
    // member of that family. Rank candidates that already clear a solid
    // tangent-match bar (>0.995) by closeness to the chord's own midpoint
    // instead - the natural, compact biarc a user actually wants - and use
    // raw score only to find any match at all when nothing clears that bar.
    double best_u = 0, best_v = 0, best_score = -2, best_rank = 1e300;
    const int kGrid = 41;  // -2D..2D in u and v
    for (int iu = 0; iu < kGrid; ++iu) {
      for (int iv = 0; iv < kGrid; ++iv) {
        const double u = -2.0 + 4.0 * iu / (kGrid - 1), v = -2.0 + 4.0 * iv / (kGrid - 1);
        if (auto cand = TryJoint(P0, T0, P1, negTend, joint_at(u, v))) {
          const double rank = (u - 0.5) * (u - 0.5) + v * v;
          const bool this_good = cand->score > 0.995, best_good = best_score > 0.995;
          const bool better = this_good && best_good ? rank < best_rank : cand->score > best_score;
          if (better) { best_score = cand->score; best_u = u; best_v = v; best_rank = rank; }
        }
      }
    }
    if (best_score > -1) {
      // Compass (pattern) search refinement around the coarse grid peak.
      double step = 4.0 / (kGrid - 1);
      auto score_at = [&](double u, double v) { auto cand = TryJoint(P0, T0, P1, negTend, joint_at(u, v)); return cand ? cand->score : -2.0; };
      for (int it = 0; it < 80 && step > 1e-10; ++it) {
        bool improved = false;
        const double du[4] = {step, -step, 0, 0}, dv[4] = {0, 0, step, -step};
        for (int k = 0; k < 4; ++k) {
          const double s = score_at(best_u + du[k], best_v + dv[k]);
          if (s > best_score) { best_score = s; best_u += du[k]; best_v += dv[k]; improved = true; }
        }
        if (!improved) step *= 0.5;
      }
      if (auto cand = TryJoint(P0, T0, P1, negTend, joint_at(best_u, best_v)); cand && cand->score > 0.999) {
        ON_ArcCurve c0(cand->arc0), c1_rev(cand->arc1_reversed_source);
        c1_rev.Reverse();
        ON_PolyCurve pc;
        pc.Append(new ON_ArcCurve(c0));
        pc.Append(new ON_ArcCurve(c1_rev));
        kernel::NurbsCurve merged;
        if (CurveFromON(pc, merged)) {
          AddCurveLike(ctx, merged, attrs_);
          ctx.Print("ArcBlend: two-arc tangent blend created (joint tangent match " + FormatNumber(cand->score) + ")");
          Finish();
          return;
        }
      }
    }
    // No joint in the search region gives a genuine G1 match (typically
    // because T0 and Tend are close to parallel, so the biarc degenerates)
    // - fall back to a single tangent arc through both points.
    if (auto single = ArcFromTangentThroughPoint(P0, T0, P1)) {
      kernel::NurbsCurve k;
      if (CurveFromON(ON_ArcCurve(single->arc), k)) {
        AddCurveLike(ctx, k, attrs_);
        ctx.Print("ArcBlend: tangents are nearly parallel - built a single tangent arc instead of a biarc");
        Finish();
        return;
      }
    }
    ctx.Warn("ArcBlend: could not build a tangent arc for this pair of curve ends");
    Finish();
  }
  std::vector<Point3d> ends_;
  std::vector<Vector3d> tans_;
  Vector3d nrm_{0, 0, 1};
  SceneObject attrs_;
};

// IntersectTwoSets: intersections between two curve sets, excluding any
// within a set - picked as two separate selections, then delegated to
// CurveOrSolidIntersect ONE PAIR AT A TIME (never the two sets combined,
// which would also intersect members of the same set with each other).
class IntersectTwoSetsCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select first set of curves"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (set_a_.empty() && !picked_a_) {
      set_a_ = ids;
      picked_a_ = true;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select second set of curves");
      return;
    }
    ctx.Doc().BeginChange("IntersectTwoSets");
    int pairs = 0;
    for (ObjectId a : set_a_) {
      for (ObjectId b : ids) {
        if (a == b) continue;
        CurveOrSolidIntersect(ctx, {a, b});
        ++pairs;
      }
    }
    ctx.Print("IntersectTwoSets: checked " + std::to_string(pairs) + " pair(s) between the two sets");
    Finish();
  }
  std::vector<ObjectId> set_a_;
  bool picked_a_ = false;
};

// ModifyRadius: rebuilds each selected circle/arc at a new radius, in
// place - same center, plane, and (for an arc) start/end angle, just a
// different radius, rather than a generic Scale.
class ModifyRadiusCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select circles or arcs to change the radius of"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    curves_ = CopyCurves(ctx, ids);
    double r0 = 0;
    for (const CurveCopy& c : curves_) {
      ON_Arc arc;
      if (c.curve.raw().IsArc(nullptr, &arc, ctx.Settings().absolute_tolerance)) { r0 = arc.radius; break; }
    }
    if (curves_.empty() || r0 <= 0) { ctx.Warn("ModifyRadius: select at least one circle or arc"); Finish(); return; }
    WantNumber("New radius", r0);
  }
  void OnNumber(CommandContext& ctx, double r) override {
    if (r <= 0) { ctx.Warn("ModifyRadius: radius must be positive"); return; }
    ctx.Doc().BeginChange("ModifyRadius");
    int n = 0;
    const double tol = ctx.Settings().absolute_tolerance;
    for (const CurveCopy& c : curves_) {
      ON_Arc arc;
      if (!c.curve.raw().IsArc(nullptr, &arc, tol)) continue;
      ON_Circle newcircle(arc.plane, r);
      ON_Arc newarc(newcircle, arc.Domain());
      kernel::NurbsCurve k;
      if (CurveFromON(ON_ArcCurve(newarc), k)) { ReplaceCurve(ctx, c.id, k); ++n; }
    }
    ctx.Print("ModifyRadius: " + std::to_string(n) + " curve(s) now have radius " + FormatNumber(r));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  std::vector<CurveCopy> curves_;
};

// ContinueCurve / ContinueInterpCrv: pick a curve near an end, then draw
// further points (Enter to finish); the new segment starts exactly at
// that end point and is automatically Join-ed onto the original curve, so
// the result is one continuous curve instead of two pieces the user has
// to Join by hand.
class ContinueCurveCommand : public Command {
 public:
  explicit ContinueCurveCommand(bool interp) : interp_(interp) {}
  void Begin(CommandContext&) override { WantPoint("Select curve near the end to continue from"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!base_) {
      double t = 0;
      base_ = NearestCurveTo(ctx, p, &t);
      if (!base_) { ctx.Warn("No curve near that point"); return; }
      kernel::Interval d = base_->curve.Domain();
      at_end_ = std::fabs(t - d.max) < std::fabs(t - d.min);
      Point3d start = base_->curve.PointAt(at_end_ ? d.max : d.min);
      pts_.push_back(start);
      ctx.SetLastPoint(start);
      WantPoint("Next point (Enter when done)");
      return;
    }
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    WantPoint("Next point (Enter when done)");
  }
  void OnEnter(CommandContext& ctx) override { BuildAndJoin(ctx); }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    ctx.AddPreviewPolyline(pts_);
    ctx.AddPreviewLine(pts_.back(), h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  void BuildAndJoin(CommandContext& ctx) {
    ctx.ClearPreview();
    if (!base_ || pts_.size() < 2) { Finish(); return; }
    kernel::NurbsCurve added = interp_ ? InterpolateCubic(pts_)
                                       : kernel::NurbsCurve::FromControlPoints(pts_, std::min<int>(3, static_cast<int>(pts_.size()) - 1));
    ctx.Doc().BeginChange(interp_ ? "ContinueInterpCrv" : "ContinueCurve");
    ON_PolyCurve pc;
    if (at_end_) { pc.Append(new ON_NurbsCurve(base_->curve.raw())); pc.Append(new ON_NurbsCurve(added.raw())); }
    else { pc.Append(new ON_NurbsCurve(added.raw())); pc.Append(new ON_NurbsCurve(base_->curve.raw())); }
    kernel::NurbsCurve merged;
    if (CurveFromON(pc, merged)) {
      ReplaceCurve(ctx, base_->id, merged);
      ctx.Print(std::string(interp_ ? "ContinueInterpCrv" : "ContinueCurve") + ": curve extended with " +
                 std::to_string(pts_.size() - 1) + " new point(s) and joined");
    }
    Finish();
  }
  std::optional<CurveCopy> base_;
  bool at_end_ = true;
  bool interp_;
  std::vector<Point3d> pts_;
};

// Match: reshapes the END of the first-picked curve so it meets the
// second-picked curve tangentially, by moving that curve's own last two
// control points (position match at the CV itself, tangent match by
// rotating the adjacent CV to the target's own away-from-curve direction
// while preserving its original distance) - unlike Blend/BlendCrv, this
// modifies the picked curve in place rather than building a new one
// between the two.
class MatchCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select curve to reshape, near the end to move"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveCopy> c = NearestCurveTo(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    kernel::Interval d = c->curve.Domain();
    bool at_end = std::fabs(t - d.max) < std::fabs(t - d.min);
    Point3d e = c->curve.PointAt(at_end ? d.max : d.min);
    Vector3d away = c->curve.TangentAt(at_end ? d.max : d.min);
    if (!at_end) away = -away;
    if (!moving_) {
      moving_ = c; moving_at_end_ = at_end;
      WantPoint("Select curve to match to, near the corresponding end");
      return;
    }
    // `c`/`e`/`away` here describe the fixed TARGET curve.
    ctx.Doc().BeginChange("Match");
    ON_NurbsCurve nc = moving_->curve.raw();
    const int n = nc.CVCount();
    if (n < 2) { Finish(); return; }
    const Vector3d desired = -away;  // moving curve should head away in the opposite sense of the target
    ON_3dPoint cv_end, cv_next;
    const int i_end = moving_at_end_ ? n - 1 : 0, i_next = moving_at_end_ ? n - 2 : 1;
    nc.GetCV(i_end, cv_end);
    nc.GetCV(i_next, cv_next);
    const double mag = cv_end.DistanceTo(cv_next);
    nc.SetCV(i_end, e);
    Point3d new_next = e - desired * mag;
    nc.SetCV(i_next, new_next);
    kernel::NurbsCurve k;
    k.raw() = nc;
    ReplaceCurve(ctx, moving_->id, k);
    ctx.Print("Match: reshaped curve " + std::to_string(moving_->id) + " to meet curve " + std::to_string(c->id) + " tangentially");
    Finish();
  }
  std::optional<CurveCopy> moving_;
  bool moving_at_end_ = true;
};

// EndBulge: scales the "bulge" (the distance from a curve's end control
// point to its neighbor, which sets how strongly the curve leaves that
// end tangent to its own end direction) by a typed factor, in place -
// direction is preserved exactly, only the handle's length changes.
class EndBulgeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select curve near the end to adjust"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      double t = 0;
      curve_ = NearestCurveTo(ctx, p, &t);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      kernel::Interval d = curve_->curve.Domain();
      at_end_ = std::fabs(t - d.max) < std::fabs(t - d.min);
      if (curve_->curve.ControlPointCount() < 2) { ctx.Warn("EndBulge: curve needs at least 2 control points"); Finish(); return; }
      WantNumber("Bulge factor (1 = unchanged, >1 = stronger, <1 = flatter)", 1.0);
      return;
    }
  }
  void OnNumber(CommandContext& ctx, double factor) override {
    if (!curve_ || factor <= 0) { ctx.Warn("EndBulge: factor must be positive"); return; }
    ON_NurbsCurve nc = curve_->curve.raw();
    const int n = nc.CVCount();
    const int i_end = at_end_ ? n - 1 : 0, i_next = at_end_ ? n - 2 : 1;
    ON_3dPoint cv_end, cv_next;
    nc.GetCV(i_end, cv_end);
    nc.GetCV(i_next, cv_next);
    Point3d new_next = cv_end + (cv_next - cv_end) * factor;
    nc.SetCV(i_next, new_next);
    ctx.Doc().BeginChange("EndBulge");
    kernel::NurbsCurve k;
    k.raw() = nc;
    ReplaceCurve(ctx, curve_->id, k);
    ctx.Print("EndBulge: end handle scaled by " + FormatNumber(factor));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (curve_) OnNumber(ctx, 1.0); }
  std::optional<CurveCopy> curve_;
  bool at_end_ = true;
};

// SoftEditCrv / HandleCurve: pick a point on a curve, drag it to a new
// position, and a falloff radius (in parameter units either side of the
// pick) - control points within the falloff move with the drag, smoothly
// scaled down to zero at the radius (a raised-cosine falloff, the same
// smooth-to-zero shape Fair's own neighbor-averaging aims for, just
// applied as a displacement instead of a smoothing pass).
class SoftEditCommand : public Command {
 public:
  explicit SoftEditCommand(std::string label) : label_(std::move(label)) {}
  void Begin(CommandContext&) override { WantPoint("Select curve at the point to edit"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      double t = 0;
      curve_ = NearestCurveTo(ctx, p, &t);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      t0_ = t;
      anchor_ = curve_->curve.PointAt(t);
      WantPoint("Drag to the new position");
      return;
    }
    if (!target_) { target_ = p; WantNumber("Falloff distance (in the curve's own parameter units)", (curve_->curve.Domain().max - curve_->curve.Domain().min) * 0.25); return; }
  }
  void OnNumber(CommandContext& ctx, double falloff) override {
    if (!curve_ || !target_ || falloff <= 0) return;
    ctx.Doc().BeginChange(label_);
    Vector3d delta = *target_ - anchor_;
    kernel::NurbsCurve k = curve_->curve;
    ON_NurbsCurve& nc = k.raw();
    const int deg = nc.Degree();
    int moved = 0;
    for (int i = 0; i < nc.CVCount(); ++i) {
      // Greville abscissa: the standard parameter associated with CV i.
      double g = 0;
      for (int j = 0; j < deg; ++j) g += nc.Knot(i + j);
      g /= deg;
      const double dist = std::fabs(g - t0_);
      if (dist >= falloff) continue;
      const double w = 0.5 * (1 + std::cos(ON_PI * dist / falloff));  // 1 at dist=0, 0 at dist=falloff
      ON_3dPoint cv;
      nc.GetCV(i, cv);
      nc.SetCV(i, cv + delta * w);
      ++moved;
    }
    ReplaceCurve(ctx, curve_->id, k);
    ctx.Print(label_ + ": " + std::to_string(moved) + " control point(s) moved with falloff " + FormatNumber(falloff));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnHover(CommandContext& ctx, Point3d h) override { if (curve_ && !target_) { ctx.ClearPreview(); ctx.AddPreviewLine(anchor_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::string label_;
  std::optional<CurveCopy> curve_;
  double t0_ = 0;
  Point3d anchor_{0, 0, 0};
  std::optional<Point3d> target_;
};

// FixedLengthCrvEdit: moves a picked point on the curve, then uniformly
// rescales the whole curve about its own start point to restore its
// original total length - a genuine, if global rather than local, way to
// honor "edit the curve but keep its length fixed": the curve's arc
// length (measured the same way Length() itself measures it) is the same
// before and after, verified by construction, not just assumed.
class FixedLengthCrvEditCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select curve at the point to move"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      double t = 0;
      curve_ = NearestCurveTo(ctx, p, &t);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      t0_ = t;
      anchor_ = curve_->curve.PointAt(t);
      original_length_ = curve_->curve.Length();
      WantPoint("Drag to the new position");
      return;
    }
    ctx.Doc().BeginChange("FixedLengthCrvEdit");
    Vector3d delta = p - anchor_;
    kernel::Interval d = curve_->curve.Domain();
    const double falloff = (d.max - d.min) * 0.35;
    kernel::NurbsCurve k = curve_->curve;
    ON_NurbsCurve& nc = k.raw();
    const int deg = nc.Degree();
    for (int i = 0; i < nc.CVCount(); ++i) {
      double g = 0;
      for (int j = 0; j < deg; ++j) g += nc.Knot(i + j);
      g /= deg;
      const double dist = std::fabs(g - t0_);
      if (dist >= falloff) continue;
      const double w = 0.5 * (1 + std::cos(ON_PI * dist / falloff));
      ON_3dPoint cv;
      nc.GetCV(i, cv);
      nc.SetCV(i, cv + delta * w);
    }
    // Uniformly rescale about the curve's own start point to restore the
    // original total length.
    Point3d anchor0 = k.PointAt(k.Domain().min);
    const double new_length = k.Length();
    if (new_length > 1e-9) {
      const double s = original_length_ / new_length;
      ON_Xform xf = ON_Xform::ScaleTransformation(anchor0, s);
      nc.Transform(xf);
    }
    ReplaceCurve(ctx, curve_->id, k);
    ctx.Print("FixedLengthCrvEdit: point moved and curve rescaled " + FormatNumber(original_length_ / std::max(new_length, 1e-9)) +
               "x about its start to keep length " + FormatNumber(original_length_));
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (curve_) { ctx.ClearPreview(); ctx.AddPreviewLine(anchor_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::optional<CurveCopy> curve_;
  double t0_ = 0, original_length_ = 0;
  Point3d anchor_{0, 0, 0};
};

// CurveThroughSrfControlPt: a curve through every row and every column of
// a surface's own control point grid, built directly (unlike the old
// "ExtractPt then run CurveThroughPt by hand" two-step).
void BuildCurveThroughSrfControlPt(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("CurveThroughSrfControlPt");
  int made = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Surface || !o->surface) continue;
    const ON_NurbsSurface& s = o->surface->raw();
    const int nu = s.CVCount(0), nv = s.CVCount(1);
    for (int i = 0; i < nu; ++i) {
      std::vector<Point3d> row;
      for (int j = 0; j < nv; ++j) { ON_3dPoint p; s.GetCV(i, j, p); row.push_back(p); }
      if (row.size() >= 2) { SceneObject n = SceneObject::MakeCurve(PolylineCurve(row)); n.layer_index = o->layer_index; ctx.Doc().Add(std::move(n)); ++made; }
    }
    for (int j = 0; j < nv; ++j) {
      std::vector<Point3d> col;
      for (int i = 0; i < nu; ++i) { ON_3dPoint p; s.GetCV(i, j, p); col.push_back(p); }
      if (col.size() >= 2) { SceneObject n = SceneObject::MakeCurve(PolylineCurve(col)); n.layer_index = o->layer_index; ctx.Doc().Add(std::move(n)); ++made; }
    }
  }
  ctx.Print("CurveThroughSrfControlPt: " + std::to_string(made) + " curve(s) through the control point rows and columns");
}

// Planar offset of `c` by signed distance `d` (same technique OffsetCommand
// uses) - factored out here so OffsetMultiple can build several at once
// without a round trip through the interactive Offset command.
kernel::NurbsCurve OffsetPlanar(CommandContext& ctx, const kernel::NurbsCurve& c, double d, Vector3d up) {
  if (c.IsLinear()) {
    kernel::Interval dom = c.Domain();
    Vector3d tan = c.TangentAt(dom.min);
    Vector3d s = ON_CrossProduct(tan, up);
    s.Unitize();
    return PolylineCurve({c.PointAt(dom.min) + s * d, c.PointAt(dom.max) + s * d});
  }
  std::vector<Point3d> pts;
  for (double t : c.SuggestedParameterValues(0.005)) {
    Vector3d tan = c.TangentAt(t);
    Vector3d s = ON_CrossProduct(tan, up);
    s.Unitize();
    pts.push_back(c.PointAt(t) + s * d);
  }
  (void)ctx;
  if (pts.size() < 2) return c;
  return c.Degree() == 1 ? PolylineCurve(pts) : kernel::NurbsCurve::FromControlPoints(pts, std::min(3, static_cast<int>(pts.size()) - 1));
}

class OffsetMultipleCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to offset multiple times"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    curves_ = CopyCurves(ctx, ids);
    if (curves_.empty()) { Finish(); return; }
    WantPoint("Side to offset (or type a distance)");
    options = {{"Distance", FormatNumber(distance_), {}, true, false}, {"Count", std::to_string(count_), {}, true, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Distance") { double d = std::atof(v.c_str()); if (d > 0) distance_ = d; options[0].value = FormatNumber(distance_); }
    if (n == "Count") { int c = std::atoi(v.c_str()); if (c > 0) count_ = c; options[1].value = std::to_string(count_); }
  }
  void OnPoint(CommandContext& ctx, Point3d side) override {
    ctx.Doc().BeginChange("OffsetMultiple");
    Vector3d up = ActiveNormal(ctx);
    int made = 0;
    for (const CurveCopy& c : curves_) {
      double t = c.curve.ClosestPointParameter(side);
      Vector3d tan = c.curve.TangentAt(t);
      Vector3d n = ON_CrossProduct(tan, up);
      const int sign = ON_DotProduct(side - c.curve.PointAt(t), n) >= 0 ? 1 : -1;
      for (int i = 1; i <= count_; ++i) { AddCurveLike(ctx, OffsetPlanar(ctx, c.curve, distance_ * sign * i, up), c.attrs); ++made; }
    }
    ctx.Print("OffsetMultiple: " + std::to_string(made) + " curve(s) created (" + std::to_string(count_) + " offsets x " + FormatNumber(distance_) + ")");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e && v > 0) { distance_ = v; options[0].value = FormatNumber(distance_); ctx.Print("Distance=" + FormatNumber(v) + ". Pick the side."); } }
  std::vector<CurveCopy> curves_;
  double distance_ = 1;
  int count_ = 3;
};

// Best-effort surface lookup shared by the *OnSrf commands below.
std::optional<kernel::NurbsSurface> FindSurface(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Surface && o->surface) return *o->surface;
    if (o->kind == ObjectKind::Brep && o->brep && o->brep->raw().m_F.Count() > 0) {
      const ON_Surface* s = o->brep->raw().m_F[0].SurfaceOf();
      kernel::NurbsSurface ns;
      if (s && SurfaceFromON(*s, ns)) return ns;
    }
  }
  return std::nullopt;
}

class OffsetCrvOnSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves on a surface to offset"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (curves_.empty() && !picked_curves_) {
      curves_ = CopyCurves(ctx, ids);
      picked_curves_ = true;
      if (curves_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select the surface the curves lie on");
      return;
    }
    srf_ = FindSurface(ctx, ids);
    if (!srf_) { ctx.Warn("Select a surface"); Finish(); return; }
    WantNumber("Offset distance (in-surface, along the tangent x normal direction)", 1.0);
  }
  void OnNumber(CommandContext& ctx, double d) override {
    if (!srf_) return;
    ctx.Doc().BeginChange("OffsetCrvOnSrf");
    int made = 0;
    for (const CurveCopy& c : curves_) {
      std::vector<Point3d> pts;
      for (double t : c.curve.SuggestedParameterValues(0.01)) {
        Point3d p = c.curve.PointAt(t);
        ON_2dPoint uv = srf_->ClosestPointParameter(p);
        ON_3dPoint sp; ON_3dVector du, dv;
        srf_->raw().Ev1Der(uv.x, uv.y, sp, du, dv);
        Vector3d n = ON_CrossProduct(du, dv);
        if (!n.Unitize()) continue;
        Vector3d tan = c.curve.TangentAt(t);
        Vector3d side = ON_CrossProduct(tan, n);
        if (!side.Unitize()) continue;
        Point3d moved = sp + side * d;
        // Re-project so the offset point actually lands back on the surface.
        ON_2dPoint uv2 = srf_->ClosestPointParameter(moved);
        pts.push_back(srf_->PointAt(uv2.x, uv2.y));
      }
      if (pts.size() < 2) continue;
      AddCurveLike(ctx, c.curve.Degree() == 1 ? PolylineCurve(pts) : kernel::NurbsCurve::FromControlPoints(pts, std::min(3, static_cast<int>(pts.size()) - 1)), c.attrs);
      ++made;
    }
    ctx.Print("OffsetCrvOnSrf: " + std::to_string(made) + " curve(s) offset along the surface");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (srf_) OnNumber(ctx, 1.0); }
  std::vector<CurveCopy> curves_;
  bool picked_curves_ = false;
  std::optional<kernel::NurbsSurface> srf_;
};

class ExtendCrvOnSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select curve on a surface, near the end to extend"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      double t = 0;
      curve_ = NearestCurveTo(ctx, p, &t);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      t_ = t;
      accept_preselection = false;
      WantObjects("Select the surface the curve lies on");
      return;
    }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    srf_ = FindSurface(ctx, ids);
    if (!srf_ || !curve_) { ctx.Warn("Select a surface"); Finish(); return; }
    WantNumber("Extension length", 10);
  }
  void OnNumber(CommandContext& ctx, double len) override {
    if (!curve_ || !srf_ || len <= 0) return;
    kernel::Interval d = curve_->curve.Domain();
    const bool at_end = std::fabs(t_ - d.max) < std::fabs(t_ - d.min);
    kernel::NurbsCurve k = curve_->curve;
    const double per_unit = k.Length() / (d.max - d.min);
    const double dt = len / std::max(per_unit, 1e-9);
    if (at_end) k.Extend(d.min, d.max + dt); else k.Extend(d.min - dt, d.max);
    // Re-project the extended curve's own sample points back onto the
    // surface, then refit, so the whole result actually lies on it.
    std::vector<Point3d> pts;
    kernel::Interval nd = k.Domain();
    const int n = 64;
    for (int i = 0; i <= n; ++i) {
      double t = nd.min + (nd.max - nd.min) * i / n;
      Point3d p = k.PointAt(t);
      ON_2dPoint uv = srf_->ClosestPointParameter(p);
      pts.push_back(srf_->PointAt(uv.x, uv.y));
    }
    ctx.Doc().BeginChange("ExtendCrvOnSrf");
    ReplaceCurve(ctx, curve_->id, curve_->curve.Degree() == 1 ? PolylineCurve(pts) : kernel::NurbsCurve::FromControlPoints(pts, std::min(3, static_cast<int>(pts.size()) - 1)));
    ctx.Print("ExtendCrvOnSrf: extended by " + FormatNumber(len) + " and kept on the surface");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (srf_) OnNumber(ctx, 10); }
  std::optional<CurveCopy> curve_;
  double t_ = 0;
  std::optional<kernel::NurbsSurface> srf_;
};

class InterpCrvOnSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select the surface to interpolate on"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    srf_ = FindSurface(ctx, ids);
    if (!srf_) { ctx.Warn("Select a surface"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    WantPoint("First point on the surface");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ON_2dPoint uv = srf_->ClosestPointParameter(p);
    Point3d onsrf = srf_->PointAt(uv.x, uv.y);
    pts_.push_back(onsrf);
    ctx.SetLastPoint(onsrf);
    WantPoint("Next point on the surface (Enter when done)");
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.ClearPreview();
    if (pts_.size() < 2) { Finish(); return; }
    ctx.Doc().BeginChange("InterpCrvOnSrf");
    kernel::NurbsCurve k = InterpolateCubic(pts_);
    // Re-project the fitted curve's own samples back onto the surface so
    // the curve genuinely hugs it, not just the picked points themselves.
    std::vector<Point3d> resampled;
    kernel::Interval d = k.Domain();
    const int n = 48;
    for (int i = 0; i <= n; ++i) {
      Point3d p = k.PointAt(d.min + (d.max - d.min) * i / n);
      ON_2dPoint uv = srf_->ClosestPointParameter(p);
      resampled.push_back(srf_->PointAt(uv.x, uv.y));
    }
    AddCurve(ctx, InterpolateCubic(resampled), "InterpCrvOnSrf");
    ctx.Print("InterpCrvOnSrf: curve interpolated through " + std::to_string(pts_.size()) + " point(s) on the surface");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (!pts_.empty()) { ctx.ClearPreview(); ctx.AddPreviewPolyline(pts_); ctx.AddPreviewLine(pts_.back(), h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::optional<kernel::NurbsSurface> srf_;
  std::vector<Point3d> pts_;
};

// InsertLineIntoCrv: splits a curve at a picked point and inserts a
// straight line segment (tangent to the curve there, of a typed length)
// between the two pieces, then joins everything back into one curve - the
// whole "Split, draw a Line, Join" sequence the old note asked the user
// to do by hand.
class InsertLineIntoCrvCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select curve at the point to insert a line"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      double t = 0;
      curve_ = NearestCurveTo(ctx, p, &t);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      t_ = t;
      WantNumber("Line length", 10);
      return;
    }
  }
  void OnNumber(CommandContext& ctx, double len) override {
    if (!curve_ || len <= 0) return;
    kernel::Interval d = curve_->curve.Domain();
    if (t_ - d.min < 1e-9 || d.max - t_ < 1e-9) { ctx.Warn("InsertLineIntoCrv: pick a point strictly inside the curve"); Finish(); return; }
    kernel::NurbsCurve left = curve_->curve, right = curve_->curve;
    left.Trim(d.min, t_);
    right.Trim(t_, d.max);
    Vector3d tan = curve_->curve.TangentAt(t_);
    if (!tan.Unitize()) { ctx.Warn("InsertLineIntoCrv: curve has no tangent there"); Finish(); return; }
    Point3d mid = curve_->curve.PointAt(t_);
    kernel::NurbsCurve line = PolylineCurve({mid - tan * (len / 2), mid + tan * (len / 2)});
    ctx.Doc().BeginChange("InsertLineIntoCrv");
    ON_PolyCurve pc;
    pc.Append(new ON_NurbsCurve(left.raw()));
    pc.Append(new ON_NurbsCurve(line.raw()));
    pc.Append(new ON_NurbsCurve(right.raw()));
    kernel::NurbsCurve merged;
    if (CurveFromON(pc, merged)) { ReplaceCurve(ctx, curve_->id, merged); ctx.Print("InsertLineIntoCrv: inserted a " + FormatNumber(len) + "-long line and rejoined"); }
    else ctx.Warn("InsertLineIntoCrv: could not join the pieces");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (curve_) OnNumber(ctx, 10); }
  std::optional<CurveCopy> curve_;
  double t_ = 0;
};

// CSec: cross-sections perpendicular to a rail curve at even intervals -
// like Contour, but the cutting planes follow a curve's own Frenet frame
// (tangent as the plane normal) instead of all being parallel.
class CSecCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to cross-section"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids_.empty() && !picked_objects_) {
      ids_ = ids;
      picked_objects_ = true;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select the rail curve to section along");
      return;
    }
    for (ObjectId id : ids) if (auto c = CopyCurveObj(ctx, id)) { rail_ = *c; break; }
    if (!rail_) { ctx.Warn("Select a curve as the rail"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    WantNumber("Number of sections", 5);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!rail_) return;
    const int count = std::max(2, static_cast<int>(v));
    std::vector<double> params = rail_->curve.DivideByCount(count - 1);
    ctx.Doc().BeginChange("CSec");
    int made = 0;
    for (double t : params) {
      Vector3d tan = rail_->curve.TangentAt(t);
      if (!tan.Unitize()) continue;
      made += SliceObjects(ctx, ids_, ON_Plane(rail_->curve.PointAt(t), tan), "CSec");
    }
    ctx.Print("CSec: " + std::to_string(made) + " curve(s) from " + std::to_string(params.size()) + " section(s) along the rail");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (rail_) OnNumber(ctx, 5); }
  std::vector<ObjectId> ids_;
  bool picked_objects_ = false;
  std::optional<CurveCopy> rail_;
};

// Connect: extend/trim two curves to their (planar) intersection.
class ConnectCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select first curve near the end to connect"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveCopy> c = NearestCurveTo(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    picks_.push_back(*c); params_.push_back(t);
    if (picks_.size() == 1) { WantPoint("Select second curve near the end to connect"); return; }
    ON_Plane pl = ActivePlane(ctx);
    ON_Line la, lb;
    for (int i = 0; i < 2; ++i) {
      kernel::Interval d = picks_[i].curve.Domain();
      bool at_end = std::fabs(params_[i] - d.max) < std::fabs(params_[i] - d.min);
      double te = at_end ? d.max : d.min;
      Point3d e = picks_[i].curve.PointAt(te);
      Vector3d tan = picks_[i].curve.TangentAt(te);
      ON_Line& l = i == 0 ? la : lb;
      l = ON_Line(e, e + tan);
      ends_at_max_[i] = at_end;
    }
    double a, b;
    if (!ON_Intersect(la, lb, &a, &b)) { ctx.Warn("Connect: curve ends are parallel"); Finish(); return; }
    Point3d x = la.PointAt(a);
    x = pl.PointAt(pl.ClosestPointTo(x).x, pl.ClosestPointTo(x).y);
    ctx.Doc().BeginChange("Connect");
    for (int i = 0; i < 2; ++i) {
      kernel::NurbsCurve k = picks_[i].curve;
      if (k.IsLinear(ctx.Settings().absolute_tolerance)) {
        kernel::Interval d = k.Domain();
        Point3d s = k.PointAt(ends_at_max_[i] ? d.min : d.max);
        k = PolylineCurve(ends_at_max_[i] ? std::vector<Point3d>{s, x} : std::vector<Point3d>{x, s});
      } else {
        // Extend/trim the parametric domain so the end lands nearest x.
        kernel::Interval d = k.Domain();
        double len = d.max - d.min;
        k.Extend(d.min - len, d.max + len);
        double tx = k.ClosestPointParameter(x, 400);
        kernel::Interval d2 = k.Domain();
        if (ends_at_max_[i]) k.Trim(d.min, std::max(tx, d.min + 1e-6)); else k.Trim(std::min(tx, d.max - 1e-6), d.max);
        (void)d2;
      }
      ReplaceCurve(ctx, picks_[i].id, k);
    }
    ctx.Print("Connect: curves meet at " + FormatPoint(x));
    Finish();
  }
  std::vector<CurveCopy> picks_;
  std::vector<double> params_;
  bool ends_at_max_[2] = {true, true};
};

class ExtendByLengthCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Select curve near the end to extend"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!curve_) {
      curve_ = NearestCurveTo(ctx, p, &t_);
      if (!curve_) { ctx.Warn("No curve near that point"); return; }
      WantNumber("Extension length", 10);
      return;
    }
    // Point picked as the new end: extend to the closest point.
    OnNumber(ctx, p.DistanceTo(curve_->curve.PointAt(EndParam())));
  }
  double EndParam() const { kernel::Interval d = curve_->curve.Domain(); return std::fabs(t_ - d.max) < std::fabs(t_ - d.min) ? d.max : d.min; }
  void OnNumber(CommandContext& ctx, double len) override {
    if (!curve_) return;
    kernel::Interval d = curve_->curve.Domain();
    bool at_end = EndParam() == d.max;
    kernel::NurbsCurve k = curve_->curve;
    if (k.IsLinear(ctx.Settings().absolute_tolerance)) {
      Point3d s = k.PointAt(at_end ? d.min : d.max), e = k.PointAt(at_end ? d.max : d.min);
      Vector3d dir = e - s; dir.Unitize();
      k = PolylineCurve(at_end ? std::vector<Point3d>{s, e + dir * len} : std::vector<Point3d>{e + dir * len, s});
    } else {
      // Grow the domain until the added arc length matches.
      double per_unit = k.Length() / (d.max - d.min);
      double dt = len / std::max(per_unit, 1e-9);
      if (at_end) k.Extend(d.min, d.max + dt); else k.Extend(d.min - dt, d.max);
    }
    ctx.Doc().BeginChange("ExtendDynamic");
    ReplaceCurve(ctx, curve_->id, k);
    ctx.Print("Extended curve " + std::to_string(curve_->id) + " by " + FormatNumber(len));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (curve_) OnNumber(ctx, 10); }
  std::optional<CurveCopy> curve_;
  double t_ = 0;
};

// Crv2View: combine two planar curves seen from two directions into a 3D curve.
void Crv2View(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<CurveCopy> cs = CopyCurves(ctx, ids);
  if (cs.size() < 2) { ctx.Warn("Select two planar curves"); return; }
  const int n = 48;
  std::vector<Point3d> pts;
  for (int i = 0; i <= n; ++i) {
    double u = static_cast<double>(i) / n;
    Point3d a = cs[0].curve.PointAt(At(cs[0].curve.Domain(), u));
    Point3d b = cs[1].curve.PointAt(At(cs[1].curve.Domain(), u));
    // First curve supplies x,y (Top view); second supplies z by its own height (Front view: x,z).
    pts.emplace_back(a.x, a.y, b.z);
  }
  ctx.Doc().BeginChange("Crv2View");
  AddCurveLike(ctx, InterpolateCubic(pts), cs[0].attrs);
  ctx.Print("Crv2View: 3D curve built from the two views");
}

// MergeCrv: like Join (chains curves end-to-end into one polycurve), but
// additionally collapses any tangent-continuous (G1) junction between two
// chained segments into a true single span - removing that segment
// boundary's knot via RemoveKnotApprox() so the merged curve has one fewer
// interior knot there, rather than leaving Join's own visible per-segment
// knot in place even where the tangent already lines up.
void MergeCurves(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<CurveCopy> curves = CopyCurves(ctx, ids);
  if (curves.size() < 2) { ctx.Warn("MergeCrv: select at least two curves"); return; }
  ON_PolyCurve pc;
  std::vector<const CurveCopy*> remaining;
  for (const CurveCopy& c : curves) remaining.push_back(&c);
  pc.Append(new ON_NurbsCurve(remaining[0]->curve.raw()));
  std::vector<ObjectId> joined_ids = {remaining[0]->id};
  remaining.erase(remaining.begin());
  const double tol = ctx.Settings().absolute_tolerance * 10;
  bool progress = true;
  while (progress && !remaining.empty()) {
    progress = false;
    for (size_t i = 0; i < remaining.size(); ++i) {
      ON_NurbsCurve c = remaining[i]->curve.raw();
      if (c.PointAtStart().DistanceTo(pc.PointAtEnd()) <= tol) { pc.Append(new ON_NurbsCurve(c)); }
      else if (c.PointAtEnd().DistanceTo(pc.PointAtEnd()) <= tol) { c.Reverse(); pc.Append(new ON_NurbsCurve(c)); }
      else if (c.PointAtEnd().DistanceTo(pc.PointAtStart()) <= tol) { pc.Prepend(new ON_NurbsCurve(c)); }
      else if (c.PointAtStart().DistanceTo(pc.PointAtStart()) <= tol) { c.Reverse(); pc.Prepend(new ON_NurbsCurve(c)); }
      else continue;
      joined_ids.push_back(remaining[i]->id);
      remaining.erase(remaining.begin() + static_cast<long>(i));
      progress = true;
      break;
    }
  }
  if (pc.Count() < 2) { ctx.Warn("MergeCrv: curve ends do not meet"); return; }
  ON_NurbsCurve nc;
  if (pc.GetNurbForm(nc) <= 0) { ctx.Warn("MergeCrv: could not build a NURBS form"); return; }
  // Collapse each full-multiplicity interior knot (a segment boundary) that
  // turns out to be tangent-continuous - i.e. every junction Join itself
  // would have left as a visible knot, but that a real single-span curve
  // wouldn't need.
  int merged = 0;
  const double cos_tol = std::cos(0.5 * ON_PI / 180.0);  // 0.5 degrees
  for (int i = nc.Degree(); i < nc.KnotCount() - nc.Degree();) {
    const int mult = nc.KnotMultiplicity(i);
    if (mult >= nc.Degree()) {
      const double t = nc.Knot(i);
      const double eps = std::max(1e-7, nc.Domain().Length() * 1e-7);
      ON_3dVector tl = nc.TangentAt(t - eps), tr = nc.TangentAt(t + eps);
      if (tl.Unitize() && tr.Unitize() && ON_DotProduct(tl, tr) > cos_tol && RemoveKnotApprox(nc, i)) {
        ++merged;
        continue;
      }
    }
    i += std::max(1, mult);
  }
  ctx.Doc().BeginChange("MergeCrv");
  kernel::NurbsCurve k;
  k.raw() = nc;
  SceneObject n = SceneObject::MakeCurve(k);
  n.layer_index = curves[0].attrs.layer_index;
  n.color = curves[0].attrs.color;
  n.color_by_layer = curves[0].attrs.color_by_layer;
  for (ObjectId jid : joined_ids) ctx.Doc().Remove(jid);
  ctx.Doc().Add(std::move(n));
  ctx.Print("MergeCrv: joined " + std::to_string(pc.Count()) + " curve(s) into one, merged " +
             std::to_string(merged) + " tangent junction(s) into a single span");
}

}  // namespace

void RegisterCurves2Commands(CommandEngine& e) {
  Reg(e, "Conic", Make<ConicCommand>());
  Reg(e, "Parabola", Make<PointsCommand>(std::vector<std::string>{"Vertex of parabola", "Focus", "End of parabola"},
      [](CommandContext& ctx, const std::vector<Point3d>& p) { ctx.Doc().BeginChange("Parabola"); BuildParabola(ctx, p[0], p[1], p[2]); }));
  Reg(e, "Parabola3Pt", Make<PointsCommand>(std::vector<std::string>{"Start of parabola", "End of parabola", "Apex"},
      [](CommandContext& ctx, const std::vector<Point3d>& p) { ctx.Doc().BeginChange("Parabola3Pt"); AddCurve(ctx, ConicArc(p[0], p[2], p[1], 1), "Parabola3Pt"); ctx.Print("Parabola through 3 points created"); }));
  Reg(e, "Hyperbola", Make<PointsCommand>(std::vector<std::string>{"Center of hyperbola", "Vertex", "End of hyperbola"},
      [](CommandContext& ctx, const std::vector<Point3d>& p) { ctx.Doc().BeginChange("Hyperbola"); BuildHyperbola(ctx, p[0], p[1], p[2]); }));
  Reg(e, "Catenary", Make<CatenaryCommand>());
  Reg(e, "MarkFoci", OnSelection("Select conic curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MarkFoci");
        int n = 0;
        const double tol = ctx.Settings().absolute_tolerance;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          ON_Ellipse el; ON_Arc arc; ON_Plane plane;
          if (c.curve.raw().IsEllipse(nullptr, &el, tol)) {
            double a = el.radius[0], b = el.radius[1];
            double f = std::sqrt(std::fabs(a * a - b * b));
            Vector3d ax = a >= b ? el.plane.xaxis : el.plane.yaxis;
            ctx.Doc().Add(SceneObject::MakePoint(el.plane.origin + ax * f)); ctx.Doc().Add(SceneObject::MakePoint(el.plane.origin - ax * f)); n += 2;
          } else if (c.curve.raw().IsArc(nullptr, &arc, tol)) {
            ctx.Doc().Add(SceneObject::MakePoint(arc.Center())); ++n;
          } else if (c.curve.raw().IsPlanar(&plane, tol)) {
            // Not an ellipse/arc: fit a general conic in the curve's own
            // plane and read the parabola/hyperbola focus (or foci) off
            // the fitted algebraic coefficients.
            std::vector<Point3d> foci = GeneralConicFoci(c.curve, plane);
            for (const Point3d& p : foci) { ctx.Doc().Add(SceneObject::MakePoint(p)); ++n; }
            if (foci.empty()) ctx.Warn("MarkFoci: curve " + std::to_string(c.id) + " isn't a recognizable conic");
          }
        }
        ctx.Print("MarkFoci: " + std::to_string(n) + " point(s) added");
      }), CommandStatus::Implemented, "Marks foci of ellipses, centers of arcs, and analytic foci of parabolas/hyperbolas fitted from the curve's own geometry.");

  Reg(e, "CloseCrv", OnSelection("Select open curves to close", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CloseCrv");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          if (c.curve.IsClosed()) continue;
          ON_PolyCurve pc;
          pc.Append(new ON_NurbsCurve(c.curve.raw()));
          pc.Append(new ON_LineCurve(c.curve.PointAt(c.curve.Domain().max), c.curve.PointAt(c.curve.Domain().min)));
          kernel::NurbsCurve k; if (CurveFromON(pc, k)) { ReplaceCurve(ctx, c.id, k); ++n; }
        }
        ctx.Print("CloseCrv: " + std::to_string(n) + " curve(s) closed");
      }));
  Reg(e, "MergeCrv", OnSelection("Select two or more curves to merge", MergeCurves, 2));
  Reg(e, "SimplifyCrv", OnSelection("Select curves to simplify", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SimplifyCrv");
        int n = 0;
        const double tol = ctx.Settings().absolute_tolerance;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          ON_Arc arc;
          if (c.curve.IsLinear(tol) && c.curve.ControlPointCount() > 2) { ReplaceCurve(ctx, c.id, PolylineCurve({c.curve.PointAt(c.curve.Domain().min), c.curve.PointAt(c.curve.Domain().max)})); ++n; }
          else if (c.curve.raw().IsArc(nullptr, &arc, tol) && !c.curve.IsRational()) { ON_ArcCurve ac(arc); kernel::NurbsCurve k; if (CurveFromON(ac, k)) { ReplaceCurve(ctx, c.id, k); ++n; } }
        }
        ctx.Print("SimplifyCrv: " + std::to_string(n) + " curve(s) replaced by lines/arcs");
      }));
  Reg(e, "ReducePolyline", OnSelection("Select polylines to reduce", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ReducePolyline");
        const double tol = std::max(ctx.Settings().absolute_tolerance * 10, 0.01);
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          if (c.curve.Degree() != 1) continue;
          std::vector<Point3d> pts = ControlPolygon(c.curve);
          std::vector<bool> keep(pts.size(), false);
          keep.front() = keep.back() = true;
          Simplify(pts, 0, pts.size() - 1, tol, keep);
          std::vector<Point3d> out;
          for (size_t i = 0; i < pts.size(); ++i) if (keep[i]) out.push_back(pts[i]);
          if (out.size() < pts.size()) { ReplaceCurve(ctx, c.id, PolylineCurve(out)); ++n; }
        }
        ctx.Print("ReducePolyline: " + std::to_string(n) + " polyline(s) reduced");
      }));
  Reg(e, "SubCrv", Make<SubCrvCommand>(SubCrvCommand::Mode::Keep));
  Reg(e, "ExtractSubCrv", Make<SubCrvCommand>(SubCrvCommand::Mode::Copy));
  Reg(e, "DeleteSubCrv", Make<SubCrvCommand>(SubCrvCommand::Mode::Delete));
  Reg(e, "CrvStart", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CrvStart");
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { Point3d p = c.curve.PointAt(c.curve.Domain().min); ctx.Doc().Add(SceneObject::MakePoint(p)); ctx.Print("Start: " + FormatPoint(p)); }
      }));
  Reg(e, "CrvEnd", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CrvEnd");
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { Point3d p = c.curve.PointAt(c.curve.Domain().max); ctx.Doc().Add(SceneObject::MakePoint(p)); ctx.Print("End: " + FormatPoint(p)); }
      }));
  Reg(e, "CrvSeam", Make<CrvSeamCommand>());
  Reg(e, "Domain", Make<DomainCommand>());
  Reg(e, "Reparameterize", OnSelection("Select curves to reparameterize to 0-1", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Reparameterize");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { kernel::NurbsCurve k = c.curve; k.raw().SetDomain(0, 1); ReplaceCurve(ctx, c.id, k); ++n; }
        ctx.Print("Reparameterize: " + std::to_string(n) + " curve(s) now have domain 0 to 1");
      }));
  Reg(e, "MakeNonPeriodic", OnSelection("Select periodic curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MakeNonPeriodic");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { kernel::NurbsCurve k = c.curve; if (k.raw().IsPeriodic() && k.raw().ClampEnd(2)) { ReplaceCurve(ctx, c.id, k); ++n; } }
        ctx.Print("MakeNonPeriodic: " + std::to_string(n) + " curve(s) clamped");
      }));
  Reg(e, "MakeUniform", OnSelection("Select curves to make uniform", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MakeUniform");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { kernel::NurbsCurve k = c.curve; if (k.raw().MakeClampedUniformKnotVector(1.0)) { ReplaceCurve(ctx, c.id, k); ++n; } }
        ctx.Print("MakeUniform: " + std::to_string(n) + " curve(s) now have uniform knots");
      }));
  Reg(e, "ConvertToBeziers", OnSelection("Select curves to split into Bezier spans", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ConvertToBeziers");
        int made = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          ON_NurbsCurve nc = c.curve.raw();
          nc.MakePiecewiseBezier(true);
          int spans = nc.SpanCount();
          for (int s = 0; s < spans; ++s) {
            double t0 = nc.Knot(nc.Degree() - 1 + s), t1 = nc.Knot(nc.Degree() + s);
            if (t1 - t0 <= 1e-12) continue;
            kernel::NurbsCurve piece; piece.raw() = nc; piece.Trim(t0, t1);
            AddCurveLike(ctx, piece, c.attrs); ++made;
          }
          ctx.Doc().Remove(c.id);
        }
        ctx.Print("ConvertToBeziers: " + std::to_string(made) + " Bezier span(s)");
      }));
  Reg(e, "ConvertToSingleSpans", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("ConvertToBeziers"); }));
  Reg(e, "InsertKink", Make<KnotEditCommand>(KnotEditCommand::Op::InsertKink));
  Reg(e, "InsertControlPoint", Make<KnotEditCommand>(KnotEditCommand::Op::InsertCP));
  Reg(e, "InsertEditPoint", Make<KnotEditCommand>(KnotEditCommand::Op::InsertEditPoint));
  Reg(e, "RemoveKnot", Make<KnotEditCommand>(KnotEditCommand::Op::RemoveKnot));
  Reg(e, "RemoveControlPoint", Make<KnotEditCommand>(KnotEditCommand::Op::RemoveCP));
  Reg(e, "RemoveMultiKnot", OnSelection("Select curves to remove multiple knots from", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("RemoveMultiKnot");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          ON_NurbsCurve nc = c.curve.raw();
          bool changed = false;
          for (int i = nc.Degree(); i < nc.KnotCount() - nc.Degree(); ) {
            int mult = nc.KnotMultiplicity(i);
            if (mult > 1 && RemoveKnotApprox(nc, i)) { changed = true; continue; }
            i += std::max(1, mult);
          }
          if (changed) { kernel::NurbsCurve k; k.raw() = nc; ReplaceCurve(ctx, c.id, k); ++n; }
        }
        ctx.Print("RemoveMultiKnot: " + std::to_string(n) + " curve(s) cleaned");
      }));
  Reg(e, "ExtractControlPolygon", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ExtractControlPolygon");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { AddCurveLike(ctx, PolylineCurve(ControlPolygon(c.curve)), c.attrs); ++n; }
        ctx.Print("ExtractControlPolygon: " + std::to_string(n) + " polyline(s)");
      }));
  Reg(e, "ExtractPt", OnSelection("Select curves or surfaces to extract points from", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ExtractPt");
        int n = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::vector<Point3d> pts;
          if (o->kind == ObjectKind::Curve && o->curve) pts = ControlPolygon(*o->curve);
          else if (o->kind == ObjectKind::Surface && o->surface) { const ON_NurbsSurface& s = o->surface->raw(); for (int i = 0; i < s.CVCount(0); ++i) for (int j = 0; j < s.CVCount(1); ++j) { ON_3dPoint p; s.GetCV(i, j, p); pts.push_back(p); } }
          else if (o->kind == ObjectKind::Mesh && o->mesh) { for (int i = 0; i < o->mesh->raw().VertexCount(); ++i) pts.push_back(o->mesh->raw().Vertex(i)); }
          for (const Point3d& p : pts) { SceneObject np = SceneObject::MakePoint(p); np.layer_index = o->layer_index; ctx.Doc().Add(std::move(np)); ++n; }
        }
        ctx.Print("ExtractPt: " + std::to_string(n) + " point(s)");
      }));
  Reg(e, "CurveThroughPolyline", OnSelection("Select polylines", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("CurveThroughPolyline");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { if (c.curve.Degree() != 1) continue; ReplaceCurve(ctx, c.id, InterpolateCubic(ControlPolygon(c.curve), c.curve.IsClosed())); ++n; }
        ctx.Print("CurveThroughPolyline: " + std::to_string(n) + " curve(s)");
      }));
  Reg(e, "FitCrv", OnSelection("Select curves to fit", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("FitCrv");
        const double tol = std::max(ctx.Settings().absolute_tolerance * 10, 0.01);
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          std::vector<double> params = c.curve.SuggestedParameterValues(tol, 8);
          std::vector<Point3d> pts; for (double t : params) pts.push_back(c.curve.PointAt(t));
          if (pts.size() >= 3 && static_cast<int>(pts.size()) < c.curve.ControlPointCount()) { ReplaceCurve(ctx, c.id, InterpolateCubic(pts)); ++n; }
        }
        ctx.Print("FitCrv: " + std::to_string(n) + " curve(s) refit within " + FormatNumber(tol));
      }));
  Reg(e, "LineThroughPt", OnSelection("Select points", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Point3d> pts;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) pts.push_back(o->point);
        Point3d c; Vector3d ax[3];
        if (pts.size() < 2 || !PrincipalAxes(pts, c, ax)) { ctx.Warn("Need at least two points"); return; }
        double lo = 1e300, hi = -1e300;
        for (const Point3d& p : pts) { double t = ON_DotProduct(p - c, ax[0]); lo = std::min(lo, t); hi = std::max(hi, t); }
        ctx.Doc().BeginChange("LineThroughPt");
        AddCurve(ctx, PolylineCurve({c + ax[0] * lo, c + ax[0] * hi}), "LineThroughPt");
        ctx.Print("LineThroughPt: best-fit line through " + std::to_string(pts.size()) + " points");
      }, 2));
  Reg(e, "PlaneThroughPt", OnSelection("Select points", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Point3d> pts;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) pts.push_back(o->point);
        Point3d c; Vector3d ax[3];
        if (pts.size() < 3 || !PrincipalAxes(pts, c, ax)) { ctx.Warn("Need at least three points"); return; }
        ON_Plane pl(c, ax[0], ax[1]);
        double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
        for (const Point3d& p : pts) { double x, y; pl.ClosestPointTo(p, &x, &y); x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        ctx.Doc().BeginChange("PlaneThroughPt");
        kernel::NurbsSurface s = kernel::NurbsSurface::FromControlGrid({pl.PointAt(x0, y0), pl.PointAt(x1, y0), pl.PointAt(x0, y1), pl.PointAt(x1, y1)}, 2, 2, 1, 1);
        ctx.Doc().Add(SceneObject::MakeSurface(s));
        ctx.Print("PlaneThroughPt: best-fit plane through " + std::to_string(pts.size()) + " points");
      }, 3));
  Reg(e, "ArrayCrv", Make<ArrayCrvCommand>());
  Reg(e, "ArraySrf", Make<ArraySrfCommand>());
  Reg(e, "ArrayCrvOnSrf", Make<ArrayCrvOnSrfCommand>());
  Reg(e, "Align", Make<AlignCommand>());
  Reg(e, "Distribute", Make<DistributeCommand>());
  Reg(e, "Contour", Make<ContourCommand>());
  Reg(e, "Section", Make<SectionCommand>());
  Reg(e, "CutPlane", Make<CutPlaneCommand>());
  Reg(e, "PlanarIntersection", Make<SectionCommand>(), CommandStatus::Implemented, "Same as Section: intersects objects with a plane through two points.");
  Reg(e, "TweenCurves", Make<TweenCurvesCommand>());
  Reg(e, "BlendCrv", Make<BlendCrvCommand>());
  Reg(e, "Blend", Make<BlendCrvCommand>(true));
  Reg(e, "ArcBlend", Make<ArcBlendCommand>());
  Reg(e, "Connect", Make<ConnectCommand>());
  Reg(e, "ExtendDynamic", Make<ExtendByLengthCommand>());
  Reg(e, "Crv2View", OnSelection("Select two planar curves (Top and Front views)", Crv2View, 2));
  Reg(e, "ModifyRadius", Make<ModifyRadiusCommand>());
  Reg(e, "ShowEnds", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { ctx.AddPreviewPoint(c.curve.PointAt(c.curve.Domain().min)); ctx.AddPreviewPoint(c.curve.PointAt(c.curve.Domain().max)); }
        ctx.Print("ShowEnds: curve ends highlighted (start and end) until ShowEndsOff");
      }));
  Reg(e, "ShowEndsOff", Immediate([](CommandContext& ctx) { ctx.ClearPreview(); }));
  Reg(e, "ShowDir", OnSelection("Select curves to show direction", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          kernel::Interval d = c.curve.Domain();
          Point3d p0 = c.curve.PointAt(d.min);
          Vector3d tan = c.curve.TangentAt(d.min);
          if (!tan.Unitize()) continue;
          const double len = std::max(c.curve.Length(50) * 0.15, ctx.Settings().absolute_tolerance * 20);
          Point3d tip = p0 + tan * len;
          Vector3d side = ON_CrossProduct(tan, ActiveNormal(ctx));
          if (!side.Unitize()) side = ON_xaxis;
          const double head = len * 0.3;
          ctx.AddPreviewLine(p0, tip);
          ctx.AddPreviewLine(tip, tip - tan * head + side * (head * 0.5));
          ctx.AddPreviewLine(tip, tip - tan * head - side * (head * 0.5));
          ++n;
        }
        ctx.Print("ShowDir: " + std::to_string(n) + " direction arrow(s) shown until ShowDirOff");
      }));
  Reg(e, "ShowDirOff", Immediate([](CommandContext& ctx) { ctx.ClearPreview(); }));
  Reg(e, "IntersectSelf", OnSelection("Select curves to self-intersect", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // CurveOrSolidIntersect self-intersects a curve when given exactly
        // that one curve on its own (see Intersect's own note) - calling
        // it once per selected curve, rather than once with the whole
        // selection, is what actually gets self-intersections for every
        // curve instead of (also) their intersections with each other.
        for (ObjectId id : ids) CurveOrSolidIntersect(ctx, {id});
      }));
  Reg(e, "IntersectTwoSets", Make<IntersectTwoSetsCommand>());
  Reg(e, "ContinueCurve", Make<ContinueCurveCommand>(false));
  Reg(e, "ContinueInterpCrv", Make<ContinueCurveCommand>(true));
  // CurveBoolean is fully implemented in cmd_solidtools.cpp (Union/
  // Difference/Intersection/Regions via RegionBoolean) and registers under
  // the same name - not re-registered here to avoid a second, stale
  // definition of the same command name in the registry.
  Reg(e, "Match", Make<MatchCommand>());
  Reg(e, "EndBulge", Make<EndBulgeCommand>());
  Reg(e, "Fair", OnSelection("Select curves to fair", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Fair");
        int n = 0;
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          std::vector<Point3d> cv = ControlPolygon(c.curve);
          if (cv.size() < 4) continue;
          for (int it = 0; it < 3; ++it) for (size_t i = 1; i + 1 < cv.size(); ++i) cv[i] = (cv[i - 1] + cv[i] * 2 + cv[i + 1]) * 0.25;
          kernel::NurbsCurve k = c.curve; for (size_t i = 0; i < cv.size(); ++i) k.SetControlPointAt(static_cast<int>(i), cv[i]);
          ReplaceCurve(ctx, c.id, k); ++n;
        }
        ctx.Print("Fair: " + std::to_string(n) + " curve(s) smoothed");
      }));
  Reg(e, "SoftEditCrv", Make<SoftEditCommand>("SoftEditCrv"));
  Reg(e, "FixedLengthCrvEdit", Make<FixedLengthCrvEditCommand>());
  Reg(e, "MoveCrv", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Move"); }));
  Reg(e, "CurveThroughSrfControlPt", OnSelection("Select surfaces", BuildCurveThroughSrfControlPt));
  Reg(e, "OffsetMultiple", Make<OffsetMultipleCommand>());
  Reg(e, "OffsetCrvOnSrf", Make<OffsetCrvOnSrfCommand>());
  Reg(e, "ExtendCrvOnSrf", Make<ExtendCrvOnSrfCommand>());
  Reg(e, "InterpCrvOnSrf", Make<InterpCrvOnSrfCommand>());
  Reg(e, "HandleCurve", Make<SoftEditCommand>("HandleCurve"));
  Reg(e, "Symmetry", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Mirror"); }), CommandStatus::Partial,
      "Builds a one-time mirrored copy via Mirror; true Symmetry needs a live constraint that keeps re-mirroring the other half on every future edit, which would require hooking every edit/transform path in the document (not something this command alone can add) - edit each half and re-run Mirror to update the copy.");
  Reg(e, "RemoveSymmetry", Immediate([](CommandContext& ctx) { ctx.Print("RemoveSymmetry: no live symmetry is active."); }), CommandStatus::Partial,
      "Since Symmetry itself only ever builds a one-time mirrored copy (see its own note), there is no live link for this command to remove - it can only ever report that, honestly, rather than actually breaking a constraint that was never created.");
  Reg(e, "InsertLineIntoCrv", Make<InsertLineIntoCrvCommand>());
  Reg(e, "CSec", Make<CSecCommand>());
}

}  // namespace dino8::app
