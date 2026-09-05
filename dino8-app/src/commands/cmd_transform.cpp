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
  void OnOption(CommandContext&, const std::string& n, const std::string&) override { if (n == "Copy") { copy_ = !copy_; options[0].value = copy_ ? "Yes" : "No"; } }
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
  void OnOption(CommandContext&, const std::string& n, const std::string&) override { if (n == "Copy") { copy_ = !copy_; options[0].value = copy_ ? "Yes" : "No"; } }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!origin_) { origin_ = p; WantPoint("Scale factor or first reference point"); return; }
    if (!ref_) { ref_ = p; WantPoint("Second reference point"); return; }
    double d0 = (*ref_ - *origin_).Length();
    if (d0 <= 0) return;
    Apply(ctx, (p - *origin_).Length() / d0, p);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double f) override { if (origin_ && !ref_) Apply(ctx, f, *origin_ + Vector3d(1, 0, 0)); }
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

class MirrorCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select objects to mirror"); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    WantPoint("Start of mirror plane");
    options = {{"Copy", "Yes", {"Yes", "No"}, false, true}, {"XAxis", "", {}, false, false}, {"YAxis", "", {}, false, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    if (n == "Copy") { copy_ = !copy_; options[0].value = copy_ ? "Yes" : "No"; }
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

}  // namespace

void RegisterTransformCommands(CommandEngine& e) {
  Reg(e, "Move", Make<MoveCommand>(false));
  Reg(e, "Copy", Make<MoveCommand>(true));
  Reg(e, "Rotate", Make<RotateCommand>());
  Reg(e, "Rotate3D", Make<RotateCommand>(), CommandStatus::Partial, "Rotates about the CPlane normal; arbitrary axis picking is planned.");
  Reg(e, "Scale", Make<ScaleCommand>(ScaleCommand::Kind::Uniform));
  Reg(e, "Scale1D", Make<ScaleCommand>(ScaleCommand::Kind::OneD));
  Reg(e, "Scale2D", Make<ScaleCommand>(ScaleCommand::Kind::TwoD));
  Reg(e, "ScaleNU", Make<ScaleCommand>(ScaleCommand::Kind::NonUniform), CommandStatus::Partial, "Scales along the CPlane X axis; per-axis factors are planned.");
  Reg(e, "Mirror", Make<MirrorCommand>());
  Reg(e, "Array", Make<ArrayCommand>());
  Reg(e, "ArrayLinear", Make<ArrayCommand>(), CommandStatus::Partial, "Uses the rectangular array with Y and Z counts of 1.");
  Reg(e, "ArrayPolar", Make<ArrayPolarCommand>());
  Reg(e, "Orient3Pt", Make<Orient3PtCommand>());
  Reg(e, "Orient", Make<Orient3PtCommand>(), CommandStatus::Partial, "Uses three reference / target points.");
  Reg(e, "ProjectToCPlane", OnSelection("Select objects to project", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ON_Plane pl = ActivePlane(ctx);
        ON_Xform xf = ON_Xform::IdentityTransformation;
        xf.PlanarProjection(pl);
        ApplyXform(ctx, ids, xf, false, "ProjectToCPlane");
      }));
  Reg(e, "SetPt", OnSelection("Select objects to set points on", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SetPt");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o) { kernel::BoundingBox bb = o->BoundingBox(); o->Transform(ON_Xform::TranslationTransformation(Vector3d(0, 0, -bb.min.z))); } }
      }), CommandStatus::Partial, "Sets Z of the selection's base to 0; X/Y/Z choice dialog is planned.");
  Reg(e, "Shear", OnSelection("Select objects to shear", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ON_Plane pl = ActivePlane(ctx);
        ApplyXform(ctx, ids, ON_Xform::ShearTransformation(pl, pl.xaxis, pl.yaxis + pl.xaxis * 0.5, pl.zaxis), false, "Shear");
      }), CommandStatus::Partial, "Shears by 0.5 along CPlane X; interactive angle is planned.");
  Reg(e, "Nudge", OnSelection("Select objects to nudge", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ApplyXform(ctx, ids, ON_Xform::TranslationTransformation(ActivePlane(ctx).xaxis * ctx.Settings().grid_spacing), false, "Nudge");
      }), CommandStatus::Partial, "Nudges one grid unit along CPlane X.");
}

}  // namespace dino8::app
