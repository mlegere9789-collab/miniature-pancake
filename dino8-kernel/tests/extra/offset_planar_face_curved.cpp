// Brep::OffsetPlanarFaceAlongNormal: translate ONE planar face of a solid along its outward normal when the neighbours are
// NOT all planar (a cylindrical wall, a rounded-corner wall) - the case boolean.h's OffsetFace()/PushPullFace() cannot take.
//
// Every expected number is an independent closed form of an extruded cross-section (never a value read back from the code
// under test): V = A_cross * h and S = 2 * A_cross + P_cross * h, with
//   * the box:            A_cross = a*b,               P_cross = 2 (a + b),
//   * the rounded box:    A_cross = a*b - r^2 (1 - pi/4), P_cross = 2 (a + b) - 2 r + (pi/2) r   (one corner rounded with radius r),
//   * the cylinder:       A_cross = pi R^2,            P_cross = 2 pi R.
// They are compared with the kernel's exact divergence-theorem Volume()/Area() (which integrate the TRIMS the edit rewrote), with an
// independent adaptive tessellation, with Check() (clean, closed, oriented) and with the bounding box.
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Result;

constexpr double kPi = 3.14159265358979323846;

// The planar face whose outward normal is `dir` and whose plane sits at `coord` along it (-1 if none).
int PlanarFaceAt(const Brep& b, const ON_3dVector& dir, double coord) {
  for (int fi = 0; fi < b.raw().m_F.Count(); ++fi) {
    const ON_BrepFace& f = b.raw().m_F[fi];
    if (f.m_face_index < 0 || !f.IsPlanar(nullptr, 1e-9)) continue;
    ON_3dVector n = f.NormalAt(f.Domain(0).Mid(), f.Domain(1).Mid());
    n.Unitize();
    if (n * dir < 0.999) continue;
    const ON_3dPoint p = f.PointAt(f.Domain(0).Mid(), f.Domain(1).Mid());
    if (std::fabs(p.x * dir.x + p.y * dir.y + p.z * dir.z - coord) < 1e-9) return fi;
  }
  return -1;
}

int NonPlanarFace(const Brep& b) {
  for (int fi = 0; fi < b.raw().m_F.Count(); ++fi) {
    const ON_BrepFace& f = b.raw().m_F[fi];
    if (f.m_face_index >= 0 && !f.IsPlanar(nullptr, 1e-9)) return fi;
  }
  return -1;
}

dino8::kernel::NurbsCurve CircleProfile(double radius) {
  const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), radius);
  ON_NurbsCurve nc;
  circle.GetNurbForm(nc);
  dino8::kernel::NurbsCurve profile;
  profile.raw() = nc;
  return profile;
}

Brep Cylinder(double radius, double height) {
  return Brep::Extrude(CircleProfile(radius), dino8::kernel::Vector3d(0, 0, height), true);
}

// A 10 x 10 square with its (10,10) corner rounded by radius 2, extruded 10 along z: one rounded wall, a planar top and bottom.
Brep RoundedBox() {
  ON_PolyCurve poly;
  poly.Append(new ON_LineCurve(ON_3dPoint(0, 0, 0), ON_3dPoint(10, 0, 0)));
  poly.Append(new ON_LineCurve(ON_3dPoint(10, 0, 0), ON_3dPoint(10, 8, 0)));
  poly.Append(new ON_ArcCurve(ON_Arc(ON_Circle(ON_Plane(ON_3dPoint(8, 8, 0), ON_3dVector(0, 0, 1)), 2.0), ON_Interval(0.0, kPi / 2.0))));
  poly.Append(new ON_LineCurve(ON_3dPoint(8, 10, 0), ON_3dPoint(0, 10, 0)));
  poly.Append(new ON_LineCurve(ON_3dPoint(0, 10, 0), ON_3dPoint(0, 0, 0)));
  ON_NurbsCurve nc;
  poly.GetNurbForm(nc);
  dino8::kernel::NurbsCurve profile;
  profile.raw() = nc;
  return Brep::Extrude(profile, dino8::kernel::Vector3d(0, 0, 10), true);
}

constexpr double kRoundR = 2.0;
const double kRoundAcross = 100.0 - kRoundR * kRoundR * (1.0 - kPi / 4.0);
const double kRoundPerimeter = 40.0 - 2.0 * kRoundR + kPi / 2.0 * kRoundR;

// The checks every result must pass, against the closed-form volume/area.
// `closed_mesh` is false for the Extrude() cylinder only: a FRESH Extrude() cylinder's polar caps are not a closed manifold under
// the adaptive tessellator either (measured), so there the edited body is instead compared with a freshly built one.
void CheckSolidMatches(const Brep& b, double volume, double area, const std::string& w, bool closed_mesh = true) {
  const double v = b.Volume();
  const double a = b.Area();
  d8test::Check(std::fabs(v - volume) < 1e-6 * volume, (w + ": exact Volume() equals the closed form to 1e-6 relative").c_str());
  d8test::Check(std::fabs(a - area) < 1e-6 * area, (w + ": exact Area() equals the closed form to 1e-6 relative").c_str());
  const Brep::CheckReport report = b.Check();
  d8test::Check(report.IsClean() && report.is_closed && report.is_oriented,
                (w + ": Check() reports no issue, closed and oriented").c_str());
  const dino8::kernel::Mesh mesh = b.TessellateToClosedMeshAdaptive(1e-7);
  if (closed_mesh) d8test::Check(mesh.IsClosedManifold(), (w + ": the adaptive tessellation is a closed manifold").c_str());
  d8test::Check(std::fabs(mesh.Volume() - volume) < 1e-5 * volume,
                (w + ": the independent tessellation volume equals the closed form to 1e-5 relative").c_str());
}

// The edited cylinder tessellates and integrates exactly like a cylinder built from scratch at the final height.
void CheckSameAsFreshCylinder(const Brep& edited, double radius, double height, const std::string& w) {
  const Brep fresh = Cylinder(radius, height);
  const double mv = edited.TessellateToClosedMeshAdaptive(1e-7).Volume(), fv = fresh.TessellateToClosedMeshAdaptive(1e-7).Volume();
  d8test::Check(std::fabs(mv - fv) < 1e-9 * fv, (w + ": the tessellated volume equals a freshly built cylinder's to 1e-9 relative").c_str());
  d8test::Check(std::fabs(edited.Volume() - fresh.Volume()) < 1e-9 * fresh.Volume() &&
                    std::fabs(edited.Area() - fresh.Area()) < 1e-9 * fresh.Area(),
                (w + ": the exact Volume() and Area() equal a freshly built cylinder's to 1e-9 relative").c_str());
}

// Extents of the live B-rep vertices (the corner points the edit moved; a surface's own bounding box may be larger than its face).
double VertexExtent(const Brep& b, int axis, bool want_max) {
  double e = want_max ? -1e300 : 1e300;
  for (int i = 0; i < b.raw().m_V.Count(); ++i) {
    if (b.raw().m_V[i].m_vertex_index < 0) continue;
    const double c = b.raw().m_V[i].point[axis];
    e = want_max ? std::max(e, c) : std::min(e, c);
  }
  return e;
}
double MaxZ(const Brep& b) { return VertexExtent(b, 2, true); }
double MinZ(const Brep& b) { return VertexExtent(b, 2, false); }

}  // namespace

// Planar neighbours only (a welded box): the four walls must be continued past their own surface rectangle and re-trimmed.
D8_EXTRA_TEST(OffsetPlanarFaceWeldedBoxTopAndWall) {
  {
    Brep b = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 10.0);
    d8test::Check(top >= 0 && b.OffsetPlanarFaceAlongNormal(top, 3.0) == Result::Ok, "box: top +3 succeeds");
    d8test::Check(std::fabs(MaxZ(b) - 13.0) < 1e-9, "box: the top is at z = 13");
    CheckSolidMatches(b, 1300.0, 720.0, "box top +3");
  }
  {
    // Control: the oracle can tell a real offset from a naive one. Translating ONLY the top surface (no vertices, edges or
    // neighbours) is exactly the wrong result the edit must not produce; Check() must flag it.
    Brep naive = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
    const int top = PlanarFaceAt(naive, ON_3dVector(0, 0, 1), 10.0);
    naive.raw().m_S[naive.raw().m_F[top].m_si]->Translate(ON_3dVector(0, 0, 3.0));
    d8test::Check(!naive.Check().IsClean(), "control: moving only the top surface leaves trim/edge gaps that Check() reports");
  }
  {
    Brep b = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 10.0);
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, -4.0) == Result::Ok, "box: top -4 succeeds");
    CheckSolidMatches(b, 600.0, 2.0 * 100.0 + 40.0 * 6.0, "box top -4");
  }
  {
    // The -x wall (outward normal -x) pushed out by 2: the body grows in -x, the box becomes 12 x 10 x 10.
    Brep b = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
    const int wall = PlanarFaceAt(b, ON_3dVector(-1, 0, 0), 0.0);
    d8test::Check(wall >= 0 && b.OffsetPlanarFaceAlongNormal(wall, 2.0) == Result::Ok, "box: the x = 0 wall +2 succeeds");
    d8test::Check(std::fabs(VertexExtent(b, 0, false) + 2.0) < 1e-9, "box: the body now starts at x = -2");
    CheckSolidMatches(b, 1200.0, 2.0 * 120.0 + 44.0 * 10.0, "box wall +2");
  }
}

// One rounded corner (a cylindrical stretch inside a single wall face): the wall must be continued and re-trimmed as a whole.
D8_EXTRA_TEST(OffsetPlanarFaceRoundedBoxTopGrowsPullsAndRoundTrips) {
  {
    Brep b = RoundedBox();
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 10.0);
    d8test::Check(top >= 0 && NonPlanarFace(b) >= 0, "fixture: a planar top and a curved wall exist");
    CheckSolidMatches(b, 10.0 * kRoundAcross, 2.0 * kRoundAcross + kRoundPerimeter * 10.0, "fixture rounded box");
    const int faces = b.FaceCount();
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, 3.0) == Result::Ok, "rounded box: top +3 succeeds");
    d8test::Check(b.FaceCount() == faces, "rounded box: the face count is unchanged (a pure re-extension)");
    d8test::Check(std::fabs(MaxZ(b) - 13.0) < 1e-9 && std::fabs(MinZ(b)) < 1e-9, "rounded box: the body spans z in [0, 13]");
    CheckSolidMatches(b, 13.0 * kRoundAcross, 2.0 * kRoundAcross + kRoundPerimeter * 13.0, "rounded box top +3");
  }
  {
    Brep b = RoundedBox();
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 10.0);
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, -4.0) == Result::Ok, "rounded box: top -4 succeeds");
    d8test::Check(std::fabs(MaxZ(b) - 6.0) < 1e-9, "rounded box: the top is now at z = 6");
    CheckSolidMatches(b, 6.0 * kRoundAcross, 2.0 * kRoundAcross + kRoundPerimeter * 6.0, "rounded box top -4");
  }
  {
    Brep b = RoundedBox();
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 10.0);
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, 3.0) == Result::Ok && b.OffsetPlanarFaceAlongNormal(top, -3.0) == Result::Ok,
                  "rounded box: +3 then -3 on the same face both succeed");
    d8test::Check(std::fabs(MaxZ(b) - 10.0) < 1e-9, "rounded box: the top is back at z = 10");
    CheckSolidMatches(b, 10.0 * kRoundAcross, 2.0 * kRoundAcross + kRoundPerimeter * 10.0, "rounded box round trip");
  }
  {
    // The bottom face's outward normal is -z: a positive distance grows the body DOWNWARD (the sign convention).
    Brep b = RoundedBox();
    const int bottom = PlanarFaceAt(b, ON_3dVector(0, 0, -1), 0.0);
    d8test::Check(bottom >= 0 && b.OffsetPlanarFaceAlongNormal(bottom, 2.0) == Result::Ok, "rounded box: bottom +2 succeeds");
    d8test::Check(std::fabs(MinZ(b) + 2.0) < 1e-9 && std::fabs(MaxZ(b) - 10.0) < 1e-9, "rounded box: the body spans z in [-2, 10]");
    CheckSolidMatches(b, 12.0 * kRoundAcross, 2.0 * kRoundAcross + kRoundPerimeter * 12.0, "rounded box bottom +2");
  }
}

// A round cylinder: the cap is a polar disk (a singular centre trim), the single seamed wall is the only neighbour.
D8_EXTRA_TEST(OffsetPlanarFaceCylinderCaps) {
  const double R = 3.0;
  const double across = kPi * R * R, perimeter = 2.0 * kPi * R;
  {
    Brep b = Cylinder(R, 8.0);
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 8.0);
    d8test::Check(top >= 0, "cylinder fixture: a planar cap at z = 8 exists");
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, 2.5) == Result::Ok, "cylinder: top cap +2.5 succeeds");
    d8test::Check(std::fabs(MaxZ(b) - 10.5) < 1e-9, "cylinder: top at z = 10.5");
    CheckSolidMatches(b, across * 10.5, 2.0 * across + perimeter * 10.5, "cylinder +2.5", false);
    CheckSameAsFreshCylinder(b, R, 10.5, "cylinder +2.5");
  }
  {
    Brep b = Cylinder(R, 8.0);
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 8.0);
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, -5.0) == Result::Ok, "cylinder: top cap -5 succeeds");
    CheckSolidMatches(b, across * 3.0, 2.0 * across + perimeter * 3.0, "cylinder -5", false);
    CheckSameAsFreshCylinder(b, R, 3.0, "cylinder -5");
  }
  {
    Brep b = Cylinder(R, 8.0);
    const int bottom = PlanarFaceAt(b, ON_3dVector(0, 0, -1), 0.0);
    d8test::Check(bottom >= 0 && b.OffsetPlanarFaceAlongNormal(bottom, 2.0) == Result::Ok, "cylinder: bottom cap +2 succeeds");
    d8test::Check(std::fabs(MinZ(b) + 2.0) < 1e-9 && std::fabs(MaxZ(b) - 8.0) < 1e-9, "cylinder: the body now spans z in [-2, 8]");
    CheckSolidMatches(b, across * 10.0, 2.0 * across + perimeter * 10.0, "cylinder bottom +2", false);
    CheckSameAsFreshCylinder(b, R, 10.0, "cylinder bottom +2");
  }
}

// Negative controls: every refusal leaves the body EXACTLY as it was, and the contract's throws/no-op hold.
D8_EXTRA_TEST(OffsetPlanarFaceRefusalsLeaveTheBodyUntouched) {
  const char* kTilted = "not a surface of translation along the face normal";
  auto names = [](const std::string& why, const char* part) { return why.find(part) != std::string::npos; };
  {
    // A drafted (tapered) cylinder: the cap's neighbour is a cone, NOT a surface of translation along z.
    Brep b = Brep::ExtrudeTapered(CircleProfile(3.0), dino8::kernel::Vector3d(0, 0, 8), 5.0 * kPi / 180.0, true);
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 8.0);
    d8test::Check(top >= 0, "fixture: the drafted cylinder keeps a planar top cap");
    const double v = b.Volume(), a = b.Area();
    std::string why;
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, 1.0, 1e-6, &why) == Result::Failed, "refusal: a conical (tilted) neighbour is refused");
    d8test::Check(names(why, kTilted) && names(why, "cone"), ("refusal: ... and names the cause (" + why + ")").c_str());
    d8test::Check(std::fabs(b.Volume() - v) < 1e-12 * v && std::fabs(b.Area() - a) < 1e-12 * a && std::fabs(MaxZ(b) - 8.0) < 1e-9,
                  "refusal: ... and the drafted body is unchanged");
  }
  {
    // A chamfered top edge: the top face's neighbour is a 45 degree plane. A tilted PLANAR neighbour would need the moved face's own
    // outline re-derived (the chamfer line slides sideways), which this method does not do: a measured refusal, not a wrong answer.
    Brep b = dino8::kernel::ChamferConvexEdge(Brep::BoxWelded(0, 0, 0, 10, 10, 10), dino8::kernel::Point3d(0, 10, 10),
                                              dino8::kernel::Point3d(10, 10, 10), 2.0, 2.0);
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 10.0);
    d8test::Check(top >= 0, "fixture: the chamfered box keeps a planar top");
    const double v = b.Volume(), a = b.Area();
    d8test::Check(std::fabs(v - (1000.0 - 2.0 * 2.0 / 2.0 * 10.0)) < 1e-6, "fixture: the chamfered box has volume 1000 - (2*2/2)*10 = 980");
    std::string why;
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, 1.0, 1e-6, &why) == Result::Failed, "refusal: a chamfer (tilted planar) neighbour is refused");
    d8test::Check(names(why, kTilted) && names(why, "chamfer"), ("refusal: ... and names the cause (" + why + ")").c_str());
    d8test::Check(std::fabs(b.Volume() - v) < 1e-12 * v && std::fabs(b.Area() - a) < 1e-12 * a && std::fabs(MaxZ(b) - 10.0) < 1e-9,
                  "refusal: ... and the chamfered body is unchanged");
    // The refusal is specific: the x = 0 wall of the same body (every neighbour contains the x direction) IS offset exactly.
    const int wall = PlanarFaceAt(b, ON_3dVector(-1, 0, 0), 0.0);
    d8test::Check(wall >= 0 && b.OffsetPlanarFaceAlongNormal(wall, 1.0) == Result::Ok, "control: the x = 0 wall of that chamfered box can be offset");
    d8test::Check(std::fabs(b.Volume() - (v + 98.0)) < 1e-6, "control: ... gaining exactly one slab of the chamfered cross-section (100 - 2 = 98) times 1");
  }
  {
    Brep b = Cylinder(3.0, 8.0);
    const int wall = NonPlanarFace(b);
    std::string why;
    d8test::Check(wall >= 0 && b.OffsetPlanarFaceAlongNormal(wall, 1.0, 1e-6, &why) == Result::Failed, "refusal: a non-planar face is refused");
    d8test::Check(names(why, "not planar"), ("refusal: ... and names the cause (" + why + ")").c_str());
    const int top = PlanarFaceAt(b, ON_3dVector(0, 0, 1), 8.0);
    const double v = b.Volume();
    const Result deep = b.OffsetPlanarFaceAlongNormal(top, -8.0, 1e-6, &why);
    d8test::Check(deep == Result::Failed && names(why, "would vanish"),
                  ("refusal: pulling the cap down to the base (zero height) is refused and says why (" + why + ")").c_str());
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, -9.0) == Result::Failed, "refusal: pulling the cap through the base is refused");
    d8test::Check(b.OffsetPlanarFaceAlongNormal(top, 0.0) == Result::NoOpAlreadySatisfied, "distance 0 is a no-op");
    const Result nan_result = b.OffsetPlanarFaceAlongNormal(top, std::nan(""), 1e-6, &why);
    d8test::Check(nan_result == Result::Failed && names(why, "not a finite number"), "a non-finite distance is refused and says why");
    d8test::Check(std::fabs(b.Volume() - v) < 1e-12 * v && std::fabs(MaxZ(b) - 8.0) < 1e-12, "refusals: the cylinder is unchanged");
    bool threw = false;
    try {
      b.OffsetPlanarFaceAlongNormal(999, 1.0);
    } catch (const std::out_of_range&) {
      threw = true;
    }
    d8test::Check(threw, "an out-of-range face index throws std::out_of_range");
  }
  {
    // A Box() (six independent surfaces, no loops/edges) has no topology to edit: refused, unchanged.
    Brep b = Brep::Box(0, 0, 0, 10, 10, 10);
    std::string why;
    const Result box_result = b.OffsetPlanarFaceAlongNormal(1, 1.0, 1e-6, &why);
    d8test::Check(box_result == Result::Failed && names(why, "Box()"),
                  ("refusal: a topology-less Box() has no boundary to move, and the reason says so (" + why + ")").c_str());
  }
}
