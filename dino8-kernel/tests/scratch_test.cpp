#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/curve.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // Independent check of NurbsCurve::Helix(): sample the fitted curve
  // densely and hand-verify radial distance + height at many points,
  // not just the handful test_basic.cpp checks.
  {
    NurbsCurve helix;
    double max_dev = -1.0;
    const Result r = NurbsCurve::Helix(Point3d(0, 0, 0), Vector3d(0, 0, 1), 5.0, 2.0, 3.0, 1e-6, helix, &max_dev);
    std::printf("Helix result=%d max_dev=%g domain=[%g,%g] cv_count=%d\n", (int)r, max_dev, helix.Domain().min,
                helix.Domain().max, helix.ControlPointCount());

    double worst_radial_err = 0, worst_height_err = 0;
    const int N = 500;
    for (int i = 0; i <= N; ++i) {
      const double s = helix.Domain().min + (helix.Domain().max - helix.Domain().min) * i / N;
      const Point3d p = helix.PointAt(s);
      const double height = p.z;
      const double radial = std::sqrt(p.x * p.x + p.y * p.y);
      const double expected_height = 2.0 * s / (2.0 * ON_PI);
      worst_radial_err = std::max(worst_radial_err, std::fabs(radial - 5.0));
      worst_height_err = std::max(worst_height_err, std::fabs(height - expected_height));
    }
    std::printf("worst radial error over %d dense samples: %g\n", N, worst_radial_err);
    std::printf("worst height error over %d dense samples: %g\n", N, worst_height_err);
  }

  // Off-origin, tilted-axis case, tight tolerance - confirm convergence
  // and that deviation really is independently verifiable (project onto
  // axis using Gram-Schmidt from scratch, not reusing kernel internals).
  {
    NurbsCurve helix;
    double max_dev = -1.0;
    const Vector3d axis_dir(1, 1, 1);
    const Point3d origin(1, 2, 3);
    const Result r = NurbsCurve::Helix(origin, axis_dir, 3.0, 1.5, 5.0, 1e-8, helix, &max_dev);
    std::printf("\nTilted Helix result=%d max_dev=%g cv_count=%d\n", (int)r, max_dev, helix.ControlPointCount());

    Vector3d axis = axis_dir;
    axis.Unitize();
    double worst_radial_err = 0;
    const int N = 500;
    for (int i = 0; i <= N; ++i) {
      const double s = helix.Domain().min + (helix.Domain().max - helix.Domain().min) * i / N;
      const Point3d p = helix.PointAt(s);
      const Vector3d rel = p - origin;
      const double height = ON_DotProduct(rel, axis);
      const Vector3d radial_vec = rel - axis * height;
      worst_radial_err = std::max(worst_radial_err, std::fabs(radial_vec.Length() - 3.0));
    }
    std::printf("worst radial error over %d dense samples (tilted): %g\n", N, worst_radial_err);
  }

  // Degenerate / invalid-input probes.
  {
    NurbsCurve dummy;
    try {
      NurbsCurve::Helix(Point3d(0, 0, 0), Vector3d(0, 0, 0), 1.0, 1.0, 1.0, 1e-6, dummy);
      std::printf("ERROR: expected throw for zero axis_dir\n");
    } catch (const std::invalid_argument& e) {
      std::printf("OK threw for zero axis_dir: %s\n", e.what());
    }
  }

  ON::End();
  return 0;
}
