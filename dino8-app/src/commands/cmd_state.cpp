// Viewport, window, application-state and housekeeping commands: Cancel /
// Echo / Fullscreen, viewport management, Camera / lens, drag and snap
// switches, draw order, named selections and CPlanes, Gumball settings,
// options import/export, and the "no licences, no accounts" commands.
#include "commands/cmd_common.h"

#include <GLFW/glfw3.h>

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

#include "app/Settings.h"
#include "i18n/I18n.h"
#include "session/Digitizer.h"

namespace dino8::app {

namespace {

namespace fs = std::filesystem;

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

GLFWwindow* Window(CommandContext& ctx) { return static_cast<GLFWwindow*>(ctx.App().native_window); }

CommandFactory Toggle(std::function<bool&(CommandContext&)> get, const char* label) {
  return Immediate([get, label](CommandContext& ctx) { bool& b = get(ctx); b = !b; ctx.Print(std::string(label) + (b ? " on" : " off")); });
}

CommandFactory SetFlag(std::function<bool&(CommandContext&)> get, bool value, const char* label) {
  return Immediate([get, value, label](CommandContext& ctx) { get(ctx) = value; ctx.Print(std::string(label) + (value ? " on" : " off")); });
}

CommandFactory Say(const char* text, bool warn = false) {
  return Immediate([text, warn](CommandContext& ctx) { if (warn) ctx.Warn(text); else ctx.Print(text); });
}

const char* kFree = "Dino 8 is free software: no licenses, accounts or subscriptions.";
const char* kDigNotConnected = "No digitizer is connected. Use DigConnect (Protocol=Ascii for real hardware, File or Simulated to test headlessly), then this command reads its calibrated point stream.";

// DigBeep: a terminal bell for each digitized point, when enabled.
// Only actually rings the terminal bell outside script/headless mode - a
// raw '\a' byte on stdout would otherwise corrupt piped/redirected output
// (including this project's own smoke tests), the same reason OpenURL and
// FileExplorer skip their real OS side effect there too.
void DigBeep(CommandContext& ctx) { if (ctx.App().State().dig_beep && !ctx.App().headless && !ctx.ScriptMode()) std::fputc('\a', stdout); }

// Takes the next typed token, or prompts for text.
class TextArgCommand : public Command {
 public:
  TextArgCommand(std::string prompt, std::function<void(CommandContext&, const std::string&)> fn, std::optional<std::string> def = std::nullopt)
      : prompt_(std::move(prompt)), fn_(std::move(fn)), def_(std::move(def)) {}
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { fn_(ctx, *t); Finish(); return; }
    WantText(prompt_, def_);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { fn_(ctx, t); Finish(); }
  void OnEnter(CommandContext& ctx) override { if (def_) fn_(ctx, *def_); Finish(); }

 private:
  std::string prompt_;
  std::function<void(CommandContext&, const std::string&)> fn_;
  std::optional<std::string> def_;
};

// Takes the next typed number, or prompts for one (Enter keeps the default).
class NumberArgCommand : public Command {
 public:
  NumberArgCommand(std::string prompt, std::function<double(CommandContext&)> current, std::function<void(CommandContext&, double)> fn)
      : prompt_(std::move(prompt)), current_(std::move(current)), fn_(std::move(fn)) {}
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { char* e = nullptr; double v = std::strtod(t->c_str(), &e); if (e && *e == 0) fn_(ctx, v); else ctx.Warn("Expected a number, got '" + *t + "'"); Finish(); return; }
    WantNumber(prompt_, current_(ctx));
  }
  void OnNumber(CommandContext& ctx, double v) override { fn_(ctx, v); Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e = nullptr; double v = std::strtod(t.c_str(), &e); if (e && *e == 0) fn_(ctx, v); Finish(); }
  void OnEnter(CommandContext&) override { Finish(); }

 private:
  std::string prompt_;
  std::function<double(CommandContext&)> current_;
  std::function<void(CommandContext&, double)> fn_;
};

// Takes the next typed point, or prompts for one.
class PointArgCommand : public Command {
 public:
  PointArgCommand(std::string prompt, std::function<void(CommandContext&, Point3d)> fn) : prompt_(std::move(prompt)), fn_(std::move(fn)) {}
  void Begin(CommandContext&) override { WantPoint(prompt_); }
  void OnPoint(CommandContext& ctx, Point3d p) override { fn_(ctx, p); Finish(); }
  void OnEnter(CommandContext&) override { Finish(); }

 private:
  std::string prompt_;
  std::function<void(CommandContext&, Point3d)> fn_;
};

// A choice option: "DragMode CPlane" or a prompt with the choices as options.
class ChoiceCommand : public Command {
 public:
  ChoiceCommand(std::string label, std::vector<std::string> choices, std::function<std::string&(CommandContext&)> get)
      : label_(std::move(label)), choices_(std::move(choices)), get_(std::move(get)) {}
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { Apply(ctx, *t); Finish(); return; }
    options.clear();
    for (const std::string& c : choices_) options.push_back({c, "", {}, false, false});
    WantEnter(label_ + " <" + get_(ctx) + ">");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override { Apply(ctx, n); Finish(); }
  void OnText(CommandContext& ctx, const std::string& t) override { Apply(ctx, t); Finish(); }
  void OnEnter(CommandContext& ctx) override { ctx.Print(label_ + " = " + get_(ctx)); Finish(); }

 private:
  void Apply(CommandContext& ctx, const std::string& t) {
    for (const std::string& c : choices_) if (Lower(c) == Lower(t)) { get_(ctx) = c; ctx.Print(label_ + " = " + c); return; }
    ctx.Warn("Unknown " + label_ + " '" + t + "'");
  }
  std::string label_;
  std::vector<std::string> choices_;
  std::function<std::string&(CommandContext&)> get_;
};

std::string ViewportNames(CommandContext& ctx) {
  std::string s;
  for (auto& vp : ctx.Viewports()) s += (s.empty() ? "" : ", ") + vp->Name() + (vp->IsActive() ? " (active)" : "");
  return s;
}

void Activate(CommandContext& ctx, Viewport* target) {
  for (auto& vp : ctx.Viewports()) vp->SetActive(vp.get() == target);
  ctx.Print("Active viewport: " + target->Name());
}

// Activates the next viewport (after the active one) that passes `pred`.
void ActivateNext(CommandContext& ctx, const std::function<bool(const Viewport&)>& pred, const char* what) {
  auto& v = ctx.Viewports();
  size_t cur = 0;
  for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) cur = i;
  for (size_t k = 1; k <= v.size(); ++k) {
    Viewport* vp = v[(cur + k) % v.size()].get();
    if (pred(*vp)) { Activate(ctx, vp); return; }
  }
  ctx.Print(std::string("No ") + what + " viewport");
}

void GrowBox(kernel::BoundingBox& box, bool& has, const Point3d& p) {
  if (!has) { box.min = box.max = p; has = true; return; }
  box.min.x = std::min(box.min.x, p.x); box.min.y = std::min(box.min.y, p.y); box.min.z = std::min(box.min.z, p.z);
  box.max.x = std::max(box.max.x, p.x); box.max.y = std::max(box.max.y, p.y); box.max.z = std::max(box.max.z, p.z);
}

// ZoomEnds: naked curve end points - curve endpoints that don't coincide
// with the endpoint of another curve (an unjoined end).
bool NakedCurveEndsBox(const Document& doc, kernel::BoundingBox& box) {
  std::vector<Point3d> ends;
  for (const SceneObject& o : doc.Objects()) {
    if (o.kind != ObjectKind::Curve || !doc.IsObjectVisible(o) || !o.curve) continue;
    const kernel::Interval dom = o.curve->Domain();
    ends.push_back(o.curve->PointAt(dom.min));
    if (!o.curve->raw().IsClosed()) ends.push_back(o.curve->PointAt(dom.max));
  }
  const double tol = 1e-6;
  bool has = false;
  for (size_t i = 0; i < ends.size(); ++i) {
    int coincident = 0;
    for (size_t j = 0; j < ends.size(); ++j) if (i != j && (ends[i] - ends[j]).Length() < tol) ++coincident;
    if (coincident == 0) GrowBox(box, has, ends[i]);
  }
  return has;
}

// ZoomNaked: naked (unjoined, single-sided) surface/mesh edges, already
// computed for display in DisplayCache::naked_edges.
bool NakedEdgesBox(const Document& doc, kernel::BoundingBox& box) {
  bool has = false;
  for (const SceneObject& o : doc.Objects()) {
    if (!doc.IsObjectVisible(o)) continue;
    const std::vector<float>& e = o.Display().naked_edges;
    for (size_t i = 0; i + 2 < e.size(); i += 3) GrowBox(box, has, Point3d(e[i], e[i + 1], e[i + 2]));
  }
  return has;
}

// Non-manifold edge test on a mesh: any edge shared by more than two faces
// (mirrors cmd_select2.cpp's SelNonManifold test).
bool HasNonManifoldMeshEdge(const kernel::Mesh& m) {
  std::map<std::pair<int, int>, int> edges;
  const ON_Mesh& raw = m.raw();
  for (int fi = 0; fi < raw.m_F.Count(); ++fi) {
    const ON_MeshFace& f = raw.m_F[fi];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      int a = f.vi[k], b = f.vi[(k + 1) % n];
      if (a == b) continue;
      if (a > b) std::swap(a, b);
      if (++edges[{a, b}] > 2) return true;
    }
  }
  return false;
}

// ZoomNonManifold: mesh objects that have at least one non-manifold edge.
bool NonManifoldMeshesBox(const Document& doc, kernel::BoundingBox& box) {
  bool has = false;
  for (const SceneObject& o : doc.Objects()) {
    if (o.kind != ObjectKind::Mesh || !doc.IsObjectVisible(o) || !o.mesh || !HasNonManifoldMeshEdge(*o.mesh)) continue;
    const kernel::BoundingBox bb = o.BoundingBox();
    GrowBox(box, has, bb.min);
    GrowBox(box, has, bb.max);
  }
  return has;
}


// Saves the document to `path` without changing its path or modified state.
void SaveCopy(CommandContext& ctx, const std::string& path) {
  Document& doc = ctx.Doc();
  const std::string old_path = doc.Path();
  const bool modified = doc.Modified();
  std::string err;
  if (!ctx.App().SaveDocument(path, err)) ctx.Warn(err);
  doc.SetPath(old_path);
  doc.SetModified(modified);
}

// Camera: prints the active camera, previews its frustum (Show), and lets
// the user pick a new location and target.
class CameraCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    Describe(ctx, *vp);
    if (auto t = ctx.Engine().TakePendingInput()) { OnOption(ctx, *t, ""); if (finished) return; }
    options = {{"Show", "", {}, false, false}, {"Hide", "", {}, false, false}, {"Toggle", "", {}, false, false}};
    WantPoint("Camera location. Press Enter to keep the current camera");
    if (ctx.App().State().camera_shown) Frustum(ctx, *vp);
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    Viewport* vp = ctx.ActiveViewport();
    AppState& st = ctx.App().State();
    const std::string l = Lower(n);
    if (l == "show") st.camera_shown = true;
    else if (l == "hide") st.camera_shown = false;
    else if (l == "toggle") st.camera_shown = !st.camera_shown;
    else { Point3d p; if (std::sscanf(n.c_str(), "%lf,%lf,%lf", &p.x, &p.y, &p.z) == 3) { OnPoint(ctx, p); return; } ctx.Warn("Unknown Camera option '" + n + "'"); Finish(); return; }
    ctx.Print(std::string("Camera widget ") + (st.camera_shown ? "shown" : "hidden"));
    ctx.ClearPreview();
    if (st.camera_shown && vp) Frustum(ctx, *vp);
    Finish();
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    Viewport* vp = ctx.ActiveViewport();
    if (!vp) { Finish(); return; }
    CameraState& c = vp->GetCamera().State();
    if (!eye_) { eye_ = p; ctx.SetLastPoint(p); WantPoint("Camera target"); return; }
    if ((p - *eye_).Length() < 1e-9) { ctx.Warn("Target must differ from the camera location"); Finish(); return; }
    c.eye = *eye_;
    c.target = p;
    if (std::fabs((p - *eye_) * c.up) > 0.999 * (p - *eye_).Length()) c.up = Vector3d(0, 1, 0);
    ctx.ClearPreview();
    Describe(ctx, *vp);
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { ctx.ClearPreview(); Finish(); }
  void OnHover(CommandContext& ctx, Point3d h) override { if (eye_) { ctx.ClearPreview(); ctx.AddPreviewLine(*eye_, h); } }
  void OnCancel(CommandContext& ctx) override { ctx.ClearPreview(); }

 private:
  static void Describe(CommandContext& ctx, Viewport& vp) {
    const CameraState& c = vp.GetCamera().State();
    ctx.Print("Camera (" + vp.Name() + "): location " + FormatPoint(c.eye) + ", target " + FormatPoint(c.target) + ", distance " + FormatNumber((c.eye - c.target).Length()) +
              (c.perspective ? ", lens " + FormatNumber(c.lens_mm) + " mm (" + FormatNumber(2 * std::atan(18.0 / c.lens_mm) * 180 / ON_PI) + " deg)" : ", parallel projection, height " + FormatNumber(c.ortho_height)));
  }
  static void Frustum(CommandContext& ctx, Viewport& vp) {
    const Camera& cam = vp.GetCamera();
    const CameraState& c = cam.State();
    const double d = (c.target - c.eye).Length() * 0.5;
    const double h = c.perspective ? d * std::tan(std::atan(18.0 / c.lens_mm)) : c.ortho_height * 0.5;
    const double w = h * vp.Aspect();
    const Vector3d f = cam.Forward(), r = cam.Right(), u = cam.Up();
    const Point3d ctr = c.eye + f * d;
    const Point3d a = ctr + r * w + u * h, b = ctr - r * w + u * h, cc = ctr - r * w - u * h, dd = ctr + r * w - u * h;
    ctx.AddPreviewPolyline({a, b, cc, dd}, true);
    for (const Point3d& p : {a, b, cc, dd}) ctx.AddPreviewLine(c.eye, p);
    ctx.AddPreviewLine(c.eye, c.target);
    ctx.AddPreviewPoint(c.eye);
    ctx.AddPreviewPoint(c.target);
  }
  std::optional<Point3d> eye_;
};

// EarthAnchorPoint: model point, latitude and longitude (Enter defaults
// each remaining prompt to 0), stored in document user text. Rhino's own
// version also drives its Sun's azimuth/altitude solar calculator from
// this; Dino 8's Sun takes Azimuth/Altitude directly instead (see
// cmd_render.cpp), so this just records the georeference.
class EarthAnchorCommand : public Command {
 public:
  void Begin(CommandContext&) override { WantPoint("Earth anchor point (model location)"); }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    p_ = p;
    ctx.SetLastPoint(p);
    WantNumber("Latitude in degrees (-90 to 90)", 0.0);
  }
  void OnNumber(CommandContext& ctx, double v) override {
    if (!lat_) { lat_ = std::clamp(v, -90.0, 90.0); WantNumber("Longitude in degrees (-180 to 180)", 0.0); return; }
    lon_ = std::clamp(v, -180.0, 180.0);
    Store(ctx);
  }
  void OnText(CommandContext& ctx, const std::string& t) override { char* e = nullptr; double v = std::strtod(t.c_str(), &e); if (e && *e == 0) OnNumber(ctx, v); else Finish(); }
  void OnEnter(CommandContext& ctx) override {
    if (!p_) { Finish(); return; }
    if (!lat_) { lat_ = 0.0; WantNumber("Longitude in degrees (-180 to 180)", 0.0); return; }
    lon_ = 0.0;
    Store(ctx);
  }

 private:
  void Store(CommandContext& ctx) {
    ctx.Doc().UserText()["EarthAnchorPoint"] = FormatPoint(*p_);
    ctx.Doc().UserText()["EarthAnchorPointLatitude"] = FormatNumber(*lat_);
    ctx.Doc().UserText()["EarthAnchorPointLongitude"] = FormatNumber(*lon_);
    ctx.Doc().Touch();
    ctx.Print("Earth anchor point " + FormatPoint(*p_) + ", latitude " + FormatNumber(*lat_) + ", longitude " + FormatNumber(*lon_));
    Finish();
  }
  std::optional<Point3d> p_;
  std::optional<double> lat_, lon_;
};

// Dot: a text dot is a point object carrying its text.
class DotCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    if (auto t = ctx.Engine().TakePendingInput()) { text_ = *t; WantPoint("Location of text dot"); return; }
    WantText("Dot text", "Dot");
  }
  void OnText(CommandContext& ctx, const std::string& t) override {
    if (!text_) { text_ = t; WantPoint("Location of text dot"); return; }
    Point3d p;
    if (std::sscanf(t.c_str(), "%lf,%lf,%lf", &p.x, &p.y, &p.z) == 3) OnPoint(ctx, p);
  }
  void OnPoint(CommandContext& ctx, Point3d p) override {
    SceneObject o = SceneObject::MakePoint(p);
    o.name = text_.value_or("Dot");
    o.user_text["Dot"] = o.name;
    ObjectId id = AddObject(ctx, std::move(o), "Dot");
    ctx.Print("Dot '" + text_.value_or("Dot") + "' at " + FormatPoint(p) + " (object " + std::to_string(id) + ")");
    Finish();
  }

 private:
  std::optional<std::string> text_;
};

// Named selections / CPlanes: "NamedSelections Save name" / "Restore name" / "List".
class NamedSetCommand : public Command {
 public:
  explicit NamedSetCommand(bool cplanes) : cplanes_(cplanes) {}
  void Begin(CommandContext& ctx) override {
    options = {{"Save", "", {}, false, false}, {"Restore", "", {}, false, false}, {"Delete", "", {}, false, false}, {"List", "", {}, false, false}};
    if (auto t = ctx.Engine().TakePendingInput()) { OnOption(ctx, *t, ""); return; }
    WantEnter(std::string(cplanes_ ? "Named CPlanes" : "Named selections") + " (Save/Restore/Delete/List)");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    const std::string l = Lower(n);
    if (l == "list") { List(ctx); Finish(); return; }
    if (l != "save" && l != "restore" && l != "delete") { ctx.Warn("Unknown option '" + n + "'"); Finish(); return; }
    action_ = l;
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Name");
  }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (action_.empty()) { OnOption(ctx, name, ""); return; }
    Document& doc = ctx.Doc();
    if (cplanes_) {
      auto& list = doc.NamedCPlanes();
      auto it = std::find_if(list.begin(), list.end(), [&](const NamedCPlane& c) { return c.name == name; });
      Viewport* vp = ctx.ActiveViewport();
      if (action_ == "save" && vp) { NamedCPlane c{name, vp->CPlane().origin, vp->CPlane().x_axis, vp->CPlane().y_axis}; if (it != list.end()) *it = c; else list.push_back(c); doc.Touch(); ctx.Print("Named CPlane '" + name + "' saved"); }
      else if (action_ == "restore" && vp) { if (it == list.end()) ctx.Warn("No named CPlane '" + name + "'"); else { vp->CPlane().origin = it->origin; vp->CPlane().x_axis = it->x_axis; vp->CPlane().y_axis = it->y_axis; ctx.Print("CPlane '" + name + "' restored"); } }
      else if (action_ == "delete") { if (it == list.end()) ctx.Warn("No named CPlane '" + name + "'"); else { list.erase(it); doc.Touch(); ctx.Print("Named CPlane '" + name + "' deleted"); } }
    } else {
      auto& list = doc.NamedSelections();
      auto it = std::find_if(list.begin(), list.end(), [&](const NamedSelection& s) { return s.name == name; });
      if (action_ == "save") { NamedSelection s{name, doc.SelectedIds()}; if (it != list.end()) *it = s; else list.push_back(s); doc.Touch(); ctx.Print("Named selection '" + name + "' saved (" + std::to_string(s.ids.size()) + " object(s))"); }
      else if (action_ == "restore") { if (it == list.end()) ctx.Warn("No named selection '" + name + "'"); else { doc.SelectNone(); for (ObjectId id : it->ids) doc.Select(id, true); ctx.Print("Named selection '" + name + "' restored: " + std::to_string(doc.SelectedCount()) + " object(s) selected"); } }
      else if (action_ == "delete") { if (it == list.end()) ctx.Warn("No named selection '" + name + "'"); else { list.erase(it); doc.Touch(); ctx.Print("Named selection '" + name + "' deleted"); } }
    }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { List(ctx); Finish(); }

 private:
  void List(CommandContext& ctx) {
    if (cplanes_) { auto& l = ctx.Doc().NamedCPlanes(); ctx.Print(std::to_string(l.size()) + " named CPlane(s)"); for (auto& c : l) ctx.Print("  " + c.name + ": origin " + FormatPoint(c.origin)); }
    else { auto& l = ctx.Doc().NamedSelections(); ctx.Print(std::to_string(l.size()) + " named selection(s)"); for (auto& s : l) ctx.Print("  " + s.name + ": " + std::to_string(s.ids.size()) + " object(s)"); }
  }
  bool cplanes_;
  std::string action_;
};

// NamedPosition: "NamedPosition Save name" saves the current selection's
// geometry by object id; "Restore name" puts every still-existing one of
// those objects back exactly as it was, independent of Undo/Redo.
class NamedPositionCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    options = {{"Save", "", {}, false, false}, {"Restore", "", {}, false, false}, {"Delete", "", {}, false, false}, {"List", "", {}, false, false}};
    if (auto t = ctx.Engine().TakePendingInput()) { OnOption(ctx, *t, ""); return; }
    WantEnter("Named positions (Save/Restore/Delete/List)");
  }
  void OnOption(CommandContext& ctx, const std::string& n, const std::string&) override {
    const std::string l = Lower(n);
    if (l == "list") { List(ctx); Finish(); return; }
    if (l != "save" && l != "restore" && l != "delete") { ctx.Warn("Unknown option '" + n + "'"); Finish(); return; }
    action_ = l;
    if (auto t = ctx.Engine().TakePendingInput()) { OnText(ctx, *t); return; }
    WantText("Name");
  }
  void OnText(CommandContext& ctx, const std::string& name) override {
    if (action_.empty()) { OnOption(ctx, name, ""); return; }
    Document& doc = ctx.Doc();
    auto& list = doc.NamedPositions();
    auto it = std::find_if(list.begin(), list.end(), [&](const NamedPosition& p) { return p.name == name; });
    if (action_ == "save") {
      NamedPosition p{name, {}};
      for (ObjectId id : doc.SelectedIds()) if (const SceneObject* o = doc.Find(id)) p.objects.push_back(*o);
      if (p.objects.empty()) { ctx.Warn("NamedPosition Save: select objects first"); Finish(); return; }
      const size_t n = p.objects.size();
      if (it != list.end()) *it = std::move(p); else list.push_back(std::move(p));
      doc.Touch();
      ctx.Print("Named position '" + name + "' saved (" + std::to_string(n) + " object(s))");
    } else if (action_ == "restore") {
      if (it == list.end()) { ctx.Warn("No named position '" + name + "'"); Finish(); return; }
      int restored = 0;
      for (const SceneObject& saved : it->objects) if (SceneObject* o = doc.Find(saved.id)) { *o = saved; ++restored; }
      doc.Touch();
      ctx.Print("Named position '" + name + "' restored: " + std::to_string(restored) + " of " + std::to_string(it->objects.size()) + " object(s) still exist");
    } else if (action_ == "delete") {
      if (it == list.end()) ctx.Warn("No named position '" + name + "'");
      else { list.erase(it); doc.Touch(); ctx.Print("Named position '" + name + "' deleted"); }
    }
    Finish();
  }
  void OnEnter(CommandContext& ctx) override { List(ctx); Finish(); }

 private:
  void List(CommandContext& ctx) {
    auto& l = ctx.Doc().NamedPositions();
    ctx.Print(std::to_string(l.size()) + " named position(s)");
    for (auto& p : l) ctx.Print("  " + p.name + ": " + std::to_string(p.objects.size()) + " object(s)");
  }
  std::string action_;
};

// Draw order lives in user text; the renderer draws objects in document order.
CommandFactory DrawOrder(const char* label, int mode) {  // 0 clear, 1 front, 2 back, 3 forward, 4 backward
  return OnSelection("Select objects to change draw order", [label, mode](CommandContext& ctx, const std::vector<ObjectId>& ids) {
    int lo = 0, hi = 0;
    for (const SceneObject& o : ctx.Doc().Objects()) { auto it = o.user_text.find("DrawOrder"); if (it != o.user_text.end()) { int v = std::atoi(it->second.c_str()); lo = std::min(lo, v); hi = std::max(hi, v); } }
    ctx.Doc().BeginChange(label);
    for (ObjectId id : ids) {
      SceneObject* o = ctx.Doc().Find(id);
      if (!o) continue;
      auto it = o->user_text.find("DrawOrder");
      const int cur = it != o->user_text.end() ? std::atoi(it->second.c_str()) : 0;
      if (mode == 0) o->user_text.erase("DrawOrder");
      else o->user_text["DrawOrder"] = std::to_string(mode == 1 ? hi + 1 : mode == 2 ? lo - 1 : mode == 3 ? cur + 1 : cur - 1);
    }
    ctx.Doc().Touch();
    ctx.Print(std::string(label) + ": " + std::to_string(ids.size()) + " object(s)");
  });
}

CommandFactory GumballChoice(const char* label, std::vector<std::string> choices, std::function<std::string&(Gumball::Settings&)> get) {
  return Make<ChoiceCommand>(label, choices, [get](CommandContext& ctx) -> std::string& { return get(ctx.App().GetGumball().GetSettings()); });
}

CommandFactory OpenUrl(const char* label) {
  return Make<TextArgCommand>("URL", [label](CommandContext& ctx, const std::string& url) {
    ctx.Print(std::string(label) + ": " + url);
    if (ctx.App().headless || ctx.ScriptMode()) return;
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) { ctx.Warn("Only http(s) URLs are opened"); return; }
#if defined(_WIN32)
    const std::string cmd = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + url + "\"";
#else
    const std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
#endif
    if (url.find('"') == std::string::npos && std::system(cmd.c_str()) != 0) ctx.Warn("Could not open a browser");
  }, std::string("https://github.com/"));
}

}  // namespace

void RegisterStateCommands(CommandEngine& e) {
  // ---- command-line control -------------------------------------------
  Reg(e, "Cancel", Immediate([](CommandContext& ctx) { ctx.Print("Nothing to cancel (typing Cancel or pressing Esc while a command runs cancels it)"); }));
  Reg(e, "Enter", Immediate([](CommandContext&) {}));
  Reg(e, "EnterEnd", Immediate([](CommandContext&) {}));
  Reg(e, "Pause", Immediate([](CommandContext& ctx) { if (!ctx.ScriptMode()) ctx.Print("Pause: continuing"); }));
  Reg(e, "MultiPause", Immediate([](CommandContext& ctx) { if (!ctx.ScriptMode()) ctx.Print("MultiPause: continuing"); }));
  Reg(e, "Echo", SetFlag([](CommandContext& ctx) -> bool& { return ctx.App().State().echo; }, true, "Echo"));
  Reg(e, "NoEcho", SetFlag([](CommandContext& ctx) -> bool& { return ctx.App().State().echo; }, false, "Echo"));
  Reg(e, "SetRedrawOn", SetFlag([](CommandContext& ctx) -> bool& { return ctx.App().State().redraw; }, true, "Redraw"));
  Reg(e, "SetRedrawOff", SetFlag([](CommandContext& ctx) -> bool& { return ctx.App().State().redraw; }, false, "Redraw"));
  Reg(e, "Alerter", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().alerter; }, "Alerter (beep when a command finishes)"));
  Reg(e, "CommandPrompt", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().command_prompt; }, "Command prompt"), CommandStatus::Implemented, "Shows or hides the command-line window and reclaims its space for the viewports; commands still run from menus, toolbars, macros and scripts while hidden.");
  Reg(e, "DisplayCommandPrompt", SetFlag([](CommandContext& ctx) -> bool& { return ctx.App().State().command_prompt; }, true, "Command prompt"), CommandStatus::Implemented, "Shows the command-line window (see CommandPrompt).");
  Reg(e, "Run", Immediate([](CommandContext& ctx) { ctx.Engine().PendingInputs().clear(); ctx.Warn("Run: Dino 8 does not execute external programs from the command line."); }), CommandStatus::Partial, "Deliberately never implemented: Dino 8 does not launch arbitrary external programs from a typed command (a real Rhino Run would exec() whatever the user typed with no sandboxing). Run Lua via RunScript/\"= expr\", or a shell command from your own terminal.");
  Reg(e, "GetIssueState", Say("GetIssueState: ok - no issues reported"));
  Reg(e, "ResetMessageBoxes", Immediate([](CommandContext& ctx) { ctx.App().State().message_boxes_reset = true; ctx.Print("ResetMessageBoxes: all 'do not show again' choices cleared"); }));
  // Switches the UI language: menu bar, panel titles, status bar and common
  // dialogs re-render in the new language immediately (i18n::Tr is read
  // fresh every frame, nothing to rebuild). Accepts either a language code
  // ("en", "es") or that language's own display name ("English", "Espanol"),
  // case-insensitively. Persisted like any other Options > General setting.
  Reg(e, "SetLanguage", Make<TextArgCommand>("Language", [](CommandContext& ctx, const std::string& raw) {
        std::string want = Lower(raw);
        std::string matched_code;
        for (const auto& lang : i18n::AvailableLanguages()) {
          if (Lower(lang.code) == want || Lower(lang.name) == want) { matched_code = lang.code; break; }
        }
        if (matched_code.empty()) {
          std::string available;
          for (const auto& lang : i18n::AvailableLanguages()) available += (available.empty() ? "" : ", ") + lang.name + " (" + lang.code + ")";
          ctx.Warn("SetLanguage: unknown language '" + raw + "'. Available: " + available);
          return;
        }
        i18n::SetLanguage(matched_code);
        ctx.App().language = matched_code;
        ctx.Print("SetLanguage: " + matched_code);
      }));
  // Test-only diagnostic (not in commands.json, not on any menu): prints a
  // few i18n::Tr() lookups to the command history so tests/smoke.sh can
  // verify, without a screenshot, that (a) a language switch really changes
  // the translated text and (b) a key missing from the active language's
  // table falls back to English rather than to a blank string or the raw
  // key - es.json deliberately leaves out "panel.imgui_demo" (a low-visibility,
  // developer-only string) so this has a real partially-translated key to
  // exercise, and a key present in no table at all to prove the final
  // fallback (to the key itself) doesn't crash or blank out either.
  Reg(e, "I18nSelfTest", Immediate([](CommandContext& ctx) {
        ctx.Print("I18nSelfTest: active=" + i18n::CurrentLanguage());
        ctx.Print("I18nSelfTest: menu.file=" + i18n::Tr("menu.file"));
        ctx.Print("I18nSelfTest: fallback(panel.imgui_demo)=" + i18n::Tr("panel.imgui_demo"));
        ctx.Print("I18nSelfTest: unknown_key=" + i18n::Tr("this.key.does.not.exist.anywhere"));
      }));

  // ---- OS window ---------------------------------------------------------
  Reg(e, "Fullscreen", Immediate([](CommandContext& ctx) {
        GLFWwindow* w = Window(ctx);
        if (!w) { ctx.Warn("No window"); return; }
        static int saved[4] = {0, 0, 1600, 900};
        if (glfwGetWindowMonitor(w)) { glfwSetWindowMonitor(w, nullptr, saved[0], saved[1], saved[2], saved[3], 0); ctx.Print("Fullscreen off"); return; }
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = mon ? glfwGetVideoMode(mon) : nullptr;
        if (!mode) { ctx.Warn("No monitor"); return; }
        glfwGetWindowPos(w, &saved[0], &saved[1]);
        glfwGetWindowSize(w, &saved[2], &saved[3]);
        glfwSetWindowMonitor(w, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
        ctx.Print("Fullscreen on (run Fullscreen again to leave)");
      }));
  Reg(e, "Maximize", Immediate([](CommandContext& ctx) { if (GLFWwindow* w = Window(ctx)) { glfwMaximizeWindow(w); ctx.Print("Window maximized"); } }));
  Reg(e, "Minimize", Immediate([](CommandContext& ctx) { if (GLFWwindow* w = Window(ctx)) { if (ctx.App().headless) { ctx.Print("Minimize: skipped in headless mode"); return; } glfwIconifyWindow(w); ctx.Print("Window minimized"); } }));
  Reg(e, "Restore", Immediate([](CommandContext& ctx) { if (GLFWwindow* w = Window(ctx)) { glfwRestoreWindow(w); ctx.Print("Window restored"); } }));

  // ---- viewports ---------------------------------------------------------
  Reg(e, "SetActiveViewport", Make<TextArgCommand>("Viewport name", [](CommandContext& ctx, const std::string& name) {
        Viewport* vp = ctx.App().FindViewport(name);
        if (!vp) for (auto& v : ctx.Viewports()) if (Lower(v->Name()) == Lower(name)) vp = v.get();
        if (!vp) { ctx.Warn("No viewport '" + name + "'. Viewports: " + ViewportNames(ctx)); return; }
        Activate(ctx, vp);
      }));
  Reg(e, "CloseViewport", Immediate([](CommandContext& ctx) {
        auto& v = ctx.Viewports();
        if (v.size() <= 1) { ctx.Warn("Cannot close the last viewport"); return; }
        for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) {
          const std::string name = v[i]->Name();
          v.erase(v.begin() + static_cast<long>(i));
          v[std::min(i, v.size() - 1)]->SetActive(true);
          ctx.Print("Closed viewport " + name + ". Viewports: " + ViewportNames(ctx));
          return;
        }
      }));
  // NewFloatingViewport, SplitViewportHorizontal, SplitViewportVertical: the
  // real implementations live in cmd_viewtools.cpp (RegisterViewToolsCommands
  // runs after this file, so it always won here anyway; these stubs were
  // dead code - NewFloatingViewport really does float via Application::
  // AddViewport(..., /*floating=*/true), and Split* really docks a new
  // viewport beside the active one).
  Reg(e, "NextViewportToTop", Immediate([](CommandContext& ctx) { ActivateNext(ctx, [](const Viewport&) { return true; }, "other"); }));
  // BringViewportToTop: superseded by cmd_viewtools.cpp's implementation (see above).
  Reg(e, "PushViewportToBack", Immediate([](CommandContext& ctx) { ctx.Engine().Execute("PrevViewport"); }));
  Reg(e, "NextOrthoViewport", Immediate([](CommandContext& ctx) { ActivateNext(ctx, [](const Viewport& v) { return !v.GetCamera().State().perspective; }, "parallel"); }));
  Reg(e, "NextPerspectiveViewport", Immediate([](CommandContext& ctx) { ActivateNext(ctx, [](const Viewport& v) { return v.GetCamera().State().perspective; }, "perspective"); }));
  Reg(e, "SwapView", Immediate([](CommandContext& ctx) {
        auto& v = ctx.Viewports();
        if (v.size() < 2) { ctx.Warn("SwapView needs two viewports"); return; }
        size_t cur = 0;
        for (size_t i = 0; i < v.size(); ++i) if (v[i]->IsActive()) cur = i;
        Viewport& a = *v[cur];
        Viewport& b = *v[(cur + 1) % v.size()];
        CameraState ca = a.GetCamera().State(), cb = b.GetCamera().State();
        a.GetCamera().SetState(cb); b.GetCamera().SetState(ca);
        DisplayMode ma = a.Mode(); a.SetMode(b.Mode()); b.SetMode(ma);
        ctx.Print("Swapped views of " + a.Name() + " and " + b.Name());
      }), CommandStatus::Implemented, "Swaps the camera and display mode of the active viewport with the next one.");
  Reg(e, "OneView", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(1); ctx.Print("Single viewport layout"); }));
  // ToggleFloatingViewport: superseded by cmd_viewtools.cpp's implementation.
  Reg(e, "LockViewport", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        vp->SetViewLocked(!vp->ViewLocked());
        ctx.Print(std::string("Viewport lock ") + (vp->ViewLocked() ? "on" : "off") + " (" + vp->Name() + ")");
      }), CommandStatus::Implemented, "Freezes the active viewport's camera: mouse pan/orbit/dolly, the wheel and the arrow-key/Page Up/Down shortcuts stop changing it, but clicking and selecting objects still works. Per viewport, like Rhino's.");
  // SetMaximizedViewport: superseded by cmd_viewtools.cpp's implementation.
  // ViewportTabs: superseded by cmd_viewtools.cpp's implementation.
  Reg(e, "ZoomEnds", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        kernel::BoundingBox box;
        if (!NakedCurveEndsBox(ctx.Doc(), box)) { ctx.Warn("ZoomEnds: no naked curve ends found"); return; }
        vp->ZoomTo(box);
        ctx.Print("ZoomEnds: zoomed to every unjoined curve end point");
      }), CommandStatus::Implemented, "Zooms to curve endpoints that don't coincide with another curve's endpoint (unjoined ends).");
  Reg(e, "ZoomNaked", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        kernel::BoundingBox box;
        if (!NakedEdgesBox(ctx.Doc(), box)) { ctx.Warn("ZoomNaked: no naked edges found"); return; }
        vp->ZoomTo(box);
        ctx.Print("ZoomNaked: zoomed to every naked surface/mesh edge (see ShowEdges)");
      }), CommandStatus::Implemented, "Zooms to every naked (single-sided) surface/mesh/polysurface edge.");
  Reg(e, "ZoomNonManifold", Immediate([](CommandContext& ctx) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        kernel::BoundingBox box;
        if (!NonManifoldMeshesBox(ctx.Doc(), box)) { ctx.Warn("ZoomNonManifold: no non-manifold meshes found"); return; }
        vp->ZoomTo(box);
        ctx.Print("ZoomNonManifold: zoomed to every mesh with a non-manifold edge (see SelNonManifold)");
      }), CommandStatus::Implemented, "Zooms to meshes that have at least one edge shared by more than two faces.");
  // Zoom1To1Calibrate, SetZoomExtentsBorder: superseded by cmd_viewtools.cpp's implementations.

  // ---- camera / lens ---------------------------------------------------
  Reg(e, "Camera", Make<CameraCommand>());
  Reg(e, "PerspectiveAngle", Make<NumberArgCommand>("Lens angle in degrees", [](CommandContext& ctx) { Viewport* vp = ctx.ActiveViewport(); return vp ? 2 * std::atan(18.0 / vp->GetCamera().State().lens_mm) * 180 / ON_PI : 40.0; }, [](CommandContext& ctx, double deg) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        deg = std::clamp(deg, 1.0, 170.0);
        CameraState& c = vp->GetCamera().State();
        c.lens_mm = 18.0 / std::tan(deg * ON_PI / 360.0);
        ctx.App().State().perspective_angle = deg;
        ctx.Print("Perspective angle " + FormatNumber(deg) + " deg (lens " + FormatNumber(c.lens_mm) + " mm)" + (c.perspective ? "" : " - stored; " + vp->Name() + " is a parallel view"));
      }));
  Reg(e, "ZoomLens", Make<NumberArgCommand>("Lens length in mm", [](CommandContext& ctx) { Viewport* vp = ctx.ActiveViewport(); return vp ? vp->GetCamera().State().lens_mm : 50.0; }, [](CommandContext& ctx, double mm) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        CameraState& c = vp->GetCamera().State();
        c.lens_mm = std::clamp(mm, 5.0, 1000.0);
        ctx.Print("Lens " + FormatNumber(c.lens_mm) + " mm" + (c.perspective ? "" : " - stored; " + vp->Name() + " is a parallel view"));
      }));
  Reg(e, "DollyZoom", Make<NumberArgCommand>("New lens length in mm (the target keeps its size)", [](CommandContext& ctx) { Viewport* vp = ctx.ActiveViewport(); return vp ? vp->GetCamera().State().lens_mm : 50.0; }, [](CommandContext& ctx, double mm) {
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        CameraState& c = vp->GetCamera().State();
        mm = std::clamp(mm, 5.0, 1000.0);
        Vector3d d = c.eye - c.target;
        d = d * (mm / c.lens_mm);
        c.eye = c.target + d;
        c.lens_mm = mm;
        ctx.Print("DollyZoom: lens " + FormatNumber(mm) + " mm, distance " + FormatNumber(d.Length()));
      }), CommandStatus::Implemented, "Type the new lens length; recomputes the camera distance so the target keeps its on-screen size (the 'Vertigo' effect).");

  // ---- snaps / drag ----------------------------------------------------
  Reg(e, "SetOrtho", Immediate([](CommandContext& ctx) { ctx.Snaps().ortho = true; ctx.Settings().ortho = true; ctx.Print("Ortho on (angle " + FormatNumber(ctx.App().State().ortho_angle) + " deg)"); }));
  Reg(e, "OrthoAngle", Make<NumberArgCommand>("Ortho angle in degrees", [](CommandContext& ctx) { return ctx.App().State().ortho_angle; }, [](CommandContext& ctx, double v) {
        ctx.App().State().ortho_angle = std::clamp(v, 1.0, 180.0);
        ctx.Snaps().ortho_angle_deg = ctx.App().State().ortho_angle;
        ctx.Print("Ortho angle = " + FormatNumber(ctx.App().State().ortho_angle) + " deg");
      }), CommandStatus::Implemented, "Sets the angle step Ortho constrains to (default every 90 degrees); any step from 1 to 180 works.");
  Reg(e, "SnapSize", Make<NumberArgCommand>("Grid snap size", [](CommandContext& ctx) { return ctx.Settings().grid_spacing; }, [](CommandContext& ctx, double v) { if (v > 0) { ctx.Settings().grid_spacing = v; ctx.Print("Grid snap size = " + FormatNumber(v)); } }));
  Reg(e, "SetSnap", Make<NumberArgCommand>("Grid snap size", [](CommandContext& ctx) { return ctx.Settings().grid_spacing; }, [](CommandContext& ctx, double v) { if (v > 0) { ctx.Settings().grid_spacing = v; ctx.Snaps().grid_snap = true; ctx.Print("Grid snap on, size = " + FormatNumber(v)); } }));
  Reg(e, "OrthoSnapToCPlaneZ", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().ortho_snap_to_cplane_z; }, "Ortho snap to CPlane Z"), CommandStatus::Implemented, "While Ortho is on, also offers the CPlane's vertical (Z) direction from the last point as a constraint, alongside the in-plane ortho directions.");
  Reg(e, "SnapToLocked", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().snap_to_locked; }, "Snap to locked objects"), CommandStatus::Implemented, "Off excludes locked objects from every object snap.");
  Reg(e, "SnapToOccluded", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().snap_to_occluded; }, "Snap to occluded objects"), CommandStatus::Implemented, "Off excludes snap candidates that are hidden behind another object along the line of sight (a real depth test against every visible object's bounding box).");
  Reg(e, "SnapToMeshes", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().snap_to_meshes; }, "Snap to meshes"), CommandStatus::Implemented, "Off excludes mesh vertices and edges from End/Vertex/Near object snaps.");
  Reg(e, "SnapToMeshObject", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().snap_to_mesh_object; }, "Snap to mesh objects"), CommandStatus::Implemented, "Off excludes mesh objects from object snaps entirely (overrides SnapToMeshes).");
  Reg(e, "SnapToSubDObject", Toggle([](CommandContext& ctx) -> bool& { return ctx.Snaps().snap_to_subd_object; }, "Snap to SubD objects"), CommandStatus::Implemented, "Off excludes SubD control-net vertices from object snaps.");
  Reg(e, "ShowOsnap", Immediate([](CommandContext& ctx) { bool& b = ctx.App().Panels().object_snaps; b = !b; ctx.Print(std::string("Osnap panel ") + (b ? "shown" : "hidden")); }));
  Reg(e, "DragMode", Make<ChoiceCommand>("DragMode", std::vector<std::string>{"CPlane", "World", "UVN", "View", "ControlPolygon"}, [](CommandContext& ctx) -> std::string& { return ctx.App().State().drag_mode; }), CommandStatus::Implemented, "Sets the plane the gumball's centre (Free) handle drags in: CPlane and World are exact; UVN and ControlPolygon (which need a surface/mesh control point, not a whole-object gumball) fall back to the view-perpendicular plane, same as View.");
  Reg(e, "DragStrength", Make<NumberArgCommand>("Drag strength percent", [](CommandContext& ctx) { return ctx.App().State().drag_strength; }, [](CommandContext& ctx, double v) { ctx.App().State().drag_strength = std::clamp(v, 1.0, 100.0); ctx.Print("Drag strength = " + FormatNumber(ctx.App().State().drag_strength) + "%"); }), CommandStatus::Implemented, "Scales every gumball translate drag (Free or an axis handle) by this percentage - 50% moves the selection half as far as the mouse.");
  Reg(e, "DragCopy", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().drag_copy; }, "Drag copy"), CommandStatus::Implemented, "When on, a gumball translate drag leaves a copy at the start position and moves the original; Alt inverts this for one drag.");
  Reg(e, "RememberCopyOptions", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().remember_copy_options; }, "Remember copy options"), CommandStatus::Implemented, "When on, whichever DragCopy behaviour a gumball drag actually used (including an Alt override) becomes the new DragCopy default for the next drag.");

  // ---- panels / UI -----------------------------------------------------
  Reg(e, "ToggleRightSidebar", Immediate([](CommandContext& ctx) { AppState& s = ctx.App().State(); s.right_sidebar = !s.right_sidebar; ctx.App().Panels().layers = s.right_sidebar; ctx.App().Panels().properties = s.right_sidebar; ctx.Print(std::string("Right sidebar (Layers, Properties) ") + (s.right_sidebar ? "shown" : "hidden")); }));
  Reg(e, "ShowToolbar", Immediate([](CommandContext& ctx) { ctx.App().Panels().toolbars = true; ctx.Print("Toolbar shown"); }));
  Reg(e, "ToolbarLock", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().toolbar_lock; }, "Toolbar lock"), CommandStatus::Implemented, "While on, the Standard toolbar's right-click customize menu (remove a button, add one from Options) is disabled; the toolbar itself is always fixed in place either way.");
  Reg(e, "Commands", Immediate([](CommandContext& ctx) { ctx.App().Panels().command_list = true; }));
  Reg(e, "PopupMenu", Immediate([](CommandContext& ctx) { ctx.App().OpenPopupToolbar(); }), CommandStatus::Implemented, "Opens the same popup icon grid a middle mouse click on a viewport does (Application::OpenPopupToolbar).");
  Reg(e, "PopupPopular", Immediate([](CommandContext& ctx) { ctx.Print("Recent commands:"); for (const std::string& n : ctx.Engine().RecentCommands()) ctx.Print("  " + n); ctx.App().Panels().command_list = true; }), CommandStatus::Implemented, "Prints the most recently used commands and opens the command list, which is sorted the same way.");
  Reg(e, "Menus", Say("Menus: File, Edit, View, Curve, Surface, Solid, Mesh, Dimension, Transform, Tools, Analyze, Render, Panels, Help - always shown in the menu bar."));
  Reg(e, "Macros", Immediate([](CommandContext& ctx) { ctx.App().Panels().macro_editor = true; }));
  Reg(e, "OptionsPage", Immediate([](CommandContext& ctx) { ctx.App().Panels().options = true; }));
  Reg(e, "PropertiesPage", Immediate([](CommandContext& ctx) { ctx.App().Panels().properties = true; }));
  Reg(e, "DocumentPropertiesPage", Immediate([](CommandContext& ctx) { ctx.App().Panels().document_properties = true; }));
  Reg(e, "Reset", Immediate([](CommandContext& ctx) {
        Application& app = ctx.App();
        app.Snaps() = SnapSettings{};
        app.Panels() = PanelState{};
        app.State() = AppState{};
        app.gumball_enabled = true;
        app.toolbar_commands.clear();
        app.curve_display_tolerance = 0.02;
        app.surface_display_tolerance = 0.05;
        app.GetGumball().GetSettings() = Gumball::Settings{};
        ctx.Print("Reset: snaps, panels, toolbar and preferences restored to defaults (recent files and theme kept)");
      }), CommandStatus::Implemented, "Restores snaps, panels, toolbar and the gumball to their defaults; keeps recent files, theme and window layout as the name promises.");
  Reg(e, "OptionsExport", Make<TextArgCommand>("Options file to write", [](CommandContext& ctx, const std::string& path) {
        std::string p = path;
        if (fs::path(p).extension().empty()) p += ".json";
        if (SaveSettingsTo(p, ctx.App(), ctx.App().ui_scale)) ctx.Print("Options exported to " + p); else ctx.Warn("Could not write " + p);
      }));
  Reg(e, "OptionsImport", Make<TextArgCommand>("Options file to read", [](CommandContext& ctx, const std::string& path) {
        float scale = ctx.App().ui_scale;
        if (LoadSettingsFrom(path, ctx.App(), scale)) ctx.Print("Options imported from " + path); else ctx.Warn("Could not read " + path);
      }));

  // ---- files -------------------------------------------------------------
  Reg(e, "SaveACopy", Immediate([](CommandContext& ctx) {
        if (auto p = ctx.Engine().TakePendingInput()) { SaveCopy(ctx, *p); return; }
        Application& app = ctx.App();
        app.ShowFileDialog("Save a copy", {".3dm"}, true, [&app](const std::string& path) {
          Document& doc = app.Doc();
          const std::string old = doc.Path(); const bool mod = doc.Modified();
          std::string err;
          if (!app.SaveDocument(path, err)) app.Notify(err);
          doc.SetPath(old); doc.SetModified(mod);
        });
      }));
  Reg(e, "SetWorkingFolder", Make<TextArgCommand>("Working folder", [](CommandContext& ctx, const std::string& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) { ctx.Warn("Not a folder: " + dir); return; }
        ctx.App().State().working_folder = dir;
        ctx.Print("Working folder: " + dir);
      }));
  Reg(e, "Autosave", Immediate([](CommandContext& ctx) {
        const std::string p = (fs::path(ConfigDirectory()) / "autosave.3dm").string();
        SaveCopy(ctx, p);
        ctx.Print("Autosave: " + p);
      }));
  Reg(e, "FileExplorer", Immediate([](CommandContext& ctx) {
        std::string dir = ctx.App().State().working_folder;
        if (dir.empty() && !ctx.Doc().Path().empty()) dir = fs::path(ctx.Doc().Path()).parent_path().string();
        if (dir.empty()) dir = fs::current_path().string();
        ctx.Print("FileExplorer: " + dir);
        if (ctx.App().headless || ctx.ScriptMode()) return;
        if (dir.find('"') != std::string::npos) { ctx.Warn("Could not open the file manager (path contains a quote)"); return; }
#if defined(_WIN32)
        const std::string cmd = "explorer \"" + dir + "\"";
#elif defined(__APPLE__)
        const std::string cmd = "open \"" + dir + "\"";
#else
        const std::string cmd = "xdg-open \"" + dir + "\" >/dev/null 2>&1 &";
#endif
        if (std::system(cmd.c_str()) != 0) ctx.Warn("Could not open the OS file manager");
      }), CommandStatus::Implemented, "Opens the OS file manager (Explorer/Finder/xdg-open) at the working folder, the current document's folder, or the current directory, in that order; only prints the folder in headless/script mode.");
  Reg(e, "OpenURL", OpenUrl("OpenURL"));
  Reg(e, "WebBrowser", OpenUrl("WebBrowser"));
  Reg(e, "Hyperlink", Make<TextArgCommand>("Hyperlink URL", [](CommandContext& ctx, const std::string& url) {
        std::vector<ObjectId> ids = ctx.Selected();
        if (ids.empty()) { ctx.Warn("Select objects first"); return; }
        ctx.Doc().BeginChange("Hyperlink");
        for (ObjectId id : ids) if (SceneObject* o = ctx.Doc().Find(id)) o->user_text["Hyperlink"] = url;
        ctx.Doc().Touch();
        ctx.Print("Hyperlink '" + url + "' set on " + std::to_string(ids.size()) + " object(s)");
      }));
  Reg(e, "SetDocumentUserText", Immediate([](CommandContext& ctx) {
        auto k = ctx.Engine().TakePendingInput();
        auto v = ctx.Engine().TakePendingInput();
        if (!k) { ctx.Print(std::to_string(ctx.Doc().UserText().size()) + " document user text key(s):"); for (auto& kv : ctx.Doc().UserText()) ctx.Print("  " + kv.first + " = " + kv.second); ctx.App().Panels().document_user_text = true; return; }
        if (!v) { ctx.Doc().UserText().erase(*k); ctx.Print("Document user text '" + *k + "' removed"); }
        else { ctx.Doc().UserText()[*k] = *v; ctx.Print("Document user text " + *k + " = " + *v); }
        ctx.Doc().Touch();
      }));
  Reg(e, "ModelBasepoint", Make<PointArgCommand>("Model base point", [](CommandContext& ctx, Point3d p) { ctx.Doc().UserText()["ModelBasepoint"] = FormatPoint(p); ctx.Doc().Touch(); ctx.Print("Model base point " + FormatPoint(p)); }));
  Reg(e, "EarthAnchorPoint", Make<EarthAnchorCommand>(), CommandStatus::Implemented, "Stores the model point, latitude and longitude in document user text (EarthAnchorPoint/Latitude/Longitude). Dino 8's Sun takes Azimuth/Altitude directly rather than deriving them from this, unlike Rhino's solar calculator.");

  // ---- objects -------------------------------------------------------------
  Reg(e, "Dot", Make<DotCommand>());
  Reg(e, "PointCloud", OnSelection("Select points for the point cloud", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<ObjectId> pts;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id); o && o->kind == ObjectKind::Point) pts.push_back(id);
        if (pts.empty()) { ctx.Warn("Select point objects"); return; }
        ctx.Doc().BeginChange("PointCloud");
        ctx.Doc().CreateGroup(pts, "PointCloud");
        ctx.Print("PointCloud: " + std::to_string(pts.size()) + " point(s) grouped");
      }), CommandStatus::Partial, "Dino 8's ObjectKind enum has no distinct point-cloud kind (only Point/Curve/Surface/Brep/Mesh/SubD), so there is no per-point-color/density point-cloud object to build here - this groups the selected Point objects instead, the closest honest approximation with the data model as it stands.");
  Reg(e, "InfinitePlane", Immediate([](CommandContext& ctx) {
        ON_Plane pl = ActivePlane(ctx);
        const double s = std::max(1.0, ctx.Settings().grid_spacing * ctx.Settings().grid_extents) * 20.0;
        std::vector<Point3d> grid = {pl.PointAt(-s, -s), pl.PointAt(s, -s), pl.PointAt(-s, s), pl.PointAt(s, s)};
        SceneObject o = SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
        o.name = "InfinitePlane";
        AddObject(ctx, std::move(o), "InfinitePlane");
        ctx.Print("InfinitePlane: " + FormatNumber(2 * s) + " x " + FormatNumber(2 * s) + " plane on the CPlane");
      }), CommandStatus::Implemented, "Creates a real (finite, but very large - 20x the grid extents) plane surface on the CPlane; Rhino's own 'infinite' plane is drawn, not modeled, the same practical limit as here.");
  Reg(e, "BringToFront", DrawOrder("BringToFront", 1));
  Reg(e, "SendToBack", DrawOrder("SendToBack", 2));
  Reg(e, "BringForward", DrawOrder("BringForward", 3));
  Reg(e, "SendBackward", DrawOrder("SendBackward", 4));
  Reg(e, "ClearDrawOrder", DrawOrder("ClearDrawOrder", 0));
  Reg(e, "NamedSelections", Make<NamedSetCommand>(false));
  Reg(e, "NamedCPlane", Make<NamedSetCommand>(true));
  Reg(e, "NamedPosition", Make<NamedPositionCommand>(), CommandStatus::Implemented, "Save/Restore/Delete/List named snapshots of the current selection's own geometry, by object id - independent of Undo/Redo, like Snapshots but scoped to the selection instead of the whole document.");
  // Snapshots, Worksession/LimitReferenceModel and the Dig* digitizer
  // commands are registered by RegisterSessionCommands (cmd_session.cpp),
  // called after this function.
  Reg(e, "HistoryPurge", Say("HistoryPurge: no construction history is recorded; nothing to purge."));
  Reg(e, "HistoryUpdate", Say("HistoryUpdate: no construction history is recorded; nothing to update."));
  // Worksession/LimitReferenceModel: superseded by cmd_session.cpp's real
  // implementations (RegisterSessionCommands runs after this file, so it
  // always wins here anyway; these stubs were dead code).
  // Bounce: superseded by cmd_solidtools.cpp's real ray-bounce implementation
  // (RegisterSolidToolsCommands runs after this file, so it always won here
  // anyway; this stub was dead code that misreported Bounce as unimplemented).
  Reg(e, "ContentFilter", Immediate([](CommandContext& ctx) {
        std::string& f = ctx.App().State().content_filter;
        if (auto p = ctx.Engine().TakePendingInput()) f = (*p == "\"\"" || Lower(*p) == "clear") ? std::string() : *p;
        ctx.App().Panels().materials = true;
        ctx.Print(std::string("ContentFilter: ") + (f.empty() ? "off (showing every entry)" : ("'" + f + "' (Materials and Textures panels; ContentFilter Clear to remove)")));
      }), CommandStatus::Implemented, "Filters the Materials and Textures panels' lists to names containing this text (case-insensitive); the Environments panel is a single set of document-wide settings, not a list, so there is nothing there to filter by name.");

  // ---- gumball ------------------------------------------------------------
  Reg(e, "GumballAlignment", GumballChoice("GumballAlignment", {"CPlane", "World", "Object"}, [](Gumball::Settings& s) -> std::string& { return s.alignment; }), CommandStatus::Implemented, "Sets the widget's own drag/rotate/scale axes: World (identity), CPlane (the active viewport's construction plane), or Object (a single selected curve's start tangent or surface's normal, falling back to World otherwise).");
  Reg(e, "GumballScaleMode", GumballChoice("GumballScaleMode", {"Independent", "Uniform"}, [](Gumball::Settings& s) -> std::string& { return s.scale_mode; }), CommandStatus::Implemented, "Sets the default for a scale-handle drag: Independent (one axis) or Uniform (all three); Shift while dragging temporarily switches to the other mode.");
  Reg(e, "GumballAutoReset", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().GetGumball().GetSettings().auto_reset; }, "Gumball auto reset"));
  Reg(e, "GumballDynamicRelocate", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().GetGumball().GetSettings().dynamic_relocate; }, "Gumball dynamic relocate"), CommandStatus::Implemented, "While on, Ctrl-dragging the gumball's centre handle moves the widget itself (like GumballRelocate) instead of the selection; nothing in the document changes.");
  Reg(e, "GumballRelocate", Make<PointArgCommand>("New gumball origin", [](CommandContext& ctx, Point3d p) { Gumball::Settings& s = ctx.App().GetGumball().GetSettings(); s.relocated = true; s.relocated_origin = p; ctx.Print("Gumball origin " + FormatPoint(p)); }), CommandStatus::Implemented, "The widget now draws and drags from this point instead of the selection's centre, until GumballAutoReset forgets it after the next transform (or GumballReset clears it now).");
  Reg(e, "GumballReset", Immediate([](CommandContext& ctx) { Gumball::Settings& s = ctx.App().GetGumball().GetSettings(); s.relocated = false; s.alignment = "CPlane"; ctx.Print("Gumball reset"); }));
  Reg(e, "GumballSettings", Immediate([](CommandContext& ctx) {
        const Gumball::Settings& s = ctx.App().GetGumball().GetSettings();
        ctx.Print(std::string("Gumball: ") + (ctx.App().gumball_enabled ? "on" : "off") + ", alignment " + s.alignment + ", scale " + s.scale_mode + ", auto reset " + (s.auto_reset ? "on" : "off") +
                  ", dynamic relocate " + (s.dynamic_relocate ? "on" : "off") + (s.relocated ? ", origin " + FormatPoint(s.relocated_origin) : ""));
        ctx.App().Panels().options = true;
      }));

  // ---- no licences, accounts or subscriptions (product rule) ------------
  for (const char* n : {"CheckInLicense", "CheckOutLicense", "Login", "Logout", "Libraries", "DownloadLibraryTextures"}) Reg(e, n, Say(kFree));

  // ---- plug-ins, Grasshopper: registered for real in cmd_flow.cpp
  // (RegisterFlowCommands, run last in Application::RegisterCommands) -------
  // DigConnect/DigDisconnect/DigCalibrate/DigScale/DigPause/DigResume/DigPoint/
  // DigListPorts/DigStatus have real implementations in cmd_session.cpp
  // (session/Digitizer.h - a real serial port, or Protocol=File/Simulated
  // headless stand-ins for testing without hardware), registered after
  // this file so they always win over the stubs that used to live here.
  // The remaining Dig* commands below build directly on top of a
  // *connected* digitizer's calibrated, unit-scaled point stream and have
  // no headless equivalent of their own: each is Partial for exactly the
  // reason DigPoint itself is (see cmd_session.cpp) - there is no digitizer
  // plugged into this environment, connected or simulated, by default.
  Reg(e, "Digitize", Immediate([](CommandContext& ctx) {
        Digitizer& d = Digitizer::Instance();
        if (!d.Connected()) { ctx.Warn(kDigNotConnected); return; }
        DigitizerPoint p;
        if (!d.ReadPoint(p)) { ctx.Warn("Digitize: no point available from the digitizer"); return; }
        DigBeep(ctx);
        AddObject(ctx, SceneObject::MakePoint(d.ToModel(p.raw)), "Digitize");
        ctx.Print("Digitize: digitized " + FormatPoint(d.ToModel(p.raw)));
      }), CommandStatus::Partial, kDigNotConnected);
  Reg(e, "DigCamera", Immediate([](CommandContext& ctx) {
        Digitizer& d = Digitizer::Instance();
        if (!d.Connected()) { ctx.Warn(kDigNotConnected); return; }
        DigitizerPoint eye, target;
        if (!d.ReadPoint(eye) || !d.ReadPoint(target)) { ctx.Warn("DigCamera: needs two points (eye, target) from the digitizer"); return; }
        Viewport* vp = ctx.ActiveViewport();
        if (!vp) return;
        DigBeep(ctx);
        vp->GetCamera().State().eye = d.ToModel(eye.raw);
        vp->GetCamera().State().target = d.ToModel(target.raw);
        ctx.Print("DigCamera: camera set from two digitized points");
      }), CommandStatus::Partial, kDigNotConnected);
  Reg(e, "DigClick", Immediate([](CommandContext& ctx) {
        Digitizer& d = Digitizer::Instance();
        if (!d.Connected()) { ctx.Warn(kDigNotConnected); return; }
        DigitizerPoint p;
        if (!d.ReadPoint(p)) { ctx.Warn("DigClick: no point available from the digitizer"); return; }
        DigBeep(ctx);
        const Point3d model = d.ToModel(p.raw);
        ctx.Print("DigClick: digitized " + FormatPoint(model));
        ctx.Engine().FeedPoint(model);
      }), CommandStatus::Partial,
      "No digitizer is connected (see the note on Digitize). Note also that unlike a real digitizer's hardware button, DigClick here is just another typed command name: this engine feeds any typed line to whatever command is already running, so DigClick can only ever fire when *no* other command is waiting for a point - it cannot interrupt one the way a real digitizer's stylus click would. It still prints and forwards the point it reads via CommandEngine::FeedPoint for whenever a future point-prompting command checks right after it runs.");
  Reg(e, "DigLine", Immediate([](CommandContext& ctx) {
        Digitizer& d = Digitizer::Instance();
        if (!d.Connected()) { ctx.Warn(kDigNotConnected); return; }
        DigitizerPoint a, b;
        if (!d.ReadPoint(a) || !d.ReadPoint(b)) { ctx.Warn("DigLine: needs two points from the digitizer"); return; }
        DigBeep(ctx);
        AddCurve(ctx, PolylineCurve({d.ToModel(a.raw), d.ToModel(b.raw)}), "DigLine");
        ctx.Print("DigLine: line digitized");
      }), CommandStatus::Partial, kDigNotConnected);
  auto dig_polyline = [](const char* name) {
    return Immediate([name](CommandContext& ctx) {
      Digitizer& d = Digitizer::Instance();
      if (!d.Connected()) { ctx.Warn(kDigNotConnected); return; }
      std::vector<Point3d> pts;
      DigitizerPoint p;
      while (pts.size() < 5000 && d.ReadPoint(p)) { DigBeep(ctx); pts.push_back(d.ToModel(p.raw)); }
      if (pts.size() < 2) { ctx.Warn(std::string(name) + ": needs at least two digitized points"); return; }
      AddCurve(ctx, PolylineCurve(pts), name);
      ctx.Print(std::string(name) + ": " + std::to_string(pts.size()) + " point(s) digitized into a curve");
    });
  };
  Reg(e, "DigSection", dig_polyline("DigSection"), CommandStatus::Partial, kDigNotConnected);
  Reg(e, "DigSketch", dig_polyline("DigSketch"), CommandStatus::Partial, kDigNotConnected);
  Reg(e, "DigBeep", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().dig_beep; }, "DigBeep"), CommandStatus::Partial,
      "Stored flag: when on, Digitize/DigCamera/DigClick/DigLine/DigSection/DigSketch print a terminal bell (\\a) for each digitized point - a real, if minimal, stand-in for the audible beep real digitizer hardware would make. Still Partial because none of those commands can ever fire without a connected digitizer.");

  // ---- clipboard captures --------------------------------------------------
  auto capture = [](const char* label) {
    return Immediate([label](CommandContext& ctx) {
      Viewport* vp = ctx.ActiveViewport();
      if (!vp) return;
      const std::string p = (fs::path(ConfigDirectory()) / "clipboard.bmp").string();
      std::string err;
      if (vp->CaptureToFile(p, err)) ctx.Print(std::string(label) + ": image written to " + p + " (system clipboard images are planned)"); else ctx.Warn(err);
    });
  };
  Reg(e, "ViewCaptureToClipboard", capture("ViewCaptureToClipboard"), CommandStatus::Partial, "There is no real system-clipboard image write here (that needs a platform-specific API - X11/Wayland selection ownership, the Win32 or Cocoa clipboard - which this GLFW-based app doesn't wire up, and X11's async selection protocol in particular doesn't survive a script exiting right after this command runs). Writes the capture to clipboard.bmp next to the settings instead.");
  Reg(e, "ScreenCaptureToClipboard", capture("ScreenCaptureToClipboard"), CommandStatus::Partial, "Same limitation as ViewCaptureToClipboard: no real system-clipboard image write, so it captures the active viewport to a file next to the settings instead.");
}

}  // namespace dino8::app
