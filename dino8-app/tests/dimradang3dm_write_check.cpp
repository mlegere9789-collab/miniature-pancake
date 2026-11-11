// dimradang3dm_write_check - reads a .3dm file (expected to be written by Dino 8's Save3dm) directly through OpenNURBS' own
// ONX_Model API, independent of Dino 8's own Load3dm reader, and reports every real ON_DimRadial and ON_DimAngular object plus
// the number of loose ON_Curve objects in the file (the negative control: a dimension written for real must not also leave
// its baked curves behind).
//
// One line per dimension:
//   dimradial N: type=Radius|Diameter center=(x,y,z) radiuspt=(x,y,z) dimline=(x,y,z) radius=R
//   dimangular N: vertex=(x,y,z) leg1=(x,y,z) leg2=(x,y,z) angle=A
// R is the plain Euclidean center-to-radius-point distance and A the angle in degrees between the two legs, both computed
// here from the world points (not read from OpenNURBS), then a final summary line.
#include <opennurbs.h>

#include <cmath>
#include <cstdio>

// Printed coordinates are snapped to 0 below 1e-9 (the file stores plane-local floats, so an exact 0 comes back as ~1e-15).
static ON_3dPoint Snap(ON_3dPoint p) {
  auto z = [](double v) { return std::fabs(v) < 1e-9 ? 0.0 : v; };
  return ON_3dPoint(z(p.x), z(p.y), z(p.z));
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <file.3dm>\n", argv[0]);
    return 1;
  }
  const char* path = argv[1];

  ONX_Model model;
  ON_TextLog log;
  if (!model.Read(path, &log)) {
    std::fprintf(stderr, "dimradang3dm_write_check: OpenNURBS could not read %s\n", path);
    return 1;
  }

  int radial = 0, angular = 0, curves = 0, geometry = 0;
  ONX_ModelComponentIterator it(model, ON_ModelComponent::Type::ModelGeometry);
  for (const ON_ModelComponent* c = it.FirstComponent(); c; c = it.NextComponent()) {
    const ON_ModelGeometryComponent* mg = ON_ModelGeometryComponent::Cast(c);
    if (!mg) continue;
    ++geometry;
    const ON_Geometry* g = mg->Geometry(nullptr);
    if (ON_Curve::Cast(g)) ++curves;
    if (const ON_DimRadial* d = ON_DimRadial::Cast(g)) {
      ++radial;
      const ON_Plane& pl = d->Plane();
      const ON_2dPoint c2 = d->CenterPoint(), r2 = d->RadiusPoint(), l2 = d->DimlinePoint();
      const ON_3dPoint cp = Snap(pl.PointAt(c2.x, c2.y)), rp = Snap(pl.PointAt(r2.x, r2.y)), lp = Snap(pl.PointAt(l2.x, l2.y));
      const double rad = std::sqrt((rp.x - cp.x) * (rp.x - cp.x) + (rp.y - cp.y) * (rp.y - cp.y) + (rp.z - cp.z) * (rp.z - cp.z));
      std::printf("dimradial %d: type=%s center=(%.6g,%.6g,%.6g) radiuspt=(%.6g,%.6g,%.6g) dimline=(%.6g,%.6g,%.6g) radius=%.6g\n", radial,
                  d->Type() == ON::AnnotationType::Diameter ? "Diameter" : "Radius", cp.x, cp.y, cp.z, rp.x, rp.y, rp.z, lp.x, lp.y, lp.z, rad);
    }
    if (const ON_DimAngular* d = ON_DimAngular::Cast(g)) {
      ++angular;
      const ON_Plane& pl = d->Plane();
      const ON_2dPoint v2 = d->CenterPoint(), a2 = d->DefPoint1(), b2 = d->DefPoint2();
      const ON_3dPoint v = Snap(pl.PointAt(v2.x, v2.y)), a = Snap(pl.PointAt(a2.x, a2.y)), b = Snap(pl.PointAt(b2.x, b2.y));
      const double ux = a.x - v.x, uy = a.y - v.y, uz = a.z - v.z, wx = b.x - v.x, wy = b.y - v.y, wz = b.z - v.z;
      const double dot = ux * wx + uy * wy + uz * wz;
      const double lu = std::sqrt(ux * ux + uy * uy + uz * uz), lw = std::sqrt(wx * wx + wy * wy + wz * wz);
      const double ang = (lu > 0 && lw > 0) ? std::acos(std::fmax(-1.0, std::fmin(1.0, dot / (lu * lw)))) * 180.0 / 3.14159265358979323846 : -1.0;
      std::printf("dimangular %d: vertex=(%.6g,%.6g,%.6g) leg1=(%.6g,%.6g,%.6g) leg2=(%.6g,%.6g,%.6g) angle=%.6g\n", angular, v.x, v.y, v.z, a.x,
                  a.y, a.z, b.x, b.y, b.z, ang);
    }
  }
  std::printf("dimradang3dm_write_check: %d real ON_DimRadial, %d real ON_DimAngular, %d loose curve(s), %d geometry object(s) in %s\n", radial,
              angular, curves, geometry, path);
  return 0;
}
