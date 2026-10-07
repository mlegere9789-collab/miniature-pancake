#include <cstdio>
#include <cmath>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Probe: a meridian plane (contains the sphere's own polar axis) against
  // a full sphere - the SSX result is a genuine great circle that passes
  // THROUGH both poles, continuing on the opposite longitude on the far
  // side of each pole. Does IntersectSurfaces() produce a sane (u, v)
  // pcurve on the sphere across a pole crossing, or does the raw cubic fit
  // swing through a bogus intermediate azimuth the way it used to for a
  // periodic seam crossing before SplitAtSeams/SeamCrossing existed?
  const double r = 2.0;
  const ON_Sphere on_sphere(ON_3dPoint(0, 0, 0), r);
  ON_NurbsSurface sphere_nurbs;
  std::printf("GetNurbForm rc=%d\n", on_sphere.GetNurbForm(sphere_nurbs));
  NurbsSurface sphere_surface;
  sphere_surface.raw() = sphere_nurbs;

  const auto du = sphere_surface.Domain(0);
  const auto dv = sphere_surface.Domain(1);
  std::printf("u domain: [%f, %f]  closed(0)=%d\n", du.min, du.max, sphere_surface.raw().IsClosed(0));
  std::printf("v domain: [%f, %f]  closed(1)=%d\n", dv.min, dv.max, sphere_surface.raw().IsClosed(1));
  std::printf("IsSingular(0,v=min)=%d IsSingular(2,v=max)=%d\n", sphere_surface.raw().IsSingular(0), sphere_surface.raw().IsSingular(2));

  // A meridian plane through the z-axis, deliberately tilted OFF the
  // sphere's own u=0 seam (so we isolate the pole degeneracy from the
  // already-separately-handled seam degeneracy) - normal (sin37, -cos37,0).
  const double theta = 0.37;
  ON_PlaneSurface plane_surface(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(std::sin(theta), -std::cos(theta), 0)));
  plane_surface.SetExtents(0, ON_Interval(-10, 10), true);
  plane_surface.SetExtents(1, ON_Interval(-10, 10), true);

  IntersectOptions opt;
  const auto ma = TessellateWithUV(plane_surface, opt);
  const auto mb = TessellateWithUV(sphere_surface.raw(), opt);
  std::printf("plane mesh pts=%zu bbox valid=%d min=(%f,%f,%f) max=(%f,%f,%f)\n", ma.pts.size(), ma.bbox.IsValid(),
              ma.bbox.Min().x, ma.bbox.Min().y, ma.bbox.Min().z, ma.bbox.Max().x, ma.bbox.Max().y, ma.bbox.Max().z);
  std::printf("sphere mesh pts=%zu bbox valid=%d min=(%f,%f,%f) max=(%f,%f,%f)\n", mb.pts.size(), mb.bbox.IsValid(),
              mb.bbox.Min().x, mb.bbox.Min().y, mb.bbox.Min().z, mb.bbox.Max().x, mb.bbox.Max().y, mb.bbox.Max().z);

  const auto curves = IntersectSurfaces(plane_surface, sphere_surface.raw(), opt);
  std::printf("curve count: %zu\n", curves.size());
  for (size_t ci = 0; ci < curves.size(); ++ci) {
    const auto& c = curves[ci];
    std::printf("curve %zu: closed=%d points=%zu\n", ci, c.closed, c.points.size());
    double max_u_jump = 0;
    for (size_t i = 0; i + 1 < c.uv_b.size(); ++i) {
      const double du_ = std::fabs(c.uv_b[i].x - c.uv_b[i + 1].x);
      max_u_jump = std::max(max_u_jump, du_);
    }
    std::printf("  max consecutive-sample u(sphere) jump: %f (domain length %f)\n", max_u_jump, du.max - du.min);
    // Sample the fitted pcurve_b (sphere uv) at many interior parameters
    // and re-evaluate the sphere there, comparing to the fitted 3D curve
    // at the same parameter - a bogus swing shows up as a large deviation.
    double max_dev = 0;
    for (int k = 0; k <= 200; ++k) {
      const double t = c.params.front() + (c.params.back() - c.params.front()) * k / 200.0;
      const ON_3dPoint p3 = c.curve.PointAt(t);
      const ON_3dPoint uv = c.pcurve_b.PointAt(t);
      const ON_3dPoint p_on_sphere = sphere_surface.raw().PointAt(uv.x, uv.y);
      const double dev = p3.DistanceTo(p_on_sphere);
      max_dev = std::max(max_dev, dev);
    }
    std::printf("  max |fitted 3D curve - sphere(pcurve_b)| over params: %f (r=%f)\n", max_dev, r);
    for (size_t i = 0; i < c.points.size(); ++i) {
      std::printf("   [%zu] p=(%.4f,%.4f,%.4f) uv_b=(%.4f,%.4f)\n", i, c.points[i].x, c.points[i].y, c.points[i].z, c.uv_b[i].x, c.uv_b[i].y);
    }
  }

  // Second probe: a plane through a cone's own axis AND apex - the apex
  // is a genuine single-point pole (IsSingular at one v end only, unlike
  // a sphere's two).
  std::printf("\n--- cone apex probe ---\n");
  const ON_Cone on_cone(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 4.0, 1.5);
  ON_NurbsSurface cone_nurbs;
  std::printf("Cone GetNurbForm rc=%d\n", on_cone.GetNurbForm(cone_nurbs));
  NurbsSurface cone_surface;
  cone_surface.raw() = cone_nurbs;
  const auto cdu = cone_surface.Domain(0);
  const auto cdv = cone_surface.Domain(1);
  std::printf("cone u domain: [%f, %f]  v domain: [%f, %f]\n", cdu.min, cdu.max, cdv.min, cdv.max);
  std::printf("cone IsSingular(0)=%d (1)=%d (2)=%d (3)=%d\n", cone_surface.raw().IsSingular(0), cone_surface.raw().IsSingular(1), cone_surface.raw().IsSingular(2), cone_surface.raw().IsSingular(3));

  ON_PlaneSurface cone_plane_surface(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(std::sin(theta), -std::cos(theta), 0)));
  cone_plane_surface.SetExtents(0, ON_Interval(-20, 20), true);
  cone_plane_surface.SetExtents(1, ON_Interval(-20, 20), true);
  const auto cone_curves = IntersectSurfaces(cone_plane_surface, cone_surface.raw(), opt);
  std::printf("cone curve count: %zu\n", cone_curves.size());
  for (size_t ci = 0; ci < cone_curves.size(); ++ci) {
    const auto& c = cone_curves[ci];
    std::printf("cone curve %zu: closed=%d points=%zu\n", ci, c.closed, c.points.size());
    double max_dev = 0;
    for (int k = 0; k <= 200; ++k) {
      const double t = c.params.front() + (c.params.back() - c.params.front()) * k / 200.0;
      const ON_3dPoint p3 = c.curve.PointAt(t);
      const ON_3dPoint uv = c.pcurve_b.PointAt(t);
      const ON_3dPoint p_on_cone = cone_surface.raw().PointAt(uv.x, uv.y);
      max_dev = std::max(max_dev, p3.DistanceTo(p_on_cone));
    }
    std::printf("  max |fitted 3D curve - cone(pcurve_b)|: %f\n", max_dev);
    for (size_t i = 0; i < c.points.size(); ++i) {
      std::printf("   [%zu] p=(%.4f,%.4f,%.4f) uv_b=(%.4f,%.4f)\n", i, c.points[i].x, c.points[i].y, c.points[i].z, c.uv_b[i].x, c.uv_b[i].y);
    }
  }
  return 0;
}
