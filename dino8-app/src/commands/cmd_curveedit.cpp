// Curve editing commands: Intersect (curve/curve), Split (curves by curves
// or points), Trim, Fillet, Chamfer, FilletCorners.
//
// OpenNURBS public ships no general curve/curve intersector, so this file
// carries its own: each curve is sampled into a polyline with the kernel's
// flatness-driven SuggestedParameterValues(), segment pairs are tested in
// the active construction plane (2D), and every candidate is refined with
// a Newton iteration on both parameters (falling back to an alternating
// golden-section closest-approach when the crossing is tangential) down to
// the document's absolute tolerance.
#include "commands/cmd_common.h"

#include <algorithm>
#include <limits>

namespace dino8::app {

namespace {

// ---------------------------------------------------------------------------
// Curve/curve intersection
// ---------------------------------------------------------------------------

struct CurveHit {
  double ta = 0, tb = 0;  // parameters on A and B
  Point3d point;          // 3D point (midway between the two curves)
  double gap = 0;         // 3D separation of the two curves at the hit
};

struct Proj {
  // Coordinates of p in the construction plane: (u, v) in-plane, w along the normal.
  const ON_Plane& plane;
  ON_3dPoint operator()(Point3d p) const {
    const Vector3d d = p - plane.origin;
    return ON_3dPoint(ON_DotProduct(d, plane.xaxis), ON_DotProduct(d, plane.yaxis), ON_DotProduct(d, plane.zaxis));
  }
  ON_2dVector Dir(Vector3d d) const { return ON_2dVector(ON_DotProduct(d, plane.xaxis), ON_DotProduct(d, plane.yaxis)); }
};

struct Sampled {
  std::vector<double> t;
  std::vector<ON_3dPoint> p;  // projected (u, v, w)
};

Sampled SampleCurve(const kernel::NurbsCurve& c, const Proj& proj, double chord_tol) {
  Sampled s;
  std::vector<double> params = c.SuggestedParameterValues(chord_tol, 10);
  // Guarantee a reasonable minimum so nearly-straight curves still get a few segments.
  if (params.size() < 9) {
    const kernel::Interval d = c.Domain();
    params.clear();
    for (int i = 0; i <= 8; ++i) params.push_back(d.min + (d.max - d.min) * i / 8.0);
  }
  for (double t : params) { s.t.push_back(t); s.p.push_back(proj(c.PointAt(t))); }
  return s;
}

// Parametric 2D segment/segment intersection. Returns fractions along each segment.
bool SegSeg2D(const ON_3dPoint& a0, const ON_3dPoint& a1, const ON_3dPoint& b0, const ON_3dPoint& b1, double& sa, double& sb) {
  const double rx = a1.x - a0.x, ry = a1.y - a0.y, qx = b1.x - b0.x, qy = b1.y - b0.y;
  const double den = rx * qy - ry * qx;
  const double len = std::sqrt(rx * rx + ry * ry) * std::sqrt(qx * qx + qy * qy);
  if (len <= 0 || std::fabs(den) <= 1e-12 * len) return false;  // parallel
  const double dx = b0.x - a0.x, dy = b0.y - a0.y;
  sa = (dx * qy - dy * qx) / den;
  sb = (dx * ry - dy * rx) / den;
  const double eps = 1e-6;
  return sa >= -eps && sa <= 1 + eps && sb >= -eps && sb <= 1 + eps;
}

double SegSegDistance2D(const ON_3dPoint& a0, const ON_3dPoint& a1, const ON_3dPoint& b0, const ON_3dPoint& b1) {
  auto pt_seg = [](const ON_3dPoint& p, const ON_3dPoint& s0, const ON_3dPoint& s1) {
    const double dx = s1.x - s0.x, dy = s1.y - s0.y, l2 = dx * dx + dy * dy;
    double f = l2 > 0 ? ((p.x - s0.x) * dx + (p.y - s0.y) * dy) / l2 : 0;
    f = std::clamp(f, 0.0, 1.0);
    const double ex = s0.x + dx * f - p.x, ey = s0.y + dy * f - p.y;
    return std::sqrt(ex * ex + ey * ey);
  };
  return std::min({pt_seg(a0, b0, b1), pt_seg(a1, b0, b1), pt_seg(b0, a0, a1), pt_seg(b1, a0, a1)});
}

struct Refiner {
  const kernel::NurbsCurve& a;
  const kernel::NurbsCurve& b;
  const Proj& proj;
  double Dist2D(double ta, double tb) const {
    const ON_3dPoint pa = proj(a.PointAt(ta)), pb = proj(b.PointAt(tb));
    return std::hypot(pa.x - pb.x, pa.y - pb.y);
  }
  // Golden-section minimum of f on [lo, hi].
  template <typename F>
  static double Golden(F f, double lo, double hi) {
    const double g = (std::sqrt(5.0) - 1) / 2;
    double c = hi - g * (hi - lo), d = lo + g * (hi - lo), fc = f(c), fd = f(d);
    for (int i = 0; i < 60 && hi - lo > 1e-13; ++i) {
      if (fc < fd) { hi = d; d = c; fd = fc; c = hi - g * (hi - lo); fc = f(c); }
      else { lo = c; c = d; fc = fd; d = lo + g * (hi - lo); fd = f(d); }
    }
    return (lo + hi) / 2;
  }
  // Newton on F(ta, tb) = A(ta) - B(tb) projected into the plane.
  bool Newton(double& ta, double& tb, const ON_Interval& wa, const ON_Interval& wb, double tol) const {
    for (int it = 0; it < 25; ++it) {
      ON_3dPoint PA, PB; ON_3dVector DA, DB;
      a.raw().Ev1Der(ta, PA, DA);
      b.raw().Ev1Der(tb, PB, DB);
      const ON_3dPoint pa = proj(PA), pb = proj(PB);
      const ON_2dVector da = proj.Dir(DA), db = proj.Dir(DB);
      const double fx = pa.x - pb.x, fy = pa.y - pb.y;
      if (std::hypot(fx, fy) <= tol * 1e-3) return true;
      const double det = -da.x * db.y + db.x * da.y;
      const double scale = da.Length() * db.Length();
      if (scale <= 0 || std::fabs(det) <= 1e-10 * scale) return false;  // tangential
      const double dta = (-fx * (-db.y) - (-fy) * (-db.x)) / det;   // Cramer's rule on [da -db][dta dtb]^T = -F
      const double dtb = (da.x * (-fy) - da.y * (-fx)) / det;
      ta = std::clamp(ta + dta, wa.Min(), wa.Max());
      tb = std::clamp(tb + dtb, wb.Min(), wb.Max());
    }
    return Dist2D(ta, tb) <= tol;
  }
  // Alternating closest-approach minimisation (robust at tangencies).
  void Alternate(double& ta, double& tb, const ON_Interval& wa, const ON_Interval& wb) const {
    for (int round = 0; round < 8; ++round) {
      const double ta_fixed = ta;
      tb = Golden([&](double t) { return Dist2D(ta_fixed, t); }, wb.Min(), wb.Max());
      const double tb_fixed = tb;
      ta = Golden([&](double t) { return Dist2D(t, tb_fixed); }, wa.Min(), wa.Max());
    }
  }
};

// All intersections of `a` with `b`, projected onto `plane`. Hits whose 3D
// separation exceeds `normal_tol` are dropped (pass a huge value for
// "apparent" intersections, the way Rhino's Trim/Split treat curves seen in
// a plan view). `same_curve` skips adjacent-segment joints so a curve can
// be intersected with itself.
std::vector<CurveHit> IntersectCurves(const kernel::NurbsCurve& a, const kernel::NurbsCurve& b, const ON_Plane& plane,
                                      double tol, double normal_tol, bool same_curve = false) {
  std::vector<CurveHit> hits;
  const Proj proj{plane};
  kernel::BoundingBox ba = a.GetTightBoundingBox(), bb = b.GetTightBoundingBox();
  const double diag = std::max((ba.max - ba.min).Length(), (bb.max - bb.min).Length());
  if (diag <= 0) return hits;
  const double chord = std::max(tol, diag * 1e-3);
  const Sampled sa = SampleCurve(a, proj, chord), sb = same_curve ? sa : SampleCurve(b, proj, chord);
  const Refiner refine{a, b, proj};
  const ON_Interval da(a.Domain().min, a.Domain().max), db(b.Domain().min, b.Domain().max);
  const bool a_closed = a.IsClosed();
  const size_t na = sa.t.size() - 1, nb = sb.t.size() - 1;
  struct Box { double x0, y0, x1, y1; };
  auto box_of = [&](const Sampled& s, size_t i) {
    return Box{std::min(s.p[i].x, s.p[i + 1].x) - chord, std::min(s.p[i].y, s.p[i + 1].y) - chord,
               std::max(s.p[i].x, s.p[i + 1].x) + chord, std::max(s.p[i].y, s.p[i + 1].y) + chord};
  };
  std::vector<Box> boxes_b;
  for (size_t j = 0; j < nb; ++j) boxes_b.push_back(box_of(sb, j));
  auto push_hit = [&](double ta, double tb, double max_2d) {
    const Point3d pa = a.PointAt(ta), pb = b.PointAt(tb);
    const double gap = pa.DistanceTo(pb);
    if (gap > normal_tol) return;
    if (refine.Dist2D(ta, tb) > max_2d) return;
    const Point3d mid = (pa + pb) * 0.5;
    if (same_curve && std::fabs(ta - tb) <= 1e-9 * std::max(1.0, da.Length())) return;
    for (const CurveHit& h : hits) if (h.point.DistanceTo(mid) <= tol * 10 || (h.point.DistanceTo(mid) <= chord && std::fabs(h.ta - ta) <= 1e-6 * da.Length() + 1e-12)) return;
    hits.push_back({ta, tb, mid, gap});
  };
  for (size_t i = 0; i < na; ++i) {
    const Box bi = box_of(sa, i);
    for (size_t j = 0; j < nb; ++j) {
      if (same_curve) {
        const size_t d = i > j ? i - j : j - i;
        if (d <= 1) continue;
        if (a_closed && (d == na - 1)) continue;
        if (j < i) continue;  // each pair once
      }
      const Box& bj = boxes_b[j];
      if (bi.x1 < bj.x0 || bj.x1 < bi.x0 || bi.y1 < bj.y0 || bj.y1 < bi.y0) continue;
      double fa = 0, fb = 0;
      const bool crossing = SegSeg2D(sa.p[i], sa.p[i + 1], sb.p[j], sb.p[j + 1], fa, fb);
      if (!crossing) {
        if (SegSegDistance2D(sa.p[i], sa.p[i + 1], sb.p[j], sb.p[j + 1]) > chord) continue;
        fa = fb = 0.5;
      }
      double ta = sa.t[i] + (sa.t[i + 1] - sa.t[i]) * std::clamp(fa, 0.0, 1.0);
      double tb = sb.t[j] + (sb.t[j + 1] - sb.t[j]) * std::clamp(fb, 0.0, 1.0);
      // Refinement windows: this segment plus its neighbours.
      const ON_Interval wa(sa.t[i > 0 ? i - 1 : 0], sa.t[std::min(i + 2, na)]);
      const ON_Interval wb(sb.t[j > 0 ? j - 1 : 0], sb.t[std::min(j + 2, nb)]);
      double ra = ta, rb = tb, max_2d = tol;
      bool ok = crossing && refine.Newton(ra, rb, wa, wb, tol);
      if (!ok) {
        ra = ta; rb = tb;
        refine.Alternate(ra, rb, wa, wb);
        ok = refine.Dist2D(ra, rb) <= tol;
        if (!ok && crossing) { ra = ta; rb = tb; max_2d = chord; ok = refine.Dist2D(ra, rb) <= chord; }  // a genuine crossing, keep the chord estimate
      }
      if (ok) push_hit(ra, rb, max_2d);
    }
  }
  std::sort(hits.begin(), hits.end(), [](const CurveHit& x, const CurveHit& y) { return x.ta < y.ta; });
  return hits;
}

constexpr double kApparent = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// A curve object copied out of the document (safe across Add/Remove).
struct CurveRef {
  ObjectId id = kNoObject;
  kernel::NurbsCurve curve;
  SceneObject attrs;  // for layer / colour / name
};

std::optional<CurveRef> CopyCurve(CommandContext& ctx, ObjectId id) {
  const SceneObject* o = ctx.Doc().Find(id);
  if (!o || o->kind != ObjectKind::Curve || !o->curve) return std::nullopt;
  CurveRef r;
  r.id = id;
  r.curve = *o->curve;
  r.attrs = *o;
  return r;
}

// Adds `c` to the document with the attributes of `like`.
ObjectId AddLike(CommandContext& ctx, const kernel::NurbsCurve& c, const SceneObject& like) {
  SceneObject n = SceneObject::MakeCurve(c);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  n.name = like.name;
  n.material_name = like.material_name;
  n.user_text = like.user_text;
  n.group_id = like.group_id;
  return ctx.Doc().Add(std::move(n));
}

// The visible, unlocked curve object nearest to `p` (excluding `skip`).
std::optional<CurveRef> NearestCurve(CommandContext& ctx, Point3d p, double* out_t, const std::vector<ObjectId>& skip = {}) {
  const SceneObject* best = nullptr;
  double best_d = std::numeric_limits<double>::max(), best_t = 0;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (o.kind != ObjectKind::Curve || !o.curve || !ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    if (std::find(skip.begin(), skip.end(), o.id) != skip.end()) continue;
    const double t = o.curve->ClosestPointParameter(p);
    const double d = o.curve->PointAt(t).DistanceTo(p);
    if (d < best_d) { best_d = d; best = &o; best_t = t; }
  }
  if (!best) return std::nullopt;
  if (out_t) *out_t = best_t;
  return CopyCurve(ctx, best->id);
}

// Joins consecutive curves (already oriented end-to-start) into one NURBS curve.
bool JoinCurves(const std::vector<kernel::NurbsCurve>& parts, kernel::NurbsCurve& out) {
  if (parts.empty()) return false;
  if (parts.size() == 1) { out = parts[0]; return true; }
  ON_PolyCurve pc;
  for (const kernel::NurbsCurve& c : parts) pc.Append(new ON_NurbsCurve(c.raw()));
  return CurveFromON(pc, out);
}

// Parameters strictly inside the domain, sorted and de-duplicated.
std::vector<double> CleanParams(const kernel::NurbsCurve& c, std::vector<double> params) {
  const kernel::Interval d = c.Domain();
  const double eps = std::max(1e-9, (d.max - d.min) * 1e-7);
  std::sort(params.begin(), params.end());
  std::vector<double> out;
  for (double t : params) {
    if (t <= d.min + eps || t >= d.max - eps) continue;
    if (!out.empty() && t - out.back() <= eps) continue;
    out.push_back(t);
  }
  return out;
}

// Splits `c` at `params`. A closed curve split at one point becomes one open
// curve starting there; at n points it becomes n pieces (the seam is healed).
std::vector<kernel::NurbsCurve> SplitAt(const kernel::NurbsCurve& c, std::vector<double> params) {
  std::vector<kernel::NurbsCurve> pieces;
  params = CleanParams(c, params);
  if (params.empty()) return pieces;
  kernel::NurbsCurve rest = c;
  for (double t : params) {
    kernel::NurbsCurve l, r;
    if (rest.Split(t, l, r) != kernel::Result::Ok) continue;
    pieces.push_back(l);
    rest = r;
  }
  pieces.push_back(rest);
  if (c.IsClosed() && pieces.size() >= 2) {
    kernel::NurbsCurve healed;
    if (JoinCurves({pieces.back(), pieces.front()}, healed)) {
      pieces.pop_back();
      pieces.front() = healed;
    }
  }
  return pieces;
}

// Parameters where `target` meets any of `cutters` (apparent intersections in the CPlane).
std::vector<double> CutParams(CommandContext& ctx, const CurveRef& target, const std::vector<CurveRef>& cutters,
                              const std::vector<Point3d>& cut_points) {
  const ON_Plane plane = ActivePlane(ctx);
  const double tol = ctx.Settings().absolute_tolerance;
  std::vector<double> params;
  for (const CurveRef& cut : cutters) {
    const bool self = cut.id == target.id;
    for (const CurveHit& h : IntersectCurves(target.curve, cut.curve, plane, tol, kApparent, self)) {
      params.push_back(h.ta);
      if (self) params.push_back(h.tb);
    }
  }
  for (const Point3d& p : cut_points) {
    const double t = target.curve.ClosestPointParameter(p);
    if (target.curve.PointAt(t).DistanceTo(p) <= tol * 10) params.push_back(t);
  }
  return params;
}

std::string Plural(size_t n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }

// ---------------------------------------------------------------------------
// Intersect
// ---------------------------------------------------------------------------

void IntersectObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<CurveRef> curves;
  for (ObjectId id : ids) if (std::optional<CurveRef> c = CopyCurve(ctx, id)) curves.push_back(*c);
  if (curves.empty()) {
    // No curves: fall back to the solid/solid intersection volume.
    if (ids.size() >= 2) {
      const SceneObject* a = ctx.Doc().Find(ids[0]); const SceneObject* b = ctx.Doc().Find(ids[1]);
      std::optional<kernel::Mesh> ma = a ? MeshOf(*a, 0.005) : std::nullopt, mb = b ? MeshOf(*b, 0.005) : std::nullopt;
      if (ma && mb && ma->IsClosedManifold() && mb->IsClosedManifold()) {
        try {
          kernel::Mesh r = kernel::BooleanCombine(*ma, *mb, kernel::BooleanOp::Intersection);
          if (r.FaceCount() == 0) { ctx.Print("No intersection"); return; }
          AddObject(ctx, SceneObject::MakeMesh(r), "Intersect");
          ctx.Print("Intersection volume " + FormatNumber(std::fabs(r.Volume())));
        } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
        return;
      }
    }
    ctx.Warn("Intersect needs two or more curves (or two closed solids)");
    return;
  }
  const ON_Plane plane = ActivePlane(ctx);
  const double tol = ctx.Settings().absolute_tolerance;
  std::vector<Point3d> points;
  auto add_points = [&](const std::vector<CurveHit>& hits) { for (const CurveHit& h : hits) points.push_back(h.point); };
  if (curves.size() == 1) {
    add_points(IntersectCurves(curves[0].curve, curves[0].curve, plane, tol, tol, true));
    if (points.empty()) { ctx.Print("Curve does not self-intersect"); return; }
  } else {
    for (size_t i = 0; i < curves.size(); ++i)
      for (size_t j = i + 1; j < curves.size(); ++j) add_points(IntersectCurves(curves[i].curve, curves[j].curve, plane, tol, tol));
  }
  if (points.empty()) { ctx.Print("No intersections found"); return; }
  ctx.Doc().BeginChange("Intersect");
  ctx.Doc().SelectNone();
  for (const Point3d& p : points) {
    const ObjectId id = ctx.Doc().Add(SceneObject::MakePoint(p));
    ctx.Doc().Select(id, true);
    ctx.Print("  intersection at " + FormatPoint(p));
  }
  ctx.Print("Found " + Plural(points.size(), "intersection") + "; created " + Plural(points.size(), "point"));
}

// ---------------------------------------------------------------------------
// Split: curves by curves/points, or solids by a plane (delegated).
// ---------------------------------------------------------------------------

class SplitCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to split"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (inner_) { inner_->OnObjects(ctx, ids); Sync(); return; }
    if (targets_.empty() && !have_targets_) {
      for (ObjectId id : ids) if (std::optional<CurveRef> c = CopyCurve(ctx, id)) targets_.push_back(*c);
      if (targets_.empty()) {
        // No curves: delegate to the solid plane split.
        const RegisteredCommand* r = ctx.Engine().Find("BooleanSplit");
        if (!r || !r->factory) { ctx.Warn("Select curves to split"); Finish(); return; }
        inner_ = r->factory();
        inner_->Begin(ctx);
        inner_->OnObjects(ctx, ids);
        Sync();
        return;
      }
      have_targets_ = true;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      WantObjects("Select cutting objects");
      accept_preselection = false;
      return;
    }
    std::vector<CurveRef> cutters;
    std::vector<Point3d> points;
    for (ObjectId id : ids) {
      if (std::optional<CurveRef> c = CopyCurve(ctx, id)) cutters.push_back(*c);
      else if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) points.push_back(o->point);
    }
    if (cutters.empty() && points.empty()) { ctx.Warn("Select curves or points as cutting objects"); Finish(); return; }
    ctx.Doc().BeginChange("Split");
    int split_count = 0, pieces_total = 0;
    ctx.Doc().SelectNone();
    for (const CurveRef& target : targets_) {
      std::vector<kernel::NurbsCurve> pieces = SplitAt(target.curve, CutParams(ctx, target, cutters, points));
      if (pieces.size() < 2 && !(pieces.size() == 1 && target.curve.IsClosed())) continue;
      ctx.Doc().Remove(target.id);
      for (const kernel::NurbsCurve& p : pieces) ctx.Doc().Select(AddLike(ctx, p, target.attrs), true);
      ++split_count;
      pieces_total += static_cast<int>(pieces.size());
    }
    if (split_count == 0) ctx.Warn("Cutting objects do not intersect the curves to split");
    else ctx.Print("Split " + Plural(split_count, "curve") + " into " + Plural(pieces_total, "piece"));
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override { if (inner_) { inner_->OnPoint(ctx, p); Sync(); } }
  void OnHover(CommandContext& ctx, Point3d p) override { if (inner_) { inner_->OnHover(ctx, p); Sync(); } }
  void OnEnter(CommandContext& ctx) override { if (inner_) { inner_->OnEnter(ctx); Sync(); } else Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override { if (inner_) { inner_->OnText(ctx, t); Sync(); } }
  void OnNumber(CommandContext& ctx, double v) override { if (inner_) { inner_->OnNumber(ctx, v); Sync(); } }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override { if (inner_) { inner_->OnOption(ctx, n, v); Sync(); } }
  void OnCancel(CommandContext& ctx) override { if (inner_) inner_->OnCancel(ctx); ctx.ClearPreview(); }

 private:
  void Sync() {
    want = inner_->want; prompt = inner_->prompt; options = inner_->options; finished = inner_->finished;
    accept_preselection = inner_->accept_preselection; min_objects = inner_->min_objects;
    default_number = inner_->default_number; default_text = inner_->default_text;
  }
  std::vector<CurveRef> targets_;
  bool have_targets_ = false;
  std::unique_ptr<Command> inner_;
};

// ---------------------------------------------------------------------------
// Trim: cutters first, then click the portions to remove.
// ---------------------------------------------------------------------------

class TrimCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select cutting objects"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      if (std::optional<CurveRef> c = CopyCurve(ctx, id)) cutters_.push_back(*c);
      else if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) points_.push_back(o->point);
    }
    if (cutters_.empty() && points_.empty()) { ctx.Warn("Select curves or points as cutting objects"); Finish(); return; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    WantPoint("Select object to trim (press Enter when done)");
  }
  void OnEnter(CommandContext&) override { Finish(); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double tc = 0;
    std::optional<CurveRef> target = NearestCurve(ctx, p, &tc);
    if (!target) { ctx.Warn("No curve near that point"); return; }
    const kernel::NurbsCurve& c = target->curve;
    const kernel::Interval dom = c.Domain();
    std::vector<double> params = CleanParams(c, CutParams(ctx, *target, cutters_, points_));
    if (params.empty()) { ctx.Warn("Cutting objects do not intersect curve " + std::to_string(target->id)); return; }
    std::vector<kernel::NurbsCurve> result;
    if (c.IsClosed()) {
      if (params.size() < 2) { ctx.Warn("A closed curve needs two intersections to trim"); return; }
      // Find the cyclic interval containing the click and keep everything else as one curve.
      size_t k = 0;
      while (k < params.size() && params[k] < tc) ++k;
      const size_t i0 = (k == 0) ? params.size() - 1 : k - 1;  // interval [params[i0], params[k % n]] (cyclic)
      const size_t i1 = k % params.size();
      const bool wraps = i1 < i0 || k == 0 || k == params.size();
      kernel::NurbsCurve kept = c;
      if (wraps) {
        // Removing the seam-crossing piece: keep [params.front(), params.back()].
        if (kept.Trim(params[i1], params[i0]) != kernel::Result::Ok) { ctx.Warn("Trim failed"); return; }
        result.push_back(kept);
      } else {
        kernel::NurbsCurve tail = c, head = c;
        if (tail.Trim(params[i1], dom.max) != kernel::Result::Ok || head.Trim(dom.min, params[i0]) != kernel::Result::Ok) { ctx.Warn("Trim failed"); return; }
        kernel::NurbsCurve joined;
        if (!JoinCurves({tail, head}, joined)) { ctx.Warn("Trim failed"); return; }
        result.push_back(joined);
      }
    } else {
      double lo = dom.min, hi = dom.max;
      for (double t : params) { if (t <= tc) lo = std::max(lo, t); if (t >= tc) hi = std::min(hi, t); }
      const bool at_start = lo <= dom.min, at_end = hi >= dom.max;
      if (at_start && at_end) { ctx.Warn("Nothing to trim"); return; }
      if (!at_start) { kernel::NurbsCurve a = c; if (a.Trim(dom.min, lo) == kernel::Result::Ok) result.push_back(a); }
      if (!at_end) { kernel::NurbsCurve b = c; if (b.Trim(hi, dom.max) == kernel::Result::Ok) result.push_back(b); }
    }
    ctx.Doc().BeginChange("Trim");
    ctx.Doc().Remove(target->id);
    for (const kernel::NurbsCurve& r : result) AddLike(ctx, r, target->attrs);
    ctx.Print("Trimmed curve " + std::to_string(target->id) + (result.size() == 2 ? " (split into two pieces)" : ""));
    WantPoint("Select object to trim (press Enter when done)");
  }

 private:
  std::vector<CurveRef> cutters_;
  std::vector<Point3d> points_;
};

// ---------------------------------------------------------------------------
// Fillet / Chamfer between two curves
// ---------------------------------------------------------------------------

class FilletChamferCommand : public Command {
 public:
  explicit FilletChamferCommand(bool chamfer) : chamfer_(chamfer), size_(1.0) {}
  const char* Name() const { return chamfer_ ? "Chamfer" : "Fillet"; }
  const char* SizeName() const { return chamfer_ ? "Distance" : "Radius"; }
  void Begin(CommandContext&) override {
    options = {{SizeName(), FormatNumber(size_), {}, true, false}, {"Join", join_ ? "Yes" : "No", {"Yes", "No"}, false, true}};
    WantPoint(std::string("Select first curve to ") + (chamfer_ ? "chamfer" : "fillet") + " (or type the " + SizeName() + ")");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == SizeName()) { const double d = std::atof(v.c_str()); if (d >= 0) size_ = d; options[0].value = FormatNumber(size_); ctx.Print(std::string(SizeName()) + "=" + FormatNumber(size_)); }
    if (n == "Join") { join_ = (v == "Yes"); options[1].value = join_ ? "Yes" : "No"; }
  }
  void OnNumber(CommandContext& ctx, double v) override { if (v >= 0) OnOption(ctx, SizeName(), FormatNumber(v)); }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* e = nullptr; const double v = std::strtod(t.c_str(), &e);
    if (e && !*e && e != t.c_str()) OnNumber(ctx, v);
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    double t = 0;
    std::optional<CurveRef> c = NearestCurve(ctx, p, &t);
    if (!c) { ctx.Warn("No curve near that point"); return; }
    if (!first_) { first_ = *c; t1_ = t; WantPoint(std::string("Select second curve to ") + (chamfer_ ? "chamfer" : "fillet")); return; }
    if (c->id == first_->id) { ctx.Warn("Pick a different curve for the second pick"); return; }
    Apply(ctx, *first_, t1_, *c, t);
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  // Trims (or extends, for lines) `cr` so it ends at `tangent` on the side of the pick `tp`.
  // Returns the curve oriented so that its END is at the tangent point.
  bool CutToTangent(CommandContext& ctx, const CurveRef& cr, double tp, Point3d corner, Vector3d dir, Point3d tangent, kernel::NurbsCurve& out, bool& approximate) {
    const kernel::NurbsCurve& c = cr.curve;
    const kernel::Interval d = c.Domain();
    if (c.IsLinear()) {
      // Exact: the kept end is the endpoint farthest along `dir` from the corner.
      const Point3d e0 = c.PointAt(d.min), e1 = c.PointAt(d.max);
      const Point3d keep = ON_DotProduct(e0 - corner, dir) >= ON_DotProduct(e1 - corner, dir) ? e0 : e1;
      // The tangent point must lie between the corner and the kept end (extending a short line is fine only up to its kept end).
      if (ON_DotProduct(keep - corner, dir) <= ON_DotProduct(tangent - corner, dir) + ctx.Settings().absolute_tolerance) return false;
      out = PolylineCurve({keep, tangent});
      return true;
    }
    approximate = true;
    const double tt = c.ClosestPointParameter(tangent);
    if (c.PointAt(tt).DistanceTo(tangent) > ctx.Settings().absolute_tolerance * 10) ctx.Warn("Tangent point is off curve " + std::to_string(cr.id) + "; result is approximate");
    out = c;
    if (tp >= tt) {
      if (out.Trim(tt, d.max) != kernel::Result::Ok) return false;
      out.Reverse();
    } else {
      if (out.Trim(d.min, tt) != kernel::Result::Ok) return false;
    }
    return true;
  }

  void Apply(CommandContext& ctx, const CurveRef& A, double ta, const CurveRef& B, double tb) {
    const ON_Plane plane = ActivePlane(ctx);
    const Proj proj{plane};
    const double tol = ctx.Settings().absolute_tolerance;
    const Point3d PA = A.curve.PointAt(ta), PB = B.curve.PointAt(tb);
    Vector3d TA = A.curve.TangentAt(ta), TB = B.curve.TangentAt(tb);
    // Corner: intersection of the local tangent lines, solved in the CPlane.
    const ON_3dPoint pa = proj(PA), pb = proj(PB);
    const ON_2dVector da = proj.Dir(TA), db = proj.Dir(TB);
    const double det = da.x * (-db.y) - da.y * (-db.x);
    if (std::fabs(det) <= 1e-9) { ctx.Warn("Curves are parallel at the picked points; cannot " + std::string(Name())); return; }
    const double rx = pb.x - pa.x, ry = pb.y - pa.y;
    const double sa = (rx * (-db.y) - ry * (-db.x)) / det;
    const double sb = (da.x * ry - da.y * rx) / det;
    const Point3d CA = PA + TA * sa, CB = PB + TB * sb;
    const Point3d corner = (CA + CB) * 0.5;
    Vector3d dirA = PA - corner, dirB = PB - corner;
    if (dirA.Length() <= tol) dirA = ON_DotProduct(TA, A.curve.PointAt(A.curve.Domain().min) - corner) > 0 ? -TA : TA;  // picked at the corner itself
    if (dirB.Length() <= tol) dirB = ON_DotProduct(TB, B.curve.PointAt(B.curve.Domain().min) - corner) > 0 ? -TB : TB;
    dirA.Unitize(); dirB.Unitize();
    const double cos_full = std::clamp(ON_DotProduct(dirA, dirB), -1.0, 1.0);
    const double half = std::acos(cos_full) / 2;  // half the angle between the kept legs
    if (half <= 1e-6 || half >= ON_PI / 2 - 1e-6) { ctx.Warn("Curves are collinear at the picked points"); return; }
    double dist = chamfer_ ? size_ : size_ / std::tan(half);  // corner -> tangent point
    const Point3d TP_A = corner + dirA * dist, TP_B = corner + dirB * dist;
    // Middle piece.
    std::optional<kernel::NurbsCurve> middle;
    if (size_ > 0) {
      if (chamfer_) middle = PolylineCurve({TP_A, TP_B});
      else {
        Vector3d bis = dirA + dirB; bis.Unitize();
        const Point3d center = corner + bis * (size_ / std::sin(half));
        Vector3d to_corner = corner - center; to_corner.Unitize();
        const Point3d mid = center + to_corner * size_;
        ON_Arc arc(TP_A, mid, TP_B);
        kernel::NurbsCurve k;
        if (!arc.IsValid() || !CurveFromON(ON_ArcCurve(arc), k)) { ctx.Warn("Could not build the fillet arc"); return; }
        middle = k;
      }
    }
    bool approx = false;
    kernel::NurbsCurve a2, b2;
    const bool okA = CutToTangent(ctx, A, ta, corner, dirA, TP_A, a2, approx);
    const bool okB = CutToTangent(ctx, B, tb, corner, dirB, TP_B, b2, approx);
    if (!okA || !okB) { ctx.Warn(std::string(SizeName()) + " " + FormatNumber(size_) + " does not fit on the curves"); return; }
    ctx.Doc().BeginChange(Name());
    ctx.Doc().SelectNone();
    if (join_) {
      std::vector<kernel::NurbsCurve> parts = {a2};
      if (middle) parts.push_back(*middle);
      b2.Reverse();  // B piece must start at its tangent point
      parts.push_back(b2);
      kernel::NurbsCurve joined;
      if (JoinCurves(parts, joined)) {
        ctx.Doc().Remove(A.id); ctx.Doc().Remove(B.id);
        ctx.Doc().Select(AddLike(ctx, joined, A.attrs), true);
        ctx.Print(std::string(Name()) + ": joined result, " + SizeName() + "=" + FormatNumber(size_) + (approx ? " (approximate on non-linear curves)" : ""));
        return;
      }
      ctx.Warn("Join failed; leaving three separate curves");
    }
    ctx.Doc().Remove(A.id); ctx.Doc().Remove(B.id);
    ctx.Doc().Select(AddLike(ctx, a2, A.attrs), true);
    ctx.Doc().Select(AddLike(ctx, b2, B.attrs), true);
    if (middle) ctx.Doc().Select(AddLike(ctx, *middle, A.attrs), true);
    ctx.Print(std::string(Name()) + ": " + SizeName() + "=" + FormatNumber(size_) + (middle ? (chamfer_ ? ", chamfer line added" : ", arc added") : ", corner made") + (approx ? " (approximate on non-linear curves)" : ""));
  }

  bool chamfer_;
  double size_;
  bool join_ = false;
  std::optional<CurveRef> first_;
  double t1_ = 0;
};

// ---------------------------------------------------------------------------
// FilletCorners: round every corner of a polyline that the radius fits.
// ---------------------------------------------------------------------------

class FilletCornersCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select polylines to fillet"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) if (std::optional<CurveRef> c = CopyCurve(ctx, id)) if (c->curve.Degree() == 1) targets_.push_back(*c);
    if (targets_.empty()) { ctx.Warn("Select polylines (degree-1 curves)"); Finish(); return; }
    WantNumber("Fillet radius", 1.0);
  }
  void OnNumber(CommandContext& ctx, double r) override {
    if (r <= 0) { ctx.Warn("Radius must be positive"); return; }
    ctx.Doc().BeginChange("FilletCorners");
    ctx.Doc().SelectNone();
    int rounded = 0, skipped = 0, curves = 0;
    for (const CurveRef& t : targets_) {
      std::vector<Point3d> v;
      for (int i = 0; i < t.curve.ControlPointCount(); ++i) {
        const Point3d p = t.curve.ControlPointAt(i);
        if (v.empty() || v.back().DistanceTo(p) > ctx.Settings().absolute_tolerance) v.push_back(p);
      }
      const bool closed = v.size() > 3 && v.front().DistanceTo(v.back()) <= ctx.Settings().absolute_tolerance;
      if (closed) v.pop_back();
      const size_t n = v.size();
      if (n < 3) continue;
      const size_t ncorner = closed ? n : n - 2;
      // Tangent points per corner (or nothing when the radius does not fit).
      struct Corner { bool ok = false; Point3d t0, mid, t1; };
      std::vector<Corner> corners(ncorner);
      auto seg_len = [&](size_t i) { return v[i].DistanceTo(v[(i + 1) % n]); };
      for (size_t k = 0; k < ncorner; ++k) {
        const size_t i = closed ? k : k + 1;
        const Point3d P = v[i], Pp = v[(i + n - 1) % n], Pn = v[(i + 1) % n];
        Vector3d u = Pp - P, w = Pn - P; u.Unitize(); w.Unitize();
        const double half = std::acos(std::clamp(ON_DotProduct(u, w), -1.0, 1.0)) / 2;
        if (half <= 1e-6 || half >= ON_PI / 2 - 1e-6) continue;  // spike or straight: nothing to round
        const double d = r / std::tan(half);
        const double prev_len = seg_len((i + n - 1) % n), next_len = seg_len(i);
        const bool prev_shared = closed || i - 1 > 0;        // the other end of that segment is itself a corner
        const bool next_shared = closed || i + 1 < n - 1;
        const double prev_avail = prev_shared ? prev_len / 2 : prev_len, next_avail = next_shared ? next_len / 2 : next_len;
        if (d > prev_avail + 1e-9 || d > next_avail + 1e-9) { ++skipped; continue; }
        Vector3d bis = u + w; bis.Unitize();
        const Point3d center = P + bis * (r / std::sin(half));
        Vector3d to_corner = P - center; to_corner.Unitize();
        corners[k] = {true, P + u * d, center + to_corner * r, P + w * d};
        ++rounded;
      }
      // Assemble: lines between consecutive tangent points, arcs at the rounded corners.
      ON_PolyCurve pc;
      auto add_line = [&](Point3d a, Point3d b) { if (a.DistanceTo(b) > ctx.Settings().absolute_tolerance) pc.Append(new ON_LineCurve(a, b)); };
      auto add_corner = [&](const Corner& c) { ON_Arc arc(c.t0, c.mid, c.t1); if (arc.IsValid()) pc.Append(new ON_ArcCurve(arc)); };
      auto corner_of = [&](size_t i) -> const Corner* { if (closed) return &corners[i]; if (i == 0 || i == n - 1) return nullptr; return &corners[i - 1]; };
      auto in_point = [&](size_t i) { const Corner* c = corner_of(i); return (c && c->ok) ? c->t0 : v[i]; };
      auto out_point = [&](size_t i) { const Corner* c = corner_of(i); return (c && c->ok) ? c->t1 : v[i]; };
      const size_t segs = closed ? n : n - 1;
      for (size_t s = 0; s < segs; ++s) {
        const size_t i = s, j = (s + 1) % n;
        add_line(out_point(i), in_point(j));
        const Corner* c = corner_of(j);
        if (c && c->ok && (closed || j != n - 1)) add_corner(*c);
      }
      if (pc.Count() == 0) continue;
      kernel::NurbsCurve k;
      if (!CurveFromON(pc, k)) { ctx.Warn("Could not rebuild curve " + std::to_string(t.id)); continue; }
      ctx.Doc().Remove(t.id);
      ctx.Doc().Select(AddLike(ctx, k, t.attrs), true);
      ++curves;
    }
    ctx.Print("FilletCorners: rounded " + Plural(rounded, "corner") + " on " + Plural(curves, "curve") + (skipped ? ", skipped " + std::to_string(skipped) + " where radius " + FormatNumber(r) + " does not fit" : ""));
    Finish();
  }

 private:
  std::vector<CurveRef> targets_;
};

}  // namespace

void RegisterCurveEditCommands(CommandEngine& e) {
  Reg(e, "Intersect", OnSelection("Select objects to intersect", IntersectObjects, 1), CommandStatus::Implemented,
      "Curve/curve intersections as point objects (a single curve is self-intersected); two closed solids give the intersection volume.");
  Reg(e, "Split", Make<SplitCommand>(), CommandStatus::Implemented,
      "Splits curves with curves or points; solids are split with a plane through two points (BooleanSplit).");
  Reg(e, "Trim", Make<TrimCommand>(), CommandStatus::Implemented,
      "Trims curves with curves or points (apparent intersections in the construction plane); surface trimming is planned.");
  Reg(e, "Fillet", Make<FilletChamferCommand>(false), CommandStatus::Implemented,
      "Exact for lines; for other curves the local tangent lines at the picks are used (approximate).");
  Reg(e, "Chamfer", Make<FilletChamferCommand>(true), CommandStatus::Implemented,
      "Exact for lines; for other curves the local tangent lines at the picks are used (approximate).");
  Reg(e, "FilletCorners", Make<FilletCornersCommand>(), CommandStatus::Implemented,
      "Rounds polyline corners with a tangent arc; corners where the radius does not fit are skipped.");
}

}  // namespace dino8::app
