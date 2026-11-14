// Regression for the openNURBS v8.34 ON_Surface::IsSphere out-of-bounds read (fixed by cmake/patches/opennurbs-8.34.26223-thread-safety.patch,
// applied at configure time). IsSphere copies the surface's u and v span vectors into ONE onmalloc block of (SpanCount(0)+SpanCount(1)+2)
// doubles, v vector last, then looped j <= SpanCount(1) and read t[j+1]: one double past the end of the block, for EVERY surface that
// gets past the iso-arc checks (every NURBS sphere, every NURBS form of a revolved arc). Unpatched, AddressSanitizer reports
// "heap-buffer-overflow ... in ON_Surface::IsSphere" from this test; the result itself was right, so only a sanitizer sees it.
//
// The boundary configurations are the ones where the off-by-one lands exactly on the allocation edge: the last v span, with one v span
// (SpanCount(1) == 1, the half circle revolved as a single rational arc), with several (knots inserted), and with the u span count
// larger and smaller than the v span count. Results are checked against the closed form (centre, radius), and the non-sphere neighbours
// (a revolved off-axis arc = torus, a revolved line = cylinder) must still be rejected.
//
// Under AddressSanitizer the test also installs an error-report callback and requires that no report was raised while it ran, so a
// regression fails here by name instead of only in the sanitizer job's log (the job counts reports inside fetched code as third-party).
#include <cmath>
#include <cstdio>
#include <memory>

#include "opennurbs.h"
#include "test_registry.h"

#if defined(__SANITIZE_ADDRESS__) && !defined(_MSC_VER)
#define D8_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define D8_TEST_ASAN 1
#endif
#endif

#ifdef D8_TEST_ASAN
extern "C" void __asan_set_error_report_callback(void (*callback)(const char*));
#endif

namespace {

int g_asan_reports = 0;
#ifdef D8_TEST_ASAN
void CountAsanReport(const char*) { ++g_asan_reports; }
#endif

// A semicircle of radius r in the xz plane from the south to the north pole, centred at `c`, as a rational NURBS arc.
std::unique_ptr<ON_NurbsCurve> HalfCircle(const ON_3dPoint& c, double r) {
  ON_Plane pl(c, ON_3dVector::XAxis, ON_3dVector::ZAxis);  // the circle's plane is spanned by x and z
  ON_Arc arc(ON_Circle(pl, r), ON_Interval(-0.5 * ON_PI, 0.5 * ON_PI));
  std::unique_ptr<ON_NurbsCurve> nc(new ON_NurbsCurve());
  arc.GetNurbForm(*nc);
  return nc;
}

// The revolved surface about the z axis through `c`, as the openNURBS surface classes see it.
std::unique_ptr<ON_RevSurface> Revolve(const ON_Curve& profile, const ON_3dPoint& c) {
  std::unique_ptr<ON_RevSurface> rs(new ON_RevSurface());
  rs->m_curve = profile.DuplicateCurve();
  rs->m_axis = ON_Line(c, c + ON_3dVector::ZAxis);
  rs->m_angle = ON_Interval(0.0, 2.0 * ON_PI);
  rs->m_t = rs->m_angle;
  rs->m_bTransposed = false;
  return rs;
}

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void CheckSphere(const ON_Surface& s, const ON_3dPoint& c, double r, const char* what) {
  char msg[200];
  ON_Sphere sph;
  const bool is = s.IsSphere(&sph, 1e-9);
  std::snprintf(msg, sizeof msg, "IsSphere (%s, %d x %d spans) accepts the sphere", what, s.SpanCount(0), s.SpanCount(1));
  d8test::Check(is, msg);
  if (!is) return;
  std::snprintf(msg, sizeof msg, "IsSphere (%s) radius %.12g == %.12g", what, sph.radius, r);
  d8test::Check(Near(sph.radius, r, 1e-9 * r), msg);
  std::snprintf(msg, sizeof msg, "IsSphere (%s) centre is (%.9g, %.9g, %.9g)", what, c.x, c.y, c.z);
  d8test::Check(sph.plane.origin.DistanceTo(c) <= 1e-9 * r, msg);
  // The tolerance-only overload (no output sphere) takes the same code path.
  std::snprintf(msg, sizeof msg, "IsSphere (%s) with a null sphere pointer agrees", what);
  d8test::Check(s.IsSphere(nullptr, 1e-9), msg);
}

}  // namespace

D8_EXTRA_TEST(OpenNurbsIsSphereSpanBounds) {
#ifdef D8_TEST_ASAN
  g_asan_reports = 0;
  __asan_set_error_report_callback(CountAsanReport);
#endif

  const ON_3dPoint c0(0.0, 0.0, 0.0);
  const ON_3dPoint c1(3.5, -2.0, 1.25);
  const double radii[] = {1.0, 7.25};

  for (double r : radii) {
    for (const ON_3dPoint& c : {c0, c1}) {
      // (1) revolved half circle, NURBS form: v (the revolve angle) is a single rational span or the profile has several.
      auto prof = HalfCircle(c, r);
      auto rev = Revolve(*prof, c);
      // the ON_RevSurface itself answers through IsSpherical (a different, bounds-safe path); it must agree
      CheckSphere(*rev, c, r, "ON_RevSurface");
      std::unique_ptr<ON_NurbsSurface> ns(rev->NurbsSurface());
      d8test::Check(ns != nullptr, "revolved half circle converts to a NURBS surface");
      if (ns) {
        CheckSphere(*ns, c, r, "revolved NURBS form");
        // (2) the same surface with knots inserted in both directions: more spans in u and in v, the u count above and below the v count
        for (int extra_u = 0; extra_u <= 3; extra_u += 3) {
          for (int extra_v = 0; extra_v <= 4; extra_v += 2) {
            std::unique_ptr<ON_NurbsSurface> d(new ON_NurbsSurface(*ns));
            for (int k = 1; k <= extra_u; ++k) {
              const ON_Interval du = d->Domain(0);
              d->InsertKnot(0, du.ParameterAt(double(k) / double(extra_u + 1)), 1);
            }
            for (int k = 1; k <= extra_v; ++k) {
              const ON_Interval dv = d->Domain(1);
              d->InsertKnot(1, dv.ParameterAt(double(k) / double(extra_v + 1)), 1);
            }
            char what[80];
            std::snprintf(what, sizeof what, "revolved NURBS form +%du +%dv knots", extra_u, extra_v);
            CheckSphere(*d, c, r, what);
          }
        }
      }
    }
    // (3) the openNURBS sphere's own NURBS form (rational, 2 x 4 spans in the u,v layout it produces)
    ON_Sphere sph(c1, r);
    std::unique_ptr<ON_NurbsSurface> sn(ON_NurbsSurface::New());
    d8test::Check(sph.GetNurbForm(*sn) != 0, "ON_Sphere::GetNurbForm builds a NURBS sphere");
    CheckSphere(*sn, c1, r, "ON_Sphere::GetNurbForm");
  }

  // Neighbours that must still be rejected (the loop bound change must not turn them into spheres).
  {
    // torus: a circle off the axis revolved
    ON_Plane pl(ON_3dPoint(4.0, 0.0, 0.0), ON_3dVector::XAxis, ON_3dVector::ZAxis);
    ON_NurbsCurve circ;
    ON_Circle(pl, 1.0).GetNurbForm(circ);
    auto tor = Revolve(circ, c0);
    std::unique_ptr<ON_NurbsSurface> tn(tor->NurbsSurface());
    d8test::Check(tn && !tn->IsSphere(nullptr, 1e-9), "IsSphere rejects the NURBS form of a torus");
    // cylinder: a line parallel to the axis revolved
    ON_NurbsCurve ln;
    ON_LineCurve(ON_3dPoint(2.0, 0.0, -1.0), ON_3dPoint(2.0, 0.0, 1.0)).GetNurbForm(ln);
    auto cyl = Revolve(ln, c0);
    std::unique_ptr<ON_NurbsSurface> cn(cyl->NurbsSurface());
    d8test::Check(cn && !cn->IsSphere(nullptr, 1e-9), "IsSphere rejects the NURBS form of a cylinder");
    // an ellipsoid-like surface: the half circle scaled 2:1 along z is not a sphere
    auto prof = HalfCircle(c0, 1.0);
    ON_Xform sc = ON_Xform::DiagonalTransformation(1.0, 1.0, 2.0);
    prof->Transform(sc);
    auto ell = Revolve(*prof, c0);
    std::unique_ptr<ON_NurbsSurface> en(ell->NurbsSurface());
    d8test::Check(en && !en->IsSphere(nullptr, 1e-9), "IsSphere rejects the NURBS form of a prolate spheroid");
  }

#ifdef D8_TEST_ASAN
  __asan_set_error_report_callback(nullptr);
  d8test::Check(g_asan_reports == 0, "no AddressSanitizer report was raised while IsSphere ran on every boundary configuration");
#endif
}
