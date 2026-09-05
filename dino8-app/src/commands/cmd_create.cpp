// Point and curve creation commands.
#include "commands/cmd_common.h"

namespace dino8::app {

namespace {

// Polyline / control-point curve / interpolated curve: pick points until Enter.
class MultiPointCurveCommand : public Command {
 public:
  enum class Kind { Polyline, ControlPoint, Interpolated, Lines };
  explicit MultiPointCurveCommand(Kind kind, int degree = 3) : kind_(kind), degree_(degree) {}
  void Begin(CommandContext&) override {
    WantPoint("Start of curve");
    options = {{"Close", "", {}, false, false}, {"Undo", "", {}, false, false}, {"Degree", std::to_string(degree_), {}, true, false}};
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    WantPoint(pts_.size() == 1 ? "Next point" : "Next point. Press Enter when done");
    if (kind_ == Kind::Lines && pts_.size() == 2) {
      AddCurve(ctx, PolylineCurve(pts_), "Lines");
      Point3d last = pts_.back();
      pts_.clear();
      pts_.push_back(last);
    }
  }
  void OnOption(CommandContext& ctx, const std::string& name, const std::string& value) override {
    if (name == "Close") { closed_ = true; OnEnter(ctx); }
    else if (name == "Undo" && !pts_.empty()) { pts_.pop_back(); if (!pts_.empty()) ctx.SetLastPoint(pts_.back()); }
    else if (name == "Degree") { int d = std::atoi(value.c_str()); if (d >= 1 && d <= 11) degree_ = d; options[2].value = std::to_string(degree_); }
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.ClearPreview();
    if (pts_.size() >= 2) {
      std::vector<Point3d> pts = pts_;
      if (closed_ && pts.size() >= 3) pts.push_back(pts.front());
      if (kind_ == Kind::Polyline || kind_ == Kind::Lines || static_cast<int>(pts.size()) <= 2) {
        AddCurve(ctx, PolylineCurve(pts), "Polyline");
      } else if (kind_ == Kind::ControlPoint) {
        int deg = std::min(degree_, static_cast<int>(pts.size()) - 1);
        AddCurve(ctx, kernel::NurbsCurve::FromControlPoints(pts, deg), "Curve");
      } else {
        // Interpolated: use OpenNURBS' cubic interpolation through the points.
        ON_3dPointArray arr;
        for (const Point3d& p : pts) arr.Append(p);
        ON_NurbsCurve nc;
        bool ok = nc.CreateClampedUniformNurbs(3, 3, arr.Count(), arr.Array()) != 0;
        if (ok) {
          // Solve so the curve passes through the points (global interpolation, chord-length parameters).
          kernel::NurbsCurve k;
          k.raw() = nc;
          // Refine: move CVs so the curve interpolates (simple fixed-point relaxation).
          for (int iter = 0; iter < 30; ++iter) {
            for (int i = 0; i < arr.Count(); ++i) {
              double t = k.raw().Domain().ParameterAt(static_cast<double>(i) / (arr.Count() - 1));
              Point3d on = k.raw().PointAt(t);
              Point3d cv;
              k.raw().GetCV(i, cv);
              k.raw().SetCV(i, cv + (arr[i] - on));
            }
          }
          AddCurve(ctx, k, "InterpCrv");
        }
      }
    }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    std::vector<Point3d> pv = pts_;
    pv.push_back(h);
    if (kind_ == Kind::ControlPoint && pv.size() >= 3) {
      int deg = std::min(degree_, static_cast<int>(pv.size()) - 1);
      ctx.AddPreviewCurve(kernel::NurbsCurve::FromControlPoints(pv, deg));
      ctx.AddPreviewPolyline(pv);
    } else {
      ctx.AddPreviewPolyline(pv);
    }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  Kind kind_;
  int degree_;
  bool closed_ = false;
  std::vector<Point3d> pts_;
};

class PointsCommandMany : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Location of point object. Press Enter when done"); }
  void OnPoint(CommandContext& ctx, Point3d p) override { AddObject(ctx, SceneObject::MakePoint(p), "Points"); ctx.SetLastPoint(p); }
  void OnEnter(CommandContext&) override { Finish(); }
};

void AddCircle(CommandContext& ctx, const ON_Circle& c, const char* label) {
  ON_ArcCurve ac(c);
  kernel::NurbsCurve k;
  if (CurveFromON(ac, k)) AddCurve(ctx, k, label);
}

void PreviewCircle(CommandContext& ctx, const ON_Circle& c) {
  ctx.AddPreviewPolyline(CirclePoints(c));
}

class CircleCommand : public PointThenDistanceCommand {
 public:
  CircleCommand()
      : PointThenDistanceCommand("Center of circle", "Radius",
                                 [](CommandContext& ctx, Point3d c, double r, Point3d) {
                                   if (r <= 0) return;
                                   AddCircle(ctx, ON_Circle(ActivePlaneAt(ctx, c), r), "Circle");
                                 }, 10.0) {
    preview_ = [](CommandContext& ctx, Point3d c, double r, Point3d) { if (r > 0) PreviewCircle(ctx, ON_Circle(ActivePlaneAt(ctx, c), r)); };
  }
  static ON_Plane ActivePlaneAt(CommandContext& ctx, Point3d origin) {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(origin);
    return pl;
  }
  void Begin(CommandContext& ctx) override {
    PointThenDistanceCommand::Begin(ctx);
    options = {{"Diameter", "", {}, false, false}, {"3Point", "", {}, false, false}, {"Vertical", "", {}, false, false}};
  }
  void OnOption(CommandContext& ctx, const std::string& name, const std::string&) override {
    if (name == "Diameter") { diameter_ = true; p2_ = "Diameter"; if (base_) WantPoint("Diameter"); }
    else if (name == "3Point") { ctx.Engine().Execute("Circle3Pt"); }
  }
  void OnNumber(CommandContext& ctx, double v) override { PointThenDistanceCommand::OnNumber(ctx, diameter_ ? v / 2 : v); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (base_ && diameter_) { OnNumber(ctx, (p - *base_).Length()); return; }
    PointThenDistanceCommand::OnPoint(ctx, p);
  }
  bool diameter_ = false;
};

}  // namespace

void RegisterCreateCommands(CommandEngine& e) {
  Reg(e, "Point", Make<PointsCommand>(std::vector<std::string>{"Location of point object"},
                                      [](CommandContext& ctx, const std::vector<Point3d>& p) { AddObject(ctx, SceneObject::MakePoint(p[0]), "Point"); }));
  Reg(e, "Points", Make<PointsCommandMany>());
  Reg(e, "Line", Make<PointsCommand>(std::vector<std::string>{"Start of line", "End of line"},
                                     [](CommandContext& ctx, const std::vector<Point3d>& p) { AddCurve(ctx, PolylineCurve(p), "Line"); },
                                     [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) { ctx.AddPreviewLine(p[0], h); }));
  Reg(e, "Lines", Make<MultiPointCurveCommand>(MultiPointCurveCommand::Kind::Lines));
  Reg(e, "Polyline", Make<MultiPointCurveCommand>(MultiPointCurveCommand::Kind::Polyline));
  Reg(e, "Curve", Make<MultiPointCurveCommand>(MultiPointCurveCommand::Kind::ControlPoint));
  Reg(e, "InterpCrv", Make<MultiPointCurveCommand>(MultiPointCurveCommand::Kind::Interpolated));
  Reg(e, "CurveThroughPt", Make<MultiPointCurveCommand>(MultiPointCurveCommand::Kind::Interpolated), CommandStatus::Partial, "Picks points instead of selecting point objects.");
  Reg(e, "Sketch", Make<MultiPointCurveCommand>(MultiPointCurveCommand::Kind::Interpolated), CommandStatus::Partial, "Click points; freehand drag sketching is planned.");
  Reg(e, "Circle", Make<CircleCommand>());
  Reg(e, "Circle3Pt", Make<PointsCommand>(std::vector<std::string>{"First point on circle", "Second point on circle", "Third point on circle"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ON_Circle c;
                                            if (c.Create(p[0], p[1], p[2])) AddCircle(ctx, c, "Circle3Pt");
                                            else ctx.Warn("Points are collinear");
                                          },
                                          [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                            if (p.size() == 1) ctx.AddPreviewLine(p[0], h);
                                            else { ON_Circle c; if (c.Create(p[0], p[1], h)) PreviewCircle(ctx, c); }
                                          }));
  Reg(e, "CircleD", Make<PointsCommand>(std::vector<std::string>{"Start of diameter", "End of diameter"},
                                        [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                          ON_Plane pl = ActivePlane(ctx);
                                          pl.SetOrigin((p[0] + p[1]) / 2.0);
                                          AddCircle(ctx, ON_Circle(pl, (p[1] - p[0]).Length() / 2), "CircleD");
                                        },
                                        [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                          ON_Plane pl = ActivePlane(ctx); pl.SetOrigin((p[0] + h) / 2.0);
                                          PreviewCircle(ctx, ON_Circle(pl, (h - p[0]).Length() / 2));
                                        }));
  Reg(e, "Arc", Make<PointsCommand>(std::vector<std::string>{"Center of arc", "Start of arc", "End of arc"},
                                    [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                      ON_Plane pl = ActivePlane(ctx);
                                      pl.SetOrigin(p[0]);
                                      double r = (p[1] - p[0]).Length();
                                      if (r <= 0) return;
                                      double a0 = std::atan2(ON_DotProduct(p[1] - p[0], pl.yaxis), ON_DotProduct(p[1] - p[0], pl.xaxis));
                                      double a1 = std::atan2(ON_DotProduct(p[2] - p[0], pl.yaxis), ON_DotProduct(p[2] - p[0], pl.xaxis));
                                      if (a1 <= a0) a1 += 2 * ON_PI;
                                      ON_Arc arc(ON_Circle(pl, r), ON_Interval(a0, a1));
                                      ON_ArcCurve ac(arc);
                                      kernel::NurbsCurve k;
                                      if (CurveFromON(ac, k)) AddCurve(ctx, k, "Arc");
                                    },
                                    [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                      if (p.size() == 1) { ctx.AddPreviewLine(p[0], h); return; }
                                      ON_Plane pl = ActivePlane(ctx); pl.SetOrigin(p[0]);
                                      double r = (p[1] - p[0]).Length();
                                      double a0 = std::atan2(ON_DotProduct(p[1] - p[0], pl.yaxis), ON_DotProduct(p[1] - p[0], pl.xaxis));
                                      double a1 = std::atan2(ON_DotProduct(h - p[0], pl.yaxis), ON_DotProduct(h - p[0], pl.xaxis));
                                      if (a1 <= a0) a1 += 2 * ON_PI;
                                      std::vector<Point3d> pts;
                                      for (int i = 0; i <= 48; ++i) pts.push_back(ON_Circle(pl, r).PointAt(a0 + (a1 - a0) * i / 48.0));
                                      ctx.AddPreviewPolyline(pts);
                                      ctx.AddPreviewLine(p[0], h);
                                    }));
  Reg(e, "Arc3Pt", Make<PointsCommand>(std::vector<std::string>{"Start of arc", "End of arc", "Point on arc"},
                                       [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                         ON_Arc arc(p[0], p[2], p[1]);
                                         if (!arc.IsValid()) { ctx.Warn("Points are collinear"); return; }
                                         ON_ArcCurve ac(arc);
                                         kernel::NurbsCurve k;
                                         if (CurveFromON(ac, k)) AddCurve(ctx, k, "Arc3Pt");
                                       },
                                       [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                         if (p.size() == 1) { ctx.AddPreviewLine(p[0], h); return; }
                                         ON_Arc arc(p[0], h, p[1]);
                                         if (!arc.IsValid()) return;
                                         std::vector<Point3d> pts;
                                         for (int i = 0; i <= 48; ++i) pts.push_back(arc.PointAt(arc.DomainRadians().ParameterAt(i / 48.0)));
                                         ctx.AddPreviewPolyline(pts);
                                       }));
  Reg(e, "Rectangle", Make<PointsCommand>(std::vector<std::string>{"First corner of rectangle", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ON_Plane pl = ActivePlane(ctx);
                                            double u0, v0, u1, v1;
                                            pl.ClosestPointTo(p[0], &u0, &v0);
                                            pl.ClosestPointTo(p[1], &u1, &v1);
                                            std::vector<Point3d> pts = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1), pl.PointAt(u0, v0)};
                                            AddCurve(ctx, PolylineCurve(pts), "Rectangle");
                                          },
                                          [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                            ON_Plane pl = ActivePlane(ctx);
                                            double u0, v0, u1, v1;
                                            pl.ClosestPointTo(p[0], &u0, &v0);
                                            pl.ClosestPointTo(h, &u1, &v1);
                                            ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
                                          }));
  Reg(e, "Rectangle3Pt", Make<PointsCommand>(std::vector<std::string>{"Start of edge", "End of edge", "Width"},
                                             [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                               Vector3d e1 = p[1] - p[0];
                                               Vector3d n = ActiveNormal(ctx);
                                               Vector3d e2 = ON_CrossProduct(n, e1);
                                               e2.Unitize();
                                               double w = ON_DotProduct(p[2] - p[1], e2);
                                               e2 *= w;
                                               AddCurve(ctx, PolylineCurve({p[0], p[1], p[1] + e2, p[0] + e2, p[0]}), "Rectangle3Pt");
                                             },
                                             [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                               if (p.size() == 1) { ctx.AddPreviewLine(p[0], h); return; }
                                               Vector3d e1 = p[1] - p[0]; Vector3d e2 = ON_CrossProduct(ActiveNormal(ctx), e1); e2.Unitize();
                                               e2 *= ON_DotProduct(h - p[1], e2);
                                               ctx.AddPreviewPolyline({p[0], p[1], p[1] + e2, p[0] + e2}, true);
                                             }));
  auto polygon = [](int sides, bool star) {
    return Make<PointsCommand>(std::vector<std::string>{"Center of polygon", "Corner of polygon"},
                               [sides, star](CommandContext& ctx, const std::vector<Point3d>& p) {
                                 ON_Plane pl = ActivePlane(ctx);
                                 pl.SetOrigin(p[0]);
                                 double r = (p[1] - p[0]).Length();
                                 if (r <= 0) return;
                                 double a0 = std::atan2(ON_DotProduct(p[1] - p[0], pl.yaxis), ON_DotProduct(p[1] - p[0], pl.xaxis));
                                 std::vector<Point3d> pts;
                                 int n = star ? sides * 2 : sides;
                                 for (int i = 0; i <= n; ++i) {
                                   double rr = (star && (i % 2 == 1)) ? r * 0.5 : r;
                                   double a = a0 + 2 * ON_PI * i / n;
                                   pts.push_back(pl.PointAt(rr * std::cos(a), rr * std::sin(a)));
                                 }
                                 AddCurve(ctx, PolylineCurve(pts), star ? "PolygonStar" : "Polygon");
                               },
                               [sides, star](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                 ON_Plane pl = ActivePlane(ctx); pl.SetOrigin(p[0]);
                                 double r = (h - p[0]).Length();
                                 double a0 = std::atan2(ON_DotProduct(h - p[0], pl.yaxis), ON_DotProduct(h - p[0], pl.xaxis));
                                 std::vector<Point3d> pts;
                                 int n = star ? sides * 2 : sides;
                                 for (int i = 0; i < n; ++i) { double rr = (star && (i % 2 == 1)) ? r * 0.5 : r; double a = a0 + 2 * ON_PI * i / n; pts.push_back(pl.PointAt(rr * std::cos(a), rr * std::sin(a))); }
                                 ctx.AddPreviewPolyline(pts, true);
                               });
  };
  Reg(e, "Polygon", polygon(6, false), CommandStatus::Partial, "Six sides; NumSides option is planned.");
  Reg(e, "PolygonStar", polygon(5, true), CommandStatus::Partial, "Five points; NumSides option is planned.");
  Reg(e, "Ellipse", Make<PointsCommand>(std::vector<std::string>{"Ellipse center", "End of first axis", "End of second axis"},
                                        [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                          ON_Plane pl = ActivePlane(ctx);
                                          pl.SetOrigin(p[0]);
                                          Vector3d x = p[1] - p[0];
                                          double a = x.Length();
                                          if (a <= 0) return;
                                          x.Unitize();
                                          Vector3d y = ON_CrossProduct(pl.zaxis, x);
                                          double b = std::fabs(ON_DotProduct(p[2] - p[0], y));
                                          if (b <= 0) return;
                                          ON_Plane epl(p[0], x, y);
                                          ON_Ellipse el(epl, a, b);
                                          ON_NurbsCurve nc;
                                          if (el.GetNurbForm(nc)) { kernel::NurbsCurve k; k.raw() = nc; AddCurve(ctx, k, "Ellipse"); }
                                        },
                                        [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                          if (p.size() == 1) { ctx.AddPreviewLine(p[0], h); return; }
                                          ON_Plane pl = ActivePlane(ctx);
                                          Vector3d x = p[1] - p[0]; double a = x.Length(); if (a <= 0) return; x.Unitize();
                                          Vector3d y = ON_CrossProduct(pl.zaxis, x);
                                          double b = std::fabs(ON_DotProduct(h - p[0], y));
                                          std::vector<Point3d> pts;
                                          for (int i = 0; i < 64; ++i) { double t = 2 * ON_PI * i / 64; pts.push_back(p[0] + x * (a * std::cos(t)) + y * (b * std::sin(t))); }
                                          ctx.AddPreviewPolyline(pts, true);
                                        }));
  Reg(e, "Helix", Make<PointsCommand>(std::vector<std::string>{"Start of axis", "End of axis", "Radius"},
                                      [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                        Vector3d axis = p[1] - p[0];
                                        double h = axis.Length();
                                        if (h <= 0) return;
                                        axis.Unitize();
                                        double r = (p[2] - p[1]).Length();
                                        if (r <= 0) return;
                                        ON_Plane pl(p[0], axis);
                                        const int turns = 5, per = 32;
                                        std::vector<Point3d> pts;
                                        for (int i = 0; i <= turns * per; ++i) {
                                          double t = static_cast<double>(i) / (turns * per);
                                          double a = 2 * ON_PI * turns * t;
                                          pts.push_back(pl.PointAt(r * std::cos(a), r * std::sin(a)) + axis * (h * t));
                                        }
                                        AddCurve(ctx, kernel::NurbsCurve::FromControlPoints(pts, 3), "Helix");
                                      }),
      CommandStatus::Partial, "Five turns; Turns/Pitch options are planned.");
  Reg(e, "Spiral", Make<PointsCommand>(std::vector<std::string>{"Start of axis", "End of axis", "First radius"},
                                       [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                         Vector3d axis = p[1] - p[0];
                                         double h = axis.Length();
                                         if (h <= 0) return;
                                         axis.Unitize();
                                         double r = (p[2] - p[1]).Length();
                                         ON_Plane pl(p[0], axis);
                                         const int turns = 5, per = 32;
                                         std::vector<Point3d> pts;
                                         for (int i = 0; i <= turns * per; ++i) {
                                           double t = static_cast<double>(i) / (turns * per);
                                           double a = 2 * ON_PI * turns * t;
                                           double rr = r * (1.0 - 0.8 * t);
                                           pts.push_back(pl.PointAt(rr * std::cos(a), rr * std::sin(a)) + axis * (h * t));
                                         }
                                         AddCurve(ctx, kernel::NurbsCurve::FromControlPoints(pts, 3), "Spiral");
                                       }),
      CommandStatus::Partial, "Five turns tapering to 20%; options are planned.");
  Reg(e, "PointGrid", Make<PointsCommand>(std::vector<std::string>{"First corner of grid", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ctx.Doc().BeginChange("PointGrid");
                                            const int n = 5;
                                            for (int i = 0; i < n; ++i)
                                              for (int j = 0; j < n; ++j)
                                                ctx.Doc().Add(SceneObject::MakePoint(Point3d(p[0].x + (p[1].x - p[0].x) * i / (n - 1), p[0].y + (p[1].y - p[0].y) * j / (n - 1), p[0].z)));
                                          }),
      CommandStatus::Partial, "5 x 5 grid; count options are planned.");
  Reg(e, "Divide", OnSelection("Select curves to divide", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("Divide");
        int made = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o || o->kind != ObjectKind::Curve) continue;
          for (double t : o->curve->DivideByCount(10)) { ctx.Doc().Add(SceneObject::MakePoint(o->curve->PointAt(t))); ++made; }
        }
        ctx.Print("Created " + std::to_string(made) + " points");
      }), CommandStatus::Partial, "Divides into 10 segments; count/length options are planned.");
  Reg(e, "ClosestPt", Make<PointsCommand>(std::vector<std::string>{"Point to test"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            double best = 1e300; Point3d bp = p[0];
                                            for (const SceneObject& o : ctx.Doc().Objects()) {
                                              if (!o.selected) continue;
                                              Point3d cp = p[0];
                                              if (o.kind == ObjectKind::Curve) cp = o.curve->ClosestPoint(p[0]);
                                              else if (o.kind == ObjectKind::Surface) cp = o.surface->ClosestPoint(p[0]);
                                              else if (o.kind == ObjectKind::Mesh) cp = o.mesh->ClosestPoint(p[0]);
                                              else if (o.kind == ObjectKind::Point) cp = o.point;
                                              else continue;
                                              double d = (cp - p[0]).Length();
                                              if (d < best) { best = d; bp = cp; }
                                            }
                                            if (best < 1e299) { AddObject(ctx, SceneObject::MakePoint(bp), "ClosestPt"); ctx.Print("Closest point " + FormatPoint(bp) + " distance " + FormatNumber(best)); }
                                            else ctx.Warn("Select curves, surfaces, meshes or points first");
                                          }));
  Reg(e, "Plane", Make<PointsCommand>(std::vector<std::string>{"First corner of plane", "Other corner"},
                                      [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                        ON_Plane pl = ActivePlane(ctx);
                                        double u0, v0, u1, v1;
                                        pl.ClosestPointTo(p[0], &u0, &v0);
                                        pl.ClosestPointTo(p[1], &u1, &v1);
                                        std::vector<Point3d> grid = {pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u0, v1), pl.PointAt(u1, v1)};
                                        AddObject(ctx, SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)), "Plane");
                                      },
                                      [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) {
                                        ON_Plane pl = ActivePlane(ctx); double u0, v0, u1, v1;
                                        pl.ClosestPointTo(p[0], &u0, &v0); pl.ClosestPointTo(h, &u1, &v1);
                                        ctx.AddPreviewPolyline({pl.PointAt(u0, v0), pl.PointAt(u1, v0), pl.PointAt(u1, v1), pl.PointAt(u0, v1)}, true);
                                      }));
  Reg(e, "Plane3Pt", Make<PointsCommand>(std::vector<std::string>{"Start of edge", "End of edge", "Width"},
                                         [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                           Vector3d e1 = p[1] - p[0];
                                           Vector3d e2 = p[2] - p[1];
                                           std::vector<Point3d> grid = {p[0], p[1], p[0] + e2, p[1] + e2};
                                           AddObject(ctx, SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)), "Plane3Pt");
                                         }));
  Reg(e, "SrfPt", Make<PointsCommand>(std::vector<std::string>{"First corner", "Second corner", "Third corner", "Fourth corner"},
                                      [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                        std::vector<Point3d> grid = {p[0], p[1], p[3], p[2]};
                                        AddObject(ctx, SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1)), "SrfPt");
                                      },
                                      [](CommandContext& ctx, const std::vector<Point3d>& p, Point3d h) { std::vector<Point3d> pv = p; pv.push_back(h); ctx.AddPreviewPolyline(pv, pv.size() > 2); }));
}

}  // namespace dino8::app
