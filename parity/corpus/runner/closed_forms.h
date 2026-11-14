// Closed-form oracles for the corpus (header only, no kernel dependency).
// Every function here is analytic geometry or a quadrature of an analytic
// integrand; none calls the kernel, so the oracle is independent of the code
// under test (plan section 9.2, two-key rule for kernel rows).
#pragma once
#include <cmath>
#include <functional>

namespace corpus {
namespace cf {

constexpr double PI = 3.14159265358979323846;

struct V3 {
  double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator*(double s, V3 a) { return a * s; }
inline V3 operator/(V3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 unit(V3 a) { return a / len(a); }

// --- primitive volumes, areas, centroids ------------------------------------
inline double box_vol(double a, double b, double c) { return a * b * c; }
inline double box_area(double a, double b, double c) { return 2 * (a * b + b * c + c * a); }
inline double cyl_vol(double r, double h) { return PI * r * r * h; }
inline double cyl_area(double r, double h) { return 2 * PI * r * r + 2 * PI * r * h; }
inline double sph_vol(double r) { return 4.0 / 3.0 * PI * r * r * r; }
inline double sph_area(double r) { return 4 * PI * r * r; }
inline double cone_vol(double r, double h) { return PI * r * r * h / 3.0; }
inline double cone_lateral(double r, double h) { return PI * r * std::sqrt(r * r + h * h); }
inline double cone_area(double r, double h) { return PI * r * r + cone_lateral(r, h); }
inline double torus_vol(double R, double r) { return 2 * PI * PI * R * r * r; }
inline double torus_area(double R, double r) { return 4 * PI * PI * R * r; }

// --- caps, segments, lenses -------------------------------------------------
// Volume of a spherical cap of height h on a sphere of radius R (exact polynomial).
inline double cap_vol(double R, double h) { return PI * h * h * (3 * R - h) / 3.0; }
inline double cap_area(double R, double h) { return 2 * PI * R * h; }  // curved part only
// Distance from the sphere centre to the centroid of a solid cap of height h (towards the cap).
inline double cap_centroid(double R, double h) { return 3 * (2 * R - h) * (2 * R - h) / (4 * (3 * R - h)); }

// Area of the circular segment of depth h (0 <= h <= 2r) of a circle of radius r.
// theta = half angle, cos(theta) = 1 - h/r.  A = r^2 (theta - sin(theta) cos(theta)).
// Small-angle series avoids the cancellation that ruins the direct form for h/r < 1e-4.
inline double seg_area(double r, double h) {
  if (h <= 0) return 0;
  if (h >= 2 * r) return PI * r * r;
  const double theta = 2.0 * std::asin(std::sqrt(h / (2.0 * r)));
  if (theta < 0.2) {
    const double t2 = theta * theta;
    // theta - sin(2 theta)/2 = 2/3 t^3 - 2/15 t^5 + 4/315 t^7 - 2/2835 t^9
    return r * r * theta * t2 * (2.0 / 3.0 - t2 * (2.0 / 15.0 - t2 * (4.0 / 315.0 - t2 * (2.0 / 2835.0))));
  }
  return r * r * (theta - std::sin(theta) * std::cos(theta));
}
// Distance from the circle centre to the centroid of the circular segment of depth h (towards the chord side).
inline double seg_centroid(double r, double h) {
  if (h <= 0) return r;
  const double theta = 2.0 * std::asin(std::sqrt(h / (2.0 * r)));
  const double s = std::sin(theta);
  return 4 * r * s * s * s / (3 * (2 * theta - std::sin(2 * theta)));
}
// Area of the lens common to two circles of radii r1, r2 whose centres are d apart.
inline double circle_lens_area(double r1, double r2, double d) {
  if (d >= r1 + r2) return 0;
  if (d <= std::fabs(r1 - r2)) { const double r = std::fmin(r1, r2); return PI * r * r; }
  const double x0 = (d * d + r1 * r1 - r2 * r2) / (2 * d);  // chord distance from circle 1's centre
  const double h1 = r1 - x0, h2 = r2 - (d - x0);
  return seg_area(r1, h1) + seg_area(r2, h2);
}
// Volume of the lens common to two spheres (radii R, r, centres d apart).
inline double sphere_lens_vol(double R, double r, double d) {
  if (d >= R + r) return 0;
  if (d <= std::fabs(R - r)) return sph_vol(std::fmin(R, r));
  const double x0 = (d * d + R * R - r * r) / (2 * d);
  return cap_vol(R, R - x0) + cap_vol(r, r - (d - x0));
}

// --- quadrature -------------------------------------------------------------
// Composite 5-point Gauss-Legendre on [a, b] after the substitution
// x = (a+b)/2 - (b-a)/2 cos(phi), which turns square-root endpoint behaviour
// (every integrand in this file) into a smooth integrand.  ~1e-14 accurate.
inline double integrate(const std::function<double(double)>& f, double a, double b, int panels = 400) {
  static const double xs[5] = {-0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640};
  static const double ws[5] = {0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891};
  const double mid = 0.5 * (a + b), half = 0.5 * (b - a);
  double sum = 0;
  for (int p = 0; p < panels; ++p) {
    const double p0 = PI * p / panels, p1 = PI * (p + 1) / panels, pm = 0.5 * (p0 + p1), ph = 0.5 * (p1 - p0);
    for (int k = 0; k < 5; ++k) {
      const double phi = pm + ph * xs[k];
      const double x = mid - half * std::cos(phi);
      sum += ws[k] * ph * f(x) * half * std::sin(phi);
    }
  }
  return sum;
}

// Volume of the intersection of two perpendicular cylinders: A along z (radius ra, centred x=0,y=0),
// B along x (radius rb) with its axis offset by `off` along y, both long enough to cross completely.
//   V = integral over y of 4 sqrt(ra^2 - y^2) sqrt(rb^2 - (y-off)^2)
inline double perp_cyl_vol(double ra, double rb, double off) {
  const double lo = std::fmax(off - rb, -ra), hi = std::fmin(off + rb, ra);
  if (hi <= lo) return 0;
  return integrate([&](double y) { return 4 * std::sqrt(std::fmax(0, ra * ra - y * y)) * std::sqrt(std::fmax(0, rb * rb - (y - off) * (y - off))); }, lo, hi);
}
// First moment (y direction) of the same intersection: integral of y * (cross-section).
inline double perp_cyl_moment_y(double ra, double rb, double off) {
  const double lo = std::fmax(off - rb, -ra), hi = std::fmin(off + rb, ra);
  if (hi <= lo) return 0;
  return integrate([&](double y) { return y * 4 * std::sqrt(std::fmax(0, ra * ra - y * y)) * std::sqrt(std::fmax(0, rb * rb - (y - off) * (y - off))); }, lo, hi);
}
// Area of cylinder A's wall (radius ra, axis z) that lies inside cylinder B (radius rb, axis x, offset `off` in y).
inline double perp_cyl_wallA_in_B(double ra, double rb, double off) {
  // wall point (ra cos p, ra sin p, z): inside B when (ra sin p - off)^2 + z^2 <= rb^2 -> z in +-sqrt(.)
  return integrate([&](double p) { const double y = ra * std::sin(p) - off; return 2 * std::sqrt(std::fmax(0, rb * rb - y * y)) * ra; }, 0, 2 * PI, 2000);
}
// Area of cylinder B's wall that lies inside cylinder A.
inline double perp_cyl_wallB_in_A(double ra, double rb, double off) {
  // wall point (x, off + rb cos q, rb sin q): inside A when x^2 + y^2 <= ra^2
  return integrate([&](double q) { const double y = off + rb * std::cos(q); return 2 * std::sqrt(std::fmax(0, ra * ra - y * y)) * rb; }, 0, 2 * PI, 2000);
}

}  // namespace cf
}  // namespace corpus
