// Independent-oracle checks of the tolerance-driven closed-mesh tessellators on a cylinder-cylinder (Steinmetz)
// BooleanCombineMixed result - the curved-seam class whose ORDINARY fixed-division tessellation is a disclosed open mesh
// (TestBooleanCombineMixedSteinmetzIntersectionConformingIsWatertight pins that) and which
// TessellateToClosedMeshAdaptive / TessellateToClosedMeshCertifiedAdaptive close by re-meshing conformingly.
//
// Before the seam chains were tolerance-driven, the shared half-ellipse chains (200 samples each) fixed the whole mesh:
// the result was bit-identical (798 vertices, 1592 faces) at every tolerance from 0.1 down to 1e-4, i.e. a 0.1 request
// was over-resolved ~400-fold. Now the conforming fallback keeps a uniform stride of the chain whose chords stay within
// half the requested tolerance of both cylinder walls, so the vertex count falls as the tolerance loosens while the
// measured deviation stays within it.
//
// The oracles are independent of the tessellators. The true surfaces are the two infinite cylinders (axis z, radius r;
// axis x, radius r - the axes cross at 90 degrees through the origin) and the four planar end discs; the volume is the
// closed form; the mesh is read straight from the ON_Mesh arrays and NOTHING is skipped:
//   (1) the mesh is a closed manifold;
//   (2) every vertex is a true surface point;
//   (3) every triangle edge MIDPOINT and every triangle CENTROID is within the tolerance of the nearest true surface;
//   (4) the volume is within the closed form by at most (surface area x tolerance).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineMixed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

constexpr double kRadius = 2.0;
constexpr double kLength = 10.0;

// Two equal-radius full-sweep cylinders, A along z, B along x, both centred on the origin (the Steinmetz fixture of
// test_basic.cpp's BuildSteinmetzCylinders at alpha = 90 degrees).
void BuildPair(Brep::CylindricalFace& a, Brep::CylindricalFace& b) {
  a.frame.origin = Point3d(0, 0, -0.5 * kLength);
  a.frame.xaxis = Vector3d(1, 0, 0);
  a.frame.yaxis = Vector3d(0, 1, 0);
  a.frame.zaxis = Vector3d(0, 0, 1);
  a.frame.UpdateEquation();
  a.radius = kRadius;
  a.angle = 2.0 * ON_PI;
  a.length = kLength;
  b.frame.origin = Point3d(-0.5 * kLength, 0, 0);
  b.frame.xaxis = Vector3d(0, 1, 0);
  b.frame.yaxis = Vector3d(0, 0, 1);
  b.frame.zaxis = Vector3d(1, 0, 0);
  b.frame.UpdateEquation();
  b.radius = kRadius;
  b.angle = 2.0 * ON_PI;
  b.length = kLength;
}

Brep Build(BooleanOp op) {
  Brep::CylindricalFace ca, cb;
  BuildPair(ca, cb);
  const Brep a = Brep::FromMixedFaces({}, {ca});
  const Brep b = Brep::FromMixedFaces({}, {cb});
  return BooleanCombineMixed(a, b, op);
}

double RhoA(const ON_3dPoint& p) { return std::hypot(p.x, p.y); }  // distance from A's axis (z)
double RhoB(const ON_3dPoint& p) { return std::hypot(p.y, p.z); }  // distance from B's axis (x)

// Distance from `p` to the nearest of: cylinder A, cylinder B, and the four planar end discs (z = +-L/2 inside A's
// circle, x = +-L/2 inside B's circle).
double SurfaceDistance(const ON_3dPoint& p) {
  const double half = 0.5 * kLength;
  double d = std::min(std::fabs(RhoA(p) - kRadius), std::fabs(RhoB(p) - kRadius));
  if (RhoA(p) <= kRadius + 1e-5) d = std::min(d, std::min(std::fabs(p.z - half), std::fabs(p.z + half)));
  if (RhoB(p) <= kRadius + 1e-5) d = std::min(d, std::min(std::fabs(p.x - half), std::fabs(p.x + half)));
  return d;
}

struct Measured {
  double worst_vertex = 0.0;    // max over vertices of the distance to the nearest true surface
  double worst_midpoint = 0.0;  // max over every triangle edge's midpoint
  double worst_centroid = 0.0;  // max over every triangle's centroid
};

Measured Measure(const Mesh& m) {
  Measured out;
  const ON_Mesh& raw = m.raw();
  for (int i = 0; i < raw.m_V.Count(); ++i) out.worst_vertex = std::max(out.worst_vertex, SurfaceDistance(ON_3dPoint(raw.m_V[i])));
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const int corners = f.IsQuad() ? 4 : 3;
    ON_3dPoint centroid(0, 0, 0);
    for (int k = 0; k < corners; ++k) {
      const ON_3dPoint a(raw.m_V[f.vi[k]]);
      const ON_3dPoint b(raw.m_V[f.vi[(k + 1) % corners]]);
      out.worst_midpoint = std::max(out.worst_midpoint, SurfaceDistance(0.5 * (a + b)));
      centroid += a;
    }
    out.worst_centroid = std::max(out.worst_centroid, SurfaceDistance(centroid / static_cast<double>(corners)));
  }
  return out;
}

// Asserts the four oracle properties for one mesh and returns its vertex count.
int CheckOne(const char* tag, const Mesh& m, double tol, double exact_volume, double surface_area) {
  const Measured q = Measure(m);
  const double err = m.Volume() - exact_volume;
  char what[512];
  std::snprintf(what, sizeof what, "%s: closed manifold (%d vertices, %d faces)", tag, m.VertexCount(), m.FaceCount());
  d8test::Check(m.IsClosedManifold(), what);
  std::snprintf(what, sizeof what, "%s: every vertex is a true surface point (worst %.2e <= 1e-5)", tag, q.worst_vertex);
  d8test::Check(q.worst_vertex <= 1e-5, what);
  std::snprintf(what, sizeof what,
                "%s: every edge midpoint (worst %.2e) and every triangle centroid (worst %.2e) is within the tolerance %g of "
                "the nearest true surface",
                tag, q.worst_midpoint, q.worst_centroid, tol);
  d8test::Check(q.worst_midpoint <= tol && q.worst_centroid <= tol, what);
  std::snprintf(what, sizeof what,
                "%s: volume error %+.3e is within surface area x tolerance (%.3e)", tag, err, surface_area * tol);
  d8test::Check(std::fabs(err) <= surface_area * tol, what);
  return m.VertexCount();
}

}  // namespace

// The Intersection (the bicylinder, Steinmetz solid): the certified tessellator at 0.1, 0.03, 0.01 and 0.001.
D8_EXTRA_TEST(SteinmetzIntersectionCertifiedClosedMeshDeviationTracksTolerance) {
  const Brep result = Build(BooleanOp::Intersection);
  const double exact = 16.0 * kRadius * kRadius * kRadius / 3.0;
  const double area = 16.0 * kRadius * kRadius;  // the bicylinder's surface area
  std::vector<int> counts;
  for (const double tol : {0.1, 0.03, 0.01, 0.001}) {
    char tag[160];
    std::snprintf(tag, sizeof tag, "Steinmetz Intersection TessellateToClosedMeshCertifiedAdaptive(%g)", tol);
    counts.push_back(CheckOne(tag, result.TessellateToClosedMeshCertifiedAdaptive(tol), tol, exact, area));
  }
  d8test::Check(counts[0] <= counts[1] && counts[1] <= counts[2] && counts[2] <= counts[3] && counts[0] * 4 <= counts[3],
                "Steinmetz Intersection: the certified closed mesh gets denser as the tolerance tightens (vertex count "
                "non-decreasing over 0.1, 0.03, 0.01, 0.001 and at least 4x between the ends) - the seam chain is "
                "tolerance-driven, no longer a fixed 798-vertex mesh");
}

// The plain heuristic adaptive tessellator takes the same tolerance-driven seam.
D8_EXTRA_TEST(SteinmetzIntersectionAdaptiveClosedMeshDeviationTracksTolerance) {
  const Brep result = Build(BooleanOp::Intersection);
  const double exact = 16.0 * kRadius * kRadius * kRadius / 3.0;
  const double area = 16.0 * kRadius * kRadius;
  int coarse = 0, fine = 0;
  for (const double tol : {0.1, 0.01}) {
    char tag[160];
    std::snprintf(tag, sizeof tag, "Steinmetz Intersection TessellateToClosedMeshAdaptive(%g)", tol);
    (tol > 0.05 ? coarse : fine) = CheckOne(tag, result.TessellateToClosedMeshAdaptive(tol), tol, exact, area);
  }
  d8test::Check(coarse < fine, "Steinmetz Intersection: TessellateToClosedMeshAdaptive(0.1) has fewer vertices than (0.01)");
}

// A seam shared with end discs and a longer wall: A - B (A's full cylinder minus the lens it shares with B).
D8_EXTRA_TEST(SteinmetzDifferenceCertifiedClosedMeshDeviationTracksTolerance) {
  const Brep result = Build(BooleanOp::Difference);
  const double exact = ON_PI * kRadius * kRadius * kLength - 16.0 * kRadius * kRadius * kRadius / 3.0;
  // A's wall + both end discs + the bicylinder surface: an upper bound on the surface the tolerance can displace.
  const double area = 2.0 * ON_PI * kRadius * kLength + 2.0 * ON_PI * kRadius * kRadius + 16.0 * kRadius * kRadius;
  int coarse = 0, fine = 0;
  for (const double tol : {0.1, 0.01}) {
    char tag[160];
    std::snprintf(tag, sizeof tag, "Steinmetz A - B TessellateToClosedMeshCertifiedAdaptive(%g)", tol);
    (tol > 0.05 ? coarse : fine) = CheckOne(tag, result.TessellateToClosedMeshCertifiedAdaptive(tol), tol, exact, area);
  }
  d8test::Check(coarse <= fine, "Steinmetz A - B: the certified closed mesh is not coarser at 0.01 than at 0.1");
}
