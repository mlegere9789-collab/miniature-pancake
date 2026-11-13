// Splitting a solid by a plane (SplitByPlane, the exact planar route, and SplitBrepByPlane, the general route that also takes curved
// faces) and by a sheet (SplitBySheet) into two CLOSED solids, against closed forms that do not come from the code under test:
//   * the volume of each half from first principles (box: wedge / corner-tetrahedron / prism volumes; cylinder: pi r^2 times a height,
//     or a circular-segment area times the height; a plane through the centre of a centrally symmetric body halves it),
//   * the two halves add back up to the original solid's volume,
//   * the topology: every half is ON_Brep::IsValid() and IsSolid() (closed, oriented, manifold) and its Euler characteristic
//     V - E + F - R (R = inner loops) is 2, with the exact vertex / edge / face counts of the polyhedron for the planar cases (a cube vertex
//     is kept if it lies on the kept side; every cube edge crossing the plane adds a vertex; a cube face is kept if one of its vertices is;
//     the cut adds one face; E = V + F - 2),
//   * each half lies on its own side of the plane.
// The cylinder cases include the planes that cross the cylinder wall's periodic seam at a point a cut-curve sample sits (almost) exactly
// on: z = 4 + 0.25 x with the seam at 0 / 180 degrees, z = 3 + 0.25 x at 0 / 45 degrees, z = 4.3 + 0.25 x at 45 degrees, z = 4 + 0.25 x at 45 degrees (the cut chain of that one has a
// kink - two SSX samples out of order - that makes the tessellation in UnifyNormals() throw after the edges were sewn). Before the seam
// handling, the sealing of SplitBrepByPlane's halves and the tolerance of that throw, their halves were open shells (IsSolid false, chi 1).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::Point3d;
using dino8::kernel::Vector3d;

struct Topo {
  int v = 0, e = 0, f = 0, inner = 0;
  int Euler() const { return v - e + f - inner; }
};

Topo Count(const Brep& b) {
  Topo t;
  const ON_Brep& r = b.raw();
  t.v = r.m_V.Count();
  t.e = r.m_E.Count();
  t.f = r.m_F.Count();
  for (int i = 0; i < r.m_L.Count(); ++i) t.inner += r.m_L[i].m_type == ON_BrepLoop::inner ? 1 : 0;
  return t;
}

bool Closed(const Brep& b) {
  ON_TextLog log;
  bool oriented = false, boundary = true;
  return b.raw().IsValid(&log) && b.raw().IsManifold(&oriented, &boundary) && oriented && !boundary && b.raw().IsSolid();
}

double Vol(const Brep& b) {
  try {
    return b.VolumeOfSealedSolid();
  } catch (const std::exception&) {
    return std::nan("");
  }
}

// Every vertex lies on the requested side of {p : n.p = off} (to `tol`).
bool OnSide(const Brep& b, Vector3d n, double off, bool plus, double tol) {
  n.Unitize();
  for (int i = 0; i < b.raw().m_V.Count(); ++i) {
    const double d = ON_DotProduct(n, Vector3d(b.raw().m_V[i].point)) - off;
    if (plus ? d < -tol : d > tol) return false;
  }
  return true;
}

std::string Fmt(const char* label, const char* what) { return std::string(label) + ": " + what; }

struct PlanarCase {
  const char* label;
  Vector3d n;
  double off;  // along the UNIT normal
  double vol_plus, vol_minus;
  Topo plus, minus;
};

}  // namespace

// The exact planar route on a 10 x 10 x 10 cube, against the polyhedra worked out by hand.
D8_EXTRA_TEST(SplitSolidByPlaneCubeObliqueExactHalves) {
  const Brep cube = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
  const double s3 = std::sqrt(3.0), s5 = std::sqrt(5.0);
  // {V, E, F, inner}. x + y + z = 6 takes the corner tetrahedron (volume 6^3 / 6 = 36) off the origin; x + y + z = 12 keeps
  // 12^3 / 6 - 3 (12 - 10)^3 / 6 = 288 - 4 = 284 below; x + y + z = 15 is the hexagonal section through the centre; x + 2 y = 10 is a
  // prism cut: the triangle (0,0) (10,0) (0,5) of area 25 times the height 10 = 250 (the plane passes exactly through the vertex (10, 0)).
  const std::vector<PlanarCase> cases = {
      {"x = 5", Vector3d(1, 0, 0), 5.0, 500.0, 500.0, {8, 12, 6, 0}, {8, 12, 6, 0}},
      {"x + y + z = 6 (corner tetrahedron)", Vector3d(1, 1, 1) / s3, 6.0 / s3, 964.0, 36.0, {10, 15, 7, 0}, {4, 6, 4, 0}},
      {"x + y + z = 12", Vector3d(1, 1, 1) / s3, 12.0 / s3, 716.0, 284.0, {10, 15, 7, 0}, {10, 15, 7, 0}},
      {"x + y + z = 15 (hexagonal section through the centre)", Vector3d(1, 1, 1) / s3, 15.0 / s3, 500.0, 500.0, {10, 15, 7, 0}, {10, 15, 7, 0}},
      {"x + 2 y = 10 (through the vertex (10, 0, z))", Vector3d(1, 2, 0) / s5, 10.0 / s5, 750.0, 250.0, {8, 12, 6, 0}, {6, 9, 5, 0}},
  };
  for (const PlanarCase& c : cases) {
    const auto [plus, minus] = dino8::kernel::SplitByPlane(cube, c.n, c.off);
    d8test::Check(Closed(plus) && Closed(minus), Fmt(c.label, "both halves are valid, closed, oriented solids").c_str());
    const Topo tp = Count(plus), tm = Count(minus);
    d8test::Check(tp.Euler() == 2 && tm.Euler() == 2, Fmt(c.label, "Euler characteristic V - E + F is 2 on both halves").c_str());
    d8test::Check(tp.v == c.plus.v && tp.e == c.plus.e && tp.f == c.plus.f, Fmt(c.label, "the +normal half has the hand-counted V / E / F").c_str());
    d8test::Check(tm.v == c.minus.v && tm.e == c.minus.e && tm.f == c.minus.f, Fmt(c.label, "the opposite half has the hand-counted V / E / F").c_str());
    const double vp = Vol(plus), vm = Vol(minus);
    d8test::Check(std::fabs(vp - c.vol_plus) < 1e-7 && std::fabs(vm - c.vol_minus) < 1e-7, Fmt(c.label, "each half's exact volume matches its closed form").c_str());
    d8test::Check(std::fabs(vp + vm - 1000.0) < 1e-7, Fmt(c.label, "the two halves add up to the cube's 1000").c_str());
    d8test::Check(OnSide(plus, c.n, c.off, true, 1e-7) && OnSide(minus, c.n, c.off, false, 1e-7), Fmt(c.label, "each half lies on its own side of the plane").c_str());
  }
}

// A plane through the centre of a (rotated, translated) box halves it whatever its normal is: central symmetry. The box is 6 x 4 x 10.
D8_EXTRA_TEST(SplitSolidByPlaneRotatedBoxCentralPlanesHalve) {
  ON_Xform place;
  place.Rotation(0.5236, ON_3dVector(0.3, 0.2, 1.0), ON_3dPoint(0, 0, 0));
  const ON_Xform move = ON_Xform::TranslationTransformation(ON_3dVector(7, -3, 2));
  const Brep box = Brep::BoxWelded(-3, -2, -5, 3, 2, 5).Transform(move * place);
  const ON_3dPoint centre = move * place * ON_3dPoint(0, 0, 0);
  const std::vector<Vector3d> normals = {Vector3d(0.3, -0.5, 0.8), Vector3d(1, 0.1, 0), Vector3d(-0.2, 0.9, 0.35)};
  for (size_t k = 0; k < normals.size(); ++k) {
    Vector3d n = normals[k];
    n.Unitize();
    const double off = ON_DotProduct(n, Vector3d(centre));
    const std::string label = "rotated box, plane " + std::to_string(k) + " through the centre";
    try {
      const auto [plus, minus] = dino8::kernel::SplitByPlane(box, n, off);
      d8test::Check(Closed(plus) && Closed(minus), (label + ": both halves are valid, closed solids").c_str());
      d8test::Check(Count(plus).Euler() == 2 && Count(minus).Euler() == 2, (label + ": Euler characteristic 2 on both halves").c_str());
      const double vp = Vol(plus), vm = Vol(minus);
      d8test::Check(std::fabs(vp - 120.0) < 1e-6 && std::fabs(vm - 120.0) < 1e-6, (label + ": each half is 120 (half of 6 x 4 x 10)").c_str());
    } catch (const std::exception& e) {
      d8test::Check(false, (label + ": threw " + e.what()).c_str());
    }
  }
}

// A plane that misses the solid leaves it whole on one side and nothing on the other.
D8_EXTRA_TEST(SplitSolidByPlaneMissLeavesWholeSolidOnOneSide) {
  const Brep cube = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
  const auto [plus, minus] = dino8::kernel::SplitByPlane(cube, Vector3d(1, 0, 0), 20.0);
  d8test::Check(plus.FaceCount() == 0, "a plane at x = 20 leaves nothing on its +normal side");
  d8test::Check(Closed(minus) && std::fabs(Vol(minus) - 1000.0) < 1e-7 && Count(minus).Euler() == 2, "...and the whole cube, closed, volume 1000, chi 2, on the other");
}

namespace {

// A closed r-radius, len-long cylinder about +z from the origin: an exact cylindrical wall and two 128-gon caps (FromMixedFaces).
ON_Plane AxisFrame() {
  ON_Plane f;
  f.origin = ON_3dPoint(0, 0, 0);
  f.xaxis = ON_3dVector(0, -1, 0);
  f.yaxis = ON_3dVector(1, 0, 0);
  f.zaxis = ON_3dVector(0, 0, 1);
  f.UpdateEquation();
  return f;
}

Brep::PlanarFace DiskCap(const ON_Plane& fr, double h, double r, bool flip, int n) {
  Brep::PlanarFace pf;
  const ON_3dPoint c = fr.origin + fr.zaxis * h;
  pf.plane = ON_Plane(c, flip ? -fr.zaxis : fr.zaxis);
  for (int i = 0; i < n; ++i) {
    const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
    pf.loop.push_back(c + fr.xaxis * (r * std::cos(a)) + fr.yaxis * (r * std::sin(a)));
  }
  pf.notch_begin = 0;
  pf.notch_count = static_cast<int>(pf.loop.size());
  return pf;
}

Brep Cylinder(double r, double len) {
  Brep::CylindricalFace cf;
  cf.frame = AxisFrame();
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = len;
  return Brep::FromMixedFaces({DiskCap(cf.frame, 0, r, true, 128), DiskCap(cf.frame, len, r, false, 128)}, {cf});
}

constexpr double kR = 3.0, kH = 8.0;
const double kCylVol = ON_PI * kR * kR * kH;

// One cylinder split: both halves closed, chi 2, volumes within `rel` of the closed forms, sum within 1e-4 of pi r^2 h.
void CheckCylinderSplit(const std::string& label, const Brep& cyl, Vector3d n, double off, double want_plus, double want_minus, double rel) {
  try {
    n.Unitize();
    const auto [plus, minus] = dino8::kernel::SplitBrepByPlane(cyl, n, off);
    d8test::Check(Closed(plus) && Closed(minus), (label + ": both halves are valid, closed, oriented solids").c_str());
    d8test::Check(Count(plus).Euler() == 2 && Count(minus).Euler() == 2, (label + ": Euler characteristic V - E + F is 2 on both halves").c_str());
    const double vp = Vol(plus), vm = Vol(minus);
    char buf[200];
    std::snprintf(buf, sizeof buf, ": +normal half %.5f (closed form %.5f), opposite half %.5f (closed form %.5f)", vp, want_plus, vm, want_minus);
    d8test::Check(std::fabs(vp - want_plus) <= rel * want_plus && std::fabs(vm - want_minus) <= rel * want_minus, (label + buf).c_str());
    d8test::Check(std::fabs(vp + vm - kCylVol) <= 1e-4 * kCylVol, (label + ": the halves add up to pi r^2 h within 1e-4").c_str());
    d8test::Check(OnSide(plus, n, off, true, 2e-3) && OnSide(minus, n, off, false, 2e-3), (label + ": each half lies on its own side of the plane").c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (label + ": threw " + e.what()).c_str());
  }
}

}  // namespace

// Planes perpendicular to / parallel to the cylinder axis: the halves are a shorter cylinder and a circular-segment prism.
D8_EXTRA_TEST(SplitSolidByPlaneCylinderAxisAlignedPlanes) {
  const Brep cyl = Cylinder(kR, kH);
  const double pr2 = ON_PI * kR * kR;
  CheckCylinderSplit("cylinder cut by z = 2.5", cyl, Vector3d(0, 0, 1), 2.5, pr2 * (kH - 2.5), pr2 * 2.5, 1e-4);
  // x = 1: the circular segment beyond the chord at distance d = 1: r^2 acos(d / r) - d sqrt(r^2 - d^2), times the height.
  const double seg = kR * kR * std::acos(1.0 / kR) - 1.0 * std::sqrt(kR * kR - 1.0);
  CheckCylinderSplit("cylinder cut by x = 1 (parallel to the axis)", cyl, Vector3d(1, 0, 0), 1.0, seg * kH, pr2 * kH - seg * kH, 1e-4);
  CheckCylinderSplit("cylinder cut by x = 0 (through the axis)", cyl, Vector3d(1, 0, 0), 0.0, 0.5 * pr2 * kH, 0.5 * pr2 * kH, 1e-4);
}

// Planes z = z0 + s x through the axis cylinder: by rotational symmetry about the axis the volume below is pi r^2 z0 whatever the
// slope, as long as the plane stays between the caps (z0 +- 3 s inside [0, 8]). The cylinder is also turned about its axis so that the
// cut curve meets the wall's seam line at different places (the seam is at angle 0 of its own frame).
D8_EXTRA_TEST(SplitSolidByPlaneCylinderObliquePlanesAcrossTheSeam) {
  struct Case {
    double z0, slope, rotate_deg;
  };
  const std::vector<Case> cases = {{4.0, 0.5, 0.0},   {4.0, 1.0, 0.0},   {4.0, 0.25, 0.0}, {4.0, 0.25, 180.0}, {3.0, 0.25, 0.0},
                                   {3.0, 0.25, 45.0}, {4.3, 0.25, 45.0}, {3.0, 0.25, 90.0}, {4.3, 0.25, 0.0}, {4.0, 0.25, 45.0}};
  const double pr2 = ON_PI * kR * kR;
  for (const Case& c : cases) {
    ON_Xform turn;
    turn.Rotation(c.rotate_deg * ON_PI / 180.0, ON_3dVector(0, 0, 1), ON_3dPoint(0, 0, 0));
    const Brep cyl = Cylinder(kR, kH).Transform(turn);
    char label[120];
    std::snprintf(label, sizeof label, "cylinder cut by z = %.2f + %.2f x, seam turned by %.0f degrees", c.z0, c.slope, c.rotate_deg);
    // the +normal side (normal (-s, 0, 1)) is the part ABOVE the plane
    CheckCylinderSplit(label, cyl, Vector3d(-c.slope, 0, 1), c.z0 / std::sqrt(1.0 + c.slope * c.slope), pr2 * (kH - c.z0), pr2 * c.z0, 3e-3);
  }
}

namespace {

Brep PlaneSheet(Vector3d n, double off, double half) {
  n.Unitize();
  const ON_Plane pl(ON_3dPoint::Origin + n * off, n);
  const std::vector<Point3d> grid = {pl.origin - pl.xaxis * half - pl.yaxis * half, pl.origin - pl.xaxis * half + pl.yaxis * half,
                                     pl.origin + pl.xaxis * half - pl.yaxis * half, pl.origin + pl.xaxis * half + pl.yaxis * half};
  return Brep::FromSurface(dino8::kernel::NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1));
}

}  // namespace

// Splitting by a SHEET (an open tilted plane patch): the same closed-form halves, with SplitBySheet's own two pieces.
D8_EXTRA_TEST(SplitSolidBySheetTiltedPlaneHalves) {
  const double s3 = std::sqrt(3.0);
  {
    const Brep cube = Brep::BoxWelded(0, 0, 0, 10, 10, 10);
    const auto [a, b] = dino8::kernel::SplitBySheet(cube, PlaneSheet(Vector3d(1, 1, 1), 12.0 / s3, 40.0));
    d8test::Check(Closed(a) && Closed(b), "cube split by the sheet x + y + z = 12: both pieces are valid, closed solids");
    d8test::Check(Count(a).Euler() == 2 && Count(b).Euler() == 2, "...with Euler characteristic 2");
    const double lo = std::min(Vol(a), Vol(b)), hi = std::max(Vol(a), Vol(b));
    d8test::Check(std::fabs(lo - 284.0) < 1e-6 && std::fabs(hi - 716.0) < 1e-6, "...and volumes 284 (x + y + z < 12) and 716, the closed forms");
  }
  {
    const Brep cyl = Cylinder(kR, kH);
    const auto [a, b] = dino8::kernel::SplitBySheet(cyl, PlaneSheet(Vector3d(0, 0, 1), 2.5, 40.0));
    d8test::Check(Closed(a) && Closed(b) && Count(a).Euler() == 2 && Count(b).Euler() == 2, "cylinder split by the sheet z = 2.5: both pieces are valid, closed solids with Euler characteristic 2");
    const double pr2 = ON_PI * kR * kR;
    const double lo = std::min(Vol(a), Vol(b)), hi = std::max(Vol(a), Vol(b));
    d8test::Check(std::fabs(lo - pr2 * 2.5) <= 1e-4 * pr2 * 2.5 && std::fabs(hi - pr2 * 5.5) <= 1e-4 * pr2 * 5.5, "...with volumes pi r^2 * 2.5 and pi r^2 * 5.5 to 1e-4");
  }
}

// SplitBrepByPlaneSealed hands back closed halves or refuses with a typed reason. Where the plane stays between the caps it succeeds; the
// measured refusal is the plane that crosses the planar end caps (z = z0 + s x with z0 +- 3 s outside [0, 8]): the engine leaves the cap chords
// and the wall's rim arcs unpaired, so the halves are valid but open.
D8_EXTRA_TEST(SplitSolidByPlaneSealedClosedOrTypedRefusal) {
  using dino8::kernel::BooleanFailureReason;
  using dino8::kernel::BooleanOperationError;
  const Brep cyl = Cylinder(kR, kH);
  const double pr2 = ON_PI * kR * kR;
  {
    Vector3d n(-1.0, 0, 1);  // z = 4 + x: the plane reaches z = 1 and 7, inside the caps
    n.Unitize();
    try {
      const auto [plus, minus] = dino8::kernel::SplitBrepByPlaneSealed(cyl, n, n.z * 4.0);
      d8test::Check(Closed(plus) && Closed(minus), "SplitBrepByPlaneSealed, z = 4 + x: both halves closed");
      d8test::Check(std::fabs(Vol(plus) - 0.5 * pr2 * kH) <= 3e-3 * 0.5 * pr2 * kH && std::fabs(Vol(minus) - 0.5 * pr2 * kH) <= 3e-3 * 0.5 * pr2 * kH,
                    "...each is half of pi r^2 h (3e-3)");
    } catch (const std::exception& e) {
      d8test::Check(false, (std::string("SplitBrepByPlaneSealed, z = 4 + x threw: ") + e.what()).c_str());
    }
  }
  {
    const auto [plus, minus] = dino8::kernel::SplitBrepByPlaneSealed(cyl, Vector3d(1, 0, 0), 20.0);  // misses: one side empty, not a refusal
    d8test::Check(plus.FaceCount() == 0 && minus.FaceCount() > 0, "SplitBrepByPlaneSealed with a plane that misses: an empty side and the whole body, no refusal");
  }
  struct Cross {
    double z0, slope, rotate_deg;
  };
  for (const Cross& c : {Cross{4.0, 1.5, 0.0}, Cross{4.0, 2.0, 0.0}, Cross{3.0, 1.2, 0.0}, Cross{4.0, 1.5, 10.0}, Cross{4.0, 1.4, 0.0}}) {
    ON_Xform turn;
    turn.Rotation(c.rotate_deg * ON_PI / 180.0, ON_3dVector(0, 0, 1), ON_3dPoint(0, 0, 0));
    Vector3d n(-c.slope, 0, 1);
    n.Unitize();
    char label[120];
    std::snprintf(label, sizeof label, "plane z = %.1f + %.1f x through the caps (seam turned %.0f degrees)", c.z0, c.slope, c.rotate_deg);
    bool typed = false, named = false, said = false;
    try {
      (void)dino8::kernel::SplitBrepByPlaneSealed(Cylinder(kR, kH).Transform(turn), n, n.z * c.z0);
    } catch (const BooleanOperationError& e) {
      typed = e.reason() == BooleanFailureReason::UnsupportedGeometry;
      named = e.function_name() == "SplitBrepByPlaneSealed";
      said = std::string(e.what()).find("is not a closed solid") != std::string::npos && std::string(e.what()).find("end caps") != std::string::npos;
    } catch (const std::exception&) {
    }
    d8test::Check(typed && named && said, (std::string(label) + ": refused with BooleanFailureReason::UnsupportedGeometry naming SplitBrepByPlaneSealed and the open half").c_str());
  }
}
