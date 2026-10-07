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
    for (int samples : {5, 10, 20, 40}) {
      const auto divs = s.SuggestedDivisions(chord_tolerance, samples);
      const double measured = s.MeasureGridTessellationDeviation(divs.u, divs.v);
      std::printf("tol=%g  isocurve_samples=%d  SuggestedDivisions u=%d v=%d  measured=%g  ratio=%.3f\n",
                  chord_tolerance, samples, divs.u, divs.v, measured, measured / chord_tolerance);
    }
  }

  // Isolate which direction's UNIFORM-parameter-spacing grid dominates
  // the excess deviation: hold u_divisions artificially high (so U can't
  // be the bottleneck) and vary only v_divisions.
  for (double chord_tolerance : {0.01}) {
    const auto divs = s.SuggestedDivisions(chord_tolerance);
    for (int test_u : {divs.u, 400}) {
      const double measured = s.MeasureGridTessellationDeviation(test_u, divs.v);
      std::printf("tol=%g  u_divisions=%d (v fixed at suggested %d)  measured=%g  ratio=%.3f\n",
                  chord_tolerance, test_u, divs.v, measured, measured / chord_tolerance);
    }
    for (int test_v : {divs.v, 200}) {
      const double measured = s.MeasureGridTessellationDeviation(divs.u, test_v);
      std::printf("tol=%g  v_divisions=%d (u fixed at suggested %d)  measured=%g  ratio=%.3f\n",
                  chord_tolerance, test_v, divs.u, measured, measured / chord_tolerance);
    }
  }

  // Does the REAL non-uniform SuggestedParameterValues(1, tol) (genuine
  // per-region adaptivity, via recursive 3D midpoint-deviation bisection -
  // not assuming uniform parameter speed) actually bound deviation near
  // the poles the way the uniform SuggestedDivisions()/TessellateGrid()
  // path above apparently does not?
  for (double chord_tolerance : {0.1, 0.01, 0.001}) {
    const auto v_values = s.SuggestedParameterValues(1, chord_tolerance);
    double worst = 0.0;
    double worst_v0 = 0, worst_v1 = 0;
    const int u_probe_samples = 40;
    for (size_t vi = 0; vi + 1 < v_values.size(); ++vi) {
      const double v0 = v_values[vi], v1 = v_values[vi + 1];
      const double vm = 0.5 * (v0 + v1);
      for (int ui = 0; ui <= u_probe_samples; ++ui) {
        const double u = du.min + (du.max - du.min) * static_cast<double>(ui) / u_probe_samples;
        const Point3d p0 = s.PointAt(u, v0);
        const Point3d p1 = s.PointAt(u, v1);
        const Point3d pm_true = s.PointAt(u, vm);
        const Point3d chord_mid((p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5, (p0.z + p1.z) * 0.5);
        const double dev = (pm_true - chord_mid).Length();
        if (dev > worst) { worst = dev; worst_v0 = v0; worst_v1 = v1; }
      }
    }
    std::printf("tol=%g  SuggestedParameterValues(1) count=%zu  worst V-direction midpoint "
                "deviation=%g  ratio=%.3f  (worst segment v=[%g,%g])\n",
                chord_tolerance, v_values.size(), worst, worst / chord_tolerance, worst_v0, worst_v1);
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

  for (double chord_tolerance : {0.1, 0.01, 0.001}) {
    try {
      double achieved = 0.0;
      const Mesh m = s.TessellateGridCertifiedAdaptive(chord_tolerance, 8, &achieved);
      std::printf("tol=%g  TessellateGridCertifiedAdaptive OK, faces=%d achieved=%g ratio=%.3f\n",
                  chord_tolerance, m.FaceCount(), achieved, achieved / chord_tolerance);
    } catch (const std::exception& e) {
      std::printf("tol=%g  TessellateGridCertifiedAdaptive threw: %s\n", chord_tolerance, e.what());
    }
  }

  ON::End();
  return 0;
}
