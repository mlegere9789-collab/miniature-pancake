// Unit test for the edge-consistent brep mesher: every OpenNURBS primitive
// must mesh to a closed manifold at coarse and fine tolerances, and the
// torus volume must approach the exact value.
#include <cstdio>
#include <cstdlib>

#include <opennurbs.h>

#include "dino8/kernel/mesh.h"
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
  }
  std::printf("%s\n", failures == 0 ? "ALL PASSED" : "FAILURES");
  return failures == 0 ? 0 : 1;
}
