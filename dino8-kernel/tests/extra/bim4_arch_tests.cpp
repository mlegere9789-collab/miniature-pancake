// Multi-plane roofs, attached wall tops, openings through sloped roofs, soffits, rooms with nested loops, wall-location room
// boundaries, room numbering, sloped ceilings and stair sketches of include/dino8/kernel/bim4_calc.h. One test per Register row. Every
// expected value is a closed form worked out in the test comment (decimal literals computed outside the code under test), a second
// implementation of the same quantity by a different route (the closed-form rectangular roof of bim3_calc.h against the polygon
// clipping of ConvexRoof; midpoint sums over a fine grid against exact facet integrals), or exact counts. Selection, undo and UI
// clauses of the rows are not tested.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "dino8/kernel/bim3_calc.h"
#include "dino8/kernel/bim4_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bim4;
using dino8::kernel::bim3::P2;
using dino8::kernel::bim3::Segment2;
using dino8::kernel::bim3::SlopePlane;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
bool NearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::max(std::fabs(a), std::fabs(b)); }

std::vector<P2> Rect(double w, double h) { return {{0, 0}, {w, 0}, {w, h}, {0, h}}; }
std::vector<RoofEdgeSpec> Edges(double s, double o, bool s0 = true, bool s1 = true, bool s2 = true, bool s3 = true) {
  RoofEdgeSpec e;
  e.slope = s;
  e.overhang = o;
  std::vector<RoofEdgeSpec> v(4, e);
  v[0].sloped = s0;
  v[1].sloped = s1;
  v[2].sloped = s2;
  v[3].sloped = s3;
  return v;
}

// D8-REVIT-ARCH-0008: "Attach a wall top or base to a roof, floor, ceiling or reference plane so it follows that surface."
// Plane z = 3.0 + 0.05 x: a wall (0,0)-(10,0) over base 0 has area (3.0 + 3.5)/2 * 10 = 32.5; a wall along y at x = 4 (top 3.2, length 5)
// has 16; the oblique wall (0,0)-(3,4) of length 5 has mean top 3.075, area 15.375. A wall whose BASE follows the plane under a flat top
// at 5 has area 10 * (5 - 3.25) = 17.5 (top minus the mean plane height). Under a hip roof of 10 x 6 (slope 0.5,
// eave at 3, base at 0) an interior wall along y = 3 sees the roof height min(1.5, 0.5 x, 0.5 (10 - x)): the extra area over the eave is
// 2 * (1/2 * 3 * 1.5) + 4 * 1.5 = 10.5, so the wall has 30 + 10.5 = 40.5. A midpoint sum of the height function over 200000 samples
// agrees.
D8_EXTRA_TEST(Rw4WallTopAttachToSurface) {
  const SlopePlane pl{0, 0, 3.0, 1, 0, 0.05};
  d8test::Check(Near(WallAreaToPlane(pl, {0, 0}, {10, 0}, 0.0), 32.5) && Near(WallAreaToPlane(pl, {4, 0}, {4, 5}, 0.0), 16.0) && Near(WallAreaToPlane(pl, {0, 0}, {3, 4}, 0.0), 15.375), "wall attach: top following a sloped plane, area by trapezoid");
  d8test::Check(Near(-WallAreaToPlane(pl, {0, 0}, {10, 0}, 5.0), 17.5), "wall attach: base following the plane under a flat top at 5 (10 x (5 - 3.25))");
  ConvexRoof hip(Rect(10, 6), Edges(0.5, 0.0));
  d8test::Check(hip.valid, "wall attach: hip roof built");
  const double a = hip.WallAreaToRoof({0, 3}, {10, 3}, 3.0, 0.0);
  d8test::Check(Near(a, 40.5, 1e-9), "wall attach: interior wall under a hip roof has 30 + 10.5 = 40.5");
  double sum = 0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) sum += (3.0 + hip.HeightAt({10.0 * (i + 0.5) / n, 3.0})) * (10.0 / n);
  d8test::Check(Near(sum, 40.5, 1e-6), "wall attach: midpoint sum of the roof height along the wall agrees");
  d8test::Check(Near(hip.WallAreaToRoof({0, 0}, {10, 0}, 3.0, 0.0), 30.0, 1e-9) && Near(hip.WallAreaToRoof({0, 3}, {10, 3}, 3.0, 3.0), 10.5, 1e-9), "wall attach: a wall on the eave line stays at the eave; base at the eave leaves only the roof rise");
}

// D8-REVIT-ARCH-0033: "Join roofs to walls and attach wall tops to roofs."
// Gable roof 10 x 6, slope 0.5, east and west edges unsloped (no overhang), eave at 3: the east gable wall (10,0)-(10,6) rises
// 0.5 min(y, 6 - y): area 3 * 6 + 1/2 * 6 * 1.5 = 22.5, volume at 0.3 thickness 6.75. The polygon-clipped roof must agree with the closed
// rectangular roof of bim3_calc.h on the same inputs: hip volume 36 / surface 86.0886 (overhang 0.5), gable 45, hip-gable 40.5.
D8_EXTRA_TEST(Rw4RoofJoinWallsGableEnd) {
  ConvexRoof g(Rect(10, 6), Edges(0.5, 0.0, true, false, true, false));
  d8test::Check(g.valid && g.facets.size() == 2 && Near(g.ridge_height, 1.5) && Near(g.volume, 45.0, 1e-9), "roof join: gable roof 10 x 6 has two facets, ridge 1.5, volume 45");
  const double area = g.WallAreaToRoof({10, 0}, {10, 6}, 3.0, 0.0);
  d8test::Check(Near(area, 22.5, 1e-9) && Near(area * 0.3, 6.75, 1e-9), "roof join: east gable wall 22.5 m2, 6.75 m3 at 0.3 thick");
  d8test::Check(Near(g.HeightAt({10, 3}), 1.5) && Near(g.HeightAt({10, 0}), 0.0) && Near(g.HeightAt({10, 1.5}), 0.75), "roof join: gable end height peaks at the ridge");
  dino8::kernel::bim3::RoofRect rr;
  rr.width = 6;
  rr.length = 10;
  rr.slope = 0.5;
  rr.overhang = 0.5;
  ConvexRoof hip(Rect(10, 6), Edges(0.5, 0.5));
  const auto hc = dino8::kernel::bim3::AnalyseRoof(rr);
  d8test::Check(hip.valid && Near(hip.volume, hc.volume, 1e-9) && Near(hip.surface_area, hc.surface_area, 1e-9) && Near(hip.ridge_height, hc.ridge_height, 1e-9) && hip.facets.size() == 4, "roof join: clipped hip roof equals the closed form (volume 36, area 86.0886)");
  rr.edge_sloped[1] = false;
  rr.edge_sloped[3] = false;
  ConvexRoof gg(Rect(10, 6), Edges(0.5, 0.5, true, false, true, false));
  const auto gc = dino8::kernel::bim3::AnalyseRoof(rr);
  d8test::Check(gg.valid && Near(gg.volume, 45.0, 1e-9) && Near(gg.volume, gc.volume, 1e-9) && Near(gg.surface_area, gc.surface_area, 1e-9), "roof join: clipped gable roof equals the closed form");
  rr.edge_sloped[3] = true;
  ConvexRoof hg(Rect(10, 6), Edges(0.5, 0.5, true, false, true, true));
  const auto hgc = dino8::kernel::bim3::AnalyseRoof(rr);
  d8test::Check(hg.valid && Near(hg.volume, 40.5, 1e-9) && Near(hg.volume, hgc.volume, 1e-9) && hg.facets.size() == 3, "roof join: clipped hip-gable roof equals the closed form (40.5)");
}

// D8-REVIT-ARCH-0109: "Specify per-edge overhang and offset from wall core or face."
// Hip over 10 x 6, slope 0.5, overhangs south/east/north/west = 0.5/0.6/0.5/0.4: the outline is 11 x 7 = 77 and the four facet plan
// areas add up to it; the surface is 77 sqrt(1.25) = 86.0886. The eave drops are slope x overhang. A different slope on every edge
// (0.5, 1.0, 0.25, 0.8, no overhang) has no closed form here, so the exact facet integrals are checked against midpoint sums on a
// 1000 x 600 grid of min_i s_i d_i (volume, and area as the mean of sqrt(1 + s^2) of the lowest plane). Moving the footprint from the wall
// centre line (10 x 6) to the outer wall face (10.3 x 6.3 for 0.3 thick walls) changes the hip volume to
// h W (3L - W)/6 with h = 0.5 * 6.3/2 = 1.575: 40.68225.
D8_EXTRA_TEST(Rw4RoofPerEdgeOverhangOffset) {
  std::vector<RoofEdgeSpec> e = Edges(0.5, 0.5);
  e[1].overhang = 0.6;
  e[3].overhang = 0.4;
  ConvexRoof r(Rect(10, 6), e);
  double facet_sum = 0;
  for (const auto& f : r.facets) facet_sum += f.plan_area;
  d8test::Check(r.valid && Near(r.outline_area, 77.0, 1e-9) && Near(facet_sum, 77.0, 1e-9) && Near(r.surface_area, 77.0 * std::sqrt(1.25), 1e-9), "roof overhang: outline 11 x 7 = 77, facets add up, surface 86.0886");
  d8test::Check(Near(r.HeightAt({-0.4, 3}), -0.2, 1e-12) && Near(r.HeightAt({10.6, 3}), -0.3, 1e-12) && Near(r.HeightAt({5, -0.5}), -0.25, 1e-12), "roof overhang: eave drop is slope x overhang at each edge (0.2 west, 0.3 east, 0.25 south)");
  std::vector<RoofEdgeSpec> mixed = Edges(0.5, 0.0);
  mixed[1].slope = 1.0;
  mixed[2].slope = 0.25;
  mixed[3].slope = 0.8;
  ConvexRoof m(Rect(10, 6), mixed);
  double vol = 0, area = 0;
  const int nx = 1000, ny = 600;
  const double sl[4] = {0.5, 1.0, 0.25, 0.8};
  for (int i = 0; i < nx; ++i)
    for (int j = 0; j < ny; ++j) {
      const double x = 10.0 * (i + 0.5) / nx, y = 6.0 * (j + 0.5) / ny;
      const double d[4] = {y, 10.0 - x, 6.0 - y, x};
      int k = 0;
      for (int q = 1; q < 4; ++q)
        if (sl[q] * d[q] < sl[k] * d[k]) k = q;
      vol += sl[k] * d[k] * (10.0 / nx) * (6.0 / ny);
      area += std::sqrt(1.0 + sl[k] * sl[k]) * (10.0 / nx) * (6.0 / ny);
    }
  d8test::Check(m.valid && NearRel(m.volume, vol, 1e-4) && NearRel(m.surface_area, area, 1e-3), "roof overhang: unequal edge slopes agree with the grid integrals");
  double fs = 0;
  for (const auto& f : m.facets) fs += f.plan_area;
  d8test::Check(Near(fs, 60.0, 1e-9) && m.facets.size() == 4, "roof overhang: the four facets of unequal slopes tile the 60 m2 footprint");
  ConvexRoof face(Rect(10.3, 6.3), Edges(0.5, 0.0));
  d8test::Check(Near(face.volume, 40.68225, 1e-9) && Near(face.ridge_height, 1.575, 1e-12), "roof overhang: footprint at the outer wall face gives 40.68225");
  std::vector<P2> off;
  d8test::Check(OffsetPolygonInward(Rect(10, 6), std::vector<double>(4, -0.15), off) && Near(dino8::kernel::bim3::PolygonArea(off), 10.3 * 6.3, 1e-9), "roof overhang: moving the centre line out by half the wall thickness gives the face outline");
}

// D8-REVIT-ARCH-0030: "Create a roof from a selected mass face."
// Face (0,0,3) (8,0,3) (8,6,6) (0,6,6): edge vectors (8,0,0) and (0,6,3) have cross product (0,-24,48): area = 8 sqrt(45) = 53.66563,
// unit normal (0, -1, 2)/sqrt(5) = (0, -0.44721, 0.89443), plan area 48, slope 24/48 = 0.5 (26.565 degrees); volume of a 0.25 thick
// roof 13.41641. A vertical face has no roof; a face with fewer than 3 vertices is refused.
D8_EXTRA_TEST(Rw4RoofFromMassFace) {
  const MassFaceRoof r = RoofFromMassFace({{0, 0, 3}, {8, 0, 3}, {8, 6, 6}, {0, 6, 6}});
  d8test::Check(r.valid && Near(r.area, 53.665631459994955, 1e-9) && Near(r.plan_area, 48.0, 1e-9) && Near(r.slope, 0.5, 1e-12) && Near(r.tilt_deg, 26.56505117707799, 1e-9), "mass face roof: area 8 sqrt(45), plan 48, slope 0.5");
  d8test::Check(Near(r.normal[0], 0.0, 1e-12) && Near(r.normal[1], -0.4472135954999579, 1e-12) && Near(r.normal[2], 0.8944271909999159, 1e-12), "mass face roof: unit normal (0, -1, 2)/sqrt(5)");
  d8test::Check(Near(r.area * 0.25, 13.416407864998738, 1e-9), "mass face roof: 0.25 thick roof volume 13.41641");
  const MassFaceRoof rev = RoofFromMassFace({{0, 6, 6}, {8, 6, 6}, {8, 0, 3}, {0, 0, 3}});
  d8test::Check(rev.valid && Near(rev.normal[2], -0.8944271909999159, 1e-12) && Near(rev.area, r.area, 1e-9), "mass face roof: reversed winding flips the normal, same area");
  const MassFaceRoof vert = RoofFromMassFace({{0, 0, 0}, {4, 0, 0}, {4, 0, 3}, {0, 0, 3}});
  d8test::Check(!vert.valid && Near(vert.area, 12.0, 1e-9) && !RoofFromMassFace({{0, 0, 0}, {1, 0, 0}}).valid, "mass face roof: a vertical face is not a roof, two points are refused");
  const MassFaceRoof flat = RoofFromMassFace({{0, 0, 2}, {5, 0, 2}, {5, 4, 2}, {0, 4, 2}});
  d8test::Check(flat.valid && Near(flat.slope, 0) && Near(flat.area, 20.0) && Near(flat.plan_area, 20.0), "mass face roof: a flat face has slope 0");
}

// D8-REVIT-ARCH-0031: "Add a soffit under a roof overhang by sketching its boundary from picked roof edges and wall faces ... with a
// layered soffit type and optional slope."
// 10 x 6 footprint, overhangs 0.5/0.6/0.5/0.4 (south/east/north/west): band = 11 * 7 - 60 = 17, also the sum of the four strips
// (0.5 * 11 + 0.5 * 11 + 0.6 * 6 + 0.4 * 6 = 17). A soffit sloped at 0.2 has 17 sqrt(1.04) = 17.33667; layers 0.02 + 0.015 give 0.035 x
// 17 = 0.595 m3.
D8_EXTRA_TEST(Rw4SoffitUnderOverhang) {
  const double o[4] = {0.5, 0.6, 0.5, 0.4};
  const double band = SoffitBandPlanArea(10, 6, o);
  d8test::Check(Near(band, 17.0, 1e-12) && Near(band, 0.5 * 11 + 0.5 * 11 + 0.6 * 6 + 0.4 * 6, 1e-12), "soffit: band 17 m2, equal to the four strips");
  d8test::Check(Near(band * std::sqrt(1.04), 17.33666, 1e-5) && Near(band * (0.02 + 0.015), 0.595, 1e-12), "soffit: sloped 0.2 area 17.33666, layered volume 0.595");
  const double none[4] = {0, 0, 0, 0};
  d8test::Check(SoffitBandPlanArea(10, 6, none) == 0.0, "soffit: no overhang, no soffit");
  ConvexRoof r(Rect(10, 6), [] {
    std::vector<RoofEdgeSpec> e = Edges(0.5, 0.5);
    e[1].overhang = 0.6;
    e[3].overhang = 0.4;
    return e;
  }());
  d8test::Check(Near(r.outline_area - 60.0, band, 1e-9), "soffit: the band equals the roof outline minus the footprint");
}

// D8-REVIT-ARCH-0071: "Cut an opening of any sketched shape through a floor, roof or ceiling, perpendicular to the picked face of that
// element."
// Roof slope 0.75 (3-4-5: sin 0.6, cos 0.8), normal thickness 0.2. A 2 x 2 sketch drawn on the face (area 4): the removed prism
// has volume 4 x 0.2 = 0.8, its plan footprint is 4 x 0.8 = 3.2, and the lower outline is shifted horizontally by 0.2 x 0.6 = 0.12
// (up-slope) against the upper one. Check by explicit 3D: the top vertices lie on the plane z = 0.75 x, the bottom vertices at
// a distance 0.2 below along the unit normal.
D8_EXTRA_TEST(Rw4OpeningPerpendicularToFace) {
  const SlopedOpening o = OpeningPerpendicular(4.0, 0.75, 0.2);
  d8test::Check(Near(o.volume, 0.8, 1e-12) && Near(o.plan_area, 3.2, 1e-12) && Near(o.bottom_shift, 0.12, 1e-12) && Near(o.face_area, 4.0), "perpendicular cut: volume 0.8, plan 3.2, shift 0.12");
  const double nx = -0.75 / 1.25, nz = 1.0 / 1.25; // unit normal of z = 0.75 x
  const double tx = 1.0, ty = 0.0, tz = 0.75;       // tangent along the slope direction (not unit)
  const double tl = std::sqrt(tx * tx + tz * tz);
  const double topx = 2.0, topz = 0.75 * 2.0;
  const double botx = topx - 0.2 * nx, botz = topz - 0.2 * nz;
  (void)ty;
  d8test::Check(Near(botx - topx, 0.12, 1e-12) && Near(0.75 * botx - botz, 0.2 * 1.25, 1e-12) && Near(tl, 1.25, 1e-12), "perpendicular cut: the bottom point is 0.2 below the face along the normal (shift 0.12, plane offset 0.25)");
  const SlopedOpening flat = OpeningPerpendicular(4.0, 0.0, 0.2);
  d8test::Check(Near(flat.volume, 0.8) && Near(flat.plan_area, 4.0) && flat.bottom_shift == 0.0, "perpendicular cut: on a flat slab the cut is vertical");
}

// D8-REVIT-ARCH-0074: "Cut an opening through a sloped roof or floor along the vertical direction rather than normal to the face."
// Same roof (slope 0.75, normal thickness 0.2). A 2 x 2 plan sketch cut vertically: face area 4 x 1.25 = 5, vertical thickness 0.2 x 1.25 = 0.25,
// volume 4 x 0.25 = 1.0 = 5 x 0.2 (area on the face times normal thickness); the upper and lower outlines coincide in plan.
D8_EXTRA_TEST(Rw4OpeningVerticalOnSlope) {
  const SlopedOpening o = OpeningVertical(4.0, 0.75, 0.2);
  d8test::Check(Near(o.face_area, 5.0, 1e-12) && Near(o.volume, 1.0, 1e-12) && Near(o.volume, o.face_area * 0.2, 1e-12) && o.bottom_shift == 0.0 && Near(o.plan_area, 4.0), "vertical cut: face area 5, volume 1.0, no shift");
  const SlopedOpening p = OpeningPerpendicular(5.0, 0.75, 0.2);
  d8test::Check(Near(p.volume, o.volume, 1e-12) && Near(p.plan_area, 4.0, 1e-12), "vertical cut: a perpendicular cut of the same face outline removes the same volume but shifts its lower end");
}

// D8-REVIT-ARCH-0065: "Place a room element inside bounded regions with number and name." (number and name are not tested; the region
// and its net area are)
// Room 10 x 8 with a closed 2 x 3 closet (4,2)-(6,5) drawn inside and not touching it: regions: the room net 80 - 6 = 74 with one hole, the closet
// 6; a 1 x 1 pillar inside the closet: closet net 5, pillar 1. The net areas of all regions add up to the outer area 80.
D8_EXTRA_TEST(Rw4RoomRegionsNestedLoops) {
  auto box = [](std::vector<Segment2>& s, double x0, double y0, double x1, double y1) {
    s.push_back({{x0, y0}, {x1, y0}});
    s.push_back({{x1, y0}, {x1, y1}});
    s.push_back({{x1, y1}, {x0, y1}});
    s.push_back({{x0, y1}, {x0, y0}});
  };
  std::vector<Segment2> s;
  box(s, 0, 0, 10, 8);
  box(s, 4, 2, 6, 5);
  std::vector<RoomRegion> r = RoomRegions(s);
  d8test::Check(r.size() == 2, "nested rooms: two regions (room and closet)");
  const RoomRegion* outer = r[0].gross_area > r[1].gross_area ? &r[0] : &r[1];
  const RoomRegion* inner = outer == &r[0] ? &r[1] : &r[0];
  d8test::Check(Near(outer->gross_area, 80.0, 1e-9) && Near(outer->net_area, 74.0, 1e-9) && outer->holes == 1 && Near(inner->net_area, 6.0, 1e-9) && inner->holes == 0, "nested rooms: room 74 net with one hole, closet 6");
  box(s, 4.5, 2.5, 5.5, 3.5);
  r = RoomRegions(s);
  double total = 0, closet = 0, pillar = 0;
  for (const RoomRegion& g : r) {
    total += g.net_area;
    if (Near(g.gross_area, 6.0, 1e-9)) closet = g.net_area;
    if (Near(g.gross_area, 1.0, 1e-9)) pillar = g.net_area;
  }
  d8test::Check(r.size() == 3 && Near(closet, 5.0, 1e-9) && Near(pillar, 1.0, 1e-9) && Near(total, 80.0, 1e-9), "nested rooms: three levels, net areas 74 + 5 + 1 = 80");
  std::vector<Segment2> two;
  box(two, 0, 0, 4, 4);
  box(two, 5, 0, 9, 4);
  r = RoomRegions(two);
  d8test::Check(r.size() == 2 && r[0].holes == 0 && r[1].holes == 0 && Near(r[0].net_area, 16.0, 1e-9), "nested rooms: two separate boxes are not nested");
}

// D8-REVIT-ARCH-0066: "Add a room separation line to subdivide open spaces."
// A 4 x 2 box with a partition from (2,0) to (2,1) leaving a 1 m gap: one open room of 8. A separation line (2,1)-(2,2) closes the gap: two rooms of
// 4 each, sum 8 preserved. A separation line that does not close the space changes nothing.
D8_EXTRA_TEST(Rw4RoomSeparationLine) {
  std::vector<Segment2> s = {{{0, 0}, {4, 0}}, {{4, 0}, {4, 2}}, {{4, 2}, {0, 2}}, {{0, 2}, {0, 0}}, {{2, 0}, {2, 1}}};
  std::vector<RoomRegion> r = RoomRegions(s);
  d8test::Check(r.size() == 1 && Near(r[0].net_area, 8.0, 1e-9), "separation line: the partition with a gap leaves one open room of 8");
  s.push_back({{2, 1}, {2, 2}});
  r = RoomRegions(s);
  d8test::Check(r.size() == 2 && Near(r[0].net_area, 4.0, 1e-9) && Near(r[1].net_area, 4.0, 1e-9) && Near(r[0].net_area + r[1].net_area, 8.0, 1e-9), "separation line: closing the gap gives two rooms of 4");
  std::vector<Segment2> t = {{{0, 0}, {4, 0}}, {{4, 0}, {4, 2}}, {{4, 2}, {0, 2}}, {{0, 2}, {0, 0}}, {{2, 0}, {2, 0.5}}, {{2, 1.5}, {2, 2}}};
  d8test::Check(RoomRegions(t).size() == 1, "separation line: two stubs with a gap still leave one room");
}

// D8-REVIT-ARCH-0014: "Mark whether a wall acts as a room bounding element for rooms and areas."
// 4 x 2 box with a middle wall: bounding -> 2 rooms of 4; the middle wall flagged not room-bounding -> 1 room of 8; the east wall not bounding leaves only the closed left
// 2 x 2 room (4); with the middle wall also not bounding no room is closed.
D8_EXTRA_TEST(Rw4WallRoomBoundingFlag) {
  std::vector<WallLine> w = {{{{0, 0}, {4, 0}}, true}, {{{4, 0}, {4, 2}}, true}, {{{4, 2}, {0, 2}}, true}, {{{0, 2}, {0, 0}}, true}, {{{2, 0}, {2, 2}}, true}};
  std::vector<RoomRegion> r = RoomRegionsFromWalls(w);
  d8test::Check(r.size() == 2 && Near(r[0].net_area, 4.0, 1e-9) && Near(r[1].net_area, 4.0, 1e-9), "room bounding: bounding middle wall gives two rooms of 4");
  w[4].room_bounding = false;
  r = RoomRegionsFromWalls(w);
  d8test::Check(r.size() == 1 && Near(r[0].net_area, 8.0, 1e-9), "room bounding: a non-bounding middle wall merges them into one room of 8");
  w[4].room_bounding = true;
  w[1].room_bounding = false;
  r = RoomRegionsFromWalls(w);
  d8test::Check(r.size() == 1 && Near(r[0].net_area, 4.0, 1e-9), "room bounding: a non-bounding east wall opens the right room, only the left 2 x 2 room stays closed");
  w[4].room_bounding = false;
  d8test::Check(RoomRegionsFromWalls(w).empty(), "room bounding: with the middle wall also non-bounding nothing is closed");
}

// D8-REVIT-ARCH-0079: "Compute a room boundary from bounding elements, finish/core location choices and separator lines." (separator
// lines are exercised by D8-REVIT-ARCH-0066; the four location names are the Register reading, recalled, needs owner check)
// Centre-line rectangle 6 x 4, wall 0.3 = finish 0.05 + core 0.2 + finish 0.05. Boundary at the finish face is 0.15 inside the centre
// line: 5.7 x 3.7 = 21.09; at the core face 0.10: 5.8 x 3.8 = 22.04; at the wall centre or core centre 0: 24. L shape
// (0,0)(4,0)(4,2)(2,2)(2,4)(0,4) moved in by 0.1: the reflex corner moves to (1.9,1.9): area 3.8 x 1.8 + 1.8 x 2.0 = 10.44. Different
// offsets per edge: 6 x 4 with 0.1 on the long sides and 0.2 on the short: 5.6 x 3.8 = 21.28.
D8_EXTRA_TEST(Rw4RoomBoundaryFromWallLocation) {
  const double fin = 0.05, core = 0.2;
  d8test::Check(Near(RoomBoundaryOffset(RoomBoundaryAt::WallFinish, fin, core, fin), 0.15, 1e-12) && Near(RoomBoundaryOffset(RoomBoundaryAt::CoreFace, fin, core, fin), 0.10, 1e-12) && Near(RoomBoundaryOffset(RoomBoundaryAt::WallCenter, fin, core, fin), 0.0) && Near(RoomBoundaryOffset(RoomBoundaryAt::CoreCenter, fin, core, fin), 0.0, 1e-12), "room boundary: offsets from the centre line 0.15 / 0.10 / 0 / 0");
  std::vector<P2> out;
  const double areas[3] = {21.09, 22.04, 24.0};
  const double offs[3] = {0.15, 0.10, 0.0};
  for (int i = 0; i < 3; ++i) {
    d8test::Check(OffsetPolygonInward(Rect(6, 4), std::vector<double>(4, offs[i]), out) && Near(dino8::kernel::bim3::PolygonArea(out), areas[i], 1e-9), "room boundary: rectangle area at each location option");
  }
  const std::vector<P2> L = {{0, 0}, {4, 0}, {4, 2}, {2, 2}, {2, 4}, {0, 4}};
  d8test::Check(OffsetPolygonInward(L, std::vector<double>(6, 0.1), out) && Near(dino8::kernel::bim3::PolygonArea(out), 10.44, 1e-9) && Near(out[3].x, 1.9, 1e-12) && Near(out[3].y, 1.9, 1e-12), "room boundary: L shape reflex corner moves to (1.9, 1.9), area 10.44");
  d8test::Check(OffsetPolygonInward(Rect(6, 4), {0.1, 0.2, 0.1, 0.2}, out) && Near(dino8::kernel::bim3::PolygonArea(out), 5.6 * 3.8, 1e-9), "room boundary: per-edge offsets 0.1 / 0.2 give 5.6 x 3.8");
  d8test::Check(!OffsetPolygonInward(Rect(6, 4), {0.1}, out) && !OffsetPolygonInward({{0, 0}, {1, 0}}, {0.1, 0.1}, out), "room boundary: mismatched or degenerate input is refused");
}

// D8-REVIT-ARCH-0080: "Number rooms in automatic sequences, optionally partitioned by level or another parameter with set starting
// values and prefix, suffix and digit formatting, while still letting users type a room number by hand." (the skip of numbers typed by
// hand is a design choice, not a vendor-verified behaviour: recalled, needs owner check)
// Prefix "R-", 3 digits, start 101, step 1, "R-102" typed by hand: R-101, R-103, R-104, R-105. Level partition: level 1 starts at 101
// and level 2 at 201 ("101","102","103" and "201","202"). Suffix "A", step 5, start 10, 3 digits: 010A, 015A, 020A.
D8_EXTRA_TEST(Rw4RoomNumberingSequence) {
  NumberingRule r;
  r.prefix = "R-";
  r.start = 101;
  r.digits = 3;
  std::vector<std::string> n = NumberRooms(4, r, {"R-102"});
  d8test::Check(n == std::vector<std::string>({"R-101", "R-103", "R-104", "R-105"}), "room numbers: sequence skips the number typed by hand");
  NumberingRule l1;
  l1.start = 101;
  l1.digits = 3;
  NumberingRule l2 = l1;
  l2.start = 201;
  d8test::Check(NumberRooms(3, l1, {}) == std::vector<std::string>({"101", "102", "103"}) && NumberRooms(2, l2, {}) == std::vector<std::string>({"201", "202"}), "room numbers: each level partition restarts at its own value");
  NumberingRule s;
  s.suffix = "A";
  s.start = 10;
  s.step = 5;
  s.digits = 3;
  d8test::Check(NumberRooms(3, s, {}) == std::vector<std::string>({"010A", "015A", "020A"}) && NumberRooms(0, s, {}).empty(), "room numbers: padding, step and suffix");
  d8test::Check(FormatNumber(r, 7) == "R-007" && FormatNumber(r, 1234) == "R-1234", "room numbers: numbers wider than the digit count are not truncated");
}

// D8-REVIT-ARCH-0037: "Sketch a closed-loop ceiling boundary manually in a ceiling plan view, optionally with inner loops for openings,
// and make it sloped using a slope arrow or slope-defining boundary lines."
// Boundary 6 x 5 with a 1 x 1 inner loop: plan area 29; with the slope arrow from (0,0) at 2.4 to (0,5) at 3.65 (slope 0.25) the ceiling surface
// is 29 sqrt(1.0625) = 29.89252; the height at the inner loop centre (3, 2.5) is 2.4 + 0.25 x 2.5 = 3.025.
D8_EXTRA_TEST(Rw4SlopedCeilingWithLoops) {
  const std::vector<P2> boundary = Rect(6, 5);
  const std::vector<std::vector<P2>> holes = {{{2.5, 2}, {3.5, 2}, {3.5, 3}, {2.5, 3}}};
  SlopePlane arrow;
  d8test::Check(dino8::kernel::bim3::SlopeFromArrow({0, 0}, {0, 5}, 2.4, 3.65, arrow) && Near(arrow.slope, 0.25, 1e-12), "ceiling: slope arrow gives 0.25");
  d8test::Check(Near(CeilingSurfaceArea(boundary, holes, arrow.slope), 29.89251578572804, 1e-9) && Near(CeilingSurfaceArea(boundary, {}, 0.0), 30.0) && Near(CeilingSurfaceArea(boundary, holes, 0.0), 29.0), "ceiling: surface 29.8925 with the opening removed, 29 flat, 30 without it");
  d8test::Check(Near(arrow.ZAt(3, 2.5), 3.025, 1e-12), "ceiling: height at the opening centre 3.025");
}

// D8-REVIT-ARCH-0053: "Create stairs by sketching riser, boundary and path lines."
// Rise 3.0, largest riser 0.18: ceil(16.67) = 17 risers of 0.17647; run 4.48 from the first to the last riser line: 16 treads of 0.28.
// Trial loop over riser counts confirms 17 is the smallest count with riser <= 0.18.
D8_EXTRA_TEST(Rw4StairFromRunSketch) {
  const StairRunPlan p = StairFromRunSketch(3.0, 0.18, 4.48);
  int trial = 1;
  while (3.0 / trial > 0.18 + 1e-12) ++trial;
  d8test::Check(p.valid && p.risers == 17 && trial == 17 && Near(p.riser, 3.0 / 17.0, 1e-12) && Near(p.tread, 0.28, 1e-12), "stair sketch: 17 risers of 0.17647, 16 treads of 0.28");
  d8test::Check(Near(p.riser * p.risers, 3.0, 1e-12) && Near(p.tread * (p.risers - 1), 4.48, 1e-12), "stair sketch: risers add up to the rise, treads to the run");
  d8test::Check(StairFromRunSketch(3.06, 0.18, 4.48).risers == 17 && StairFromRunSketch(3.07, 0.18, 4.48).risers == 18, "stair sketch: 3.06 still needs 17, 3.07 needs 18");
  d8test::Check(!StairFromRunSketch(0, 0.18, 4).valid && !StairFromRunSketch(3, 0.18, 0).valid && !StairFromRunSketch(0.1, 0.18, 4).valid, "stair sketch: no rise, no run or a single riser is refused");
}

// D8-REVIT-ARCH-0054: "Turn a stair into a multistory stair ... so the repeated stairs adjust automatically when level heights change."
// (alignment by mid-landing or run start is not tested; the design choice that the riser count is kept while the riser stays in range is
// recalled, needs owner check). Heights 3.0, 3.0, 3.3 with 17 base risers in [0.10, 0.19]: 17 / 0.17647, 17 / 0.17647, and 3.3/17 = 0.1941 is
// too tall: ceil(3.3/0.19) = 18 risers of 0.18333.
D8_EXTRA_TEST(Rw4MultistoryStairRisers) {
  const auto r = MultistoryRisers({3.0, 3.0, 3.3}, 17, 0.10, 0.19);
  d8test::Check(r.size() == 3 && r[0].first == 17 && r[1].first == 17 && Near(r[0].second, 3.0 / 17.0, 1e-12) && r[2].first == 18 && Near(r[2].second, 3.3 / 18.0, 1e-12), "multistory stair: 17, 17, 18 risers");
  const auto low = MultistoryRisers({1.5}, 17, 0.10, 0.19);
  d8test::Check(low[0].first == 8 && Near(low[0].second, 0.1875, 1e-12), "multistory stair: a low level (riser 0.088 < 0.10) is recomputed to 8 risers of 0.1875");
  double total = 0;
  for (size_t i = 0; i < r.size(); ++i) total += r[i].first * r[i].second;
  d8test::Check(Near(total, 9.3, 1e-9), "multistory stair: the risers of every level add up to the level heights");
}

} // namespace
