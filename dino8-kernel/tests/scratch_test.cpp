#include <cstdio>
#include <cmath>
#include "dino8/kernel/surface.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/mesh.h"
#include "opennurbs.h"

int main() {
  ON::Begin();
  using namespace dino8::kernel;

  // A unit-radius full sphere: two genuine singular sides (poles), where
  // ON_Surface::IsSingular collapses an entire row of the NURBS grid to a
  // single point - does SuggestedDivisions()/SuggestedParameterValues()'s
  // isocurve-sampling-based curvature estimate actually resolve the real
  // facet deviation near a pole, where the OTHER direction's isocurve
  // (a circle of latitude) shrinks toward zero radius and its curvature
  // (1/radius) blows up?
  const ON_Sphere sphere(ON_3dPoint(0, 0, 0), 1.0);
  ON_NurbsSurface sphere_surface;
  const int rc = sphere.GetNurbForm(sphere_surface);
  std::printf("GetNurbForm rc=%d\n", rc);
  NurbsSurface s;
  s.raw() = sphere_surface;

  const auto du = s.Domain(0);
  const auto dv = s.Domain(1);
  std::printf("u domain: [%f, %f]\n", du.min, du.max);
  std::printf("v domain: [%f, %f]\n", dv.min, dv.max);
  std::printf("IsSingular(side 0, v=min): %d\n", s.raw().IsSingular(0));
  std::printf("IsSingular(side 1, u=max): %d\n", s.raw().IsSingular(1));
  std::printf("IsSingular(side 2, v=max): %d\n", s.raw().IsSingular(2));
  std::printf("IsSingular(side 3, u=min): %d\n", s.raw().IsSingular(3));

  for (double chord_tolerance : {0.1, 0.01, 0.001}) {
    const auto divs = s.SuggestedDivisions(chord_tolerance);
    const double measured = s.MeasureGridTessellationDeviation(divs.u, divs.v);
    std::printf("tol=%g  SuggestedDivisions u=%d v=%d  measured(whole grid)=%g  ratio=%.3f\n",
                chord_tolerance, divs.u, divs.v, measured, measured / chord_tolerance);
  }

  // Direct geometric check of the hypothesis: does the U-direction
  // isocurve (circle of latitude) shrink toward zero radius approaching
  // each pole, meaning its curvature (1/radius) grows without bound even
  // though SuggestedDivisions()'s own isocurve_samples are spaced evenly
  // across the FULL v domain and may land nowhere near the pole where
  // that spike is sharpest?
  {
    const double v_min = dv.min, v_max = dv.max;
    for (double frac : {0.0, 0.001, 0.01, 0.05, 0.1, 0.2, 0.5}) {
      const double v = v_min + frac * (v_max - v_min);
      ON_Curve* iso = s.raw().IsoCurve(0, v);
      double radius = -1.0;
      if (iso != nullptr) {
        const ON_3dPoint p0 = iso->PointAt(iso->Domain().Mid());
        const ON_3dPoint center(0, 0, p0.z);
        radius = p0.DistanceTo(center);
        delete iso;
      }
      std::printf("v-frac-from-pole=%.4f  latitude radius=%g\n", frac, radius);
    }
  }

  ON::End();
  return 0;
}
