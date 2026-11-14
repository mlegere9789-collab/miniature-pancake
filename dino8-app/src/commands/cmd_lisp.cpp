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
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <type_traits>

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
    int n = o->curve->ControlPointCount();
    const bool closed = o->curve->IsClosed();
    if (closed && n > 3 && o->curve->ControlPointAt(0).DistanceTo(o->curve->ControlPointAt(n - 1)) < kEntTol) --n;   // the closing vertex repeats the first
    out.push_back(DI(90, n));
    out.push_back(DI(70, closed ? 1 : 0));
    for (int k = 0; k < n; ++k) out.push_back(DP(10, o->curve->ControlPointAt(k)));
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

// (entmake alist) for POINT, LINE, CIRCLE, ARC and LWPOLYLINE. "" when the entity was added (it is then (entlast)),
// otherwise why nothing was made.
std::string MakeEntity(Document& doc, const std::vector<DxfItem>& items) {
  std::string type;
  std::optional<std::string> layer_name;
  std::optional<long long> color, n90, flag70;
  std::vector<Point3d> p10s;
  std::optional<Point3d> p11, n210;
  std::optional<double> r40, a50, a51;
  for (const DxfItem& d : items) {
    const bool num = d.k == DxfItem::K::Real || d.k == DxfItem::K::Int;
    switch (d.code) {
      case -1: case 5: break;   // the name and handle are the app's to assign
      case 0:
        if (d.k != DxfItem::K::Str) return "group 0 needs the entity type as a string";
        type = d.s;
        for (char& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        break;
      case 8:
        if (d.k != DxfItem::K::Str) return "group 8 (layer) needs a string";
        layer_name = d.s;
        break;
      case 62:
        if (d.k != DxfItem::K::Int) return "group 62 (color) needs an integer";
        color = d.i;
        break;
      case 10:
        if (d.k != DxfItem::K::Point) return "group 10 needs a point";
        p10s.push_back(Point3d(d.x, d.y, d.z));
        break;
      case 11:
        if (d.k != DxfItem::K::Point) return "group 11 needs a point";
        p11 = Point3d(d.x, d.y, d.z);
        break;
      case 210:
        if (d.k != DxfItem::K::Point) return "group 210 needs a point";
        n210 = Point3d(d.x, d.y, d.z);
        break;
      case 40: if (!num) return "group 40 needs a number"; r40 = d.x; break;
      case 50: if (!num) return "group 50 needs a number"; a50 = d.x; break;
      case 51: if (!num) return "group 51 needs a number"; a51 = d.x; break;
      case 90: if (d.k != DxfItem::K::Int) return "group 90 needs an integer"; n90 = d.i; break;
      case 70: if (d.k != DxfItem::K::Int) return "group 70 needs an integer"; flag70 = d.i; break;
      default: return "group " + std::to_string(d.code) + " is not supported by entmake";
    }
  }
  if (type.empty()) return "group 0 (the entity type) is required";
  if (type != "POINT" && type != "LINE" && type != "CIRCLE" && type != "ARC" && type != "LWPOLYLINE")
    return "the entity type " + type + " is not supported (POINT, LINE, CIRCLE, ARC, LWPOLYLINE)";
  int layer = -1;
  if (layer_name) {
    layer = doc.FindLayer(*layer_name);
    if (layer < 0) return "layer \"" + *layer_name + "\" does not exist";
  }
  std::optional<int> aci;
  bool bylayer = true;
  if (color) {
    if (*color == 256) bylayer = true;
    else if (*color >= 1 && *color <= 255) { aci = static_cast<int>(*color); bylayer = false; }
    else return "group 62: color " + std::to_string(*color) + " is not supported (use 1..255, or 256 for BYLAYER)";
  }
  const bool circular = type == "CIRCLE" || type == "ARC";
  if (n210 && !circular) return "group 210 is only used by CIRCLE and ARC";
  if (n210 && (std::fabs(n210->x) > 1e-9 || std::fabs(n210->y) > 1e-9 || std::fabs(n210->z - 1) > 1e-9))
    return "group 210: only the normal 0,0,1 is supported";
  if (type != "LWPOLYLINE" && p10s.size() != 1) return "a " + type + " needs exactly one group 10";
  if (type != "LINE" && p11) return "group 11 is only used by LINE";
  if (!circular && (r40 || a50 || a51)) return "groups 40/50/51 are only used by CIRCLE and ARC";
  if (type != "LWPOLYLINE" && (n90 || flag70)) return "groups 90/70 are only used by LWPOLYLINE";
  SceneObject o;
  if (type == "POINT") {
    o = SceneObject::MakePoint(p10s[0]);
  } else if (type == "LINE") {
    if (!p11) return "a LINE needs group 11 (the end point)";
    if (p10s[0].DistanceTo(*p11) < 1e-9) return "a LINE needs two different points";
    o = SceneObject::MakeCurve(PolylineCurve({p10s[0], *p11}));
  } else if (type == "LWPOLYLINE") {
    if (p10s.size() < 3) return "an LWPOLYLINE needs at least three vertices (group 10); two make a LINE";
    if (n90 && *n90 != static_cast<long long>(p10s.size())) return "group 90 says " + std::to_string(*n90) + " vertices but " + std::to_string(p10s.size()) + " were given";
    std::vector<Point3d> pts = p10s;
    if (flag70 && (*flag70 & 1)) pts.push_back(pts.front());
    o = SceneObject::MakeCurve(PolylineCurve(pts));
  } else {
    if (!r40 || !(*r40 > 0)) return "a " + type + " needs a positive radius (group 40)";
    const ON_Plane pl(p10s[0], ON_xaxis, ON_yaxis);
    kernel::NurbsCurve k;
    if (type == "CIRCLE") {
      ON_ArcCurve ac(ON_Circle(pl, *r40));
      if (!CurveFromON(ac, k)) return "the circle could not be built";
    } else {
      if (!a50 || !a51) return "an ARC needs the start and end angles (groups 50 and 51)";
      if (!(*a51 > *a50) || *a51 - *a50 > 2 * ON_PI + 1e-9) return "groups 50/51: the end angle must be greater than the start angle and the sweep at most 2 pi";
      ON_ArcCurve ac(ON_Arc(ON_Circle(pl, *r40), ON_Interval(*a50, *a51)));
      if (!CurveFromON(ac, k)) return "the arc could not be built";
    }
    o = SceneObject::MakeCurve(k);
  }
  if (layer >= 0) o.layer_index = layer;
  if (aci) {
    const std::array<int, 3> rgb = AciToRgb(*aci);
    o.color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
  }
  o.color_by_layer = bylayer;
  doc.BeginChange("Lisp entmake");
  doc.Add(std::move(o));
  doc.SetModified(true);
  return "";
}

// The entity as polylines in the XY plane for windowed selection: a point is one vertex, a curve its sampled
// path, anything else the outline of its bounding box.
bool EntityOutline(const Document& doc, long long id, std::vector<std::vector<std::array<double, 2>>>& out) {
  const SceneObject* o = doc.Find(static_cast<ObjectId>(id));
  if (!o) return false;
  out.clear();
  if (o->kind == ObjectKind::Point) {
    out.push_back({{o->point.x, o->point.y}});
  } else if (o->kind == ObjectKind::Curve && o->curve) {
    const kernel::Interval d = o->curve->Domain();
    std::vector<std::array<double, 2>> pl;
    if (o->curve->IsLinear(kEntTol)) {
      const Point3d a = o->curve->PointAt(d.min), b = o->curve->PointAt(d.max);
      pl = {{a.x, a.y}, {b.x, b.y}};
    } else {
      const int n = std::max(64, o->curve->ControlPointCount() * 8);
      for (int k = 0; k <= n; ++k) {
        const Point3d p = o->curve->PointAt(d.min + (d.max - d.min) * k / n);
        pl.push_back({p.x, p.y});
      }
    }
    out.push_back(std::move(pl));
  } else {
    const kernel::BoundingBox b = o->BoundingBox();
    out.push_back({{b.min.x, b.min.y}, {b.max.x, b.min.y}, {b.max.x, b.max.y}, {b.min.x, b.max.y}, {b.min.x, b.min.y}});
  }
  return true;
}

// ---- running a program on a worker thread so getint & co. can wait for the user -------------------------------
// The interpreter runs on a worker thread; the main thread is inside the command's Begin / OnText and hands control
// over strictly (never both run at once). Everything the program does to the application (printing, running
// commands, reading and changing the document) is marshalled back to the main thread, so no app code ever runs on
// the worker. When a get* function needs typed input and none is queued, the worker parks and the command stays
// active with a prompt; the next thing the user types resumes it.
class LispJob {
 public:
  enum class St { Running, MainTask, NeedInput, Done };
  LispJob() = default;
  LispJob(const LispJob&) = delete;
  LispJob& operator=(const LispJob&) = delete;
  ~LispJob() { Abandon(); }

  bool Active() const { return started_ && !joined_; }

  void Start(std::function<void()> body) {
    started_ = true;
    joined_ = false;
    st_ = St::Running;
    cancel_ = false;
    th_ = std::thread([this, body = std::move(body)] {
      body();
      std::lock_guard<std::mutex> lk(m_);
      st_ = St::Done;
      cv_.notify_all();
    });
  }

  // Main thread: lets the worker run until it needs typed input or finishes, doing its main-thread requests.
  St Pump() {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
      cv_.wait(lk, [&] { return st_ != St::Running; });
      if (st_ == St::MainTask) {
        std::function<void()> t = std::move(task_);
        lk.unlock();
        std::exception_ptr err;
        try { t(); } catch (...) { err = std::current_exception(); }
        lk.lock();
        task_err_ = err;
        st_ = St::Running;
        cv_.notify_all();
        continue;
      }
      if (st_ == St::Done) {
        lk.unlock();
        if (th_.joinable()) th_.join();
        joined_ = true;
        return St::Done;
      }
      return St::NeedInput;
    }
  }
  void Provide(std::string answer) {
    std::lock_guard<std::mutex> lk(m_);
    answer_ = std::move(answer);
    st_ = St::Running;
    cv_.notify_all();
  }
  void Cancel() {
    std::lock_guard<std::mutex> lk(m_);
    cancel_ = true;
    if (st_ == St::NeedInput) st_ = St::Running;
    cv_.notify_all();
  }
  void Abandon() {
    if (!Active()) return;
    Cancel();
    Pump();
  }
  const std::string& Prompt() const { return prompt_; }
  char Kind() const { return kind_; }

  // Worker thread.
  void CallOnMain(const std::function<void()>& f) {
    std::unique_lock<std::mutex> lk(m_);
    task_ = f;
    task_err_ = nullptr;
    st_ = St::MainTask;
    cv_.notify_all();
    cv_.wait(lk, [&] { return st_ == St::Running; });
    if (task_err_) { std::exception_ptr e = task_err_; task_err_ = nullptr; std::rethrow_exception(e); }
  }
  std::string RequestInput(const std::string& prompt, char kind) {
    std::unique_lock<std::mutex> lk(m_);
    if (cancel_) throw lisp::LispError("Function cancelled");
    prompt_ = prompt;
    kind_ = kind;
    st_ = St::NeedInput;
    cv_.notify_all();
    cv_.wait(lk, [&] { return st_ == St::Running; });
    if (cancel_) throw lisp::LispError("Function cancelled");
    return answer_;
  }

 private:
  std::thread th_;
  std::mutex m_;
  std::condition_variable cv_;
  St st_ = St::Done;
  bool started_ = false, joined_ = true, cancel_ = false;
  std::function<void()> task_;
  std::exception_ptr task_err_;
  std::string prompt_, answer_;
  char kind_ = 's';
};

LispJob*& TlInputJob() {
  static thread_local LispJob* j = nullptr;   // set on a worker thread: the job it may park (null on the main thread)
  return j;
}

LispJob*& CurrentJob() {
  static LispJob* j = nullptr;   // the job whose worker is (or may be) inside the interpreter
  return j;
}

// A std::function that runs `f` on the main thread via the job.
template <class R, class... A>
std::function<R(A...)> OnMain(LispJob& job, std::function<R(A...)> f) {
  if (!f) return f;
  return [&job, f](A... a) -> R {
    if constexpr (std::is_void_v<R>) {
      job.CallOnMain([&] { f(a...); });
    } else {
      std::optional<R> r;
      job.CallOnMain([&] { r.emplace(f(a...)); });
      return std::move(*r);
    }
  };
}

void MarshalHost(lisp::LispHost& h, LispJob& job) {
  h.print = OnMain(job, h.print);
  h.command = OnMain(job, h.command);
  h.last_entity = OnMain(job, h.last_entity);
  h.all_entities = OnMain(job, h.all_entities);
  h.on_define_command = OnMain(job, h.on_define_command);
  h.read_file = OnMain(job, h.read_file);
  h.ent_get = OnMain(job, h.ent_get);
  h.ent_mod = OnMain(job, h.ent_mod);
  h.ent_del = OnMain(job, h.ent_del);
  h.ent_next = OnMain(job, h.ent_next);
  h.ent_make = OnMain(job, h.ent_make);
  h.ent_upd = OnMain(job, h.ent_upd);
  h.ent_outline = OnMain(job, h.ent_outline);
  // next_input stays on the worker: it reads the typed-input queue and may park the worker.
}

std::unique_ptr<Command> MakeLispCall(const std::string& fn_name);

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
  h.ent_make = [doc](const std::vector<lisp::DxfItem>& items) { return MakeEntity(*doc, items); };
  h.ent_upd = [doc](long long id) {
    SceneObject* o = doc->Find(static_cast<ObjectId>(id));
    if (!o) return false;
    o->InvalidateDisplay();
    return true;
  };
  h.ent_outline = [doc](long long id, std::vector<std::vector<std::array<double, 2>>>& out) { return EntityOutline(*doc, id, out); };
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
  // Typed input: first what was typed after the expression; then, when a program runs as an interactive job,
  // the worker parks until the user types the answer; otherwise nothing (the function returns nil).
  h.next_input = [](const std::string& prompt, char kind) -> std::optional<std::string> {
    std::deque<std::string>& q = LispInputs();
    if (!q.empty()) {
      std::string s = q.front();
      q.pop_front();
      return s;
    }
    if (TlInputJob()) return TlInputJob()->RequestInput(prompt, kind);
    return std::nullopt;
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
    eng->Register(cmd, [fn_name]() { return MakeLispCall(fn_name); }, CommandStatus::Implemented,
                  "User command defined in AutoLISP: (defun " + fn_name + " () ...)");
    (void)app;
  };
}

// Evaluates `text`; echoes the final value like AutoCAD does. Returns false on an error.
// Synchronous (files, acad.lsp): typed input comes only from the queue, never from a prompt.
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

// A command that runs a Lisp program as a job: Lisp <expr>, "(expr)" typed at the command line, and c:name commands.
class LispJobCommand : public Command {
 public:
  ~LispJobCommand() override {
    if (job_.Active()) {
      job_.Abandon();
      if (CurrentJob() == &job_) CurrentJob() = nullptr;
    }
  }
  bool NumberIsLiteralValue() const override { return true; }   // a number typed for getpoint is text, not a distance

  void OnText(CommandContext& ctx, const std::string& t) override { Resume(ctx, t); }
  void OnEnter(CommandContext& ctx) override { Resume(ctx, ""); }
  void OnPoint(CommandContext& ctx, kernel::Point3d p) override {
    char b[128];
    std::snprintf(b, sizeof b, "%.17g,%.17g,%.17g", p.x, p.y, p.z);
    Resume(ctx, b);
  }
  void OnCancel(CommandContext& ctx) override {
    if (!job_.Active()) return;
    BindHost(ctx);
    MarshalHost(TheInterp().host, job_);
    job_.Cancel();
    Settle(ctx);
  }

 protected:
  // Starts `eval` on the worker and runs it to the first request for input or to its end.
  void Run(CommandContext& ctx, std::function<lisp::Value()> eval, std::string label, bool echo, bool is_call) {
    label_ = std::move(label);
    echo_ = echo;
    is_call_ = is_call;
    if (CurrentJob() != nullptr) {
      // Started from inside a running program, e.g. (command "mycmd"): runs in line on this thread (the outer
      // worker is waiting for us), with the app bound directly, and cannot park for input.
      const lisp::LispHost outer = TheInterp().host;
      BindHost(ctx);
      lisp::Interp& in = TheInterp();
      try {
        lisp::Value v = eval();
        in.FlushOutput();
        ok_ = true;
        text_ = lisp::Interp::Format(v, true);
      } catch (const lisp::LispError& e) {
        ok_ = false;
        text_ = e.what();
        try { in.FlushOutput(); } catch (...) {}
      }
      TheInterp().host = outer;
      LispInputs().clear();
      Report(ctx);
      Finish();
      return;
    }
    BindHost(ctx);
    MarshalHost(TheInterp().host, job_);
    CurrentJob() = &job_;
    job_.Start([this, eval = std::move(eval)] {
      TlInputJob() = &job_;
      lisp::Interp& in = TheInterp();
      try {
        lisp::Value v = eval();
        in.FlushOutput();
        ok_ = true;
        text_ = lisp::Interp::Format(v, true);
      } catch (const lisp::LispError& e) {
        ok_ = false;
        text_ = e.what();
        try { in.FlushOutput(); } catch (...) {}
      } catch (...) {
        ok_ = false;
        text_ = "internal error";
      }
    });
    Settle(ctx);
  }

 private:
  void Resume(CommandContext& ctx, const std::string& answer) {
    if (!job_.Active()) { Finish(); return; }
    BindHost(ctx);
    MarshalHost(TheInterp().host, job_);
    job_.Provide(answer);
    Settle(ctx);
  }
  // Runs the worker until it parks or ends; a parked program leaves the command waiting for the answer.
  void Settle(CommandContext& ctx) {
    if (job_.Pump() == LispJob::St::NeedInput) {
      const std::string p = job_.Prompt().empty() ? std::string("Lisp input:") : job_.Prompt();
      if (job_.Kind() == 'p') WantPoint(p); else WantText(p);
      return;
    }
    if (CurrentJob() == &job_) CurrentJob() = nullptr;
    LispInputs().clear();
    BindHost(ctx);   // the worker is gone: the next run starts from the direct host again
    Report(ctx);
    Finish();
  }
  void Report(CommandContext& ctx) {
    if (ok_) {
      if (echo_) ctx.Print(label_ + ": " + text_);
    } else {
      ctx.Warn(is_call_ ? "Lisp error in " + label_ + ": " + text_ : label_ + " error: " + text_);
    }
  }

  LispJob job_;
  std::string label_, text_;
  bool echo_ = false, is_call_ = false, ok_ = true;
};

// (c:name): called by typing the command name; the tokens after the name are typed input for its get* calls.
class LispCallCommand : public LispJobCommand {
 public:
  explicit LispCallCommand(std::string fn) : fn_(std::move(fn)) {}
  void Begin(CommandContext& ctx) override {
    LispInputs().clear();
    std::deque<std::string>& pend = ctx.Engine().PendingInputs();   // "mybox 5 7": 5 and 7 are for the getint / getpoint calls
    while (!pend.empty()) { LispInputs().push_back(pend.front()); pend.pop_front(); }
    const std::string fn = fn_;
    Run(ctx, [fn] { return TheInterp().CallNamed(fn); }, fn_, false, true);
  }

 private:
  std::string fn_;
};

std::unique_ptr<Command> MakeLispCall(const std::string& fn_name) { return std::make_unique<LispCallCommand>(fn_name); }

class LispCommand : public LispJobCommand {
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
      Finish();
      return;
    }
    // "(setq n (getint \"n: \")) 5": what follows the last balanced form is typed input for the get* functions.
    std::string forms, rest;
    lisp::Interp::SplitInput(text, forms, rest);
    LispInputs().clear();
    for (const std::string& tok : CommandEngine::Tokenize(rest)) LispInputs().push_back(tok);
    Run(ctx, [forms] { return TheInterp().EvalText(forms); }, "Lisp", true, false);
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
