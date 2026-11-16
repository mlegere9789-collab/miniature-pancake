// Near-tangent sphere contacts of the golden/evil Boolean corpus. The seed meshes of the surface/surface intersector are inscribed in
// their surfaces (up to the chord tolerance inside), so a lens whose rim is a few hundredths across, a sphere sunk a thousandth into a
// face, and two spheres a millionth apart were each invisible or garbled before the closed-form circle seeds. Cases are built exactly as
// the corpus runner builds them (ON_BrepSphere with a generic pole axis, so the seam and poles sit where the corpus puts them) and
// expected values are the corpus's closed forms.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::IntersectionCurve;
using dino8::kernel::IntersectOptions;

ON_Plane FrameFor(const ON_3dPoint& origin, ON_3dVector axis) {
  axis.Unitize();
  const ON_3dVector seed = std::fabs(axis.x) < 0.9 ? ON_3dVector(1, 0, 0) : ON_3dVector(0, 1, 0);
  ON_3dVector x = seed - (seed * axis) * axis;
  x.Unitize();
  return ON_Plane(origin, x, ON_CrossProduct(axis, x));
}

const ON_3dVector kAxis1(0.2857142857142857, 0.42857142857142855, 0.8571428571428571);
const ON_3dVector kAxis2(-0.42857142857142855, 0.2857142857142857, 0.8571428571428571);

Brep Wrap(ON_Brep* b) {
  Brep k;
  k.raw() = *b;
  delete b;
  if (k.raw().IsSolid() && k.VolumeOfSealedSolid() < 0) k.raw().Flip();  // outward normals, as the corpus runner does
  return k;
}

ON_Brep* RawSphere(const ON_3dPoint& c, double r, const ON_3dVector& axis) {
  ON_Sphere s(c, r);
  s.plane = FrameFor(c, axis);
  return ON_BrepSphere(s);
}

Brep Sphere(const ON_3dPoint& c, double r, const ON_3dVector& axis) { return Wrap(RawSphere(c, r, axis)); }

Brep Box(double x0, double y0, double z0, double x1, double y1, double z1) {
  const ON_3dPoint c[8] = {ON_3dPoint(x0, y0, z0), ON_3dPoint(x1, y0, z0), ON_3dPoint(x1, y1, z0), ON_3dPoint(x0, y1, z0),
                           ON_3dPoint(x0, y0, z1), ON_3dPoint(x1, y0, z1), ON_3dPoint(x1, y1, z1), ON_3dPoint(x0, y1, z1)};
  return Wrap(ON_BrepBox(c));
}

bool RelNear(double got, double want, double rel) { return std::fabs(got - want) <= rel * std::fabs(want); }

double SphereVolume(double r) { return 4.0 * ON_PI * r * r * r / 3.0; }
double CapVolume(double r, double h) { return ON_PI * h * h * (3.0 * r - h) / 3.0; }

}  // namespace

// Two unit spheres overlapping by 1e-3: the lens rim is a circle of radius sqrt(d - d^2/4) = 0.0316 about the line of centres, far below
// one seed-mesh cell. The curve pieces (the rim is cut at the seams of both spheres) must add up to that circle and lie on both surfaces.
D8_EXTRA_TEST(SSXSphereSphereLensRimIsTheExactCircle) {
  ON_Brep* a = RawSphere(ON_3dPoint(0, 0, 0), 1.0, kAxis1);
  ON_Brep* b = RawSphere(ON_3dPoint(2.0 - 1e-3, 0, 0), 1.0, kAxis2);
  const IntersectOptions opt;
  const std::vector<IntersectionCurve> curves = dino8::kernel::IntersectFaces(&a->m_F[0], *a->m_F[0].SurfaceOf(), &b->m_F[0], *b->m_F[0].SurfaceOf(), opt);
  const double rho = std::sqrt(1e-3 - 0.25e-6);
  double length = 0, worst = 0;
  for (const IntersectionCurve& c : curves) {
    for (size_t i = 0; i < c.points.size(); ++i) {
      worst = std::max(worst, std::fabs(c.points[i].DistanceTo(ON_3dPoint(0, 0, 0)) - 1.0));
      worst = std::max(worst, std::fabs(c.points[i].DistanceTo(ON_3dPoint(2.0 - 1e-3, 0, 0)) - 1.0));
      if (i > 0) length += c.points[i].DistanceTo(c.points[i - 1]);
    }
  }
  d8test::Check(!curves.empty(), "overlapping unit spheres by 1e-3: the rim is found");
  d8test::Check(worst <= opt.tolerance, "overlapping unit spheres by 1e-3: every rim point lies on both spheres to the solve tolerance");
  d8test::Check(RelNear(length, 2.0 * ON_PI * rho, 0.03), "overlapping unit spheres by 1e-3: the rim pieces add up to the circle's circumference");
  delete a;
  delete b;
}

// A unit sphere 1e-6 inside a sphere of radius 2 along their internal tangency never touches it: no intersection curve exists, however
// close the surfaces come. The seed meshes used to produce dozens of spurious fragments that tore the boundary of the 2-1 difference.
D8_EXTRA_TEST(SSXNestedSpheresMicroGapHaveNoIntersectionAndBooleanKeepsTheVoid) {
  ON_Brep* a = RawSphere(ON_3dPoint(0, 0, 0), 2.0, kAxis1);
  ON_Brep* b = RawSphere(ON_3dPoint(1.0 - 1e-6, 0, 0), 1.0, kAxis2);
  const IntersectOptions opt;
  d8test::Check(dino8::kernel::IntersectFaces(&a->m_F[0], *a->m_F[0].SurfaceOf(), &b->m_F[0], *b->m_F[0].SurfaceOf(), opt).empty(),
                "a unit sphere 1e-6 inside a sphere of radius 2: no intersection curve");
  delete a;
  delete b;
  try {
    const auto s = BooleanCombineGeneralSealed(Sphere(ON_3dPoint(0, 0, 0), 2.0, kAxis1), Sphere(ON_3dPoint(1.0 - 1e-6, 0, 0), 1.0, kAxis2), BooleanOp::Difference);
    d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), "R2 sphere - nested unit sphere: a valid closed solid");
    d8test::Check(RelNear(std::fabs(s.brep.VolumeOfSealedSolid()), SphereVolume(2.0) - SphereVolume(1.0), 0.02), "R2 sphere - nested unit sphere: volume 4 pi / 3 x 7");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("R2 sphere - nested unit sphere threw: ") + e.what()).c_str());
  }
}

// Two unit spheres overlapping by 1e-3: the union loses a lens of volume 2 cap(1, 5e-4), the intersection IS that lens (1.57e-6). Both
// used to be wrong (a two-shell union, an empty intersection) because the lens rim was never found.
D8_EXTRA_TEST(BooleanSphereSphereOverlapByOneThousandthUnionAndLens) {
  const double lens = 2.0 * CapVolume(1.0, 0.5e-3);
  try {
    const Brep a = Sphere(ON_3dPoint(0, 0, 0), 1.0, kAxis1), b = Sphere(ON_3dPoint(2.0 - 1e-3, 0, 0), 1.0, kAxis2);
    const auto u = BooleanCombineGeneralSealed(a, b, BooleanOp::Union);
    d8test::Check(u.brep.raw().IsValid() && u.brep.raw().IsSolid(), "spheres overlapping by 1e-3, A+B: a valid closed solid");
    d8test::Check(RelNear(u.brep.VolumeOfSealedSolid(), 2.0 * SphereVolume(1.0) - lens, 0.02), "spheres overlapping by 1e-3, A+B: volume 8.3776");
    const auto i = BooleanCombineGeneralSealed(a, b, BooleanOp::Intersection);
    d8test::Check(i.brep.raw().IsSolid(), "spheres overlapping by 1e-3, A*B: a closed solid (the lens)");
    d8test::Check(RelNear(i.brep.VolumeOfSealedSolid(), lens, 0.05), "spheres overlapping by 1e-3, A*B: the lens volume 1.57e-6 within 5%");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("spheres overlapping by 1e-3 threw: ") + e.what()).c_str());
  }
}

// A unit sphere sunk 1e-3 into the top face of the box [0,4]^3 (generic pole axis): the contact is a circle of radius sqrt(2 r d - d^2) =
// 0.0447 in the face, which the face then carries as a hole. The union is the box plus the sphere minus the sunk cap.
D8_EXTRA_TEST(BooleanSphereSunkOneThousandthIntoBoxFaceUnion) {
  try {
    const Brep box = Box(0, 0, 0, 4, 4, 4), sphere = Sphere(ON_3dPoint(2, 2, 5 - 1e-3), 1.0, kAxis1);
    const auto u = BooleanCombineGeneralSealed(box, sphere, BooleanOp::Union);
    d8test::Check(u.brep.raw().IsValid() && u.brep.raw().IsSolid(), "sphere sunk 1e-3 into a box face, A+B: a valid closed solid");
    d8test::Check(RelNear(u.brep.VolumeOfSealedSolid(), 64.0 + SphereVolume(1.0) - CapVolume(1.0, 1e-3), 0.02),
                  "sphere sunk 1e-3 into a box face, A+B: volume 64 + 4 pi / 3 - cap");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("sphere sunk into a box face threw: ") + e.what()).c_str());
  }
}

// A unit sphere poking 1e-3 out of the inside of a sphere of radius 2 (internal tangency): A - B is the big ball with a dent whose
// volume is the lens of the two balls. The rim of that lens is 0.063 across.
D8_EXTRA_TEST(BooleanSphereSphereInternalTangencyPokingOutDifference) {
  try {
    const double e = 1e-3, R = 2.0, r = 1.0, d = 1.0 + e;
    const double x0 = (d * d + R * R - r * r) / (2.0 * d);  // radical plane, measured from A's centre
    const double lens = CapVolume(R, R - x0) + CapVolume(r, r - (d - x0));
    const auto s = BooleanCombineGeneralSealed(Sphere(ON_3dPoint(0, 0, 0), R, kAxis1), Sphere(ON_3dPoint(d, 0, 0), r, kAxis2), BooleanOp::Difference);
    d8test::Check(s.brep.raw().IsValid() && s.brep.raw().IsSolid(), "R2 sphere - unit sphere poking out by 1e-3: a valid closed solid");
    d8test::Check(RelNear(s.brep.VolumeOfSealedSolid(), SphereVolume(R) - lens, 0.02), "R2 sphere - unit sphere poking out by 1e-3: volume 4 pi / 3 x 8 - lens");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("R2 sphere - unit sphere poking out threw: ") + e.what()).c_str());
  }
}

// A ray whose origin is 1e-3 inside a unit sphere meets the sphere 1e-3 ahead. The seed mesh is inscribed (up to 0.02 inside the sphere),
// so the origin is outside the mesh and the ray crossed no triangle: the hit was missed and a point-in-solid parity count came out even.
D8_EXTRA_TEST(IntersectCurveSurfaceRayStartingInsideTheMeshSagFindsTheSphere) {
  ON_Brep* sphere = RawSphere(ON_3dPoint(0, 0, 0), 1.0, kAxis1);
  const ON_3dPoint origin(1.0 - 1e-3, 0, 0);
  const ON_LineCurve ray(origin, origin + ON_3dVector(1, 0.3, 0.2) * 3.0);
  const IntersectOptions opt;
  const auto hits = dino8::kernel::IntersectCurveSurface(ray, *sphere->m_F[0].SurfaceOf(), opt);
  d8test::Check(hits.size() == 1, "a ray starting 1e-3 inside a unit sphere: exactly one hit");
  if (!hits.empty()) {
    d8test::Check(std::fabs(hits[0].point.DistanceTo(ON_3dPoint(0, 0, 0)) - 1.0) < 2e-3, "that hit lies on the sphere");
    d8test::Check(hits[0].point.DistanceTo(origin) < 5e-3, "that hit is within a few thousandths of the ray origin");
  }
  delete sphere;
}
