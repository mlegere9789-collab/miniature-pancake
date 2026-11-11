// NurbsSurface::Classify(), AnalyticSurfacePoint(), AnalyticClosestPoint(): the typed analytic surface (plane, sphere,
// cylinder, cone, torus) behind a NURBS surface, with closed-form evaluation and inversion. See surface.h for the
// parameter conventions.
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "dino8/kernel/surface.h"

namespace dino8::kernel {

SurfaceClassification NurbsSurface::Classify(double tolerance) const {
  SurfaceClassification out;
  const ON_NurbsSurface& s = surface_;
  if (!s.IsValid()) return out;
  const double tol = (tolerance > 0.0 && ON_IsValid(tolerance)) ? tolerance : ON_ZERO_TOLERANCE;

  ON_Plane plane;
  if (s.IsPlanar(&plane, tol) && plane.IsValid()) {
    out.kind = SurfaceKind::Plane;
    out.plane = plane;
    return out;
  }
  ON_Sphere sphere;
  if (s.IsSphere(&sphere, tol) && sphere.IsValid()) {
    out.kind = SurfaceKind::Sphere;
    out.sphere = sphere;
    return out;
  }
  ON_Cylinder cylinder;
  if (s.IsCylinder(&cylinder, tol) && cylinder.circle.IsValid()) {
    out.kind = SurfaceKind::Cylinder;
    out.cylinder = cylinder;
    return out;
  }
  ON_Cone cone;
  if (s.IsCone(&cone, tol) && cone.plane.IsValid() && cone.radius != 0.0 && cone.height != 0.0) {
    out.kind = SurfaceKind::Cone;
    out.cone = cone;
    return out;
  }
  ON_Torus torus;
  if (s.IsTorus(&torus, tol) && torus.plane.IsValid()) {
    out.kind = SurfaceKind::Torus;
    out.torus = torus;
    return out;
  }
  return out;
}

namespace {

double Angle(const ON_Plane& f, const Vector3d& d) { return std::atan2(ON_DotProduct(d, f.yaxis), ON_DotProduct(d, f.xaxis)); }

// Unit vector, or false when `v` is too short to have a direction (relative to `scale`).
bool UnitOf(Vector3d v, double scale, Vector3d& unit) {
  const double len = v.Length();
  if (!(len > 1e-13 * std::max(1.0, scale))) return false;
  unit = v / len;
  return true;
}

}  // namespace

Point3d AnalyticSurfacePoint(const SurfaceClassification& c, double u, double v) {
  const double cu = std::cos(u), su = std::sin(u);
  switch (c.kind) {
    case SurfaceKind::Plane: return c.plane.origin + u * c.plane.xaxis + v * c.plane.yaxis;
    case SurfaceKind::Sphere: {
      const ON_Plane& f = c.sphere.plane;
      return f.origin + c.sphere.radius * (std::cos(v) * (cu * f.xaxis + su * f.yaxis) + std::sin(v) * f.zaxis);
    }
    case SurfaceKind::Cylinder: {
      const ON_Plane& f = c.cylinder.circle.plane;
      return f.origin + c.cylinder.circle.radius * (cu * f.xaxis + su * f.yaxis) + v * f.zaxis;
    }
    case SurfaceKind::Cone: {
      const ON_Plane& f = c.cone.plane;
      return f.origin + v * f.zaxis + (v * c.cone.radius / c.cone.height) * (cu * f.xaxis + su * f.yaxis);
    }
    case SurfaceKind::Torus: {
      const ON_Plane& f = c.torus.plane;
      const Vector3d rad = cu * f.xaxis + su * f.yaxis;
      return f.origin + (c.torus.major_radius + c.torus.minor_radius * std::cos(v)) * rad + c.torus.minor_radius * std::sin(v) * f.zaxis;
    }
    case SurfaceKind::General: break;
  }
  throw std::invalid_argument("dino8::kernel::AnalyticSurfacePoint: a General classification has no closed form");
}

bool AnalyticClosestPoint(const SurfaceClassification& c, Point3d p, AnalyticFoot& out) {
  AnalyticFoot r;
  switch (c.kind) {
    case SurfaceKind::Plane: {
      const ON_Plane& f = c.plane;
      const Vector3d w = p - f.origin;
      r.u = ON_DotProduct(w, f.xaxis);
      r.v = ON_DotProduct(w, f.yaxis);
      r.normal = f.zaxis;
      r.signed_distance = ON_DotProduct(w, f.zaxis);
      r.point = p - r.signed_distance * f.zaxis;
      break;
    }
    case SurfaceKind::Sphere: {
      const ON_Plane& f = c.sphere.plane;
      Vector3d n;
      if (!UnitOf(p - f.origin, c.sphere.radius, n)) return false;
      r.normal = n;
      r.point = f.origin + c.sphere.radius * n;
      r.signed_distance = (p - f.origin).Length() - c.sphere.radius;
      r.v = std::asin(std::max(-1.0, std::min(1.0, ON_DotProduct(n, f.zaxis))));
      r.u = Angle(f, n);
      break;
    }
    case SurfaceKind::Cylinder: {
      const ON_Plane& f = c.cylinder.circle.plane;
      const Vector3d w = p - f.origin;
      const double h = ON_DotProduct(w, f.zaxis);
      const Vector3d radial = w - h * f.zaxis;
      Vector3d n;
      if (!UnitOf(radial, c.cylinder.circle.radius, n)) return false;
      r.normal = n;
      r.point = f.origin + c.cylinder.circle.radius * n + h * f.zaxis;
      r.signed_distance = radial.Length() - c.cylinder.circle.radius;
      r.u = Angle(f, n);
      r.v = h;
      break;
    }
    case SurfaceKind::Cone: {
      const ON_Plane& f = c.cone.plane;
      const double H = c.cone.height, R = std::fabs(c.cone.radius), L = std::hypot(R, H);
      const double g_rho = R / L, g_h = H / L;  // generatrix direction in the (radius, axis) half plane
      const Vector3d w = p - f.origin;
      const double h = ON_DotProduct(w, f.zaxis);
      const Vector3d radial = w - h * f.zaxis;
      const double rho = radial.Length();
      const double s = rho * g_rho + h * g_h;  // distance along the generatrix ray from the apex
      Vector3d nr;
      const bool has_dir = UnitOf(radial, L, nr);
      if (s <= 0.0) {  // behind the apex: the apex itself is closest on the single nappe
        Vector3d away;
        if (!UnitOf(w, L, away)) return false;
        r.point = f.origin;
        r.normal = away;
        r.signed_distance = w.Length();
        r.u = has_dir ? Angle(f, nr) : 0.0;
        r.v = 0.0;
        break;
      }
      if (!has_dir) return false;  // on the axis inside the nappe: every generatrix is equally close
      const double sg = H > 0.0 ? 1.0 : -1.0;
      const double n_rho = sg * g_h, n_h = -sg * g_rho;  // outward normal in the half plane
      r.normal = n_rho * nr + n_h * f.zaxis;
      r.point = f.origin + s * (g_rho * nr + g_h * f.zaxis);
      r.signed_distance = rho * n_rho + h * n_h;
      r.u = Angle(f, nr);
      r.v = s * g_h;
      break;
    }
    case SurfaceKind::Torus: {
      const ON_Plane& f = c.torus.plane;
      const double R = c.torus.major_radius, rr = c.torus.minor_radius;
      const Vector3d w = p - f.origin;
      const double h = ON_DotProduct(w, f.zaxis);
      const Vector3d radial = w - h * f.zaxis;
      Vector3d nr, m;
      if (!UnitOf(radial, R, nr)) return false;
      const Point3d ring = f.origin + R * nr;
      if (!UnitOf(p - ring, rr, m)) return false;
      r.normal = m;
      r.point = ring + rr * m;
      r.signed_distance = (p - ring).Length() - rr;
      r.u = Angle(f, nr);
      r.v = std::atan2(ON_DotProduct(m, f.zaxis), ON_DotProduct(m, nr));
      break;
    }
    case SurfaceKind::General: return false;
  }
  out = r;
  return true;
}

}  // namespace dino8::kernel
