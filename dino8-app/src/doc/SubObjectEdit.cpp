#include "doc/SubObjectEdit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "geom/BrepMesher.h"

namespace dino8::app {

using kernel::Point3d;
using kernel::Vector3d;

const char* SubObjectKindName(SubObjectKind k) {
  switch (k) {
    case SubObjectKind::Vertex: return "vertex";
    case SubObjectKind::Edge: return "edge";
    case SubObjectKind::Face: return "face";
  }
  return "vertex";
}

namespace {

bool SubDNet(const SceneObject& o, kernel::Mesh& out) {
  if (o.kind != ObjectKind::SubD || !o.subd) return false;
  try {
    out = o.subd->ToApproximateMesh();
    return true;
  } catch (...) {
    return false;
  }
}

Point3d MeshVertex(const ON_Mesh& m, int i) {
  if (m.HasDoublePrecisionVertices() && i < m.m_dV.Count()) return m.m_dV[i];
  const ON_3fPoint& v = m.m_V[i];
  return Point3d(v.x, v.y, v.z);
}

std::vector<int> FaceVertices(const ON_MeshFace& f) {
  std::vector<int> v = {f.vi[0], f.vi[1], f.vi[2]};
  if (f.vi[3] != f.vi[2]) v.push_back(f.vi[3]);
  return v;
}

using EdgeKey = std::pair<int, int>;
EdgeKey Key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

// Edge -> faces adjacency of a polygon mesh.
std::map<EdgeKey, std::vector<int>> EdgeFaces(const ON_Mesh& m) {
  std::map<EdgeKey, std::vector<int>> out;
  for (int fi = 0; fi < m.m_F.Count(); ++fi) {
    const std::vector<int> v = FaceVertices(m.m_F[fi]);
    for (size_t k = 0; k < v.size(); ++k) out[Key(v[k], v[(k + 1) % v.size()])].push_back(fi);
  }
  return out;
}

void Push(std::vector<float>& v, Point3d p) {
  v.push_back(static_cast<float>(p.x)); v.push_back(static_cast<float>(p.y)); v.push_back(static_cast<float>(p.z));
}

void PushTri(std::vector<float>& v, Point3d a, Point3d b, Point3d c) {
  Vector3d n = ON_CrossProduct(b - a, c - a);
  if (!n.Unitize()) n = Vector3d(0, 0, 1);
  for (const Point3d* p : {&a, &b, &c}) { Push(v, *p); Push(v, Point3d(n.x, n.y, n.z)); }
}

void AppendPolygonFill(const std::vector<Point3d>& poly, std::vector<float>& tris) {
  for (size_t k = 1; k + 1 < poly.size(); ++k) PushTri(tris, poly[0], poly[k], poly[k + 1]);
}

// SubD control-net vertex matching by position (the net mesh vertices are
// exactly the ON_SubDVertex control net points).
ON_SubDVertex* FindSubDVertex(ON_SubD& s, Point3d p) {
  ON_SubDVertexIterator vit = s.VertexIterator();
  const ON_SubDVertex* best = nullptr;
  double bd = 1e300;
  for (const ON_SubDVertex* v = vit.FirstVertex(); v; v = vit.NextVertex()) {
    const double d = v->ControlNetPoint().DistanceTo(p);
    if (d < bd) { bd = d; best = v; }
  }
  return bd < 1e-6 ? const_cast<ON_SubDVertex*>(best) : nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Control points / vertices
// ---------------------------------------------------------------------------

int ControlPointCount(const SceneObject& o) {
  switch (o.kind) {
    case ObjectKind::Curve: return o.curve ? o.curve->ControlPointCount() : 0;
    case ObjectKind::Surface: return o.surface ? o.surface->CVCountU() * o.surface->CVCountV() : 0;
    case ObjectKind::Mesh: return o.mesh ? o.mesh->VertexCount() : 0;
    case ObjectKind::SubD: return o.subd ? o.subd->VertexCount() : 0;
    default: return 0;
  }
}

bool SurfaceGrid(const SceneObject& o, int& nu, int& nv) {
  if (o.kind != ObjectKind::Surface || !o.surface) return false;
  nu = o.surface->CVCountU();
  nv = o.surface->CVCountV();
  return true;
}

bool ControlPointPosition(const SceneObject& o, int index, Point3d& out) {
  if (index < 0) return false;
  switch (o.kind) {
    case ObjectKind::Curve:
      if (!o.curve || index >= o.curve->ControlPointCount()) return false;
      out = o.curve->ControlPointAt(index);
      return true;
    case ObjectKind::Surface: {
      int nu, nv;
      if (!SurfaceGrid(o, nu, nv) || index >= nu * nv) return false;
      out = o.surface->ControlPointAt(index / nv, index % nv);
      return true;
    }
    case ObjectKind::Mesh:
      if (!o.mesh || index >= o.mesh->VertexCount()) return false;
      out = MeshVertex(o.mesh->raw(), index);
      return true;
    case ObjectKind::SubD: {
      kernel::Mesh net;
      if (!SubDNet(o, net) || index >= net.VertexCount()) return false;
      out = MeshVertex(net.raw(), index);
      return true;
    }
    default: return false;
  }
}

bool SetControlPointPosition(SceneObject& o, int index, Point3d p) {
  if (index < 0) return false;
  bool ok = false;
  switch (o.kind) {
    case ObjectKind::Curve:
      if (o.curve && index < o.curve->ControlPointCount()) ok = o.curve->SetControlPointAt(index, p) != kernel::Result::Failed;
      break;
    case ObjectKind::Surface: {
      int nu, nv;
      if (SurfaceGrid(o, nu, nv) && index < nu * nv) ok = o.surface->SetControlPointAt(index / nv, index % nv, p) != kernel::Result::Failed;
      break;
    }
    case ObjectKind::Mesh:
      if (o.mesh && index < o.mesh->VertexCount()) {
        ON_Mesh& m = o.mesh->raw();
        ok = m.SetVertex(index, ON_3dPoint(p));
        m.InvalidateBoundingBoxes();
        m.DestroyTopology();
      }
      break;
    case ObjectKind::SubD: {
      Point3d old;
      if (!ControlPointPosition(o, index, old)) return false;
      ON_SubDVertex* v = FindSubDVertex(o.subd->raw(), old);
      if (!v) return false;
      ok = v->SetControlNetPoint(ON_3dPoint(p), true);
      o.subd->raw().ClearEvaluationCache();
      break;
    }
    default: break;
  }
  if (ok) o.InvalidateDisplay();
  return ok;
}

// ---------------------------------------------------------------------------
// Sub-object geometry
// ---------------------------------------------------------------------------

const ON_Mesh* TopologyMesh(const SceneObject& o, kernel::Mesh& scratch) {
  if (o.kind == ObjectKind::Mesh && o.mesh) return &o.mesh->raw();
  if (o.kind == ObjectKind::SubD && SubDNet(o, scratch)) return &scratch.raw();
  return nullptr;
}

std::vector<int> SubObjectVertexIndices(const SceneObject& o, const SubObjectRef& r) {
  std::vector<int> out;
  if (r.kind == SubObjectKind::Vertex) { out.push_back(r.index); return out; }
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m) return out;
  if (r.kind == SubObjectKind::Edge) {
    if (r.index >= 0 && r.index2 >= 0 && r.index < m->VertexCount() && r.index2 < m->VertexCount()) { out.push_back(r.index); out.push_back(r.index2); }
    return out;
  }
  if (r.index >= 0 && r.index < m->m_F.Count()) out = FaceVertices(m->m_F[r.index]);
  return out;
}

std::vector<Point3d> BrepEdgePolyline(const ON_Brep& b, int ei, int samples) {
  std::vector<Point3d> pts;
  if (ei < 0 || ei >= b.m_E.Count()) return pts;
  const ON_BrepEdge& e = b.m_E[ei];
  if (e.m_edge_index < 0) return pts;
  const ON_Interval dom = e.Domain();
  const int n = std::max(2, samples);
  for (int k = 0; k <= n; ++k) pts.push_back(e.PointAt(dom.ParameterAt(static_cast<double>(k) / n)));
  return pts;
}

std::vector<Point3d> SubObjectPoints(const SceneObject& o, const SubObjectRef& r) {
  std::vector<Point3d> pts;
  if (r.kind == SubObjectKind::Vertex) {
    Point3d p;
    if (ControlPointPosition(o, r.index, p)) pts.push_back(p);
    return pts;
  }
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    if (r.kind == SubObjectKind::Edge) return BrepEdgePolyline(b, r.index, 8);
    if (r.index < 0 || r.index >= b.m_F.Count()) return pts;
    const ON_BrepFace& f = b.m_F[r.index];
    for (int li = 0; li < f.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
      for (int ti = 0; ti < loop.m_ti.Count(); ++ti) {
        const int ei = b.m_T[loop.m_ti[ti]].m_ei;
        for (const Point3d& p : BrepEdgePolyline(b, ei, 4)) pts.push_back(p);
      }
    }
    if (pts.empty()) {
      // No edge topology: the surface corners.
      const ON_Surface* s = f.SurfaceOf();
      if (s) for (int c = 0; c < 4; ++c) pts.push_back(s->PointAt(s->Domain(0).ParameterAt(c == 1 || c == 2 ? 1 : 0), s->Domain(1).ParameterAt(c >= 2 ? 1 : 0)));
    }
    return pts;
  }
  if (o.kind == ObjectKind::Surface && o.surface) {
    const kernel::NurbsSurface& s = *o.surface;
    if (r.kind == SubObjectKind::Face) {
      for (int i = 0; i < s.CVCountU(); ++i) for (int j = 0; j < s.CVCountV(); ++j) pts.push_back(s.ControlPointAt(i, j));
    } else {
      // Edge 0..3 = W, S, E, N boundary.
      const kernel::Interval du = s.Domain(0), dv = s.Domain(1);
      for (int k = 0; k <= 8; ++k) {
        const double t = k / 8.0;
        switch (r.index) {
          case 0: pts.push_back(s.PointAt(du.min, dv.min + (dv.max - dv.min) * t)); break;
          case 1: pts.push_back(s.PointAt(du.min + (du.max - du.min) * t, dv.min)); break;
          case 2: pts.push_back(s.PointAt(du.max, dv.min + (dv.max - dv.min) * t)); break;
          default: pts.push_back(s.PointAt(du.min + (du.max - du.min) * t, dv.max)); break;
        }
      }
    }
    return pts;
  }
  for (int vi : SubObjectVertexIndices(o, r)) {
    Point3d p;
    if (ControlPointPosition(o, vi, p)) pts.push_back(p);
  }
  return pts;
}

void AppendSubObjectDisplay(const SceneObject& o, const SubObjectRef& r, std::vector<float>& points,
                            std::vector<float>& lines, std::vector<float>& tris) {
  if (r.kind == SubObjectKind::Vertex) {
    Point3d p;
    if (ControlPointPosition(o, r.index, p)) Push(points, p);
    return;
  }
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    if (r.kind == SubObjectKind::Edge) {
      const std::vector<Point3d> pl = BrepEdgePolyline(b, r.index, 32);
      for (size_t k = 1; k < pl.size(); ++k) { Push(lines, pl[k - 1]); Push(lines, pl[k]); }
      return;
    }
    if (r.index < 0 || r.index >= b.m_F.Count()) return;
    BrepMeshOptions opt;
    opt.chord_tolerance = 0.05;
    std::vector<kernel::Mesh> meshes;
    try { meshes = MeshBrepFaces(b, opt); } catch (...) { return; }
    if (r.index < static_cast<int>(meshes.size())) {
      const ON_Mesh& m = meshes[static_cast<size_t>(r.index)].raw();
      for (int fi = 0; fi < m.m_F.Count(); ++fi) {
        const std::vector<int> v = FaceVertices(m.m_F[fi]);
        std::vector<Point3d> poly;
        for (int vi : v) poly.push_back(MeshVertex(m, vi));
        AppendPolygonFill(poly, tris);
      }
    }
    const ON_BrepFace& f = b.m_F[r.index];
    for (int li = 0; li < f.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
      for (int ti = 0; ti < loop.m_ti.Count(); ++ti) {
        const std::vector<Point3d> pl = BrepEdgePolyline(b, b.m_T[loop.m_ti[ti]].m_ei, 24);
        for (size_t k = 1; k < pl.size(); ++k) { Push(lines, pl[k - 1]); Push(lines, pl[k]); }
      }
    }
    return;
  }
  if (o.kind == ObjectKind::Surface) {
    const std::vector<Point3d> pl = SubObjectPoints(o, r);
    if (r.kind == SubObjectKind::Edge) {
      for (size_t k = 1; k < pl.size(); ++k) { Push(lines, pl[k - 1]); Push(lines, pl[k]); }
    } else {
      // Whole surface: its display triangles are re-tinted by the caller; draw the boundary.
      o.EnsureDisplay(0.02, 0.05);
      const std::vector<float>& e = o.Display().edges;
      lines.insert(lines.end(), e.begin(), e.end());
    }
    return;
  }
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m) return;
  if (r.kind == SubObjectKind::Edge) {
    if (r.index < 0 || r.index2 < 0 || r.index >= m->VertexCount() || r.index2 >= m->VertexCount()) return;
    Push(lines, MeshVertex(*m, r.index));
    Push(lines, MeshVertex(*m, r.index2));
    return;
  }
  if (r.index < 0 || r.index >= m->m_F.Count()) return;
  std::vector<Point3d> poly;
  for (int vi : FaceVertices(m->m_F[r.index])) poly.push_back(MeshVertex(*m, vi));
  AppendPolygonFill(poly, tris);
  for (size_t k = 0; k < poly.size(); ++k) { Push(lines, poly[k]); Push(lines, poly[(k + 1) % poly.size()]); }
}

// ---------------------------------------------------------------------------
// Brep face / edge moves (approximate MoveFace / MoveEdge)
// ---------------------------------------------------------------------------

namespace {

// Distance from p to a polyline.
double PolylineDistance(const std::vector<Point3d>& pl, Point3d p) {
  double best = 1e300;
  for (size_t k = 1; k < pl.size(); ++k) {
    ON_Line seg(pl[k - 1], pl[k]);
    double t;
    seg.ClosestPointTo(p, &t);
    t = std::clamp(t, 0.0, 1.0);
    best = std::min(best, seg.PointAt(t).DistanceTo(p));
  }
  if (pl.size() == 1) best = pl[0].DistanceTo(p);
  return best;
}

// Makes face `fi` own a NURBS surface nobody else references; returns it.
ON_NurbsSurface* OwnNurbsSurface(ON_Brep& b, int fi) {
  ON_BrepFace& f = b.m_F[fi];
  const int si = f.m_si;
  if (si < 0 || si >= b.m_S.Count() || !b.m_S[si]) return nullptr;
  int users = 0;
  for (int k = 0; k < b.m_F.Count(); ++k) if (b.m_F[k].m_face_index >= 0 && b.m_F[k].m_si == si) ++users;
  ON_NurbsSurface* ns = ON_NurbsSurface::Cast(b.m_S[si]);
  if (ns && users == 1) return ns;
  ON_NurbsSurface* fresh = new ON_NurbsSurface;
  if (b.m_S[si]->GetNurbForm(*fresh) <= 0) { delete fresh; return nullptr; }
  if (users == 1) {
    delete b.m_S[si];
    b.m_S[si] = fresh;
  } else {
    f.m_si = b.AddSurface(fresh);
  }
  f.SetProxySurface(fresh);
  return fresh;
}

// Rigidly transforms face fi's surface (duplicating a shared one).
void TransformFaceSurface(ON_Brep& b, int fi, const ON_Xform& xf) {
  ON_BrepFace& f = b.m_F[fi];
  const int si = f.m_si;
  if (si < 0 || si >= b.m_S.Count() || !b.m_S[si]) return;
  int users = 0;
  for (int k = 0; k < b.m_F.Count(); ++k) if (b.m_F[k].m_face_index >= 0 && b.m_F[k].m_si == si) ++users;
  if (users > 1) {
    ON_Surface* dup = b.m_S[si]->DuplicateSurface();
    f.m_si = b.AddSurface(dup);
    f.SetProxySurface(dup);
  }
  b.m_S[f.m_si]->Transform(xf);
  f.ClearBoundingBox();
}

void TransformEdgeCurve(ON_Brep& b, int ei, const ON_Xform& xf) {
  ON_BrepEdge& e = b.m_E[ei];
  const int ci = e.m_c3i;
  if (ci < 0 || ci >= b.m_C3.Count() || !b.m_C3[ci]) return;
  int users = 0;
  for (int k = 0; k < b.m_E.Count(); ++k) if (b.m_E[k].m_edge_index >= 0 && b.m_E[k].m_c3i == ci) ++users;
  if (users > 1) {
    ON_Curve* dup = b.m_C3[ci]->DuplicateCurve();
    const bool rev = e.ProxyCurveIsReversed();
    // SetProxyCurve() always resets the proxy's reversed flag to false, and it is
    // protected on ON_CurveProxy, so bake the reversal into the duplicate's own
    // parameterization instead of trying to restore the flag afterward.
    if (rev) dup->Reverse();
    e.m_c3i = b.AddEdgeCurve(dup);
    const ON_Interval dom = e.Domain();
    e.SetProxyCurve(dup, dup->Domain());
    e.SetDomain(dom);
  }
  b.m_C3[e.m_c3i]->Transform(xf);
}

// Re-fits an edge curve whose end vertices moved by d0 (start) / d1 (end):
// the NURBS control points slide linearly from d0 to d1.
void RefitEdgeCurve(ON_Brep& b, int ei, Vector3d d0, Vector3d d1) {
  ON_BrepEdge& e = b.m_E[ei];
  const int ci = e.m_c3i;
  if (ci < 0 || ci >= b.m_C3.Count() || !b.m_C3[ci]) return;
  ON_NurbsCurve* nc = new ON_NurbsCurve;
  if (b.m_C3[ci]->GetNurbForm(*nc) <= 0) { delete nc; return; }
  const bool rev = e.ProxyCurveIsReversed();
  const Vector3d cd0 = rev ? d1 : d0, cd1 = rev ? d0 : d1;  // curve-direction displacements
  const int n = nc->CVCount();
  for (int k = 0; k < n; ++k) {
    ON_3dPoint p;
    nc->GetCV(k, p);
    const double s = n > 1 ? static_cast<double>(k) / (n - 1) : 0.0;
    nc->SetCV(k, p + cd0 * (1 - s) + cd1 * s);
  }
  const ON_Interval dom = e.Domain();
  // nc was fit in the original curve's own parameter direction (the cd0/cd1 swap
  // above accounts for that). SetProxyCurve() always resets the reversed flag to
  // false and it's protected on ON_CurveProxy, so if the edge was reversed relative
  // to its curve, flip nc's own parameterization to match the edge instead.
  if (rev) nc->Reverse();
  delete b.m_C3[ci];
  b.m_C3[ci] = nc;
  e.SetProxyCurve(nc, nc->Domain());
  e.SetDomain(dom);
}

bool MoveBrepParts(ON_Brep& b, const std::set<int>& rigid_faces, const std::set<int>& moved_edges_in, const ON_Xform& xf) {
  if (b.m_E.Count() == 0 && rigid_faces.empty()) return false;
  // Every edge of a rigid face moves rigidly too.
  std::set<int> moved_edges = moved_edges_in;
  for (int fi : rigid_faces) {
    if (fi < 0 || fi >= b.m_F.Count()) continue;
    const ON_BrepFace& f = b.m_F[fi];
    for (int li = 0; li < f.m_li.Count(); ++li) {
      const ON_BrepLoop& loop = b.m_L[f.m_li[li]];
      for (int ti = 0; ti < loop.m_ti.Count(); ++ti) {
        const int ei = b.m_T[loop.m_ti[ti]].m_ei;
        if (ei >= 0) moved_edges.insert(ei);
      }
    }
  }
  // Original edge polylines and vertex positions.
  std::map<int, std::vector<Point3d>> original_edges;
  for (int ei : moved_edges) original_edges[ei] = BrepEdgePolyline(b, ei, 16);
  std::vector<Point3d> old_vertices(static_cast<size_t>(b.m_V.Count()));
  for (int vi = 0; vi < b.m_V.Count(); ++vi) old_vertices[static_cast<size_t>(vi)] = b.m_V[vi].point;
  double diag = 1.0;
  {
    ON_BoundingBox bb = b.BoundingBox();
    if (bb.IsValid()) diag = std::max(bb.Diagonal().Length(), 1e-6);
  }
  const double near_tol = 1e-4 * diag + 1e-6;

  // 1. Rigid faces and edges.
  for (int fi : rigid_faces) if (fi >= 0 && fi < b.m_F.Count()) TransformFaceSurface(b, fi, xf);
  std::set<int> moved_vertices;
  for (int ei : moved_edges) {
    if (ei < 0 || ei >= b.m_E.Count() || b.m_E[ei].m_edge_index < 0) continue;
    TransformEdgeCurve(b, ei, xf);
    for (int k = 0; k < 2; ++k) {
      const int vi = b.m_E[ei].m_vi[k];
      if (vi >= 0 && moved_vertices.insert(vi).second) b.m_V[vi].point = xf * b.m_V[vi].point;
    }
  }
  // 2. Neighbouring faces follow along the moved edges.
  std::set<std::tuple<int, int, int>> moved_cvs;  // (surface index, i, j)
  std::set<int> touched_faces;
  for (int ei : moved_edges) {
    if (ei < 0 || ei >= b.m_E.Count()) continue;
    const ON_BrepEdge& e = b.m_E[ei];
    for (int k = 0; k < e.m_ti.Count(); ++k) {
      const ON_BrepTrim& t = b.m_T[e.m_ti[k]];
      const int fi = t.FaceIndexOf();
      if (fi < 0 || rigid_faces.count(fi)) continue;
      ON_NurbsSurface* ns = OwnNurbsSurface(b, fi);
      if (!ns) continue;
      touched_faces.insert(fi);
      const int si = b.m_F[fi].m_si;
      const int nu = ns->CVCount(0), nv = ns->CVCount(1);
      auto move_cv = [&](int i, int j) {
        if (!moved_cvs.insert({si, i, j}).second) return;
        ON_3dPoint p;
        ns->GetCV(i, j, p);
        ns->SetCV(i, j, xf * p);
      };
      bool done = false;
      switch (t.m_iso) {
        case ON_Surface::W_iso: for (int j = 0; j < nv; ++j) move_cv(0, j); done = true; break;
        case ON_Surface::E_iso: for (int j = 0; j < nv; ++j) move_cv(nu - 1, j); done = true; break;
        case ON_Surface::S_iso: for (int i = 0; i < nu; ++i) move_cv(i, 0); done = true; break;
        case ON_Surface::N_iso: for (int i = 0; i < nu; ++i) move_cv(i, nv - 1); done = true; break;
        default: break;
      }
      if (!done) {
        // Trimmed or seam edge: the control points lying on the original edge.
        const std::vector<Point3d>& pl = original_edges[ei];
        for (int i = 0; i < nu; ++i)
          for (int j = 0; j < nv; ++j) {
            ON_3dPoint p;
            ns->GetCV(i, j, p);
            if (PolylineDistance(pl, p) <= near_tol * 10) move_cv(i, j);
          }
      }
      b.m_F[fi].ClearBoundingBox();
    }
  }
  // 3. Re-fit the other edges whose vertices moved.
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    const ON_BrepEdge& e = b.m_E[ei];
    if (e.m_edge_index < 0 || moved_edges.count(ei)) continue;
    Vector3d d0(0, 0, 0), d1(0, 0, 0);
    if (e.m_vi[0] >= 0) d0 = b.m_V[e.m_vi[0]].point - old_vertices[static_cast<size_t>(e.m_vi[0])];
    if (e.m_vi[1] >= 0) d1 = b.m_V[e.m_vi[1]].point - old_vertices[static_cast<size_t>(e.m_vi[1])];
    if (d0.Length() < 1e-12 && d1.Length() < 1e-12) continue;
    RefitEdgeCurve(b, ei, d0, d1);
  }
  b.ClearBoundingBox();
  for (int fi = 0; fi < b.m_F.Count(); ++fi) b.m_F[fi].ClearBoundingBox();
  b.DestroyMesh(ON::any_mesh);
  b.SetVertexTolerances(false);
  b.SetEdgeTolerances(false);
  b.SetTolerancesBoxesAndFlags(false, true, true, false, false, false, true);
  return true;
}

}  // namespace

bool MoveBrepFaces(ON_Brep& b, const std::vector<int>& faces, const ON_Xform& xf) {
  return MoveBrepParts(b, std::set<int>(faces.begin(), faces.end()), {}, xf);
}

bool MoveBrepEdges(ON_Brep& b, const std::vector<int>& edges, const ON_Xform& xf) {
  return MoveBrepParts(b, {}, std::set<int>(edges.begin(), edges.end()), xf);
}

// ---------------------------------------------------------------------------
// Transform / delete
// ---------------------------------------------------------------------------

bool TransformSubObjects(SceneObject& o, const std::vector<SubObjectRef>& refs, const ON_Xform& xf) {
  if (refs.empty()) return false;
  if (o.kind == ObjectKind::Brep && o.brep) {
    std::set<int> faces, edges;
    for (const SubObjectRef& r : refs) {
      if (r.kind == SubObjectKind::Face) faces.insert(r.index);
      else if (r.kind == SubObjectKind::Edge) edges.insert(r.index);
    }
    if (faces.empty() && edges.empty()) return false;
    const bool ok = MoveBrepParts(o.brep->raw(), faces, edges, xf);
    if (ok) o.InvalidateDisplay();
    return ok;
  }
  if (o.kind == ObjectKind::Surface && o.surface) {
    int nu, nv;
    SurfaceGrid(o, nu, nv);
    std::set<int> idx;
    for (const SubObjectRef& r : refs) {
      if (r.kind == SubObjectKind::Vertex) idx.insert(r.index);
      else if (r.kind == SubObjectKind::Face) for (int k = 0; k < nu * nv; ++k) idx.insert(k);
      else {
        // Boundary rows: 0 = W (i=0), 1 = S (j=0), 2 = E, 3 = N.
        for (int i = 0; i < nu; ++i)
          for (int j = 0; j < nv; ++j) {
            const bool on = (r.index == 0 && i == 0) || (r.index == 2 && i == nu - 1) || (r.index == 1 && j == 0) || (r.index == 3 && j == nv - 1);
            if (on) idx.insert(i * nv + j);
          }
      }
    }
    bool any = false;
    for (int k : idx) {
      Point3d p;
      if (ControlPointPosition(o, k, p)) any = SetControlPointPosition(o, k, xf * p) || any;
    }
    return any;
  }
  // Curves, meshes, SubDs: the union of the vertices.
  std::set<int> idx;
  for (const SubObjectRef& r : refs) for (int v : SubObjectVertexIndices(o, r)) idx.insert(v);
  if (idx.empty()) return false;
  // Read every position first (SubD matching is by position).
  std::vector<std::pair<int, Point3d>> moves;
  for (int k : idx) {
    Point3d p;
    if (ControlPointPosition(o, k, p)) moves.push_back({k, xf * p});
  }
  if (o.kind == ObjectKind::SubD && o.subd) {
    // Match every vertex before moving any (positions are the key).
    std::vector<std::pair<ON_SubDVertex*, Point3d>> targets;
    for (const auto& [k, np] : moves) {
      Point3d old;
      if (!ControlPointPosition(o, k, old)) continue;
      if (ON_SubDVertex* v = FindSubDVertex(o.subd->raw(), old)) targets.push_back({v, np});
    }
    for (auto& [v, np] : targets) v->SetControlNetPoint(ON_3dPoint(np), true);
    o.subd->raw().ClearEvaluationCache();
    o.InvalidateDisplay();
    return !targets.empty();
  }
  bool any = false;
  for (const auto& [k, np] : moves) any = SetControlPointPosition(o, k, np) || any;
  return any;
}

bool DeleteSubObjects(SceneObject& o, const std::vector<SubObjectRef>& refs, bool& remove_object, std::string& message) {
  remove_object = false;
  if (refs.empty()) return false;
  switch (o.kind) {
    case ObjectKind::Curve: {
      std::set<int> idx;
      for (const SubObjectRef& r : refs) if (r.kind == SubObjectKind::Vertex) idx.insert(r.index);
      if (idx.empty()) { message = "Curves only have control points to delete"; return false; }
      const ON_NurbsCurve& nc = o.curve->raw();
      const int left = nc.CVCount() - static_cast<int>(idx.size());
      if (left < nc.Order()) {
        if (left < 2) { remove_object = true; return true; }
        message = "A degree " + std::to_string(nc.Degree()) + " curve needs at least " + std::to_string(nc.Order()) + " control points";
        return false;
      }
      std::vector<Point3d> cvs;
      for (int i = 0; i < nc.CVCount(); ++i) if (!idx.count(i)) cvs.push_back(o.curve->ControlPointAt(i));
      kernel::NurbsCurve k = kernel::NurbsCurve::FromControlPoints(cvs, nc.Degree());
      *o.curve = k;
      o.InvalidateDisplay();
      return true;
    }
    case ObjectKind::Surface: {
      message = "Surface control points cannot be deleted; use RemoveKnot or Rebuild";
      return false;
    }
    case ObjectKind::Brep: {
      std::set<int> faces;
      for (const SubObjectRef& r : refs) if (r.kind == SubObjectKind::Face) faces.insert(r.index);
      if (faces.empty()) { message = "Only brep faces can be deleted (edges and vertices cannot)"; return false; }
      ON_Brep& b = o.brep->raw();
      if (static_cast<int>(faces.size()) >= b.m_F.Count()) { remove_object = true; return true; }
      for (auto it = faces.rbegin(); it != faces.rend(); ++it) if (*it >= 0 && *it < b.m_F.Count()) b.DeleteFace(b.m_F[*it], true);
      b.Compact();
      o.InvalidateDisplay();
      return true;
    }
    case ObjectKind::Mesh: {
      ON_Mesh& m = o.mesh->raw();
      std::set<int> faces;
      std::set<int> verts;
      for (const SubObjectRef& r : refs) {
        if (r.kind == SubObjectKind::Face) faces.insert(r.index);
        else if (r.kind == SubObjectKind::Vertex) verts.insert(r.index);
        else { verts.insert(r.index); verts.insert(r.index2); }
      }
      for (int fi = 0; fi < m.m_F.Count(); ++fi) for (int v : FaceVertices(m.m_F[fi])) if (verts.count(v)) faces.insert(fi);
      if (faces.empty()) { message = "Nothing to delete"; return false; }
      if (static_cast<int>(faces.size()) >= m.m_F.Count()) { remove_object = true; return true; }
      for (auto it = faces.rbegin(); it != faces.rend(); ++it) if (*it >= 0 && *it < m.m_F.Count()) m.DeleteFace(*it);
      m.CullUnusedVertices();
      m.Compact();
      m.DestroyTopology();
      m.InvalidateBoundingBoxes();
      o.InvalidateDisplay();
      return true;
    }
    case ObjectKind::SubD: {
      kernel::Mesh net;
      if (!SubDNet(o, net)) { message = "Could not read the SubD control net"; return false; }
      std::set<int> faces, verts;
      for (const SubObjectRef& r : refs) {
        if (r.kind == SubObjectKind::Face) faces.insert(r.index);
        else if (r.kind == SubObjectKind::Vertex) verts.insert(r.index);
        else { verts.insert(r.index); verts.insert(r.index2); }
      }
      ON_Mesh& m = net.raw();
      for (int fi = 0; fi < m.m_F.Count(); ++fi) for (int v : FaceVertices(m.m_F[fi])) if (verts.count(v)) faces.insert(fi);
      if (faces.empty()) { message = "Nothing to delete"; return false; }
      if (static_cast<int>(faces.size()) >= m.m_F.Count()) { remove_object = true; return true; }
      for (auto it = faces.rbegin(); it != faces.rend(); ++it) if (*it >= 0 && *it < m.m_F.Count()) m.DeleteFace(*it);
      m.CullUnusedVertices();
      m.Compact();
      try {
        *o.subd = kernel::SubD::FromControlMesh(net);
      } catch (...) {
        message = "Could not rebuild the SubD without those faces";
        return false;
      }
      o.InvalidateDisplay();
      return true;
    }
    default:
      message = "Nothing to delete";
      return false;
  }
}

// ---------------------------------------------------------------------------
// Topology
// ---------------------------------------------------------------------------

std::vector<int> FacesOfEdge(const ON_Mesh& m, int a, int b) {
  std::vector<int> out;
  const EdgeKey key = Key(a, b);
  for (int fi = 0; fi < m.m_F.Count(); ++fi) {
    const std::vector<int> v = FaceVertices(m.m_F[fi]);
    for (size_t k = 0; k < v.size(); ++k) if (Key(v[k], v[(k + 1) % v.size()]) == key) { out.push_back(fi); break; }
  }
  return out;
}

namespace {

// Edge of face `fi` opposite to (a,b) in a quad; -1 for triangles.
bool OppositeEdge(const ON_Mesh& m, int fi, int a, int b, int& c, int& d) {
  const std::vector<int> v = FaceVertices(m.m_F[fi]);
  if (v.size() != 4) return false;
  for (size_t k = 0; k < 4; ++k) {
    if (Key(v[k], v[(k + 1) % 4]) == Key(a, b)) { c = v[(k + 2) % 4]; d = v[(k + 3) % 4]; return true; }
  }
  return false;
}

// Edges around vertex v, with their faces.
std::vector<EdgeKey> EdgesAtVertex(const std::map<EdgeKey, std::vector<int>>& ef, int v) {
  std::vector<EdgeKey> out;
  for (const auto& [k, faces] : ef) if (k.first == v || k.second == v) out.push_back(k);
  return out;
}

}  // namespace

std::vector<SubObjectRef> EdgeLoop(const SceneObject& o, const SubObjectRef& edge) {
  std::vector<SubObjectRef> out;
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m || edge.kind != SubObjectKind::Edge) return out;
  const auto ef = EdgeFaces(*m);
  std::set<EdgeKey> seen;
  auto add = [&](EdgeKey k) { if (seen.insert(k).second) out.push_back(SubObjectRef::MeshEdge(o.id, k.first, k.second)); };
  add(Key(edge.index, edge.index2));
  // Walk both ways: at a 4-valent interior vertex continue with the edge
  // sharing no face with the current one.
  for (int dir = 0; dir < 2; ++dir) {
    int prev = dir == 0 ? edge.index : edge.index2, cur = dir == 0 ? edge.index2 : edge.index;
    for (int guard = 0; guard < 100000; ++guard) {
      const std::vector<EdgeKey> around = EdgesAtVertex(ef, cur);
      if (around.size() != 4) break;
      auto cf = ef.find(Key(prev, cur));
      if (cf == ef.end() || cf->second.size() != 2) break;  // boundary or non-manifold: stop
      EdgeKey next{-1, -1};
      for (const EdgeKey& k : around) {
        if (k == Key(prev, cur)) continue;
        const std::vector<int>& faces = ef.at(k);
        bool shares = false;
        for (int f : faces) if (std::find(cf->second.begin(), cf->second.end(), f) != cf->second.end()) shares = true;
        if (!shares) { next = k; break; }
      }
      if (next.first < 0 || seen.count(next)) break;
      add(next);
      prev = cur;
      cur = next.first == cur ? next.second : next.first;
    }
  }
  return out;
}

std::vector<SubObjectRef> EdgeRing(const SceneObject& o, const SubObjectRef& edge) {
  std::vector<SubObjectRef> out;
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m || edge.kind != SubObjectKind::Edge) return out;
  const auto ef = EdgeFaces(*m);
  std::set<EdgeKey> seen;
  auto add = [&](EdgeKey k) { if (seen.insert(k).second) out.push_back(SubObjectRef::MeshEdge(o.id, k.first, k.second)); };
  add(Key(edge.index, edge.index2));
  auto it = ef.find(Key(edge.index, edge.index2));
  if (it == ef.end()) return out;
  for (int start_face : it->second) {
    int a = edge.index, b = edge.index2, fi = start_face;
    for (int guard = 0; guard < 100000; ++guard) {
      int c, d;
      if (!OppositeEdge(*m, fi, a, b, c, d)) break;
      const EdgeKey k = Key(c, d);
      if (seen.count(k)) break;
      add(k);
      auto nf = ef.find(k);
      if (nf == ef.end() || nf->second.size() != 2) break;
      fi = nf->second[0] == fi ? nf->second[1] : nf->second[0];
      a = c; b = d;
    }
  }
  return out;
}

std::vector<SubObjectRef> FaceLoop(const SceneObject& o, const SubObjectRef& edge) {
  std::vector<SubObjectRef> out;
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m || edge.kind != SubObjectKind::Edge) return out;
  const auto ef = EdgeFaces(*m);
  std::set<int> seen;
  auto it = ef.find(Key(edge.index, edge.index2));
  if (it == ef.end()) return out;
  for (int start_face : it->second) {
    int a = edge.index, b = edge.index2, fi = start_face;
    for (int guard = 0; guard < 100000; ++guard) {
      if (!seen.insert(fi).second) break;
      out.push_back(SubObjectRef::Face(o.id, fi));
      int c, d;
      if (!OppositeEdge(*m, fi, a, b, c, d)) break;
      auto nf = ef.find(Key(c, d));
      if (nf == ef.end() || nf->second.size() != 2) break;
      fi = nf->second[0] == fi ? nf->second[1] : nf->second[0];
      a = c; b = d;
    }
  }
  return out;
}

std::vector<SubObjectRef> FacesToBoundary(const SceneObject& o, const std::vector<int>& seeds,
                                          const std::vector<SubObjectRef>& boundary) {
  std::vector<SubObjectRef> out;
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m) return out;
  const auto ef = EdgeFaces(*m);
  std::set<EdgeKey> wall;
  for (const SubObjectRef& r : boundary) if (r.kind == SubObjectKind::Edge && r.id == o.id) wall.insert(Key(r.index, r.index2));
  std::set<int> seen;
  std::vector<int> queue(seeds.begin(), seeds.end());
  while (!queue.empty()) {
    const int fi = queue.back();
    queue.pop_back();
    if (fi < 0 || fi >= m->m_F.Count() || !seen.insert(fi).second) continue;
    out.push_back(SubObjectRef::Face(o.id, fi));
    const std::vector<int> v = FaceVertices(m->m_F[fi]);
    for (size_t k = 0; k < v.size(); ++k) {
      const EdgeKey key = Key(v[k], v[(k + 1) % v.size()]);
      if (wall.count(key)) continue;
      for (int nf : ef.at(key)) if (!seen.count(nf)) queue.push_back(nf);
    }
  }
  return out;
}

std::vector<SubObjectRef> ConnectedFaces(const SceneObject& o, int seed) {
  return FacesToBoundary(o, {seed}, {});
}

std::vector<SubObjectRef> AllEdges(const SceneObject& o) {
  std::vector<SubObjectRef> out;
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    for (int ei = 0; ei < b.m_E.Count(); ++ei) if (b.m_E[ei].m_edge_index >= 0) out.push_back(SubObjectRef::BrepEdge(o.id, ei));
    return out;
  }
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m) return out;
  for (const auto& [k, faces] : EdgeFaces(*m)) out.push_back(SubObjectRef::MeshEdge(o.id, k.first, k.second));
  return out;
}

std::vector<SubObjectRef> NakedEdges(const SceneObject& o) {
  std::vector<SubObjectRef> out;
  if (o.kind == ObjectKind::Brep && o.brep) {
    const ON_Brep& b = o.brep->raw();
    for (int ei = 0; ei < b.m_E.Count(); ++ei) if (b.m_E[ei].m_edge_index >= 0 && b.m_E[ei].TrimCount() == 1) out.push_back(SubObjectRef::BrepEdge(o.id, ei));
    return out;
  }
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m) return out;
  for (const auto& [k, faces] : EdgeFaces(*m)) if (faces.size() == 1) out.push_back(SubObjectRef::MeshEdge(o.id, k.first, k.second));
  return out;
}

std::vector<SubObjectRef> NonManifoldEdges(const SceneObject& o) {
  std::vector<SubObjectRef> out;
  kernel::Mesh scratch;
  const ON_Mesh* m = TopologyMesh(o, scratch);
  if (!m) return out;
  for (const auto& [k, faces] : EdgeFaces(*m)) if (faces.size() > 2) out.push_back(SubObjectRef::MeshEdge(o.id, k.first, k.second));
  return out;
}

std::vector<SubObjectRef> NakedEdgeVertices(const SceneObject& o) {
  std::vector<SubObjectRef> out;
  std::set<int> verts;
  for (const SubObjectRef& e : NakedEdges(o)) {
    if (e.index2 < 0) continue;
    verts.insert(e.index);
    verts.insert(e.index2);
  }
  for (int v : verts) out.push_back(SubObjectRef::Vertex(o.id, v));
  return out;
}

std::vector<SubObjectRef> SubDCreaseEdges(const SceneObject& o) {
  std::vector<SubObjectRef> out;
  kernel::Mesh net;
  if (!SubDNet(o, net)) return out;
  const ON_Mesh& m = net.raw();
  auto index_of = [&](Point3d p) {
    for (int i = 0; i < m.VertexCount(); ++i) if (MeshVertex(m, i).DistanceTo(p) < 1e-6) return i;
    return -1;
  };
  ON_SubDEdgeIterator eit = o.subd->raw().EdgeIterator();
  for (const ON_SubDEdge* e = eit.FirstEdge(); e; e = eit.NextEdge()) {
    if (!e->IsCrease() || !e->Vertex(0) || !e->Vertex(1)) continue;
    const int a = index_of(e->Vertex(0)->ControlNetPoint()), b = index_of(e->Vertex(1)->ControlNetPoint());
    if (a >= 0 && b >= 0) out.push_back(SubObjectRef::MeshEdge(o.id, a, b));
  }
  return out;
}

}  // namespace dino8::app
