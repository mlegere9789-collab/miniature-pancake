#include "dino8/kernel/curve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include "dino8/kernel/tolerance.h"

#include "dino8/kernel/detail/degree_elevate.h"
#include "dino8/kernel/detail/polygon2d.h"

namespace dino8::kernel {

namespace {

// Which of the two possible in-plane perpendicular directions `dir`
// belongs to, relative to a geometrically-known "true outward" direction
// at the same point - same reasoning as NurbsSurface::OffsetAnalytic()'s
// own OffsetNormalSign() (surface.cpp), duplicated here rather than
// shared since the two files have no common detail header for it.
double OffsetSignAlong(const Vector3d& dir, const Vector3d& true_outward) {
  return ON_DotProduct(dir, true_outward) >= 0.0 ? 1.0 : -1.0;
}

// Shared by both `OffsetInPlane` overloads' exact line case: a line
// offsets exactly along `line_direction x normal` for ANY `normal` not
// parallel to the line, whether that normal comes from this curve's own
// `IsPlanar()` fit or a plane the caller supplied directly.
Result OffsetLineAlongNormal(const Point3d& p0, const Point3d& p1, const Vector3d& normal,
                              double distance, NurbsCurve& out) {
  Vector3d dir = p1 - p0;
  if (!dir.Unitize()) return Result::Failed;
  Vector3d offset_dir = ON_CrossProduct(dir, normal);
  if (!offset_dir.Unitize()) return Result::Failed;
  out = NurbsCurve::FromControlPoints({p0 + distance * offset_dir, p1 + distance * offset_dir}, 1);
  return Result::Ok;
}

// Shared by both `OffsetInPlane` overloads' general (non-exact) case:
// sample `curve` uniformly, move each sample by `distance` along
// `TangentAt(t) x normal`, guard against a curvature fold, then
// tolerance-drive a `FitLeastSquares()` refit against the sampled locus.
// `normal` is this curve's own fitted-plane zaxis for the single-plane
// overload, or a caller-supplied plane's zaxis for the other - the loop
// itself doesn't care which, since it never asks whether `curve` is
// actually planar in `normal`'s plane at all.
Result OffsetGeneralAlongNormal(const NurbsCurve& curve, const Vector3d& normal, double distance,
                                 double tol, NurbsCurve& out) {
  const Interval dom = curve.Domain();
  const BoundingBox bbox = curve.GetTightBoundingBox();
  const double diag = (bbox.max - bbox.min).Length();
  const double chord_tol = dino8::kernel::tolerance::RelativeDistance(diag);
  const int n = std::max(curve.SuggestedSamples(chord_tol), 4 * curve.ControlPointCount());
  std::vector<Point3d> offset_points;
  offset_points.reserve(static_cast<size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const double t = dom.min + (dom.max - dom.min) * i / n;
    Vector3d offset_dir = ON_CrossProduct(curve.TangentAt(t), normal);
    if (!offset_dir.Unitize()) return Result::Failed;  // tangent parallel to normal here

    const Vector3d kappa_vec = curve.CurvatureAt(t);
    const double kappa = kappa_vec.Length();
    if (kappa > dino8::kernel::tolerance::kZeroVector) {
      Vector3d to_center = kappa_vec;
      to_center.Unitize();
      const double inward_component = distance * OffsetSignAlong(offset_dir, to_center);
      if (inward_component >= 1.0 / kappa) return Result::Failed;  // folds through its own center of curvature
    }

    offset_points.push_back(curve.PointAt(t) + distance * offset_dir);
  }

  const int max_cv_count = static_cast<int>(offset_points.size());
  int cv_count = std::min(curve.ControlPointCount(), max_cv_count);
  NurbsCurve fitted;
  for (;;) {
    if (NurbsCurve::FitLeastSquares(offset_points, curve.Degree(), cv_count, fitted) != Result::Ok) {
      return Result::Failed;
    }
    double worst = 0.0;
    for (const Point3d& p : offset_points) {
      worst = std::max(worst, fitted.ClosestPoint(p, 50).DistanceTo(p));
    }
    if (worst <= tol) {
      out = fitted;
      return Result::Ok;
    }
    if (cv_count >= max_cv_count) return Result::Failed;  // tolerance unreachable even at the maximum feasible count
    cv_count = std::min(cv_count * 2, max_cv_count);
  }
}

// Shared by both `OffsetInPlane` overloads' polyline case: a genuinely
// piecewise-linear curve (detected via `ON_Curve::IsPolyline()`, which
// recognizes one whether it happens to be stored as a degree-1 NURBS
// curve or any other exactly-straight-segment representation) offsets
// EXACTLY, corner by corner, instead of falling to
// `OffsetGeneralAlongNormal()`'s sampled least-squares refit - which
// smooths every kink into a blurred curve and, for a closed polygon, can
// disagree with itself at the seam (PARITY_MAP.md's offsetshell
// category, "Planar curve offset": "a kinked polyline goes through the
// smooth refit, blurring corners and potentially splitting a closed
// polygon's seam").
//
// Each edge gets its own offset line, `distance` away along
// `edge_direction x normal` (unit, same convention as every other branch
// in this file); a vertex shared by two edges lands at the EXACT
// intersection of their two offset lines via the standard angle-bisector
// miter point `v + (distance / (1 + dot(n0, n1))) * (n0 + n1)` - the
// same closed-form `OffsetConvexPolyline()` (sweep.cpp) already uses for
// its own convex-only solid-cap case, reused here algebraically
// unchanged but WITHOUT that function's convexity restriction: the
// formula itself needs no convexity, only that no corner is within
// `1e-9` of a full 180-degree fold, where no finite miter exists at all
// (`denom <= 1e-9` below). A concave corner offset inward past its own
// local feature size can still self-intersect - the same
// honestly-disclosed risk PARITY_MAP.md's own "Offset self-intersection
// / invalid-loop removal" item already names for every exact offset in
// this file, inherited here rather than hidden, not newly introduced.
//
// The exact per-vertex intersection above is only valid when every edge
// is perpendicular to `normal` - i.e. the whole polyline is genuinely
// coplanar in a plane normal to `normal` (n0/n1 are always confined to
// that plane by construction, but a vertex where the INCOMING edge has a
// component along `normal` would need its offset "line" reasoned about
// in 3D, where this planar miter formula no longer lands exactly on
// both offset edges). The single-plane `OffsetInPlane(double, ...)`
// overload already guarantees this (`normal` IS this curve's own
// `IsPlanar()` fit), so the check below is a no-op there; the
// explicit-plane overload can hand this a genuinely 3D polyline, for
// which this function returns `false` (not applicable) rather than a
// silently wrong "exact" corner, letting the caller fall through to the
// old sampled general path unchanged.
//
// Returns `false` if `curve` isn't a polyline, has fewer than 3 distinct
// vertices (a single segment is already the exact Line case above), or
// isn't coplanar in a plane normal to `normal` - the caller should fall
// through to the old behavior in every such case. Returns `true` with
// `result` set to `Result::Ok` (and `out` populated) or `Result::Failed`
// (a zero-length edge, an edge parallel to `normal`, or a near-180-degree
// fold) otherwise.
// A single corner's own contribution to the assembled Round/Chamfer-style
// offset curve below: `departs` is where the PRECEDING edge's own offset
// line should end, `arrives` is where the FOLLOWING edge's own offset line
// should start - equal to each other (the exact miter point) for a
// `Sharp`-equivalent corner (concave/contracting, or a degenerate near-
// zero turn), or the two distinct tangent points of a genuine fillet arc
// or straight chamfer segment (`has_piece`, with `piece` the arc or
// segment itself, built directly rather than via `FilletCornerArc()` since
// this corner's own tangent points and radius are already known up front -
// no tangent-length solve needed, unlike `FilletCornerArc()`'s own
// two-legs-plus-unknown-corner case).
struct RoundOffsetCorner {
  Point3d departs;
  Point3d arrives;
  bool has_piece = false;
  NurbsCurve piece;
};

// Builds one corner of the Round/Chamfer-style polyline offset at
// `vertex`, whose incoming/outgoing edges have already-offset unit
// directions `n0`/`n1` (same convention as the Sharp miter loop below), in
// the plane with normal `normal`. `turn_n` - `dot(cross(n0, n1), normal)` -
// is passed in rather than recomputed here since it is also independently
// useful for a caller-side sanity check; its SIGN, together with
// `distance`'s own sign, decides whether this corner is the "fill"
// (convex, gap-opening) side of its own turn or the "cross" (concave,
// contracting) side - a corner's own fixed geometry (`turn_n`'s sign)
// means opposite things for an outward vs an inward offset, which is
// exactly why `distance`'s sign has to enter this test too, not just the
// turn's own handedness. Only a `Round`- or `Chamfer`-requested FILL
// corner ever gets a piece; a cross corner is always the exact miter-line
// intersection, identical to `Sharp`, since there is no gap there to cut
// in the first place (cutting it would carve into the shape instead of
// filling a point sticking out of it).
//
// A fill corner's own two tangent points, `T0 = vertex + distance * n0`
// and `T1 = vertex + distance * n1`, are shared by both styles - both
// already exactly `|distance|` from `vertex` by construction, unlike
// `FilletCornerArc()`'s own two-legs-plus-unknown-corner case, which has
// to solve for its tangent points from an independently-chosen radius
// first. `Round` joins them with a circular arc centered on `vertex`
// itself (only the sweep direction needs resolving, via the same "measure
// the angle, flip if negative" trick `FilletCornerArc()` already uses,
// curve.cpp above); `Chamfer` joins them with the single straight segment
// between them directly - literally the arc's own chord, needing no angle
// or sweep-direction computation at all, since a 2-point line is already
// fully determined by its endpoints.
//
// Returns `Result::Failed` only for the same genuine degeneracy the
// `Sharp` loop already refuses on a cross corner (a near-180-degree fold,
// where the two offset lines have no finite intersection) - a fill corner
// needs no such check: both `Round`'s arc and `Chamfer`'s segment are
// built directly from `n0`/`n1` and never require their lines to actually
// intersect, so even a turn close to a full 180 degrees (where `Sharp`'s
// own miter would be refused) still gets a well-defined piece.
Result BuildRoundOffsetCorner(const Point3d& vertex, const Vector3d& normal, const Vector3d& n0,
                               const Vector3d& n1, double turn_n, double distance,
                               CurveOffsetCornerStyle corner_style, RoundOffsetCorner& corner) {
  const bool is_fill_corner = (corner_style == CurveOffsetCornerStyle::Round ||
                                corner_style == CurveOffsetCornerStyle::Chamfer) &&
                               ((turn_n > 1e-9 && distance > 0.0) || (turn_n < -1e-9 && distance < 0.0));
  if (is_fill_corner) {
    const Point3d T0 = vertex + distance * n0;
    const Point3d T1 = vertex + distance * n1;
    if (corner_style == CurveOffsetCornerStyle::Chamfer) {
      NurbsCurve chamfer_segment = NurbsCurve::FromControlPoints({T0, T1}, 1);
      corner.departs = T0;
      corner.arrives = T1;
      corner.has_piece = true;
      corner.piece = chamfer_segment;
      return Result::Ok;
    }
    Vector3d xaxis = T0 - vertex;
    Vector3d zaxis = normal;
    if (xaxis.Unitize()) {
      Vector3d yaxis = ON_CrossProduct(zaxis, xaxis);
      if (yaxis.Unitize()) {
        const Vector3d to_T1 = T1 - vertex;
        double phi = std::atan2(ON_DotProduct(to_T1, yaxis), ON_DotProduct(to_T1, xaxis));
        if (phi < 0.0) {
          zaxis = -zaxis;
          yaxis = -yaxis;
          phi = std::atan2(ON_DotProduct(to_T1, yaxis), ON_DotProduct(to_T1, xaxis));
        }
        if (phi >= 1e-9) {
          const ON_Plane arc_plane(vertex, xaxis, yaxis);
          const ON_Arc arc(arc_plane, std::fabs(distance), phi);
          ON_NurbsCurve arc_nurbs;
          if (arc.GetNurbForm(arc_nurbs) != 0) {
            NurbsCurve arc_curve;
            arc_curve.raw() = arc_nurbs;
            corner.departs = T0;
            corner.arrives = T1;
            corner.has_piece = true;
            corner.piece = arc_curve;
            return Result::Ok;
          }
        }
      }
    }
    // Degenerate arc frame (T0 effectively equals vertex, or T1 lands
    // exactly opposite T0's own xaxis): fall through to the exact miter
    // point below instead, the same as any non-fill corner.
  }

  const double denom = 1.0 + ON_DotProduct(n0, n1);
  if (denom <= 1e-9) return Result::Failed;  // near-180-degree fold: no finite miter point exists
  const Point3d miter = vertex + (distance / denom) * (n0 + n1);
  corner.departs = miter;
  corner.arrives = miter;
  corner.has_piece = false;
  return Result::Ok;
}

// True if the closed polygon `poly` - every vertex coplanar in a plane
// normal to `normal`, the same guarantee TryOffsetPolylineAlongNormal()
// below already establishes before calling this - has no two non-adjacent
// edges properly crossing. Projects into an arbitrary orthonormal basis of
// that plane (any one works: this tests topology only, not a metric
// quantity that could depend on which basis was picked) and delegates to
// detail::IsSimplePolygon(), the same proper-crossing test
// boolean.cpp/surface.cpp's own concave-polygon code already relies on.
bool IsOffsetPolygonSimple(const std::vector<Point3d>& poly, const Vector3d& normal) {
  Vector3d basis_hint(0.0, 0.0, 1.0);
  if (std::fabs(ON_DotProduct(normal, basis_hint)) > 0.9) basis_hint = Vector3d(1.0, 0.0, 0.0);
  Vector3d xaxis = ON_CrossProduct(normal, basis_hint);
  xaxis.Unitize();
  const Vector3d yaxis = ON_CrossProduct(normal, xaxis);

  std::vector<Point2d> poly2d(poly.size());
  for (size_t i = 0; i < poly.size(); ++i) {
    const Vector3d rel = poly[i] - poly[0];
    poly2d[i] = Point2d(ON_DotProduct(rel, xaxis), ON_DotProduct(rel, yaxis));
  }
  return detail::IsSimplePolygon(poly2d);
}

bool TryOffsetPolylineAlongNormal(const NurbsCurve& curve, const Vector3d& normal, double distance,
                                   double tol, CurveOffsetCornerStyle corner_style, NurbsCurve& out,
                                   Result& result) {
  ON_SimpleArray<ON_3dPoint> pline;
  if (!curve.raw().IsPolyline(&pline)) return false;

  const bool closed = curve.IsClosed();
  const int total = pline.Count();
  const int vcount = closed ? total - 1 : total;
  if (vcount < 3) return false;

  std::vector<Point3d> v(static_cast<size_t>(vcount));
  for (int i = 0; i < vcount; ++i) v[static_cast<size_t>(i)] = pline[i];

  for (int i = 1; i < vcount; ++i) {
    if (std::fabs(ON_DotProduct(v[static_cast<size_t>(i)] - v[0], normal)) > tol) return false;
  }

  const int edge_count = closed ? vcount : vcount - 1;
  std::vector<Vector3d> ndir(static_cast<size_t>(edge_count));
  for (int i = 0; i < edge_count; ++i) {
    Vector3d d = v[static_cast<size_t>((i + 1) % vcount)] - v[static_cast<size_t>(i)];
    if (!d.Unitize()) {
      result = Result::Failed;  // zero-length edge
      return true;
    }
    Vector3d n = ON_CrossProduct(d, normal);
    if (!n.Unitize()) {
      result = Result::Failed;  // edge parallel to normal - no offset direction exists
      return true;
    }
    ndir[static_cast<size_t>(i)] = n;
  }

  if (corner_style == CurveOffsetCornerStyle::Sharp) {
    std::vector<Point3d> offset_v(static_cast<size_t>(vcount));
    for (int i = 0; i < vcount; ++i) {
      if (!closed && i == 0) {
        offset_v[0] = v[0] + distance * ndir[0];
        continue;
      }
      if (!closed && i == vcount - 1) {
        offset_v[static_cast<size_t>(i)] =
            v[static_cast<size_t>(i)] + distance * ndir[static_cast<size_t>(edge_count - 1)];
        continue;
      }
      const Vector3d& n0 = ndir[static_cast<size_t>((i - 1 + edge_count) % edge_count)];
      const Vector3d& n1 = ndir[static_cast<size_t>(i % edge_count)];
      const double denom = 1.0 + ON_DotProduct(n0, n1);
      if (denom <= 1e-9) {
        result = Result::Failed;  // near-180-degree fold: no finite miter point exists
        return true;
      }
      offset_v[static_cast<size_t>(i)] = v[static_cast<size_t>(i)] + (distance / denom) * (n0 + n1);
    }

    if (closed && !IsOffsetPolygonSimple(offset_v, normal)) {
      // Detection-only invalid-loop guard: PARITY_MAP.md's own "Offset
      // self-intersection / invalid-loop removal" item discloses that an
      // inward offset of a concave polygon past its own local feature size
      // can self-intersect with no repair - and, until now, no detection
      // either for this exact (non-refit) polyline path, which silently
      // returned the bowtied loop as Result::Ok. Refuse instead, the same
      // honest "detect, don't repair" convention this file's other
      // feasibility guards (the near-180-degree fold check just above,
      // OffsetAnalytic's spindle guard, etc.) already follow.
      result = Result::Failed;
      return true;
    }

    if (closed) offset_v.push_back(offset_v.front());
    out = NurbsCurve::FromControlPoints(offset_v, 1);
    result = Result::Ok;
    return true;
  }

  // --- Round/Chamfer: assemble one line per edge, joined through a
  // genuine fillet arc or straight chamfer segment at every FILL corner
  // (BuildRoundOffsetCorner() above) via NurbsCurve::Join() - the same
  // position-only C0 line-piece-line splice FilletCorner() already uses
  // for its own single corner, repeated here for however many corners this
  // polyline has. A non-fill corner contributes no separate piece at all:
  // its `departs`/`arrives` are the same exact miter point, so the two
  // adjacent edge lines already meet there on their own.
  std::vector<RoundOffsetCorner> corners(static_cast<size_t>(vcount));
  std::vector<bool> corner_valid(static_cast<size_t>(vcount), false);
  for (int i = 0; i < vcount; ++i) {
    if (!closed && (i == 0 || i == vcount - 1)) continue;
    const Vector3d& n0 = ndir[static_cast<size_t>((i - 1 + edge_count) % edge_count)];
    const Vector3d& n1 = ndir[static_cast<size_t>(i % edge_count)];
    const double turn_n = ON_DotProduct(ON_CrossProduct(n0, n1), normal);
    if (BuildRoundOffsetCorner(v[static_cast<size_t>(i)], normal, n0, n1, turn_n, distance, corner_style,
                               corners[static_cast<size_t>(i)]) != Result::Ok) {
      result = Result::Failed;
      return true;
    }
    corner_valid[static_cast<size_t>(i)] = true;
  }

  auto EdgeStart = [&](int edge_index) -> Point3d {
    if (!closed && edge_index == 0) return v[0] + distance * ndir[0];
    return corners[static_cast<size_t>(edge_index)].arrives;
  };
  auto EdgeEnd = [&](int edge_index) -> Point3d {
    if (!closed && edge_index == edge_count - 1) {
      return v[static_cast<size_t>(vcount - 1)] + distance * ndir[static_cast<size_t>(edge_count - 1)];
    }
    return corners[static_cast<size_t>((edge_index + 1) % vcount)].departs;
  };

  const double join_tol = std::max(std::fabs(distance), 1.0) * 1e-6;
  NurbsCurve assembled = NurbsCurve::FromControlPoints({EdgeStart(0), EdgeEnd(0)}, 1);
  for (int edge_index = 1; edge_index <= edge_count - 1; ++edge_index) {
    const int vertex_index = edge_index;  // corner shared by edge (edge_index - 1) and edge_index
    if (corner_valid[static_cast<size_t>(vertex_index)] && corners[static_cast<size_t>(vertex_index)].has_piece) {
      if (assembled.Join(corners[static_cast<size_t>(vertex_index)].piece, join_tol) != Result::Ok) {
        result = Result::Failed;
        return true;
      }
    }
    NurbsCurve edge_piece = NurbsCurve::FromControlPoints({EdgeStart(edge_index), EdgeEnd(edge_index)}, 1);
    if (assembled.Join(edge_piece, join_tol) != Result::Ok) {
      result = Result::Failed;
      return true;
    }
  }
  if (closed && corner_valid[0] && corners[0].has_piece) {
    if (assembled.Join(corners[0].piece, join_tol) != Result::Ok) {
      result = Result::Failed;
      return true;
    }
  }

  out = assembled;
  result = Result::Ok;
  return true;
}

void SubdivideForFlatness(const NurbsCurve& curve, double t0, double t1, double chord_tolerance,
                           int depth, int max_depth, std::vector<double>& out) {
  const Point3d p0 = curve.PointAt(t0);
  const Point3d p1 = curve.PointAt(t1);
  const double tm = 0.5 * (t0 + t1);
  const Point3d pm = curve.PointAt(tm);
  const Point3d chord_mid((p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5, (p0.z + p1.z) * 0.5);
  const double deviation = (pm - chord_mid).Length();
  if (deviation > chord_tolerance && depth < max_depth) {
    SubdivideForFlatness(curve, t0, tm, chord_tolerance, depth + 1, max_depth, out);
    SubdivideForFlatness(curve, tm, t1, chord_tolerance, depth + 1, max_depth, out);
  } else {
    out.push_back(t1);
  }
}

// Value of basis function N_i(t) for the clamped B-spline defined by
// `knot` (ON's own compressed convention), `cv_count` control points and
// `order` = degree + 1. Used to build the least-squares normal equations
// in FitLeastSquares() below - real Cox-de Boor evaluation via
// OpenNURBS' own ON_NurbsSpanIndex/ON_EvaluateNurbsBasis, not an
// approximation of it.
double BasisValue(const std::vector<double>& knot, int cv_count, int order, int i, double t) {
  const int span = ON_NurbsSpanIndex(order, cv_count, knot.data(), t, 0, 0);
  if (i < span || i > span + order - 1) return 0.0;
  // ON_EvaluateNurbsBasis writes a full order-by-order triangular table
  // (every degree up to `degree`, not just the final row) - its own doc
  // comment spells out "If N were declared as double N[order][order]".
  // Passing a buffer of only `order` doubles here (an earlier draft's
  // bug, caught by the corruption it caused) overruns it; the degree-d
  // values this function actually wants are the first `order` entries.
  std::vector<double> B(static_cast<size_t>(order) * static_cast<size_t>(order));
  ON_EvaluateNurbsBasis(order, knot.data() + span, t, B.data());
  return B[static_cast<size_t>(i - span)];
}

// Solves the dense linear system A*X = B in place via Gaussian
// elimination with partial pivoting. A is `size` x `size` (row-major),
// B is `size` x `rhs_count` (row-major) and is overwritten with the
// solution X. Returns false if A is (numerically) singular.
bool SolveLinearSystem(std::vector<double>& a, std::vector<double>& b, int size, int rhs_count) {
  for (int col = 0; col < size; ++col) {
    int pivot_row = col;
    double pivot_val = std::abs(a[static_cast<size_t>(col * size + col)]);
    for (int row = col + 1; row < size; ++row) {
      const double v = std::abs(a[static_cast<size_t>(row * size + col)]);
      if (v > pivot_val) { pivot_val = v; pivot_row = row; }
    }
    if (pivot_val < 1e-14) return false;
    if (pivot_row != col) {
      for (int k = 0; k < size; ++k) std::swap(a[static_cast<size_t>(col * size + k)], a[static_cast<size_t>(pivot_row * size + k)]);
      for (int k = 0; k < rhs_count; ++k) std::swap(b[static_cast<size_t>(col * rhs_count + k)], b[static_cast<size_t>(pivot_row * rhs_count + k)]);
    }
    const double diag = a[static_cast<size_t>(col * size + col)];
    for (int row = 0; row < size; ++row) {
      if (row == col) continue;
      const double factor = a[static_cast<size_t>(row * size + col)] / diag;
      if (factor == 0.0) continue;
      for (int k = col; k < size; ++k) a[static_cast<size_t>(row * size + k)] -= factor * a[static_cast<size_t>(col * size + k)];
      for (int k = 0; k < rhs_count; ++k) b[static_cast<size_t>(row * rhs_count + k)] -= factor * b[static_cast<size_t>(col * rhs_count + k)];
    }
  }
  for (int row = 0; row < size; ++row) {
    const double diag = a[static_cast<size_t>(row * size + row)];
    for (int k = 0; k < rhs_count; ++k) b[static_cast<size_t>(row * rhs_count + k)] /= diag;
  }
  return true;
}

}  // namespace

Result NurbsCurve::FitLeastSquares(const std::vector<Point3d>& points, int degree, int control_point_count,
                                    NurbsCurve& out) {
  const int m_plus_1 = static_cast<int>(points.size());
  const int p = degree;
  const int order = p + 1;
  const int n_plus_1 = control_point_count;
  const int n = n_plus_1 - 1;
  const int m = m_plus_1 - 1;
  if (m_plus_1 < 2 || p < 1 || n_plus_1 < order || n_plus_1 > m_plus_1) {
    return Result::Failed;
  }

  // Chord-length parameterization, u_bar[0..m] in [0, 1].
  std::vector<double> u_bar(static_cast<size_t>(m_plus_1));
  {
    double total = 0.0;
    std::vector<double> seg(static_cast<size_t>(m_plus_1), 0.0);
    for (int k = 1; k <= m; ++k) {
      seg[static_cast<size_t>(k)] = (points[static_cast<size_t>(k)] - points[static_cast<size_t>(k - 1)]).Length();
      total += seg[static_cast<size_t>(k)];
    }
    u_bar[0] = 0.0;
    u_bar[static_cast<size_t>(m)] = 1.0;
    if (total <= 0.0) {
      for (int k = 1; k < m; ++k) u_bar[static_cast<size_t>(k)] = static_cast<double>(k) / m;
    } else {
      double acc = 0.0;
      for (int k = 1; k < m; ++k) {
        acc += seg[static_cast<size_t>(k)];
        u_bar[static_cast<size_t>(k)] = acc / total;
      }
    }
  }

  // Knot vector via the standard approximation knot-averaging formula
  // (Piegl & Tiller eq. 9.68/9.69): textbook (uncompressed) convention
  // first, length n+p+2, then converted to ON's compressed storage.
  std::vector<double> u_full(static_cast<size_t>(n + p + 2));
  for (int i = 0; i <= p; ++i) u_full[static_cast<size_t>(i)] = 0.0;
  for (int i = 0; i <= p; ++i) u_full[static_cast<size_t>(n + 1 + i)] = 1.0;
  const int h = n - p;
  if (h > 0) {
    const double d = static_cast<double>(m_plus_1) / static_cast<double>(n - p + 1);
    for (int j = 1; j <= h; ++j) {
      const double jd = j * d;
      int i = static_cast<int>(jd);
      const double alpha = jd - i;
      if (i < 1) i = 1;
      if (i > m) i = m;
      const double u_im1 = u_bar[static_cast<size_t>(i - 1)];
      const double u_i = u_bar[static_cast<size_t>(i)];
      u_full[static_cast<size_t>(p + j)] = (1.0 - alpha) * u_im1 + alpha * u_i;
    }
  }
  // ON's compressed knot array drops the redundant first/last textbook
  // entries: compressed[k] = u_full[k + 1], length n + p.
  std::vector<double> knot(static_cast<size_t>(n + p));
  for (int k = 0; k < n + p; ++k) knot[static_cast<size_t>(k)] = u_full[static_cast<size_t>(k + 1)];

  const Point3d q0 = points.front();
  const Point3d qm = points.back();

  if (n == 1) {
    // Only the two (fixed) endpoints - no interior unknowns to solve for.
    NurbsCurve result;
    result.curve_.Create(3, false, order, n_plus_1);
    result.curve_.SetCV(0, q0);
    result.curve_.SetCV(1, qm);
    for (int k = 0; k < n + p; ++k) result.curve_.SetKnot(k, knot[static_cast<size_t>(k)]);
    out = result;
    return Result::Ok;
  }

  // Normal equations for the (n-1) interior control points P_1..P_{n-1}.
  const int unknowns = n - 1;
  std::vector<double> a(static_cast<size_t>(unknowns) * static_cast<size_t>(unknowns), 0.0);
  std::vector<double> rhs(static_cast<size_t>(unknowns) * 3, 0.0);
  for (int k = 1; k < m; ++k) {
    const double t = u_bar[static_cast<size_t>(k)];
    const double n0 = BasisValue(knot, n_plus_1, order, 0, t);
    const double nn = BasisValue(knot, n_plus_1, order, n, t);
    const Point3d rk((points[static_cast<size_t>(k)].x - n0 * q0.x - nn * qm.x),
                      (points[static_cast<size_t>(k)].y - n0 * q0.y - nn * qm.y),
                      (points[static_cast<size_t>(k)].z - n0 * q0.z - nn * qm.z));
    std::vector<double> ni(static_cast<size_t>(unknowns));
    for (int i = 1; i <= unknowns; ++i) ni[static_cast<size_t>(i - 1)] = BasisValue(knot, n_plus_1, order, i, t);
    for (int i = 0; i < unknowns; ++i) {
      if (ni[static_cast<size_t>(i)] == 0.0) continue;
      rhs[static_cast<size_t>(i * 3 + 0)] += ni[static_cast<size_t>(i)] * rk.x;
      rhs[static_cast<size_t>(i * 3 + 1)] += ni[static_cast<size_t>(i)] * rk.y;
      rhs[static_cast<size_t>(i * 3 + 2)] += ni[static_cast<size_t>(i)] * rk.z;
      for (int j = 0; j < unknowns; ++j) {
        a[static_cast<size_t>(i * unknowns + j)] += ni[static_cast<size_t>(i)] * ni[static_cast<size_t>(j)];
      }
    }
  }
  if (!SolveLinearSystem(a, rhs, unknowns, 3)) {
    return Result::Failed;
  }

  NurbsCurve result;
  result.curve_.Create(3, false, order, n_plus_1);
  result.curve_.SetCV(0, q0);
  for (int i = 1; i < n; ++i) {
    const Point3d p_i(rhs[static_cast<size_t>((i - 1) * 3 + 0)], rhs[static_cast<size_t>((i - 1) * 3 + 1)],
                       rhs[static_cast<size_t>((i - 1) * 3 + 2)]);
    result.curve_.SetCV(i, p_i);
  }
  result.curve_.SetCV(n, qm);
  for (int k = 0; k < n + p; ++k) result.curve_.SetKnot(k, knot[static_cast<size_t>(k)]);
  out = result;
  return Result::Ok;
}

NurbsCurve NurbsCurve::FromControlPoints(const std::vector<Point3d>& control_points,
                                          int degree) {
  // ON_NurbsCurve::Create() refuses order < 2 or cv_count < order by
  // returning false BEFORE it sets m_order/m_cv_count or allocates m_cv,
  // and the SetCV()/MakeClampedUniformKnotVector() calls below then
  // silently no-op against that never-allocated curve. Without this
  // check the result was a completely empty curve that still looked
  // usable - Degree() 0, ControlPointCount() 0, KnotCount() -2,
  // Domain() == [ON_UNSET_VALUE, ON_UNSET_VALUE], PointAt() == (0, 0, 0)
  // and Length() == 0 for every input (confirmed by a debug run, not
  // assumed) - i.e. a silently wrong result rather than a failure, and
  // reachable from any caller forwarding a user-supplied degree without
  // first clamping it to the point count (the app's Python AddCurve does).
  if (degree < 1) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FromControlPoints: degree must be at least 1");
  }
  const int order = degree + 1;
  const int cv_count = static_cast<int>(control_points.size());
  if (cv_count < order) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FromControlPoints: a degree-" + std::to_string(degree) +
        " curve needs at least " + std::to_string(order) + " control points, got " +
        std::to_string(cv_count));
  }
  NurbsCurve result;
  result.curve_.Create(/*dimension=*/3, /*is_rational=*/false, order, cv_count);

  for (int i = 0; i < cv_count; ++i) {
    result.curve_.SetCV(i, control_points[static_cast<size_t>(i)]);
  }

  // Clamped, uniform knot vector — matches the "straightforward
  // construction" this chunk promises; a real curve-fit/knot-spacing
  // strategy belongs to whichever later chunk actually needs it.
  result.curve_.MakeClampedUniformKnotVector();

  return result;
}

Result NurbsCurve::FilletCornerArc(Point3d p0, Point3d corner, Point3d p1, double radius, NurbsCurve& arc_out) {
  if (!(radius > 0.0)) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::FilletCornerArc: radius must be positive");
  }
  Vector3d u = p0 - corner;
  Vector3d v = p1 - corner;
  const double len_u = u.Length();
  const double len_v = v.Length();
  if (!u.Unitize() || !v.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FilletCornerArc: p0/p1 must be distinct from corner");
  }
  const double cos_theta = std::max(-1.0, std::min(1.0, ON_DotProduct(u, v)));
  const double theta = std::acos(cos_theta);
  if (theta < 1e-9 || theta > ON_PI - 1e-9) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FilletCornerArc: p0, corner and p1 are collinear - no finite tangent circle "
        "exists for a straight corner");
  }
  const double half = 0.5 * theta;
  const double d = radius / std::tan(half);
  if (d >= len_u || d >= len_v) {
    return Result::Failed;
  }
  const Point3d T0 = corner + d * u;
  const Point3d T1 = corner + d * v;

  Vector3d bis = u + v;
  if (!bis.Unitize()) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::FilletCornerArc: degenerate (180-degree) bisector");
  }
  const double L = radius / std::sin(half);
  const Point3d C = corner + L * bis;

  Vector3d xaxis = T0 - C;
  if (!xaxis.Unitize()) {
    throw std::runtime_error("dino8::kernel::NurbsCurve::FilletCornerArc: degenerate arc frame (please report this as a bug)");
  }
  Vector3d zaxis = ON_CrossProduct(u, v);
  if (!zaxis.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::FilletCornerArc: p0, corner and p1 are collinear - no finite tangent circle "
        "exists for a straight corner");
  }
  Vector3d yaxis = ON_CrossProduct(zaxis, xaxis);
  yaxis.Unitize();
  const Vector3d to_T1 = T1 - C;
  double phi = std::atan2(ON_DotProduct(to_T1, yaxis), ON_DotProduct(to_T1, xaxis));
  if (phi < 0.0) {
    // The other of the two candidate normal directions sweeps the short
    // way from T0 to T1 instead - flip zaxis/yaxis (xaxis is unaffected)
    // and re-measure, rather than assuming a fixed cross-product winding
    // holds for every corner orientation.
    zaxis = -zaxis;
    yaxis = -yaxis;
    phi = std::atan2(ON_DotProduct(to_T1, yaxis), ON_DotProduct(to_T1, xaxis));
  }
  if (phi < 1e-9 || phi > ON_PI + 1e-9) {
    throw std::runtime_error(
        "dino8::kernel::NurbsCurve::FilletCornerArc: computed arc sweep out of the expected (0, pi) range (please "
        "report this as a bug)");
  }
  // Checked invariant, not assumed: T1 really is at `radius` from C.
  if (std::fabs(to_T1.Length() - radius) > std::max(radius, 1.0) * 1e-6) {
    throw std::runtime_error(
        "dino8::kernel::NurbsCurve::FilletCornerArc: reconstructed tangent point does not lie on the arc's own "
        "circle (please report this as a bug)");
  }

  const ON_Plane arc_plane(C, xaxis, yaxis);
  const ON_Arc arc(arc_plane, radius, phi);
  ON_NurbsCurve arc_nurbs;
  if (arc.GetNurbForm(arc_nurbs) == 0) {
    return Result::Failed;
  }
  NurbsCurve arc_curve;
  arc_curve.curve_ = arc_nurbs;
  arc_out = arc_curve;
  return Result::Ok;
}

Result NurbsCurve::FilletCorner(Point3d p0, Point3d corner, Point3d p1, double radius, NurbsCurve& out) {
  NurbsCurve arc_curve;
  const Result arc_result = FilletCornerArc(p0, corner, p1, radius, arc_curve);
  if (arc_result != Result::Ok) {
    return arc_result;
  }
  const Interval arc_dom = arc_curve.Domain();
  const Point3d T0 = arc_curve.PointAt(arc_dom.min);
  const Point3d T1 = arc_curve.PointAt(arc_dom.max);

  NurbsCurve result = FromControlPoints({p0, T0}, 1);
  const double tol = std::max(radius, 1.0) * 1e-6;
  if (result.Join(arc_curve, tol) != Result::Ok) {
    return Result::Failed;
  }
  NurbsCurve tail = FromControlPoints({T1, p1}, 1);
  if (result.Join(tail, tol) != Result::Ok) {
    return Result::Failed;
  }
  out = result;
  return Result::Ok;
}

Result NurbsCurve::ChamferCorner(Point3d p0, Point3d corner, Point3d p1, double distance0, double distance1,
                                 NurbsCurve& out) {
  if (!(distance0 > 0.0) || !(distance1 > 0.0)) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::ChamferCorner: distances must be positive");
  }
  Vector3d u = p0 - corner;
  Vector3d v = p1 - corner;
  const double len_u = u.Length();
  const double len_v = v.Length();
  if (!u.Unitize() || !v.Unitize()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::ChamferCorner: p0/p1 must be distinct from corner");
  }
  if (distance0 >= len_u || distance1 >= len_v) {
    return Result::Failed;
  }
  const Point3d T0 = corner + distance0 * u;
  const Point3d T1 = corner + distance1 * v;
  out = FromControlPoints({p0, T0, T1, p1}, 1);
  return Result::Ok;
}

namespace {

// Reads position + derivatives up to order `continuity` (1..3) of `curve`
// at parameter `t` via the generic `ON_Curve::Evaluate` (the same base
// evaluation method `TangentAt`/`CurvatureAt` are themselves built on top
// of, per `Ev1Der`/`Ev2Der`/`EvCurvature`), applying the t -> -t
// reparametrization sign flip to every ODD-order derivative when
// `reverse` is true - see `BlendCurves`' own doc comment for why that is
// the correct identity (EVEN-order derivatives, including the 0th/
// position, are unaffected). `out_derivs[k-1]` holds the (possibly sign-
// flipped) k-th derivative for k in [1, continuity]. Returns false if
// `Evaluate` itself fails.
bool EvaluateBlendEnd(const ON_NurbsCurve& curve, double t, bool reverse, int continuity, Point3d& out_point,
                       std::array<Vector3d, 3>& out_derivs) {
  double v[4 * 3] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  if (!curve.Evaluate(t, continuity, 3, v)) return false;
  out_point = Point3d(v[0], v[1], v[2]);
  for (int k = 1; k <= continuity; ++k) {
    Vector3d d(v[k * 3 + 0], v[k * 3 + 1], v[k * 3 + 2]);
    if (reverse && (k % 2 == 1)) d = -d;
    out_derivs[static_cast<size_t>(k - 1)] = d;
  }
  return true;
}

}  // namespace

Result NurbsCurve::BlendCurves(const NurbsCurve& curve0, double t0, bool reverse0, const NurbsCurve& curve1,
                                double t1, bool reverse1, int continuity, NurbsCurve& out) {
  if (continuity < 1 || continuity > 3) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::BlendCurves: continuity must be 1 (G1), 2 (G2) or 3 (G3)");
  }
  const Interval dom0 = curve0.Domain();
  const Interval dom1 = curve1.Domain();
  if (t0 < dom0.min || t0 > dom0.max) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::BlendCurves: t0 is outside curve0's own Domain()");
  }
  if (t1 < dom1.min || t1 > dom1.max) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::BlendCurves: t1 is outside curve1's own Domain()");
  }

  Point3d P0, P1;
  std::array<Vector3d, 3> D0{}, D1{};
  if (!EvaluateBlendEnd(curve0.curve_, t0, reverse0, continuity, P0, D0) ||
      !EvaluateBlendEnd(curve1.curve_, t1, reverse1, continuity, P1, D1)) {
    return Result::Failed;
  }

  const double scale = std::max(1.0, P0.DistanceTo(Point3d(0, 0, 0)));
  if (P0.DistanceTo(P1) <= 1e-9 * scale) {
    return Result::Failed;
  }

  // Every step below is deliberately written using only Point-Point
  // (-> Vector) subtraction, Vector*scalar, and Point+-Vector - the
  // operators this codebase's own Point3d/Vector3d arithmetic already
  // relies on everywhere else (e.g. FilletCornerArc's own `corner +
  // d*u`) - rather than a direct `scalar*Point3d` combination, which
  // this file does not otherwise use. Each formula below is the
  // textbook forward/backward Bezier finite-difference identity
  // (`BlendCurves`' own doc comment derives it), algebraically
  // rearranged into that same "Point + Vector" shape:
  //   2*P1 - P0        == P1 + (P1 - P0)
  //   3*P2 - 3*P1 + P0  == P0 + 3*(P2 - P1)
  // and the mirror-image rearrangement for the back (u=1) side.
  const int d = 2 * continuity + 1;
  const double inv_d1 = 1.0 / static_cast<double>(d);
  const double inv_d2 = 1.0 / static_cast<double>(d * (d - 1));
  const double inv_d3 = 1.0 / static_cast<double>(d * (d - 1) * (d - 2));
  std::vector<Point3d> ctrl(static_cast<size_t>(d) + 1);
  ctrl[0] = P0;
  if (continuity >= 1) ctrl[1] = ctrl[0] + D0[0] * inv_d1;
  if (continuity >= 2) {
    ctrl[2] = ctrl[1] + (ctrl[1] - ctrl[0]) + D0[1] * inv_d2;
  }
  if (continuity >= 3) {
    ctrl[3] = ctrl[0] + 3.0 * (ctrl[2] - ctrl[1]) + D0[2] * inv_d3;
  }

  ctrl[static_cast<size_t>(d)] = P1;
  if (continuity >= 1) {
    ctrl[static_cast<size_t>(d - 1)] = ctrl[static_cast<size_t>(d)] - D1[0] * inv_d1;
  }
  if (continuity >= 2) {
    ctrl[static_cast<size_t>(d - 2)] = ctrl[static_cast<size_t>(d - 1)] -
                                        (ctrl[static_cast<size_t>(d)] - ctrl[static_cast<size_t>(d - 1)]) +
                                        D1[1] * inv_d2;
  }
  if (continuity >= 3) {
    ctrl[static_cast<size_t>(d - 3)] =
        ctrl[static_cast<size_t>(d)] -
        3.0 * (ctrl[static_cast<size_t>(d - 1)] - ctrl[static_cast<size_t>(d - 2)]) - D1[2] * inv_d3;
  }

  out = FromControlPoints(ctrl, d);
  return Result::Ok;
}

int NurbsCurve::Degree() const { return curve_.Degree(); }

int NurbsCurve::ControlPointCount() const { return curve_.CVCount(); }

bool NurbsCurve::IsRational() const { return curve_.IsRational(); }

double NurbsCurve::WeightAt(int i) const { return curve_.Weight(i); }

Result NurbsCurve::SetWeightAt(int i, double weight) {
  if (i < 0 || i >= ControlPointCount()) {
    return Result::Failed;
  }
  if (WeightAt(i) == weight) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.SetWeight(i, weight) ? Result::Ok : Result::Failed;
}

Point3d NurbsCurve::ControlPointAt(int i) const {
  if (i < 0 || i >= ControlPointCount()) {
    throw std::out_of_range(
        "dino8::kernel::NurbsCurve::ControlPointAt: i out of range");
  }
  ON_3dPoint point;
  curve_.GetCV(i, point);
  return point;
}

Result NurbsCurve::SetControlPointAt(int i, Point3d point) {
  if (i < 0 || i >= ControlPointCount()) {
    throw std::out_of_range(
        "dino8::kernel::NurbsCurve::SetControlPointAt: i out of range");
  }
  if (ControlPointAt(i) == point) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.SetCV(i, point) ? Result::Ok : Result::Failed;
}

int NurbsCurve::KnotCount() const { return curve_.KnotCount(); }

double NurbsCurve::KnotAt(int i) const {
  if (i < 0 || i >= KnotCount()) {
    throw std::out_of_range("dino8::kernel::NurbsCurve::KnotAt: i out of range");
  }
  return curve_.Knot(i);
}

Result NurbsCurve::SetKnotAt(int i, double value) {
  if (i < 0 || i >= KnotCount()) {
    return Result::Failed;
  }
  if (curve_.Knot(i) == value) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.SetKnot(i, value) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::InsertKnotAt(double knot_value, int multiplicity) {
  const Interval domain = Domain();
  if (knot_value <= domain.min || knot_value >= domain.max) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::InsertKnotAt: knot_value must be "
        "strictly inside the curve's own domain");
  }
  if (multiplicity < 1 || multiplicity > Degree()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::InsertKnotAt: multiplicity must be "
        "between 1 and Degree() inclusive");
  }
  return curve_.InsertKnot(knot_value, multiplicity) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::MakeRational() {
  if (curve_.IsRational()) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.MakeRational() ? Result::Ok : Result::Failed;
}

Result NurbsCurve::MakeNonRational() {
  if (!curve_.IsRational()) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.MakeNonRational() ? Result::Ok : Result::Failed;
}

Result NurbsCurve::ElevateDegree(int new_degree) {
  if (new_degree <= Degree()) {
    return Result::NoOpAlreadySatisfied;
  }
  // Deliberately NOT `ON_NurbsCurve::IncreaseDegree` - see
  // detail/degree_elevate.h for the measured, shape-corrupting inaccuracy
  // that routine has on non-uniform knot vectors.
  ON_NurbsCurve elevated;
  if (!detail::DegreeElevateNurbsCurve(curve_, new_degree, elevated)) {
    return Result::Failed;
  }
  curve_ = elevated;
  return Result::Ok;
}

Interval NurbsCurve::Domain() const {
  const ON_Interval domain = curve_.Domain();
  return Interval{domain.Min(), domain.Max()};
}

Point3d NurbsCurve::PointAt(double t) const {
  ON_3dPoint pt;
  curve_.EvPoint(t, pt);
  return pt;
}

Vector3d NurbsCurve::CurvatureAt(double t) const { return curve_.CurvatureAt(t); }

int NurbsCurve::SuggestedSamples(double chord_tolerance, int curvature_samples) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::SuggestedSamples: chord_tolerance must "
        "be positive");
  }

  const ON_Interval domain = curve_.Domain();
  double max_kappa = 0.0;
  for (int i = 0; i <= curvature_samples; ++i) {
    const double t = domain.ParameterAt(static_cast<double>(i) / curvature_samples);
    max_kappa = std::max(max_kappa, CurvatureAt(t).Length());
  }

  if (max_kappa < 1e-12) {
    return 1;  // negligible curvature everywhere - a straight line needs one segment
  }

  const double radius = 1.0 / max_kappa;
  // Chord-height (sagitta) formula for a circular arc of radius R:
  // sagitta = R * (1 - cos(half_angle)). Solve for the largest angular
  // step whose sagitta stays within chord_tolerance.
  const double clamped_ratio = std::min(1.0, chord_tolerance / radius);
  const double max_angle_step = 2.0 * std::acos(1.0 - clamped_ratio);
  // Total turning angle assuming the whole curve turns at the tightest
  // radius found - the conservative approximation this method documents.
  const double total_angle = Length() / radius;
  return std::max(1, static_cast<int>(std::ceil(total_angle / max_angle_step)));
}

std::vector<double> NurbsCurve::SuggestedParameterValues(double chord_tolerance,
                                                          int max_depth) const {
  if (chord_tolerance <= 0.0) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::SuggestedParameterValues: chord_tolerance "
        "must be positive");
  }

  const ON_Interval domain = curve_.Domain();
  std::vector<double> out;
  out.push_back(domain.Min());
  SubdivideForFlatness(*this, domain.Min(), domain.Max(), chord_tolerance, 0, max_depth, out);
  return out;
}

double NurbsCurve::ClosestPointParameter(Point3d point, int samples) const {
  const ON_Interval domain = curve_.Domain();
  const bool closed = curve_.IsClosed() != 0;
  auto distance_squared = [&](double t) { return (PointAt(t) - point).LengthSquared(); };
  // Maps a parameter that may have wandered outside `domain` back into it
  // by wrapping around the seam, for a closed curve only - see the
  // wrap-vs-clamp discussion below.
  auto wrap = [&](double t) {
    if (!closed) return t;
    const double len = domain.Length();
    if (len <= 0.0) return t;
    double r = std::fmod(t - domain.Min(), len);
    if (r < 0.0) r += len;
    return domain.Min() + r;
  };
  auto distance_squared_wrapped = [&](double t) { return distance_squared(wrap(t)); };

  double best_t = domain.Min();
  double best_d2 = distance_squared(best_t);
  for (int i = 1; i <= samples; ++i) {
    const double t = domain.ParameterAt(static_cast<double>(i) / samples);
    const double d2 = distance_squared(t);
    if (d2 < best_d2) {
      best_d2 = d2;
      best_t = t;
    }
  }

  const double step = domain.Length() / samples;
  // For an open curve, the refinement window is clamped to the domain -
  // the true closest point can never lie outside it. For a closed curve,
  // clamping is wrong whenever the coarse sample above happens to land
  // near `domain.Min()` or `domain.Max()`: the true nearest point may
  // sit just past that boundary, on the far side of the same physical
  // seam point, and clamping would wall the golden-section search off
  // from ever reaching it. Letting the window extend past the domain and
  // evaluating through `wrap()` lets the search cross the seam like any
  // other point on the curve.
  double lo = closed ? (best_t - step) : std::max(domain.Min(), best_t - step);
  double hi = closed ? (best_t + step) : std::min(domain.Max(), best_t + step);

  // Golden-section search assumes the distance function is unimodal on
  // the window it polishes, and a window that straddles a C0 kink
  // violates that: a closed-but-not-periodic curve's own seam
  // (FromControlPoints() with matching first/last points is only C0
  // there), or any interior knot of full multiplicity, puts two different
  // polynomial pieces in one window, each with its own local minimum. A
  // real, reproduced case (TestCurveClosestPointAcrossClampedSeamKink):
  // with the true nearest point INSIDE the window on the far side of the
  // seam, a single golden section over the whole window walked to the
  // near side's own local minimum instead - 2.9x farther away - and a
  // plain sub-sampling pass did not cure it (both basins' minima sat
  // within one sub-step of the seam sample). The distance function can
  // only kink at a knot, so the window is split at every knot inside it
  // (wrapped copies too, for a closed curve, since the window may extend
  // past the seam) and each piece is polished on its own; a sub-sampled
  // scan of the window additionally localizes any smooth wiggle at the
  // sample scale, and the answer is the best of every polished piece and
  // every sample seen - so it is never worse than the coarse scan,
  // instead of "whichever side the golden section happened to pick".
  auto golden_section = [&](double a, double b) {
    const double golden_ratio = (std::sqrt(5.0) - 1.0) / 2.0;
    double c = b - golden_ratio * (b - a);
    double d = a + golden_ratio * (b - a);
    for (int iter = 0; iter < 100 && (b - a) > 1e-13; ++iter) {
      if (distance_squared_wrapped(c) < distance_squared_wrapped(d)) {
        b = d;
      } else {
        a = c;
      }
      c = b - golden_ratio * (b - a);
      d = a + golden_ratio * (b - a);
    }
    return (a + b) / 2.0;
  };
  auto consider = [&](double t) {
    const double d2 = distance_squared_wrapped(t);
    if (d2 < best_d2) {
      best_d2 = d2;
      best_t = t;
    }
  };

  constexpr int kSubSamples = 32;
  for (int i = 0; i <= kSubSamples; ++i) {
    consider(lo + (hi - lo) * static_cast<double>(i) / kSubSamples);
  }
  const double sub_step = (hi - lo) / kSubSamples;
  const double sub_lo = closed ? (best_t - sub_step) : std::max(domain.Min(), best_t - sub_step);
  const double sub_hi = closed ? (best_t + sub_step) : std::min(domain.Max(), best_t + sub_step);

  std::vector<double> breaks = {lo, hi, sub_lo, sub_hi};
  const double period = domain.Length();
  for (int i = 0; i < curve_.KnotCount(); ++i) {
    const double knot = curve_.Knot(i);
    for (int shift = -1; shift <= 1; ++shift) {
      if (shift != 0 && !closed) continue;
      const double shifted = knot + shift * period;
      if (shifted > lo && shifted < hi) breaks.push_back(shifted);
    }
  }
  std::sort(breaks.begin(), breaks.end());
  for (size_t i = 0; i + 1 < breaks.size(); ++i) {
    if (breaks[i + 1] - breaks[i] > 1e-13) consider(golden_section(breaks[i], breaks[i + 1]));
  }
  return wrap(best_t);
}

Point3d NurbsCurve::ClosestPoint(Point3d point, int samples) const {
  return PointAt(ClosestPointParameter(point, samples));
}

BoundingBox NurbsCurve::GetTightBoundingBox() const {
  ON_BoundingBox box;
  if (!curve_.GetTightBoundingBox(box)) {
    throw std::runtime_error(
        "dino8::kernel::NurbsCurve::GetTightBoundingBox: ON_Curve::"
        "GetTightBoundingBox failed");
  }
  return BoundingBox{box.Min(), box.Max()};
}

double NurbsCurve::Length(int samples) const {
  const ON_Interval domain = curve_.Domain();
  Point3d previous = PointAt(domain.ParameterAt(0.0));
  double length = 0.0;
  for (int i = 1; i <= samples; ++i) {
    const Point3d current = PointAt(domain.ParameterAt(static_cast<double>(i) / samples));
    length += (current - previous).Length();
    previous = current;
  }
  return length;
}

double NurbsCurve::ParameterAtArcLength(double target_length, int samples) const {
  const ON_Interval domain = curve_.Domain();
  if (target_length <= 0.0) {
    return domain.Min();
  }

  double previous_length = 0.0;
  Point3d previous_point = PointAt(domain.Min());
  double previous_t = domain.Min();
  for (int i = 1; i <= samples; ++i) {
    const double t = domain.ParameterAt(static_cast<double>(i) / samples);
    const Point3d point = PointAt(t);
    const double segment_length = (point - previous_point).Length();
    const double cumulative_length = previous_length + segment_length;
    if (cumulative_length >= target_length) {
      if (segment_length < 1e-15) {
        return t;
      }
      // Clamped to [0, 1]: `cumulative_length` is the ROUNDED sum
      // `previous_length + segment_length`, so `target_length -
      // previous_length` can exceed `segment_length` by a rounding ulp of
      // the (possibly huge) cumulative length. Unclamped, that put the
      // result past `t` - for `target_length == Length(samples)` exactly,
      // a few 1e-12 PAST `Domain().max` (reproduced on a curve whose first
      // polyline segment dwarfs its last; see
      // TestCurveParameterAtArcLengthStaysInsideDomain), breaking this
      // method's own "clamps to Domain().Min()/Max()" promise.
      const double fraction = std::min(1.0, std::max(0.0, (target_length - previous_length) / segment_length));
      return previous_t + fraction * (t - previous_t);
    }
    previous_length = cumulative_length;
    previous_point = point;
    previous_t = t;
  }
  return domain.Max();
}

std::vector<double> NurbsCurve::DivideByCount(int count, int samples) const {
  if (count <= 0) {
    throw std::invalid_argument("dino8::kernel::NurbsCurve::DivideByCount: count must be positive");
  }

  const ON_Interval domain = curve_.Domain();
  const double total_length = Length(samples);
  std::vector<double> values;
  values.reserve(static_cast<size_t>(count) + 1);
  values.push_back(domain.Min());
  for (int i = 1; i < count; ++i) {
    values.push_back(ParameterAtArcLength(total_length * static_cast<double>(i) / count, samples));
  }
  values.push_back(domain.Max());
  return values;
}

Vector3d NurbsCurve::TangentAt(double t) const { return curve_.TangentAt(t); }

bool NurbsCurve::IsClosed() const { return curve_.IsClosed(); }

bool NurbsCurve::IsPeriodic() const { return curve_.IsPeriodic(); }

bool NurbsCurve::IsPlanar(double tolerance) const { return curve_.IsPlanar(nullptr, tolerance); }

bool NurbsCurve::IsLinear(double tolerance) const { return curve_.IsLinear(tolerance); }

bool NurbsCurve::IsArc(double tolerance) const { return curve_.IsArc(nullptr, nullptr, tolerance); }

bool NurbsCurve::IsCircle(double tolerance) const {
  ON_Arc arc;
  if (!curve_.IsArc(nullptr, &arc, tolerance)) {
    return false;
  }
  return arc.IsCircle();
}

Result NurbsCurve::Reverse() { return curve_.Reverse() ? Result::Ok : Result::Failed; }

Result NurbsCurve::Trim(double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  return curve_.Trim(ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::Extend(double t0, double t1) {
  if (t0 >= t1) {
    return Result::Failed;
  }
  if (curve_.IsClosed()) {
    return Result::Failed;
  }
  const ON_Interval current = curve_.Domain();
  if (t0 >= current.Min() && t1 <= current.Max()) {
    return Result::NoOpAlreadySatisfied;
  }
  return curve_.Extend(ON_Interval(t0, t1)) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::Join(const NurbsCurve& other, double tolerance) {
  if (curve_.CVCount() < 2 || other.curve_.CVCount() < 2) {
    return Result::Failed;
  }
  if (curve_.IsClosed()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::Join: this curve is closed - nothing can be "
        "appended to a closed loop");
  }
  // ON_NurbsCurve::Append discards `other`'s first control point without
  // ever checking it coincides with this curve's last one (see the
  // header comment), so the meeting condition is enforced here.
  const Point3d end = curve_.PointAtEnd();
  const double gap_to_start = end.DistanceTo(other.curve_.PointAtStart());
  const double gap_to_end = end.DistanceTo(other.curve_.PointAtEnd());
  ON_NurbsCurve tail = other.curve_;
  if (gap_to_start > tolerance) {
    if (gap_to_end > tolerance) {
      throw std::invalid_argument(
          "dino8::kernel::NurbsCurve::Join: neither end of `other` meets this "
          "curve's end within tolerance " +
          std::to_string(tolerance) + " (gap to other's start " + std::to_string(gap_to_start) +
          ", gap to other's end " + std::to_string(gap_to_end) + ")");
    }
    if (!tail.Reverse()) {
      return Result::Failed;
    }
  }
  return curve_.Append(tail) ? Result::Ok : Result::Failed;
}

Result NurbsCurve::MakePeriodicExact() {
  if (curve_.IsPeriodic()) {
    return Result::NoOpAlreadySatisfied;
  }
  if (!IsClosed()) {
    return Result::Failed;
  }
  const int p = curve_.Degree();
  if (p < 2) {
    return Result::Failed;
  }

  // Work on a scratch copy so a failure partway through leaves curve_
  // untouched.
  ON_NurbsCurve c = curve_;
  const int order = c.Order();

  // Bezier-decompose: bring every interior knot up to full multiplicity
  // p, via real (already-verified-exact) Boehm knot insertion. After
  // this, every former breakpoint - including the two ends, which a
  // clamped curve already stores at multiplicity p - has the same,
  // uniform structure.
  {
    const int span_count = c.SpanCount();
    std::vector<double> span_vector(static_cast<size_t>(span_count) + 1);
    if (!c.GetSpanVector(span_vector.data())) {
      return Result::Failed;
    }
    for (int i = 1; i < span_count; ++i) {
      if (!c.InsertKnot(span_vector[static_cast<size_t>(i)], p)) {
        return Result::Failed;
      }
    }
  }

  const int n = c.CVCount() - 1;  // last CV index; CV[0] == CV[n] (closed)
  const int L = n;                // distinct CVs once the duplicate is dropped
  const int old_knot_count = c.KnotCount();
  std::vector<double> old_knots(static_cast<size_t>(old_knot_count));
  for (int i = 0; i < old_knot_count; ++i) old_knots[static_cast<size_t>(i)] = c.Knot(i);
  const double domain_min = old_knots[static_cast<size_t>(order - 2)];
  const double domain_max = old_knots[static_cast<size_t>(c.CVCount() - 1)];
  const double period = domain_max - domain_min;
  if (!(period > 0.0)) {
    return Result::Failed;
  }

  const int new_cv_count = L + p;
  const int new_knot_count = new_cv_count + order - 2;  // = old_knot_count + (p - 1)
  std::vector<double> new_knots(static_cast<size_t>(new_knot_count));
  for (int i = 0; i < old_knot_count; ++i) new_knots[static_cast<size_t>(i)] = old_knots[static_cast<size_t>(i)];
  // Extend past the old domain end by continuing the curve's own knot
  // spacing one period later - the periodic wraparound's own knots, not
  // a newly invented pattern. These land strictly outside [domain_min,
  // domain_max], so they take no part in reproducing the curve there.
  for (int j = 0; j < p - 1; ++j) {
    new_knots[static_cast<size_t>(old_knot_count + j)] = old_knots[static_cast<size_t>(p + j)] + period;
  }

  // See MakePeriodicExact()'s own doc comment: the last `p` knots (the
  // ex-clamped end's own full-multiplicity run, now landing at the new
  // domain's own end) collapse to a single, genuinely zero-width span at
  // the new domain boundary, which OpenNURBS' own rational evaluator
  // does not handle there (confirmed: it returns Inf/garbage, not an
  // approximate value). Spread that one run apart by a relative 1e-13 of
  // the period - the domain's own end point (last entry) is untouched,
  // so the reported domain does not move.
  constexpr double kRelativeSeparation = 1e-13;
  const double separation = kRelativeSeparation * period;
  const int domain_max_index = new_cv_count - 1;
  for (int k = 0; k < p - 1; ++k) {
    const int idx = domain_max_index - 1 - k;
    if (idx < 0) break;
    new_knots[static_cast<size_t>(idx)] = domain_max - static_cast<double>(k + 1) * separation;
  }

  ON_NurbsCurve pc;
  if (!pc.Create(3, c.IsRational(), order, new_cv_count)) {
    return Result::Failed;
  }
  for (int i = 0; i < L; ++i) {
    ON_4dPoint cv;
    c.GetCV(i, cv);
    pc.SetCV(i, cv);
  }
  for (int i = 0; i < p; ++i) {
    ON_4dPoint cv;
    c.GetCV(i, cv);
    pc.SetCV(L + i, cv);
  }
  for (int i = 0; i < new_knot_count; ++i) pc.SetKnot(i, new_knots[static_cast<size_t>(i)]);

  curve_ = pc;
  return Result::Ok;
}

Result NurbsCurve::Split(double t, NurbsCurve& out_left, NurbsCurve& out_right) const {
  ON_Curve* left_curve = nullptr;
  ON_Curve* right_curve = nullptr;
  const bool ok = curve_.Split(t, left_curve, right_curve);
  if (!ok) {
    delete left_curve;
    delete right_curve;
    return Result::Failed;
  }

  ON_NurbsCurve* left_nurbs = ON_NurbsCurve::Cast(left_curve);
  ON_NurbsCurve* right_nurbs = ON_NurbsCurve::Cast(right_curve);
  if (left_nurbs == nullptr || right_nurbs == nullptr) {
    delete left_curve;
    delete right_curve;
    return Result::Failed;
  }

  out_left.curve_ = *left_nurbs;
  out_right.curve_ = *right_nurbs;
  delete left_curve;
  delete right_curve;
  return Result::Ok;
}

Result NurbsCurve::OffsetInPlane(double distance, NurbsCurve& out, double tolerance,
                                  CurveOffsetCornerStyle corner_style) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetInPlane: distance must be finite");
  }

  const BoundingBox bbox = GetTightBoundingBox();
  const double diag = (bbox.max - bbox.min).Length();
  const double tol = tolerance > 0.0 ? tolerance : dino8::kernel::tolerance::DistanceForSize(diag);

  ON_Plane plane;
  if (!curve_.IsPlanar(&plane, tol)) {
    return Result::Failed;
  }

  if (distance == 0.0) {
    out.curve_ = curve_;
    return Result::Ok;
  }

  const Interval dom = Domain();
  const double t_mid = 0.5 * (dom.min + dom.max);

  // --- Line ------------------------------------------------------------
  if (curve_.IsLinear(tol)) {
    return OffsetLineAlongNormal(curve_.PointAtStart(), curve_.PointAtEnd(), plane.zaxis, distance, out);
  }

  // --- Polyline (3+ segments): exact per-corner miter, not a blurred fit -
  {
    Result polyline_result;
    NurbsCurve polyline_out;
    if (TryOffsetPolylineAlongNormal(*this, plane.zaxis, distance, tol, corner_style, polyline_out,
                                      polyline_result)) {
      if (polyline_result == Result::Ok) out = polyline_out;
      return polyline_result;
    }
  }

  // --- Circular arc / full circle ---------------------------------------
  {
    ON_Arc arc;
    if (curve_.IsArc(nullptr, &arc, tol)) {
      const Vector3d tangent = TangentAt(t_mid);
      Vector3d offset_dir = ON_CrossProduct(tangent, arc.plane.zaxis);
      Vector3d radial = PointAt(t_mid) - arc.Center();
      if (!offset_dir.Unitize() || !radial.Unitize()) return Result::Failed;
      const double sign = OffsetSignAlong(offset_dir, radial);
      const double new_radius = arc.radius + sign * distance;
      if (!(new_radius > 0.0)) return Result::Failed;
      const ON_Circle new_circle(arc.plane, new_radius);
      const ON_Arc new_arc(new_circle, arc.DomainRadians());
      ON_NurbsCurve nc;
      if (new_arc.GetNurbForm(nc) == 0) return Result::Failed;
      out.curve_ = nc;
      return Result::Ok;
    }
  }

  // --- General planar curve: approximate, curvature-checked ---------------
  return OffsetGeneralAlongNormal(*this, plane.zaxis, distance, tol, out);
}

Result NurbsCurve::OffsetInPlane(const ON_Plane& plane, double distance, NurbsCurve& out, double tolerance,
                                  CurveOffsetCornerStyle corner_style) const {
  if (!ON_IsValid(distance)) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetInPlane: distance must be finite");
  }
  if (!plane.IsValid()) {
    throw std::invalid_argument(
        "dino8::kernel::NurbsCurve::OffsetInPlane: plane must be valid");
  }

  const BoundingBox bbox = GetTightBoundingBox();
  const double diag = (bbox.max - bbox.min).Length();
  const double tol = tolerance > 0.0 ? tolerance : dino8::kernel::tolerance::DistanceForSize(diag);

  if (distance == 0.0) {
    out.curve_ = curve_;
    return Result::Ok;
  }

  // --- Line: still exact, for ANY plane not parallel to it -------------
  if (curve_.IsLinear(tol)) {
    return OffsetLineAlongNormal(curve_.PointAtStart(), curve_.PointAtEnd(), plane.zaxis, distance, out);
  }

  // --- Polyline (3+ segments), coplanar in `plane`: exact per-corner
  // miter. Falls through (not `false`-returning) to the general sampled
  // path below for a polyline that ISN'T coplanar in `plane` - see
  // `TryOffsetPolylineAlongNormal()`'s own doc comment for why that case
  // can't use the exact miter formula.
  {
    Result polyline_result;
    NurbsCurve polyline_out;
    if (TryOffsetPolylineAlongNormal(*this, plane.zaxis, distance, tol, corner_style, polyline_out,
                                      polyline_result)) {
      if (polyline_result == Result::Ok) out = polyline_out;
      return polyline_result;
    }
  }

  // --- Everything else, planar-in-its-own-plane or genuinely 3D --------
  // No `IsPlanar()` precondition here - the whole point of this overload
  // is offsetting along a caller-chosen plane's normal even when this
  // curve doesn't lie in any single plane at all.
  return OffsetGeneralAlongNormal(*this, plane.zaxis, distance, tol, out);
}

}  // namespace dino8::kernel
