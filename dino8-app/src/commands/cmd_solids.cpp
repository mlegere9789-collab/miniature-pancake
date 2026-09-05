// Surface, solid, mesh and SubD creation commands.
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

kernel::Brep WrapBrep(ON_Brep* b) {
  kernel::Brep k;
  if (b) {
    k.raw() = *b;
    delete b;
  }
  return k;
}

void AddBrep(CommandContext& ctx, ON_Brep* b, const char* label) {
  if (!b) { ctx.Warn(std::string(label) + " failed"); return; }
  AddObject(ctx, SceneObject::MakeBrep(WrapBrep(b)), label);
}

kernel::Mesh CubeMesh(Point3d a, Point3d b) {
  kernel::Mesh m;
  ON_Mesh& r = m.raw();
  const double x[2] = {std::min(a.x, b.x), std::max(a.x, b.x)}, y[2] = {std::min(a.y, b.y), std::max(a.y, b.y)}, z[2] = {std::min(a.z, b.z), std::max(a.z, b.z)};
  for (int k = 0; k < 2; ++k) for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) r.SetVertex(k * 4 + j * 2 + i, ON_3dPoint(x[i], y[j], z[k]));
  const int f[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
  for (int i = 0; i < 6; ++i) r.SetQuad(i, f[i][0], f[i][1], f[i][2], f[i][3]);
  r.ComputeFaceNormals();
  r.ComputeVertexNormals();
  return m;
}

// Box: two corners on the CPlane, then height.
class BoxCommand : public Command {
 public:
  enum class Out { Brep, Mesh, SubD };
  explicit BoxCommand(Out out) : out_(out) {}
  void Begin(CommandContext&) override { WantPoint("First corner of base"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() == 1) WantPoint("Other corner of base");
    else if (pts_.size() == 2) WantPoint("Height. Press Enter for a cube");
    else Build(ctx, HeightFrom(ctx, p));
  }
  void OnNumber(CommandContext& ctx, double v) override { if (pts_.size() == 2) Build(ctx, v); }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override {
    if (pts_.size() == 2) Build(ctx, std::min((pts_[1] - pts_[0]).Length(), std::fabs(pts_[1].x - pts_[0].x) > 0 ? std::fabs(pts_[1].x - pts_[0].x) : (pts_[1] - pts_[0]).Length()));
  }
  double HeightFrom(CommandContext& ctx, Point3d p) { return ON_DotProduct(p - pts_[1], ActiveNormal(ctx)); }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    if (pts_.size() == 1) PreviewBase(ctx, pts_[0], h, 0);
    else if (pts_.size() == 2) PreviewBase(ctx, pts_[0], pts_[1], HeightFrom(ctx, h));
  }
  void PreviewBase(CommandContext& ctx, Point3d a, Point3d b, double h) {
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(a, &u0, &v0);
    pl.ClosestPointTo(b, &u1, &v1);
    std::vector<Point3d> base = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)};
    ctx.AddPreviewPolyline(base, true);
    if (h != 0) {
      std::vector<Point3d> top;
      for (const Point3d& p : base) { top.push_back(p + pl.zaxis * h); ctx.AddPreviewLine(p, p + pl.zaxis * h); }
      ctx.AddPreviewPolyline(top, true);
    }
  }
  void Build(CommandContext& ctx, double h) {
    ctx.ClearPreview();
    if (h == 0) { ctx.Warn("Height must be non-zero"); Finish(); return; }
    ON_Plane pl = ActivePlane(ctx);
    double u0, v0, u1, v1;
    pl.ClosestPointTo(pts_[0], &u0, &v0);
    pl.ClosestPointTo(pts_[1], &u1, &v1);
    if (u0 > u1) std::swap(u0, u1);
    if (v0 > v1) std::swap(v0, v1);
    if (h < 0) { pl.SetOrigin(pl.origin + pl.zaxis * h); h = -h; }
    if (out_ == Out::Brep && pl.zaxis.IsParallelTo(ON_zaxis) != 0 && std::fabs(pl.origin.z) >= 0) {
      Point3d c0 = pl.PointAt(u0, v0), c1 = pl.PointAt(u1, v1) + pl.zaxis * h;
      ON_3dPoint corners[8] = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1),
                               pl.PointAt(u0, v0) + pl.zaxis * h, pl.PointAt(u1, v0) + pl.zaxis * h, pl.PointAt(u1, v1) + pl.zaxis * h, pl.PointAt(u0, v1) + pl.zaxis * h};
      (void)c0; (void)c1;
      AddBrep(ctx, ON_BrepBox(corners), "Box");
    } else if (out_ == Out::Brep) {
      ON_3dPoint corners[8] = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1),
                               pl.PointAt(u0, v0) + pl.zaxis * h, pl.PointAt(u1, v0) + pl.zaxis * h, pl.PointAt(u1, v1) + pl.zaxis * h, pl.PointAt(u0, v1) + pl.zaxis * h};
      AddBrep(ctx, ON_BrepBox(corners), "Box");
    } else {
      kernel::Mesh m = CubeMesh(pl.PointAt(u0, v0), pl.PointAt(u1, v1) + pl.zaxis * h);
      if (out_ == Out::Mesh) AddObject(ctx, SceneObject::MakeMesh(m), "MeshBox");
      else AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(m)), "SubDBox");
    }
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Out out_;
  std::vector<Point3d> pts_;
};

// Centre + radius commands (sphere family).
CommandFactory SphereLike(const char* label, std::function<void(CommandContext&, Point3d, double)> build) {
  return [=]() -> std::unique_ptr<Command> {
    auto c = std::make_unique<PointThenDistanceCommand>("Center of sphere", "Radius",
                                                        [=](CommandContext& ctx, Point3d c, double r, Point3d) { if (r > 0) build(ctx, c, r); else ctx.Warn(std::string(label) + ": radius must be positive"); }, 10.0);
    c->preview_ = [](CommandContext& ctx, Point3d c, double r, Point3d) {
      for (int axis = 0; axis < 3; ++axis) {
        ON_Plane pl(c, axis == 0 ? ON_zaxis : axis == 1 ? ON_xaxis : ON_yaxis);
        ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, r), 48));
      }
    };
    return c;
  };
}

// Base circle then height (cylinder / cone / tube...).
class CylinderCommand : public Command {
 public:
  enum class Kind { Cylinder, Cone, TCone, Tube, Pyramid, MeshCylinder, MeshCone, SubDCylinder, SubDCone };
  explicit CylinderCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override { WantPoint("Base of " + Name()); }
  std::string Name() const {
    switch (kind_) { case Kind::Cylinder: return "cylinder"; case Kind::Cone: return "cone"; case Kind::TCone: return "truncated cone"; case Kind::Tube: return "tube"; case Kind::Pyramid: return "pyramid"; case Kind::MeshCylinder: return "mesh cylinder"; case Kind::MeshCone: return "mesh cone"; case Kind::SubDCylinder: return "SubD cylinder"; default: return "SubD cone"; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (stage_ == 0) { base_ = p; stage_ = 1; WantPoint("Radius"); }
    else if (stage_ == 1) { OnNumber(ctx, (p - base_).Length()); }
    else if (stage_ == 2 && kind_ == Kind::Tube) { OnNumber(ctx, (p - base_).Length()); }
    else { OnNumber(ctx, ON_DotProduct(p - base_, ActiveNormal(ctx))); }
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (stage_ == 1) { radius_ = std::fabs(v); if (radius_ <= 0) return; stage_ = 2; WantPoint(kind_ == Kind::Tube ? "Second radius" : "End of " + Name()); }
    else if (stage_ == 2 && kind_ == Kind::Tube) { radius2_ = std::fabs(v); stage_ = 3; WantPoint("End of tube"); }
    else if (stage_ == 2 && kind_ == Kind::TCone) { height_ = v; stage_ = 3; WantPoint("Radius at end"); }
    else if (stage_ == 3 && kind_ == Kind::TCone) { radius2_ = std::fabs(v); Build(ctx); }
    else { height_ = v; Build(ctx); }
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(base_);
    if (stage_ == 1) { ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, (h - base_).Length()))); ctx.AddPreviewLine(base_, h); }
    else if (stage_ >= 2) {
      double hh = ON_DotProduct(h - base_, pl.zaxis);
      ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, radius_)));
      ON_Plane top = pl; top.SetOrigin(base_ + pl.zaxis * hh);
      double rt = (kind_ == Kind::Cone || kind_ == Kind::MeshCone || kind_ == Kind::SubDCone || kind_ == Kind::Pyramid) ? 0.0 : radius_;
      if (rt > 0) ctx.AddPreviewPolyline(CirclePoints(ON_Circle(top, rt)));
      for (int i = 0; i < 4; ++i) { double a = i * ON_PI / 2; ctx.AddPreviewLine(pl.PointAt(radius_ * std::cos(a), radius_ * std::sin(a)), top.PointAt(rt * std::cos(a), rt * std::sin(a))); }
    }
  }
  void Build(CommandContext& ctx) {
    ctx.ClearPreview();
    if (height_ == 0) { ctx.Warn("Height must be non-zero"); Finish(); return; }
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(base_);
    Vector3d axis = pl.zaxis;
    double h = height_;
    if (h < 0) { pl = ON_Plane(base_, -pl.zaxis); axis = pl.zaxis; h = -h; }
    switch (kind_) {
      case Kind::Cylinder: {
        ON_Cylinder cyl(ON_Circle(pl, radius_), h);
        AddBrep(ctx, ON_BrepCylinder(cyl, true, true), "Cylinder");
        break;
      }
      case Kind::Cone: {
        ON_Cone cone;
        cone.Create(pl, h, radius_);
        AddBrep(ctx, ON_BrepCone(cone, true), "Cone");
        break;
      }
      case Kind::TCone: {
        // Revolve a trapezoid profile.
        ON_Plane axis_plane(base_, pl.xaxis, pl.zaxis);
        ON_Polyline profile;
        profile.Append(base_ + pl.xaxis * radius_);
        profile.Append(base_ + pl.xaxis * radius2_ + pl.zaxis * h);
        ON_PolylineCurve* pc = new ON_PolylineCurve(profile);
        ON_RevSurface* rs = ON_RevSurface::New();
        rs->m_curve = pc;
        rs->m_axis = ON_Line(base_, base_ + pl.zaxis);
        rs->m_angle = ON_Interval(0, 2 * ON_PI);
        rs->m_t = pc->Domain();
        AddBrep(ctx, ON_BrepRevSurface(rs, true, true), "TCone");
        break;
      }
      case Kind::Tube: {
        double r_out = std::max(radius_, radius2_), r_in = std::min(radius_, radius2_);
        if (r_in <= 0 || r_in >= r_out) { ctx.Warn("Tube needs two different positive radii"); break; }
        ON_Polyline profile;
        profile.Append(base_ + pl.xaxis * r_in);
        profile.Append(base_ + pl.xaxis * r_out);
        profile.Append(base_ + pl.xaxis * r_out + pl.zaxis * h);
        profile.Append(base_ + pl.xaxis * r_in + pl.zaxis * h);
        profile.Append(base_ + pl.xaxis * r_in);
        ON_PolylineCurve* pc = new ON_PolylineCurve(profile);
        ON_RevSurface* rs = ON_RevSurface::New();
        rs->m_curve = pc;
        rs->m_axis = ON_Line(base_, base_ + pl.zaxis);
        rs->m_angle = ON_Interval(0, 2 * ON_PI);
        rs->m_t = pc->Domain();
        AddBrep(ctx, ON_BrepRevSurface(rs, false, false), "Tube");
        break;
      }
      case Kind::Pyramid:
        AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Cone(base_, axis, radius_, h, 4, 1)), "Pyramid");
        break;
      case Kind::MeshCylinder:
        AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Cylinder(base_, axis, radius_, h)), "MeshCylinder");
        break;
      case Kind::MeshCone:
        AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Cone(base_, axis, radius_, h)), "MeshCone");
        break;
      case Kind::SubDCylinder:
        AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Mesh::Cylinder(base_, axis, radius_, h, 16, 4))), "SubDCylinder");
        break;
      case Kind::SubDCone:
        AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Mesh::Cone(base_, axis, radius_, h, 16, 4))), "SubDCone");
        break;
    }
    Finish();
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  int stage_ = 0;
  Point3d base_;
  double radius_ = 0, radius2_ = 0, height_ = 0;
};

class TorusCommand : public Command {
 public:
  enum class Kind { Brep, Mesh, SubD };
  explicit TorusCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override { WantPoint("Center of torus"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (stage_ == 0) { center_ = p; stage_ = 1; WantPoint("Major radius"); }
    else OnNumber(ctx, stage_ == 1 ? (p - center_).Length() : std::fabs((p - center_).Length() - major_));
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double v) override {
    if (stage_ == 1) { major_ = std::fabs(v); stage_ = 2; WantPoint("Minor radius"); return; }
    double minor = std::fabs(v);
    ctx.ClearPreview();
    if (minor <= 0 || major_ <= 0) { ctx.Warn("Radii must be positive"); Finish(); return; }
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(center_);
    if (kind_ == Kind::Brep) {
      ON_Torus t(pl, major_, minor);
      AddBrep(ctx, ON_BrepTorus(t), "Torus");
    } else if (kind_ == Kind::Mesh) {
      AddObject(ctx, SceneObject::MakeMesh(kernel::Mesh::Torus(center_, pl.zaxis, major_, minor)), "MeshTorus");
    } else {
      AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Mesh::Torus(center_, pl.zaxis, major_, minor, 16, 8))), "SubDTorus");
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(center_);
    if (stage_ == 1) ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, (h - center_).Length())));
    else if (stage_ == 2) {
      double minor = std::fabs((h - center_).Length() - major_);
      ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, major_ + minor)));
      ctx.AddPreviewPolyline(CirclePoints(ON_Circle(pl, std::max(0.0, major_ - minor))));
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  int stage_ = 0;
  Point3d center_;
  double major_ = 0;
};

// Extrude selected curves along the CPlane normal by a typed or picked distance.
class ExtrudeCommand : public Command {
 public:
  enum class Kind { Curve, Surface, ToPoint };
  explicit ExtrudeCommand(Kind k) : kind_(k) {}
  void Begin(CommandContext&) override {
    WantObjects(kind_ == Kind::Surface ? "Select surfaces to extrude" : "Select curves to extrude");
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      if (kind_ == Kind::Surface ? (o->kind == ObjectKind::Surface || o->kind == ObjectKind::Brep) : o->kind == ObjectKind::Curve) ids_.push_back(id);
    }
    if (ids_.empty()) { ctx.Warn("Nothing suitable selected"); Finish(); return; }
    ctx.Doc().BoundingBoxOf(ids_, bbox_);
    if (kind_ == Kind::ToPoint) WantPoint("Point to extrude to");
    else WantPoint("Extrusion distance");
    options = {{"BothSides", "No", {"Yes", "No"}, false, true}, {"Solid", "Yes", {"Yes", "No"}, false, true}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string&) override {
    if (n == "BothSides") { both_ = !both_; options[0].value = both_ ? "Yes" : "No"; }
    if (n == "Solid") { solid_ = !solid_; options[1].value = solid_ ? "Yes" : "No"; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (kind_ == Kind::ToPoint) { BuildToPoint(ctx, p); return; }
    Point3d c((bbox_.min.x + bbox_.max.x) / 2, (bbox_.min.y + bbox_.max.y) / 2, (bbox_.min.z + bbox_.max.z) / 2);
    OnNumber(ctx, ON_DotProduct(p - c, ActiveNormal(ctx)));
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnNumber(CommandContext& ctx, double d) override {
    ctx.ClearPreview();
    if (d == 0) { ctx.Warn("Distance must be non-zero"); Finish(); return; }
    Vector3d n = ActiveNormal(ctx);
    ctx.Doc().BeginChange("Extrude");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      Vector3d v = n * d;
      Point3d shift(0, 0, 0);
      if (both_) { v = n * (2 * d); shift = Point3d(0, 0, 0) - n * d; }
      if (o->kind == ObjectKind::Curve) {
        if (ExtrudeCurve(ctx, *o->curve, v, shift)) ++made;
      } else if (o->kind == ObjectKind::Surface) {
        ON_Brep* b = ON_Brep::New();
        b->Create(new ON_NurbsSurface(o->surface->raw()));
        if (shift != Point3d(0, 0, 0)) b->Translate(shift);
        ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
        if (ON_BrepExtrudeFace(*b, 0, path, solid_) >= 0) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ++made; }
        else delete b;
      } else if (o->kind == ObjectKind::Brep) {
        ON_Brep* b = new ON_Brep(o->brep->raw());
        if (shift != Point3d(0, 0, 0)) b->Translate(shift);
        ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
        bool ok = true;
        const int nf = b->m_F.Count();
        for (int f = 0; f < nf && ok; ++f) ok = ON_BrepExtrudeFace(*b, f, path, solid_) >= 0;
        if (ok) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ++made; } else delete b;
      }
    }
    ctx.Print("Extruded " + std::to_string(made) + " object(s)");
    Finish();
  }
  bool ExtrudeCurve(CommandContext& ctx, const kernel::NurbsCurve& kc, Vector3d v, Point3d shift) {
    ON_NurbsCurve c = kc.raw();
    if (shift != Point3d(0, 0, 0)) c.Translate(shift);
    ON_Plane plane;
    const bool closed = c.IsClosed();
    if (closed && solid_ && c.IsPlanar(&plane, ctx.Settings().absolute_tolerance)) {
      ON_Brep* b = ON_BrepTrimmedPlane(plane, c);
      if (b) {
        ON_LineCurve path(ON_Line(ON_3dPoint::Origin, ON_3dPoint::Origin + v));
        if (ON_BrepExtrudeFace(*b, 0, path, true) >= 0) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); return true; }
        delete b;
      }
    }
    ON_SumSurface ss;
    if (!ss.Create(c, v)) return false;
    kernel::NurbsSurface k;
    if (!SurfaceFromON(ss, k)) return false;
    ctx.Doc().Add(SceneObject::MakeSurface(k));
    return true;
  }
  void BuildToPoint(CommandContext& ctx, Point3d apex) {
    ctx.ClearPreview();
    ctx.Doc().BeginChange("ExtrudeCrvToPoint");
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      // Ruled surface from the curve to a degenerate curve at the apex.
      ON_NurbsCurve a = o->curve->raw();
      ON_NurbsCurve b = a;
      for (int i = 0; i < b.CVCount(); ++i) b.SetCV(i, apex);
      ON_NurbsSurface s;
      if (s.CreateRuledSurface(a, b)) { kernel::NurbsSurface k; k.raw() = s; ctx.Doc().Add(SceneObject::MakeSurface(k)); }
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    ctx.ClearPreview();
    Point3d c((bbox_.min.x + bbox_.max.x) / 2, (bbox_.min.y + bbox_.max.y) / 2, (bbox_.min.z + bbox_.max.z) / 2);
    Vector3d v = kind_ == Kind::ToPoint ? (h - c) : ActiveNormal(ctx) * ON_DotProduct(h - c, ActiveNormal(ctx));
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      const DisplayCache& d = o->Display();
      for (size_t i = 0; i + 5 < d.lines.size(); i += 6) {
        Point3d a(d.lines[i], d.lines[i + 1], d.lines[i + 2]), b(d.lines[i + 3], d.lines[i + 4], d.lines[i + 5]);
        if (kind_ == Kind::ToPoint) ctx.AddPreviewLine(a, h);
        else { ctx.AddPreviewLine(a + v, b + v); ctx.AddPreviewLine(a, a + v); }
      }
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Kind kind_;
  std::vector<ObjectId> ids_;
  kernel::BoundingBox bbox_{};
  bool both_ = false, solid_ = true;
};

// Revolve selected curves around an axis defined by two points.
class RevolveCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to revolve"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) ids_.push_back(id); }
    if (ids_.empty()) { ctx.Warn("Select curves"); Finish(); return; }
    WantPoint("Start of revolve axis");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!a_) { a_ = p; WantPoint("End of revolve axis"); return; }
    if ((p - *a_).Length() <= 0) return;
    ctx.Doc().BeginChange("Revolve");
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      ON_RevSurface* rs = ON_RevSurface::New();
      rs->m_curve = new ON_NurbsCurve(o->curve->raw());
      rs->m_axis = ON_Line(*a_, p);
      rs->m_angle = ON_Interval(0, 2 * ON_PI);
      rs->m_t = rs->m_curve->Domain();
      ON_Brep* b = ON_BrepRevSurface(rs, true, true);
      if (b) ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b)));
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::vector<ObjectId> ids_;
  std::optional<Point3d> a_;
};

// Loft: surface through the selected curves (in selection order).
void Loft(CommandContext& ctx, const std::vector<ObjectId>& ids, bool subd) {
  std::vector<const kernel::NurbsCurve*> curves;
  for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) curves.push_back(o->curve.get()); }
  if (curves.size() < 2) { ctx.Warn("Select at least two curves"); return; }
  const int n = 24;
  std::vector<Point3d> grid;
  const bool closed = curves.front()->IsClosed();
  for (const kernel::NurbsCurve* c : curves) {
    kernel::Interval d = c->Domain();
    for (int i = 0; i < n; ++i) grid.push_back(c->PointAt(d.min + (d.max - d.min) * i / (closed ? n : n - 1.0)));
  }
  ctx.Doc().BeginChange(subd ? "SubDLoft" : "Loft");
  if (subd || closed) {
    std::vector<std::vector<Point3d>> rings;
    for (size_t k = 0; k < curves.size(); ++k) rings.emplace_back(grid.begin() + static_cast<long>(k * n), grid.begin() + static_cast<long>((k + 1) * n));
    if (closed) {
      kernel::Mesh m = kernel::Mesh::LoftClosedRings(rings);
      if (subd) ctx.Doc().Add(SceneObject::MakeSubD(kernel::SubD::FromControlMesh(m)));
      else ctx.Doc().Add(SceneObject::MakeMesh(m));
      return;
    }
  }
  int vdeg = std::min(3, static_cast<int>(curves.size()) - 1);
  ctx.Doc().Add(SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, n, static_cast<int>(curves.size()), 3, vdeg)));
}

void MeshFromSelection(CommandContext& ctx, const std::vector<ObjectId>& ids, const char* label, bool to_subd) {
  ctx.Doc().BeginChange(label);
  int made = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) continue;
    SceneObject n = to_subd ? SceneObject::MakeSubD(kernel::SubD::FromControlMesh(*m)) : SceneObject::MakeMesh(*m);
    n.layer_index = o->layer_index;
    n.name = o->name;
    ctx.Doc().Add(std::move(n));
    ++made;
  }
  ctx.Print(std::string(label) + ": created " + std::to_string(made) + " object(s)");
}

}  // namespace

void RegisterSolidCommands(CommandEngine& e) {
  Reg(e, "Box", Make<BoxCommand>(BoxCommand::Out::Brep));
  Reg(e, "MeshBox", Make<BoxCommand>(BoxCommand::Out::Mesh));
  Reg(e, "SubDBox", Make<BoxCommand>(BoxCommand::Out::SubD));
  Reg(e, "Sphere", SphereLike("Sphere", [](CommandContext& ctx, Point3d c, double r) { AddBrep(ctx, ON_BrepSphere(ON_Sphere(c, r)), "Sphere"); }));
  Reg(e, "MeshSphere", SphereLike("MeshSphere", [](CommandContext& ctx, Point3d c, double r) { AddObject(ctx, SceneObject::MakeMesh(kernel::Brep::Sphere(c, r).TessellateToClosedMesh(24, 12)), "MeshSphere"); }));
  Reg(e, "SubDSphere", SphereLike("SubDSphere", [](CommandContext& ctx, Point3d c, double r) { AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::Brep::Sphere(c, r).TessellateToClosedMesh(8, 4))), "SubDSphere"); }));
  Reg(e, "Ellipsoid", SphereLike("Ellipsoid", [](CommandContext& ctx, Point3d c, double r) {
        ON_Brep* b = ON_BrepSphere(ON_Sphere(c, r));
        if (b) { ON_Xform s = ON_Xform::DiagonalTransformation(1.0, 0.7, 0.5); ON_Xform t0 = ON_Xform::TranslationTransformation(ON_3dPoint::Origin - c); ON_Xform t1 = ON_Xform::TranslationTransformation(c - ON_3dPoint::Origin); b->Transform(t1 * s * t0); }
        AddBrep(ctx, b, "Ellipsoid");
      }), CommandStatus::Partial, "Axis ratios 1 : 0.7 : 0.5; axis picking is planned.");
  Reg(e, "SubDEllipsoid", SphereLike("SubDEllipsoid", [](CommandContext& ctx, Point3d c, double r) {
        kernel::Mesh m = kernel::Brep::Sphere(c, r).TessellateToClosedMesh(8, 4);
        ON_Xform s = ON_Xform::DiagonalTransformation(1.0, 0.7, 0.5);
        m = m.Transform(ON_Xform::TranslationTransformation(c - ON_3dPoint::Origin) * s * ON_Xform::TranslationTransformation(ON_3dPoint::Origin - c));
        AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(m)), "SubDEllipsoid");
      }), CommandStatus::Partial, "Axis ratios 1 : 0.7 : 0.5.");
  Reg(e, "Cylinder", Make<CylinderCommand>(CylinderCommand::Kind::Cylinder));
  Reg(e, "Cone", Make<CylinderCommand>(CylinderCommand::Kind::Cone));
  Reg(e, "TCone", Make<CylinderCommand>(CylinderCommand::Kind::TCone));
  Reg(e, "Tube", Make<CylinderCommand>(CylinderCommand::Kind::Tube));
  Reg(e, "Pyramid", Make<CylinderCommand>(CylinderCommand::Kind::Pyramid), CommandStatus::Partial, "Four-sided mesh pyramid.");
  Reg(e, "MeshCylinder", Make<CylinderCommand>(CylinderCommand::Kind::MeshCylinder));
  Reg(e, "MeshCone", Make<CylinderCommand>(CylinderCommand::Kind::MeshCone));
  Reg(e, "SubDCylinder", Make<CylinderCommand>(CylinderCommand::Kind::SubDCylinder));
  Reg(e, "SubDCone", Make<CylinderCommand>(CylinderCommand::Kind::SubDCone));
  Reg(e, "Torus", Make<TorusCommand>(TorusCommand::Kind::Brep));
  Reg(e, "MeshTorus", Make<TorusCommand>(TorusCommand::Kind::Mesh));
  Reg(e, "SubDTorus", Make<TorusCommand>(TorusCommand::Kind::SubD));
  Reg(e, "ExtrudeCrv", Make<ExtrudeCommand>(ExtrudeCommand::Kind::Curve));
  Reg(e, "Extrude", Make<ExtrudeCommand>(ExtrudeCommand::Kind::Curve));
  Reg(e, "ExtrudeSrf", Make<ExtrudeCommand>(ExtrudeCommand::Kind::Surface));
  Reg(e, "ExtrudeCrvToPoint", Make<ExtrudeCommand>(ExtrudeCommand::Kind::ToPoint));
  Reg(e, "Revolve", Make<RevolveCommand>());
  Reg(e, "Loft", OnSelection("Select curves to loft in order", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Loft(ctx, ids, false); }, 2), CommandStatus::Partial, "Normal loft; Loose/Tight/Straight styles are planned.");
  Reg(e, "SubDLoft", OnSelection("Select curves to loft in order", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Loft(ctx, ids, true); }, 2));
  Reg(e, "PlanarSrf", OnSelection("Select planar closed curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("PlanarSrf");
        int made = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Curve) continue;
          ON_Plane pl;
          if (!o->curve->raw().IsClosed() || !o->curve->raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance)) continue;
          if (ON_Brep* b = ON_BrepTrimmedPlane(pl, o->curve->raw())) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ++made; }
        }
        if (made == 0) ctx.Warn("Select closed planar curves"); else ctx.Print("Created " + std::to_string(made) + " planar surface(s)");
      }));
  Reg(e, "Mesh", OnSelection("Select surfaces, polysurfaces or SubDs to mesh", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MeshFromSelection(ctx, ids, "Mesh", false); }));
  Reg(e, "ToSubD", OnSelection("Select meshes or polysurfaces to convert", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MeshFromSelection(ctx, ids, "ToSubD", true); }));
  Reg(e, "MeshToSubD", OnSelection("Select meshes to convert", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MeshFromSelection(ctx, ids, "MeshToSubD", true); }));
  Reg(e, "ToNURBS", OnSelection("Select SubDs or meshes to convert", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("ToNURBS");
        int made = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          if (o->kind == ObjectKind::SubD) {
            ON_SubD copy = o->subd->raw();
            ON_Brep* b = copy.GetSurfaceBrep(ON_SubDToBrepParameters::Default, nullptr);
            if (b) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ++made; }
          } else if (o->kind == ObjectKind::Mesh) {
            ON_Brep* b = ON_BrepFromMesh(o->mesh->raw().Topology());
            if (b) { ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); ++made; }
          }
        }
        ctx.Print("Converted " + std::to_string(made) + " object(s)");
      }));
  Reg(e, "MeshToNURB", OnSelection("Select meshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("MeshToNURB");
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) if (ON_Brep* b = ON_BrepFromMesh(o->mesh->raw().Topology())) ctx.Doc().Add(SceneObject::MakeBrep(WrapBrep(b))); }
      }));
  Reg(e, "SubDivide", OnSelection("Select SubDs to subdivide", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("SubDivide");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::SubD) { o->subd->Subdivide(1); o->InvalidateDisplay(); } }
      }));
  Reg(e, "MeshPlane", Make<PointsCommand>(std::vector<std::string>{"First corner", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ON_Plane pl = ActivePlane(ctx); double u0, v0, u1, v1;
                                            pl.ClosestPointTo(p[0], &u0, &v0); pl.ClosestPointTo(p[1], &u1, &v1);
                                            std::vector<Point3d> grid = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u0, v1), pl.PointAt(u1, v1)};
                                            AddObject(ctx, SceneObject::MakeMesh(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1).TessellateGrid(8, 8)), "MeshPlane");
                                          }));
  Reg(e, "SubDPlane", Make<PointsCommand>(std::vector<std::string>{"First corner", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ON_Plane pl = ActivePlane(ctx); double u0, v0, u1, v1;
                                            pl.ClosestPointTo(p[0], &u0, &v0); pl.ClosestPointTo(p[1], &u1, &v1);
                                            std::vector<Point3d> grid = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u0, v1), pl.PointAt(u1, v1)};
                                            AddObject(ctx, SceneObject::MakeSubD(kernel::SubD::FromControlMesh(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1).TessellateGrid(4, 4))), "SubDPlane");
                                          }));
  Reg(e, "Cap", OnSelection("Select open surfaces to cap", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Cap");
        int capped = 0;
        for (ObjectId id : ids) {
          SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Brep) continue;
          ON_Brep& b = o->brep->raw();
          if (b.IsSolid()) continue;
          // Cap every planar closed naked-edge loop.
          ON_SimpleArray<int> naked;
          for (int ei = 0; ei < b.m_E.Count(); ++ei) if (b.m_E[ei].TrimCount() == 1) naked.Append(ei);
          if (naked.Count() == 0) continue;
          ON_Polyline pl;
          for (int i = 0; i < naked.Count(); ++i) { const ON_BrepEdge& ed = b.m_E[naked[i]]; for (int s = 0; s < 8; ++s) pl.Append(ed.PointAt(ed.Domain().ParameterAt(s / 8.0))); }
          ON_Plane plane;
          if (!pl.IsClosed(ctx.Settings().absolute_tolerance)) pl.Append(pl[0]);
          ON_PolylineCurve pc(pl);
          if (pc.IsPlanar(&plane, ctx.Settings().absolute_tolerance * 10)) {
            if (ON_Brep* cap = ON_BrepTrimmedPlane(plane, pc)) { b.Append(*cap); delete cap; b.JoinNakedEdges(ctx.Settings().absolute_tolerance * 10); o->InvalidateDisplay(); ++capped; }
          }
        }
        ctx.Print("Capped " + std::to_string(capped) + " object(s)");
      }), CommandStatus::Partial, "Caps a single planar opening per polysurface.");
}

}  // namespace dino8::app
