// Independent-oracle checks of Brep::TessellateToClosedMeshTolerant() on sealed solids with curved faces: a full sphere,
// a full torus (closed analytic faces) and the two general-boolean solids box - cylinder and box & cylinder, whose
// trimmed cylindrical wall and 128-gon planar caps share a circle sampled at different angles.
//
// Before TessellateToClosedMeshTolerant() fell back to TessellateGeneralBooleanClosedMesh(), the two cylinder solids came
// out OPEN at every resolution (the planar cap polygon vertices and the trimmed wall's circle samples lie on the same
// circle a sagitta apart, so no weld tolerance and no edge split closes them). The oracles are independent of the
// tessellator - analytic distance functions of the true surfaces, read straight from the ON_Mesh arrays:
//   (1) the mesh is a closed manifold;
//   (2) every vertex is a true surface point (<= 1e-6);
//   (3) every edge midpoint and triangle centroid is within the chord bound of the nearest true surface:
//         bound = 1.6 x (largest radius of curvature direction) x (1 - cos(pi / n))   for n x n divisions;
//   (4) the volume is within (surface area x measured worst deviation) of the closed form, and the deviation shrinks
//       roughly 4x per doubling of n (checked as: strictly decreasing, and at least 2.5x from n = 8 to n = 16).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneral;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

using Distance = std::function<double(const ON_3dPoint&)>;

// A z-axis cylinder through (cx, cy) from z0 to z1 with 128-gon planar caps (the fixture of test_basic.cpp's
// MakeCylinderZForBoxCylinderTest).
Brep MakeCylinderZ(double cx, double cy, double z0, double z1, double r) {
  Brep::CylindricalFace cf;
  cf.frame = ON_Plane(ON_3dPoint(cx, cy, z0), ON_3dVector(1, 0, 0), ON_3dVector(0, 1, 0));
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = z1 - z0;
  const int n = 128;
  auto make_cap = [&](double z, bool flip) {
    Brep::PlanarFace pf;
    pf.plane = ON_Plane(ON_3dPoint(cx, cy, z), ON_3dVector(0, 0, flip ? -1 : 1));
    for (int i = 0; i <= n; ++i) {
      const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
      pf.loop.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a), z);
    }
    pf.loop.pop_back();
    pf.notch_begin = 0;
    pf.notch_count = static_cast<int>(pf.loop.size());
    return pf;
  };
  return Brep::FromMixedFaces({make_cap(z0, true), make_cap(z1, false)}, {cf});
}

struct Measured {
  double worst_vertex = 0.0;
  double worst_midpoint = 0.0;
  double worst_centroid = 0.0;
};

Measured Measure(const Mesh& m, const Distance& dist) {
  Measured out;
  const ON_Mesh& raw = m.raw();
  for (int i = 0; i < raw.m_V.Count(); ++i) out.worst_vertex = std::max(out.worst_vertex, dist(ON_3dPoint(raw.m_V[i])));
  for (int i = 0; i < raw.m_F.Count(); ++i) {
    const ON_MeshFace& f = raw.m_F[i];
    const int corners = f.IsQuad() ? 4 : 3;
    ON_3dPoint centroid(0, 0, 0);
    for (int k = 0; k < corners; ++k) {
      const ON_3dPoint a(raw.m_V[f.vi[k]]);
      const ON_3dPoint b(raw.m_V[f.vi[(k + 1) % corners]]);
      out.worst_midpoint = std::max(out.worst_midpoint, dist(0.5 * (a + b)));
      centroid += a;
    }
    out.worst_centroid = std::max(out.worst_centroid, dist(centroid / static_cast<double>(corners)));
  }
  return out;
}

// Runs the four oracle properties for `solid` at 8, 16 and 32 divisions. `curvature_radius` is the radius entering the
// chord bound (the sphere radius; R + r for the torus; the cylinder radius for the drilled/cut box).
void CheckSolid(const char* name, const Brep& solid, const Distance& dist, double curvature_radius, double exact_volume,
                double surface_area) {
  std::vector<double> worst;
  for (const int n : {8, 16, 32}) {
    const Mesh m = solid.TessellateToClosedMeshTolerant(n, n);
    const Measured q = Measure(m, dist);
    const double bound = 1.6 * curvature_radius * (1.0 - std::cos(ON_PI / n));
    const double err = m.Volume() - exact_volume;
    const double dev = std::max(q.worst_midpoint, q.worst_centroid);
    char what[512];
    std::snprintf(what, sizeof what, "%s tolerant tessellation at %dx%d: closed manifold (%d vertices, %d faces)", name, n, n,
                  m.VertexCount(), m.FaceCount());
    d8test::Check(m.IsClosedManifold(), what);
    std::snprintf(what, sizeof what, "%s at %dx%d: every vertex is a true surface point (worst %.2e <= 1e-6)", name, n, n,
                  q.worst_vertex);
    d8test::Check(q.worst_vertex <= 1e-6, what);
    std::snprintf(what, sizeof what,
                  "%s at %dx%d: every edge midpoint (worst %.2e) and triangle centroid (worst %.2e) is within the chord bound %.2e",
                  name, n, n, q.worst_midpoint, q.worst_centroid, bound);
    d8test::Check(dev <= bound, what);
    std::snprintf(what, sizeof what, "%s at %dx%d: volume error %+.3e is within surface area x worst deviation (%.3e)", name, n,
                  n, err, surface_area * dev);
    d8test::Check(std::fabs(err) <= surface_area * dev, what);
    worst.push_back(dev);
  }
  char what[512];
  std::snprintf(what, sizeof what, "%s: the worst deviation shrinks with the divisions (%.2e, %.2e, %.2e; at least 2.5x from 8 to 16)",
                name, worst[0], worst[1], worst[2]);
  d8test::Check(worst[1] < worst[0] && worst[2] < worst[1] && worst[0] >= 2.5 * worst[1], what);
}

double Plane(double v, double c) { return std::fabs(v - c); }

}  // namespace

D8_EXTRA_TEST(TolerantSealedSphereAndTorusChordOracle) {
  const double r = 2.0;
  CheckSolid("sphere r=2", Brep::Sphere(Point3d(0, 0, 0), r),
             [&](const ON_3dPoint& p) { return std::fabs(p.DistanceTo(ON_3dPoint(0, 0, 0)) - r); }, r, 4.0 / 3.0 * ON_PI * r * r * r,
             4.0 * ON_PI * r * r);
  const double R = 3.0, rr = 1.0;
  CheckSolid("torus R=3 r=1", Brep::Torus(Point3d(0, 0, 0), Vector3d(0, 0, 1), R, rr),
             [&](const ON_3dPoint& p) { return std::fabs(std::hypot(std::hypot(p.x, p.y) - R, p.z) - rr); }, R + rr,
             2.0 * ON_PI * ON_PI * R * rr * rr, 4.0 * ON_PI * ON_PI * R * rr);
}

// Box [-1,1]^3 minus / and a r = 0.5 z-axis cylinder through it.
D8_EXTRA_TEST(TolerantSealedBoxCylinderBooleansChordOracle) {
  const double r = 0.5;
  const Brep box = Brep::Box(-1, -1, -1, 1, 1, 1);
  const Brep cyl = MakeCylinderZ(0, 0, -2, 2, r);
  const Distance dist = [&](const ON_3dPoint& p) {
    double d = std::fabs(std::hypot(p.x, p.y) - r);
    for (const double c : {-1.0, 1.0}) d = std::min(d, std::min(Plane(p.x, c), std::min(Plane(p.y, c), Plane(p.z, c))));
    return d;
  };
  const double through = ON_PI * r * r * 2.0;  // the cylinder's part inside the box
  const double wall = 2.0 * ON_PI * r * 2.0;    // its wall area inside the box
  CheckSolid("box - cylinder", BooleanCombineGeneral(box, cyl, BooleanOp::Difference), dist, r, 8.0 - through, 24.0 + wall);
  CheckSolid("box & cylinder", BooleanCombineGeneral(box, cyl, BooleanOp::Intersection), dist, r, through, wall + 2.0 * ON_PI * r * r);
}
