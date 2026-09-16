#include <cstdio>
#include <cmath>
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/boolean.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"

using namespace dino8::kernel;

// Closed finite cylinder, axis along +z from z0 to z1, built as 3
// independently-built faces (wall untrimmed periodic tube via
// ON_Cylinder::GetNurbForm through NurbsSurface's raw() escape hatch,
// two disk caps via TrimmedPlanarFace) Compound()ed together - no shared
// topology between them is needed since BooleanCombineGeneral only reads
// each face's own trim independently.
Brep MakeCylinderZ(double cx, double cy, double z0, double z1, double r) {
  ON_Circle circle(ON_Plane(ON_3dPoint(cx, cy, 0), ON_3dVector(0, 0, 1)), r);
  ON_Cylinder cyl(circle);
  cyl.height[0] = z0;
  cyl.height[1] = z1;
  ON_NurbsSurface wall_ns;
  if (cyl.GetNurbForm(wall_ns) == 0) throw std::runtime_error("cylinder GetNurbForm failed");
  NurbsSurface wall;
  wall.raw() = wall_ns;
  Brep wall_brep = Brep::FromSurface(wall);

  auto make_cap = [&](double z, bool flip) {
    const double R = r * 1.5;
    std::vector<Point3d> grid = {
        Point3d(cx - R, cy - R, z), Point3d(cx - R, cy + R, z),
        Point3d(cx + R, cy - R, z), Point3d(cx + R, cy + R, z)};
    NurbsSurface plane = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
    std::vector<Point2d> loop;
    const int n = 64;
    for (int i = 0; i < n; ++i) {
      double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
      const double x = cx + r * std::cos(a), y = cy + r * std::sin(a);
      loop.emplace_back((x - (cx - R)) / (2 * R), (y - (cy - R)) / (2 * R));
    }
    return Brep::TrimmedPlanarFace(plane, loop);
  };
  Brep cap0 = make_cap(z0, /*flip=*/true);   // bottom: outward normal -z, so reverse winding
  Brep cap1 = make_cap(z1, /*flip=*/false);  // top: outward normal +z
  return Brep::Compound({wall_brep, cap0, cap1});
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
      printf("%s: faces=%d volume=%f valid=%d\n", name, r.FaceCount(), vol, (int)valid);
    } catch (const std::exception& e) {
      printf("%s: EXCEPTION %s\n", name, e.what());
    }
  }
  return 0;
}
