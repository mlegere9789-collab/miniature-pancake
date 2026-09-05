// SubD editing commands: creases, control-net editing (ExtrudeSubD,
// OffsetSubD, Inset, Bridge, InsertEdge, InsertPoint, Stitch, Slide, Fill,
// SubDSpinEdge, SubDExpandEdges), RepairSubD / DivideAlongCreases, display
// toggles, SubD primitives and conversions (SubDTruncatedCone,
// AutomaticSubDFromMesh, QuadRemesh, ShrinkWrap, MakeSubDFriendly).
//
// Every edit works on the SubD's control net - the ON_SubD control
// vertices, edges and faces of the active level. The net is unpacked into a
// plain polygon structure (Net), edited, and packed back into a fresh
// ON_SubD with ON_SubD::AddVertex/AddEdge/AddFace followed by
// UpdateAllTagsAndSectorCoefficients, so interior crease tags survive the
// round trip. Sub-objects are picked by a clicked 3D point: the nearest
// control-net vertex, edge (closest point on the segment) or face centroid.
#include "commands/cmd_common.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace dino8::app {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool ParseNumber(const std::string& t, double& v) {
  char* e = nullptr;
  v = std::strtod(t.c_str(), &e);
  return e && e != t.c_str() && *e == 0;
}

std::string Num(double v) { return FormatNumber(v); }

// ---------------------------------------------------------------------------
// The control net as an editable polygon mesh.
// ---------------------------------------------------------------------------
using Poly = std::vector<int>;
using EKey = std::pair<int, int>;
EKey Key(int a, int b) { return a < b ? EKey(a, b) : EKey(b, a); }

struct Net {
  std::vector<Point3d> v;
  std::vector<Poly> f;
  std::set<EKey> crease;  // interior edges tagged as creases

  Vector3d FaceNormal(const Poly& p) const {
    // Newell's method: robust for non-planar n-gons.
    Vector3d n(0, 0, 0);
    const size_t m = p.size();
    for (size_t i = 0; i < m; ++i) {
      const Point3d& a = v[p[i]];
      const Point3d& b = v[p[(i + 1) % m]];
      n.x += (a.y - b.y) * (a.z + b.z);
      n.y += (a.z - b.z) * (a.x + b.x);
      n.z += (a.x - b.x) * (a.y + b.y);
    }
    n.Unitize();
    return n;
  }
  Point3d Centroid(const Poly& p) const {
    Vector3d s(0, 0, 0);
    for (int i : p) s += v[i] - Point3d::Origin;
    return p.empty() ? Point3d::Origin : Point3d::Origin + s / static_cast<double>(p.size());
  }
  // Undirected edge -> faces using it.
  std::map<EKey, std::vector<int>> EdgeFaces() const {
    std::map<EKey, std::vector<int>> em;
    for (size_t fi = 0; fi < f.size(); ++fi) {
      const Poly& p = f[fi];
      for (size_t k = 0; k < p.size(); ++k) {
        const int a = p[k], b = p[(k + 1) % p.size()];
        if (a != b) em[Key(a, b)].push_back(static_cast<int>(fi));
      }
    }
    return em;
  }
  std::vector<std::vector<int>> VertexFaces() const {
    std::vector<std::vector<int>> vf(v.size());
    for (size_t fi = 0; fi < f.size(); ++fi)
      for (int i : f[fi]) if (i >= 0 && i < static_cast<int>(v.size())) vf[i].push_back(static_cast<int>(fi));
    return vf;
  }
  std::vector<Vector3d> VertexNormals() const {
    std::vector<Vector3d> n(v.size(), Vector3d(0, 0, 0));
    for (const Poly& p : f) { const Vector3d fn = FaceNormal(p); for (int i : p) n[i] += fn; }
    for (Vector3d& x : n) if (!x.Unitize()) x = Vector3d(0, 0, 1);
    return n;
  }
  int NakedEdgeCount() const {
    int n = 0;
    for (const auto& kv : EdgeFaces()) if (kv.second.size() == 1) ++n;
    return n;
  }
  // Drops faces with fewer than three distinct corners (after removing
  // repeated consecutive corners) and unused vertices. Returns the number
  // of faces removed.
  int Clean() {
    std::vector<Poly> keep;
    int removed = 0;
    for (Poly p : f) {
      Poly q;
      for (int i : p) if (i >= 0 && i < static_cast<int>(v.size()) && (q.empty() || q.back() != i)) q.push_back(i);
      while (q.size() > 1 && q.front() == q.back()) q.pop_back();
      std::set<int> distinct(q.begin(), q.end());
      if (q.size() < 3 || distinct.size() < 3) { ++removed; continue; }
      keep.push_back(q);
    }
    f = keep;
    Compact();
    return removed;
  }
  void Compact() {
    std::vector<int> remap(v.size(), -1);
    std::vector<Point3d> nv;
    for (Poly& p : f)
      for (int& i : p) { if (remap[i] < 0) { remap[i] = static_cast<int>(nv.size()); nv.push_back(v[i]); } i = remap[i]; }
    std::set<EKey> nc;
    for (const EKey& k : crease) if (remap[k.first] >= 0 && remap[k.second] >= 0) nc.insert(Key(remap[k.first], remap[k.second]));
    v = nv;
    crease = nc;
  }
  // Keeps only creases that still name an existing edge.
  void PruneCreases() {
    const auto em = EdgeFaces();
    std::set<EKey> nc;
    for (const EKey& k : crease) if (em.count(k)) nc.insert(k);
    crease = nc;
  }
  int Add(Point3d p) { v.push_back(p); return static_cast<int>(v.size()) - 1; }
};

Net Unpack(const ON_SubD& s) {
  Net n;
  std::map<unsigned int, int> idx;
  ON_SubDVertexIterator vit = s.VertexIterator();
  for (const ON_SubDVertex* v = vit.FirstVertex(); v; v = vit.NextVertex()) {
    idx[v->m_id] = static_cast<int>(n.v.size());
    n.v.push_back(v->ControlNetPoint());
  }
  ON_SubDFaceIterator fit = s.FaceIterator();
  for (const ON_SubDFace* f = fit.FirstFace(); f; f = fit.NextFace()) {
    Poly p;
    for (unsigned int i = 0; i < f->EdgeCount(); ++i) {
      const ON_SubDVertex* v = f->Vertex(i);
      if (!v) continue;
      auto it = idx.find(v->m_id);
      if (it != idx.end()) p.push_back(it->second);
    }
    if (p.size() >= 3) n.f.push_back(p);
  }
  ON_SubDEdgeIterator eit = s.EdgeIterator();
  for (const ON_SubDEdge* e = eit.FirstEdge(); e; e = eit.NextEdge()) {
    if (!e->IsCrease() || e->FaceCount() < 2 || !e->Vertex(0) || !e->Vertex(1)) continue;
    auto a = idx.find(e->Vertex(0)->m_id), b = idx.find(e->Vertex(1)->m_id);
    if (a != idx.end() && b != idx.end()) n.crease.insert(Key(a->second, b->second));
  }
  return n;
}

// Builds a fresh ON_SubD from the net. Boundary edges and vertex tags are
// left Unset and computed by UpdateAllTagsAndSectorCoefficients.
bool Pack(const Net& n, ON_SubD& out) {
  out = ON_SubD();
  if (n.f.empty()) return false;
  std::vector<ON_SubDVertex*> vs;
  vs.reserve(n.v.size());
  for (const Point3d& p : n.v) vs.push_back(out.AddVertex(ON_SubDVertexTag::Unset, &p.x));
  std::map<EKey, ON_SubDEdge*> es;
  int faces = 0;
  for (const Poly& p : n.f) {
    std::vector<ON_SubDEdge*> edges;
    for (size_t k = 0; k < p.size(); ++k) {
      const int a = p[k], b = p[(k + 1) % p.size()];
      if (a == b || a < 0 || b < 0 || a >= static_cast<int>(vs.size()) || b >= static_cast<int>(vs.size())) continue;
      const EKey key = Key(a, b);
      auto it = es.find(key);
      if (it == es.end()) {
        ON_SubDEdge* e = out.AddEdge(n.crease.count(key) ? ON_SubDEdgeTag::Crease : ON_SubDEdgeTag::Unset, vs[a], vs[b]);
        it = es.emplace(key, e).first;
      }
      if (it->second) edges.push_back(it->second);
    }
    if (edges.size() >= 3 && out.AddFace(edges.data(), static_cast<unsigned int>(edges.size()))) ++faces;
  }
  if (faces == 0) return false;
  out.UpdateAllTagsAndSectorCoefficients(true);
  return true;
}

Net NetFromMesh(const ON_Mesh& m) {
  Net n;
  for (int i = 0; i < m.VertexCount(); ++i) n.v.push_back(m.Vertex(i));
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    Poly p = {f.vi[0], f.vi[1], f.vi[2]};
    if (f.vi[3] != f.vi[2]) p.push_back(f.vi[3]);
    n.f.push_back(p);
  }
  n.Clean();
  return n;
}

kernel::Mesh MeshFromNet(const Net& n) {
  kernel::Mesh km;
  ON_Mesh& m = km.raw();
  for (size_t i = 0; i < n.v.size(); ++i) m.SetVertex(static_cast<int>(i), n.v[i]);
  int fi = 0;
  for (const Poly& p : n.f) {
    if (p.size() == 3) m.SetTriangle(fi++, p[0], p[1], p[2]);
    else if (p.size() == 4) m.SetQuad(fi++, p[0], p[1], p[2], p[3]);
    else for (size_t k = 1; k + 1 < p.size(); ++k) m.SetTriangle(fi++, p[0], p[k], p[k + 1]);  // fan
  }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  return km;
}

// ---------------------------------------------------------------------------
// Targets and picking
// ---------------------------------------------------------------------------
struct Target {
  ObjectId id = kNoObject;
  Net net;
  bool changed = false;
};

std::vector<Target> SubDTargets(CommandContext& ctx, const std::vector<ObjectId>& ids, const std::string& label) {
  std::vector<Target> out;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind != ObjectKind::SubD || !o->subd) { ctx.Warn(label + ": object " + std::to_string(id) + " is not a SubD; skipped"); continue; }
    Target t;
    t.id = id;
    t.net = Unpack(o->subd->raw());
    out.push_back(std::move(t));
  }
  if (out.empty()) ctx.Warn(label + ": select a SubD");
  return out;
}

// Writes an edited net back into its object.
bool CommitNet(CommandContext& ctx, ObjectId id, Net& n, const std::string& label) {
  SceneObject* o = ctx.Doc().Find(id);
  if (!o || o->kind != ObjectKind::SubD) return false;
  n.PruneCreases();
  ON_SubD s;
  if (!Pack(n, s)) { ctx.Warn(label + ": the result has no valid faces; object " + std::to_string(id) + " left unchanged"); return false; }
  o->subd->raw() = std::move(s);
  o->InvalidateDisplay();
  return true;
}

ObjectId AddSubDLike(CommandContext& ctx, const Net& n, const SceneObject* like) {
  ON_SubD s;
  if (!Pack(n, s)) return kNoObject;
  kernel::SubD ks;
  ks.raw() = std::move(s);
  SceneObject o = SceneObject::MakeSubD(ks);
  if (like) { o.layer_index = like->layer_index; o.color = like->color; o.color_by_layer = like->color_by_layer; o.name = like->name; }
  return ctx.Doc().Add(std::move(o));
}

double SegmentDistance(Point3d p, Point3d a, Point3d b, double* t_out = nullptr) {
  const Vector3d d = b - a;
  const double l2 = d.LengthSquared();
  double t = l2 > 0 ? ON_DotProduct(p - a, d) / l2 : 0;
  t = std::max(0.0, std::min(1.0, t));
  if (t_out) *t_out = t;
  return (a + d * t).DistanceTo(p);
}

struct VertexPick { int target = -1; int v = -1; double dist = 0; };
struct EdgePick { int target = -1; int a = -1, b = -1; double dist = 0, t = 0; };
struct FacePick { int target = -1; int face = -1; double dist = 0; };

std::optional<VertexPick> PickVertex(const std::vector<Target>& ts, Point3d p) {
  std::optional<VertexPick> best;
  for (size_t ti = 0; ti < ts.size(); ++ti)
    for (size_t i = 0; i < ts[ti].net.v.size(); ++i) {
      const double d = ts[ti].net.v[i].DistanceTo(p);
      if (!best || d < best->dist) best = VertexPick{static_cast<int>(ti), static_cast<int>(i), d};
    }
  return best;
}

std::optional<EdgePick> PickEdge(const std::vector<Target>& ts, Point3d p) {
  std::optional<EdgePick> best;
  for (size_t ti = 0; ti < ts.size(); ++ti)
    for (const auto& kv : ts[ti].net.EdgeFaces()) {
      double t = 0;
      const double d = SegmentDistance(p, ts[ti].net.v[kv.first.first], ts[ti].net.v[kv.first.second], &t);
      if (!best || d < best->dist) best = EdgePick{static_cast<int>(ti), kv.first.first, kv.first.second, d, t};
    }
  return best;
}

std::optional<FacePick> PickFace(const std::vector<Target>& ts, Point3d p) {
  std::optional<FacePick> best;
  for (size_t ti = 0; ti < ts.size(); ++ti)
    for (size_t fi = 0; fi < ts[ti].net.f.size(); ++fi) {
      const double d = ts[ti].net.Centroid(ts[ti].net.f[fi]).DistanceTo(p);
      if (!best || d < best->dist) best = FacePick{static_cast<int>(ti), static_cast<int>(fi), d};
    }
  return best;
}

// ---------------------------------------------------------------------------
// A generic "select SubDs, pick sub-objects until Enter, answer numbers"
// command. Number prompts are asked as points (a typed number, Enter for the
// default, or a pick whose distance from the last pick is used).
// ---------------------------------------------------------------------------
struct NumStep { std::string prompt; double def = 0; };

struct Input {
  std::vector<Point3d> picks;
  std::vector<double> nums;
  std::map<std::string, std::string> opts;
  double N(size_t i, double fallback = 0) const { return i < nums.size() ? nums[i] : fallback; }
  std::string Opt(const std::string& name, const std::string& def = "") const { auto it = opts.find(Lower(name)); return it == opts.end() ? def : it->second; }
  bool Yes(const std::string& name) const { return Lower(Opt(name)) == "yes"; }
  double OptNum(const std::string& name, double def) const { double v; return ParseNumber(Opt(name), v) ? v : def; }
};

using Action = std::function<void(CommandContext&, const std::vector<ObjectId>&, const Input&)>;

class SubDCommand : public Command {
 public:
  // max_picks: 0 = no pick phase, -1 = unlimited (Enter ends), n = stop after n.
  SubDCommand(std::string select_prompt, std::string pick_prompt, int max_picks, std::vector<NumStep> nums,
              std::vector<OptionSpec> opts, Action action, int min_objects = 1)
      : select_(std::move(select_prompt)), pick_(std::move(pick_prompt)), max_picks_(max_picks), nums_(std::move(nums)),
        opts_(std::move(opts)), action_(std::move(action)), min_(min_objects) {
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
    if (phase_ == Phase::Picks) { phase_ = Phase::Numbers; Next(ctx); return; }
    if (phase_ == Phase::Numbers && step_ < nums_.size()) { OnNumber(ctx, nums_[step_].def); return; }
    ctx.ClearPreview();
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    if (phase_ == Phase::Picks) {
      in_.picks.push_back(p);
      if (max_picks_ > 0 && static_cast<int>(in_.picks.size()) >= max_picks_) phase_ = Phase::Numbers;
      Next(ctx);
      return;
    }
    if (step_ >= nums_.size()) return;
    Point3d ref = in_.picks.empty() ? ActivePlane(ctx).origin : in_.picks.back();
    OnNumber(ctx, (p - ref).Length());
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (phase_ != Phase::Numbers || step_ >= nums_.size()) return;
    in_.nums.push_back(v);
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
      else if (!value.empty()) {
        std::string chosen = value;
        for (const std::string& c : o.choices) if (Lower(c) == Lower(value)) chosen = c;
        o.value = chosen;
      }
      in_.opts[Lower(o.name)] = o.value;
    }
    opts_ = options;
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (phase_ != Phase::Numbers || in_.picks.empty()) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(in_.picks.back(), h);
  }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  enum class Phase { Picks, Numbers };
  void Next(CommandContext& ctx) {
    if (phase_ == Phase::Picks && max_picks_ == 0) phase_ = Phase::Numbers;
    if (phase_ == Phase::Picks) {
      WantPoint(pick_ + (max_picks_ < 0 || static_cast<int>(in_.picks.size()) > 0 ? " (Enter when done)" : ""));
      options = opts_;
      return;
    }
    if (step_ >= nums_.size()) {
      ctx.ClearPreview();
      action_(ctx, ids_, in_);
      Finish();
      return;
    }
    WantPoint(nums_[step_].prompt + " <" + FormatNumber(nums_[step_].def) + ">");
    options = opts_;
  }
  std::string select_, pick_;
  int max_picks_;
  std::vector<NumStep> nums_;
  std::vector<OptionSpec> opts_;
  Action action_;
  int min_;
  std::vector<ObjectId> ids_;
  Input in_;
  Phase phase_ = Phase::Picks;
  size_t step_ = 0;
};

OptionSpec Toggle(const char* name, bool on) { return {name, on ? "Yes" : "No", {"Yes", "No"}, false, true}; }
OptionSpec Numeric(const char* name, double v) { return {name, FormatNumber(v), {}, true, false}; }
OptionSpec Choice(const char* name, std::vector<std::string> choices) { return {name, choices.front(), choices, false, false}; }

CommandFactory SubDTool(std::string select_prompt, std::string pick_prompt, int max_picks, std::vector<NumStep> nums,
                        std::vector<OptionSpec> opts, Action action, int min_objects = 1) {
  return [=]() -> std::unique_ptr<Command> {
    return std::make_unique<SubDCommand>(select_prompt, pick_prompt, max_picks, nums, opts, action, min_objects);
  };
}

const char* kSelectSubD = "Select SubD";

// ---------------------------------------------------------------------------
// Net editing primitives
// ---------------------------------------------------------------------------

// Inserts a new vertex at parameter t along edge (a,b) into every face that
// uses the edge. Returns the new vertex index (or -1 if no face uses it).
int SplitEdge(Net& n, int a, int b, double t) {
  const Point3d p = n.v[a] + (n.v[b] - n.v[a]) * t;
  int nv = -1;
  for (Poly& poly : n.f) {
    for (size_t k = 0; k < poly.size(); ++k) {
      const int x = poly[k], y = poly[(k + 1) % poly.size()];
      if ((x == a && y == b) || (x == b && y == a)) {
        if (nv < 0) nv = n.Add(p);
        poly.insert(poly.begin() + static_cast<long>(k) + 1, nv);
        break;
      }
    }
  }
  if (nv >= 0 && n.crease.count(Key(a, b))) { n.crease.erase(Key(a, b)); n.crease.insert(Key(a, nv)); n.crease.insert(Key(nv, b)); }
  return nv;
}

// Splits face fi along the chord between two of its (non-adjacent) corners.
bool SplitFace(Net& n, int fi, int va, int vb) {
  Poly& p = n.f[fi];
  const size_t m = p.size();
  size_t i = m, j = m;
  for (size_t k = 0; k < m; ++k) { if (p[k] == va) i = k; if (p[k] == vb) j = k; }
  if (i == m || j == m || i == j || (i + 1) % m == j || (j + 1) % m == i) return false;
  Poly p1, p2;
  for (size_t k = i;; k = (k + 1) % m) { p1.push_back(p[k]); if (k == j) break; }
  for (size_t k = j;; k = (k + 1) % m) { p2.push_back(p[k]); if (k == i) break; }
  p = p1;
  n.f.push_back(p2);
  return true;
}

// Position of undirected edge {a,b} in polygon p (index of its first corner), or -1.
int EdgePos(const Poly& p, int a, int b) {
  for (size_t k = 0; k < p.size(); ++k) {
    const int x = p[k], y = p[(k + 1) % p.size()];
    if ((x == a && y == b) || (x == b && y == a)) return static_cast<int>(k);
  }
  return -1;
}

int OtherFace(const std::map<EKey, std::vector<int>>& em, int a, int b, int not_face) {
  auto it = em.find(Key(a, b));
  if (it == em.end()) return -1;
  for (int f : it->second) if (f != not_face) return f;
  return -1;
}

// Extrudes the faces in S (all faces when empty) by `d` along their
// normals, building side quads along the boundary of the selection.
// Returns the number of side faces added.
int ExtrudeFaces(Net& n, std::set<int> S, double d) {
  if (S.empty()) for (size_t i = 0; i < n.f.size(); ++i) S.insert(static_cast<int>(i));
  std::vector<Vector3d> fn(n.f.size());
  for (size_t i = 0; i < n.f.size(); ++i) fn[i] = n.FaceNormal(n.f[i]);
  // Boundary edges of the selection: used by exactly one selected face.
  std::map<EKey, int> use;
  for (int fi : S) { const Poly& p = n.f[fi]; for (size_t k = 0; k < p.size(); ++k) use[Key(p[k], p[(k + 1) % p.size()])]++; }
  std::set<int> boundary_vertices;
  for (const auto& kv : use) if (kv.second == 1) { boundary_vertices.insert(kv.first.first); boundary_vertices.insert(kv.first.second); }
  std::map<int, Vector3d> vn;
  for (int fi : S) for (int vi : n.f[fi]) vn[vi] += fn[fi];
  std::map<int, int> dup;
  for (auto& kv : vn) {
    Vector3d dir = kv.second;
    if (!dir.Unitize()) continue;
    const Point3d np = n.v[kv.first] + dir * d;
    if (boundary_vertices.count(kv.first)) dup[kv.first] = n.Add(np);
    else n.v[kv.first] = np;
  }
  const std::vector<Poly> original = n.f;
  for (int fi : S) for (int& vi : n.f[fi]) { auto it = dup.find(vi); if (it != dup.end()) vi = it->second; }
  int sides = 0;
  for (int fi : S) {
    const Poly& p = original[fi];
    for (size_t k = 0; k < p.size(); ++k) {
      const int a = p[k], b = p[(k + 1) % p.size()];
      if (use[Key(a, b)] != 1 || !dup.count(a) || !dup.count(b)) continue;
      n.f.push_back({a, b, dup[b], dup[a]});
      ++sides;
    }
  }
  // Creases follow the moved copies.
  std::set<EKey> nc = n.crease;
  for (const EKey& k : n.crease) if (dup.count(k.first) && dup.count(k.second)) nc.insert(Key(dup[k.first], dup[k.second]));
  n.crease = nc;
  return sides;
}

// Insets each face in S by distance d: a new inner face plus a ring of quads.
int InsetFaces(Net& n, const std::set<int>& S, double d) {
  int done = 0;
  for (int fi : S) {
    const Poly p = n.f[fi];
    const Point3d c = n.Centroid(p);
    Poly inner;
    for (int vi : p) {
      Vector3d to = c - n.v[vi];
      const double len = to.Length();
      if (len <= ON_ZERO_TOLERANCE) { inner.push_back(vi); continue; }
      to /= len;
      inner.push_back(n.Add(n.v[vi] + to * std::min(d, 0.9 * len)));
    }
    n.f[fi] = inner;
    for (size_t k = 0; k < p.size(); ++k) {
      const size_t k1 = (k + 1) % p.size();
      n.f.push_back({p[k], p[k1], inner[k1], inner[k]});
    }
    ++done;
  }
  return done;
}

// Removes faces f1 and f2 and joins their vertex rings with a tube of quads.
bool BridgeFaces(Net& n, int f1, int f2, std::string& why) {
  const Poly a = n.f[f1], b = n.f[f2];
  if (a.size() != b.size()) { why = "the faces have " + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " corners"; return false; }
  const int m = static_cast<int>(a.size());
  // Pairing a[i] <-> b[(s - i) mod m]: choose the offset with the shortest links.
  int best_s = 0;
  double best = std::numeric_limits<double>::max();
  for (int s = 0; s < m; ++s) {
    double tot = 0;
    for (int i = 0; i < m; ++i) tot += n.v[a[i]].DistanceTo(n.v[b[((s - i) % m + m) % m]]);
    if (tot < best) { best = tot; best_s = s; }
  }
  std::vector<Poly> quads;
  for (int i = 0; i < m; ++i) {
    const int i1 = (i + 1) % m;
    const int j = ((best_s - i - 1) % m + m) % m, j1 = ((best_s - i) % m + m) % m;
    quads.push_back({a[i], a[i1], b[j], b[j1]});
  }
  std::vector<Poly> keep;
  for (size_t i = 0; i < n.f.size(); ++i) if (static_cast<int>(i) != f1 && static_cast<int>(i) != f2) keep.push_back(n.f[i]);
  n.f = keep;
  n.f.insert(n.f.end(), quads.begin(), quads.end());
  return true;
}

// Offsets every control vertex along its normal. With `solid`, keeps the
// original faces, adds the offset copy and walls along naked edges.
void OffsetNet(Net& n, double d, bool solid) {
  const std::vector<Vector3d> vn = n.VertexNormals();
  if (!solid) { for (size_t i = 0; i < n.v.size(); ++i) n.v[i] += vn[i] * d; return; }
  const int base = static_cast<int>(n.v.size());
  for (int i = 0; i < base; ++i) n.v.push_back(n.v[i] + vn[i] * d);
  const std::vector<Poly> original = n.f;
  const auto em = n.EdgeFaces();
  // The outer shell keeps its orientation, the inner one is flipped.
  for (Poly& p : n.f) if (d >= 0) std::reverse(p.begin(), p.end());
  for (const Poly& p : original) {
    Poly q;
    for (int i : p) q.push_back(i + base);
    if (d < 0) std::reverse(q.begin(), q.end());
    n.f.push_back(q);
  }
  for (const Poly& p : original)
    for (size_t k = 0; k < p.size(); ++k) {
      const int a = p[k], b = p[(k + 1) % p.size()];
      if (em.at(Key(a, b)).size() != 1) continue;
      if (d >= 0) n.f.push_back({a, b, b + base, a + base});
      else n.f.push_back({b, a, a + base, b + base});
    }
  std::set<EKey> nc = n.crease;
  for (const EKey& k : n.crease) nc.insert(Key(k.first + base, k.second + base));
  n.crease = nc;
}

// Naked-edge loops, each as an ordered vertex ring oriented so a face built
// on it is consistent with its neighbours.
std::vector<Poly> NakedLoops(const Net& n) {
  std::map<int, int> next;  // b -> a for each naked edge traversed a->b
  const auto em = n.EdgeFaces();
  for (const Poly& p : n.f)
    for (size_t k = 0; k < p.size(); ++k) {
      const int a = p[k], b = p[(k + 1) % p.size()];
      if (em.at(Key(a, b)).size() == 1) next[b] = a;
    }
  std::vector<Poly> loops;
  std::set<int> used;
  for (const auto& kv : next) {
    if (used.count(kv.first)) continue;
    Poly loop;
    int cur = kv.first;
    while (!used.count(cur) && next.count(cur)) { used.insert(cur); loop.push_back(cur); cur = next[cur]; }
    if (loop.size() >= 3 && cur == loop.front()) loops.push_back(loop);
  }
  return loops;
}

// Walks a quad ring from edge (left,right) through face fi, collecting
// (face, left/right of entering edge, left/right of opposite edge).
struct RingStep { int face; int l0, r0, l1, r1; };
std::vector<RingStep> WalkRing(const Net& n, const std::map<EKey, std::vector<int>>& em, int fi, int left, int right, std::set<int>& visited) {
  std::vector<RingStep> steps;
  while (fi >= 0 && !visited.count(fi)) {
    const Poly& p = n.f[fi];
    if (p.size() != 4) break;
    const int k = EdgePos(p, left, right);
    if (k < 0) break;
    visited.insert(fi);
    const int c2 = p[(k + 2) % 4], c3 = p[(k + 3) % 4];
    int l1, r1;
    if (p[k] == left) { l1 = c3; r1 = c2; } else { l1 = c2; r1 = c3; }
    steps.push_back({fi, left, right, l1, r1});
    left = l1; right = r1;
    fi = OtherFace(em, left, right, fi);
  }
  return steps;
}

// Inserts an edge loop across the quad ring through edge (a,b) at parameter t.
int InsertEdgeLoop(Net& n, int a, int b, double t) {
  const auto em = n.EdgeFaces();
  std::set<int> visited;
  std::vector<RingStep> steps;
  auto it = em.find(Key(a, b));
  if (it == em.end()) return 0;
  for (int fi : it->second) {
    std::vector<RingStep> s = WalkRing(n, em, fi, a, b, visited);
    steps.insert(steps.end(), s.begin(), s.end());
  }
  if (steps.empty()) return 0;
  std::map<EKey, int> mid;
  auto split = [&](int l, int r) {
    const EKey k = Key(l, r);
    if (!mid.count(k)) mid[k] = SplitEdge(n, l, r, t);
    return mid[k];
  };
  int added = 0;
  for (const RingStep& s : steps) {
    const int m0 = split(s.l0, s.r0), m1 = split(s.l1, s.r1);
    if (m0 >= 0 && m1 >= 0 && SplitFace(n, s.face, m0, m1)) ++added;
  }
  return added;
}

// Spins the edge shared by two faces one step around the merged polygon.
bool SpinEdge(Net& n, int a, int b, std::string& why) {
  const auto em = n.EdgeFaces();
  auto it = em.find(Key(a, b));
  if (it == em.end() || it->second.size() != 2) { why = "the edge is not shared by exactly two faces"; return false; }
  const int f1 = it->second[0], f2 = it->second[1];
  const Poly p1 = n.f[f1], p2 = n.f[f2];
  const int k1 = EdgePos(p1, a, b), k2 = EdgePos(p2, a, b);
  if (k1 < 0 || k2 < 0) { why = "edge not found"; return false; }
  // Merged polygon: p1 from after the edge round to its start, then p2 likewise.
  Poly merged;
  const int m1 = static_cast<int>(p1.size()), m2 = static_cast<int>(p2.size());
  for (int i = 1; i < m1; ++i) merged.push_back(p1[(k1 + i) % m1]);  // p1[k1+1] ... p1[k1] excluded end
  for (int i = 1; i < m2; ++i) merged.push_back(p2[(k2 + i) % m2]);
  // The old chord joins merged[m1-2] (== p1[k1]) and merged[m1-1] (== p2[k2+1]),
  // spin it by one corner.
  const int m = static_cast<int>(merged.size());
  const int i0 = (m1 - 2 + 1) % m, i1 = (m1 - 1 + 1) % m;
  // Ensure the two are non-adjacent.
  if ((i0 + 1) % m == i1 || (i1 + 1) % m == i0) { why = "faces too small to spin"; return false; }
  Poly q1, q2;
  for (int k = i0;; k = (k + 1) % m) { q1.push_back(merged[k]); if (k == i1) break; }
  for (int k = i1;; k = (k + 1) % m) { q2.push_back(merged[k]); if (k == i0) break; }
  n.f[f1] = q1;
  n.f[f2] = q2;
  n.crease.erase(Key(a, b));
  return true;
}

// Merges vertex `from` into `to` (moving `to` to the midpoint).
void MergeVertices(Net& n, int to, int from) {
  if (to == from) return;
  n.v[to] = n.v[to] + (n.v[from] - n.v[to]) * 0.5;
  for (Poly& p : n.f) for (int& i : p) if (i == from) i = to;
  std::set<EKey> nc;
  for (const EKey& k : n.crease) { const int x = k.first == from ? to : k.first, y = k.second == from ? to : k.second; if (x != y) nc.insert(Key(x, y)); }
  n.crease = nc;
}

// Welds coincident vertices; returns how many were merged.
int WeldVertices(Net& n, double tol) {
  std::vector<int> remap(n.v.size());
  std::iota(remap.begin(), remap.end(), 0);
  int merged = 0;
  for (size_t i = 0; i < n.v.size(); ++i) {
    if (remap[i] != static_cast<int>(i)) continue;
    for (size_t j = i + 1; j < n.v.size(); ++j)
      if (remap[j] == static_cast<int>(j) && n.v[i].DistanceTo(n.v[j]) <= tol) { remap[j] = static_cast<int>(i); ++merged; }
  }
  if (!merged) return 0;
  for (Poly& p : n.f) for (int& i : p) i = remap[i];
  std::set<EKey> nc;
  for (const EKey& k : n.crease) if (remap[k.first] != remap[k.second]) nc.insert(Key(remap[k.first], remap[k.second]));
  n.crease = nc;
  return merged;
}

int RemoveDuplicateFaces(Net& n) {
  std::set<std::vector<int>> seen;
  std::vector<Poly> keep;
  int removed = 0;
  for (const Poly& p : n.f) {
    std::vector<int> key = p;
    std::sort(key.begin(), key.end());
    if (!seen.insert(key).second) { ++removed; continue; }
    keep.push_back(p);
  }
  n.f = keep;
  return removed;
}

// Splits the net into face components separated by crease and naked edges.
std::vector<Net> DivideAtCreases(const Net& n) {
  std::vector<int> parent(n.f.size());
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
  for (const auto& kv : n.EdgeFaces()) {
    if (kv.second.size() < 2 || n.crease.count(kv.first)) continue;
    for (size_t i = 1; i < kv.second.size(); ++i) parent[find(kv.second[i])] = find(kv.second[0]);
  }
  std::map<int, Net> parts;
  for (size_t fi = 0; fi < n.f.size(); ++fi) {
    Net& part = parts[find(static_cast<int>(fi))];
    if (part.v.empty()) part.v = n.v;
    part.f.push_back(n.f[fi]);
  }
  std::vector<Net> out;
  for (auto& kv : parts) { kv.second.Compact(); out.push_back(std::move(kv.second)); }
  return out;
}

// ---------------------------------------------------------------------------
// 3D convex hull (incremental, with horizon rebuilding).
// ---------------------------------------------------------------------------
struct Hull {
  std::vector<Point3d> pts;
  std::vector<std::array<int, 3>> tris;
};

std::optional<Hull> ConvexHull(std::vector<Point3d> pts) {
  // Drop duplicates.
  std::sort(pts.begin(), pts.end(), [](const Point3d& a, const Point3d& b) { return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z < b.z); });
  pts.erase(std::unique(pts.begin(), pts.end(), [](const Point3d& a, const Point3d& b) { return a.DistanceTo(b) < 1e-9; }), pts.end());
  const size_t N = pts.size();
  if (N < 4) return std::nullopt;
  double scale = 0;
  for (const Point3d& p : pts) scale = std::max(scale, std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
  const double eps = std::max(1e-9, scale * 1e-9);
  // Initial tetrahedron.
  size_t i0 = 0, i1 = N - 1;  // extremes in x after sorting
  size_t i2 = N;
  double best = 0;
  const Vector3d d01 = pts[i1] - pts[i0];
  for (size_t i = 0; i < N; ++i) { const double d = ON_CrossProduct(d01, pts[i] - pts[i0]).Length(); if (d > best) { best = d; i2 = i; } }
  if (i2 == N || best <= eps) return std::nullopt;
  Vector3d nrm = ON_CrossProduct(d01, pts[i2] - pts[i0]);
  nrm.Unitize();
  size_t i3 = N;
  best = 0;
  for (size_t i = 0; i < N; ++i) { const double d = std::fabs(ON_DotProduct(nrm, pts[i] - pts[i0])); if (d > best) { best = d; i3 = i; } }
  if (i3 == N || best <= eps) return std::nullopt;
  auto plane_normal = [&](const std::array<int, 3>& t) { Vector3d nn = ON_CrossProduct(pts[t[1]] - pts[t[0]], pts[t[2]] - pts[t[0]]); nn.Unitize(); return nn; };
  std::vector<std::array<int, 3>> tris;
  {
    const int a = static_cast<int>(i0), b = static_cast<int>(i1), c = static_cast<int>(i2), d = static_cast<int>(i3);
    tris = {{a, b, c}, {a, c, d}, {a, d, b}, {b, d, c}};
    const Point3d centre = (pts[a] + pts[b] + pts[c] + pts[d]) / 4.0;
    for (auto& t : tris) if (ON_DotProduct(plane_normal(t), centre - pts[t[0]]) > 0) std::swap(t[1], t[2]);
  }
  for (size_t i = 0; i < N; ++i) {
    if (i == i0 || i == i1 || i == i2 || i == i3) continue;
    const Point3d& p = pts[i];
    std::vector<char> visible(tris.size(), 0);
    bool any = false;
    for (size_t t = 0; t < tris.size(); ++t) {
      if (ON_DotProduct(plane_normal(tris[t]), p - pts[tris[t][0]]) > eps) { visible[t] = 1; any = true; }
    }
    if (!any) continue;
    std::set<std::pair<int, int>> directed;
    for (size_t t = 0; t < tris.size(); ++t) if (visible[t]) for (int k = 0; k < 3; ++k) directed.insert({tris[t][k], tris[t][(k + 1) % 3]});
    std::vector<std::pair<int, int>> horizon;
    for (const auto& e : directed) if (!directed.count({e.second, e.first})) horizon.push_back(e);
    std::vector<std::array<int, 3>> keep;
    for (size_t t = 0; t < tris.size(); ++t) if (!visible[t]) keep.push_back(tris[t]);
    for (const auto& e : horizon) keep.push_back({e.first, e.second, static_cast<int>(i)});
    tris = keep;
  }
  Hull h;
  h.pts = pts;
  h.tris = tris;
  return h;
}

// ---------------------------------------------------------------------------
// Command actions
// ---------------------------------------------------------------------------

void CreaseAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in, bool add) {
  const std::string label = add ? "Crease" : "RemoveCrease";
  std::vector<Target> ts = SubDTargets(ctx, ids, label);
  if (ts.empty()) return;
  if (in.picks.empty()) { ctx.Warn(label + ": no edges picked"); return; }
  int count = 0;
  for (Point3d p : in.picks) {
    std::optional<EdgePick> e = PickEdge(ts, p);
    if (!e) continue;
    Net& n = ts[e->target].net;
    const EKey k = Key(e->a, e->b);
    if (add ? n.crease.insert(k).second : n.crease.erase(k) > 0) { ++count; ts[e->target].changed = true; }
  }
  ctx.Doc().BeginChange(label);
  int objects = 0;
  for (Target& t : ts) if (t.changed && CommitNet(ctx, t.id, t.net, label)) ++objects;
  ctx.Print(label + ": " + std::to_string(count) + " edge(s) " + (add ? "creased" : "smoothed") + " on " + std::to_string(objects) + " SubD(s)");
}

void ExtrudeSubDAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "ExtrudeSubD");
  if (ts.empty()) return;
  const double d = in.N(0, 1);
  std::map<int, std::set<int>> faces;
  for (Point3d p : in.picks) { std::optional<FacePick> f = PickFace(ts, p); if (f) faces[f->target].insert(f->face); }
  ctx.Doc().BeginChange("ExtrudeSubD");
  int objects = 0, sides = 0, count = 0;
  for (size_t i = 0; i < ts.size(); ++i) {
    if (!in.picks.empty() && !faces.count(static_cast<int>(i))) continue;
    std::set<int> S = faces[static_cast<int>(i)];
    count += S.empty() ? static_cast<int>(ts[i].net.f.size()) : static_cast<int>(S.size());
    sides += ExtrudeFaces(ts[i].net, S, d);
    if (CommitNet(ctx, ts[i].id, ts[i].net, "ExtrudeSubD")) ++objects;
  }
  ctx.Print("ExtrudeSubD: extruded " + std::to_string(count) + " face(s) by " + Num(d) + " on " + std::to_string(objects) + " SubD(s), " + std::to_string(sides) + " side face(s) added");
}

void InsetAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "Inset");
  if (ts.empty()) return;
  const double d = in.N(0, 1);
  std::map<int, std::set<int>> faces;
  for (Point3d p : in.picks) { std::optional<FacePick> f = PickFace(ts, p); if (f) faces[f->target].insert(f->face); }
  if (faces.empty()) { ctx.Warn("Inset: no faces picked"); return; }
  ctx.Doc().BeginChange("Inset");
  int count = 0;
  for (auto& kv : faces) {
    count += InsetFaces(ts[kv.first].net, kv.second, d);
    CommitNet(ctx, ts[kv.first].id, ts[kv.first].net, "Inset");
  }
  ctx.Print("Inset: " + std::to_string(count) + " face(s) inset by " + Num(d));
}

void BridgeAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "Bridge");
  if (ts.empty()) return;
  if (in.picks.size() < 2) { ctx.Warn("Bridge: pick two faces"); return; }
  std::optional<FacePick> f1 = PickFace(ts, in.picks[0]);
  std::optional<FacePick> f2 = PickFace(ts, in.picks[1]);
  if (!f1 || !f2) { ctx.Warn("Bridge: no faces found"); return; }
  if (f1->target == f2->target && f1->face == f2->face) { ctx.Warn("Bridge: pick two different faces"); return; }
  ctx.Doc().BeginChange("Bridge");
  Target& t1 = ts[f1->target];
  int face2 = f2->face;
  ObjectId removed = kNoObject;
  if (f2->target != f1->target) {
    // Merge the second SubD's net into the first.
    Target& t2 = ts[f2->target];
    const int base = static_cast<int>(t1.net.v.size());
    t1.net.v.insert(t1.net.v.end(), t2.net.v.begin(), t2.net.v.end());
    face2 = static_cast<int>(t1.net.f.size()) + f2->face;
    for (const Poly& p : t2.net.f) { Poly q; for (int i : p) q.push_back(i + base); t1.net.f.push_back(q); }
    for (const EKey& k : t2.net.crease) t1.net.crease.insert(Key(k.first + base, k.second + base));
    removed = t2.id;
  }
  std::string why;
  const int quads = static_cast<int>(t1.net.f[f1->face].size());
  if (!BridgeFaces(t1.net, f1->face, face2, why)) { ctx.Warn("Bridge: " + why); return; }
  if (CommitNet(ctx, t1.id, t1.net, "Bridge")) {
    if (removed != kNoObject) ctx.Doc().Remove(removed);
    ctx.Print("Bridge: joined 2 faces with a tube of " + std::to_string(quads) + " quads" + (removed != kNoObject ? ", 2 SubDs merged" : ""));
  }
}

void OffsetSubDAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "OffsetSubD");
  if (ts.empty()) return;
  const double d = in.N(0, 1);
  const bool solid = in.Yes("Solid"), delete_input = in.Yes("DeleteInput");
  ctx.Doc().BeginChange("OffsetSubD");
  int done = 0;
  for (Target& t : ts) {
    OffsetNet(t.net, d, solid);
    const SceneObject* like = ctx.Doc().Find(t.id);
    if (delete_input || solid) { if (CommitNet(ctx, t.id, t.net, "OffsetSubD")) ++done; }
    else if (AddSubDLike(ctx, t.net, like) != kNoObject) ++done;
  }
  ctx.Print("OffsetSubD: " + std::string(solid ? "solid shell, " : "") + "distance " + Num(d) + ", " + std::to_string(done) + " SubD(s)");
}

void InsertEdgeAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "InsertEdge");
  if (ts.empty()) return;
  if (in.picks.empty()) { ctx.Warn("InsertEdge: pick an edge"); return; }
  const double t = std::max(0.05, std::min(0.95, in.OptNum("Position", 0.5)));
  ctx.Doc().BeginChange("InsertEdge");
  int added = 0;
  for (Point3d p : in.picks) {
    std::optional<EdgePick> e = PickEdge(ts, p);
    if (!e) continue;
    const int n = InsertEdgeLoop(ts[e->target].net, e->a, e->b, t);
    if (n) { added += n; ts[e->target].changed = true; }
  }
  for (Target& t2 : ts) if (t2.changed) CommitNet(ctx, t2.id, t2.net, "InsertEdge");
  ctx.Print("InsertEdge: " + std::to_string(added) + " edge(s) inserted at " + Num(t) + " along the ring");
}

void InsertPointAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "InsertPoint");
  if (ts.empty()) return;
  if (in.picks.empty()) { ctx.Warn("InsertPoint: pick a point on an edge"); return; }
  ctx.Doc().BeginChange("InsertPoint");
  int added = 0;
  for (Point3d p : in.picks) {
    std::optional<EdgePick> e = PickEdge(ts, p);
    if (!e) continue;
    const double t = std::max(0.02, std::min(0.98, e->t));
    if (SplitEdge(ts[e->target].net, e->a, e->b, t) >= 0) { ++added; ts[e->target].changed = true; }
  }
  for (Target& t2 : ts) if (t2.changed) CommitNet(ctx, t2.id, t2.net, "InsertPoint");
  ctx.Print("InsertPoint: " + std::to_string(added) + " point(s) inserted on edges");
}

void ExpandEdgesAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "SubDExpandEdges");
  if (ts.empty()) return;
  if (in.picks.empty()) { ctx.Warn("SubDExpandEdges: pick an edge"); return; }
  const double d = in.N(0, 1);
  ctx.Doc().BeginChange("SubDExpandEdges");
  int faces = 0;
  for (Point3d p : in.picks) {
    std::optional<EdgePick> e = PickEdge(ts, p);
    if (!e) continue;
    Net& n = ts[e->target].net;
    const auto em = n.EdgeFaces();
    auto it = em.find(Key(e->a, e->b));
    if (it == em.end()) continue;
    for (int fi : std::vector<int>(it->second)) {
      const Poly p0 = n.f[fi];
      const int k = EdgePos(p0, e->a, e->b);
      if (k < 0 || p0.size() < 4) continue;
      const int m = static_cast<int>(p0.size());
      const int x = p0[k], y = p0[(k + 1) % m], yn = p0[(k + 2) % m], xp = p0[(k + m - 1) % m];
      const double ly = n.v[y].DistanceTo(n.v[yn]), lx = n.v[x].DistanceTo(n.v[xp]);
      const int my = SplitEdge(n, y, yn, std::min(0.45, ly > 0 ? d / ly : 0.45));
      const int mx = SplitEdge(n, x, xp, std::min(0.45, lx > 0 ? d / lx : 0.45));
      if (my >= 0 && mx >= 0 && SplitFace(n, fi, my, mx)) ++faces;
    }
    ts[e->target].changed = true;
  }
  for (Target& t2 : ts) if (t2.changed) CommitNet(ctx, t2.id, t2.net, "SubDExpandEdges");
  ctx.Print("SubDExpandEdges: " + std::to_string(faces) + " strip face(s) added, width " + Num(d));
}

void SpinEdgeAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "SubDSpinEdge");
  if (ts.empty()) return;
  if (in.picks.empty()) { ctx.Warn("SubDSpinEdge: pick an edge"); return; }
  ctx.Doc().BeginChange("SubDSpinEdge");
  int spun = 0;
  for (Point3d p : in.picks) {
    std::optional<EdgePick> e = PickEdge(ts, p);
    if (!e) continue;
    std::string why;
    if (SpinEdge(ts[e->target].net, e->a, e->b, why)) { ++spun; ts[e->target].changed = true; }
    else ctx.Warn("SubDSpinEdge: " + why);
  }
  for (Target& t2 : ts) if (t2.changed) CommitNet(ctx, t2.id, t2.net, "SubDSpinEdge");
  ctx.Print("SubDSpinEdge: " + std::to_string(spun) + " edge(s) spun");
}

void StitchAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "Stitch");
  if (ts.empty()) return;
  if (in.picks.size() < 2) { ctx.Warn("Stitch: pick two edges (or two vertices with Mode=Vertices)"); return; }
  const bool vertices = Lower(in.Opt("Mode", "Edges")) == "vertices";
  ctx.Doc().BeginChange("Stitch");
  if (vertices) {
    std::optional<VertexPick> a = PickVertex(ts, in.picks[0]), b = PickVertex(ts, in.picks[1]);
    if (!a || !b || a->target != b->target) { ctx.Warn("Stitch: pick two vertices of the same SubD"); return; }
    if (a->v == b->v) { ctx.Warn("Stitch: pick two different vertices"); return; }
    Net& n = ts[a->target].net;
    MergeVertices(n, a->v, b->v);
    const int dropped = n.Clean();
    if (CommitNet(ctx, ts[a->target].id, n, "Stitch")) ctx.Print("Stitch: 2 vertices merged, " + std::to_string(dropped) + " degenerate face(s) removed");
    return;
  }
  std::optional<EdgePick> e1 = PickEdge(ts, in.picks[0]), e2 = PickEdge(ts, in.picks[1]);
  if (!e1 || !e2 || e1->target != e2->target) { ctx.Warn("Stitch: pick two edges of the same SubD"); return; }
  if (Key(e1->a, e1->b) == Key(e2->a, e2->b)) { ctx.Warn("Stitch: pick two different edges"); return; }
  Net& n = ts[e1->target].net;
  const double straight = n.v[e1->a].DistanceTo(n.v[e2->a]) + n.v[e1->b].DistanceTo(n.v[e2->b]);
  const double crossed = n.v[e1->a].DistanceTo(n.v[e2->b]) + n.v[e1->b].DistanceTo(n.v[e2->a]);
  const int c = straight <= crossed ? e2->a : e2->b, d = straight <= crossed ? e2->b : e2->a;
  MergeVertices(n, e1->a, c);
  MergeVertices(n, e1->b, d);
  const int dropped = n.Clean();
  if (CommitNet(ctx, ts[e1->target].id, n, "Stitch")) ctx.Print("Stitch: 2 edges merged, " + std::to_string(dropped) + " degenerate face(s) removed");
}

void SlideAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "Slide");
  if (ts.empty()) return;
  if (in.picks.size() < 2) { ctx.Warn("Slide: pick a vertex and a point to slide towards"); return; }
  std::optional<VertexPick> vp = PickVertex(ts, in.picks[0]);
  if (!vp) return;
  Net& n = ts[vp->target].net;
  const int vi = vp->v;
  Vector3d want = in.picks[1] - n.v[vi];
  const double dist = want.Length();
  if (!want.Unitize()) { ctx.Warn("Slide: zero distance"); return; }
  // Pick the adjacent edge best aligned with the requested direction.
  int best = -1;
  double best_dot = -2;
  Vector3d best_dir;
  double best_len = 0;
  for (const auto& kv : n.EdgeFaces()) {
    if (kv.first.first != vi && kv.first.second != vi) continue;
    const int other = kv.first.first == vi ? kv.first.second : kv.first.first;
    Vector3d dir = n.v[other] - n.v[vi];
    const double len = dir.Length();
    if (!dir.Unitize()) continue;
    const double dot = ON_DotProduct(dir, want);
    if (dot > best_dot) { best_dot = dot; best = other; best_dir = dir; best_len = len; }
  }
  if (best < 0) { ctx.Warn("Slide: the vertex has no edges"); return; }
  const double move = std::min(dist * std::max(0.0, best_dot), 0.95 * best_len);
  ctx.Doc().BeginChange("Slide");
  n.v[vi] += best_dir * move;
  if (CommitNet(ctx, ts[vp->target].id, n, "Slide")) ctx.Print("Slide: vertex moved " + Num(move) + " along its edge towards vertex " + std::to_string(best));
}

void FillAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "Fill");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("Fill");
  int filled = 0;
  for (Target& t : ts) {
    std::vector<Poly> loops = NakedLoops(t.net);
    if (loops.empty()) continue;
    std::vector<Poly> chosen;
    if (in.picks.empty()) chosen = loops;
    else {
      for (Point3d p : in.picks) {
        double bd = std::numeric_limits<double>::max();
        const Poly* bl = nullptr;
        for (const Poly& l : loops) {
          for (size_t k = 0; k < l.size(); ++k) {
            const double d = SegmentDistance(p, t.net.v[l[k]], t.net.v[l[(k + 1) % l.size()]]);
            if (d < bd) { bd = d; bl = &l; }
          }
        }
        if (bl && std::find(chosen.begin(), chosen.end(), *bl) == chosen.end()) chosen.push_back(*bl);
      }
    }
    for (const Poly& l : chosen) { t.net.f.push_back(l); ++filled; }
    if (!chosen.empty()) CommitNet(ctx, t.id, t.net, "Fill");
  }
  ctx.Print("Fill: " + std::to_string(filled) + " hole(s) filled with a face");
}

void RepairAction(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "RepairSubD");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("RepairSubD");
  for (Target& t : ts) {
    Net& n = t.net;
    const int before_v = static_cast<int>(n.v.size()), before_f = static_cast<int>(n.f.size());
    double diag = 0;
    if (!n.v.empty()) { ON_BoundingBox b; for (const Point3d& p : n.v) b.Set(p, true); diag = b.Diagonal().Length(); }
    const int welded = WeldVertices(n, std::max(1e-9, diag * 1e-6));
    const int degenerate = n.Clean();
    const int duplicates = RemoveDuplicateFaces(n);
    n.Compact();
    const int naked = n.NakedEdgeCount();
    int nonmanifold = 0;
    for (const auto& kv : n.EdgeFaces()) if (kv.second.size() > 2) ++nonmanifold;
    if (welded || degenerate || duplicates || before_v != static_cast<int>(n.v.size())) CommitNet(ctx, t.id, n, "RepairSubD");
    ctx.Print("RepairSubD: object " + std::to_string(t.id) + ": " + std::to_string(welded) + " vertex(es) welded, " + std::to_string(degenerate) +
              " degenerate and " + std::to_string(duplicates) + " duplicate face(s) removed, " + std::to_string(before_v - static_cast<int>(n.v.size())) +
              " unused vertex(es) dropped; " + std::to_string(n.f.size()) + " face(s) (was " + std::to_string(before_f) + "), " + std::to_string(naked) +
              " naked edge(s), " + std::to_string(nonmanifold) + " non-manifold edge(s), " + (naked == 0 && nonmanifold == 0 ? "closed" : "open"));
  }
}

void DivideAlongCreasesAction(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<Target> ts = SubDTargets(ctx, ids, "DivideAlongCreases");
  if (ts.empty()) return;
  ctx.Doc().BeginChange("DivideAlongCreases");
  int created = 0, divided = 0;
  for (Target& t : ts) {
    std::vector<Net> parts = DivideAtCreases(t.net);
    if (parts.size() < 2) { ctx.Print("DivideAlongCreases: object " + std::to_string(t.id) + " has no interior creases dividing it"); continue; }
    const SceneObject like = *ctx.Doc().Find(t.id);
    ctx.Doc().Remove(t.id);
    for (const Net& p : parts) if (AddSubDLike(ctx, p, &like) != kNoObject) ++created;
    ++divided;
  }
  ctx.Print("DivideAlongCreases: " + std::to_string(divided) + " SubD(s) divided into " + std::to_string(created) + " piece(s)");
}

// Cycles the sub-object selection filter (faces / edges / vertices / whole objects).
int g_subd_filter = 0;
const char* kFilterNames[] = {"Objects", "Faces", "Edges", "Vertices"};

void MakeFriendlyAction(CommandContext& ctx, const std::vector<ObjectId>& ids, bool friendly) {
  const std::string label = friendly ? "MakeSubDFriendly" : "SubDUnfriend";
  ctx.Doc().BeginChange(label);
  int curves = 0, surfaces = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Curve && o->curve) {
      const bool closed = o->curve->IsClosed();
      const int n = std::max(closed ? 6 : 4, o->curve->ControlPointCount());
      std::vector<Point3d> pts;
      if (friendly) {
        for (double t : o->curve->DivideByCount(closed ? n : n - 1)) pts.push_back(o->curve->PointAt(t));
        if (closed && !pts.empty()) pts.pop_back();
      } else {
        for (int i = 0; i < o->curve->ControlPointCount(); ++i) pts.push_back(o->curve->ControlPointAt(i));
      }
      ON_NurbsCurve nc;
      const int cv = static_cast<int>(pts.size()) + (closed && friendly ? 3 : 0);
      nc.Create(3, false, 4, cv);
      for (int i = 0; i < cv; ++i) nc.SetCV(i, pts[static_cast<size_t>(i) % pts.size()]);
      if (closed && friendly) nc.MakePeriodicUniformKnotVector(1.0);
      else if (friendly) nc.MakeClampedUniformKnotVector(1.0);
      else {
        // Unfriend: chord-length knot spacing (no longer uniform).
        nc.MakeClampedUniformKnotVector(1.0);
        std::vector<double> chord = {0};
        for (size_t i = 1; i < pts.size(); ++i) chord.push_back(chord.back() + std::max(1e-6, pts[i].DistanceTo(pts[i - 1])));
        const int kc = nc.KnotCount();
        for (int i = 3; i < kc - 3; ++i) {
          const size_t j = static_cast<size_t>(i - 2);
          double s = 0;
          for (size_t k = j; k < j + 3 && k < chord.size(); ++k) s += chord[k];
          nc.SetKnot(i, chord.back() > 0 ? s / (3 * chord.back()) : 0.5);
        }
      }
      if (nc.IsValid()) { o->curve->raw() = nc; o->InvalidateDisplay(); ++curves; }
    } else if (o->kind == ObjectKind::Surface && o->surface) {
      const int nu = std::max(4, o->surface->CVCountU()), nv = std::max(4, o->surface->CVCountV());
      std::vector<Point3d> grid;
      const kernel::Interval du = o->surface->Domain(0), dv = o->surface->Domain(1);
      for (int i = 0; i < nu; ++i)
        for (int j = 0; j < nv; ++j)
          grid.push_back(friendly ? o->surface->PointAt(du.min + (du.max - du.min) * i / (nu - 1), dv.min + (dv.max - dv.min) * j / (nv - 1))
                                  : o->surface->ControlPointAt(std::min(i, o->surface->CVCountU() - 1), std::min(j, o->surface->CVCountV() - 1)));
      kernel::NurbsSurface ns = kernel::NurbsSurface::FromControlGrid(grid, nu, nv, 3, 3);
      if (!friendly) {
        // Unfriend: squash the interior knots towards one end.
        for (int dir = 0; dir < 2; ++dir) {
          const int kc = ns.KnotCount(dir);
          for (int i = 3; i < kc - 3; ++i) ns.SetKnotAt(dir, i, std::pow(ns.KnotAt(dir, i), 2.0));
        }
      }
      *o->surface = ns;
      o->InvalidateDisplay();
      ++surfaces;
    } else {
      ctx.Warn(label + ": object " + std::to_string(id) + " is not a curve or surface; skipped");
    }
  }
  ctx.Print(label + ": " + std::to_string(curves) + " curve(s) and " + std::to_string(surfaces) + " surface(s) rebuilt as degree 3 " + (friendly ? "uniform" : "non-uniform"));
}

// Mesh -> SubD with creases at sharp edges.
void AutoSubDAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  const double angle = in.OptNum("Angle", 30) * ON_PI / 180.0;
  const bool delete_input = in.Yes("DeleteInput");
  ctx.Doc().BeginChange("AutomaticSubDFromMesh");
  int done = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) { ctx.Warn("AutomaticSubDFromMesh: object " + std::to_string(id) + " has no mesh; skipped"); continue; }
    Net n = NetFromMesh(m->raw());
    WeldVertices(n, 1e-6);
    n.Clean();
    std::vector<Vector3d> fn(n.f.size());
    for (size_t i = 0; i < n.f.size(); ++i) fn[i] = n.FaceNormal(n.f[i]);
    int creases = 0;
    for (const auto& kv : n.EdgeFaces()) {
      if (kv.second.size() != 2) continue;
      const double dot = std::max(-1.0, std::min(1.0, ON_DotProduct(fn[kv.second[0]], fn[kv.second[1]])));
      if (std::acos(dot) > angle) { n.crease.insert(kv.first); ++creases; }
    }
    const SceneObject like = *o;
    ObjectId nid = AddSubDLike(ctx, n, &like);
    if (nid == kNoObject) { ctx.Warn("AutomaticSubDFromMesh: could not build a SubD from object " + std::to_string(id)); continue; }
    if (delete_input) ctx.Doc().Remove(id);
    ++done;
    ctx.Print("AutomaticSubDFromMesh: SubD with " + std::to_string(n.f.size()) + " face(s), " + std::to_string(creases) + " crease edge(s) sharper than " + Num(in.OptNum("Angle", 30)) + " degrees");
  }
  if (done) ctx.Print("AutomaticSubDFromMesh: " + std::to_string(done) + " object(s) converted");
}

// Greedy triangle pairing into quads (quad-dominant remesh).
kernel::Mesh QuadDominant(const kernel::Mesh& src, int& quads, int& tris) {
  Net n = NetFromMesh(src.raw());
  std::vector<Poly> out;
  std::vector<char> used(n.f.size(), 0);
  const auto em = n.EdgeFaces();
  std::vector<Vector3d> fn(n.f.size());
  for (size_t i = 0; i < n.f.size(); ++i) fn[i] = n.FaceNormal(n.f[i]);
  quads = tris = 0;
  for (size_t fi = 0; fi < n.f.size(); ++fi) {
    if (used[fi]) continue;
    const Poly& p = n.f[fi];
    if (p.size() != 3) { out.push_back(p); used[fi] = 1; if (p.size() == 4) ++quads; continue; }
    int best = -1, best_k = -1;
    double best_score = -1;
    for (int k = 0; k < 3; ++k) {
      const int a = p[k], b = p[(k + 1) % 3];
      const int other = OtherFace(em, a, b, static_cast<int>(fi));
      if (other < 0 || used[other] || n.f[other].size() != 3) continue;
      const double score = ON_DotProduct(fn[fi], fn[other]);
      if (score > best_score) { best_score = score; best = other; best_k = k; }
    }
    if (best < 0 || best_score < 0.5) { out.push_back(p); used[fi] = 1; ++tris; continue; }
    const int a = p[best_k], b = p[(best_k + 1) % 3], c = p[(best_k + 2) % 3];
    const Poly& q = n.f[best];
    int d = -1;
    for (int i : q) if (i != a && i != b) d = i;
    out.push_back({a, d, b, c});
    used[fi] = used[best] = 1;
    ++quads;
  }
  n.f = out;
  return MeshFromNet(n);
}

void QuadRemeshAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  const int target = std::max(4, static_cast<int>(in.OptNum("TargetQuadCount", 400)));
  ctx.Doc().BeginChange("QuadRemesh");
  int done = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    kernel::Mesh result;
    int quads = 0, tris = 0;
    if (o->kind == ObjectKind::Surface && o->surface) {
      const kernel::SurfaceSize sz = o->surface->GetApproximateSize();
      const double ratio = sz.height > 0 ? std::max(0.1, std::min(10.0, sz.width / sz.height)) : 1.0;
      const int nv = std::max(2, static_cast<int>(std::lround(std::sqrt(target / ratio)))), nu = std::max(2, static_cast<int>(std::lround(nv * ratio)));
      result = o->surface->TessellateGrid(nu, nv);
      quads = result.FaceCount();
    } else {
      std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
      if (!m || m->FaceCount() == 0) { ctx.Warn("QuadRemesh: object " + std::to_string(id) + " has no mesh; skipped"); continue; }
      result = QuadDominant(*m, quads, tris);
    }
    SceneObject n = SceneObject::MakeMesh(result);
    n.layer_index = o->layer_index; n.color = o->color; n.color_by_layer = o->color_by_layer; n.name = o->name;
    if (in.Yes("DeleteInput")) ctx.Doc().Remove(id);
    ctx.Doc().Add(std::move(n));
    ++done;
    ctx.Print("QuadRemesh: " + std::to_string(quads) + " quad(s), " + std::to_string(tris) + " triangle(s) left");
  }
  if (done) ctx.Print("QuadRemesh: " + std::to_string(done) + " mesh(es) created (surfaces are sampled on a UV grid; other objects are quad-paired)");
}

void ShrinkWrapAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  std::vector<Point3d> pts;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    switch (o->kind) {
      case ObjectKind::Point: pts.push_back(o->point); break;
      case ObjectKind::Curve: for (double t : o->curve->DivideByCount(64)) pts.push_back(o->curve->PointAt(t)); break;
      default: {
        std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
        if (m) for (int i = 0; i < m->VertexCount(); ++i) pts.push_back(m->raw().Vertex(i));
        break;
      }
    }
  }
  std::optional<Hull> h = ConvexHull(pts);
  if (!h) { ctx.Warn("ShrinkWrap: need at least four non-coplanar points"); return; }
  Net n;
  n.v = h->pts;
  for (const auto& t : h->tris) n.f.push_back({t[0], t[1], t[2]});
  n.Compact();
  const double offset = in.OptNum("Offset", 0);
  if (offset != 0) { const std::vector<Vector3d> vn = n.VertexNormals(); for (size_t i = 0; i < n.v.size(); ++i) n.v[i] += vn[i] * offset; }
  kernel::Mesh m = MeshFromNet(n);
  ctx.Doc().BeginChange("ShrinkWrap");
  ctx.Doc().Add(SceneObject::MakeMesh(m));
  ctx.Print("ShrinkWrap: convex hull with " + std::to_string(m.FaceCount()) + " faces around " + std::to_string(pts.size()) + " points" + (offset != 0 ? ", offset " + Num(offset) : "") +
            ", volume " + Num(m.Volume()));
}

void TruncatedConeAction(CommandContext& ctx, const std::vector<ObjectId>&, const Input& in) {
  if (in.picks.empty()) { ctx.Warn("SubDTruncatedCone: base point needed"); return; }
  const Point3d base = in.picks[0];
  const double r1 = std::fabs(in.N(0, 5)), r2 = std::fabs(in.N(1, 2.5)), h = in.N(2, 10);
  if (r1 <= 0 || h == 0) { ctx.Warn("SubDTruncatedCone: base radius and height must be non-zero"); return; }
  const ON_Plane pl = ActivePlane(ctx);
  const int segs = 16;
  Net n;
  const Point3d top = base + pl.zaxis * h;
  for (int i = 0; i < segs; ++i) { const double a = 2 * ON_PI * i / segs; n.Add(base + pl.xaxis * (r1 * std::cos(a)) + pl.yaxis * (r1 * std::sin(a))); }
  const bool apex = r2 <= 1e-9;
  if (apex) n.Add(top);
  else for (int i = 0; i < segs; ++i) { const double a = 2 * ON_PI * i / segs; n.Add(top + pl.xaxis * (r2 * std::cos(a)) + pl.yaxis * (r2 * std::sin(a))); }
  for (int i = 0; i < segs; ++i) {
    const int j = (i + 1) % segs;
    if (apex) n.f.push_back({i, j, segs});
    else n.f.push_back({i, j, segs + j, segs + i});
  }
  const int bc = n.Add(base);
  for (int i = 0; i < segs; i += 2) n.f.push_back({bc, (i + 2) % segs, i + 1, i});
  if (!apex) {
    const int tc = n.Add(top);
    for (int i = 0; i < segs; i += 2) n.f.push_back({tc, segs + i, segs + i + 1, segs + (i + 2) % segs});
  }
  if (h < 0) for (Poly& p : n.f) std::reverse(p.begin(), p.end());
  ctx.Doc().BeginChange("SubDTruncatedCone");
  if (AddSubDLike(ctx, n, nullptr) == kNoObject) { ctx.Warn("SubDTruncatedCone: could not build the SubD"); return; }
  ctx.Print("SubDTruncatedCone: base radius " + Num(r1) + ", top radius " + Num(r2) + ", height " + Num(h) + ", " + std::to_string(n.f.size()) + " faces");
}

// Runs another registered command and converts the object it created to a SubD.
class SweepThenSubDCommand : public Command {
 public:
  SweepThenSubDCommand(std::string inner_name, std::string label) : inner_name_(std::move(inner_name)), label_(std::move(label)) {}
  void Begin(CommandContext& ctx) override {
    const RegisteredCommand* r = ctx.Engine().Find(inner_name_);
    if (!r || !r->factory) { ctx.Warn(label_ + ": " + inner_name_ + " is not available"); Finish(); return; }
    for (const SceneObject& o : ctx.Doc().Objects()) max_id_ = std::max(max_id_, o.id);
    inner_ = r->factory();
    inner_->Begin(ctx);
    Sync(ctx);
  }
  void OnPoint(CommandContext& ctx, Point3d p) override { if (inner_) { inner_->OnPoint(ctx, p); Sync(ctx); } }
  void OnNumber(CommandContext& ctx, double v) override { if (inner_) { inner_->OnNumber(ctx, v); Sync(ctx); } }
  void OnText(CommandContext& ctx, const std::string& t) override { if (inner_) { inner_->OnText(ctx, t); Sync(ctx); } }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override { if (inner_) { inner_->OnObjects(ctx, ids); Sync(ctx); } }
  void OnEnter(CommandContext& ctx) override { if (inner_) { inner_->OnEnter(ctx); Sync(ctx); } }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string& v) override { if (inner_) { inner_->OnOption(ctx, n, v); Sync(ctx); } }
  void OnHover(CommandContext& ctx, Point3d h) override { if (inner_) inner_->OnHover(ctx, h); }
  void OnCancel(CommandContext& ctx) override { if (inner_) inner_->OnCancel(ctx); }

 private:
  void Sync(CommandContext& ctx) {
    want = inner_->want;
    prompt = inner_->prompt;
    options = inner_->options;
    min_objects = inner_->min_objects;
    accept_preselection = inner_->accept_preselection;
    default_number = inner_->default_number;
    default_text = inner_->default_text;
    if (!inner_->finished) return;
    Convert(ctx);
    Finish();
  }
  void Convert(CommandContext& ctx) {
    std::vector<ObjectId> created;
    for (const SceneObject& o : ctx.Doc().Objects()) if (o.id > max_id_) created.push_back(o.id);
    int done = 0;
    for (ObjectId id : created) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o || o->kind == ObjectKind::SubD) continue;
      std::optional<kernel::Mesh> m;
      if (o->kind == ObjectKind::Surface && o->surface) {
        const kernel::SurfaceDivisions div = o->surface->SuggestedDivisions(ctx.App().surface_display_tolerance * 8);
        m = o->surface->TessellateGrid(std::clamp(div.u, 4, 24), std::clamp(div.v, 4, 24));
      } else {
        m = MeshOf(*o, ctx.App().surface_display_tolerance);
      }
      if (!m || m->FaceCount() == 0) continue;
      Net n = NetFromMesh(m->raw());
      WeldVertices(n, 1e-6);
      n.Clean();
      const SceneObject like = *o;
      if (AddSubDLike(ctx, n, &like) == kNoObject) continue;
      ctx.Doc().Remove(id);
      ++done;
    }
    ctx.Print(label_ + ": " + std::to_string(done) + " object(s) converted to SubD");
  }
  std::string inner_name_, label_;
  std::unique_ptr<Command> inner_;
  ObjectId max_id_ = kNoObject;
};

void ToggleDisplay(CommandContext& ctx, const std::vector<ObjectId>& ids) {
  std::vector<ObjectId> targets = ids;
  if (targets.empty()) for (const SceneObject& o : ctx.Doc().Objects()) if (o.kind == ObjectKind::SubD) targets.push_back(o.id);
  int smooth = 0, net = 0;
  for (ObjectId id : targets) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::SubD) continue;
    o->show_control_net = !o->show_control_net;
    o->InvalidateDisplay();
    (o->show_control_net ? net : smooth)++;
  }
  ctx.RequestRedraw();
  ctx.Print("SubDDisplayToggle: " + std::to_string(net) + " SubD(s) show the control polygon, " + std::to_string(smooth) + " the smooth surface");
}

bool IsClosedSubD(const SceneObject& o) { return o.kind == ObjectKind::SubD && o.subd && Unpack(o.subd->raw()).NakedEdgeCount() == 0; }

CommandFactory SelWhere(std::function<bool(const SceneObject&)> pred) {
  return Immediate([pred](CommandContext& ctx) {
    ctx.Doc().SelectWhere([pred, &ctx](const SceneObject& o) { return pred(o) && ctx.Doc().IsObjectVisible(o) && !ctx.Doc().IsObjectLocked(o); }, true);
    ctx.Print(std::to_string(ctx.Doc().SelectedCount()) + " object(s) selected");
  });
}

}  // namespace

void RegisterSubDCommands(CommandEngine& e) {
  // ---- creases ---------------------------------------------------------------
  auto crease = [](bool add) {
    return SubDTool(kSelectSubD, add ? "Pick edges to crease" : "Pick creased edges to smooth", -1, {}, {},
                    [add](CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) { CreaseAction(ctx, ids, in, add); });
  };
  Reg(e, "Crease", crease(true));
  Reg(e, "SubDCrease", crease(true));
  Reg(e, "RemoveCrease", crease(false));
  Reg(e, "SubDExpandEdges", SubDTool(kSelectSubD, "Pick edges to expand", -1, {{"Strip width", 1}}, {}, ExpandEdgesAction),
      CommandStatus::Partial, "Splits the faces next to each picked edge into a strip of the given width (no edge-loop continuation).");
  Reg(e, "DivideAlongCreases", OnSelection("Select SubDs to divide", DivideAlongCreasesAction));
  Reg(e, "MakeSubDFriendly", OnSelection("Select curves or surfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MakeFriendlyAction(ctx, ids, true); }),
      CommandStatus::Partial, "Rebuilds as degree-3 uniform curves/surfaces through sampled points (approximate).");
  Reg(e, "SubDUnfriend", OnSelection("Select SubD-friendly curves or surfaces", [](CommandContext& ctx, const std::vector<ObjectId>& ids) { MakeFriendlyAction(ctx, ids, false); }),
      CommandStatus::Partial, "Re-spaces the knots so the object is no longer uniform.");
  Reg(e, "RepairSubD", OnSelection("Select SubDs to repair", RepairAction));
  Reg(e, "PackSubDFaces", OnSelection("Select SubDs", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        for (ObjectId id : ids) { const SceneObject* o = ctx.Doc().Find(id); if (o && o->kind == ObjectKind::SubD) ctx.Print("PackSubDFaces: object " + std::to_string(id) + ": " + std::to_string(o->subd->FaceCount()) + " face(s) in 1 pack (texture packing is not stored in this build)"); }
      }), CommandStatus::Partial, "Reports the face count; texture packs are not stored.");

  // ---- display / selection ------------------------------------------------------
  Reg(e, "SubDDisplayToggle", Immediate([](CommandContext& ctx) { ToggleDisplay(ctx, ctx.Selected()); }));
  Reg(e, "SubDFaceEdgeVertexToggle", Immediate([](CommandContext& ctx) {
        g_subd_filter = (g_subd_filter + 1) % 4;
        ctx.Print(std::string("SubD selection filter: ") + kFilterNames[g_subd_filter] + " (sub-object picking is by point in this build)");
      }), CommandStatus::Partial, "Cycles a selection filter flag; sub-objects are picked by point.");
  Reg(e, "SelSubDEdges", Immediate([](CommandContext& ctx) {
        int subds = 0, creases = 0;
        ctx.Doc().SelectWhere([&](const SceneObject& o) { if (o.kind != ObjectKind::SubD || !o.subd) return false; ++subds; creases += o.subd->CreaseEdgeCount(); return true; }, true);
        ctx.Print("SelSubDEdges: " + std::to_string(subds) + " SubD(s) selected with " + std::to_string(creases) + " crease edge(s); edge sub-object selection is not available in this build");
      }), CommandStatus::Partial, "Selects the SubDs; edges cannot be selected as sub-objects.");
  Reg(e, "SelClosedSubD", SelWhere([](const SceneObject& o) { return IsClosedSubD(o); }));
  Reg(e, "SelOpenSubD", SelWhere([](const SceneObject& o) { return o.kind == ObjectKind::SubD && !IsClosedSubD(o); }));

  // ---- control-net editing --------------------------------------------------------
  Reg(e, "ExtrudeSubD", SubDTool(kSelectSubD, "Pick faces to extrude (Enter for the whole SubD)", -1, {{"Extrusion distance", 1}}, {}, ExtrudeSubDAction));
  Reg(e, "OffsetSubD", SubDTool("Select SubDs to offset", "", 0, {{"Offset distance", 1}}, {Toggle("Solid", false), Toggle("DeleteInput", false)}, OffsetSubDAction));
  Reg(e, "Inset", SubDTool(kSelectSubD, "Pick faces to inset", -1, {{"Inset distance", 1}}, {}, InsetAction));
  Reg(e, "Bridge", SubDTool("Select SubDs", "Pick two faces to bridge", 2, {}, {}, BridgeAction));
  Reg(e, "InsertEdge", SubDTool(kSelectSubD, "Pick an edge of the quad ring", -1, {}, {Numeric("Position", 0.5)}, InsertEdgeAction),
      CommandStatus::Implemented, "Inserts an edge loop across the quad ring through the picked edge.");
  Reg(e, "InsertPoint", SubDTool(kSelectSubD, "Pick points on edges", -1, {}, {}, InsertPointAction),
      CommandStatus::Partial, "Splits the nearest edge at the pick; the adjacent faces gain a corner (no connecting edges).");
  Reg(e, "Stitch", SubDTool(kSelectSubD, "Pick two edges (or vertices)", 2, {}, {Choice("Mode", {"Edges", "Vertices"})}, StitchAction));
  Reg(e, "Slide", SubDTool(kSelectSubD, "Pick a vertex, then the point to slide towards", 2, {}, {}, SlideAction),
      CommandStatus::Partial, "Slides one control vertex along its best-aligned edge.");
  Reg(e, "SubDSpinEdge", SubDTool(kSelectSubD, "Pick edges to spin", -1, {}, {}, SpinEdgeAction),
      CommandStatus::Partial, "Spins the edge one corner around the two faces sharing it; no direction option.");
  Reg(e, "Fill", SubDTool(kSelectSubD, "Pick naked edges of the holes to fill (Enter for all)", -1, {}, {}, FillAction));
  Reg(e, "AddGuide", Immediate([](CommandContext& ctx) { ctx.Print("AddGuide: guide curves are not stored in this build; use Slide and InsertEdge to shape the control net."); }),
      CommandStatus::Partial, "Guide curves are not stored.");
  Reg(e, "RemoveGuide", Immediate([](CommandContext& ctx) { ctx.Print("RemoveGuide: no guide curves are stored in this build."); }),
      CommandStatus::Partial, "Guide curves are not stored.");

  // ---- creation / conversion -------------------------------------------------------
  Reg(e, "SubDTruncatedCone", SubDTool("", "Base point", 1, {{"Base radius", 5}, {"Top radius", 2.5}, {"Height", 10}}, {}, TruncatedConeAction, 0));
  Reg(e, "SubDSweep1", [] { return std::unique_ptr<Command>(std::make_unique<SweepThenSubDCommand>("Sweep1", "SubDSweep1")); },
      CommandStatus::Implemented, "Runs Sweep1 and converts the result to a SubD.");
  Reg(e, "SubDSweep2", [] { return std::unique_ptr<Command>(std::make_unique<SweepThenSubDCommand>("Sweep2", "SubDSweep2")); },
      CommandStatus::Implemented, "Runs Sweep2 and converts the result to a SubD.");
  Reg(e, "AutomaticSubDFromMesh", SubDTool("Select meshes to convert", "", 0, {}, {Numeric("Angle", 30), Toggle("DeleteInput", true)}, AutoSubDAction));
  Reg(e, "QuadRemesh", SubDTool("Select objects to remesh", "", 0, {}, {Numeric("TargetQuadCount", 400), Toggle("DeleteInput", false)}, QuadRemeshAction),
      CommandStatus::Partial, "Surfaces are sampled on a UV grid; other objects have their triangles paired into quads.");
  Reg(e, "ShrinkWrap", SubDTool("Select objects to wrap", "", 0, {}, {Numeric("Offset", 0)}, ShrinkWrapAction),
      CommandStatus::Partial, "Builds the 3D convex hull of the selection (concavities are not followed).");
}

}  // namespace dino8::app
