// Brep::SphereWelded() / Brep::TorusWelded(): curved primitives with genuine B-rep topology.
//
// Brep::Sphere()/Torus() are one bare surface-only face (no vertices/edges/loops/trims, ON_Brep::IsValid() false).
// The "Welded" siblings carry real topology, so the topological invariants of the solid can be checked against
// closed forms that do not depend on how the body was built:
//  - Euler characteristic V - E + F: 2 for a sphere (genus 0), 0 for a torus (genus 1);
//  - the exact volumes 4/3 pi r^3 and 2 pi^2 R r^2 (VolumeOfSealedSolid(), which requires IsSolid() && IsValid());
//  - point membership of the tessellation: tube centre inside, the torus hole and far points outside;
//  - every seam edge borders the SAME face twice (a periodic surface glued to itself).
#include <cmath>
#include <stdexcept>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

constexpr double kPi = 3.14159265358979323846;

int EulerCharacteristic(const Brep& b) { return b.LiveVertexCount() - b.LiveEdgeCount() + b.LiveFaceCount(); }

template <class F>
bool Throws(F f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

D8_EXTRA_TEST(SphereWeldedHasGenuineTopology) {
  using d8test::Check;
  const double r = 2.0;
  const Brep s = Brep::SphereWelded(Point3d(1, 2, 3), r);
  ON_TextLog log;
  Check(s.raw().IsValid(&log) && s.raw().IsSolid(), "SphereWelded: IsValid() and IsSolid()");
  Check(s.LiveFaceCount() == 1 && s.LiveVertexCount() == 2 && s.LiveEdgeCount() == 1,
        "SphereWelded: 1 face, 2 pole vertices, 1 seam edge");
  Check(EulerCharacteristic(s) == 2, "SphereWelded: V - E + F == 2 (genus 0)");
  Check(s.Check().IsClean(), "SphereWelded: Check() is clean");
  Check(s.FacesOfEdge(0).size() == 1 && s.raw().m_E[0].m_ti.Count() == 2,
        "SphereWelded: the seam edge has 2 trims and both belong to the one face");
  Check(s.SplitDisjointPieces().size() == 1, "SphereWelded: SplitDisjointPieces() finds one piece (it throws on Sphere())");

  const double exact = 4.0 / 3.0 * kPi * r * r * r;
  Check(std::abs(s.VolumeOfSealedSolid() - exact) < 1e-9 * exact, "SphereWelded: VolumeOfSealedSolid() == 4/3 pi r^3");

  const auto m = s.TessellateToClosedMesh(24, 24);
  Check(m.IsClosedManifold(), "SphereWelded: tessellation is a closed manifold");
  Check(m.Volume() > 0.95 * exact && m.Volume() < exact, "SphereWelded: inscribed mesh volume is just below the exact volume");
  Check(m.ContainsPoint(Point3d(1, 2, 3)), "SphereWelded: centre is inside");
  Check(m.ContainsPoint(Point3d(1 + 0.9 * r, 2, 3)) && m.ContainsPoint(Point3d(1, 2, 3 - 0.9 * r)),
        "SphereWelded: points at 0.9 r are inside");
  Check(!m.ContainsPoint(Point3d(1 + 1.1 * r, 2, 3)) && !m.ContainsPoint(Point3d(1, 2, 3 + 1.1 * r)),
        "SphereWelded: points at 1.1 r are outside");

  const Brep bare = Brep::Sphere(Point3d(1, 2, 3), r);
  Check(bare.LiveVertexCount() == 0 && bare.LiveEdgeCount() == 0 && !bare.raw().IsValid(&log),
        "contrast: Sphere() still has no vertices/edges and is not a valid ON_Brep");
  Check(Throws([] { Brep::SphereWelded(Point3d(0, 0, 0), 0.0); }) && Throws([] { Brep::SphereWelded(Point3d(0, 0, 0), -1.0); }),
        "SphereWelded: a non-positive radius is refused");
}

D8_EXTRA_TEST(TorusWeldedHasGenuineTopology) {
  using d8test::Check;
  const double R = 3.0, r = 1.0;
  const Brep t = Brep::TorusWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), R, r);
  ON_TextLog log;
  Check(t.raw().IsValid(&log) && t.raw().IsSolid(), "TorusWelded: IsValid() and IsSolid()");
  Check(t.LiveFaceCount() == 1 && t.LiveVertexCount() == 1 && t.LiveEdgeCount() == 2,
        "TorusWelded: 1 face, 1 vertex, 2 seam edges");
  Check(EulerCharacteristic(t) == 0, "TorusWelded: V - E + F == 0 (genus 1)");
  Check(t.Check().IsClean(), "TorusWelded: Check() is clean");
  for (int e = 0; e < t.raw().m_E.Count(); ++e) {
    Check(t.raw().m_E[e].m_ti.Count() == 2 && t.FacesOfEdge(e).size() == 1,
          "TorusWelded: each seam edge has 2 trims, both of the one face");
  }
  Check(t.SplitDisjointPieces().size() == 1, "TorusWelded: SplitDisjointPieces() finds one piece");

  const double exact = 2.0 * kPi * kPi * R * r * r;
  Check(std::abs(t.VolumeOfSealedSolid() - exact) < 1e-9 * exact, "TorusWelded: VolumeOfSealedSolid() == 2 pi^2 R r^2");

  const auto m = t.TessellateToClosedMesh(24, 24);
  Check(m.IsClosedManifold(), "TorusWelded: tessellation is a closed manifold");
  Check(m.Volume() > 0.95 * exact && m.Volume() < exact, "TorusWelded: inscribed mesh volume is just below the exact volume");
  Check(m.ContainsPoint(Point3d(R, 0, 0)) && m.ContainsPoint(Point3d(0, -R, 0.5 * r)), "TorusWelded: points on the tube centreline are inside");
  Check(!m.ContainsPoint(Point3d(0, 0, 0)), "TorusWelded: the hole (the centre) is outside");
  Check(!m.ContainsPoint(Point3d(R + 1.5 * r, 0, 0)) && !m.ContainsPoint(Point3d(R, 0, 1.5 * r)), "TorusWelded: points beside the tube are outside");

  // A tilted, off-origin torus: same invariants and volume; membership is along the new frame.
  const Point3d c(1, 2, 3);
  const Brep tt = Brep::TorusWelded(c, Vector3d(2, 2, 2), R, r);  // axis need not be unit
  Check(tt.raw().IsValid(&log) && EulerCharacteristic(tt) == 0, "tilted TorusWelded: valid, Euler characteristic 0");
  Check(std::abs(tt.VolumeOfSealedSolid() - exact) < 1e-9 * exact, "tilted TorusWelded: same exact volume");
  const auto mt = tt.TessellateToClosedMesh(32, 32);
  const double s2 = std::sqrt(2.0);
  Check(mt.ContainsPoint(Point3d(c.x + R / s2, c.y - R / s2, c.z)), "tilted TorusWelded: a point R from the axis, in the plane, is inside");
  Check(!mt.ContainsPoint(c), "tilted TorusWelded: the centre is outside");
  const double a = 1.5 * r / std::sqrt(3.0);  // 1.5 r along the axis from the tube centreline
  Check(!mt.ContainsPoint(Point3d(c.x + R / s2 + a, c.y - R / s2 + a, c.z + a)),
        "tilted TorusWelded: 1.5 r above the tube centreline along the axis is outside");

  const Brep bare = Brep::Torus(c, Vector3d(0, 0, 1), R, r);
  Check(bare.LiveVertexCount() == 0 && bare.LiveEdgeCount() == 0 && !bare.raw().IsValid(&log),
        "contrast: Torus() still has no vertices/edges and is not a valid ON_Brep");
  Check(Throws([] { Brep::TorusWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 0.0); }), "TorusWelded: zero minor radius refused");
  Check(Throws([] { Brep::TorusWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 1.0); }), "TorusWelded: major == minor refused");
  Check(Throws([] { Brep::TorusWelded(Point3d(0, 0, 0), Vector3d(0, 0, 0), 3.0, 1.0); }), "TorusWelded: zero axis refused");
}
