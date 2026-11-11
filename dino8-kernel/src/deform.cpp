#include "dino8/kernel/deform.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace dino8::kernel {

namespace {

bool Finite(const Point3d& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

Point3d Rotated(const Point3d& a, const Vector3d& u, const Point3d& p, double angle) {
  const Vector3d rel = p - a;
  const double c = std::cos(angle), s = std::sin(angle);
  const Vector3d along = u * ON_DotProduct(u, rel);
  const Vector3d perp = rel - along;
  return a + along + perp * c + ON_CrossProduct(u, perp) * s;
}

Point3d CheckedMap(const PointMap& map, const Point3d& p) {
  const Point3d q = map(p);
  if (!Finite(q)) throw std::invalid_argument("dino8::kernel::Deform: the point map returned a non-finite point");
  return q;
}

// Distinct knot values strictly inside (lo, hi), plus the two ends: the span boundaries of a clamped knot vector.
template <class KnotAt>
std::vector<double> SpanBreaks(int knot_count, KnotAt knot, double lo, double hi) {
  std::vector<double> b = {lo};
  for (int i = 0; i < knot_count; ++i) {
    const double k = knot(i);
    if (k > b.back() + 1e-12 * std::max(1.0, hi - lo) && k < hi - 1e-12 * std::max(1.0, hi - lo)) b.push_back(k);
  }
  b.push_back(hi);
  return b;
}

}  // namespace

PointMap MakeTwist(Point3d axis_start, Point3d axis_end, double angle_radians, bool infinite) {
  Vector3d u = axis_end - axis_start;
  const double len = u.Length();
  if (!(len > 1e-12) || !std::isfinite(angle_radians)) {
    throw std::invalid_argument("dino8::kernel::MakeTwist: the axis must have non-zero length and the angle must be finite");
  }
  u.Unitize();
  return [=](Point3d p) {
    const double t = ON_DotProduct(p - axis_start, u) / len;
    return Rotated(axis_start, u, p, angle_radians * (infinite ? t : std::clamp(t, 0.0, 1.0)));
  };
}

void BendRadiusAndAngle(Point3d spine_start, Point3d spine_end, Point3d through, double& radius, double& angle_radians) {
  Vector3d u = spine_end - spine_start;
  if (!(u.Length() > 1e-12)) throw std::invalid_argument("dino8::kernel::MakeBend: the spine must have non-zero length");
  u.Unitize();
  const Vector3d d = through - spine_start;
  const double along = ON_DotProduct(d, u);
  const double dn = (d - u * along).Length();
  if (!(dn > 1e-12) || !(along > 1e-12)) {
    throw std::invalid_argument("dino8::kernel::MakeBend: the bend point must lie off the spine and ahead of its start");
  }
  radius = d.LengthSquared() / (2.0 * dn);
  angle_radians = 2.0 * std::atan2(dn, along);
}

PointMap MakeBend(Point3d spine_start, Point3d spine_end, Point3d through) {
  double radius = 0.0, theta = 0.0;
  BendRadiusAndAngle(spine_start, spine_end, through, radius, theta);
  Vector3d u = spine_end - spine_start;
  const double len = u.Length();
  u.Unitize();
  const Vector3d d = through - spine_start;
  Vector3d n = d - u * ON_DotProduct(d, u);
  n.Unitize();
  const Point3d a = spine_start;
  return [=](Point3d p) {
    const Vector3d rel = p - a;
    const double s = ON_DotProduct(rel, u), h = ON_DotProduct(rel, n);
    if (s <= 0.0) return p;
    const Vector3d other = rel - u * s - n * h;
    const double phi = theta * std::min(s, len) / len;
    const Point3d center = a + n * radius;
    Point3d bent = center + (u * std::sin(phi) - n * std::cos(phi)) * (radius - h) + other;
    if (s > len) bent += (u * std::cos(theta) + n * std::sin(theta)) * (s - len);
    return bent;
  };
}

PointMap MakeTaper(Point3d axis_start, Point3d axis_end, double d0, double d1, bool infinite) {
  Vector3d u = axis_end - axis_start;
  const double len = u.Length();
  if (!(len > 1e-12) || !(d0 > 0.0) || !std::isfinite(d1)) {
    throw std::invalid_argument("dino8::kernel::MakeTaper: the axis must have non-zero length, d0 > 0 and d1 finite");
  }
  u.Unitize();
  return [=](Point3d p) {
    const Vector3d rel = p - axis_start;
    const double s = ON_DotProduct(rel, u);
    const double t = infinite ? s / len : std::clamp(s / len, 0.0, 1.0);
    const double f = ((1.0 - t) * d0 + t * d1) / d0;
    return axis_start + u * s + (rel - u * s) * f;
  };
}

Mesh DeformMesh(const Mesh& mesh, const PointMap& map) {
  Mesh out = mesh;
  ON_Mesh& raw = out.raw();
  for (int i = 0; i < raw.m_V.Count(); ++i) {
    const ON_3fPoint& v = raw.m_V[i];
    const Point3d q = CheckedMap(map, Point3d(v.x, v.y, v.z));
    raw.m_V[i] = ON_3fPoint(static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z));
  }
  raw.m_N.Destroy();
  raw.m_FN.Destroy();
  return out;
}

Result DeformSurface(const NurbsSurface& surface, const PointMap& map, double tolerance, NurbsSurface& out, DeformReport* report,
                     int max_refinements) {
  if (!(tolerance > 0.0) || !std::isfinite(tolerance)) {
    throw std::invalid_argument("dino8::kernel::DeformSurface: tolerance must be finite and positive");
  }
  if (max_refinements < 0) throw std::invalid_argument("dino8::kernel::DeformSurface: max_refinements must be non-negative");
  const Interval du = surface.Domain(0), dv = surface.Domain(1);
  NurbsSurface refined = surface;  // same geometry, finer knots
  double best = 1e300;
  for (int round = 0; round <= max_refinements; ++round) {
    // the control net mapped point by point (rational: the Euclidean position is mapped, the weight kept)
    NurbsSurface mapped = refined;
    ON_NurbsSurface& m = mapped.raw();
    const bool rational = m.IsRational();
    for (int i = 0; i < m.CVCount(0); ++i) {
      for (int j = 0; j < m.CVCount(1); ++j) {
        ON_4dPoint cv;
        m.GetCV(i, j, cv);
        const double w = rational ? cv.w : 1.0;
        const Point3d q = CheckedMap(map, Point3d(cv.x / w, cv.y / w, cv.z / w));
        if (rational) m.SetCV(i, j, ON_4dPoint(q.x * w, q.y * w, q.z * w, w));
        else m.SetCV(i, j, ON_3dPoint(q.x, q.y, q.z));
      }
    }
    // verification: three interior samples per knot span per direction
    const ON_NurbsSurface& r = refined.raw();
    const std::vector<double> ub = SpanBreaks(r.KnotCount(0), [&](int k) { return r.Knot(0, k); }, du.min, du.max);
    const std::vector<double> vb = SpanBreaks(r.KnotCount(1), [&](int k) { return r.Knot(1, k); }, dv.min, dv.max);
    double worst = 0.0;
    for (size_t a = 0; a + 1 < ub.size(); ++a) {
      for (int ka = 1; ka <= 3; ++ka) {
        const double u = ub[a] + (ub[a + 1] - ub[a]) * ka / 4.0;
        for (size_t b = 0; b + 1 < vb.size(); ++b) {
          for (int kb = 1; kb <= 3; ++kb) {
            const double v = vb[b] + (vb[b + 1] - vb[b]) * kb / 4.0;
            const Point3d want = CheckedMap(map, surface.PointAt(u, v));
            worst = std::max(worst, want.DistanceTo(mapped.PointAt(u, v)));
          }
        }
      }
    }
    best = std::min(best, worst);
    if (report) {
      report->refinements = round;
      report->achieved_deviation = worst;
      report->u_count = mapped.CVCountU();
      report->v_count = mapped.CVCountV();
    }
    if (worst <= tolerance) {
      out = mapped;
      return Result::Ok;
    }
    if (round == max_refinements) break;
    // refine: a knot at the middle of every span, both directions
    for (size_t a = 0; a + 1 < ub.size(); ++a) refined.InsertKnotAt(0, 0.5 * (ub[a] + ub[a + 1]));
    for (size_t b = 0; b + 1 < vb.size(); ++b) refined.InsertKnotAt(1, 0.5 * (vb[b] + vb[b + 1]));
  }
  if (report) report->achieved_deviation = best;
  return Result::Failed;
}

Result DeformCurve(const NurbsCurve& curve, const PointMap& map, double tolerance, NurbsCurve& out, DeformReport* report,
                   int max_refinements) {
  if (!(tolerance > 0.0) || !std::isfinite(tolerance)) {
    throw std::invalid_argument("dino8::kernel::DeformCurve: tolerance must be finite and positive");
  }
  if (max_refinements < 0) throw std::invalid_argument("dino8::kernel::DeformCurve: max_refinements must be non-negative");
  const Interval d = curve.Domain();
  NurbsCurve refined = curve;
  double best = 1e300;
  for (int round = 0; round <= max_refinements; ++round) {
    NurbsCurve mapped = refined;
    ON_NurbsCurve& m = mapped.raw();
    const bool rational = m.IsRational();
    for (int i = 0; i < m.CVCount(); ++i) {
      ON_4dPoint cv;
      m.GetCV(i, cv);
      const double w = rational ? cv.w : 1.0;
      const Point3d q = CheckedMap(map, Point3d(cv.x / w, cv.y / w, cv.z / w));
      if (rational) m.SetCV(i, ON_4dPoint(q.x * w, q.y * w, q.z * w, w));
      else m.SetCV(i, ON_3dPoint(q.x, q.y, q.z));
    }
    const ON_NurbsCurve& r = refined.raw();
    const std::vector<double> b = SpanBreaks(r.KnotCount(), [&](int k) { return r.Knot(k); }, d.min, d.max);
    double worst = 0.0;
    for (size_t a = 0; a + 1 < b.size(); ++a) {
      for (int k = 1; k <= 3; ++k) {
        const double t = b[a] + (b[a + 1] - b[a]) * k / 4.0;
        worst = std::max(worst, CheckedMap(map, curve.PointAt(t)).DistanceTo(mapped.PointAt(t)));
      }
    }
    best = std::min(best, worst);
    if (report) {
      report->refinements = round;
      report->achieved_deviation = worst;
      report->u_count = mapped.ControlPointCount();
      report->v_count = 0;
    }
    if (worst <= tolerance) {
      out = mapped;
      return Result::Ok;
    }
    if (round == max_refinements) break;
    for (size_t a = 0; a + 1 < b.size(); ++a) refined.InsertKnotAt(0.5 * (b[a] + b[a + 1]));
  }
  if (report) report->achieved_deviation = best;
  return Result::Failed;
}

}  // namespace dino8::kernel
