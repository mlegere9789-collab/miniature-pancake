// Self-test of the tests/extra/ mechanism (test_registry.h): two tiny tests
// that exercise registration, the d8test::Check output format and the kernel
// link. Cheap (microseconds); a template for new extra tests.
#include <cmath>
#include <vector>

#include "dino8/kernel/curve.h"
#include "test_registry.h"

D8_EXTRA_TEST(RegistrySelfTestStraightLineLength) {
  using dino8::kernel::NurbsCurve;
  using dino8::kernel::Point3d;
  const std::vector<Point3d> pts = {Point3d(0, 0, 0), Point3d(3, 4, 0)};
  const NurbsCurve line = NurbsCurve::FromControlPoints(pts, /*degree=*/1);
  d8test::Check(std::abs(line.Length(8) - 5.0) < 1e-9,
                "extra registry self-test: a straight 3-4-5 line has length 5");
}

D8_EXTRA_TEST(RegistrySelfTestSecondTestRuns) {
  d8test::Check(true, "extra registry self-test: a second registered test also ran");
}
