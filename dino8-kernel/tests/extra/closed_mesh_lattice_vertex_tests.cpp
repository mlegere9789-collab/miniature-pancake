// A concave flat face whose reflex corner is a node of the tessellation lattice (NurbsSurface::TessellateGridClippedExact
// nudges such a trim vertex off the grid line) must still weld into a closed mesh with its neighbours.
//
// The flat-faced L prism  (0,0) (a,0) (a,w) (w,w) (w,a) (0,a)  x H, built by FromPlanarFaces, with w = a/2 and an even number
// of divisions has its reflex corner (w,w) ON a lattice node. The nudge was a fixed fraction (1e-6) of a CELL, i.e. 2.5e-6 for a
// = 20 and 8 divisions - above the weld tolerance (1e-6) - so the cap's corner no longer welded to the walls' exact copy and
// TessellateToClosedMesh(8, 8) left 100 open edges (measured; closed at (24, 24)). Before the fix 23 of the 630 prisms
// (a = 10..30, 4..10 divisions, w = a/4..3a/4 in steps of a/40) were open, all at w = a/2 and an even division count.
//
// Oracles: Mesh::IsClosedManifold(); the closed form  mesh volume = (a*w + w*(a-w)) * H  (float mesh: 1e-5 relative); the mesh
// vertex nearest the exact corner (w, w, 0) (the nudge must not move it more than float resolution).
#include <cmath>
#include <string>
#include <vector>

#include <opennurbs.h>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using d8test::Check;

Brep LPrism(double a, double w, double H) {
  const std::vector<Point3d> P = {Point3d(0, 0, 0), Point3d(a, 0, 0), Point3d(a, w, 0), Point3d(w, w, 0), Point3d(w, a, 0), Point3d(0, a, 0)};
  std::vector<Brep::PlanarFace> fs;
  auto add = [&](std::vector<Point3d> loop, ON_3dVector n) {
    Brep::PlanarFace f;
    f.loop = loop;
    f.plane = ON_Plane(loop[0], n);
    fs.push_back(f);
  };
  add(std::vector<Point3d>(P.rbegin(), P.rend()), ON_3dVector(0, 0, -1));
  std::vector<Point3d> top = P;
  for (auto& p : top) p.z = H;
  add(top, ON_3dVector(0, 0, 1));
  for (size_t k = 0; k < P.size(); ++k) {
    const Point3d p = P[k], q = P[(k + 1) % P.size()];
    ON_3dVector n(q.y - p.y, -(q.x - p.x), 0);
    n.Unitize();
    add({p, q, Point3d(q.x, q.y, H), Point3d(p.x, p.y, H)}, n);
  }
  return Brep::FromPlanarFaces(fs);
}

std::string Tag(double a, double w, int div) { return "L prism a=" + std::to_string(a) + " w=" + std::to_string(w) + " at (" + std::to_string(div) + ", " + std::to_string(div) + ")"; }

}  // namespace

D8_EXTRA_TEST(ClosedMeshOfLPrismArmWidth5To15) {
  // The family named in the task: a = 20, arm width 5 .. 15 in steps of 0.5, at the (8, 8) the volume gate uses.
  int open = 0, wrong_volume = 0, total = 0;
  std::string first_open;
  for (int k = 0; k <= 20; ++k) {
    const double w = 5.0 + 0.5 * k;
    const Mesh m = LPrism(20.0, w, 8.0).TessellateToClosedMesh(8, 8);
    ++total;
    if (!m.IsClosedManifold()) {
      ++open;
      if (first_open.empty()) first_open = "w=" + std::to_string(w);
      continue;
    }
    const double expect = (20.0 * w + w * (20.0 - w)) * 8.0;
    if (std::fabs(std::fabs(m.Volume()) - expect) > 1e-5 * expect) ++wrong_volume;
  }
  Check(total == 21, "21 L prisms (a = 20, arm width 5, 5.5 .. 15)");
  Check(open == 0, ("every one is a closed manifold at (8, 8); open: " + std::to_string(open) + " (first " + first_open + ")").c_str());
  Check(wrong_volume == 0, "...and the closed mesh's volume equals (a*w + w*(a-w))*H to 1e-5 relative");
}

D8_EXTRA_TEST(ClosedMeshOfSymmetricLPrismAtEvenDivisions) {
  // The reflex corner (a/2, a/2) is a lattice node for every even division count.
  for (double a : {10.0, 12.0, 16.0, 20.0, 24.0, 30.0}) {
    for (int div : {4, 6, 8, 10}) {
      const double w = 0.5 * a;
      const Mesh m = LPrism(a, w, 8.0).TessellateToClosedMesh(div, div);
      const bool closed = m.IsClosedManifold();
      Check(closed, (Tag(a, w, div) + ": closed manifold").c_str());
      if (!closed) continue;
      const double expect = (a * w + w * (a - w)) * 8.0;
      Check(std::fabs(std::fabs(m.Volume()) - expect) <= 1e-5 * expect, (Tag(a, w, div) + ": mesh volume equals the closed form").c_str());
    }
  }
}

D8_EXTRA_TEST(LatticeNodeCornerIsNotMovedByTheGridNudge) {
  // The nudge exists only so the clipper has no vertex exactly on a grid line; it must not move the corner in space.
  const double a = 20.0, w = 10.0;
  const Mesh m = LPrism(a, w, 8.0).TessellateToClosedMesh(8, 8);
  double nearest = 1e300;
  for (int i = 0; i < m.raw().m_V.Count(); ++i) {
    const ON_3fPoint v = m.raw().m_V[i];
    nearest = std::min(nearest, std::hypot(std::hypot(v.x - w, v.y - w), v.z - 0.0));
  }
  Check(nearest < 5e-7, ("a mesh vertex sits within 5e-7 of the exact reflex corner (10, 10, 0); nearest " + std::to_string(nearest)).c_str());
}
