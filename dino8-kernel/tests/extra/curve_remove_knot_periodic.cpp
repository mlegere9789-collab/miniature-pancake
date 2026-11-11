// NurbsCurve::RemoveKnotAt() on a PERIODIC curve (unclamped, cyclic knot vector).
//
// Oracle: the inverse of knot insertion. A periodic B-spline is built here by hand from cyclic control points and knot
// increments; one knot is inserted by hand with Boehm's formula (Q_i = a_i P_i + (1 - a_i) P_{i-1}, a_i = (u - U_i) /
// (U_{i+p} - U_i), applied cyclically, in homogeneous coordinates for the rational case) - code written in this file, not
// the kernel's InsertKnotAt, which does not handle periodic curves. Removing that knot must give back EXACTLY the
// original curve: same knot vector, same control points index by index, same shape, deviation bound ~ 0. A knot that
// carries shape must be refused at tolerance 1e-9 (curve untouched) and, with a loose tolerance, removed with the
// reported bound honoured at every sample.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/curve.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsCurve;
using dino8::kernel::Point3d;
using dino8::kernel::Result;

struct Hom {
  double x, y, z, w;
};

struct Spline {
  int p = 3;
  int n = 0;                    // unique control points
  std::vector<double> delta;    // n knot increments, cyclic
  std::vector<Hom> cyc;         // n homogeneous control points, cyclic
  bool rational = false;
};

// Unwrapped knot j of a spline whose knots start at U(0) = 0 with cyclic increments delta.
double Uw(const std::vector<double>& delta, int j) {
  const int n = static_cast<int>(delta.size());
  double period = 0.0;
  for (double d : delta) period += d;
  int q = j / n;
  int rem = j % n;
  if (rem < 0) {
    rem += n;
    --q;
  }
  double v = q * period;
  for (int k = 0; k < rem; ++k) v += delta[static_cast<size_t>(k)];
  return v;
}

NurbsCurve Build(int p, const std::vector<double>& knots_std, const std::vector<Hom>& cv_cyc, int anchor, bool rational) {
  // knots_std: textbook array of cv_count + p + 1 entries; cv i (0 .. cv_count - 1) = cv_cyc[(i - anchor) mod unique].
  const int unique = static_cast<int>(cv_cyc.size());
  const int cv_count = unique + p;
  NurbsCurve c;
  ON_NurbsCurve& r = c.raw();
  r.Create(3, rational, p + 1, cv_count);
  for (int k = 0; k < r.KnotCount(); ++k) r.SetKnot(k, knots_std[static_cast<size_t>(k + 1)]);
  for (int i = 0; i < cv_count; ++i) {
    const Hom& h = cv_cyc[static_cast<size_t>((((i - anchor) % unique) + unique) % unique)];
    if (rational) r.SetCV(i, ON_4dPoint(h.x, h.y, h.z, h.w));
    else r.SetCV(i, ON_3dPoint(h.x, h.y, h.z));
  }
  return c;
}

Spline MakeSpline(int p, int n, bool rational) {
  Spline s;
  s.p = p;
  s.n = n;
  s.rational = rational;
  const double inc[10] = {1.0, 1.3, 0.7, 1.1, 0.9, 1.4, 0.8, 1.0, 1.2, 0.6};
  for (int i = 0; i < n; ++i) s.delta.push_back(inc[i % 10] * (1.0 + 0.01 * i));
  for (int i = 0; i < n; ++i) {
    const double a = 6.283185307179586 * i / n;
    const double w = rational ? 1.0 + 0.3 * std::sin(2.0 * a + 0.4) : 1.0;
    const double x = (3.0 + std::cos(3.0 * a) * 0.8) * std::cos(a), y = (3.0 + std::cos(3.0 * a) * 0.8) * std::sin(a), z = 0.9 * std::sin(2.0 * a) + 0.1 * i;
    s.cyc.push_back(Hom{x * w, y * w, z * w, w});
  }
  return s;
}

NurbsCurve BuildOriginal(const Spline& s) {
  std::vector<double> U(static_cast<size_t>(s.n + 2 * s.p + 1));
  for (int j = 0; j < static_cast<int>(U.size()); ++j) U[static_cast<size_t>(j)] = Uw(s.delta, j);
  return Build(s.p, U, s.cyc, 0, s.rational);
}

// Hand Boehm insertion of u into the span [U(k), U(k + 1)), p <= k < p + n, applied cyclically with all periodic images.
NurbsCurve BuildRefined(const Spline& s, int k, double u) {
  const int p = s.p, n = s.n;
  auto P = [&](int i) { return s.cyc[static_cast<size_t>(((i % n) + n) % n)]; };
  std::vector<Hom> anchored;  // new cyclic list anchored at new index k - p: P_{k-p}, p blended, P_k .. P_{k+n-p-1}
  anchored.push_back(P(k - p));
  for (int i = k - p + 1; i <= k; ++i) {
    const double a = (u - Uw(s.delta, i)) / (Uw(s.delta, i + p) - Uw(s.delta, i));
    const Hom &x = P(i), &y = P(i - 1);
    anchored.push_back(Hom{a * x.x + (1 - a) * y.x, a * x.y + (1 - a) * y.y, a * x.z + (1 - a) * y.z, a * x.w + (1 - a) * y.w});
  }
  for (int i = k; i <= k + n - p - 1; ++i) anchored.push_back(P(i));
  // Knots: the n + 1 knots of the domain period [U(p), U(p + n)) with u inserted after U(k), extended periodically
  // (period = U(p + n) - U(p)) to both sides, so every periodic image of u is present in the textbook array too.
  std::vector<double> K;
  for (int i = 0; i < n; ++i) {
    K.push_back(Uw(s.delta, p + i));
    if (p + i == k) K.push_back(u);
  }
  const double period = Uw(s.delta, p + n) - Uw(s.delta, p);
  const int len = static_cast<int>(K.size());
  std::vector<double> U(static_cast<size_t>(n + 1 + 2 * p + 1));
  for (int j = 0; j < static_cast<int>(U.size()); ++j) {
    int q = (j - p) / len;
    if ((j - p) % len != 0 && (j - p) < 0) --q;
    U[static_cast<size_t>(j)] = K[static_cast<size_t>(j - p - q * len)] + q * period;
  }
  return Build(p, U, anchored, k - p, s.rational);
}

double MaxShapeDiff(const NurbsCurve& a, const NurbsCurve& b, int samples = 400) {
  double worst = 0.0;
  const double t0 = a.raw().Domain().Min(), t1 = a.raw().Domain().Max();
  for (int i = 0; i <= samples; ++i) {
    const double t = t0 + (t1 - t0) * i / samples;
    worst = std::max(worst, a.raw().PointAt(t).DistanceTo(b.raw().PointAt(t)));
  }
  return worst;
}

bool SameNet(const NurbsCurve& a, const NurbsCurve& b, double tol) {
  if (a.raw().CVCount() != b.raw().CVCount() || a.raw().KnotCount() != b.raw().KnotCount()) return false;
  for (int k = 0; k < a.raw().KnotCount(); ++k) {
    if (std::fabs(a.raw().Knot(k) - b.raw().Knot(k)) > tol) return false;
  }
  for (int i = 0; i < a.raw().CVCount(); ++i) {
    ON_4dPoint x, y;
    a.raw().GetCV(i, x);
    b.raw().GetCV(i, y);
    if (std::fabs(x.x - y.x) > tol || std::fabs(x.y - y.y) > tol || std::fabs(x.z - y.z) > tol || std::fabs(x.w - y.w) > tol) return false;
  }
  return true;
}

}  // namespace

D8_EXTRA_TEST(PeriodicKnotRemovalUndoesHandInsertion) {
  struct Case {
    int p, n, k;
    bool rational;
    const char* what;
  };
  const Case cases[] = {
      {3, 10, 6, false, "cubic, middle span"},          {3, 10, 3, false, "cubic, span right after the seam"},
      {3, 10, 12, false, "cubic, last span before the seam"}, {2, 9, 5, false, "quadratic, middle span"},
      {3, 10, 7, true, "rational cubic"},               {2, 9, 10, true, "rational quadratic, near the seam"},
      {4, 12, 9, false, "quartic"},
  };
  for (const Case& c : cases) {
    const Spline s = MakeSpline(c.p, c.n, c.rational);
    const NurbsCurve orig = BuildOriginal(s);
    const double u = 0.5 * (Uw(s.delta, c.k) + Uw(s.delta, c.k + 1));
    NurbsCurve refined = BuildRefined(s, c.k, u);
    const std::string tag = std::string(c.what) + ": ";
    d8test::Check(orig.raw().IsValid() && orig.raw().IsPeriodic() && orig.raw().IsClosed(), (tag + "setup: the hand-built original is a valid periodic closed curve").c_str());
    d8test::Check(refined.raw().IsValid() && refined.raw().IsPeriodic() && refined.raw().CVCount() == orig.raw().CVCount() + 1,
                  (tag + "setup: the hand-refined curve is valid, periodic, one control point larger").c_str());
    d8test::Check(MaxShapeDiff(orig, refined) < 1e-11, (tag + "setup: hand Boehm insertion preserves the shape (oracle check)").c_str());
    double dev = -1.0;
    const Result r = refined.RemoveKnotAt(c.k, 1e-9, &dev);
    d8test::Check(r == Result::Ok, (tag + "removing the inserted knot succeeds").c_str());
    if (r != Result::Ok) continue;
    d8test::Check(refined.raw().IsPeriodic() && refined.raw().IsClosed(), (tag + "result is still periodic and closed").c_str());
    d8test::Check(dev >= 0.0 && dev < 1e-10, (tag + "reported deviation bound is ~0 for an exactly removable knot").c_str());
    d8test::Check(SameNet(refined, orig, 1e-10), (tag + "knots and control points are the original ones, index by index").c_str());
    d8test::Check(MaxShapeDiff(refined, orig) < 1e-10, (tag + "shape equals the original at 400 samples").c_str());
  }
}

D8_EXTRA_TEST(PeriodicKnotRemovalHonoursToleranceAndRefusals) {
  const Spline s = MakeSpline(3, 10, false);
  const NurbsCurve orig = BuildOriginal(s);
  const int k = 6;  // ON index of an original (shape-carrying) knot
  {
    NurbsCurve c = orig;
    double dev = -1.0;
    const Result r = c.RemoveKnotAt(k, 1e-9, &dev);
    d8test::Check(r == Result::Failed && std::isfinite(dev) && dev > 1e-3, "a shape-carrying periodic knot is refused at 1e-9 with a finite measured bound");
    d8test::Check(SameNet(c, orig, 0.0), "a refused periodic removal leaves the curve bit-identical");
  }
  {
    NurbsCurve c = orig;
    double dev = -1.0;
    const Result r = c.RemoveKnotAt(k, 1e9, &dev);
    d8test::Check(r == Result::Ok && c.raw().IsPeriodic() && c.raw().IsClosed() && c.raw().CVCount() == orig.raw().CVCount() - 1,
                  "with a loose tolerance the knot is removed, leaving a periodic closed curve with one control point fewer");
    d8test::Check(r == Result::Ok && MaxShapeDiff(c, orig) <= dev + 1e-12, "the reported bound covers the measured deviation at every sample");
    d8test::Check(r == Result::Ok && dev > 1e-3, "and it is a real deviation, not zero");
  }
  {
    // Seam knots and out-of-domain indices keep their old contract.
    NurbsCurve c = orig;
    bool threw = false;
    try {
      c.RemoveKnotAt(0, 1e9);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    d8test::Check(threw, "a knot outside the domain still throws std::invalid_argument");
  }
  {
    // Too few control points left: a periodic cubic with 4 unique control points cannot lose one.
    const Spline tiny = MakeSpline(3, 4, false);
    NurbsCurve c = BuildOriginal(tiny);
    d8test::Check(c.RemoveKnotAt(4, 1e9) == Result::Failed, "a periodic curve that would drop to degree + 0 unique control points is refused");
  }
}
