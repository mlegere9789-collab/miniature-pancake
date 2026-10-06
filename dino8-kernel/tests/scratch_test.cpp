#include <cstdio>
#include <cmath>
#include <vector>
#include "dino8/kernel/curve.h"

using namespace dino8::kernel;

int main() {
  ON::Begin();

  const double radius = 5.0;
  const ON_Circle on_circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), radius);
  ON_NurbsCurve nurbs_form;
  on_circle.GetNurbForm(nurbs_form);
  NurbsCurve circle;
  circle.raw() = nurbs_form;

  const ON_Interval domain = nurbs_form.Domain();
  printf("domain = [%.6f, %.6f]\n", domain.Min(), domain.Max());
  printf("knot count = %d, cv count = %d, order = %d\n", nurbs_form.KnotCount(),
         nurbs_form.CVCount(), nurbs_form.Order());
  for (int i = 0; i < nurbs_form.KnotCount(); ++i) {
    printf("  knot[%d] = %.6f\n", i, nurbs_form.Knot(i));
  }

  // Chord-based SuggestedParameterValues: check actual spacing uniformity
  // at several tolerances, including much tighter ones that force deeper
  // recursion - does non-uniformity ever show up, or is this curve's
  // chord-deviation criterion genuinely uniform at every depth?
  for (double chord_tolerance : {0.01, 0.001, 0.0001, 0.00001}) {
    const auto chord_values = circle.SuggestedParameterValues(chord_tolerance);
    double min_d = 1e300, max_d = -1e300;
    for (size_t i = 1; i < chord_values.size(); ++i) {
      double d = chord_values[i] - chord_values[i - 1];
      min_d = std::min(min_d, d);
      max_d = std::max(max_d, d);
    }
    printf("CHORD tol=%.6f: segments=%d min_delta=%.10f max_delta=%.10f ratio=%.6f\n",
           chord_tolerance, (int)chord_values.size() - 1, min_d, max_d, max_d / min_d);
  }

  // Print tangent magnitude at several t to check if TangentAt is unit.
  for (double frac : {0.0, 0.05, 0.1, 0.125, 0.25, 0.5}) {
    double t = domain.ParameterAt(frac);
    Vector3d tan = circle.TangentAt(t);
    printf("  t_frac=%.4f tangent_len=%.8f\n", frac, tan.Length());
  }

  return 0;
}
