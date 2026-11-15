// Unit test for the edge-consistent brep mesher: every OpenNURBS primitive
// must mesh to a closed manifold at coarse and fine tolerances, and the
// torus volume must approach the exact value.
#include <cstdio>
#include <cstdlib>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "geom/BrepMesher.h"

using dino8::app::BrepMeshOptions;
using dino8::app::MeshBrepClosed;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace


// Thicken() results: a body whose faces share NO edge with each other (each cap and wall is built on its own). Faces closed in a
// direction (a thickened cylinder wall, a revolved zone) have a SEAM edge whose two trims are on the same face; that is not a
// shared edge, and the mesher must still take its unified-edge-sampling retry for them (it used to see "a shared edge" and skip
// the retry, leaving T-junctions: the mesh was not closed and its Volume read as an open body). Chord tolerance 0.005, the
// tolerance Volume uses in the app; the closed forms are pi*h*((r+t)^2 - r^2) and 2pi/3*((R+t)^3 - R^3)*(cos30 - cos60).
void ThickenedSeamBodies() {
  using dino8::kernel::Brep;
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::NurbsSurface;
  BrepMeshOptions opt;
  opt.chord_tolerance = 0.005;
  char label[200];
  for (const double turns : {1.0, 0.75}) {
    ON_NurbsCurve nc;
    const ON_Circle circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 2.0);
    if (turns >= 1.0) {
      circle.GetNurbForm(nc);
    } else {
      ON_Arc(circle, ON_Interval(0.0, 2.0 * ON_PI * turns)).GetNurbForm(nc);
    }
    NurbsCurve profile;
    profile.raw() = nc;
    const Brep wall = Brep::Extrude(profile, dino8::kernel::Vector3d(0, 0, 3), false);
    NurbsSurface sheet;
    sheet.raw() = *ON_NurbsSurface::Cast(wall.raw().m_S[0]);
    for (const double t : {0.2, -0.2}) {
      const Brep body = Brep::Thicken(Brep::FromSurface(sheet), t, false);
      const dino8::kernel::Mesh m = MeshBrepClosed(body.raw(), opt);
      const double exact = turns * ON_PI * 3.0 * (t > 0 ? (2.2 * 2.2 - 4.0) : (4.0 - 1.8 * 1.8));
      const double v = std::fabs(m.Volume());
      std::snprintf(label, sizeof(label), "thickened %s cylinder wall (t=%+.1f) meshes closed with volume %.4f within 0.5%% of %.4f",
                    turns >= 1.0 ? "full-turn (seamed)" : "270-degree", t, v, exact);
      Check(m.IsClosedManifold() && std::fabs(v - exact) < 5e-3 * exact, label);
    }
  }
  // The revolved zone (ON_BrepRevSurface: closed in u with a seam), R = 3, polar 30..60 degrees.
  const double a0 = 30.0 * ON_PI / 180.0, a1 = 60.0 * ON_PI / 180.0;
  ON_Arc arc(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(1, 0, 0), ON_3dVector(0, 0, 1)), 3.0), ON_Interval(ON_PI / 2.0 - a1, ON_PI / 2.0 - a0));
  ON_RevSurface* rev = ON_RevSurface::New();
  rev->m_curve = new ON_ArcCurve(arc);
  rev->m_axis = ON_Line(ON_3dPoint(0, 0, 0), ON_3dPoint(0, 0, 1));
  rev->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
  rev->m_t = rev->m_angle;
  rev->m_bTransposed = false;
  rev->BoundingBox();
  ON_Brep* zone = ON_BrepRevSurface(rev, false, false);
  Check(zone != nullptr, "revolved spherical zone builds");
  if (zone != nullptr) {
    const double c = std::cos(a0) - std::cos(a1);
    for (const double t : {0.2, -0.2}) {
      Brep sheet;
      sheet.raw() = *zone;
      const Brep body = Brep::Thicken(sheet, t, false);
      const dino8::kernel::Mesh m = MeshBrepClosed(body.raw(), opt);
      const double exact = 2.0 * ON_PI / 3.0 * (t > 0 ? (3.2 * 3.2 * 3.2 - 27.0) : (27.0 - 2.8 * 2.8 * 2.8)) * c;
      const double v = std::fabs(m.Volume());
      std::snprintf(label, sizeof(label), "thickened spherical zone (t=%+.1f) meshes closed with volume %.4f within 0.5%% of %.4f", t, v, exact);
      Check(m.IsClosedManifold() && std::fabs(v - exact) < 5e-3 * exact, label);
    }
    delete zone;
  }
  // Negative control: the un-thickened full-turn wall is an open tube (two naked circles) - it must NOT mesh closed, so the retry
  // above cannot be what makes everything look closed.
  {
    ON_NurbsCurve nc;
    ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 2.0).GetNurbForm(nc);
    NurbsCurve profile;
    profile.raw() = nc;
    const Brep tube = Brep::Extrude(profile, dino8::kernel::Vector3d(0, 0, 3), false);
    Check(!MeshBrepClosed(tube.raw(), opt).IsClosedManifold(), "negative control: the open (un-thickened) cylinder wall does not mesh closed");
  }
}

int main() {
  const double tols[] = {0.005, 0.02, 0.1, 0.3};
  for (double tol : tols) {
    BrepMeshOptions opt;
    opt.chord_tolerance = tol;
    char label[128];

    ON_Brep* sphere = ON_BrepSphere(ON_Sphere(ON_3dPoint(1, 2, 3), 8.0));
    dino8::kernel::Mesh ms = MeshBrepClosed(*sphere, opt);
    std::snprintf(label, sizeof(label), "sphere closed at tol %g", tol);
    Check(ms.IsClosedManifold(), label);
    const double vs = std::fabs(ms.Volume()), exact_s = 4.0 / 3.0 * ON_PI * 512;
    std::snprintf(label, sizeof(label), "sphere volume %.1f within 5%% of %.1f", vs, exact_s);
    Check(std::fabs(vs - exact_s) < 0.05 * exact_s, label);
    delete sphere;

    ON_Brep* cyl = ON_BrepCylinder(ON_Cylinder(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 5.0), 10.0), true, true);
    dino8::kernel::Mesh mc = MeshBrepClosed(*cyl, opt);
    std::snprintf(label, sizeof(label), "cylinder closed at tol %g", tol);
    Check(mc.IsClosedManifold(), label);
    delete cyl;

    ON_Cone cone;
    cone.Create(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 12.0, 4.0);
    ON_Brep* bcone = ON_BrepCone(cone, true);
    dino8::kernel::Mesh mco = MeshBrepClosed(*bcone, opt);
    std::snprintf(label, sizeof(label), "cone closed at tol %g", tol);
    Check(mco.IsClosedManifold(), label);
    delete bcone;

    ON_Brep* torus = ON_BrepTorus(ON_Torus(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 10.0, 3.0));
    dino8::kernel::Mesh mt = MeshBrepClosed(*torus, opt);
    std::snprintf(label, sizeof(label), "torus closed at tol %g", tol);
    Check(mt.IsClosedManifold(), label);
    delete torus;

    ON_3dPoint corners[8] = {ON_3dPoint(0, 0, 0), ON_3dPoint(4, 0, 0), ON_3dPoint(4, 3, 0), ON_3dPoint(0, 3, 0), ON_3dPoint(0, 0, 2), ON_3dPoint(4, 0, 2), ON_3dPoint(4, 3, 2), ON_3dPoint(0, 3, 2)};
    ON_Brep* box = ON_BrepBox(corners);
    dino8::kernel::Mesh mb = MeshBrepClosed(*box, opt);
    std::snprintf(label, sizeof(label), "box closed at tol %g", tol);
    Check(mb.IsClosedManifold() && std::fabs(std::fabs(mb.Volume()) - 24.0) < 1e-6, label);
    delete box;

    // The same check far from the origin (the smoke suite's own
    // "Box 1800,0,0 1810,10,0 10" fixture). MeshBrepClosed welds its face
    // meshes at diagonal * 1e-8, so the vertex-snapping quotient
    // coordinate / tolerance is ~1e10 here: that overflowed the 32-bit
    // `long` std::lround returns on Windows (Mesh::MergeAndWeld now uses
    // std::llround), welding the box's far corners together and returning
    // an open mesh - which the origin box above could never show.
    ON_3dPoint far_corners[8] = {ON_3dPoint(1800, 0, 0), ON_3dPoint(1810, 0, 0), ON_3dPoint(1810, 10, 0), ON_3dPoint(1800, 10, 0), ON_3dPoint(1800, 0, 10), ON_3dPoint(1810, 0, 10), ON_3dPoint(1810, 10, 10), ON_3dPoint(1800, 10, 10)};
    ON_Brep* far_box = ON_BrepBox(far_corners);
    dino8::kernel::Mesh mfb = MeshBrepClosed(*far_box, opt);
    std::snprintf(label, sizeof(label), "box at x=1800 closed with volume 1000 at tol %g (got volume %.6f)", tol, std::fabs(mfb.Volume()));
    Check(mfb.IsClosedManifold() && std::fabs(std::fabs(mfb.Volume()) - 1000.0) < 1e-3, label);
    delete far_box;
  }
  ThickenedSeamBodies();
  std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
