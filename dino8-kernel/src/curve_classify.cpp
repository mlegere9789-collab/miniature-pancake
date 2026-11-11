// NurbsCurve::Classify() / NurbsCurve::Segments(): the typed (line / arc / circle / ellipse / polyline / general)
// view of a NURBS curve and its composite decomposition. See curve.h for the contract.
#include <algorithm>
#include <cmath>
#include <vector>

#include "dino8/kernel/curve.h"

namespace dino8::kernel {

namespace {

// Solves the n x n system A x = b in place (Gaussian elimination, partial pivoting); false when singular.
bool SolveSmall(std::vector<double>& A, std::vector<double>& b, int n) {
  for (int col = 0; col < n; ++col) {
    int piv = col;
    for (int r = col + 1; r < n; ++r) {
      if (std::fabs(A[static_cast<size_t>(r * n + col)]) > std::fabs(A[static_cast<size_t>(piv * n + col)])) piv = r;
    }
    if (!(std::fabs(A[static_cast<size_t>(piv * n + col)]) > 1e-14)) return false;
    if (piv != col) {
      for (int k = 0; k < n; ++k) std::swap(A[static_cast<size_t>(col * n + k)], A[static_cast<size_t>(piv * n + k)]);
      std::swap(b[static_cast<size_t>(col)], b[static_cast<size_t>(piv)]);
    }
    for (int r = col + 1; r < n; ++r) {
      const double f = A[static_cast<size_t>(r * n + col)] / A[static_cast<size_t>(col * n + col)];
      for (int k = col; k < n; ++k) A[static_cast<size_t>(r * n + k)] -= f * A[static_cast<size_t>(col * n + k)];
      b[static_cast<size_t>(r)] -= f * b[static_cast<size_t>(col)];
    }
  }
  for (int r = n - 1; r >= 0; --r) {
    double v = b[static_cast<size_t>(r)];
    for (int k = r + 1; k < n; ++k) v -= A[static_cast<size_t>(r * n + k)] * b[static_cast<size_t>(k)];
    b[static_cast<size_t>(r)] = v / A[static_cast<size_t>(r * n + r)];
  }
  return true;
}

// Is the closed curve `c` a full ellipse within `tol`? Fits the plane through three samples, the conic
// A x^2 + B x y + C y^2 + D x + E y = 1 (least squares over 32 samples, coordinates centred on their mean), reads the
// centre and axes off it, and only accepts when 256 other samples all lie within `tol` of that ellipse (first-order
// distance |f| / |grad f|, exact to second order for the sub-tolerance deviations that matter here).
bool FitEllipse(const ON_NurbsCurve& c, double tol, ON_Ellipse& out) {
  const ON_Interval dom = c.Domain();
  auto at = [&](double u) { return c.PointAt(dom.ParameterAt(u)); };
  const int N = 32;
  std::vector<ON_3dPoint> pts;
  ON_3dPoint mean(0, 0, 0);
  for (int i = 0; i < N; ++i) {
    pts.push_back(at(static_cast<double>(i) / N));
    mean += ON_3dVector(pts.back());
  }
  mean = ON_3dPoint(mean.x / N, mean.y / N, mean.z / N);
  ON_3dVector normal = ON_CrossProduct(at(1.0 / 3.0) - at(0.0), at(2.0 / 3.0) - at(0.0));
  if (!normal.Unitize()) return false;
  ON_3dVector ex = pts[0] - mean;
  ex = ex - ON_DotProduct(ex, normal) * normal;
  if (!ex.Unitize()) return false;
  const ON_3dVector ey = ON_CrossProduct(normal, ex);
  auto local = [&](const ON_3dPoint& p, double& x, double& y, double& z) {
    const ON_3dVector d = p - mean;
    x = ON_DotProduct(d, ex);
    y = ON_DotProduct(d, ey);
    z = ON_DotProduct(d, normal);
  };

  std::vector<double> M(25, 0.0), rhs(5, 0.0);
  for (const ON_3dPoint& p : pts) {
    double x, y, z;
    local(p, x, y, z);
    const double row[5] = {x * x, x * y, y * y, x, y};
    for (int i = 0; i < 5; ++i) {
      rhs[static_cast<size_t>(i)] += row[i];
      for (int j = 0; j < 5; ++j) M[static_cast<size_t>(i * 5 + j)] += row[i] * row[j];
    }
  }
  if (!SolveSmall(M, rhs, 5)) return false;
  const double A = rhs[0], B = rhs[1], C = rhs[2], D = rhs[3], E = rhs[4];

  // Centre: grad = 0  ->  [2A B; B 2C] (cx, cy) = -(D, E).
  const double det = 4.0 * A * C - B * B;
  if (!(std::fabs(det) > 1e-14)) return false;
  const double cx = (-D * 2.0 * C + E * B) / det, cy = (-E * 2.0 * A + D * B) / det;
  // Value of the conic form at the centre: f(c) = (D cx + E cy) / 2 - 1 (the constant term is -1), so about the centre
  // A u^2 + B u v + C v^2 = 1 - (D cx + E cy) / 2 =: g.
  const double g = 1.0 - 0.5 * (D * cx + E * cy);
  // Principal axes of [A B/2; B/2 C]: angle th with tan(2 th) = B / (A - C).
  const double th = 0.5 * std::atan2(B, A - C);
  const double ct = std::cos(th), st = std::sin(th);
  const double l0 = A * ct * ct + B * ct * st + C * st * st;
  const double l1 = A * st * st - B * ct * st + C * ct * ct;
  if (!(l0 * g > 0.0) || !(l1 * g > 0.0)) return false;  // not an ellipse (hyperbola or empty)
  double r0 = std::sqrt(g / l0), r1 = std::sqrt(g / l1);
  ON_3dVector ax = ct * ex + st * ey, ay = -st * ex + ct * ey;
  if (r1 > r0) {
    std::swap(r0, r1);
    const ON_3dVector t = ax;
    ax = ay;
    ay = -t;
  }
  const ON_3dPoint centre = mean + cx * ex + cy * ey;

  // Traced direction decides the sign of the plane normal (counter-clockwise about it).
  double area = 0.0;
  for (int i = 0; i < N; ++i) {
    const ON_3dVector p = pts[static_cast<size_t>(i)] - centre, q = pts[static_cast<size_t>((i + 1) % N)] - centre;
    area += ON_DotProduct(ON_CrossProduct(p, q), normal);
  }
  if (area < 0.0) ay = -ay;
  const ON_Plane plane(centre, ax, ay);
  if (!plane.IsValid()) return false;

  const int V = 256;
  for (int i = 0; i < V; ++i) {
    const ON_3dVector d = at((i + 0.5) / V) - centre;
    const double x = ON_DotProduct(d, plane.xaxis), y = ON_DotProduct(d, plane.yaxis), z = ON_DotProduct(d, plane.zaxis);
    const double f = (x * x) / (r0 * r0) + (y * y) / (r1 * r1) - 1.0;
    const double gl = 2.0 * std::sqrt(x * x / std::pow(r0, 4) + y * y / std::pow(r1, 4));
    if (!(gl > 0.0) || std::fabs(f) / gl > tol || std::fabs(z) > tol) return false;
  }
  out = ON_Ellipse(plane, r0, r1);
  return out.IsValid();
}

}  // namespace


CurveClassification NurbsCurve::Classify(double tolerance) const {
  CurveClassification out;
  const ON_NurbsCurve& c = curve_;
  if (!c.IsValid()) return out;
  const double tol = (tolerance > 0.0 && ON_IsValid(tolerance)) ? tolerance : ON_ZERO_TOLERANCE;
  const Point3d a = c.PointAtStart(), b = c.PointAtEnd();
  const bool closed = c.IsClosed();

  // A closed curve is never a line; a curve whose ends coincide within tolerance has no direction to report.
  if (!closed && a.DistanceTo(b) > tol && c.IsLinear(tol)) {
    out.kind = CurveKind::Line;
    out.line = ON_Line(a, b);
    return out;
  }

  ON_Arc arc;
  if (c.IsArc(nullptr, &arc, tol) && arc.IsValid()) {
    out.kind = arc.IsCircle() ? CurveKind::Circle : CurveKind::Arc;
    out.arc = arc;
    return out;
  }

  if (closed) {
    ON_Ellipse ellipse;
    if (FitEllipse(c, tol, ellipse) && std::fabs(ellipse.radius[0] - ellipse.radius[1]) > tol) {
      out.kind = CurveKind::Ellipse;
      out.ellipse = ellipse;
      return out;
    }
  }

  if (c.Degree() == 1) {
    std::vector<Point3d> v;
    for (int i = 0; i < c.CVCount(); ++i) {
      ON_3dPoint p;
      if (!c.GetCV(i, p)) return CurveClassification{};
      if (v.empty() || p.DistanceTo(v.back()) > 0.0) v.push_back(p);
    }
    if (v.size() >= 3) {
      out.kind = CurveKind::Polyline;
      out.vertices = std::move(v);
    }
  }
  return out;
}

std::vector<CurveSegment> NurbsCurve::Segments(double tolerance) const {
  std::vector<CurveSegment> out;
  const ON_NurbsCurve& c = curve_;
  if (!c.IsValid()) return out;
  const ON_Interval dom = c.Domain();

  const CurveClassification whole = Classify(tolerance);
  if (whole.kind != CurveKind::General && whole.kind != CurveKind::Polyline) {
    out.push_back(CurveSegment{dom.Min(), dom.Max(), whole});
    return out;
  }

  // C0 breaks: interior knots of multiplicity >= degree (every interior knot of a degree-1 curve).
  std::vector<double> cuts = {dom.Min()};
  const int degree = c.Degree();
  const int knot_count = c.KnotCount();
  for (int i = 0; i < knot_count;) {
    int j = i;
    while (j + 1 < knot_count && c.Knot(j + 1) == c.Knot(i)) ++j;
    const double k = c.Knot(i);
    if (k > dom.Min() && k < dom.Max() && (j - i + 1) >= degree) cuts.push_back(k);
    i = j + 1;
  }
  cuts.push_back(dom.Max());

  for (size_t i = 0; i + 1 < cuts.size(); ++i) {
    NurbsCurve piece;
    piece.curve_ = c;
    if (cuts.size() > 2 && !piece.curve_.Trim(ON_Interval(cuts[i], cuts[i + 1]))) return {};
    out.push_back(CurveSegment{cuts[i], cuts[i + 1], piece.Classify(tolerance)});
  }
  return out;
}

}  // namespace dino8::kernel
