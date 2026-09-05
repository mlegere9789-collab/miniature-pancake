#include "doc/SceneObject.h"

#include "geom/BrepMesher.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace dino8::app {

const char* AnalysisModeName(AnalysisMode mode) {
  switch (mode) {
    case AnalysisMode::None: return "None";
    case AnalysisMode::Zebra: return "Zebra";
    case AnalysisMode::EMap: return "EMap";
    case AnalysisMode::Curvature: return "CurvatureAnalysis";
    case AnalysisMode::DraftAngle: return "DraftAngleAnalysis";
  }
  return "None";
}

const char* TextureMappingName(TextureMapping m) {
  switch (m) {
    case TextureMapping::Default: return "Default";
    case TextureMapping::Surface: return "Surface";
    case TextureMapping::Planar: return "Planar";
    case TextureMapping::Box: return "Box";
    case TextureMapping::Cylindrical: return "Cylindrical";
    case TextureMapping::Spherical: return "Spherical";
    case TextureMapping::Custom: return "Custom";
  }
  return "Default";
}

bool ParseTextureMapping(const std::string& text, TextureMapping& out) {
  std::string t;
  for (char c : text) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  static const std::pair<const char*, TextureMapping> names[] = {
      {"default", TextureMapping::Default}, {"surface", TextureMapping::Surface}, {"planar", TextureMapping::Planar},
      {"box", TextureMapping::Box}, {"cylindrical", TextureMapping::Cylindrical}, {"spherical", TextureMapping::Spherical},
      {"custom", TextureMapping::Custom}};
  for (const auto& [n, m] : names) if (t == n) { out = m; return true; }
  return false;
}

const char* ObjectKindName(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::Point: return "point";
    case ObjectKind::Curve: return "curve";
    case ObjectKind::Surface: return "surface";
    case ObjectKind::Brep: return "polysurface";
    case ObjectKind::Mesh: return "mesh";
    case ObjectKind::SubD: return "SubD";
  }
  return "object";
}

SceneObject::SceneObject() = default;
SceneObject::~SceneObject() = default;
SceneObject::SceneObject(SceneObject&&) noexcept = default;
SceneObject& SceneObject::operator=(SceneObject&&) noexcept = default;

SceneObject::SceneObject(const SceneObject& other) { CopyFrom(other); }

SceneObject& SceneObject::operator=(const SceneObject& other) {
  if (this != &other) CopyFrom(other);
  return *this;
}

void SceneObject::CopyFrom(const SceneObject& other) {
  id = other.id;
  name = other.name;
  layer_index = other.layer_index;
  kind = other.kind;
  color = other.color;
  color_by_layer = other.color_by_layer;
  visible = other.visible;
  locked = other.locked;
  selected = other.selected;
  show_control_points = other.show_control_points;
  show_control_net = other.show_control_net;
  highlight_edges = other.highlight_edges;
  analysis = other.analysis;
  group_id = other.group_id;
  material_name = other.material_name;
  mapping = other.mapping;
  mapping_scale = other.mapping_scale;
  linetype = other.linetype;
  user_text = other.user_text;
  point = other.point;
  curve = other.curve ? std::make_unique<kernel::NurbsCurve>(*other.curve) : nullptr;
  surface = other.surface ? std::make_unique<kernel::NurbsSurface>(*other.surface) : nullptr;
  brep = other.brep ? std::make_unique<kernel::Brep>(*other.brep) : nullptr;
  mesh = other.mesh ? std::make_unique<kernel::Mesh>(*other.mesh) : nullptr;
  subd = other.subd ? std::make_unique<kernel::SubD>(*other.subd) : nullptr;
  cache_ = other.cache_;
  display_dashes_ = other.display_dashes_;
}

void SceneObject::SetDisplayDashes(const std::vector<double>& dashes) const {
  if (dashes == display_dashes_) return;
  display_dashes_ = dashes;
  cache_.dirty = true;
}

std::vector<std::vector<kernel::Point3d>> DashPolyline(const std::vector<kernel::Point3d>& points,
                                                       const std::vector<double>& pattern) {
  std::vector<std::vector<kernel::Point3d>> out;
  double total = 0;
  for (double d : pattern) total += std::max(0.0, d);
  if (points.size() < 2 || pattern.empty() || total <= 1e-9) {
    if (points.size() >= 2) out.push_back(points);
    return out;
  }
  // Walk the polyline, consuming pattern elements (even = dash, odd = gap).
  size_t idx = 0;
  double left = std::max(0.0, pattern[0]);
  bool on = true;
  std::vector<kernel::Point3d> run;
  run.push_back(points[0]);
  auto flush = [&]() {
    if (on && run.size() >= 2) out.push_back(run);
    run.clear();
  };
  auto next_element = [&](kernel::Point3d at) {
    flush();
    idx = (idx + 1) % pattern.size();
    on = (idx % 2) == 0;
    left = std::max(0.0, pattern[idx]);
    run.push_back(at);
  };
  for (size_t i = 1; i < points.size(); ++i) {
    kernel::Point3d a = points[i - 1];
    const kernel::Point3d b = points[i];
    double seg = a.DistanceTo(b);
    int guard = 0;
    while (seg > 1e-12 && guard++ < 100000) {
      if (left <= 1e-12) { next_element(a); continue; }
      if (seg <= left) {
        run.push_back(b);
        left -= seg;
        seg = 0;
      } else {
        const kernel::Point3d m = a + (b - a) * (left / seg);
        run.push_back(m);
        seg -= left;
        left = 0;
        a = m;
      }
    }
  }
  flush();
  return out;
}

SceneObject SceneObject::MakePoint(kernel::Point3d p) {
  SceneObject o;
  o.kind = ObjectKind::Point;
  o.point = p;
  return o;
}

SceneObject SceneObject::MakeCurve(const kernel::NurbsCurve& c) {
  SceneObject o;
  o.kind = ObjectKind::Curve;
  o.curve = std::make_unique<kernel::NurbsCurve>(c);
  return o;
}

SceneObject SceneObject::MakeSurface(const kernel::NurbsSurface& s) {
  SceneObject o;
  o.kind = ObjectKind::Surface;
  o.surface = std::make_unique<kernel::NurbsSurface>(s);
  return o;
}

SceneObject SceneObject::MakeBrep(const kernel::Brep& b) {
  SceneObject o;
  o.kind = ObjectKind::Brep;
  o.brep = std::make_unique<kernel::Brep>(b);
  return o;
}

SceneObject SceneObject::MakeMesh(const kernel::Mesh& m) {
  SceneObject o;
  o.kind = ObjectKind::Mesh;
  o.mesh = std::make_unique<kernel::Mesh>(m);
  return o;
}

SceneObject SceneObject::MakeSubD(const kernel::SubD& s) {
  SceneObject o;
  o.kind = ObjectKind::SubD;
  o.subd = std::make_unique<kernel::SubD>(s);
  return o;
}

void SceneObject::Transform(const ON_Xform& xform) {
  switch (kind) {
    case ObjectKind::Point: point = xform * point; break;
    case ObjectKind::Curve: curve->raw().Transform(xform); break;
    case ObjectKind::Surface: surface->raw().Transform(xform); break;
    case ObjectKind::Brep: brep->raw().Transform(xform); break;
    case ObjectKind::Mesh: *mesh = mesh->Transform(xform); break;
    case ObjectKind::SubD: subd->raw().Transform(xform); break;
  }
  // Block instances remember their insertion point (see cmd_drafting.cpp).
  auto it = user_text.find("BlockInsert");
  if (it != user_text.end()) {
    double x = 0, y = 0, z = 0;
    if (std::sscanf(it->second.c_str(), "%lf,%lf,%lf", &x, &y, &z) == 3) {
      const kernel::Point3d p = xform * kernel::Point3d(x, y, z);
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%g,%g,%g", p.x, p.y, p.z);
      it->second = buf;
    }
  }
  cache_.dirty = true;
}

namespace {

void ExpandBox(kernel::BoundingBox& box, bool& has, const kernel::Point3d& p) {
  if (!has) {
    box.min = box.max = p;
    has = true;
    return;
  }
  box.min.x = std::min(box.min.x, p.x);
  box.min.y = std::min(box.min.y, p.y);
  box.min.z = std::min(box.min.z, p.z);
  box.max.x = std::max(box.max.x, p.x);
  box.max.y = std::max(box.max.y, p.y);
  box.max.z = std::max(box.max.z, p.z);
}

void AppendMeshTriangles(const kernel::Mesh& mesh, std::vector<float>& out,
                         kernel::BoundingBox& box, bool& has_box, std::vector<float>* uvs = nullptr) {
  const ON_Mesh& raw = mesh.raw();
  const int face_count = raw.m_F.Count();
  std::vector<kernel::Vector3d> normals = mesh.ComputeVertexNormals();
  // Texture coordinates: ON_Mesh keeps them in m_S (surface parameters,
  // what the kernel writes) or the older m_T array (what Rhino files carry).
  const bool has_s = raw.m_S.Count() == raw.m_V.Count() && raw.m_V.Count() > 0;
  const bool has_t = !has_s && raw.m_T.Count() == raw.m_V.Count() && raw.m_V.Count() > 0;
  if (uvs && !has_s && !has_t) uvs = nullptr;
  auto push = [&](int vi, const ON_3fPoint& p, const ON_3fVector& fn) {
    out.push_back(p.x);
    out.push_back(p.y);
    out.push_back(p.z);
    if (uvs) {
      if (has_s) { uvs->push_back(static_cast<float>(raw.m_S[vi].x)); uvs->push_back(static_cast<float>(raw.m_S[vi].y)); }
      else { uvs->push_back(raw.m_T[vi].x); uvs->push_back(raw.m_T[vi].y); }
    }
    // Prefer smooth vertex normals; fall back to the face normal.
    if (vi >= 0 && vi < static_cast<int>(normals.size()) && normals[vi].Length() > 0.5) {
      out.push_back(static_cast<float>(normals[vi].x));
      out.push_back(static_cast<float>(normals[vi].y));
      out.push_back(static_cast<float>(normals[vi].z));
    } else {
      out.push_back(fn.x);
      out.push_back(fn.y);
      out.push_back(fn.z);
    }
    ExpandBox(box, has_box, kernel::Point3d(p.x, p.y, p.z));
  };
  for (int i = 0; i < face_count; ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const ON_3fPoint a = raw.m_V[f.vi[0]];
    const ON_3fPoint b = raw.m_V[f.vi[1]];
    const ON_3fPoint c = raw.m_V[f.vi[2]];
    ON_3fVector fn = ON_CrossProduct(b - a, c - a);
    fn.Unitize();
    push(f.vi[0], a, fn);
    push(f.vi[1], b, fn);
    push(f.vi[2], c, fn);
    if (f.IsQuad()) {
      const ON_3fPoint d = raw.m_V[f.vi[3]];
      push(f.vi[0], a, fn);
      push(f.vi[2], c, fn);
      push(f.vi[3], d, fn);
    }
  }
}

// Tessellates a NURBS surface on a u x v grid, emitting normalised
// parameter-space texture coordinates alongside the triangles.
void AppendSurfaceGrid(const kernel::NurbsSurface& srf, int nu, int nv, std::vector<float>& out,
                       std::vector<float>& uvs, kernel::BoundingBox& box, bool& has_box) {
  const kernel::Interval du = srf.Domain(0), dv = srf.Domain(1);
  const int cols = nu + 1, rows = nv + 1;
  std::vector<kernel::Point3d> pts(static_cast<size_t>(cols) * rows);
  std::vector<kernel::Vector3d> nrm(static_cast<size_t>(cols) * rows);
  for (int i = 0; i < cols; ++i) {
    for (int j = 0; j < rows; ++j) {
      const double u = du.min + (du.max - du.min) * i / nu, v = dv.min + (dv.max - dv.min) * j / nv;
      pts[static_cast<size_t>(i) * rows + j] = srf.PointAt(u, v);
      kernel::Vector3d n = srf.NormalAt(u, v);
      if (!n.Unitize()) n = kernel::Vector3d(0, 0, 0);
      nrm[static_cast<size_t>(i) * rows + j] = n;
    }
  }
  auto emit = [&](int i, int j, const kernel::Vector3d& fallback) {
    const kernel::Point3d& p = pts[static_cast<size_t>(i) * rows + j];
    kernel::Vector3d n = nrm[static_cast<size_t>(i) * rows + j];
    if (n.Length() < 0.5) n = fallback;
    out.push_back(static_cast<float>(p.x)); out.push_back(static_cast<float>(p.y)); out.push_back(static_cast<float>(p.z));
    out.push_back(static_cast<float>(n.x)); out.push_back(static_cast<float>(n.y)); out.push_back(static_cast<float>(n.z));
    uvs.push_back(static_cast<float>(i) / nu); uvs.push_back(static_cast<float>(j) / nv);
    ExpandBox(box, has_box, p);
  };
  for (int i = 0; i < nu; ++i) {
    for (int j = 0; j < nv; ++j) {
      const kernel::Point3d& a = pts[static_cast<size_t>(i) * rows + j];
      const kernel::Point3d& b = pts[static_cast<size_t>(i + 1) * rows + j];
      const kernel::Point3d& c = pts[static_cast<size_t>(i + 1) * rows + j + 1];
      const kernel::Point3d& d = pts[static_cast<size_t>(i) * rows + j + 1];
      kernel::Vector3d fn = ON_CrossProduct(c - a, d - b);
      if (!fn.Unitize()) fn = kernel::Vector3d(0, 0, 1);
      // Degenerate grid cells (poles) collapse to one triangle.
      if ((b - a).Length() > 1e-12 || (c - b).Length() > 1e-12) { emit(i, j, fn); emit(i + 1, j, fn); emit(i + 1, j + 1, fn); }
      if ((d - c).Length() > 1e-12 || (a - d).Length() > 1e-12) { emit(i, j, fn); emit(i + 1, j + 1, fn); emit(i, j + 1, fn); }
    }
  }
}

void AppendMeshEdges(const kernel::Mesh& mesh, std::vector<float>& out) {
  const ON_Mesh& raw = mesh.raw();
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) {
      const ON_3fPoint a = raw.m_V[f.vi[k]];
      const ON_3fPoint b = raw.m_V[f.vi[(k + 1) % n]];
      out.push_back(a.x); out.push_back(a.y); out.push_back(a.z);
      out.push_back(b.x); out.push_back(b.y); out.push_back(b.z);
    }
  }
}

void AppendCurvePolyline(const kernel::NurbsCurve& curve, double tolerance,
                         std::vector<float>& out, kernel::BoundingBox& box, bool& has_box) {
  std::vector<double> params = curve.SuggestedParameterValues(tolerance, 10);
  // Guarantee a reasonable minimum sample density for long, gently curved
  // spans that the chord test alone would leave coarse.
  if (params.size() < 8) {
    const kernel::Interval d = curve.Domain();
    params.clear();
    for (int i = 0; i <= 24; ++i) params.push_back(d.min + (d.max - d.min) * i / 24.0);
  }
  kernel::Point3d prev = curve.PointAt(params.front());
  ExpandBox(box, has_box, prev);
  for (size_t i = 1; i < params.size(); ++i) {
    const kernel::Point3d p = curve.PointAt(params[i]);
    out.push_back(static_cast<float>(prev.x)); out.push_back(static_cast<float>(prev.y)); out.push_back(static_cast<float>(prev.z));
    out.push_back(static_cast<float>(p.x)); out.push_back(static_cast<float>(p.y)); out.push_back(static_cast<float>(p.z));
    ExpandBox(box, has_box, p);
    prev = p;
  }
}

void AppendSurfaceIsocurves(const kernel::NurbsSurface& s, std::vector<float>& out) {
  const kernel::Interval du = s.Domain(0);
  const kernel::Interval dv = s.Domain(1);
  const int iso_u = std::max(2, s.CVCountU());
  const int iso_v = std::max(2, s.CVCountV());
  const int samples = 24;
  auto add_line = [&](kernel::Point3d a, kernel::Point3d b) {
    out.push_back(static_cast<float>(a.x)); out.push_back(static_cast<float>(a.y)); out.push_back(static_cast<float>(a.z));
    out.push_back(static_cast<float>(b.x)); out.push_back(static_cast<float>(b.y)); out.push_back(static_cast<float>(b.z));
  };
  for (int i = 0; i < iso_u; ++i) {
    const double u = du.min + (du.max - du.min) * i / (iso_u - 1);
    kernel::Point3d prev = s.PointAt(u, dv.min);
    for (int k = 1; k <= samples; ++k) {
      const double v = dv.min + (dv.max - dv.min) * k / samples;
      const kernel::Point3d p = s.PointAt(u, v);
      add_line(prev, p);
      prev = p;
    }
  }
  for (int j = 0; j < iso_v; ++j) {
    const double v = dv.min + (dv.max - dv.min) * j / (iso_v - 1);
    kernel::Point3d prev = s.PointAt(du.min, v);
    for (int k = 1; k <= samples; ++k) {
      const double u = du.min + (du.max - du.min) * k / samples;
      const kernel::Point3d p = s.PointAt(u, v);
      add_line(prev, p);
      prev = p;
    }
  }
}

// The four boundary curves of an (untrimmed) surface.
void AppendSurfaceBoundary(const kernel::NurbsSurface& s, std::vector<float>& out) {
  const kernel::Interval du = s.Domain(0);
  const kernel::Interval dv = s.Domain(1);
  const int samples = 32;
  auto add_line = [&](kernel::Point3d a, kernel::Point3d b) {
    out.push_back(static_cast<float>(a.x)); out.push_back(static_cast<float>(a.y)); out.push_back(static_cast<float>(a.z));
    out.push_back(static_cast<float>(b.x)); out.push_back(static_cast<float>(b.y)); out.push_back(static_cast<float>(b.z));
  };
  for (int side = 0; side < 4; ++side) {
    auto at = [&](double t) {
      switch (side) {
        case 0: return s.PointAt(du.min + (du.max - du.min) * t, dv.min);
        case 1: return s.PointAt(du.min + (du.max - du.min) * t, dv.max);
        case 2: return s.PointAt(du.min, dv.min + (dv.max - dv.min) * t);
        default: return s.PointAt(du.max, dv.min + (dv.max - dv.min) * t);
      }
    };
    kernel::Point3d prev = at(0);
    for (int k = 1; k <= samples; ++k) {
      const kernel::Point3d p = at(static_cast<double>(k) / samples);
      add_line(prev, p);
      prev = p;
    }
  }
}

// Mesh edges: every edge once into `edges`, edges used by a single face
// into `naked`.
void AppendMeshNakedEdges(const kernel::Mesh& mesh, std::vector<float>& edges, std::vector<float>& naked) {
  const ON_Mesh& raw = mesh.raw();
  std::unordered_map<std::uint64_t, int> use_count;
  auto key = [](int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) | static_cast<std::uint32_t>(b);
  };
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const int n = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < n; ++k) use_count[key(f.vi[k], f.vi[(k + 1) % n])]++;
  }
  for (const auto& [k, count] : use_count) {
    const int a = static_cast<int>(k >> 32), b = static_cast<int>(k & 0xffffffffu);
    if (a == b || a >= raw.m_V.Count() || b >= raw.m_V.Count()) continue;
    const ON_3fPoint pa = raw.m_V[a], pb = raw.m_V[b];
    const float seg[6] = {pa.x, pa.y, pa.z, pb.x, pb.y, pb.z};
    edges.insert(edges.end(), seg, seg + 6);
    if (count == 1) naked.insert(naked.end(), seg, seg + 6);
  }
}

// Appends the brep's edge polylines to `out` (wires) and, sorted by
// topology, to `edges` (all) and `naked` (edges with a single trim).
void AppendBrepEdges(const kernel::Brep& brep, std::vector<float>& out, std::vector<float>& edges,
                     std::vector<float>& naked) {
  const ON_Brep& raw = brep.raw();
  for (int i = 0; i < raw.m_E.Count(); ++i) {
    const ON_BrepEdge& e = raw.m_E[i];
    if (e.m_edge_index < 0) continue;
    const ON_Interval d = e.Domain();
    const int samples = 32;
    const bool is_naked = e.TrimCount() == 1;
    ON_3dPoint prev = e.PointAt(d.Min());
    for (int k = 1; k <= samples; ++k) {
      const ON_3dPoint p = e.PointAt(d.ParameterAt(static_cast<double>(k) / samples));
      const float seg[6] = {static_cast<float>(prev.x), static_cast<float>(prev.y), static_cast<float>(prev.z),
                            static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
      out.insert(out.end(), seg, seg + 6);
      edges.insert(edges.end(), seg, seg + 6);
      if (is_naked) naked.insert(naked.end(), seg, seg + 6);
      prev = p;
    }
  }
  // Isocurves on untrimmed, non-planar faces (a sphere is otherwise just
  // its seam in wireframe). Trimmed faces keep edges only so no isocurve
  // strays outside the trim.
  for (int fi = 0; fi < raw.m_F.Count(); ++fi) {
    const ON_BrepFace& f = raw.m_F[fi];
    if (f.m_face_index < 0 || f.m_li.Count() != 1) continue;
    const ON_Surface* srf = f.SurfaceOf();
    if (!srf || srf->IsPlanar(nullptr, 1e-6)) continue;
    const ON_BrepLoop& loop = raw.m_L[f.m_li[0]];
    const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
    const ON_BoundingBox pb = loop.m_pbox;
    const double eu = 1e-6 * (1 + du.Length()), ev = 1e-6 * (1 + dv.Length());
    if (!pb.IsValid() || std::fabs(pb.m_min.x - du.Min()) > eu || std::fabs(pb.m_max.x - du.Max()) > eu ||
        std::fabs(pb.m_min.y - dv.Min()) > ev || std::fabs(pb.m_max.y - dv.Max()) > ev) continue;
    const int samples = 48;
    auto emit = [&](bool along_u, double fixed) {
      ON_3dPoint prev = along_u ? srf->PointAt(du.Min(), fixed) : srf->PointAt(fixed, dv.Min());
      for (int k = 1; k <= samples; ++k) {
        const double t = static_cast<double>(k) / samples;
        const ON_3dPoint p = along_u ? srf->PointAt(du.ParameterAt(t), fixed) : srf->PointAt(fixed, dv.ParameterAt(t));
        out.push_back(static_cast<float>(prev.x)); out.push_back(static_cast<float>(prev.y)); out.push_back(static_cast<float>(prev.z));
        out.push_back(static_cast<float>(p.x)); out.push_back(static_cast<float>(p.y)); out.push_back(static_cast<float>(p.z));
        prev = p;
      }
    };
    for (double q : {0.25, 0.5, 0.75}) {
      emit(true, dv.ParameterAt(q));
      emit(false, du.ParameterAt(q));
    }
  }
  // Breps built by the kernel's own primitives may carry no edge topology
  // yet (they store trim loops separately); fall back to surface isocurves.
  if (raw.m_E.Count() == 0) {
    for (int i = 0; i < raw.m_S.Count(); ++i) {
      const ON_NurbsSurface* ns = ON_NurbsSurface::Cast(raw.m_S[i]);
      if (!ns) continue;
      kernel::NurbsSurface wrapper;
      wrapper.raw() = *ns;
      AppendSurfaceIsocurves(wrapper, out);
    }
  }
}

// Smooth display of a SubD: Catmull-Clark subdivides a copy (up to two
// levels, bounded by the resulting face count) and shows that control net.
kernel::Mesh SmoothSubDMesh(const kernel::SubD& subd, const kernel::Mesh& net) {
  int faces = subd.FaceCount(), levels = 0;
  while (levels < 2 && faces > 0 && faces * 4 <= 24000) { faces *= 4; ++levels; }
  if (levels == 0) return net;
  try {
    kernel::SubD copy = subd;
    copy.Subdivide(levels);
    return copy.ToApproximateMesh();
  } catch (...) {
    return net;
  }
}

}  // namespace

void SceneObject::EnsureDisplay(double curve_tolerance, double surface_tolerance) const {
  if (!cache_.dirty) return;
  cache_.triangles.clear();
  cache_.lines.clear();
  cache_.points.clear();
  cache_.control_polygon.clear();
  cache_.control_points.clear();
  cache_.edges.clear();
  cache_.naked_edges.clear();
  cache_.colors.clear();
  cache_.colors_valid = false;
  cache_.uvs.clear();
  cache_.mapped_uvs.clear();
  cache_.mapped_type = TextureMapping::Default;
  cache_.mapped_scale = 0.f;
  cache_.has_bbox = false;
  switch (kind) {
    case ObjectKind::Point:
      cache_.points = {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
      ExpandBox(cache_.bbox, cache_.has_bbox, point);
      break;
    case ObjectKind::Curve: {
      if (display_dashes_.empty()) {
        AppendCurvePolyline(*curve, curve_tolerance, cache_.lines, cache_.bbox, cache_.has_bbox);
      } else {
        // Dashed linetype: sample the curve, then keep only the dashes.
        std::vector<float> full;
        AppendCurvePolyline(*curve, curve_tolerance, full, cache_.bbox, cache_.has_bbox);
        std::vector<kernel::Point3d> pts;
        for (size_t i = 0; i + 5 < full.size(); i += 6) {
          if (pts.empty()) pts.emplace_back(full[i], full[i + 1], full[i + 2]);
          pts.emplace_back(full[i + 3], full[i + 4], full[i + 5]);
        }
        for (const std::vector<kernel::Point3d>& dash : DashPolyline(pts, display_dashes_)) {
          for (size_t i = 1; i < dash.size(); ++i) {
            const kernel::Point3d& p = dash[i - 1];
            const kernel::Point3d& q = dash[i];
            cache_.lines.push_back(static_cast<float>(p.x)); cache_.lines.push_back(static_cast<float>(p.y)); cache_.lines.push_back(static_cast<float>(p.z));
            cache_.lines.push_back(static_cast<float>(q.x)); cache_.lines.push_back(static_cast<float>(q.y)); cache_.lines.push_back(static_cast<float>(q.z));
          }
        }
      }
      if (show_control_points) {
        for (int i = 0; i < curve->ControlPointCount(); ++i) {
          const kernel::Point3d p = curve->ControlPointAt(i);
          cache_.control_points.push_back(static_cast<float>(p.x));
          cache_.control_points.push_back(static_cast<float>(p.y));
          cache_.control_points.push_back(static_cast<float>(p.z));
          if (i > 0) {
            const kernel::Point3d q = curve->ControlPointAt(i - 1);
            cache_.control_polygon.push_back(static_cast<float>(q.x)); cache_.control_polygon.push_back(static_cast<float>(q.y)); cache_.control_polygon.push_back(static_cast<float>(q.z));
            cache_.control_polygon.push_back(static_cast<float>(p.x)); cache_.control_polygon.push_back(static_cast<float>(p.y)); cache_.control_polygon.push_back(static_cast<float>(p.z));
          }
        }
      }
      break;
    }
    case ObjectKind::Surface: {
      const kernel::SurfaceDivisions div = surface->SuggestedDivisions(surface_tolerance);
      const int u = std::clamp(div.u, 4, 96);
      const int v = std::clamp(div.v, 4, 96);
      AppendSurfaceGrid(*surface, u, v, cache_.triangles, cache_.uvs, cache_.bbox, cache_.has_bbox);
      AppendSurfaceIsocurves(*surface, cache_.lines);
      AppendSurfaceBoundary(*surface, cache_.edges);
      cache_.naked_edges = cache_.edges;  // an untrimmed surface's edges are all naked
      if (show_control_points) {
        for (int i = 0; i < surface->CVCountU(); ++i) {
          for (int j = 0; j < surface->CVCountV(); ++j) {
            const kernel::Point3d p = surface->ControlPointAt(i, j);
            cache_.control_points.push_back(static_cast<float>(p.x));
            cache_.control_points.push_back(static_cast<float>(p.y));
            cache_.control_points.push_back(static_cast<float>(p.z));
          }
        }
      }
      break;
    }
    case ObjectKind::Brep: {
      std::vector<kernel::Mesh> meshes;
      try {
        BrepMeshOptions opt;
        opt.chord_tolerance = surface_tolerance;
        meshes = MeshBrepFaces(brep->raw(), opt);
      } catch (...) {
        meshes.clear();
      }
      for (const kernel::Mesh& m : meshes) {
        AppendMeshTriangles(m, cache_.triangles, cache_.bbox, cache_.has_bbox);
      }
      AppendBrepEdges(*brep, cache_.lines, cache_.edges, cache_.naked_edges);
      break;
    }
    case ObjectKind::Mesh:
      AppendMeshTriangles(*mesh, cache_.triangles, cache_.bbox, cache_.has_bbox, &cache_.uvs);
      AppendMeshEdges(*mesh, cache_.lines);
      AppendMeshNakedEdges(*mesh, cache_.edges, cache_.naked_edges);
      break;
    case ObjectKind::SubD: {
      const kernel::Mesh net = subd->ToApproximateMesh();
      const kernel::Mesh m = show_control_net ? net : SmoothSubDMesh(*subd, net);
      AppendMeshTriangles(m, cache_.triangles, cache_.bbox, cache_.has_bbox);
      AppendMeshEdges(show_control_net ? net : m, cache_.lines);
      AppendMeshNakedEdges(m, cache_.edges, cache_.naked_edges);
      if (show_control_points) {
        // The control net doubles as the control polygon.
        const ON_Mesh& raw = net.raw();
        for (int i = 0; i < raw.VertexCount(); ++i) {
          const ON_3dPoint p = raw.Vertex(i);
          cache_.control_points.push_back(static_cast<float>(p.x)); cache_.control_points.push_back(static_cast<float>(p.y)); cache_.control_points.push_back(static_cast<float>(p.z));
        }
        AppendMeshEdges(net, cache_.control_polygon);
      }
      break;
    }
  }
  cache_.dirty = false;
}

void SceneObject::EnsureMappedUVs(TextureMapping mapping, float scale) const {
  if (mapping == TextureMapping::Default) mapping = TextureMapping::Surface;
  if (scale <= 0.f) scale = 1.f;
  const size_t n = cache_.triangles.size() / 6;
  if (!cache_.mapped_uvs.empty() && cache_.mapped_type == mapping && cache_.mapped_scale == scale &&
      cache_.mapped_uvs.size() == n * 2) return;
  cache_.mapped_type = mapping;
  cache_.mapped_scale = scale;
  cache_.mapped_uvs.assign(n * 2, 0.f);
  if (mapping == TextureMapping::Surface && cache_.uvs.size() == n * 2) {
    for (size_t i = 0; i < n * 2; ++i) cache_.mapped_uvs[i] = cache_.uvs[i] * scale;
    return;
  }
  if (mapping == TextureMapping::Surface) mapping = TextureMapping::Box;
  // Bounding-box space: every projection is expressed in the object's
  // own box so a texture tiles once across the object at scale 1.
  const kernel::Point3d mn = cache_.has_bbox ? cache_.bbox.min : kernel::Point3d(0, 0, 0);
  const kernel::Point3d mx = cache_.has_bbox ? cache_.bbox.max : kernel::Point3d(1, 1, 1);
  const double sx = std::max(mx.x - mn.x, 1e-9), sy = std::max(mx.y - mn.y, 1e-9), sz = std::max(mx.z - mn.z, 1e-9);
  const kernel::Point3d c((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2);
  for (size_t i = 0; i < n; ++i) {
    const float* v = &cache_.triangles[i * 6];
    const double x = v[0], y = v[1], z = v[2];
    double u = 0, w = 0;
    switch (mapping) {
      case TextureMapping::Planar:
      case TextureMapping::Custom:
        u = (x - mn.x) / sx; w = (y - mn.y) / sy;
        break;
      case TextureMapping::Box: {
        const double ax = std::fabs(v[3]), ay = std::fabs(v[4]), az = std::fabs(v[5]);
        if (az >= ax && az >= ay) { u = (x - mn.x) / sx; w = (y - mn.y) / sy; }
        else if (ax >= ay) { u = (y - mn.y) / sy; w = (z - mn.z) / sz; }
        else { u = (x - mn.x) / sx; w = (z - mn.z) / sz; }
        break;
      }
      case TextureMapping::Cylindrical:
        u = std::atan2(y - c.y, x - c.x) / (2 * ON_PI) + 0.5; w = (z - mn.z) / sz;
        break;
      case TextureMapping::Spherical: {
        kernel::Vector3d d(x - c.x, y - c.y, z - c.z);
        if (!d.Unitize()) d = kernel::Vector3d(0, 0, 1);
        u = std::atan2(d.y, d.x) / (2 * ON_PI) + 0.5; w = 1.0 - std::acos(std::clamp(d.z, -1.0, 1.0)) / ON_PI;
        break;
      }
      default: break;
    }
    cache_.mapped_uvs[i * 2] = static_cast<float>(u * scale);
    cache_.mapped_uvs[i * 2 + 1] = static_cast<float>(w * scale);
  }
}

namespace {

// Rhino-style analysis ramp: blue -> cyan -> green -> yellow -> red.
void RampColor(double t, float& r, float& g, float& b) {
  t = std::clamp(t, 0.0, 1.0);
  struct Stop { double t; float r, g, b; };
  static const Stop stops[] = {{0.0, 0.0f, 0.0f, 1.0f}, {0.25, 0.0f, 1.0f, 1.0f}, {0.5, 0.0f, 1.0f, 0.0f},
                               {0.75, 1.0f, 1.0f, 0.0f}, {1.0, 1.0f, 0.0f, 0.0f}};
  for (int i = 0; i < 4; ++i) {
    if (t <= stops[i + 1].t) {
      const double f = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
      r = static_cast<float>(stops[i].r + (stops[i + 1].r - stops[i].r) * f);
      g = static_cast<float>(stops[i].g + (stops[i + 1].g - stops[i].g) * f);
      b = static_cast<float>(stops[i].b + (stops[i + 1].b - stops[i].b) * f);
      return;
    }
  }
  r = 1.0f; g = 0.0f; b = 0.0f;
}

struct VertexKey {
  float x, y, z;
  bool operator==(const VertexKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct VertexKeyHash {
  size_t operator()(const VertexKey& k) const {
    std::uint32_t a, b, c;
    std::memcpy(&a, &k.x, 4); std::memcpy(&b, &k.y, 4); std::memcpy(&c, &k.z, 4);
    return (static_cast<size_t>(a) * 73856093u) ^ (static_cast<size_t>(b) * 19349663u) ^ (static_cast<size_t>(c) * 83492791u);
  }
};

// Discrete curvature on the (unindexed) display triangles: vertices are
// welded by exact position, Gaussian curvature is the angle deficit divided
// by the barycentric vertex area, mean curvature comes from the cotangent
// Laplacian of the position. Boundary vertices, where neither formula
// holds, take the average of their interior neighbours.
std::vector<double> DiscreteCurvature(const std::vector<float>& tri, bool gaussian) {
  const size_t n_verts = tri.size() / 6;
  std::vector<int> index(n_verts);
  std::vector<kernel::Point3d> pos;
  std::vector<kernel::Vector3d> nrm;
  std::unordered_map<VertexKey, int, VertexKeyHash> weld;
  for (size_t i = 0; i < n_verts; ++i) {
    const VertexKey k{tri[i * 6], tri[i * 6 + 1], tri[i * 6 + 2]};
    auto it = weld.find(k);
    if (it == weld.end()) {
      it = weld.emplace(k, static_cast<int>(pos.size())).first;
      pos.emplace_back(k.x, k.y, k.z);
      nrm.emplace_back(tri[i * 6 + 3], tri[i * 6 + 4], tri[i * 6 + 5]);
    }
    index[i] = it->second;
  }
  const size_t n = pos.size();
  std::vector<double> angle_sum(n, 0.0), area(n, 0.0);
  std::vector<kernel::Vector3d> lap(n, kernel::Vector3d(0, 0, 0));
  std::unordered_map<std::uint64_t, int> edge_uses;
  auto ekey = [](int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(a)) << 32) | static_cast<std::uint32_t>(b);
  };
  auto cot = [](const kernel::Vector3d& u, const kernel::Vector3d& v) {
    const double c = ON_CrossProduct(u, v).Length();
    return c > 1e-18 ? ON_DotProduct(u, v) / c : 0.0;
  };
  for (size_t t = 0; t + 2 < n_verts; t += 3) {
    const int i0 = index[t], i1 = index[t + 1], i2 = index[t + 2];
    if (i0 == i1 || i1 == i2 || i0 == i2) continue;
    const kernel::Point3d& p0 = pos[i0]; const kernel::Point3d& p1 = pos[i1]; const kernel::Point3d& p2 = pos[i2];
    const double a = ON_CrossProduct(p1 - p0, p2 - p0).Length() * 0.5;
    if (a < 1e-18) continue;
    const int idx[3] = {i0, i1, i2};
    const kernel::Point3d* pp[3] = {&p0, &p1, &p2};
    for (int k = 0; k < 3; ++k) {
      const kernel::Vector3d u = *pp[(k + 1) % 3] - *pp[k];
      const kernel::Vector3d v = *pp[(k + 2) % 3] - *pp[k];
      angle_sum[idx[k]] += ON_3dVector::Angle(u, v);
      area[idx[k]] += a / 3.0;
      // Cotangent weights: the angle at vertex k is opposite the edge (k+1, k+2).
      const double w = cot(u, v);
      const int ia = idx[(k + 1) % 3], ib = idx[(k + 2) % 3];
      lap[ia] += (*pp[(k + 2) % 3] - *pp[(k + 1) % 3]) * w;
      lap[ib] += (*pp[(k + 1) % 3] - *pp[(k + 2) % 3]) * w;
      edge_uses[ekey(ia, ib)]++;
    }
  }
  std::vector<bool> boundary(n, false);
  std::vector<std::vector<int>> nbrs(n);
  for (const auto& [k, uses] : edge_uses) {
    const int a = static_cast<int>(k >> 32), b = static_cast<int>(k & 0xffffffffu);
    nbrs[a].push_back(b); nbrs[b].push_back(a);
    if (uses == 1) { boundary[a] = true; boundary[b] = true; }
  }
  std::vector<double> value(n, 0.0);
  for (size_t i = 0; i < n; ++i) {
    if (boundary[i] || area[i] <= 1e-18) continue;
    if (gaussian) {
      value[i] = (2.0 * ON_PI - angle_sum[i]) / area[i];
    } else {
      // Laplace-Beltrami of the position is -2 H n (outward normal, convex positive).
      const kernel::Vector3d d = lap[i] / (2.0 * area[i]);
      value[i] = -0.5 * ON_DotProduct(d, nrm[i]);
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (!boundary[i]) continue;
    double sum = 0; int cnt = 0;
    for (int j : nbrs[i]) if (!boundary[j]) { sum += value[j]; ++cnt; }
    if (cnt == 0) for (int j : nbrs[i]) for (int k : nbrs[j]) if (!boundary[k]) { sum += value[k]; ++cnt; }
    value[i] = cnt ? sum / cnt : 0.0;
  }
  // Two passes of neighbour averaging: discrete curvature on an uneven
  // display tessellation is noisy vertex to vertex, and Rhino's analysis
  // mesh is smoother than ours.
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<double> smoothed(n);
    for (size_t i = 0; i < n; ++i) {
      double sum = value[i]; int cnt = 1;
      for (int j : nbrs[i]) { sum += value[j]; ++cnt; }
      smoothed[i] = sum / cnt;
    }
    value.swap(smoothed);
  }
  std::vector<double> out(n_verts);
  for (size_t i = 0; i < n_verts; ++i) out[i] = value[index[i]];
  return out;
}

}  // namespace

void SceneObject::EnsureAnalysisColors(const AnalysisSettings& settings) const {
  const bool wants_colors = settings.mode == AnalysisMode::Curvature || settings.mode == AnalysisMode::DraftAngle;
  if (!wants_colors) {
    cache_.colors.clear();
    cache_.colors_valid = false;
    return;
  }
  if (cache_.colors_valid && cache_.colors_settings.SameColoring(settings) &&
      cache_.colors.size() == cache_.triangles.size() / 2) return;
  const size_t n_verts = cache_.triangles.size() / 6;
  std::vector<double> values(n_verts, 0.0);
  double lo = settings.range_min, hi = settings.range_max;
  if (settings.mode == AnalysisMode::Curvature) {
    values = DiscreteCurvature(cache_.triangles, settings.curvature_style == CurvatureStyle::Gaussian);
    if (settings.auto_range && !values.empty()) {
      // Robust range: 5th .. 95th percentile so a few noisy vertices do not
      // wash out the ramp.
      std::vector<double> sorted = values;
      std::sort(sorted.begin(), sorted.end());
      lo = sorted[static_cast<size_t>(sorted.size() * 0.05)];
      hi = sorted[std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * 0.95))];
    }
  } else {
    kernel::Vector3d pull = settings.draft_direction;
    if (!pull.Unitize()) pull = kernel::Vector3d(0, 0, 1);
    for (size_t i = 0; i < n_verts; ++i) {
      kernel::Vector3d nv(cache_.triangles[i * 6 + 3], cache_.triangles[i * 6 + 4], cache_.triangles[i * 6 + 5]);
      const double ang = ON_3dVector::Angle(nv, pull);  // 0 .. pi
      values[i] = 90.0 - ang * 180.0 / ON_PI;
    }
    lo = settings.draft_min; hi = settings.draft_max;
  }
  if (hi - lo < 1e-12) {
    const double mid = (hi + lo) * 0.5;
    const double half = std::max(std::fabs(mid) * 0.5, 1e-9);
    lo = mid - half; hi = mid + half;
  }
  cache_.colors.resize(n_verts * 3);
  for (size_t i = 0; i < n_verts; ++i) {
    float r, g, b;
    RampColor((values[i] - lo) / (hi - lo), r, g, b);
    cache_.colors[i * 3] = r; cache_.colors[i * 3 + 1] = g; cache_.colors[i * 3 + 2] = b;
  }
  cache_.colors_min = lo;
  cache_.colors_max = hi;
  cache_.colors_settings = settings;
  cache_.colors_valid = true;
}

kernel::BoundingBox SceneObject::BoundingBox() const {
  EnsureDisplay(0.05, 0.1);
  if (cache_.has_bbox) return cache_.bbox;
  return kernel::BoundingBox{point, point};
}

std::string SceneObject::Describe() const {
  std::ostringstream out;
  out << ObjectKindName(kind);
  if (!name.empty()) out << " \"" << name << "\"";
  out << "\n  ID: " << id << "\n  Layer index: " << layer_index;
  if (linetype != "ByLayer") out << "\n  Linetype: " << linetype;
  switch (kind) {
    case ObjectKind::Point:
      out << "\n  Location: " << point.x << ", " << point.y << ", " << point.z;
      break;
    case ObjectKind::Curve:
      out << "\n  Degree: " << curve->Degree() << "\n  Control points: " << curve->ControlPointCount()
          << "\n  Rational: " << (curve->IsRational() ? "yes" : "no")
          << "\n  Closed: " << (curve->IsClosed() ? "yes" : "no")
          << "\n  Length: " << curve->Length();
      break;
    case ObjectKind::Surface:
      out << "\n  Degree: " << surface->DegreeU() << " x " << surface->DegreeV()
          << "\n  Control points: " << surface->CVCountU() << " x " << surface->CVCountV()
          << "\n  Rational: " << (surface->IsRational() ? "yes" : "no");
      break;
    case ObjectKind::Brep:
      out << "\n  Faces: " << brep->FaceCount();
      break;
    case ObjectKind::Mesh:
      out << "\n  Vertices: " << mesh->VertexCount() << "\n  Faces: " << mesh->FaceCount()
          << "\n  Closed: " << (mesh->IsClosedManifold() ? "yes" : "no");
      break;
    case ObjectKind::SubD:
      out << "\n  Faces: " << subd->FaceCount() << "\n  Vertices: " << subd->VertexCount()
          << "\n  Edges: " << subd->EdgeCount() << "\n  Crease edges: " << subd->CreaseEdgeCount();
      break;
  }
  const kernel::BoundingBox b = BoundingBox();
  out << "\n  Bounding box: (" << b.min.x << ", " << b.min.y << ", " << b.min.z << ") to ("
      << b.max.x << ", " << b.max.y << ", " << b.max.z << ")";
  for (const auto& kv : user_text) out << "\n  " << kv.first << " = " << kv.second;
  return out.str();
}

}  // namespace dino8::app
