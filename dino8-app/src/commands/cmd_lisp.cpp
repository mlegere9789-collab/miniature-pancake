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

// A closed planar curve that is an ellipse (not a circle): fitted exactly through a conic least-squares fit and checked on
// samples between the fitting samples. The Ellipse command and entmake both store ellipses as rational NURBS.
struct EllipseFit {
  Point3d center;
  Vector3d major;   // centre to the end of the major axis
  double ratio = 1;
  Vector3d normal;
};

bool FitEllipse(const kernel::NurbsCurve& c, EllipseFit& f) {
  if (!c.IsClosed()) return false;
  ON_Plane pl;
  if (!c.raw().IsPlanar(&pl, 1e-6)) return false;
  const kernel::Interval d = c.Domain();
  const int N = 96;
  auto local = [&](double t, double& x, double& y) {
    const Vector3d v = c.PointAt(t) - pl.origin;
    x = ON_DotProduct(v, pl.xaxis);
    y = ON_DotProduct(v, pl.yaxis);
  };
  std::vector<double> X(N), Y(N);
  double mx = 0, my = 0;
  for (int k = 0; k < N; ++k) { local(d.min + (d.max - d.min) * k / N, X[k], Y[k]); mx += X[k]; my += Y[k]; }
  mx /= N; my /= N;
  double s = 0;
  for (int k = 0; k < N; ++k) { X[k] -= mx; Y[k] -= my; s = std::max(s, std::max(std::fabs(X[k]), std::fabs(Y[k]))); }
  if (!(s > 1e-12)) return false;
  for (int k = 0; k < N; ++k) { X[k] /= s; Y[k] /= s; }
  // normal equations of  A x^2 + B xy + C y^2 + D x + E y = 1
  double M[5][6] = {};
  for (int k = 0; k < N; ++k) {
    const double r[5] = {X[k] * X[k], X[k] * Y[k], Y[k] * Y[k], X[k], Y[k]};
    for (int i = 0; i < 5; ++i) { for (int j = 0; j < 5; ++j) M[i][j] += r[i] * r[j]; M[i][5] += r[i]; }
  }
  for (int i = 0; i < 5; ++i) {
    int p = i;
    for (int r = i + 1; r < 5; ++r) if (std::fabs(M[r][i]) > std::fabs(M[p][i])) p = r;
    if (std::fabs(M[p][i]) < 1e-12) return false;
    for (int j = 0; j < 6; ++j) std::swap(M[i][j], M[p][j]);
    for (int r = 0; r < 5; ++r) if (r != i) { const double m = M[r][i] / M[i][i]; for (int j = i; j < 6; ++j) M[r][j] -= m * M[i][j]; }
  }
  const double A = M[0][5] / M[0][0], B = M[1][5] / M[1][1], C = M[2][5] / M[2][2], D = M[3][5] / M[3][3], E = M[4][5] / M[4][4];
  const double det = 4 * A * C - B * B;
  if (!(det > 1e-12)) return false;
  const double cx = (B * E - 2 * C * D) / det, cy = (B * D - 2 * A * E) / det;
  const double h = 1 - (A * cx * cx + B * cx * cy + C * cy * cy + D * cx + E * cy);   // A u^2 + B uv + C v^2 = h
  if (!(h > 1e-12)) return false;
  const double m11 = A / h, m12 = B / (2 * h), m22 = C / h;
  const double tr = m11 + m22, dt = m11 * m22 - m12 * m12, disc = std::sqrt(std::max(0.0, tr * tr / 4 - dt));
  const double l_small = tr / 2 - disc, l_big = tr / 2 + disc;
  if (!(l_small > 1e-14)) return false;
  const double a = 1 / std::sqrt(l_small), b = 1 / std::sqrt(l_big);
  double ex, ey;   // eigenvector of l_small
  if (std::fabs(m12) > 1e-14) { ex = m12; ey = l_small - m11; } else if (m11 <= m22) { ex = 1; ey = 0; } else { ex = 0; ey = 1; }
  const double en = std::hypot(ex, ey);
  ex /= en; ey /= en;
  if (ex < -1e-12 || (std::fabs(ex) <= 1e-12 && ey < 0)) { ex = -ex; ey = -ey; }
  // verify between the fitting samples
  for (int k = 0; k < N; ++k) {
    double x, y;
    local(d.min + (d.max - d.min) * (k + 0.5) / N, x, y);
    x = (x - mx) / s - cx; y = (y - my) / s - cy;
    const double u = ex * x + ey * y, v = -ey * x + ex * y;
    if (std::fabs((u / a) * (u / a) + (v / b) * (v / b) - 1) > 1e-6) return false;
  }
  f.center = pl.origin + pl.xaxis * (mx + cx * s) + pl.yaxis * (my + cy * s);
  f.major = (pl.xaxis * ex + pl.yaxis * ey) * (a * s);
  if (f.major.x < -1e-12 || (std::fabs(f.major.x) <= 1e-12 && f.major.y < 0)) f.major = -f.major;   // the plane's own axes may be flipped: report the +x / +y end
  f.ratio = b / a;
  f.normal = pl.zaxis;
  return std::fabs(a - b) > 1e-9 * a;
}

// The DXF entity type of an object: LINE / CIRCLE / ARC / ELLIPSE / LWPOLYLINE / SPLINE for curves, POINT, and the solid / surface kinds.
std::string EntityType(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Point: return "POINT";
    case ObjectKind::Curve: {
      if (!o.curve) return "SPLINE";
      ON_Arc arc;
      if (o.curve->IsLinear(kEntTol)) return "LINE";
      if (o.curve->raw().IsArc(nullptr, &arc, kEntTol)) return arc.IsCircle() ? "CIRCLE" : "ARC";
      if (o.curve->Degree() == 1) return "LWPOLYLINE";
      EllipseFit ef;
      if (FitEllipse(*o.curve, ef)) return "ELLIPSE";
      return "SPLINE";
    }
    case ObjectKind::Surface: return "SURFACE";
    case ObjectKind::Brep: return o.brep && o.brep->raw().IsSolid() ? "3DSOLID" : "SURFACE";
    case ObjectKind::Mesh: return "MESH";
    case ObjectKind::SubD: return "SUBD";
    case ObjectKind::PointCloud: return "POINTCLOUD";
  }
  return "UNKNOWN";
}

// Symbol table records get negative entity names: layer k is -(k+1), block k is -(1000001+k).
constexpr long long kBlockIdBase = 1000000;

int LayerAciOf(const Layer& l) {
  auto b = [](float v) { return static_cast<int>(std::lround(v * 255.f)); };
  return RgbToAci(b(l.color.r), b(l.color.g), b(l.color.b));
}

// Record `index` of a table as DXF groups: 1 found, 0 past the end, -1 no such table.
int TableRecord(const Document& doc, const std::string& table, int index, std::vector<DxfItem>& out, long long& id) {
  out.clear();
  if (table == "LAYER") {
    if (index < 0 || index >= static_cast<int>(doc.Layers().size())) return 0;
    const Layer& l = doc.Layers()[static_cast<size_t>(index)];
    out.push_back(DS(0, "LAYER"));
    out.push_back(DS(2, l.name));
    out.push_back(DI(70, l.locked ? 4 : 0));
    out.push_back(DI(62, l.visible ? LayerAciOf(l) : -LayerAciOf(l)));   // a layer that is off has a negative color
    out.push_back(DS(6, l.linetype));
    id = -(static_cast<long long>(index) + 1);
    return 1;
  }
  if (table == "BLOCK") {
    if (index < 0 || index >= static_cast<int>(doc.Blocks().size())) return 0;
    const BlockDefinition& b = doc.Blocks()[static_cast<size_t>(index)];
    out.push_back(DS(0, "BLOCK"));
    out.push_back(DS(2, b.name));
    out.push_back(DI(70, 0));
    out.push_back(DP(10, b.base));
    if (!b.description.empty()) out.push_back(DS(4, b.description));
    id = -(kBlockIdBase + 1 + static_cast<long long>(index));
    return 1;
  }
  return -1;
}

// A circle / arc the way DXF reports it (AutoCAD DXF reference, "Arbitrary Axis Algorithm"): group 210 is the extrusion direction N, the
// centre (group 10) is in the object coordinate system (OCS) whose X axis is Wy x N (Wz x N when N is nearer the Y axis than 1/64), and
// groups 50 / 51 are the angles in radians, counter-clockwise about N, from that OCS X axis. A vertical normal pointing down is reported
// as +Z with the two ends swapped, which draws the same arc. The end angle is the start angle plus the sweep (it may pass 2 pi).
struct ArcFrame {
  ON_3dVector ax, ay, n;   // the OCS axes (world components)
  ON_3dPoint centre_wcs;
  ON_3dVector centre_ocs;
  double start = 0, end = 0;
};

void OcsAxes(ON_3dVector n, ON_3dVector& ax, ON_3dVector& ay) {
  if (std::fabs(n.x) < 1.0 / 64 && std::fabs(n.y) < 1.0 / 64) ax = ON_CrossProduct(ON_3dVector(0, 1, 0), n);
  else ax = ON_CrossProduct(ON_3dVector(0, 0, 1), n);
  ax.Unitize();
  ay = ON_CrossProduct(n, ax);
  ay.Unitize();
}

ON_3dVector Snap01(ON_3dVector n) {   // a normal within 1e-9 of a coordinate axis is that axis exactly
  n.Unitize();
  if (std::fabs(n.x) < 1e-9 && std::fabs(n.y) < 1e-9) return ON_3dVector(0, 0, n.z < 0 ? -1 : 1);
  return n;
}

void ArcFrameOf(const ON_Arc& arc, ArcFrame& f) {
  ON_3dVector n = Snap01(arc.Plane().Normal());
  ON_3dPoint p0 = arc.StartPoint(), p1 = arc.EndPoint();
  if (n.x == 0 && n.y == 0 && n.z < 0) { n = ON_3dVector(0, 0, 1); std::swap(p0, p1); }
  f.n = n;
  OcsAxes(n, f.ax, f.ay);
  f.centre_wcs = arc.Center();
  const ON_3dVector c(f.centre_wcs.x, f.centre_wcs.y, f.centre_wcs.z);
  f.centre_ocs = ON_3dVector(ON_DotProduct(c, f.ax), ON_DotProduct(c, f.ay), ON_DotProduct(c, f.n));
  const ON_3dVector v = p0 - f.centre_wcs;
  double s = std::atan2(ON_DotProduct(v, f.ay), ON_DotProduct(v, f.ax));
  if (s < 0) s += 2 * ON_PI;
  if (std::fabs(s) < 1e-12 || std::fabs(s - 2 * ON_PI) < 1e-12) s = 0;   // a start on the X axis is 0, not 2 pi minus rounding
  f.start = s;
  f.end = s + arc.AngleRadians();
}

bool GetEntity(const Document& doc, long long id, std::vector<DxfItem>& out) {
  if (id < 0) {
    const bool is_block = id <= -(kBlockIdBase + 1);
    const long long index = is_block ? -id - kBlockIdBase - 1 : -id - 1;
    long long rid = 0;
    std::vector<DxfItem> rec;
    if (TableRecord(doc, is_block ? "BLOCK" : "LAYER", static_cast<int>(index), rec, rid) != 1) return false;
    out.clear();
    out.push_back(DI(-1, id));
    for (const DxfItem& d : rec) out.push_back(d);
    return true;
  }
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
    ArcFrame fr;
    ArcFrameOf(arc, fr);
    out.push_back(DP(10, fr.centre_ocs.x, fr.centre_ocs.y, fr.centre_ocs.z));
    out.push_back(DR(40, arc.Radius()));
    if (type == "ARC") {
      out.push_back(DR(50, fr.start));
      out.push_back(DR(51, fr.end));
    }
    out.push_back(DP(210, fr.n.x, fr.n.y, fr.n.z));
  } else if (type == "ELLIPSE") {
    EllipseFit ef;
    FitEllipse(*o->curve, ef);
    out.push_back(DP(10, ef.center));
    out.push_back(DP(11, ef.major.x, ef.major.y, ef.major.z));
    out.push_back(DR(40, ef.ratio));
    out.push_back(DR(41, 0.0));
    out.push_back(DR(42, 2 * ON_PI));
    out.push_back(DP(210, ef.normal.x, ef.normal.y, ef.normal.z));
  } else if (type == "LWPOLYLINE") {
    int n = o->curve->ControlPointCount();
    const bool closed = o->curve->IsClosed();
    if (closed && n > 3 && o->curve->ControlPointAt(0).DistanceTo(o->curve->ControlPointAt(n - 1)) < kEntTol) --n;   // the closing vertex repeats the first
    out.push_back(DI(90, n));
    out.push_back(DI(70, closed ? 1 : 0));
    for (int k = 0; k < n; ++k) out.push_back(DP(10, o->curve->ControlPointAt(k)));
  } else if (type == "SPLINE") {
    if (o->curve) {
      const ON_NurbsCurve& nc = o->curve->raw();
      const int n = nc.CVCount(), deg = nc.Degree();
      out.push_back(DI(70, 8));
      out.push_back(DI(71, deg));
      out.push_back(DI(73, n));
      const int kc = nc.KnotCount();
      if (kc > 0) {   // the full knot vector of n + degree + 1 values: ON stores it without its first and last repeats
        out.push_back(DR(40, nc.Knot(0)));
        for (int k = 0; k < kc; ++k) out.push_back(DR(40, nc.Knot(k)));
        out.push_back(DR(40, nc.Knot(kc - 1)));
      }
      if (nc.IsRational()) for (int k = 0; k < n; ++k) out.push_back(DR(41, nc.Weight(k)));
      for (int k = 0; k < n; ++k) out.push_back(DP(10, o->curve->ControlPointAt(k)));
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
std::string ModTableRecord(Document& doc, long long id, const std::vector<DxfItem>& items);

std::string ModEntity(Document& doc, long long id, const std::vector<DxfItem>& items) {
  if (id < 0) return ModTableRecord(doc, id, items);
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
  bool poly_changed = false;
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
    } else if (type == "LWPOLYLINE" && (d.code == 10 || d.code == 70 || d.code == 90)) {
      poly_changed = true;   // the vertex list is rebuilt below from all the given groups
    } else {
      return "group " + std::to_string(d.code) + " of a " + type + " cannot be changed";
    }
  }
  if (type == "LWPOLYLINE" && !poly_changed) {
    // removing or reordering vertices leaves no changed group 10 behind, so compare the whole vertex lists
    std::vector<const DxfItem*> now, given;
    for (const DxfItem& c : cur) if (c.code == 10 && c.k == DxfItem::K::Point) now.push_back(&c);
    for (const DxfItem& d : items) if (d.code == 10 && d.k == DxfItem::K::Point) given.push_back(&d);
    bool same = now.size() == given.size();
    for (size_t k = 0; same && k < now.size(); ++k) same = SameItem(*now[k], *given[k]);
    if (!same) { poly_changed = true; changed.push_back(given.empty() ? items.front() : *given.front()); }
  }
  if (type == "ARC" && (a50 || a51)) {
    ON_Arc a;
    doc.Find(static_cast<ObjectId>(id))->curve->raw().IsArc(nullptr, &a, kEntTol);
    ArcFrame fr;
    ArcFrameOf(a, fr);
    double s = fr.start, e = fr.end;
    if (a50) s = *a50;
    if (a51) e = *a51;
    if (!(e > s) || e - s > 2 * ON_PI + 1e-9) return "groups 50/51: the end angle must be greater than the start angle and the sweep at most 2 pi";
  }
  std::vector<Point3d> poly_pts;
  bool poly_closed = false;
  if (type == "LWPOLYLINE" && poly_changed) {
    for (const DxfItem& d : items) if (d.code == 10 && d.k == DxfItem::K::Point) poly_pts.push_back(Point3d(d.x, d.y, d.z));
    if (poly_pts.size() < 3) return "an LWPOLYLINE needs at least three vertices (group 10)";
    // group 90 is the vertex count, derived from the group 10 list: a stale count is ignored
    poly_closed = std::any_of(items.begin(), items.end(), [](const DxfItem& d) { return d.code == 70 && d.i == 1; });
  }
  if (changed.empty()) return "";
  doc.BeginChange("Lisp entmod");
  SceneObject* o = doc.Find(static_cast<ObjectId>(id));
  if (type == "LWPOLYLINE" && poly_changed) {
    std::vector<Point3d> pts = poly_pts;
    if (poly_closed) pts.push_back(pts.front());
    *o->curve = PolylineCurve(pts);
  }
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
    ArcFrame fr;
    ArcFrameOf(arc, fr);
    ON_3dPoint centre = fr.centre_wcs;
    if (p10) centre = ON_3dPoint(fr.ax * p10->x + fr.ay * p10->y + fr.n * p10->z);   // group 10 is in the OCS
    const ON_Plane plane(centre, fr.ax, fr.ay);
    const double r = r40 ? *r40 : arc.Radius();
    const ON_Interval dom(a50 ? *a50 : fr.start, a51 ? *a51 : fr.end);
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

// (entmake alist) for POINT, LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE and SPLINE. "" when the entity was added (it is then
// (entlast)), otherwise why nothing was made. TEXT, MTEXT and HATCH are not entities in this app (text and hatches are
// baked into curve / surface geometry), so they are refused by name.
std::string MakeEntity(Document& doc, const std::vector<DxfItem>& items) {
  std::string type;
  for (const DxfItem& d : items) {
    if (d.code != 0) continue;
    if (d.k != DxfItem::K::Str) return "group 0 needs the entity type as a string";
    type = d.s;
    for (char& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    break;
  }
  if (type.empty()) return "group 0 (the entity type) is required";
  if (type == "TEXT" || type == "MTEXT" || type == "HATCH")
    return "the entity type " + type + " is not supported: this app has no live text or hatch entity (text and hatches become curve and surface geometry)";
  if (type != "POINT" && type != "LINE" && type != "CIRCLE" && type != "ARC" && type != "ELLIPSE" && type != "LWPOLYLINE" && type != "SPLINE")
    return "the entity type " + type + " is not supported (POINT, LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE, SPLINE)";
  static const std::map<std::string, std::set<int>> allowed = {
      {"POINT", {10}}, {"LINE", {10, 11}}, {"CIRCLE", {10, 40, 210}}, {"ARC", {10, 40, 50, 51, 210}},
      {"ELLIPSE", {10, 11, 40, 41, 42, 210}}, {"LWPOLYLINE", {10, 70, 90}}, {"SPLINE", {10, 40, 41, 70, 71, 73}}};
  static const std::set<int> known = {10, 11, 40, 41, 42, 50, 51, 70, 71, 73, 90, 210};
  std::optional<std::string> layer_name;
  std::optional<long long> color, n90, n70, n71, n73;
  std::vector<Point3d> p10s, p11s;
  std::vector<double> v40, v41;
  std::optional<double> a42, a50, a51;
  std::optional<Point3d> n210;
  for (const DxfItem& d : items) {
    const bool num = d.k == DxfItem::K::Real || d.k == DxfItem::K::Int;
    switch (d.code) {
      case -1: case 5: case 0: break;   // the name and handle are the app's to assign
      case 8:
        if (d.k != DxfItem::K::Str) return "group 8 (layer) needs a string";
        layer_name = d.s;
        break;
      case 62:
        if (d.k != DxfItem::K::Int) return "group 62 (color) needs an integer";
        color = d.i;
        break;
      default:
        if (!known.count(d.code)) return "group " + std::to_string(d.code) + " is not supported by entmake";
        if (!allowed.at(type).count(d.code)) return "group " + std::to_string(d.code) + " is not used by a " + type;
        switch (d.code) {
          case 10: if (d.k != DxfItem::K::Point) return "group 10 needs a point"; p10s.push_back(Point3d(d.x, d.y, d.z)); break;
          case 11: if (d.k != DxfItem::K::Point) return "group 11 needs a point"; p11s.push_back(Point3d(d.x, d.y, d.z)); break;
          case 210: if (d.k != DxfItem::K::Point) return "group 210 needs a point"; n210 = Point3d(d.x, d.y, d.z); break;
          case 40: if (!num) return "group 40 needs a number"; v40.push_back(d.x); break;
          case 41: if (!num) return "group 41 needs a number"; v41.push_back(d.x); break;
          case 42: if (!num) return "group 42 needs a number"; a42 = d.x; break;
          case 50: if (!num) return "group 50 needs a number"; a50 = d.x; break;
          case 51: if (!num) return "group 51 needs a number"; a51 = d.x; break;
          case 90: if (d.k != DxfItem::K::Int) return "group 90 needs an integer"; n90 = d.i; break;
          case 70: if (d.k != DxfItem::K::Int) return "group 70 needs an integer"; n70 = d.i; break;
          case 71: if (d.k != DxfItem::K::Int) return "group 71 needs an integer"; n71 = d.i; break;
          case 73: if (d.k != DxfItem::K::Int) return "group 73 needs an integer"; n73 = d.i; break;
          default: break;
        }
    }
  }
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
  if (n210 && (std::fabs(n210->x) > 1e-9 || std::fabs(n210->y) > 1e-9 || std::fabs(n210->z - 1) > 1e-9))
    return "group 210: only the normal 0,0,1 is supported";
  if (v40.size() > 1 && type != "SPLINE") return "a " + type + " takes one group 40";
  if (type != "LWPOLYLINE" && type != "SPLINE" && p10s.size() != 1) return "a " + type + " needs exactly one group 10";
  SceneObject o;
  if (type == "POINT") {
    o = SceneObject::MakePoint(p10s[0]);
  } else if (type == "LINE") {
    if (p11s.size() != 1) return "a LINE needs group 11 (the end point)";
    if (p10s[0].DistanceTo(p11s[0]) < 1e-9) return "a LINE needs two different points";
    o = SceneObject::MakeCurve(PolylineCurve({p10s[0], p11s[0]}));
  } else if (type == "LWPOLYLINE") {
    if (p10s.size() < 3) return "an LWPOLYLINE needs at least three vertices (group 10); two make a LINE";
    if (n90 && *n90 != static_cast<long long>(p10s.size())) return "group 90 says " + std::to_string(*n90) + " vertices but " + std::to_string(p10s.size()) + " were given";
    std::vector<Point3d> pts = p10s;
    if (n70 && (*n70 & 1)) pts.push_back(pts.front());
    o = SceneObject::MakeCurve(PolylineCurve(pts));
  } else if (type == "CIRCLE" || type == "ARC") {
    if (v40.empty() || !(v40[0] > 0)) return "a " + type + " needs a positive radius (group 40)";
    const double r = v40[0];
    const ON_Plane pl(p10s[0], ON_xaxis, ON_yaxis);
    kernel::NurbsCurve k;
    if (type == "CIRCLE") {
      if (a50 || a51) return "groups 50/51 are only used by an ARC";
      ON_ArcCurve ac(ON_Circle(pl, r));
      if (!CurveFromON(ac, k)) return "the circle could not be built";
    } else {
      if (!a50 || !a51) return "an ARC needs the start and end angles (groups 50 and 51)";
      if (!(*a51 > *a50) || *a51 - *a50 > 2 * ON_PI + 1e-9) return "groups 50/51: the end angle must be greater than the start angle and the sweep at most 2 pi";
      ON_ArcCurve ac(ON_Arc(ON_Circle(pl, r), ON_Interval(*a50, *a51)));
      if (!CurveFromON(ac, k)) return "the arc could not be built";
    }
    o = SceneObject::MakeCurve(k);
  } else if (type == "ELLIPSE") {
    // 10 centre, 11 the end of the major axis relative to the centre, 40 minor / major ratio, 41/42 start and end (full only)
    if (p11s.size() != 1) return "an ELLIPSE needs group 11 (the major axis end, relative to the centre)";
    const Vector3d major(p11s[0].x, p11s[0].y, p11s[0].z);
    const double a = major.Length();
    if (!(a > 1e-9)) return "an ELLIPSE needs a major axis of nonzero length (group 11)";
    if (v40.empty() || !(v40[0] > 0) || v40[0] > 1 + 1e-12) return "an ELLIPSE needs a ratio in (0, 1] (group 40)";
    if (std::fabs(major.z) > 1e-9) return "group 11: the major axis must lie in the XY plane";
    if ((v41.size() > 1) || (!v41.empty() && std::fabs(v41[0]) > 1e-9) || (a42 && std::fabs(*a42 - 2 * ON_PI) > 1e-9))
      return "partial ellipses (groups 41/42) are not supported; give 0 and 2 pi or leave them out";
    if (v40[0] > 1 - 1e-9) return "an ELLIPSE with ratio 1 is a circle: make a CIRCLE";
    Vector3d xa = major;
    xa.Unitize();
    const Vector3d ya = ON_CrossProduct(Vector3d(0, 0, 1), xa);
    ON_Ellipse el(ON_Plane(p10s[0], xa, ya), a, a * v40[0]);
    ON_NurbsCurve nc;
    kernel::NurbsCurve k;
    if (!el.GetNurbForm(nc)) return "the ellipse could not be built";
    k.raw() = nc;
    o = SceneObject::MakeCurve(k);
  } else {   // SPLINE: 71 degree, 40 knot vector (n + degree + 1 values, clamped), 41 weights, 10 control points
    if (!p11s.empty()) return "fit-point splines (group 11) are not supported; give control points (group 10)";
    if (!n71 || *n71 < 1 || *n71 > 9) return "a SPLINE needs its degree in group 71 (1..9)";
    const int deg = static_cast<int>(*n71), n = static_cast<int>(p10s.size());
    if (n < deg + 1) return "a degree " + std::to_string(deg) + " SPLINE needs at least " + std::to_string(deg + 1) + " control points (group 10)";
    if (n73 && *n73 != n) return "group 73 says " + std::to_string(*n73) + " control points but " + std::to_string(n) + " were given";
    if (n70 && (*n70 & 3)) return "closed or periodic splines (group 70 bits 1 and 2) are not supported";
    if (!v41.empty()) {
      if (static_cast<int>(v41.size()) != n) return "group 41: one weight per control point is needed (" + std::to_string(n) + ")";
      for (double w : v41) if (!(w > 0)) return "group 41: weights must be positive";
    }
    ON_NurbsCurve nc;
    if (v40.empty()) {
      if (!v41.empty()) return "weights (group 41) need an explicit knot vector (group 40)";
      ON_3dPointArray arr;
      for (const Point3d& p : p10s) arr.Append(p);
      if (!nc.CreateClampedUniformNurbs(3, deg + 1, n, arr.Array())) return "the control points do not make a curve";
    } else {
      if (static_cast<int>(v40.size()) != n + deg + 1) return "group 40: " + std::to_string(n + deg + 1) + " knots (control points + degree + 1) are needed, " + std::to_string(v40.size()) + " given";
      for (size_t k = 1; k < v40.size(); ++k) if (v40[k] < v40[k - 1]) return "group 40: the knots must not decrease";
      for (int k = 1; k <= deg; ++k)
        if (v40[static_cast<size_t>(k)] != v40[0] || v40[v40.size() - 1 - static_cast<size_t>(k)] != v40.back()) return "group 40: the knot vector must be clamped (the first and last " + std::to_string(deg + 1) + " knots equal)";
      if (!(v40.back() > v40.front())) return "group 40: the knot vector must span a positive interval";
      const bool rational = !v41.empty();
      if (!nc.Create(3, rational, deg + 1, n)) return "the spline could not be created";
      for (int k = 0; k < n; ++k) {
        if (rational) {
          const double w[4] = {p10s[static_cast<size_t>(k)].x, p10s[static_cast<size_t>(k)].y, p10s[static_cast<size_t>(k)].z, v41[static_cast<size_t>(k)]};
          nc.SetCV(k, ON::point_style::euclidean_rational, w);
        } else {
          nc.SetCV(k, p10s[static_cast<size_t>(k)]);
        }
      }
      for (int k = 0; k < n + deg - 1; ++k) nc.SetKnot(k, v40[static_cast<size_t>(k) + 1]);
    }
    if (!nc.IsValid()) return "the control points and knots do not make a valid curve";
    kernel::NurbsCurve k;
    k.raw() = nc;
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

// ---- system variables ----------------------------------------------------------------------------------------------
struct UserVars {
  long long i[5] = {0, 0, 0, 0, 0};
  double r[5] = {0, 0, 0, 0, 0};
  std::string s[5];
};
UserVars& TheUserVars() {
  static UserVars u;
  return u;
}

// getvar: CLAYER, EXTMIN, EXTMAX, USERI1-5, USERR1-5, USERS1-5.
bool GetSysVar(const Document& doc, const std::string& name, DxfItem& out) {
  if (name == "CLAYER") {
    const int cur = doc.CurrentLayer();
    if (cur < 0 || cur >= static_cast<int>(doc.Layers().size())) return false;
    out = DS(0, doc.Layers()[static_cast<size_t>(cur)].name);
    return true;
  }
  if (name == "EXTMIN" || name == "EXTMAX") {
    double lo[3] = {1e20, 1e20, 1e20}, hi[3] = {-1e20, -1e20, -1e20};
    for (const SceneObject& o : doc.Objects()) {
      const kernel::BoundingBox b = o.BoundingBox();
      lo[0] = std::min(lo[0], b.min.x); lo[1] = std::min(lo[1], b.min.y); lo[2] = std::min(lo[2], b.min.z);
      hi[0] = std::max(hi[0], b.max.x); hi[1] = std::max(hi[1], b.max.y); hi[2] = std::max(hi[2], b.max.z);
    }
    const double* v = name == "EXTMIN" ? lo : hi;
    out = DP(0, v[0], v[1], v[2]);
    return true;
  }
  if (name.size() == 6 && name.compare(0, 5, "USERI") == 0 && name[5] >= '1' && name[5] <= '5') { out = DI(0, TheUserVars().i[name[5] - '1']); return true; }
  if (name.size() == 6 && name.compare(0, 5, "USERR") == 0 && name[5] >= '1' && name[5] <= '5') { out = DR(0, TheUserVars().r[name[5] - '1']); return true; }
  if (name.size() == 6 && name.compare(0, 5, "USERS") == 0 && name[5] >= '1' && name[5] <= '5') { out = DS(0, TheUserVars().s[name[5] - '1']); return true; }
  return false;
}

std::string SetSysVar(Document& doc, const std::string& name, const DxfItem& v) {
  DxfItem probe;
  if (!GetSysVar(doc, name, probe)) return "unknown system variable (CLAYER, EXTMIN, EXTMAX, USERI1-5, USERR1-5, USERS1-5)";
  if (name == "EXTMIN" || name == "EXTMAX") return "the variable is read-only";
  if (name == "CLAYER") {
    if (v.k != DxfItem::K::Str) return "CLAYER needs a layer name (a string)";
    const int idx = doc.FindLayer(v.s);
    if (idx < 0) return "layer \"" + v.s + "\" does not exist";
    doc.SetCurrentLayer(idx);
    return "";
  }
  const int k = name[5] - '1';
  if (name[4] == 'I') { if (v.k != DxfItem::K::Int) return name + " needs an integer"; TheUserVars().i[k] = v.i; return ""; }
  if (name[4] == 'R') { if (v.k != DxfItem::K::Real && v.k != DxfItem::K::Int) return name + " needs a number"; TheUserVars().r[k] = v.x; return ""; }
  if (v.k != DxfItem::K::Str) return name + " needs a string";
  TheUserVars().s[k] = v.s;
  return "";
}

// entmod of a symbol table record: a LAYER's color (62, negative = off) and locked bit (70 bit 4).
std::string ModTableRecord(Document& doc, long long id, const std::vector<DxfItem>& items) {
  std::vector<DxfItem> cur;
  if (!GetEntity(doc, id, cur)) return "no entity " + std::to_string(id);
  if (cur.size() < 2 || cur[1].s != "LAYER") return "only LAYER table records can be changed with entmod";
  const int index = static_cast<int>(-id - 1);
  std::optional<long long> col, flags;
  for (const DxfItem& d : items) {
    bool same = false;
    for (const DxfItem& c : cur) if (c.code == d.code && SameItem(c, d)) { same = true; break; }
    if (same) continue;
    if (d.code == 62 && d.k == DxfItem::K::Int) {
      const long long a = d.i < 0 ? -d.i : d.i;
      if (a < 1 || a > 255) return "group 62: color " + std::to_string(d.i) + " is not supported (1..255; negative turns the layer off)";
      col = d.i;
    } else if (d.code == 70 && d.k == DxfItem::K::Int) {
      if (d.i & ~4LL) return "group 70: only bit 4 (locked) is supported";
      flags = d.i;
    } else {
      return "group " + std::to_string(d.code) + " of a LAYER record cannot be changed";
    }
  }
  if (!col && !flags) return "";
  doc.BeginChange("Lisp entmod");
  Layer& l = doc.Layers()[static_cast<size_t>(index)];
  if (col) {
    const std::array<int, 3> rgb = AciToRgb(static_cast<int>(*col < 0 ? -*col : *col));
    l.color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
    l.visible = *col > 0;
  }
  if (flags) l.locked = (*flags & 4) != 0;
  doc.SetModified(true);
  return "";
}

// ---- curve queries for vlax-curve-* -----------------------------------------------------------------------------
// The parameter is the NURBS parameter of the stored curve; distances come from a 20000-segment chord sum.
std::string CurveOp(const Document& doc, long long id, const std::string& op, const std::vector<double>& in, std::vector<double>& out) {
  const SceneObject* o = doc.Find(static_cast<ObjectId>(id));
  if (!o) return "no such entity";
  if (o->kind != ObjectKind::Curve || !o->curve) return "the entity is not a curve";
  const kernel::NurbsCurve& c = *o->curve;
  const kernel::Interval d = c.Domain();
  const double span = d.max - d.min;
  out.clear();
  auto need = [&](size_t n) { return in.size() == n; };
  if (op == "startparam") { out = {d.min}; return ""; }
  if (op == "endparam") { out = {d.max}; return ""; }
  if (op == "isclosed") { out = {c.IsClosed() ? 1.0 : 0.0}; return ""; }
  if (op == "pointat") {
    if (!need(1)) return "one parameter is needed";
    if (in[0] < d.min - 1e-9 || in[0] > d.max + 1e-9) return "the parameter is outside the curve's domain [" + std::to_string(d.min) + ", " + std::to_string(d.max) + "]";
    const Point3d p = c.PointAt(std::min(std::max(in[0], d.min), d.max));
    out = {p.x, p.y, p.z};
    return "";
  }
  if (op == "closestpoint" || op == "paramatpoint") {
    if (!need(3)) return "a point is needed";
    const Point3d q(in[0], in[1], in[2]);
    double t = c.ClosestPointParameter(q, 2000);
    double lo = std::max(d.min, t - 2 * span / 2000), hi = std::min(d.max, t + 2 * span / 2000);
    for (int it = 0; it < 80; ++it) {   // golden-section refinement of the squared distance
      const double m1 = lo + (hi - lo) * 0.381966, m2 = lo + (hi - lo) * 0.618034;
      if (c.PointAt(m1).DistanceTo(q) < c.PointAt(m2).DistanceTo(q)) hi = m2; else lo = m1;
    }
    const double tr = (lo + hi) / 2, tk = std::min(std::max(c.ClosestPointParameter(q, 2000), d.min), d.max);
    t = c.PointAt(tr).DistanceTo(q) <= c.PointAt(tk).DistanceTo(q) ? tr : tk;
    const Point3d p = c.PointAt(t);
    if (op == "closestpoint") out = {p.x, p.y, p.z}; else out = {t, p.DistanceTo(q)};
    return "";
  }
  // arc length: a chord sum over N equal parameter steps
  const int N = 20000;
  std::vector<double> cum(static_cast<size_t>(N) + 1, 0.0);
  Point3d prev = c.PointAt(d.min);
  for (int k = 1; k <= N; ++k) {
    const Point3d p = c.PointAt(d.min + span * k / N);
    cum[static_cast<size_t>(k)] = cum[static_cast<size_t>(k) - 1] + p.DistanceTo(prev);
    prev = p;
  }
  const double total = cum.back();
  if (op == "distatparam") {
    if (!need(1)) return "one parameter is needed";
    if (in[0] < d.min - 1e-9 || in[0] > d.max + 1e-9) return "the parameter is outside the curve's domain";
    const double x = (std::min(std::max(in[0], d.min), d.max) - d.min) / span * N;
    const int i = std::min(static_cast<int>(x), N - 1);
    out = {cum[static_cast<size_t>(i)] + (cum[static_cast<size_t>(i) + 1] - cum[static_cast<size_t>(i)]) * (x - i)};
    return "";
  }
  if (op == "paramatdist" || op == "pointatdist") {
    if (!need(1)) return "one distance is needed";
    if (in[0] < -1e-9 || in[0] > total + 1e-9) return "the distance is outside the curve (length " + std::to_string(total) + ")";
    const double s = std::min(std::max(in[0], 0.0), total);
    const size_t i = static_cast<size_t>(std::min<long>(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin() - 1, N - 1));
    const double seg = cum[i + 1] - cum[i];
    const double t = d.min + span * (static_cast<double>(i) + (seg > 0 ? (s - cum[i]) / seg : 0.0)) / N;
    if (op == "paramatdist") { out = {t}; return ""; }
    const Point3d p = c.PointAt(t);
    out = {p.x, p.y, p.z};
    return "";
  }
  return "unknown curve operation " + op;
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
  h.getvar = OnMain(job, h.getvar);
  h.setvar = OnMain(job, h.setvar);
  h.tbl_at = OnMain(job, h.tbl_at);
  h.curve_op = OnMain(job, h.curve_op);
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
  h.getvar = [doc](const std::string& name, lisp::DxfItem& out) { return GetSysVar(*doc, name, out); };
  h.setvar = [doc](const std::string& name, const lisp::DxfItem& v) { return SetSysVar(*doc, name, v); };
  h.tbl_at = [doc](const std::string& table, int index, std::vector<lisp::DxfItem>& out, long long& id) { return TableRecord(*doc, table, index, out, id); };
  h.curve_op = [doc](long long id, const std::string& op, const std::vector<double>& in, std::vector<double>& out) { return CurveOp(*doc, id, op, in, out); };
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
