// dimlinear3dm_write_check - reads a .3dm file (expected to be written by Dino 8's Save3dm) directly through OpenNURBS' own
// ONX_Model/ON_DimLinear API, independent of Dino 8's own Load3dm reader, and reports every real ON_DimLinear object plus
// the number of loose ON_Curve objects in the file.
//
// The write-side mirror of the ON_DimLinear half of Load3dm: a DimLinear/DimAligned Dino 8 made and saved must come back
// as a real ON_DimLinear to a plain OpenNURBS reader, not merely to Dino 8's own Open command (which also accepts the
// baked extension-line/dimension-line/arrow/glyph-curve fallback and never notices the difference). The loose-curve count
// is the negative control: a dimension written as a real ON_DimLinear must not ALSO leave its baked curves in the file.
//
// Output, one line per dimension:
//   dimlinear N: type=Aligned|Rotated p0=(x,y,z) p1=(x,y,z) dimline=(x,y,z) length=L
// where the three points are the plane-local definition points mapped to world coordinates and L is the plain Euclidean
// distance of p0 and p1 computed here (not read from OpenNURBS), then a final summary line.
#include <opennurbs.h>

#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <file.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path, &log)) {
    std::fprintf(stderr, "dimlinear3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  int dim_count = 0, curve_count = 0, geometry_count = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    ++geometry_count;
    const ON_Geometry* g = mg->Geometry(nullptr);
    if (ON_Curve::Cast(g)) ++curve_count;
    const ON_DimLinear* dim = ON_DimLinear::Cast(g);
    if (!dim) continue;
    ++dim_count;
    const ON_Plane& plane = dim->Plane();
    const ON_2dPoint a = dim->DefPoint1(), b = dim->DefPoint2(), d = dim->DimlinePoint();
    const ON_3dPoint p0 = plane.PointAt(a.x, a.y), p1 = plane.PointAt(b.x, b.y), dl = plane.PointAt(d.x, d.y);
    const double len = std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y) + (p1.z - p0.z) * (p1.z - p0.z));
    std::printf("dimlinear %d: type=%s p0=(%.6g,%.6g,%.6g) p1=(%.6g,%.6g,%.6g) dimline=(%.6g,%.6g,%.6g) length=%.6g\n", dim_count,
                dim->Type() == ON::AnnotationType::Aligned ? "Aligned" : "Rotated", p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, dl.x, dl.y, dl.z, len);
  }
  std::printf("dimlinear3dm_write_check: %d real ON_DimLinear object(s), %d loose curve(s), %d geometry object(s) in %s\n", dim_count,
              curve_count, geometry_count, path);
  return 0;
}
