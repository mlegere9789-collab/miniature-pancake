// Transform commands: Move, Copy, Rotate, Scale, Mirror, Array...
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

void ApplyXform(CommandContext& ctx, const std::vector<ObjectId>& ids, const ON_Xform& xf, bool copy, const std::string& label) {
  ctx.Doc().BeginChange(label);
  std::vector<ObjectId> made;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (copy) {
      SceneObject dup = *o;
      dup.id = kNoObject;
      dup.selected = false;
      dup.Transform(xf);
      made.push_back(ctx.Doc().Add(std::move(dup)));
    } else {
      o->Transform(xf);
    }
  }
  if (copy) ctx.Print("Copied " + std::to_string(made.size()) + " object(s)");
}

// "Copy=Yes" / "Copy=No" set the flag; a bare "Copy" (or a click) toggles it.
bool CopyValue(const std::string& v, bool current) {
  const std::string l = ToLower(v);
  if (l == "yes" || l == "y" || l == "true" || l == "1") return true;
  if (l == "no" || l == "n" || l == "false" || l == "0") return false;
  return !current;
}

void PreviewXform(CommandContext& ctx, const std::vector<ObjectId>& ids, const ON_Xform& xf) {
  ctx.ClearPreview();
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    o->EnsureDisplay(ctx.App().curve_display_tolerance, ctx.App().surface_display_tolerance);
    const DisplayCache& d = o->Display();
    int budget = 4000;
    for (size_t i = 0; i + 5 < d.lines.size() && budget-- > 0; i += 6) {
      Point3d a(d.lines[i], d.lines[i + 1], d.lines[i + 2]), b(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]);
      ctx.AddPreviewLine(xf * a, xf * b);
    }
    if (o->kind == ObjectKind::Point) ctx.AddPreviewPoint(xf * o->point);
    if (d.lines.empty() && !d.triangles.empty()) {
      for (size_t i = 0; i + 17 < d.triangles.size() && budget-- > 0; i += 18) {
        Point3d a(d.triangles[i], d.triangles[i + 1], d.triangles[i + 2]), b(d.triangles[i + 6], d.triangles[i + 7], d.triangles[i + 8]), c(d.triangles[i + 12], d.triangles[i + 13], d.triangles[i + 14]);
        ctx.AddPreviewPolyline({xf * a, xf * b, xf * c}, true);
      }
    }
  }
}

// Select objects, base point, then target point (Move / Copy).
class MoveCommand : public Command {
 public:
  explicit MoveCommand(bool copy) : copy_(copy) {}
  void Begin(CommandContext&) override { WantObjects(copy_ ? "Select objects to copy" : "Select objects to move"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Point to move from");
    options = {{"Vertical", "No", {"Yes", "No"}, false, true}};
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!base_) { base_ = p; WantPoint(copy_ ? "Point to copy to. Press Enter when done" : "Point to move to"); return; }
    ON_Xform xf = ON_Xform::TranslationTransformation(p - *base_);
    ApplyXform(ctx, ids_, xf, copy_, copy_ ? "Copy" : "Move");
    if (copy_) { ctx.ClearPreview(); return; }
    ctx.ClearPreview();
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { ctx.ClearPreview(); Finish(); }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!base_) return;
    PreviewXform(ctx, ids_, ON_Xform::TranslationTransformation(h - *base_));
    ctx.AddPreviewLine(*base_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  bool copy_;
  std::vector<ObjectId> ids_;
  std::optional<Point3d> base_;
};

class RotateCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to rotate"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Center of rotation");
    options = {{"Copy", "No", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Copy") { copy_ = CopyValue(v, copy_); options[0].value = copy_ ? "Yes" : "No"; } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!center_) { center_ = p; WantPoint("Angle or first reference point"); return; }
    if (!ref_) { ref_ = p; WantPoint("Second reference point"); return; }
    double a = AngleBetween(ctx, *ref_, p);
    Apply(ctx, a);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double deg) override { if (center_ && !ref_) Apply(ctx, deg * ON_PI / 180.0); }
  double AngleBetween(CommandContext& ctx, Point3d a, Point3d b) {
    ON_Plane pl = ActivePlane(ctx);
    Vector3d va = a - *center_, vb = b - *center_;
    double a0 = std::atan2(ON_DotProduct(va, pl.yaxis), ON_DotProduct(va, pl.xaxis));
    double a1 = std::atan2(ON_DotProduct(vb, pl.yaxis), ON_DotProduct(vb, pl.xaxis));
    return a1 - a0;
  }
  void Apply(CommandContext& ctx, double radians) {
    ON_Xform xf;
    xf.Rotation(radians, ActiveNormal(ctx), *center_);
    ApplyXform(ctx, ids_, xf, copy_, "Rotate");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!center_) return;
    if (!ref_) { ctx.ClearPreview(); ctx.AddPreviewLine(*center_, h); return; }
    ON_Xform xf;
    xf.Rotation(AngleBetween(ctx, *ref_, h), ActiveNormal(ctx), *center_);
    PreviewXform(ctx, ids_, xf);
    ctx.AddPreviewLine(*center_, *ref_);
    ctx.AddPreviewLine(*center_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> center_, ref_;
  bool copy_ = false;
};

// Rotate3D: rotates about an arbitrary axis (two picked points), unlike
// Rotate which always spins about the CPlane normal through a single center.
class Rotate3DCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to rotate"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Start of rotation axis");
    options = {{"Copy", "No", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Copy") { copy_ = CopyValue(v, copy_); options[0].value = copy_ ? "Yes" : "No"; } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!axis_start_) { axis_start_ = p; WantPoint("End of rotation axis"); return; }
    if (!axis_end_) {
      axis_end_ = p;
      axis_dir_ = *axis_end_ - *axis_start_;
      if (axis_dir_.Length() <= 1e-9) { ctx.Warn("Rotate3D: axis start and end coincide"); Finish(); return; }
      axis_dir_.Unitize();
      basis_ = ON_Plane(*axis_start_, axis_dir_);
      WantPoint("Angle or first reference point");
      return;
    }
    if (!ref_) { ref_ = p; WantPoint("Second reference point"); return; }
    Apply(ctx, AngleBetween(*ref_, p));
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double deg) override { if (axis_end_ && !ref_) Apply(ctx, deg * ON_PI / 180.0); }
  double AngleBetween(Point3d a, Point3d b) {
    Vector3d va = a - *axis_start_, vb = b - *axis_start_;
    double a0 = std::atan2(ON_DotProduct(va, basis_.yaxis), ON_DotProduct(va, basis_.xaxis));
    double a1 = std::atan2(ON_DotProduct(vb, basis_.yaxis), ON_DotProduct(vb, basis_.xaxis));
    return a1 - a0;
  }
  void Apply(CommandContext& ctx, double radians) {
    ON_Xform xf;
    xf.Rotation(radians, axis_dir_, *axis_start_);
    ApplyXform(ctx, ids_, xf, copy_, "Rotate3D");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!axis_start_) return;
    ctx.ClearPreview();
    if (!axis_end_) { ctx.AddPreviewLine(*axis_start_, h); return; }
    if (!ref_) { ctx.AddPreviewLine(*axis_start_, *axis_end_); ctx.AddPreviewLine(*axis_start_, h); return; }
    ON_Xform xf; xf.Rotation(AngleBetween(*ref_, h), axis_dir_, *axis_start_);
    PreviewXform(ctx, ids_, xf);
    ctx.AddPreviewLine(*axis_start_, *axis_end_);
    ctx.AddPreviewLine(*axis_start_, *ref_);
    ctx.AddPreviewLine(*axis_start_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> axis_start_, axis_end_, ref_;
  Vector3d axis_dir_;
  ON_Plane basis_;
  bool copy_ = false;
};

class ScaleCommand : public Command {
 public:
  enum class Kind { Uniform, OneD, TwoD, NonUniform };
  explicit ScaleCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override { WantObjects("Select objects to scale"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Origin point");
    options = {{"Copy", "No", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Copy") { copy_ = CopyValue(v, copy_); options[0].value = copy_ ? "Yes" : "No"; } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!origin_) { origin_ = p; WantPoint("Scale factor or first reference point"); return; }
    if (!ref_) { ref_ = p; WantPoint("Second reference point"); return; }
    double d0 = (*ref_ - *origin_).Length();
    if (d0 <= 0) return;
    Apply(ctx, (p - *origin_).Length() / d0, p);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  // Scale1D's default axis (no reference point picked) must be the active
  // CPlane's X axis, not world X: in a Front/Right viewport those differ, and
  // using world X silently no-ops the scale whenever the point lies on the
  // CPlane's own X axis (as it does after Move/typed input in that view).
  void OnNumber(CommandContext& ctx, double f) override { if (origin_ && !ref_) Apply(ctx, f, *origin_ + ActivePlane(ctx).xaxis); }
  ON_Xform Xform(CommandContext& ctx, double f, Point3d dir_point) {
    ON_Plane pl = ActivePlane(ctx);
    ON_Xform xf = ON_Xform::IdentityTransformation;
    switch (kind_) {
      case Kind::Uniform: xf = ON_Xform::ScaleTransformation(*origin_, f); break;
      case Kind::OneD: {
        Vector3d d = ref_ ? (*ref_ - *origin_) : (dir_point - *origin_);
        if (d.Length() <= 0) d = pl.xaxis;
        d.Unitize();
        ON_Plane sp(*origin_, d);
        xf = ON_Xform::ScaleTransformation(sp, 1.0, 1.0, f);
        break;
      }
      case Kind::TwoD: { ON_Plane sp = pl; sp.SetOrigin(*origin_); xf = ON_Xform::ScaleTransformation(sp, f, f, 1.0); break; }
      case Kind::NonUniform: { ON_Plane sp = pl; sp.SetOrigin(*origin_); xf = ON_Xform::ScaleTransformation(sp, f, 1.0, 1.0); break; }
    }
    return xf;
  }
  void Apply(CommandContext& ctx, double f, Point3d dir_point) {
    if (f == 0) { ctx.Warn("Scale factor must be non-zero"); return; }
    ApplyXform(ctx, ids_, Xform(ctx, f, dir_point), copy_, "Scale");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!origin_) return;
    if (!ref_) { ctx.ClearPreview(); ctx.AddPreviewLine(*origin_, h); return; }
    double d0 = (*ref_ - *origin_).Length();
    if (d0 <= 0) return;
    PreviewXform(ctx, ids_, Xform(ctx, (h - *origin_).Length() / d0, h));
    ctx.AddPreviewLine(*origin_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  std::vector<ObjectId> ids_;
  std::optional<Point3d> origin_, ref_;
  bool copy_ = false;
};

// ScaleNU: non-uniform scale with three independently-typed factors along
// the CPlane X/Y axes and its normal (unlike ScaleCommand's NonUniform kind,
// which only ever scales along CPlane X).
class ScaleNUCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to scale"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Origin point");
    options = {{"Copy", "No", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Copy") { copy_ = CopyValue(v, copy_); options[0].value = copy_ ? "Yes" : "No"; } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!origin_) { origin_ = p; WantNumber("X scale factor", 1.0); }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!origin_) return;
    if (stage_ == 0) { fx_ = v; ++stage_; WantNumber("Y scale factor", 1.0); return; }
    if (stage_ == 1) { fy_ = v; ++stage_; WantNumber("Z scale factor", 1.0); return; }
    fz_ = v;
    Apply(ctx);
  }
  void Apply(CommandContext& ctx) {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(*origin_);
    ON_Xform xf = ON_Xform::ScaleTransformation(pl, fx_, fy_, fz_);
    ApplyXform(ctx, ids_, xf, copy_, "ScaleNU");
    ctx.ClearPreview();
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> origin_;
  int stage_ = 0;
  double fx_ = 1, fy_ = 1, fz_ = 1;
  bool copy_ = false;
};

class MirrorCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to mirror"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Start of mirror plane");
    options = {{"Copy", "Yes", {"Yes", "No"}, false, true}, {"XAxis", "", {}, false, false}, {"YAxis", "", {}, false, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override {
    if (n == "Copy") { copy_ = CopyValue(v, copy_); options[0].value = copy_ ? "Yes" : "No"; }
    if (n == "XAxis") { ON_Plane pl = ActivePlane(ctx); Apply(ctx, pl.origin, pl.origin + pl.xaxis); }
    if (n == "YAxis") { ON_Plane pl = ActivePlane(ctx); Apply(ctx, pl.origin, pl.origin + pl.yaxis); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of mirror plane"); return; }
    Apply(ctx, *a_, p);
  }
  ON_Xform Xform(CommandContext& ctx, Point3d a, Point3d b) {
    Vector3d dir = b - a;
    Vector3d n = ON_CrossProduct(dir, ActiveNormal(ctx));
    if (n.Length() <= 0) n = ActivePlane(ctx).yaxis;
    n.Unitize();
    return ON_Xform::MirrorTransformation(ON_PlaneEquation(n.x, n.y, n.z, -ON_DotProduct(n, a)));
  }
  void Apply(CommandContext& ctx, Point3d a, Point3d b) {
    ApplyXform(ctx, ids_, Xform(ctx, a, b), copy_, "Mirror");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!a_) return;
    PreviewXform(ctx, ids_, Xform(ctx, *a_, h));
    ctx.AddPreviewLine(*a_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
  bool copy_ = true;
};

// Rectangular array: counts in X, Y, Z then spacing picked as a point.
class ArrayCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to array"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantNumber("Number in X direction", 2); }
  void OnNumber(CommandContext& ctx, double v) override {
    counts_[stage_] = std::max(1, static_cast<int>(v));
    ++stage_;
    if (stage_ == 1) WantNumber("Number in Y direction", 2);
    else if (stage_ == 2) WantNumber("Number in Z direction", 1);
    else {
      kernel::BoundingBox bb;
      ctx.Doc().BoundingBoxOf(ids_, bb);
      base_ = bb.min;
      WantPoint("Corner of unit cell (or type spacing)");
    }
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* e; double v = std::strtod(t.c_str(), &e);
    if (e && !*e) { if (stage_ < 3) OnNumber(ctx, v); else Apply(ctx, Vector3d(v, v, v)); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override { if (stage_ >= 3) Apply(ctx, p - base_); }
  void Apply(CommandContext& ctx, Vector3d spacing) {
    ctx.Doc().BeginChange("Array");
    int made = 0;
    for (int i = 0; i < counts_[0]; ++i)
      for (int j = 0; j < counts_[1]; ++j)
        for (int k = 0; k < counts_[2]; ++k) {
          if (i == 0 && j == 0 && k == 0) continue;
          ON_Xform xf = ON_Xform::TranslationTransformation(Vector3d(spacing.x * i, spacing.y * j, spacing.z * k));
          for (ObjectId id : ids_) {
            SceneObject* o = ctx.Doc().Find(id);
            if (!o) continue;
            SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf);
            ctx.Doc().Add(std::move(dup));
            ++made;
          }
        }
    ctx.Print("Array created " + std::to_string(made) + " object(s)");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (stage_ < 3) return;
    ctx.ClearPreview();
    Vector3d s = h - base_;
    for (int i = 0; i < counts_[0]; ++i) for (int j = 0; j < counts_[1]; ++j) for (int k = 0; k < counts_[2]; ++k)
      ctx.AddPreviewPoint(base_ + Vector3d(s.x * i, s.y * j, s.z * k));
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  int counts_[3] = {2, 2, 1};
  int stage_ = 0;
  Point3d base_;
};

// Rhino's real ArrayLinear: a count plus a single direction/spacing vector
// (given as two points), unlike the rectangular Array's per-axis counts.
class ArrayLinearCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to array"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantNumber("Number of items", 3); }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!count_) { count_ = std::max(2, static_cast<int>(v)); WantPoint("Direction start point (spacing between items)"); }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!start_) { start_ = p; WantPoint("Direction end point"); return; }
    Apply(ctx, p - *start_);
  }
  void Apply(CommandContext& ctx, Vector3d spacing) {
    ctx.Doc().BeginChange("ArrayLinear");
    int made = 0;
    for (int i = 1; i < *count_; ++i) {
      ON_Xform xf = ON_Xform::TranslationTransformation(spacing * i);
      for (ObjectId id : ids_) {
        SceneObject* o = ctx.Doc().Find(id);
        if (!o) continue;
        SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf);
        ctx.Doc().Add(std::move(dup));
        ++made;
      }
    }
    ctx.Print("ArrayLinear created " + std::to_string(made) + " object(s)");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!count_ || !start_) return;
    ctx.ClearPreview();
    Vector3d spacing = h - *start_;
    for (int i = 1; i < *count_; ++i) PreviewXform(ctx, ids_, ON_Xform::TranslationTransformation(spacing * i));
    ctx.AddPreviewLine(*start_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<int> count_;
  std::optional<Point3d> start_;
};

class ArrayPolarCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to array"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Center of polar array"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { center_ = p; WantNumber("Number of items", 6); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!count_) { count_ = std::max(2, static_cast<int>(v)); WantNumber("Angle to fill", 360); return; }
    double fill = v * ON_PI / 180.0;
    ctx.Doc().BeginChange("ArrayPolar");
    const int n = *count_;
    const bool full = std::fabs(v - 360.0) < 1e-9;
    for (int i = 1; i < n; ++i) {
      ON_Xform xf;
      xf.Rotation(fill * i / (full ? n : n - 1), ActiveNormal(ctx), center_);
      for (ObjectId id : ids_) { SceneObject* o = ctx.Doc().Find(id); if (!o) continue; SceneObject dup = *o; dup.id = kNoObject; dup.selected = false; dup.Transform(xf); ctx.Doc().Add(std::move(dup)); }
    }
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  std::vector<ObjectId> ids_;
  Point3d center_;
  std::optional<int> count_;
};

class Orient3PtCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to orient"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Reference point 1"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    const char* prompts[] = {"Reference point 1", "Reference point 2", "Reference point 3", "Target point 1", "Target point 2", "Target point 3"};
    if (pts_.size() < 6) { WantPoint(prompts[pts_.size()]); return; }
    ON_Plane p0(pts_[0], pts_[1], pts_[2]), p1(pts_[3], pts_[4], pts_[5]);
    if (!p0.IsValid() || !p1.IsValid()) { ctx.Warn("Points are collinear"); Finish(); return; }
    ON_Xform xf;
    xf.Rotation(p0, p1);
    ApplyXform(ctx, ids_, xf, false, "Orient3Pt");
    Finish();
  }
  std::vector<ObjectId> ids_;
  std::vector<Point3d> pts_;
};

// Rhino's real Orient: two reference points (base + direction/scale) mapped
// to two target points, unlike Orient3Pt's plane-to-plane (3+3 point) form.
class OrientCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to orient"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantPoint("Reference point 1"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    const char* prompts[] = {"Reference point 1", "Reference point 2", "Target point 1", "Target point 2"};
    if (pts_.size() < 4) { WantPoint(prompts[pts_.size()]); return; }
    Point3d r1 = pts_[0], r2 = pts_[1], t1 = pts_[2], t2 = pts_[3];
    Vector3d vr = r2 - r1, vt = t2 - t1;
    double lr = vr.Length();
    if (lr <= 1e-9) { ctx.Warn("Orient: reference points coincide"); Finish(); return; }
    double lt = vt.Length();
    double scale = lt > 1e-9 ? lt / lr : 1.0;
    ON_Plane pl = ActivePlane(ctx);
    double a0 = std::atan2(ON_DotProduct(vr, pl.yaxis), ON_DotProduct(vr, pl.xaxis));
    double a1 = lt > 1e-9 ? std::atan2(ON_DotProduct(vt, pl.yaxis), ON_DotProduct(vt, pl.xaxis)) : a0;
    ON_Xform r; r.Rotation(a1 - a0, pl.zaxis, r1);
    ON_Xform s = ON_Xform::ScaleTransformation(r1, scale);
    ON_Xform t = ON_Xform::TranslationTransformation(t1 - r1);
    ApplyXform(ctx, ids_, t * r * s, false, "Orient");
    Finish();
  }
  std::vector<ObjectId> ids_;
  std::vector<Point3d> pts_;
};

// SetPt: real Rhino asks which world coordinates to set and to what value in
// a dialog; here the same choice is made with command-line options (SetX/
// SetY/SetZ pick the axes, X/Y/Z their target values), applied immediately
// on selection. Defaults (SetZ=Yes, target 0) match the previous fixed
// behavior of dropping the selection's base to the CPlane.
class SetPtCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {
        {"SetX", "No", {"Yes", "No"}, false, true},
        {"SetY", "No", {"Yes", "No"}, false, true},
        {"SetZ", "Yes", {"Yes", "No"}, false, true},
        {"X", "0", {}, true, false},
        {"Y", "0", {}, true, false},
        {"Z", "0", {}, true, false},
    };
    WantObjects("Select objects to set points (SetX/SetY/SetZ pick axes, X/Y/Z their values)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    auto set = [&](const char* name, const std::string& val) { for (OptionSpec& o : options) if (o.name == name) o.value = val; };
    if (n == "SetX") { set_x_ = ToLower(v) == "yes"; set("SetX", set_x_ ? "Yes" : "No"); return; }
    if (n == "SetY") { set_y_ = ToLower(v) == "yes"; set("SetY", set_y_ ? "Yes" : "No"); return; }
    if (n == "SetZ") { set_z_ = ToLower(v) == "yes"; set("SetZ", set_z_ ? "Yes" : "No"); return; }
    char* e = nullptr;
    double d = std::strtod(v.c_str(), &e);
    if (!e || *e) return;
    if (n == "X") { x_ = d; set("X", FormatNumber(d)); }
    else if (n == "Y") { y_ = d; set("Y", FormatNumber(d)); }
    else if (n == "Z") { z_ = d; set("Z", FormatNumber(d)); }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!set_x_ && !set_y_ && !set_z_) { ctx.Warn("SetPt: no coordinate selected (SetX/SetY/SetZ)"); Finish(); return; }
    ctx.Doc().BeginChange("SetPt");
    int done = 0;
    for (ObjectId id : ids) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      kernel::BoundingBox bb = o->BoundingBox();
      Vector3d d(0, 0, 0);
      if (set_x_) d.x = x_ - bb.min.x;
      if (set_y_) d.y = y_ - bb.min.y;
      if (set_z_) d.z = z_ - bb.min.z;
      o->Transform(ON_Xform::TranslationTransformation(d));
      ++done;
    }
    ctx.Print("SetPt: set " + std::to_string(done) + " object(s)");
    Finish();
  }
  bool set_x_ = false, set_y_ = false, set_z_ = true;
  double x_ = 0, y_ = 0, z_ = 0;
};

// Nudge: moves the selection one grid unit along a chosen CPlane direction
// (defaults to +X, matching the previous fixed behavior).
class NudgeCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Direction", "+X", {"+X", "-X", "+Y", "-Y", "+Z", "-Z"}, false, false}};
    WantObjects("Select objects to nudge");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n != "Direction") return;
    dir_ = v;
    for (OptionSpec& o : options) if (o.name == "Direction") o.value = v;
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ON_Plane pl = ActivePlane(ctx);
    Vector3d step = pl.xaxis;
    if (dir_ == "-X") step = -pl.xaxis;
    else if (dir_ == "+Y") step = pl.yaxis;
    else if (dir_ == "-Y") step = -pl.yaxis;
    else if (dir_ == "+Z") step = pl.zaxis;
    else if (dir_ == "-Z") step = -pl.zaxis;
    ApplyXform(ctx, ids, ON_Xform::TranslationTransformation(step * ctx.Settings().grid_spacing), false, "Nudge");
    Finish();
  }
  std::string dir_ = "+X";
};

}  // namespace

void RegisterTransformCommands(CommandEngine& e) {
  Reg(e, "Move", Make<MoveCommand>(false));
  Reg(e, "Copy", Make<MoveCommand>(true));
  Reg(e, "Rotate", Make<RotateCommand>());
  Reg(e, "Rotate3D", Make<Rotate3DCommand>());
  Reg(e, "Scale", Make<ScaleCommand>(ScaleCommand::Kind::Uniform));
  Reg(e, "Scale1D", Make<ScaleCommand>(ScaleCommand::Kind::OneD));
  Reg(e, "Scale2D", Make<ScaleCommand>(ScaleCommand::Kind::TwoD));
  Reg(e, "ScaleNU", Make<ScaleNUCommand>());
  Reg(e, "Mirror", Make<MirrorCommand>());
  Reg(e, "Array", Make<ArrayCommand>());
  Reg(e, "ArrayLinear", Make<ArrayLinearCommand>());
  Reg(e, "ArrayPolar", Make<ArrayPolarCommand>());
  Reg(e, "Orient3Pt", Make<Orient3PtCommand>());
  Reg(e, "Orient", Make<OrientCommand>());
  Reg(e, "ProjectToCPlane", OnSelection("Select objects to project", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ON_Plane pl = ActivePlane(ctx);
        ON_Xform xf = ON_Xform::IdentityTransformation;
        xf.PlanarProjection(pl);
        ApplyXform(ctx, ids, xf, false, "ProjectToCPlane");
      }));
  Reg(e, "SetPt", Make<SetPtCommand>());
  // Shear: superseded, dead code (RegisterMeshToolsCommands registers the
  // real interactive Shear afterwards; see Application::RegisterCommands).
  Reg(e, "Nudge", Make<NudgeCommand>());
}

}  // namespace dino8::app
