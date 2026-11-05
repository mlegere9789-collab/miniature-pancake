// Brep::SphereWelded() / TorusWelded() / CylinderWelded(): curved primitives with genuine B-rep topology.
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

D8_EXTRA_TEST(CylinderWeldedHasGenuineTopology) {
  using d8test::Check;
  const double r = 1.5, h = 4.0;
  const Point3d base(1, -2, 0.5);
  const Brep c = Brep::CylinderWelded(base, Vector3d(0, 3, 3), r, h);  // axis along (0,1,1), not unit
  ON_TextLog log;
  Check(c.raw().IsValid(&log) && c.raw().IsSolid(), "CylinderWelded: IsValid() and IsSolid()");
  // wall + 2 caps; one vertex on each cap circle; 2 circle edges + 1 seam edge.
  Check(c.LiveFaceCount() == 3 && c.LiveVertexCount() == 2 && c.LiveEdgeCount() == 3, "CylinderWelded: 3 faces, 2 vertices, 3 edges");
  Check(EulerCharacteristic(c) == 2, "CylinderWelded: V - E + F == 2 (genus 0)");
  Check(c.Check().IsClean(), "CylinderWelded: Check() is clean");
  int two_face_edges = 0, self_edges = 0;
  for (int e = 0; e < c.raw().m_E.Count(); ++e) {
    Check(c.raw().m_E[e].m_ti.Count() == 2, "CylinderWelded: every edge has exactly 2 trims");
    const size_t nf = c.FacesOfEdge(e).size();
    two_face_edges += nf == 2;
    self_edges += nf == 1;
  }
  Check(two_face_edges == 2 && self_edges == 1, "CylinderWelded: two cap circles border wall+cap, the seam borders the wall twice");
  Check(c.SplitDisjointPieces().size() == 1, "CylinderWelded: one piece");

  const double exact = kPi * r * r * h;
  // The two trimmed cap discs are integrated with VolumeOfSealedSolid()'s own 1e-6 trimmed-face tolerance; the
  // measured error here is ~5e-8 relative (the sphere and torus have no trimmed faces and agree to 1e-9).
  Check(std::abs(c.VolumeOfSealedSolid() - exact) < 1e-6 * exact, "CylinderWelded: VolumeOfSealedSolid() == pi r^2 h to 1e-6");

  const auto m = c.TessellateToClosedMesh(32, 8);
  Check(m.IsClosedManifold(), "CylinderWelded: tessellation is a closed manifold");
  const double s2 = std::sqrt(2.0);
  const auto at = [&](double along, double across) {  // along the axis (0,1,1)/sqrt2, across along x
    return Point3d(base.x + across, base.y + along / s2, base.z + along / s2);
  };
  Check(m.ContainsPoint(at(0.5 * h, 0.0)) && m.ContainsPoint(at(0.1 * h, 0.8 * r)) && m.ContainsPoint(at(0.9 * h, -0.8 * r)),
        "CylinderWelded: interior points are inside");
  Check(!m.ContainsPoint(at(-0.1 * h, 0.0)) && !m.ContainsPoint(at(1.1 * h, 0.0)) && !m.ContainsPoint(at(0.5 * h, 1.2 * r)),
        "CylinderWelded: points past either cap or outside the wall are outside");

  Check(Throws([] { Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 0.0, 1.0); }), "CylinderWelded: zero radius refused");
  Check(Throws([] { Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, -1.0); }), "CylinderWelded: negative height refused");
  Check(Throws([] { Brep::CylinderWelded(Point3d(0, 0, 0), Vector3d(0, 0, 0), 1.0, 1.0); }), "CylinderWelded: zero axis refused");
}

D8_EXTRA_TEST(ConeWeldedFullConeApexIsDegenerateNotSeam) {
  using d8test::Check;
  const double r = 2.0, h = 3.0;
  const Point3d base(1, 2, 3);
  const Brep c = Brep::ConeWelded(base, Vector3d(0, 0, 1), r, 0.0, h);
  ON_TextLog log;
  Check(c.raw().IsValid(&log) && c.raw().IsSolid(), "full cone: IsValid() and IsSolid()");
  // wall + base cap; apex vertex + base-circle vertex; base circle edge + seam edge (the apex has no edge of its own).
  Check(c.LiveFaceCount() == 2 && c.LiveVertexCount() == 2 && c.LiveEdgeCount() == 2, "full cone: 2 faces, 2 vertices, 2 edges");
  Check(EulerCharacteristic(c) == 2, "full cone: V - E + F == 2 (genus 0)");
  Check(c.Check().IsClean(), "full cone: Check() is clean");
  int singular = 0;
  for (int t = 0; t < c.raw().m_T.Count(); ++t) {
    if (c.raw().m_T[t].m_type != ON_BrepTrim::singular) continue;
    ++singular;
    const ON_BrepVertex* v = c.raw().m_T[t].Vertex(0);
    Check(v != nullptr && std::abs(v->point.x - base.x) < 1e-12 && std::abs(v->point.y - base.y) < 1e-12 &&
              std::abs(v->point.z - (base.z + h)) < 1e-12,
          "full cone: the singular trim sits at the apex point");
  }
  Check(singular == 1, "full cone: exactly one singular (degenerate) trim, at the apex");
  Check(c.SplitDisjointPieces().size() == 1, "full cone: one piece");
  const auto m = c.TessellateToClosedMesh(32, 8);
  Check(m.IsClosedManifold(), "full cone: tessellation is a closed manifold");
  Check(m.ContainsPoint(Point3d(1, 2, 3.5)) && m.ContainsPoint(Point3d(1.5, 2, 3.5)) && m.ContainsPoint(Point3d(1, 2, 5.5)),
        "full cone: interior points are inside (just above the base, near the apex)");
  // At height z above the base the radius is r (1 - z/h): 1.3 at z = 0.75 (radius 1.5) is inside, 1.5 at z = 2 (radius 0.667) is outside.
  Check(m.ContainsPoint(Point3d(1 + 1.3, 2, 3.75)) && !m.ContainsPoint(Point3d(1 + 1.5, 2, 5.0)) && !m.ContainsPoint(Point3d(1, 2, 6.2)),
        "full cone: the radius shrinks linearly to the apex");
}

D8_EXTRA_TEST(ConeWeldedFrustumTopology) {
  using d8test::Check;
  const double r0 = 2.0, r1 = 0.75, h = 3.0;
  const Brep f = Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 2), r0, r1, h);
  ON_TextLog log;
  Check(f.raw().IsValid(&log) && f.raw().IsSolid(), "frustum: IsValid() and IsSolid()");
  Check(f.LiveFaceCount() == 3 && f.LiveVertexCount() == 2 && f.LiveEdgeCount() == 3, "frustum: 3 faces, 2 vertices, 3 edges");
  Check(EulerCharacteristic(f) == 2, "frustum: V - E + F == 2");
  Check(f.Check().IsClean(), "frustum: Check() is clean");
  int singular = 0;
  for (int t = 0; t < f.raw().m_T.Count(); ++t) singular += f.raw().m_T[t].m_type == ON_BrepTrim::singular;
  Check(singular == 0, "frustum: no singular trim (no apex)");
  const double V = kPi * h / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1);
  Check(std::abs(f.VolumeOfSealedSolid() - V) < 1e-6 * V, "frustum: VolumeOfSealedSolid() == pi h/3 (R0^2 + R0 R1 + R1^2)");
  const auto m = f.TessellateToClosedMesh(32, 8);
  Check(m.IsClosedManifold(), "frustum: tessellation is a closed manifold");
  Check(m.ContainsPoint(Point3d(1.9, 0, 0.05)) && !m.ContainsPoint(Point3d(1.9, 0, 2.5)) && m.ContainsPoint(Point3d(0.7, 0, 2.9)) &&
            !m.ContainsPoint(Point3d(0, 0, 3.2)),
        "frustum: wide near the base, narrow near the top, closed by both caps");
  // An inverted frustum (top wider than base) is the same construction.
  const Brep inv = Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), r1, r0, h);
  Check(inv.raw().IsValid(&log) && inv.raw().IsSolid() && std::abs(inv.VolumeOfSealedSolid() - V) < 1e-6 * V,
        "inverted frustum: valid, same volume by symmetry");
  Check(Throws([] { Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 0.0, 0.0, 1.0); }), "ConeWelded: zero base radius refused");
  Check(Throws([] { Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, -0.1, 1.0); }), "ConeWelded: negative top radius refused");
  Check(Throws([] { Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 1), 1.0, 0.5, 0.0); }), "ConeWelded: zero height refused");
  Check(Throws([] { Brep::ConeWelded(Point3d(0, 0, 0), Vector3d(0, 0, 0), 1.0, 0.5, 1.0); }), "ConeWelded: zero axis refused");
}
