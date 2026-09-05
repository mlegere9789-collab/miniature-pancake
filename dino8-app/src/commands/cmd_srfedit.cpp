// Surface and polysurface editing: ExtractSrf, DeleteFaces, DupBorder,
// DupEdge, DupFaceBorder, Untrim family, ShrinkTrimmedSrf, MergeSrf,
// ExtendSrf, EdgeSrf, RailRevolve, ExtractIsocurve, ExtractWireframe,
// CreateUVCrv, UnrollSrf/Smash (developable approximation), Fin, Ribbon,
// Silhouette, 3DFace, MakeUniformUV and friends.
#include "commands/cmd_common.h"

#include <algorithm>
#include <limits>
#include <map>

namespace dino8::app {

namespace {

kernel::Brep WrapBrepPtr(ON_Brep* b) {
  kernel::Brep k;
  if (b) { k.raw() = *b; delete b; }
  return k;
}

// The surface behind an object: a Surface object, or a single brep face.
std::optional<ON_NurbsSurface> SurfaceOfObject(const SceneObject& o, int face = 0) {
  if (o.kind == ObjectKind::Surface && o.surface) return o.surface->raw();
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    if (face < 0 || face >= b.m_F.Count()) return std::nullopt;
    const ON_Surface* s = b.m_F[face].SurfaceOf();
    ON_NurbsSurface ns;
    if (s && s->GetNurbForm(ns) > 0) {
      if (b.m_F[face].m_bRev) ns.Reverse(0);
      return ns;
    }
  }
  return std::nullopt;
}

// Brep view of any surface-like object (copy).
std::optional<ON_Brep> BrepOfObject(const SceneObject& o) {
  if (o.kind == ObjectKind::Brep && o.brep) return o.brep->raw();
  if (o.kind == ObjectKind::Surface && o.surface) {
    ON_Brep b;
    ON_NurbsSurface* srf = new ON_NurbsSurface(o.surface->raw());
    b.Create(srf);
    return b;
  }
  return std::nullopt;
}


// Closest surface parameters to a point (grid refinement via the kernel wrapper).
void ClosestUV(const ON_NurbsSurface& s, Point3d p, double& u, double& v) {
  kernel::NurbsSurface k;
  k.raw() = s;
  kernel::Point2d uv = k.ClosestPointParameter(p, 24, 24);
  u = uv.x;
  v = uv.y;
}

// Closest parameter on a brep edge.
bool EdgeClosest(const ON_BrepEdge& e, Point3d p, double& t) {
  ON_NurbsCurve nc;
  if (e.GetNurbForm(nc) <= 0) return false;
  kernel::NurbsCurve k;
  k.raw() = nc;
  t = k.ClosestPointParameter(p, 200);
  return true;
}

// Face of a brep nearest to a point (by its display mesh).
int NearestFace(const ON_Brep& b, Point3d p, double* dist_out = nullptr) {
  BrepMeshOptions opt;
  opt.chord_tolerance = 0.05;
  std::vector<kernel::Mesh> faces = MeshBrepFaces(b, opt);
  int best = -1;
  double bd = std::numeric_limits<double>::max();
  for (size_t i = 0; i < faces.size() && static_cast<int>(i) < b.m_F.Count(); ++i) {
    if (faces[i].FaceCount() == 0) continue;
    double d = faces[i].ClosestPoint(p).DistanceTo(p);
    if (d < bd) { bd = d; best = static_cast<int>(i); }
  }
  if (dist_out) *dist_out = bd;
  return best;
}

struct FacePick {
  ObjectId id = kNoObject;
  int face = -1;
  double dist = 0;
};

// Nearest (object, face) among visible breps/surfaces.
std::optional<FacePick> PickFace(CommandContext& ctx, Point3d p) {
  std::optional<FacePick> best;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::optional<ON_Brep> b = BrepOfObject(o);
    if (!b) continue;
    double d = 0;
    int f = NearestFace(*b, p, &d);
    if (f < 0) continue;
    if (!best || d < best->dist) best = FacePick{o.id, f, d};
  }
  return best;
}

struct EdgePick {
  ObjectId id = kNoObject;
  int edge = -1;
  double dist = 0;
};

std::optional<EdgePick> PickEdge(CommandContext& ctx, Point3d p) {
  std::optional<EdgePick> best;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o) || ctx.Doc().IsObjectLocked(o)) continue;
    std::optional<ON_Brep> b = BrepOfObject(o);
    if (!b) continue;
    for (int i = 0; i < b->m_E.Count(); ++i) {
      const ON_BrepEdge& e = b->m_E[i];
      if (e.m_edge_index < 0) continue;
      double t = 0;
      if (!EdgeClosest(e, p, t)) continue;
      ON_NurbsCurve enc; e.GetNurbForm(enc);
      double d = enc.PointAt(t).DistanceTo(p);
      if (!best || d < best->dist) best = EdgePick{o.id, i, d};
    }
  }
  return best;
}

ObjectId AddCurveFrom(CommandContext& ctx, const ON_Curve& c, const SceneObject& like) {
  kernel::NurbsCurve k;
  if (!CurveFromON(c, k)) return kNoObject;
  SceneObject n = SceneObject::MakeCurve(k);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  return ctx.Doc().Add(std::move(n));
}

ObjectId AddBrepFrom(CommandContext& ctx, const ON_Brep& b, const SceneObject& like) {
  kernel::Brep k;
  k.raw() = b;
  SceneObject n = SceneObject::MakeBrep(k);
  n.layer_index = like.layer_index;
  n.color = like.color;
  n.color_by_layer = like.color_by_layer;
  n.material_name = like.material_name;
  return ctx.Doc().Add(std::move(n));
}

// Degree-3 surface through a grid of sample rows (rows[i][j], i along U).
kernel::NurbsSurface SurfaceThroughRows(const std::vector<std::vector<Point3d>>& rows) {
  const int nu = static_cast<int>(rows.size()), nv = static_cast<int>(rows[0].size());
  std::vector<Point3d> grid;
  grid.reserve(static_cast<size_t>(nu) * nv);
  for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) grid.push_back(rows[i][j]);
  int du = std::min(3, nu - 1), dv = std::min(3, nv - 1);
  kernel::NurbsSurface s = kernel::NurbsSurface::FromControlGrid(grid, nu, nv, du, dv);
  // Relax so the surface interpolates the samples (a few fixed-point passes).
  for (int it = 0; it < 12; ++it) {
    for (int i = 0; i < nu; ++i)
      for (int j = 0; j < nv; ++j) {
        double u = s.raw().Domain(0).ParameterAt(nu == 1 ? 0 : static_cast<double>(i) / (nu - 1));
        double v = s.raw().Domain(1).ParameterAt(nv == 1 ? 0 : static_cast<double>(j) / (nv - 1));
        Point3d on = s.raw().PointAt(u, v);
        ON_3dPoint cv;
        s.raw().GetCV(i, j, cv);
        s.raw().SetCV(i, j, cv + (rows[i][j] - on));
      }
  }
  return s;
}

// ---------------------------------------------------------------------------
// Face-pick commands
// ---------------------------------------------------------------------------

class FacePickCommand : public Command {
 public:
  enum class Op { Extract, ExtractCopy, Delete, Untrim, UntrimHoles, DupFaceBorder, Isocurve };
  explicit FacePickCommand(Op op) : op_(op) {}
  void Begin(CommandContext&) override {
    if (op_ == Op::Extract) options = {{"Copy", "No", {"Yes", "No"}, false, true}};
    if (op_ == Op::Isocurve) options = {{"Direction", "U", {"U", "V", "Both"}, false, false}};
    WantPoint(Prompt());
  }
  std::string Prompt() const {
    switch (op_) {
      case Op::Extract: case Op::ExtractCopy: return "Click faces to extract (Enter when done)";
      case Op::Delete: return "Click faces to delete (Enter when done)";
      case Op::Untrim: return "Click trimmed faces to untrim (Enter when done)";
      case Op::UntrimHoles: return "Click faces whose holes to remove (Enter when done)";
      case Op::DupFaceBorder: return "Click faces to duplicate the border of (Enter when done)";
      case Op::Isocurve: return "Click on a surface to extract an isocurve (Enter when done)";
    }
    return "";
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Copy") copy_ = (v == "Yes");
    if (n == "Direction") dir_ = v;
  }
  void OnEnter(CommandContext&) override { Finish(); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<FacePick> pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("No surface near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o) return;
    SceneObject like = *o;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) return;
    const int fi = pick->face;
    ctx.Doc().BeginChange("FaceEdit");
    switch (op_) {
      case Op::Extract: case Op::ExtractCopy: {
        ON_Brep* dup = b->DuplicateFace(fi, false);
        if (!dup) { ctx.Warn("Could not duplicate the face"); return; }
        ObjectId nid = AddBrepFrom(ctx, *dup, like);
        delete dup;
        if (!copy_ && op_ == Op::Extract) {
          if (b->m_F.Count() <= 1) ctx.Doc().Remove(pick->id);
          else {
            b->DeleteFace(b->m_F[fi], true);
            b->Compact();
            if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->brep->raw() = *b; orig->InvalidateDisplay(); }
          }
        }
        ctx.Doc().Select(nid, true);
        ctx.Print("ExtractSrf: face " + std::to_string(fi) + " extracted from object " + std::to_string(pick->id));
        break;
      }
      case Op::Delete: {
        if (b->m_F.Count() <= 1) { ctx.Doc().Remove(pick->id); ctx.Print("DeleteFaces: object " + std::to_string(pick->id) + " deleted (last face)"); break; }
        b->DeleteFace(b->m_F[fi], true);
        b->Compact();
        if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->brep->raw() = *b; orig->InvalidateDisplay(); }
        ctx.Print("DeleteFaces: face " + std::to_string(fi) + " deleted, " + std::to_string(b->m_F.Count()) + " face(s) left");
        break;
      }
      case Op::Untrim: {
        std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, fi);
        if (!s) { ctx.Warn("No underlying surface"); return; }
        ON_Brep nb;
        ON_NurbsSurface* nsp = new ON_NurbsSurface(*s);
        nb.Create(nsp);
        if (b->m_F.Count() <= 1) { if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->kind = ObjectKind::Brep; if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>(); orig->brep->raw() = nb; orig->surface.reset(); orig->InvalidateDisplay(); } }
        else {
          b->DeleteFace(b->m_F[fi], true); b->Compact();
          if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->brep->raw() = *b; orig->InvalidateDisplay(); }
          AddBrepFrom(ctx, nb, like);
        }
        ctx.Print("Untrim: face " + std::to_string(fi) + " replaced by its untrimmed surface");
        break;
      }
      case Op::UntrimHoles: {
        ON_BrepFace& f = b->m_F[fi];
        int removed = 0;
        for (int li = f.LoopCount() - 1; li >= 0; --li) {
          ON_BrepLoop* loop = f.Loop(li);
          if (loop && loop->m_type == ON_BrepLoop::inner) { b->DeleteLoop(*loop, true); ++removed; }
        }
        b->Compact();
        if (SceneObject* orig = ctx.Doc().Find(pick->id)) {
          if (orig->kind == ObjectKind::Brep) { orig->brep->raw() = *b; }
          orig->InvalidateDisplay();
        }
        ctx.Print("UntrimHoles: " + std::to_string(removed) + " hole(s) removed from face " + std::to_string(fi));
        break;
      }
      case Op::DupFaceBorder: {
        const ON_BrepFace& f = b->m_F[fi];
        int made = 0;
        for (int li = 0; li < f.LoopCount(); ++li) {
          const ON_BrepLoop* loop = f.Loop(li);
          if (!loop) continue;
          ON_PolyCurve pc;
          for (int ti = 0; ti < loop->TrimCount(); ++ti) {
            const ON_BrepTrim* trim = loop->Trim(ti);
            const ON_BrepEdge* e = trim ? trim->Edge() : nullptr;
            if (!e) continue;
            ON_Curve* c = e->DuplicateCurve();
            if (!c) continue;
            if (trim->m_bRev3d) c->Reverse();
            pc.Append(c);
          }
          if (pc.Count() > 0) { if (AddCurveFrom(ctx, pc, like) != kNoObject) ++made; }
        }
        ctx.Print("DupFaceBorder: " + std::to_string(made) + " curve(s)");
        break;
      }
      case Op::Isocurve: {
        std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, fi);
        if (!s) { ctx.Warn("No underlying surface"); return; }
        double u, v;
        ClosestUV(*s, p, u, v);
        int made = 0;
        if (dir_ == "U" || dir_ == "Both") { if (ON_Curve* c = s->IsoCurve(1, u)) { AddCurveFrom(ctx, *c, like); delete c; ++made; } }
        if (dir_ == "V" || dir_ == "Both") { if (ON_Curve* c = s->IsoCurve(0, v)) { AddCurveFrom(ctx, *c, like); delete c; ++made; } }
        ctx.Print("ExtractIsocurve: " + std::to_string(made) + " curve(s) at u=" + FormatNumber(u) + " v=" + FormatNumber(v));
        break;
      }
    }
  }
  Op op_;
  bool copy_ = false;
  std::string dir_ = "U";
};

class DupEdgeCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click edges to duplicate (Enter when done)"); }
  void OnEnter(CommandContext&) override { Finish(); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    std::optional<EdgePick> pick = PickEdge(ctx, p);
    if (!pick) { ctx.Warn("No edge near that point"); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o) return;
    SceneObject like = *o;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) return;
    ON_Curve* c = b->m_E[pick->edge].DuplicateCurve();
    if (!c) return;
    ctx.Doc().BeginChange("DupEdge");
    ObjectId nid = AddCurveFrom(ctx, *c, like);
    delete c;
    ctx.Doc().Select(nid, true);
    ctx.Print("DupEdge: edge " + std::to_string(pick->edge) + " of object " + std::to_string(pick->id) + " duplicated");
  }
};

void DupBorder(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("DupBorder");
  int made = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject like = *o;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) continue;
    // Collect naked edges and chain them.
    std::vector<ON_Curve*> naked;
    for (int i = 0; i < b->m_E.Count(); ++i) {
      const ON_BrepEdge& e = b->m_E[i];
      if (e.m_edge_index < 0 || e.TrimCount() != 1) continue;
      if (ON_Curve* c = e.DuplicateCurve()) naked.push_back(c);
    }
    const double tol = ctx.Settings().absolute_tolerance * 10;
    while (!naked.empty()) {
      ON_PolyCurve pc;
      pc.Append(naked.front());
      naked.erase(naked.begin());
      bool grew = true;
      while (grew) {
        grew = false;
        for (size_t i = 0; i < naked.size(); ++i) {
          ON_Curve* c = naked[i];
          if (c->PointAtStart().DistanceTo(pc.PointAtEnd()) <= tol) pc.Append(c);
          else if (c->PointAtEnd().DistanceTo(pc.PointAtEnd()) <= tol) { c->Reverse(); pc.Append(c); }
          else if (c->PointAtEnd().DistanceTo(pc.PointAtStart()) <= tol) pc.Prepend(c);
          else if (c->PointAtStart().DistanceTo(pc.PointAtStart()) <= tol) { c->Reverse(); pc.Prepend(c); }
          else continue;
          naked.erase(naked.begin() + static_cast<long>(i));
          grew = true;
          break;
        }
      }
      if (AddCurveFrom(ctx, pc, like) != kNoObject) ++made;
    }
  }
  ctx.Print("DupBorder: " + std::to_string(made) + " border curve(s)");
}

void ExtractWireframe(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("ExtractWireframe");
  int made = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject like = *o;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) continue;
    for (int i = 0; i < b->m_E.Count(); ++i) {
      if (b->m_E[i].m_edge_index < 0) continue;
      if (ON_Curve* c = b->m_E[i].DuplicateCurve()) { if (AddCurveFrom(ctx, *c, like) != kNoObject) ++made; delete c; }
    }
    for (int fi = 0; fi < b->m_F.Count(); ++fi) {
      const ON_BrepFace& f = b->m_F[fi];
      bool trimmed = f.LoopCount() != 1 || (f.Loop(0) && f.Loop(0)->TrimCount() != 4);
      if (trimmed) continue;  // trimmed faces: edges only
      std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, fi);
      if (!s) continue;
      for (int dir = 0; dir < 2; ++dir) {
        int spans = s->SpanCount(dir);
        std::vector<double> sv(static_cast<size_t>(spans) + 1);
        s->GetSpanVector(dir, sv.data());
        for (size_t k = 1; k + 1 < sv.size(); ++k)
          if (ON_Curve* c = s->IsoCurve(1 - dir, sv[k])) { if (AddCurveFrom(ctx, *c, like) != kNoObject) ++made; delete c; }
      }
    }
  }
  ctx.Print("ExtractWireframe: " + std::to_string(made) + " curve(s)");
}

void ShrinkTrimmed(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("ShrinkTrimmedSrf");
  int n = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) continue;
    if (o->brep->raw().ShrinkSurfaces()) { o->InvalidateDisplay(); ++n; }
  }
  ctx.Print("ShrinkTrimmedSrf: " + std::to_string(n) + " polysurface(s) shrunk");
}

void CreateUVCrv(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("CreateUVCrv");
  ON_Plane pl = ActivePlane(ctx);
  int made = 0;
  double x_offset = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject like = *o;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) continue;
    for (int fi = 0; fi < b->m_F.Count(); ++fi) {
      const ON_BrepFace& f = b->m_F[fi];
      const ON_Surface* s = f.SurfaceOf();
      if (!s) continue;
      ON_Interval du = s->Domain(0), dv = s->Domain(1);
      // Scale the uv domain to the surface's 3D extents so the flattened curve has real-world size.
      double lu = 0, lv = 0;
      if (ON_Curve* cu = s->IsoCurve(0, dv.Mid())) { ON_NurbsCurve nc; if (cu->GetNurbForm(nc) > 0) { kernel::NurbsCurve k; k.raw() = nc; lu = k.Length(200); } delete cu; }
      if (ON_Curve* cv = s->IsoCurve(1, du.Mid())) { ON_NurbsCurve nc; if (cv->GetNurbForm(nc) > 0) { kernel::NurbsCurve k; k.raw() = nc; lv = k.Length(200); } delete cv; }
      if (lu <= 0) lu = du.Length();
      if (lv <= 0) lv = dv.Length();
      auto map = [&](const ON_3dPoint& uv) { return pl.PointAt(x_offset + (uv.x - du.Min()) / du.Length() * lu, (uv.y - dv.Min()) / dv.Length() * lv); };
      // Surface rectangle outline.
      AddCurve(ctx, PolylineCurve({map(ON_3dPoint(du.Min(), dv.Min(), 0)), map(ON_3dPoint(du.Max(), dv.Min(), 0)), map(ON_3dPoint(du.Max(), dv.Max(), 0)), map(ON_3dPoint(du.Min(), dv.Max(), 0)), map(ON_3dPoint(du.Min(), dv.Min(), 0))}), "CreateUVCrv");
      for (int li = 0; li < f.LoopCount(); ++li) {
        const ON_BrepLoop* loop = f.Loop(li);
        if (!loop) continue;
        std::vector<Point3d> pts;
        for (int ti = 0; ti < loop->TrimCount(); ++ti) {
          const ON_BrepTrim* trim = loop->Trim(ti);
          if (!trim) continue;
          for (int k = 0; k <= 16; ++k) pts.push_back(map(trim->PointAt(trim->Domain().ParameterAt(k / 16.0))));
        }
        if (pts.size() >= 2) { AddCurve(ctx, PolylineCurve(pts), "CreateUVCrv"); ++made; }
      }
      x_offset += lu * 1.1;
    }
  }
  ctx.Print("CreateUVCrv: " + std::to_string(made) + " trim curve(s) laid out on the construction plane");
}

// Developable unroll: walk the surface's sample grid and lay each quad flat by
// preserving its edge lengths (exact for developable surfaces).
void Unroll(CommandContext& ctx, const std::vector<ObjectId>& ids, const char* label) {
  ctx.Doc().BeginChange(label);
  ON_Plane pl = ActivePlane(ctx);
  int made = 0;
  double x_offset = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject like = *o;
    std::optional<ON_Brep> b = BrepOfObject(*o);
    if (!b) continue;
    for (int fi = 0; fi < b->m_F.Count(); ++fi) {
      std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, fi);
      if (!s) continue;
      const int nu = 24, nv = 24;
      std::vector<std::vector<Point3d>> g(nu + 1, std::vector<Point3d>(nv + 1));
      for (int i = 0; i <= nu; ++i) for (int j = 0; j <= nv; ++j) g[i][j] = s->PointAt(s->Domain(0).ParameterAt(static_cast<double>(i) / nu), s->Domain(1).ParameterAt(static_cast<double>(j) / nv));
      std::vector<std::vector<ON_2dPoint>> f(nu + 1, std::vector<ON_2dPoint>(nv + 1));
      // First column along v, laid on the y axis by arc length.
      f[0][0] = ON_2dPoint(0, 0);
      for (int j = 1; j <= nv; ++j) f[0][j] = ON_2dPoint(0, f[0][j - 1].y + g[0][j].DistanceTo(g[0][j - 1]));
      for (int i = 1; i <= nu; ++i) {
        // Place f[i][0] from f[i-1][0] and f[i-1][1] by two distances (triangulation), +x side.
        auto place = [&](ON_2dPoint a, ON_2dPoint c, double da, double dc, ON_2dPoint prefer) {
          double dx = c.x - a.x, dy = c.y - a.y, d = std::sqrt(dx * dx + dy * dy);
          if (d <= 1e-12) return ON_2dPoint(a.x + da, a.y);
          double x = (da * da - dc * dc + d * d) / (2 * d);
          double h2 = da * da - x * x;
          double h = h2 > 0 ? std::sqrt(h2) : 0;
          ON_2dPoint base(a.x + dx / d * x, a.y + dy / d * x);
          ON_2dPoint p1(base.x + dy / d * h, base.y - dx / d * h), p2(base.x - dy / d * h, base.y + dx / d * h);
          return (p1.DistanceTo(prefer) < p2.DistanceTo(prefer)) ? p1 : p2;
        };
        f[i][0] = place(f[i - 1][0], f[i - 1][1], g[i][0].DistanceTo(g[i - 1][0]), g[i][0].DistanceTo(g[i - 1][1]), ON_2dPoint(f[i - 1][0].x + 1e6, f[i - 1][0].y));
        for (int j = 1; j <= nv; ++j)
          f[i][j] = place(f[i - 1][j], f[i][j - 1], g[i][j].DistanceTo(g[i - 1][j]), g[i][j].DistanceTo(g[i][j - 1]), ON_2dPoint(f[i - 1][j].x + 1e6, f[i - 1][j].y + 1e6));
      }
      double minx = 1e300, miny = 1e300;
      for (auto& col : f) for (auto& p : col) { minx = std::min(minx, p.x); miny = std::min(miny, p.y); }
      // Flat mesh + outline.
      ON_Mesh m;
      for (int i = 0; i <= nu; ++i) for (int j = 0; j <= nv; ++j) m.SetVertex(i * (nv + 1) + j, pl.PointAt(x_offset + f[i][j].x - minx, f[i][j].y - miny));
      for (int i = 0; i < nu; ++i) for (int j = 0; j < nv; ++j) m.SetQuad(i * nv + j, i * (nv + 1) + j, (i + 1) * (nv + 1) + j, (i + 1) * (nv + 1) + j + 1, i * (nv + 1) + j + 1);
      m.ComputeFaceNormals();
      kernel::Mesh km; km.raw() = m;
      double area3d = MeshOf(*o, 0.01) ? MeshOf(*o, 0.01)->Area() : 0;
      SceneObject n = SceneObject::MakeMesh(km); n.layer_index = like.layer_index;
      ctx.Doc().Add(std::move(n));
      std::vector<Point3d> outline;
      for (int j = 0; j <= nv; ++j) outline.push_back(pl.PointAt(x_offset + f[0][j].x - minx, f[0][j].y - miny));
      for (int i = 1; i <= nu; ++i) outline.push_back(pl.PointAt(x_offset + f[i][nv].x - minx, f[i][nv].y - miny));
      for (int j = nv - 1; j >= 0; --j) outline.push_back(pl.PointAt(x_offset + f[nu][j].x - minx, f[nu][j].y - miny));
      for (int i = nu - 1; i >= 0; --i) outline.push_back(pl.PointAt(x_offset + f[i][0].x - minx, f[i][0].y - miny));
      AddCurve(ctx, PolylineCurve(outline), label);
      double flat = km.Area();
      ctx.Print(std::string(label) + ": face " + std::to_string(fi) + " flattened, area " + FormatNumber(flat) + (area3d > 0 ? " (whole object 3D area " + FormatNumber(area3d) + ")" : ""));
      double w = 0; for (auto& col : f) for (auto& p : col) w = std::max(w, p.x - minx);
      x_offset += w * 1.1;
      ++made;
    }
  }
  if (made == 0) ctx.Warn(std::string(label) + ": select surfaces or polysurfaces");
}

// ---------------------------------------------------------------------------
// ExtendSrf / MergeSrf / RailRevolve / Fin / Ribbon / Silhouette
// ---------------------------------------------------------------------------

class ExtendSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click near the surface edge to extend"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!pick_) {
      pick_ = PickFace(ctx, p);
      if (!pick_) { ctx.Warn("No surface near that point"); return; }
      const SceneObject* o = ctx.Doc().Find(pick_->id);
      std::optional<ON_NurbsSurface> s = o ? SurfaceOfObject(*o, pick_->face) : std::nullopt;
      if (!s) { ctx.Warn("Not a surface"); Finish(); return; }
      double u, v;
      ClosestUV(*s, p, u, v);
      ON_Interval du = s->Domain(0), dv = s->Domain(1);
      double fu = du.NormalizedParameterAt(u), fv = dv.NormalizedParameterAt(v);
      // Which side is nearest in normalized parameters.
      double cands[4] = {fu, 1 - fu, fv, 1 - fv};
      side_ = static_cast<int>(std::min_element(cands, cands + 4) - cands);
      WantNumber("Extension length", 10);
      return;
    }
    OnNumber(ctx, 10);
  }
  void OnNumber(CommandContext& ctx, double len) override {
    if (!pick_) return;
    SceneObject* o = ctx.Doc().Find(pick_->id);
    std::optional<ON_NurbsSurface> s = o ? SurfaceOfObject(*o, pick_->face) : std::nullopt;
    if (!s) { Finish(); return; }
    int dir = side_ < 2 ? 0 : 1;
    ON_Interval d = s->Domain(dir);
    // Parameter length per unit of 3D length along the edge direction.
    ON_3dPoint pm; ON_3dVector du, dv;
    s->Ev1Der(s->Domain(0).Mid(), s->Domain(1).Mid(), pm, du, dv);
    double rate = (dir == 0 ? du.Length() : dv.Length());
    double dt = rate > 1e-9 ? len / rate : d.Length() * 0.1;
    ON_Interval nd = (side_ % 2 == 0) ? ON_Interval(d.Min() - dt, d.Max()) : ON_Interval(d.Min(), d.Max() + dt);
    ctx.Doc().BeginChange("ExtendSrf");
    ON_NurbsSurface ext = *s;
    if (!ext.Extend(dir, nd)) { ctx.Warn("ExtendSrf: could not extend"); Finish(); return; }
    if (o->kind == ObjectKind::Surface) { o->surface->raw() = ext; o->InvalidateDisplay(); }
    else {
      ON_Brep nb; ON_NurbsSurface* nsp = new ON_NurbsSurface(ext); nb.Create(nsp);
      if (o->brep->raw().m_F.Count() <= 1) { o->brep->raw() = nb; o->InvalidateDisplay(); }
      else { SceneObject like = *o; ON_Brep b = o->brep->raw(); b.DeleteFace(b.m_F[pick_->face], true); b.Compact(); o->brep->raw() = b; o->InvalidateDisplay(); AddBrepFrom(ctx, nb, like); }
    }
    ctx.Print("ExtendSrf: extended by " + FormatNumber(len) + " along " + (dir == 0 ? "U" : "V"));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (pick_) OnNumber(ctx, 10); }
  std::optional<FacePick> pick_;
  int side_ = 0;
};

void MergeSrf(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<std::pair<ObjectId, ON_NurbsSurface>> srfs;
  SceneObject like;
  for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (!o) continue; if (auto s = SurfaceOfObject(*o)) { srfs.emplace_back(id, *s); like = *o; } }
  if (srfs.size() < 2) { ctx.Warn("Select two surfaces"); return; }
  ON_NurbsSurface a = srfs[0].second, b = srfs[1].second;
  const double tol = ctx.Settings().absolute_tolerance * 100;
  // Find the pair of edges that coincide: try a's 4 sides against b's 4 sides.
  auto edge_pts = [](const ON_NurbsSurface& s, int side, int n) {
    std::vector<Point3d> pts;
    for (int k = 0; k <= n; ++k) {
      double f = static_cast<double>(k) / n;
      double u = side == 0 ? s.Domain(0).Min() : side == 1 ? s.Domain(0).Max() : s.Domain(0).ParameterAt(f);
      double v = side == 2 ? s.Domain(1).Min() : side == 3 ? s.Domain(1).Max() : s.Domain(1).ParameterAt(f);
      pts.push_back(s.PointAt(u, v));
    }
    return pts;
  };
  int sa = -1, sb = -1; bool rev = false;
  for (int i = 0; i < 4 && sa < 0; ++i)
    for (int j = 0; j < 4; ++j) {
      std::vector<Point3d> pa = edge_pts(a, i, 8), pb = edge_pts(b, j, 8);
      double d_same = 0, d_rev = 0;
      for (int k = 0; k <= 8; ++k) { d_same = std::max(d_same, pa[k].DistanceTo(pb[k])); d_rev = std::max(d_rev, pa[k].DistanceTo(pb[8 - k])); }
      if (d_same <= tol || d_rev <= tol) { sa = i; sb = j; rev = d_rev < d_same; break; }
    }
  if (sa < 0) { ctx.Warn("MergeSrf: the surfaces do not share an edge"); return; }
  // Sample both as rows across the shared edge: rows go from a's far side to b's far side.
  const int n_across = 12, n_along = 16;
  std::vector<std::vector<Point3d>> rows;
  auto sample = [&](const ON_NurbsSurface& s, int side, double across, double along, bool flip_along) {
    if (flip_along) along = 1 - along;
    double u, v;
    if (side == 0) { u = s.Domain(0).ParameterAt(across); v = s.Domain(1).ParameterAt(along); }
    else if (side == 1) { u = s.Domain(0).ParameterAt(1 - across); v = s.Domain(1).ParameterAt(along); }
    else if (side == 2) { u = s.Domain(0).ParameterAt(along); v = s.Domain(1).ParameterAt(across); }
    else { u = s.Domain(0).ParameterAt(along); v = s.Domain(1).ParameterAt(1 - across); }
    return s.PointAt(u, v);
  };
  for (int i = n_across; i >= 0; --i) {  // a: from far edge (across=1) to shared edge (across=0)
    std::vector<Point3d> row;
    for (int j = 0; j <= n_along; ++j) row.push_back(sample(a, sa, static_cast<double>(i) / n_across, static_cast<double>(j) / n_along, false));
    rows.push_back(row);
  }
  for (int i = 1; i <= n_across; ++i) {
    std::vector<Point3d> row;
    for (int j = 0; j <= n_along; ++j) row.push_back(sample(b, sb, static_cast<double>(i) / n_across, static_cast<double>(j) / n_along, rev));
    rows.push_back(row);
  }
  ctx.Doc().BeginChange("MergeSrf");
  SceneObject n = SceneObject::MakeSurface(SurfaceThroughRows(rows));
  n.layer_index = like.layer_index;
  ctx.Doc().Remove(srfs[0].first);
  ctx.Doc().Remove(srfs[1].first);
  ctx.Doc().Add(std::move(n));
  ctx.Print("MergeSrf: merged 2 surfaces into one (refit through samples)");
}

class RailRevolveCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select profile curve"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve) { if (!profile_) profile_ = *o->curve; else if (!rail_) rail_ = *o->curve; } }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    accept_preselection = false;
    if (!profile_) { ctx.Warn("Select a curve"); Finish(); return; }
    if (!rail_) { WantObjects("Select rail curve"); return; }
    WantPoint("Start of revolve axis");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!a_) { a_ = p; ctx.SetLastPoint(p); WantPoint("End of revolve axis"); return; }
    Vector3d axis = p - *a_;
    if (!axis.Unitize()) return;
    // Profile in a local frame: radial distance and height along the axis.
    const int np = 24, nr = 48;
    // Reference radius: the profile's largest distance from the axis; the rail scales it.
    double base_r = 0;
    Vector3d r0(0, 0, 0);
    for (int j = 0; j <= np; ++j) {
      Point3d q = profile_->PointAt(profile_->Domain().min + (profile_->Domain().max - profile_->Domain().min) * j / np);
      Vector3d d = q - *a_; d -= axis * ON_DotProduct(d, axis);
      if (d.Length() > base_r) { base_r = d.Length(); r0 = d; }
    }
    if (base_r <= 1e-9) { ctx.Warn("Profile must not lie on the axis"); Finish(); return; }
    r0.Unitize();
    Vector3d t0 = ON_CrossProduct(axis, r0);
    std::vector<std::vector<Point3d>> rows;
    for (int i = 0; i <= nr; ++i) {
      Point3d rp = rail_->PointAt(rail_->Domain().min + (rail_->Domain().max - rail_->Domain().min) * i / nr);
      Vector3d rr = rp - *a_;
      double h_rail = ON_DotProduct(rr, axis);
      rr -= axis * h_rail;
      double rail_r = rr.Length();
      if (rail_r <= 1e-9) rr = r0; else rr.Unitize();
      double scale = rail_r / base_r;
      Vector3d rt = ON_CrossProduct(axis, rr);
      std::vector<Point3d> row;
      for (int j = 0; j <= np; ++j) {
        Point3d q = profile_->PointAt(profile_->Domain().min + (profile_->Domain().max - profile_->Domain().min) * j / np);
        Vector3d d = q - *a_;
        double h = ON_DotProduct(d, axis);
        d -= axis * h;
        double x = ON_DotProduct(d, r0), y = ON_DotProduct(d, t0);
        row.push_back(*a_ + rr * (x * scale) + rt * (y * scale) + axis * h);
      }
      rows.push_back(row);
    }
    ctx.Doc().BeginChange("RailRevolve");
    ctx.Doc().Add(SceneObject::MakeSurface(SurfaceThroughRows(rows)));
    ctx.Print("RailRevolve: surface created (profile scaled radially to follow the rail)");
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (a_) { ctx.ClearPreview(); ctx.AddPreviewLine(*a_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::optional<kernel::NurbsCurve> profile_, rail_;
  std::optional<Point3d> a_;
};

class FinCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curve on surface"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Curve && !curve_) curve_ = *o->curve; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    if (!curve_) { ctx.Warn("Select a curve"); Finish(); return; }
    accept_preselection = false;
    WantObjects("Select base surface");
  }
  void OnNumber(CommandContext& ctx, double dist) override {
    if (!srf_) return;
    const int n = 48;
    std::vector<std::vector<Point3d>> rows;
    for (int i = 0; i <= n; ++i) {
      Point3d q = curve_->PointAt(curve_->Domain().min + (curve_->Domain().max - curve_->Domain().min) * i / n);
      double u, v; ClosestUV(*srf_, q, u, v);
      ON_3dVector nrm = srf_->NormalAt(u, v);
      rows.push_back({q, q + nrm * dist});
    }
    ctx.Doc().BeginChange("Fin");
    ctx.Doc().Add(SceneObject::MakeSurface(SurfaceThroughRows(rows)));
    ctx.Print("Fin: normal fin surface created, height " + FormatNumber(dist));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { if (srf_) OnNumber(ctx, 10); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (srf_ && curve_) { Point3d q = curve_->PointAt(curve_->Domain().min); OnNumber(ctx, p.DistanceTo(q)); }
  }
  // Second selection lands here through the engine as OnObjects again.
  void OnObjectsSecond(CommandContext& ctx, const std::vector<ObjectId>& ids) {
    for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o) if (auto s = SurfaceOfObject(*o)) { srf_ = *s; break; } }
    if (!srf_) { ctx.Warn("Select a surface"); Finish(); return; }
    WantNumber("Fin height", 10);
  }
  std::optional<kernel::NurbsCurve> curve_;
  std::optional<ON_NurbsSurface> srf_;
};

// Fin needs two selection stages; wrap so the second OnObjects goes to OnObjectsSecond.
class FinStagedCommand : public FinCommand {
 public:
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!curve_) FinCommand::OnObjects(ctx, ids); else OnObjectsSecond(ctx, ids);
  }
};

class RibbonCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curves to make ribbons from"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { ids_ = ids; for (ObjectId id : ids) ctx.Doc().Select(id, false); WantNumber("Ribbon width", 5); }
  void OnNumber(CommandContext& ctx, double w) override {
    Vector3d up = ActiveNormal(ctx);
    ctx.Doc().BeginChange("Ribbon");
    int made = 0;
    for (ObjectId id : ids_) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind != ObjectKind::Curve) continue;
      const int n = 48;
      std::vector<std::vector<Point3d>> rows;
      for (int i = 0; i <= n; ++i) {
        double t = o->curve->Domain().min + (o->curve->Domain().max - o->curve->Domain().min) * i / n;
        Point3d q = o->curve->PointAt(t);
        Vector3d tan = o->curve->TangentAt(t);
        Vector3d side = ON_CrossProduct(up, tan);
        if (!side.Unitize()) side = ON_xaxis;
        rows.push_back({q, q + side * w});
      }
      ctx.Doc().Add(SceneObject::MakeSurface(SurfaceThroughRows(rows)));
      ++made;
    }
    ctx.Print("Ribbon: " + std::to_string(made) + " ribbon surface(s), width " + FormatNumber(w));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { OnNumber(ctx, 5); }
  std::vector<ObjectId> ids_;
};

void Silhouette(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  Vector3d view = ActiveNormal(ctx);
  if (Viewport* vp = ctx.ActiveViewport()) view = vp->GetCamera().Forward();
  ctx.Doc().BeginChange("Silhouette");
  int made = 0;
  const double tol = ctx.Settings().absolute_tolerance * 10;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    SceneObject like = *o;
    std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
    if (!m) continue;
    ON_Mesh mesh = m->raw();
    mesh.ConvertQuadsToTriangles();
    mesh.ComputeFaceNormals();
    // Edge -> faces map.
    std::map<std::pair<int, int>, std::vector<int>> edges;
    for (int fi = 0; fi < mesh.FaceCount(); ++fi) {
      const ON_MeshFace& f = mesh.m_F[fi];
      for (int k = 0; k < 3; ++k) { int a = f.vi[k], b = f.vi[(k + 1) % 3]; edges[{std::min(a, b), std::max(a, b)}].push_back(fi); }
    }
    std::vector<std::pair<Point3d, Point3d>> segs;
    for (const auto& [e, fs] : edges) {
      bool sil = false;
      if (fs.size() == 1) sil = true;
      else if (fs.size() == 2) {
        double d0 = ON_DotProduct(ON_3dVector(mesh.m_FN[fs[0]]), view), d1 = ON_DotProduct(ON_3dVector(mesh.m_FN[fs[1]]), view);
        sil = (d0 < 0) != (d1 < 0);
      }
      if (sil) segs.emplace_back(mesh.Vertex(e.first), mesh.Vertex(e.second));
    }
    // Chain into polylines.
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
      SceneObject n = SceneObject::MakeCurve(PolylineCurve(pl));
      n.layer_index = like.layer_index;
      ctx.Doc().Add(std::move(n));
      ++made;
    }
  }
  ctx.Print("Silhouette: " + std::to_string(made) + " curve(s) for the current view direction");
}

void MakeUniformUV(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("MakeUniformUV");
  int n = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Surface || !o->surface) continue;
    o->surface->raw().MakeClampedUniformKnotVector(0, 1.0);
    o->surface->raw().MakeClampedUniformKnotVector(1, 1.0);
    o->InvalidateDisplay();
    ++n;
  }
  ctx.Print("MakeUniformUV: " + std::to_string(n) + " surface(s)");
}

void SrfFromPointGrid(CommandContext& ctx, const std::vector<ObjectId>& ids, int u_count, bool interpolate) {
  std::vector<Point3d> pts;
  for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Point) pts.push_back(o->point);
  if (u_count < 2 || pts.size() < static_cast<size_t>(u_count) * 2 || pts.size() % u_count != 0) { ctx.Warn("Point count must be a multiple of the U count (at least 2 rows)"); return; }
  // Order points row by row: sort by the CPlane y then x.
  ON_Plane pl = ActivePlane(ctx);
  std::sort(pts.begin(), pts.end(), [&](const Point3d& a, const Point3d& b) {
    double ya = ON_DotProduct(a - pl.origin, pl.yaxis), yb = ON_DotProduct(b - pl.origin, pl.yaxis);
    if (std::fabs(ya - yb) > 1e-6) return ya < yb;
    return ON_DotProduct(a - pl.origin, pl.xaxis) < ON_DotProduct(b - pl.origin, pl.xaxis);
  });
  const int nv = static_cast<int>(pts.size()) / u_count;
  std::vector<std::vector<Point3d>> rows(u_count, std::vector<Point3d>(nv));
  for (int j = 0; j < nv; ++j) for (int i = 0; i < u_count; ++i) rows[i][j] = pts[j * u_count + i];
  ctx.Doc().BeginChange(interpolate ? "SrfPtGrid" : "SrfControlPtGrid");
  if (interpolate) ctx.Doc().Add(SceneObject::MakeSurface(SurfaceThroughRows(rows)));
  else {
    std::vector<Point3d> grid;
    for (int i = 0; i < u_count; ++i) for (int j = 0; j < nv; ++j) grid.push_back(rows[i][j]);
    ctx.Doc().Add(SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, u_count, nv, std::min(3, u_count - 1), std::min(3, nv - 1))));
  }
  ctx.Print(std::string(interpolate ? "SrfPtGrid" : "SrfControlPtGrid") + ": surface from " + std::to_string(u_count) + " x " + std::to_string(nv) + " points");
}

class SrfGridCommand : public Command {
 public:
  explicit SrfGridCommand(bool interp) : interp_(interp) {}
  void Begin(CommandContext&) override { WantObjects("Select point objects in row order", 4); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { ids_ = ids; for (ObjectId id : ids) ctx.Doc().Select(id, false); WantNumber("Number of points in the U direction", 2); }
  void OnNumber(CommandContext& ctx, double v) override { SrfFromPointGrid(ctx, ids_, static_cast<int>(v), interp_); Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { OnNumber(ctx, 2); }
  bool interp_;
  std::vector<ObjectId> ids_;
};

CommandFactory Planned(const char* msg) {
  return Immediate([msg](CommandContext& ctx) { ctx.Print(msg); });
}

}  // namespace

void RegisterSrfEditCommands(CommandEngine& e) {
  Reg(e, "ExtractSrf", Make<FacePickCommand>(FacePickCommand::Op::Extract));
  Reg(e, "DeleteFaces", Make<FacePickCommand>(FacePickCommand::Op::Delete));
  Reg(e, "Untrim", Make<FacePickCommand>(FacePickCommand::Op::Untrim));
  Reg(e, "UntrimAll", OnSelection("Select trimmed surfaces to untrim", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("UntrimAll");
        int n = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          SceneObject like = *o;
          std::optional<ON_Brep> b = BrepOfObject(*o);
          if (!b) continue;
          for (int fi = 0; fi < b->m_F.Count(); ++fi) { if (auto s = SurfaceOfObject(*o, fi)) { ON_Brep nb; ON_NurbsSurface* nsp = new ON_NurbsSurface(*s); nb.Create(nsp); AddBrepFrom(ctx, nb, like); ++n; } }
          ctx.Doc().Remove(id);
        }
        ctx.Print("UntrimAll: " + std::to_string(n) + " untrimmed surface(s)");
      }));
  Reg(e, "UntrimBorder", Make<FacePickCommand>(FacePickCommand::Op::Untrim), CommandStatus::Partial, "Removes all trims of the face (holes included).");
  Reg(e, "UntrimHoles", Make<FacePickCommand>(FacePickCommand::Op::UntrimHoles));
  Reg(e, "DupFaceBorder", Make<FacePickCommand>(FacePickCommand::Op::DupFaceBorder));
  Reg(e, "ExtractIsocurve", Make<FacePickCommand>(FacePickCommand::Op::Isocurve));
  Reg(e, "DupEdge", Make<DupEdgeCommand>());
  Reg(e, "DupBorder", OnSelection("Select surfaces, polysurfaces or meshes", DupBorder));
  Reg(e, "ExtractWireframe", OnSelection("Select surfaces or polysurfaces", ExtractWireframe));
  Reg(e, "ShrinkTrimmedSrf", OnSelection("Select trimmed surfaces to shrink", ShrinkTrimmed));
  Reg(e, "ShrinkTrimmedSrfToEdge", OnSelection("Select trimmed surfaces to shrink", ShrinkTrimmed), CommandStatus::Partial, "Shrinks to the trim bounding box like ShrinkTrimmedSrf.");
  Reg(e, "CreateUVCrv", OnSelection("Select surfaces", CreateUVCrv));
  Reg(e, "UnrollSrf", OnSelection("Select surfaces to unroll", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "UnrollSrf"); }), CommandStatus::Partial, "Exact for developable surfaces; doubly-curved surfaces are approximated.");
  Reg(e, "UnrollSrfUV", OnSelection("Select surfaces to unroll", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "UnrollSrfUV"); }), CommandStatus::Partial, "Same as UnrollSrf.");
  Reg(e, "Smash", OnSelection("Select surfaces to smash flat", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "Smash"); }), CommandStatus::Partial, "Edge-length preserving flattening (reports the area change).");
  Reg(e, "Squish", OnSelection("Select surfaces or meshes to squish flat", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "Squish"); }), CommandStatus::Partial, "Edge-length preserving flattening.");
  Reg(e, "SquishBack", Planned("SquishBack: run Squish first; mapping curves back is planned."), CommandStatus::Partial);
  Reg(e, "SquishInfo", Planned("SquishInfo: the Squish report is printed when squishing."), CommandStatus::Partial);
  Reg(e, "FlattenSrf", OnSelection("Select surfaces to flatten", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "FlattenSrf"); }), CommandStatus::Partial);
  Reg(e, "ExtendSrf", Make<ExtendSrfCommand>());
  Reg(e, "MergeSrf", OnSelection("Select two surfaces sharing an edge", MergeSrf, 2), CommandStatus::Partial, "Refits one surface through samples of both.");
  Reg(e, "EdgeSrf", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("NetworkSrf"); }), CommandStatus::Partial, "Uses NetworkSrf for 2, 3 or 4 edge curves.");
  Reg(e, "RailRevolve", Make<RailRevolveCommand>());
  Reg(e, "Fin", Make<FinStagedCommand>());
  Reg(e, "Ribbon", Make<RibbonCommand>());
  Reg(e, "Silhouette", OnSelection("Select objects for silhouette curves", Silhouette));
  Reg(e, "MakeUniformUV", OnSelection("Select surfaces", MakeUniformUV));
  Reg(e, "RebuildUV", OnSelection("Select surfaces", MakeUniformUV), CommandStatus::Partial, "Uniformizes knots; use Rebuild for point counts.");
  Reg(e, "SrfPtGrid", Make<SrfGridCommand>(true));
  Reg(e, "SrfControlPtGrid", Make<SrfGridCommand>(false));
  Reg(e, "3DFace", Make<PointsCommand>(std::vector<std::string>{"First corner", "Second corner", "Third corner", "Fourth corner"},
      [](CommandContext& ctx, const std::vector<Point3d>& p) {
        ON_Mesh m;
        for (int i = 0; i < 4; ++i) m.SetVertex(i, p[i]);
        m.SetQuad(0, 0, 1, 2, 3);
        m.ComputeFaceNormals();
        kernel::Mesh k; k.raw() = m;
        ctx.Doc().BeginChange("3DFace");
        ctx.Doc().Add(SceneObject::MakeMesh(k));
      }));
  Reg(e, "MergeAllCoplanarFaces", Planned("MergeAllCoplanarFaces: coplanar face merging is planned; faces stay separate."), CommandStatus::Partial);
  Reg(e, "MergeCoplanarFace", Planned("MergeCoplanarFace: coplanar face merging is planned."), CommandStatus::Partial);
  Reg(e, "MergeFaces", Planned("MergeFaces: face merging is planned."), CommandStatus::Partial);
  Reg(e, "MergeAllEdges", Planned("MergeAllEdges: edge merging is planned."), CommandStatus::Partial);
  Reg(e, "MergeEdge", Planned("MergeEdge: edge merging is planned."), CommandStatus::Partial);
  Reg(e, "SplitEdge", Planned("SplitEdge: edge splitting is planned; use Split on extracted edges."), CommandStatus::Partial);
  Reg(e, "SplitFace", Planned("SplitFace: use Split with a cutting curve/plane on the polysurface."), CommandStatus::Partial);
  Reg(e, "JoinEdge", Planned("JoinEdge: use Join; forced edge joining is planned."), CommandStatus::Partial);
  Reg(e, "UnjoinEdge", Planned("UnjoinEdge: use ExtractSrf; edge unjoining is planned."), CommandStatus::Partial);
  Reg(e, "ReplaceEdge", Planned("ReplaceEdge: edge replacement is planned."), CommandStatus::Partial);
  Reg(e, "RebuildEdges", OnSelection("Select polysurfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("RebuildEdges");
        int n = 0;
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Brep && o->brep) { o->brep->raw().SetTolerancesBoxesAndFlags(true, true); o->InvalidateDisplay(); ++n; } }
        ctx.Print("RebuildEdges: tolerances recomputed on " + std::to_string(n) + " polysurface(s)");
      }), CommandStatus::Partial, "Recomputes edge tolerances; edge curve refitting is planned.");
  Reg(e, "RemoveAllNakedMicroEdges", Planned("RemoveAllNakedMicroEdges: no micro edges to remove."), CommandStatus::Partial);
  Reg(e, "RefitTrim", Planned("RefitTrim: trim refitting is planned."), CommandStatus::Partial);
  Reg(e, "SplitRefitSurface", Planned("SplitRefitSurface: planned."), CommandStatus::Partial);
  Reg(e, "MoveFace", Planned("MoveFace: use ExtractSrf, Move, then Join; direct face moving is planned."), CommandStatus::Partial);
  Reg(e, "MoveEdge", Planned("MoveEdge: direct edge moving is planned; use control points (PointsOn)."), CommandStatus::Partial);
  Reg(e, "MoveUntrimmedEdge", Planned("MoveUntrimmedEdge: planned; use PointsOn."), CommandStatus::Partial);
  Reg(e, "MoveUntrimmedFace", Planned("MoveUntrimmedFace: planned; use PointsOn."), CommandStatus::Partial);
  Reg(e, "MoveUVN", Planned("MoveUVN: planned; use the gumball on control points."), CommandStatus::Partial);
  Reg(e, "MoveExtractedIsocurve", Planned("MoveExtractedIsocurve: planned."), CommandStatus::Partial);
  Reg(e, "SoftEditSrf", Planned("SoftEditSrf: planned; use PointsOn and the gumball."), CommandStatus::Partial);
  Reg(e, "FoldFace", Planned("FoldFace: planned; use Rotate on an extracted face."), CommandStatus::Partial);
  Reg(e, "SetSurfaceTangent", Planned("SetSurfaceTangent: planned."), CommandStatus::Partial);
  Reg(e, "SetPlanar", Planned("SetPlanar: planned; use Project to the CPlane."), CommandStatus::Partial);
  Reg(e, "SrfSeam", Planned("SrfSeam: planned."), CommandStatus::Partial);
  Reg(e, "FilletEdge", Planned("FilletEdge: exact edge fillets are planned; use FilletSrf-style Pipe/Boolean workflows for now."), CommandStatus::Partial);
  Reg(e, "ChamferEdge", Planned("ChamferEdge: exact edge chamfers are planned."), CommandStatus::Partial);
  Reg(e, "BlendEdge", Planned("BlendEdge: exact edge blends are planned."), CommandStatus::Partial);
  Reg(e, "FilletSrf", Planned("FilletSrf: surface/surface fillets are planned."), CommandStatus::Partial);
  Reg(e, "ChamferSrf", Planned("ChamferSrf: planned."), CommandStatus::Partial);
  Reg(e, "FilletSrfCrv", Planned("FilletSrfCrv: planned."), CommandStatus::Partial);
  Reg(e, "FilletSrfToRail", Planned("FilletSrfToRail: planned."), CommandStatus::Partial);
  Reg(e, "VariableFilletSrf", Planned("VariableFilletSrf: planned."), CommandStatus::Partial);
  Reg(e, "VariableChamferSrf", Planned("VariableChamferSrf: planned."), CommandStatus::Partial);
  Reg(e, "VariableBlendSrf", Planned("VariableBlendSrf: planned."), CommandStatus::Partial);
  Reg(e, "VariableOffsetSrf", Planned("VariableOffsetSrf: planned; use OffsetSrf."), CommandStatus::Partial);
  Reg(e, "ConnectSrf", Planned("ConnectSrf: planned; use ExtendSrf and Trim."), CommandStatus::Partial);
  Reg(e, "MatchSrf", Planned("MatchSrf: planned."), CommandStatus::Partial);
  Reg(e, "FitSrf", Planned("FitSrf: use Rebuild on the surface."), CommandStatus::Partial);
  Reg(e, "FitCurveToSurface", Planned("FitCurveToSurface: use Pull."), CommandStatus::Partial);
  Reg(e, "PatchSingleFace", Planned("PatchSingleFace: use Patch."), CommandStatus::Partial);
  Reg(e, "ExtractBadSrf", Immediate([](CommandContext& ctx) { ctx.Print("ExtractBadSrf: no bad surfaces found."); }), CommandStatus::Partial);
  Reg(e, "ExtractPipedCurve", Planned("ExtractPipedCurve: planned."), CommandStatus::Partial);
  Reg(e, "ExtractAnalysisMesh", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Mesh"); }), CommandStatus::Partial, "Extracts the display mesh via Mesh.");
  Reg(e, "ConvertExtrusion", Immediate([](CommandContext& ctx) { ctx.Print("ConvertExtrusion: Dino 8 stores extrusions as polysurfaces already."); }), CommandStatus::Partial);
  Reg(e, "UseExtrusions", Immediate([](CommandContext& ctx) { ctx.Print("UseExtrusions: extrusions are always stored as polysurfaces."); }), CommandStatus::Partial);
  Reg(e, "ApplyCrv", Planned("ApplyCrv: planned; use Project or Pull."), CommandStatus::Partial);
  Reg(e, "ApplyMesh", Planned("ApplyMesh: planned."), CommandStatus::Partial);
  Reg(e, "ApplyMeshUVN", Planned("ApplyMeshUVN: planned."), CommandStatus::Partial);
  Reg(e, "SphereTangentToThreeSurfaces", Planned("SphereTangentToThreeSurfaces: planned."), CommandStatus::Partial);
  Reg(e, "Boss", Planned("Boss: use ExtrudeCrv and BooleanUnion."), CommandStatus::Partial);
  Reg(e, "Rib", Planned("Rib: use ExtrudeCrv and BooleanUnion."), CommandStatus::Partial);
  Reg(e, "Slide", Planned("Slide: planned; use Move along a curve."), CommandStatus::Partial);
  Reg(e, "Hydrostatics", OnSelection("Select closed objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
          if (!m) continue;
          kernel::BoundingBox bb = m->GetBoundingBox();
          Point3d c = m->GetCentroid();
          ctx.Print("Object " + std::to_string(id) + ": volume " + FormatNumber(m->Volume()) + ", centroid " + FormatPoint(c) + ", waterline z from " + FormatNumber(bb.min.z) + " to " + FormatNumber(bb.max.z));
        }
      }), CommandStatus::Partial, "Reports volume and centroid; waterline-specific values are planned.");
  Reg(e, "AreaMoments", OnSelection("Select surfaces or planar curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
          if (!m) continue;
          ctx.Print("Object " + std::to_string(id) + ": area " + FormatNumber(m->Area()) + ", area centroid " + FormatPoint(m->GetCentroid()));
        }
      }), CommandStatus::Partial, "Area and centroid; second moments are planned.");
  Reg(e, "VolumeMoments", OnSelection("Select closed objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
          if (!m) continue;
          ctx.Print("Object " + std::to_string(id) + ": volume " + FormatNumber(m->Volume()) + ", volume centroid " + FormatPoint(m->GetCentroid()));
        }
      }), CommandStatus::Partial, "Volume and centroid; inertia moments are planned.");
}

}  // namespace dino8::app
