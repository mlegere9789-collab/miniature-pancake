#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  Brep brep;
  ON_Brep& b = brep.raw();

  const std::vector<Point3d> grid = {Point3d(0, 0, 0), Point3d(0, 1, 0), Point3d(1, 0, 0), Point3d(1, 1, 0)};
  const NurbsSurface surface = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  const int surface_index = b.AddSurface(new ON_NurbsSurface(surface.raw()));
  const int face_index = b.NewFace(surface_index).m_face_index;
  const int loop_index = b.NewLoop(ON_BrepLoop::outer, b.m_F[face_index]).m_loop_index;

  const int v00 = b.NewVertex(Point3d(0, 0, 0), 0.0).m_vertex_index;
  const int v10 = b.NewVertex(Point3d(1, 0, 0), 0.0).m_vertex_index;
  const int v11 = b.NewVertex(Point3d(1, 1, 0), 0.0).m_vertex_index;
  const int v01 = b.NewVertex(Point3d(0, 1, 0), 0.0).m_vertex_index;

  auto add_straight_side = [&](int va, int vb, Point2d uv_a, Point2d uv_b) {
    const int c3i = b.AddEdgeCurve(new ON_LineCurve(b.m_V[va].point, b.m_V[vb].point));
    const int edge_index = b.NewEdge(b.m_V[va], b.m_V[vb], c3i).m_edge_index;
    b.m_E[edge_index].m_tolerance = 0.0;
    const int c2i = b.AddTrimCurve(new ON_LineCurve(uv_a, uv_b));
    ON_BrepTrim& trim = b.NewTrim(b.m_E[edge_index], false, b.m_L[loop_index], c2i);
    trim.m_tolerance[0] = trim.m_tolerance[1] = 0.0;
    return edge_index;
  };

  int wavy_edge_index;
  std::vector<Point3d> ctrl;
  const int N = 10;
  for (int i = 0; i < N; ++i) {
    const double u = static_cast<double>(i) / (N - 1);
    const double v = (i == 0 || i == N - 1) ? 0.0 : 0.001 * std::sin(i * 2.5);
    ctrl.push_back(Point3d(u, v, 0));
  }
  {
    const NurbsCurve wavy = NurbsCurve::FromControlPoints(ctrl, 3);
    ON_NurbsCurve raw2d = wavy.raw();
    raw2d.ChangeDimension(2);
    const int c3i = b.AddEdgeCurve(new ON_LineCurve(Point3d(0, 0, 0), Point3d(1, 0, 0)));
    wavy_edge_index = b.NewEdge(b.m_V[v00], b.m_V[v10], c3i).m_edge_index;
    b.m_E[wavy_edge_index].m_tolerance = 0.0;
    const int c2i = b.AddTrimCurve(new ON_NurbsCurve(raw2d));
    ON_BrepTrim& trim = b.NewTrim(b.m_E[wavy_edge_index], false, b.m_L[loop_index], c2i);
    trim.m_tolerance[0] = trim.m_tolerance[1] = 0.0;
  }
  add_straight_side(v10, v11, Point2d(1, 0), Point2d(1, 1));
  add_straight_side(v11, v01, Point2d(1, 1), Point2d(0, 1));
  add_straight_side(v01, v00, Point2d(0, 1), Point2d(0, 0));

  b.SetTrimIsoFlags();
  b.SetTolerancesBoxesAndFlags();

  const int wavy_trim_index = b.m_E[wavy_edge_index].m_ti[0];

  // Print the pre-refit curve's own waviness directly, hand-checkable
  // against the ctrl points fed in above.
  std::printf("Original control points (u, v):\n");
  for (const Point3d& c : ctrl) std::printf("  (%.6f, %.6f)\n", c.x, c.y);

  for (double tol : {0.1, 0.01, 0.005, 0.002, 0.0005}) {
    // Rebuild a fresh fixture each time since RefitTrim mutates in place.
    Brep b2 = brep;  // deep-ish copy via ON_Brep's own copy semantics
    ON_Brep& bb = b2.raw();
    const int ti = bb.m_E[wavy_edge_index].m_ti[0];
    ON_NurbsCurve before;
    bb.m_T[ti].GetNurbForm(before);
    const Result r = b2.RefitTrim(ti, tol);
    ON_NurbsCurve after;
    bb.m_T[ti].GetNurbForm(after);
    double worst = 0;
    for (int i = 0; i <= 500; ++i) {
      const double t = bb.m_T[ti].Domain().ParameterAt(static_cast<double>(i) / 500);
      const ON_3dPoint uv = bb.m_T[ti].PointAt(t);
      worst = std::max(worst, std::fabs(uv.y));
    }
    std::printf("tol=%g: result=%d before_cv=%d after_cv=%d worst_deviation_from_straight=%g\n", tol, (int)r,
                before.CVCount(), after.CVCount(), worst);
  }

  // Endpoint preservation check - the refit curve must still start/end
  // exactly at (0,0) and (1,0), or the loop would no longer close.
  {
    Brep b3 = brep;
    ON_Brep& bb = b3.raw();
    const int ti = bb.m_E[wavy_edge_index].m_ti[0];
    b3.RefitTrim(ti, 0.01);
    const ON_3dPoint p0 = bb.m_T[ti].PointAtStart();
    const ON_3dPoint p1 = bb.m_T[ti].PointAtEnd();
    std::printf("endpoints after refit: start=(%.9f,%.9f) end=(%.9f,%.9f)\n", p0.x, p0.y, p1.x, p1.y);
  }

  try {
    brep.RefitTrim(-1, 0.01);
    std::printf("ERROR: expected throw for negative trim_index\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK threw for negative trim_index: %s\n", e.what());
  }

  ON::End();
  return 0;
}
