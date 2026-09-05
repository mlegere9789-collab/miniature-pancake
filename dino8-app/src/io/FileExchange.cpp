#include "io/FileExchange.h"

#include "viewport/Viewport.h"

#include <opennurbs.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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

struct DxfImportStats {
  int curves = 0, points = 0, meshes = 0, skipped = 0, layers = 0;
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
      nc.SetCV(static_cast<int>(i), cvs[i]);
      if (rational) nc.SetWeight(static_cast<int>(i), weights[i]);
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

  void Entity(const DxfEntity& e, const std::vector<DxfEntity>& vertices) {
    const std::string& t = e.type;
    if (t == "LINE") Line(e);
    else if (t == "POINT") Point(e);
    else if (t == "CIRCLE") Circle(e);
    else if (t == "ARC") Arc(e);
    else if (t == "ELLIPSE") Ellipse(e);
    else if (t == "SPLINE") Spline(e);
    else if (t == "LWPOLYLINE") LwPolyline(e);
    else if (t == "POLYLINE") Polyline(e, vertices);
    else if (t == "3DFACE") Face(e);
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
  if (stats.layers) ss << ", " << stats.layers << " new layer" << (stats.layers == 1 ? "" : "s");
  if (stats.skipped) ss << "; " << stats.skipped << " unsupported entit" << (stats.skipped == 1 ? "y" : "ies") << " skipped";
  summary = ss.str();
  if (stats.curves + stats.points + stats.meshes == 0) {
    if (entities.empty()) summary = "No entities found in " + path;
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
  std::vector<kernel::Mesh> owned;
  for (const SceneObject& o : doc.Objects()) {
    if (selected_only && !o.selected) continue;
    if (!doc.IsObjectVisible(o)) continue;
    if (o.kind == ObjectKind::Mesh && o.mesh) owned.push_back(*o.mesh);
    else if (o.kind == ObjectKind::Surface && o.surface) owned.push_back(o.surface->TessellateGridAdaptive(0.01));
    else if (o.kind == ObjectKind::SubD && o.subd) owned.push_back(o.subd->ToApproximateMesh());
    else if (o.kind == ObjectKind::Brep && o.brep) {
      // Use the display tessellation (already a closed render mesh).
      o.EnsureDisplay(0.01, 0.05);
      const std::vector<float>& t = o.Display().triangles;
      kernel::Mesh m;
      ON_Mesh& raw = m.raw();
      for (size_t i = 0; i + 17 < t.size(); i += 18) {
        const int base = raw.VertexCount();
        for (int k = 0; k < 3; ++k) raw.m_V.Append(ON_3fPoint(t[i + k * 6], t[i + k * 6 + 1], t[i + k * 6 + 2]));
        ON_MeshFace f; f.vi[0] = base; f.vi[1] = base + 1; f.vi[2] = base + 2; f.vi[3] = base + 2;
        raw.m_F.Append(f);
      }
      raw.CombineIdenticalVertices(true, true);
      if (raw.FaceCount() > 0) owned.push_back(m);
    }
  }
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
