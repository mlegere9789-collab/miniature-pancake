#include "geom/BrepTrimFace.h"

namespace dino8::app {

int AddTrimmedFace(ON_Brep& brep, ON_NurbsSurface* srf, const std::vector<std::vector<LoopSeg>>& loops, double tol) {
  const int si = brep.AddSurface(srf);
  ON_BrepFace& face = brep.NewFace(si);
  const int fi = face.m_face_index;
  const int first_vertex = brep.m_V.Count();
  const int first_edge = brep.m_E.Count();
  auto find_or_add_vertex = [&](ON_3dPoint p) {
    for (int vi = first_vertex; vi < brep.m_V.Count(); ++vi) if (brep.m_V[vi].point.DistanceTo(p) <= tol) return vi;
    ON_BrepVertex& v = brep.NewVertex(p, 0.0);
    return v.m_vertex_index;
  };
  for (size_t k = 0; k < loops.size(); ++k) {
    const std::vector<LoopSeg>& segs = loops[k];
    if (segs.empty()) continue;
    ON_BrepLoop& loop = brep.NewLoop(k == 0 ? ON_BrepLoop::outer : ON_BrepLoop::inner, brep.m_F[fi]);
    const int li = loop.m_loop_index;
    for (size_t i = 0; i < segs.size(); ++i) {
      const LoopSeg& seg = segs[i];
      ON_NurbsCurve* c2 = new ON_NurbsCurve(seg.c2);
      c2->ChangeDimension(2);
      const int c2i = brep.AddTrimCurve(c2);
      const ON_3dPoint p0 = seg.c3.PointAtStart(), p1 = seg.c3.PointAtEnd();
      const ON_3dPoint pm = seg.c3.PointAt(seg.c3.Domain().Mid());
      const bool degenerate = p0.DistanceTo(p1) <= tol && pm.DistanceTo(p0) <= tol;
      const int v0 = find_or_add_vertex(p0);
      if (degenerate) {
        ON_BrepTrim& t = brep.NewSingularTrim(brep.m_V[v0], brep.m_L[li], ON_Surface::not_iso, c2i);
        t.m_tolerance[0] = t.m_tolerance[1] = 0.0;
        continue;
      }
      const int v1 = find_or_add_vertex(p1);
      // Seam / duplicate edge inside this face?
      int reuse = -1;
      bool rev = false;
      for (int ei = first_edge; ei < brep.m_E.Count() && reuse < 0; ++ei) {
        const ON_BrepEdge& e = brep.m_E[ei];
        if (e.m_ti.Count() != 1) continue;
        const ON_3dPoint em = e.PointAt(e.Domain().Mid());
        if (em.DistanceTo(pm) > tol) continue;
        if (e.m_vi[0] == v1 && e.m_vi[1] == v0) { reuse = ei; rev = true; }
        else if (e.m_vi[0] == v0 && e.m_vi[1] == v1 && v0 != v1) { reuse = ei; rev = false; }
        else if (v0 == v1 && e.m_vi[0] == v0 && e.m_vi[1] == v0) {
          reuse = ei;
          rev = ON_DotProduct(e.TangentAt(e.Domain().Min()), seg.c3.TangentAt(seg.c3.Domain().Min())) < 0;
        }
      }
      ON_BrepTrim* trim = nullptr;
      if (reuse >= 0) {
        trim = &brep.NewTrim(brep.m_E[reuse], rev, brep.m_L[li], c2i);
      } else {
        const int c3i = brep.AddEdgeCurve(new ON_NurbsCurve(seg.c3));
        ON_BrepEdge& e = brep.NewEdge(brep.m_V[v0], brep.m_V[v1], c3i);
        e.m_tolerance = tol;
        trim = &brep.NewTrim(e, false, brep.m_L[li], c2i);
      }
      trim->m_tolerance[0] = trim->m_tolerance[1] = 0.0;
      trim->m_type = ON_BrepTrim::boundary;
    }
  }
  return fi;
}

void FinishBrepTrims(ON_Brep& b) {
  for (int ei = 0; ei < b.m_E.Count(); ++ei) {
    ON_BrepEdge& e = b.m_E[ei];
    if (e.m_edge_index < 0) continue;
    for (int k = 0; k < e.m_ti.Count(); ++k) {
      ON_BrepTrim& t = b.m_T[e.m_ti[k]];
      if (e.m_ti.Count() == 1) t.m_type = ON_BrepTrim::boundary;
      else {
        bool same_face = false;
        for (int j = 0; j < e.m_ti.Count(); ++j) if (j != k && b.m_T[e.m_ti[j]].FaceIndexOf() == t.FaceIndexOf()) same_face = true;
        t.m_type = same_face ? ON_BrepTrim::seam : ON_BrepTrim::mated;
      }
    }
  }
  b.SetTrimIsoFlags();
  b.SetTolerancesBoxesAndFlags();
}

}  // namespace dino8::app
