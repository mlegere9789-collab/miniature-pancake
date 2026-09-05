// Helpers shared by the cmd_*.cpp files.
#pragma once

#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/Application.h"
#include "commands/Command.h"
#include "commands/CommandEngine.h"
#include "dino8/kernel/boolean.h"
#include "geom/BrepMesher.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

// Wraps an OpenNURBS curve as a kernel NurbsCurve (exact NURBS form).
inline bool CurveFromON(const ON_Curve& c, kernel::NurbsCurve& out) {
  ON_NurbsCurve nc;
  if (c.GetNurbForm(nc) <= 0) return false;
  out.raw() = nc;
  return true;
}

inline bool SurfaceFromON(const ON_Surface& s, kernel::NurbsSurface& out) {
  ON_NurbsSurface ns;
  if (s.GetNurbForm(ns) <= 0) return false;
  out.raw() = ns;
  return true;
}

inline kernel::NurbsCurve PolylineCurve(const std::vector<Point3d>& pts) {
  ON_Polyline pl;
  for (const Point3d& p : pts) pl.Append(p);
  ON_PolylineCurve pc(pl);
  kernel::NurbsCurve out;
  CurveFromON(pc, out);
  return out;
}

inline ObjectId AddObject(CommandContext& ctx, SceneObject obj, const std::string& label) {
  ctx.Doc().BeginChange(label);
  return ctx.Doc().Add(std::move(obj));
}

inline ObjectId AddCurve(CommandContext& ctx, const kernel::NurbsCurve& c, const std::string& label) {
  return AddObject(ctx, SceneObject::MakeCurve(c), label);
}

// Best-effort closed mesh for an object (for booleans, volume, export).
inline std::optional<kernel::Mesh> MeshOf(const SceneObject& o, double tol = 0.01) {
  switch (o.kind) {
    case ObjectKind::Mesh: if (o.mesh) return *o.mesh; break;
    case ObjectKind::Brep: if (o.brep) { BrepMeshOptions opt; opt.chord_tolerance = tol; return MeshBrepClosed(o.brep->raw(), opt); } break;
    case ObjectKind::Surface: if (o.surface) return o.surface->TessellateGridAdaptive(tol); break;
    case ObjectKind::SubD: if (o.subd) return o.subd->ToApproximateMesh(); break;
    default: break;
  }
  return std::nullopt;
}

// Plane of the active viewport's construction plane.
inline ON_Plane ActivePlane(CommandContext& ctx) {
  if (Viewport* vp = ctx.ActiveViewport()) {
    const ConstructionPlane& cp = vp->CPlane();
    return ON_Plane(cp.origin, cp.x_axis, cp.y_axis);
  }
  return ON_Plane(ON_origin, ON_xaxis, ON_yaxis);
}

inline Vector3d ActiveNormal(CommandContext& ctx) { return ActivePlane(ctx).zaxis; }

inline std::vector<Point3d> CirclePoints(const ON_Circle& c, int n = 64) {
  std::vector<Point3d> pts;
  for (int i = 0; i <= n; ++i) pts.push_back(c.PointAt(2 * ON_PI * i / n));
  return pts;
}

// A command driven by a fixed list of point prompts, with live preview.
class PointsCommand : public Command {
 public:
  using Preview = std::function<void(CommandContext&, const std::vector<Point3d>&, Point3d hover)>;
  using Build = std::function<void(CommandContext&, const std::vector<Point3d>&)>;
  PointsCommand(std::vector<std::string> prompts, Build build, Preview preview = nullptr)
      : prompts_(std::move(prompts)), build_(std::move(build)), preview_(std::move(preview)) {}

  void Begin(CommandContext&) override { WantPoint(prompts_.front()); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(p);
    ctx.SetLastPoint(p);
    if (pts_.size() >= prompts_.size()) {
      ctx.ClearPreview();
      build_(ctx, pts_);
      Finish();
    } else {
      WantPoint(prompts_[pts_.size()]);
    }
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!preview_ || pts_.empty()) return;
    ctx.ClearPreview();
    preview_(ctx, pts_, h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 protected:
  std::vector<std::string> prompts_;
  std::vector<Point3d> pts_;
  Build build_;
  Preview preview_;
};

// A command that asks for a base point then a number (radius/height...), or
// accepts a second point whose distance from the base is used.
class PointThenDistanceCommand : public Command {
 public:
  using Build = std::function<void(CommandContext&, Point3d, double, Point3d)>;
  PointThenDistanceCommand(std::string p1, std::string p2, Build build, double default_value = 0)
      : p1_(std::move(p1)), p2_(std::move(p2)), build_(std::move(build)), default_(default_value) {}
  void Begin(CommandContext&) override { WantPoint(p1_); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!base_) {
      base_ = p;
      ctx.SetLastPoint(p);
      WantPoint(p2_ + (default_ > 0 ? " <" + FormatNumber(default_) + ">" : ""));
      return;
    }
    ctx.ClearPreview();
    build_(ctx, *base_, (p - *base_).Length(), p);
    Finish();
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!base_) return;
    ctx.ClearPreview();
    Point3d dir = *base_ + Vector3d(v, 0, 0);
    build_(ctx, *base_, std::fabs(v), dir);
    Finish();
  }
  void OnEnter(CommandContext& ctx) override {
    if (base_ && default_ > 0) OnNumber(ctx, default_);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    char* end = nullptr;
    double v = std::strtod(t.c_str(), &end);
    if (end && *end == 0) OnNumber(ctx, v);
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (!base_) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(*base_, h);
    if (preview_) preview_(ctx, *base_, (h - *base_).Length(), h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  Build preview_;

 protected:
  std::string p1_, p2_;
  std::optional<Point3d> base_;
  Build build_;
  double default_;
};

// Makes face orientations consistent across shared edges (breadth-first from
// each unvisited face), so a topologically closed brep also passes
// ON_Brep::IsSolid()'s orientation test. Returns the number of faces flipped.
inline int OrientBrepFaces(ON_Brep& b) {
  std::vector<char> done(static_cast<size_t>(b.m_F.Count()), 0);
  int flipped = 0;
  for (int seed = 0; seed < b.m_F.Count(); ++seed) {
    if (done[static_cast<size_t>(seed)] || b.m_F[seed].m_face_index < 0) continue;
    std::vector<int> queue = {seed};
    done[static_cast<size_t>(seed)] = 1;
    while (!queue.empty()) {
      const int fi = queue.back();
      queue.pop_back();
      const ON_BrepFace& f = b.m_F[fi];
      for (int li = 0; li < f.m_li.Count(); ++li) {
        const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
        for (int k = 0; k < loop.m_ti.Count(); ++k) {
          const int ti = loop.m_ti[k];
          const ON_BrepTrim& t = b.m_T[ti];
          if (t.m_ei < 0) continue;
          const ON_BrepEdge& e = b.m_E[t.m_ei];
          if (e.m_ti.Count() != 2) continue;
          const int oti = e.m_ti[0] == ti ? e.m_ti[1] : e.m_ti[0];
          const ON_BrepTrim& ot = b.m_T[oti];
          const int ofi = ot.FaceIndexOf();
          if (ofi < 0 || done[static_cast<size_t>(ofi)]) continue;
          ON_BrepFace& of = b.m_F[ofi];
          // Each face must traverse the shared edge in the opposite direction
          // (a clockwise loop, e.g. from ON_BrepTrimmedPlane on a clockwise
          // boundary, walks its edges the other way round).
          const bool cw0 = b.LoopDirection(loop) < 0, cw1 = b.LoopDirection(b.m_L[ot.m_li]) < 0;
          const bool d0 = (t.m_bRev3d != f.m_bRev) != cw0, d1 = (ot.m_bRev3d != of.m_bRev) != cw1;
          if (d0 == d1) { b.FlipFace(of); ++flipped; }
          done[static_cast<size_t>(ofi)] = 1;
          queue.push_back(ofi);
        }
      }
    }
  }
  return flipped;
}

// Joins coincident naked edges of a brep (OpenNURBS ships no JoinEdges) and
// orients the faces consistently afterwards.
inline int JoinNakedEdges(ON_Brep& b, double tol) {
  int joined = 0;
  for (int i = 0; i < b.m_E.Count(); ++i) {
    ON_BrepEdge& e0 = b.m_E[i];
    if (e0.m_edge_index < 0 || e0.TrimCount() != 1) continue;
    ON_3dPoint a0 = e0.PointAtStart(), a1 = e0.PointAtEnd();
    for (int j = i + 1; j < b.m_E.Count(); ++j) {
      ON_BrepEdge& e1 = b.m_E[j];
      if (e1.m_edge_index < 0 || e1.TrimCount() != 1) continue;
      ON_3dPoint b0 = e1.PointAtStart(), b1 = e1.PointAtEnd();
      bool forward = a0.DistanceTo(b0) <= tol && a1.DistanceTo(b1) <= tol;
      bool reversed = !forward && a0.DistanceTo(b1) <= tol && a1.DistanceTo(b0) <= tol;
      if (forward && a0.DistanceTo(a1) <= tol) {
        // Closed edges: the endpoints say nothing about direction; compare tangents.
        forward = ON_DotProduct(e0.TangentAt(e0.Domain().Min()), e1.TangentAt(e1.Domain().Min())) > 0;
        reversed = !forward;
      }
      if (!forward && !reversed) continue;
      ON_3dPoint m0 = e0.PointAt(e0.Domain().Mid()), m1 = e1.PointAt(e1.Domain().Mid());
      if (m0.DistanceTo(m1) > tol * 10) continue;
      if (reversed && !e1.Reverse()) continue;
      // CombineCoincidentEdges needs the two edges to share their vertices.
      for (int k = 0; k < 2; ++k) {
        if (e0.m_vi[k] == e1.m_vi[k]) continue;
        if (!b.CombineCoincidentVertices(b.m_V[e0.m_vi[k]], b.m_V[e1.m_vi[k]])) break;
      }
      if (b.CombineCoincidentEdges(e0, e1)) { ++joined; break; }
    }
  }
  if (joined) {
    OrientBrepFaces(b);
    b.SetTolerancesBoxesAndFlags();
  }
  return joined;
}

inline void Reg(CommandEngine& e, const char* name, CommandFactory f, CommandStatus s = CommandStatus::Implemented,
                const char* note = "") {
  e.Register(name, std::move(f), s, note);
}

template <typename T, typename... Args>
CommandFactory Make(Args... args) {
  return [=]() -> std::unique_ptr<Command> { return std::make_unique<T>(args...); };
}

inline CommandFactory Immediate(std::function<void(CommandContext&)> fn) {
  return [fn]() -> std::unique_ptr<Command> { return std::make_unique<ImmediateCommand>(fn); };
}

inline CommandFactory OnSelection(const std::string& prompt,
                                  std::function<void(CommandContext&, const std::vector<ObjectId>&)> fn, int min = 1) {
  return [=]() -> std::unique_ptr<Command> { return std::make_unique<SelectThenActCommand>(prompt, fn, min); };
}

}  // namespace dino8::app
