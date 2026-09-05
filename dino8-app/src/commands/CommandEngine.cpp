#include "commands/CommandEngine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "app/Application.h"

namespace dino8::app {

// Kernel/OpenNURBS failures inside a command must never take the app down:
// report them on the command line and end the command cleanly.
#define DINO8_GUARD(expr)                                        \
  try {                                                          \
    expr;                                                        \
  } catch (const std::exception& ex) {                           \
    HandleCommandException(ex.what());                           \
  }

using kernel::Point3d;
using kernel::Vector3d;

const char* CommandStatusName(CommandStatus s) {
  switch (s) {
    case CommandStatus::Implemented: return "Implemented";
    case CommandStatus::Partial: return "Partial";
    case CommandStatus::Planned: return "Planned";
  }
  return "";
}

std::string FormatNumber(double v) {
  char buf[64];
  if (std::abs(v - std::round(v)) < 1e-9) std::snprintf(buf, sizeof(buf), "%.0f", v);
  else std::snprintf(buf, sizeof(buf), "%.4g", v);
  return buf;
}

std::string FormatPoint(Point3d p) {
  return FormatNumber(p.x) + "," + FormatNumber(p.y) + "," + FormatNumber(p.z);
}

// ---------------------------------------------------------------------------
// CommandContext
// ---------------------------------------------------------------------------

Viewport* CommandContext::ActiveViewport() { return app_.ActiveViewport(); }
std::vector<std::unique_ptr<Viewport>>& CommandContext::Viewports() { return app_.Viewports(); }
SnapSettings& CommandContext::Snaps() { return app_.Snaps(); }
void CommandContext::Print(const std::string& line) { engine_.Print(line); }
void CommandContext::Warn(const std::string& line) { engine_.Print("! " + line); }
std::vector<float>& CommandContext::PreviewLines() { return engine_.PreviewLines(); }
std::vector<float>& CommandContext::PreviewPoints() { return engine_.PreviewPoints(); }
void CommandContext::ClearPreview() { engine_.ClearPreview(); }
std::optional<Point3d> CommandContext::LastPoint() const { return engine_.LastPoint(); }
void CommandContext::SetLastPoint(std::optional<Point3d> p) { engine_.SetLastPoint(p); }
std::optional<Point3d> CommandContext::HoverPoint() const { return engine_.HoverPoint(); }
bool CommandContext::ScriptMode() const { return engine_.ScriptMode(); }
void CommandContext::RequestRedraw() {}
void CommandContext::ZoomExtentsAll() { app_.ZoomExtentsAll(); }

void CommandContext::AddPreviewLine(Point3d a, Point3d b) {
  std::vector<float>& v = engine_.PreviewLines();
  v.push_back(static_cast<float>(a.x)); v.push_back(static_cast<float>(a.y)); v.push_back(static_cast<float>(a.z));
  v.push_back(static_cast<float>(b.x)); v.push_back(static_cast<float>(b.y)); v.push_back(static_cast<float>(b.z));
}

void CommandContext::AddPreviewPolyline(const std::vector<Point3d>& pts, bool closed) {
  for (size_t i = 1; i < pts.size(); ++i) AddPreviewLine(pts[i - 1], pts[i]);
  if (closed && pts.size() > 2) AddPreviewLine(pts.back(), pts.front());
}

void CommandContext::AddPreviewCurve(const kernel::NurbsCurve& curve) {
  std::vector<double> params = curve.SuggestedParameterValues(0.05, 8);
  if (params.size() < 8) {
    const kernel::Interval d = curve.Domain();
    params.clear();
    for (int i = 0; i <= 32; ++i) params.push_back(d.min + (d.max - d.min) * i / 32.0);
  }
  Point3d prev = curve.PointAt(params.front());
  for (size_t i = 1; i < params.size(); ++i) {
    const Point3d p = curve.PointAt(params[i]);
    AddPreviewLine(prev, p);
    prev = p;
  }
}

void CommandContext::AddPreviewPoint(Point3d p) {
  std::vector<float>& v = engine_.PreviewPoints();
  v.push_back(static_cast<float>(p.x)); v.push_back(static_cast<float>(p.y)); v.push_back(static_cast<float>(p.z));
}

// ---------------------------------------------------------------------------
// CommandEngine
// ---------------------------------------------------------------------------

CommandEngine::CommandEngine(Application& app, Document& doc, CommandCatalog& catalog)
    : app_(app), doc_(doc), catalog_(catalog) {}

void CommandEngine::Register(const std::string& name, CommandFactory factory, CommandStatus status,
                             const std::string& note) {
  RegisteredCommand& r = registry_[ToLower(name)];
  r.name = name;
  r.info = catalog_.Find(name);
  if (r.info) r.name = r.info->name;
  r.factory = std::move(factory);
  r.status = status;
  r.note = note;
}

void CommandEngine::RegisterCatalogPlaceholders() {
  for (const CommandInfo& info : catalog_.All()) {
    const std::string key = ToLower(info.name);
    if (registry_.count(key)) {
      registry_[key].info = &info;
      continue;
    }
    RegisteredCommand r;
    r.name = info.name;
    r.info = &info;
    r.status = CommandStatus::Planned;
    r.note = "Not implemented in this build yet. The full Rhino 8 help for this command is available in the Help panel.";
    registry_[key] = std::move(r);
  }
}

const RegisteredCommand* CommandEngine::Find(const std::string& name) const {
  const auto it = registry_.find(ToLower(name));
  return it == registry_.end() ? nullptr : &it->second;
}

size_t CommandEngine::CountWithStatus(CommandStatus s) const {
  size_t n = 0;
  for (const auto& kv : registry_) {
    if (kv.second.status == s) ++n;
  }
  return n;
}

void CommandEngine::InstallDefaultAliases() {
  // Rhino's shipped default aliases.
  const std::pair<const char*, const char*> defaults[] = {
      {"a", "Arc"}, {"b", "Box"}, {"bs", "BlendSrf"}, {"c", "Circle"}, {"co", "Copy"},
      {"cp", "CPlane"}, {"d", "Delete"}, {"di", "Distance"}, {"e", "Extend"}, {"ex", "Extrude"},
      {"f", "Fillet"}, {"g", "Group"}, {"h", "Hide"}, {"j", "Join"}, {"l", "Line"}, {"lo", "Loft"},
      {"m", "Move"}, {"mi", "Mirror"}, {"o", "Offset"}, {"p", "Polyline"}, {"pl", "Planar"},
      {"r", "Rotate"}, {"re", "Rebuild"}, {"s", "Scale"}, {"sp", "Split"}, {"t", "Trim"},
      {"u", "Undo"}, {"z", "Zoom"}, {"ze", "ZoomExtents"}, {"zs", "ZoomSelected"}, {"ss", "SelAll"},
      {"sn", "SelNone"}, {"sh", "Show"}, {"sc", "SelCrv"}, {"ssf", "SelSrf"}, {"x", "Explode"},
      {"pt", "Point"}, {"ug", "Ungroup"}, {"cl", "ClosestPt"}, {"cu", "Curve"}, {"ic", "InterpCrv"},
      {"rec", "Rectangle"}, {"sph", "Sphere"}, {"cyl", "Cylinder"}, {"bu", "BooleanUnion"},
      {"bd", "BooleanDifference"}, {"bi", "BooleanIntersection"}, {"4v", "4View"}, {"mv", "MaxViewport"},
  };
  for (const auto& kv : defaults) aliases_[kv.first] = kv.second;
}

std::string CommandEngine::ResolveName(const std::string& typed) const {
  std::string t = typed;
  // Strip Rhino's prefixes: "!" cancels, "_" = english name, "-" = script mode.
  while (!t.empty() && (t.front() == '_' || t.front() == '-' || t.front() == '!' || t.front() == '\'')) t.erase(t.begin());
  const std::string lower = ToLower(t);
  const auto alias = aliases_.find(lower);
  if (alias != aliases_.end()) return alias->second;
  if (registry_.count(lower)) return registry_.at(lower).name;
  // Unique prefix match.
  std::vector<std::string> matches;
  for (const auto& kv : registry_) {
    if (kv.first.compare(0, lower.size(), lower) == 0) matches.push_back(kv.second.name);
  }
  if (matches.size() == 1) return matches.front();
  return t;
}

void CommandEngine::HandleCommandException(const std::string& what) {
  Print("! " + (active_name_.empty() ? std::string("Command") : active_name_) + " failed: " + what);
  if (active_) active_->finished = true;
}

void CommandEngine::Print(const std::string& line) {
  history_.push_back(line);
  while (history_.size() > 2000) history_.pop_front();
}

void CommandEngine::Execute(const std::string& raw_input) {
  std::string input = raw_input;
  // Trim.
  while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) input.pop_back();
  size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) ++start;
  input.erase(0, start);

  if (active_) {
    if (input.empty() || ToLower(input) == "enter" || input == "_Enter") FeedEnter();
    else if (ToLower(input) == "cancel" || ToLower(input) == "_cancel" || ToLower(input) == "!cancel") Cancel();
    else {
      // A typed line may carry several tokens ("SelID 3" while an object
      // prompt is up): feed them one by one.
      std::istringstream ss(input);
      std::string tok;
      std::vector<std::string> toks;
      while (ss >> tok) toks.push_back(tok);
      if (toks.empty()) { FeedEnter(); return; }
      for (size_t i = 1; i < toks.size(); ++i) pending_inputs_.push_back(toks[i]);
      FeedText(toks[0]);
    }
    return;
  }
  if (input.empty()) {
    RepeatLast();
    return;
  }
  // Tokenise: first token is the command, the rest are queued inputs.
  std::istringstream ss(input);
  std::string first;
  ss >> first;
  std::vector<std::string> rest;
  std::string tok;
  while (ss >> tok) rest.push_back(tok);
  if (first == "!") {
    Cancel();
    return;
  }
  bool script = false;
  {
    std::string t = first;
    while (!t.empty() && (t.front() == '_' || t.front() == '!' || t.front() == '\'')) t.erase(t.begin());
    if (!t.empty() && t.front() == '-') script = true;
  }
  const std::string name = ResolveName(first);
  Print("Command: " + input);
  pending_inputs_.assign(rest.begin(), rest.end());
  RunCommand(name, script);
}

void CommandEngine::RunCommand(const std::string& name, bool script_mode) {
  const RegisteredCommand* r = Find(name);
  if (!r) {
    Print("Unknown command: " + name);
    pending_inputs_.clear();
    return;
  }
  if (!r->factory) {
    Print(r->name + " is " + ToLower(CommandStatusName(r->status)) + " in this build: " + r->note);
    app_.ShowHelpFor(r->name);
    pending_inputs_.clear();
    return;
  }
  if (active_) Cancel();
  active_ = r->factory();
  active_name_ = r->name;
  script_mode_ = script_mode;
  last_command_ = r->name;
  recent_.erase(std::remove(recent_.begin(), recent_.end(), r->name), recent_.end());
  recent_.insert(recent_.begin(), r->name);
  if (recent_.size() > 30) recent_.pop_back();
  CommandContext ctx(app_, doc_, *this);
  DINO8_GUARD(active_->Begin(ctx));
  AfterCallback();
}

void CommandEngine::AfterCallback() {
  if (!active_) return;
  if (active_->finished) {
    active_.reset();
    active_name_.clear();
    ClearPreview();
    last_point_.reset();
    pending_inputs_.clear();
    return;
  }
  // Pre-selection: a command asking for objects with objects already
  // selected gets them immediately (Rhino behaviour).
  if (active_->want == Want::Objects && active_->accept_preselection) {
    const std::vector<ObjectId> sel = doc_.SelectedIds();
    if (static_cast<int>(sel.size()) >= active_->min_objects) {
      active_->accept_preselection = false;
      CommandContext ctx(app_, doc_, *this);
      DINO8_GUARD(active_->OnObjects(ctx, sel));
      AfterCallback();
      return;
    }
    active_->accept_preselection = false;
  }
  StartPendingInputs();
}

void CommandEngine::StartPendingInputs() {
  while (active_ && !pending_inputs_.empty()) {
    const std::string tok = pending_inputs_.front();
    pending_inputs_.pop_front();
    if (ToLower(tok) == "enter" || tok == "_Enter") FeedEnter();
    else FeedText(tok);
  }
}

void CommandEngine::Cancel() {
  if (std::getenv("DINO8_UI_DEBUG")) std::fprintf(stderr, "[engine] Cancel active=%s\n", active_name_.c_str());
  if (active_) {
    CommandContext ctx(app_, doc_, *this);
    DINO8_GUARD(active_->OnCancel(ctx));
    Print("Command cancelled: " + active_name_);
  }
  active_.reset();
  active_name_.clear();
  script_mode_ = false;
  ClearPreview();
  last_point_.reset();
  pending_inputs_.clear();
}

void CommandEngine::RepeatLast() {
  if (last_command_.empty()) return;
  Print("Command: " + last_command_);
  RunCommand(last_command_, false);
}

void CommandEngine::FeedPoint(Point3d p) {
  if (std::getenv("DINO8_UI_DEBUG")) std::fprintf(stderr, "[engine] FeedPoint %s active=%s want=%d\n", FormatPoint(p).c_str(), active_name_.c_str(), active_ ? static_cast<int>(active_->want) : -1);
  if (!active_) return;
  if (active_->want != Want::Point) return;
  CommandContext ctx(app_, doc_, *this);
  DINO8_GUARD(active_->OnPoint(ctx, p));
  last_point_ = p;
  AfterCallback();
}

void CommandEngine::FeedEnter() {
  if (!active_) {
    RepeatLast();
    return;
  }
  CommandContext ctx(app_, doc_, *this);
  if (active_->want == Want::Objects) {
    const std::vector<ObjectId> sel = doc_.SelectedIds();
    if (static_cast<int>(sel.size()) >= active_->min_objects) {
      DINO8_GUARD(active_->OnObjects(ctx, sel));
    } else if (sel.empty() && active_->min_objects > 0) {
      DINO8_GUARD(active_->OnEnter(ctx));
    } else {
      DINO8_GUARD(active_->OnObjects(ctx, sel));
    }
  } else if (active_->want == Want::Number && active_->default_number) {
    DINO8_GUARD(active_->OnNumber(ctx, *active_->default_number));
  } else if (active_->want == Want::Text && active_->default_text) {
    DINO8_GUARD(active_->OnText(ctx, *active_->default_text));
  } else {
    DINO8_GUARD(active_->OnEnter(ctx));
  }
  AfterCallback();
}

void CommandEngine::FeedObjects(const std::vector<ObjectId>& ids) {
  if (!active_ || active_->want != Want::Objects) return;
  CommandContext ctx(app_, doc_, *this);
  DINO8_GUARD(active_->OnObjects(ctx, ids));
  AfterCallback();
}

void CommandEngine::FeedOption(const std::string& name) {
  if (!active_) return;
  CommandContext ctx(app_, doc_, *this);
  for (const OptionSpec& o : active_->options) {
    if (ToLower(o.name) == ToLower(name)) {
      if (o.toggle) {
        const std::string next = (ToLower(o.value) == "yes") ? "No" : "Yes";
        DINO8_GUARD(active_->OnOption(ctx, o.name, next));
      } else if (!o.choices.empty()) {
        // Cycle to the next choice.
        size_t idx = 0;
        for (size_t i = 0; i < o.choices.size(); ++i) {
          if (ToLower(o.choices[i]) == ToLower(o.value)) idx = (i + 1) % o.choices.size();
        }
        DINO8_GUARD(active_->OnOption(ctx, o.name, o.choices[idx]));
      } else {
        DINO8_GUARD(active_->OnOption(ctx, o.name, ""));
      }
      AfterCallback();
      return;
    }
  }
}

void CommandEngine::FeedHover(std::optional<Point3d> p) {
  hover_point_ = p;
  if (!active_ || !p) return;
  CommandContext ctx(app_, doc_, *this);
  DINO8_GUARD(active_->OnHover(ctx, *p));
}

bool CommandEngine::TryParsePoint(const std::string& text, Point3d& out) {
  std::string t = text;
  bool relative = false;
  if (!t.empty() && (t.front() == '@' || t.front() == 'r' || t.front() == 'R')) {
    if (t.front() == '@' || (t.size() > 1 && (std::isdigit(static_cast<unsigned char>(t[1])) || t[1] == '-' || t[1] == '.'))) {
      relative = true;
      t.erase(t.begin());
    }
  }
  // Polar: "<angle" alone (distance from hover) or "dist<angle".
  double u = 0, v = 0, w = 0;
  const size_t lt = t.find('<');
  if (lt != std::string::npos) {
    double dist = 0, ang = 0;
    if (lt > 0 && std::sscanf(t.c_str(), "%lf<%lf", &dist, &ang) == 2) {
      u = dist * std::cos(ang * ON_PI / 180.0);
      v = dist * std::sin(ang * ON_PI / 180.0);
      relative = relative || last_point_.has_value();
    } else {
      return false;
    }
  } else {
    const int n = std::sscanf(t.c_str(), "%lf,%lf,%lf", &u, &v, &w);
    if (n < 2) return false;
    if (n == 2) w = 0;
  }
  const Viewport* vp = app_.ActiveViewport();
  ConstructionPlane cp;
  if (vp) cp = const_cast<Viewport*>(vp)->CPlane();
  if (relative && last_point_) {
    out = *last_point_ + cp.x_axis * u + cp.y_axis * v + cp.Normal() * w;
  } else {
    out = cp.ToWorld(u, v, w);
  }
  return true;
}

bool CommandEngine::TryOption(const std::string& text) {
  if (!active_) return false;
  const std::string lower = ToLower(text);
  std::string name = lower, value;
  const size_t eq = lower.find('=');
  if (eq != std::string::npos) {
    name = lower.substr(0, eq);
    value = text.substr(eq + 1);
  }
  // Exact, then unique prefix, against option names.
  const OptionSpec* match = nullptr;
  int prefix_matches = 0;
  for (const OptionSpec& o : active_->options) {
    const std::string on = ToLower(o.name);
    if (on == name) {
      match = &o;
      prefix_matches = 1;
      break;
    }
    // Options are typically matched by their capital letters in Rhino, but
    // a plain prefix is friendlier and unambiguous here.
    if (on.compare(0, name.size(), name) == 0) {
      match = &o;
      ++prefix_matches;
    }
  }
  if (!match || prefix_matches != 1) return false;
  CommandContext ctx(app_, doc_, *this);
  if (!value.empty()) {
    DINO8_GUARD(active_->OnOption(ctx, match->name, value));
  } else if (match->toggle) {
    DINO8_GUARD(active_->OnOption(ctx, match->name, ToLower(match->value) == "yes" ? "No" : "Yes"));
  } else if (!match->choices.empty()) {
    size_t idx = 0;
    for (size_t i = 0; i < match->choices.size(); ++i) {
      if (ToLower(match->choices[i]) == ToLower(match->value)) idx = (i + 1) % match->choices.size();
    }
    DINO8_GUARD(active_->OnOption(ctx, match->name, match->choices[idx]));
  } else {
    DINO8_GUARD(active_->OnOption(ctx, match->name, ""));
  }
  return true;
}

void CommandEngine::FeedText(const std::string& text) {
  if (std::getenv("DINO8_UI_DEBUG")) std::fprintf(stderr, "[engine] FeedText '%s' active=%s\n", text.c_str(), active_name_.c_str());
  if (!active_) {
    Execute(text);
    return;
  }
  CommandContext ctx(app_, doc_, *this);
  const Want want = active_->want;
  // Options first: they're valid in any state.
  if (TryOption(text)) {
    AfterCallback();
    return;
  }
  switch (want) {
    case Want::Point: {
      Point3d p;
      double number = 0;
      if (TryParsePoint(text, p)) {
        Print(FormatPoint(p));
        DINO8_GUARD(active_->OnPoint(ctx, p));
        last_point_ = p;
      } else if (std::sscanf(text.c_str(), "%lf", &number) == 1 && last_point_ && hover_point_) {
        // Distance constraint: a number places the point `number` units
        // from the last point, in the direction of the cursor.
        Vector3d dir = *hover_point_ - *last_point_;
        if (dir.Length() < 1e-9) dir = Vector3d(1, 0, 0);
        dir.Unitize();
        p = *last_point_ + dir * number;
        Print(FormatPoint(p));
        DINO8_GUARD(active_->OnPoint(ctx, p));
        last_point_ = p;
      } else {
        DINO8_GUARD(active_->OnText(ctx, text));
      }
      break;
    }
    case Want::Number: {
      double number = 0;
      if (std::sscanf(text.c_str(), "%lf", &number) == 1) {
        DINO8_GUARD(active_->OnNumber(ctx, number));
      } else {
        Print("Invalid number: " + text);
      }
      break;
    }
    case Want::Objects: {
      // Nested selection commands (SelAll, SelCrv, ...) are allowed here.
      const std::string name = ResolveName(text);
      const RegisteredCommand* r = Find(name);
      if (r && r->factory && ToLower(name).compare(0, 3, "sel") == 0) {
        std::unique_ptr<Command> nested = r->factory();
        DINO8_GUARD(nested->Begin(ctx));
      } else {
        DINO8_GUARD(active_->OnText(ctx, text));
      }
      break;
    }
    case Want::Text:
      DINO8_GUARD(active_->OnText(ctx, text));
      break;
    case Want::Enter:
    case Want::Nothing:
      DINO8_GUARD(active_->OnText(ctx, text));
      break;
  }
  AfterCallback();
}

std::string CommandEngine::Prompt() const {
  if (!active_) return "Command:";
  return active_->prompt;
}

Want CommandEngine::CurrentWant() const { return active_ ? active_->want : Want::Nothing; }

const std::vector<OptionSpec>* CommandEngine::CurrentOptions() const {
  return active_ ? &active_->options : nullptr;
}

}  // namespace dino8::app
