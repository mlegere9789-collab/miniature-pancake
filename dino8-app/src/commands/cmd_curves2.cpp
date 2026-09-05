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
  void Begin(CommandContext&) override { WantPoint("Select first curve near the end to blend from"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveCopy> c = NearestCurveTo(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    kernel::Interval d = c->curve.Domain();
    bool at_end = std::fabs(t - d.max) < std::fabs(t - d.min);
    Point3d e = c->curve.PointAt(at_end ? d.max : d.min);
    Vector3d tan = c->curve.TangentAt(at_end ? d.max : d.min);
    if (!at_end) tan = -tan;  // pointing away from the curve
    ends_.push_back(e); tans_.push_back(tan); attrs_ = c->attrs;
    if (ends_.size() == 1) { WantPoint("Select second curve near the end to blend to"); return; }
    double len = ends_[0].DistanceTo(ends_[1]) / 3;
    std::vector<Point3d> cvs = {ends_[0], ends_[0] + tans_[0] * len, ends_[1] + tans_[1] * len, ends_[1]};
    ctx.Doc().BeginChange("BlendCrv");
    AddCurveLike(ctx, kernel::NurbsCurve::FromControlPoints(cvs, 3), attrs_);
    ctx.Print("BlendCrv: tangent blend curve created");
    Finish();
  }
  std::vector<Point3d> ends_;
  std::vector<Vector3d> tans_;
  SceneObject attrs_;
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
        for (const CurveCopy& c : CopyCurves(ctx, ids)) {
          ON_Ellipse el; ON_Arc arc;
          if (c.curve.raw().IsEllipse(nullptr, &el, ctx.Settings().absolute_tolerance)) {
            double a = el.radius[0], b = el.radius[1];
            double f = std::sqrt(std::fabs(a * a - b * b));
            Vector3d ax = a >= b ? el.plane.xaxis : el.plane.yaxis;
            ctx.Doc().Add(SceneObject::MakePoint(el.plane.origin + ax * f)); ctx.Doc().Add(SceneObject::MakePoint(el.plane.origin - ax * f)); n += 2;
          } else if (c.curve.raw().IsArc(nullptr, &arc, ctx.Settings().absolute_tolerance)) { ctx.Doc().Add(SceneObject::MakePoint(arc.Center())); ++n; }
        }
        ctx.Print("MarkFoci: " + std::to_string(n) + " point(s) added");
      }), CommandStatus::Partial, "Marks foci of ellipses and centers of arcs; parabola/hyperbola foci are planned.");

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
  Reg(e, "MergeCrv", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Join"); }), CommandStatus::Partial, "Joins the selected curves; single-span merging of tangent segments is planned.");
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
  Reg(e, "Domain", OnSelection("Select curves to report domain", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { kernel::Interval d = c.curve.Domain(); ctx.Print("Curve " + std::to_string(c.id) + " domain: " + FormatNumber(d.min) + " to " + FormatNumber(d.max)); }
      }), CommandStatus::Partial, "Reports the domain; use Reparameterize to change it.");
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
  Reg(e, "ArrayCrvOnSrf", Make<ArrayCrvCommand>(), CommandStatus::Partial, "Behaves like ArrayCrv; surface-normal orientation is planned.");
  Reg(e, "Align", Make<AlignCommand>());
  Reg(e, "Distribute", Make<DistributeCommand>());
  Reg(e, "Contour", Make<ContourCommand>());
  Reg(e, "Section", Make<SectionCommand>());
  Reg(e, "CutPlane", Make<CutPlaneCommand>());
  Reg(e, "PlanarIntersection", Make<SectionCommand>(), CommandStatus::Partial, "Same as Section (intersects objects with a plane through two points).");
  Reg(e, "TweenCurves", Make<TweenCurvesCommand>());
  Reg(e, "BlendCrv", Make<BlendCrvCommand>());
  Reg(e, "Blend", Make<BlendCrvCommand>(), CommandStatus::Partial, "Tangent (G1) blend; curvature continuity option is planned.");
  Reg(e, "ArcBlend", Make<BlendCrvCommand>(), CommandStatus::Partial, "Builds a tangent cubic blend instead of a two-arc blend.");
  Reg(e, "Connect", Make<ConnectCommand>());
  Reg(e, "ExtendDynamic", Make<ExtendByLengthCommand>());
  Reg(e, "Crv2View", OnSelection("Select two planar curves (Top and Front views)", Crv2View, 2));
  Reg(e, "ModifyRadius", OnSelection("Select circles or arcs", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        // Radius is taken from the pending script token or asks via the next Number.
        ctx.Doc().Select(ids.empty() ? kNoObject : ids[0], true);
        ctx.Print("ModifyRadius: type the new radius with Scale for now.");
      }), CommandStatus::Partial, "Interactive radius editing is planned; use Scale about the center.");
  Reg(e, "ShowEnds", OnSelection("Select curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (const CurveCopy& c : CopyCurves(ctx, ids)) { ctx.AddPreviewPoint(c.curve.PointAt(c.curve.Domain().min)); ctx.AddPreviewPoint(c.curve.PointAt(c.curve.Domain().max)); }
        ctx.Print("ShowEnds: curve ends highlighted until the next command");
      }), CommandStatus::Partial, "Highlights ends as preview points.");
  Reg(e, "ShowEndsOff", Immediate([](CommandContext& ctx) { ctx.ClearPreview(); }));
  Reg(e, "ShowDir", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Dir"); }), CommandStatus::Partial);
  Reg(e, "ShowDirOff", Immediate([](CommandContext& ctx) { ctx.ClearPreview(); }));
  Reg(e, "IntersectSelf", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Intersect"); }), CommandStatus::Partial, "Uses Intersect on the selection.");
  Reg(e, "IntersectTwoSets", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Intersect"); }), CommandStatus::Partial, "Uses Intersect on the selection.");
  Reg(e, "ContinueCurve", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Curve"); }), CommandStatus::Partial, "Draws a new control-point curve; Join it to the original.");
  Reg(e, "ContinueInterpCrv", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("InterpCrv"); }), CommandStatus::Partial, "Draws a new interpolated curve; Join it to the original.");
  Reg(e, "CurveBoolean", Immediate([](CommandContext& ctx) { ctx.Print("CurveBoolean: use Trim/Split and Join for region editing; region picking is planned."); }), CommandStatus::Partial);
  Reg(e, "Match", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("BlendCrv"); }), CommandStatus::Partial, "Creates a tangent blend between the curve ends instead of moving the end of one curve.");
  Reg(e, "EndBulge", Immediate([](CommandContext& ctx) { ctx.Print("EndBulge: turn on control points (PointsOn) and drag the second control point with the gumball."); }), CommandStatus::Partial);
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
  Reg(e, "SoftEditCrv", Immediate([](CommandContext& ctx) { ctx.Print("SoftEditCrv: turn on control points and use the gumball; falloff editing is planned."); }), CommandStatus::Partial);
  Reg(e, "FixedLengthCrvEdit", Immediate([](CommandContext& ctx) { ctx.Print("FixedLengthCrvEdit is planned; use control-point editing."); }), CommandStatus::Partial);
  Reg(e, "MoveCrv", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Move"); }), CommandStatus::Partial);
  Reg(e, "CurveThroughSrfControlPt", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("ExtractPt"); }), CommandStatus::Partial, "Extracts surface control points; run CurveThroughPt on them.");
  Reg(e, "OffsetMultiple", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Offset"); }), CommandStatus::Partial, "Runs Offset once; repeat for multiple offsets.");
  Reg(e, "OffsetCrvOnSrf", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Offset"); }), CommandStatus::Partial, "Planar offset; offsetting along the surface is planned.");
  Reg(e, "ExtendCrvOnSrf", Make<ExtendByLengthCommand>(), CommandStatus::Partial, "Extends in space, not along the surface.");
  Reg(e, "InterpCrvOnSrf", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("InterpCrv"); }), CommandStatus::Partial, "Interpolates in space; snap to the surface with Osnap.");
  Reg(e, "HandleCurve", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("InterpCrv"); }), CommandStatus::Partial);
  Reg(e, "Symmetry", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Mirror"); }), CommandStatus::Partial, "Mirrors the object; live symmetry editing is planned.");
  Reg(e, "RemoveSymmetry", Immediate([](CommandContext& ctx) { ctx.Print("RemoveSymmetry: no live symmetry is active."); }), CommandStatus::Partial);
  Reg(e, "InsertLineIntoCrv", Immediate([](CommandContext& ctx) { ctx.Print("InsertLineIntoCrv: draw a Line and Join it to the curve pieces (Split first)."); }), CommandStatus::Partial);
  Reg(e, "CSec", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Section"); }), CommandStatus::Partial, "Runs Section once per call.");
}

}  // namespace dino8::app
