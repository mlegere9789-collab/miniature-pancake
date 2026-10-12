#include <cmath>
#include <cstdio>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"

// Disposable probe used by the 2026-10-12 `kernel: Local / direct-edit
// operations` round to re-verify Brep::SplitNakedEdgeAt()'s own "linear
// edges only" restriction against the EXACT fixture that originally
// motivated it (see TestBrepSplitNakedEdgeAtSplitsCurvedNakedRim's own
// comment in tests/test_basic.cpp for the full writeup) - swept across
// several split fractions to confirm the original defect no longer
// reproduces before lifting the restriction for real. Kept here, not
// deleted, as a working template for any future session re-probing this
// method on a fresh curved fixture.
int main() {
  ON::Begin();
  using namespace dino8::kernel;

  const ON_Plane frame = []() {
    ON_Plane f;
    f.origin = ON_3dPoint(0, 0, 0);
    f.xaxis = ON_3dVector(0, -1, 0);
    f.yaxis = ON_3dVector(1, 0, 0);
    f.zaxis = ON_3dVector(0, 0, 1);
    f.UpdateEquation();
    return f;
  }();

  Brep::CylindricalFace cf;
  cf.frame = frame;
  cf.radius = 1.0;
  cf.angle = ON_PI;  // the half-cylinder wedge TestBrepSplitNakedEdgeAtSplitsCurvedNakedRim also builds
  cf.length = 2.0;
  Brep wedge = Brep::FromMixedFaces({}, {cf});
  std::printf("faces=%d edges=%d\n", wedge.FaceCount(), wedge.raw().m_E.Count());

  int curved_edge = -1;
  for (int i = 0; i < wedge.raw().m_E.Count(); ++i) {
    const ON_BrepEdge& e = wedge.raw().m_E[i];
    if (e.TrimCount() == 1 && !e.IsClosed() && !e.IsLinear(1e-6)) { curved_edge = i; break; }
  }
  std::printf("curved naked edge index: %d\n", curved_edge);

  for (double frac : {0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 0.9}) {
    Brep w = Brep::FromMixedFaces({}, {cf});
    const ON_BrepEdge& ee = w.raw().m_E[curved_edge];
    const Point3d sp = ee.PointAt(ee.Domain().ParameterAt(frac));
    const Result rr = w.SplitNakedEdgeAt(curved_edge, sp);
    const auto ar = w.Check();
    std::printf("frac=%.2f result=%s issues=%zu LoopGap=%d InvalidTrim=%d\n", frac,
                rr == Result::Ok ? "Ok" : "Failed", ar.issues.size(),
                ar.Count(Brep::CheckIssue::Kind::LoopGap), ar.Count(Brep::CheckIssue::Kind::InvalidTrim));
  }

  // --- SewTJunctions curved-edge debug probe ---
  {
    Brep::CylindricalFace cf2;
    cf2.frame = frame;
    cf2.radius = 1.0;
    cf2.angle = ON_PI;
    cf2.length = 2.0;
    Brep probe = Brep::FromMixedFaces({}, {cf2});
    int ce = -1;
    for (int i = 0; i < probe.raw().m_E.Count(); ++i) {
      const ON_BrepEdge& e = probe.raw().m_E[i];
      if (e.TrimCount() == 1 && !e.IsClosed() && !e.IsLinear(1e-6)) { ce = i; break; }
    }
    const Point3d p = probe.raw().m_E[ce].PointAt(probe.raw().m_E[ce].Domain().ParameterAt(0.4));
    std::printf("\n[sewT debug] p=(%.6f,%.6f,%.6f)\n", p.x, p.y, p.z);

    Brep::PlanarFace b;
    b.loop = {p, p + Point3d(0.3, 0, 1.0), p + Point3d(-0.3, 0, 1.0)};
    b.plane = ON_Plane(b.loop[0], ON_3dVector(0, 1, 0));
    // notch_begin/notch_count left at their defaults (0, 0) - "no notch",
    // matching CheckHealFace() exactly (it never sets them either).

    Brep fixture = Brep::FromMixedFaces({b}, {cf2});
    std::printf("[sewT debug] faces=%d edges=%d verts=%d\n", fixture.FaceCount(), fixture.raw().m_E.Count(),
                fixture.raw().m_V.Count());
    for (int i = 0; i < fixture.raw().m_E.Count(); ++i) {
      const ON_BrepEdge& e = fixture.raw().m_E[i];
      std::printf("  edge %d: trimcount=%d closed=%d linear=%d start=(%.4f,%.4f,%.4f) end=(%.4f,%.4f,%.4f)\n", i,
                  e.TrimCount(), e.IsClosed() ? 1 : 0, e.IsLinear(1e-6) ? 1 : 0, e.PointAtStart().x,
                  e.PointAtStart().y, e.PointAtStart().z, e.PointAtEnd().x, e.PointAtEnd().y, e.PointAtEnd().z);
    }

    std::printf("[sewT debug] pre-split IsValid=%d\n", fixture.raw().IsValid() ? 1 : 0);

    // Isolate further: does plain Compact() alone (NO split at all)
    // already corrupt this exact 2-face fixture?
    {
      Brep f2 = fixture;
      f2.raw().Compact();
      std::printf("[sewT debug] Compact()-only (no split) IsValid=%d\n", f2.raw().IsValid() ? 1 : 0);
    }

    // Control: does splitting a STRAIGHT naked edge (edge 1, one of the
    // wedge's own linear sides) on this SAME 2-face fixture also corrupt
    // it, or is this specific to the curved edge / new code path?
    {
      Brep f3 = fixture;
      const Point3d mid1 = f3.raw().m_E[1].PointAt(f3.raw().m_E[1].Domain().Mid());
      const Result rs = f3.SplitNakedEdgeAt(1, mid1, 1e-6);
      std::printf("[sewT debug] control: splitting STRAIGHT edge 1 on the 2-face fixture result=%s IsValid=%d\n",
                  rs == Result::Ok ? "Ok" : "Failed", f3.raw().IsValid() ? 1 : 0);
    }

    const Result rsplit = fixture.SplitNakedEdgeAt(0, p, 1e-6);
    std::printf("[sewT debug] direct SplitNakedEdgeAt result=%s, post-split-only IsValid=%d\n",
                rsplit == Result::Ok ? "Ok" : "Failed", fixture.raw().IsValid() ? 1 : 0);
    const int splits = 0;
    std::printf("[sewT debug] (skipping SewTJunctions() itself for this probe)\n");
    ON_wString log_str;
    ON_TextLog log(log_str);
    const bool valid = fixture.raw().IsValid(&log);
    std::printf("[sewT debug] post IsValid=%d\n", valid ? 1 : 0);
    if (!valid) {
      ON_String s(log_str);
      std::printf("IsValid log:\n%s\n", s.Array());
    }
    for (int i = 0; i < fixture.raw().m_E.Count(); ++i) {
      const ON_BrepEdge& e = fixture.raw().m_E[i];
      if (e.m_edge_index < 0) continue;
      std::printf("  edge %d: trimcount=%d closed=%d linear=%d start=(%.4f,%.4f,%.4f) end=(%.4f,%.4f,%.4f)\n", i,
                  e.TrimCount(), e.IsClosed() ? 1 : 0, e.IsLinear(1e-6) ? 1 : 0, e.PointAtStart().x,
                  e.PointAtStart().y, e.PointAtStart().z, e.PointAtEnd().x, e.PointAtEnd().y, e.PointAtEnd().z);
    }
    std::printf("[sewT debug] faces=%d\n", fixture.raw().m_F.Count());
    for (int fi = 0; fi < fixture.raw().m_F.Count(); ++fi) {
      const ON_BrepFace& f = fixture.raw().m_F[fi];
      std::printf("  face %d: index=%d loops=[", fi, f.m_face_index);
      for (int li = 0; li < f.m_li.Count(); ++li) std::printf("%d ", f.m_li[li]);
      std::printf("]\n");
    }
    std::printf("[sewT debug] loops=%d\n", fixture.raw().m_L.Count());
    for (int li = 0; li < fixture.raw().m_L.Count(); ++li) {
      const ON_BrepLoop& lp = fixture.raw().m_L[li];
      std::printf("  loop %d: index=%d type=%d face=%d trims=[", li, lp.m_loop_index, (int)lp.m_type, lp.m_fi);
      for (int ti = 0; ti < lp.m_ti.Count(); ++ti) std::printf("%d ", lp.m_ti[ti]);
      std::printf("]\n");
    }
    std::printf("[sewT debug] trims=%d\n", fixture.raw().m_T.Count());
    for (int ti = 0; ti < fixture.raw().m_T.Count(); ++ti) {
      const ON_BrepTrim& t = fixture.raw().m_T[ti];
      std::printf("  trim %d: index=%d edge=%d loop=%d bRev3d=%d type=%d\n", ti, t.m_trim_index, t.m_ei, t.m_li,
                  t.m_bRev3d ? 1 : 0, (int)t.m_type);
    }
  }

  // --- Minimal repro attempt: TWO disjoint planar quads, no cylinder at
  // all, splitting a naked edge on one of them. Isolates whether the
  // bug found above needs a CylindricalFace specifically, or just "any
  // second, unrelated face in the same Brep".
  {
    Brep::PlanarFace qa;
    qa.loop = {Point3d(0, 0, 0), Point3d(1, 0, 0), Point3d(1, 1, 0), Point3d(0, 1, 0)};
    qa.plane = ON_Plane(qa.loop[0], ON_3dVector(0, 0, 1));
    Brep::PlanarFace qb;
    qb.loop = {Point3d(10, 0, 0), Point3d(11, 0, 0), Point3d(11, 1, 0), Point3d(10, 1, 0)};
    qb.plane = ON_Plane(qb.loop[0], ON_3dVector(0, 0, 1));
    Brep two = Brep::FromMixedFaces({qa, qb}, {});
    std::printf("\n[minimal repro] faces=%d edges=%d valid=%d\n", two.FaceCount(), two.raw().m_E.Count(),
                two.raw().IsValid() ? 1 : 0);
    const Point3d mid = two.raw().m_E[0].PointAt(two.raw().m_E[0].Domain().Mid());
    const Result r = two.SplitNakedEdgeAt(0, mid, 1e-6);
    std::printf("[minimal repro] split result=%s post-IsValid=%d\n", r == Result::Ok ? "Ok" : "Failed",
                two.raw().IsValid() ? 1 : 0);
  }

  ON::End();
  return 0;
}
