// Rib / web feature (MakeRib). Every expected number is an independent closed form (areas of the rib polygon, similar
// triangles, the footprint a web leaves on a plate) - never read back from the code under test. Volumes come from the B-rep's
// own divergence-theorem integral, areas from the B-rep's trimmed-face area integral.
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <string>
#include <vector>

#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/rib.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::MakeRib;
using dino8::kernel::MakeRibFlush;
using dino8::kernel::MakeRibs;
using dino8::kernel::RibFlushPlane;
using dino8::kernel::Point3d;
using dino8::kernel::RibResult;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

// A body from a planar polygon swept along `d`, built from true planar faces (the same construction the rib tool uses, written
// out independently here so the test does not depend on the code under test for its fixtures).
Brep PolygonPrism(std::vector<Point3d> poly, ON_3dVector d) {
  ON_3dVector n(0, 0, 0);
  for (size_t i = 0; i < poly.size(); ++i) {
    const Point3d& a = poly[i];
    const Point3d& b = poly[(i + 1) % poly.size()];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  n.Unitize();
  if (n * d < 0) {
    std::reverse(poly.begin(), poly.end());
    n = -n;
  }
  const size_t N = poly.size();
  std::vector<Brep::PlanarFace> fs;
  auto shift = [&](const Point3d& p) { return Point3d(p.x + d.x, p.y + d.y, p.z + d.z); };
  {
    Brep::PlanarFace f;
    f.plane = ON_Plane(ON_3dPoint(poly[0].x, poly[0].y, poly[0].z), -n);
    for (size_t i = N; i-- > 0;) f.loop.push_back(poly[i]);
    fs.push_back(f);
  }
  {
    Brep::PlanarFace f;
    const Point3d p0 = shift(poly[0]);
    f.plane = ON_Plane(ON_3dPoint(p0.x, p0.y, p0.z), n);
    for (size_t i = 0; i < N; ++i) f.loop.push_back(shift(poly[i]));
    fs.push_back(f);
  }
  for (size_t i = 0; i < N; ++i) {
    const Point3d a = poly[i], b = poly[(i + 1) % N];
    Brep::PlanarFace f;
    f.loop = {a, b, shift(b), shift(a)};
    ON_3dVector side = ON_CrossProduct(ON_3dVector(b.x - a.x, b.y - a.y, b.z - a.z), d);
    side.Unitize();
    f.plane = ON_Plane(ON_3dPoint(a.x, a.y, a.z), side);
    fs.push_back(f);
  }
  return Brep::FromPlanarFaces(fs);
}

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

void ExpectBody(const RibResult& r, double volume, double tol, const char* what) {
  const std::string w(what);
  d8test::Check(r.brep.raw().IsValid(), (w + ": result is a valid ON_Brep").c_str());
  d8test::Check(r.brep.raw().IsSolid(), (w + ": result is a closed solid").c_str());
  const double v = r.brep.VolumeOfSealedSolid();
  d8test::Check(Near(v, volume, tol), (w + ": exact volume " + std::to_string(v) + " matches closed form " + std::to_string(volume)).c_str());
}

bool ThrowsWith(const std::function<void()>& fn, const std::string& needle, bool* invalid_arg = nullptr) {
  try {
    fn();
  } catch (const std::invalid_argument& e) {
    if (invalid_arg) *invalid_arg = true;
    return std::string(e.what()).find(needle) != std::string::npos;
  } catch (const std::exception& e) {
    if (invalid_arg) *invalid_arg = false;
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

}  // namespace

// A rectangular web on a 10 x 10 x 1 plate. Profile in the plane y = 5, thickness 0.4, x 2..8, z 0.8..4: 0.2 sunk into the
// plate, 3 above it. Fused volume = plate + 6 * 3 * 0.4; area = plate - footprint + two flanks + two ends + top.
D8_EXTRA_TEST(RibRectangularWebOnPlateMatchesClosedFormVolumeAndArea) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  const RibResult r = MakeRib(plate, {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(2, 5, 4)}, 0.4);
  ExpectBody(r, 100.0 + 6.0 * 3.0 * 0.4, 1e-6, "rect rib on plate");
  d8test::Check(Near(r.tool_volume, 6.0 * 3.2 * 0.4, 1e-9), "rect rib: web volume is width x height x thickness");
  d8test::Check(Near(r.added_volume, 7.2, 1e-6), "rect rib: added volume is the part above the plate (6 x 3 x 0.4)");
  const double area = 240.0 - 6.0 * 0.4 + 2.0 * 6.0 * 3.0 + 2.0 * 0.4 * 3.0 + 6.0 * 0.4;
  d8test::Check(Near(r.brep.Area(), area, 1e-6), "rect rib: surface area matches the closed form 278.4");
  const auto bb = r.brep.GetTightBoundingBox();
  // GetTightBoundingBox() of a trimmed B-rep carries a small slack (measured 4.16 for a true 4.0), so only bound it.
  d8test::Check(bb.max.z >= 4.0 - 1e-9 && bb.max.z < 4.5 && Near(bb.min.z, 0.0, 1e-6) && Near(bb.max.y, 10.0, 1e-6),
                "rect rib: bounding box reaches the rib's 4.0 (and not meaningfully past it); the plate is untouched elsewhere");
}

// A triangular web (apex over the middle): the part above the plate is a similar triangle clipped at z = 1.
D8_EXTRA_TEST(RibTriangularWebAddsSimilarTriangleAboveSurface) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  const RibResult r = MakeRib(plate, {P(2, 5, 0.5), P(5, 5, 4), P(8, 5, 0.5)}, 0.4);
  // Full triangle: base 6 at z = 0.5, apex at z = 4 (height 3.5). At z = 1 the width is 6 * (4 - 1) / 3.5; the cap above is a
  // similar triangle of height 3.
  const double w = 6.0 * 3.0 / 3.5;
  const double added = 0.4 * 0.5 * w * 3.0;
  ExpectBody(r, 100.0 + added, 1e-5, "triangular rib");
  d8test::Check(Near(r.added_volume, added, 1e-5), "triangular rib: added volume is thickness x similar-triangle area");
}

// The same rib listed in the opposite vertex order is the same web (the sweep follows the Newell normal, symmetric about the
// plane), and a concave U-shaped (lightened) profile takes its notch out of the added volume.
D8_EXTRA_TEST(RibVertexOrderAndConcaveProfile) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  const RibResult fwd = MakeRib(plate, {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(2, 5, 4)}, 0.4);
  const RibResult rev = MakeRib(plate, {P(2, 5, 4), P(8, 5, 4), P(8, 5, 0.8), P(2, 5, 0.8)}, 0.4);
  d8test::Check(Near(fwd.brep.VolumeOfSealedSolid(), rev.brep.VolumeOfSealedSolid(), 1e-5) && Near(fwd.brep.Area(), rev.brep.Area(), 1e-4),
                "reversed vertex order gives the same volume and area");
  // U profile: 6 x 3.2 block minus a 2 x 1.5 notch cut from the top edge; only the part above z = 1 counts: 6*3 - 2*1.5 = 15.
  const RibResult u = MakeRib(plate,
                              {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(6, 5, 4), P(6, 5, 2.5), P(4, 5, 2.5), P(4, 5, 4), P(2, 5, 4)}, 0.4);
  ExpectBody(u, 100.0 + 0.4 * 15.0, 1e-6, "U-shaped (concave) rib");
}

// A corner gusset in the inside corner of an L bracket crosses TWO faces of the body (floor and wall).
D8_EXTRA_TEST(RibCornerGussetInLBracketMatchesClosedForm) {
  // L section in xz (floor z 0..1 over x 0..10, wall x 0..1 over z 0..10), 10 long in y: volume 10 * 19 = 190.
  const Brep ell = PolygonPrism({P(0, 0, 0), P(10, 0, 0), P(10, 0, 1), P(1, 0, 1), P(1, 0, 10), P(0, 0, 10)}, ON_3dVector(0, 10, 0));
  d8test::Check(Near(ell.Volume(), 190.0, 1e-9), "L bracket fixture volume is 190");
  // Gusset triangle (0.7,0.7) (6,0.7) (0.7,6) in the plane y = 5: the part outside the L is x >= 1, z >= 1, x + z <= 6.7, a right
  // triangle with legs 4.7.
  const RibResult g = MakeRib(ell, {P(0.7, 5, 0.7), P(6, 5, 0.7), P(0.7, 5, 6)}, 0.4);
  const double added = 0.4 * 0.5 * 4.7 * 4.7;
  ExpectBody(g, 190.0 + added, 1e-6, "corner gusset");
  d8test::Check(Near(g.added_volume, added, 1e-6), "corner gusset: added volume is thickness x 4.7^2 / 2");
}

// Two disjoint ribs fused as one compound tool.
D8_EXTRA_TEST(RibTwoParallelRibsFuseInOneBoolean) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  const RibResult b = MakeRibs(plate,
                               {{P(2, 3, 0.8), P(8, 3, 0.8), P(8, 3, 4), P(2, 3, 4)},
                                {P(2, 7, 0.8), P(8, 7, 0.8), P(8, 7, 4), P(2, 7, 4)}},
                               0.4);
  ExpectBody(b, 100.0 + 2.0 * 7.2, 1e-6, "two ribs");
  d8test::Check(Near(b.tool_volume, 2.0 * 7.68, 1e-9), "two ribs: tool volume is the sum of both webs");
  bool inv = false;
  d8test::Check(ThrowsWith([&] {
                  MakeRibs(plate, {{P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(2, 5, 4)},
                                   {P(5, 2, 0.8), P(5, 8, 0.8), P(5, 8, 4), P(5, 2, 4)}}, 0.4);
                }, "overlapping bounding boxes", &inv) && inv,
                "crossing ribs (a cross) are refused with the measured reason");
}

D8_EXTRA_TEST(RibRefusesBadArgumentsWithMeasuredReasons) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  const std::vector<Point3d> ok = {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(2, 5, 4)};
  bool inv = false;
  d8test::Check(ThrowsWith([&] { MakeRib(Brep(), ok, 0.4); }, "no faces", &inv) && inv, "empty solid is an invalid argument");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, ok, 0.0); }, "thickness", &inv) && inv, "zero thickness is an invalid argument");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, ok, -1.0); }, "thickness", &inv) && inv, "negative thickness is an invalid argument");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(0, 0, 0), P(1, 0, 0)}, 0.4); }, "at least 3", &inv) && inv, "two vertices are refused");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(2, 5, 1), P(4, 5, 1), P(6, 5, 1)}, 0.4); }, "zero area", &inv) && inv,
                "collinear vertices are refused as zero area");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 6, 4), P(2, 5, 4)}, 0.4); }, "not planar", &inv) && inv,
                "a non-planar profile is refused and names the distance");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(2, 5, 0.8), P(8, 5, 0.8), P(3, 5, 4), P(7, 5, 4)}, 0.4); }, "self-intersects", &inv) && inv,
                "a bow-tie profile is refused as self-intersecting");
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(2, 5, 0.8), P(8, 5, 0.8), P(8, 5, 4), P(2, 5, 0.8)}, 0.4); }, "repeats a vertex", &inv) && inv,
                "a repeated vertex is refused");
  // Disjoint: a rib floating 5 above the plate, and one entirely inside it.
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(2, 5, 6), P(8, 5, 6), P(8, 5, 9), P(2, 5, 9)}, 0.4); }, "bounding box does not meet", &inv) && !inv,
                "a floating rib is refused (runtime_error) naming the disjoint bounding boxes");
  bool threw = false;
  try {
    MakeRib(plate, {P(2, 5, 0.2), P(8, 5, 0.2), P(8, 5, 0.8), P(2, 5, 0.8)}, 0.4);
  } catch (const std::exception&) {
    threw = true;
  }
  d8test::Check(threw, "a rib buried inside the plate adds nothing and is refused rather than returned");
}

// A rib on a CURVED face: the web crosses the cylinder wall in two separate curves, which BooleanCombineGeneral's one-crossing
// scope refuses today. The contract pinned here is "either the exact right solid or an honest refusal", never a degraded body.
D8_EXTRA_TEST(RibOnCylinderWallIsExactOrHonestlyRefused) {
  const double R = 3, H = 6, t = 0.4;
  std::vector<Point3d> circ;
  const ON_Circle c(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), R);
  ON_NurbsCurve nurbs;
  c.GetNurbForm(nurbs);
  dino8::kernel::NurbsCurve prof;
  prof.raw() = nurbs;
  const Brep cyl = Brep::Extrude(prof, dino8::kernel::Vector3d(0, 0, H));
  // Fin radiating from the wall at +x: x 2.5..5, z 1..5, plane y = 0.
  double expected = 0;
  {
    const int n = 20000;
    const double a = -t / 2, b = t / 2, h = (b - a) / n;
    for (int i = 0; i < n; ++i) {
      const double y = a + (i + 0.5) * h;
      expected += (5.0 - std::sqrt(R * R - y * y)) * 4.0 * h;
    }
    expected += M_PI * R * R * H;
  }
  try {
    const RibResult r = MakeRib(cyl, {P(2.5, 0, 1), P(5, 0, 1), P(5, 0, 5), P(2.5, 0, 5)}, t);
    d8test::Check(r.brep.raw().IsValid() && r.brep.raw().IsSolid(), "fin on cylinder: if returned, it is a valid closed solid");
    d8test::Check(Near(r.brep.VolumeOfSealedSolid(), expected, 5e-3), "fin on cylinder: if returned, the volume matches the quadrature closed form");
  } catch (const std::exception& e) {
    d8test::Check(std::string(e.what()).find("MakeRib") != std::string::npos, "fin on cylinder: refusal is a named MakeRib error");
  }
}


// A rib flush with the plate's end face x = 10. A plain MakeRib with the web ending exactly at x = 10 is refused by the engine
// (coincident faces); MakeRibFlush takes the web overhanging to x = 11 and trims it back with the plane x = 10.
D8_EXTRA_TEST(RibFlushWithEndFaceMatchesClosedForm) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  const Point3d o(10, 5, 0);
  const RibResult r = MakeRibFlush(plate, {P(2, 5, 0.8), P(11, 5, 0.8), P(11, 5, 4), P(2, 5, 4)}, 0.4, {RibFlushPlane{o, dino8::kernel::Vector3d(1, 0, 0)}});
  ExpectBody(r, 100.0 + 8.0 * 3.0 * 0.4, 1e-6, "flush rib");
  d8test::Check(Near(r.added_volume, 9.6, 1e-6), "flush rib: added volume is 8 x 3 x 0.4");
  // plate 240 - footprint 3.2 + flanks 2 x 24 + two end faces 2 x 1.2 + top 3.2
  d8test::Check(Near(r.brep.Area(), 240.0 - 3.2 + 48.0 + 2.4 + 3.2, 1e-5), "flush rib: surface area 290.4");
  d8test::Check(dino8::kernel::ContainsPointExact(P(9.9, 5, 2), r.brep.raw()) && !dino8::kernel::ContainsPointExact(P(10.1, 5, 2), r.brep.raw()) &&
                    dino8::kernel::ContainsPointExact(P(6, 5, 3.9), r.brep.raw()) && !dino8::kernel::ContainsPointExact(P(6, 5.5, 3), r.brep.raw()),
                "flush rib: point probes inside the web near the end face, past the end face, near its top and beside it");
  // Plain MakeRib exactly flush is the measured refusal this function exists for.
  d8test::Check(ThrowsWith([&] { MakeRib(plate, {P(2, 5, 0.8), P(10, 5, 0.8), P(10, 5, 4), P(2, 5, 4)}, 0.4); }, "refused"),
                "plain MakeRib of a web ending exactly on the end face is refused (the reason MakeRibFlush exists)");
}

// A rib running the whole length, both ends flush. Single cuts at either end were always exact (109.6); the two cuts together gave 113.111
// for 112 however they were issued (sequential, one compound cutter, one U-shaped cutter solid) and the sealing step's 1% bounds did not see
// it. Root cause (DINO8_BOOL_DEBUG + per-face areas): the cut bodies' faces were geometrically right (a ray-cast membership grid of 6600
// points agrees everywhere) but the web's flank faces kept a zero-width SPIKE along each cut plane (down x = 10 from z = 1 to the sunk
// depth 0.8 and straight back), and the area of a face with such a spike was 29.333 / 28.583 for 30 (EarClipTriangulate loses area on it),
// so every area / volume / tessellation of the body was wrong: Area() 300.317 for 302.4, volume 113.111 for 112. Fixed by cutting exact
// retraces out of the fragment loops (RemoveBacktrackSpikes). Expected: plate 100 + web above the plate 10 x 3 x 0.4 = 12 -> 112, area
// 240 - 4 (footprint) + 60 (flanks) + 4 (top) + 2 x 1.2 (end faces) = 302.4.
D8_EXTRA_TEST(RibFlushAtBothEndsMatchesClosedForm) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  try {
    const RibResult r = MakeRibFlush(plate, {P(-1, 5, 0.8), P(11, 5, 0.8), P(11, 5, 4), P(-1, 5, 4)}, 0.4,
                                     {RibFlushPlane{P(10, 5, 0), dino8::kernel::Vector3d(1, 0, 0)}, RibFlushPlane{P(0, 5, 0), dino8::kernel::Vector3d(-1, 0, 0)}});
    ExpectBody(r, 100.0 + 10.0 * 3.0 * 0.4, 1e-6, "full-length flush rib");
    d8test::Check(Near(r.added_volume, 12.0, 1e-6), "full-length flush rib: added volume is 10 x 3 x 0.4");
    d8test::Check(Near(r.brep.Area(), 302.4, 1e-4), ("full-length flush rib: surface area " + std::to_string(r.brep.Area()) + " vs closed form 302.4").c_str());
    using dino8::kernel::ContainsPointExact;
    d8test::Check(ContainsPointExact(P(0.2, 5, 2), r.brep.raw()) && ContainsPointExact(P(9.8, 5, 2), r.brep.raw()) && !ContainsPointExact(P(-0.3, 5, 2), r.brep.raw()) &&
                      !ContainsPointExact(P(10.3, 5, 2), r.brep.raw()) && !ContainsPointExact(P(5, 5.6, 2), r.brep.raw()),
                  "full-length flush rib: probes at both flush ends, past both ends and beside the web");
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("full-length flush rib was refused: ") + e.what()).c_str());
  }
}

D8_EXTRA_TEST(RibFlushRefusesPlaneThroughTheSolid) {
  const Brep plate = Brep::Box(0, 0, 0, 10, 10, 1);
  bool inv = false;
  d8test::Check(ThrowsWith([&] { MakeRibFlush(plate, {P(2, 5, 0.8), P(11, 5, 0.8), P(11, 5, 4), P(2, 5, 4)}, 0.4, {RibFlushPlane{P(5, 5, 0), dino8::kernel::Vector3d(1, 0, 0)}}); },
                           "not behind the solid", &inv) && inv,
                "a flush plane through the middle of the plate is refused (it would cut the host)");
  d8test::Check(ThrowsWith([&] { MakeRibFlush(plate, {P(2, 5, 0.8), P(11, 5, 0.8), P(11, 5, 4), P(2, 5, 4)}, 0.4, {}); }, "no flush planes", &inv) && inv,
                "no planes is an invalid argument");
}

// A rib standing on the top cap of a cylinder (R = 3, cap z = 6): x in [-2, 2], z 5.5..8, thickness 0.4 centred on y = 0. Before the sealing
// ladder gained its finer rungs this was refused (4 micro-edges left unpaired). Added = 4 x 2 x 0.4; the engine's exact integral is
// measured 0.9% below that (3.171) until the planar fan cap's area was taken from its 3D trims (now 3.198), so the bound is 0.5%.
D8_EXTRA_TEST(RibOnCylinderCapMatchesClosedForm) {
  const ON_Circle c(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), 3.0);
  ON_NurbsCurve nurbs;
  c.GetNurbForm(nurbs);
  dino8::kernel::NurbsCurve prof;
  prof.raw() = nurbs;
  const Brep cyl = Brep::Extrude(prof, dino8::kernel::Vector3d(0, 0, 6));
  const RibResult r = MakeRib(cyl, {P(-2, 0, 5.5), P(2, 0, 5.5), P(2, 0, 8), P(-2, 0, 8)}, 0.4);
  d8test::Check(r.brep.raw().IsValid() && r.brep.raw().IsSolid(), "cap rib: valid closed solid");
  d8test::Check(Near(r.added_volume, 3.2, 0.005 * 3.2), ("cap rib: added volume " + std::to_string(r.added_volume) + " vs closed form 3.2 (bound 0.5%)").c_str());
  using dino8::kernel::ContainsPointExact;
  d8test::Check(ContainsPointExact(P(0, 0, 7), r.brep.raw()) && ContainsPointExact(P(1.9, 0.1, 7.9), r.brep.raw()) &&
                    !ContainsPointExact(P(2.3, 0, 7), r.brep.raw()) && !ContainsPointExact(P(0, 0.5, 7), r.brep.raw()) &&
                    !ContainsPointExact(P(0, 0, 8.2), r.brep.raw()),
                "cap rib: probes inside the web, past its end, beside it and above its top");
  // Off-centre ribs on the same FAN-capped cap. y = 0.5 and y = 1 used to be refused ("3 or more fragment loops"): the footprint
  // fragment of the cap is a thin strip in the fan's strongly non-linear (u, v), and its representative point (the mean of the boundary's
  // (u, v) vertices) landed 1.4e-4 from the web wall, so it was classified Out and the footprint stayed on the cap.
  for (const double y : {0.5, 1.0, -1.0}) {
    const std::string label = "off-centre cap rib at y = " + std::to_string(y) + ": ";
    try {
      const RibResult off = MakeRib(cyl, {P(-2, y, 5.5), P(2, y, 5.5), P(2, y, 8), P(-2, y, 8)}, 0.4);
      d8test::Check(off.brep.raw().IsValid() && off.brep.raw().IsSolid(), (label + "valid closed solid").c_str());
      d8test::Check(Near(off.added_volume, 3.2, 0.005 * 3.2), (label + "added volume " + std::to_string(off.added_volume) + " vs closed form 3.2 (bound 0.5%)").c_str());
      d8test::Check(ContainsPointExact(P(0, y, 7), off.brep.raw()) && !ContainsPointExact(P(0, y + 0.5, 7), off.brep.raw()) &&
                        !ContainsPointExact(P(2.3, y, 7), off.brep.raw()) && !ContainsPointExact(P(0, y, 8.2), off.brep.raw()),
                    (label + "probes inside the web, beside it, past its end and above its top").c_str());
    } catch (const std::exception& e) {
      d8test::Check(false, (label + "was refused: " + e.what()).c_str());
    }
  }
}

namespace {
// A cylinder of radius r and length h about +z from loose faces: two 128-gon planar discs and one true cylindrical wall
// (Brep::FromMixedFaces). It has no shared edges, so IsSolid() is false and its tessellation is not a closed manifold: no Volume().
Brep LooseFaceCylinder(double r, double h) {
  Brep::CylindricalFace cf;
  cf.frame = ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1));
  cf.radius = r;
  cf.angle = 2.0 * ON_PI;
  cf.length = h;
  auto disk = [&](double z, bool flip) {
    Brep::PlanarFace pf;
    const ON_3dPoint c(0, 0, z);
    pf.plane = ON_Plane(c, flip ? ON_3dVector(0, 0, -1) : ON_3dVector(0, 0, 1));
    const int n = 128;
    for (int i = 0; i < n; ++i) {
      const double a = flip ? -2.0 * ON_PI * i / n : 2.0 * ON_PI * i / n;
      pf.loop.push_back(Point3d(r * std::cos(a), r * std::sin(a), z));
    }
    pf.notch_begin = 0;
    pf.notch_count = static_cast<int>(pf.loop.size());
    return pf;
  };
  return Brep::FromMixedFaces({disk(0.0, true), disk(h, false)}, {cf});
}
}  // namespace

// The same cap rib on a host with a PLANAR cap (loose-face cylinder, R = 3, cap z = 6), centred and off-centre: x in [-2, 2], z 5.5..8,
// thickness 0.4 -> added exactly 4 x 2 x 0.4 = 3.2 (the cap is planar, so there is no sag). Two things used to hide this: MakeRib refused the
// host because it has no measurable volume ("not a closed, watertight solid"), and the off-centre ribs on the FAN-capped Extrude() cylinder
// are refused by the engine ("3 or more fragment loops"; y = 0.5 and 1 measured, y = 0, -1, 2 fuse), which a planar-cap host does not share.
D8_EXTRA_TEST(RibOffCentreOnPlanarDiscCapMatchesClosedForm) {
  const Brep cyl = LooseFaceCylinder(3.0, 6.0);
  using dino8::kernel::ContainsPointExact;
  for (const double y : {0.0, 0.5, 1.0, -1.0}) {
    const std::string label = "planar-cap rib at y = " + std::to_string(y) + ": ";
    try {
      const RibResult r = MakeRib(cyl, {P(-2, y, 5.5), P(2, y, 5.5), P(2, y, 8), P(-2, y, 8)}, 0.4);
      d8test::Check(r.brep.raw().IsValid() && r.brep.raw().IsSolid(), (label + "valid closed solid").c_str());
      d8test::Check(Near(r.added_volume, 3.2, 1e-6), (label + "added volume " + std::to_string(r.added_volume) + " vs closed form 3.2").c_str());
      d8test::Check(ContainsPointExact(P(0, y, 7), r.brep.raw()) && ContainsPointExact(P(1.9, y + 0.1, 7.9), r.brep.raw()) &&
                        !ContainsPointExact(P(2.3, y, 7), r.brep.raw()) && !ContainsPointExact(P(0, y + 0.5, 7), r.brep.raw()) &&
                        !ContainsPointExact(P(0, y, 8.2), r.brep.raw()),
                    (label + "probes inside the web, past its end, beside it and above its top").c_str());
    } catch (const std::exception& e) {
      d8test::Check(false, (label + "was refused: " + e.what()).c_str());
    }
  }
}
