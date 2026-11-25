// Two perpendicular EQUAL-radius fillets meeting at a right-angle box corner whose third edge stays sharp (FilletConvexEdgePairMitre). The result is the
// box minus the union of the two removed rounded prisms. Independent closed form: each prism removes a L r^2 (a = 1 - pi/4), their overlap is
// integral_0^r (r - sqrt(r^2 - s^2))^2 ds = r^3 (5/3 - pi/2), so V = V_box - a r^2 (L_A + L_B) + r^3 (5/3 - pi/2), whatever the two edge lengths.
// Failing first: the function did not exist (the first version of this file did not compile against the unchanged headers).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "dino8/kernel/brep.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::FilletConvexEdgePairMitre;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;

void Check(bool condition, const char* what) { d8test::Check(condition, what); }

std::string ReasonOf(const std::function<void()>& f) {
  try { f(); } catch (const std::exception& e) { return e.what(); }
  return "";
}

double MitreVolume(double box_volume, double len_a, double len_b, double r) {
  const double a = 1.0 - ON_PI / 4.0;
  return box_volume - a * r * r * (len_a + len_b) + r * r * r * (5.0 / 3.0 - ON_PI / 2.0);
}

// Valid closed oriented solid, and a closed oriented mesh whose volume converges to the closed form as the chord tolerance shrinks.
void CheckMitreBody(const Brep& b, double exact, int expected_faces, const std::string& tag) {
  ON_TextLog log;
  bool oriented = false, has_boundary = true;
  const bool manifold = b.raw().IsManifold(&oriented, &has_boundary);
  Check(b.raw().IsValid(&log) && b.raw().IsSolid() && manifold && oriented && !has_boundary && b.FaceCount() == expected_faces,
        (tag + "valid, closed, oriented solid with " + std::to_string(expected_faces) + " faces").c_str());
  struct C { double chord, tol; };
  for (const C& c : {C{1e-5, 2e-6}, C{1e-7, 2e-7}}) {
    const Mesh m = b.TessellateToClosedMeshAdaptive(c.chord);
    const Mesh::CheckReport r = m.Check();
    Check(m.IsClosedManifold() && r.naked_edges == 0 && r.orientation_conflicts == 0,
          (tag + "adaptive chord " + std::to_string(c.chord) + " mesh is closed and oriented (naked " + std::to_string(r.naked_edges) + ")").c_str());
    Check(std::fabs(m.Volume() - exact) < c.tol * exact,
          (tag + "adaptive chord " + std::to_string(c.chord) + " volume " + std::to_string(m.Volume()) + " vs closed form " + std::to_string(exact)).c_str());
  }
}

}  // namespace

D8_EXTRA_TEST(PairMitreBoxCorner) {
  const double L = 10.0, r = 1.0;
  const Brep box = Brep::Box(0, 0, 0, L, L, L);
  Brep out;
  const std::string why = ReasonOf([&] { out = FilletConvexEdgePairMitre(box, Point3d(0, 0, L), Point3d(0, 0, 0), Point3d(L, 0, L), r, r); });
  Check(why.empty(), ("[mitre] FilletConvexEdgePairMitre accepts two equal-radius edges at a box corner" + (why.empty() ? "" : ": " + why)).c_str());
  if (!why.empty()) return;
  const double exact = MitreVolume(L * L * L, L, L, r);
  CheckMitreBody(out, exact, 8, "[mitre box] ");
  // Two exact cylinders of radius r whose axes are perpendicular and intersect at O = (r, r, L - r).
  std::vector<ON_Cylinder> cyl;
  for (int f = 0; f < out.raw().m_F.Count(); ++f) {
    ON_Cylinder c;
    if (out.raw().m_F[f].SurfaceOf()->IsCylinder(&c, 1e-8)) cyl.push_back(c);
  }
  Check(cyl.size() == 2 && std::fabs(cyl[0].circle.Radius() - r) < 1e-9 && std::fabs(cyl[1].circle.Radius() - r) < 1e-9, "[mitre box] two exact cylinders of radius r");
  if (cyl.size() == 2) {
    Check(std::fabs(cyl[0].Axis() * cyl[1].Axis()) < 1e-9, "[mitre box] the two cylinder axes are perpendicular");
    const ON_3dPoint O(r, r, L - r);
    auto dist_to_axis = [&](const ON_Cylinder& c) {
      const ON_3dVector rel = O - c.circle.plane.origin;
      const ON_3dVector ax = c.Axis();
      return (rel - (rel * ax) * ax).Length();
    };
    Check(dist_to_axis(cyl[0]) < 1e-9 && dist_to_axis(cyl[1]) < 1e-9, "[mitre box] both axes pass through O = (r, r, L - r)");
  }
  // The edge shared by the two cylinders lies in the plane x + z = L (the mitre plane).
  double worst = 0.0;
  int shared = 0;
  const ON_Brep& br = out.raw();
  for (int e = 0; e < br.m_E.Count(); ++e) {
    const ON_BrepEdge& ed = br.m_E[e];
    if (ed.TrimCount() != 2) continue;
    ON_Cylinder c0, c1;
    if (!br.m_F[br.m_T[ed.m_ti[0]].FaceIndexOf()].SurfaceOf()->IsCylinder(&c0, 1e-8) || !br.m_F[br.m_T[ed.m_ti[1]].FaceIndexOf()].SurfaceOf()->IsCylinder(&c1, 1e-8)) continue;
    ++shared;
    for (int s = 0; s <= 20; ++s) {
      const ON_3dPoint p = ed.PointAt(ed.Domain().ParameterAt(s / 20.0));
      worst = std::max(worst, std::fabs(p.x + p.z - L));
    }
  }
  Check(shared >= 1 && worst < 2e-6, ("[mitre box] the cylinder/cylinder edge lies in the plane x + z = L (" + std::to_string(shared) + " edge(s), worst deviation " + std::to_string(worst) + ")").c_str());
  // Brep::Volume() works on the mitred body.
  double v = 0;
  std::string vwhy;
  try { v = out.Volume(); } catch (const std::exception& e) { vwhy = e.what(); }
  Check(vwhy.empty() && std::fabs(v - exact) < 2e-5 * exact, ("[mitre box] Brep::Volume() " + std::to_string(v) + " vs closed form " + std::to_string(exact) + (vwhy.empty() ? "" : " THREW: " + vwhy)).c_str());
  // Negative controls: the wrong radius changes the volume well outside the tolerance, and ignoring the overlap of the two prisms would be wrong.
  Check(std::fabs(MitreVolume(L * L * L, L, L, 1.1 * r) - exact) > 5e-4 * exact, "[mitre box] negative control: a 10% larger radius would NOT match");
  const double a = 1.0 - ON_PI / 4.0;
  Check(std::fabs((L * L * L - 2.0 * a * L * r * r) - exact) > 1e-5 * exact, "[mitre box] negative control: ignoring the overlap of the two prisms would NOT match");
}

D8_EXTRA_TEST(PairMitreUnequalEdgeLengthsAndARotatedSolid) {
  // Different edge lengths: the 8 x 6 x 5 box, edges A (length 5) and B (length 8) at (0, 0, 5), r = 0.7.
  const double r = 0.7;
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  Brep out;
  std::string why = ReasonOf([&] { out = FilletConvexEdgePairMitre(box, Point3d(0, 0, 5), Point3d(0, 0, 0), Point3d(8, 0, 5), r, r); });
  Check(why.empty(), ("[mitre 8x6x5] accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) CheckMitreBody(out, MitreVolume(240.0, 5.0, 8.0, r), 8, "[mitre 8x6x5] ");
  // The same body rotated about an oblique axis and moved: the construction must not depend on the axis-aligned frame.
  ON_Xform rot_xf;
  rot_xf.Rotation(0.7, ON_3dVector(1, 2, 3), ON_3dPoint(0, 0, 0));
  const ON_Xform xf = ON_Xform::TranslationTransformation(ON_3dVector(3, -2, 1)) * rot_xf;
  const Brep rot = box.Transform(xf);
  Brep out_rot;
  why = ReasonOf([&] { out_rot = FilletConvexEdgePairMitre(rot, xf * Point3d(0, 0, 5), xf * Point3d(0, 0, 0), xf * Point3d(8, 0, 5), r, r); });
  Check(why.empty(), ("[mitre rotated] accepted" + (why.empty() ? "" : ": " + why)).c_str());
  if (why.empty()) CheckMitreBody(out_rot, MitreVolume(240.0, 5.0, 8.0, r), 8, "[mitre rotated] ");
}

D8_EXTRA_TEST(PairMitreRefusalsNameTheirReasons) {
  const double L = 10.0;
  const Brep box = Brep::Box(0, 0, 0, L, L, L);
  const Point3d V(0, 0, L), A(0, 0, 0), B(L, 0, L);
  // Unequal radii are built since fillet_pair_unequal.cpp (a space-curve mitre, 9 faces); the body is checked there against its closed form.
  Brep unequal;
  std::string m = ReasonOf([&] { unequal = FilletConvexEdgePairMitre(box, V, A, B, 1.0, 1.5); });
  Check(m.empty() && unequal.FaceCount() == 9, ("[mitre] unequal radii are accepted and give 9 faces" + (m.empty() ? std::string() : ": " + m)).c_str());
  m = ReasonOf([&] { FilletConvexEdgePairMitre(box, Point3d(0, 0, 2.5), A, B, 1.0, 1.0); });
  Check(m.find("trihedral") != std::string::npos, ("[mitre refusal] a point that is not a corner is refused: " + m).c_str());
  m = ReasonOf([&] { FilletConvexEdgePairMitre(box, V, A, Point3d(L, L, L), 1.0, 1.0); });
  Check(!m.empty(), ("[mitre refusal] two points that are not both edges at the corner are refused: " + m).c_str());
  m = ReasonOf([&] { FilletConvexEdgePairMitre(box, V, A, B, 11.0, 11.0); });
  Check(m.find("longer than the radius") != std::string::npos, ("[mitre refusal] a radius longer than the edges is refused: " + m).c_str());
  m = ReasonOf([&] { FilletConvexEdgePairMitre(box, V, A, B, -1.0, -1.0); });
  Check(!m.empty(), ("[mitre refusal] a negative radius is refused: " + m).c_str());
}
