// Register rows of the kernel-geometry factory (SOLID and SURF domains), each checked against a closed form that the test
// writes out itself (frustum, pyramid and cone volumes, Cavalieri's principle for a sheared prism, the shoelace area, the
// Bernstein basis, the angle between two planes) and not against another call of the code under test. Every test names the
// Register row it supports; none of them marks a row verified.
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface_intersect.h"
#include "dino8/kernel/surface.h"
#include "test_registry.h"

namespace {

using dino8::kernel::BooleanCombineGeneralSealed;
using dino8::kernel::BooleanOp;
using dino8::kernel::Brep;
using dino8::kernel::Mesh;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }
std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }
double Rel(double got, double want) { return std::fabs(got - want) / std::fabs(want); }

NurbsCurve CircleCurve(const Point3d& c, const Vector3d& n, double r) {
  const ON_Circle circle(ON_Plane(c, n), r);
  ON_NurbsCurve nurbs;
  circle.GetNurbForm(nurbs);
  NurbsCurve k;
  k.raw() = nurbs;
  return k;
}

NurbsCurve ClosedPolygon(const std::vector<Point3d>& v) {
  std::vector<Point3d> pts = v;
  pts.push_back(v.front());
  return NurbsCurve::FromControlPoints(pts, 1);
}

// The area of a planar polygon in the XY plane by the shoelace formula.
double Shoelace(const std::vector<Point3d>& v) {
  double a = 0.0;
  for (size_t i = 0; i < v.size(); ++i) {
    const Point3d& p = v[i];
    const Point3d& q = v[(i + 1) % v.size()];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5 * std::fabs(a);
}

// Closed-form volume of a frustum with parallel faces of area a0, a1 and height h.
double Frustum(double a0, double a1, double h) { return h / 3.0 * (a0 + std::sqrt(a0 * a1) + a1); }

}  // namespace

// SURF-0027 ExtrudeSrfTapered: a square and a circle extruded with a draft are exact frusta.
D8_EXTRA_TEST(RowExtrudeSrfTaperedSquareAndCircleVolumesAreFrustumClosedForms) {
  const double h = 3.0, draft = 0.1;
  const NurbsCurve square = ClosedPolygon({P(0, 0, 0), P(2, 0, 0), P(2, 2, 0), P(0, 2, 0)});
  const Brep sq = Brep::ExtrudeTapered(square, Vector3d(0, 0, h), draft, true);
  const double side1 = 2.0 - 2.0 * h * std::tan(draft);  // every side moves inward by h tan(draft)
  const double want_sq = Frustum(4.0, side1 * side1, h);
  const double got_sq = sq.Volume();
  d8test::Check(Rel(got_sq, want_sq) < 1e-6, Msg("tapered square prism volume equals the frustum formula, relative error", Rel(got_sq, want_sq)).c_str());
  d8test::Check(sq.raw().IsSolid(), "the tapered square prism is a closed solid");

  const double r0 = 2.0;
  const Brep ci = Brep::ExtrudeTapered(CircleCurve(P(0, 0, 0), Vector3d(0, 0, 1), r0), Vector3d(0, 0, h), draft, true);
  const double r1 = r0 - h * std::tan(draft);
  const double want_ci = kPi * h / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1);
  const double got_ci = ci.Volume();
  d8test::Check(Rel(got_ci, want_ci) < 1e-4, Msg("tapered circle volume equals the cone frustum formula, relative error", Rel(got_ci, want_ci)).c_str());
  // A zero draft is the straight prism.
  const Brep straight = Brep::ExtrudeTapered(square, Vector3d(0, 0, h), 0.0, true);
  d8test::Check(Rel(straight.Volume(), 4.0 * h) < 1e-9, "zero draft gives the straight prism, volume area times height");
}

// SURF-0028 ExtrudeSrfToPoint: the volume of a cone over any base is one third of base area times the height of the apex, also
// for an apex that is not above the middle of the base (Cavalieri).
D8_EXTRA_TEST(RowExtrudeSrfToPointPyramidAndObliqueConeVolumesAreAreaTimesHeightOverThree) {
  const std::vector<Point3d> base = {P(0, 0, 0), P(4, 0, 0), P(4, 3, 0), P(1, 3, 0)};  // a trapezoid-like quadrilateral
  const Brep pyr = Brep::ExtrudeToPoint(ClosedPolygon(base), P(2, 1, 6), true);
  const double want = Shoelace(base) * 6.0 / 3.0;
  d8test::Check(Rel(pyr.Volume(), want) < 1e-9, Msg("pyramid volume is base area times height over three, relative error", Rel(pyr.Volume(), want)).c_str());
  d8test::Check(pyr.raw().IsSolid(), "the capped pyramid is a closed solid");
  const double r = 2.0, h = 5.0;
  const Brep cone = Brep::ExtrudeToPoint(CircleCurve(P(0, 0, 0), Vector3d(0, 0, 1), r), P(1.5, -0.5, h), true);
  const double want_cone = kPi * r * r * h / 3.0;
  d8test::Check(Rel(cone.Volume(), want_cone) < 1e-6, Msg("oblique cone volume is pi r^2 h / 3, relative error", Rel(cone.Volume(), want_cone)).c_str());
}

// SURF-0026 ExtrudeSrfAlongCrv: a profile swept along a path encloses area times the net height along the profile normal, for a
// straight sheared path and for a bent one (Cavalieri).
D8_EXTRA_TEST(RowExtrudeSrfAlongCrvVolumeIsAreaTimesNetHeightForStraightAndBentPaths) {
  const double r = 1.5;
  const NurbsCurve profile = CircleCurve(P(0, 0, 0), Vector3d(0, 0, 1), r);
  const NurbsCurve straight = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(3, 2, 4)}, 1);
  const Brep a = Brep::ExtrudeAlongCurve(profile, straight, true);
  const double want = kPi * r * r * 4.0;
  d8test::Check(Rel(a.Volume(), want) < 1e-6, Msg("sheared cylinder volume is pi r^2 times the height 4, relative error", Rel(a.Volume(), want)).c_str());
  const NurbsCurve bent = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(2, 1, 1), P(1, 3, 4)}, 2);
  const Brep b = Brep::ExtrudeAlongCurve(profile, bent, true);
  d8test::Check(Rel(b.Volume(), want) < 1e-6, Msg("bent path gives the same volume (net height 4), relative error", Rel(b.Volume(), want)).c_str());
}

namespace {

// Splits every face of `box` that spans the cut plane (coordinate `axis` = cut) into two coplanar faces by clipping its loop.
std::vector<Brep::PlanarFace> SplitAt(const std::vector<Brep::PlanarFace>& faces, int axis, double cut, bool* any = nullptr) {
  std::vector<Brep::PlanarFace> out;
  for (const Brep::PlanarFace& f : faces) {
    double lo = 1e300, hi = -1e300;
    for (const Point3d& p : f.loop) {
      const double c = axis == 0 ? p.x : axis == 1 ? p.y : p.z;
      lo = std::min(lo, c);
      hi = std::max(hi, c);
    }
    if (!(lo < cut - 1e-12 && hi > cut + 1e-12)) {
      out.push_back(f);
      continue;
    }
    if (any) *any = true;
    for (int side = 0; side < 2; ++side) {
      Brep::PlanarFace g;
      g.plane = f.plane;
      for (const Point3d& p : f.loop) {
        Point3d q = p;
        double& c = axis == 0 ? q.x : axis == 1 ? q.y : q.z;
        c = side == 0 ? std::min(c, cut) : std::max(c, cut);
        g.loop.push_back(q);
      }
      out.push_back(g);
    }
  }
  return out;
}

}  // namespace

// SOLID-0016 MergeCoplanarFace and SOLID-0017 MergeAllCoplanarFaces: a 3 x 2 x 1 box whose faces were split at y = 0.5 and at
// y = 1.5 (three coplanar strips on four sides) merges back to six faces with the box's closed-form area 22 and volume 6.
D8_EXTRA_TEST(RowMergeCoplanarFacesRestoresASplitBoxToSixFacesWithClosedFormAreaAndVolume) {
  const std::vector<Brep::PlanarFace> whole = Brep::Box(0, 0, 0, 3, 2, 1).PlanarFaces();
  d8test::Check(whole.size() == 6, "a box has six planar faces");
  const std::vector<Brep::PlanarFace> once = SplitAt(whole, 1, 0.5);
  const std::vector<Brep::PlanarFace> twice = SplitAt(once, 1, 1.5);
  Brep box = Brep::FromPlanarFaces(twice);
  d8test::Check(box.FaceCount() == 14, "bottom, top, left and right are each cut in three, front and back stay whole: 14 faces");
  d8test::Check(box.raw().IsSolid(), "the split box is a closed solid");
  d8test::Check(std::fabs(box.Area() - 22.0) < 1e-9, "area before merging is 2 (3*2 + 3*1 + 2*1) = 22");
  const int merges = box.MergeCoplanarFaces();
  d8test::Check(merges == 8, Msg("eight merges (four sides, two each)", merges).c_str());
  d8test::Check(box.FaceCount() == 6, "six faces remain");
  d8test::Check(box.raw().IsValid() && box.raw().IsSolid(), "the merged box is still valid and closed");
  d8test::Check(std::fabs(box.Area() - 22.0) < 1e-9, "area after merging is still 22");
  d8test::Check(std::fabs(box.Volume() - 6.0) < 1e-9, "volume after merging is still 6");
  d8test::Check(box.MergeCoplanarFaces() == 0, "nothing is left to merge");
  // A single cut: only the pair of faces that was cut merges, the others are untouched.
  Brep once_box = Brep::FromPlanarFaces(once);
  const int n0 = once_box.FaceCount();
  d8test::Check(once_box.MergeCoplanarFaces() == n0 - 6 && once_box.FaceCount() == 6, "a box cut once also merges back to six faces");
}

namespace {

int FindPlanarFace(const Brep& solid, const Vector3d& normal, const Point3d& through) {
  const std::vector<Brep::PlanarFace> faces = solid.PlanarFaces();
  for (size_t i = 0; i < faces.size(); ++i) {
    const ON_3dVector z = faces[i].plane.zaxis;
    if (ON_DotProduct(z, normal) < 0.999) continue;
    if (std::fabs(ON_DotProduct(faces[i].plane.origin - through, normal)) < 1e-9) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

// SOLID-0035 PushPull (multiface): push one face and pull the opposite one in a single call; the volume changes by the two exact
// slabs.
D8_EXTRA_TEST(RowPushPullMultiFaceChangesABoxByTheExactSlabVolumes) {
  const Brep box = Brep::Box(0, 0, 0, 2, 3, 4);  // volume 24
  const int top = FindPlanarFace(box, Vector3d(0, 0, 1), P(0, 0, 4));
  const int bottom = FindPlanarFace(box, Vector3d(0, 0, -1), P(0, 0, 0));
  const int right = FindPlanarFace(box, Vector3d(1, 0, 0), P(2, 0, 0));
  d8test::Check(top >= 0 && bottom >= 0 && right >= 0, "the box faces are found by their normals");
  // Push the top out by 1.5 (adds 2*3*1.5 = 9) and pull the bottom in by 0.5 (removes 2*3*0.5 = 3).
  const Brep both = dino8::kernel::PushPullFaces(box, {{top, 1.5}, {bottom, -0.5}});
  d8test::Check(std::fabs(both.Volume() - (24.0 + 9.0 - 3.0)) < 1e-9, Msg("volume is 24 + 9 - 3 = 30", both.Volume()).c_str());
  d8test::Check(both.raw().IsSolid(), "the result is a closed solid");
  const dino8::kernel::BoundingBox bb = both.GetTightBoundingBox();
  d8test::Check(std::fabs(bb.min.z - 0.5) < 1e-9 && std::fabs(bb.max.z - 5.5) < 1e-9, "the box now spans z from 0.5 to 5.5");
  // Three faces at once, none adjacent to another named face: top, bottom and... two named faces sharing an edge are refused.
  bool refused = false;
  try {
    dino8::kernel::PushPullFaces(box, {{top, 1.0}, {right, 1.0}});
  } catch (const std::exception&) {
    refused = true;
  }
  d8test::Check(refused, "two named faces that share an edge are refused, nothing is guessed");
}

// SOLID-0032 BoundingBox: the box of a sphere equals the closed form; the box of a tilted extruded circle contains the closed-form
// box.
D8_EXTRA_TEST(RowBoundingBoxOfSphereAndTiltedCylinderMatchesClosedForms) {
  const Brep s = Brep::Sphere(P(1, 2, 3), 2.0);
  const dino8::kernel::BoundingBox bs = s.GetTightBoundingBox();
  d8test::Check(std::fabs(bs.min.x + 1) < 1e-9 && std::fabs(bs.max.x - 3) < 1e-9 && std::fabs(bs.min.y) < 1e-9 && std::fabs(bs.max.y - 4) < 1e-9 &&
                    std::fabs(bs.min.z - 1) < 1e-9 && std::fabs(bs.max.z - 5) < 1e-9,
                "sphere of radius 2 at (1,2,3): box is [-1,3] x [0,4] x [1,5]");
  // A circle of radius r about c in a plane with unit normal n has half-extent r sqrt(1 - n_i^2) along axis i.
  Vector3d n(1, 2, 2);
  n.Unitize();
  const double r = 1.5;
  const Point3d c(0, 0, 0);
  const NurbsCurve circle = CircleCurve(c, n, r);
  const Vector3d dir = 4.0 * n;  // a right cylinder along the normal
  const Brep cyl = Brep::Extrude(circle, dir, true);
  const dino8::kernel::BoundingBox bc = cyl.GetTightBoundingBox();
  const double nn[3] = {n.x, n.y, n.z};
  const double lo[3] = {bc.min.x, bc.min.y, bc.min.z}, hi[3] = {bc.max.x, bc.max.y, bc.max.z};
  for (int i = 0; i < 3; ++i) {
    const double ext = r * std::sqrt(1.0 - nn[i] * nn[i]);
    const double shift = 4.0 * nn[i];
    const double want_lo = std::min(-ext, shift - ext), want_hi = std::max(ext, shift + ext);
    // The kernel documents this box as a bound that never excludes part of the shape, not as tight for a tilted rational face:
    // only the containment is claimed here (the overshoot is reported by the message, not hidden).
    d8test::Check(lo[i] <= want_lo + 1e-9 && hi[i] >= want_hi - 1e-9,
                  Msg("tilted cylinder box contains the exact box, axis " + std::to_string(i) + ", upper overshoot", hi[i] - want_hi).c_str());
  }
}

// SOLID-0008 Cap: an uncapped extrusion gets its two planar caps and becomes a closed solid of volume area x height.
D8_EXTRA_TEST(RowCapPlanarHolesClosesAnUncappedPrismToAnExactVolume) {
  const std::vector<Point3d> pent = {P(0, 0, 0), P(4, 0, 0), P(5, 2, 0), P(2.5, 4, 0), P(-1, 2.5, 0)};
  Brep open = Brep::Extrude(ClosedPolygon(pent), Vector3d(0, 0, 3), false);
  d8test::Check(!open.raw().IsSolid(), "the uncapped extrusion is open");
  const int caps = open.CapPlanarHoles();
  d8test::Check(caps == 2, Msg("two caps are added", caps).c_str());
  d8test::Check(open.raw().IsValid() && open.raw().IsSolid(), "the capped result is a closed valid solid");
  d8test::Check(std::fabs(open.Volume() - Shoelace(pent) * 3.0) < 1e-9, Msg("volume is shoelace area times 3", open.Volume() - Shoelace(pent) * 3.0).c_str());
  // A closed solid has no hole to cap.
  Brep box = Brep::Box(0, 0, 0, 1, 1, 1);
  d8test::Check(box.CapPlanarHoles() == 0, "a closed box gets no caps");
}

// SOLID-0012 CutVolume: the volume of the intersection of a solid and a box. A square pyramid (base 4 x 4, height 6) cut by the
// slab 0 <= z <= 3 leaves the frustum 4*6/3 - 2*2*3/3 = 28.
D8_EXTRA_TEST(RowCutVolumeOfAPyramidAndABoxIsTheClosedFormFrustum) {
  const std::vector<Point3d> base = {P(0, 0, 0), P(4, 0, 0), P(4, 4, 0), P(0, 4, 0)};
  const Brep pyr = Brep::ExtrudeToPoint(ClosedPolygon(base), P(2, 2, 6), true);
  const auto cut = BooleanCombineGeneralSealed(pyr, Brep::Box(-1, -1, -1, 5, 5, 3), BooleanOp::Intersection);
  const double v = cut.brep.VolumeOfSealedSolid();
  d8test::Check(cut.brep.raw().IsValid() && cut.brep.raw().IsSolid(), "the intersection is a valid closed solid");
  d8test::Check(Rel(v, 28.0) < 1e-9, Msg("intersection volume is the frustum 32 - 4 = 28, relative error", Rel(v, 28.0)).c_str());
  const auto outside = BooleanCombineGeneralSealed(pyr, Brep::Box(-1, -1, 3, 5, 5, 8), BooleanOp::Intersection);
  d8test::Check(Rel(outside.brep.VolumeOfSealedSolid(), 4.0) < 1e-6, "the upper slab 3 <= z <= 6 holds the small pyramid, volume 4");
}

// SOLID-0027 Volume: volumes of an extruded polygon, an extruded circle, a revolved cone and a revolved trapezoid against closed
// forms.
D8_EXTRA_TEST(RowVolumeOfPrismCylinderConeAndFrustumMatchClosedForms) {
  const std::vector<Point3d> tri = {P(0, 0, 0), P(6, 0, 0), P(2, 5, 0)};
  d8test::Check(Rel(Brep::Extrude(ClosedPolygon(tri), Vector3d(0, 0, 2.5), true).Volume(), Shoelace(tri) * 2.5) < 1e-9, "triangular prism: shoelace area times height");
  const double r = 2.0, h = 7.0;
  d8test::Check(Rel(Brep::Extrude(CircleCurve(P(0, 0, 0), Vector3d(0, 0, 1), r), Vector3d(0, 0, h), true).Volume(), kPi * r * r * h) < 1e-6, "cylinder: pi r^2 h");
  const NurbsCurve tri_profile = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(4, 0, 0), P(0, 0, 6), P(0, 0, 0)}, 1);
  d8test::Check(Rel(Brep::Revolve(tri_profile, P(0, 0, 0), Vector3d(0, 0, 1)).VolumeOfSealedSolid(), kPi * 16.0 * 6.0 / 3.0) < 1e-6, "revolved right triangle is a cone: pi r^2 h / 3");
  const NurbsCurve trap = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(0, 0, 0), P(3, 0, 0), P(1.5, 0, 4), P(0, 0, 4), P(0, 0, 0)}, 1);
  const double want = kPi * 4.0 / 3.0 * (9.0 + 3.0 * 1.5 + 2.25);
  d8test::Check(Rel(Brep::Revolve(trap, P(0, 0, 0), Vector3d(0, 0, 1)).VolumeOfSealedSolid(), want) < 1e-6, "revolved trapezoid is a cone frustum: pi h (R^2 + R r + r^2) / 3");
}

namespace {

// The drilled wall: a face of `b` that is an analytic cylinder of radius `r` whose axis passes through (cx, cy) along z.
bool HasVerticalCylinderWall(const Brep& b, double r, double cx, double cy) {
  for (int i = 0; i < b.raw().m_F.Count(); ++i) {
    ON_Cylinder cyl;
    const ON_Surface* s = b.raw().m_F[i].SurfaceOf();
    if (!s || !s->IsCylinder(&cyl, 1e-9)) continue;
    if (std::fabs(cyl.circle.radius - r) > 1e-9) continue;
    if (std::fabs(std::fabs(cyl.circle.plane.zaxis.z) - 1.0) > 1e-9) continue;
    if (std::hypot(cyl.circle.plane.origin.x - cx, cyl.circle.plane.origin.y - cy) < 1e-9) return true;
  }
  return false;
}

}  // namespace

// SURF-0041 MakeHole and SURF-0066 PlaceHole: a through hole is a cylindrical wall of exactly the requested radius about the
// requested axis, and the block loses pi r^2 times its thickness. The kernel's result is not a sealed solid (its exact volume
// path refuses it), so the volume is read from the closed tessellation and only to 0.5 percent of the removed volume: that gap
// to the row's volume tolerance is reported, not hidden.
D8_EXTRA_TEST(RowMakeHoleThroughHolesAreExactCylindersAndRemoveTheClosedFormVolume) {
  const Brep block = Brep::Box(0, 0, 0, 10, 10, 4);
  const double r = 1.5;
  const double removed = kPi * r * r * 4.0;
  const Brep through = dino8::kernel::MakeHole(block, P(5, 5, 4), Vector3d(0, 0, -1), r, 0.0, true);
  d8test::Check(through.raw().IsValid(), "the drilled block is a valid brep");
  d8test::Check(through.FaceCount() == 7, "one cylindrical wall is added to the six faces of the block");
  d8test::Check(HasVerticalCylinderWall(through, r, 5.0, 5.0), "the wall is an analytic cylinder of radius 1.5 on the axis x = 5, y = 5");
  const double v = through.TessellateToClosedMesh(256, 64).Volume();
  d8test::Check(std::fabs(v - (400.0 - removed)) < 5e-3 * removed, Msg("through hole: 400 - pi r^2 4, absolute error", v - (400.0 - removed)).c_str());
  const Brep two = dino8::kernel::MakeHoles(block, {P(2.5, 5, 4), P(7.5, 5, 4)}, Vector3d(0, 0, -1), r, 0.0, true);
  d8test::Check(HasVerticalCylinderWall(two, r, 2.5, 5.0) && HasVerticalCylinderWall(two, r, 7.5, 5.0), "two walls, one on each requested axis");
  const double v2 = two.TessellateToClosedMesh(256, 64).Volume();
  d8test::Check(std::fabs(v2 - (400.0 - 2.0 * removed)) < 5e-3 * 2.0 * removed, Msg("two through holes remove twice the cylinder, absolute error", v2 - (400.0 - 2.0 * removed)).c_str());
}

// SURF-0089 SplitFace: a curve across a planar face splits it in two; solid, area and volume are unchanged.
D8_EXTRA_TEST(RowSplitFaceByCurveCutsTheTopOfABoxIntoTheTwoClosedFormRegions) {
  const Brep box = Brep::Box(0, 0, 0, 4, 2, 1);
  int top = -1;
  for (int i = 0; i < box.FaceCount(); ++i) {
    const ON_BrepFace& f = box.raw().m_F[i];
    const ON_BoundingBox bb = f.BoundingBox();
    if (std::fabs(bb.m_min.z - 1.0) < 1e-9 && std::fabs(bb.m_max.z - 1.0) < 1e-9) top = i;
  }
  d8test::Check(top >= 0, "the top face (z = 1) is found by its bounding box");
  const NurbsCurve cut = NurbsCurve::FromControlPoints(std::vector<Point3d>{P(1.5, 0, 1), P(1.5, 2, 1)}, 1);
  const Brep split = dino8::kernel::SplitFaceByCurve(box, top, cut);
  d8test::Check(split.FaceCount() == 7, Msg("seven faces after the split", split.FaceCount()).c_str());
  // Fragment areas from the trimming loops: evaluate each outer loop's polygon on its surface and take the shoelace area in XY
  // (the top face is horizontal). The two fragments must be 1.5 x 2 = 3 and 2.5 x 2 = 5.
  std::vector<double> areas;
  for (int i = 0; i < split.FaceCount(); ++i) {
    const ON_BrepFace& f = split.raw().m_F[i];
    const ON_BoundingBox bb = f.BoundingBox();
    if (!(std::fabs(bb.m_min.z - 1.0) < 1e-9 && std::fabs(bb.m_max.z - 1.0) < 1e-9)) continue;
    const std::vector<ON_2dPoint> uv = dino8::kernel::LoopPolygon(split.raw(), f.OuterLoop()->m_loop_index, 64);
    std::vector<Point3d> xy;
    for (const ON_2dPoint& q : uv) xy.push_back(f.SurfaceOf()->PointAt(q.x, q.y));
    areas.push_back(Shoelace(xy));
  }
  std::sort(areas.begin(), areas.end());
  d8test::Check(areas.size() == 2, "two faces lie in the plane z = 1");
  if (areas.size() == 2) {
    d8test::Check(std::fabs(areas[0] - 3.0) < 1e-6, Msg("one fragment has the closed-form area 1.5 x 2 = 3", areas[0] - 3.0).c_str());
    d8test::Check(std::fabs(areas[1] - 5.0) < 1e-6, Msg("the other has the closed-form area 2.5 x 2 = 5", areas[1] - 5.0).c_str());
  }
  d8test::Check(std::fabs(split.Area() - 28.0) < 1e-6, "total area stays 2 (4*2 + 4*1 + 2*1) = 28");
  d8test::Check(std::fabs(split.VolumeOfSealedSolid() - 8.0) < 1e-6, "volume stays 8");
}

// SURF-0094 SrfControlPtGrid: a grid of control points becomes the tensor-product surface. Oracle: the Bernstein double sum for a
// 4 x 4 bicubic Bezier and the bilinear formula for a 2 x 2 grid.
D8_EXTRA_TEST(RowSrfControlPtGridEqualsTheTensorBernsteinSum) {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) grid.push_back(P(i * 1.0, j * 1.2, std::sin(0.7 * i) + 0.3 * j * j - 0.5 * i * j));
  const NurbsSurface s = NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
  const auto bern = [](int i, double t) {
    const double c[4] = {1, 3, 3, 1};
    return c[i] * std::pow(t, i) * std::pow(1.0 - t, 3 - i);
  };
  const auto dom_u = s.Domain(0), dom_v = s.Domain(1);
  double worst = 0.0;
  for (int a = 0; a <= 10; ++a)
    for (int b = 0; b <= 10; ++b) {
      const double u = a / 10.0, v = b / 10.0;
      Point3d want(0, 0, 0);
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) want += bern(i, u) * bern(j, v) * grid[static_cast<size_t>(i * 4 + j)];
      worst = std::max(worst, s.PointAt(dom_u.min + u * (dom_u.max - dom_u.min), dom_v.min + v * (dom_v.max - dom_v.min)).DistanceTo(want));
    }
  d8test::Check(worst < 1e-12, Msg("bicubic surface equals the Bernstein double sum of its control grid", worst).c_str());
  d8test::Check(s.PointAt(dom_u.min, dom_v.min).DistanceTo(grid[0]) < 1e-12 && s.PointAt(dom_u.max, dom_v.max).DistanceTo(grid[15]) < 1e-12, "the corner control points lie on the surface");
  const std::vector<Point3d> quad = {P(0, 0, 0), P(0, 2, 1), P(3, 0, 2), P(3, 2, 5)};  // index u * 2 + v
  const NurbsSurface b = NurbsSurface::FromControlGrid(quad, 2, 2, 1, 1);
  const auto du = b.Domain(0), dv = b.Domain(1);
  const double u = 0.3, v = 0.8;
  const Point3d want = (1 - u) * (1 - v) * quad[0] + (1 - u) * v * quad[1] + u * (1 - v) * quad[2] + u * v * quad[3];
  d8test::Check(b.PointAt(du.min + u * (du.max - du.min), dv.min + v * (dv.max - dv.min)).DistanceTo(want) < 1e-12, "a 2 x 2 grid is the bilinear patch");
}

// SURF-0096 SrfPtGrid: a surface through a grid of points passes through every one of them at the averaged chord-length
// parameters (Piegl & Tiller 9.4.1), which the test computes itself.
D8_EXTRA_TEST(RowSrfPtGridInterpolatesEveryGridPointAtTheAveragedChordLengthParameters) {
  const int nu = 5, nv = 4;
  std::vector<Point3d> grid;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) grid.push_back(P(i * 1.0 + 0.1 * j, j * 1.3 - 0.1 * i, std::cos(0.5 * i) * std::sin(0.8 * j) + 0.05 * i * j));
  NurbsSurface s;
  d8test::Check(NurbsSurface::InterpolateThroughGrid(grid, nu, nv, s) == Result::Ok, "InterpolateThroughGrid succeeds");
  std::vector<double> ub(static_cast<size_t>(nu), 0.0), vb(static_cast<size_t>(nv), 0.0);
  for (int j = 0; j < nv; ++j) {  // chord parameters along u of each v-column, averaged
    std::vector<double> t(static_cast<size_t>(nu), 0.0);
    for (int i = 1; i < nu; ++i) t[static_cast<size_t>(i)] = t[static_cast<size_t>(i - 1)] + grid[static_cast<size_t>(i * nv + j)].DistanceTo(grid[static_cast<size_t>((i - 1) * nv + j)]);
    for (int i = 0; i < nu; ++i) ub[static_cast<size_t>(i)] += t[static_cast<size_t>(i)] / t.back() / nv;
  }
  for (int i = 0; i < nu; ++i) {
    std::vector<double> t(static_cast<size_t>(nv), 0.0);
    for (int j = 1; j < nv; ++j) t[static_cast<size_t>(j)] = t[static_cast<size_t>(j - 1)] + grid[static_cast<size_t>(i * nv + j)].DistanceTo(grid[static_cast<size_t>(i * nv + j - 1)]);
    for (int j = 0; j < nv; ++j) vb[static_cast<size_t>(j)] += t[static_cast<size_t>(j)] / t.back() / nu;
  }
  const auto du = s.Domain(0), dv = s.Domain(1);
  double worst = 0.0;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j)
      worst = std::max(worst, s.PointAt(du.min + ub[static_cast<size_t>(i)] * (du.max - du.min), dv.min + vb[static_cast<size_t>(j)] * (dv.max - dv.min)).DistanceTo(grid[static_cast<size_t>(i * nv + j)]));
  d8test::Check(worst < 1e-9, Msg("every grid point lies on the surface at its averaged chord-length parameters", worst).c_str());
}

// SURF-0075 Rebuild (surface): refitting a surface that a smaller net can represent reproduces it. Oracle: the closed form
// z = x^2 + y^2 over the unit square, built as a biquadratic Bezier and rebuilt as a bicubic with 5 x 5 control points.
D8_EXTRA_TEST(RowRebuildSurfaceReproducesABiquadraticPatchWithADifferentNet) {
  // Bernstein coefficients of x^2 over [0,1] in degree 2: (0, 0, 1); of x itself in degree 2: (0, 1/2, 1).
  const double cx[3] = {0.0, 0.0, 1.0}, lin[3] = {0.0, 0.5, 1.0};
  std::vector<Point3d> grid;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) grid.push_back(P(lin[i], lin[j], cx[i] + cx[j]));
  const NurbsSurface src = NurbsSurface::FromControlGrid(grid, 3, 3, 2, 2);
  const auto du = src.Domain(0), dv = src.Domain(1);
  double worst_src = 0.0;
  for (int a = 0; a <= 10; ++a)
    for (int b = 0; b <= 10; ++b) {
      const double u = a / 10.0, v = b / 10.0;
      worst_src = std::max(worst_src, src.PointAt(du.min + u * (du.max - du.min), dv.min + v * (dv.max - dv.min)).DistanceTo(P(u, v, u * u + v * v)));
    }
  d8test::Check(worst_src < 1e-12, "the source patch is the closed-form surface (x, y, x^2 + y^2) with x = u, y = v");
  NurbsSurface out;
  double dev = -1.0;
  d8test::Check(src.Rebuild(5, 5, 3, 3, out, &dev) == Result::Ok, "Rebuild to degree 3 with 5 x 5 control points succeeds");
  d8test::Check(out.Domain(0).min == du.min && out.Domain(0).max == du.max, "the u domain is kept");
  double worst = 0.0;
  for (int a = 0; a <= 20; ++a)
    for (int b = 0; b <= 20; ++b) {
      const double u = a / 20.0, v = b / 20.0;
      const auto ou = out.Domain(0), ov = out.Domain(1);
      worst = std::max(worst, out.PointAt(ou.min + u * (ou.max - ou.min), ov.min + v * (ov.max - ov.min)).DistanceTo(P(u, v, u * u + v * v)));
    }
  d8test::Check(worst < 1e-9, Msg("the rebuilt surface is the same closed-form surface", worst).c_str());
  d8test::Check(dev >= 0.0 && dev < 1e-9, Msg("the reported deviation is below 1e-9", dev).c_str());
}

// SURF-0128 GlobalEdgeContinuity: the measured position gap and tangent-plane angle between two patches that share an edge equal
// the construction: a hinge angle and an offset.
D8_EXTRA_TEST(RowMeasureEdgeContinuityReadsTheHingeAngleAndTheGapOfTwoPlanarPatches) {
  const auto plane_patch = [](const Point3d& o, const Vector3d& eu, const Vector3d& ev) {
    std::vector<Point3d> g = {o, o + ev, o + eu, o + eu + ev};  // index u * 2 + v
    return NurbsSurface::FromControlGrid(g, 2, 2, 1, 1);
  };
  const NurbsSurface a = plane_patch(P(0, 0, 0), Vector3d(1, 0, 0), Vector3d(0, 1, 0));  // z = 0, edge x = 1 is its u maximum
  for (double deg : {0.0, 15.0, 30.0, 60.0}) {
    const double th = deg * kPi / 180.0;
    const NurbsSurface b = plane_patch(P(1, 0, 0), Vector3d(std::cos(th), 0, std::sin(th)), Vector3d(0, 1, 0));  // hinged at x = 1
    dino8::kernel::NurbsSurface::EdgeContinuityMeasure m;
    const Result r = b.MeasureEdgeContinuity(a, 0, false, 9, m);
    d8test::Check(r == Result::Ok, "the measurement succeeds");
    d8test::Check(m.max_position_gap < 1e-6, Msg("hinged patches share the edge: position gap", m.max_position_gap).c_str());
    d8test::Check(std::fabs(m.max_tangent_angle - th) < 1e-6, Msg("the normal angle equals the hinge angle " + std::to_string(deg) + " degrees", m.max_tangent_angle - th).c_str());
  }
  // Lifting the second patch by 0.1 opens a gap of exactly 0.1 while the normals stay parallel.
  const NurbsSurface lifted = plane_patch(P(1, 0, 0.1), Vector3d(1, 0, 0), Vector3d(0, 1, 0));
  dino8::kernel::NurbsSurface::EdgeContinuityMeasure m2;
  d8test::Check(lifted.MeasureEdgeContinuity(a, 0, false, 9, m2) == Result::Ok, "the lifted patch can be measured");
  d8test::Check(std::fabs(m2.max_position_gap - 0.1) < 1e-6 && m2.max_tangent_angle < 1e-6, "the gap is the 0.1 lift and the normals are parallel");
}

// SOLID-0018 Pyramid: a pyramid with n sides has volume n R^2 sin(2 pi / n) h / 6 (a regular n-gon of circumradius R).
D8_EXTRA_TEST(RowPyramidWithNSidesHasTheRegularPolygonClosedFormVolume) {
  const double r = 3.0, h = 5.0;
  for (int n : {3, 4, 5, 6, 8}) {
    const Mesh m = Mesh::Cone(P(0, 0, 0), Vector3d(0, 0, 1), r, h, n, 1);
    const double want = 0.5 * n * r * r * std::sin(2.0 * kPi / n) * h / 3.0;
    d8test::Check(Rel(m.Volume(), want) < 1e-6, Msg("pyramid with " + std::to_string(n) + " sides, relative volume error", Rel(m.Volume(), want)).c_str());
  }
}

// SURF-0044 MergeSrf: two halves of one surface merge into one surface that equals the original everywhere. Oracle: the Bernstein
// double sum of the 4 x 4 bicubic control grid.
D8_EXTRA_TEST(RowMergeSrfJoinsTheTwoHalvesOfABicubicPatchBackIntoTheOriginalSurface) {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) grid.push_back(P(i * 1.0, j * 1.2, std::sin(0.7 * i) + 0.3 * j * j - 0.5 * i * j));
  const NurbsSurface whole = NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
  const auto bern = [](int i, double t) {
    const double c[4] = {1, 3, 3, 1};
    return c[i] * std::pow(t, i) * std::pow(1.0 - t, 3 - i);
  };
  const auto du = whole.Domain(0), dv = whole.Domain(1);
  NurbsSurface west, east;
  d8test::Check(whole.Split(0, du.min + 0.4 * (du.max - du.min), west, east) == Result::Ok, "split the patch at 40 percent of u");
  NurbsSurface merged = west;
  d8test::Check(merged.Join(east) == Result::Ok, "Join accepts the two halves along their shared edge");
  const auto mu = merged.Domain(0), mv = merged.Domain(1);
  double worst = 0.0;
  for (int a = 0; a <= 20; ++a)
    for (int b = 0; b <= 20; ++b) {
      const double u = a / 20.0, v = b / 20.0;
      Point3d want(0, 0, 0);
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) want += bern(i, u) * bern(j, v) * grid[static_cast<size_t>(i * 4 + j)];
      worst = std::max(worst, merged.PointAt(mu.min + u * (mu.max - mu.min), mv.min + v * (mv.max - mv.min)).DistanceTo(want));
    }
  d8test::Check(worst < 1e-9, Msg("the merged surface equals the Bernstein sum of the original grid", worst).c_str());
  d8test::Check(std::fabs((mu.max - mu.min) - (du.max - du.min)) < 1e-12 && std::fabs(mu.min - du.min) < 1e-12, "the merged u domain is the original one");
  // Two patches that do not share an edge are refused and the first one is unchanged.
  const NurbsSurface far_patch = NurbsSurface::FromControlGrid({P(20, 0, 0), P(20, 1, 0), P(21, 0, 0), P(21, 1, 0)}, 2, 2, 1, 1);
  NurbsSurface kept = west;
  bool refused = false;
  try {
    refused = kept.Join(far_patch) == Result::Failed;
  } catch (const std::exception&) {
    refused = true;
  }
  d8test::Check(refused, "patches without a shared edge are refused");
}

// SURF-0113 VariableOffsetSrf: the offset distance varies linearly along u. Oracle: a flat patch offsets to the plane
// z = d0 + (d1 - d0) (x - x0) / (x1 - x0) (the u direction runs along x, the surface normal is +z).
D8_EXTRA_TEST(RowVariableOffsetSrfOfAFlatPatchIsTheLinearlyTiltedPlane) {
  std::vector<Point3d> grid;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) grid.push_back(P(3.0 * i, 2.0 * j, 0.0));
  const NurbsSurface flat = NurbsSurface::FromControlGrid(grid, 4, 4, 3, 3);
  const double d0 = 0.5, d1 = 2.0;
  NurbsSurface out;
  d8test::Check(flat.OffsetVariable(d0, d1, out) == Result::Ok, "OffsetVariable succeeds on a flat patch");
  const auto du = out.Domain(0), dv = out.Domain(1);
  double worst = 0.0;
  for (int a = 0; a <= 10; ++a)
    for (int b = 0; b <= 10; ++b) {
      const Point3d q = out.PointAt(du.min + a / 10.0 * (du.max - du.min), dv.min + b / 10.0 * (dv.max - dv.min));
      const double want_z = d0 + (d1 - d0) * q.x / 9.0;
      worst = std::max(worst, std::fabs(q.z - want_z));
      worst = std::max(worst, std::fabs(q.y - 6.0 * b / 10.0));  // the plane position in y is unchanged
    }
  d8test::Check(worst < 1e-9, Msg("offset surface is the closed-form tilted plane", worst).c_str());
  NurbsSurface uniform;
  d8test::Check(flat.OffsetVariable(1.25, 1.25, uniform) == Result::Ok, "an equal pair is a plain offset");
  const Point3d c = uniform.PointAt(0.5 * (uniform.Domain(0).min + uniform.Domain(0).max), 0.5 * (uniform.Domain(1).min + uniform.Domain(1).max));
  d8test::Check(std::fabs(c.z - 1.25) < 1e-9, "an equal pair moves the flat patch by exactly that distance");
}
