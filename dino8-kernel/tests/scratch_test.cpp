#include <cmath>
#include <cstdio>
#include "dino8/kernel/brep.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Brep::VolumeMassProperties(), exercised standalone against hand-
  // derivable analytic fixtures before trusting the real registered
  // test (TestBrepVolumeMassProperties).

  // --- Axis-aligned box 2x3x4, offset from the origin ---
  // V = 24; centroid = (1, 1.5, 2); for a box with side lengths a,b,c
  // about its OWN centroid: Ixx=V(b^2+c^2)/12, Iyy=V(a^2+c^2)/12,
  // Izz=V(a^2+b^2)/12, all products of inertia 0 (axis-aligned symmetry).
  // a=2,b=3,c=4 => Ixx=24*25/12=50, Iyy=24*20/12=40, Izz=24*13/12=26.
  {
    const Brep box = Brep::Box(0, 0, 0, 2, 3, 4);
    const MassProperties mp = box.VolumeMassProperties();
    std::printf("box: volume=%.6f (expect 24)\n", mp.volume);
    std::printf("box: centroid=(%.6f,%.6f,%.6f) (expect (1,1.5,2))\n", mp.centroid.x, mp.centroid.y, mp.centroid.z);
    std::printf("box: ixx=%.6f iyy=%.6f izz=%.6f (expect 50,40,26)\n", mp.ixx, mp.iyy, mp.izz);
    std::printf("box: ixy=%.6f iyz=%.6f ixz=%.6f (expect 0,0,0)\n", mp.ixy, mp.iyz, mp.ixz);
    std::printf("box: principal_moments=(%.6f,%.6f,%.6f)\n", mp.principal_moments[0], mp.principal_moments[1],
                mp.principal_moments[2]);
  }

  // --- Sphere, radius 5, centered off-origin ---
  // V = 4/3 pi r^3; centroid = center; Ixx=Iyy=Izz=(2/5) V r^2 about the
  // centroid (any central axis - a sphere has no preferred axis), all
  // products of inertia 0.
  {
    const double r = 5.0;
    const Point3d center(10, -3, 7);
    const Brep sphere = Brep::Sphere(center, r);
    const MassProperties mp = sphere.VolumeMassProperties();
    const double v_expected = (4.0 / 3.0) * M_PI * r * r * r;
    const double i_expected = 0.4 * v_expected * r * r;
    std::printf("sphere: volume=%.6f (expect %.6f)\n", mp.volume, v_expected);
    std::printf("sphere: centroid=(%.6f,%.6f,%.6f) (expect (10,-3,7))\n", mp.centroid.x, mp.centroid.y, mp.centroid.z);
    std::printf("sphere: ixx=%.6f iyy=%.6f izz=%.6f (expect all %.6f)\n", mp.ixx, mp.iyy, mp.izz, i_expected);
    std::printf("sphere: ixy=%.6f iyz=%.6f ixz=%.6f (expect 0,0,0)\n", mp.ixy, mp.iyz, mp.ixz);
  }

  // --- Negative volume (every face flipped inward) throws ---
  {
    Brep box = Brep::Box(0, 0, 0, 2, 3, 4);
    for (int i = 0; i < box.raw().m_F.Count(); ++i) box.raw().m_F[i].m_bRev = !box.raw().m_F[i].m_bRev;
    try {
      const MassProperties mp = box.VolumeMassProperties();
      std::printf("ERROR: expected throw for inward-flipped box, got volume=%.6f\n", mp.volume);
    } catch (const std::invalid_argument& e) {
      std::printf("OK: inward-flipped box throws invalid_argument: %s\n", e.what());
    }
  }

  return 0;
}
