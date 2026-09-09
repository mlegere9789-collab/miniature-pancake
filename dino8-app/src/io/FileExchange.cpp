#include "io/FileExchange.h"

#include "commands/DimGeometry.h"
#include "drafting/HatchBuild.h"
#include "drafting/HatchLibrary.h"
#include "geom/TextOutline.h"
#include "util/ThreadPool.h"
#include "viewport/Viewport.h"

#include <opennurbs.h>

// GNU LibreDWG (see the "DWG (via GNU LibreDWG)" section below and
// docs/INTEROP_LIMITATIONS.md / THIRD_PARTY_LICENSES.md). Pure C headers,
// but both are extern "C"-guarded for C++ already.
#include <dwg.h>
#include <dwg_api.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

namespace {

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

std::string Num(double v, int decimals = 6) {
  if (!std::isfinite(v)) v = 0.0;
  if (std::fabs(v) < 1e-12) v = 0.0;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  // Trim trailing zeros (and a dangling '.') so files stay compact.
  std::string s(buf);
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string Upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool CurveFromON(const ON_Curve& c, kernel::NurbsCurve& out) {
  ON_NurbsCurve nc;
  if (c.GetNurbForm(nc) <= 0 || !nc.IsValid()) return false;
  out.raw() = nc;
  return true;
}

// Millimetres per document unit, so a forced print scale is meaningful.
double MillimetresPerUnit(const Document& doc) {
  const std::string& u = doc.Settings().unit_system;
  if (u == "Inches") return 25.4;
  if (u == "Feet") return 304.8;
  if (u == "Centimeters") return 10.0;
  if (u == "Meters") return 1000.0;
  return 1.0;
}

// ---- AutoCAD colour index (ACI) palette ------------------------------------

std::array<int, 3> AciToRgb(int aci) {
  static const std::array<int, 3> base[] = {
      {0, 0, 0},       {255, 0, 0},     {255, 255, 0},   {0, 255, 0},     {0, 255, 255},
      {0, 0, 255},     {255, 0, 255},   {0, 0, 0},       {128, 128, 128}, {192, 192, 192},
  };
  if (aci >= 0 && aci <= 9) return base[aci];
  if (aci >= 250 && aci <= 255) {
    static const int greys[] = {51, 91, 132, 173, 214, 255};
    const int g = greys[aci - 250];
    return {g, g, g};
  }
  if (aci < 10 || aci > 249) return {0, 0, 0};
  const int h = (aci - 10) / 10;  // 24 hues, 15 degrees apart
  const int j = (aci - 10) % 10;
  static const double levels[] = {1.0, 0.8, 0.6, 0.5, 0.3};
  const double v = levels[j / 2];
  const double s = (j % 2) ? 0.5 : 1.0;
  const double hue = h * 15.0 / 60.0;  // in sextants
  const int sector = static_cast<int>(std::floor(hue)) % 6;
  const double f = hue - std::floor(hue);
  const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  double r, g, b;
  switch (sector) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return {static_cast<int>(std::lround(r * 255)), static_cast<int>(std::lround(g * 255)), static_cast<int>(std::lround(b * 255))};
}

int RgbToAci(int r, int g, int b) {
  // Black (the usual "draw on white" colour) is ACI 7 by convention.
  if (r < 8 && g < 8 && b < 8) return 7;
  int best = 7;
  long best_d = 1L << 40;
  for (int i = 1; i <= 255; ++i) {
    const std::array<int, 3> c = AciToRgb(i);
    const long d = static_cast<long>(c[0] - r) * (c[0] - r) + static_cast<long>(c[1] - g) * (c[1] - g) +
                   static_cast<long>(c[2] - b) * (c[2] - b);
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

int ColorToAci(const Color& c) {
  return RgbToAci(static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)),
                  static_cast<int>(std::lround(c.b * 255)));
}

long ColorToTrueColor(const Color& c) {
  return (static_cast<long>(std::lround(c.r * 255)) << 16) | (static_cast<long>(std::lround(c.g * 255)) << 8) |
         static_cast<long>(std::lround(c.b * 255));
}

Color TrueColorToColor(long v) { return Color::FromBytes((v >> 16) & 255, (v >> 8) & 255, v & 255); }

// ---- Display-cache polylines -----------------------------------------------

struct Polyline3 {
  std::vector<Point3d> pts;
  bool closed = false;
};

// Chains the display cache's segment pairs into polylines. Consecutive
// segments that share an endpoint (every sampled curve, every brep edge)
// become one polyline; a polyline whose two ends coincide is marked closed
// and its duplicated last point dropped.
std::vector<Polyline3> ChainSegments(const std::vector<float>& lines) {
  std::vector<Polyline3> out;
  double extent = 1.0;
  for (size_t i = 0; i + 2 < lines.size(); i += 3) {
    extent = std::max({extent, std::fabs(static_cast<double>(lines[i])), std::fabs(static_cast<double>(lines[i + 1])),
                       std::fabs(static_cast<double>(lines[i + 2]))});
  }
  const double eps = 1e-6 * extent;
  for (size_t i = 0; i + 5 < lines.size(); i += 6) {
    const Point3d a(lines[i], lines[i + 1], lines[i + 2]);
    const Point3d b(lines[i + 3], lines[i + 4], lines[i + 5]);
    if (a.DistanceTo(b) <= eps) continue;
    if (!out.empty() && out.back().pts.back().DistanceTo(a) <= eps) {
      out.back().pts.push_back(b);
    } else {
      Polyline3 p;
      p.pts = {a, b};
      out.push_back(std::move(p));
    }
  }
  for (Polyline3& p : out) {
    if (p.pts.size() >= 4 && p.pts.front().DistanceTo(p.pts.back()) <= eps * 10) {
      p.closed = true;
      p.pts.pop_back();
    }
    // Drop interior samples that sit on the segment joining their
    // neighbours (straight brep edges are sampled 32 times in the cache).
    std::vector<Point3d> kept;
    kept.reserve(p.pts.size());
    const size_t n = p.pts.size();
    for (size_t i = 0; i < n; ++i) {
      const bool interior = p.closed ? n >= 4 : (i > 0 && i + 1 < n);
      if (interior) {
        const Point3d& a = p.closed ? (kept.empty() ? p.pts[(i + n - 1) % n] : kept.back()) : kept.back();
        const Point3d& c = p.pts[(i + 1) % n];
        ON_Line seg(a, c);
        if (seg.Length() > eps && seg.DistanceTo(p.pts[i]) <= eps * 10 &&
            ON_DotProduct(p.pts[i] - a, c - p.pts[i]) > 0) {
          continue;
        }
      }
      kept.push_back(p.pts[i]);
    }
    if (kept.size() >= 2) p.pts.swap(kept);
  }
  return out;
}

std::vector<Polyline3> ObjectPolylines(const SceneObject& o) {
  o.EnsureDisplay(0.01, 0.05);
  return ChainSegments(o.Display().lines);
}

// Samples a NURBS curve for formats that only know polylines.
std::vector<Point3d> SampleCurve(const kernel::NurbsCurve& c, double tol) {
  std::vector<double> params = c.SuggestedParameterValues(tol, 10);
  if (params.size() < 8) {
    const kernel::Interval d = c.Domain();
    params.clear();
    for (int i = 0; i <= 24; ++i) params.push_back(d.min + (d.max - d.min) * i / 24.0);
  }
  std::vector<Point3d> pts;
  pts.reserve(params.size());
  for (double t : params) pts.push_back(c.PointAt(t));
  return pts;
}

// ---------------------------------------------------------------------------
// DXF writer
// ---------------------------------------------------------------------------

class DxfWriter {
 public:
  explicit DxfWriter(std::ostream& os) : os_(os) {}

  void G(int code, const std::string& v) { os_ << code << "\n" << v << "\n"; }
  void G(int code, double v) { G(code, Num(v, 9)); }
  void G(int code, int v) { G(code, std::to_string(v)); }
  void G(int code, long v) { G(code, std::to_string(v)); }
  std::string Handle() {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lX", next_handle_++);
    return buf;
  }
  long NextHandleValue() const { return next_handle_; }

  void Point(int base, Point3d p) { G(base, p.x); G(base + 10, p.y); G(base + 20, p.z); }

  void BeginEntity(const char* type, const std::string& layer, const Color* color) {
    G(0, std::string(type));
    G(5, Handle());
    G(100, "AcDbEntity");
    G(8, layer);
    if (color) {
      G(62, ColorToAci(*color));
      G(420, ColorToTrueColor(*color));
    }
  }

 private:
  std::ostream& os_;
  long next_handle_ = 0x100;
};

std::string DxfLayerName(std::string name) {
  for (char& c : name) {
    if (std::strchr("<>/\\\":;?*|=`", c) != nullptr) c = '_';
  }
  name = Trim(name);
  return name.empty() ? std::string("0") : name;
}

double DxfAngleDeg(Point3d center, Point3d p) {
  double a = std::atan2(p.y - center.y, p.x - center.x) * 180.0 / ON_PI;
  if (a < 0) a += 360.0;
  return a;
}

void WriteDxfPolyline(DxfWriter& w, const std::vector<Point3d>& pts, bool closed, const std::string& layer,
                      const Color* color) {
  if (pts.size() < 2) return;
  bool planar = true;
  for (const Point3d& p : pts) if (std::fabs(p.z - pts.front().z) > 1e-9) { planar = false; break; }
  if (planar) {
    w.BeginEntity("LWPOLYLINE", layer, color);
    w.G(100, "AcDbPolyline");
    w.G(90, static_cast<int>(pts.size()));
    w.G(70, closed ? 1 : 0);
    w.G(38, pts.front().z);
    for (const Point3d& p : pts) { w.G(10, p.x); w.G(20, p.y); }
    return;
  }
  w.BeginEntity("POLYLINE", layer, color);
  w.G(100, "AcDb3dPolyline");
  w.G(66, 1);
  w.G(10, 0.0); w.G(20, 0.0); w.G(30, 0.0);
  w.G(70, (closed ? 1 : 0) | 8);
  for (const Point3d& p : pts) {
    w.BeginEntity("VERTEX", layer, nullptr);
    w.G(100, "AcDbVertex");
    w.G(100, "AcDb3dPolylineVertex");
    w.Point(10, p);
    w.G(70, 32);
  }
  w.G(0, "SEQEND");
  w.G(5, w.Handle());
  w.G(100, "AcDbEntity");
  w.G(8, layer);
}

// A generic freeform NURBS curve, exact: DXF's SPLINE entity carries the
// full control-point/knot/weight data, so (unlike the polyline fallback)
// re-importing this reconstructs the identical curve rather than a chord
// approximation of it. DXF's knot vector has two more entries than
// OpenNURBS' (it doesn't elide the duplicated first/last knot), matching
// what DxfImporter::Spline()'s "knots.size() == want + 2" branch expects.
void WriteDxfSpline(DxfWriter& w, const ON_NurbsCurve& nc, const std::string& layer, const Color* color) {
  const int order = nc.Order();
  const int cv_count = nc.CVCount();
  const bool rational = nc.IsRational();
  w.BeginEntity("SPLINE", layer, color);
  w.G(100, "AcDbSpline");
  int flags = 0;
  if (nc.IsClosed()) flags |= 1;
  if (nc.IsPeriodic()) flags |= 2;
  if (rational) flags |= 4;
  w.G(70, flags);
  w.G(71, order - 1);
  const int knot_count = nc.KnotCount() + 2;
  w.G(72, knot_count);
  w.G(73, cv_count);
  w.G(74, 0);
  w.G(40, nc.Knot(0));
  for (int i = 0; i < nc.KnotCount(); ++i) w.G(40, nc.Knot(i));
  w.G(40, nc.Knot(nc.KnotCount() - 1));
  for (int i = 0; i < cv_count; ++i) {
    if (rational) w.G(41, nc.Weight(i));
    ON_3dPoint p;
    nc.GetCV(i, p);
    w.Point(10, p);
  }
}

void WriteDxfCurve(DxfWriter& w, const kernel::NurbsCurve& curve, const std::string& layer, const Color* color) {
  const ON_NurbsCurve& nc = curve.raw();
  const double tol = 1e-6;
  if (nc.IsLinear(tol)) {
    w.BeginEntity("LINE", layer, color);
    w.G(100, "AcDbLine");
    w.Point(10, nc.PointAtStart());
    w.Point(11, nc.PointAtEnd());
    return;
  }
  ON_Arc arc;
  if (nc.IsArc(nullptr, &arc, tol) && arc.IsValid()) {
    const double nz = arc.plane.zaxis.z;
    if (std::fabs(std::fabs(nz) - 1.0) < 1e-6) {
      const Point3d c = arc.Center();
      if (arc.IsCircle()) {
        w.BeginEntity("CIRCLE", layer, color);
        w.G(100, "AcDbCircle");
        w.Point(10, c);
        w.G(40, arc.radius);
        return;
      }
      // DXF arcs always run counter-clockwise about +Z: swap the ends when
      // the arc's own plane points down.
      const Point3d a = nz > 0 ? arc.StartPoint() : arc.EndPoint();
      const Point3d b = nz > 0 ? arc.EndPoint() : arc.StartPoint();
      w.BeginEntity("ARC", layer, color);
      w.G(100, "AcDbCircle");
      w.Point(10, c);
      w.G(40, arc.radius);
      w.G(100, "AcDbArc");
      w.G(50, DxfAngleDeg(c, a));
      w.G(51, DxfAngleDeg(c, b));
      return;
    }
  }
  if (nc.Degree() == 1) {
    std::vector<Point3d> pts;
    for (int i = 0; i < nc.CVCount(); ++i) { ON_3dPoint p; nc.GetCV(i, p); pts.push_back(p); }
    bool closed = false;
    if (pts.size() >= 3 && pts.front().DistanceTo(pts.back()) <= tol) { closed = true; pts.pop_back(); }
    WriteDxfPolyline(w, pts, closed, layer, color);
    return;
  }
  // Any other freeform curve (including non-circular ellipses - OpenNURBS'
  // own ON_Curve::IsEllipse() only ever recognizes circles, delegating to
  // IsArc(), so there is no reliable way to single a true ellipse back out
  // of its NURBS form here): write the exact NURBS as a SPLINE instead of
  // flattening it to a polyline, so export-then-reimport round-trips the
  // real control points/knots/weights rather than a chord approximation.
  if (nc.IsValid() && nc.CVCount() >= 2) {
    WriteDxfSpline(w, nc, layer, color);
    return;
  }
  std::vector<Point3d> pts = SampleCurve(curve, 0.01);
  bool closed = false;
  if (pts.size() >= 3 && nc.IsClosed()) { closed = true; pts.pop_back(); }
  WriteDxfPolyline(w, pts, closed, layer, color);
}

void WriteDxfMesh(DxfWriter& w, const ON_Mesh& m, const std::string& layer, const Color* color) {
  for (int i = 0; i < m.FaceCount(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    w.BeginEntity("3DFACE", layer, color);
    w.G(100, "AcDbFace");
    w.Point(10, m.Vertex(f.vi[0]));
    w.Point(11, m.Vertex(f.vi[1]));
    w.Point(12, m.Vertex(f.vi[2]));
    w.Point(13, m.Vertex(f.vi[3]));
  }
}

}  // namespace

bool ExportDxf(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  std::vector<const SceneObject*> objs;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    objs.push_back(&o);
  }
  if (objs.empty()) {
    error = "Nothing to export";
    return false;
  }
  std::ofstream os(path, std::ios::binary);
  if (!os) {
    error = "Could not write " + path;
    return false;
  }
  DxfWriter w(os);

  // Layer names, de-duplicated after sanitising.
  std::vector<std::string> layer_names;
  std::map<std::string, int> used;
  for (size_t i = 0; i < doc.Layers().size(); ++i) {
    std::string n = DxfLayerName(doc.LayerFullPath(static_cast<int>(i)));
    if (used.count(n)) n += "_" + std::to_string(++used[n]);
    used[n] = 0;
    layer_names.push_back(n);
  }
  if (layer_names.empty()) layer_names.push_back("0");

  // HEADER
  w.G(0, "SECTION"); w.G(2, "HEADER");
  w.G(9, "$ACADVER"); w.G(1, "AC1015");
  w.G(9, "$INSUNITS");
  {
    const std::string& u = doc.Settings().unit_system;
    int units = 4;  // millimetres
    if (u == "Inches") units = 1; else if (u == "Feet") units = 2; else if (u == "Centimeters") units = 5; else if (u == "Meters") units = 6;
    w.G(70, units);
  }
  w.G(9, "$HANDSEED"); w.G(5, "FFFF");
  w.G(0, "ENDSEC");

  // TABLES (just LAYER; readers create the rest with defaults)
  w.G(0, "SECTION"); w.G(2, "TABLES");
  w.G(0, "TABLE"); w.G(2, "LAYER"); w.G(5, "2"); w.G(330, "0"); w.G(100, "AcDbSymbolTable");
  w.G(70, static_cast<int>(layer_names.size()));
  for (size_t i = 0; i < layer_names.size(); ++i) {
    const Layer* L = i < doc.Layers().size() ? &doc.Layers()[i] : nullptr;
    w.G(0, "LAYER"); w.G(5, w.Handle()); w.G(330, "2");
    w.G(100, "AcDbSymbolTableRecord"); w.G(100, "AcDbLayerTableRecord");
    w.G(2, layer_names[i]);
    w.G(70, L && L->locked ? 4 : 0);
    const int aci = L ? ColorToAci(L->color) : 7;
    w.G(62, L && !L->visible ? -aci : aci);
    if (L) w.G(420, ColorToTrueColor(L->color));
    w.G(6, "Continuous");
  }
  w.G(0, "ENDTAB");
  w.G(0, "ENDSEC");

  // ENTITIES
  w.G(0, "SECTION"); w.G(2, "ENTITIES");
  int written = 0;
  for (const SceneObject* o : objs) {
    const size_t li = static_cast<size_t>(std::clamp(o->layer_index, 0, static_cast<int>(layer_names.size()) - 1));
    const std::string& layer = layer_names[li];
    const Color* color = o->color_by_layer ? nullptr : &o->color;
    switch (o->kind) {
      case ObjectKind::Point:
        w.BeginEntity("POINT", layer, color);
        w.G(100, "AcDbPoint");
        w.Point(10, o->point);
        ++written;
        break;
      case ObjectKind::Curve:
        if (o->curve) { WriteDxfCurve(w, *o->curve, layer, color); ++written; }
        break;
      case ObjectKind::Mesh:
        if (o->mesh) { WriteDxfMesh(w, o->mesh->raw(), layer, color); ++written; }
        break;
      case ObjectKind::Brep: {
        // Exact edge curves: a box becomes twelve LINEs, a cylinder two
        // CIRCLEs and a seam line.
        if (!o->brep) break;
        const ON_Brep& b = o->brep->raw();
        int edges = 0;
        for (int i = 0; i < b.m_E.Count(); ++i) {
          const ON_BrepEdge& e = b.m_E[i];
          if (e.m_edge_index < 0) continue;
          kernel::NurbsCurve k;
          if (!CurveFromON(e, k)) continue;
          WriteDxfCurve(w, k, layer, color);
          ++edges;
        }
        if (edges == 0) for (const Polyline3& pl : ObjectPolylines(*o)) WriteDxfPolyline(w, pl.pts, pl.closed, layer, color);
        ++written;
        break;
      }
      case ObjectKind::Surface: {
        // The four boundary curves, exact.
        if (!o->surface) break;
        const ON_NurbsSurface& srf = o->surface->raw();
        int edges = 0;
        for (int dir = 0; dir < 2; ++dir) {
          const ON_Interval d = srf.Domain(1 - dir);
          for (int end = 0; end < 2; ++end) {
            ON_Curve* c = srf.IsoCurve(dir, end == 0 ? d.Min() : d.Max());
            if (!c) continue;
            kernel::NurbsCurve k;
            if (CurveFromON(*c, k)) { WriteDxfCurve(w, k, layer, color); ++edges; }
            delete c;
          }
        }
        if (edges == 0) for (const Polyline3& pl : ObjectPolylines(*o)) WriteDxfPolyline(w, pl.pts, pl.closed, layer, color);
        ++written;
        break;
      }
      case ObjectKind::SubD: {
        for (const Polyline3& pl : ObjectPolylines(*o)) WriteDxfPolyline(w, pl.pts, pl.closed, layer, color);
        ++written;
        break;
      }
    }
  }
  w.G(0, "ENDSEC");
  w.G(0, "EOF");
  if (!os) {
    error = "Could not write " + path;
    return false;
  }
  if (written == 0) {
    error = "Nothing exportable in the selection";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// DXF reader
// ---------------------------------------------------------------------------

namespace {

struct DxfGroup {
  int code;
  std::string value;
};

struct DxfEntity {
  std::string type;
  std::vector<DxfGroup> groups;

  bool Has(int code) const {
    for (const DxfGroup& g : groups) if (g.code == code) return true;
    return false;
  }
  std::string S(int code, const std::string& def = "") const {
    for (const DxfGroup& g : groups) if (g.code == code) return g.value;
    return def;
  }
  double D(int code, double def = 0.0) const {
    for (const DxfGroup& g : groups) if (g.code == code) return std::atof(g.value.c_str());
    return def;
  }
  int I(int code, int def = 0) const {
    for (const DxfGroup& g : groups) if (g.code == code) return std::atoi(g.value.c_str());
    return def;
  }
  std::vector<double> All(int code) const {
    std::vector<double> v;
    for (const DxfGroup& g : groups) if (g.code == code) v.push_back(std::atof(g.value.c_str()));
    return v;
  }
  Point3d P(int base, Point3d def = Point3d(0, 0, 0)) const {
    return Point3d(D(base, def.x), D(base + 10, def.y), D(base + 20, def.z));
  }
  Vector3d Normal() const { return Vector3d(D(210, 0), D(220, 0), D(230, 1)); }
};

// AutoCAD's "arbitrary axis algorithm": the object coordinate system for
// an extrusion direction.
ON_Plane OcsPlane(Point3d origin, Vector3d n) {
  if (!n.Unitize()) n = Vector3d(0, 0, 1);
  Vector3d ax = (std::fabs(n.x) < 1.0 / 64.0 && std::fabs(n.y) < 1.0 / 64.0) ? ON_CrossProduct(Vector3d(0, 1, 0), n)
                                                                              : ON_CrossProduct(Vector3d(0, 0, 1), n);
  ax.Unitize();
  Vector3d ay = ON_CrossProduct(n, ax);
  ay.Unitize();
  return ON_Plane(origin, ax, ay);
}

Point3d OcsToWorld(const ON_Plane& ocs, Point3d p) { return ocs.PointAt(p.x, p.y, p.z); }

// A polyline vertex with an optional bulge (tan of a quarter of the arc's
// included angle) leading to the next vertex.
struct BulgeVertex {
  Point3d p;
  double bulge = 0.0;
};

// Builds a curve from bulge-polyline vertices: an ON_PolyCurve of lines and
// exact arcs when any bulge is present, a plain polyline otherwise.
bool BulgePolylineCurve(const std::vector<BulgeVertex>& verts, bool closed, kernel::NurbsCurve& out) {
  if (verts.size() < 2) return false;
  bool any_bulge = false;
  for (const BulgeVertex& v : verts) if (std::fabs(v.bulge) > 1e-12) any_bulge = true;
  const size_t n = verts.size();
  const size_t segs = closed ? n : n - 1;
  if (!any_bulge) {
    ON_Polyline pl;
    for (const BulgeVertex& v : verts) pl.Append(v.p);
    if (closed) pl.Append(verts.front().p);
    return CurveFromON(ON_PolylineCurve(pl), out);
  }
  ON_PolyCurve pc;
  for (size_t i = 0; i < segs; ++i) {
    const Point3d a = verts[i].p;
    const Point3d b = verts[(i + 1) % n].p;
    const double bulge = verts[i].bulge;
    if (a.DistanceTo(b) < 1e-12) continue;
    if (std::fabs(bulge) < 1e-12) {
      pc.Append(new ON_LineCurve(a, b));
      continue;
    }
    // Arc midpoint: chord midpoint pushed sideways by the sagitta.
    const Vector3d chord = b - a;
    const double d = chord.Length();
    const double s = bulge * d / 2.0;
    Vector3d right(chord.y, -chord.x, 0.0);
    right.Unitize();
    const Point3d mid = (a + b) * 0.5 + right * s;
    ON_Arc arc(a, mid, b);
    if (!arc.IsValid()) { pc.Append(new ON_LineCurve(a, b)); continue; }
    pc.Append(new ON_ArcCurve(arc));
  }
  if (pc.Count() == 0) return false;
  return CurveFromON(pc, out);
}

// ---- MTEXT: inline-formatting-code stripping and glyph-outline layout -----
//
// Shared by both DXF (DxfImporter::MText below) and DWG (WalkDwgEntities'
// DWG_TYPE_MTEXT case, further down) - both carry the identical inline
// markup, just concatenated differently (DXF's repeated group-3 chunks plus
// a final group-1 chunk vs. DWG's single `text` field).

// Vertical line spacing, as a multiple of cap height. MTEXT's own metric
// (DXF group 44 / DWG's linespace_factor, a percentage of "3-on-5" font
// leading) is not read - this is a fixed typographic default instead, the
// same honest-approximation category as everything else this function
// documents. 1.5x reads comfortably for the DejaVu/Liberation/FreeSans
// fallback fonts TextOutline.h uses.
constexpr double kMTextLineSpacingFactor = 1.5;

// Strips MTEXT's inline formatting codes down to plain text, split into
// lines on \P (paragraph break - the only code that changes the line
// count). Handled: \P -> newline, \~ -> a plain space (non-breaking is not
// a distinction this importer's plain-text output preserves), \\ \{ \} ->
// literal backslash/brace, { and } (formatting-group grouping) dropped,
// and every \<letter>...; run (\Cn; colour, \Fname; font, \Hn; height,
// \Wn; width factor, \Qn; oblique, \Tn; tracking, \An alignment, \Sn/d;
// stacked fractions, etc.) dropped as one unit up to its terminating ';',
// or just the two-character code itself when it takes no argument (\L \l
// \O \o \K \k - underline/overline/strikethrough on/off). This is the
// tractable, common-case subset: it renders MTEXT as plain, unstyled
// multi-line text (no per-run colour/font/height override, no real
// stacked-fraction typesetting, no field-code substitution) - an honest
// simplification, not a silent wrong-output case. A \S stacked-fraction
// run's numerator/denominator text is dropped along with its \S...; code
// (same treatment as every other bracketed formatting code here) rather
// than approximated as "num/den", since the generic ';'-terminated scan
// can't tell \S's argument apart from any other code's.
std::vector<std::string> MTextToLines(const std::string& raw) {
  std::string plain;
  plain.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '\\' && i + 1 < raw.size()) {
      const char n = raw[i + 1];
      if (n == 'P') { plain += '\n'; ++i; continue; }
      if (n == '~') { plain += ' '; ++i; continue; }
      if (n == '\\' || n == '{' || n == '}') { plain += n; ++i; continue; }
      if (std::isalpha(static_cast<unsigned char>(n))) {
        size_t j = i + 2;
        while (j < raw.size() && raw[j] != ';' && raw[j] != '\\' && raw[j] != '{' && raw[j] != '}' && raw[j] != '\n') ++j;
        if (j < raw.size() && raw[j] == ';') { i = j; continue; }
        // No terminating ';' before the next control character: a
        // no-argument code (\L \l \O \o \K \k and similar) - drop just the
        // two characters, keep whatever follows as literal text.
        ++i;
        continue;
      }
      // An escape this doesn't recognise: drop the backslash, keep the
      // character literally rather than losing it.
      plain += n;
      ++i;
      continue;
    }
    if (c == '{' || c == '}') continue;  // formatting-group braces
    if (c == '\r') continue;
    plain += c;
  }
  std::vector<std::string> lines;
  std::string cur;
  for (char c : plain) {
    if (c == '\n') { lines.push_back(cur); cur.clear(); }
    else cur += c;
  }
  lines.push_back(cur);
  return lines;
}

// Lays out MTEXT's plain (already formatting-stripped) lines as stacked
// glyph-outline curves, one TextToCurves call per line (same conversion as
// TEXT import), anchored per `attachment` (1-9, a 3x3 grid: 1=top-left,
// 2=top-center, 3=top-right, 4=middle-left, ..., 9=bottom-right - DXF group
// 71 / DWG's identical `attachment` field) relative to `base` (base.origin
// is the MTEXT insertion point; base's plane already carries the entity's
// own rotation and any accumulated block-instance transform).
//
// What's exact: attachment 1 (top-left, the default and by far the most
// common) needs zero extra offset in either axis and positions the first
// line's cap-top exactly at the insertion point, same as TEXT's own
// baseline-at-insertion-point convention.
//
// What's approximated, honestly: the other 8 attachment points position
// the block using this function's own kMTextLineSpacingFactor-based total
// height, not real font leading/descender metrics, so vertical placement
// for middle/bottom attachment can be off by a small fraction of a line
// versus what AutoCAD would compute. Horizontal centring/right-justifying
// (attachment columns 2/3/5/6/8/9) aligns each line about its OWN width
// independently (via TextToCurves' advance_width), not against a shared
// paragraph-box width computed from the widest line the way AutoCAD
// justifies a real (possibly word-wrapped) MTEXT paragraph - so a
// multi-line block with very different line lengths will have each line
// individually centred/right-aligned rather than sharing one ragged edge.
// Word-wrap itself (DXF group 41 / DWG rect_width, a reference box width)
// is not applied at all - every line is exactly the line the \P breaks
// define, unbounded, matching this session's existing "treat as unbounded"
// scoping for that field.
bool BuildMTextGlyphs(const std::vector<std::string>& lines, double height, int attachment, const ON_Plane& base,
                      std::vector<std::vector<kernel::NurbsCurve>>& out_lines, std::string& font_used) {
  if (lines.empty() || height <= 0) return false;
  attachment = std::clamp(attachment, 1, 9);
  const int col = (attachment - 1) % 3;  // 0 left, 1 center, 2 right
  const int row = (attachment - 1) / 3;  // 0 top, 1 middle, 2 bottom
  const double n = static_cast<double>(lines.size());
  const double total_height = height + (n - 1.0) * height * kMTextLineSpacingFactor;
  double top_y = 0.0;
  if (row == 1) top_y = total_height * 0.5;
  else if (row == 2) top_y = total_height;
  out_lines.assign(lines.size(), {});
  bool any = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].empty()) continue;  // a genuinely blank paragraph: nothing to lay out
    const double baseline_y = top_y - height - static_cast<double>(i) * height * kMTextLineSpacingFactor;
    double advance = 0.0;
    std::vector<kernel::NurbsCurve> glyphs;
    std::string f;
    const ON_Plane pl(base.PointAt(0.0, baseline_y), base.xaxis, base.yaxis);
    if (!TextToCurves(lines[i], height, pl, glyphs, f, &advance) || glyphs.empty()) continue;
    if (col != 0) {
      const double dx = col == 1 ? -advance * 0.5 : -advance;
      const ON_Xform shift = ON_Xform::TranslationTransformation(base.xaxis * dx);
      for (kernel::NurbsCurve& g : glyphs) g.raw().Transform(shift);
    }
    out_lines[i] = std::move(glyphs);
    font_used = f;
    any = true;
  }
  return any;
}

// Adds one rebuilt dimension's curves + label text as a group to `doc`,
// tagged and named exactly like AddAnnotationGroup (commands/
// annotate_common.h) would - so SelDim finds it (group name match) and
// UpdateDimensions can rebuild it (the tags in `tags` are exactly what
// cmd_annotate.cpp's LoadLinearDimLayout/LoadRadiusDimLayout read back, see
// commands/DimGeometry.h). Shared by DXF DIMENSION import
// (DxfImporter::Dimension below) and DWG DIMENSION import (WalkDwgEntities,
// further down this file - a single unnamed namespace spans the whole
// translation unit, so this is visible there too). No DimRefObj#/
// DimRefEnd# associativity tags: an imported dimension has no live document
// object it was measured from (the source geometry it once referenced
// isn't tracked by handle here), so UpdateDimensions falls back to the
// static DimP0/DimP1/DimCenter/DimRadiusVal points recorded in `tags` -
// same as a live dimension whose picked points didn't land on a real
// object. Returns false if there is nothing to add (degenerate geometry or
// a font-less environment with no arrow/line curves either, which cannot
// happen here since BuildLinearDimensionGeometry/BuildRadiusDimensionGeometry
// always produce at least the line/arrow curves independent of the font).
bool AddDimensionGroupToDoc(Document& doc, const std::string& kind, int layer,
                            const std::vector<kernel::NurbsCurve>& curves, const DimGlyphSpec& text,
                            const std::map<std::string, std::string>& tags) {
  std::vector<ObjectId> ids;
  for (const kernel::NurbsCurve& c : curves) {
    SceneObject o = SceneObject::MakeCurve(c);
    o.layer_index = layer;
    o.user_text["Annotation"] = kind;
    o.user_text["Style"] = "Standard";
    for (const auto& [k, v] : tags) o.user_text[k] = v;
    ids.push_back(doc.Add(std::move(o)));
  }
  if (!text.text.empty()) {
    std::vector<kernel::NurbsCurve> glyphs;
    std::string font_used;
    double width = 0;
    if (TextToCurves(text.text, text.height, text.plane, glyphs, font_used, &width)) {
      const ON_Xform shift = ON_Xform::TranslationTransformation(-text.plane.xaxis * (text.center ? width / 2 : 0));
      for (kernel::NurbsCurve gc : glyphs) {
        if (text.center) gc.raw().Transform(shift);
        SceneObject o = SceneObject::MakeCurve(gc);
        o.layer_index = layer;
        o.user_text["Annotation"] = kind;
        o.user_text["Style"] = "Standard";
        ids.push_back(doc.Add(std::move(o)));
      }
    }
  }
  if (ids.empty()) return false;
  doc.CreateGroup(ids, kind);
  return true;
}

// Text height fallback for an imported dimension's label: the document's
// current annotation style, or twice its grid spacing - same as
// commands/annotate_common.h's AnnotationTextHeight(CommandContext&), which
// this has no CommandContext to call. Neither DXF's nor DWG's DIMENSION
// entity carries the dimension's own text height directly (it lives in the
// referenced DIMSTYLE, which this importer does not resolve - see
// DxfImporter::Dimension's comment below), so this is the same honest
// fallback default used for a document with no annotation style at all.
// Shared by both DXF and DWG DIMENSION import, same reasoning as
// AddDimensionGroupToDoc above.
double ImportDimTextHeight(Document& doc) {
  const AnnotationStyle& ast = doc.CurrentAnnotationStyle();
  return ast.text_height > 0 ? ast.text_height : std::max(doc.Settings().grid_spacing * 2.0, 1e-6);
}

struct DxfImportStats {
  int curves = 0, points = 0, meshes = 0, hatches = 0, dimensions = 0, skipped = 0, layers = 0;
};

class DxfImporter {
 public:
  DxfImporter(Document& doc, DxfImportStats& stats) : doc_(doc), stats_(stats) {}

  int LayerFor(const std::string& raw_name) {
    const std::string name = Trim(raw_name);
    auto it = layer_map_.find(name);
    if (it != layer_map_.end()) return it->second;
    int idx;
    if (name.empty() || name == "0") {
      idx = 0;
    } else {
      idx = doc_.FindLayer(name);
      if (idx < 0) { idx = doc_.AddLayer(name); ++stats_.layers; }
    }
    layer_map_[name] = idx;
    return idx;
  }

  void DefineLayer(const DxfEntity& e) {
    const std::string name = Trim(e.S(2));
    if (name.empty()) return;
    const int idx = LayerFor(name);
    Layer& L = doc_.Layers()[static_cast<size_t>(idx)];
    if (e.Has(420)) L.color = TrueColorToColor(std::atol(e.S(420).c_str()));
    else if (e.Has(62)) {
      const int aci = std::abs(e.I(62));
      const std::array<int, 3> rgb = AciToRgb(aci);
      L.color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
      if (e.I(62) < 0) L.visible = false;
    }
    const int flags = e.I(70);
    if (flags & 1) L.visible = false;   // frozen
    if (flags & 4) L.locked = true;
  }

  void ApplyAttributes(SceneObject& o, const DxfEntity& e) {
    o.layer_index = LayerFor(e.S(8, "0"));
    if (e.Has(420)) {
      o.color_by_layer = false;
      o.color = TrueColorToColor(std::atol(e.S(420).c_str()));
    } else if (e.Has(62)) {
      const int aci = e.I(62);
      if (aci > 0 && aci < 256) {
        const std::array<int, 3> rgb = AciToRgb(aci);
        o.color_by_layer = false;
        o.color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
      }
    }
  }

  void AddCurve(const ON_Curve& c, const DxfEntity& e) {
    kernel::NurbsCurve k;
    if (!CurveFromON(c, k)) { ++stats_.skipped; return; }
    AddCurve(k, e);
  }
  void AddCurve(const kernel::NurbsCurve& k, const DxfEntity& e) {
    SceneObject o = SceneObject::MakeCurve(k);
    ApplyAttributes(o, e);
    doc_.Add(std::move(o));
    ++stats_.curves;
  }

  void Line(const DxfEntity& e) { AddCurve(ON_LineCurve(e.P(10), e.P(11)), e); }

  void Point(const DxfEntity& e) {
    SceneObject o = SceneObject::MakePoint(e.P(10));
    ApplyAttributes(o, e);
    doc_.Add(std::move(o));
    ++stats_.points;
  }

  void Text(const DxfEntity& e) {
    const std::string value = e.S(1);
    const double height = e.D(40, 1.0);
    if (value.empty() || height <= 0) { ++stats_.skipped; return; }
    const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), e.Normal());
    ON_Plane pl(OcsToWorld(ocs, e.P(10)), ocs.xaxis, ocs.yaxis);
    pl.Rotate(ON_DEGREES_TO_RADIANS * e.D(50, 0.0), ocs.zaxis);
    std::vector<kernel::NurbsCurve> glyphs;
    std::string font_used;
    if (!TextToCurves(value, height, pl, glyphs, font_used) || glyphs.empty()) { ++stats_.skipped; return; }
    for (kernel::NurbsCurve& g : glyphs) {
      SceneObject o = SceneObject::MakeCurve(g);
      ApplyAttributes(o, e);
      o.user_text["Annotation"] = "Text";
      o.user_text["Style"] = "Standard";
      o.user_text["Text"] = value;
      doc_.Add(std::move(o));
      ++stats_.curves;
    }
  }

  // MTEXT: group 1 is the final (<=250-char) text chunk, and any number of
  // repeated group 3 entries are the earlier chunks in file order -
  // concatenated (group 3s first, then group 1) they form the full raw
  // text, inline formatting codes and all. See MTextToLines/BuildMTextGlyphs
  // above for exactly what's stripped/approximated; converted to real
  // glyph-outline curves the same way TEXT is (TextToCurves), one call per
  // plain-text line.
  void MText(const DxfEntity& e) {
    std::string raw;
    for (const DxfGroup& g : e.groups) if (g.code == 3) raw += g.value;
    raw += e.S(1);
    const double height = e.D(40, 1.0);
    if (raw.empty() || height <= 0) { ++stats_.skipped; return; }
    const std::vector<std::string> lines = MTextToLines(raw);
    const int attachment = e.I(71, 1);
    const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), e.Normal());
    ON_Plane base(OcsToWorld(ocs, e.P(10)), ocs.xaxis, ocs.yaxis);
    base.Rotate(ON_DEGREES_TO_RADIANS * e.D(50, 0.0), ocs.zaxis);
    std::vector<std::vector<kernel::NurbsCurve>> per_line;
    std::string font_used;
    if (!BuildMTextGlyphs(lines, height, attachment, base, per_line, font_used)) { ++stats_.skipped; return; }
    // SelText's "Text" user-text filter should match the plain (formatting
    // stripped) content, not the raw markup - lines rejoined with \n.
    std::string plain_joined;
    for (size_t i = 0; i < lines.size(); ++i) { if (i) plain_joined += "\n"; plain_joined += lines[i]; }
    for (std::vector<kernel::NurbsCurve>& glyphs : per_line) {
      for (kernel::NurbsCurve& g : glyphs) {
        SceneObject o = SceneObject::MakeCurve(g);
        ApplyAttributes(o, e);
        o.user_text["Annotation"] = "Text";
        o.user_text["Style"] = "Standard";
        o.user_text["Text"] = plain_joined;
        doc_.Add(std::move(o));
        ++stats_.curves;
      }
    }
  }

  void Circle(const DxfEntity& e) {
    const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), e.Normal());
    const Point3d center = OcsToWorld(ocs, e.P(10));
    ON_Circle c(ON_Plane(center, ocs.xaxis, ocs.yaxis), e.D(40, 1.0));
    AddCurve(ON_ArcCurve(c), e);
  }

  void Arc(const DxfEntity& e) {
    const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), e.Normal());
    const Point3d center = OcsToWorld(ocs, e.P(10));
    double a0 = e.D(50, 0.0) * ON_PI / 180.0;
    double a1 = e.D(51, 360.0) * ON_PI / 180.0;
    while (a1 <= a0 + 1e-12) a1 += 2.0 * ON_PI;
    ON_Circle c(ON_Plane(center, ocs.xaxis, ocs.yaxis), e.D(40, 1.0));
    ON_Arc arc(c, ON_Interval(a0, a1));
    AddCurve(ON_ArcCurve(arc), e);
  }

  void Ellipse(const DxfEntity& e) {
    const Point3d center = e.P(10);
    const Vector3d major = Vector3d(e.D(11), e.D(21), e.D(31));
    Vector3d n = e.Normal();
    if (!n.Unitize()) n = Vector3d(0, 0, 1);
    const double a = major.Length();
    const double ratio = e.D(40, 1.0);
    if (a < 1e-12 || ratio <= 0) { ++stats_.skipped; return; }
    Vector3d x = major; x.Unitize();
    Vector3d y = ON_CrossProduct(n, x); y.Unitize();
    const ON_Plane plane(center, x, y);
    double t0 = e.D(41, 0.0), t1 = e.D(42, 2.0 * ON_PI);
    while (t1 <= t0 + 1e-12) t1 += 2.0 * ON_PI;
    if (t1 - t0 > 2.0 * ON_PI) t1 = t0 + 2.0 * ON_PI;
    // A circular arc of radius `a` in the ellipse plane, then squashed along
    // the minor axis: NURBS geometry is exact under affine maps, so this is
    // the DXF parameterisation exactly.
    ON_Arc arc(ON_Circle(plane, a), ON_Interval(t0, t1));
    ON_NurbsCurve nc;
    if (ON_ArcCurve(arc).GetNurbForm(nc) <= 0) { ++stats_.skipped; return; }
    // Note: ON_Xform::Scale(plane, x, y, z) forwards its factors as
    // (x, z, y) in OpenNURBS 8, so use the underlying factory directly.
    const ON_Xform squash = ON_Xform::ScaleTransformation(plane, 1.0, ratio, 1.0);
    nc.Transform(squash);
    kernel::NurbsCurve k;
    k.raw() = nc;
    AddCurve(k, e);
  }

  void Spline(const DxfEntity& e) {
    const int flags = e.I(70);
    const int degree = std::max(1, e.I(71, 3));
    std::vector<Point3d> cvs;
    std::vector<Point3d> fit;
    std::vector<double> knots, weights;
    for (const DxfGroup& g : e.groups) {
      const double v = std::atof(g.value.c_str());
      switch (g.code) {
        case 10: cvs.push_back(Point3d(v, 0, 0)); break;
        case 20: if (!cvs.empty()) cvs.back().y = v; break;
        case 30: if (!cvs.empty()) cvs.back().z = v; break;
        case 11: fit.push_back(Point3d(v, 0, 0)); break;
        case 21: if (!fit.empty()) fit.back().y = v; break;
        case 31: if (!fit.empty()) fit.back().z = v; break;
        case 40: knots.push_back(v); break;
        case 41: weights.push_back(v); break;
        default: break;
      }
    }
    if (cvs.size() < 2) {
      if (fit.size() >= 2) {
        ON_Polyline pl;
        for (const Point3d& p : fit) pl.Append(p);
        AddCurve(ON_PolylineCurve(pl), e);
      } else {
        ++stats_.skipped;
      }
      return;
    }
    const int order = std::min(degree + 1, static_cast<int>(cvs.size()));
    const bool rational = weights.size() == cvs.size();
    ON_NurbsCurve nc;
    nc.Create(3, rational, order, static_cast<int>(cvs.size()));
    for (size_t i = 0; i < cvs.size(); ++i) {
      if (rational) {
        // ON_NurbsCurve::SetCV(ON_3dPoint) followed by SetWeight() does NOT
        // do what it looks like it does: SetCV(ON_3dPoint) always stamps the
        // homogeneous weight component to 1 first, and SetWeight() then
        // overwrites just that component without rescaling x/y/z - so the
        // pair silently rescales the point by 1/weight instead of setting a
        // weighted control point. Feed the already-weighted homogeneous
        // coordinates directly (SetCV(ON_4dPoint) stores them as-is on a
        // rational curve) so a weight != 1 lands on the exact control point
        // DXF specified, not on a corrupted one.
        const double w = weights[i];
        const Point3d& p = cvs[i];
        nc.SetCV(static_cast<int>(i), ON_4dPoint(p.x * w, p.y * w, p.z * w, w));
      } else {
        nc.SetCV(static_cast<int>(i), cvs[i]);
      }
    }
    // DXF stores cv_count + order knots (clamped); OpenNURBS drops the two
    // superfluous end knots.
    const int want = nc.KnotCount();
    if (static_cast<int>(knots.size()) == want + 2) {
      for (int i = 0; i < want; ++i) nc.SetKnot(i, knots[static_cast<size_t>(i + 1)]);
    } else if (static_cast<int>(knots.size()) == want) {
      for (int i = 0; i < want; ++i) nc.SetKnot(i, knots[static_cast<size_t>(i)]);
    } else if ((flags & 2) && cvs.size() > static_cast<size_t>(order)) {
      nc.MakePeriodicUniformKnotVector();
    } else {
      nc.MakeClampedUniformKnotVector();
    }
    if (!nc.IsValid()) {
      nc.MakeClampedUniformKnotVector();
      if (!nc.IsValid()) { ++stats_.skipped; return; }
    }
    kernel::NurbsCurve k;
    k.raw() = nc;
    AddCurve(k, e);
  }

  void LwPolyline(const DxfEntity& e) {
    const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), e.Normal());
    const double elevation = e.D(38, 0.0);
    std::vector<BulgeVertex> verts;
    for (const DxfGroup& g : e.groups) {
      const double v = std::atof(g.value.c_str());
      if (g.code == 10) { BulgeVertex bv; bv.p = Point3d(v, 0, elevation); verts.push_back(bv); }
      else if (g.code == 20 && !verts.empty()) verts.back().p.y = v;
      else if (g.code == 42 && !verts.empty()) verts.back().bulge = v;
    }
    const bool closed = (e.I(70) & 1) != 0;
    kernel::NurbsCurve k;
    if (!BulgePolylineCurve(verts, closed, k)) { ++stats_.skipped; return; }
    if (e.Has(210)) {
      ON_Xform x;
      x.Rotation(ON_Plane(ON_origin, ON_xaxis, ON_yaxis), ocs);
      k.raw().Transform(x);
    }
    AddCurve(k, e);
  }

  // POLYLINE + VERTEX... + SEQEND. Handles 2D/3D polylines and polyface meshes.
  void Polyline(const DxfEntity& e, const std::vector<DxfEntity>& vertices) {
    const int flags = e.I(70);
    if (flags & 64) {  // polyface mesh
      ON_Mesh m;
      for (const DxfEntity& v : vertices) {
        const int vf = v.I(70);
        if ((vf & 128) && !(vf & 64)) {
          int idx[4] = {std::abs(v.I(71)), std::abs(v.I(72)), std::abs(v.I(73)), std::abs(v.I(74))};
          if (idx[0] <= 0 || idx[1] <= 0 || idx[2] <= 0) continue;
          ON_MeshFace f;
          f.vi[0] = idx[0] - 1; f.vi[1] = idx[1] - 1; f.vi[2] = idx[2] - 1;
          f.vi[3] = idx[3] > 0 ? idx[3] - 1 : f.vi[2];
          bool ok = true;
          for (int i : f.vi) if (i < 0 || i >= m.VertexCount()) ok = false;
          if (ok) m.m_F.Append(f);
        } else if (vf & 64) {
          m.m_V.Append(ON_3fPoint(v.P(10)));
        }
      }
      if (m.FaceCount() == 0) { ++stats_.skipped; return; }
      m.ComputeVertexNormals();
      kernel::Mesh k;
      k.raw() = m;
      SceneObject o = SceneObject::MakeMesh(k);
      ApplyAttributes(o, e);
      doc_.Add(std::move(o));
      ++stats_.meshes;
      return;
    }
    if (flags & 16) { ++stats_.skipped; return; }  // polygon mesh: not supported
    std::vector<BulgeVertex> verts;
    for (const DxfEntity& v : vertices) {
      const int vf = v.I(70);
      if (vf & (16 | 128)) continue;  // spline frame control points / face records
      BulgeVertex bv;
      bv.p = v.P(10);
      bv.bulge = v.D(42, 0.0);
      verts.push_back(bv);
    }
    kernel::NurbsCurve k;
    if (!BulgePolylineCurve(verts, (flags & 1) != 0, k)) { ++stats_.skipped; return; }
    if (!(flags & 8) && e.Has(210)) {
      ON_Xform x;
      x.Rotation(ON_Plane(ON_origin, ON_xaxis, ON_yaxis), OcsPlane(Point3d(0, 0, 0), e.Normal()));
      k.raw().Transform(x);
    }
    AddCurve(k, e);
  }

  void Face(const DxfEntity& e) {
    const int layer = LayerFor(e.S(8, "0"));
    ON_Mesh& m = faces_[layer];
    const Point3d p[4] = {e.P(10), e.P(11), e.P(12), e.Has(13) ? e.P(13) : e.P(12)};
    const int base = m.VertexCount();
    ON_MeshFace f;
    for (int i = 0; i < 3; ++i) { m.m_V.Append(ON_3fPoint(p[i])); f.vi[i] = base + i; }
    if (p[3].DistanceTo(p[2]) > 1e-12) { m.m_V.Append(ON_3fPoint(p[3])); f.vi[3] = base + 3; }
    else f.vi[3] = f.vi[2];
    m.m_F.Append(f);
  }

  void FlushFaces() {
    for (auto& [layer, m] : faces_) {
      if (m.FaceCount() == 0) continue;
      m.CombineIdenticalVertices(true, true);
      m.ComputeVertexNormals();
      kernel::Mesh k;
      k.raw() = m;
      SceneObject o = SceneObject::MakeMesh(k);
      o.layer_index = layer;
      doc_.Add(std::move(o));
      ++stats_.meshes;
    }
    faces_.clear();
  }

  // HATCH: real import for the common, tractable case only - one boundary
  // path of type "polyline" (a closed sequence of straight/bulge-arc
  // vertices, group code 92 bit 0x2), which is what the overwhelming
  // majority of real-world HATCH entities use (matching what Dino 8's own
  // Hatch command would export if ExportDxf ever wrote native HATCH -
  // it currently doesn't). Group codes 10/20/42/72/73/92/93 repeat per
  // vertex/path, so unlike every other entity here this can't be read with
  // DxfEntity::S()/D()/I() (first-occurrence lookups) - it walks e.groups
  // in file order like a small state machine instead.
  //
  // Explicitly NOT handled, detected and skipped rather than guessed at:
  //   - more than one boundary path (islands/holes: group code 91 != 1)
  //   - edge-type boundaries (LINE/ARC/ELLIPSE/SPLINE edge records instead
  //     of a polyline vertex list - group 92 without bit 0x2)
  // Both fall into the ordinary skipped-entity count, same as DIMENSION/
  // SPLINE-boundary elsewhere in this importer.
  void Hatch(const DxfEntity& e) {
    if (e.I(91, 0) != 1) { ++stats_.skipped; return; }  // multiple loops/islands: unsupported
    size_t i = 0;
    bool is_polyline = false;
    for (; i < e.groups.size(); ++i) {
      if (e.groups[i].code == 92) { is_polyline = (std::atoi(e.groups[i].value.c_str()) & 2) != 0; ++i; break; }
    }
    if (!is_polyline) { ++stats_.skipped; return; }  // edge-type (arc/spline) boundary: unsupported
    bool has_bulge = false;
    int nverts = -1;
    for (; i < e.groups.size(); ++i) {
      const DxfGroup& g = e.groups[i];
      if (g.code == 72) has_bulge = std::atoi(g.value.c_str()) != 0;
      else if (g.code == 93) { nverts = std::atoi(g.value.c_str()); ++i; break; }
      else if (g.code == 10) break;  // no explicit 93 before the vertices: malformed, bail below
    }
    if (nverts < 3) { ++stats_.skipped; return; }
    std::vector<BulgeVertex> verts;
    for (; i < e.groups.size() && static_cast<int>(verts.size()) < nverts; ++i) {
      if (e.groups[i].code != 10) continue;
      BulgeVertex bv;
      bv.p = Point3d(std::atof(e.groups[i].value.c_str()), 0, 0);
      if (i + 1 < e.groups.size() && e.groups[i + 1].code == 20) { bv.p.y = std::atof(e.groups[i + 1].value.c_str()); ++i; }
      if (has_bulge && i + 1 < e.groups.size() && e.groups[i + 1].code == 42) { bv.bulge = std::atof(e.groups[i + 1].value.c_str()); ++i; }
      verts.push_back(bv);
    }
    if (static_cast<int>(verts.size()) != nverts) { ++stats_.skipped; return; }
    kernel::NurbsCurve boundary;
    if (!BulgePolylineCurve(verts, /*closed=*/true, boundary)) { ++stats_.skipped; return; }
    const int layer = LayerFor(e.S(8, "0"));
    const double tol = doc_.Settings().absolute_tolerance;
    bool built = false;
    if (e.I(70, 0) != 0) {  // solid fill flag
      built = drafting::BuildSolidHatch(doc_, boundary, kNoObject, layer, tol);
    } else {
      const drafting::HatchPattern* pat = drafting::HatchLibrary::Instance().Find(e.S(2, "ANSI31"));
      if (!pat) pat = drafting::HatchLibrary::Instance().Find("ANSI31");
      if (pat) {
        double scale = e.D(41, 1.0);
        if (scale <= 0) scale = 1.0;
        built = drafting::BuildPatternHatch(doc_, *pat, boundary, kNoObject, layer, tol, scale, e.D(52, 0.0), doc_.Settings().hatch_base);
      }
    }
    if (!built) { ++stats_.skipped; return; }
    ++stats_.hatches;
  }

  // DIMENSION: rebuilds a real, live/re-measurable Dino8 dimension
  // (BuildLinearDimensionGeometry/BuildRadiusDimensionGeometry, commands/
  // DimGeometry.h - the exact point-to-curve math cmd_annotate.cpp's own
  // live Dim/DimAligned/DimRadius/DimDiameter commands use) from the
  // entity's semantic definition points, rather than copying its
  // pre-rendered anonymous block (group 2) - a rebuilt dimension is
  // selectable via SelDim and re-measurable via UpdateDimensions, which
  // frozen block geometry could never be.
  //
  // Group-code -> point mapping verified against LibreDWG's dwg.spec (the
  // same struct layouts drive both its DWG encode/decode AND its DXF ascii
  // export, so they are authoritative for what a real DXF file contains -
  // see DWG_ENTITY(DIMENSION_LINEAR/ALIGNED/RADIUS/DIAMETER) in
  // build/_deps/libredwg-src/src/dwg.spec):
  //   type 0 (rotated/linear): xline1_pt=13/23/33, xline2_pt=14/24/34,
  //     def_pt=10/20/30 (a point ON the dimension line - exactly what the
  //     live DimLinear command's third "dimension line location" pick
  //     supplies), dim_rotation=50 (degrees, 0 default = horizontal).
  //   type 1 (aligned): same 13/14/10, but group 50 is the extension-line
  //     obliquing angle instead of a dimension-line rotation.
  //   type 4 (radius): def_pt=10/20/30 is the arc/circle CENTER,
  //     first_arc_pt=15/25/35 is the point on the circle the leader/
  //     dimension line touches, leader_len=40 is the stand-off beyond it.
  //   type 3 (diameter): first_arc_pt=15/25/35 is one point on the circle,
  //     def_pt=10/20/30 is "far_chord_pt" - the point diametrically
  //     opposite (per dwg.spec's own comment on DIMENSION_DIAMETER's def_pt)
  //     - so center = midpoint(15,10), radius = half their distance;
  //     leader_len=40 same meaning as radius.
  // All DIMENSION point groups are full 3D (13/23/33 etc., unlike LINE/
  // TEXT/CIRCLE's OCS-relative 10/20/30) so they need no extrusion-plane
  // transform to read; only the *orientation* used to build arrows/text
  // (which side is "up") comes from the extrusion normal (group 210), same
  // OcsPlane helper as every other entity here.
  //
  // Explicitly NOT rebuilt, detected and skipped rather than guessed at:
  //   - type 0 with an oblique (not ~0/~90 degree) dim_rotation: Dino8's own
  //     DimLinear only models horizontal/vertical dimension lines, so an
  //     arbitrarily rotated one has no faithful representation to rebuild.
  //   - angular dimensions (types 2/5): the measured angle depends on which
  //     of two complementary arc sweeps AutoCAD chose, which DXF does not
  //     encode anywhere this importer can recover - guessing risks silently
  //     measuring the wrong angle.
  //   - ordinate dimensions (type 6): which axis (X or Y) is being read is
  //     carried only in the "use X axis" bit inside the same flag byte as
  //     the block-reference/associativity bits this importer does not
  //     otherwise need to decode, and a wrong guess silently reports the
  //     wrong offset - not attempted.
  // Both categories fall into the ordinary skipped-entity count.
  void Dimension(const DxfEntity& e) {
    const int type = e.I(70) & 7;
    const double h = ImportDimTextHeight(doc_);
    const int layer = LayerFor(e.S(8, "0"));
    const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), e.Normal());
    if (type == 0 || type == 1) {
      const Point3d p0 = e.P(13), p1 = e.P(14), loc = e.P(10);
      if (p0.DistanceTo(p1) < 1e-9) { ++stats_.skipped; return; }
      LinearDimLayout L;
      L.plane = ON_Plane(p0, ocs.xaxis, ocs.yaxis);
      L.aligned = (type == 1);
      if (!L.aligned) {
        const double rot = std::fmod(std::fabs(e.D(50, 0.0)), 180.0);
        const bool near0 = rot < 1.0 || rot > 179.0;
        const bool near90 = rot > 89.0 && rot < 91.0;
        if (!near0 && !near90) { ++stats_.skipped; return; }  // oblique rotation: not representable, see comment above
        L.horizontal = near0;
        double ua, va, ub, vb, ul, vl;
        L.plane.ClosestPointTo(p0, &ua, &va); L.plane.ClosestPointTo(p1, &ub, &vb); L.plane.ClosestPointTo(loc, &ul, &vl);
        L.offset = L.horizontal ? vl : ul;
      } else {
        Vector3d n = ON_CrossProduct(L.plane.zaxis, Vector3d(p1 - p0));
        n.Unitize();
        L.offset = ON_DotProduct(loc - p0, n);
      }
      std::vector<kernel::NurbsCurve> curves;
      DimGlyphSpec text;
      std::map<std::string, std::string> tags;
      if (!BuildLinearDimensionGeometry(p0, p1, L, h, curves, text, tags)) { ++stats_.skipped; return; }
      if (AddDimensionGroupToDoc(doc_, L.aligned ? "DimAligned" : "DimLinear", layer, curves, text, tags)) ++stats_.dimensions;
      else ++stats_.skipped;
      return;
    }
    if (type == 3 || type == 4) {
      const bool diameter = (type == 3);
      const Point3d p10 = e.P(10), arc_pt = e.P(15);
      Point3d center; double radius;
      if (diameter) { center = (p10 + arc_pt) / 2.0; radius = p10.DistanceTo(arc_pt) / 2.0; }
      else { center = p10; radius = p10.DistanceTo(arc_pt); }
      if (radius < 1e-9) { ++stats_.skipped; return; }
      RadiusDimLayout L;
      L.diameter = diameter;
      L.plane = ON_Plane(center, ocs.xaxis, ocs.yaxis);
      L.dir = Vector3d(arc_pt - center);
      L.extra = e.D(40, 0.0);
      std::vector<kernel::NurbsCurve> curves;
      DimGlyphSpec text;
      std::map<std::string, std::string> tags;
      if (!BuildRadiusDimensionGeometry(center, radius, L, h, curves, text, tags)) { ++stats_.skipped; return; }
      if (AddDimensionGroupToDoc(doc_, diameter ? "DimDiameter" : "DimRadius", layer, curves, text, tags)) ++stats_.dimensions;
      else ++stats_.skipped;
      return;
    }
    ++stats_.skipped;  // angular (2/5) / ordinate (6): not tractable to rebuild correctly, see comment above
  }

  void Entity(const DxfEntity& e, const std::vector<DxfEntity>& vertices) {
    const std::string& t = e.type;
    if (t == "LINE") Line(e);
    else if (t == "POINT") Point(e);
    else if (t == "CIRCLE") Circle(e);
    else if (t == "ARC") Arc(e);
    else if (t == "TEXT") Text(e);
    else if (t == "MTEXT") MText(e);
    else if (t == "ELLIPSE") Ellipse(e);
    else if (t == "SPLINE") Spline(e);
    else if (t == "LWPOLYLINE") LwPolyline(e);
    else if (t == "POLYLINE") Polyline(e, vertices);
    else if (t == "3DFACE") Face(e);
    else if (t == "HATCH") Hatch(e);
    else if (t == "DIMENSION") Dimension(e);
    else if (t == "VERTEX" || t == "SEQEND") {}
    else ++stats_.skipped;
  }

 private:
  Document& doc_;
  DxfImportStats& stats_;
  std::map<std::string, int> layer_map_;
  std::map<int, ON_Mesh> faces_;
};

}  // namespace

bool ImportDxf(Document& doc, const std::string& path, std::string& summary) {
  summary.clear();
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    summary = "Could not open " + path;
    return false;
  }
  std::vector<DxfGroup> groups;
  {
    std::string code_line, value_line;
    while (std::getline(is, code_line)) {
      if (!std::getline(is, value_line)) break;
      if (!code_line.empty() && code_line.back() == '\r') code_line.pop_back();
      if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
      const std::string c = Trim(code_line);
      if (c.empty()) continue;
      char* end = nullptr;
      const long code = std::strtol(c.c_str(), &end, 10);
      if (end == c.c_str()) continue;
      groups.push_back({static_cast<int>(code), Trim(value_line)});
    }
  }
  if (groups.empty()) {
    summary = "Not a DXF file: " + path;
    return false;
  }

  DxfImportStats stats;
  DxfImporter importer(doc, stats);

  // Split into sections, then each section into entities (records) at
  // every group-0 boundary.
  std::string section;
  std::vector<DxfEntity> entities;   // ENTITIES section records, in order
  DxfEntity* current = nullptr;
  std::vector<DxfEntity> table_records;
  for (size_t i = 0; i < groups.size(); ++i) {
    const DxfGroup& g = groups[i];
    if (g.code == 0) {
      const std::string v = Upper(g.value);
      if (v == "SECTION") {
        section.clear();
        if (i + 1 < groups.size() && groups[i + 1].code == 2) { section = Upper(groups[i + 1].value); ++i; }
        current = nullptr;
        continue;
      }
      if (v == "ENDSEC") { section.clear(); current = nullptr; continue; }
      if (v == "EOF") break;
      if (section == "ENTITIES") {
        entities.push_back(DxfEntity{v, {}});
        current = &entities.back();
      } else if (section == "TABLES" && v == "LAYER") {
        table_records.push_back(DxfEntity{v, {}});
        current = &table_records.back();
      } else {
        current = nullptr;
      }
      continue;
    }
    if (current) current->groups.push_back(g);
  }

  for (const DxfEntity& rec : table_records) importer.DefineLayer(rec);

  for (size_t i = 0; i < entities.size(); ++i) {
    const DxfEntity& e = entities[i];
    std::vector<DxfEntity> vertices;
    if (e.type == "POLYLINE") {
      size_t j = i + 1;
      for (; j < entities.size() && entities[j].type == "VERTEX"; ++j) vertices.push_back(entities[j]);
      if (j < entities.size() && entities[j].type == "SEQEND") ++j;
      importer.Entity(e, vertices);
      i = j - 1;
      continue;
    }
    importer.Entity(e, vertices);
  }
  importer.FlushFaces();

  std::ostringstream ss;
  ss << "DXF: " << stats.curves << " curve" << (stats.curves == 1 ? "" : "s") << ", " << stats.points << " point"
     << (stats.points == 1 ? "" : "s") << ", " << stats.meshes << " mesh" << (stats.meshes == 1 ? "" : "es");
  if (stats.hatches) ss << ", " << stats.hatches << " hatch" << (stats.hatches == 1 ? "" : "es");
  if (stats.dimensions) ss << ", " << stats.dimensions << " dimension" << (stats.dimensions == 1 ? "" : "s");
  if (stats.layers) ss << ", " << stats.layers << " new layer" << (stats.layers == 1 ? "" : "s");
  if (stats.skipped) ss << "; " << stats.skipped << " unsupported entit" << (stats.skipped == 1 ? "y" : "ies") << " skipped";
  summary = ss.str();
  if (stats.curves + stats.points + stats.meshes + stats.hatches + stats.dimensions == 0) {
    if (entities.empty()) summary = "No entities found in " + path;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// DWG (via GNU LibreDWG)
// ---------------------------------------------------------------------------
//
// DWG is not a published format the way DXF is (see FileExchange.h and
// docs/INTEROP_LIMITATIONS.md), so unlike every writer/reader above this one
// is not hand-rolled: it links GNU LibreDWG, a genuine GPLv3 open-source
// implementation, the same way FreeCAD's importDWG addon does.
//
// Export bridges through Dino 8's own, already-tested DXF writer: write a
// temporary ASCII DXF with ExportDxf, hand it to LibreDWG's DXF reader
// (dxf_read_file - a public, documented entry point), then LibreDWG's own
// DWG writer (dwg_write_file). This is not a shortcut taken to avoid real
// work: LibreDWG's DXF<->DWG bridge is its own best-tested, most heavily
// exercised path (its test suite's dxf-roundtrip.sh does exactly this in
// reverse), so composing it with Dino 8's mature DXF writer gives every
// entity ExportDxf already supports (LINE/CIRCLE/ARC/LWPOLYLINE/POLYLINE/
// 3DFACE, layers, colours) real DWG bytes, without a second, independently-
// written and independently-buggy geometry-to-DWG-struct translator.
//
// Import cannot use the same trick symmetrically: LibreDWG's DWG-to-DXF
// writer (dwg_write_dxf) is only reachable through its internal headers
// (src/out_dxf.h / src/bits.h), not the public include/dwg.h and
// include/dwg_api.h this project links against. So ImportDwg instead reads
// the decoded Dwg_Data directly through the same public, stable dwg_api.h
// struct layout LibreDWG's own add_test.c and dwgadd.c programs use to
// build DWGs by hand, walking model-space entities with
// get_first_owned_entity/get_next_owned_entity (both public, declared in
// dwg.h) and switching on `fixedtype`. Coverage: LINE, POINT, CIRCLE, ARC,
// LWPOLYLINE (bulges + closed flag), TEXT and MTEXT (converted to real
// glyph-outline curves, see geom/TextOutline.h - MTEXT's inline formatting
// codes are stripped to plain multi-line text, see MTextToLines/
// BuildMTextGlyphs above for exactly what's exact vs. approximated),
// INSERT (flattened recursively into transformed copies of the referenced
// block's own entities - the same "instance is a transformed copy, not a
// live GPU reference" model InstantiateBlock (cmd_drafting.cpp) already
// uses for blocks defined in-app), and HATCH for the common case only:
// exactly one boundary path that is a polyline loop (Dwg_HATCH_Path::flag
// bit 2) - built through the same drafting::BuildSolidHatch/
// BuildPatternHatch helpers the in-app Hatch command uses
// (src/drafting/HatchBuild.h), so an imported hatch is a real hatch
// (selectable via SelHatch, rebuildable via HatchScale). Multiple boundary
// paths (islands/holes) and edge-type (line/arc/spline segment) boundaries
// fall into the skipped count, like SPLINE, 3D solids/meshes, xrefs and
// anything else outside this importer's coverage - exactly ImportDxf's own
// honest gap for entities it can't read. DIMENSION_LINEAR/DIMENSION_ALIGNED/
// DIMENSION_RADIUS/DIMENSION_DIAMETER (DWG splits DIMENSION into distinct
// entity subtypes by fixedtype, unlike DXF's one entity + a type flag) are
// rebuilt the same way as DxfImporter::Dimension - real Dino8 dimension
// geometry (commands/DimGeometry.h) from the entity's semantic definition
// points, not its frozen pre-rendered block. Angular/ordinate DIMENSION
// subtypes and the jogged-radius LARGE_RADIAL_DIMENSION/ARC_DIMENSION fall
// into the skipped count, same honest scope as DXF's angular/ordinate gap.

namespace {

// A process-unique temp file path next to `hint` (same directory, so the
// rename/open is on one filesystem) rather than a fixed name, so two
// concurrent exports never collide.
std::string TempPathNear(const std::string& hint, const std::string& suffix) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::path(hint).parent_path();
  if (dir.empty()) dir = std::filesystem::current_path(ec);
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return (dir / (".dino8_dwg_" + std::to_string(stamp) + suffix)).string();
}

struct DwgImportStats {
  int curves = 0, points = 0, blocks_flattened = 0, hatches = 0, dimensions = 0, skipped = 0, layers = 0;
};

// Resolves a DWG entity's layer name ("0" when unset - DWG's default layer,
// same convention as DXF group 8) and finds or creates the matching Dino 8
// layer, colouring a freshly-created one from the DWG LAYER's ACI index when
// available.
int DwgLayerFor(Document& doc, std::map<std::string, int>& layer_map, Dwg_Object_Entity* ent, DwgImportStats& stats) {
  std::string name = "0";
  int aci = 7;  // ACI 7 = white/black, DXF/DWG's implicit default
  if (ent->layer && ent->layer->obj && ent->layer->obj->tio.object &&
      ent->layer->obj->fixedtype == DWG_TYPE_LAYER) {
    Dwg_Object_LAYER* L = ent->layer->obj->tio.object->tio.LAYER;
    if (L->name && *L->name) name = L->name;
    if (L->color.index > 0 && L->color.index < 256) aci = L->color.index;
  }
  auto it = layer_map.find(name);
  if (it != layer_map.end()) return it->second;
  int idx;
  if (name == "0") {
    idx = 0;
  } else {
    idx = doc.FindLayer(name);
    if (idx < 0) {
      idx = doc.AddLayer(name);
      ++stats.layers;
      const std::array<int, 3> rgb = AciToRgb(aci);
      doc.Layers()[static_cast<size_t>(idx)].color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
    }
  }
  layer_map[name] = idx;
  return idx;
}

// Per-entity colour override (mirrors DxfImporter::ApplyAttributes): only
// set when the entity carries an explicit ACI 1-255, i.e. not
// BYLAYER (256) or BYBLOCK (0).
void ApplyDwgColor(SceneObject& o, const Dwg_Color& c) {
  if (c.index > 0 && c.index < 256) {
    const std::array<int, 3> rgb = AciToRgb(c.index);
    o.color_by_layer = false;
    o.color = Color::FromBytes(rgb[0], rgb[1], rgb[2]);
  }
}

// Walks the entities directly owned by `block_obj` (a BLOCK_HEADER object -
// model space itself, or a named block definition reached through an
// INSERT) and adds them to `doc`, transformed by `xf`. Recurses into INSERT
// with an accumulated transform so nested blocks flatten correctly; `depth`
// guards against a malformed or self-referential DWG looping forever.
void WalkDwgEntities(Document& doc, Dwg_Object* block_obj, const ON_Xform& xf, std::map<std::string, int>& layer_map,
                     DwgImportStats& stats, int depth) {
  if (!block_obj || depth > 16) return;
  for (Dwg_Object* o = get_first_owned_entity(block_obj); o; o = get_next_owned_entity(block_obj, o)) {
    if (!o || o->supertype != DWG_SUPERTYPE_ENTITY || !o->tio.entity) continue;
    Dwg_Object_Entity* ent = o->tio.entity;
    switch (o->fixedtype) {
      case DWG_TYPE_LINE: {
        Dwg_Entity_LINE* e = ent->tio.LINE;
        kernel::NurbsCurve k;
        if (!CurveFromON(ON_LineCurve(Point3d(e->start.x, e->start.y, e->start.z), Point3d(e->end.x, e->end.y, e->end.z)), k)) { ++stats.skipped; break; }
        k.raw().Transform(xf);
        SceneObject so = SceneObject::MakeCurve(k);
        so.layer_index = DwgLayerFor(doc, layer_map, ent, stats);
        ApplyDwgColor(so, ent->color);
        doc.Add(std::move(so));
        ++stats.curves;
        break;
      }
      case DWG_TYPE_CIRCLE: {
        Dwg_Entity_CIRCLE* e = ent->tio.CIRCLE;
        ON_Circle c(ON_Plane(Point3d(e->center.x, e->center.y, e->center.z), ON_xaxis, ON_yaxis), e->radius);
        kernel::NurbsCurve k;
        if (!CurveFromON(ON_ArcCurve(c), k)) { ++stats.skipped; break; }
        k.raw().Transform(xf);
        SceneObject so = SceneObject::MakeCurve(k);
        so.layer_index = DwgLayerFor(doc, layer_map, ent, stats);
        ApplyDwgColor(so, ent->color);
        doc.Add(std::move(so));
        ++stats.curves;
        break;
      }
      case DWG_TYPE_ARC: {
        Dwg_Entity_ARC* e = ent->tio.ARC;
        double a0 = e->start_angle, a1 = e->end_angle;
        while (a1 <= a0 + 1e-12) a1 += 2.0 * ON_PI;
        ON_Circle c(ON_Plane(Point3d(e->center.x, e->center.y, e->center.z), ON_xaxis, ON_yaxis), e->radius);
        ON_Arc arc(c, ON_Interval(a0, a1));
        kernel::NurbsCurve k;
        if (!CurveFromON(ON_ArcCurve(arc), k)) { ++stats.skipped; break; }
        k.raw().Transform(xf);
        SceneObject so = SceneObject::MakeCurve(k);
        so.layer_index = DwgLayerFor(doc, layer_map, ent, stats);
        ApplyDwgColor(so, ent->color);
        doc.Add(std::move(so));
        ++stats.curves;
        break;
      }
      case DWG_TYPE_SPLINE: {
        Dwg_Entity_SPLINE* e = ent->tio.SPLINE;
        kernel::NurbsCurve k;
        bool ok = false;
        if (e->num_ctrl_pts >= 2 && e->ctrl_pts) {
          const int degree = std::max(1, static_cast<int>(e->degree));
          const int order = std::min(degree + 1, static_cast<int>(e->num_ctrl_pts));
          const bool rational = e->weighted != 0;
          ON_NurbsCurve nc;
          nc.Create(3, rational, order, static_cast<int>(e->num_ctrl_pts));
          for (unsigned i = 0; i < e->num_ctrl_pts; ++i) {
            const Dwg_SPLINE_control_point& cp = e->ctrl_pts[i];
            if (rational) nc.SetCV(static_cast<int>(i), ON_4dPoint(cp.x * cp.w, cp.y * cp.w, cp.z * cp.w, cp.w));
            else nc.SetCV(static_cast<int>(i), ON_3dPoint(cp.x, cp.y, cp.z));
          }
          if (e->num_knots == static_cast<unsigned>(nc.KnotCount()) && e->knots) {
            for (int i = 0; i < nc.KnotCount(); ++i) nc.SetKnot(i, e->knots[i]);
          } else {
            nc.MakeClampedUniformKnotVector();
          }
          ok = CurveFromON(nc, k);
        } else if (e->num_fit_pts >= 2 && e->fit_pts) {
          // No real control-point data (some writers - including LibreDWG's
          // own dwg_add_SPLINE - only ever emit fit points, matching DXF
          // SPLINE import's identical fallback): approximate with a
          // polyline through the fit points rather than skip entirely.
          ON_Polyline pl;
          for (unsigned i = 0; i < e->num_fit_pts; ++i) pl.Append(Point3d(e->fit_pts[i].x, e->fit_pts[i].y, e->fit_pts[i].z));
          ok = CurveFromON(ON_PolylineCurve(pl), k);
        }
        if (!ok) { ++stats.skipped; break; }
        k.raw().Transform(xf);
        SceneObject so = SceneObject::MakeCurve(k);
        so.layer_index = DwgLayerFor(doc, layer_map, ent, stats);
        ApplyDwgColor(so, ent->color);
        doc.Add(std::move(so));
        ++stats.curves;
        break;
      }
      case DWG_TYPE_TEXT: {
        Dwg_Entity_TEXT* e = ent->tio.TEXT;
        if (!e->text_value || !*e->text_value || e->height <= 0) { ++stats.skipped; break; }
        ON_Plane pl(Point3d(e->ins_pt.x, e->ins_pt.y, e->elevation), ON_xaxis, ON_yaxis);
        pl.Rotate(e->rotation, ON_zaxis);
        pl.Transform(xf);
        std::vector<kernel::NurbsCurve> glyphs;
        std::string font_used;
        if (!TextToCurves(e->text_value, e->height, pl, glyphs, font_used) || glyphs.empty()) { ++stats.skipped; break; }
        const int layer = DwgLayerFor(doc, layer_map, ent, stats);
        for (kernel::NurbsCurve& g : glyphs) {
          SceneObject so = SceneObject::MakeCurve(g);
          so.layer_index = layer;
          so.user_text["Annotation"] = "Text";
          so.user_text["Style"] = "Standard";
          so.user_text["Text"] = e->text_value;
          ApplyDwgColor(so, ent->color);
          doc.Add(std::move(so));
          ++stats.curves;
        }
        break;
      }
      // MTEXT: DWG's single `text` field carries the same concatenated,
      // formatting-code-laden content as DXF's group 3/1 chunks (see
      // MTextToLines/BuildMTextGlyphs above for what's stripped/
      // approximated). No `rotation` field on this struct - x_axis_dir is
      // the rotation vector, same as DXF group 11 would be.
      case DWG_TYPE_MTEXT: {
        Dwg_Entity_MTEXT* e = ent->tio.MTEXT;
        if (!e->text || !*e->text || e->text_height <= 0) { ++stats.skipped; break; }
        const std::vector<std::string> lines = MTextToLines(e->text);
        const int attachment = static_cast<int>(e->attachment);
        double angle = 0.0;
        if (std::fabs(e->x_axis_dir.x) > 1e-12 || std::fabs(e->x_axis_dir.y) > 1e-12) {
          angle = std::atan2(e->x_axis_dir.y, e->x_axis_dir.x);
        }
        ON_Plane pl(Point3d(e->ins_pt.x, e->ins_pt.y, e->ins_pt.z), ON_xaxis, ON_yaxis);
        pl.Rotate(angle, ON_zaxis);
        pl.Transform(xf);
        std::vector<std::vector<kernel::NurbsCurve>> per_line;
        std::string font_used;
        if (!BuildMTextGlyphs(lines, e->text_height, attachment, pl, per_line, font_used)) { ++stats.skipped; break; }
        std::string plain_joined;
        for (size_t i = 0; i < lines.size(); ++i) { if (i) plain_joined += "\n"; plain_joined += lines[i]; }
        const int layer = DwgLayerFor(doc, layer_map, ent, stats);
        for (std::vector<kernel::NurbsCurve>& glyphs : per_line) {
          for (kernel::NurbsCurve& g : glyphs) {
            SceneObject so = SceneObject::MakeCurve(g);
            so.layer_index = layer;
            so.user_text["Annotation"] = "Text";
            so.user_text["Style"] = "Standard";
            so.user_text["Text"] = plain_joined;
            ApplyDwgColor(so, ent->color);
            doc.Add(std::move(so));
            ++stats.curves;
          }
        }
        break;
      }
      case DWG_TYPE_POINT: {
        Dwg_Entity_POINT* e = ent->tio.POINT;
        Point3d p(e->x, e->y, e->z);
        p = xf * p;
        SceneObject so = SceneObject::MakePoint(p);
        so.layer_index = DwgLayerFor(doc, layer_map, ent, stats);
        ApplyDwgColor(so, ent->color);
        doc.Add(std::move(so));
        ++stats.points;
        break;
      }
      case DWG_TYPE_LWPOLYLINE: {
        Dwg_Entity_LWPOLYLINE* e = ent->tio.LWPOLYLINE;
        if (e->num_points < 2) { ++stats.skipped; break; }
        std::vector<BulgeVertex> verts;
        verts.reserve(e->num_points);
        for (unsigned i = 0; i < e->num_points; ++i) {
          BulgeVertex bv;
          bv.p = Point3d(e->points[i].x, e->points[i].y, e->elevation);
          if (e->bulges && i < e->num_bulges) bv.bulge = e->bulges[i];
          verts.push_back(bv);
        }
        const bool closed = (e->flag & 512) != 0;  // DXF 70 bit 512
        kernel::NurbsCurve k;
        if (!BulgePolylineCurve(verts, closed, k)) { ++stats.skipped; break; }
        k.raw().Transform(xf);
        SceneObject so = SceneObject::MakeCurve(k);
        so.layer_index = DwgLayerFor(doc, layer_map, ent, stats);
        ApplyDwgColor(so, ent->color);
        doc.Add(std::move(so));
        ++stats.curves;
        break;
      }
      // HATCH: same "common case only" scope as ImportDxf's HATCH handler -
      // exactly one boundary path that is a polyline loop. Multiple loops
      // (islands/holes) and edge-type (line/arc/spline segment) boundaries
      // are detected and skipped, not approximated.
      case DWG_TYPE_HATCH: {
        Dwg_Entity_HATCH* e = ent->tio.HATCH;
        if (e->num_paths != 1 || !e->paths) { ++stats.skipped; break; }
        const Dwg_HATCH_Path& path = e->paths[0];
        if (!(path.flag & 2) || !path.polyline_paths || path.num_segs_or_paths < 3) { ++stats.skipped; break; }
        std::vector<BulgeVertex> verts;
        verts.reserve(path.num_segs_or_paths);
        for (BITCODE_BL i = 0; i < path.num_segs_or_paths; ++i) {
          BulgeVertex bv;
          bv.p = Point3d(path.polyline_paths[i].point.x, path.polyline_paths[i].point.y, e->elevation);
          if (path.bulges_present) bv.bulge = path.polyline_paths[i].bulge;
          verts.push_back(bv);
        }
        kernel::NurbsCurve boundary;
        if (!BulgePolylineCurve(verts, /*closed=*/true, boundary)) { ++stats.skipped; break; }
        boundary.raw().Transform(xf);
        const int layer = DwgLayerFor(doc, layer_map, ent, stats);
        const double tol = doc.Settings().absolute_tolerance;
        bool built = false;
        if (e->is_solid_fill) {
          built = drafting::BuildSolidHatch(doc, boundary, kNoObject, layer, tol);
        } else {
          const drafting::HatchPattern* pat = drafting::HatchLibrary::Instance().Find(e->name && *e->name ? e->name : "ANSI31");
          if (!pat) pat = drafting::HatchLibrary::Instance().Find("ANSI31");
          if (pat) {
            const double scale = e->scale_spacing > 0 ? e->scale_spacing : 1.0;
            built = drafting::BuildPatternHatch(doc, *pat, boundary, kNoObject, layer, tol, scale, e->angle * 180.0 / ON_PI, doc.Settings().hatch_base);
          }
        }
        if (!built) { ++stats.skipped; break; }
        ++stats.hatches;
        break;
      }
      // DIMENSION: same rebuild-from-semantic-points approach and the same
      // commands/DimGeometry.h math as DxfImporter::Dimension (see its
      // comment for the full group-code/field rationale, verified against
      // this exact dwg.spec). DWG splits DIMENSION by fixedtype instead of a
      // single entity + a type flag, so LINEAR/ALIGNED and RADIUS/DIAMETER
      // are separate cases with their own struct layouts
      // (Dwg_Entity_DIMENSION_LINEAR/ALIGNED/RADIUS/DIAMETER, dwg_api.h) -
      // xline1_pt/xline2_pt/def_pt for linear+aligned (def_pt is
      // DIMENSION_COMMON's shared field, the dimension-line location point,
      // same as DXF group 10), first_arc_pt/def_pt/leader_len for
      // radius+diameter (def_pt = far_chord_pt for diameter). Angular
      // (ANG2LN/ANG3PT) and ordinate (ORDINATE) DIMENSION subtypes, and the
      // jogged-radius LARGE_RADIAL_DIMENSION/ARC_DIMENSION entities, fall
      // into the default case below (skipped), same honest scope as DXF.
      case DWG_TYPE_DIMENSION_LINEAR:
      case DWG_TYPE_DIMENSION_ALIGNED: {
        const bool aligned = (o->fixedtype == DWG_TYPE_DIMENSION_ALIGNED);
        BITCODE_3BD raw1, raw2, raw_def, raw_ext;
        double dim_rotation = 0.0;
        if (aligned) {
          Dwg_Entity_DIMENSION_ALIGNED* e = ent->tio.DIMENSION_ALIGNED;
          raw1 = e->xline1_pt; raw2 = e->xline2_pt; raw_def = e->def_pt; raw_ext = e->extrusion;
        } else {
          Dwg_Entity_DIMENSION_LINEAR* e = ent->tio.DIMENSION_LINEAR;
          raw1 = e->xline1_pt; raw2 = e->xline2_pt; raw_def = e->def_pt; raw_ext = e->extrusion;
          dim_rotation = e->dim_rotation * 180.0 / ON_PI;  // DWG stores radians; DXF group 50 is degrees
        }
        const Point3d p0 = xf * Point3d(raw1.x, raw1.y, raw1.z);
        const Point3d p1 = xf * Point3d(raw2.x, raw2.y, raw2.z);
        const Point3d loc = xf * Point3d(raw_def.x, raw_def.y, raw_def.z);
        if (p0.DistanceTo(p1) < 1e-9) { ++stats.skipped; break; }
        const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), Vector3d(raw_ext.x, raw_ext.y, raw_ext.z));
        LinearDimLayout L;
        L.plane = ON_Plane(p0, ocs.xaxis, ocs.yaxis);
        L.aligned = aligned;
        if (!aligned) {
          const double rot = std::fmod(std::fabs(dim_rotation), 180.0);
          const bool near0 = rot < 1.0 || rot > 179.0;
          const bool near90 = rot > 89.0 && rot < 91.0;
          if (!near0 && !near90) { ++stats.skipped; break; }  // oblique: not representable, see DxfImporter::Dimension
          L.horizontal = near0;
          double ua, va, ub, vb, ul, vl;
          L.plane.ClosestPointTo(p0, &ua, &va); L.plane.ClosestPointTo(p1, &ub, &vb); L.plane.ClosestPointTo(loc, &ul, &vl);
          L.offset = L.horizontal ? vl : ul;
        } else {
          Vector3d n = ON_CrossProduct(L.plane.zaxis, Vector3d(p1 - p0));
          n.Unitize();
          L.offset = ON_DotProduct(loc - p0, n);
        }
        std::vector<kernel::NurbsCurve> curves;
        DimGlyphSpec text;
        std::map<std::string, std::string> tags;
        if (!BuildLinearDimensionGeometry(p0, p1, L, ImportDimTextHeight(doc), curves, text, tags)) { ++stats.skipped; break; }
        const int layer = DwgLayerFor(doc, layer_map, ent, stats);
        if (AddDimensionGroupToDoc(doc, aligned ? "DimAligned" : "DimLinear", layer, curves, text, tags)) ++stats.dimensions;
        else ++stats.skipped;
        break;
      }
      case DWG_TYPE_DIMENSION_RADIUS:
      case DWG_TYPE_DIMENSION_DIAMETER: {
        const bool diameter = (o->fixedtype == DWG_TYPE_DIMENSION_DIAMETER);
        BITCODE_3BD raw_arc, raw_def, raw_ext;
        double leader_len;
        if (diameter) {
          Dwg_Entity_DIMENSION_DIAMETER* e = ent->tio.DIMENSION_DIAMETER;
          raw_arc = e->first_arc_pt; raw_def = e->def_pt; leader_len = e->leader_len; raw_ext = e->extrusion;
        } else {
          Dwg_Entity_DIMENSION_RADIUS* e = ent->tio.DIMENSION_RADIUS;
          raw_arc = e->first_arc_pt; raw_def = e->def_pt; leader_len = e->leader_len; raw_ext = e->extrusion;
        }
        const Point3d arc_pt = xf * Point3d(raw_arc.x, raw_arc.y, raw_arc.z);
        const Point3d p10 = xf * Point3d(raw_def.x, raw_def.y, raw_def.z);
        Point3d center; double radius;
        if (diameter) { center = (p10 + arc_pt) / 2.0; radius = p10.DistanceTo(arc_pt) / 2.0; }
        else { center = p10; radius = p10.DistanceTo(arc_pt); }
        if (radius < 1e-9) { ++stats.skipped; break; }
        const ON_Plane ocs = OcsPlane(Point3d(0, 0, 0), Vector3d(raw_ext.x, raw_ext.y, raw_ext.z));
        RadiusDimLayout L;
        L.diameter = diameter;
        L.plane = ON_Plane(center, ocs.xaxis, ocs.yaxis);
        L.dir = Vector3d(arc_pt - center);
        L.extra = leader_len;
        std::vector<kernel::NurbsCurve> curves;
        DimGlyphSpec text;
        std::map<std::string, std::string> tags;
        if (!BuildRadiusDimensionGeometry(center, radius, L, ImportDimTextHeight(doc), curves, text, tags)) { ++stats.skipped; break; }
        const int layer = DwgLayerFor(doc, layer_map, ent, stats);
        if (AddDimensionGroupToDoc(doc, diameter ? "DimDiameter" : "DimRadius", layer, curves, text, tags)) ++stats.dimensions;
        else ++stats.skipped;
        break;
      }
      case DWG_TYPE_INSERT: {
        Dwg_Entity_INSERT* e = ent->tio.INSERT;
        Dwg_Object* blkdef = e->block_header ? e->block_header->obj : nullptr;
        if (!blkdef || blkdef->fixedtype != DWG_TYPE_BLOCK_HEADER || !blkdef->tio.object) { ++stats.skipped; break; }
        Dwg_Object_BLOCK_HEADER* bh = blkdef->tio.object->tio.BLOCK_HEADER;
        const Point3d base(bh->base_pt.x, bh->base_pt.y, bh->base_pt.z);
        const Point3d ins(e->ins_pt.x, e->ins_pt.y, e->ins_pt.z);
        const double sx = e->scale.x != 0.0 ? e->scale.x : 1.0;
        const double sy = e->scale.y != 0.0 ? e->scale.y : 1.0;
        const double sz = e->scale.z != 0.0 ? e->scale.z : 1.0;
        ON_Xform to_origin = ON_Xform::TranslationTransformation(-Vector3d(base.x, base.y, base.z));
        ON_Xform scale = ON_Xform::DiagonalTransformation(sx, sy, sz);
        ON_Xform rot = ON_Xform::IdentityTransformation;
        rot.Rotation(e->rotation, ON_zaxis, ON_origin);
        ON_Xform to_ins = ON_Xform::TranslationTransformation(Vector3d(ins.x, ins.y, ins.z));
        const ON_Xform local = to_ins * rot * scale * to_origin;
        WalkDwgEntities(doc, blkdef, xf * local, layer_map, stats, depth + 1);
        ++stats.blocks_flattened;
        break;
      }
      default:
        ++stats.skipped;
        break;
    }
  }
}

}  // namespace

bool ImportDwg(Document& doc, const std::string& path, std::string& summary) {
  summary.clear();
  Dwg_Data dwg{};
  const int err = dwg_read_file(path.c_str(), &dwg);
  if (err >= DWG_ERR_CRITICAL) {
    summary = "Could not read " + path + " (LibreDWG error 0x" + [&] { std::ostringstream h; h << std::hex << err; return h.str(); }() + ")";
    dwg_free(&dwg);
    return false;
  }
  Dwg_Object* mspace = dwg_model_space_object(&dwg);
  if (!mspace) {
    summary = "No model space found in " + path;
    dwg_free(&dwg);
    return false;
  }
  std::map<std::string, int> layer_map;
  DwgImportStats stats;
  WalkDwgEntities(doc, mspace, ON_Xform::IdentityTransformation, layer_map, stats, 0);
  dwg_free(&dwg);

  std::ostringstream ss;
  ss << "DWG: " << stats.curves << " curve" << (stats.curves == 1 ? "" : "s") << ", " << stats.points << " point"
     << (stats.points == 1 ? "" : "s");
  if (stats.hatches) ss << ", " << stats.hatches << " hatch" << (stats.hatches == 1 ? "" : "es");
  if (stats.dimensions) ss << ", " << stats.dimensions << " dimension" << (stats.dimensions == 1 ? "" : "s");
  if (stats.layers) ss << ", " << stats.layers << " new layer" << (stats.layers == 1 ? "" : "s");
  if (stats.blocks_flattened) ss << ", " << stats.blocks_flattened << " block instance" << (stats.blocks_flattened == 1 ? "" : "s") << " flattened";
  if (stats.skipped) ss << "; " << stats.skipped << " unsupported entit" << (stats.skipped == 1 ? "y" : "ies") << " skipped (angular/ordinate dimensions, multi-loop/edge-boundary hatches, 3D solids and meshes are not read back yet)";
  summary = ss.str();
  if (stats.curves + stats.points + stats.hatches + stats.dimensions == 0) {
    summary = "No supported entities found in " + path + (stats.skipped ? " (" + std::to_string(stats.skipped) + " unsupported entities skipped)" : "");
    return false;
  }
  return true;
}

bool ExportDwg(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  const std::string tmp_dxf = TempPathNear(path, ".dxf");
  if (!ExportDxf(doc, tmp_dxf, selected_only, error)) return false;

  Dwg_Data dwg{};
  int err = dxf_read_file(tmp_dxf.c_str(), &dwg);
  std::error_code ec;
  std::filesystem::remove(tmp_dxf, ec);
  if (err >= DWG_ERR_CRITICAL) {
    error = "LibreDWG could not parse the intermediate DXF (error 0x" + [&] { std::ostringstream h; h << std::hex << err; return h.str(); }() + ")";
    dwg_free(&dwg);
    return false;
  }
  err = dwg_write_file(path.c_str(), &dwg);
  dwg_free(&dwg);
  if (err >= DWG_ERR_CRITICAL) {
    error = "Could not write " + path + " (LibreDWG error 0x" + [&] { std::ostringstream h; h << std::hex << err; return h.str(); }() + ")";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Vector drawing (shared by SVG and PDF)
// ---------------------------------------------------------------------------

namespace {

struct Path2 {
  std::vector<ON_2dPoint> pts;
  bool closed = false;
  bool is_point = false;
  Color color;
  int layer = 0;
};

// World -> drawing plane. Parallel cameras project in world units (so a
// forced scale is meaningful); perspective cameras go through the viewport's
// pixel projection (y flipped so +y is up); no viewport means Top.
struct Projector {
  const Viewport* vp = nullptr;
  bool perspective = false;
  Point3d eye{0, 0, 0};
  Vector3d right{1, 0, 0}, up{0, 1, 0};

  explicit Projector(const Viewport* v) : vp(v) {
    if (!vp) return;
    const Camera& cam = vp->GetCamera();
    perspective = cam.State().perspective;
    eye = cam.State().eye;
    right = cam.Right();
    up = cam.Up();
  }
  bool Project(Point3d p, ON_2dPoint& out) const {
    if (!vp) { out.Set(p.x, p.y); return true; }
    if (perspective) {
      double px, py;
      if (!vp->WorldToPixel(p, px, py)) return false;
      out.Set(px, -py);
      return true;
    }
    const Vector3d rel = p - eye;
    out.Set(ON_DotProduct(rel, right), ON_DotProduct(rel, up));
    return true;
  }
};

std::vector<Path2> CollectPaths(const Document& doc, const Projector& proj, bool selected_only) {
  std::vector<Path2> paths;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    const Color color = doc.EffectiveColor(o);
    if (o.kind == ObjectKind::Point) {
      Path2 p;
      ON_2dPoint q;
      if (!proj.Project(o.point, q)) continue;
      p.pts.push_back(q);
      p.is_point = true;
      p.color = color;
      p.layer = o.layer_index;
      paths.push_back(std::move(p));
      continue;
    }
    for (const Polyline3& pl : ObjectPolylines(o)) {
      Path2 p;
      p.color = color;
      p.layer = o.layer_index;
      bool all_ok = true;
      for (const Point3d& w : pl.pts) {
        ON_2dPoint q;
        if (!proj.Project(w, q)) {
          all_ok = false;
          if (p.pts.size() >= 2) paths.push_back(p);
          p.pts.clear();
          continue;
        }
        p.pts.push_back(q);
      }
      if (p.pts.size() >= 2) {
        p.closed = pl.closed && all_ok;
        paths.push_back(std::move(p));
      }
    }
  }
  return paths;
}

// Page layout: maps drawing-plane coordinates to page millimetres with the
// origin at the bottom-left corner and +y up.
struct PageLayout {
  double width_mm = 297, height_mm = 210;
  double scale = 1.0;
  double ox = 0, oy = 0;  // drawing-plane point that lands at the margin corner
  double margin = 10;
  double min_x = 0, min_y = 0;
  double ToPageX(double x) const { return ox + (x - min_x) * scale; }
  double ToPageY(double y) const { return oy + (y - min_y) * scale; }
};

PageLayout LayoutPage(const std::vector<Path2>& paths, const DrawingOptions& opts, const Document& doc, bool perspective) {
  PageLayout L;
  L.width_mm = opts.page_width_mm > 0 ? opts.page_width_mm : 297.0;
  L.height_mm = opts.page_height_mm > 0 ? opts.page_height_mm : 210.0;
  L.margin = std::max(0.0, opts.margin_mm);
  double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
  for (const Path2& p : paths) {
    for (const ON_2dPoint& q : p.pts) {
      minx = std::min(minx, q.x); maxx = std::max(maxx, q.x);
      miny = std::min(miny, q.y); maxy = std::max(maxy, q.y);
    }
  }
  if (minx > maxx) { minx = miny = 0; maxx = maxy = 1; }
  const double bw = std::max(maxx - minx, 1e-9), bh = std::max(maxy - miny, 1e-9);
  L.min_x = minx; L.min_y = miny;
  const double avail_w = std::max(L.width_mm - 2 * L.margin, 1.0);
  const double avail_h = std::max(L.height_mm - 2 * L.margin, 1.0);
  if (opts.scale > 0 && !perspective) {
    L.scale = opts.scale * MillimetresPerUnit(doc);
    // Grow the page rather than clip a forced-scale print.
    L.width_mm = std::max(L.width_mm, bw * L.scale + 2 * L.margin);
    L.height_mm = std::max(L.height_mm, bh * L.scale + 2 * L.margin);
  } else {
    L.scale = std::min(avail_w / bw, avail_h / bh);
  }
  L.ox = (L.width_mm - bw * L.scale) / 2.0;
  L.oy = (L.height_mm - bh * L.scale) / 2.0;
  return L;
}

std::string HexColor(const Color& c) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", static_cast<int>(std::lround(c.r * 255)),
                static_cast<int>(std::lround(c.g * 255)), static_cast<int>(std::lround(c.b * 255)));
  return buf;
}

std::string XmlEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

std::string PdfEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '(' || c == ')' || c == '\\') o += '\\';
    if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) o += '?';
    else o += c;
  }
  return o;
}

bool PrepareDrawing(const Document& doc, const Viewport* view, bool selected_only, const DrawingOptions& opts,
                    std::vector<Path2>& paths, PageLayout& layout, std::string& error) {
  Projector proj(view);
  paths = CollectPaths(doc, proj, selected_only);
  if (paths.empty()) {
    error = selected_only ? "Nothing to export: select some visible objects" : "Nothing to export: the document is empty";
    return false;
  }
  layout = LayoutPage(paths, opts, doc, proj.perspective);
  return true;
}

}  // namespace

bool ExportSvg(const Document& doc, const Viewport* view, const std::string& path, bool selected_only,
               const DrawingOptions& opts, std::string& error) {
  std::vector<Path2> paths;
  PageLayout L;
  if (!PrepareDrawing(doc, view, selected_only, opts, paths, L, error)) return false;
  std::ofstream os(path, std::ios::binary);
  if (!os) { error = "Could not write " + path; return false; }
  const double W = L.width_mm, H = L.height_mm;
  const double marker = 1.0;  // point marker half-size in mm
  os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  os << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" << Num(W, 3) << "mm\" height=\"" << Num(H, 3)
     << "mm\" viewBox=\"0 0 " << Num(W, 3) << " " << Num(H, 3) << "\">\n";
  os << "<title>" << XmlEscape(doc.Settings().title.empty() ? std::filesystem::path(path).stem().string() : doc.Settings().title) << "</title>\n";
  os << "<desc>Exported by Dino 8" << (view ? " from the " + view->Name() + " viewport" : std::string(" (Top view)")) << "</desc>\n";
  os << "<g fill=\"none\" stroke-width=\"" << Num(opts.line_width_mm > 0 ? opts.line_width_mm : 0.25, 3)
     << "\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
  // One group per layer so Illustrator / Inkscape keep the structure.
  std::map<int, std::vector<const Path2*>> by_layer;
  for (const Path2& p : paths) by_layer[p.layer].push_back(&p);
  int written = 0;
  for (const auto& [layer, list] : by_layer) {
    std::string name = layer >= 0 && static_cast<size_t>(layer) < doc.Layers().size() ? doc.LayerFullPath(layer) : "Default";
    os << "<g id=\"" << XmlEscape(name) << "\">\n";
    for (const Path2* p : list) {
      os << "<path stroke=\"" << HexColor(p->color) << "\" d=\"";
      if (p->is_point) {
        const double x = L.ToPageX(p->pts[0].x), y = H - L.ToPageY(p->pts[0].y);
        os << "M" << Num(x - marker, 3) << " " << Num(y, 3) << " L" << Num(x + marker, 3) << " " << Num(y, 3)
           << " M" << Num(x, 3) << " " << Num(y - marker, 3) << " L" << Num(x, 3) << " " << Num(y + marker, 3);
      } else {
        for (size_t i = 0; i < p->pts.size(); ++i) {
          os << (i == 0 ? "M" : " L") << Num(L.ToPageX(p->pts[i].x), 3) << " " << Num(H - L.ToPageY(p->pts[i].y), 3);
        }
        if (p->closed) os << " Z";
      }
      os << "\"/>\n";
      ++written;
    }
    os << "</g>\n";
  }
  os << "</g>\n</svg>\n";
  if (!os) { error = "Could not write " + path; return false; }
  (void)written;
  return true;
}

bool ExportPdf(const Document& doc, const Viewport* view, const std::string& path, bool selected_only,
               const DrawingOptions& opts, std::string& error) {
  std::vector<Path2> paths;
  PageLayout L;
  if (!PrepareDrawing(doc, view, selected_only, opts, paths, L, error)) return false;
  const double pt = 72.0 / 25.4;  // points per millimetre
  const double W = L.width_mm * pt, H = L.height_mm * pt;
  const double marker = 1.0 * pt;

  // Content stream.
  std::ostringstream cs;
  cs << "q\n" << Num((opts.line_width_mm > 0 ? opts.line_width_mm : 0.25) * pt, 3) << " w 1 J 1 j\n";
  std::string last_color;
  for (const Path2& p : paths) {
    const std::string color = Num(p.color.r, 3) + " " + Num(p.color.g, 3) + " " + Num(p.color.b, 3) + " RG\n";
    if (color != last_color) { cs << color; last_color = color; }
    if (p.is_point) {
      const double x = L.ToPageX(p.pts[0].x) * pt, y = L.ToPageY(p.pts[0].y) * pt;
      cs << Num(x - marker, 3) << " " << Num(y, 3) << " m " << Num(x + marker, 3) << " " << Num(y, 3) << " l S\n";
      cs << Num(x, 3) << " " << Num(y - marker, 3) << " m " << Num(x, 3) << " " << Num(y + marker, 3) << " l S\n";
      continue;
    }
    for (size_t i = 0; i < p.pts.size(); ++i) {
      cs << Num(L.ToPageX(p.pts[i].x) * pt, 3) << " " << Num(L.ToPageY(p.pts[i].y) * pt, 3) << (i == 0 ? " m\n" : " l\n");
    }
    if (p.closed) cs << "h\n";
    cs << "S\n";
  }
  cs << "Q\n";
  const std::string content = cs.str();

  // Objects: 1 catalog, 2 pages, 3 page, 4 content, 5 info.
  std::string out;
  std::vector<size_t> offsets(6, 0);
  out += "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
  auto obj = [&](int n, const std::string& body) {
    offsets[static_cast<size_t>(n)] = out.size();
    out += std::to_string(n) + " 0 obj\n" + body + "\nendobj\n";
  };
  obj(1, "<< /Type /Catalog /Pages 2 0 R >>");
  obj(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  obj(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + Num(W, 3) + " " + Num(H, 3) + "] /Contents 4 0 R /Resources << >> >>");
  obj(4, "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
  const std::string title = doc.Settings().title.empty() ? std::filesystem::path(path).stem().string() : doc.Settings().title;
  obj(5, "<< /Producer (Dino 8) /Creator (Dino 8) /Title (" + PdfEscape(title) + ")" +
             (doc.Settings().author.empty() ? "" : " /Author (" + PdfEscape(doc.Settings().author) + ")") + " >>");
  const size_t xref = out.size();
  out += "xref\n0 6\n0000000000 65535 f \n";
  for (int i = 1; i <= 5; ++i) {
    char line[32];
    std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offsets[static_cast<size_t>(i)]);
    out += line;
  }
  out += "trailer\n<< /Size 6 /Root 1 0 R /Info 5 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";

  std::ofstream os(path, std::ios::binary);
  if (!os) { error = "Could not write " + path; return false; }
  os.write(out.data(), static_cast<std::streamsize>(out.size()));
  if (!os) { error = "Could not write " + path; return false; }
  return true;
}

// ---------------------------------------------------------------------------
// PLY
// ---------------------------------------------------------------------------

bool ExportPly(const Document& doc, const std::string& path, bool selected_only, std::string& error) {
  std::vector<const ON_Mesh*> meshes;
  // Pass 1 (serial, cheap): which objects are actually included, in
  // Objects() order - order matters for reproducible output, so this list
  // is decided up front rather than as a side effect of the parallel loop.
  std::vector<const SceneObject*> included;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    if ((o.kind == ObjectKind::Mesh && o.mesh) || (o.kind == ObjectKind::Surface && o.surface) ||
        (o.kind == ObjectKind::SubD && o.subd) || (o.kind == ObjectKind::Brep && o.brep)) {
      included.push_back(&o);
    }
  }
  // Pass 2 (parallel): tessellate each included object into its own slot.
  // Safe to parallelize - each iteration only reads its own SceneObject
  // (a distinct unique_ptr<Surface/SubD/Brep> per object, none shared) and
  // writes only to `results[i]`, its own std::optional. This is the same
  // "each object's work is fully independent of every other object's"
  // shape as EnsureDisplay's warmup pass in main.cpp's RunStressTest - see
  // util/ThreadPool.h - and unlike HoleArray's cutter generation (see the
  // comment there) nothing here calls into kernel::BooleanCombine/Manifold,
  // only tessellation (TessellateGridAdaptive/ToApproximateMesh/
  // EnsureDisplay), so there is no boolean-library thread-safety question
  // to be cautious about. The actual file write below stays a single
  // serial pass over an ordinary std::ofstream, as it must.
  std::vector<std::optional<kernel::Mesh>> results(included.size());
  ParallelFor(included.size(), [&](std::size_t i) {
    const SceneObject& o = *included[i];
    if (o.kind == ObjectKind::Mesh && o.mesh) {
      results[i] = *o.mesh;
    } else if (o.kind == ObjectKind::Surface && o.surface) {
      results[i] = o.surface->TessellateGridAdaptive(0.01);
    } else if (o.kind == ObjectKind::SubD && o.subd) {
      results[i] = o.subd->ToApproximateMesh();
    } else if (o.kind == ObjectKind::Brep && o.brep) {
      // Use the display tessellation (already a closed render mesh).
      o.EnsureDisplay(0.01, 0.05);
      const std::vector<float>& t = o.Display().triangles;
      kernel::Mesh m;
      ON_Mesh& raw = m.raw();
      for (size_t k2 = 0; k2 + 17 < t.size(); k2 += 18) {
        const int base = raw.VertexCount();
        for (int k = 0; k < 3; ++k) raw.m_V.Append(ON_3fPoint(t[k2 + k * 6], t[k2 + k * 6 + 1], t[k2 + k * 6 + 2]));
        ON_MeshFace f; f.vi[0] = base; f.vi[1] = base + 1; f.vi[2] = base + 2; f.vi[3] = base + 2;
        raw.m_F.Append(f);
      }
      raw.CombineIdenticalVertices(true, true);
      if (raw.FaceCount() > 0) results[i] = m;
    }
  });
  std::vector<kernel::Mesh> owned;
  owned.reserve(results.size());
  for (std::optional<kernel::Mesh>& r : results) if (r) owned.push_back(std::move(*r));
  for (const kernel::Mesh& m : owned) meshes.push_back(&m.raw());
  if (meshes.empty()) {
    error = "Nothing to export: select meshes, surfaces, polysurfaces or SubDs";
    return false;
  }
  std::ofstream os(path, std::ios::binary);
  if (!os) { error = "Could not write " + path; return false; }
  int nv = 0, nf = 0;
  bool normals = true;
  for (const ON_Mesh* m : meshes) {
    nv += m->VertexCount();
    nf += m->FaceCount();
    if (m->m_N.Count() != m->VertexCount()) normals = false;
  }
  os << "ply\nformat ascii 1.0\ncomment Exported by Dino 8\n";
  os << "element vertex " << nv << "\nproperty float x\nproperty float y\nproperty float z\n";
  if (normals) os << "property float nx\nproperty float ny\nproperty float nz\n";
  os << "element face " << nf << "\nproperty list uchar int vertex_indices\nend_header\n";
  for (const ON_Mesh* m : meshes) {
    for (int i = 0; i < m->VertexCount(); ++i) {
      const ON_3dPoint p = m->Vertex(i);
      os << Num(p.x) << " " << Num(p.y) << " " << Num(p.z);
      if (normals) { const ON_3fVector& n = m->m_N[i]; os << " " << Num(n.x) << " " << Num(n.y) << " " << Num(n.z); }
      os << "\n";
    }
  }
  int base = 0;
  for (const ON_Mesh* m : meshes) {
    for (int i = 0; i < m->FaceCount(); ++i) {
      const ON_MeshFace& f = m->m_F[i];
      if (f.IsTriangle()) os << "3 " << base + f.vi[0] << " " << base + f.vi[1] << " " << base + f.vi[2] << "\n";
      else os << "4 " << base + f.vi[0] << " " << base + f.vi[1] << " " << base + f.vi[2] << " " << base + f.vi[3] << "\n";
    }
    base += m->VertexCount();
  }
  if (!os) { error = "Could not write " + path; return false; }
  return true;
}

namespace {

struct PlyProperty {
  std::string name;
  std::string type;        // scalar type, or the item type of a list
  std::string count_type;  // non-empty for list properties
};

struct PlyElement {
  std::string name;
  long count = 0;
  std::vector<PlyProperty> props;
};

size_t PlyTypeSize(const std::string& t) {
  if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
  if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
  if (t == "int" || t == "uint" || t == "float" || t == "int32" || t == "uint32" || t == "float32") return 4;
  if (t == "double" || t == "float64") return 8;
  return 4;
}

double PlyReadBinary(std::istream& is, const std::string& t, bool big_endian) {
  unsigned char buf[8];
  const size_t n = PlyTypeSize(t);
  is.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(n));
  if (big_endian) std::reverse(buf, buf + n);
  if (t == "char" || t == "int8") return static_cast<signed char>(buf[0]);
  if (t == "uchar" || t == "uint8") return buf[0];
  if (t == "short" || t == "int16") { std::int16_t v; std::memcpy(&v, buf, 2); return v; }
  if (t == "ushort" || t == "uint16") { std::uint16_t v; std::memcpy(&v, buf, 2); return v; }
  if (t == "int" || t == "int32") { std::int32_t v; std::memcpy(&v, buf, 4); return v; }
  if (t == "uint" || t == "uint32") { std::uint32_t v; std::memcpy(&v, buf, 4); return v; }
  if (t == "double" || t == "float64") { double v; std::memcpy(&v, buf, 8); return v; }
  float v; std::memcpy(&v, buf, 4); return v;
}

}  // namespace

bool ImportPly(Document& doc, const std::string& path, std::string& error) {
  std::ifstream is(path, std::ios::binary);
  if (!is) { error = "Could not open " + path; return false; }
  std::string line;
  if (!std::getline(is, line) || Trim(line) != "ply") { error = "Not a PLY file: " + path; return false; }
  std::string format;
  std::vector<PlyElement> elements;
  while (std::getline(is, line)) {
    line = Trim(line);
    std::istringstream ls(line);
    std::string kw;
    ls >> kw;
    if (kw == "format") { ls >> format; }
    else if (kw == "element") { PlyElement e; ls >> e.name >> e.count; elements.push_back(e); }
    else if (kw == "property" && !elements.empty()) {
      PlyProperty p;
      std::string t;
      ls >> t;
      if (t == "list") { ls >> p.count_type >> p.type >> p.name; }
      else { p.type = t; ls >> p.name; }
      elements.back().props.push_back(p);
    } else if (kw == "end_header") break;
  }
  const bool ascii = format == "ascii";
  const bool big = format == "binary_big_endian";
  if (!ascii && !big && format != "binary_little_endian") { error = "Unsupported PLY format: " + format; return false; }

  kernel::Mesh mesh;
  ON_Mesh& m = mesh.raw();
  for (const PlyElement& e : elements) {
    int ix = -1, iy = -1, iz = -1, inx = -1, iny = -1, inz = -1;
    for (size_t i = 0; i < e.props.size(); ++i) {
      const std::string& n = e.props[i].name;
      if (n == "x") ix = static_cast<int>(i); else if (n == "y") iy = static_cast<int>(i); else if (n == "z") iz = static_cast<int>(i);
      else if (n == "nx") inx = static_cast<int>(i); else if (n == "ny") iny = static_cast<int>(i); else if (n == "nz") inz = static_cast<int>(i);
    }
    const bool is_vertex = e.name == "vertex" && ix >= 0 && iy >= 0 && iz >= 0;
    const bool is_face = e.name == "face";
    for (long r = 0; r < e.count; ++r) {
      std::vector<double> scalars(e.props.size(), 0.0);
      std::vector<long> indices;
      if (ascii) {
        if (!std::getline(is, line)) { error = "PLY file ended early"; return false; }
        std::istringstream ls(line);
        for (size_t i = 0; i < e.props.size(); ++i) {
          if (!e.props[i].count_type.empty()) {
            long cnt = 0; ls >> cnt;
            for (long k = 0; k < cnt; ++k) { long v = 0; ls >> v; if (i == 0 || is_face) indices.push_back(v); }
          } else {
            ls >> scalars[i];
          }
        }
      } else {
        for (size_t i = 0; i < e.props.size(); ++i) {
          if (!e.props[i].count_type.empty()) {
            const long cnt = static_cast<long>(PlyReadBinary(is, e.props[i].count_type, big));
            for (long k = 0; k < cnt; ++k) {
              const long v = static_cast<long>(PlyReadBinary(is, e.props[i].type, big));
              if (is_face && indices.size() < 64) indices.push_back(v);
            }
          } else {
            scalars[i] = PlyReadBinary(is, e.props[i].type, big);
          }
        }
        if (!is) { error = "PLY file ended early"; return false; }
      }
      if (is_vertex) {
        m.m_V.Append(ON_3fPoint(static_cast<float>(scalars[static_cast<size_t>(ix)]), static_cast<float>(scalars[static_cast<size_t>(iy)]), static_cast<float>(scalars[static_cast<size_t>(iz)])));
        if (inx >= 0 && iny >= 0 && inz >= 0) {
          m.m_N.Append(ON_3fVector(static_cast<float>(scalars[static_cast<size_t>(inx)]), static_cast<float>(scalars[static_cast<size_t>(iny)]), static_cast<float>(scalars[static_cast<size_t>(inz)])));
        }
      } else if (is_face && indices.size() >= 3) {
        // Fan-triangulate anything beyond a quad.
        auto valid = [&](long v) { return v >= 0 && v < m.VertexCount(); };
        if (indices.size() == 4 && valid(indices[0]) && valid(indices[1]) && valid(indices[2]) && valid(indices[3])) {
          ON_MeshFace f;
          for (int k = 0; k < 4; ++k) f.vi[k] = static_cast<int>(indices[static_cast<size_t>(k)]);
          m.m_F.Append(f);
        } else {
          for (size_t k = 1; k + 1 < indices.size(); ++k) {
            if (!valid(indices[0]) || !valid(indices[k]) || !valid(indices[k + 1])) continue;
            ON_MeshFace f;
            f.vi[0] = static_cast<int>(indices[0]); f.vi[1] = static_cast<int>(indices[k]); f.vi[2] = static_cast<int>(indices[k + 1]); f.vi[3] = f.vi[2];
            m.m_F.Append(f);
          }
        }
      }
    }
  }
  if (m.m_N.Count() != m.VertexCount()) { m.m_N.Destroy(); }
  if (m.FaceCount() == 0) { error = "No faces found in " + path; return false; }
  if (m.m_N.Count() == 0) m.ComputeVertexNormals();
  SceneObject o = SceneObject::MakeMesh(mesh);
  o.name = std::filesystem::path(path).stem().string();
  doc.Add(std::move(o));
  return true;
}

}  // namespace dino8::app
