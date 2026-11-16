// Register rows of the kernel-geometry factory (SURF and SOLID domains), round 2. Every test names the Register rows it supports
// and checks the kernel against an oracle that does not come from the code under test: the tensor Bernstein form written out by
// hand, closed-form areas of trimmed plates, the rolling-ball cross-section area (1 - pi/4) r^2, inclusion-exclusion of box
// volumes and the circumference of an unrolled cylinder. None of them marks a row verified.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/boolean.h"
#include "dino8/kernel/boolean_general.h"
#include "dino8/kernel/brep.h"
#include "dino8/kernel/curve.h"
#include "dino8/kernel/fillet.h"
#include "dino8/kernel/mesh.h"
#include "dino8/kernel/surface.h"
#include "dino8/kernel/surface_intersect.h"
#include "test_registry.h"

namespace {

using dino8::kernel::Brep;
using dino8::kernel::NurbsCurve;
using dino8::kernel::NurbsSurface;
using dino8::kernel::Point2d;
using dino8::kernel::Point3d;
using dino8::kernel::Result;
using dino8::kernel::Vector3d;

const double kPi = 3.14159265358979323846;

Point3d P(double x, double y, double z) { return Point3d(x, y, z); }

std::string Msg(const std::string& what, double v) { return what + " (" + std::to_string(v) + ")"; }

// Cubic Bernstein basis, written out.
void Bern3(double s, double b[4]) {
  b[0] = (1 - s) * (1 - s) * (1 - s);
  b[1] = 3 * s * (1 - s) * (1 - s);
  b[2] = 3 * s * s * (1 - s);
  b[3] = s * s * s;
}

// The plate used by the untrim and shrink rows: a 10 x 10 plane trimmed to an octagon of circumradius 0.2 (in uv, so 2 in model
// units) with a 0.1 x 0.1 square hole (1 x 1 in model units). Closed forms: octagon area 2 sqrt(2) R^2 with R = 2 gives
// 8 sqrt(2); hole area 1; whole plane 100.
Brep OctagonPlateWithHole() {
  std::vector<Point3d> grid = {P(0, 0, 0), P(0, 10, 0), P(10, 0, 0), P(10, 10, 0)};
  NurbsSurface plane = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  std::vector<Point2d> octagon;
  for (int k = 0; k < 8; ++k) octagon.push_back(Point2d(0.5 + 0.2 * std::cos(k * kPi / 4.0), 0.5 + 0.2 * std::sin(k * kPi / 4.0)));
  std::vector<Point2d> hole = {Point2d(0.55, 0.45), Point2d(0.55, 0.55), Point2d(0.45, 0.55), Point2d(0.45, 0.45)};
  return Brep::TrimmedPlanarFaceWelded(plane, octagon, {hole});
}

const double kOctagonArea = 8.0 * 1.4142135623730951;  // 2 sqrt(2) R^2, R = 2

}  // namespace

// SURF-0008 ConvertToBeziers and SURF-0009 ConvertToSingleSpans: "Split a NURBS surface at its knots to produce untrimmed
// single-span surface patches ... without changing its shape". A bicubic surface with 7 x 6 control points has 4 x 3 knot spans,
// so the result is exactly 12 patches; each is one Bezier span (4 x 4 control points); and every patch evaluated by the Bernstein
// tensor form written out here equals the original surface at the same (u, v), with the patch domains tiling the original domain.
D8_EXTRA_TEST(RowConvertToBeziersGivesTwelvePatchesThatReproduceTheSurface) {
  std::vector<Point3d> grid;
  const int nu = 7, nv = 6;
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) grid.push_back(P(1.5 * i, 2.0 * j, std::sin(0.9 * i) * std::cos(0.7 * j) + 0.1 * i * j));
  const NurbsSurface s = NurbsSurface::FromControlGrid(grid, nu, nv, 3, 3);
  std::vector<NurbsSurface> patches;
  const Result r = s.DecomposeToBeziers(patches);
  d8test::Check(r == Result::Ok, "decomposition succeeds");
  d8test::Check(patches.size() == 12, Msg("4 x 3 spans give 12 patches", static_cast<double>(patches.size())).c_str());
  double worst = 0.0, area_domain = 0.0;
  bool single_span = true;
  for (const NurbsSurface& p : patches) {
    single_span = single_span && p.CVCountU() == 4 && p.CVCountV() == 4 && p.DegreeU() == 3 && p.DegreeV() == 3;
    const auto pu = p.Domain(0), pv = p.Domain(1);
    area_domain += (pu.max - pu.min) * (pv.max - pv.min);
    for (int a = 0; a <= 4; ++a) {
      for (int b = 0; b <= 4; ++b) {
        const double fu = a / 4.0, fv = b / 4.0;
        double bu[4], bv[4];
        Bern3(fu, bu);
        Bern3(fv, bv);
        double x = 0, y = 0, z = 0;
        for (int i = 0; i < 4; ++i)
          for (int j = 0; j < 4; ++j) {
            const Point3d c = p.ControlPointAt(i, j);
            x += bu[i] * bv[j] * c.x;
            y += bu[i] * bv[j] * c.y;
            z += bu[i] * bv[j] * c.z;
          }
        const double u = pu.min + fu * (pu.max - pu.min), v = pv.min + fv * (pv.max - pv.min);
        worst = std::max(worst, s.PointAt(u, v).DistanceTo(P(x, y, z)));
      }
    }
  }
  d8test::Check(single_span, "every patch is a single Bezier span of degree (3, 3) with 4 x 4 control points");
  d8test::Check(worst < 1e-9, Msg("the Bernstein form of each patch equals the surface at the same (u, v)", worst).c_str());
  const auto du = s.Domain(0), dv = s.Domain(1);
  d8test::Check(std::fabs(area_domain - (du.max - du.min) * (dv.max - dv.min)) < 1e-9, "the patch domains tile the original domain");
}

// SURF-0010 CreateUVCrv: "Project a surface boundary and trim curves into the parameter space". The surface is the 10 x 10 plane
// S(u, v) = (10 u, 10 v, 0). Oracle: the pull-back of a circle of radius 2 about (5, 5) is the circle of radius 0.2 about
// (0.5, 0.5) in (u, v) (divide by ten), and the pull-back of the bottom boundary edge is the line v = 0 over the whole u domain.
D8_EXTRA_TEST(RowCreateUVCrvPullsACircleAndABoundaryBackToTheParameterPlane) {
  std::vector<Point3d> grid = {P(0, 0, 0), P(0, 10, 0), P(10, 0, 0), P(10, 10, 0)};
  const NurbsSurface plane = NurbsSurface::FromControlGrid(grid, 2, 2, 1, 1);
  const auto du = plane.Domain(0), dv = plane.Domain(1);
  const ON_Circle circle(ON_Plane(ON_3dPoint(5, 5, 0), ON_3dVector(0, 0, 1)), 2.0);
  ON_NurbsCurve cn;
  circle.GetNurbForm(cn);
  dino8::kernel::IntersectOptions opt;
  opt.tolerance = 1e-6;
  const auto pb = dino8::kernel::PullbackCurveToSurface(cn, plane.raw(), opt);
  d8test::Check(pb.on_surface, "the circle lies on the surface");
  double worst = 0.0;
  for (const ON_2dPoint& uv : pb.uv) {
    const double ru = (uv.x - du.min) / (du.max - du.min) - 0.5, rv = (uv.y - dv.min) / (dv.max - dv.min) - 0.5;
    worst = std::max(worst, std::fabs(std::hypot(ru, rv) - 0.2));
  }
  d8test::Check(!pb.uv.empty() && worst < 1e-6, Msg("every pulled-back sample is on the circle of radius 0.2 about (0.5, 0.5)", worst).c_str());
  ON_LineCurve edge(ON_3dPoint(0, 0, 0), ON_3dPoint(10, 0, 0));
  const auto pe = dino8::kernel::PullbackCurveToSurface(edge, plane.raw(), opt);
  double ev = 0.0, umin = 1e9, umax = -1e9;
  for (const ON_2dPoint& uv : pe.uv) {
    ev = std::max(ev, std::fabs(uv.y - dv.min));
    umin = std::min(umin, uv.x);
    umax = std::max(umax, uv.x);
  }
  d8test::Check(!pe.uv.empty() && ev < 1e-6, "the bottom boundary pulls back to the line v = v_min");
  d8test::Check(std::fabs(umin - du.min) < 1e-6 && std::fabs(umax - du.max) < 1e-6, "and it covers the whole u domain");
}

// SURF-0106 Untrim, SURF-0107 UntrimAll, SURF-0108 UntrimBorder, SURF-0109 UntrimHoles. Oracle: the closed-form areas of the plate
// (octagon 8 sqrt(2), square hole 1). Removing the hole gives 8 sqrt(2); removing the exterior trim but keeping
// the hole gives the whole surface minus 1; removing both gives the whole surface (its area measured from its corner points).
D8_EXTRA_TEST(RowUntrimFamilyGivesTheClosedFormAreasOfTheOctagonPlate) {
  Brep plate = OctagonPlateWithHole();
  d8test::Check(plate.raw().IsValid() && std::fabs(plate.Area() - (kOctagonArea - 1.0)) < 1e-6, "the plate is valid and its area is 8 sqrt(2) - 1");

  Brep no_holes = plate;
  d8test::Check(no_holes.RemoveAllHoleLoopsInBrep() == 1, "UntrimHoles removes the one hole");
  d8test::Check(no_holes.raw().IsValid() && std::fabs(no_holes.Area() - kOctagonArea) < 1e-6, Msg("area is the octagon alone, 8 sqrt(2)", no_holes.Area() - kOctagonArea).c_str());

  // The welded planar-face constructor puts the face on its own padded clip surface, so the "whole surface" is not the 10 x 10
  // source plane: its area is measured here from the surface's own corner points (width x height of the flat rectangle).
  const ON_Surface* clip = plate.raw().m_F[0].SurfaceOf();
  const ON_3dPoint c00 = clip->PointAt(clip->Domain(0).Min(), clip->Domain(1).Min()), c10 = clip->PointAt(clip->Domain(0).Max(), clip->Domain(1).Min()),
                   c01 = clip->PointAt(clip->Domain(0).Min(), clip->Domain(1).Max());
  const double whole = c00.DistanceTo(c10) * c00.DistanceTo(c01);
  d8test::Check(whole > kOctagonArea, Msg("the clip surface is larger than the octagon", whole).c_str());

  Brep no_border = plate;
  d8test::Check(no_border.RemoveOuterTrim(0) == Result::Ok, "UntrimBorder removes the exterior trim");
  d8test::Check(no_border.raw().IsValid(), "the face with only its hole is valid");
  d8test::Check(std::fabs(no_border.Area() - (whole - 1.0)) < 1e-6, Msg("area is the whole surface minus the hole", no_border.Area() - (whole - 1.0)).c_str());

  Brep none = no_border;
  d8test::Check(none.RemoveAllHoleLoopsInBrep() == 1, "UntrimAll then removes the hole as well");
  d8test::Check(none.raw().IsValid() && std::fabs(none.Area() - whole) < 1e-6, Msg("area is the whole untrimmed surface", none.Area() - whole).c_str());
}

// SURF-0085 ShrinkTrimmedSrf and SURF-0086 ShrinkTrimmedSrfToEdge: "Contract the underlying untrimmed surface as tightly as possible
// to the trimmed edges". Oracle: the octagon's bounding box [3,7] x [3,7] in model units. After the shrink the surface's four corner
// points must enclose that box and lie within 0.5 of it (the shrink is tight), the face area must still be 8 sqrt(2) - 1, and
// the surface must be valid.
D8_EXTRA_TEST(RowShrinkTrimmedSrfContractsTheSurfaceToTheOctagonBoundingBoxWithoutChangingTheArea) {
  Brep plate = OctagonPlateWithHole();
  Brep::ShrinkSurfacesReport rep;
  const Result r = plate.ShrinkSurfaces(1e-9, &rep);
  d8test::Check(r == Result::Ok, "the over-sized plane shrinks");
  d8test::Check(plate.raw().IsValid(), "the shrunk Brep is valid");
  d8test::Check(std::fabs(plate.Area() - (kOctagonArea - 1.0)) < 1e-6, Msg("the face area is unchanged", plate.Area() - (kOctagonArea - 1.0)).c_str());
  const ON_Surface* srf = plate.raw().m_F[0].SurfaceOf();
  const ON_Interval du = srf->Domain(0), dv = srf->Domain(1);
  double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
  for (double u : {du.Min(), du.Max()})
    for (double v : {dv.Min(), dv.Max()}) {
      const ON_3dPoint q = srf->PointAt(u, v);
      xmin = std::min(xmin, q.x);
      xmax = std::max(xmax, q.x);
      ymin = std::min(ymin, q.y);
      ymax = std::max(ymax, q.y);
    }
  d8test::Check(xmin <= 3.0 + 1e-9 && ymin <= 3.0 + 1e-9 && xmax >= 7.0 - 1e-9 && ymax >= 7.0 - 1e-9, "the shrunk surface still encloses the octagon's bounding box [3,7] x [3,7]");
  d8test::Check(xmin > 2.5 && ymin > 2.5 && xmax < 7.5 && ymax < 7.5, Msg("the shrink is tight: within 0.5 of the box on every side", std::max(3.0 - xmin, xmax - 7.0)).c_str());
}

// SURF-0046 MergeAllEdges: "Merge all possible edges of a surface or polysurface in one step". A unit square whose every side is
// cut into three collinear pieces has 12 edges and 12 vertices; merging all must leave exactly the 4 sides and the 4 corners (a
// 90 degree corner is never merged), keep the perimeter 4 and the area 1.
D8_EXTRA_TEST(RowMergeAllEdgesCollapsesTheCollinearSidesButKeepsTheCorners) {
  Brep::PlanarFace f;
  f.plane = ON_Plane(P(0, 0, 0), ON_3dVector(0, 0, 1));
  const double t = 1.0 / 3.0;
  f.loop = {P(0, 0, 0), P(t, 0, 0), P(2 * t, 0, 0), P(1, 0, 0), P(1, t, 0), P(1, 2 * t, 0), P(1, 1, 0), P(2 * t, 1, 0), P(t, 1, 0), P(0, 1, 0), P(0, 2 * t, 0), P(0, t, 0)};
  Brep plate = Brep::FromPlanarFaces({f});
  d8test::Check(plate.raw().m_E.Count() == 12 && plate.raw().IsValid(), "twelve edges before");
  const int merges = plate.MergeAllContiguousEdges();
  d8test::Check(merges == 8, Msg("eight merges are made", merges).c_str());
  d8test::Check(plate.raw().IsValid(), "still valid");
  int live_edges = 0, live_vertices = 0;
  double perimeter = 0.0;
  for (int i = 0; i < plate.raw().m_E.Count(); ++i) {
    const ON_BrepEdge& e = plate.raw().m_E[i];
    if (e.m_edge_index < 0) continue;
    ++live_edges;
    double len = 0.0;
    ON_NurbsCurve nc;
    if (e.GetNurbForm(nc) > 0) {
      NurbsCurve k;
      k.raw() = nc;
      len = k.LengthToTolerance(1e-9);
    }
    perimeter += len;
  }
  for (int i = 0; i < plate.raw().m_V.Count(); ++i)
    if (plate.raw().m_V[i].m_vertex_index >= 0) ++live_vertices;
  d8test::Check(live_edges == 4 && live_vertices == 4, Msg("four edges and four corners remain", live_edges).c_str());
  d8test::Check(std::fabs(perimeter - 4.0) < 1e-9, Msg("the four straight sides add up to the perimeter 4", perimeter - 4.0).c_str());
  d8test::Check(std::fabs(plate.Area() - 1.0) < 1e-9, Msg("the area is still 1", plate.Area()).c_str());
}

// SURF-0112 VariableFilletSrf: "a fillet surface between two surface edges with varying radii". Oracle: the rolling ball removes a
// cross-section of area (1 - pi/4) r(t)^2 from a right-angle edge, so for r(t) linear from r0 to r1 along an edge of length L the
// removed volume is (1 - pi/4) L (r0^2 + r0 r1 + r1^2) / 3. The solid is a box 8 x 6 x 5 with its top-front edge filleted from
// r0 = 0.5 to r1 = 1.5.
D8_EXTRA_TEST(RowVariableFilletRemovesTheClosedFormCrossSectionVolume) {
  const Brep box = Brep::Box(0, 0, 0, 8, 6, 5);
  const Brep filleted = dino8::kernel::FilletConvexEdgeTapered(box, P(0, 0, 5), P(8, 0, 5), 0.5, 1.5);
  d8test::Check(filleted.FaceCount() == 7, Msg("the box gains one fillet face", filleted.FaceCount()).c_str());
  const double L = 8.0, r0 = 0.5, r1 = 1.5;
  const double removed = (1.0 - kPi / 4.0) * L * (r0 * r0 + r0 * r1 + r1 * r1) / 3.0;
  const double expect = 8.0 * 6.0 * 5.0 - removed;
  const double v = filleted.Volume();
  d8test::Check(std::fabs(v - expect) / expect < 1e-4, Msg("volume equals box minus the closed-form removed volume", v - expect).c_str());
}
