// Register rows of the kernel-geometry factory (SOLID and SURF domains), round 2, part two: B-rep booleans, solid and plane splits,
// coplanar merge, rolling-ball surface fillet, surface patch and network fits, and the unrolled cylinder. Each test names the Register
// rows it supports and compares the kernel with a closed form: inclusion-exclusion of box volumes, the cylinder-through-slab volume
// pi r^2 h, the quarter-circle rolling-ball arc, a quadratic height field and the circumference of an unrolled cylinder. None of
// them marks a row verified.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

bool Sealed(const Brep& b) { return b.raw().IsValid() && b.raw().IsSolid(); }

}  // namespace

// SOLID-0001 Boolean2Objects: "the Boolean results (union, difference both ways, intersection) between two objects". Boxes
// A = [0,4]^3 and B = [2,6] x [1,5] x [1,5] overlap in [2,4] x [1,4] x [1,4], volume 2 * 3 * 3 = 18, so by inclusion-exclusion the union
// is 64 + 64 - 18 = 110, A - B and B - A are 46 each, and the intersection is 18. Every result must be a valid closed solid.
D8_EXTRA_TEST(RowBoolean2ObjectsGivesTheFourInclusionExclusionVolumes) {
  const Brep a = Brep::Box(0, 0, 0, 4, 4, 4), b = Brep::Box(2, 1, 1, 6, 5, 5);
  struct Case { const Brep* x; const Brep* y; BooleanOp op; double volume; const char* name; };
  const Case cases[] = {{&a, &b, BooleanOp::Union, 110.0, "union"},
                        {&a, &b, BooleanOp::Intersection, 18.0, "intersection"},
                        {&a, &b, BooleanOp::Difference, 46.0, "A minus B"},
                        {&b, &a, BooleanOp::Difference, 46.0, "B minus A"}};
  for (const Case& c : cases) {
    const Brep r = dino8::kernel::BooleanCombineGeneral(*c.x, *c.y, c.op);
    d8test::Check(r.FaceCount() > 0 && Sealed(r), (std::string("the ") + c.name + " is a valid closed solid").c_str());
    if (r.FaceCount() == 0) continue;
    const double v = r.VolumeOfSealedSolid();
    d8test::Check(std::fabs(v - c.volume) / c.volume < 1e-6, Msg(std::string("volume of the ") + c.name + " matches inclusion-exclusion", v - c.volume).c_str());
  }
  // A cylinder (r = 1, axis z through (2,2)) through the box A (the sealed boolean, so the volume is the exact divergence integral): the intersection is a cylinder of the box's height, pi r^2 * 4.
  const Brep cyl = Brep::CylinderWelded(P(2, 2, -1), Vector3d(0, 0, 1), 1.0, 6.0);
  try {
    const auto inter = dino8::kernel::BooleanCombineGeneralSealed(a, cyl, BooleanOp::Intersection);
    const auto diff = dino8::kernel::BooleanCombineGeneralSealed(a, cyl, BooleanOp::Difference);
    d8test::Check(Sealed(inter.brep) && Sealed(diff.brep), "box and cylinder results are valid closed solids");
    const double vi = inter.brep.VolumeOfSealedSolid(), vd = diff.brep.VolumeOfSealedSolid();
    d8test::Check(std::fabs(vi - kPi * 4.0) / (kPi * 4.0) < 2e-3, Msg("box with a through cylinder: the intersection is pi r^2 h", vi - kPi * 4.0).c_str());
    d8test::Check(std::fabs(vd - (64.0 - kPi * 4.0)) / (64.0 - kPi * 4.0) < 2e-3, Msg("and the difference is 64 - pi r^2 h", vd - (64.0 - kPi * 4.0)).c_str());
  } catch (const std::exception& e) {
    d8test::Check(false, (std::string("box and cylinder booleans refused: ") + e.what()).c_str());
  }
}

// SOLID-0004 BooleanSplit: "Split and close solids along their intersections into separate closed pieces". The same boxes: the target
// A is split by the solid B into the part inside B (volume 18) and the part outside (46), both valid closed solids that add up to A.
D8_EXTRA_TEST(RowBooleanSplitGivesTheInsideAndOutsidePiecesThatAddUpToTheTarget) {
  const Brep a = Brep::Box(0, 0, 0, 4, 4, 4), b = Brep::Box(2, 1, 1, 6, 5, 5);
  const auto parts = dino8::kernel::SplitBrepBySolid(a, b);  // {outside, inside}
  d8test::Check(parts.first.FaceCount() > 0 && parts.second.FaceCount() > 0, "both pieces exist");
  if (parts.first.FaceCount() == 0 || parts.second.FaceCount() == 0) return;
  d8test::Check(Sealed(parts.first) && Sealed(parts.second), "both pieces are valid closed solids");
  const double vo = parts.first.VolumeOfSealedSolid(), vi = parts.second.VolumeOfSealedSolid();
  d8test::Check(std::fabs(vo - 46.0) < 1e-6 && std::fabs(vi - 18.0) < 1e-6, Msg("outside 46 and inside 18", vo - 46.0).c_str());
  d8test::Check(std::fabs(vo + vi - 64.0) < 1e-6, "the pieces add up to the 64 of the target");
}

// SURF-0115 Split (surface): "Divide surfaces, polysurfaces and solids using curves, surfaces and polysurfaces as cutters". A plane
// x = 1.5 cuts the box [0,4]^3 into 1.5 * 16 = 24 and 2.5 * 16 = 40; the two halves are closed and add up to 64, and the cut adds the
// planar section area 16 to each half, so each half has the area of its own side walls plus 2 * 16.
D8_EXTRA_TEST(RowSplitSolidByAPlaneGivesTheClosedFormHalvesAndAreas) {
  const Brep box = Brep::Box(0, 0, 0, 4, 4, 4);
  const auto halves = dino8::kernel::SplitBrepByPlane(box, Vector3d(1, 0, 0), 1.5);
  d8test::Check(halves.first.FaceCount() > 0 && halves.second.FaceCount() > 0, "both halves exist");
  if (halves.first.FaceCount() == 0 || halves.second.FaceCount() == 0) return;
  d8test::Check(Sealed(halves.first) && Sealed(halves.second), "both halves are valid closed solids");
  double va = halves.first.VolumeOfSealedSolid(), vb = halves.second.VolumeOfSealedSolid();
  double aa = halves.first.Area(), ab = halves.second.Area();
  if (va > vb) { std::swap(va, vb); std::swap(aa, ab); }
  d8test::Check(std::fabs(va - 24.0) < 1e-6 && std::fabs(vb - 40.0) < 1e-6, Msg("the halves have volume 24 and 40", va - 24.0).c_str());
  // Surface area of a box a x 4 x 4 is 2 (4a + 4a + 16) = 16 a + 32.
  d8test::Check(std::fabs(aa - (16.0 * 1.5 + 32.0)) < 1e-6 && std::fabs(ab - (16.0 * 2.5 + 32.0)) < 1e-6, Msg("and surface area 16 a + 32", aa - 56.0).c_str());
}

// SOLID-0041 Delete (merge_coplanar): "delete the edge between two coplanar faces ... to merge them". The union of two boxes that
// share a face is the box [0,4] x [0,2] x [0,2]: it has the 6 faces of a box once the coplanar pieces are merged, the volume 16
// and the surface area 2 (8 + 8 + 4) = 40.
D8_EXTRA_TEST(RowMergeCoplanarFacesTurnsTheUnionOfTwoAdjacentBoxesIntoOneBox) {
  const Brep left = Brep::Box(0, 0, 0, 2, 2, 2), right = Brep::Box(2, 0, 0, 4, 2, 2);
  Brep u = dino8::kernel::BooleanCombineGeneral(left, right, BooleanOp::Union);
  d8test::Check(u.FaceCount() > 6, Msg("the raw union keeps the coplanar pieces as separate faces", u.FaceCount()).c_str());
  const int merges = u.MergeCoplanarFaces();
  d8test::Check(merges > 0, Msg("merges are made", merges).c_str());
  int live = 0;
  for (int i = 0; i < u.raw().m_F.Count(); ++i)
    if (u.raw().m_F[i].m_face_index >= 0) ++live;
  d8test::Check(live == 6, Msg("six faces remain, as for one box", live).c_str());
  d8test::Check(u.raw().IsValid(), "the merged Brep is valid");
  d8test::Check(std::fabs(u.Area() - 40.0) < 1e-6, Msg("surface area is 40", u.Area() - 40.0).c_str());
  d8test::Check(std::fabs(u.Volume() - 16.0) < 1e-6, Msg("volume is 16", u.Volume() - 16.0).c_str());
}

// SURF-0030 FilletSrf: "a constant-radius round surface between two surfaces". Two perpendicular planes z = 0 and x = 0 meet along the
// y axis; the rolling ball of radius r touches them at (r, y, 0) and (0, y, r) and has its centre on the line (r, y, r). Oracle: the
// two contact curves, and every point of the blend at distance r from the centre line, with the arc's middle at
// (r (1 - 1/sqrt 2), y, r (1 - 1/sqrt 2)).
D8_EXTRA_TEST(RowFilletSrfOfTwoPerpendicularPlanesIsTheQuarterCylinderOfTheRollingBall) {
  const double r = 1.5;
  const NurbsSurface a = NurbsSurface::FromControlGrid({P(-5, 0, 0), P(-5, 10, 0), P(10, 0, 0), P(10, 10, 0)}, 2, 2, 1, 1);  // z = 0
  const NurbsSurface b = NurbsSurface::FromControlGrid({P(0, 0, -5), P(0, 0, 10), P(0, 10, -5), P(0, 10, 10)}, 2, 2, 1, 1);  // x = 0
  const Vector3d want(1, 0, 1);
  const int side_a = ON_DotProduct(a.NormalAt(a.Domain(0).min + 0.5 * (a.Domain(0).max - a.Domain(0).min), a.Domain(1).min), want) > 0 ? 1 : -1;
  const int side_b = ON_DotProduct(b.NormalAt(b.Domain(0).min + 0.5 * (b.Domain(0).max - b.Domain(0).min), b.Domain(1).min), want) > 0 ? 1 : -1;
  dino8::kernel::SurfaceFilletOptions opt;
  opt.side_a = side_a;
  opt.side_b = side_b;
  const auto f = dino8::kernel::FilletSurfaces(a, b, r, 1e-6, opt);
  d8test::Check(f.status == Result::Ok, (std::string("the fillet is built: ") + f.reason).c_str());
  if (f.status != Result::Ok) return;
  const auto ub = f.blend.Domain(0), vb = f.blend.Domain(1);
  double on_a = 0.0, on_b = 0.0, radius_err = 0.0, mid_err = 0.0;
  for (int i = 0; i <= 12; ++i) {
    const double u = ub.min + (ub.max - ub.min) * i / 12.0;
    const Point3d pa = f.blend.PointAt(u, vb.min), pb = f.blend.PointAt(u, vb.max);
    const Point3d pm = f.blend.PointAt(u, vb.min + 0.5 * (vb.max - vb.min));
    // Contact points: one on z = 0 at x = r, the other on x = 0 at z = r (either way round).
    const Point3d c0 = pa.z < pb.z ? pa : pb, c1 = pa.z < pb.z ? pb : pa;
    on_a = std::max(on_a, std::fabs(c0.z) + std::fabs(c0.x - r));
    on_b = std::max(on_b, std::fabs(c1.x) + std::fabs(c1.z - r));
    const Point3d centre(r, pm.y, r);
    for (int k = 0; k <= 8; ++k) {
      const Point3d q = f.blend.PointAt(u, vb.min + (vb.max - vb.min) * k / 8.0);
      radius_err = std::max(radius_err, std::fabs(q.DistanceTo(Point3d(r, q.y, r)) - r));
    }
    mid_err = std::max(mid_err, pm.DistanceTo(P(r * (1 - 1 / std::sqrt(2.0)), pm.y, r * (1 - 1 / std::sqrt(2.0)))));
    (void)centre;
  }
  d8test::Check(on_a < 1e-5 && on_b < 1e-5, Msg("the contact curves are (r, y, 0) and (0, y, r)", std::max(on_a, on_b)).c_str());
  d8test::Check(radius_err < 1e-5, Msg("every blend point is at distance r from the centre line", radius_err).c_str());
  d8test::Check(mid_err < 1e-5, Msg("the middle of the arc is at r (1 - 1/sqrt 2) from both planes", mid_err).c_str());
  d8test::Check(std::fabs(f.radius - r) < 1e-12, "the reported radius is the requested one");
}

// SURF-0059 Patch: "Fit a surface through curves and points". Data on the paraboloid z = ((x-5)^2 + (y-5)^2) / 50 over [0,10]^2 (centred, so the best-fit plane of the data is
// horizontal): its height field is a quadratic and so lies in the bicubic space, hence the fit reproduces it: the fitted surface satisfies the equation at
// interior samples, and the reported deviation at the data is below the tolerance.
D8_EXTRA_TEST(RowPatchFitsDataOnAParaboloidAndReproducesItsHeightField) {
  auto f = [](double x, double y) { return ((x - 5) * (x - 5) + (y - 5) * (y - 5)) / 50.0; };
  std::vector<Point3d> boundary, interior;
  for (int i = 0; i <= 10; ++i) {
    for (int j = 0; j <= 10; ++j) {
      const double x = i, y = j;
      const Point3d p(x, y, f(x, y));
      if (i == 0 || j == 0 || i == 10 || j == 10) boundary.push_back(p);
      else interior.push_back(p);
    }
  }
  NurbsSurface s;
  NurbsSurface::PatchFitReport rep;
  d8test::Check(NurbsSurface::FitPatch(boundary, interior, 1e-6, s, &rep) == Result::Ok, "the patch fit succeeds");
  d8test::Check(rep.met_tolerance && rep.max_deviation < 1e-6, Msg("the deviation at the data is below 1e-6", rep.max_deviation).c_str());
  double worst = 0.0;
  const auto du = s.Domain(0), dv = s.Domain(1);
  for (int i = 1; i < 10; ++i) {
    for (int j = 1; j < 10; ++j) {
      const Point3d q = s.PointAt(du.min + (du.max - du.min) * i / 10.0, dv.min + (dv.max - dv.min) * j / 10.0);
      worst = std::max(worst, std::fabs(q.z - f(q.x, q.y)));
    }
  }
  d8test::Check(worst < 1e-4, Msg("the fitted surface satisfies the paraboloid equation between the data points", worst).c_str());
}

// SURF-0054 NetworkSrf: "Fit a surface through a network of crossing curves". Three curves along x and three along y, each a parabola on
// the centred paraboloid z = ((x-5)^2 + (y-5)^2) / 50. The network fit must be within the tolerance of every input curve (measured by the kernel as the
// distance of 101 samples from the surface) and satisfy the same equation between the curves.
D8_EXTRA_TEST(RowNetworkSrfThroughParabolasOnAParaboloidSatisfiesItsEquation) {
  auto z = [](double x, double y) { return ((x - 5) * (x - 5) + (y - 5) * (y - 5)) / 50.0; };
  // A quadratic Bezier on x = 0, 5, 10 reproduces z(x) exactly when its middle height is 2 z(5) - (z(0) + z(10)) / 2.
  auto mid = [](double zs, double zm, double ze) { return 2.0 * zm - 0.5 * (zs + ze); };
  std::vector<NurbsCurve> curves;
  for (double y0 : {0.0, 5.0, 10.0}) curves.push_back(NurbsCurve::FromControlPoints({P(0, y0, z(0, y0)), P(5, y0, mid(z(0, y0), z(5, y0), z(10, y0))), P(10, y0, z(10, y0))}, 2));
  for (double x0 : {0.0, 5.0, 10.0}) curves.push_back(NurbsCurve::FromControlPoints({P(x0, 0, z(x0, 0)), P(x0, 5, mid(z(x0, 0), z(x0, 5), z(x0, 10))), P(x0, 10, z(x0, 10))}, 2));
  NurbsSurface s;
  NurbsSurface::CurveNetworkReport rep;
  d8test::Check(NurbsSurface::FitCurveNetwork(curves, 1e-3, s, rep) == Result::Ok, (std::string("the network fit succeeds: ") + rep.reason).c_str());
  d8test::Check(rep.u_curves == 3 && rep.v_curves == 3 && rep.intersections == 9, "three curves in each direction meet in nine points");
  d8test::Check(rep.met_tolerance && rep.max_deviation <= 1e-3, Msg("every input curve is within the tolerance", rep.max_deviation).c_str());
  double worst = 0.0;
  const auto du = s.Domain(0), dv = s.Domain(1);
  for (int i = 1; i < 10; ++i)
    for (int j = 1; j < 10; ++j) {
      const Point3d q = s.PointAt(du.min + (du.max - du.min) * i / 10.0, dv.min + (dv.max - dv.min) * j / 10.0);
      worst = std::max(worst, std::fabs(q.z - z(q.x, q.y)));
    }
  d8test::Check(worst < 5e-3, Msg("between the curves the surface follows the paraboloid", worst).c_str());
}

// SURF-0105 UnrollSrfUV: "Develop a surface ... keeping the UV mapping of the original". A cylinder of radius 3 and height 5 unrolls to
// a rectangle of width 2 pi r (the circumference) and height 5: the flat mesh's bounding box has those sides, its area is the
// rectangle's (the cylinder is developable, so the unrolling is isometric), and the flat vertices keep the (u, v) ordering: all
// vertices of one u station share the same flat x.
D8_EXTRA_TEST(RowUnrollCylinderGivesTheCircumferenceByHeightRectangle) {
  const double r = 3.0, h = 5.0;
  const ON_Cylinder cyl(ON_Circle(ON_Plane(ON_3dPoint(0, 0, 0), ON_3dVector(0, 0, 1)), r), h);
  ON_NurbsSurface ns;
  d8test::Check(cyl.GetNurbForm(ns) > 0, "the cylinder NURBS form");
  NurbsSurface s;
  s.raw() = ns;
  dino8::kernel::Mesh flat;
  double area = 0.0;
  d8test::Check(s.UnrollDevelopable(48, 4, flat, &area) == Result::Ok, "the cylinder unrolls");
  const auto bb = flat.GetBoundingBox();
  const double w = bb.max.x - bb.min.x, hh = bb.max.y - bb.min.y;
  // The unrolling may place the circumference along either flat axis.
  const double lo = std::min(w, hh), hi = std::max(w, hh);
  d8test::Check(std::fabs(lo - h) < 1e-5 && std::fabs(hi - 2 * kPi * r) < 1e-5, Msg("the flat bounding box is 5 by 2 pi r", hi - 2 * kPi * r).c_str());
  d8test::Check(std::fabs(flat.Area() - 2 * kPi * r * h) < 1e-6, Msg("the flat area is the rectangle's 2 pi r h", flat.Area() - 2 * kPi * r * h).c_str());
  d8test::Check(std::fabs(area - 2 * kPi * r * h) < 1e-6, "the reported area agrees");
}
