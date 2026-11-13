// NurbsSurface::RemoveKnotAt() on a PERIODIC direction.
//
// Oracle: the inverse of knot insertion, done by hand in this file (cyclic Boehm in homogeneous coordinates, written
// independently of the kernel) on every control line of a surface that is periodic in one direction (a hand-built
// non-uniform periodic spline in that direction, clamped quadratic in the other). Removing the inserted knot must return the
// ORIGINAL surface exactly: same knot vectors, same control net index by index (1e-10), same shape, bound ~0. A shape-carrying
// knot is refused at 1e-9 (surface bit-identical, finite measured bound) and removed at a loose tolerance with the reported bound
// covering the measured deviation.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::NurbsSurface;
using dino8::kernel::Result;

struct Hom {
  double x, y, z, w;
};

double Uw(const std::vector<double>& delta, int j) {
  const int n = static_cast<int>(delta.size());
  double period = 0.0;
  for (double d : delta) period += d;
  int q = j / n, rem = j % n;
  if (rem < 0) {
    rem += n;
    --q;
  }
  double v = q * period;
  for (int k = 0; k < rem; ++k) v += delta[static_cast<size_t>(k)];
  return v;
}

struct Line {
  std::vector<Hom> cyc;  // n unique homogeneous control points
};

// Periodic textbook knots from the domain-period knot list K (length len), period `period`, array size total.
std::vector<double> WrapKnots(const std::vector<double>& K, double period, int p, int total) {
  const int len = static_cast<int>(K.size());
  std::vector<double> U(static_cast<size_t>(total));
  for (int j = 0; j < total; ++j) {
    int q = (j - p) / len;
    if ((j - p) % len != 0 && (j - p) < 0) --q;
    U[static_cast<size_t>(j)] = K[static_cast<size_t>(j - p - q * len)] + q * period;
  }
  return U;
}

// Surface periodic in `dir` (cubic or quadratic p, n unique), clamped quadratic in the other direction (4 lines).
// `refine_k` < 0: the original; otherwise a knot u is inserted by hand into span k of every line.
NurbsSurface Build(int dir, int p, int n, bool rational, const std::vector<double>& delta, const std::vector<Line>& lines, int refine_k, double u) {
  const int m = static_cast<int>(lines.size());
  std::vector<double> K;
  for (int i = 0; i < n; ++i) {
    K.push_back(Uw(delta, p + i));
    if (refine_k >= 0 && p + i == refine_k) K.push_back(u);
  }
  const double period = Uw(delta, p + n) - Uw(delta, p);
  const int unique = static_cast<int>(K.size());
  const int cv_count = unique + p;
  const std::vector<double> U = WrapKnots(K, period, p, cv_count + p + 1);
  NurbsSurface s;
  ON_NurbsSurface& r = s.raw();
  if (dir == 0) r.Create(3, rational, p + 1, 3, cv_count, m);
  else r.Create(3, rational, 3, p + 1, m, cv_count);
  for (int k = 0; k < r.KnotCount(dir); ++k) r.SetKnot(dir, k, U[static_cast<size_t>(k + 1)]);
  const double vk[6] = {0, 0, 0, 1, 1, 1};  // clamped quadratic with 3 lines would be {0,0,0,1,1,1}; 4 lines use an interior knot
  (void)vk;
  const std::vector<double> other = m == 4 ? std::vector<double>{0, 0, 0, 0.5, 1, 1, 1} : std::vector<double>{0, 0, 0, 1, 1, 1};
  for (int k = 0; k < r.KnotCount(1 - dir); ++k) r.SetKnot(1 - dir, k, other[static_cast<size_t>(k + 1)]);
  for (int j = 0; j < m; ++j) {
    std::vector<Hom> list;
    if (refine_k < 0) {
      list = lines[static_cast<size_t>(j)].cyc;
    } else {
      auto P = [&](int i) { return lines[static_cast<size_t>(j)].cyc[static_cast<size_t>(((i % n) + n) % n)]; };
      list.push_back(P(refine_k - p));
      for (int i = refine_k - p + 1; i <= refine_k; ++i) {
        const double a = (u - Uw(delta, i)) / (Uw(delta, i + p) - Uw(delta, i));
        const Hom x = P(i), y = P(i - 1);
        list.push_back(Hom{a * x.x + (1 - a) * y.x, a * x.y + (1 - a) * y.y, a * x.z + (1 - a) * y.z, a * x.w + (1 - a) * y.w});
      }
      for (int i = refine_k; i <= refine_k + n - p - 1; ++i) list.push_back(P(i));
    }
    const int anchor = refine_k < 0 ? 0 : refine_k - p;
    const int ul = static_cast<int>(list.size());
    for (int i = 0; i < cv_count; ++i) {
      const Hom& h = list[static_cast<size_t>((((i - anchor) % ul) + ul) % ul)];
      const int a = dir == 0 ? i : j, b = dir == 0 ? j : i;
      if (rational) r.SetCV(a, b, ON_4dPoint(h.x, h.y, h.z, h.w));
      else r.SetCV(a, b, ON_3dPoint(h.x, h.y, h.z));
    }
  }
  return s;
}

double MaxShapeDiff(const NurbsSurface& a, const NurbsSurface& b) {
  double worst = 0.0;
  const ON_Interval du = a.raw().Domain(0), dv = a.raw().Domain(1);
  for (int i = 0; i <= 40; ++i)
    for (int j = 0; j <= 20; ++j) {
      const double u = du.ParameterAt(i / 40.0), v = dv.ParameterAt(j / 20.0);
      worst = std::max(worst, a.raw().PointAt(u, v).DistanceTo(b.raw().PointAt(u, v)));
    }
  return worst;
}

bool SameNet(const NurbsSurface& a, const NurbsSurface& b, double tol) {
  const ON_NurbsSurface &x = a.raw(), &y = b.raw();
  if (x.CVCount(0) != y.CVCount(0) || x.CVCount(1) != y.CVCount(1)) return false;
  for (int d = 0; d < 2; ++d) {
    if (x.Degree(d) != y.Degree(d) || x.KnotCount(d) != y.KnotCount(d)) return false;
    for (int k = 0; k < x.KnotCount(d); ++k)
      if (std::fabs(x.Knot(d, k) - y.Knot(d, k)) > tol) return false;
  }
  for (int i = 0; i < x.CVCount(0); ++i)
    for (int j = 0; j < x.CVCount(1); ++j) {
      ON_4dPoint p, q;
      x.GetCV(i, j, p);
      y.GetCV(i, j, q);
      if (std::fabs(p.x - q.x) > tol || std::fabs(p.y - q.y) > tol || std::fabs(p.z - q.z) > tol || std::fabs(p.w - q.w) > tol) return false;
    }
  return true;
}

std::vector<Line> MakeLines(int n, int m, bool rational) {
  std::vector<Line> L(static_cast<size_t>(m));
  for (int j = 0; j < m; ++j)
    for (int i = 0; i < n; ++i) {
      const double a = 6.283185307179586 * i / n;
      const double w = rational ? 1.0 + 0.3 * std::sin(2.0 * a + 0.4 + j) : 1.0;
      const double rr = 3.0 + 0.8 * std::cos(3.0 * a) + 0.4 * j;
      L[static_cast<size_t>(j)].cyc.push_back(Hom{rr * std::cos(a) * w, rr * std::sin(a) * w, (0.9 * std::sin(2.0 * a) + 1.3 * j) * w, w});
    }
  return L;
}

std::vector<double> Deltas(int n) {
  const double inc[10] = {1.0, 1.3, 0.7, 1.1, 0.9, 1.4, 0.8, 1.0, 1.2, 0.6};
  std::vector<double> d;
  for (int i = 0; i < n; ++i) d.push_back(inc[i % 10] * (1.0 + 0.01 * i));
  return d;
}

}  // namespace

D8_EXTRA_TEST(PeriodicSurfaceKnotRemovalUndoesHandInsertion) {
  struct Case {
    int dir, p, n, k;
    bool rational;
    const char* what;
  };
  const Case cases[] = {
      {0, 3, 10, 6, false, "U periodic cubic, middle span"},
      {0, 3, 10, 3, false, "U periodic cubic, span right after the seam"},
      {0, 3, 10, 12, false, "U periodic cubic, last span before the seam"},
      {0, 2, 9, 5, true, "U periodic rational quadratic"},
      {1, 3, 10, 7, true, "V periodic rational cubic"},
      {1, 2, 9, 10, false, "V periodic quadratic near the seam"},
  };
  for (const Case& c : cases) {
    const std::vector<double> delta = Deltas(c.n);
    const std::vector<Line> lines = MakeLines(c.n, 4, c.rational);
    const NurbsSurface orig = Build(c.dir, c.p, c.n, c.rational, delta, lines, -1, 0.0);
    const double u = 0.5 * (Uw(delta, c.k) + Uw(delta, c.k + 1));
    NurbsSurface refined = Build(c.dir, c.p, c.n, c.rational, delta, lines, c.k, u);
    const std::string tag = std::string(c.what) + ": ";
    d8test::Check(orig.raw().IsValid() && orig.raw().IsPeriodic(c.dir), (tag + "setup: the hand-built original is a valid surface periodic in that direction").c_str());
    d8test::Check(refined.raw().IsValid() && refined.raw().IsPeriodic(c.dir) && MaxShapeDiff(orig, refined) < 1e-11, (tag + "setup: hand Boehm insertion is valid, periodic and shape-preserving").c_str());
    double dev = -1.0;
    const Result r = refined.RemoveKnotAt(c.dir, c.k, 1e-9, &dev);
    d8test::Check(r == Result::Ok, (tag + "removing the inserted knot succeeds").c_str());
    if (r != Result::Ok) continue;
    d8test::Check(refined.raw().IsPeriodic(c.dir) && dev >= 0.0 && dev < 1e-10, (tag + "still periodic, bound ~0").c_str());
    d8test::Check(SameNet(refined, orig, 1e-10), (tag + "knots and control net are the original's").c_str());
    const double sd = MaxShapeDiff(refined, orig);
    std::printf("  %s shape deviation %.3e (bound %.3e)\n", c.what, sd, dev);
    d8test::Check(sd < 1e-10, (tag + "shape equals the original").c_str());
  }
}

D8_EXTRA_TEST(PeriodicSurfaceKnotRemovalToleranceAndRefusal) {
  const std::vector<double> delta = Deltas(10);
  const std::vector<Line> lines = MakeLines(10, 4, false);
  const NurbsSurface orig = Build(0, 3, 10, false, delta, lines, -1, 0.0);
  {
    NurbsSurface s = orig;
    double dev = -1.0;
    d8test::Check(s.RemoveKnotAt(0, 6, 1e-9, &dev) == Result::Failed && std::isfinite(dev) && dev > 1e-3 && SameNet(s, orig, 0.0), "a shape-carrying periodic knot is refused at 1e-9, bound measured, surface bit-identical");
  }
  {
    NurbsSurface s = orig;
    double dev = -1.0;
    const Result r = s.RemoveKnotAt(0, 6, 1e9, &dev);
    d8test::Check(r == Result::Ok && s.raw().IsPeriodic(0) && s.raw().CVCount(0) == orig.raw().CVCount(0) - 1, "with a loose tolerance it is removed, leaving a periodic direction one control row shorter");
    d8test::Check(r == Result::Ok && MaxShapeDiff(s, orig) <= dev + 1e-12 && dev > 1e-3, "the reported bound covers the measured deviation");
  }
}
