// Dino 8 case runner for the Rhino 8 reference oracle (parity/oracle/rhino).
//
// Reads parity/oracle/rhino/cases.jsonl (authored by us, see gen_cases.py) and executes every case through the Dino 8 KERNEL API,
// measuring exactly the fields each case lists in `measure`. One line per case is printed to stdout in the golden record shape,
//   D8RES {"build":"dino8-<commit>","error":"<class>","id":"...","ms":N,"ok":true|false,"out":{...},"tolerance":0.001}
// so that compare.py can be run on the `D8RES ` lines read from the CI job log (artifact download is blocked by the proxy).
//
// Rules this runner keeps (CLEANROOM.md rule 3):
//   * Nothing is tuned to match the golden. Every operation is the kernel call a user of the app reaches, with the document
//     tolerance 0.001 where the kernel takes one (IntersectOptions default, BooleanCombineGeneral default, ...). Where Dino 8
//     refuses or throws the record is ok=false with error = the exception class name (or "EmptyResult" when an operation
//     returned no geometry, the analogue of the reference returning no result).
//   * A measurement that throws (e.g. Area() of a pseudo-trimmed face) leaves the field out of `out` and is named in "note";
//     the comparer then reports it as a divergence for that field. Diagnostic extras ("route", "note", "msg", "alt") are not
//     part of the golden schema; compare.py ignores them.
//   * Each case runs in a forked child with an alarm, so one crash or hang is recorded for that case and the sweep goes on.
//
// Routes mirrored from the app (no kernel entry exists that does the whole job):
//   * Boolean: the app's order (cmd_boolean.cpp): the planar engine when both operands are planar-faced, otherwise (or when it
//     refuses) the general engine + SealGeneralBooleanResult, then the app's sew-the-naked-edges healing. The app's mesh-boolean
//     cross-check and mesh fallback are not replicated (a mesh has no faces/valid/solid to measure).
//   * Offset of a closed solid: the app's TryExactOffsetSolid (cmd_surface.cpp) for solid=No: a whole sphere or a plain capped
//     cylinder is the same primitive with its radius moved; a planar convex solid goes to kernel OffsetSolidConvexPlanar.
//   * Chamfer/fillet of all edges: kernel FilletConvexEdges / ChamferConvexEdges (batch, all edges); when the batch chamfer
//     refuses a shared vertex (its documented scope) every edge is chamfered one at a time with ChamferConvexEdge, as picking
//     the edges one after the other in the app does.
//
// The test is a measurement, not an expectation: it only fails when the case file is missing or a record is lost. It runs only
// when DINO8_TEST_FILTER names it (so the ordinary full kernel run is not slowed); D8_ORACLE_CASES overrides the case file.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <typeinfo>
#include <unistd.h>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/curve_interpolate.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/offset_solid.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;
namespace k = dino8::kernel;

constexpr double kDocTol = 0.001;  // the document tolerance the cases and the golden records state
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------------------------------------------
// Minimal JSON reader (cases.jsonl is plain JSON written by json.dumps: objects, arrays, numbers, strings, booleans).
struct J {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;
  const J& at(const std::string& key) const {
    for (const auto& kv : o) if (kv.first == key) return kv.second;
    throw std::runtime_error("case field missing: " + key);
  }
  bool has(const std::string& key) const {
    for (const auto& kv : o) if (kv.first == key) return true;
    return false;
  }
  double num() const { if (t != Num) throw std::runtime_error("case field is not a number"); return n; }
  const std::string& str() const { if (t != Str) throw std::runtime_error("case field is not a string"); return s; }
  bool boolean() const { if (t != Bool) throw std::runtime_error("case field is not a boolean"); return b; }
};

struct JParser {
  const std::string& x;
  size_t i = 0;
  explicit JParser(const std::string& text) : x(text) {}
  void ws() { while (i < x.size() && (x[i] == ' ' || x[i] == '\t' || x[i] == '\r' || x[i] == '\n')) ++i; }
  [[noreturn]] void fail(const char* m) { throw std::runtime_error(std::string("json: ") + m); }
  J value() {
    ws();
    if (i >= x.size()) fail("unexpected end");
    J v;
    const char c = x[i];
    if (c == '{') {
      v.t = J::Obj; ++i; ws();
      if (x[i] == '}') { ++i; return v; }
      for (;;) {
        ws(); std::string key = string_();
        ws(); if (x[i] != ':') fail("expected ':'"); ++i;
        v.o.emplace_back(key, value());
        ws();
        if (x[i] == ',') { ++i; continue; }
        if (x[i] == '}') { ++i; break; }
        fail("expected ',' or '}'");
      }
    } else if (c == '[') {
      v.t = J::Arr; ++i; ws();
      if (x[i] == ']') { ++i; return v; }
      for (;;) {
        v.a.push_back(value());
        ws();
        if (x[i] == ',') { ++i; continue; }
        if (x[i] == ']') { ++i; break; }
        fail("expected ',' or ']'");
      }
    } else if (c == '"') {
      v.t = J::Str; v.s = string_();
    } else if (x.compare(i, 4, "true") == 0) { v.t = J::Bool; v.b = true; i += 4;
    } else if (x.compare(i, 5, "false") == 0) { v.t = J::Bool; v.b = false; i += 5;
    } else if (x.compare(i, 4, "null") == 0) { v.t = J::Null; i += 4;
    } else {
      char* end = nullptr;
      v.n = std::strtod(x.c_str() + i, &end);
      if (end == x.c_str() + i) fail("bad number");
      v.t = J::Num; i = static_cast<size_t>(end - x.c_str());
    }
    return v;
  }
  std::string string_() {
    if (x[i] != '"') fail("expected string");
    ++i;
    std::string r;
    while (i < x.size() && x[i] != '"') {
      if (x[i] == '\\' && i + 1 < x.size()) {
        ++i;
        switch (x[i]) { case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'u': r += '?'; i += 4; break; default: r += x[i]; }
      } else {
        r += x[i];
      }
      ++i;
    }
    ++i;
    return r;
  }
};

// ---------------------------------------------------------------------------------------------------------------------
// Output record: field name -> already rendered JSON text.
std::string Num(double v) {
  if (!std::isfinite(v)) return "null";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.12g", v);
  return buf;
}
std::string Esc(const std::string& s) {
  std::string r = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { r += '\\'; r += static_cast<char>(c); }
    else if (c < 0x20) r += ' ';
    else r += static_cast<char>(c);
  }
  return r + "\"";
}

struct Rec {
  std::map<std::string, std::string> out;  // measured fields (the golden "out")
  std::map<std::string, std::string> extra;  // diagnostics: route, note, alt
  void Note(const std::string& what) {
    std::string& n = extra["note"];
    n += (n.empty() ? "" : "; ") + what.substr(0, 160);
  }
};

// An operation that returned no geometry or declined; error = this class name.
struct Refusal : std::runtime_error {
  Refusal(const std::string& name, const std::string& msg) : std::runtime_error(msg), name_(name) {}
  std::string name_;
};

std::string ClassName(const std::exception& e) {
  if (const auto* r = dynamic_cast<const Refusal*>(&e)) return r->name_;
  int st = 0;
  char* d = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &st);
  std::string r = (st == 0 && d) ? d : typeid(e).name();
  std::free(d);
  return r;
}

Point3d P3(const J& v) { return Point3d(v.a.at(0).num(), v.a.at(1).num(), v.a.at(2).num()); }

// ---------------------------------------------------------------------------------------------------------------------
// Measuring a B-rep: exactly the fields listed in the case.
bool Wants(const std::vector<std::string>& want, const char* key) { return std::find(want.begin(), want.end(), key) != want.end(); }

// The last measured B-rep, kept so the child can add a tessellation-based bounding box as a diagnostic after the record is written.
Brep* g_last_brep = nullptr;
bool g_last_wants_bbox = false;

void MeasureBrep(const Brep& b, const std::vector<std::string>& want, Rec& rec) {
  delete g_last_brep;
  g_last_brep = new Brep(b);
  g_last_wants_bbox = Wants(want, "bbox");
  const bool solid = b.raw().IsSolid();
  const bool valid = b.raw().IsValid();
  if (Wants(want, "area")) {
    try { rec.out["area"] = Num(b.Area()); } catch (const std::exception& e) { rec.Note(std::string("area: ") + ClassName(e) + ": " + e.what()); }
  }
  if (Wants(want, "volume")) {
    double v = 0;
    bool got = false;
    std::string why;
    if (solid && valid) {
      try { v = b.VolumeOfSealedSolid(); got = true; } catch (const std::exception& e) { why = e.what(); }
    }
    if (!got) {
      try { v = b.Volume(); got = true; } catch (const std::exception& e) { why = e.what(); }
    }
    if (got) rec.out["volume"] = Num(v);
    else rec.Note("volume: " + why);
  }
  if (Wants(want, "bbox")) {
    try {
      const k::BoundingBox bb = b.GetTightBoundingBox();
      rec.out["bbox"] = "[" + Num(bb.min.x) + "," + Num(bb.min.y) + "," + Num(bb.min.z) + "," + Num(bb.max.x) + "," + Num(bb.max.y) + "," + Num(bb.max.z) + "]";
    } catch (const std::exception& e) { rec.Note(std::string("bbox: ") + e.what()); }
  }
  if (Wants(want, "valid")) rec.out["valid"] = valid ? "true" : "false";
  if (Wants(want, "solid")) rec.out["solid"] = solid ? "true" : "false";
  const int F = b.LiveFaceCount(), E = b.LiveEdgeCount(), V = b.LiveVertexCount();
  if (Wants(want, "faces")) rec.out["faces"] = std::to_string(F);
  if (Wants(want, "edges")) rec.out["edges"] = std::to_string(E);
  if (Wants(want, "vertices")) rec.out["vertices"] = std::to_string(V);
  if (Wants(want, "euler")) rec.out["euler"] = std::to_string(V - E + F);
}

void RequireGeometry(const Brep& b) {
  if (b.raw().m_F.Count() == 0 || b.LiveFaceCount() == 0) throw Refusal("EmptyResult", "the operation returned no geometry");
}

// ---------------------------------------------------------------------------------------------------------------------
// Shapes.
Brep MakeShape(const J& s) {
  const std::string& prim = s.at("prim").str();
  const J& mv = s.at("move");
  const double x = mv.a.at(0).num(), y = mv.a.at(1).num(), z = mv.a.at(2).num();
  if (prim == "box") return Brep::BoxWelded(x, y, z, x + s.at("dx").num(), y + s.at("dy").num(), z + s.at("dz").num());
  if (prim == "sphere") return Brep::SphereWelded(Point3d(x, y, z), s.at("r").num());
  if (prim == "cylinder") return Brep::CylinderWelded(Point3d(x, y, z), Vector3d(0, 0, 1), s.at("r").num(), s.at("h").num());
  if (prim == "cone") return Brep::ConeWelded(Point3d(x, y, z), Vector3d(0, 0, 1), s.at("r").num(), 0.0, s.at("h").num());
  if (prim == "torus") return Brep::TorusWelded(Point3d(x, y, z), Vector3d(0, 0, 1), s.at("R").num(), s.at("r").num());
  throw std::runtime_error("unknown primitive " + prim);
}

NurbsCurve CircleCurve(const Point3d& c, double r) {
  ON_NurbsCurve n;
  ON_Circle(ON_Plane(c, ON_3dVector(0, 0, 1)), r).GetNurbForm(n);
  NurbsCurve out;
  out.raw() = n;
  return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Boolean (the app's order: planar engine, else general engine + seal, then heal).
Brep DoBoolean(const Brep& a, const Brep& b, k::BooleanOp op, Rec& rec) {
  bool planar = false;
  try { a.PlanarFaces(); b.PlanarFaces(); planar = true; } catch (const std::exception&) {}
  std::string planar_why;
  if (planar) {
    try {
      Brep r = k::BooleanCombinePlanarNAry({a}, {b}, op);
      rec.extra["route"] = Esc("planar");
      RequireGeometry(r);
      return r;
    } catch (const Refusal&) {
      throw;
    } catch (const std::exception& e) {
      planar_why = ClassName(e) + ": " + e.what();
    }
  }
  rec.extra["route"] = Esc(planar ? "planar-declined>general" : "general");
  if (!planar_why.empty()) rec.Note("planar declined: " + planar_why);
  Brep raw = k::BooleanCombineGeneralNAry({a}, {b}, op, kDocTol);
  if (raw.raw().m_F.Count() == 0) throw Refusal("EmptyResult", "the general engine returned no geometry");
  double va = std::nan(""), vb = std::nan("");
  try { va = std::fabs(a.Volume()); vb = std::fabs(b.Volume()); } catch (const std::exception&) { va = vb = std::nan(""); }
  try {
    k::SealedGeneralBoolean sb = k::SealGeneralBooleanResult(raw, op, va, vb);
    if (sb.brep.raw().m_F.Count() > 0) return sb.brep;
  } catch (const std::exception& e) {
    rec.Note(std::string("seal declined: ") + ClassName(e) + ": " + e.what());
  }
  // The app's sewing fallback (HealGeneralBooleanResult).
  if (raw.raw().IsSolid()) return raw;
  ON_BoundingBox bb;
  raw.raw().GetBoundingBox(bb);
  const double diag = bb.Diagonal().Length();
  if (std::isfinite(diag) && diag > 0) {
    for (double fraction : {2e-4, 1e-3, 3e-3, 1e-2}) {
      Brep c = raw;
      const double tol = fraction * diag;
      try {
        for (int pass = 0; pass < 3 && !c.raw().IsSolid(); ++pass) { c.SewTJunctions(tol); c.JoinNakedEdges(tol); }
      } catch (const std::exception&) { continue; }
      if (c.raw().IsSolid()) { rec.extra["route"] = Esc("general-healed"); return c; }
    }
  }
  throw Refusal("BooleanNotClosed", "the general engine's result could not be sewn into a closed solid");
}

// ---------------------------------------------------------------------------------------------------------------------
// Blend of all edges.
using EdgeList = std::vector<std::pair<Point3d, Point3d>>;

EdgeList AllEdges(const Brep& b) {
  EdgeList e;
  const ON_Brep& r = b.raw();
  for (int i = 0; i < r.m_E.Count(); ++i) {
    const ON_BrepEdge& edge = r.m_E[i];
    if (edge.m_edge_index < 0) continue;
    e.emplace_back(edge.PointAtStart(), edge.PointAtEnd());
  }
  return e;
}

// The live straight edge of `cur` lying on the segment a-b (the longest one), or false.
bool FindEdgeOn(const Brep& cur, const Point3d& a, const Point3d& b, Point3d& p0, Point3d& p1) {
  const Vector3d d = b - a;
  const double len = d.Length();
  if (!(len > 0)) return false;
  const Vector3d u = d / len;
  const double tol = 1e-7 * std::max(1.0, len);
  double best = 0;
  const ON_Brep& r = cur.raw();
  for (int i = 0; i < r.m_E.Count(); ++i) {
    const ON_BrepEdge& e = r.m_E[i];
    if (e.m_edge_index < 0) continue;
    const Point3d s = e.PointAtStart(), t = e.PointAtEnd();
    auto off = [&](const Point3d& p) { const Vector3d w = p - a; return (w - (w * u) * u).Length(); };
    auto along = [&](const Point3d& p) { return (p - a) * u; };
    if (off(s) > tol || off(t) > tol) continue;
    if (along(s) < -tol || along(s) > len + tol || along(t) < -tol || along(t) > len + tol) continue;
    const double l = (t - s).Length();
    if (l > best + 1e-12) { best = l; p0 = s; p1 = t; }
  }
  return best > 1e-9;
}

Brep DoBlend(const J& args, Rec& rec) {
  const Brep box = MakeShape(args.at("shape"));
  const double amount = args.at("radius").num();
  const std::string& kind = args.at("blend").str();
  const EdgeList edges = AllEdges(box);
  if (kind == "fillet") {
    rec.extra["route"] = Esc("FilletConvexEdges");
    return k::FilletConvexEdges(box, edges, amount);
  }
  if (kind != "chamfer") throw std::runtime_error("unknown blend " + kind);
  try {
    rec.extra["route"] = Esc("ChamferConvexEdges");
    return k::ChamferConvexEdges(box, edges, amount, amount);
  } catch (const std::exception& e) {
    rec.Note(std::string("batch chamfer declined: ") + ClassName(e) + ": " + e.what());
  }
  rec.extra["route"] = Esc("ChamferConvexEdge-sequential");
  Brep cur = box;
  int done = 0;
  for (const auto& e : edges) {
    Point3d p0, p1;
    if (!FindEdgeOn(cur, e.first, e.second, p0, p1)) throw Refusal("EdgeNotFound", "edge " + std::to_string(done) + " no longer present after earlier chamfers");
    try {
      cur = k::ChamferConvexEdge(cur, p0, p1, amount, amount);
    } catch (const std::exception& ex) {
      rec.Note("sequential chamfer failed at edge " + std::to_string(done) + " of " + std::to_string(edges.size()) + ": " + ex.what());
      throw;
    }
    ++done;
  }
  return cur;
}

// ---------------------------------------------------------------------------------------------------------------------
// Offset of a closed solid (solid=No), the app's exact routes.
Brep DoOffset(const J& args, Rec& rec) {
  const J& shape = args.at("shape");
  const std::string& prim = shape.at("prim").str();
  const double d = args.at("distance").num();
  const Brep solid = MakeShape(shape);
  const J& mv = shape.at("move");
  const Point3d o(mv.a.at(0).num(), mv.a.at(1).num(), mv.a.at(2).num());
  if (prim == "sphere") {
    rec.extra["route"] = Esc("SphereWelded(r+d)");
    const double r = shape.at("r").num() + d;
    if (!(r > 0)) throw Refusal("OffsetCollapses", "the sphere would collapse");
    return Brep::SphereWelded(o, r);
  }
  if (prim == "cylinder") {
    rec.extra["route"] = Esc("CylinderWelded(r+d, caps moved d)");
    const double r = shape.at("r").num() + d, h = shape.at("h").num() + 2 * d;
    if (!(r > 0) || !(h > 0)) throw Refusal("OffsetCollapses", "the cylinder would collapse");
    return Brep::CylinderWelded(Point3d(o.x, o.y, o.z - d), Vector3d(0, 0, 1), r, h);
  }
  rec.extra["route"] = Esc("OffsetSolidConvexPlanar");
  return k::OffsetSolidConvexPlanar(solid, d);
}

// ---------------------------------------------------------------------------------------------------------------------
// Extrude / revolve / loft.
Brep DoExtrude(const J& args) {
  const int n = static_cast<int>(args.at("sides").num());
  const double r = args.at("radius").num(), h = args.at("height").num();
  std::vector<Point3d> pts;
  for (int i = 0; i <= n; ++i) {
    const double a = 2.0 * kPi * (i % n) / n;  // the closing point is the first one exactly
    pts.emplace_back(r * std::cos(a), r * std::sin(a), 0.0);
  }
  return Brep::Extrude(NurbsCurve::FromControlPoints(pts, 1), Vector3d(0, 0, h), args.at("cap").boolean());
}

Brep DoRevolve(const J& args) {
  const double x0 = args.at("x0").num(), x1 = args.at("x1").num(), z0 = args.at("z0").num(), z1 = args.at("z1").num();
  const std::vector<Point3d> pts = {Point3d(x0, 0, z0), Point3d(x1, 0, z0), Point3d(x1, 0, z1), Point3d(x0, 0, z1), Point3d(x0, 0, z0)};
  return Brep::Revolve(NurbsCurve::FromControlPoints(pts, 1), Point3d(0, 0, 0), Vector3d(0, 0, 1), args.at("angle_deg").num() * kPi / 180.0,
                       args.at("cap").boolean());
}

Brep DoLoft(const J& args) {
  const double h = args.at("height").num();
  std::vector<NurbsCurve> sections = {CircleCurve(Point3d(0, 0, 0), args.at("r0").num()), CircleCurve(Point3d(0, 0, h), args.at("r1").num())};
  return Brep::Loft(sections, 1, false, args.at("cap").boolean());
}

// ---------------------------------------------------------------------------------------------------------------------
// Intersections and curves.
std::string PointsJson(std::vector<Point3d> pts) {
  std::string s = "[";
  for (size_t i = 0; i < pts.size(); ++i) {
    if (i) s += ",";
    s += "[" + Num(pts[i].x) + "," + Num(pts[i].y) + "," + Num(pts[i].z) + "]";
  }
  return s + "]";
}

void DoCircles(const J& args, const std::vector<std::string>& want, Rec& rec) {
  const NurbsCurve a = CircleCurve(Point3d(0, 0, 0), args.at("r0").num());
  const NurbsCurve b = CircleCurve(Point3d(args.at("dist").num(), 0, 0), args.at("r1").num());
  k::IntersectOptions opt;
  opt.tolerance = kDocTol;
  const auto hits = k::IntersectCurves(a.raw(), b.raw(), opt);
  std::vector<Point3d> pts;
  for (const auto& h : hits) pts.push_back(h.point);
  if (Wants(want, "count")) rec.out["count"] = std::to_string(hits.size());
  if (Wants(want, "points")) rec.out["points"] = PointsJson(pts);
}

void DoSection(const Brep& body, const ON_Plane& plane, const std::vector<std::string>& want, Rec& rec) {
  k::IntersectOptions opt;
  opt.tolerance = kDocTol;
  const auto contours = k::SectionBrepByPlaneJoined(body.raw(), plane, opt);
  double len = 0;
  for (const auto& c : contours) len += c.curve.LengthToTolerance(1e-6);
  if (Wants(want, "count")) rec.out["count"] = std::to_string(contours.size());
  if (Wants(want, "length")) rec.out["length"] = Num(len);
  rec.extra["route"] = Esc("SectionBrepByPlaneJoined");
}

void MeasureCurve(const NurbsCurve& c, const std::vector<std::string>& want, Rec& rec) {
  if (Wants(want, "length")) rec.out["length"] = Num(c.LengthToTolerance(1e-6));
  if (Wants(want, "bbox")) {
    const k::BoundingBox bb = c.GetTightBoundingBox(1e-6);
    rec.out["bbox"] = "[" + Num(bb.min.x) + "," + Num(bb.min.y) + "," + Num(bb.min.z) + "," + Num(bb.max.x) + "," + Num(bb.max.y) + "," + Num(bb.max.z) + "]";
  }
  if (Wants(want, "degree")) rec.out["degree"] = std::to_string(c.Degree());
  if (Wants(want, "ctrlpts")) rec.out["ctrlpts"] = std::to_string(c.ControlPointCount());
}

// ---------------------------------------------------------------------------------------------------------------------
// One case -> record (throws on refusal).
void RunCase(const J& c, Rec& rec) {
  const std::string& op = c.at("op").str();
  const J& args = c.at("args");
  std::vector<std::string> want;
  for (const J& m : c.at("measure").a) want.push_back(m.str());

  if (op == "make") {
    const Brep b = MakeShape(args.at("shape"));
    MeasureBrep(b, want, rec);
  } else if (op == "boolean") {
    const Brep a = MakeShape(args.at("a")), b = MakeShape(args.at("b"));
    const std::string& name = args.at("op").str();
    const k::BooleanOp o = name == "union" ? k::BooleanOp::Union : name == "difference" ? k::BooleanOp::Difference : k::BooleanOp::Intersection;
    MeasureBrep(DoBoolean(a, b, o, rec), want, rec);
  } else if (op == "blend_edges") {
    const Brep r = DoBlend(args, rec);
    RequireGeometry(r);
    MeasureBrep(r, want, rec);
  } else if (op == "offset_solid") {
    const Brep r = DoOffset(args, rec);
    RequireGeometry(r);
    MeasureBrep(r, want, rec);
  } else if (op == "extrude_polygon") {
    const Brep r = DoExtrude(args);
    RequireGeometry(r);
    MeasureBrep(r, want, rec);
  } else if (op == "revolve_rect") {
    const Brep r = DoRevolve(args);
    RequireGeometry(r);
    MeasureBrep(r, want, rec);
  } else if (op == "loft_circles") {
    const Brep r = DoLoft(args);
    RequireGeometry(r);
    MeasureBrep(r, want, rec);
  } else if (op == "intersect_circles") {
    DoCircles(args, want, rec);
  } else if (op == "section_plane_sphere") {
    const double r = args.at("r").num(), z = args.at("z").num();
    const ON_Plane plane(ON_3dPoint(0, 0, z), ON_3dVector(0, 0, 1));
    DoSection(Brep::SphereWelded(Point3d(0, 0, 0), r), plane, want, rec);
    // The analytic plane/sphere routine for comparison (it reports a tangent point, which the B-rep route does not).
    try {
      const auto a = k::IntersectPlaneSphere(plane, ON_Sphere(ON_3dPoint(0, 0, 0), r), kDocTol);
      const int cnt = a.empty ? 0 : 1;
      const double len = (a.empty || a.tangent) ? 0.0 : a.circle.Circumference();
      rec.extra["alt"] = "{\"analytic_count\":" + std::to_string(cnt) + ",\"analytic_length\":" + Num(len) + "}";
    } catch (const std::exception& e) {
      rec.Note(std::string("analytic: ") + e.what());
    }
  } else if (op == "section_plane_box") {
    const ON_Plane plane(ON_3dPoint(0, 0, args.at("z").num()), ON_3dVector(0, 0, 1));
    DoSection(Brep::BoxWelded(0, 0, 0, args.at("dx").num(), args.at("dy").num(), args.at("dz").num()), plane, want, rec);
  } else if (op == "interp_curve") {
    std::vector<Point3d> pts;
    for (const J& p : args.at("points").a) pts.push_back(P3(p));
    k::CurveInterpolationOptions opt;
    opt.degree = static_cast<int>(args.at("degree").num());
    NurbsCurve out;
    if (k::InterpolateCurve(pts, opt, out) != k::Result::Ok) throw Refusal("InterpolationFailed", "InterpolateCurve returned Failed");
    MeasureCurve(out, want, rec);
  } else if (op == "arc") {
    const double r = args.at("r").num(), ang = args.at("angle_deg").num() * kPi / 180.0;
    ON_NurbsCurve n;
    ON_Arc(ON_Circle(ON_Plane::World_xy, r), ang).GetNurbForm(n);
    NurbsCurve out;
    out.raw() = n;
    MeasureCurve(out, want, rec);
  } else {
    throw Refusal("UnknownOperation", "the runner has no route for " + op);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
std::string Build() {
  for (const char* var : {"DINO8_EVIDENCE_SHA", "GITHUB_SHA"}) {
    if (const char* v = std::getenv(var)) {
      if (std::strlen(v) >= 8) return std::string("dino8-") + std::string(v, 8);
    }
  }
  return "dino8-unknown";
}

std::string Render(const std::string& id, bool ok, const std::string& error, const Rec& rec, long ms, const std::string& build) {
  std::map<std::string, std::string> top;
  top["build"] = Esc(build);
  top["id"] = Esc(id);
  top["ms"] = std::to_string(ms);
  top["ok"] = ok ? "true" : "false";
  top["tolerance"] = "0.001";
  std::string out = "{";
  bool first = true;
  for (const auto& kv : rec.out) { out += (first ? "" : ",") + Esc(kv.first) + ":" + kv.second; first = false; }
  out += "}";
  top["out"] = out;
  if (!ok) top["error"] = Esc(error);
  for (const auto& kv : rec.extra) top[kv.first] = kv.first == "route" || kv.first == "alt" ? kv.second : Esc(kv.second);
  std::string line = "{";
  first = true;
  for (const auto& kv : top) { line += (first ? "" : ",") + Esc(kv.first) + ":" + kv.second; first = false; }
  return line + "}";
}

void WriteAll(int fd, const std::string& text) {
  size_t off = 0;
  while (off < text.size()) {
    const ssize_t w = write(fd, text.data() + off, text.size() - off);
    if (w <= 0) break;
    off += static_cast<size_t>(w);
  }
}

// Runs one case in a forked child (alarm = timeout) and returns its record line.
std::string RunIsolated(const J& c, int timeout_s, const std::string& build, bool diagnostics) {
  const std::string id = c.at("id").str();
  int fd[2];
  if (pipe(fd) != 0) throw std::runtime_error("pipe failed");
  std::fflush(stdout);
  std::fflush(stderr);
  const auto t0 = std::chrono::steady_clock::now();
  const pid_t pid = fork();
  if (pid < 0) throw std::runtime_error("fork failed");
  if (pid == 0) {
    close(fd[0]);
    const int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }
    alarm(static_cast<unsigned>(timeout_s));
    Rec rec;
    bool ok = true;
    std::string error;
    try {
      RunCase(c, rec);
    } catch (const std::exception& e) {
      ok = false;
      error = ClassName(e);
      rec.out.clear();
      rec.Note(std::string(e.what()));
    } catch (...) {
      ok = false;
      error = "UnknownException";
      rec.out.clear();
    }
    const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    WriteAll(fd[1], Render(id, ok, error, rec, ms, build) + "\n");
    // Diagnostic, after the record is safely written (a hang or crash here loses only this line): the bounding box of the closed
    // tessellation, to tell a kernel tight box that ignores face trims from a genuinely different shape.
    if (ok && g_last_brep && g_last_wants_bbox && diagnostics) {
      alarm(8);
      try {
        const k::BoundingBox bb = g_last_brep->TessellateToClosedMeshAdaptive(1e-4).GetBoundingBox();
        WriteAll(fd[1], "BBOXMESH [" + Num(bb.min.x) + "," + Num(bb.min.y) + "," + Num(bb.min.z) + "," + Num(bb.max.x) + "," + Num(bb.max.y) + "," +
                            Num(bb.max.z) + "]\n");
      } catch (...) {
      }
    }
    _exit(0);
  }
  close(fd[1]);
  std::string got;
  char buf[4096];
  for (;;) {
    const ssize_t r = read(fd[0], buf, sizeof buf);
    if (r > 0) got.append(buf, static_cast<size_t>(r));
    else if (r < 0 && errno == EINTR) continue;
    else break;
  }
  close(fd[0]);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  const size_t nl = got.find('\n');
  if (nl != std::string::npos && nl > 0 && got[nl - 1] == '}') {  // the record line is complete, whatever happened to the diagnostic after it
    std::string record = got.substr(0, nl);
    const size_t d = got.find("BBOXMESH [", nl);
    if (d != std::string::npos) {
      const size_t e = got.find(']', d);
      if (e != std::string::npos) record.insert(record.size() - 1, ",\"bbox_mesh\":" + got.substr(d + 9, e - (d + 9) + 1));
    }
    return record;
  }
  Rec rec;
  std::string error = "ChildLost";
  if (WIFSIGNALED(status)) {
    const int sig = WTERMSIG(status);
    error = sig == SIGALRM ? "Timeout" : sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE" : "SIG" + std::to_string(sig);
  }
  return Render(id, false, error, rec, ms, build);
}

std::string FindCases() {
  std::vector<std::string> cand;
  if (const char* e = std::getenv("D8_ORACLE_CASES")) cand.push_back(e);
  std::string here = __FILE__;  // <repo>/dino8-kernel/tests/extra/oracle_rhino.cpp
  for (int i = 0; i < 4; ++i) {
    const size_t p = here.find_last_of('/');
    if (p == std::string::npos) break;
    here = here.substr(0, p);
  }
  cand.push_back(here + "/parity/oracle/rhino/cases.jsonl");
  for (const char* rel : {"parity/oracle/rhino/cases.jsonl", "../parity/oracle/rhino/cases.jsonl", "../../parity/oracle/rhino/cases.jsonl"}) cand.push_back(rel);
  for (const auto& p : cand) {
    std::ifstream f(p);
    if (f.good()) return p;
  }
  return "";
}

}  // namespace

D8_EXTRA_TEST(OracleRhino) {
  const char* filter = std::getenv("DINO8_TEST_FILTER");
  if (!filter || !std::strstr(filter, "OracleRhino")) {
    d8test::Check(true, "oracle rhino: skipped (run it with DINO8_TEST_FILTER=OracleRhino)");
    return;
  }
  const std::string path = FindCases();
  d8test::Check(!path.empty(), "oracle rhino: cases.jsonl found");
  if (path.empty()) return;
  const std::string build = Build();
  std::ifstream in(path);
  std::string line;
  int total = 0, written = 0;
  const auto start = std::chrono::steady_clock::now();
  std::printf("D8RES-BEGIN build=%s cases=%s\n", build.c_str(), path.c_str());
  while (std::getline(in, line)) {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
    ++total;
    std::string rec;
    try {
      const J c = JParser(line).value();
      const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      if (used > 1250) {  // the CI step has a 1500 s limit: stop cleanly rather than lose the whole sweep
        Rec r;
        rec = Render(c.at("id").str(), false, "BudgetExceeded", r, 0, build);
      } else {
        rec = RunIsolated(c, 60, build, used < 600);
      }
    } catch (const std::exception& e) {
      std::printf("D8ERR line %d: %s\n", total, e.what());
      continue;
    }
    std::printf("D8RES %s\n", rec.c_str());
    std::fflush(stdout);
    ++written;
  }
  std::printf("D8RES-END cases=%d records=%d\n", total, written);
  d8test::Check(total > 0 && written == total, "oracle rhino: one record per case");
}
