#include <cstdio>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"

using namespace dino8::kernel;

// Diagnostic: report IsClosedManifold(), boundary-edge count, non-manifold
// (3+ face) edge count, and print up to a handful of the offending edges'
// own 3D positions plus which Brep edge (by index, matched via nearest
// midpoint) they sit closest to - to confirm/refute the "dense polyline
// edge, adjacent faces sample it differently" T-junction hypothesis.
static void DiagnoseManifold(const char* label, const Brep& brep, const Mesh& mesh) {
  const ON_Mesh& m = mesh.raw();
  std::map<std::pair<int, int>, int> undirected_count;
  for (int i = 0; i < m.m_F.Count(); ++i) {
    const ON_MeshFace& f = m.m_F[i];
    auto visit = [&](int a, int b) { ++undirected_count[std::minmax(a, b)]; };
    visit(f.vi[0], f.vi[1]);
    visit(f.vi[1], f.vi[2]);
    if (f.IsQuad()) {
      visit(f.vi[2], f.vi[3]);
      visit(f.vi[3], f.vi[0]);
    } else {
      visit(f.vi[2], f.vi[0]);
    }
  }
  int boundary = 0, nonmanifold = 0;
  int printed_boundary = 0, printed_nonmanifold = 0;
  for (const auto& [edge, count] : undirected_count) {
    if (count == 1) {
      ++boundary;
      if (printed_boundary < 5) {
        const ON_3fPoint& a = m.m_V[edge.first];
        const ON_3fPoint& b = m.m_V[edge.second];
        printf("  [%s] BOUNDARY edge v%d-v%d  (%.6f,%.6f,%.6f)-(%.6f,%.6f,%.6f)\n", label,
               edge.first, edge.second, a.x, a.y, a.z, b.x, b.y, b.z);
        ++printed_boundary;
      }
    } else if (count > 2) {
      ++nonmanifold;
      if (printed_nonmanifold < 5) {
        const ON_3fPoint& a = m.m_V[edge.first];
        const ON_3fPoint& b = m.m_V[edge.second];
        printf("  [%s] NONMANIFOLD(%d) edge v%d-v%d  (%.6f,%.6f,%.6f)-(%.6f,%.6f,%.6f)\n", label,
               count, edge.first, edge.second, a.x, a.y, a.z, b.x, b.y, b.z);
        ++printed_nonmanifold;
      }
    }
  }
  printf("[%s] IsClosedManifold=%d boundary_edges=%d nonmanifold_edges=%d (total undirected edges=%zu) faces=%d verts=%d\n",
         label, (int)mesh.IsClosedManifold(), boundary, nonmanifold, undirected_count.size(),
         m.m_F.Count(), m.m_V.Count());
}

// Closed finite cylinder, axis along +z from z0 to z1, built via the
// kernel's own proven CylindricalFace + FromMixedFaces() path. A bare
// CylindricalFace alone (FromMixedFaces({}, {cf})) is DELIBERATELY an open
// tube with no end caps (see boolean.cpp's RayVsMixedFace/BuildEndCap doc
// comments: a bare-cylinder boolean *operand* normally gets its end
// material from the other operand, so caps are synthesized situationally
// inside boolean.cpp itself, not by FromMixedFaces()). For a genuinely
// standalone, watertight solid cylinder we instead supply two explicit
// disk PlanarFace caps alongside the CylindricalFace, each one a fine
// polygon sampled at the exact same angles as the cylinder's own rim and
// marked via notch_begin/notch_count as one true circular arc coincident
// with that rim - the documented mechanism (see PlanarFace's own doc
// comment in brep.h) for welding a planar cap to a cylindrical face's edge
// into one real shared ON_BrepEdge instead of two merely-touching pieces.
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
    pf.loop.pop_back();  // closed polygon: don't repeat the seam point
    pf.notch_begin = 0;
    pf.notch_count = static_cast<int>(pf.loop.size());
    return pf;
  };
  Brep::PlanarFace cap0 = make_cap(z0, /*flip=*/true);   // bottom: outward normal -z
  Brep::PlanarFace cap1 = make_cap(z1, /*flip=*/false);  // top: outward normal +z
  return Brep::FromMixedFaces({cap0, cap1}, {cf});
}

int main() {
  ON::Begin();
  {
    Brep cyl = MakeCylinderZ(0, 0, 0, 5, 1.0);
    Mesh m = cyl.TessellateToClosedMesh(8, 32);
    printf("lone cylinder: faces=%d volume=%f (expect %f) valid=%d\n", cyl.FaceCount(), m.Volume(),
           ON_PI * 1.0 * 1.0 * 5.0, (int)cyl.raw().IsValid());
  }
  Brep a = Brep::Box(0,0,0, 2,2,2);
  Brep b = Brep::Box(1,1,1, 3,3,3);

  for (BooleanOp op : {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference}) {
    const char* name = op==BooleanOp::Union?"Union":op==BooleanOp::Intersection?"Intersection":"Difference";
    try {
      Brep r = BooleanCombineGeneral(a, b, op);
      Mesh m = r.TessellateToClosedMesh(8,8);
      double vol = m.Volume();
      bool valid = r.raw().IsValid();
      printf("box+box %s: faces=%d volume=%f valid=%d\n", name, r.FaceCount(), vol, (int)valid);
      DiagnoseManifold((std::string("box+box ") + name).c_str(), r, m);
      Mesh mc = r.TessellateToClosedMeshConforming(8, 8);
      DiagnoseManifold((std::string("box+box(conforming) ") + name).c_str(), r, mc);
      Mesh mf = TessellateGeneralBooleanClosedMesh(r, 8, 8);
      printf("box+box %s FIXED volume=%f\n", name, mf.Volume());
      DiagnoseManifold((std::string("box+box(FIXED) ") + name).c_str(), r, mf);
    } catch (const std::exception& e) {
      printf("box+box %s: EXCEPTION %s\n", name, e.what());
    }
  }

  // Plane+cylinder validation: a 4x4x2 box (volume 32) fully pierced by a
  // radius-1 cylinder along its own z-axis, taller than the box on both
  // ends (so the cylinder passes all the way through). Closed-form:
  // intersection = pi*r^2*box_height = 2*pi; union = 32 + 4*pi - 2*pi =
  // 32 + 2*pi; difference (box - cyl) = 32 - 2*pi. This is a case the
  // existing special-cased BooleanCombineMixed engine already handles
  // exactly (plane+cylinder) - matching it here is the required proof
  // the general pipeline reproduces already-known-correct results.
  {
    Brep box = Brep::Box(-2, -2, -1, 2, 2, 1);
    Brep cyl = MakeCylinderZ(0, 0, -2, 2, 1.0);
    const double box_h = 2.0, r = 1.0;
    const double expect_i = ON_PI * r * r * box_h;
    const double expect_u = 32.0 + 4.0 * ON_PI * r * r - expect_i;
    const double expect_d = 32.0 - expect_i;
    for (BooleanOp op : {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference}) {
      const char* name = op==BooleanOp::Union?"Union":op==BooleanOp::Intersection?"Intersection":"Difference";
      const double expect = op==BooleanOp::Union?expect_u:op==BooleanOp::Intersection?expect_i:expect_d;
      try {
        Brep r = BooleanCombineGeneral(box, cyl, op);
        Mesh m = r.TessellateToClosedMesh(8,32);
        double vol = m.Volume();
        bool valid = r.raw().IsValid();
        printf("box+cylinder %s: faces=%d volume=%f (expect %f) valid=%d\n", name, r.FaceCount(), vol, expect, (int)valid);
        DiagnoseManifold((std::string("box+cylinder ") + name).c_str(), r, m);
        Mesh mc = r.TessellateToClosedMeshConforming(8, 32);
        DiagnoseManifold((std::string("box+cylinder(conforming) ") + name).c_str(), r, mc);
        Mesh mf = TessellateGeneralBooleanClosedMesh(r, 8, 32);
        printf("box+cylinder %s FIXED volume=%f\n", name, mf.Volume());
        DiagnoseManifold((std::string("box+cylinder(FIXED) ") + name).c_str(), r, mf);
      } catch (const std::exception& e) {
        printf("box+cylinder %s: EXCEPTION %s\n", name, e.what());
      }
    }
  }

  // General-only case: sphere+box. The existing special-cased engine has
  // NO sphere support at all - this is the proof the generalization is
  // real, not just a reproduction of an already-solved case. Sphere
  // radius 2 at the origin, intersected with a box that fully contains
  // the positive octant and extends well past the sphere everywhere else:
  // the intersection is exactly one octant of the sphere, closed form
  // (4/3*pi*r^3)/8 = (4/3)*pi for r=2.
  {
    Brep sphere = Brep::Sphere(Point3d(0, 0, 0), 2.0);
    Brep box = Brep::Box(0, 0, 0, 10, 10, 10);
    const double r = 2.0;
    const double expect_i = (4.0 / 3.0) * ON_PI * r * r * r / 8.0;
    for (BooleanOp op : {BooleanOp::Union, BooleanOp::Intersection, BooleanOp::Difference}) {
      const char* name = op==BooleanOp::Union?"Union":op==BooleanOp::Intersection?"Intersection":"Difference";
      try {
        Brep r2 = BooleanCombineGeneral(sphere, box, op);
        Mesh m = r2.TessellateToClosedMesh(16,32);
        double vol = m.Volume();
        bool valid = r2.raw().IsValid();
        if (op == BooleanOp::Intersection) {
          printf("sphere+box %s: faces=%d volume=%f (expect %f) valid=%d\n", name, r2.FaceCount(), vol, expect_i, (int)valid);
        } else {
          printf("sphere+box %s: faces=%d volume=%f valid=%d\n", name, r2.FaceCount(), vol, (int)valid);
        }
        DiagnoseManifold((std::string("sphere+box ") + name).c_str(), r2, m);
        {
          Mesh mc = r2.TessellateToClosedMeshConforming(16, 32);
          DiagnoseManifold((std::string("sphere+box(conforming) ") + name).c_str(), r2, mc);
          Mesh mf = TessellateGeneralBooleanClosedMesh(r2, 16, 32);
          printf("sphere+box %s FIXED volume=%f\n", name, mf.Volume());
          DiagnoseManifold((std::string("sphere+box(FIXED) ") + name).c_str(), r2, mf);
        }
      } catch (const std::exception& e) {
        printf("sphere+box %s: EXCEPTION %s\n", name, e.what());
      }
    }
  }
  return 0;
}
