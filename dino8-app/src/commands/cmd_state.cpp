// Viewport, window, application-state and housekeeping commands: Cancel /
// Echo / Fullscreen, viewport management, Camera / lens, drag and snap
// switches, draw order, named selections and CPlanes, Gumball settings,
// options import/export, and the "no licences, no accounts" commands.
#include "commands/cmd_common.h"

#include <GLFW/glfw3.h>

#include <cctype>
#include <filesystem>
#include <set>

#include "app/Settings.h"

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

std::string UniqueViewportName(CommandContext& ctx, const std::string& base) {
  if (!ctx.App().FindViewport(base)) return base;
  for (int i = 2;; ++i) { std::string n = base + " " + std::to_string(i); if (!ctx.App().FindViewport(n)) return n; }
}

// Adds a viewport that copies the active one's camera, mode and CPlane.
Viewport* AddViewportLike(CommandContext& ctx, Viewport* src) {
  const std::string name = UniqueViewportName(ctx, src ? src->StandardView() : "Perspective");
  auto vp = std::make_unique<Viewport>(name, src ? src->StandardView() : "Perspective");
  if (src) { vp->GetCamera().SetState(src->GetCamera().State()); vp->SetMode(src->Mode()); vp->CPlane() = src->CPlane(); }
  Viewport* out = vp.get();
  ctx.Viewports().push_back(std::move(vp));
  return out;
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
  Reg(e, "CommandPrompt", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().command_prompt; }, "Command prompt"), CommandStatus::Partial, "Stored; the command line stays visible so you can always type.");
  Reg(e, "DisplayCommandPrompt", SetFlag([](CommandContext& ctx) -> bool& { return ctx.App().State().command_prompt; }, true, "Command prompt"), CommandStatus::Partial, "The command line is always shown.");
  Reg(e, "Run", Immediate([](CommandContext& ctx) { ctx.Engine().PendingInputs().clear(); ctx.Warn("Run: Dino 8 does not execute external programs from the command line."); }), CommandStatus::Partial, "External programs are never run; use your shell.");
  Reg(e, "GetIssueState", Say("GetIssueState: ok - no issues reported"));
  Reg(e, "ResetMessageBoxes", Immediate([](CommandContext& ctx) { ctx.App().State().message_boxes_reset = true; ctx.Print("ResetMessageBoxes: all 'do not show again' choices cleared"); }));

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
  Reg(e, "NewFloatingViewport", Immediate([](CommandContext& ctx) { Viewport* vp = AddViewportLike(ctx, ctx.ActiveViewport()); Activate(ctx, vp); }), CommandStatus::Partial, "Adds a docked viewport; drag its tab out to float it.");
  Reg(e, "SplitViewportHorizontal", Immediate([](CommandContext& ctx) { Viewport* vp = AddViewportLike(ctx, ctx.ActiveViewport()); ctx.Print("Added viewport " + vp->Name()); }), CommandStatus::Partial, "Adds a copy of the active viewport; dock it beside the original.");
  Reg(e, "SplitViewportVertical", Immediate([](CommandContext& ctx) { Viewport* vp = AddViewportLike(ctx, ctx.ActiveViewport()); ctx.Print("Added viewport " + vp->Name()); }), CommandStatus::Partial, "Adds a copy of the active viewport; dock it below the original.");
  Reg(e, "NextViewportToTop", Immediate([](CommandContext& ctx) { ActivateNext(ctx, [](const Viewport&) { return true; }, "other"); }));
  Reg(e, "BringViewportToTop", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) Activate(ctx, vp); }));
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
      }), CommandStatus::Partial, "Swaps the active viewport's view with the next one.");
  Reg(e, "OneView", Immediate([](CommandContext& ctx) { ctx.App().SetViewportLayout(1); ctx.Print("Single viewport layout"); }));
  Reg(e, "ToggleFloatingViewport", Say("ToggleFloatingViewport: drag a viewport tab out of the dock to float it, or back in to dock it."), CommandStatus::Partial);
  Reg(e, "LockViewport", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().lock_viewport; }, "Viewport lock"), CommandStatus::Partial, "Records the lock; mouse navigation still works.");
  Reg(e, "SetMaximizedViewport", Make<TextArgCommand>("Viewport to maximize", [](CommandContext& ctx, const std::string& name) {
        Viewport* target = ctx.App().FindViewport(name);
        if (!target) for (auto& v : ctx.Viewports()) if (Lower(v->Name()) == Lower(name)) target = v.get();
        if (!target) { ctx.Warn("No viewport '" + name + "'. Viewports: " + ViewportNames(ctx)); return; }
        for (auto& v : ctx.Viewports()) { v->SetMaximized(v.get() == target); v->SetActive(v.get() == target); }
        ctx.Print("Maximized viewport " + target->Name());
      }));
  Reg(e, "ViewportTabs", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().viewport_tabs; }, "Viewport tabs"), CommandStatus::Partial, "Viewports are always tabbed dock windows.");
  Reg(e, "ZoomEnds", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->ZoomExtents(ctx.Doc(), ctx.Doc().SelectedCount() > 0); }), CommandStatus::Partial, "Zooms to the selected curves.");
  Reg(e, "ZoomNaked", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->ZoomExtents(ctx.Doc(), ctx.Doc().SelectedCount() > 0); }), CommandStatus::Partial, "Zooms to the selection; see ShowEdges for naked edges.");
  Reg(e, "ZoomNonManifold", Immediate([](CommandContext& ctx) { if (Viewport* vp = ctx.ActiveViewport()) vp->ZoomExtents(ctx.Doc(), ctx.Doc().SelectedCount() > 0); }), CommandStatus::Partial, "Zooms to the selection; SelNonManifold finds the meshes.");
  Reg(e, "Zoom1To1Calibrate", Say("Zoom1To1Calibrate: Zoom1To1 assumes a 96 dpi screen; measure a Zoom1To1 view with a ruler and scale from there."), CommandStatus::Partial);
  Reg(e, "SetZoomExtentsBorder", Make<NumberArgCommand>("Zoom extents border factor", [](CommandContext& ctx) { return ctx.App().State().zoom_extents_border; }, [](CommandContext& ctx, double v) { ctx.App().State().zoom_extents_border = std::max(1.0, v); ctx.Print("Zoom extents border = " + FormatNumber(std::max(1.0, v))); }), CommandStatus::Partial, "Stored; ZoomExtents uses its built-in margin.");

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
      }), CommandStatus::Partial, "Type the lens length; interactive dragging is planned.");

  // ---- snaps / drag ----------------------------------------------------
  Reg(e, "SetOrtho", Immediate([](CommandContext& ctx) { ctx.Snaps().ortho = true; ctx.Settings().ortho = true; ctx.Print("Ortho on (angle " + FormatNumber(ctx.App().State().ortho_angle) + " deg)"); }));
  Reg(e, "OrthoAngle", Make<NumberArgCommand>("Ortho angle in degrees", [](CommandContext& ctx) { return ctx.App().State().ortho_angle; }, [](CommandContext& ctx, double v) { ctx.App().State().ortho_angle = std::clamp(v, 1.0, 180.0); ctx.Print("Ortho angle = " + FormatNumber(ctx.App().State().ortho_angle) + " deg"); }), CommandStatus::Partial, "Stored; ortho constrains to 90 degree steps.");
  Reg(e, "SnapSize", Make<NumberArgCommand>("Grid snap size", [](CommandContext& ctx) { return ctx.Settings().grid_spacing; }, [](CommandContext& ctx, double v) { if (v > 0) { ctx.Settings().grid_spacing = v; ctx.Print("Grid snap size = " + FormatNumber(v)); } }));
  Reg(e, "SetSnap", Make<NumberArgCommand>("Grid snap size", [](CommandContext& ctx) { return ctx.Settings().grid_spacing; }, [](CommandContext& ctx, double v) { if (v > 0) { ctx.Settings().grid_spacing = v; ctx.Snaps().grid_snap = true; ctx.Print("Grid snap on, size = " + FormatNumber(v)); } }));
  Reg(e, "OrthoSnapToCPlaneZ", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().ortho_snap_to_cplane_z; }, "Ortho snap to CPlane Z"), CommandStatus::Partial, "Stored flag.");
  Reg(e, "SnapToLocked", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().snap_to_locked; }, "Snap to locked objects"), CommandStatus::Partial, "Stored flag; snaps consider every visible object.");
  Reg(e, "SnapToOccluded", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().snap_to_occluded; }, "Snap to occluded objects"), CommandStatus::Partial, "Stored flag; snaps consider every visible object.");
  Reg(e, "SnapToMeshes", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().snap_to_meshes; }, "Snap to meshes"), CommandStatus::Partial, "Stored flag.");
  Reg(e, "SnapToMeshObject", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().snap_to_mesh_object; }, "Snap to mesh objects"), CommandStatus::Partial, "Stored flag.");
  Reg(e, "SnapToSubDObject", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().snap_to_subd_object; }, "Snap to SubD objects"), CommandStatus::Partial, "Stored flag.");
  Reg(e, "ShowOsnap", Immediate([](CommandContext& ctx) { bool& b = ctx.App().Panels().object_snaps; b = !b; ctx.Print(std::string("Osnap panel ") + (b ? "shown" : "hidden")); }));
  Reg(e, "DragMode", Make<ChoiceCommand>("DragMode", std::vector<std::string>{"CPlane", "World", "UVN", "View", "ControlPolygon"}, [](CommandContext& ctx) -> std::string& { return ctx.App().State().drag_mode; }), CommandStatus::Partial, "Stored; dragging follows the CPlane.");
  Reg(e, "DragStrength", Make<NumberArgCommand>("Drag strength percent", [](CommandContext& ctx) { return ctx.App().State().drag_strength; }, [](CommandContext& ctx, double v) { ctx.App().State().drag_strength = std::clamp(v, 1.0, 100.0); ctx.Print("Drag strength = " + FormatNumber(ctx.App().State().drag_strength) + "%"); }), CommandStatus::Partial, "Stored flag.");
  Reg(e, "DragCopy", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().drag_copy; }, "Drag copy"), CommandStatus::Partial, "Stored; Alt-drag copies.");
  Reg(e, "RememberCopyOptions", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().remember_copy_options; }, "Remember copy options"), CommandStatus::Partial, "Stored flag.");

  // ---- panels / UI -----------------------------------------------------
  Reg(e, "ToggleLeftSidebar", Immediate([](CommandContext& ctx) { AppState& s = ctx.App().State(); s.left_sidebar = !s.left_sidebar; ctx.App().Panels().toolbars = s.left_sidebar; ctx.Print(std::string("Left sidebar (toolbar) ") + (s.left_sidebar ? "shown" : "hidden")); }));
  Reg(e, "ToggleRightSidebar", Immediate([](CommandContext& ctx) { AppState& s = ctx.App().State(); s.right_sidebar = !s.right_sidebar; ctx.App().Panels().layers = s.right_sidebar; ctx.App().Panels().properties = s.right_sidebar; ctx.Print(std::string("Right sidebar (Layers, Properties) ") + (s.right_sidebar ? "shown" : "hidden")); }));
  Reg(e, "ShowToolbar", Immediate([](CommandContext& ctx) { ctx.App().Panels().toolbars = true; ctx.Print("Toolbar shown"); }));
  Reg(e, "ToolbarLock", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().State().toolbar_lock; }, "Toolbar lock"), CommandStatus::Partial, "Stored flag; the toolbar is docked.");
  Reg(e, "Commands", Immediate([](CommandContext& ctx) { ctx.App().Panels().command_list = true; }));
  Reg(e, "PopupMenu", Immediate([](CommandContext& ctx) { ctx.App().Panels().command_list = true; }), CommandStatus::Partial, "Opens the command list; middle-click a viewport for the popup toolbar.");
  Reg(e, "PopupPopular", Immediate([](CommandContext& ctx) { ctx.Print("Recent commands:"); for (const std::string& n : ctx.Engine().RecentCommands()) ctx.Print("  " + n); ctx.App().Panels().command_list = true; }), CommandStatus::Partial, "Lists recent commands and opens the command list.");
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
      }), CommandStatus::Partial, "Keeps recent files, theme and window layout.");
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
      }), CommandStatus::Partial, "Prints the folder; use Open for the file browser.");
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
  Reg(e, "EarthAnchorPoint", Make<PointArgCommand>("Earth anchor point (model location)", [](CommandContext& ctx, Point3d p) { ctx.Doc().UserText()["EarthAnchorPoint"] = FormatPoint(p); ctx.Doc().Touch(); ctx.Print("Earth anchor point " + FormatPoint(p)); }), CommandStatus::Partial, "Stores the model point; latitude/longitude are planned.");

  // ---- objects -------------------------------------------------------------
  Reg(e, "Dot", Make<DotCommand>());
  Reg(e, "PointCloud", OnSelection("Select points for the point cloud", [](CommandContext& ctx, const std::vector<ObjectId>& ids) {
        std::vector<ObjectId> pts;
        for (ObjectId id : ids) if (const SceneObject* o = ctx.Doc().Find(id); o && o->kind == ObjectKind::Point) pts.push_back(id);
        if (pts.empty()) { ctx.Warn("Select point objects"); return; }
        ctx.Doc().BeginChange("PointCloud");
        ctx.Doc().CreateGroup(pts, "PointCloud");
        ctx.Print("PointCloud: " + std::to_string(pts.size()) + " point(s) grouped");
      }), CommandStatus::Partial, "Groups the points; a dedicated point-cloud object is planned.");
  Reg(e, "InfinitePlane", Immediate([](CommandContext& ctx) {
        ON_Plane pl = ActivePlane(ctx);
        const double s = std::max(1.0, ctx.Settings().grid_spacing * ctx.Settings().grid_extents) * 20.0;
        std::vector<Point3d> grid = {pl.PointAt(-s, -s), pl.PointAt(s, -s), pl.PointAt(-s, s), pl.PointAt(s, s)};
        SceneObject o = SceneObject::MakeSurface(kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
        o.name = "InfinitePlane";
        AddObject(ctx, std::move(o), "InfinitePlane");
        ctx.Print("InfinitePlane: " + FormatNumber(2 * s) + " x " + FormatNumber(2 * s) + " plane on the CPlane");
      }), CommandStatus::Partial, "Creates a very large plane surface.");
  Reg(e, "BringToFront", DrawOrder("BringToFront", 1), CommandStatus::Partial, "Draw order is stored in user text.");
  Reg(e, "SendToBack", DrawOrder("SendToBack", 2), CommandStatus::Partial, "Draw order is stored in user text.");
  Reg(e, "BringForward", DrawOrder("BringForward", 3), CommandStatus::Partial, "Draw order is stored in user text.");
  Reg(e, "SendBackward", DrawOrder("SendBackward", 4), CommandStatus::Partial, "Draw order is stored in user text.");
  Reg(e, "ClearDrawOrder", DrawOrder("ClearDrawOrder", 0), CommandStatus::Partial, "Draw order is stored in user text.");
  Reg(e, "NamedSelections", Make<NamedSetCommand>(false));
  Reg(e, "NamedCPlane", Make<NamedSetCommand>(true));
  Reg(e, "NamedPosition", Say("NamedPosition: saved object positions are planned; use NamedSelections and Undo for now."), CommandStatus::Partial);
  Reg(e, "Snapshots", Say("Snapshots: saved model states are planned; NamedView, NamedSelections and Undo cover the common cases."), CommandStatus::Partial);
  Reg(e, "HistoryPurge", Say("HistoryPurge: no construction history is recorded; nothing to purge."));
  Reg(e, "HistoryUpdate", Say("HistoryUpdate: no construction history is recorded; nothing to update."));
  Reg(e, "Worksession", Say("Worksession: reference models are planned; Import brings another file's objects in."), CommandStatus::Partial);
  Reg(e, "LimitReferenceModel", Say("LimitReferenceModel: reference models are planned."), CommandStatus::Partial);
  Reg(e, "Bounce", Say("Bounce: ray bounce curves are planned."), CommandStatus::Partial);
  Reg(e, "ContentFilter", Say("ContentFilter: render content filtering is planned; the Materials panel lists every material."), CommandStatus::Partial);

  // ---- gumball ------------------------------------------------------------
  Reg(e, "GumballAlignment", GumballChoice("GumballAlignment", {"CPlane", "World", "Object"}, [](Gumball::Settings& s) -> std::string& { return s.alignment; }), CommandStatus::Partial, "Stored; the widget uses world axes.");
  Reg(e, "GumballScaleMode", GumballChoice("GumballScaleMode", {"Independent", "Uniform"}, [](Gumball::Settings& s) -> std::string& { return s.scale_mode; }), CommandStatus::Partial, "Stored; handles scale independently.");
  Reg(e, "GumballAutoReset", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().GetGumball().GetSettings().auto_reset; }, "Gumball auto reset"));
  Reg(e, "GumballDynamicRelocate", Toggle([](CommandContext& ctx) -> bool& { return ctx.App().GetGumball().GetSettings().dynamic_relocate; }, "Gumball dynamic relocate"), CommandStatus::Partial, "Stored flag.");
  Reg(e, "GumballRelocate", Make<PointArgCommand>("New gumball origin", [](CommandContext& ctx, Point3d p) { Gumball::Settings& s = ctx.App().GetGumball().GetSettings(); s.relocated = true; s.relocated_origin = p; ctx.Print("Gumball origin " + FormatPoint(p)); }), CommandStatus::Partial, "Stored; the widget draws at the selection centre.");
  Reg(e, "GumballReset", Immediate([](CommandContext& ctx) { Gumball::Settings& s = ctx.App().GetGumball().GetSettings(); s.relocated = false; s.alignment = "CPlane"; ctx.Print("Gumball reset"); }));
  Reg(e, "GumballSettings", Immediate([](CommandContext& ctx) {
        const Gumball::Settings& s = ctx.App().GetGumball().GetSettings();
        ctx.Print(std::string("Gumball: ") + (ctx.App().gumball_enabled ? "on" : "off") + ", alignment " + s.alignment + ", scale " + s.scale_mode + ", auto reset " + (s.auto_reset ? "on" : "off") +
                  ", dynamic relocate " + (s.dynamic_relocate ? "on" : "off") + (s.relocated ? ", origin " + FormatPoint(s.relocated_origin) : ""));
        ctx.App().Panels().options = true;
      }));

  // ---- no licences, accounts or subscriptions (product rule) ------------
  for (const char* n : {"CheckInLicense", "CheckOutLicense", "Login", "Logout", "Libraries", "DownloadLibraryTextures"}) Reg(e, n, Say(kFree));

  // ---- plug-ins, Grasshopper, digitizers ----------------------------------
  const char* plugins = "Plug-ins and Grasshopper are not yet available in Dino 8.";
  for (const char* n : {"MigratePlugins", "PlugInManager", "GrasshopperDeveloperSettings", "GrasshopperFolders", "GrasshopperGetSDKDocumentation", "GrasshopperIgnorePlugin",
                        "GrasshopperLoadOneByOne", "GrasshopperPlayer", "GrasshopperPluginList"})
    Reg(e, n, Say(plugins), CommandStatus::Partial, plugins);
  const char* dig = "No digitizer is connected; 3D digitizer support is planned.";
  for (const char* n : {"Digitize", "DigCalibrate", "DigCamera", "DigClick", "DigDisconnect", "DigLine", "DigPause", "DigScale", "DigSection", "DigSketch"})
    Reg(e, n, Say(dig), CommandStatus::Partial, dig);
  Reg(e, "DigBeep", Say("DigBeep: no digitizer is connected."), CommandStatus::Partial, dig);

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
  Reg(e, "ViewCaptureToClipboard", capture("ViewCaptureToClipboard"), CommandStatus::Partial, "Writes the capture to a file next to the settings.");
  Reg(e, "ScreenCaptureToClipboard", capture("ScreenCaptureToClipboard"), CommandStatus::Partial, "Captures the active viewport to a file next to the settings.");
}

}  // namespace dino8::app
