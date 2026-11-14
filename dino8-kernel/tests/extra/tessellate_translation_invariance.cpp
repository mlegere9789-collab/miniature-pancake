// Adaptive closed-mesh tessellation must not depend on where the solid sits in world space.
//
// Defect (found by the thread audit): Brep::Sphere((cx,0,0), 1.5).TessellateToClosedMeshAdaptive(0.5) returned 8 faces at
// cx = 0 but 2,683,676 faces at cx = 1 (and std::bad_alloc at tolerance 0.01). Cause: the sphere's two pole isocurves are
// collapsed to a point (length ~2e-13); NurbsCurve::SuggestedSamples() took a curvature from the round-off in their
// evaluated positions (huge, arbitrary, proportional to the coordinate magnitude), then divided the total turning angle
// by the chord-height step, and asked for 1,341,838 samples. The fix treats a curve whose extent is round-off relative to
// its own coordinates as a point (one sample).
//
// Independent oracles (closed forms, not the tessellator):
//   * the expected division count: a circle of radius R needs ceil(2 pi / theta) segments, theta = 2 acos(1 - tol / R);
//     a UV sphere has 2 triangles per cell of an (around x half-around) grid, a torus 2 per cell of (major x minor);
//     a cylinder 4 triangles per around-segment (2 for the wall, two cap fans of n - 2 triangles each);
//   * the volume of the closed mesh against the closed-form volume, error bounded by area x tolerance (an inscribed
//     mesh whose chords deviate at most tol loses at most area x tol); a sphere's triangle centroids sit about 1.4 to 1.8
//     chord sagittas off the surface, so its bound is 2 x area x tol.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "opennurbs.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

constexpr double kPi = 3.14159265358979323846;
const double kCentres[] = {0.0, 1.0, 10.0, 1000.0};

double Segments(double radius, double tol) { return std::ceil(2.0 * kPi / (2.0 * std::acos(1.0 - tol / radius))); }

void Check(bool ok, const char* what) { d8test::Check(ok, what); }

void CheckWithin(int count, double lo, double hi, const char* shape, double cx, double tol) {
  char what[256];
  std::snprintf(what, sizeof what, "%s at cx=%g, tolerance %g: %d faces within the closed-form band [%.0f, %.0f]", shape, cx,
                tol, count, lo, hi);
  Check(count >= lo && count <= hi, what);
}

// faces(cx) against faces(0): the same solid moved must tessellate to a comparable count (1.25 x either way).
void CheckSameAsOrigin(int count, int at_origin, const char* shape, double cx, double tol) {
  char what[256];
  std::snprintf(what, sizeof what, "%s at cx=%g, tolerance %g: %d faces vs %d at the origin (within 1.25x)", shape, cx, tol,
                count, at_origin);
  Check(count <= 1.25 * at_origin && at_origin <= 1.25 * count, what);
}

Brep MakeSphere(double cx) { return Brep::Sphere(Point3d(cx, 0, 0), 1.5); }
Brep MakeCylinder(double cx) { return Brep::CylinderWelded(Point3d(cx, 0, 0), Vector3d(0, 0, 1), 1.5, 3.0); }
Brep MakeTorus(double cx) { return Brep::Torus(Point3d(cx, 0, 0), Vector3d(0, 0, 1), 3.0, 1.0); }

}  // namespace

// The reported case: sphere radius 1.5 at tolerance 0.5 was 8 faces at the origin and 2,683,676 at cx = 1.
D8_EXTRA_TEST(TessellateAdaptiveSphereFaceCountIndependentOfCentre) {
  for (const double tol : {0.5, 0.05, 0.01}) {
    const double n = Segments(1.5, tol);
    const double estimate = 2.0 * n * (n / 2.0);  // around x half-around cells, 2 triangles each
    int at_origin = -1;
    for (const double cx : kCentres) {
      const Mesh m = MakeSphere(cx).TessellateToClosedMeshAdaptive(tol);
      if (cx == 0.0) at_origin = m.FaceCount();
      CheckWithin(m.FaceCount(), 0.25 * estimate, 4.0 * estimate, "sphere", cx, tol);
      CheckSameAsOrigin(m.FaceCount(), at_origin, "sphere", cx, tol);
      char what[200];
      std::snprintf(what, sizeof what, "sphere at cx=%g, tolerance %g: the adaptive mesh is a closed manifold", cx, tol);
      Check(m.IsClosedManifold(), what);
    }
  }
}

// The angle-driven sibling shares the sampling estimate and must not depend on the centre either.
D8_EXTRA_TEST(TessellateAdaptiveByAngleSphereFaceCountIndependentOfCentre) {
  for (const double angle : {0.5, 0.2}) {
    int at_origin = -1;
    for (const double cx : kCentres) {
      const Mesh m = MakeSphere(cx).TessellateToClosedMeshAdaptiveByAngle(angle);
      if (cx == 0.0) at_origin = m.FaceCount();
      const double n = std::ceil(2.0 * kPi / angle);
      CheckWithin(m.FaceCount(), 0.25 * n * n, 4.0 * n * n, "sphere (by angle)", cx, angle);
      CheckSameAsOrigin(m.FaceCount(), at_origin, "sphere (by angle)", cx, angle);
    }
  }
}

D8_EXTRA_TEST(TessellateAdaptiveCylinderFaceCountIndependentOfCentre) {
  for (const double tol : {0.5, 0.05, 0.01}) {
    const double n = Segments(1.5, tol);
    int at_origin = -1;
    for (const double cx : kCentres) {
      const Mesh m = MakeCylinder(cx).TessellateToClosedMeshAdaptive(tol);
      if (cx == 0.0) at_origin = m.FaceCount();
      CheckWithin(m.FaceCount(), 2.0 * n, 8.0 * n, "cylinder", cx, tol);
      CheckSameAsOrigin(m.FaceCount(), at_origin, "cylinder", cx, tol);
    }
  }
}

D8_EXTRA_TEST(TessellateAdaptiveTorusFaceCountIndependentOfCentre) {
  for (const double tol : {0.5, 0.05, 0.01}) {
    const double nu = Segments(4.0, tol);  // the major circle at its outer equator, radius R + r
    const double nv = Segments(1.0, tol);  // the minor circle
    const double estimate = 2.0 * nu * nv;
    int at_origin = -1;
    for (const double cx : kCentres) {
      const Mesh m = MakeTorus(cx).TessellateToClosedMeshAdaptive(tol);
      if (cx == 0.0) at_origin = m.FaceCount();
      CheckWithin(m.FaceCount(), 0.25 * estimate, 4.0 * estimate, "torus", cx, tol);
      CheckSameAsOrigin(m.FaceCount(), at_origin, "torus", cx, tol);
    }
  }
}

// Volume of the closed mesh against the closed form, at every centre. (Mesh vertices are single precision, so a solid at
// cx = 1000 carries ~6e-5 of position rounding: the same bound holds, and the centre-to-centre agreement is relative 1e-3.)
D8_EXTRA_TEST(TessellateAdaptiveClosedMeshVolumeWithinToleranceAtAnyCentre) {
  struct Shape {
    const char* name;
    Brep (*make)(double);
    double volume, area, slack;
  };
  const Shape shapes[] = {
      {"sphere", &MakeSphere, 4.0 / 3.0 * kPi * 1.5 * 1.5 * 1.5, 4.0 * kPi * 1.5 * 1.5, 2.0},
      {"cylinder", &MakeCylinder, kPi * 1.5 * 1.5 * 3.0, 2.0 * kPi * 1.5 * 3.0 + 2.0 * kPi * 1.5 * 1.5, 1.0},
      {"torus", &MakeTorus, 2.0 * kPi * kPi * 3.0 * 1.0 * 1.0, 4.0 * kPi * kPi * 3.0 * 1.0, 1.0},
  };
  for (const Shape& s : shapes) {
    for (const double tol : {0.1, 0.01}) {
      double at_origin = 0.0;
      for (const double cx : kCentres) {
        const double v = s.make(cx).TessellateToClosedMeshAdaptive(tol).Volume();
        if (cx == 0.0) at_origin = v;
        char what[256];
        std::snprintf(what, sizeof what, "%s at cx=%g, tolerance %g: volume %.5f vs closed form %.5f, error %.3e <= %.1f x area x tol = %.3e",
                      s.name, cx, tol, v, s.volume, v - s.volume, s.slack, s.slack * s.area * tol);
        Check(std::fabs(v - s.volume) <= s.slack * s.area * tol, what);
        std::snprintf(what, sizeof what, "%s at cx=%g, tolerance %g: volume %.5f agrees with the origin's %.5f to 1e-3 relative",
                      s.name, cx, tol, v, at_origin);
        Check(std::fabs(v - at_origin) <= 1e-3 * s.volume, what);
      }
    }
  }
}

// The cause, one level down: the collapsed pole isocurve of a sphere is a point, so one sample, wherever the sphere is;
// the live isocurves keep their closed-form counts.
D8_EXTRA_TEST(SphereIsocurveSuggestedSamplesDoNotDependOnCentre) {
  const double tol = 0.5;
  for (const double cx : kCentres) {
    const Brep sphere = MakeSphere(cx);
    const ON_Brep& ob = sphere.raw();
    bool all_ok = ob.m_F.Count() > 0;
    for (int i = 0; i < ob.m_F.Count(); ++i) {
      ON_NurbsSurface ns;
      ob.m_S[ob.m_F[i].m_si]->GetNurbForm(ns);
      NurbsSurface w;
      w.raw() = ns;
      const dino8::kernel::SurfaceDivisions d = w.SuggestedDivisions(tol);
      all_ok = all_ok && d.u >= 1 && d.u <= 8 && d.v >= 1 && d.v <= 8;
      // pole isocurves: v at the two ends of the domain
      for (const double f : {0.0, 1.0}) {
        const NurbsCurve pole = w.IsoCurve(0, w.raw().Domain(1).ParameterAt(f));
        all_ok = all_ok && pole.SuggestedSamples(tol) == 1 && pole.SuggestedSamplesByAngle(0.3) == 1;
      }
    }
    char what[200];
    std::snprintf(what, sizeof what, "sphere at cx=%g: pole isocurves get 1 sample and SuggestedDivisions(%g) stays within 8", cx, tol);
    Check(all_ok, what);
  }
}

// The guard must not swallow a genuinely tiny curve: a circle of radius 1e-3 needs many samples at tolerance 1e-6, at the
// origin and far from it (its extent is 2e-3, 2e-6 of the largest coordinate at x = 1000, far above round-off).
D8_EXTRA_TEST(SuggestedSamplesStillResolvesSmallRealCircleAtAnyCentre) {
  const double radius = 1e-3, tol = 1e-6;
  const double expected = Segments(radius, tol);
  for (const double cx : kCentres) {
    const ON_Circle circle(ON_Plane(ON_3dPoint(cx, 0, 0), ON_3dVector(0, 0, 1)), radius);
    ON_NurbsCurve nc;
    circle.GetNurbForm(nc);
    NurbsCurve c;
    c.raw() = nc;
    const int n = c.SuggestedSamples(tol);
    char what[200];
    std::snprintf(what, sizeof what, "circle radius %g at cx=%g, tolerance %g: %d samples within 0.5x..2x of the closed form %.0f", radius, cx,
                  tol, n, expected);
    Check(n >= 0.5 * expected && n <= 2.0 * expected, what);
  }
}
