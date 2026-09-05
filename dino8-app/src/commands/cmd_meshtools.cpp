// Mesh tools and deformations: Twist/Bend/Taper/Stretch/Shear/Maelstrom/
// Smooth/SoftMove, mesh editing (ExtrudeMesh, OffsetMesh, FillMeshHoles,
// Weld/Unweld, MeshRepair, extraction, collapse, intersection...) and
// mesh primitives (MeshEllipsoid, MeshTruncatedCone, Paraboloid, Slab...).
//
// Everything here works on kernel::Mesh; breps, surfaces and SubDs are
// converted through MeshOf() first (and the command line says so).
#include "commands/cmd_common.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace dino8::app {

namespace {

constexpr double kPi = ON_PI;

double Clamp01(double t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
double Deg(double rad) { return rad * 180.0 / kPi; }
double Rad(double deg) { return deg * kPi / 180.0; }

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool ParseNumber(const std::string& t, double& v) {
  char* e = nullptr;
  v = std::strtod(t.c_str(), &e);
  return e && e != t.c_str() && *e == 0;
}

// ---------------------------------------------------------------------------
// A plain, editable mesh representation. Faces store four indices; a
// triangle repeats its last index (f[2] == f[3]), exactly like ON_MeshFace.
// ---------------------------------------------------------------------------
using Face = std::array<int, 4>;

struct RawMesh {
  std::vector<Point3d> v;
  std::vector<Face> f;
  static bool IsTri(const Face& f) { return f[2] == f[3]; }
  static int Corners(const Face& f) { return IsTri(f) ? 3 : 4; }
  static Face Tri(int a, int b, int c) { return {a, b, c, c}; }
  static Face Quad(int a, int b, int c, int d) { return {a, b, c, d}; }
  static Face Flipped(const Face& f) { return IsTri(f) ? Tri(f[0], f[2], f[1]) : Quad(f[0], f[3], f[2], f[1]); }
  Vector3d FaceNormal(const Face& f) const {
    Vector3d n = ON_CrossProduct(v[f[1]] - v[f[0]], v[f[2]] - v[f[0]]);
    if (!IsTri(f)) n += ON_CrossProduct(v[f[2]] - v[f[0]], v[f[3]] - v[f[0]]);
    n.Unitize();
    return n;
  }
  double FaceArea(const Face& f) const {
    double a = ON_CrossProduct(v[f[1]] - v[f[0]], v[f[2]] - v[f[0]]).Length() / 2;
    if (!IsTri(f)) a += ON_CrossProduct(v[f[2]] - v[f[0]], v[f[3]] - v[f[0]]).Length() / 2;
    return a;
  }
  Point3d FaceCenter(const Face& f) const {
    const int n = Corners(f);
    Vector3d s(0, 0, 0);
    for (int i = 0; i < n; ++i) s += v[f[i]] - Point3d::Origin;
    return Point3d::Origin + s / n;
  }
  void FlipAll() { for (Face& f : this->f) f = Flipped(f); }
};

RawMesh Unpack(const ON_Mesh& m) {
  RawMesh r;
  r.v.reserve(m.VertexCount());
  for (int i = 0; i < m.VertexCount(); ++i) r.v.push_back(m.Vertex(i));
  r.f.reserve(m.FaceCount());
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    r.f.push_back({f.vi[0], f.vi[1], f.vi[2], f.vi[3]});
  }
  return r;
}

// Builds a kernel mesh from the raw form, dropping unused vertices and
// faces with fewer than three distinct corners, and recomputing normals.
kernel::Mesh Pack(const RawMesh& r) {
  kernel::Mesh km;
  ON_Mesh& m = km.raw();
  std::vector<int> remap(r.v.size(), -1);
  int nv = 0;
  std::vector<Face> faces;
  for (const Face& f0 : r.f) {
    Face f = f0;
    bool ok = true;
    for (int k = 0; k < 4; ++k) if (f[k] < 0 || f[k] >= static_cast<int>(r.v.size())) ok = false;
    if (!ok) continue;
    // Collapse a quad with a repeated corner into a triangle.
    if (!RawMesh::IsTri(f)) {
      if (f[0] == f[1]) f = RawMesh::Tri(f[1], f[2], f[3]);
      else if (f[1] == f[2]) f = RawMesh::Tri(f[0], f[1], f[3]);
      else if (f[0] == f[3]) f = RawMesh::Tri(f[0], f[1], f[2]);
    }
    if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2]) continue;
    for (int k = 0; k < 4; ++k) if (remap[f[k]] < 0) { remap[f[k]] = nv; m.SetVertex(nv, r.v[f[k]]); ++nv; }
    faces.push_back({remap[f[0]], remap[f[1]], remap[f[2]], remap[f[3]]});
  }
  int fi = 0;
  for (const Face& f : faces) {
    if (RawMesh::IsTri(f)) m.SetTriangle(fi++, f[0], f[1], f[2]);
    else m.SetQuad(fi++, f[0], f[1], f[2], f[3]);
  }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  return km;
}

// Undirected edge -> every face use of it (kept with the direction the
// face traverses it in, which is what closing side walls needs).
struct EdgeUse { int a, b, face; };
using EdgeKey = std::pair<int, int>;
using EdgeMap = std::map<EdgeKey, std::vector<EdgeUse>>;
EdgeKey Key(int a, int b) { return a < b ? EdgeKey(a, b) : EdgeKey(b, a); }

EdgeMap BuildEdges(const RawMesh& r) {
  EdgeMap em;
  for (size_t fi = 0; fi < r.f.size(); ++fi) {
    const Face& f = r.f[fi];
    const int n = RawMesh::Corners(f);
    for (int k = 0; k < n; ++k) {
      const int a = f[k], b = f[(k + 1) % n];
      if (a == b) continue;
      em[Key(a, b)].push_back({a, b, static_cast<int>(fi)});
    }
  }
  return em;
}

std::vector<EdgeUse> NakedEdges(const EdgeMap& em) {
  std::vector<EdgeUse> out;
  for (const auto& kv : em) if (kv.second.size() == 1) out.push_back(kv.second[0]);
  return out;
}

// Naked edges chained into open chains and closed loops (vertex indices,
// following the faces' own winding).
struct Chain { std::vector<int> v; bool closed = false; };

std::vector<Chain> NakedChains(const RawMesh& r) {
  const EdgeMap em = BuildEdges(r);
  const std::vector<EdgeUse> naked = NakedEdges(em);
  std::multimap<int, size_t> by_start;
  std::set<int> has_incoming;
  for (size_t i = 0; i < naked.size(); ++i) { by_start.insert({naked[i].a, i}); has_incoming.insert(naked[i].b); }
  std::vector<bool> used(naked.size(), false);
  std::vector<Chain> chains;
  auto walk = [&](size_t start) {
    Chain c;
    c.v = {naked[start].a, naked[start].b};
    used[start] = true;
    int cur = naked[start].b;
    while (true) {
      bool advanced = false;
      auto range = by_start.equal_range(cur);
      for (auto it = range.first; it != range.second; ++it) {
        if (used[it->second]) continue;
        used[it->second] = true;
        cur = naked[it->second].b;
        advanced = true;
        break;
      }
      if (!advanced) break;
      if (cur == c.v.front()) { c.closed = true; break; }
      c.v.push_back(cur);
    }
    chains.push_back(std::move(c));
  };
  // Open chains first (from edges nothing leads into), then whatever is left are loops.
  for (size_t i = 0; i < naked.size(); ++i) if (!used[i] && !has_incoming.count(naked[i].a)) walk(i);
  for (size_t i = 0; i < naked.size(); ++i) if (!used[i]) walk(i);
  return chains;
}

std::vector<Vector3d> VertexNormals(const RawMesh& r) {
  std::vector<Vector3d> n(r.v.size(), Vector3d(0, 0, 0));
  for (const Face& f : r.f) {
    Vector3d fn = ON_CrossProduct(r.v[f[1]] - r.v[f[0]], r.v[f[2]] - r.v[f[0]]);
    if (!RawMesh::IsTri(f)) fn += ON_CrossProduct(r.v[f[2]] - r.v[f[0]], r.v[f[3]] - r.v[f[0]]);
    for (int k = 0; k < RawMesh::Corners(f); ++k) n[f[k]] += fn;  // area weighted
  }
  for (Vector3d& v : n) if (!v.Unitize()) v = Vector3d(0, 0, 1);
  return n;
}

std::vector<std::vector<int>> VertexFaces(const RawMesh& r) {
  std::vector<std::vector<int>> vf(r.v.size());
  for (size_t fi = 0; fi < r.f.size(); ++fi) for (int k = 0; k < RawMesh::Corners(r.f[fi]); ++k) vf[r.f[fi][k]].push_back(static_cast<int>(fi));
  return vf;
}

int NearestFace(const RawMesh& r, Point3d p) {
  int best = -1;
  double bd = 0;
  for (size_t i = 0; i < r.f.size(); ++i) {
    const double d = (r.FaceCenter(r.f[i]) - p).Length();
    if (best < 0 || d < bd) { best = static_cast<int>(i); bd = d; }
  }
  return best;
}

int NearestVertex(const RawMesh& r, Point3d p) {
  int best = -1;
  double bd = 0;
  for (size_t i = 0; i < r.v.size(); ++i) {
    const double d = (r.v[i] - p).Length();
    if (best < 0 || d < bd) { best = static_cast<int>(i); bd = d; }
  }
  return best;
}

double PointToSegment(Point3d p, Point3d a, Point3d b) {
  Vector3d ab = b - a;
  const double l2 = ab.LengthSquared();
  double t = l2 > 0 ? Clamp01(ON_DotProduct(p - a, ab) / l2) : 0;
  return (p - (a + ab * t)).Length();
}

// Faces reachable from `seed` across shared edges whose dihedral angle is
// at most `max_angle` (radians; >= pi means "any").
std::vector<int> FloodFaces(const RawMesh& r, int seed, double max_angle) {
  const EdgeMap em = BuildEdges(r);
  std::vector<Vector3d> fn;
  for (const Face& f : r.f) fn.push_back(r.FaceNormal(f));
  std::vector<bool> seen(r.f.size(), false);
  std::vector<int> stack = {seed}, out;
  seen[seed] = true;
  while (!stack.empty()) {
    const int fi = stack.back();
    stack.pop_back();
    out.push_back(fi);
    const Face& f = r.f[fi];
    const int n = RawMesh::Corners(f);
    for (int k = 0; k < n; ++k) {
      const auto it = em.find(Key(f[k], f[(k + 1) % n]));
      if (it == em.end()) continue;
      for (const EdgeUse& u : it->second) {
        if (seen[u.face]) continue;
        const double ang = std::acos(std::max(-1.0, std::min(1.0, ON_DotProduct(fn[fi], fn[u.face]))));
        if (ang <= max_angle) { seen[u.face] = true; stack.push_back(u.face); }
      }
    }
  }
  return out;
}

// Splits `r` into the faces listed and the rest.
std::pair<RawMesh, RawMesh> SplitFaces(const RawMesh& r, const std::vector<int>& faces) {
  std::vector<bool> take(r.f.size(), false);
  for (int fi : faces) if (fi >= 0 && fi < static_cast<int>(r.f.size())) take[fi] = true;
  RawMesh a, b;
  a.v = r.v;
  b.v = r.v;
  for (size_t i = 0; i < r.f.size(); ++i) (take[i] ? a : b).f.push_back(r.f[i]);
  return {a, b};
}

// Makes face windings consistent (walking across shared edges) and, for a
// closed solid, outward. Returns the number of faces flipped.
int UnifyNormals(RawMesh& r) {
  const EdgeMap em = BuildEdges(r);
  std::vector<bool> seen(r.f.size(), false);
  int flipped = 0;
  for (size_t start = 0; start < r.f.size(); ++start) {
    if (seen[start]) continue;
    seen[start] = true;
    std::vector<int> stack = {static_cast<int>(start)};
    while (!stack.empty()) {
      const int fi = stack.back();
      stack.pop_back();
      const Face f = r.f[fi];
      const int n = RawMesh::Corners(f);
      for (int k = 0; k < n; ++k) {
        const int a = f[k], b = f[(k + 1) % n];
        const auto it = em.find(Key(a, b));
        if (it == em.end() || it->second.size() != 2) continue;
        for (const EdgeUse& u : it->second) {
          if (u.face == fi || seen[u.face]) continue;
          // The neighbour must traverse the shared edge in the opposite direction.
          const Face& g = r.f[u.face];
          const int gn = RawMesh::Corners(g);
          bool same_dir = false;
          for (int j = 0; j < gn; ++j) if (g[j] == a && g[(j + 1) % gn] == b) same_dir = true;
          if (same_dir) { r.f[u.face] = RawMesh::Flipped(g); ++flipped; }
          seen[u.face] = true;
          stack.push_back(u.face);
        }
      }
    }
  }
  kernel::Mesh km = Pack(r);
  if (km.IsClosedManifold() && km.Volume() < 0) { r.FlipAll(); flipped += static_cast<int>(r.f.size()); }
  return flipped;
}

// Merges each group of vertices into one at the group's centroid.
RawMesh CollapseGroups(const RawMesh& r, const std::vector<std::vector<int>>& groups) {
  RawMesh out = r;
  std::vector<int> remap(r.v.size());
  std::iota(remap.begin(), remap.end(), 0);
  for (const std::vector<int>& g : groups) {
    if (g.empty()) continue;
    Vector3d s(0, 0, 0);
    for (int i : g) s += r.v[i] - Point3d::Origin;
    out.v[g[0]] = Point3d::Origin + s / static_cast<double>(g.size());
    for (int i : g) remap[i] = g[0];
  }
  for (Face& f : out.f) for (int& k : f) k = remap[k];
  return out;  // Pack() drops the faces that became degenerate
}

// ---------------------------------------------------------------------------
// Objects <-> meshes
// ---------------------------------------------------------------------------

struct Target {
  ObjectId id;
  kernel::Mesh mesh;
  bool converted;  // the object was not a mesh (brep/surface/SubD)
};

std::vector<Target> Targets(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label) {
  std::vector<Target> out;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) { ctx.Warn(label + ": object " + std::to_string(id) + " is not a mesh, surface, polysurface or SubD; skipped"); continue; }
    const bool converted = o->kind != ObjectKind::Mesh;
    if (converted) ctx.Print(label + ": object " + std::to_string(id) + " converted to a mesh");
    out.push_back({id, *m, converted});
  }
  if (out.empty()) ctx.Warn(label + ": nothing to work on");
  return out;
}

// Adds `n` with the layer/name/colour of `like`.
ObjectId AddLike(CommandContext& ctx, ObjectId like, SceneObject n) {
  if (const SceneObject* o = ctx.Doc().Find(like)) {
    n.layer_index = o->layer_index;
    n.name = o->name;
    n.color = o->color;
    n.color_by_layer = o->color_by_layer;
  }
  return ctx.Doc().Add(std::move(n));
}

// Stores `m` as the target's geometry: in place for mesh objects, replacing
// the object with a new mesh object otherwise. Returns the holder's id.
ObjectId Commit(CommandContext& ctx, ObjectId id, const kernel::Mesh& m) {
  SceneObject* o = ctx.Doc().Find(id);
  if (!o) return kNoObject;
  if (o->kind == ObjectKind::Mesh) { *o->mesh = m; o->InvalidateDisplay(); return id; }
  SceneObject n = SceneObject::MakeMesh(m);
  n.layer_index = o->layer_index;
  n.name = o->name;
  n.color = o->color;
  n.color_by_layer = o->color_by_layer;
  ctx.Doc().Remove(id);
  return ctx.Doc().Add(std::move(n));
}

// Applies a per-point map to any object: points move, curves move their
// control points, meshes their vertices; breps/surfaces/SubDs are
// converted to meshes first. Returns the number of objects deformed.
using PointMap = std::function<Point3d(Point3d)>;

void MapMesh(ON_Mesh& m, const PointMap& fn) {
  for (int i = 0; i < m.VertexCount(); ++i) m.SetVertex(i, fn(m.Vertex(i)));
  m.DestroyRuntimeCache(true);
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
}

int DeformObjects(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label, const PointMap& fn, bool copy) {
  ctx.Doc().BeginChange(label);
  int done = 0, converted = 0;
  for (ObjectId id : ids) {
    SceneObject* src = ctx.Doc().Find(id);
    if (!src) continue;
    SceneObject dup;
    if (copy) { dup = *src; dup.id = kNoObject; dup.selected = false; }
    SceneObject& o = copy ? dup : *src;
    bool ok = false;
    switch (o.kind) {
      case ObjectKind::Point: o.point = fn(o.point); ok = true; break;
      case ObjectKind::Curve:
        for (int i = 0; i < o.curve->ControlPointCount(); ++i) o.curve->SetControlPointAt(i, fn(o.curve->ControlPointAt(i)));
        ok = true;
        break;
      case ObjectKind::Mesh: MapMesh(o.mesh->raw(), fn); ok = true; break;
      case ObjectKind::Brep:
      case ObjectKind::Surface:
      case ObjectKind::SubD: {
        std::optional<kernel::Mesh> m = MeshOf(o, ctx.App().surface_display_tolerance);
        if (!m || m->FaceCount() == 0) break;
        MapMesh(m->raw(), fn);
        SceneObject n = SceneObject::MakeMesh(*m);
        n.layer_index = o.layer_index; n.name = o.name; n.color = o.color; n.color_by_layer = o.color_by_layer;
        if (!copy) ctx.Doc().Remove(id);
        ctx.Doc().Add(std::move(n));
        ++converted;
        ++done;
        continue;  // src is invalid now
      }
      default: break;
    }
    if (!ok) { ctx.Warn(label + ": object " + std::to_string(id) + " cannot be deformed; skipped"); continue; }
    o.InvalidateDisplay();
    if (copy) ctx.Doc().Add(std::move(dup));
    ++done;
  }
  std::string msg = label + ": " + std::string(copy ? "copied and deformed " : "deformed ") + std::to_string(done) + " object(s)";
  if (converted) msg += " (" + std::to_string(converted) + " converted to meshes)";
  ctx.Print(msg);
  return done;
}

// ---------------------------------------------------------------------------
// A generic "select, then answer a fixed list of point/number prompts"
// command. Number prompts are asked as points so a pick (distance from the
// previous point), a typed number or Enter (the default) all work, in the
// UI and in scripts alike.
// ---------------------------------------------------------------------------
struct Step {
  std::string prompt;
  bool number = false;
  double def = 0;
};

struct ToolInput {
  std::vector<std::optional<Point3d>> pts;
  std::vector<std::optional<double>> nums;
  std::map<std::string, std::string> opts;  // lowercase name -> value
  bool HasPoint(size_t i) const { return i < pts.size() && pts[i].has_value(); }
  Point3d P(size_t i) const { return HasPoint(i) ? *pts[i] : Point3d::Origin; }
  double N(size_t i, double fallback = 0) const { return i < nums.size() && nums[i] ? *nums[i] : fallback; }
  std::string Opt(const std::string& name, const std::string& def = "") const { auto it = opts.find(Lower(name)); return it == opts.end() ? def : it->second; }
  bool Yes(const std::string& name) const { return Lower(Opt(name)) == "yes"; }
  double OptNum(const std::string& name, double def) const { double v; return ParseNumber(Opt(name), v) ? v : def; }
};

using ToolAction = std::function<void(CommandContext&, const std::vector<ObjectId>&, const ToolInput&)>;

class ToolCommand : public Command {
 public:
  ToolCommand(std::string select_prompt, std::vector<Step> steps, std::vector<OptionSpec> opts, ToolAction action, int min_objects = 1)
      : select_(std::move(select_prompt)), steps_(std::move(steps)), opts_(std::move(opts)), action_(std::move(action)), min_(min_objects) {
    in_.pts.resize(steps_.size());
    in_.nums.resize(steps_.size());
    for (const OptionSpec& o : opts_) in_.opts[Lower(o.name)] = o.value;
  }
  void Begin(CommandContext& ctx) override {
    options = opts_;
    if (select_.empty()) Next(ctx);
    else WantObjects(select_, min_);
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    ids_ = ids;
    if (ids_.empty() && min_ > 0) { ctx.Warn("Nothing selected"); Finish(); return; }
    Next(ctx);
  }
  void OnEnter(CommandContext& ctx) override {
    if (want == Want::Objects) { if (min_ == 0) Next(ctx); else { ctx.Warn("Nothing selected"); Finish(); } return; }
    if (step_ < steps_.size() && steps_[step_].number) { OnNumber(ctx, steps_[step_].def); return; }
    ctx.ClearPreview();
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    if (step_ >= steps_.size()) return;
    ctx.SetLastPoint(p);
    in_.pts[step_] = p;
    if (steps_[step_].number) {
      // Distance from the previous picked point (or from the origin of the CPlane).
      Point3d ref = ActivePlane(ctx).origin;
      for (size_t i = step_; i-- > 0;) if (in_.pts[i]) { ref = *in_.pts[i]; break; }
      in_.nums[step_] = (p - ref).Length();
    }
    ++step_;
    Next(ctx);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (step_ >= steps_.size()) return;
    in_.nums[step_] = v;
    ++step_;
    Next(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    double v;
    if (ParseNumber(t, v)) { OnNumber(ctx, v); return; }
    ctx.Warn("Unknown option: " + t);
  }
  void OnOption(CommandContext&, const std::string& name, const std::string& value) override {
    for (OptionSpec& o : options) {
      if (o.name != name) continue;
      if (o.toggle) o.value = value.empty() ? (Lower(o.value) == "yes" ? "No" : "Yes") : (Lower(value) == "yes" || value == "1" ? "Yes" : "No");
      else if (o.numeric) { double v; if (ParseNumber(value, v)) o.value = FormatNumber(v); }
      else if (!value.empty()) o.value = value;
      in_.opts[Lower(o.name)] = o.value;
    }
    opts_ = options;
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (step_ >= steps_.size()) return;
    ctx.ClearPreview();
    for (size_t i = step_; i-- > 0;) if (in_.pts[i]) { ctx.AddPreviewLine(*in_.pts[i], h); break; }
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  void Next(CommandContext& ctx) {
    if (step_ >= steps_.size()) {
      ctx.ClearPreview();
      action_(ctx, ids_, in_);
      Finish();
      return;
    }
    const Step& s = steps_[step_];
    WantPoint(s.number ? s.prompt + " <" + FormatNumber(s.def) + ">" : s.prompt);
    options = opts_;
  }
  std::string select_;
  std::vector<Step> steps_;
  std::vector<OptionSpec> opts_;
  ToolAction action_;
  int min_;
  std::vector<ObjectId> ids_;
  ToolInput in_;
  size_t step_ = 0;
};

Step PointStep(const char* prompt) { return {prompt, false, 0}; }
Step NumberStep(const char* prompt, double def) { return {prompt, true, def}; }
OptionSpec Toggle(const char* name, bool on) { return {name, on ? "Yes" : "No", {"Yes", "No"}, false, true}; }
OptionSpec Numeric(const char* name, double v) { return {name, FormatNumber(v), {}, true, false}; }

CommandFactory Tool(std::string select_prompt, std::vector<Step> steps, std::vector<OptionSpec> opts, ToolAction action, int min_objects = 1) {
  return [=]() -> std::unique_ptr<Command> { return std::make_unique<ToolCommand>(select_prompt, steps, opts, action, min_objects); };
}

// A deformation: a ToolCommand whose action builds a point map.
using MapBuilder = std::function<std::optional<PointMap>(CommandContext&, const ToolInput&)>;
CommandFactory Deform(std::string label, std::string select_prompt, std::vector<Step> steps, std::vector<OptionSpec> extra, MapBuilder make) {
  std::vector<OptionSpec> opts = {Toggle("Copy", false)};
  opts.insert(opts.end(), extra.begin(), extra.end());
  return Tool(std::move(select_prompt), std::move(steps), opts, [=](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
    std::optional<PointMap> fn = make(ctx, in);
    if (fn) DeformObjects(ctx, ids, label, *fn, in.Yes("Copy"));
  });
}

CommandFactory Stub(const char* name) {
  return Immediate([name](CommandContext& ctx) { ctx.Print(std::string(name) + " is not yet available in this build."); });
}

// ---------------------------------------------------------------------------
// Deformations
// ---------------------------------------------------------------------------

bool Axis(CommandContext& ctx, const ToolInput& in, Point3d& a, Vector3d& u, double& len, const char* label) {
  a = in.P(0);
  u = in.P(1) - a;
  len = u.Length();
  if (len <= ON_ZERO_TOLERANCE || !in.HasPoint(1)) { ctx.Warn(std::string(label) + ": axis has zero length"); return false; }
  u.Unitize();
  return true;
}

Point3d RotateAbout(Point3d p, Point3d a, Vector3d u, double ang) {
  if (std::fabs(ang) < 1e-15) return p;
  ON_Xform rot;
  rot.Rotation(ang, u, a);
  return rot * p;
}

std::optional<PointMap> TwistMap(CommandContext& ctx, const ToolInput& in) {
  Point3d a; Vector3d u; double len;
  if (!Axis(ctx, in, a, u, len, "Twist")) return std::nullopt;
  const double angle = Rad(in.N(2, 90));
  const bool infinite = in.Yes("Infinite");
  ctx.Print("Twist: " + FormatNumber(Deg(angle)) + " degrees over " + FormatNumber(len) + " units");
  return [=](Point3d p) {
    const double s = ON_DotProduct(p - a, u) / len;
    return RotateAbout(p, a, u, angle * (infinite ? s : Clamp01(s)));
  };
}

std::optional<PointMap> BendMap(CommandContext& ctx, const ToolInput& in) {
  Point3d a; Vector3d u; double len;
  if (!Axis(ctx, in, a, u, len, "Bend")) return std::nullopt;
  const Vector3d d = in.P(2) - a;
  Vector3d n = d - u * ON_DotProduct(d, u);
  const double dn = n.Length();
  if (dn <= ON_ZERO_TOLERANCE || ON_DotProduct(d, u) <= ON_ZERO_TOLERANCE) { ctx.Warn("Bend: the bend point lies on the spine; nothing to do"); return std::nullopt; }
  n.Unitize();
  // Circular arc from `a`, tangent to `u`, through the bend point.
  const double radius = d.LengthSquared() / (2 * dn);
  const double theta = 2 * std::atan2(dn, ON_DotProduct(d, u));
  ctx.Print("Bend: radius " + FormatNumber(radius) + ", angle " + FormatNumber(Deg(theta)) + " degrees");
  return [=](Point3d p) {
    const Vector3d rel = p - a;
    const double s = ON_DotProduct(rel, u), h = ON_DotProduct(rel, n);
    if (s <= 0) return p;
    const Vector3d other = rel - u * s - n * h;
    const double sc = std::min(s, len);
    const double phi = theta * sc / len;
    const Point3d center = a + n * radius;
    Point3d bent = center + (u * std::sin(phi) - n * std::cos(phi)) * (radius - h) + other;
    if (s > len) bent += (u * std::cos(theta) + n * std::sin(theta)) * (s - len);
    return bent;
  };
}

std::optional<PointMap> TaperMap(CommandContext& ctx, const ToolInput& in) {
  Point3d a; Vector3d u; double len;
  if (!Axis(ctx, in, a, u, len, "Taper")) return std::nullopt;
  const double d0 = in.N(2, 10), d1 = in.N(3, 5);
  if (d0 <= 0) { ctx.Warn("Taper: start distance must be positive"); return std::nullopt; }
  const bool infinite = in.Yes("Infinite");
  ctx.Print("Taper: " + FormatNumber(d0) + " -> " + FormatNumber(d1));
  return [=](Point3d p) {
    const Vector3d rel = p - a;
    const double s = ON_DotProduct(rel, u);
    const double t = infinite ? s / len : Clamp01(s / len);
    const double f = ((1 - t) * d0 + t * d1) / d0;
    return a + u * s + (rel - u * s) * f;
  };
}

std::optional<PointMap> StretchMap(CommandContext& ctx, const ToolInput& in) {
  Point3d a; Vector3d u; double len;
  if (!Axis(ctx, in, a, u, len, "Stretch")) return std::nullopt;
  double new_len = in.HasPoint(2) ? ON_DotProduct(in.P(2) - a, u) : in.N(2, len);
  if (new_len <= ON_ZERO_TOLERANCE) { ctx.Warn("Stretch: new length must be positive"); return std::nullopt; }
  ctx.Print("Stretch: " + FormatNumber(len) + " -> " + FormatNumber(new_len));
  return [=](Point3d p) {
    const double s = ON_DotProduct(p - a, u);
    if (s <= 0) return p;
    if (s < len) return p + u * (s * new_len / len - s);
    return p + u * (new_len - len);
  };
}

std::optional<PointMap> ShearMap(CommandContext& ctx, const ToolInput& in) {
  const Point3d base = in.P(0), ref = in.P(1);
  Vector3d n = ref - base;
  const double height = n.Length();
  if (height <= ON_ZERO_TOLERANCE || !in.HasPoint(1)) { ctx.Warn("Shear: reference point coincides with the base point"); return std::nullopt; }
  n.Unitize();
  Vector3d shear;
  if (in.HasPoint(2)) {
    shear = in.P(2) - ref;
    shear -= n * ON_DotProduct(shear, n);
  } else {
    // A typed angle shears along the CPlane X axis (or Y if X is the reference direction).
    ON_Plane pl = ActivePlane(ctx);
    Vector3d dir = pl.xaxis - n * ON_DotProduct(pl.xaxis, n);
    if (dir.Length() < 1e-6) dir = pl.yaxis - n * ON_DotProduct(pl.yaxis, n);
    dir.Unitize();
    shear = dir * (std::tan(Rad(in.N(2, 30))) * height);
  }
  ctx.Print("Shear: angle " + FormatNumber(Deg(std::atan2(shear.Length(), height))) + " degrees");
  return [=](Point3d p) { return p + shear * (ON_DotProduct(p - base, n) / height); };
}

std::optional<PointMap> MaelstromMap(CommandContext& ctx, const ToolInput& in) {
  const Point3d c = in.P(0);
  const Vector3d axis = ActiveNormal(ctx);
  const double r0 = in.N(1, 0), r1 = in.N(2, 10), angle = Rad(in.N(3, 90));
  if (std::fabs(r1 - r0) <= ON_ZERO_TOLERANCE) { ctx.Warn("Maelstrom: radii must differ"); return std::nullopt; }
  ctx.Print("Maelstrom: " + FormatNumber(Deg(angle)) + " degrees between radius " + FormatNumber(r0) + " and " + FormatNumber(r1));
  return [=](Point3d p) {
    const Vector3d rel = p - c;
    const double r = (rel - axis * ON_DotProduct(rel, axis)).Length();
    return RotateAbout(p, c, axis, angle * Clamp01((r - r0) / (r1 - r0)));
  };
}

std::optional<PointMap> SoftMoveMap(CommandContext& ctx, const ToolInput& in) {
  const Point3d from = in.P(0), to = in.P(1);
  const double radius = in.OptNum("Radius", 10);
  if (radius <= 0) { ctx.Warn("SoftMove: radius must be positive"); return std::nullopt; }
  const Vector3d delta = to - from;
  ctx.Print("SoftMove: " + FormatNumber(delta.Length()) + " units with falloff radius " + FormatNumber(radius));
  return [=](Point3d p) {
    const double d = (p - from).Length();
    if (d >= radius) return p;
    const double w = 0.5 * (1 + std::cos(kPi * d / radius));
    return p + delta * w;
  };
}

// Laplacian smoothing of mesh vertices / curve control points.
void SmoothObjects(CommandContext& ctx, const std::vector<ObjectId>& ids, double factor, int iterations, bool fix_boundaries) {
  ctx.Doc().BeginChange("Smooth");
  int done = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Curve) {
      const int n = o->curve->ControlPointCount();
      if (n < 3) continue;
      const bool closed = o->curve->IsClosed();
      for (int it = 0; it < iterations; ++it) {
        std::vector<Point3d> cp;
        for (int i = 0; i < n; ++i) cp.push_back(o->curve->ControlPointAt(i));
        for (int i = 0; i < n; ++i) {
          if (!closed && (i == 0 || i == n - 1)) continue;
          const Point3d prev = cp[(i + n - 1) % n], next = cp[(i + 1) % n];
          const Point3d avg = Point3d::Origin + ((prev - Point3d::Origin) + (next - Point3d::Origin)) / 2;
          o->curve->SetControlPointAt(i, cp[i] + (avg - cp[i]) * factor);
        }
      }
      o->InvalidateDisplay();
      ++done;
      continue;
    }
    std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) { ctx.Warn("Smooth: object " + std::to_string(id) + " skipped"); continue; }
    if (o->kind != ObjectKind::Mesh) ctx.Print("Smooth: object " + std::to_string(id) + " converted to a mesh");
    RawMesh r = Unpack(m->raw());
    std::vector<std::set<int>> nb(r.v.size());
    for (const Face& f : r.f) {
      const int n = RawMesh::Corners(f);
      for (int k = 0; k < n; ++k) { nb[f[k]].insert(f[(k + 1) % n]); nb[f[(k + 1) % n]].insert(f[k]); }
    }
    std::vector<bool> fixed(r.v.size(), false);
    if (fix_boundaries) for (const EdgeUse& e : NakedEdges(BuildEdges(r))) { fixed[e.a] = true; fixed[e.b] = true; }
    for (int it = 0; it < iterations; ++it) {
      std::vector<Point3d> nv = r.v;
      for (size_t i = 0; i < r.v.size(); ++i) {
        if (fixed[i] || nb[i].empty()) continue;
        Vector3d s(0, 0, 0);
        for (int j : nb[i]) s += r.v[j] - Point3d::Origin;
        const Point3d avg = Point3d::Origin + s / static_cast<double>(nb[i].size());
        nv[i] = r.v[i] + (avg - r.v[i]) * factor;
      }
      r.v = nv;
    }
    Commit(ctx, id, Pack(r));
    ++done;
  }
  ctx.Print("Smooth: smoothed " + std::to_string(done) + " object(s), factor " + FormatNumber(factor) + ", " + std::to_string(iterations) + " iteration(s)");
}

// ---------------------------------------------------------------------------
// Mesh editing
// ---------------------------------------------------------------------------

// Offsets every vertex along `dir[i]` by `d`; with `solid`, the original
// (flipped) and offset shells are joined by walls along naked edges.
RawMesh Thicken(const RawMesh& r, const std::vector<Vector3d>& dir, double d, bool solid) {
  RawMesh out;
  const int n = static_cast<int>(r.v.size());
  out.v = r.v;
  for (int i = 0; i < n; ++i) out.v.push_back(r.v[i] + dir[i] * d);
  if (solid) for (const Face& f : r.f) out.f.push_back(RawMesh::Flipped(f));
  for (const Face& f : r.f) {
    Face g = f;
    for (int& k : g) k += n;
    out.f.push_back(g);
  }
  if (solid) for (const EdgeUse& e : NakedEdges(BuildEdges(r))) out.f.push_back(RawMesh::Quad(e.a, e.b, e.b + n, e.a + n));
  if (solid && d < 0) out.FlipAll();
  if (!solid) {  // keep only the offset shell
    RawMesh shell;
    for (int i = 0; i < n; ++i) shell.v.push_back(out.v[n + i]);
    for (const Face& f : r.f) shell.f.push_back(f);
    return shell;
  }
  return out;
}

void ExtrudeMesh(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  const double d = in.N(0, 1);
  if (d == 0) { ctx.Warn("ExtrudeMesh: distance must be non-zero"); return; }
  std::vector<Target> ts = Targets(ctx, ids, "ExtrudeMesh");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("ExtrudeMesh");
  const bool keep = !in.Yes("DeleteInput");
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    const std::vector<Vector3d> nrm = VertexNormals(r);
    const bool closed = NakedEdges(BuildEdges(r)).empty();
    RawMesh out = closed ? Thicken(r, nrm, d, false) : Thicken(r, nrm, d, true);
    kernel::Mesh km = Pack(out);
    if (keep) AddLike(ctx, t.id, SceneObject::MakeMesh(km)); else Commit(ctx, t.id, km);
    ctx.Print(std::string("ExtrudeMesh: ") + (closed ? "offset closed mesh" : "extruded open mesh into a closed solid") + " (" + std::to_string(km.FaceCount()) + " faces)");
  }
}

void OffsetMesh(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  const double d = in.N(0, 1);
  if (d == 0) { ctx.Warn("OffsetMesh: distance must be non-zero"); return; }
  std::vector<Target> ts = Targets(ctx, ids, "OffsetMesh");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("OffsetMesh");
  const bool solid = in.Yes("Solid"), keep = !in.Yes("DeleteInput");
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    RawMesh out = Thicken(r, VertexNormals(r), d, solid);
    kernel::Mesh km = Pack(out);
    if (keep) AddLike(ctx, t.id, SceneObject::MakeMesh(km)); else Commit(ctx, t.id, km);
    ctx.Print("OffsetMesh: " + std::string(solid ? "solid shell" : "offset shell") + " with " + std::to_string(km.FaceCount()) + " faces");
  }
}

// Fan-triangulates the naked loops of `r` (all of them, or the one whose
// centre is nearest `near_pt`). Returns the number of holes filled.
int FillHoles(RawMesh& r, const std::optional<Point3d>& near_pt) {
  std::vector<Chain> loops;
  for (Chain& c : NakedChains(r)) if (c.closed && c.v.size() >= 3) loops.push_back(std::move(c));
  if (loops.empty()) return 0;
  if (near_pt) {
    size_t best = 0;
    double bd = 0;
    for (size_t i = 0; i < loops.size(); ++i) {
      Vector3d s(0, 0, 0);
      for (int vi : loops[i].v) s += r.v[vi] - Point3d::Origin;
      const double d = (Point3d::Origin + s / static_cast<double>(loops[i].v.size()) - *near_pt).Length();
      if (i == 0 || d < bd) { best = i; bd = d; }
    }
    loops = {loops[best]};
  }
  for (const Chain& c : loops) {
    const int n = static_cast<int>(c.v.size());
    if (n == 3) { r.f.push_back(RawMesh::Tri(c.v[0], c.v[2], c.v[1])); continue; }
    if (n == 4) { r.f.push_back(RawMesh::Quad(c.v[0], c.v[3], c.v[2], c.v[1])); continue; }
    Vector3d s(0, 0, 0);
    for (int vi : c.v) s += r.v[vi] - Point3d::Origin;
    const int ci = static_cast<int>(r.v.size());
    r.v.push_back(Point3d::Origin + s / n);
    for (int i = 0; i < n; ++i) r.f.push_back(RawMesh::Tri(c.v[(i + 1) % n], c.v[i], ci));
  }
  return static_cast<int>(loops.size());
}

void FillMeshHoles(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::optional<Point3d>& near_pt) {
  std::vector<Target> ts = Targets(ctx, ids, "FillMeshHoles");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("FillMeshHoles");
  int total = 0;
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    const int n = FillHoles(r, near_pt);
    if (n) Commit(ctx, t.id, Pack(r));
    total += n;
  }
  ctx.Print("FillMeshHoles: filled " + std::to_string(total) + " hole(s)");
}

// Welds coincident vertices whose normals differ by at most `angle`.
RawMesh WeldRaw(const RawMesh& r, double tol, double angle, int* merged) {
  const std::vector<Vector3d> vn = VertexNormals(r);
  const double cos_tol = std::cos(std::min(angle, kPi));
  auto snap = [tol](double v) { return static_cast<long long>(std::llround(v / tol)); };
  std::map<std::tuple<long long, long long, long long>, std::vector<int>> cells;
  std::vector<int> remap(r.v.size());
  std::iota(remap.begin(), remap.end(), 0);
  int count = 0;
  for (size_t i = 0; i < r.v.size(); ++i) {
    std::vector<int>& reps = cells[{snap(r.v[i].x), snap(r.v[i].y), snap(r.v[i].z)}];
    bool joined = false;
    for (int rep : reps) {
      if (angle >= kPi - 1e-9 || ON_DotProduct(vn[i], vn[rep]) >= cos_tol) { remap[i] = rep; joined = true; ++count; break; }
    }
    if (!joined) reps.push_back(static_cast<int>(i));
  }
  RawMesh out = r;
  for (Face& f : out.f) for (int& k : f) k = remap[k];
  if (merged) *merged = count;
  return out;
}

// Gives each fan of faces around a vertex that is separated by an edge
// sharper than `angle` its own vertex copy. `only` restricts to one vertex.
RawMesh UnweldRaw(const RawMesh& r, double angle, int only, int* added) {
  std::vector<Vector3d> fn;
  for (const Face& f : r.f) fn.push_back(r.FaceNormal(f));
  const std::vector<std::vector<int>> vf = VertexFaces(r);
  RawMesh out = r;
  int count = 0;
  const double cos_tol = std::cos(std::max(0.0, std::min(angle, kPi)));
  for (size_t v = 0; v < r.v.size(); ++v) {
    if (only >= 0 && static_cast<int>(v) != only) continue;
    const std::vector<int>& faces = vf[v];
    if (faces.size() < 2) continue;
    std::vector<int> parent(faces.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
    // Faces sharing an edge (v, x): candidates for staying welded.
    std::map<int, std::vector<int>> by_other;
    for (size_t i = 0; i < faces.size(); ++i) {
      const Face& f = r.f[faces[i]];
      const int n = RawMesh::Corners(f);
      for (int k = 0; k < n; ++k) if (f[k] == static_cast<int>(v)) { by_other[f[(k + 1) % n]].push_back(static_cast<int>(i)); by_other[f[(k + n - 1) % n]].push_back(static_cast<int>(i)); }
    }
    if (angle >= 0) {
      for (const auto& kv : by_other)
        for (size_t i = 0; i < kv.second.size(); ++i)
          for (size_t j = i + 1; j < kv.second.size(); ++j)
            if (ON_DotProduct(fn[faces[kv.second[i]]], fn[faces[kv.second[j]]]) >= cos_tol) parent[find(kv.second[i])] = find(kv.second[j]);
    }
    std::map<int, int> root_vertex;
    for (size_t i = 0; i < faces.size(); ++i) {
      const int root = find(static_cast<int>(i));
      int nv;
      if (root_vertex.empty()) { nv = static_cast<int>(v); root_vertex[root] = nv; }
      else if (root_vertex.count(root)) nv = root_vertex[root];
      else { nv = static_cast<int>(out.v.size()); out.v.push_back(r.v[v]); root_vertex[root] = nv; ++count; }
      Face& f = out.f[faces[i]];
      for (int k = 0; k < 4; ++k) if (r.f[faces[i]][k] == static_cast<int>(v)) f[k] = nv;
    }
  }
  if (added) *added = count;
  return out;
}

void WeldCommandAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  std::vector<Target> ts = Targets(ctx, ids, "Weld");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("Weld");
  const double angle = Rad(std::fabs(in.N(0, 180)));
  for (const Target& t : ts) {
    int merged = 0;
    RawMesh r = WeldRaw(Unpack(t.mesh.raw()), ctx.Settings().absolute_tolerance, angle, &merged);
    kernel::Mesh km = Pack(r);
    Commit(ctx, t.id, km);
    ctx.Print("Weld: merged " + std::to_string(merged) + " vertex(es); " + std::to_string(km.VertexCount()) + " vertices remain");
  }
}

void UnweldCommandAction(CommandContext& ctx, const std::vector<ObjectId>& ids, double angle, const std::optional<Point3d>& vertex_near, const char* label) {
  std::vector<Target> ts = Targets(ctx, ids, label);
  if (ts.empty()) return;
  ctx.Doc().BeginChange(label);
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    int only = vertex_near ? NearestVertex(r, *vertex_near) : -1;
    int added = 0;
    RawMesh out = UnweldRaw(r, angle, only, &added);
    Commit(ctx, t.id, Pack(out));
    ctx.Print(std::string(label) + ": added " + std::to_string(added) + " vertex copy(ies)");
  }
}

void MeshRepair(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Target> ts = Targets(ctx, ids, "MeshRepair");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("MeshRepair");
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    const int v0 = static_cast<int>(r.v.size()), f0 = static_cast<int>(r.f.size());
    int merged = 0;
    r = WeldRaw(r, ctx.Settings().absolute_tolerance, kPi, &merged);
    // Drop degenerate faces (repeated corners after welding, zero area).
    RawMesh clean;
    clean.v = r.v;
    int degenerate = 0;
    for (const Face& f : r.f) {
      std::set<int> uniq(f.begin(), f.end());
      if (uniq.size() < 3 || r.FaceArea(f) <= ON_ZERO_TOLERANCE) { ++degenerate; continue; }
      clean.f.push_back(f);
    }
    const int flipped = UnifyNormals(clean);
    const EdgeMap em = BuildEdges(clean);
    int naked = 0, nonmanifold = 0;
    for (const auto& kv : em) { if (kv.second.size() == 1) ++naked; else if (kv.second.size() > 2) ++nonmanifold; }
    kernel::Mesh km = Pack(clean);
    Commit(ctx, t.id, km);
    ctx.Print("MeshRepair: object " + std::to_string(t.id) + ": " + std::to_string(v0) + " -> " + std::to_string(km.VertexCount()) + " vertices (" + std::to_string(merged) + " welded), " +
              std::to_string(f0) + " -> " + std::to_string(km.FaceCount()) + " faces (" + std::to_string(degenerate) + " degenerate removed), " + std::to_string(flipped) + " normal(s) unified, " +
              std::to_string(naked) + " naked edge(s), " + std::to_string(nonmanifold) + " non-manifold edge(s), " + (km.IsClosedManifold() ? "closed" : "open"));
  }
}

// Convex hull (Andrew's monotone chain) of 2D points; returns CCW indices.
std::vector<int> ConvexHull2D(const std::vector<ON_2dPoint>& pts) {
  std::vector<int> idx(pts.size());
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return pts[a].x < pts[b].x || (pts[a].x == pts[b].x && pts[a].y < pts[b].y); });
  auto cross = [&](int o, int a, int b) { return (pts[a].x - pts[o].x) * (pts[b].y - pts[o].y) - (pts[a].y - pts[o].y) * (pts[b].x - pts[o].x); };
  std::vector<int> h(2 * idx.size());
  size_t k = 0;
  for (int i : idx) { while (k >= 2 && cross(h[k - 2], h[k - 1], i) <= 0) --k; h[k++] = i; }
  for (size_t i = idx.size() - 1, t = k + 1; i-- > 0;) { while (k >= t && cross(h[k - 2], h[k - 1], idx[i]) <= 0) --k; h[k++] = idx[i]; }
  h.resize(k > 0 ? k - 1 : 0);
  return h;
}

void MeshOutline(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Target> ts = Targets(ctx, ids, "MeshOutline");
  if (ts.empty()) return;
  const ON_Plane pl = ActivePlane(ctx);
  ctx.Doc().BeginChange("MeshOutline");
  int made = 0;
  for (const Target& t : ts) {
    std::vector<ON_2dPoint> uv;
    const ON_Mesh& m = t.mesh.raw();
    for (int i = 0; i < m.VertexCount(); ++i) { double u, v; pl.ClosestPointTo(m.Vertex(i), &u, &v); uv.emplace_back(u, v); }
    std::vector<int> hull = ConvexHull2D(uv);
    if (hull.size() < 3) continue;
    std::vector<Point3d> pts;
    for (int i : hull) pts.push_back(pl.PointAt(uv[i].x, uv[i].y));
    pts.push_back(pts.front());
    AddLike(ctx, t.id, SceneObject::MakeCurve(PolylineCurve(pts)));
    ++made;
  }
  ctx.Print("MeshOutline: created " + std::to_string(made) + " outline curve(s)");
}

// Moves the listed faces into a new mesh object; removes the source if emptied.
void ExtractFaces(CommandContext& ctx, const Target& t, const std::vector<int>& faces, const char* label) {
  if (faces.empty()) { ctx.Print(std::string(label) + ": no faces matched on object " + std::to_string(t.id)); return; }
  RawMesh r = Unpack(t.mesh.raw());
  auto [taken, rest] = SplitFaces(r, faces);
  const ObjectId like = t.id;
  if (rest.f.empty()) { ctx.Doc().Remove(t.id); ctx.Print(std::string(label) + ": all " + std::to_string(faces.size()) + " faces extracted"); }
  else { Commit(ctx, t.id, Pack(rest)); ctx.Print(std::string(label) + ": extracted " + std::to_string(faces.size()) + " of " + std::to_string(r.f.size()) + " faces"); }
  SceneObject n = SceneObject::MakeMesh(Pack(taken));
  n.selected = true;
  AddLike(ctx, like, std::move(n));
}

void ExtractNear(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in, const char* label, double max_angle, bool single) {
  std::vector<Target> ts = Targets(ctx, ids, label);
  if (ts.empty()) return;
  const Point3d p = in.P(0);
  // Only the mesh whose face is nearest the pick.
  size_t best = 0; int best_face = -1; double bd = 0;
  for (size_t i = 0; i < ts.size(); ++i) {
    RawMesh r = Unpack(ts[i].mesh.raw());
    const int fi = NearestFace(r, p);
    if (fi < 0) continue;
    const double d = (r.FaceCenter(r.f[fi]) - p).Length();
    if (best_face < 0 || d < bd) { best = i; best_face = fi; bd = d; }
  }
  if (best_face < 0) return;
  ctx.Doc().BeginChange(label);
  RawMesh r = Unpack(ts[best].mesh.raw());
  ExtractFaces(ctx, ts[best], single ? std::vector<int>{best_face} : FloodFaces(r, best_face, max_angle), label);
}

using FaceMetric = std::function<double(const RawMesh&, const Face&)>;
void ExtractByMetric(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in, const char* label, const FaceMetric& metric) {
  std::vector<Target> ts = Targets(ctx, ids, label);
  if (ts.empty()) return;
  const double lo = in.N(0, 0), hi = in.N(1, 1e300);
  ctx.Doc().BeginChange(label);
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    std::vector<int> faces;
    for (size_t i = 0; i < r.f.size(); ++i) { const double m = metric(r, r.f[i]); if (m >= lo && m <= hi) faces.push_back(static_cast<int>(i)); }
    ExtractFaces(ctx, t, faces, label);
  }
}

double LongestEdge(const RawMesh& r, const Face& f) { double m = 0; const int n = RawMesh::Corners(f); for (int k = 0; k < n; ++k) m = std::max(m, (r.v[f[(k + 1) % n]] - r.v[f[k]]).Length()); return m; }
double ShortestEdge(const RawMesh& r, const Face& f) { double m = 1e300; const int n = RawMesh::Corners(f); for (int k = 0; k < n; ++k) m = std::min(m, (r.v[f[(k + 1) % n]] - r.v[f[k]]).Length()); return m; }

void ExtractEdgeCurves(CommandContext& ctx, const std::vector<ObjectId>& ids, const char* label, int mode, const std::optional<Point3d>& near_pt) {
  // mode 0: naked chains and loops, 1: closed loops only, 2: non-manifold edges
  std::vector<Target> ts = Targets(ctx, ids, label);
  if (ts.empty()) return;
  ctx.Doc().BeginChange(label);
  int made = 0;
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    if (mode == 2) {
      for (const auto& kv : BuildEdges(r)) if (kv.second.size() > 2) { AddLike(ctx, t.id, SceneObject::MakeCurve(PolylineCurve({r.v[kv.first.first], r.v[kv.first.second]}))); ++made; }
      continue;
    }
    std::vector<Chain> chains = NakedChains(r);
    if (near_pt) {
      // Keep the chain nearest the pick.
      int best = -1; double bd = 0;
      for (size_t i = 0; i < chains.size(); ++i) {
        const std::vector<int>& v = chains[i].v;
        for (size_t k = 0; k + 1 < v.size() + (chains[i].closed ? 1 : 0); ++k) {
          const double d = PointToSegment(*near_pt, r.v[v[k]], r.v[v[(k + 1) % v.size()]]);
          if (best < 0 || d < bd) { best = static_cast<int>(i); bd = d; }
        }
      }
      if (best < 0) continue;
      chains = {chains[best]};
    }
    for (const Chain& c : chains) {
      if (mode == 1 && !c.closed) continue;
      std::vector<Point3d> pts;
      for (int vi : c.v) pts.push_back(r.v[vi]);
      if (c.closed) pts.push_back(pts.front());
      if (pts.size() < 2) continue;
      AddLike(ctx, t.id, SceneObject::MakeCurve(PolylineCurve(pts)));
      ++made;
    }
  }
  ctx.Print(std::string(label) + ": created " + std::to_string(made) + " curve(s)");
}

void CollapseSmallest(CommandContext& ctx, const std::vector<ObjectId>& ids, int what) {
  // what 0: smallest face, 1: shortest edge
  const char* label = what == 0 ? "CollapseMeshFace" : "CollapseMeshEdge";
  std::vector<Target> ts = Targets(ctx, ids, label);
  if (ts.empty()) return;
  ctx.Doc().BeginChange(label);
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    std::vector<int> group;
    if (what == 0) {
      int best = -1; double ba = 0;
      for (size_t i = 0; i < r.f.size(); ++i) { const double a = r.FaceArea(r.f[i]); if (best < 0 || a < ba) { best = static_cast<int>(i); ba = a; } }
      if (best < 0) continue;
      for (int k = 0; k < RawMesh::Corners(r.f[best]); ++k) group.push_back(r.f[best][k]);
      ctx.Print(std::string(label) + ": collapsed face " + std::to_string(best) + " (area " + FormatNumber(ba) + ")");
    } else {
      EdgeKey best{-1, -1}; double bl = 0;
      for (const auto& kv : BuildEdges(r)) { const double l = (r.v[kv.first.second] - r.v[kv.first.first]).Length(); if (best.first < 0 || l < bl) { best = kv.first; bl = l; } }
      if (best.first < 0) continue;
      group = {best.first, best.second};
      ctx.Print(std::string(label) + ": collapsed edge of length " + FormatNumber(bl));
    }
    Commit(ctx, t.id, Pack(CollapseGroups(r, {group})));
  }
}

void SplitEdgeNear(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  std::vector<Target> ts = Targets(ctx, ids, "SplitMeshEdge");
  if (ts.empty()) return;
  const Point3d p = in.P(0);
  ctx.Doc().BeginChange("SplitMeshEdge");
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    const EdgeMap em = BuildEdges(r);
    const std::vector<EdgeUse>* best = nullptr; EdgeKey bk{-1, -1}; double bd = 0;
    for (const auto& kv : em) { const double d = PointToSegment(p, r.v[kv.first.first], r.v[kv.first.second]); if (!best || d < bd) { best = &kv.second; bk = kv.first; bd = d; } }
    if (!best) continue;
    const int mid = static_cast<int>(r.v.size());
    r.v.push_back(Point3d::Origin + ((r.v[bk.first] - Point3d::Origin) + (r.v[bk.second] - Point3d::Origin)) / 2);
    std::vector<int> touched;
    for (const EdgeUse& u : *best) touched.push_back(u.face);
    std::vector<Face> add;
    for (int fi : touched) {
      const Face f = r.f[fi];
      const int n = RawMesh::Corners(f);
      int k = -1;
      for (int i = 0; i < n; ++i) if (Key(f[i], f[(i + 1) % n]) == bk) k = i;
      if (k < 0) continue;
      const int a = f[k], b = f[(k + 1) % n], c = f[(k + 2) % n];
      if (n == 3) { r.f[fi] = RawMesh::Tri(a, mid, c); add.push_back(RawMesh::Tri(mid, b, c)); }
      else { const int d = f[(k + 3) % n]; r.f[fi] = RawMesh::Tri(a, mid, d); add.push_back(RawMesh::Quad(mid, b, c, d)); }
    }
    r.f.insert(r.f.end(), add.begin(), add.end());
    Commit(ctx, t.id, Pack(r));
    ctx.Print("SplitMeshEdge: split 1 edge at its midpoint (" + std::to_string(touched.size()) + " face(s) affected)");
  }
}

void SwapEdgeNear(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  std::vector<Target> ts = Targets(ctx, ids, "SwapMeshEdge");
  if (ts.empty()) return;
  const Point3d p = in.P(0);
  ctx.Doc().BeginChange("SwapMeshEdge");
  for (const Target& t : ts) {
    kernel::Mesh km = t.mesh;
    ON_Mesh& m = km.raw();
    const ON_MeshTopology& top = m.Topology();
    int best = -1; double bd = 0;
    for (int i = 0; i < top.m_tope.Count(); ++i) {
      const ON_MeshTopologyEdge& e = top.m_tope[i];
      if (e.m_topf_count != 2) continue;
      const double d = PointToSegment(p, m.Vertex(top.m_topv[e.m_topvi[0]].m_vi[0]), m.Vertex(top.m_topv[e.m_topvi[1]].m_vi[0]));
      if (best < 0 || d < bd) { best = i; bd = d; }
    }
    if (best < 0) { ctx.Warn("SwapMeshEdge: no interior edge found"); continue; }
    if (!m.IsSwappableEdge(best)) { ctx.Warn("SwapMeshEdge: the nearest edge is not between two triangles"); continue; }
    if (m.SwapEdge(best)) { m.ComputeFaceNormals(); m.ComputeVertexNormals(); Commit(ctx, t.id, km); ctx.Print("SwapMeshEdge: swapped 1 edge"); }
    else ctx.Warn("SwapMeshEdge: swap failed");
  }
}

void Merge2Faces(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  std::vector<Target> ts = Targets(ctx, ids, "Merge2MeshFaces");
  if (ts.empty()) return;
  const Point3d p = in.P(0);
  ctx.Doc().BeginChange("Merge2MeshFaces");
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    const int fi = NearestFace(r, p);
    if (fi < 0 || !RawMesh::IsTri(r.f[fi])) { ctx.Warn("Merge2MeshFaces: the nearest face is not a triangle"); continue; }
    const EdgeMap em = BuildEdges(r);
    const Face f = r.f[fi];
    bool merged = false;
    for (int k = 0; k < 3 && !merged; ++k) {
      const int a = f[k], b = f[(k + 1) % 3], c = f[(k + 2) % 3];
      const auto it = em.find(Key(a, b));
      if (it == em.end()) continue;
      for (const EdgeUse& u : it->second) {
        if (u.face == fi || !RawMesh::IsTri(r.f[u.face])) continue;
        const Face& g = r.f[u.face];
        int d = -1;
        for (int j = 0; j < 3; ++j) if (g[j] != a && g[j] != b) d = g[j];
        if (d < 0) continue;
        r.f[fi] = RawMesh::Quad(a, d, b, c);
        r.f.erase(r.f.begin() + u.face);
        merged = true;
        break;
      }
    }
    if (!merged) { ctx.Warn("Merge2MeshFaces: no adjacent triangle found"); continue; }
    Commit(ctx, t.id, Pack(r));
    ctx.Print("Merge2MeshFaces: merged two triangles into a quad");
  }
}

// ---- intersections ---------------------------------------------------------

// Where triangle `t` crosses the plane (n, d): 0 or 2 points.
int ClipTriangle(const Point3d t[3], Vector3d n, double d, Point3d out[2]) {
  double s[3];
  for (int i = 0; i < 3; ++i) s[i] = ON_DotProduct(n, t[i] - Point3d::Origin) - d;
  const double eps = 1e-9;
  int count = 0;
  for (int i = 0; i < 3 && count < 2; ++i) {
    const int j = (i + 1) % 3;
    if (std::fabs(s[i]) <= eps) { out[count++] = t[i]; continue; }
    if ((s[i] > 0) != (s[j] > 0) && std::fabs(s[j]) > eps) {
      const double f = s[i] / (s[i] - s[j]);
      out[count++] = t[i] + (t[j] - t[i]) * f;
    }
  }
  if (count == 2 && (out[0] - out[1]).Length() <= eps) return 0;
  return count == 2 ? 2 : 0;
}

bool TriTri(const Point3d a[3], const Point3d b[3], Point3d& s0, Point3d& s1) {
  Vector3d na = ON_CrossProduct(a[1] - a[0], a[2] - a[0]), nb = ON_CrossProduct(b[1] - b[0], b[2] - b[0]);
  if (!na.Unitize() || !nb.Unitize()) return false;
  Vector3d line = ON_CrossProduct(na, nb);
  if (!line.Unitize()) return false;  // coplanar or parallel
  Point3d pa[2], pb[2];
  if (ClipTriangle(a, nb, ON_DotProduct(nb, b[0] - Point3d::Origin), pa) != 2) return false;
  if (ClipTriangle(b, na, ON_DotProduct(na, a[0] - Point3d::Origin), pb) != 2) return false;
  double ta0 = ON_DotProduct(line, pa[0] - Point3d::Origin), ta1 = ON_DotProduct(line, pa[1] - Point3d::Origin);
  double tb0 = ON_DotProduct(line, pb[0] - Point3d::Origin), tb1 = ON_DotProduct(line, pb[1] - Point3d::Origin);
  if (ta0 > ta1) std::swap(ta0, ta1);
  if (tb0 > tb1) std::swap(tb0, tb1);
  const double lo = std::max(ta0, tb0), hi = std::min(ta1, tb1);
  if (hi - lo <= 1e-9) return false;
  const Point3d base = ta0 <= ta1 ? pa[0] : pa[1];
  const double tbase = ON_DotProduct(line, base - Point3d::Origin);
  s0 = base + line * (lo - tbase);
  s1 = base + line * (hi - tbase);
  return true;
}

struct Tri { Point3d p[3]; int face; ON_BoundingBox box; };

std::vector<Tri> Triangles(const RawMesh& r) {
  std::vector<Tri> out;
  for (size_t i = 0; i < r.f.size(); ++i) {
    const Face& f = r.f[i];
    auto add = [&](int a, int b, int c) {
      Tri t{{r.v[a], r.v[b], r.v[c]}, static_cast<int>(i), ON_BoundingBox()};
      t.box.Set(t.p[0], false); t.box.Set(t.p[1], true); t.box.Set(t.p[2], true);
      out.push_back(t);
    };
    add(f[0], f[1], f[2]);
    if (!RawMesh::IsTri(f)) add(f[0], f[2], f[3]);
  }
  return out;
}

bool BoxesTouch(const ON_BoundingBox& a, const ON_BoundingBox& b, double tol) {
  return !(a.m_max.x + tol < b.m_min.x || b.m_max.x + tol < a.m_min.x || a.m_max.y + tol < b.m_min.y || b.m_max.y + tol < a.m_min.y || a.m_max.z + tol < b.m_min.z || b.m_max.z + tol < a.m_min.z);
}

// Chains segments into polylines by matching endpoints within `tol`.
std::vector<std::vector<Point3d>> ChainSegments(const std::vector<std::pair<Point3d, Point3d>>& segs, double tol) {
  std::vector<bool> used(segs.size(), false);
  std::vector<std::vector<Point3d>> out;
  for (size_t i = 0; i < segs.size(); ++i) {
    if (used[i]) continue;
    used[i] = true;
    std::vector<Point3d> pl = {segs[i].first, segs[i].second};
    for (int dir = 0; dir < 2; ++dir) {
      while (true) {
        const Point3d end = dir == 0 ? pl.back() : pl.front();
        bool grew = false;
        for (size_t j = 0; j < segs.size() && !grew; ++j) {
          if (used[j]) continue;
          Point3d next;
          if ((segs[j].first - end).Length() <= tol) next = segs[j].second;
          else if ((segs[j].second - end).Length() <= tol) next = segs[j].first;
          else continue;
          used[j] = true;
          if (dir == 0) pl.push_back(next); else pl.insert(pl.begin(), next);
          grew = true;
        }
        if (!grew) break;
      }
    }
    out.push_back(std::move(pl));
  }
  return out;
}

void MeshIntersect(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Target> ts = Targets(ctx, ids, "MeshIntersect");
  if (ts.size() < 2) { ctx.Warn("MeshIntersect: select at least two meshes"); return; }
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-6);
  std::vector<std::pair<Point3d, Point3d>> segs;
  std::vector<std::vector<Tri>> tris;
  for (const Target& t : ts) tris.push_back(Triangles(Unpack(t.mesh.raw())));
  for (size_t i = 0; i < ts.size(); ++i)
    for (size_t j = i + 1; j < ts.size(); ++j)
      for (const Tri& a : tris[i])
        for (const Tri& b : tris[j]) {
          if (!BoxesTouch(a.box, b.box, tol)) continue;
          Point3d s0, s1;
          if (TriTri(a.p, b.p, s0, s1)) segs.push_back({s0, s1});
        }
  if (segs.empty()) { ctx.Print("MeshIntersect: no intersections found"); return; }
  ctx.Doc().BeginChange("MeshIntersect");
  int made = 0;
  for (const std::vector<Point3d>& pl : ChainSegments(segs, tol * 10)) { if (pl.size() >= 2) { AddCurve(ctx, PolylineCurve(pl), "MeshIntersect"); ++made; } }
  ctx.Print("MeshIntersect: " + std::to_string(segs.size()) + " segment(s) in " + std::to_string(made) + " polyline(s)");
}

void MeshSelfIntersect(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Target> ts = Targets(ctx, ids, "MeshSelfIntersect");
  if (ts.empty()) return;
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-6);
  for (const Target& t : ts) {
    RawMesh r = Unpack(t.mesh.raw());
    std::vector<Tri> tris = Triangles(r);
    std::vector<std::pair<Point3d, Point3d>> segs;
    int pairs = 0;
    for (size_t i = 0; i < tris.size(); ++i)
      for (size_t j = i + 1; j < tris.size(); ++j) {
        if (tris[i].face == tris[j].face || !BoxesTouch(tris[i].box, tris[j].box, tol)) continue;
        // Skip triangles that share a vertex.
        bool adjacent = false;
        for (int a = 0; a < 3 && !adjacent; ++a) for (int b = 0; b < 3; ++b) if ((tris[i].p[a] - tris[j].p[b]).Length() <= tol) { adjacent = true; break; }
        if (adjacent) continue;
        Point3d s0, s1;
        if (TriTri(tris[i].p, tris[j].p, s0, s1)) { ++pairs; segs.push_back({s0, s1}); }
      }
    if (pairs == 0) { ctx.Print("MeshSelfIntersect: object " + std::to_string(t.id) + " does not self-intersect"); continue; }
    ctx.Doc().BeginChange("MeshSelfIntersect");
    for (const std::vector<Point3d>& pl : ChainSegments(segs, tol * 10)) if (pl.size() >= 2) AddLike(ctx, t.id, SceneObject::MakeCurve(PolylineCurve(pl)));
    ctx.Print("MeshSelfIntersect: object " + std::to_string(t.id) + " has " + std::to_string(pairs) + " self-intersecting triangle pair(s); intersection curves added");
  }
}

// ---- triangulations -------------------------------------------------------

// Delaunay triangulation (Bowyer-Watson) of 2D points; returns CCW triangles.
std::vector<std::array<int, 3>> Delaunay(const std::vector<ON_2dPoint>& pts) {
  struct T { int a, b, c; double cx, cy, r2; };
  std::vector<std::array<int, 3>> out;
  if (pts.size() < 3) return out;
  double minx = pts[0].x, maxx = minx, miny = pts[0].y, maxy = miny;
  for (const ON_2dPoint& p : pts) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
  const double span = std::max(maxx - minx, maxy - miny) * 20 + 1, mx = (minx + maxx) / 2, my = (miny + maxy) / 2;
  std::vector<ON_2dPoint> P = pts;
  const int n = static_cast<int>(pts.size());
  P.emplace_back(mx - span, my - span);
  P.emplace_back(mx + span, my - span);
  P.emplace_back(mx, my + span);
  auto make = [&](int a, int b, int c) {
    T t{a, b, c, 0, 0, 0};
    const double ax = P[a].x, ay = P[a].y, bx = P[b].x, by = P[b].y, cx = P[c].x, cy = P[c].y;
    const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(d) < 1e-300) { t.r2 = -1; return t; }
    t.cx = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
    t.cy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
    t.r2 = (ax - t.cx) * (ax - t.cx) + (ay - t.cy) * (ay - t.cy);
    return t;
  };
  std::vector<T> tris = {make(n, n + 1, n + 2)};
  for (int i = 0; i < n; ++i) {
    std::vector<T> keep;
    std::map<std::pair<int, int>, int> edge_count;
    std::vector<std::pair<int, int>> edges;
    for (const T& t : tris) {
      const double dx = P[i].x - t.cx, dy = P[i].y - t.cy;
      if (t.r2 >= 0 && dx * dx + dy * dy < t.r2) {
        for (auto e : {std::make_pair(t.a, t.b), std::make_pair(t.b, t.c), std::make_pair(t.c, t.a)}) { edges.push_back(e); ++edge_count[Key(e.first, e.second)]; }
      } else keep.push_back(t);
    }
    for (const auto& e : edges) if (edge_count[Key(e.first, e.second)] == 1) keep.push_back(make(e.first, e.second, i));
    tris = std::move(keep);
  }
  for (const T& t : tris) {
    if (t.a >= n || t.b >= n || t.c >= n) continue;
    const double cross = (P[t.b].x - P[t.a].x) * (P[t.c].y - P[t.a].y) - (P[t.b].y - P[t.a].y) * (P[t.c].x - P[t.a].x);
    if (std::fabs(cross) < 1e-12) continue;
    out.push_back(cross > 0 ? std::array<int, 3>{t.a, t.b, t.c} : std::array<int, 3>{t.a, t.c, t.b});
  }
  return out;
}

// Ear-clipping triangulation of a simple 2D polygon; returns CCW triangles.
std::vector<std::array<int, 3>> EarClip(std::vector<ON_2dPoint> poly) {
  std::vector<std::array<int, 3>> out;
  int n = static_cast<int>(poly.size());
  if (n < 3) return out;
  double area = 0;
  for (int i = 0; i < n; ++i) area += poly[i].x * poly[(i + 1) % n].y - poly[(i + 1) % n].x * poly[i].y;
  std::vector<int> idx(n);
  std::iota(idx.begin(), idx.end(), 0);
  if (area < 0) std::reverse(idx.begin(), idx.end());
  auto cross = [&](int a, int b, int c) { return (poly[b].x - poly[a].x) * (poly[c].y - poly[a].y) - (poly[b].y - poly[a].y) * (poly[c].x - poly[a].x); };
  auto inside = [&](int p, int a, int b, int c) { return cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0; };
  int guard = 0;
  while (idx.size() > 3 && guard++ < 10000) {
    bool clipped = false;
    const int m = static_cast<int>(idx.size());
    for (int i = 0; i < m; ++i) {
      const int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
      if (cross(a, b, c) <= 1e-14) continue;
      bool ear = true;
      for (int j = 0; j < m && ear; ++j) { const int p = idx[j]; if (p == a || p == b || p == c) continue; if (inside(p, a, b, c)) ear = false; }
      if (!ear) continue;
      out.push_back({a, b, c});
      idx.erase(idx.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) {  // degenerate polygon: clip any convex corner to make progress
      int i = 0;
      for (int k = 0; k < m; ++k) if (cross(idx[(k + m - 1) % m], idx[k], idx[(k + 1) % m]) > 0) { i = k; break; }
      out.push_back({idx[(i + m - 1) % m], idx[i], idx[(i + 1) % m]});
      idx.erase(idx.begin() + i);
    }
  }
  if (idx.size() == 3) out.push_back({idx[0], idx[1], idx[2]});
  return out;
}

// Vertices of a closed curve as a polygon (control points for polylines,
// samples otherwise), without the closing duplicate.
std::vector<Point3d> CurvePolygon(const kernel::NurbsCurve& c, int samples = 64) {
  std::vector<Point3d> pts;
  if (c.Degree() == 1 && !c.IsRational()) {
    for (int i = 0; i < c.ControlPointCount(); ++i) pts.push_back(c.ControlPointAt(i));
  } else {
    const kernel::Interval d = c.Domain();
    for (int i = 0; i < samples; ++i) pts.push_back(c.PointAt(d.min + (d.max - d.min) * i / samples));
  }
  while (pts.size() > 1 && (pts.back() - pts.front()).Length() <= ON_ZERO_TOLERANCE) pts.pop_back();
  // Drop consecutive duplicates.
  std::vector<Point3d> clean;
  for (const Point3d& p : pts) if (clean.empty() || (p - clean.back()).Length() > ON_ZERO_TOLERANCE) clean.push_back(p);
  return clean;
}

std::optional<ON_Plane> ClosedPlanarCurvePlane(CommandContext& ctx, const SceneObject& o) {
  if (o.kind != ObjectKind::Curve || !o.curve->raw().IsClosed()) return std::nullopt;
  ON_Plane pl;
  if (!o.curve->raw().IsPlanar(&pl, ctx.Settings().absolute_tolerance * 10)) return std::nullopt;
  if (ON_DotProduct(pl.zaxis, ActiveNormal(ctx)) < 0) pl.Flip();
  return pl;
}

// Triangulated planar polygon as a mesh with its normal along `pl.zaxis`.
std::optional<RawMesh> PolygonMesh(const std::vector<Point3d>& poly, const ON_Plane& pl) {
  if (poly.size() < 3) return std::nullopt;
  std::vector<ON_2dPoint> uv;
  for (const Point3d& p : poly) { double u, v; pl.ClosestPointTo(p, &u, &v); uv.emplace_back(u, v); }
  RawMesh r;
  r.v = poly;
  for (const auto& t : EarClip(uv)) r.f.push_back(RawMesh::Tri(t[0], t[1], t[2]));
  if (r.f.empty()) return std::nullopt;
  return r;
}

void PlanarMesh(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("PlanarMesh");
  int made = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<ON_Plane> pl = ClosedPlanarCurvePlane(ctx, *o);
    if (!pl) { ctx.Warn("PlanarMesh: object " + std::to_string(id) + " is not a closed planar curve; skipped"); continue; }
    std::optional<RawMesh> r = PolygonMesh(CurvePolygon(*o->curve), *pl);
    if (!r) continue;
    AddLike(ctx, id, SceneObject::MakeMesh(Pack(*r)));
    ++made;
  }
  ctx.Print("PlanarMesh: created " + std::to_string(made) + " mesh(es)");
}

// Miter offset of a closed polygon in `pl` (positive = outward).
std::vector<Point3d> OffsetPolygon(const std::vector<Point3d>& poly, const ON_Plane& pl, double d) {
  const int n = static_cast<int>(poly.size());
  std::vector<ON_2dPoint> uv;
  for (const Point3d& p : poly) { double u, v; pl.ClosestPointTo(p, &u, &v); uv.emplace_back(u, v); }
  double area = 0;
  for (int i = 0; i < n; ++i) area += uv[i].x * uv[(i + 1) % n].y - uv[(i + 1) % n].x * uv[i].y;
  const double sign = area >= 0 ? 1 : -1;  // CCW: outward normal is (t.y, -t.x)
  std::vector<Point3d> out;
  for (int i = 0; i < n; ++i) {
    ON_2dVector e1 = uv[i] - uv[(i + n - 1) % n], e2 = uv[(i + 1) % n] - uv[i];
    e1.Unitize(); e2.Unitize();
    ON_2dVector n1(e1.y * sign, -e1.x * sign), n2(e2.y * sign, -e2.x * sign);
    ON_2dVector bis = n1 + n2;
    double scale = 1;
    if (bis.Unitize()) { const double c = ON_DotProduct(bis, n1); scale = c > 0.2 ? 1 / c : 5; } else bis = n1;
    const ON_2dPoint q = uv[i] + bis * (d * scale);
    out.push_back(pl.PointAt(q.x, q.y));
  }
  return out;
}

// Ring of quads between a polygon and its offset (normal along pl.zaxis).
RawMesh RingMesh(const std::vector<Point3d>& inner, const std::vector<Point3d>& outer, const ON_Plane& pl) {
  RawMesh r;
  const int n = static_cast<int>(inner.size());
  r.v = inner;
  r.v.insert(r.v.end(), outer.begin(), outer.end());
  for (int i = 0; i < n; ++i) r.f.push_back(RawMesh::Quad(i, (i + 1) % n, n + (i + 1) % n, n + i));
  if (ON_DotProduct(r.FaceNormal(r.f[0]), pl.zaxis) < 0) r.FlipAll();
  return r;
}

void Slab(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in, bool with_height) {
  const char* label = with_height ? "Slab" : "Ribbon";
  const double d = in.N(0, 1);
  const double h = with_height ? in.N(1, 5) : 0;
  if (d == 0 || (with_height && h == 0)) { ctx.Warn(std::string(label) + ": distance and height must be non-zero"); return; }
  ctx.Doc().BeginChange(label);
  int made = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<ON_Plane> pl = ClosedPlanarCurvePlane(ctx, *o);
    if (!pl) { ctx.Warn(std::string(label) + ": object " + std::to_string(id) + " is not a closed planar curve; skipped"); continue; }
    const std::vector<Point3d> poly = CurvePolygon(*o->curve);
    if (poly.size() < 3) continue;
    RawMesh ring = RingMesh(poly, OffsetPolygon(poly, *pl, d), *pl);
    RawMesh out = ring;
    if (with_height) out = Thicken(ring, std::vector<Vector3d>(ring.v.size(), pl->zaxis), h, true);
    AddLike(ctx, id, SceneObject::MakeMesh(Pack(out)));
    ++made;
  }
  ctx.Print(std::string(label) + ": created " + std::to_string(made) + " mesh(es)");
}

void MeshPatch(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  const ON_Plane pl = ActivePlane(ctx);
  std::vector<Point3d> pts;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Point) pts.push_back(o->point);
    else if (o->kind == ObjectKind::Curve) { std::vector<Point3d> pg = CurvePolygon(*o->curve, 32); pts.insert(pts.end(), pg.begin(), pg.end()); }
    else if (std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance)) for (int i = 0; i < m->VertexCount(); ++i) pts.push_back(m->raw().Vertex(i));
  }
  // Drop duplicates in the plane.
  std::vector<ON_2dPoint> uv;
  std::vector<Point3d> keep;
  for (const Point3d& p : pts) {
    double u, v;
    pl.ClosestPointTo(p, &u, &v);
    bool dup = false;
    for (const ON_2dPoint& q : uv) if (std::fabs(q.x - u) < 1e-9 && std::fabs(q.y - v) < 1e-9) { dup = true; break; }
    if (dup) continue;
    uv.emplace_back(u, v);
    keep.push_back(p);
  }
  if (keep.size() < 3) { ctx.Warn("MeshPatch: need at least three distinct points"); return; }
  RawMesh r;
  r.v = keep;
  for (const auto& t : Delaunay(uv)) r.f.push_back(RawMesh::Tri(t[0], t[1], t[2]));
  if (r.f.empty()) { ctx.Warn("MeshPatch: points are collinear"); return; }
  if (ON_DotProduct(r.FaceNormal(r.f[0]), pl.zaxis) < 0) r.FlipAll();
  AddObject(ctx, SceneObject::MakeMesh(Pack(r)), "MeshPatch");
  ctx.Print("MeshPatch: " + std::to_string(r.f.size()) + " triangles from " + std::to_string(keep.size()) + " points");
}

void MeshFromLines(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  const double tol = std::max(ctx.Settings().absolute_tolerance, 1e-6);
  std::vector<Point3d> verts;
  auto vertex = [&](Point3d p) { for (size_t i = 0; i < verts.size(); ++i) if ((verts[i] - p).Length() <= tol) return static_cast<int>(i); verts.push_back(p); return static_cast<int>(verts.size() - 1); };
  std::set<EdgeKey> edges;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Curve) continue;
    const kernel::NurbsCurve& c = *o->curve;
    std::vector<Point3d> pg;
    if (c.Degree() == 1) for (int i = 0; i < c.ControlPointCount(); ++i) pg.push_back(c.ControlPointAt(i));
    else { pg.push_back(c.PointAt(c.Domain().min)); pg.push_back(c.PointAt(c.Domain().max)); }
    for (size_t i = 0; i + 1 < pg.size(); ++i) { const int a = vertex(pg[i]), b = vertex(pg[i + 1]); if (a != b) edges.insert(Key(a, b)); }
  }
  if (edges.empty()) { ctx.Warn("MeshFromLines: select lines or polylines"); return; }
  std::vector<std::set<int>> adj(verts.size());
  for (const EdgeKey& e : edges) { adj[e.first].insert(e.second); adj[e.second].insert(e.first); }
  RawMesh r;
  r.v = verts;
  std::set<std::vector<int>> seen;
  auto add = [&](std::vector<int> cyc, Face f) { std::sort(cyc.begin(), cyc.end()); if (seen.insert(cyc).second) r.f.push_back(f); };
  for (const EdgeKey& e : edges) {
    const int a = e.first, b = e.second;
    for (int c : adj[a]) if (c != b && adj[b].count(c)) add({a, b, c}, RawMesh::Tri(a, b, c));
  }
  for (const EdgeKey& e : edges) {
    const int a = e.first, b = e.second;
    for (int c : adj[b]) {
      if (c == a) continue;
      for (int d : adj[c]) {
        if (d == a || d == b || !adj[d].count(a)) continue;
        if (adj[a].count(c) || adj[b].count(d)) continue;  // has a chord: covered by triangles
        add({a, b, c, d}, RawMesh::Quad(a, b, c, d));
      }
    }
  }
  if (r.f.empty()) { ctx.Warn("MeshFromLines: no closed 3- or 4-line loops found"); return; }
  UnifyNormals(r);
  AddObject(ctx, SceneObject::MakeMesh(Pack(r)), "MeshFromLines");
  ctx.Print("MeshFromLines: built " + std::to_string(r.f.size()) + " face(s) from " + std::to_string(edges.size()) + " line(s)");
}

// PolylineOnMesh: points until Enter, each pulled to the mesh.
class PolylineOnMeshCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantObjects("Select mesh"); }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    std::vector<Target> ts = Targets(ctx, ids, "PolylineOnMesh");
    if (ts.empty()) { Finish(); return; }
    mesh_ = ts.front().mesh;
    WantPoint("Start of polyline on mesh");
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    pts_.push_back(mesh_.ClosestPoint(p));
    ctx.SetLastPoint(pts_.back());
    WantPoint("Next point. Press Enter when done");
  }
  void OnEnter(CommandContext& ctx) override {
    ctx.ClearPreview();
    if (pts_.size() >= 2) { AddCurve(ctx, PolylineCurve(pts_), "PolylineOnMesh"); ctx.Print("PolylineOnMesh: " + std::to_string(pts_.size()) + " points pulled to the mesh"); }
    Finish();
  }
  void OnHover(CommandContext& ctx, Point3d h) override { if (!pts_.empty()) { ctx.ClearPreview(); ctx.AddPreviewPolyline(pts_); ctx.AddPreviewLine(pts_.back(), mesh_.ClosestPoint(h)); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }
  kernel::Mesh mesh_;
  std::vector<Point3d> pts_;
};

// MeshTrim: cut by a plane through two points (normal to the CPlane) and
// delete the side a third point is on.
void MeshTrim(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  std::vector<Target> ts = Targets(ctx, ids, "MeshTrim");
  if (ts.empty()) return;
  const Point3d a = in.P(0), b = in.P(1), side = in.P(2);
  Vector3d n = ON_CrossProduct(b - a, ActiveNormal(ctx));
  if (!n.Unitize()) { ctx.Warn("MeshTrim: degenerate cutting line"); return; }
  if (ON_DotProduct(side - a, n) > 0) n = -n;  // n now points toward the kept side
  const double offset = ON_DotProduct(n, a - Point3d::Origin);
  ctx.Doc().BeginChange("MeshTrim");
  for (const Target& t : ts) {
    if (t.mesh.IsClosedManifold()) {
      try {
        auto [keep, drop] = kernel::SplitByPlane(t.mesh, n, offset);
        Commit(ctx, t.id, keep);
        ctx.Print("MeshTrim: trimmed closed mesh " + std::to_string(t.id) + " (" + std::to_string(keep.FaceCount()) + " faces kept)");
        continue;
      } catch (const std::exception& ex) { ctx.Warn(std::string("MeshTrim: ") + ex.what() + "; falling back to face deletion"); }
    }
    RawMesh r = Unpack(t.mesh.raw());
    RawMesh out;
    out.v = r.v;
    for (const Face& f : r.f) if (ON_DotProduct(n, r.FaceCenter(f) - Point3d::Origin) - offset >= 0) out.f.push_back(f);
    Commit(ctx, t.id, Pack(out));
    ctx.Print("MeshTrim: removed " + std::to_string(r.f.size() - out.f.size()) + " face(s) on the far side of the cut");
  }
}

void MatchMeshEdge(CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
  std::vector<Target> ts = Targets(ctx, ids, "MatchMeshEdge");
  if (ts.size() < 2) { ctx.Warn("MatchMeshEdge: select at least two meshes"); return; }
  const double dist = in.N(0, 1);
  std::vector<RawMesh> raws;
  std::vector<std::vector<int>> naked;
  for (const Target& t : ts) {
    raws.push_back(Unpack(t.mesh.raw()));
    std::set<int> nv;
    for (const EdgeUse& e : NakedEdges(BuildEdges(raws.back()))) { nv.insert(e.a); nv.insert(e.b); }
    naked.emplace_back(nv.begin(), nv.end());
  }
  int moved = 0;
  for (size_t i = 0; i < raws.size(); ++i)
    for (int vi : naked[i]) {
      Point3d best = raws[i].v[vi]; double bd = dist; bool found = false;
      for (size_t j = 0; j < raws.size(); ++j) {
        if (j == i) continue;
        for (int vj : naked[j]) { const double d = (raws[j].v[vj] - raws[i].v[vi]).Length(); if (d <= bd) { bd = d; best = raws[j].v[vj]; found = true; } }
      }
      if (found && bd > ON_ZERO_TOLERANCE) { raws[i].v[vi] = Point3d::Origin + ((raws[i].v[vi] - Point3d::Origin) + (best - Point3d::Origin)) / 2; ++moved; }
    }
  ctx.Doc().BeginChange("MatchMeshEdge");
  for (size_t i = 0; i < ts.size(); ++i) Commit(ctx, ts[i].id, Pack(raws[i]));
  ctx.Print("MatchMeshEdge: moved " + std::to_string(moved) + " naked-edge vertex(es) within " + FormatNumber(dist));
}

void ComputeVertexColors(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  ctx.Doc().BeginChange("ComputeVertexColors");
  int done = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Mesh) continue;
    ON_Mesh& m = o->mesh->raw();
    m.ComputeVertexNormals();
    m.m_C.SetCount(0);
    for (int i = 0; i < m.VertexCount(); ++i) {
      const ON_3fVector n = m.m_N.Count() > i ? m.m_N[i] : ON_3fVector(0, 0, 1);
      m.m_C.Append(ON_Color(static_cast<int>((n.x * 0.5 + 0.5) * 255), static_cast<int>((n.y * 0.5 + 0.5) * 255), static_cast<int>((n.z * 0.5 + 0.5) * 255)));
    }
    o->InvalidateDisplay();
    ++done;
  }
  ctx.Print("ComputeVertexColors: coloured " + std::to_string(done) + " mesh(es) by vertex normal");
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

// Rings of `sides` vertices at (radius, height) pairs along `axis` from
// `base`, capped at both ends; a zero radius ring becomes a single apex.
RawMesh RingsSolid(Point3d base, Vector3d axis, const std::vector<std::pair<double, double>>& rings, int sides, double phase = 0) {
  ON_Plane pl(base, axis);
  RawMesh r;
  std::vector<std::vector<int>> ring_idx;
  for (const auto& [radius, height] : rings) {
    std::vector<int> idx;
    const Point3d c = base + axis * height;
    if (radius <= ON_ZERO_TOLERANCE) { idx.assign(sides, static_cast<int>(r.v.size())); r.v.push_back(c); }
    else for (int i = 0; i < sides; ++i) { const double a = phase + 2 * kPi * i / sides; idx.push_back(static_cast<int>(r.v.size())); r.v.push_back(c + pl.xaxis * (radius * std::cos(a)) + pl.yaxis * (radius * std::sin(a))); }
    ring_idx.push_back(idx);
  }
  for (size_t k = 0; k + 1 < ring_idx.size(); ++k)
    for (int i = 0; i < sides; ++i) {
      const int a = ring_idx[k][i], b = ring_idx[k][(i + 1) % sides], c = ring_idx[k + 1][(i + 1) % sides], d = ring_idx[k + 1][i];
      if (a == b) r.f.push_back(RawMesh::Tri(a, c, d));
      else if (c == d) r.f.push_back(RawMesh::Tri(a, b, c));
      else r.f.push_back(RawMesh::Quad(a, b, c, d));
    }
  auto cap = [&](const std::vector<int>& idx, double height, bool bottom) {
    if (idx[0] == idx[1]) return;
    const int c = static_cast<int>(r.v.size());
    r.v.push_back(base + axis * height);
    for (int i = 0; i < sides; ++i) { const int a = idx[i], b = idx[(i + 1) % sides]; r.f.push_back(bottom ? RawMesh::Tri(b, a, c) : RawMesh::Tri(a, b, c)); }
  };
  cap(ring_idx.front(), rings.front().second, true);
  cap(ring_idx.back(), rings.back().second, false);
  return r;
}

void AddPrimitive(CommandContext& ctx, RawMesh r, const char* label) {
  kernel::Mesh km = Pack(r);
  if (km.Volume() < 0) { r.FlipAll(); km = Pack(r); }
  AddObject(ctx, SceneObject::MakeMesh(km), label);
  ctx.Print(std::string(label) + ": " + std::to_string(km.FaceCount()) + " faces" + (km.IsClosedManifold() ? ", closed" : ""));
}

void MeshEllipsoid(CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) {
  const Point3d c = in.P(0);
  const double rx = std::fabs(in.N(1, 10)), ry = std::fabs(in.N(2, 7)), rz = std::fabs(in.N(3, 5));
  if (rx <= 0 || ry <= 0 || rz <= 0) { ctx.Warn("MeshEllipsoid: radii must be positive"); return; }
  kernel::Mesh m = kernel::Brep::Sphere(Point3d::Origin, 1.0).TessellateToClosedMesh(32, 16);
  ON_Plane pl = ActivePlane(ctx);
  pl.SetOrigin(c);
  ON_Xform to_plane;
  to_plane.Rotation(ON_Plane(ON_origin, ON_xaxis, ON_yaxis), pl);
  m = m.Transform(to_plane * ON_Xform::DiagonalTransformation(rx, ry, rz));
  AddObject(ctx, SceneObject::MakeMesh(m), "MeshEllipsoid");
  ctx.Print("MeshEllipsoid: radii " + FormatNumber(rx) + ", " + FormatNumber(ry) + ", " + FormatNumber(rz));
}

void TruncatedSolid(CommandContext& ctx, const ToolInput& in, int sides, const char* label) {
  const Point3d base = in.P(0);
  const double r0 = std::fabs(in.N(1, 10)), h = in.N(2, 10), r1 = std::fabs(in.N(3, 5));
  if (r0 <= 0 || h == 0) { ctx.Warn(std::string(label) + ": radius and height must be non-zero"); return; }
  int n = sides > 0 ? sides : static_cast<int>(in.OptNum("Sides", 4));
  n = std::max(3, std::min(n, 256));
  Vector3d axis = ActiveNormal(ctx);
  double hh = h;
  if (h < 0) { axis = -axis; hh = -h; }
  AddPrimitive(ctx, RingsSolid(base, axis, {{r0, 0}, {r1, hh}}, n, sides > 0 ? 0 : kPi / n), label);
}

void Paraboloid(CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) {
  const Point3d apex = in.P(0);
  const double radius = std::fabs(in.N(1, 10)), h = in.N(2, 10);
  if (radius <= 0 || h == 0) { ctx.Warn("Paraboloid: radius and height must be non-zero"); return; }
  Vector3d axis = ActiveNormal(ctx);
  double hh = h;
  if (h < 0) { axis = -axis; hh = -h; }
  const int K = 16;
  std::vector<std::pair<double, double>> rings;
  for (int k = 0; k <= K; ++k) { const double t = static_cast<double>(k) / K; rings.push_back({radius * t, hh * t * t}); }
  RawMesh r = RingsSolid(apex, axis, rings, 32);
  if (!in.Yes("Cap")) {
    // Remove the top cap: it is the last fan of faces around the last vertex.
    const int cap_center = static_cast<int>(r.v.size()) - 1;
    r.f.erase(std::remove_if(r.f.begin(), r.f.end(), [&](const Face& f) { return f[2] == cap_center; }), r.f.end());
  }
  AddPrimitive(ctx, r, "Paraboloid");
}

void Heightfield(CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) {
  ON_Plane pl = ActivePlane(ctx);
  double u0, v0, u1, v1;
  pl.ClosestPointTo(in.P(0), &u0, &v0);
  pl.ClosestPointTo(in.P(1), &u1, &v1);
  if (u0 > u1) std::swap(u0, u1);
  if (v0 > v1) std::swap(v0, v1);
  const int n = std::max(2, std::min(200, static_cast<int>(in.OptNum("Resolution", 24))));
  const double amp = in.OptNum("Amplitude", std::max(u1 - u0, v1 - v0) / 8), waves = in.OptNum("Waves", 2);
  RawMesh r;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const double s = static_cast<double>(i) / (n - 1), t = static_cast<double>(j) / (n - 1);
      const double z = amp * 0.5 * (1 + std::sin(2 * kPi * waves * s) * std::cos(2 * kPi * waves * t));
      r.v.push_back(pl.PointAt(u0 + (u1 - u0) * s, v0 + (v1 - v0) * t) + pl.zaxis * z);
    }
  for (int j = 0; j + 1 < n; ++j) for (int i = 0; i + 1 < n; ++i) r.f.push_back(RawMesh::Quad(j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i));
  AddObject(ctx, SceneObject::MakeMesh(Pack(r)), "Heightfield");
  ctx.Print("Heightfield: " + std::to_string(n) + "x" + std::to_string(n) + " grid, amplitude " + FormatNumber(amp));
}

bool RayTriangle(Point3d o, Vector3d d, const Point3d t[3], double& out_t) {
  const Vector3d e1 = t[1] - t[0], e2 = t[2] - t[0];
  const Vector3d p = ON_CrossProduct(d, e2);
  const double det = ON_DotProduct(e1, p);
  if (std::fabs(det) < 1e-12) return false;
  const double inv = 1 / det;
  const Vector3d s = o - t[0];
  const double u = ON_DotProduct(s, p) * inv;
  if (u < 0 || u > 1) return false;
  const Vector3d q = ON_CrossProduct(s, e1);
  const double v = ON_DotProduct(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  out_t = ON_DotProduct(e2, q) * inv;
  return out_t >= 0;
}

void Drape(CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) {
  ON_Plane pl = ActivePlane(ctx);
  double u0, v0, u1, v1;
  pl.ClosestPointTo(in.P(0), &u0, &v0);
  pl.ClosestPointTo(in.P(1), &u1, &v1);
  if (u0 > u1) std::swap(u0, u1);
  if (v0 > v1) std::swap(v0, v1);
  const int n = std::max(2, std::min(200, static_cast<int>(in.OptNum("Resolution", 20))));
  // Everything visible that can be meshed is a drape target.
  std::vector<Tri> tris;
  double top = 0;
  bool any = false;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o)) continue;
    std::optional<kernel::Mesh> m = MeshOf(o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) continue;
    for (const Tri& t : Triangles(Unpack(m->raw()))) { tris.push_back(t); for (const Point3d& p : t.p) { const double z = ON_DotProduct(p - pl.origin, pl.zaxis); if (!any || z > top) { top = z; any = true; } } }
  }
  if (!any) { ctx.Warn("Drape: no visible surfaces or meshes to drape over"); return; }
  const double start = top + 1;
  RawMesh r;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const Point3d origin = pl.PointAt(u0 + (u1 - u0) * i / (n - 1), v0 + (v1 - v0) * j / (n - 1)) + pl.zaxis * start;
      double best = -1;
      for (const Tri& t : tris) { double d; if (RayTriangle(origin, -pl.zaxis, t.p, d) && (best < 0 || d < best)) best = d; }
      r.v.push_back(best >= 0 ? origin - pl.zaxis * best : pl.PointAt(u0 + (u1 - u0) * i / (n - 1), v0 + (v1 - v0) * j / (n - 1)));
    }
  for (int j = 0; j + 1 < n; ++j) for (int i = 0; i + 1 < n; ++i) r.f.push_back(RawMesh::Quad(j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i));
  AddObject(ctx, SceneObject::MakeMesh(Pack(r)), "Drape");
  ctx.Print("Drape: " + std::to_string(n) + "x" + std::to_string(n) + " grid draped over " + std::to_string(tris.size()) + " triangles");
}

// Runs an action on the selection inside one undo step, catching kernel errors.
CommandFactory MeshAction(const char* prompt, const char* label, std::function<void(CommandContext&, const std::vector<ObjectId>&)> fn, int min = 1) {
  return OnSelection(prompt, [=](CommandContext& ctx, const std::vector<ObjectId>& ids) {
    try { fn(ctx, ids); } catch (const std::exception& ex) { ctx.Warn(std::string(label) + " failed: " + ex.what()); }
  }, min);
}

}  // namespace

void RegisterMeshToolsCommands(CommandEngine& e) {
  // ---- deformations --------------------------------------------------------
  Reg(e, "Twist", Deform("Twist", "Select objects to twist", {PointStep("Start of twist axis"), PointStep("End of twist axis"), NumberStep("Twist angle in degrees", 90)}, {Toggle("Infinite", false)}, TwistMap));
  Reg(e, "Bend", Deform("Bend", "Select objects to bend", {PointStep("Start of spine"), PointStep("End of spine"), PointStep("Point to bend through")}, {}, BendMap));
  Reg(e, "Taper", Deform("Taper", "Select objects to taper", {PointStep("Start of taper axis"), PointStep("End of taper axis"), NumberStep("Start distance", 10), NumberStep("End distance", 5)}, {Toggle("Infinite", false)}, TaperMap));
  Reg(e, "Stretch", Deform("Stretch", "Select objects to stretch", {PointStep("Start of stretch axis"), PointStep("End of stretch axis"), PointStep("Point to stretch to (or new length)")}, {}, StretchMap));
  Reg(e, "Shear", Deform("Shear", "Select objects to shear", {PointStep("Base point"), PointStep("Reference point"), PointStep("Shear angle or point")}, {}, ShearMap));
  Reg(e, "Maelstrom", Deform("Maelstrom", "Select objects to deform", {PointStep("Center of maelstrom"), NumberStep("Start radius", 0), NumberStep("End radius", 10), NumberStep("Angle in degrees", 90)}, {}, MaelstromMap),
      CommandStatus::Partial, "Rotates about the CPlane normal through the centre.");
  Reg(e, "SoftMove", Deform("SoftMove", "Select objects to move", {PointStep("Point to move from"), PointStep("Point to move to")}, {Numeric("Radius", 10)}, SoftMoveMap));
  Reg(e, "SoftTransform", Deform("SoftTransform", "Select objects to transform", {PointStep("Point to move from"), PointStep("Point to move to")}, {Numeric("Radius", 10)}, SoftMoveMap),
      CommandStatus::Partial, "Soft move only; rotation and scaling with falloff are planned.");
  Reg(e, "Smooth", Tool("Select objects to smooth", {NumberStep("Smooth factor", 0.2)}, {Numeric("Iterations", 1), Toggle("FixBoundaries", true)},
                        [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
                          SmoothObjects(ctx, ids, std::max(0.0, std::min(1.0, in.N(0, 0.2))), std::max(1, static_cast<int>(in.OptNum("Iterations", 1))), in.Yes("FixBoundaries"));
                        }));

  // ---- mesh editing ----------------------------------------------------------
  Reg(e, "ExtrudeMesh", Tool("Select meshes to extrude", {NumberStep("Extrusion distance", 1)}, {Toggle("DeleteInput", true)}, ExtrudeMesh));
  Reg(e, "OffsetMesh", Tool("Select meshes to offset", {NumberStep("Offset distance", 1)}, {Toggle("Solid", false), Toggle("DeleteInput", false)}, OffsetMesh));
  Reg(e, "FillMeshHoles", MeshAction("Select meshes to fill holes in", "FillMeshHoles", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { FillMeshHoles(ctx, ids, std::nullopt); }));
  Reg(e, "FillMeshHole", Tool("Select meshes", {PointStep("Point near the hole to fill")}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { FillMeshHoles(ctx, ids, in.P(0)); }));
  Reg(e, "Weld", Tool("Select meshes to weld", {NumberStep("Angle tolerance in degrees", 180)}, {}, WeldCommandAction));
  Reg(e, "WeldVertices", Tool("Select meshes", {}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { ToolInput all = in; all.nums = {180.0}; WeldCommandAction(ctx, ids, all); }),
      CommandStatus::Partial, "Welds every coincident vertex of the selected meshes; vertex picking is planned.");
  Reg(e, "Unweld", Tool("Select meshes to unweld", {NumberStep("Angle tolerance in degrees", 30)}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { UnweldCommandAction(ctx, ids, Rad(in.N(0, 30)), std::nullopt, "Unweld"); }));
  Reg(e, "UnweldEdge", MeshAction("Select meshes", "UnweldEdge", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { UnweldCommandAction(ctx, ids, -1, std::nullopt, "UnweldEdge"); }),
      CommandStatus::Partial, "Unwelds every edge (each face gets its own vertices); edge picking is planned.");
  Reg(e, "UnweldVertex", Tool("Select meshes", {PointStep("Point near the vertex to unweld")}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { UnweldCommandAction(ctx, ids, -1, in.P(0), "UnweldVertex"); }));
  Reg(e, "MeshRepair", MeshAction("Select meshes to repair", "MeshRepair", MeshRepair));
  Reg(e, "RebuildMesh", MeshAction("Select meshes to rebuild", "RebuildMesh", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Target> ts = Targets(ctx, ids, "RebuildMesh");
        if (ts.empty()) return;
        ctx.Doc().BeginChange("RebuildMesh");
        for (const Target& t : ts) { kernel::Mesh km = Pack(Unpack(t.mesh.raw())); Commit(ctx, t.id, km); ctx.Print("RebuildMesh: rebuilt from " + std::to_string(km.FaceCount()) + " faces (normals, colours and texture coordinates recomputed/dropped)"); }
      }));
  Reg(e, "MeshOutline", MeshAction("Select meshes to outline", "MeshOutline", MeshOutline), CommandStatus::Partial, "Convex hull of the mesh projected on the CPlane.");
  Reg(e, "ExtractMeshFaces", Tool("Select meshes", {PointStep("Point on the face to extract")}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { ExtractNear(ctx, ids, in, "ExtractMeshFaces", 0, true); }),
      CommandStatus::Partial, "Extracts the face nearest a picked point; multi-face selection is planned.");
  Reg(e, "ExtractMeshPart", Tool("Select meshes", {PointStep("Point on the part to extract")}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { ExtractNear(ctx, ids, in, "ExtractMeshPart", kPi + 1, false); }));
  Reg(e, "ExtractConnectedMeshFaces", Tool("Select meshes", {PointStep("Point on the starting face")}, {Numeric("Angle", 30)}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { ExtractNear(ctx, ids, in, "ExtractConnectedMeshFaces", Rad(in.OptNum("Angle", 30)), false); }));
  Reg(e, "ExtractNonManifoldMeshEdges", MeshAction("Select meshes", "ExtractNonManifoldMeshEdges", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ExtractEdgeCurves(ctx, ids, "ExtractNonManifoldMeshEdges", 2, std::nullopt); }));
  Reg(e, "ExtractMeshEdges", MeshAction("Select meshes", "ExtractMeshEdges", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ExtractEdgeCurves(ctx, ids, "ExtractMeshEdges", 0, std::nullopt); }));
  Reg(e, "DupMeshEdge", Tool("Select meshes", {PointStep("Point near the naked edge to duplicate")}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { ExtractEdgeCurves(ctx, ids, "DupMeshEdge", 0, in.P(0)); }),
      CommandStatus::Partial, "Duplicates the whole naked-edge chain nearest the pick.");
  Reg(e, "DupMeshHoleBoundary", MeshAction("Select meshes", "DupMeshHoleBoundary", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { ExtractEdgeCurves(ctx, ids, "DupMeshHoleBoundary", 1, std::nullopt); }));
  struct Metric { const char* extract; const char* sel; const char* what; double def_hi; FaceMetric fn; };
  const Metric metrics[] = {
      {"ExtractMeshFacesByArea", "SelMeshFacesByArea", "area", 1e300, [](const RawMesh& r, const Face& f) { return r.FaceArea(f); }},
      {"ExtractMeshFacesByAspectRatio", "SelMeshFacesByAspectRatio", "aspect ratio", 1e300, [](const RawMesh& r, const Face& f) { const double s = ShortestEdge(r, f); return s > 0 ? LongestEdge(r, f) / s : 1e300; }},
      {"ExtractMeshFacesByEdgeLength", "SelMeshFacesByEdgeLength", "edge length", 1e300, [](const RawMesh& r, const Face& f) { return LongestEdge(r, f); }},
      {"ExtractMeshFacesByDraftAngle", "SelMeshFacesByDraftAngle", "draft angle in degrees", 90, nullptr},
  };
  for (const Metric& m : metrics) {
    const std::string what = m.what;
    FaceMetric fn = m.fn;
    const char* label = m.extract;
    auto action = [what, fn, label](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) {
      FaceMetric metric = fn;
      if (!metric) { const Vector3d up = ActiveNormal(ctx); metric = [up](const RawMesh& r, const Face& f) { return Deg(std::acos(std::max(-1.0, std::min(1.0, ON_DotProduct(r.FaceNormal(f), up))))); }; }
      ExtractByMetric(ctx, ids, in, label, metric);
    };
    const std::vector<Step> steps = {NumberStep(("Minimum " + what).c_str(), 0), NumberStep(("Maximum " + what).c_str(), m.def_hi)};
    Reg(e, m.extract, Tool("Select meshes", steps, {}, action));
    Reg(e, m.sel, Tool("Select meshes", steps, {}, action), CommandStatus::Partial, "Extracts (and selects) the matching faces as a new mesh instead of highlighting them.");
  }
  Reg(e, "CollapseMeshFace", MeshAction("Select meshes", "CollapseMeshFace", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { CollapseSmallest(ctx, ids, 0); }), CommandStatus::Partial, "Collapses the smallest face of each mesh; face picking is planned.");
  Reg(e, "CollapseMeshEdge", MeshAction("Select meshes", "CollapseMeshEdge", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { CollapseSmallest(ctx, ids, 1); }), CommandStatus::Partial, "Collapses the shortest edge of each mesh; edge picking is planned.");
  Reg(e, "CollapseMeshVertex", MeshAction("Select meshes", "CollapseMeshVertex", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { CollapseSmallest(ctx, ids, 1); }), CommandStatus::Partial, "Collapses the shortest edge of each mesh; vertex picking is planned.");
  Reg(e, "TriangulateNonPlanarQuads", MeshAction("Select meshes", "TriangulateNonPlanarQuads", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Target> ts = Targets(ctx, ids, "TriangulateNonPlanarQuads");
        if (ts.empty()) return;
        ctx.Doc().BeginChange("TriangulateNonPlanarQuads");
        for (const Target& t : ts) { kernel::Mesh km = t.mesh; const unsigned n = km.raw().ConvertNonPlanarQuadsToTriangles(ctx.Settings().absolute_tolerance, Rad(1), 0); km.raw().ComputeFaceNormals(); km.raw().ComputeVertexNormals(); Commit(ctx, t.id, km); ctx.Print("TriangulateNonPlanarQuads: split " + std::to_string(n) + " quad(s)"); }
      }));
  Reg(e, "TriangulateRenderMeshes", MeshAction("Select meshes", "TriangulateRenderMeshes", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<Target> ts = Targets(ctx, ids, "TriangulateRenderMeshes");
        if (ts.empty()) return;
        ctx.Doc().BeginChange("TriangulateRenderMeshes");
        for (const Target& t : ts) { kernel::Mesh km = t.mesh; km.raw().ConvertQuadsToTriangles(); Commit(ctx, t.id, km); ctx.Print("TriangulateRenderMeshes: " + std::to_string(km.FaceCount()) + " triangles"); }
      }), CommandStatus::Partial, "Triangulates the selected mesh objects (render meshes are not separate objects here).");
  Reg(e, "AddNgonsToMesh", Stub("AddNgonsToMesh"), CommandStatus::Partial, "Not yet available in this build.");
  Reg(e, "DeleteMeshNgons", MeshAction("Select meshes", "DeleteMeshNgons", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        ctx.Doc().BeginChange("DeleteMeshNgons");
        for (ObjectId id : ids) { SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::Mesh) { const unsigned n = o->mesh->raw().NgonCount(); o->mesh->raw().RemoveAllNgons(); o->InvalidateDisplay(); ctx.Print("DeleteMeshNgons: removed " + std::to_string(n) + " ngon(s)"); } }
      }));
  Reg(e, "SwapMeshEdge", Tool("Select meshes", {PointStep("Point near the edge to swap")}, {}, SwapEdgeNear), CommandStatus::Partial, "Swaps the interior edge nearest the pick.");
  Reg(e, "SplitMeshEdge", Tool("Select meshes", {PointStep("Point near the edge to split")}, {}, SplitEdgeNear), CommandStatus::Partial, "Splits the nearest edge at its midpoint.");
  Reg(e, "MatchMeshEdge", Tool("Select meshes to match", {NumberStep("Distance", 1)}, {}, MatchMeshEdge, 2), CommandStatus::Partial, "Moves naked-edge vertices of the meshes together when within the distance; run Weld afterwards to join.");
  Reg(e, "ComputeVertexColors", MeshAction("Select meshes", "ComputeVertexColors", ComputeVertexColors), CommandStatus::Partial, "Colours vertices by normal; the display does not show vertex colours yet.");
  Reg(e, "FlatShade", Stub("FlatShade"), CommandStatus::Partial, "Display toggle not yet available; Unweld a mesh for faceted shading.");
  Reg(e, "MeshIntersect", MeshAction("Select meshes to intersect", "MeshIntersect", MeshIntersect, 2));
  Reg(e, "MeshTrim", Tool("Select meshes to trim", {PointStep("Start of cutting line"), PointStep("End of cutting line"), PointStep("Point on the side to remove")}, {}, MeshTrim), CommandStatus::Partial, "Trims with a plane through two points (normal to the CPlane); open meshes lose whole faces.");
  Reg(e, "MeshSelfIntersect", MeshAction("Select meshes to check", "MeshSelfIntersect", MeshSelfIntersect));
  Reg(e, "PolylineOnMesh", Make<PolylineOnMeshCommand>(), CommandStatus::Partial, "Straight segments between points pulled to the mesh.");
  Reg(e, "MeshPatch", MeshAction("Select points, curves and meshes to patch", "MeshPatch", MeshPatch));
  Reg(e, "MeshFromLines", MeshAction("Select lines forming 3- and 4-sided loops", "MeshFromLines", MeshFromLines), CommandStatus::Partial, "Builds faces from closed 3- and 4-line loops.");
  Reg(e, "PlanarMesh", MeshAction("Select closed planar curves", "PlanarMesh", PlanarMesh));
  Reg(e, "Merge2MeshFaces", Tool("Select meshes", {PointStep("Point on the first triangle")}, {}, Merge2Faces), CommandStatus::Partial, "Merges the triangle nearest the pick with an adjacent triangle.");

  // ---- primitives ------------------------------------------------------------
  Reg(e, "MeshEllipsoid", Tool("", {PointStep("Center of ellipsoid"), NumberStep("Radius in X", 10), NumberStep("Radius in Y", 7), NumberStep("Radius in Z", 5)}, {}, MeshEllipsoid, 0));
  Reg(e, "MeshTruncatedCone", Tool("", {PointStep("Base of truncated cone"), NumberStep("Radius", 10), NumberStep("Height", 10), NumberStep("Radius at end", 5)}, {},
                                  [](CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) { TruncatedSolid(ctx, in, 32, "MeshTruncatedCone"); }, 0));
  if (const RegisteredCommand* tcone = e.Find("TCone"); tcone && tcone->factory) Reg(e, "TruncatedCone", tcone->factory);
  else Reg(e, "TruncatedCone", Tool("", {PointStep("Base of truncated cone"), NumberStep("Radius", 10), NumberStep("Height", 10), NumberStep("Radius at end", 5)}, {},
                                    [](CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) { TruncatedSolid(ctx, in, 32, "TruncatedCone"); }, 0));
  Reg(e, "TruncatedPyramid", Tool("", {PointStep("Base of truncated pyramid"), NumberStep("Radius", 10), NumberStep("Height", 10), NumberStep("Radius at end", 5)}, {Numeric("Sides", 4)},
                                  [](CommandContext& ctx, const std::vector<ObjectId>&, const ToolInput& in) { TruncatedSolid(ctx, in, 0, "TruncatedPyramid"); }, 0));
  Reg(e, "Paraboloid", Tool("", {PointStep("Vertex of paraboloid"), NumberStep("Radius at the open end", 10), NumberStep("Height", 10)}, {Toggle("Cap", true)}, Paraboloid, 0), CommandStatus::Partial, "Mesh output; a NURBS paraboloid is planned.");
  Reg(e, "Slab", Tool("Select closed planar curves", {NumberStep("Offset distance", 1), NumberStep("Height", 5)}, {}, [](CommandContext& ctx, const std::vector<ObjectId>& ids, const ToolInput& in) { Slab(ctx, ids, in, true); }));
  Reg(e, "Heightfield", Tool("", {PointStep("First corner"), PointStep("Other corner")}, {Numeric("Resolution", 24), Numeric("Amplitude", 5), Numeric("Waves", 2)}, Heightfield, 0), CommandStatus::Partial, "Grid mesh from a sine-wave function; image input is planned.");
  Reg(e, "Drape", Tool("", {PointStep("First corner"), PointStep("Other corner")}, {Numeric("Resolution", 20)}, Drape, 0), CommandStatus::Partial, "Drapes a grid mesh over the visible objects along the CPlane normal.");
}

}  // namespace dino8::app
