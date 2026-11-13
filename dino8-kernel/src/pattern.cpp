#include "dino8/kernel/pattern.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace dino8::kernel {

CurveArrayFrame ArrayFrameOnCurve(const NurbsCurve& path, double t, Vector3d up) {
  CurveArrayFrame f;
  f.origin = path.PointAt(t);
  Vector3d tan = path.TangentAt(t);
  if (!tan.Unitize()) tan = ON_xaxis;
  Vector3d n = ON_CrossProduct(up, tan);
  if (!n.Unitize()) {
    n = ON_CrossProduct(ON_zaxis, tan);
    if (!n.Unitize()) n = ON_CrossProduct(ON_yaxis, tan);
    n.Unitize();
  }
  Vector3d b = ON_CrossProduct(tan, n);
  b.Unitize();
  f.tangent = tan;
  f.normal = n;
  f.binormal = b;
  return f;
}

std::vector<ON_Xform> ArrayAlongCurveTransforms(const NurbsCurve& path, int count, bool orient, Vector3d up) {
  if (count < 2) {
    throw std::invalid_argument("dino8::kernel::ArrayAlongCurveTransforms: count must be at least 2 (got " + std::to_string(count) + ")");
  }
  if (!path.raw().IsValid()) throw std::invalid_argument("dino8::kernel::ArrayAlongCurveTransforms: the path curve is not valid");
  const std::vector<double> params = path.DivideByCount(count - 1);
  if (static_cast<int>(params.size()) != count) {
    throw std::invalid_argument("dino8::kernel::ArrayAlongCurveTransforms: the path could not be divided into " + std::to_string(count - 1) +
                                " equal arc-length parts");
  }
  const CurveArrayFrame f0 = ArrayFrameOnCurve(path, params.front(), up);
  const ON_Plane p0(f0.origin, f0.tangent, f0.normal);
  std::vector<ON_Xform> out;
  out.reserve(params.size());
  for (size_t i = 0; i < params.size(); ++i) {
    if (i == 0) {
      out.push_back(ON_Xform::IdentityTransformation);
      continue;
    }
    if (orient) {
      const CurveArrayFrame fi = ArrayFrameOnCurve(path, params[i], up);
      ON_Xform xf;
      xf.Rotation(p0, ON_Plane(fi.origin, fi.tangent, fi.normal));
      out.push_back(xf);
    } else {
      out.push_back(ON_Xform::TranslationTransformation(path.PointAt(params[i]) - f0.origin));
    }
  }
  return out;
}

std::vector<Brep> ArrayBrepAlongCurve(const Brep& body, const NurbsCurve& path, int count, bool orient, Vector3d up) {
  const std::vector<ON_Xform> maps = ArrayAlongCurveTransforms(path, count, orient, up);
  std::vector<Brep> out;
  out.reserve(maps.size());
  for (const ON_Xform& xf : maps) out.push_back(body.Transform(xf));
  return out;
}

SurfaceArrayFrame ArrayFrameOnSurface(const NurbsSurface& surface, double u, double v) {
  SurfaceArrayFrame f;
  ON_3dPoint p;
  ON_3dVector du, dv;
  if (!surface.raw().Ev1Der(u, v, p, du, dv)) {
    p = surface.raw().PointAt(u, v);
    du = ON_3dVector::ZeroVector;
    dv = ON_3dVector::ZeroVector;
  }
  f.origin = p;
  Vector3d n = ON_CrossProduct(du, dv);
  if (!n.Unitize()) n = ON_zaxis;
  Vector3d x = du;
  // Su is orthogonal to Su x Sv, so x is already in the tangent plane; a degenerate Su (or the world-z fallback normal) is repaired
  // by projecting the world x axis (then y) into the plane orthogonal to the normal.
  if (!x.Unitize() || std::fabs(ON_DotProduct(x, n)) > 1e-9) {
    x = ON_xaxis - n * ON_DotProduct(ON_xaxis, n);
    if (!x.Unitize()) {
      x = ON_yaxis - n * ON_DotProduct(ON_yaxis, n);
      x.Unitize();
    }
  }
  Vector3d y = ON_CrossProduct(n, x);
  y.Unitize();
  f.u_dir = x;
  f.v_dir = y;
  f.normal = n;
  return f;
}

std::vector<Point2d> ArrayLatticeOnSurface(const NurbsSurface& surface, int nu, int nv) {
  if (nu < 1 || nv < 1) {
    throw std::invalid_argument("dino8::kernel::ArrayLatticeOnSurface: nu and nv must be at least 1 (got " + std::to_string(nu) + " x " +
                                std::to_string(nv) + ")");
  }
  const Interval du = surface.Domain(0), dv = surface.Domain(1);
  std::vector<Point2d> out;
  out.reserve(static_cast<size_t>(nu) * static_cast<size_t>(nv));
  for (int i = 0; i < nu; ++i) {
    const double fu = nu == 1 ? 0.5 : static_cast<double>(i) / (nu - 1);
    for (int j = 0; j < nv; ++j) {
      const double fv = nv == 1 ? 0.5 : static_cast<double>(j) / (nv - 1);
      out.emplace_back(du.min + (du.max - du.min) * fu, dv.min + (dv.max - dv.min) * fv);
    }
  }
  return out;
}

std::vector<ON_Xform> ArrayOnSurfaceTransforms(const NurbsSurface& surface, int nu, int nv, const ON_Plane& reference, bool orient) {
  if (!surface.raw().IsValid()) throw std::invalid_argument("dino8::kernel::ArrayOnSurfaceTransforms: the surface is not valid");
  const std::vector<Point2d> lattice = ArrayLatticeOnSurface(surface, nu, nv);
  std::vector<ON_Xform> out;
  out.reserve(lattice.size());
  for (const Point2d& uv : lattice) {
    const SurfaceArrayFrame f = ArrayFrameOnSurface(surface, uv.x, uv.y);
    if (orient) {
      ON_Xform xf;
      xf.Rotation(reference, ON_Plane(f.origin, f.u_dir, f.v_dir));
      out.push_back(xf);
    } else {
      out.push_back(ON_Xform::TranslationTransformation(f.origin - reference.origin));
    }
  }
  return out;
}

std::vector<Brep> ArrayBrepOnSurface(const Brep& body, const NurbsSurface& surface, int nu, int nv, const ON_Plane& reference, bool orient) {
  const std::vector<ON_Xform> maps = ArrayOnSurfaceTransforms(surface, nu, nv, reference, orient);
  std::vector<Brep> out;
  out.reserve(maps.size());
  for (const ON_Xform& xf : maps) out.push_back(body.Transform(xf));
  return out;
}

ON_Plane SurfaceNormalFrameAt(const NurbsSurface& surface, Point3d near_point, Point2d* uv) {
  if (!surface.raw().IsValid()) throw std::invalid_argument("dino8::kernel::SurfaceNormalFrameAt: the surface is not valid");
  const Point2d p = surface.ClosestPointParameter(near_point, 40, 40);
  if (uv) *uv = p;
  return ON_Plane(surface.PointAt(p.x, p.y), surface.NormalAt(p.x, p.y));
}

ON_Xform OrientOnSurfaceTransform(const NurbsSurface& surface, const ON_Plane& from, Point3d near_point) {
  ON_Xform xf;
  xf.Rotation(from, SurfaceNormalFrameAt(surface, near_point));
  return xf;
}

Brep OrientBrepOnSurface(const Brep& body, const NurbsSurface& surface, const ON_Plane& from, Point3d near_point) {
  return body.Transform(OrientOnSurfaceTransform(surface, from, near_point));
}

namespace {

// Length of the isoparametric curve of `s` in direction `dir` (0: u runs, v = `fixed`; 1: v runs, u = `fixed`) between the parameters a and b:
// composite 5-point Gauss-Legendre of |dS/dt| over 96 panels (smooth inside a knot span, C1 across the knots of the rational circles).
double IsoArcLength(const ON_Surface& s, int dir, double fixed, double a, double b) {
  static const double x[5] = {0.0, -0.5384693101056831, 0.5384693101056831, -0.9061798459386640, 0.9061798459386640};
  static const double w[5] = {0.5688888888888889, 0.4786286704993665, 0.4786286704993665, 0.2369268850561891, 0.2369268850561891};
  if (a == b) return 0.0;
  const int panels = 96;
  const double h = (b - a) / panels;
  double sum = 0;
  for (int k = 0; k < panels; ++k) {
    const double mid = a + (k + 0.5) * h;
    for (int i = 0; i < 5; ++i) {
      const double t = mid + 0.5 * h * x[i];
      ON_3dPoint pt;
      ON_3dVector du, dv;
      if (dir == 0) s.Ev1Der(t, fixed, pt, du, dv); else s.Ev1Der(fixed, t, pt, du, dv);
      sum += w[i] * (dir == 0 ? du.Length() : dv.Length()) * 0.5 * h;
    }
  }
  return std::fabs(sum);
}

// The parameter t in [lo, hi] at which the iso arc length from lo reaches `fraction` of the whole, by safeguarded Newton on the arc length.
// A collapsed iso curve (a pole: zero length) falls back to the parameter fraction.
double IsoParamAtArcFraction(const ON_Surface& s, int dir, double fixed, double lo, double hi, double fraction) {
  const double total = IsoArcLength(s, dir, fixed, lo, hi);
  if (!(total > 1e-12)) return lo + (hi - lo) * fraction;
  const double want = std::min(1.0, std::max(0.0, fraction)) * total;
  double a = lo, b = hi, t = lo + (hi - lo) * fraction;
  for (int it = 0; it < 60; ++it) {
    const double f = IsoArcLength(s, dir, fixed, lo, t) - want;
    if (std::fabs(f) < 1e-13 * total) break;
    (f < 0 ? a : b) = t;
    ON_3dPoint pt;
    ON_3dVector du, dv;
    if (dir == 0) s.Ev1Der(t, fixed, pt, du, dv); else s.Ev1Der(fixed, t, pt, du, dv);
    const double speed = dir == 0 ? du.Length() : dv.Length();
    double nt = speed > 1e-12 ? t - f / speed : 0.5 * (a + b);
    if (!(nt > a && nt < b)) nt = 0.5 * (a + b);
    t = nt;
  }
  return t;
}

}  // namespace

Point3d FlowPointAlongSurface(const NurbsSurface& base, const NurbsSurface& target, Point3d p) {
  if (!base.raw().IsValid() || !target.raw().IsValid()) throw std::invalid_argument("dino8::kernel::FlowPointAlongSurface: a surface is not valid");
  const Interval bu = base.Domain(0), bv = base.Domain(1), tu = target.Domain(0), tv = target.Domain(1);
  const Point2d uv = base.ClosestPointParameter(p, 40, 40);
  const Point3d on = base.PointAt(uv.x, uv.y);
  const Vector3d n = base.NormalAt(uv.x, uv.y);
  const double d = ON_DotProduct(p - on, n);
  // ARC-LENGTH fractions: how far along the base's u isocurve through the point (v fixed) and along its v isocurve (u fixed) the point is, as
  // fractions of those isocurves' lengths, are reproduced on the target's isocurves. The two target parameters are coupled (each isocurve
  // is taken at the other's value), so they are iterated from the domain fractions; for a separable pair (plane / cylinder) the first pass is
  // exact.
  const ON_Surface& bs = base.raw();
  const ON_Surface& ts = target.raw();
  const double lu = IsoArcLength(bs, 0, uv.y, bu.min, bu.max), lv = IsoArcLength(bs, 1, uv.x, bv.min, bv.max);
  const double fu = lu > 1e-12 ? IsoArcLength(bs, 0, uv.y, bu.min, uv.x) / lu : (uv.x - bu.min) / (bu.max - bu.min);
  const double fv = lv > 1e-12 ? IsoArcLength(bs, 1, uv.x, bv.min, uv.y) / lv : (uv.y - bv.min) / (bv.max - bv.min);
  double u2 = tu.min + (tu.max - tu.min) * ((uv.x - bu.min) / (bu.max - bu.min));
  double v2 = tv.min + (tv.max - tv.min) * ((uv.y - bv.min) / (bv.max - bv.min));
  for (int pass = 0; pass < 4; ++pass) {
    const double nu = IsoParamAtArcFraction(ts, 0, v2, tu.min, tu.max, fu);
    const double nv = IsoParamAtArcFraction(ts, 1, nu, tv.min, tv.max, fv);
    const bool settled = std::fabs(nu - u2) < 1e-13 && std::fabs(nv - v2) < 1e-13;
    u2 = nu;
    v2 = nv;
    if (settled) break;
  }
  return target.PointAt(u2, v2) + target.NormalAt(u2, v2) * d;
}

}  // namespace dino8::kernel
