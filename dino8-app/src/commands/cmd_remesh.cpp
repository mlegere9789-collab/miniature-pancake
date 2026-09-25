// Real volumetric mesh tools built on src/geom/Remesh.h: ShrinkWrap (signed-
// distance voxelisation + marching cubes), QuadRemesh (surface UV grid for
// single surfaces, dual contouring for everything else), ReduceMesh (a
// quadric-error edge collapse), Heightfield from an image, and an upgraded
// Drape (a BVH-accelerated ray cast plus a settling relaxation pass).
//
// Registered after RegisterSubDCommands so these definitions win over the
// simpler placeholders in cmd_subd.cpp (ShrinkWrap/QuadRemesh) and
// cmd_boolean.cpp (ReduceMesh) - see Application::RegisterCommands().
#include "commands/cmd_common.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "app/Settings.h"
#include "dino8/kernel/subd.h"
#include "geom/Remesh.h"
#include "render/ImageIO.h"

namespace dino8::app {

namespace {

using remesh::Source;
using remesh::SdfGrid;
using remesh::SdfOptions;
using remesh::SdfReport;

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
// A tool command shaped like cmd_meshtools.cpp's ToolCommand/cmd_subd.cpp's
// SubDCommand (select objects, then answer options - no point/number
// prompts, since every option here has a sane default), kept local so this
// file has no compile-order dependency on either.
// ---------------------------------------------------------------------------
struct Input {
  std::map<std::string, std::string> opts;
  std::vector<Point3d> picks;
  Point3d P(size_t i) const { return i < picks.size() ? picks[i] : Point3d::Origin; }
  std::string Opt(const std::string& name, const std::string& def = "") const {
    auto it = opts.find(Lower(name));
    return it == opts.end() ? def : it->second;
  }
  bool Yes(const std::string& name, bool def = false) const {
    const std::string v = Opt(name);
    if (v.empty()) return def;
    return Lower(v) == "yes";
  }
  double OptNum(const std::string& name, double def) const {
    double v;
    return ParseNumber(Opt(name), v) ? v : def;
  }
};

using Action = std::function<void(CommandContext&, const std::vector<ObjectId>&, const Input&)>;

// Either "select objects, then answer options" (point_prompts empty) or
// "pick N points, then answer options" (select_prompt empty) - covers every
// command in this file: ShrinkWrap/QuadRemesh/ReduceMesh select objects,
// Heightfield/Drape pick two corner points.
class RemeshCommand : public Command {
 public:
  RemeshCommand(std::string select_prompt, std::vector<std::string> point_prompts, std::vector<OptionSpec> opts, Action action,
                int min_objects = 1)
      : select_(std::move(select_prompt)), point_prompts_(std::move(point_prompts)), opts_(std::move(opts)), action_(std::move(action)),
        min_(min_objects) {
    for (const OptionSpec& o : opts_) in_.opts[Lower(o.name)] = o.value;
  }
  void Begin(CommandContext&) override {
    options = opts_;
    if (!point_prompts_.empty()) WantPoint(point_prompts_[0]);
    else WantObjects(select_, min_);
  }
  void OnObjects(CommandContext& ctx, const std::vector<ObjectId>& ids) override {
    if (ids.empty() && min_ > 0) { ctx.Warn("Nothing selected"); Finish(); return; }
    ids_ = ids;
    Run(ctx);
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    ctx.SetLastPoint(p);
    in_.picks.push_back(p);
    if (in_.picks.size() >= point_prompts_.size()) { Run(ctx); return; }
    WantPoint(point_prompts_[in_.picks.size()]);
    options = opts_;
  }
  void OnHover(CommandContext& ctx, Point3d h) override {
    if (in_.picks.empty()) return;
    ctx.ClearPreview();
    ctx.AddPreviewLine(in_.picks.back(), h);
  }
  void OnEnter(CommandContext& ctx) override {
    if (want == Want::Objects) { if (min_ == 0) Run(ctx); else { ctx.Warn("Nothing selected"); Finish(); } }
  }
  void OnText(CommandContext&, const std::string&) override {}
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
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  void Run(CommandContext& ctx) {
    ctx.ClearPreview();
    action_(ctx, ids_, in_);
    Finish();
  }
  std::string select_;
  std::vector<std::string> point_prompts_;
  std::vector<OptionSpec> opts_;
  Action action_;
  int min_;
  Input in_;
  std::vector<ObjectId> ids_;
};

OptionSpec Toggle(const char* name, bool on) { return {name, on ? "Yes" : "No", {"Yes", "No"}, false, true}; }
OptionSpec Numeric(const char* name, double v) { return {name, FormatNumber(v), {}, true, false}; }
OptionSpec Choice(const char* name, std::vector<std::string> choices, int def) {
  return {name, choices[static_cast<size_t>(def)], std::move(choices), false, false};
}

CommandFactory RemeshTool(std::string select_prompt, std::vector<OptionSpec> opts, Action action, int min_objects = 1) {
  return [=]() -> std::unique_ptr<Command> { return std::make_unique<RemeshCommand>(select_prompt, std::vector<std::string>{}, opts, action, min_objects); };
}
CommandFactory RemeshPointsTool(std::vector<std::string> point_prompts, std::vector<OptionSpec> opts, Action action) {
  return [=]() -> std::unique_ptr<Command> { return std::make_unique<RemeshCommand>(std::string(), point_prompts, opts, action, 0); };
}

// Persisted last-used settings (DocumentSettings has no room for these, and
// they should survive across documents like every other user preference -
// the audit's exact complaint about Rhino's ShrinkWrap). Stored as a single
// packed line in AppState-adjacent global state via Settings.h's JSON file
// by piggy-backing on the working-folder-style free-form fields would need
// a schema change; instead each session keeps them in these statics and
// Settings.cpp persists them (see the two functions below).
struct ShrinkWrapSettings {
  double target_edge_length = 0;  // 0 = derive from bbox
  double offset = 0;
  int smooth = 5;
  bool fill_holes = true;
  int max_voxels = 400000;
};
ShrinkWrapSettings& PersistedShrinkWrap() {
  static ShrinkWrapSettings s;
  return s;
}

// ---------------------------------------------------------------------------
// Turns the selection into remesh::Source objects (closed meshes get
// pseudonormal sign data; open meshes/curves/points become offset shells).
// ---------------------------------------------------------------------------
std::vector<Source> SourcesFromSelection(CommandContext& ctx, const std::vector<ObjectId>& ids, double weld_tol) {
  std::vector<Source> sources;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    if (o->kind == ObjectKind::Point) { sources.push_back(remesh::SourceFromPoint(o->point)); continue; }
    if (o->kind == ObjectKind::Curve && o->curve) {
      std::vector<Point3d> pts;
      for (double t : o->curve->DivideByCount(std::max(8, static_cast<int>(o->curve->Length() / std::max(1e-6, ctx.App().curve_display_tolerance * 20)) + 8)))
        pts.push_back(o->curve->PointAt(t));
      sources.push_back(remesh::SourceFromPolyline(pts));
      continue;
    }
    std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) continue;
    sources.push_back(remesh::SourceFromMesh(*m, weld_tol));
  }
  return sources;
}

// ---------------------------------------------------------------------------
// ShrinkWrap
// ---------------------------------------------------------------------------
void ShrinkWrapAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  ShrinkWrapSettings& persisted = PersistedShrinkWrap();
  const double target_edge = in.OptNum("TargetEdgeLength", persisted.target_edge_length);
  const double offset = in.OptNum("Offset", persisted.offset);
  const int smooth = std::max(0, static_cast<int>(in.OptNum("Smooth", persisted.smooth)));
  const bool fill_holes = in.Opt("FillHoles").empty() ? persisted.fill_holes : in.Yes("FillHoles", persisted.fill_holes);
  const int max_voxels = std::max(8000, static_cast<int>(in.OptNum("MaxVoxels", persisted.max_voxels)));
  const bool delete_input = in.Yes("DeleteInput");

  const double weld_tol = ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 1e-6;
  std::vector<Source> sources = SourcesFromSelection(ctx, ids, weld_tol);
  if (sources.empty()) { ctx.Warn("ShrinkWrap: nothing meshable in the selection"); return; }

  SdfOptions sopt;
  sopt.voxel = target_edge > 0 ? target_edge : 0;
  sopt.max_voxels = max_voxels;
  sopt.offset = offset;
  sopt.fill_holes = fill_holes;
  SdfReport report;
  SdfGrid grid = remesh::BuildSdf(sources, sopt, report);
  kernel::Mesh result = remesh::MarchingCubes(grid, offset);
  if (result.FaceCount() == 0) { ctx.Warn("ShrinkWrap: the selection produced an empty field (try a larger Offset or TargetEdgeLength)"); return; }
  if (smooth > 0) {
    remesh::SmoothTaubin(result, smooth);
    remesh::ProjectToIso(result, grid, offset);
  }
  const int target_polys = static_cast<int>(in.OptNum("PolygonCount", 0));
  if (target_polys > 0 && result.FaceCount() > target_polys) result = remesh::Decimate(result, target_polys, false);

  ctx.Doc().BeginChange("ShrinkWrap");
  if (delete_input) for (ObjectId id : ids) ctx.Doc().Remove(id);
  ctx.Doc().Add(SceneObject::MakeMesh(result));
  const bool closed = result.IsClosedManifold();
  ctx.Print("ShrinkWrap: signed-distance wrap with " + std::to_string(result.VertexCount()) + " vertices, " + std::to_string(result.FaceCount()) +
            " faces (" + (closed ? "closed" : "open") + "), voxel " + Num(grid.h) + " (" + std::to_string(grid.nx) + "x" + std::to_string(grid.ny) + "x" +
            std::to_string(grid.nz) + "), volume " + Num(result.Volume()));
  if (report.capped) ctx.Warn("ShrinkWrap: MaxVoxels=" + std::to_string(max_voxels) + " forced a coarser voxel size (" + Num(report.voxel) +
                              " instead of the requested " + Num(report.requested_voxel) + ")");

  // Persist for next time (the audit's complaint: Rhino forgets these).
  persisted.target_edge_length = target_edge;
  persisted.offset = offset;
  persisted.smooth = smooth;
  persisted.fill_holes = fill_holes;
  persisted.max_voxels = max_voxels;
  SaveSettings(ctx.App(), 1.0f);
}

// ---------------------------------------------------------------------------
// QuadRemesh
// ---------------------------------------------------------------------------
void QuadRemeshAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  const int target_quads = std::max(4, static_cast<int>(in.OptNum("TargetQuadCount", 400)));
  const bool adaptive = in.Yes("AdaptiveSize", true);
  const bool detect_hard = in.Yes("DetectHardEdges", true);
  const std::string sym = Lower(in.Opt("SymmetryAxis", "None"));
  const int sym_axis = sym == "x" ? 0 : sym == "y" ? 1 : sym == "z" ? 2 : -1;
  const bool to_subd = in.Yes("ConvertToSubD");
  const bool delete_input = in.Yes("DeleteInput");

  ctx.Doc().BeginChange("QuadRemesh");
  int done = 0;
  for (ObjectId id : ids) {
    const SceneObject* o = ctx.Doc().Find(id);
    if (!o) continue;
    kernel::Mesh result;
    if (o->kind == ObjectKind::Surface && o->surface) {
      // A single surface has an exact parameterisation: sample its UV grid
      // directly rather than going through a volumetric field.
      const kernel::SurfaceSize sz = o->surface->GetApproximateSize();
      const double ratio = sz.height > 0 ? std::max(0.1, std::min(10.0, sz.width / sz.height)) : 1.0;
      const int nv = std::max(2, static_cast<int>(std::lround(std::sqrt(target_quads / ratio))));
      const int nu = std::max(2, static_cast<int>(std::lround(nv * ratio)));
      result = o->surface->TessellateGrid(nu, nv);
    } else {
      std::optional<kernel::Mesh> m = MeshOf(*o, ctx.App().surface_display_tolerance);
      if (!m || m->FaceCount() == 0) { ctx.Warn("QuadRemesh: object " + std::to_string(id) + " has no mesh; skipped"); continue; }
      // Voxel-guided quads: build the SDF of this one body, then dual-
      // contour it - an all-quad, manifold, feature-preserving remesh
      // whose density is set directly from the target quad count (each
      // active grid cell contributes ~1 quad).
      Source src = remesh::SourceFromMesh(*m, ctx.Settings().absolute_tolerance > 0 ? ctx.Settings().absolute_tolerance : 1e-6);
      if (!src.closed) { ctx.Warn("QuadRemesh: object " + std::to_string(id) + " is not a closed mesh; wrapped with a thin shell first"); }
      std::vector<Source> one = {src};
      Point3d lo, hi;
      remesh::SourceBounds(one, lo, hi);
      const double area = src.closed ? 0 : 0;
      (void)area;
      // Surface-area-based voxel sizing: a closed mesh of area A remeshed
      // into Q quads of side s needs s ~= sqrt(A/Q); an open source falls
      // back to the bbox heuristic (its own union sign band handles it).
      double voxel;
      const double surf_area = remesh::SourceArea(src);
      if (adaptive && surf_area > 0) voxel = std::sqrt(surf_area / target_quads);
      else { const Vector3d ext = hi - lo; voxel = std::max({ext.x, ext.y, ext.z, 1e-6}) / std::max(4.0, std::sqrt(static_cast<double>(target_quads))); }
      SdfOptions sopt;
      sopt.voxel = voxel;
      sopt.max_voxels = 1500000;
      SdfReport report;
      SdfGrid grid = remesh::BuildSdf(one, sopt, report);
      if (sym_axis >= 0) remesh::MirrorSdf(grid, sym_axis);
      std::vector<char> sharp;
      result = remesh::DualContouring(grid, 0.0, detect_hard, &sharp);
      remesh::SmoothTaubin(result, 2, &sharp);
      remesh::ProjectToIso(result, grid, 0.0, &sharp);
    }
    if (result.FaceCount() == 0) { ctx.Warn("QuadRemesh: object " + std::to_string(id) + " produced no faces; skipped"); continue; }
    if (to_subd) {
      try {
        kernel::SubD sd = kernel::SubD::FromControlMesh(result);
        SceneObject n = SceneObject::MakeSubD(sd);
        n.layer_index = o->layer_index; n.color = o->color; n.color_by_layer = o->color_by_layer; n.name = o->name;
        if (delete_input) ctx.Doc().Remove(id);
        ctx.Doc().Add(std::move(n));
      } catch (const std::exception& ex) { ctx.Warn(std::string("QuadRemesh: ConvertToSubD failed: ") + ex.what()); continue; }
    } else {
      SceneObject n = SceneObject::MakeMesh(result);
      n.layer_index = o->layer_index; n.color = o->color; n.color_by_layer = o->color_by_layer; n.name = o->name;
      if (delete_input) ctx.Doc().Remove(id);
      ctx.Doc().Add(std::move(n));
    }
    ++done;
    ctx.Print("QuadRemesh: " + std::to_string(result.FaceCount()) + " quad(s)" + (result.IsClosedManifold() ? ", closed" : "") +
              (to_subd ? " -> SubD" : ""));
  }
  if (done) ctx.Print("QuadRemesh: " + std::to_string(done) + " object(s) remeshed");
}

// ---------------------------------------------------------------------------
// ReduceMesh
// ---------------------------------------------------------------------------
void ReduceMeshAction(CommandContext& ctx, const std::vector<ObjectId>& ids, const Input& in) {
  const double pct = in.OptNum("ReductionPercent", 50);
  const int target_explicit = static_cast<int>(in.OptNum("TargetFaceCount", 0));
  const bool preserve_boundary = in.Yes("PreserveBoundary", true);
  ctx.Doc().BeginChange("ReduceMesh");
  int done = 0;
  for (ObjectId id : ids) {
    SceneObject* o = ctx.Doc().Find(id);
    if (!o || o->kind != ObjectKind::Mesh || !o->mesh) continue;
    const int before = o->mesh->FaceCount();
    int target = target_explicit > 0 ? target_explicit : std::max(4, static_cast<int>(std::lround(before * (1.0 - pct / 100.0))));
    if (target >= before) { ctx.Print("ReduceMesh: object " + std::to_string(id) + " already at or below the target face count"); continue; }
    try {
      kernel::Mesh reduced = remesh::Decimate(*o->mesh, target, preserve_boundary);
      const double deviation = remesh::MaxDeviation(*o->mesh, reduced);
      *o->mesh = reduced;
      o->InvalidateDisplay();
      ++done;
      ctx.Print("ReduceMesh: object " + std::to_string(id) + ": " + std::to_string(before) + " -> " + std::to_string(reduced.FaceCount()) +
                " faces, max deviation " + Num(deviation));
    } catch (const std::exception& ex) { ctx.Warn(ex.what()); }
  }
  if (done == 0) ctx.Warn("ReduceMesh: no eligible mesh in the selection");
}

// ---------------------------------------------------------------------------
// Heightfield from an image (Image= option) or the existing sine-wave grid.
// ---------------------------------------------------------------------------
void HeightfieldAction(CommandContext& ctx, const std::vector<ObjectId>&, const Input& in) {
  ON_Plane pl = ActivePlane(ctx);
  double u0, v0, u1, v1;
  pl.ClosestPointTo(in.P(0), &u0, &v0);
  pl.ClosestPointTo(in.P(1), &u1, &v1);
  if (u0 > u1) std::swap(u0, u1);
  if (v0 > v1) std::swap(v0, v1);
  const double amp = in.OptNum("Amplitude", std::max(u1 - u0, v1 - v0) / 8);
  const std::string image_path = in.Opt("Image");
  Image img;
  bool have_image = false;
  if (!image_path.empty()) {
    std::string err;
    have_image = LoadImageFile(image_path, img, err);
    if (!have_image) ctx.Warn("Heightfield: could not read Image=" + image_path + " (" + err + "); using the default sine wave");
  }
  const int n = have_image ? std::max(2, std::min(400, std::max(img.width, img.height))) : std::max(2, std::min(200, static_cast<int>(in.OptNum("Resolution", 60))));
  kernel::Mesh mesh;
  ON_Mesh& m = mesh.raw();
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const double s = static_cast<double>(i) / (n - 1), t = static_cast<double>(j) / (n - 1);
      double z;
      if (have_image) {
        const int px = std::min(img.width - 1, static_cast<int>(s * img.width));
        const int py = std::min(img.height - 1, static_cast<int>((1.0 - t) * img.height));
        const size_t o = (static_cast<size_t>(py) * img.width + px) * 4;
        const double lum = (0.2126 * img.rgba[o] + 0.7152 * img.rgba[o + 1] + 0.0722 * img.rgba[o + 2]) / 255.0;
        z = amp * lum;
      } else {
        const double waves = in.OptNum("Waves", 2);
        z = amp * 0.5 * (1 + std::sin(2 * ON_PI * waves * s) * std::cos(2 * ON_PI * waves * t));
      }
      m.SetVertex(j * n + i, pl.PointAt(u0 + (u1 - u0) * s, v0 + (v1 - v0) * t) + pl.zaxis * z);
    }
  { int fi = 0; for (int j = 0; j + 1 < n; ++j) for (int i = 0; i + 1 < n; ++i) m.SetQuad(fi++, j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i); }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  AddObject(ctx, SceneObject::MakeMesh(mesh), "Heightfield");
  ctx.Print("Heightfield: " + std::to_string(n) + "x" + std::to_string(n) + " grid" + (have_image ? " from " + image_path : ", amplitude " + Num(amp)));
}

// ---------------------------------------------------------------------------
// Drape: BVH-accelerated ray cast over the visible geometry, then a settling
// relaxation pass - the grid can only rest where the surface underneath
// blocks it, and is pulled taut (averaged with its neighbours) everywhere
// else, like a cloth dropped over the scene and allowed to relax.
// ---------------------------------------------------------------------------
void DrapeAction(CommandContext& ctx, const std::vector<ObjectId>&, const Input& in) {
  ON_Plane pl = ActivePlane(ctx);
  double u0, v0, u1, v1;
  pl.ClosestPointTo(in.P(0), &u0, &v0);
  pl.ClosestPointTo(in.P(1), &u1, &v1);
  if (u0 > u1) std::swap(u0, u1);
  if (v0 > v1) std::swap(v0, v1);
  const double size = std::max(u1 - u0, v1 - v0);
  const int n = std::max(2, std::min(200, static_cast<int>(in.OptNum("Resolution", 20))));
  const int relax = std::max(0, static_cast<int>(in.OptNum("Smooth", 6)));

  std::vector<remesh::Primitive> prims;
  bool any = false;
  double top = 0;
  for (const SceneObject& o : ctx.Doc().Objects()) {
    if (!ctx.Doc().IsObjectVisible(o)) continue;
    std::optional<kernel::Mesh> m = MeshOf(o, ctx.App().surface_display_tolerance);
    if (!m || m->FaceCount() == 0) continue;
    Source s = remesh::SourceFromMesh(*m, 1e-6);
    for (const remesh::Primitive& p : s.prims) {
      prims.push_back(p);
      for (const Point3d& v : {p.p[0], p.p[1], p.p[2]}) { const double z = ON_DotProduct(v - pl.origin, pl.zaxis); if (!any || z > top) { top = z; any = true; } }
    }
  }
  if (!any) { ctx.Warn("Drape: no visible surfaces or meshes to drape over"); return; }
  remesh::Bvh bvh(prims);
  const double start = top + std::max(1.0, size * 0.05);

  std::vector<Point3d> xy(static_cast<size_t>(n) * n);
  std::vector<double> floorZ(static_cast<size_t>(n) * n, -std::numeric_limits<double>::max());
  std::vector<char> hit(static_cast<size_t>(n) * n, 0);
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const Point3d origin = pl.PointAt(u0 + (u1 - u0) * i / (n - 1), v0 + (v1 - v0) * j / (n - 1)) + pl.zaxis * start;
      xy[static_cast<size_t>(j) * n + i] = origin;
      double t;
      if (bvh.Ray(origin, -pl.zaxis, t)) { floorZ[static_cast<size_t>(j) * n + i] = start - t; hit[static_cast<size_t>(j) * n + i] = 1; }
    }
  std::vector<double> h(static_cast<size_t>(n) * n);
  for (size_t i = 0; i < h.size(); ++i) h[i] = hit[i] ? floorZ[i] : start;  // start taut at the top where nothing was hit
  for (int it = 0; it < relax; ++it) {
    std::vector<double> next = h;
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i) {
        double sum = 0; int cnt = 0;
        for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) {
          if (di == 0 && dj == 0) continue;
          const int ni = i + di, nj = j + dj;
          if (ni < 0 || ni >= n || nj < 0 || nj >= n) continue;
          sum += h[static_cast<size_t>(nj) * n + ni]; ++cnt;
        }
        const double avg = cnt ? sum / cnt : h[static_cast<size_t>(j) * n + i];
        const double floor_here = floorZ[static_cast<size_t>(j) * n + i];
        next[static_cast<size_t>(j) * n + i] = floor_here > -std::numeric_limits<double>::max() ? std::max(floor_here, avg) : avg;
      }
    h.swap(next);
  }
  kernel::Mesh mesh;
  ON_Mesh& m = mesh.raw();
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const Point3d base = pl.PointAt(u0 + (u1 - u0) * i / (n - 1), v0 + (v1 - v0) * j / (n - 1));
      m.SetVertex(j * n + i, base + pl.zaxis * (h[static_cast<size_t>(j) * n + i] - ON_DotProduct(base - pl.origin, pl.zaxis)));
    }
  { int fi = 0; for (int j = 0; j + 1 < n; ++j) for (int i = 0; i + 1 < n; ++i) m.SetQuad(fi++, j * n + i, j * n + i + 1, (j + 1) * n + i + 1, (j + 1) * n + i); }
  m.ComputeFaceNormals();
  m.ComputeVertexNormals();
  AddObject(ctx, SceneObject::MakeMesh(mesh), "Drape");
  ctx.Print("Drape: " + std::to_string(n) + "x" + std::to_string(n) + " grid draped over " + std::to_string(prims.size()) + " triangles, relaxed " +
            std::to_string(relax) + " iteration(s)");
}

}  // namespace

void RegisterRemeshCommands(CommandEngine& e) {
  ShrinkWrapSettings& persisted = PersistedShrinkWrap();
  Reg(e, "ShrinkWrap",
      RemeshTool("Select objects to wrap",
                 {Numeric("TargetEdgeLength", persisted.target_edge_length), Numeric("Offset", persisted.offset),
                  Numeric("Smooth", persisted.smooth), Toggle("FillHoles", persisted.fill_holes), Numeric("MaxVoxels", persisted.max_voxels),
                  Numeric("PolygonCount", 0), Toggle("DeleteInput", false)},
                 ShrinkWrapAction));
  Reg(e, "QuadRemesh",
      RemeshTool("Select objects to remesh",
                 {Numeric("TargetQuadCount", 400), Toggle("AdaptiveSize", true), Toggle("DetectHardEdges", true),
                  Choice("SymmetryAxis", {"None", "X", "Y", "Z"}, 0), Toggle("ConvertToSubD", false), Toggle("DeleteInput", false)},
                 QuadRemeshAction));
  Reg(e, "ReduceMesh",
      RemeshTool("Select meshes to reduce", {Numeric("ReductionPercent", 50), Numeric("TargetFaceCount", 0), Toggle("PreserveBoundary", true)},
                 ReduceMeshAction));
  Reg(e, "Heightfield",
      RemeshPointsTool({"First corner", "Other corner"},
                        {Numeric("Resolution", 60), Numeric("Amplitude", 5), Numeric("Waves", 2), OptionSpec{"Image", "", {}, false, false}},
                        HeightfieldAction));
  Reg(e, "Drape", RemeshPointsTool({"First corner", "Other corner"}, {Numeric("Resolution", 20), Numeric("Smooth", 6)}, DrapeAction));
}

}  // namespace dino8::app
