#include "doc/SceneObject.h"

#include "geom/BrepMesher.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace dino8::app {

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
  group_id = other.group_id;
  material_name = other.material_name;
  user_text = other.user_text;
  point = other.point;
  curve = other.curve ? std::make_unique<kernel::NurbsCurve>(*other.curve) : nullptr;
  surface = other.surface ? std::make_unique<kernel::NurbsSurface>(*other.surface) : nullptr;
  brep = other.brep ? std::make_unique<kernel::Brep>(*other.brep) : nullptr;
  mesh = other.mesh ? std::make_unique<kernel::Mesh>(*other.mesh) : nullptr;
  subd = other.subd ? std::make_unique<kernel::SubD>(*other.subd) : nullptr;
  cache_ = other.cache_;
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
                         kernel::BoundingBox& box, bool& has_box) {
  const ON_Mesh& raw = mesh.raw();
  const int face_count = raw.m_F.Count();
  std::vector<kernel::Vector3d> normals = mesh.ComputeVertexNormals();
  auto push = [&](int vi, const ON_3fPoint& p, const ON_3fVector& fn) {
    out.push_back(p.x);
    out.push_back(p.y);
    out.push_back(p.z);
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

void AppendBrepEdges(const kernel::Brep& brep, std::vector<float>& out) {
  const ON_Brep& raw = brep.raw();
  for (int i = 0; i < raw.m_E.Count(); ++i) {
    const ON_BrepEdge& e = raw.m_E[i];
    const ON_Interval d = e.Domain();
    const int samples = 32;
    ON_3dPoint prev = e.PointAt(d.Min());
    for (int k = 1; k <= samples; ++k) {
      const ON_3dPoint p = e.PointAt(d.ParameterAt(static_cast<double>(k) / samples));
      out.push_back(static_cast<float>(prev.x)); out.push_back(static_cast<float>(prev.y)); out.push_back(static_cast<float>(prev.z));
      out.push_back(static_cast<float>(p.x)); out.push_back(static_cast<float>(p.y)); out.push_back(static_cast<float>(p.z));
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

}  // namespace

void SceneObject::EnsureDisplay(double curve_tolerance, double surface_tolerance) const {
  if (!cache_.dirty) return;
  cache_.triangles.clear();
  cache_.lines.clear();
  cache_.points.clear();
  cache_.control_polygon.clear();
  cache_.control_points.clear();
  cache_.has_bbox = false;
  switch (kind) {
    case ObjectKind::Point:
      cache_.points = {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
      ExpandBox(cache_.bbox, cache_.has_bbox, point);
      break;
    case ObjectKind::Curve: {
      AppendCurvePolyline(*curve, curve_tolerance, cache_.lines, cache_.bbox, cache_.has_bbox);
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
      const kernel::Mesh m = surface->TessellateGrid(u, v);
      AppendMeshTriangles(m, cache_.triangles, cache_.bbox, cache_.has_bbox);
      AppendSurfaceIsocurves(*surface, cache_.lines);
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
      AppendBrepEdges(*brep, cache_.lines);
      break;
    }
    case ObjectKind::Mesh:
      AppendMeshTriangles(*mesh, cache_.triangles, cache_.bbox, cache_.has_bbox);
      AppendMeshEdges(*mesh, cache_.lines);
      break;
    case ObjectKind::SubD: {
      const kernel::Mesh m = subd->ToApproximateMesh();
      AppendMeshTriangles(m, cache_.triangles, cache_.bbox, cache_.has_bbox);
      AppendMeshEdges(m, cache_.lines);
      break;
    }
  }
  cache_.dirty = false;
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
