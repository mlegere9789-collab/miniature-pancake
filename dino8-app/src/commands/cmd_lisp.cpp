// AutoLISP-style scripting (PARITY_MAP.md's "AutoLISP-equivalent command
// scripting language"): the Lisp dialect itself is script/Lisp.{h,cpp}; this
// file plugs it into the command line.
//   (expr ...)            typed at the command line (CommandEngine::Execute hands the
//                         raw text over), in a script file line, or as  Lisp <expr>
//   LoadLisp <file>       evaluates a .lsp file (RunScript / LoadScript of a .lsp do the same)
//   (defun c:name () ...) defines a new command "name"
//   <config dir>/acad.lsp is loaded at every start-up, so its c: commands survive a restart
#include "commands/cmd_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "app/Application.h"
#include "app/Settings.h"
#include "io/AciPalette.h"
#include "script/Lisp.h"

namespace dino8::app {

void RunLispFile(CommandContext& ctx, const std::string& path);

namespace {

lisp::Interp& TheInterp() {
  static lisp::Interp in;
  return in;
}
std::set<std::string>& LispCommands() {
  static std::set<std::string> s;   // lowercase names registered by (defun c:name ...)
  return s;
}

std::deque<std::string>& LispInputs() {
  static std::deque<std::string> q;   // typed input waiting for getint / getreal / getpoint / getstring / getkword
  return q;
}

using lisp::DxfItem;
DxfItem DS(int code, const std::string& s) { DxfItem d; d.code = code; d.k = DxfItem::K::Str; d.s = s; return d; }
DxfItem DI(int code, long long i) { DxfItem d; d.code = code; d.k = DxfItem::K::Int; d.i = i; d.x = static_cast<double>(i); return d; }
DxfItem DR(int code, double x) { DxfItem d; d.code = code; d.k = DxfItem::K::Real; d.x = x; return d; }
DxfItem DP(int code, double x, double y, double z) { DxfItem d; d.code = code; d.k = DxfItem::K::Point; d.x = x; d.y = y; d.z = z; return d; }
DxfItem DP(int code, const Point3d& p) { return DP(code, p.x, p.y, p.z); }

constexpr double kEntTol = 1e-6;

std::string HexHandle(long long id) {
  char b[32];
  std::snprintf(b, sizeof b, "%llX", static_cast<unsigned long long>(id));
  return b;
}

// The DXF entity type of an object: LINE / CIRCLE / ARC / LWPOLYLINE / SPLINE for curves, POINT, and the solid / surface kinds.
std::string EntityType(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Point: return "POINT";
    case ObjectKind::Curve: {
      if (!o.curve) return "SPLINE";
      ON_Arc arc;
      if (o.curve->IsLinear(kEntTol)) return "LINE";
      if (o.curve->raw().IsArc(nullptr, &arc, kEntTol)) return arc.IsCircle() ? "CIRCLE" : "ARC";
      return o.curve->Degree() == 1 ? "LWPOLYLINE" : "SPLINE";
    }
    case ObjectKind::Surface: return "SURFACE";
    case ObjectKind::Brep: return o.brep && o.brep->raw().IsSolid() ? "3DSOLID" : "SURFACE";
    case ObjectKind::Mesh: return "MESH";
    case ObjectKind::SubD: return "SUBD";
    case ObjectKind::PointCloud: return "POINTCLOUD";
  }
  return "UNKNOWN";
}

bool GetEntity(const Document& doc, long long id, std::vector<DxfItem>& out) {
  const SceneObject* o = doc.Find(static_cast<ObjectId>(id));
  if (!o) return false;
  out.clear();
  const std::string type = EntityType(*o);
  out.push_back(DI(-1, id));
  out.push_back(DS(0, type));
  out.push_back(DS(5, HexHandle(id)));
  const auto& layers = doc.Layers();
  out.push_back(DS(8, o->layer_index >= 0 && o->layer_index < static_cast<int>(layers.size()) ? layers[static_cast<size_t>(o->layer_index)].name : "Default"));
  if (!o->color_by_layer) {
    auto b = [](float v) { return static_cast<int>(std::lround(v * 255.f)); };
    out.push_back(DI(62, RgbToAci(b(o->color.r), b(o->color.g), b(o->color.b))));
  }
  if (type == "POINT") {
    out.push_back(DP(10, o->point));
  } else if (type == "LINE") {
    const kernel::Interval d = o->curve->Domain();
    out.push_back(DP(10, o->curve->PointAt(d.min)));
    out.push_back(DP(11, o->curve->PointAt(d.max)));
  } else if (type == "CIRCLE" || type == "ARC") {
    ON_Arc arc;
    o->curve->raw().IsArc(nullptr, &arc, kEntTol);
    out.push_back(DP(10, arc.Center()));
    out.push_back(DR(40, arc.Radius()));
    if (type == "ARC") {
      out.push_back(DR(50, arc.Domain().Min()));
      out.push_back(DR(51, arc.Domain().Max()));
    }
    const ON_3dVector n = arc.Plane().Normal();
    out.push_back(DP(210, n.x, n.y, n.z));
  } else if (type == "LWPOLYLINE") {
    out.push_back(DI(90, o->curve->ControlPointCount()));
    out.push_back(DI(70, o->curve->IsClosed() ? 1 : 0));
    for (int k = 0; k < o->curve->ControlPointCount(); ++k) out.push_back(DP(10, o->curve->ControlPointAt(k)));
  } else if (type == "SPLINE") {
    if (o->curve) {
      out.push_back(DI(71, o->curve->Degree()));
      out.push_back(DI(73, o->curve->ControlPointCount()));
      for (int k = 0; k < o->curve->ControlPointCount(); ++k) out.push_back(DP(10, o->curve->ControlPointAt(k)));
    }
  }
  return true;
}

bool SameItem(const DxfItem& a, const DxfItem& b) {
  if (a.code != b.code || a.k != b.k) return false;
  switch (a.k) {
    case DxfItem::K::Str: return a.s == b.s;
    case DxfItem::K::Int: return a.i == b.i;
    case DxfItem::K::Real: return std::fabs(a.x - b.x) <= 1e-9;
    case DxfItem::K::Point: return std::fabs(a.x - b.x) <= 1e-9 && std::fabs(a.y - b.y) <= 1e-9 && std::fabs(a.z - b.z) <= 1e-9;
  }
  return false;
}

// Applies the changed groups of an entget-style list. "" when done, otherwise why nothing was changed.
std::string ModEntity(Document& doc, long long id, const std::vector<DxfItem>& items) {
  std::vector<DxfItem> cur;
  if (!GetEntity(doc, id, cur)) return "no entity " + std::to_string(id);
  const std::string type = cur.size() > 1 ? cur[1].s : "";
  // Which groups differ from what the entity has now?
  std::vector<DxfItem> changed;
  for (const DxfItem& d : items) {
    bool same = false;
    for (const DxfItem& c : cur) if (c.code == d.code && SameItem(c, d)) { same = true; break; }
    if (d.code == 62 && d.k == DxfItem::K::Int && d.i == 256 && !std::any_of(cur.begin(), cur.end(), [](const DxfItem& c) { return c.code == 62; })) same = true;
    if (!same) changed.push_back(d);
  }
  const bool editable_geometry = type == "POINT" || type == "LINE" || type == "CIRCLE" || type == "ARC";
  int layer = -1;
  std::optional<int> aci;
  bool bylayer = false;
  std::optional<Point3d> p10, p11;
  std::optional<double> r40, a50, a51;
  for (const DxfItem& d : changed) {
    const bool num = d.k == DxfItem::K::Real || d.k == DxfItem::K::Int;
    if (d.code == 8) {
      if (d.k != DxfItem::K::Str) return "group 8 (layer) needs a string";
      layer = doc.FindLayer(d.s);
      if (layer < 0) return "layer \"" + d.s + "\" does not exist";
    } else if (d.code == 62) {
      if (d.k != DxfItem::K::Int) return "group 62 (color) needs an integer";
      if (d.i == 256) bylayer = true;
      else if (d.i >= 1 && d.i <= 255) aci = static_cast<int>(d.i);
      else return "group 62: color " + std::to_string(d.i) + " is not supported (use 1..255, or 256 for BYLAYER)";
    } else if (editable_geometry && d.code == 10 && d.k == DxfItem::K::Point) {
      p10 = Point3d(d.x, d.y, d.z);
    } else if (type == "LINE" && d.code == 11 && d.k == DxfItem::K::Point) {
      p11 = Point3d(d.x, d.y, d.z);
    } else if ((type == "CIRCLE" || type == "ARC") && d.code == 40 && num) {
      if (d.x <= 0) return "group 40: the radius must be positive";
      r40 = d.x;
    } else if (type == "ARC" && d.code == 50 && num) {
      a50 = d.x;
    } else if (type == "ARC" && d.code == 51 && num) {
      a51 = d.x;
    } else {
      return "group " + std::to_string(d.code) + " of a " + type + " cannot be changed";
    }
  }
  if (type == "ARC" && (a50 || a51)) {
    ON_Arc a;
    doc.Find(static_cast<ObjectId>(id))->curve->raw().IsArc(nullptr, &a, kEntTol);
    const double s = a50 ? *a50 : a.Domain().Min(), e = a51 ? *a51 : a.Domain().Max();
    if (!(e > s) || e - s > 2 * ON_PI + 1e-9) return "groups 50/51: the end angle must be greater than the start angle and the sweep at most 2 pi";
  }
  if (changed.empty()) return "";
  doc.BeginChange("Lisp entmod");
  SceneObject* o = doc.Find(static_cast<ObjectId>(id));
  if (layer >= 0) o->layer_index = layer;
  if (bylayer) o->color_by_layer = true;
  if (aci) {
    const std::array<int, 3> rgb = AciToRgb(*aci);
    o->color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
    o->color_by_layer = false;
  }
  if (type == "POINT" && p10) o->point = *p10;
  if (type == "LINE" && (p10 || p11)) {
    const kernel::Interval d = o->curve->Domain();
    const Point3d s = p10 ? *p10 : o->curve->PointAt(d.min), e = p11 ? *p11 : o->curve->PointAt(d.max);
    *o->curve = PolylineCurve({s, e});
  }
  if ((type == "CIRCLE" || type == "ARC") && (p10 || r40 || a50 || a51)) {
    ON_Arc arc;
    o->curve->raw().IsArc(nullptr, &arc, kEntTol);
    ON_Plane plane = arc.Plane();
    if (p10) { plane.origin = *p10; plane.UpdateEquation(); }
    const double r = r40 ? *r40 : arc.Radius();
    const ON_Interval dom(a50 ? *a50 : arc.Domain().Min(), a51 ? *a51 : arc.Domain().Max());
    ON_Arc na(ON_Circle(plane, r), dom);
    ON_ArcCurve ac(na);
    kernel::NurbsCurve k;
    if (CurveFromON(ac, k)) *o->curve = k;
  }
  o->InvalidateDisplay();
  doc.SetModified(true);
  return "";
}

std::string QuoteArg(const std::string& a) {
  if (a.empty()) return "Enter";
  if (a.find_first_of(" \t\"") != std::string::npos) {
    std::string o = "\"";
    for (char c : a) if (c != '"') o += c;
    return o + "\"";
  }
  return a;
}

// Points the interpreter's host callbacks at the running app. The pointers
// stay valid for the life of the process (one Application).
void BindHost(CommandContext& ctx) {
  CommandEngine* eng = &ctx.Engine();
  Document* doc = &ctx.Doc();
  Application* app = &ctx.App();
  lisp::LispHost& h = TheInterp().host;
  h.print = [eng](const std::string& line) { eng->Print(line); };
  h.command = [eng](const std::vector<std::string>& parts) {
    std::string line = parts[0];
    for (size_t k = 1; k < parts.size(); ++k) line += " " + QuoteArg(parts[k]);
    return eng->RunNestedImmediate(line);
  };
  h.last_entity = [doc]() -> std::optional<long long> {
    const auto& objs = doc->Objects();
    if (objs.empty()) return std::nullopt;
    return static_cast<long long>(objs.back().id);
  };
  h.all_entities = [doc]() {
    std::vector<long long> ids;
    for (const SceneObject& o : doc->Objects()) ids.push_back(static_cast<long long>(o.id));
    return ids;
  };
  h.ent_get = [doc](long long id, std::vector<lisp::DxfItem>& out) { return GetEntity(*doc, id, out); };
  h.ent_mod = [doc](long long id, const std::vector<lisp::DxfItem>& items) { return ModEntity(*doc, id, items); };
  h.ent_del = [doc](long long id) {
    if (!doc->Find(static_cast<ObjectId>(id))) return false;
    doc->BeginChange("Lisp entdel");
    return doc->RemoveMany({static_cast<ObjectId>(id)}) == 1;
  };
  h.ent_next = [doc](std::optional<long long> after) -> std::optional<long long> {
    const auto& objs = doc->Objects();
    if (objs.empty()) return std::nullopt;
    if (!after) return static_cast<long long>(objs.front().id);
    for (size_t k = 0; k + 1 < objs.size(); ++k)
      if (static_cast<long long>(objs[k].id) == *after) return static_cast<long long>(objs[k + 1].id);
    return std::nullopt;
  };
  h.next_input = []() -> std::optional<std::string> {
    std::deque<std::string>& q = LispInputs();
    if (q.empty()) return std::nullopt;
    std::string s = q.front();
    q.pop_front();
    return s;
  };
  h.read_file = [](const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
  };
  h.on_define_command = [eng, app](const std::string& fn_name) {
    const std::string cmd = fn_name.substr(2);
    if (cmd.empty()) throw lisp::LispError("(defun c:) needs a command name after c:");
    const std::string lower = ToLower(cmd);
    const RegisteredCommand* existing = eng->Find(lower);
    if (existing && existing->factory && !LispCommands().count(lower))
      throw lisp::LispError("c:" + cmd + " would replace the built-in command " + existing->name + "; not defined");
    LispCommands().insert(lower);
    eng->Register(cmd, Immediate([fn_name](CommandContext& c) {
      lisp::Interp& in = TheInterp();
      BindHost(c);
      LispInputs().clear();
      std::deque<std::string>& pend = c.Engine().PendingInputs();   // "mybox 5 7": 5 and 7 are for the getint / getpoint calls
      while (!pend.empty()) { LispInputs().push_back(pend.front()); pend.pop_front(); }
      try {
        in.CallNamed(fn_name);
        in.FlushOutput();
        LispInputs().clear();
      } catch (const lisp::LispError& e) {
        LispInputs().clear();
        in.FlushOutput();
        c.Warn(std::string("Lisp error in ") + fn_name + ": " + e.what());
      }
    }), CommandStatus::Implemented, "User command defined in AutoLISP: (defun " + fn_name + " () ...)");
    (void)app;
  };
}

// Evaluates `text`; echoes the final value like AutoCAD does. Returns false on an error.
bool RunText(CommandContext& ctx, const std::string& text, const std::string& label, bool echo) {
  BindHost(ctx);
  lisp::Interp& in = TheInterp();
  try {
    lisp::Value v = in.EvalText(text);
    in.FlushOutput();
    if (echo) ctx.Print(label + ": " + lisp::Interp::Format(v, true));
    return true;
  } catch (const lisp::LispError& e) {
    in.FlushOutput();
    ctx.Warn(label + " error: " + e.what());
    return false;
  }
}

class LispCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::string text = ctx.Engine().PendingLispText();
    ctx.Engine().PendingLispText().clear();
    if (text.empty()) {
      std::deque<std::string>& pend = ctx.Engine().PendingInputs();
      while (!pend.empty()) { text += (text.empty() ? "" : " ") + pend.front(); pend.pop_front(); }
    }
    if (text.empty()) {
      ctx.Warn("Lisp: give an expression, e.g. Lisp (+ 1 2), or type it directly: (+ 1 2)");
    } else {
      // "(setq n (getint \"n: \")) 5": what follows the last balanced form is typed input for the get* functions.
      std::string forms, rest;
      lisp::Interp::SplitInput(text, forms, rest);
      LispInputs().clear();
      for (const std::string& tok : CommandEngine::Tokenize(rest)) LispInputs().push_back(tok);
      RunText(ctx, forms, "Lisp", true);
      LispInputs().clear();
    }
    Finish();
  }
};

class LoadLispCommand : public Command {
 public:
  void Begin(CommandContext& ctx) override {
    std::optional<std::string> path = ctx.Engine().TakePendingInput();
    if (!path) ctx.Warn("LoadLisp: give a .lsp file, e.g. LoadLisp tools.lsp");
    else RunLispFile(ctx, *path);
    Finish();
  }
};

}  // namespace

void RunLispFile(CommandContext& ctx, const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { ctx.Warn("LoadLisp: cannot open " + path); return; }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (RunText(ctx, ss.str(), "LoadLisp " + path, false)) ctx.Print("LoadLisp: loaded " + path);
}

void LoadLispAutoload(Application& app) {
  const std::filesystem::path p = std::filesystem::path(ConfigDirectory()) / "acad.lsp";
  std::error_code ec;
  if (!std::filesystem::exists(p, ec)) return;
  CommandContext ctx(app, app.Doc(), app.Engine());
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  if (RunText(ctx, ss.str(), "acad.lsp", false)) ctx.Print("Lisp: autoloaded acad.lsp");
}

void RegisterLispCommands(CommandEngine& e) {
  Reg(e, "Lisp", Make<LispCommand>(), CommandStatus::Implemented,
      "Evaluates an AutoLISP-style expression: (+ 1 2) typed directly, or Lisp (expr). A subset of AutoLISP: defun (with c:name commands), setq, "
      "if/cond/while/repeat/foreach, list/string/math functions, dotted pairs, (command ...) to run any command, entget/entmod/entdel/entnext/entlast, "
      "(ssget \"X\" '((0 . \"LINE\") (8 . \"Walls\"))) with type/layer/color filters, ssadd/ssdel, getint/getreal/getpoint/getstring/getkword/initget "
      "(they read the input typed after the expression, e.g. (setq n (getint \"n: \")) 5), a vl-* subset, (load \"f.lsp\").");
  Reg(e, "LoadLisp", Make<LoadLispCommand>(), CommandStatus::Implemented,
      "Evaluates a .lsp file: LoadLisp <file>. RunScript/LoadScript accept .lsp files too. <config dir>/acad.lsp loads at every start-up.");
}

}  // namespace dino8::app
