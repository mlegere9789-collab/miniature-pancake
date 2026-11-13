#pragma once

// Exact-structure degree reduction of one clamped NURBS curve by one degree, shared by NurbsCurve::ReduceDegree and
// NurbsSurface::ReduceDegree (which applies it to every control row / column of the net: the operation is linear in the
// homogeneous control points, so one curve routine serves both).

#include <array>
#include <vector>

#include <opennurbs.h>

namespace dino8::kernel::detail {

// One degree of reduction on the Bezier chain of a clamped curve (Piegl & Tiller, The NURBS Book, 5.5): the curve is cut
// into Bezier spans (every interior knot raised to multiplicity p), each span of degree p is written as a degree p - 1
// Bezier whose degree elevation it is - the left half of the control points from the elevation formula
// P_i = (i / p) Q_{i-1} + (1 - i / p) Q_i solved forward, the right half solved backward - and the spans are chained C0
// (end points kept exactly). For a curve that IS the elevation of a degree p - 1 curve every span is recovered exactly;
// for any other curve the result is an approximation the caller must measure. The chain has multiplicity p - 1 at every
// former knot; the caller removes the knots again (RemoveMultiKnots). Homogeneous coordinates, so a rational curve
// reduces the same way (weights included). False when the curve cannot be treated (degree < 2, unclamped, a
// non-positive weight appears).
inline bool ReduceOneDegreeBezierChain(const ON_NurbsCurve& in, ON_NurbsCurve& out) {
  const int p = in.Degree();
  if (p < 2 || !in.IsClamped(2)) return false;
  ON_NurbsCurve c = in;
  const ON_Interval dom = c.Domain();
  std::vector<double> breaks = {dom.Min()};
  for (int k = 0; k < c.KnotCount(); ++k) {
    const double u = c.Knot(k);
    if (u > dom.Min() && u < dom.Max() && u != breaks.back()) breaks.push_back(u);
  }
  breaks.push_back(dom.Max());
  for (size_t k = 1; k + 1 < breaks.size(); ++k) {
    if (!c.InsertKnot(breaks[k], p)) return false;
  }
  const int cv_count = c.CVCount();
  const int nseg = static_cast<int>(breaks.size()) - 1;
  if (cv_count != nseg * p + 1) return false;
  const bool rational = c.IsRational();
  const int q = p - 1;
  const int m = q / 2;  // Q_0 .. Q_m from the left recursion, Q_{m+1} .. Q_q from the right one
  std::vector<std::array<double, 4>> chain;
  chain.reserve(static_cast<size_t>(nseg) * static_cast<size_t>(q) + 1);
  for (int s = 0; s < nseg; ++s) {
    std::vector<std::array<double, 4>> P(static_cast<size_t>(p) + 1), Q(static_cast<size_t>(q) + 1);
    for (int i = 0; i <= p; ++i) {
      ON_4dPoint h;
      c.GetCV(s * p + i, h);
      if (!rational) h.w = 1.0;
      P[static_cast<size_t>(i)] = {h.x, h.y, h.z, h.w};
    }
    Q[0] = P[0];
    for (int i = 1; i <= m; ++i) {
      for (int k = 0; k < 4; ++k) Q[static_cast<size_t>(i)][static_cast<size_t>(k)] = (p * P[static_cast<size_t>(i)][static_cast<size_t>(k)] - i * Q[static_cast<size_t>(i) - 1][static_cast<size_t>(k)]) / (p - i);
    }
    Q[static_cast<size_t>(q)] = P[static_cast<size_t>(p)];
    for (int i = q; i >= m + 2; --i) {
      for (int k = 0; k < 4; ++k) Q[static_cast<size_t>(i) - 1][static_cast<size_t>(k)] = (p * P[static_cast<size_t>(i)][static_cast<size_t>(k)] - (p - i) * Q[static_cast<size_t>(i)][static_cast<size_t>(k)]) / i;
    }
    for (int j = (s == 0 ? 0 : 1); j <= q; ++j) chain.push_back(Q[static_cast<size_t>(j)]);
  }
  std::vector<double> std_knots;
  for (int j = 0; j <= q; ++j) std_knots.push_back(breaks.front());
  for (size_t k = 1; k + 1 < breaks.size(); ++k) {
    for (int j = 0; j < q; ++j) std_knots.push_back(breaks[k]);
  }
  for (int j = 0; j <= q; ++j) std_knots.push_back(breaks.back());
  ON_NurbsCurve r;
  if (!r.Create(3, rational, q + 1, static_cast<int>(chain.size()))) return false;
  if (static_cast<int>(std_knots.size()) != r.CVCount() + q + 1) return false;
  for (int k = 0; k < r.KnotCount(); ++k) r.SetKnot(k, std_knots[static_cast<size_t>(k) + 1]);
  for (int i = 0; i < r.CVCount(); ++i) {
    const std::array<double, 4>& h = chain[static_cast<size_t>(i)];
    if (rational) {
      if (!(h[3] > 0.0)) return false;
      r.SetCV(i, ON_4dPoint(h[0], h[1], h[2], h[3]));
    } else {
      r.SetCV(i, ON_3dPoint(h[0], h[1], h[2]));
    }
  }
  if (!r.IsValid()) return false;
  out = r;
  return true;
}


}  // namespace dino8::kernel::detail
