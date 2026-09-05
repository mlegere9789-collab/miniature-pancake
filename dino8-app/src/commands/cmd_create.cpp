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


// Polygon / star with a NumSides option, centre and corner (or edge) picks.
class PolygonCommand : public Command {
 public:
  explicit PolygonCommand(bool star) : star_(star) {}
  void Begin(CommandContext&) override {
    WantPoint("Center of polygon");
    options = {{"NumSides", std::to_string(sides_), {}, true, false}, {"Edge", "", {}, false, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "NumSides") { int k = std::atoi(v.c_str()); if (k >= 3 && k <= 360) sides_ = k; options[0].value = std::to_string(sides_); }
    if (n == "Edge") edge_mode_ = true;
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!center_) { int k = std::atoi(t.c_str()); if (k >= 3 && k <= 360) { sides_ = k; options[0].value = std::to_string(sides_); ctx.Print("NumSides=" + std::to_string(sides_)); } }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (!center_) { center_ = p; WantPoint(star_ ? "Corner of star" : (edge_mode_ ? "End of edge" : "Corner of polygon")); return; }
    ctx.ClearPreview();
    std::vector<Point3d> pts = Build(ctx, p, true);
    if (pts.size() >= 4) AddCurve(ctx, PolylineCurve(pts), star_ ? "PolygonStar" : "Polygon");
    Finish();
  }
  std::vector<Point3d> Build(CommandContext& ctx, Point3d p, bool close) {
    ON_Plane pl = ActivePlane(ctx);
    pl.SetOrigin(*center_);
    double r = (p - *center_).Length();
    if (r <= 0) return {};
    double a0 = std::atan2(ON_DotProduct(p - *center_, pl.yaxis), ON_DotProduct(p - *center_, pl.xaxis));
    if (edge_mode_ && !star_) {
      // p is an edge endpoint: circumradius from the edge length.
      const double half = ON_PI / sides_;
      r = r / std::cos(half);
      a0 += half;
    }
    std::vector<Point3d> pts;
    const int n = star_ ? sides_ * 2 : sides_;
    for (int i = 0; i < n + (close ? 1 : 0); ++i) {
      const double rr = (star_ && (i % 2 == 1)) ? r * 0.5 : r;
      const double a = a0 + 2 * ON_PI * i / n;
      pts.push_back(pl.PointAt(rr * std::cos(a), rr * std::sin(a)));
    }
    return pts;
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!center_) return;
    ctx.ClearPreview();
    std::vector<Point3d> pts = Build(ctx, h, false);
    if (!pts.empty()) ctx.AddPreviewPolyline(pts, true);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  bool star_;
  bool edge_mode_ = false;
  int sides_ = 6;
  std::optional<Point3d> center_;
};

// Select curves, then a count: Divide / Rebuild.
class CurveCountCommand : public Command {
 public:
  CurveCountCommand(std::string prompt, std::string count_prompt, int def, std::function<void(CommandContext&, ObjectId, int)> apply, std::string label)
      : prompt_(std::move(prompt)), count_prompt_(std::move(count_prompt)), def_(def), apply_(std::move(apply)), label_(std::move(label)) {}
  void Begin(CommandContext&) override { WantObjects(prompt_); }
  void OnObjects(CommandContext&, const std::vector<ObjectId>& ids) override { ids_ = ids; WantNumber(count_prompt_, def_); }
  void OnNumber(CommandContext& ctx, double v) override {
    ctx.Doc().BeginChange(label_);
    for (ObjectId id : ids_) apply_(ctx, id, static_cast<int>(v));
    Finish();
  }
  std::string prompt_, count_prompt_;
  int def_;
  std::function<void(CommandContext&, ObjectId, int)> apply_;
  std::string label_;
  std::vector<ObjectId> ids_;
};

// Helix / spiral with Turns and Radius options.
class HelixCommand : public Command {
 public:
  explicit HelixCommand(bool spiral) : spiral_(spiral) {}
  void Begin(CommandContext&) override {
    WantPoint("Start of axis");
    options = {{"Turns", FormatNumber(turns_), {}, true, false}, {"Pitch", "", {}, true, false}};
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Turns") { double t = std::atof(v.c_str()); if (t > 0) turns_ = t; options[0].value = FormatNumber(turns_); }
    if (n == "Pitch") { double p = std::atof(v.c_str()); if (p > 0) pitch_ = p; }
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    pts_.push_back(p);
    if (pts_.size() == 1) { WantPoint("End of axis"); return; }
    if (pts_.size() == 2) { WantPoint(spiral_ ? "First radius" : "Radius"); return; }
    if (spiral_ && pts_.size() == 3) { WantPoint("Second radius"); return; }
    Build(ctx, pts_.size() == 4 ? (pts_[3] - pts_[1]).Length() : (pts_[2] - pts_[1]).Length() * (spiral_ ? 0.2 : 1.0));
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* e; double v = std::strtod(t.c_str(), &e);
    if (e && !*e && pts_.size() >= 2) {
      if (pts_.size() == 2) { pts_.push_back(pts_[1] + Vector3d(v, 0, 0)); if (spiral_) { WantPoint("Second radius"); return; } Build(ctx, v); }
      else Build(ctx, v);
    }
  }
  void Build(CommandContext& ctx, double r2) {
    ctx.ClearPreview();
    Vector3d axis = pts_[1] - pts_[0];
    const double h = axis.Length();
    const double r1 = (pts_[2] - pts_[1]).Length();
    if (h <= 0 || r1 <= 0) { ctx.Warn("Axis and radius must be non-zero"); Finish(); return; }
    axis.Unitize();
    double turns = turns_;
    if (pitch_ > 0) turns = h / pitch_;
    ON_Plane pl(pts_[0], axis);
    const int per = 24;
    const int n = std::max(8, static_cast<int>(turns * per));
    std::vector<Point3d> pts;
    for (int i = 0; i <= n; ++i) {
      const double t = static_cast<double>(i) / n;
      const double a = 2 * ON_PI * turns * t;
      const double rr = r1 + (r2 - r1) * t;
      pts.push_back(pl.PointAt(rr * std::cos(a), rr * std::sin(a)) + axis * (h * t));
    }
    AddCurve(ctx, kernel::NurbsCurve::FromControlPoints(pts, 3), spiral_ ? "Spiral" : "Helix");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (pts_.empty()) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(pts_.back(), h);
    if (pts_.size() >= 2) { ON_Plane pl = ActivePlane(ctx); pl.SetOrigin(pts_[1]); ctx.AddPreviewPolyline(CirclePoints(ON_Circle(ON_Plane(pts_[1], pts_[1] - pts_[0]), (h - pts_[1]).Length()))); }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  bool spiral_;
  double turns_ = 5, pitch_ = 0;
  std::vector<Point3d> pts_;
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
  Reg(e, "Polygon", Make<PolygonCommand>(false));
  Reg(e, "PolygonStar", Make<PolygonCommand>(true));
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
  Reg(e, "Helix", Make<HelixCommand>(false));
  Reg(e, "Spiral", Make<HelixCommand>(true));
  Reg(e, "PointGrid", Make<PointsCommand>(std::vector<std::string>{"First corner of grid", "Other corner"},
                                          [](CommandContext& ctx, const std::vector<Point3d>& p) {
                                            ctx.Doc().BeginChange("PointGrid");
                                            const int n = 5;
                                            for (int i = 0; i < n; ++i)
                                              for (int j = 0; j < n; ++j)
                                                ctx.Doc().Add(SceneObject::MakePoint(Point3d(p[0].x + (p[1].x - p[0].x) * i / (n - 1), p[0].y + (p[1].y - p[0].y) * j / (n - 1), p[0].z)));
                                          }),
      CommandStatus::Partial, "5 x 5 grid; count options are planned.");
  Reg(e, "Divide", Make<CurveCountCommand>("Select curves to divide", "Number of segments", 10,
                                           [](CommandContext& ctx, ObjectId id, int n) {
                                             const SceneObject* o = ctx.Doc().Find(id);
                                             if (!o || o->kind != ObjectKind::Curve || n < 1) return;
                                             const kernel::NurbsCurve curve = *o->curve;  // Add() may reallocate objects
                                             for (double t : curve.DivideByCount(n)) ctx.Doc().Add(SceneObject::MakePoint(curve.PointAt(t)));
                                           }, "Divide"));
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
