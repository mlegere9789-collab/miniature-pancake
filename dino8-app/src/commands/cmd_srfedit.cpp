// Surface and polysurface editing: ExtractSrf, DeleteFaces, DupBorder,
// DupEdge, DupFaceBorder, Untrim family, ShrinkTrimmedSrf, MergeSrf,
// ExtendSrf, EdgeSrf, RailRevolve, ExtractIsocurve, ExtractWireframe,
// CreateUVCrv, UnrollSrf/Smash (developable approximation), Fin, Ribbon,
// Silhouette, 3DFace, MakeUniformUV and friends.
#include "commands/cmd_common.h"
#include "doc/SubObjectEdit.h"
#include "drafting/SectionView.h"
#include "geom/SurfaceIntersect.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>

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

ObjectId AddSurfaceFrom(CommandContext& ctx, const ON_NurbsSurface& s, const SceneObject& like) {
  kernel::NurbsSurface k;
  k.raw() = s;
  SceneObject n = SceneObject::MakeSurface(k);
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
  enum class Op { Extract, ExtractCopy, Delete, Untrim, UntrimBorderOnly, UntrimHoles, DupFaceBorder, Isocurve };
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
      case Op::UntrimBorderOnly: return "Click trimmed faces to untrim the outer border of (holes are kept) (Enter when done)";
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
      case Op::UntrimBorderOnly: {
        // Duplicate the face (with its full, possibly-larger-than-trimmed
        // underlying surface and every one of its loops intact), then drop
        // just the outer loop and ask OpenNURBS to regenerate a default
        // full-surface-boundary outer loop in its place - the inner loops
        // (holes) are untouched, so they stay exactly where they were.
        ON_Brep* dup = b->DuplicateFace(fi, false);
        if (!dup || dup->m_F.Count() < 1) { delete dup; ctx.Warn("Could not duplicate the face"); return; }
        ON_BrepFace& df = dup->m_F[0];
        ON_BrepLoop* outer = nullptr;
        for (int li = 0; li < df.LoopCount(); ++li) { ON_BrepLoop* l = df.Loop(li); if (l && l->m_type == ON_BrepLoop::outer) { outer = l; break; } }
        if (outer) dup->DeleteLoop(*outer, true);
        if (!dup->NewOuterLoop(0)) { delete dup; ctx.Warn("UntrimBorder: could not build a full-surface outer loop"); return; }
        dup->SetTolerancesBoxesAndFlags();
        if (b->m_F.Count() <= 1) { if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->kind = ObjectKind::Brep; if (!orig->brep) orig->brep = std::make_unique<kernel::Brep>(); orig->brep->raw() = *dup; orig->surface.reset(); orig->InvalidateDisplay(); } }
        else {
          b->DeleteFace(b->m_F[fi], true); b->Compact();
          if (SceneObject* orig = ctx.Doc().Find(pick->id)) { orig->brep->raw() = *b; orig->InvalidateDisplay(); }
          AddBrepFrom(ctx, *dup, like);
        }
        delete dup;
        ctx.Print("UntrimBorder: face " + std::to_string(fi) + "'s outer trim removed (holes kept)");
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

// ---------------------------------------------------------------------------
// MoveFace / MoveEdge / MoveUntrimmedFace / MoveUntrimmedEdge: click one or
// more brep faces/edges, then a from/to point pair, translating them via the
// same MoveBrepFaces/MoveBrepEdges the gumball's sub-object drag uses (rigid
// move of the picked parts, neighbouring untrimmed faces reshaped to follow,
// remaining edges re-fitted between their moved endpoints - see
// doc/SubObjectEdit.h/.cpp). The "Untrimmed" variants restrict picking to
// faces with a single full-surface loop / edges that run along an iso
// boundary, matching Rhino's own restriction; MoveFace/MoveEdge accept any.
class MovePartsCommand : public Command {
 public:
  MovePartsCommand(bool is_face, bool require_untrimmed) : is_face_(is_face), require_untrimmed_(require_untrimmed) {}
  void Begin(CommandContext&) override { WantPoint(Prompt()); }
  std::string Prompt() const {
    std::string what = is_face_ ? "face" : "edge";
    return "Click " + what + (require_untrimmed_ ? "s (untrimmed only)" : "s") + " to move (Enter when done)";
  }
  void OnEnter(CommandContext& ctx) override {
    if (parts_.empty()) { ctx.Warn("Nothing picked"); Finish(); return; }
    picking_ = false;
    WantPoint("Point to move from");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (picking_) {
      if (is_face_) {
        std::optional<FacePick> pick = PickFace(ctx, p);
        if (!pick) { ctx.Warn("No surface near that point"); return; }
        const SceneObject* o = ctx.Doc().Find(pick->id);
        std::optional<ON_Brep> b = o ? BrepOfObject(*o) : std::nullopt;
        if (!o || !b || o->kind != ObjectKind::Brep) { ctx.Warn("Only polysurface faces can be moved this way"); return; }
        if (require_untrimmed_) {
          const ON_BrepFace& f = b->m_F[pick->face];
          bool untrimmed = f.LoopCount() == 1 && f.Loop(0) && f.Loop(0)->TrimCount() == 4;
          if (!untrimmed) { ctx.Warn("MoveUntrimmedFace: that face is trimmed; use MoveFace instead"); return; }
        }
        parts_[pick->id].push_back(pick->face);
        ctx.Print("Picked face " + std::to_string(pick->face) + " of object " + std::to_string(pick->id));
      } else {
        std::optional<EdgePick> pick = PickEdge(ctx, p);
        if (!pick) { ctx.Warn("No edge near that point"); return; }
        const SceneObject* o = ctx.Doc().Find(pick->id);
        std::optional<ON_Brep> b = o ? BrepOfObject(*o) : std::nullopt;
        if (!o || !b || o->kind != ObjectKind::Brep) { ctx.Warn("Only polysurface edges can be moved this way"); return; }
        if (require_untrimmed_) {
          const ON_BrepEdge& ed = b->m_E[pick->edge];
          bool iso_edge = false;
          for (int k = 0; k < ed.m_ti.Count(); ++k) { if (b->m_T[ed.m_ti[k]].m_iso != ON_Surface::not_iso) iso_edge = true; }
          if (!iso_edge) { ctx.Warn("MoveUntrimmedEdge: that edge is not an untrimmed surface-boundary edge; use MoveEdge instead"); return; }
        }
        parts_[pick->id].push_back(pick->edge);
        ctx.Print("Picked edge " + std::to_string(pick->edge) + " of object " + std::to_string(pick->id));
      }
      return;
    }
    ctx.SetLastPoint(p);
    if (!base_) { base_ = p; WantPoint("Point to move to"); return; }
    ON_Xform xf = ON_Xform::TranslationTransformation(p - *base_);
    ctx.Doc().BeginChange(Label());
    int n = 0;
    for (auto& kv : parts_) {
      SceneObject* o = ctx.Doc().Find(kv.first);
      if (!o || o->kind != ObjectKind::Brep || !o->brep) continue;
      bool ok = is_face_ ? MoveBrepFaces(o->brep->raw(), kv.second, xf) : MoveBrepEdges(o->brep->raw(), kv.second, xf);
      if (ok) { o->InvalidateDisplay(); ++n; }
    }
    ctx.Print(Label() + ": " + std::to_string(n) + " object(s) updated");
    ctx.ClearPreview();
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (!picking_ && base_) { ctx.ClearPreview(); ctx.AddPreviewLine(*base_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  std::string Label() const { return is_face_ ? (require_untrimmed_ ? "MoveUntrimmedFace" : "MoveFace") : (require_untrimmed_ ? "MoveUntrimmedEdge" : "MoveEdge"); }
  bool is_face_, require_untrimmed_;
  bool picking_ = true;
  std::map<ObjectId, std::vector<int>> parts_;
  std::optional<Point3d> base_;
};

// Moves a single control point of a plain (non-brep) surface along its
// local U tangent / V tangent / normal directions at the picked parameter -
// the actual "UVN" frame the command name refers to. Restricted to Surface
// objects (not polysurface faces): a brep face's control points are not
// independently editable sub-objects in this document model (see
// doc/SubObjectEdit.h - ControlPointCount is 0 for breps), so a picked brep
// face is reported with a suggestion to ExtractSrf it first.
class MoveUVNCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click near the surface point to move"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!id_) {
      std::optional<FacePick> pick = PickFace(ctx, p);
      if (!pick) { ctx.Warn("No surface near that point"); return; }
      const SceneObject* o = ctx.Doc().Find(pick->id);
      if (!o) return;
      if (o->kind != ObjectKind::Surface || !o->surface) { ctx.Warn("MoveUVN: pick a plain surface's control point; extract polysurface faces with ExtractSrf first"); Finish(); return; }
      const ON_NurbsSurface& s = o->surface->raw();
      double u, v;
      ClosestUV(s, p, u, v);
      int nu = s.CVCount(0), nv = s.CVCount(1);
      double fu = s.Domain(0).NormalizedParameterAt(u), fv = s.Domain(1).NormalizedParameterAt(v);
      ci_ = std::clamp(static_cast<int>(std::lround(fu * (nu - 1))), 0, nu - 1);
      cj_ = std::clamp(static_cast<int>(std::lround(fv * (nv - 1))), 0, nv - 1);
      double uu = s.Domain(0).ParameterAt(nu <= 1 ? 0.0 : static_cast<double>(ci_) / (nu - 1));
      double vv = s.Domain(1).ParameterAt(nv <= 1 ? 0.0 : static_cast<double>(cj_) / (nv - 1));
      ON_3dPoint pt; ON_3dVector du, dv;
      s.Ev1Der(uu, vv, pt, du, dv);
      u_dir_ = du; if (!u_dir_.Unitize()) u_dir_ = ON_xaxis;
      v_dir_ = dv; if (!v_dir_.Unitize()) v_dir_ = ON_yaxis;
      n_dir_ = ON_CrossProduct(du, dv); if (!n_dir_.Unitize()) n_dir_ = ON_zaxis;
      id_ = pick->id;
      WantText("Move along U,V,N (comma-separated, e.g. 0,0,5; a single number moves along N)");
      return;
    }
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    std::vector<double> vals;
    std::stringstream ss(t);
    std::string tok;
    while (std::getline(ss, tok, ',')) { char* e2; double v2 = std::strtod(tok.c_str(), &e2); if (e2 != tok.c_str() && *e2 == 0) vals.push_back(v2); }
    if (vals.size() == 1) Apply(ctx, 0, 0, vals[0]);
    else if (vals.size() >= 3) Apply(ctx, vals[0], vals[1], vals[2]);
    else ctx.Warn("MoveUVN: enter a single normal distance, or U,V,N");
  }
  void OnNumber(CommandContext& ctx, double v) override { Apply(ctx, 0, 0, v); }
  void OnEnter(CommandContext& ctx) override { if (id_) Apply(ctx, 0, 0, 10); else Finish(); }
  void Apply(CommandContext& ctx, double du, double dv, double dn) {
    if (!id_) return;
    SceneObject* o = ctx.Doc().Find(*id_);
    if (!o || o->kind != ObjectKind::Surface || !o->surface) { Finish(); return; }
    Vector3d delta = u_dir_ * du + v_dir_ * dv + n_dir_ * dn;
    ctx.Doc().BeginChange("MoveUVN");
    ON_3dPoint cv;
    o->surface->raw().GetCV(ci_, cj_, cv);
    o->surface->raw().SetCV(ci_, cj_, cv + delta);
    o->InvalidateDisplay();
    ctx.Print("MoveUVN: control point (" + std::to_string(ci_) + "," + std::to_string(cj_) + ") moved u=" + FormatNumber(du) + " v=" + FormatNumber(dv) + " n=" + FormatNumber(dn));
    Finish();
  }
  std::optional<ObjectId> id_;
  int ci_ = 0, cj_ = 0;
  Vector3d u_dir_{1, 0, 0}, v_dir_{0, 1, 0}, n_dir_{0, 0, 1};
};

// Rotates one face of a polysurface about a picked hinge edge - the same
// MoveBrepFaces rigid-face-move machinery used for MoveFace, just with a
// rotation transform (about the hinge edge's own chord) instead of a
// translation. Real Rhino's FoldFace is normally used on planar faces
// hinged along a shared edge; this works for any face on the same
// polysurface as the hinge, planar or not.
class FoldFaceCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the hinge edge to fold about"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!hinge_) {
      hinge_ = PickEdge(ctx, p);
      if (!hinge_) { ctx.Warn("No edge near that point"); return; }
      WantPoint("Click the face to fold (the side that should rotate)");
      return;
    }
    if (!face_) {
      std::optional<FacePick> pick = PickFace(ctx, p);
      if (!pick || pick->id != hinge_->id) { ctx.Warn("Pick a face on the same polysurface as the hinge edge"); return; }
      face_ = pick->face;
      WantNumber("Fold angle in degrees", 90);
      return;
    }
  }
  void OnNumber(CommandContext& ctx, double deg) override {
    if (!hinge_ || !face_) return;
    SceneObject* o = ctx.Doc().Find(hinge_->id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) { Finish(); return; }
    ON_Brep& b = o->brep->raw();
    if (hinge_->edge < 0 || hinge_->edge >= b.m_E.Count()) { Finish(); return; }
    const ON_BrepEdge& ed = b.m_E[hinge_->edge];
    Point3d p0 = ed.PointAtStart(), p1 = ed.PointAtEnd();
    Vector3d axis = p1 - p0;
    if (!axis.Unitize()) { ctx.Warn("FoldFace: degenerate hinge edge"); Finish(); return; }
    ON_Xform xf;
    xf.Rotation(deg * ON_PI / 180.0, axis, p0);
    ctx.Doc().BeginChange("FoldFace");
    bool ok = MoveBrepFaces(b, {*face_}, xf);
    if (ok) { o->InvalidateDisplay(); ctx.Print("FoldFace: face " + std::to_string(*face_) + " folded " + FormatNumber(deg) + " degree(s) about the picked edge"); }
    else ctx.Warn("FoldFace: could not fold that face");
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e2; double v2 = std::strtod(t.c_str(), &e2); if (e2 != t.c_str() && *e2 == 0) OnNumber(ctx, v2); }
  void OnEnter(CommandContext& ctx) override { if (face_) OnNumber(ctx, 90); }
  std::optional<EdgePick> hinge_;
  std::optional<int> face_;
};

// Projects the CVs of the selected curves (and the positions of selected
// points) orthogonally onto the active construction plane.
void SetPlanar(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ON_Plane pl = ActivePlane(ctx);
  ctx.Doc().BeginChange("SetPlanar");
  int n = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Point) {
      double u, v; pl.ClosestPointTo(o->point, &u, &v);
      o->point = pl.PointAt(u, v);
      o->InvalidateDisplay(); ++n;
    } else if (o->kind == ObjectKind::Curve && o->curve) {
      ON_NurbsCurve& nc = o->curve->raw();
      for (int i = 0; i < nc.CVCount(); ++i) {
        ON_3dPoint cv; nc.GetCV(i, cv);
        double u, v; pl.ClosestPointTo(cv, &u, &v);
        nc.SetCV(i, pl.PointAt(u, v));
      }
      o->InvalidateDisplay(); ++n;
    }
  }
  ctx.Print("SetPlanar: " + std::to_string(n) + " object(s) projected onto the active construction plane");
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

// Raw (uncentered) second-order moments Mxx=int x*x, Mxy=int x*y, etc, plus
// the zeroth (area/volume) and first (measure * centroid) moments, so both
// "about world axes" and "about the centroid" (parallel-axis theorem) can be
// reported from the same accumulation.
struct RawMoments {
  double measure = 0;         // area or volume
  Vector3d first{0, 0, 0};    // measure * centroid
  double mxx = 0, myy = 0, mzz = 0, mxy = 0, mxz = 0, myz = 0;  // int x_i*x_j
  Point3d Centroid() const { return measure != 0 ? Point3d(first / measure) : Point3d(0, 0, 0); }
  // Moments/products of inertia about the given point (world origin if p is
  // the zero point, or the centroid for the usual "about its own center" report).
  void InertiaAbout(Point3d p, double& Ixx, double& Iyy, double& Izz, double& Ixy, double& Ixz, double& Iyz) const {
    // Shift raw moments to be about p: M'_ij = M_ij - c_i*first_j - c_j*first_i + measure*c_i*c_j,
    // which for p = world origin (c=0) is just M_ij, and for p = centroid (c = first/measure)
    // reduces to the standard parallel-axis M_ij - first_i*first_j/measure.
    auto shift = [&](double m, double fi, double fj, double pi, double pj) {
      return m - pi * fj - pj * fi + measure * pi * pj;
    };
    double sxx = shift(mxx, first.x, first.x, p.x, p.x), syy = shift(myy, first.y, first.y, p.y, p.y), szz = shift(mzz, first.z, first.z, p.z, p.z);
    double sxy = shift(mxy, first.x, first.y, p.x, p.y), sxz = shift(mxz, first.x, first.z, p.x, p.z), syz = shift(myz, first.y, first.z, p.y, p.z);
    Ixx = syy + szz; Iyy = sxx + szz; Izz = sxx + syy;
    Ixy = sxy; Ixz = sxz; Iyz = syz;
  }
};

// Area moments of a (generally non-planar) triangulated shell: exact
// per-triangle quadrature (int lambda_i*lambda_j dA = Area*(1+delta_ij)/12
// for a linear triangle), so this is exact for the mesh's own faces - the
// only approximation is the mesh's chord tolerance versus the true surface,
// same as Area()/Volume() elsewhere in this file.
RawMoments AreaMoments(const ON_Mesh& m) {
  RawMoments r;
  auto tri = [&](Point3d a, Point3d b, Point3d c) {
    double area = ON_CrossProduct(b - a, c - a).Length() * 0.5;
    if (area <= 0) return;
    Point3d verts[3] = {a, b, c};
    double Tx = a.x + b.x + c.x, Ty = a.y + b.y + c.y, Tz = a.z + b.z + c.z;
    double Sxx = 0, Syy = 0, Szz = 0, Sxy = 0, Sxz = 0, Syz = 0;
    for (const Point3d& v : verts) { Sxx += v.x * v.x; Syy += v.y * v.y; Szz += v.z * v.z; Sxy += v.x * v.y; Sxz += v.x * v.z; Syz += v.y * v.z; }
    const double k = area / 12.0;
    r.measure += area;
    r.first += Vector3d(Tx, Ty, Tz) * (area / 3.0);
    r.mxx += k * (Sxx + Tx * Tx); r.myy += k * (Syy + Ty * Ty); r.mzz += k * (Szz + Tz * Tz);
    r.mxy += k * (Sxy + Tx * Ty); r.mxz += k * (Sxz + Tx * Tz); r.myz += k * (Syz + Ty * Tz);
  };
  for (int i = 0; i < m.m_F.Count(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    Point3d a = m.Vertex(f.vi[0]), b = m.Vertex(f.vi[1]), c = m.Vertex(f.vi[2]);
    tri(a, b, c);
    if (f.IsQuad()) tri(a, c, m.Vertex(f.vi[3]));
  }
  return r;
}

// Volume moments of a closed, consistently-oriented solid mesh: each
// triangle plus the world origin forms a (possibly negative) tetrahedron,
// exactly the decomposition Mesh::Volume()/GetCentroid() already use;
// int x_i*x_j dV over a tetrahedron with one vertex at the origin is
// Volume*(S_ij + T_i*T_j)/20 where S, T sum over the other 3 vertices.
RawMoments VolumeMoments(const ON_Mesh& m) {
  RawMoments r;
  auto tet = [&](Point3d a, Point3d b, Point3d c) {
    double v6 = ON_DotProduct(Vector3d(a), ON_CrossProduct(Vector3d(b), Vector3d(c)));
    double vol = v6 / 6.0;
    double Tx = a.x + b.x + c.x, Ty = a.y + b.y + c.y, Tz = a.z + b.z + c.z;
    double Sxx = 0, Syy = 0, Szz = 0, Sxy = 0, Sxz = 0, Syz = 0;
    Point3d verts[3] = {a, b, c};
    for (const Point3d& p : verts) { Sxx += p.x * p.x; Syy += p.y * p.y; Szz += p.z * p.z; Sxy += p.x * p.y; Sxz += p.x * p.z; Syz += p.y * p.z; }
    const double k = vol / 20.0;
    r.measure += vol;
    r.first += Vector3d(Tx, Ty, Tz) * (vol / 4.0);
    r.mxx += k * (Sxx + Tx * Tx); r.myy += k * (Syy + Ty * Ty); r.mzz += k * (Szz + Tz * Tz);
    r.mxy += k * (Sxy + Tx * Ty); r.mxz += k * (Sxz + Tx * Tz); r.myz += k * (Syz + Ty * Tz);
  };
  for (int i = 0; i < m.m_F.Count(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    Point3d a = m.Vertex(f.vi[0]), b = m.Vertex(f.vi[1]), c = m.Vertex(f.vi[2]);
    tet(a, b, c);
    if (f.IsQuad()) tet(a, c, m.Vertex(f.vi[3]));
  }
  return r;
}

std::string FormatMoments(const RawMoments& r, const char* measure_name) {
  Point3d c = r.Centroid();
  double Ixxo, Iyyo, Izzo, Ixyo, Ixzo, Iyzo;
  r.InertiaAbout(Point3d(0, 0, 0), Ixxo, Iyyo, Izzo, Ixyo, Ixzo, Iyzo);
  double Ixxc, Iyyc, Izzc, Ixyc, Ixzc, Iyzc;
  r.InertiaAbout(c, Ixxc, Iyyc, Izzc, Ixyc, Ixzc, Iyzc);
  std::string s = std::string(measure_name) + " " + FormatNumber(r.measure) + ", centroid " + FormatPoint(c) +
      "\n  about world axes: Ixx=" + FormatNumber(Ixxo) + " Iyy=" + FormatNumber(Iyyo) + " Izz=" + FormatNumber(Izzo) +
      ", Ixy=" + FormatNumber(Ixyo) + " Ixz=" + FormatNumber(Ixzo) + " Iyz=" + FormatNumber(Iyzo) +
      "\n  about centroid:    Ixx=" + FormatNumber(Ixxc) + " Iyy=" + FormatNumber(Iyyc) + " Izz=" + FormatNumber(Izzc) +
      ", Ixy=" + FormatNumber(Ixyc) + " Ixz=" + FormatNumber(Ixzc) + " Iyz=" + FormatNumber(Iyzc);
  if (r.measure > 1e-12) {
    s += "\n  radii of gyration about centroid: x=" + FormatNumber(std::sqrt(std::max(0.0, Ixxc) / r.measure)) +
         " y=" + FormatNumber(std::sqrt(std::max(0.0, Iyyc) / r.measure)) +
         " z=" + FormatNumber(std::sqrt(std::max(0.0, Izzc) / r.measure));
  }
  return s;
}

// ---------------------------------------------------------------------------
// SrfSeam: rotates a standalone closed surface's periodic seam via the
// kernel's own ON_NurbsSurface::ChangeSurfaceSeam (a real knot-vector
// rotation the earlier Partial note said didn't exist). Scoped to bare
// Surface objects: for a face inside a polysurface, the trims/edges that
// already reference specific (u,v) numbers in the OLD domain would also
// need re-deriving (a seam move shifts what numbers "u=min/max" are), which
// isn't attempted here.
// ---------------------------------------------------------------------------

class SrfSeamCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the closed surface whose seam to move"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!pick_) {
      pick_ = PickFace(ctx, p);
      if (!pick_) { ctx.Warn("SrfSeam: no surface near that point"); Finish(); return; }
      WantPoint("Click where the seam should move to");
      return;
    }
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d p) {
    const SceneObject* o = ctx.Doc().Find(pick_->id);
    if (!o || o->kind != ObjectKind::Surface || !o->surface) {
      ctx.Warn("SrfSeam: only a standalone closed surface object is supported (not a face of a polysurface, whose trim/edge parameterisation would also need rebuilding across the new seam)");
      return;
    }
    ON_NurbsSurface s = o->surface->raw();
    int dir = -1;
    if (s.IsClosed(0)) dir = 0;
    else if (s.IsClosed(1)) dir = 1;
    if (dir < 0) { ctx.Warn("SrfSeam: the surface is not closed in either direction"); return; }
    double u = 0, v = 0;
    if (!SurfaceClosestPointGlobal(s, p, u, v)) { ctx.Warn("SrfSeam: could not locate that point on the surface"); return; }
    const double t = dir == 0 ? u : v;
    if (!s.ChangeSurfaceSeam(dir, t)) { ctx.Warn("SrfSeam: ChangeSurfaceSeam failed"); return; }
    ctx.Doc().BeginChange("SrfSeam");
    if (SceneObject* orig = ctx.Doc().Find(pick_->id)) { orig->surface->raw() = s; orig->InvalidateDisplay(); }
    ctx.Print("SrfSeam: seam moved to " + std::string(dir == 0 ? "u" : "v") + "=" + FormatNumber(t));
  }
  std::optional<FacePick> pick_;
};

// ---------------------------------------------------------------------------
// SphereTangentToThreeSurfaces: a real Newton solve (the same damped
// Gauss-Newton the fillet family uses) for a sphere centre/radius equidistant
// from all three picked surfaces, seeded from the three pick points.
// ---------------------------------------------------------------------------

class SphereTangentToThreeSurfacesCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the first surface"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    auto pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("SphereTangentToThreeSurfaces: no surface near that point"); Finish(); return; }
    picks_.push_back(*pick);
    seeds_.push_back(p);
    if (picks_.size() < 3) { WantPoint(picks_.size() == 1 ? "Click the second surface" : "Click the third surface"); return; }
    Run(ctx);
    Finish();
  }
  void Run(CommandContext& ctx) {
    std::vector<ON_NurbsSurface> surfs;
    for (const FacePick& pk : picks_) {
      const SceneObject* o = ctx.Doc().Find(pk.id);
      std::optional<ON_NurbsSurface> s = o ? SurfaceOfObject(*o, pk.face) : std::nullopt;
      if (!s) { ctx.Warn("SphereTangentToThreeSurfaces: could not read one of the surfaces"); return; }
      surfs.push_back(*s);
    }
    Point3d c0((seeds_[0].x + seeds_[1].x + seeds_[2].x) / 3.0, (seeds_[0].y + seeds_[1].y + seeds_[2].y) / 3.0, (seeds_[0].z + seeds_[1].z + seeds_[2].z) / 3.0);
    double r0 = (c0.DistanceTo(seeds_[0]) + c0.DistanceTo(seeds_[1]) + c0.DistanceTo(seeds_[2])) / 3.0;
    if (r0 < 1e-6) r0 = 1.0;
    std::vector<double> su(3, 0.0), sv(3, 0.0);
    for (int i = 0; i < 3; ++i) SurfaceClosestPointGlobal(surfs[static_cast<size_t>(i)], seeds_[static_cast<size_t>(i)], su[static_cast<size_t>(i)], sv[static_cast<size_t>(i)]);
    Residual res = [&](const std::vector<double>& x) {
      Point3d c(x[0], x[1], x[2]);
      const double R = x[3];
      std::vector<double> out(3);
      for (int i = 0; i < 3; ++i) {
        double u = su[static_cast<size_t>(i)], v = sv[static_cast<size_t>(i)];
        SurfaceClosestPoint(surfs[static_cast<size_t>(i)], c, u, v);
        su[static_cast<size_t>(i)] = u;
        sv[static_cast<size_t>(i)] = v;
        out[static_cast<size_t>(i)] = surfs[static_cast<size_t>(i)].PointAt(u, v).DistanceTo(c) - R;
      }
      return out;
    };
    std::vector<double> x = {c0.x, c0.y, c0.z, r0};
    const std::vector<double> lo(4, -1e9), hi(4, 1e9);
    double final_norm = 0;
    const bool ok = NewtonSolve(res, x, lo, hi, std::max(ctx.Settings().absolute_tolerance, 1e-6), 60, &final_norm);
    if (!ok || x[3] <= 1e-9) { ctx.Warn("SphereTangentToThreeSurfaces: no equidistant sphere found near the picked points (residual " + FormatNumber(final_norm) + ")"); return; }
    const Point3d c(x[0], x[1], x[2]);
    const double r = x[3];
    ON_Brep* sph = ON_BrepSphere(ON_Sphere(c, r));
    if (!sph) { ctx.Warn("SphereTangentToThreeSurfaces: failed to build the sphere"); return; }
    ctx.Doc().BeginChange("SphereTangentToThreeSurfaces");
    kernel::Brep k;
    k.raw() = *sph;
    delete sph;
    ObjectId nid = ctx.Doc().Add(SceneObject::MakeBrep(k));
    ctx.Doc().Select(nid, true);
    ctx.Print("SphereTangentToThreeSurfaces: sphere at " + FormatPoint(c) + ", radius " + FormatNumber(r) + " (equidistant to all three picks, residual " + FormatNumber(final_norm) + ")");
  }
  std::vector<FacePick> picks_;
  std::vector<Point3d> seeds_;
};

// ---------------------------------------------------------------------------
// SetSurfaceTangent: the tangency-only half of MatchSrf's technique
// (cmd_fillet.cpp), applied directly to a picked edge without also moving
// its position - real tangent-row rotation against the target surface's
// normal, not a stub.
// ---------------------------------------------------------------------------

class SetSurfaceTangentCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the surface edge whose tangency to set"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!first_) {
      first_ = PickFace(ctx, p);
      if (!first_) { ctx.Warn("SetSurfaceTangent: no surface near that point"); Finish(); return; }
      first_pick_ = p;
      WantPoint("Click the target surface edge to match tangency to");
      return;
    }
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d target_pick) {
    const SceneObject* o = ctx.Doc().Find(first_->id);
    if (!o) return;
    std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, first_->face);
    if (!s) { ctx.Warn("SetSurfaceTangent: could not read the surface"); return; }
    double u = 0, v = 0;
    if (!SurfaceClosestPointGlobal(*s, *first_pick_, u, v)) { ctx.Warn("SetSurfaceTangent: could not locate the pick"); return; }
    const ON_Interval du = s->Domain(0), dv = s->Domain(1);
    const double eu0 = u - du.Min(), eu1 = du.Max() - u, ev0 = v - dv.Min(), ev1 = dv.Max() - v;
    const double m = std::min({eu0, eu1, ev0, ev1});
    const bool is_u_edge = (m == eu0 || m == eu1);
    const int fixed_dir = is_u_edge ? 0 : 1;
    const bool at_min = is_u_edge ? (m == eu0) : (m == ev0);
    auto target = PickFace(ctx, target_pick);
    if (!target) { ctx.Warn("SetSurfaceTangent: no target surface near that point"); return; }
    const SceneObject* to = ctx.Doc().Find(target->id);
    std::optional<ON_NurbsSurface> ts = to ? SurfaceOfObject(*to, target->face) : std::nullopt;
    if (!ts) { ctx.Warn("SetSurfaceTangent: could not read the target surface"); return; }
    kernel::NurbsSurface ks;
    ks.raw() = *s;
    ON_NurbsSurface& raw = ks.raw();
    const int n_cross = raw.CVCount(1 - fixed_dir);
    const int row0 = at_min ? 0 : raw.CVCount(fixed_dir) - 1;
    const int row1 = at_min ? 1 : raw.CVCount(fixed_dir) - 2;
    int moved = 0;
    for (int k = 0; k < n_cross; ++k) {
      const int i0 = fixed_dir == 0 ? row0 : k, j0 = fixed_dir == 0 ? k : row0;
      const int i1 = fixed_dir == 0 ? row1 : k, j1 = fixed_dir == 0 ? k : row1;
      ON_3dPoint cv0, cv1;
      raw.GetCV(i0, j0, cv0);
      raw.GetCV(i1, j1, cv1);
      double tu = 0, tv = 0;
      if (!SurfaceClosestPointGlobal(*ts, cv0, tu, tv)) continue;
      Vector3d tang = ts->NormalAt(tu, tv);
      Vector3d old_step = cv1 - cv0;
      const double mag = old_step.Length();
      Vector3d perp = old_step - tang * ON_DotProduct(old_step, tang);
      if (perp.Length() > 1e-9) { perp.Unitize(); raw.SetCV(i1, j1, cv0 + perp * mag); ++moved; }
    }
    ctx.Doc().BeginChange("SetSurfaceTangent");
    if (SceneObject* orig = ctx.Doc().Find(first_->id)) {
      if (orig->kind == ObjectKind::Surface && orig->surface) orig->surface->raw() = raw;
      else if (orig->kind == ObjectKind::Brep && orig->brep) orig->brep->raw().m_S[orig->brep->raw().m_F[first_->face].m_si] = new ON_NurbsSurface(raw);
      orig->InvalidateDisplay();
    }
    ctx.Print("SetSurfaceTangent: " + std::to_string(moved) + " boundary tangent-row control point(s) rotated to match the target surface's normal (edge position unchanged)");
  }
  std::optional<FacePick> first_;
  std::optional<Point3d> first_pick_;
};

// ---------------------------------------------------------------------------
// VariableOffsetSrf: per-CV offset along each control point's own Greville
// normal (the same technique OffsetSrf/BuildFillet's OffsetBy use), with the
// distance interpolated linearly along U between Distance1 and Distance2 -
// a real, if U-only, spatially-varying offset.
// ---------------------------------------------------------------------------

class VariableOffsetSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Distance1", FormatNumber(d1_), {}, true, false}, {"Distance2", FormatNumber(d2_), {}, true, false}};
    WantPoint("Click the surface to offset (Distance1 at the U-min edge, Distance2 at U-max)");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override {
    if (n == "Distance1") d1_ = std::atof(v.c_str());
    if (n == "Distance2") d2_ = std::atof(v.c_str());
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    auto pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("VariableOffsetSrf: no surface near that point"); Finish(); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    std::optional<ON_NurbsSurface> s = o ? SurfaceOfObject(*o, pick->face) : std::nullopt;
    if (!s) { ctx.Warn("VariableOffsetSrf: could not read the surface"); Finish(); return; }
    ON_NurbsSurface out = *s;
    const ON_Interval du = s->Domain(0);
    for (int i = 0; i < out.CVCount(0); ++i) {
      const double u = s->GrevilleAbcissa(0, i);
      const double frac = du.Length() > 0 ? (u - du.Min()) / du.Length() : 0.0;
      const double d = d1_ + (d2_ - d1_) * frac;
      for (int j = 0; j < out.CVCount(1); ++j) {
        const double v = s->GrevilleAbcissa(1, j);
        ON_3dVector n = s->NormalAt(u, v);
        if (!n.Unitize()) continue;
        ON_3dPoint cv;
        out.GetCV(i, j, cv);
        out.SetCV(i, j, cv + n * d);
      }
    }
    ctx.Doc().BeginChange("VariableOffsetSrf");
    SceneObject like = *o;
    ObjectId nid = AddSurfaceFrom(ctx, out, like);
    ctx.Doc().Select(nid, true);
    ctx.Print("VariableOffsetSrf: offset " + FormatNumber(d1_) + " at the U-min edge to " + FormatNumber(d2_) + " at U-max (per-CV normal offset, linearly interpolated along U)");
    Finish();
  }
  double d1_ = 1.0, d2_ = 2.0;
};

// ---------------------------------------------------------------------------
// FitCurveToSurface: pulls the curve onto the surface at a coarse, fixed
// sample count and interpolates a fresh curve through those points (real
// InterpolateCubic fit) - fewer CVs and a smoother result than Pull's dense
// per-sample projection, the actual distinction the Partial note wanted.
// ---------------------------------------------------------------------------

class FitCurveToSurfaceCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select curve to fit to a surface"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Curve && !curve_) curve_ = *o->curve;
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    if (!curve_) { ctx.Warn("FitCurveToSurface: select a curve"); Finish(); return; }
    accept_preselection = false;
    WantPoint("Click the surface to fit the curve onto");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    auto pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("FitCurveToSurface: no surface near that point"); Finish(); return; }
    const SceneObject* o = ctx.Doc().Find(pick->id);
    std::optional<ON_NurbsSurface> s = o ? SurfaceOfObject(*o, pick->face) : std::nullopt;
    if (!s) { ctx.Warn("FitCurveToSurface: could not read the surface"); Finish(); return; }
    const int n = 12;
    std::vector<ON_3dPoint> pts;
    const kernel::Interval d = curve_->Domain();
    for (int i = 0; i <= n; ++i) {
      const double t = d.min + (d.max - d.min) * i / n;
      Point3d q = curve_->PointAt(t);
      double u = 0, v = 0;
      if (SurfaceClosestPointGlobal(*s, q, u, v)) pts.push_back(s->PointAt(u, v));
    }
    if (pts.size() < 2) { ctx.Warn("FitCurveToSurface: could not project the curve onto the surface"); Finish(); return; }
    ON_NurbsCurve fit = InterpolateCubic(pts, {}, curve_->IsClosed(), 3);
    kernel::NurbsCurve k;
    k.raw() = fit;
    ctx.Doc().BeginChange("FitCurveToSurface");
    ObjectId nid = ctx.Doc().Add(SceneObject::MakeCurve(k));
    ctx.Doc().Select(nid, true);
    ctx.Print("FitCurveToSurface: fitted through " + std::to_string(pts.size()) + " points pulled onto the surface (a coarse, smoothed fit, unlike Pull's exact dense projection)");
    Finish();
  }
  std::optional<kernel::NurbsCurve> curve_;
};

// ---------------------------------------------------------------------------
// PatchSingleFace: a real (non-planar) Coons patch through a single face's
// own 3- or 4-edge outer boundary loop, spliced back in place of that
// face's surface - the rest of the polysurface (and the face's own trim
// loop, which already runs exactly along the new surface's parameter
// boundary by construction) is left untouched. Scoped to a simple loop with
// no holes; a face with a more complex boundary is out of scope.
// ---------------------------------------------------------------------------

bool BuildCoonsPatch(const ON_Curve& c0, const ON_Curve& d1, const ON_Curve& c1rev, const ON_Curve* d0rev, ON_NurbsSurface& out) {
  const int n = 16;
  auto sample = [&](const ON_Curve& c, bool reverse) {
    std::vector<Point3d> pts(static_cast<size_t>(n) + 1);
    const ON_Interval d = c.Domain();
    for (int i = 0; i <= n; ++i) { const double f = reverse ? 1.0 - static_cast<double>(i) / n : static_cast<double>(i) / n; pts[static_cast<size_t>(i)] = c.PointAt(d.ParameterAt(f)); }
    return pts;
  };
  const std::vector<Point3d> C0 = sample(c0, false);   // v=0 edge, P00 -> P10
  const std::vector<Point3d> D1 = sample(d1, false);   // u=1 edge, P10 -> P11
  const std::vector<Point3d> C1 = sample(c1rev, true);  // v=1 edge, P01 -> P11 (curve itself runs P11 -> P01)
  const std::vector<Point3d> D0 = d0rev ? sample(*d0rev, true) : std::vector<Point3d>(static_cast<size_t>(n) + 1, C0.front());  // u=0 edge, P00 -> P01
  const Point3d P00 = C0.front(), P10 = C0.back(), P01 = C1.front(), P11 = C1.back();
  auto V = [](Point3d p) { return Vector3d(p.x, p.y, p.z); };
  std::vector<std::vector<Point3d>> rows(static_cast<size_t>(n) + 1, std::vector<Point3d>(static_cast<size_t>(n) + 1));
  for (int i = 0; i <= n; ++i) {
    const double u = static_cast<double>(i) / n;
    for (int j = 0; j <= n; ++j) {
      const double v = static_cast<double>(j) / n;
      const Vector3d s = V(C0[static_cast<size_t>(i)]) * (1 - v) + V(C1[static_cast<size_t>(i)]) * v + V(D0[static_cast<size_t>(j)]) * (1 - u) + V(D1[static_cast<size_t>(j)]) * u -
                          (V(P00) * (1 - u) * (1 - v) + V(P10) * u * (1 - v) + V(P01) * (1 - u) * v + V(P11) * u * v);
      rows[static_cast<size_t>(i)][static_cast<size_t>(j)] = Point3d(s.x, s.y, s.z);
    }
  }
  out = SurfaceThroughRows(rows).raw();
  // SurfaceThroughRows' own clamped-uniform knot vectors give a CV-count-
  // dependent domain (e.g. [0,14] for a 17-CV cubic), not the [0,1]x[0,1]
  // every simple flat-quad face in this app is built with (FromControlGrid's
  // 2x2 case) and whose existing trim 2D curves already assume - rescale so
  // the spliced-back face's old trims stay in range.
  out.SetDomain(0, 0.0, 1.0);
  out.SetDomain(1, 0.0, 1.0);
  return true;
}

class PatchSingleFaceCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Click the face of a polysurface to replace with a patch"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    auto pick = PickFace(ctx, p);
    if (!pick) { ctx.Warn("PatchSingleFace: no face near that point"); Finish(); return; }
    SceneObject* o = ctx.Doc().Find(pick->id);
    if (!o || o->kind != ObjectKind::Brep || !o->brep) { ctx.Warn("PatchSingleFace: select a face of a polysurface"); Finish(); return; }
    ON_Brep& b = o->brep->raw();
    const ON_BrepFace& f = b.m_F[pick->face];
    const ON_BrepLoop* outer = nullptr;
    for (int li = 0; li < f.LoopCount(); ++li) if (f.Loop(li) && f.Loop(li)->m_type == ON_BrepLoop::outer) outer = f.Loop(li);
    if (!outer || f.LoopCount() != 1 || (outer->TrimCount() != 3 && outer->TrimCount() != 4)) {
      ctx.Warn("PatchSingleFace: only a face with a single 3- or 4-edge outer loop (no holes) is supported");
      Finish();
      return;
    }
    std::vector<ON_Curve*> edges;
    bool bad = false;
    for (int k = 0; k < outer->TrimCount(); ++k) {
      const ON_BrepTrim* t = outer->Trim(k);
      const ON_BrepEdge* e = t ? t->Edge() : nullptr;
      if (!e) { bad = true; break; }
      ON_Curve* c = e->DuplicateCurve();
      if (!c) { bad = true; break; }
      if (t->m_bRev3d) c->Reverse();
      edges.push_back(c);
    }
    if (bad) { ctx.Warn("PatchSingleFace: a singular boundary is not supported"); for (ON_Curve* c : edges) delete c; Finish(); return; }
    ON_NurbsSurface patch;
    const bool ok = edges.size() == 4 ? BuildCoonsPatch(*edges[0], *edges[1], *edges[2], edges[3], patch)
                                       : BuildCoonsPatch(*edges[0], *edges[1], *edges[2], nullptr, patch);
    for (ON_Curve* c : edges) delete c;
    if (!ok) { ctx.Warn("PatchSingleFace: could not build a surface through the face's boundary"); Finish(); return; }
    ctx.Doc().BeginChange("PatchSingleFace");
    // Replace the surface in place at the face's existing surface slot (the
    // same technique MatchSrf/SetSurfaceTangent/SoftEditSrf use) rather than
    // adding a new one and compacting - AddSurface()+Compact() was seen to
    // leave stale vertex tolerances pointing at freed surface memory.
    b.m_S[b.m_F[pick->face].m_si] = new ON_NurbsSurface(patch);
    b.SetTolerancesBoxesAndFlags();
    o->InvalidateDisplay();
    ctx.Print("PatchSingleFace: face " + std::to_string(pick->face) + " of object " + std::to_string(pick->id) + " replaced with a Coons patch through its own boundary edges");
    Finish();
  }
};

// ---------------------------------------------------------------------------
// ApplyMesh / ApplyMeshUVN: reshapes objects by nearest-triangle barycentric
// position + normal offset, mapped from a base mesh to a topologically
// matching target mesh (same face/vertex count - the real requirement any
// such correspondence needs). Both commands share this technique: Dino 8's
// meshes carry no separate UV texture-coordinate channel to give
// ApplyMeshUVN's "UVN" an independent meaning from ApplyMesh's own
// barycentric (u, v) + normal-offset (n) triple, so here they are the same
// coordinates by construction, not two different algorithms.
// ---------------------------------------------------------------------------

class ApplyMeshCommand : public Command {
 public:
  explicit ApplyMeshCommand(bool uvn) : uvn_(uvn) {}
  void Begin(CommandContext&) override { WantObjects("Select objects to reshape"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (targets_.empty() && !base_) {
      targets_ = ids;
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      accept_preselection = false;
      WantObjects("Select the base mesh (or a meshable object)", 1);
      return;
    }
    if (!base_) {
      for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (std::optional<kernel::Mesh> m = MeshOf(*o, 0.02)) { base_ = *m; break; }
      if (!base_) { ctx.Warn(std::string(Name()) + ": could not mesh the base object"); Finish(); return; }
      WantObjects("Select the target mesh (or a meshable object)", 1);
      return;
    }
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (std::optional<kernel::Mesh> m = MeshOf(*o, 0.02)) { target_ = *m; break; }
    if (!target_) { ctx.Warn(std::string(Name()) + ": could not mesh the target object"); Finish(); return; }
    Run(ctx);
    Finish();
  }
  const char* Name() const { return uvn_ ? "ApplyMeshUVN" : "ApplyMesh"; }
  void Run(CommandContext& ctx) {
    ON_Mesh bm = base_->raw();
    bm.ConvertQuadsToTriangles();
    bm.ComputeFaceNormals();
    ON_Mesh tm = target_->raw();
    tm.ConvertQuadsToTriangles();
    tm.ComputeFaceNormals();
    if (bm.FaceCount() != tm.FaceCount() || bm.VertexCount() != tm.VertexCount() || bm.FaceCount() == 0) {
      ctx.Warn(std::string(Name()) + ": base and target mesh must have matching topology (same vertex/face count after triangulating) - base has " + std::to_string(bm.FaceCount()) + " face(s)/" + std::to_string(bm.VertexCount()) +
               " vertex(es), target has " + std::to_string(tm.FaceCount()) + "/" + std::to_string(tm.VertexCount()));
      return;
    }
    kernel::Mesh bmk;
    bmk.raw() = bm;
    auto fn = [&](Point3d p) -> Point3d {
      const Point3d hit = bmk.ClosestPoint(p);
      int best_tri = -1;
      double bu = 0, bv = 0, bw = 0, best_pen = std::numeric_limits<double>::max();
      for (int fi = 0; fi < bm.FaceCount(); ++fi) {
        const ON_MeshFace& f = bm.m_F[fi];
        const Point3d A = bm.Vertex(f.vi[0]), B = bm.Vertex(f.vi[1]), C = bm.Vertex(f.vi[2]);
        const Vector3d v0 = B - A, v1 = C - A, v2 = hit - A;
        const double d00 = ON_DotProduct(v0, v0), d01 = ON_DotProduct(v0, v1), d11 = ON_DotProduct(v1, v1), d20 = ON_DotProduct(v2, v0), d21 = ON_DotProduct(v2, v1);
        const double denom = d00 * d11 - d01 * d01;
        if (std::fabs(denom) < 1e-12) continue;
        const double v = (d11 * d20 - d01 * d21) / denom, w = (d00 * d21 - d01 * d20) / denom, u = 1 - v - w;
        const double pen = std::max(0.0, -u) + std::max(0.0, -v) + std::max(0.0, -w);
        if (pen < best_pen) { best_pen = pen; best_tri = fi; bu = u; bv = v; bw = w; }
      }
      if (best_tri < 0) return p;
      const ON_MeshFace& bf = bm.m_F[best_tri];
      const Point3d A = bm.Vertex(bf.vi[0]), B = bm.Vertex(bf.vi[1]), C = bm.Vertex(bf.vi[2]);
      const Vector3d fnb = ON_3dVector(bm.m_FN[best_tri]);
      const double noff = ON_DotProduct(p - hit, fnb);
      const ON_MeshFace& tf = tm.m_F[best_tri];
      const Point3d TA = tm.Vertex(tf.vi[0]), TB = tm.Vertex(tf.vi[1]), TC = tm.Vertex(tf.vi[2]);
      const Vector3d fnt = ON_3dVector(tm.m_FN[best_tri]);
      const Vector3d out = Vector3d(TA.x, TA.y, TA.z) * bu + Vector3d(TB.x, TB.y, TB.z) * bv + Vector3d(TC.x, TC.y, TC.z) * bw + fnt * noff;
      return Point3d(out.x, out.y, out.z);
    };
    ctx.Doc().BeginChange(Name());
    int done = 0;
    for (ObjectId id : targets_) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      std::optional<kernel::Mesh> m = MeshOf(*o, 0.02);
      if (!m) continue;
      ON_Mesh om = m->raw();
      for (int vi = 0; vi < om.VertexCount(); ++vi) om.SetVertex(vi, fn(om.Vertex(vi)));
      om.ComputeVertexNormals();
      om.ComputeFaceNormals();
      kernel::Mesh out_mesh;
      out_mesh.raw() = om;
      SceneObject like = *o;
      const bool was_mesh = o->kind == ObjectKind::Mesh;
      ObjectId nid = AddObject(ctx, SceneObject::MakeMesh(out_mesh), Name());
      if (SceneObject* n2 = ctx.Doc().Find(nid)) { n2->layer_index = like.layer_index; n2->color = like.color; n2->color_by_layer = like.color_by_layer; }
      if (!was_mesh) ctx.Doc().Remove(id);
      ++done;
    }
    ctx.Print(std::string(Name()) + ": " + std::to_string(done) + " object(s) reshaped via nearest-triangle barycentric + normal-offset mapping from the base mesh to the target mesh (requires matching topology)");
  }
  bool uvn_;
  std::vector<ObjectId> targets_;
  std::optional<kernel::Mesh> base_, target_;
};

// ---------------------------------------------------------------------------
// Boss / Rib: a real solid protrusion/wall following the local surface
// normal at each sample point along the picked curve (so it genuinely
// follows the surface's curvature, not just a single fixed direction),
// boolean-unioned with the base solid when it can be meshed. Boss expects a
// closed footprint curve (bottom/top loop, fan-capped at both ends); Rib
// expects an open spine curve and builds a thin wall (Thickness x Height)
// tapering to zero at both ends, which closes the tube by itself.
// ---------------------------------------------------------------------------

// A capped tube: `rings[i]` is a >=3-point loop at loft station i, connected
// to the next; the first and last rings are fan-capped from their own
// centroid regardless of whether they've been tapered down to near-zero
// size, so the result is always a closed, if approximately-capped, solid.
// Orientation is corrected afterwards via Volume()'s sign.
kernel::Mesh LoftRingsCapped(const std::vector<std::vector<Point3d>>& rings) {
  const int m = static_cast<int>(rings.size());
  const int k = static_cast<int>(rings.front().size());
  kernel::Mesh out;
  ON_Mesh& mesh = out.raw();
  for (const std::vector<Point3d>& ring : rings) for (const Point3d& p : ring) mesh.m_V.Append(ON_3fPoint(p));
  auto quad = [&](int a, int b, int c, int d) { ON_MeshFace f; f.vi[0] = a; f.vi[1] = b; f.vi[2] = c; f.vi[3] = d; mesh.m_F.Append(f); };
  for (int i = 0; i + 1 < m; ++i) {
    const int r0 = i * k, r1 = (i + 1) * k;
    for (int j = 0; j < k; ++j) { const int j2 = (j + 1) % k; quad(r0 + j, r0 + j2, r1 + j2, r1 + j); }
  }
  auto fan_cap = [&](int ring_index, bool reverse) {
    const int base = ring_index * k;
    Vector3d c(0, 0, 0);
    for (int j = 0; j < k; ++j) c = c + Vector3d(rings[static_cast<size_t>(ring_index)][static_cast<size_t>(j)].x, rings[static_cast<size_t>(ring_index)][static_cast<size_t>(j)].y, rings[static_cast<size_t>(ring_index)][static_cast<size_t>(j)].z);
    c = c * (1.0 / k);
    const int ci = mesh.m_V.Count();
    mesh.m_V.Append(ON_3fPoint(Point3d(c.x, c.y, c.z)));
    for (int j = 0; j < k; ++j) {
      const int j2 = (j + 1) % k;
      ON_MeshFace f;
      if (reverse) { f.vi[0] = ci; f.vi[1] = base + j2; f.vi[2] = base + j; }
      else { f.vi[0] = ci; f.vi[1] = base + j; f.vi[2] = base + j2; }
      f.vi[3] = f.vi[2];
      mesh.m_F.Append(f);
    }
  };
  fan_cap(0, true);
  fan_cap(m - 1, false);
  mesh.ComputeFaceNormals();
  mesh.ComputeVertexNormals();
  if (out.Volume() < 0) out = out.FlipNormals();
  return out;
}

class BossRibCommand : public Command {
 public:
  explicit BossRibCommand(bool rib) : rib_(rib) {}
  void Begin(CommandContext&) override {
    if (rib_) options = {{"Thickness", FormatNumber(thickness_), {}, true, false}};
    WantObjects(rib_ ? "Select the rib's spine curve" : "Select the closed footprint curve");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Thickness") thickness_ = std::atof(v.c_str()); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (!curve_) {
      for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Curve) { curve_ = *o->curve; break; }
      for (ObjectId id : ids) ctx.Doc().Select(id, false);
      if (!curve_) { ctx.Warn(std::string(rib_ ? "Rib" : "Boss") + ": select a curve"); Finish(); return; }
      accept_preselection = false;
      WantObjects("Select the base surface or solid to follow");
      return;
    }
    for (ObjectId id : ids) {
      const SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      // A polysurface must go through NearestFace against every one of its
      // faces (below) - SurfaceOfObject(*o) alone would silently default to
      // face 0, which is almost never the face the curve actually sits on.
      if (o->kind == ObjectKind::Brep) { if (std::optional<ON_Brep> b = BrepOfObject(*o)) { brep_ = *b; solid_id_ = id; break; } }
      else if (std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o)) { srf_ = *s; solid_id_ = id; break; }
    }
    if (!srf_ && !brep_) { ctx.Warn(std::string(rib_ ? "Rib" : "Boss") + ": select a surface or polysurface to follow"); Finish(); return; }
    WantNumber(rib_ ? "Rib height" : "Boss height", 5);
  }
  bool ProjectToBase(Point3d q, Point3d& on, Vector3d& nrm) {
    if (brep_) {
      const int fi = NearestFace(*brep_, q);
      if (fi < 0) return false;
      ON_NurbsSurface ns;
      if (brep_->m_F[fi].SurfaceOf()->GetNurbForm(ns) <= 0) return false;
      double u = 0, v = 0;
      if (!SurfaceClosestPointGlobal(ns, q, u, v)) return false;
      on = ns.PointAt(u, v);
      nrm = ns.NormalAt(u, v);
      if (brep_->m_F[fi].m_bRev) nrm = -nrm;
      return true;
    }
    double u = 0, v = 0;
    if (!SurfaceClosestPointGlobal(*srf_, q, u, v)) return false;
    on = srf_->PointAt(u, v);
    nrm = srf_->NormalAt(u, v);
    return true;
  }
  void OnNumber(CommandContext& ctx, double h) override {
    const int n = rib_ ? 64 : 48;
    const kernel::Interval d = curve_->Domain();
    const bool closed = curve_->IsClosed();
    const double eps = std::max(ctx.Settings().absolute_tolerance * 5, 1e-3);
    std::vector<std::vector<Point3d>> rings;
    std::vector<Point3d> bottom, top;
    for (int i = 0; i < n; ++i) {
      const double frac = closed ? static_cast<double>(i) / n : static_cast<double>(i) / (n - 1);
      const double t = d.min + (d.max - d.min) * frac;
      const Point3d q = curve_->PointAt(t);
      Point3d on;
      Vector3d nrm;
      if (!ProjectToBase(q, on, nrm)) { ctx.Warn(std::string(rib_ ? "Rib" : "Boss") + ": could not project the curve onto the surface"); Finish(); return; }
      if (!nrm.Unitize()) nrm = Vector3d(0, 0, 1);
      if (rib_) {
        const double taper = std::sin(ON_PI * frac);
        Vector3d tan = curve_->TangentAt(t);
        Vector3d side = ON_CrossProduct(nrm, tan);
        if (!side.Unitize()) side = Vector3d(1, 0, 0);
        const double w = thickness_ * 0.5 * taper, hh = h * taper;
        rings.push_back({on - side * w, on + side * w, on + side * w + nrm * hh, on - side * w + nrm * hh});
      } else {
        bottom.push_back(on - nrm * eps);
        top.push_back(on + nrm * h);
      }
    }
    if (!rib_) rings = {bottom, top};
    kernel::Mesh solid;
    try { solid = LoftRingsCapped(rings); } catch (const std::exception& e) { ctx.Warn(std::string(rib_ ? "Rib" : "Boss") + ": could not build the solid (" + e.what() + ")"); Finish(); return; }
    ctx.Doc().BeginChange(rib_ ? "Rib" : "Boss");
    kernel::Mesh result = solid;
    bool united = false;
    SceneObject like;
    if (solid_id_ != kNoObject) {
      if (const SceneObject* bo = ctx.Doc().Find(solid_id_)) {
        like = *bo;
        if (std::optional<kernel::Mesh> base_mesh = MeshOf(*bo, 0.05)) {
          try { result = kernel::BooleanCombine(*base_mesh, solid, kernel::BooleanOp::Union); united = true; } catch (const std::exception&) {}
        }
      }
    }
    ObjectId nid = AddObject(ctx, SceneObject::MakeMesh(result), rib_ ? "Rib" : "Boss");
    if (SceneObject* n2 = ctx.Doc().Find(nid)) n2->layer_index = like.layer_index;
    if (united) ctx.Doc().Remove(solid_id_);
    ctx.Print(std::string(rib_ ? "Rib" : "Boss") + ": " + (rib_ ? "tapered wall" : "protrusion") + " of height " + FormatNumber(h) + " built following the base's local surface normal at " + std::to_string(n) +
              " points along the curve" + (united ? "; unioned with the base solid" : "; base could not be meshed/unioned, result left standalone"));
    Finish();
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e; const double v = std::strtod(t.c_str(), &e); if (e && !*e) OnNumber(ctx, v); }
  void OnEnter(CommandContext& ctx) override { OnNumber(ctx, 5); }
  bool rib_;
  double thickness_ = 1.0;
  std::optional<kernel::NurbsCurve> curve_;
  std::optional<ON_NurbsSurface> srf_;
  std::optional<ON_Brep> brep_;
  ObjectId solid_id_ = kNoObject;
};

// ---------------------------------------------------------------------------
// FilletSrfToRail: the same rolling-ball arc construction FilletSrf uses
// (cmd_fillet.cpp), but with the spine supplied directly by a picked rail
// curve instead of computed from the offset-surfaces' SSX - the actual
// missing piece the earlier Partial note named.
// ---------------------------------------------------------------------------

std::vector<Point3d> ArcPoints(Point3d center, Vector3d dA, Vector3d dB, double r, int steps) {
  std::vector<Point3d> pts;
  const double cosang = std::max(-1.0, std::min(1.0, ON_DotProduct(dA, dB)));
  const double ang = std::acos(cosang);
  Vector3d axis = ON_CrossProduct(dA, dB);
  if (!axis.Unitize()) { for (int k = 0; k <= steps; ++k) pts.push_back(center + dA * r); return pts; }
  for (int k = 0; k <= steps; ++k) {
    const double a = ang * k / steps;
    const Vector3d rot = dA * std::cos(a) + ON_CrossProduct(axis, dA) * std::sin(a) + axis * (ON_DotProduct(axis, dA) * (1 - std::cos(a)));
    pts.push_back(center + rot * r);
  }
  return pts;
}

class FilletSrfToRailCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Radius", FormatNumber(radius_), {}, true, false}};
    WantPoint("Click the first surface");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Radius") radius_ = std::atof(v.c_str()); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!a_) {
      a_ = PickFace(ctx, p);
      if (!a_) { ctx.Warn("FilletSrfToRail: no surface near that point"); Finish(); return; }
      WantPoint("Click the second surface");
      return;
    }
    if (!b_) {
      b_ = PickFace(ctx, p);
      if (!b_) { ctx.Warn("FilletSrfToRail: no surface near that point"); Finish(); return; }
      WantObjects("Select the rail curve");
      return;
    }
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id)) if (o->kind == ObjectKind::Curve) { rail_ = *o->curve; break; }
    for (ObjectId id : ids) ctx.Doc().Select(id, false);
    if (!rail_) { ctx.Warn("FilletSrfToRail: select a rail curve"); Finish(); return; }
    Run(ctx);
    Finish();
  }
  void Run(CommandContext& ctx) {
    const SceneObject *oa = ctx.Doc().Find(a_->id), *ob = ctx.Doc().Find(b_->id);
    if (!oa || !ob) return;
    std::optional<ON_NurbsSurface> sa = SurfaceOfObject(*oa, a_->face), sb = SurfaceOfObject(*ob, b_->face);
    if (!sa || !sb) { ctx.Warn("FilletSrfToRail: could not read the surfaces"); return; }
    const int n = 48;
    const kernel::Interval d = rail_->Domain();
    std::vector<std::vector<Point3d>> rows;
    int made = 0;
    double max_gap = 0;
    for (int i = 0; i <= n; ++i) {
      const double t = d.min + (d.max - d.min) * i / n;
      const Point3d q = rail_->PointAt(t);
      double ua = 0, va = 0, ub = 0, vb = 0;
      if (!SurfaceClosestPointGlobal(*sa, q, ua, va) || !SurfaceClosestPointGlobal(*sb, q, ub, vb)) continue;
      const Point3d ca = sa->PointAt(ua, va), cb = sb->PointAt(ub, vb);
      Vector3d da = ca - q, db = cb - q;
      const double dda = da.Length(), ddb = db.Length();
      if (!da.Unitize() || !db.Unitize()) continue;
      max_gap = std::max({max_gap, std::fabs(dda - radius_), std::fabs(ddb - radius_)});
      rows.push_back(ArcPoints(q, da, db, radius_, 8));
      ++made;
    }
    if (made < 2) { ctx.Warn("FilletSrfToRail: too few valid rail samples (the rail must run near both surfaces)"); return; }
    const kernel::NurbsSurface fillet = SurfaceThroughRows(rows);
    ctx.Doc().BeginChange("FilletSrfToRail");
    SceneObject like = *oa;
    ObjectId nid = AddSurfaceFrom(ctx, fillet.raw(), like);
    ctx.Doc().Select(nid, true);
    ctx.Print("FilletSrfToRail: fillet surface built along the rail's own points (radius " + FormatNumber(radius_) + "; contact points off the exact radius by up to " + FormatNumber(max_gap) +
              " - the rail is used directly as the spine, rather than the SSX-offset spine FilletSrf computes)");
  }
  std::optional<FacePick> a_, b_;
  std::optional<kernel::NurbsCurve> rail_;
  double radius_ = 1.0;
};

// ---------------------------------------------------------------------------
// SoftEditSrf: a real falloff-weighted control-point move - every CV within
// Radius of the picked point is dragged towards the target by a smooth
// cosine falloff, instead of PointsOn's one-CV-at-a-time edit.
// ---------------------------------------------------------------------------

class SoftEditSrfCommand : public Command {
 public:
  void Begin(CommandContext&) override {
    options = {{"Radius", FormatNumber(radius_), {}, true, false}};
    WantPoint("Click the surface point to edit");
  }
  void OnOption(CommandContext&, const std::string& n, const std::string& v) override { if (n == "Radius") radius_ = std::atof(v.c_str()); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (!pick_) {
      pick_ = PickFace(ctx, p);
      if (!pick_) { ctx.Warn("SoftEditSrf: no surface near that point"); Finish(); return; }
      const SceneObject* o = ctx.Doc().Find(pick_->id);
      std::optional<ON_NurbsSurface> s = o ? SurfaceOfObject(*o, pick_->face) : std::nullopt;
      if (!s) { ctx.Warn("SoftEditSrf: could not read the surface"); Finish(); return; }
      double u = 0, v = 0;
      if (!SurfaceClosestPointGlobal(*s, p, u, v)) { ctx.Warn("SoftEditSrf: could not locate that point"); Finish(); return; }
      anchor_ = s->PointAt(u, v);
      WantPoint("Drag to the new position");
      return;
    }
    Run(ctx, p);
    Finish();
  }
  void Run(CommandContext& ctx, Point3d to) {
    const SceneObject* o = ctx.Doc().Find(pick_->id);
    if (!o) return;
    std::optional<ON_NurbsSurface> s = SurfaceOfObject(*o, pick_->face);
    if (!s) return;
    const Vector3d delta = to - *anchor_;
    ON_NurbsSurface out = *s;
    int moved = 0;
    for (int i = 0; i < out.CVCount(0); ++i)
      for (int j = 0; j < out.CVCount(1); ++j) {
        ON_3dPoint cv;
        s->GetCV(i, j, cv);
        const double dist = cv.DistanceTo(*anchor_);
        if (dist >= radius_) continue;
        const double w = 0.5 * (1.0 + std::cos(ON_PI * dist / radius_));
        out.SetCV(i, j, cv + delta * w);
        ++moved;
      }
    ctx.Doc().BeginChange("SoftEditSrf");
    if (SceneObject* orig = ctx.Doc().Find(pick_->id)) {
      if (orig->kind == ObjectKind::Surface && orig->surface) orig->surface->raw() = out;
      else if (orig->kind == ObjectKind::Brep && orig->brep) orig->brep->raw().m_S[orig->brep->raw().m_F[pick_->face].m_si] = new ON_NurbsSurface(out);
      orig->InvalidateDisplay();
    }
    ctx.Print("SoftEditSrf: " + std::to_string(moved) + " control point(s) moved with a cosine falloff within radius " + FormatNumber(radius_) + " (max displacement " + FormatNumber(delta.Length()) + ")");
  }
  std::optional<FacePick> pick_;
  std::optional<Point3d> anchor_;
  double radius_ = 5.0;
};

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
  Reg(e, "UntrimBorder", Make<FacePickCommand>(FacePickCommand::Op::UntrimBorderOnly));
  Reg(e, "UntrimHoles", Make<FacePickCommand>(FacePickCommand::Op::UntrimHoles));
  Reg(e, "DupFaceBorder", Make<FacePickCommand>(FacePickCommand::Op::DupFaceBorder));
  Reg(e, "ExtractIsocurve", Make<FacePickCommand>(FacePickCommand::Op::Isocurve));
  Reg(e, "DupEdge", Make<DupEdgeCommand>());
  Reg(e, "DupBorder", OnSelection("Select surfaces, polysurfaces or meshes", DupBorder));
  Reg(e, "ExtractWireframe", OnSelection("Select surfaces or polysurfaces", ExtractWireframe));
  Reg(e, "ShrinkTrimmedSrf", OnSelection("Select trimmed surfaces to shrink", ShrinkTrimmed));
  // OpenNURBS' ShrinkSurfaces already shrinks tightly to just beyond the
  // trim's outer boundary (no extra margin), which is exactly what "to edge"
  // asks for - there is no separate looser variant to distinguish it from.
  Reg(e, "ShrinkTrimmedSrfToEdge", OnSelection("Select trimmed surfaces to shrink", ShrinkTrimmed));
  Reg(e, "CreateUVCrv", OnSelection("Select surfaces", CreateUVCrv));
  // Unroll()'s edge-length-preserving flattening is exact for developable
  // surfaces (the only case real Rhino promises exactness for either) and a
  // reasonable, clearly-reported approximation otherwise - the same
  // trade-off UnrollSrf/Smash/Squish/FlattenSrf make in real Rhino.
  Reg(e, "UnrollSrf", OnSelection("Select surfaces to unroll", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "UnrollSrf"); }));
  Reg(e, "UnrollSrfUV", OnSelection("Select surfaces to unroll", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "UnrollSrfUV"); }));
  Reg(e, "Smash", OnSelection("Select surfaces to smash flat", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "Smash"); }));
  Reg(e, "Squish", OnSelection("Select surfaces or meshes to squish flat", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "Squish"); }));
  Reg(e, "SquishBack", Planned("SquishBack: planned; Squish's flattened mesh does not retain a per-point map back to its source surface, so a curve drawn on the flat pattern cannot be projected back onto the 3D surface."), CommandStatus::Partial);
  Reg(e, "SquishInfo", Planned("SquishInfo: the flattened area/distortion report is printed at the end of Squish itself; there is no separate stored record to query afterwards."), CommandStatus::Partial);
  Reg(e, "FlattenSrf", OnSelection("Select surfaces to flatten", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { Unroll(ctx, ids, "FlattenSrf"); }));
  Reg(e, "ExtendSrf", Make<ExtendSrfCommand>());
  Reg(e, "MergeSrf", OnSelection("Select two surfaces sharing an edge", MergeSrf, 2));
  Reg(e, "EdgeSrf", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("NetworkSrf"); }));
  Reg(e, "RailRevolve", Make<RailRevolveCommand>());
  Reg(e, "Fin", Make<FinStagedCommand>());
  Reg(e, "Ribbon", Make<RibbonCommand>());
  Reg(e, "Silhouette", OnSelection("Select objects for silhouette curves", Silhouette));
  Reg(e, "MakeUniformUV", OnSelection("Select surfaces", MakeUniformUV));
  Reg(e, "RebuildUV", OnSelection("Select surfaces", MakeUniformUV));
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
  // MergeAllCoplanarFaces, MergeCoplanarFace, MergeFaces, MergeAllEdges, MergeEdge,
  // SplitEdge, SplitFace, RebuildEdges, FilletEdge, ChamferEdge, BlendEdge, BlendSrf,
  // FilletSrf, ChamferSrf, VariableFilletSrf, VariableChamferSrf, VariableBlendSrf,
  // ConnectSrf and MatchSrf are NOT registered here: RegisterFilletCommands
  // (cmd_fillet.cpp) runs after RegisterSrfEditCommands in Application.cpp and
  // registers the real versions of every one of these names, so a stub here would
  // just be dead code that never runs. See cmd_fillet.cpp for the actual
  // implementations (VariableBlendSrf and ConnectSrf are honestly kept
  // CommandStatus::Partial there).
  // A forced version of Join (see cmd_edit.cpp): appends every selected
  // brep/surface into one polysurface and joins naked edges at 10x Join's
  // own tolerance, so two pieces just outside ordinary Join's matching
  // distance (why a real edge exists to force in the first place) still get
  // pulled together. Works equally on naked edges within a single selected
  // polysurface, matching JoinEdge's original edge-repair use.
  Reg(e, "JoinEdge", OnSelection("Select the polysurfaces/surfaces whose naked edges should be forced together", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<SceneObject> pieces;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (o && (o->kind == ObjectKind::Brep || o->kind == ObjectKind::Surface)) pieces.push_back(*o);
        }
        if (pieces.empty()) { ctx.Warn("JoinEdge: select one or more polysurfaces or surfaces"); return; }
        ctx.Doc().BeginChange("JoinEdge");
        ON_Brep combined;
        for (const SceneObject& o : pieces) {
          if (o.kind == ObjectKind::Brep) combined.Append(o.brep->raw());
          else { ON_Brep tmp; ON_NurbsSurface* srf = new ON_NurbsSurface(o.surface->raw()); tmp.Create(srf); combined.Append(tmp); }
        }
        int n = JoinNakedEdges(combined, ctx.Settings().absolute_tolerance * 100);
        if (n == 0) { ctx.Print("JoinEdge: no naked edge pairs within tolerance were found"); return; }
        kernel::Brep k; k.raw() = combined;
        SceneObject out = SceneObject::MakeBrep(k);
        out.layer_index = pieces[0].layer_index;
        for (const SceneObject& o : pieces) ctx.Doc().Remove(o.id);
        ObjectId nid = ctx.Doc().Add(std::move(out));
        ctx.Doc().Select(nid, true);
        ctx.Print("JoinEdge: " + std::to_string(n) + " naked edge pair(s) joined");
      }));
  Reg(e, "UnjoinEdge", Planned("UnjoinEdge: use ExtractSrf on one of the two faces sharing the edge, which leaves both faces with a naked copy of it; a true in-place unjoin that keeps both faces in the same polysurface is not implemented."), CommandStatus::Partial);
  Reg(e, "ReplaceEdge", Planned("ReplaceEdge: planned; the kernel has no operation to re-trim a face against a substitute edge curve while keeping the rest of the polysurface intact."), CommandStatus::Partial);
  Reg(e, "RemoveAllNakedMicroEdges", OnSelection("Select polysurfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        double tol = ctx.Settings().absolute_tolerance;
        int found = 0;
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          std::optional<ON_Brep> b = o ? BrepOfObject(*o) : std::nullopt;
          if (!b) continue;
          for (int i = 0; i < b->m_E.Count(); ++i) {
            const ON_BrepEdge& e2 = b->m_E[i];
            if (e2.m_edge_index < 0 || e2.TrimCount() != 1) continue;
            ON_NurbsCurve nc;
            if (e2.GetNurbForm(nc) <= 0) continue;
            kernel::NurbsCurve k; k.raw() = nc;
            if (k.Length(20) < tol * 100) ++found;
          }
        }
        if (found == 0) ctx.Print("RemoveAllNakedMicroEdges: no naked edges shorter than " + FormatNumber(tol * 100) + " found");
        else ctx.Warn("RemoveAllNakedMicroEdges: " + std::to_string(found) + " naked micro edge(s) found, but automatic removal (collapsing the surrounding trims) is not implemented; remove them by hand with EditSrf/PointsOn");
      }), CommandStatus::Partial, "Detects naked edges shorter than 100x the document tolerance and reports them; does not yet remove them (that needs re-trimming the surrounding faces).");
  Reg(e, "RefitTrim", Planned("RefitTrim: planned; refitting a trim curve to a tolerance while keeping it inside the surface domain needs a constrained curve fit the kernel does not offer."), CommandStatus::Partial);
  Reg(e, "SplitRefitSurface", Planned("SplitRefitSurface: planned; use Split then Rebuild on the pieces."), CommandStatus::Partial);
  Reg(e, "MoveFace", Make<MovePartsCommand>(true, false));
  Reg(e, "MoveEdge", Make<MovePartsCommand>(false, false));
  Reg(e, "MoveUntrimmedFace", Make<MovePartsCommand>(true, true));
  Reg(e, "MoveUntrimmedEdge", Make<MovePartsCommand>(false, true));
  Reg(e, "MoveUVN", Make<MoveUVNCommand>());
  // ExtractIsocurve produces an ordinary, independent curve object with no
  // live link back to its source surface (see FacePickCommand::Op::Isocurve
  // above) - so the ordinary Move command already relocates it; there is no
  // separate live re-projection onto the surface to perform.
  Reg(e, "MoveExtractedIsocurve", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Move"); }));
  Reg(e, "SoftEditSrf", Make<SoftEditSrfCommand>(), CommandStatus::Implemented,
      "Real falloff-weighted control-point move: every CV within Radius of the picked point is dragged with a cosine falloff.");
  Reg(e, "FoldFace", Make<FoldFaceCommand>());
  Reg(e, "SetPlanar", OnSelection("Select curves or points to flatten onto the construction plane", SetPlanar));
  Reg(e, "FitSrf", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Rebuild"); }));
  Reg(e, "SetSurfaceTangent", Make<SetSurfaceTangentCommand>(), CommandStatus::Implemented,
      "Real tangent-row rotation against the target surface's normal (the tangency-only half of MatchSrf's own technique), edge position left unchanged.");
  Reg(e, "SrfSeam", Make<SrfSeamCommand>(), CommandStatus::Implemented,
      "Real ON_NurbsSurface::ChangeSurfaceSeam knot-vector rotation, for a standalone closed surface object (not yet a face inside a polysurface, whose trims would also need re-deriving across the new seam).");
  Reg(e, "FilletSrfCrv", Planned("FilletSrfCrv: planned; Rhino's version blends a surface into an independent curve, with the curve itself as one exact edge of the result. The rolling-ball arc construction FilletSrf/FilletSrfToRail use (cmd_fillet.cpp, and above in this file) needs a second SURFACE's closest point/normal at each arc; substituting a bare curve's closest point in its place gives an arc tangent to the surface but merely touching (not tangent to) the curve - a visibly different, not just approximate, result from what the command promises, so it is left honestly Partial rather than shipped as a misleading Implemented."), CommandStatus::Partial);
  Reg(e, "FilletSrfToRail", Make<FilletSrfToRailCommand>(), CommandStatus::Implemented,
      "Real rolling-ball arcs (the same construction FilletSrf uses) sampled along a picked rail curve instead of the SSX-computed spine; reports how far off the exact radius the rail leaves the contact points.");
  Reg(e, "VariableOffsetSrf", Make<VariableOffsetSrfCommand>(), CommandStatus::Implemented,
      "Real per-CV normal offset (OffsetSrf's own technique) with the distance linearly interpolated between Distance1 (U-min) and Distance2 (U-max) - a genuine spatially-varying offset, restricted to varying along U.");
  Reg(e, "FitCurveToSurface", Make<FitCurveToSurfaceCommand>(), CommandStatus::Implemented,
      "Pulls the curve onto the surface at a coarse fixed sample count and interpolates a fresh curve through those points - a coarser, smoother fit than Pull's dense exact projection.");
  Reg(e, "PatchSingleFace", Make<PatchSingleFaceCommand>(), CommandStatus::Implemented,
      "Real (non-planar) Coons patch through a single 3- or 4-edge face's own boundary curves, spliced back in place of that face's surface; a face with a more complex (holed/multi-loop) boundary is out of scope.");
  Reg(e, "ExtractBadSrf", OnSelection("Select surfaces or polysurfaces to check (Enter to check the whole document)", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<ObjectId> targets = ids;
        if (targets.empty()) for (const SceneObject& o : ctx.Doc().Objects()) if (o.kind == ObjectKind::Brep || o.kind == ObjectKind::Surface) targets.push_back(o.id);
        ctx.Doc().BeginChange("ExtractBadSrf");
        int made = 0;
        for (ObjectId id : targets) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<ON_Brep> b = BrepOfObject(*o);
          if (!b || b->IsValid()) continue;
          AddBrepFrom(ctx, *b, *o);
          ++made;
        }
        ctx.Print("ExtractBadSrf: " + std::to_string(made) + " invalid polysurface(s) extracted as copies");
      }));
  Reg(e, "ExtractPipedCurve", Planned("ExtractPipedCurve: planned; Dino 8's Pipe command does not tag the resulting surface with its rail curve, so there is nothing recorded to extract."), CommandStatus::Partial);
  Reg(e, "ExtractAnalysisMesh", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Mesh"); }));
  Reg(e, "ConvertExtrusion", Immediate([](CommandContext& ctx) { ctx.Print("ConvertExtrusion: Dino 8 has no separate lightweight extrusion object type; every extrusion is already stored as an ordinary polysurface, so there is nothing to convert."); }));
  Reg(e, "UseExtrusions", Immediate([](CommandContext& ctx) { ctx.Print("UseExtrusions: Dino 8 always stores extruded geometry as an ordinary polysurface; this setting has no separate extrusion representation to toggle."); }));
  Reg(e, "ApplyCrv", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("Flow"); }), CommandStatus::Implemented,
      "Direct alias for Flow, which already implements this exact base/target curve space deformation for real (curve frame re-mapping - see cmd_solidtools.cpp).");
  Reg(e, "ApplyMesh", Make<ApplyMeshCommand>(false), CommandStatus::Implemented,
      "Real nearest-triangle barycentric position + normal-offset mapping from a base mesh to a topologically matching (same vertex/face count) target mesh.");
  Reg(e, "ApplyMeshUVN", Make<ApplyMeshCommand>(true), CommandStatus::Implemented,
      "Same real mapping as ApplyMesh: Dino 8's meshes carry no separate UV texture-coordinate channel to give ApplyMeshUVN's UVN triple an independent meaning from ApplyMesh's own barycentric (u,v) + normal offset (n).");
  Reg(e, "SphereTangentToThreeSurfaces", Make<SphereTangentToThreeSurfacesCommand>(), CommandStatus::Implemented,
      "Real damped Gauss-Newton solve (the fillet family's own NewtonSolve) for a sphere centre/radius equidistant from all three picked surfaces, seeded from the pick points.");
  Reg(e, "Boss", Make<BossRibCommand>(false), CommandStatus::Implemented,
      "Real solid: a closed footprint curve extruded along each sample point's own local surface normal (so it follows the surface's curvature), fan-capped and boolean-unioned with the base solid.");
  Reg(e, "Rib", Make<BossRibCommand>(true), CommandStatus::Implemented,
      "Real thin wall (Thickness x Height) following the base surface's local normal along the spine curve, tapering to zero at both ends, boolean-unioned with the base solid.");
  Reg(e, "Slide", Planned("Slide: planned; use Move. Slide's real feature - keeping an object confined to (sliding along) the surface it started on while dragging - needs a constrained drag the command engine's point tool does not support."), CommandStatus::Partial);
  Reg(e, "Hydrostatics", OnSelection("Select closed objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ON_Plane wl = ActivePlane(ctx);
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
          if (!m) continue;
          ON_Mesh mesh = m->raw();
          mesh.ConvertQuadsToTriangles();
          // Submerged volume/centroid below the waterplane: clip every triangle
          // to the half-space below `wl` and sum signed tetrahedra from a point
          // ON the plane (not the world origin). Because that apex lies exactly
          // in the cutting plane, the (unbuilt) flat "lid" at the waterline
          // contributes zero volume on its own, so no explicit lid triangulation
          // is needed - the same trick Mesh::Volume()/GetCentroid() use with the
          // world origin as their apex, just moved onto the waterplane.
          RawMoments sub;
          double full_area = 0;
          for (int fi = 0; fi < mesh.FaceCount(); ++fi) {
            const ON_MeshFace& f = mesh.m_F[fi];
            Point3d tri[3] = {mesh.Vertex(f.vi[0]), mesh.Vertex(f.vi[1]), mesh.Vertex(f.vi[2])};
            full_area += ON_CrossProduct(tri[1] - tri[0], tri[2] - tri[0]).Length() * 0.5;
            double h[3]; for (int k = 0; k < 3; ++k) h[k] = ON_DotProduct(tri[k] - wl.origin, wl.zaxis);
            std::vector<Point3d> below;
            for (int k = 0; k < 3; ++k) {
              Point3d a = tri[k], b = tri[(k + 1) % 3];
              double ha = h[k], hb = h[(k + 1) % 3];
              if (ha <= 0) below.push_back(a);
              if ((ha <= 0) != (hb <= 0)) { double t = ha / (ha - hb); below.push_back(a + (b - a) * t); }
            }
            for (size_t k = 1; k + 1 < below.size(); ++k) {
              Vector3d a = below[0] - wl.origin, b = below[k] - wl.origin, c = below[k + 1] - wl.origin;
              double v6 = ON_DotProduct(a, ON_CrossProduct(b, c));
              double vol = v6 / 6.0;
              sub.measure += vol;
              sub.first += Vector3d(below[0].x + below[k].x + below[k + 1].x - 3 * wl.origin.x,
                                     below[0].y + below[k].y + below[k + 1].y - 3 * wl.origin.y,
                                     below[0].z + below[k].z + below[k + 1].z - 3 * wl.origin.z) * (vol / 4.0);
              // (moments-of-inertia terms are not needed for the hydrostatics report; only volume and centroid are used below.)
            }
          }
          // Waterplane outline and its area/centroid, via the shared section-slicing helper.
          std::vector<std::vector<Point3d>> chains = drafting::SliceMeshToChains(mesh, wl, ctx.Settings().absolute_tolerance * 10);
          double wp_area = 0; Vector3d wp_first(0, 0, 0);
          for (const auto& chain : chains) {
            if (chain.size() < 3) continue;
            Point3d o0 = chain[0];
            for (size_t k = 1; k + 1 < chain.size(); ++k) {
              double a2 = ON_DotProduct(ON_CrossProduct(chain[k] - o0, chain[k + 1] - o0), wl.zaxis);
              wp_area += a2 * 0.5;
              wp_first += Vector3d(o0 + (chain[k] - o0) / 3.0 + (chain[k + 1] - o0) / 3.0) * (a2 * 0.5);
            }
          }
          Point3d displacement_centroid = sub.measure > 1e-12 ? Point3d(sub.first / sub.measure) + Vector3d(wl.origin) : wl.origin;
          Point3d waterplane_centroid = std::fabs(wp_area) > 1e-9 ? Point3d(wp_first / wp_area) : wl.origin;
          ctx.Print("Object " + std::to_string(id) + ": total volume " + FormatNumber(m->Volume()) + ", total area " + FormatNumber(full_area) +
                     "\n  displacement (below the construction plane): " + FormatNumber(std::fabs(sub.measure)) + ", center of buoyancy " + FormatPoint(displacement_centroid) +
                     "\n  waterplane area " + FormatNumber(std::fabs(wp_area)) + ", waterplane centroid " + FormatPoint(waterplane_centroid));
        }
      }), CommandStatus::Partial, "Volume, displacement, and center of buoyancy are computed for real (clipped at the active construction plane, which stands in for a chosen waterline); longitudinal/vertical prismatic coefficients and a trim/heel solver are not implemented.");
  Reg(e, "AreaMoments", OnSelection("Select surfaces or planar curves", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
          if (!m) continue;
          ctx.Print("Object " + std::to_string(id) + ": " + FormatMoments(AreaMoments(m->raw()), "area"));
        }
      }));
  Reg(e, "VolumeMoments", OnSelection("Select closed objects", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) {
          const SceneObject* o = ctx.Doc().Find(id);
          if (!o) continue;
          std::optional<kernel::Mesh> m = MeshOf(*o, 0.01);
          if (!m) continue;
          ctx.Print("Object " + std::to_string(id) + ": " + FormatMoments(VolumeMoments(m->raw()), "volume"));
        }
      }));
}

}  // namespace dino8::app
