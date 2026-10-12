#include <cstdio>
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  Brep box = Brep::BoxWelded(0, 0, 0, 1, 1, 1);
  std::printf("edge count = %d\n", box.raw().m_E.Count());

  try {
    box.RefitTrim(box.raw().m_E.Count() + 50, 0.01);
    std::printf("ERROR: expected throw\n");
  } catch (const std::out_of_range& e) {
    std::printf("OK out_of_range: %s\n", e.what());
  }

  box.raw().m_E[0].m_edge_index = -1;
  try {
    box.RefitTrim(0, 0.01);
    std::printf("ERROR: expected throw\n");
  } catch (const std::invalid_argument& e) {
    std::printf("OK invalid_argument: %s\n", e.what());
  }

  // Positive control for ReplaceEdge(): slide one endpoint of a fresh
  // box's own edge to a different point ON THE SAME bordering faces.
  Brep box2 = Brep::BoxWelded(0, 0, 0, 2, 2, 2);
  const ON_BrepEdge& e0 = box2.raw().m_E[0];
  const int v0 = e0.m_vi[0], v1 = e0.m_vi[1];
  const Point3d p0 = box2.raw().m_V[v0].point;
  const Point3d p1 = box2.raw().m_V[v1].point;
  std::printf("v1 valence = %d\n", box2.raw().m_V[v1].m_ei.Count());

  // A box corner has 3 mutually non-parallel bordering faces - any move
  // off the exact original point should fail.
  const Point3d p1_moved = p1 + ON_3dVector(0.3, 0, 0);
  const NurbsCurve moved_curve = NurbsCurve::FromControlPoints({p0, p1_moved}, 1);
  const Result r = box2.ReplaceEdge(0, moved_curve, 0.01);
  std::printf("ReplaceEdge on box corner (expect Failed=1): %d\n", r == Result::Failed);
  std::printf("v0 unchanged: %d, v1 unchanged: %d\n", box2.raw().m_V[v0].point.DistanceTo(p0) < 1e-9,
              box2.raw().m_V[v1].point.DistanceTo(p1) < 1e-9);

  ON::End();
  return 0;
}
