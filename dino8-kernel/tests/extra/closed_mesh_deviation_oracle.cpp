// Independent-oracle checks of the watertight, tolerance-driven closed-mesh tessellators on a CAP-LESS/CAPPED
// cylinder-cylinder (Steinmetz) BooleanCombineMixed result - the curved-seam class whose ORDINARY fixed-division
// tessellation is a disclosed open mesh (TestBooleanCombineMixedSteinmetzIntersectionConformingIsWatertight pins that),
// and which TessellateToClosedMeshAdaptive / TessellateToClosedMeshCertifiedAdaptive close by re-meshing conformingly.
// Until now only box-minus-cylinder (TestBrepAdaptiveClosedMeshesCloseBoxMinusCylinder) had the chord-tolerance oracle;
// the cylinder/cylinder seams (half-ellipses shared by two curved walls, not a planar cap against a wall) had none.
//
// Oracles are independent of the tessellators: the true surfaces are the two infinite cylinders (axis z, radius r; and
// axis x, radius r - the crossing is at 90 degrees, both axes through the origin), the volume is the closed form, and
// the mesh is read straight from the ON_Mesh arrays:
//   (1) every mesh vertex lies on one of the two cylinders (a vertex is a true surface point);
//   (2) every triangle edge whose two ends are on the same cylinder has its MIDPOINT inside that cylinder by at most
//       the chord tolerance (the facet sagitta, measured, not trusted from the tessellator);
//   (3) the mesh is a closed manifold;
//   (4) the volume is within the closed form by at most (surface area x tolerance) and, for a relative cross-check,
//       within 0.5% - the inscribed-polyhedron bound.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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

// Two equal-radius full-sweep cylinders, A along z, B along x, both centred on the origin (the Steinmetz fixture of
// test_basic.cpp's BuildSteinmetzCylinders at alpha = 90 degrees).
void BuildPair(double r, double length, Brep::CylindricalFace& a, Brep::CylindricalFace& b) {
  a.frame.origin = Point3d(0, 0, -0.5 * length);
  a.frame.xaxis = Vector3d(1, 0, 0);
  a.frame.yaxis = Vector3d(0, 1, 0);
  a.frame.zaxis = Vector3d(0, 0, 1);
  a.frame.UpdateEquation();
  a.radius = r;
  a.angle = 2.0 * ON_PI;
  a.length = length;
  b.frame.origin = Point3d(-0.5 * length, 0, 0);
  b.frame.xaxis = Vector3d(0, 1, 0);
  b.frame.yaxis = Vector3d(0, 0, 1);
  b.frame.zaxis = Vector3d(1, 0, 0);
  b.frame.UpdateEquation();
  b.radius = r;
  b.angle = 2.0 * ON_PI;
  b.length = length;
}

double RhoA(const ON_3dPoint& p) { return std::hypot(p.x, p.y); }  // distance from A's axis (z)
double RhoB(const ON_3dPoint& p) { return std::hypot(p.y, p.z); }  // distance from B's axis (x)

struct Measured {
  double worst_vertex = 0.0;    // max over vertices of min(|rhoA - r|, |rhoB - r|)
  double worst_sagitta = 0.0;   // max over same-cylinder edges of r - rho(midpoint)
  double worst_outside = 0.0;   // max over those midpoints of rho - r (must be ~0: chords lie inside)
  int edges_measured = 0;
};

Measured Measure(const Mesh& m, double r, double length) {
  Measured out;
  const ON_Mesh& raw = m.raw();
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const int corners = f.IsQuad() ? 4 : 3;
    for (int k = 0; k < corners; ++k) {
      const ON_3dPoint a(raw.m_V[f.vi[k]]);
      const ON_3dPoint b(raw.m_V[f.vi[(k + 1) % corners]]);
      // A vertex is a true surface point if it is on either cylinder or on one of the four planar end caps
      // (z = +-L/2 inside A's circle, x = +-L/2 inside B's circle) - the caps are exact planar discs.
      const double half = 0.5 * length;
      double off = std::min(std::fabs(RhoA(a) - r), std::fabs(RhoB(a) - r));
      if (RhoA(a) <= r + 1e-5) off = std::min(off, std::min(std::fabs(a.z - half), std::fabs(a.z + half)));
      if (RhoB(a) <= r + 1e-5) off = std::min(off, std::min(std::fabs(a.x - half), std::fabs(a.x + half)));
      out.worst_vertex = std::max(out.worst_vertex, off);
      const ON_3dPoint mid = 0.5 * (a + b);
      const bool on_a = std::fabs(RhoA(a) - r) < 1e-5 && std::fabs(RhoA(b) - r) < 1e-5;
      const bool on_b = std::fabs(RhoB(a) - r) < 1e-5 && std::fabs(RhoB(b) - r) < 1e-5;
      if (on_a) {
        out.worst_sagitta = std::max(out.worst_sagitta, r - RhoA(mid));
        out.worst_outside = std::max(out.worst_outside, RhoA(mid) - r);
        ++out.edges_measured;
      }
      if (on_b) {
        out.worst_sagitta = std::max(out.worst_sagitta, r - RhoB(mid));
        out.worst_outside = std::max(out.worst_outside, RhoB(mid) - r);
        ++out.edges_measured;
      }
    }
  }
  return out;
}

void CheckOne(const char* tag, const Mesh& m, double r, double length, double tol, double exact_volume, double surface_area) {
  const Measured q = Measure(m, r, length);
  const double err = m.Volume() - exact_volume;
  char what[400];
  std::snprintf(what, sizeof what, "%s: closed manifold", tag);
  d8test::Check(m.IsClosedManifold(), what);
  std::snprintf(what, sizeof what, "%s: every vertex lies on a true cylinder (worst %.3e <= 1e-5) over %d measured edges", tag,
                q.worst_vertex, q.edges_measured);
  d8test::Check(q.worst_vertex <= 1e-5 && q.edges_measured > 100, what);
  std::snprintf(what, sizeof what, "%s: measured facet sagitta %.3e <= chord tolerance %g, no edge midpoint outside the cylinder (%.1e)",
                tag, q.worst_sagitta, tol, q.worst_outside);
  d8test::Check(q.worst_sagitta <= tol && q.worst_outside <= 1e-5, what);
  std::snprintf(what, sizeof what, "%s: volume error %+.3e within surface area x tolerance (%.3e) and 0.5%% of the closed form", tag, err,
                surface_area * tol);
  d8test::Check(std::fabs(err) <= surface_area * tol && std::fabs(err) <= 5e-3 * exact_volume, what);
}

}  // namespace

D8_EXTRA_TEST(SteinmetzIntersectionAdaptiveClosedMeshesMeetChordToleranceOracle) {
  const double r = 2.0, length = 10.0;
  Brep::CylindricalFace ca, cb;
  BuildPair(r, length, ca, cb);
  const Brep a = Brep::FromMixedFaces({}, {ca});
  const Brep b = Brep::FromMixedFaces({}, {cb});
  const Brep result = BooleanCombineMixed(a, b, BooleanOp::Intersection);
  const double exact = 16.0 * r * r * r / 3.0;
  const double area = 16.0 * r * r;  // the Steinmetz bicylinder's surface area
  const double tol = 0.1;
  CheckOne("Steinmetz Intersection TessellateToClosedMeshAdaptive(0.1)", result.TessellateToClosedMeshAdaptive(tol), r, length, tol, exact, area);
  CheckOne("Steinmetz Intersection TessellateToClosedMeshCertifiedAdaptive(0.1)", result.TessellateToClosedMeshCertifiedAdaptive(tol), r,
           length, tol, exact, area);
}

D8_EXTRA_TEST(SteinmetzDifferenceCertifiedClosedMeshMeetsChordToleranceOracle) {
  const double r = 2.0, length = 10.0;
  Brep::CylindricalFace ca, cb;
  BuildPair(r, length, ca, cb);
  const Brep a = Brep::FromMixedFaces({}, {ca});
  const Brep b = Brep::FromMixedFaces({}, {cb});
  const Brep result = BooleanCombineMixed(a, b, BooleanOp::Difference);
  // A - B: A's full cylinder (pi r^2 L) minus the Steinmetz lens it shares with B.
  const double exact = ON_PI * r * r * length - 16.0 * r * r * r / 3.0;
  const double area = 2.0 * ON_PI * r * length + 2.0 * ON_PI * r * r + 16.0 * r * r;  // A's wall + caps + the bicylinder surface, an upper bound
  const double tol = 0.1;
  CheckOne("Steinmetz A - B TessellateToClosedMeshCertifiedAdaptive(0.1)", result.TessellateToClosedMeshCertifiedAdaptive(tol), r, length, tol,
           exact, area);
}

// A tolerance tighter than the shared half-ellipse chains' own sampling: the Steinmetz Intersection result is
// bit-identical at every tolerance from 0.1 down to 1e-4 (measured: 798 vertices, 1592 faces, volume 42.664035 each
// time - the 200-sample seam chains, not the tolerance, set the resolution). The certified closed-mesh API's contract is
// "every curved face measures within chord_tolerance, or the open weld comes back"; the independent sagitta oracle must
// therefore hold for ANY returned mesh that is closed. Asserted here as the conjunction a caller relies on: if the mesh
// is closed then its measured edge-midpoint sagitta is within the requested tolerance.
D8_EXTRA_TEST(SteinmetzIntersectionCertifiedClosedMeshNeverClaimsTighterThanItMeasures) {
  const double r = 2.0, length = 10.0;
  Brep::CylindricalFace ca, cb;
  BuildPair(r, length, ca, cb);
  const Brep a = Brep::FromMixedFaces({}, {ca});
  const Brep b = Brep::FromMixedFaces({}, {cb});
  const Brep result = BooleanCombineMixed(a, b, BooleanOp::Intersection);
  for (const double tol : {1e-4, 1e-5}) {
    Mesh m;
    bool threw = false;
    try {
      m = result.TessellateToClosedMeshCertifiedAdaptive(tol);
    } catch (const std::exception&) {
      threw = true;
    }
    const Measured q = Measure(m, r, length);
    char what[400];
    std::snprintf(what, sizeof what,
                  "Steinmetz Intersection TessellateToClosedMeshCertifiedAdaptive(%g): closed=%d threw=%d measured sagitta %.3e over %d edges - a closed mesh must meet the requested tolerance",
                  tol, (int)m.IsClosedManifold(), (int)threw, q.worst_sagitta, q.edges_measured);
    d8test::Check(threw || !m.IsClosedManifold() || q.worst_sagitta <= tol, what);
  }
}
