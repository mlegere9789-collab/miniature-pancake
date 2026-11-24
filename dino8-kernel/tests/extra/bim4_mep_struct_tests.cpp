// Slanted columns, braces, sloped beams, sloped slabs, insulation take-off, pipe slopes, slope notation and static-regain duct
// sizing of include/dino8/kernel/bim4_calc.h. One test per Register row. Expected values are closed forms (3-4-5 triangles, cylinder
// and annulus volumes, Ohm-style linear relations) or decimal literals computed outside the code under test. Selection, undo and UI
// clauses of the rows are not tested.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "dino8/kernel/bim3_calc.h"
#include "dino8/kernel/bim4_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel::bim4;
using dino8::kernel::bim3::P2;

bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

// D8-REVIT-STRUCT-0001: "Place a vertical or slanted structural column from a loadable family with base/top levels and offsets."
// Levels 0 and 4, no offsets, slant 30 degrees from vertical: length 4 / cos 30 = 4.618802, plan offset 4 tan 30 = 2.309401. Offsets +0.5 at
// the base and -0.5 at the top reduce the height to 3: length 3.464102. A vertical column (0 degrees) has length = height, offset 0.
D8_EXTRA_TEST(Rw4SlantedColumnLevelsOffsets) {
  const SlantedColumn c = ColumnFromAngle(0, 0, 4, 0, 30.0);
  d8test::Check(Near(c.height, 4.0) && Near(c.length, 4.618802153517006, 1e-12) && Near(c.offset, 2.309401076758503, 1e-12), "slanted column: 30 degrees over 4 m is 4.6188 long, 2.3094 over");
  d8test::Check(Near(std::hypot(c.height, c.offset), c.length, 1e-12), "slanted column: length is the hypotenuse of height and plan offset");
  const SlantedColumn o = ColumnFromAngle(0, 0.5, 4, -0.5, 30.0);
  d8test::Check(Near(o.height, 3.0) && Near(o.length, 3.0 / std::cos(30.0 * 3.14159265358979323846 / 180.0), 1e-12) && Near(o.length, 3.4641016151377544, 1e-12), "slanted column: base +0.5 and top -0.5 leave height 3, length 3.4641");
  const SlantedColumn v = ColumnFromAngle(0, 0, 3.2, 0, 0.0);
  d8test::Check(Near(v.length, 3.2) && Near(v.offset, 0.0), "slanted column: a vertical column has length = height");
}

// D8-REVIT-STRUCT-0049: "Slanted columns have a style that is either angle driven or end-point driven, deciding how they adjust when
// attached beams move, and columns can separately attach top or base to floors, roofs and other targets with chosen cut behavior." (the
// attachment and cut behaviour are not tested; the two driving styles are, in the reading that an angle-driven column keeps its angle
// when the top level rises, an end-point-driven one keeps its plan offset: recalled, needs owner check)
// Height 4: angle driven 30 degrees -> offset 2.3094; end-point driven offset 3 -> angle 36.8699 degrees, length 5. The top rises to 5:
// angle driven keeps 30 degrees and the offset grows to 5 tan 30 = 2.8868; end-point driven keeps the offset 3 and the angle drops to
// atan(3/5) = 30.9638 degrees.
D8_EXTRA_TEST(Rw4SlantedColumnDrivenStyles) {
  const SlantedColumn a4 = ColumnFromAngle(0, 0, 4, 0, 30.0), a5 = ColumnFromAngle(0, 0, 5, 0, 30.0);
  d8test::Check(Near(a4.slant_deg, 30.0) && Near(a5.slant_deg, 30.0) && Near(a5.offset, 2.8867513459481287, 1e-12) && a5.offset > a4.offset, "slanted style: angle driven keeps 30 degrees, the offset grows to 2.8868");
  const SlantedColumn e4 = ColumnFromEnds(4.0, 3.0), e5 = ColumnFromEnds(5.0, 3.0);
  d8test::Check(Near(e4.slant_deg, 36.86989764584402, 1e-9) && Near(e4.length, 5.0, 1e-12) && Near(e5.offset, 3.0) && Near(e5.slant_deg, 30.96375653207352, 1e-9), "slanted style: end-point driven keeps offset 3, the angle falls to 30.9638");
  d8test::Check(Near(e5.length, std::sqrt(34.0), 1e-12) && ColumnFromEnds(4, 0).slant_deg == 0.0, "slanted style: length sqrt(5^2 + 3^2); zero offset is vertical");
}

// D8-REVIT-STRUCT-0003: "Place a diagonal brace between two levels or two columns with start/end offsets."
// Columns 4 m apart, levels 0 and 4: brace from 0.5 above the lower level to 0.5 below the upper: dz = 3, run 4: length 5, angle
// atan(3/4) = 36.8699 degrees, slope 0.75. Without offsets (dz 4, run 3): length 5, angle 53.1301. Between two levels the sign of dz does not matter for
// the length.
D8_EXTRA_TEST(Rw4BraceBetweenLevels) {
  const Member3 m = MemberBetween(4.0, 0.5, 3.5);
  d8test::Check(Near(m.length, 5.0, 1e-12) && Near(m.angle_from_horizontal_deg, 36.86989764584402, 1e-9) && Near(m.slope, 0.75, 1e-12), "brace: run 4, dz 3 -> length 5, 36.87 degrees");
  const Member3 n = MemberBetween(3.0, 0.0, 4.0), r = MemberBetween(3.0, 4.0, 0.0);
  d8test::Check(Near(n.length, 5.0, 1e-12) && Near(n.angle_from_horizontal_deg, 53.13010235415598, 1e-9) && Near(r.length, 5.0, 1e-12) && Near(r.slope, -4.0 / 3.0, 1e-12) && Near(r.angle_from_horizontal_deg, n.angle_from_horizontal_deg, 1e-12), "brace: run 3, dz +-4 -> length 5, 53.13 degrees both ways");
}

// D8-REVIT-STRUCT-0045: "Offset beam ends vertically from levels to create sloped framing."
// Span 6 m, ends -0.1 and -0.4 below the level: dz = -0.3, length sqrt(36 + 0.09) = 6.007495, slope -0.05, angle 2.8624 degrees. Equal offsets keep
// the beam level with length 6.
D8_EXTRA_TEST(Rw4SlopedBeamEndOffsets) {
  const Member3 b = MemberBetween(6.0, -0.1, -0.4);
  d8test::Check(Near(b.length, 6.0074953183502355, 1e-12) && Near(b.slope, -0.05, 1e-12) && Near(b.angle_from_horizontal_deg, 2.8624052261117474, 1e-9), "sloped beam: length 6.0075, slope -5 %");
  const Member3 l = MemberBetween(6.0, -0.2, -0.2);
  d8test::Check(Near(l.length, 6.0) && l.slope == 0.0 && l.angle_from_horizontal_deg == 0.0, "sloped beam: equal end offsets keep the beam level");
}

// D8-REVIT-STRUCT-0010: "Create a structural foundation slab from a boundary with thickness and slope."
// 10 x 8 plan = 80 m2, thickness 0.5, slope 0.05. Thickness taken vertically: 40 m3; taken normal to the face: surface area 80 sqrt(1.0025) =
// 80.1 m2, volume 40.04997 (the vendor's convention is not stated in the row: both are provided, recalled, needs owner check).
D8_EXTRA_TEST(Rw4FoundationSlabSlopeVolume) {
  const SlabVolume v = SlopedSlab(80.0, 0.5, 0.05, false), n = SlopedSlab(80.0, 0.5, 0.05, true);
  d8test::Check(Near(v.volume, 40.0, 1e-12) && Near(v.surface_area, 80.0 * 1.0012492197250393, 1e-9), "foundation slab: vertical thickness 0.5 over 80 m2 is 40 m3");
  d8test::Check(Near(n.volume, 40.049968789001575, 1e-9) && Near(n.volume, 0.5 * n.surface_area, 1e-12), "foundation slab: normal thickness 0.5 gives 40.05 m3");
  d8test::Check(Near(SlopedSlab(80.0, 0.5, 0.0, true).volume, 40.0) && Near(SlopedSlab(0.0, 0.5, 0.05, true).volume, 0.0), "foundation slab: flat slab 40 m3, no area no volume");
}

// D8-REVIT-STRUCT-0051: "A foundation slab takes its thickness from the layered construction of its type, a slope from an instance
// property, and an edge offset from the boundary sketch options."
// Layers 0.10 + 0.40 = 0.50; boundary 10 x 8 offset outward by 0.5 (mitred): 11 x 9 = 99; slope 0.05 with vertical thickness: 99 x 0.5 = 49.5.
D8_EXTRA_TEST(Rw4FoundationSlabLayersEdgeOffset) {
  const std::vector<dino8::kernel::bim3::Layer> layers = {{"blinding", 0.10, false}, {"concrete", 0.40, true}};
  const auto st = dino8::kernel::bim3::StackAtPoint(layers, 0.0, 0.0);
  d8test::Check(st.valid && Near(st.total, 0.5, 1e-12), "slab layers: type thickness 0.10 + 0.40 = 0.50");
  const std::vector<P2> boundary = {{0, 0}, {10, 0}, {10, 8}, {0, 8}};
  std::vector<P2> off;
  d8test::Check(OffsetPolygonInward(boundary, std::vector<double>(4, -0.5), off) && Near(dino8::kernel::bim3::PolygonArea(off), 99.0, 1e-9), "slab layers: boundary offset 0.5 outward is 11 x 9 = 99");
  const SlabVolume v = SlopedSlab(dino8::kernel::bim3::PolygonArea(off), st.total, 0.05, false);
  d8test::Check(Near(v.volume, 49.5, 1e-9) && Near(v.surface_area, 99.0 * 1.0012492197250393, 1e-9), "slab layers: 99 x 0.5 = 49.5 m3 on a 5 % slope");
  std::vector<P2> in;
  d8test::Check(OffsetPolygonInward(boundary, std::vector<double>(4, 0.5), in) && Near(dino8::kernel::bim3::PolygonArea(in), 9.0 * 7.0, 1e-9), "slab layers: a negative edge offset (inward 0.5) is 9 x 7");
}

// D8-REVIT-SYS-0076: "Schedule insulation by size, thickness and length for takeoff."
// Pipe outer diameter 0.06, insulation 0.03, length 10: volume pi t (d + t) L = pi 0.03 0.09 10 = 0.0848230 m3, equal to the annulus
// pi/4 (0.12^2 - 0.06^2) 10 and to the radial integral of 2 pi r dr from 0.03 to 0.06; outer surface pi 0.12 10 = 3.76991. Schedule:
// DN25 (d 0.034, t 0.02, 5 m) -> 0.0169646; DN50 t 0.03 (10 m + 4 m) -> 0.1187522 (count 2, length 14); DN50 t 0.05 (2 m) -> 0.0345575. Rows sort by size then
// thickness.
D8_EXTRA_TEST(Rw4InsulationTakeoffSchedule) {
  const double v = InsulationVolumeRound(0.06, 0.03, 10.0);
  double radial = 0;
  const int n = 100000;
  for (int i = 0; i < n; ++i) {
    const double r = 0.03 + 0.03 * (i + 0.5) / n;
    radial += 2.0 * 3.14159265358979323846 * r * (0.03 / n) * 10.0;
  }
  d8test::Check(Near(v, 0.0848230016469244, 1e-12) && Near(v, 3.14159265358979323846 / 4.0 * (0.12 * 0.12 - 0.06 * 0.06) * 10.0, 1e-12) && Near(v, radial, 1e-9) && Near(InsulationOuterSurface(0.06, 0.03, 10.0), 3.7699111843077517, 1e-12), "insulation: 0.0848 m3 equals the annulus and the radial integral, surface 3.7699");
  const auto rows = InsulationSchedule({{"DN50", 0.06, 0.03, 10}, {"DN25", 0.034, 0.02, 5}, {"DN50", 0.06, 0.05, 2}, {"DN50", 0.06, 0.03, 4}});
  d8test::Check(rows.size() == 3 && rows[0].size_label == "DN25" && rows[1].size_label == "DN50" && Near(rows[1].thickness, 0.03) && Near(rows[2].thickness, 0.05), "insulation: three rows sorted by size then thickness");
  d8test::Check(Near(rows[0].volume, 0.01696460032938489, 1e-12) && rows[1].count == 2 && Near(rows[1].length, 14.0) && Near(rows[1].volume, 0.11875220230569418, 1e-12) && Near(rows[2].volume, 0.034557519189487726, 1e-12), "insulation: row volumes 0.01696 / 0.11875 / 0.03456");
  d8test::Check(InsulationSchedule({}).empty() && InsulationVolumeRound(0.06, 0.0, 10.0) == 0.0, "insulation: nothing to schedule, no thickness no volume");
}

// D8-ARCHICAD-MEP-0012: "Add an insulation layer of a given thickness to duct and pipe routes, either uniformly for all parts of a route or
// separately per part."
// Route parts (d, L): (0.10, 3), (0.08, 2), (0.05, 1). Uniform 0.02: annuli pi/4 ((d+0.04)^2 - d^2) L sum to 0.0395841. Per part 0.02 / 0.03 / 0.04: 0.0546637.
D8_EXTRA_TEST(Rw4RouteInsulationUniformPerPart) {
  const double d[3] = {0.10, 0.08, 0.05}, len[3] = {3, 2, 1}, per[3] = {0.02, 0.03, 0.04};
  double uni = 0, sep = 0;
  for (int i = 0; i < 3; ++i) {
    uni += InsulationVolumeRound(d[i], 0.02, len[i]);
    sep += InsulationVolumeRound(d[i], per[i], len[i]);
  }
  d8test::Check(Near(uni, 0.03958406743523139, 1e-12) && Near(sep, 0.054663712172462416, 1e-12), "route insulation: uniform 0.0396 m3, per part 0.0547 m3");
  double a = 0;
  for (int i = 0; i < 3; ++i) a += 3.14159265358979323846 / 4.0 * ((d[i] + 2 * per[i]) * (d[i] + 2 * per[i]) - d[i] * d[i]) * len[i];
  d8test::Check(Near(a, sep, 1e-12) && sep > uni, "route insulation: per-part total equals the annulus sum and is larger here");
  const auto rows = InsulationSchedule({{"d100", d[0], 0.02, 3}, {"d80", d[1], 0.02, 2}, {"d50", d[2], 0.02, 1}});
  double s = 0;
  for (const auto& r : rows) s += r.volume;
  d8test::Check(Near(s, uni, 1e-12), "route insulation: the schedule of the uniform route adds up to the same volume");
}

// D8-REVIT-SYS-0056: "Apply a chosen slope value to selected pipe segments or a whole piping system, using the lowest point or a picked
// branch as the reference end that stays fixed."
// Chain (0,0) (4,0) (4,3) (10,3) at z = 1.0, 0.9, 1.2, 0.5: the lowest point is the last (0.5); slope 0.02 rising away from it: distances
// 0, 6, 9, 13 from the reference give z = 0.5, 0.62, 0.68, 0.76 (listed from the reference). Tree: a branch (4,0)-(4,-2) with reference at point 0:
// distances 0, 4, 7, 13 and 6 for the branch: z = 1.0 + 0.02 d.
D8_EXTRA_TEST(Rw4PipeSlopeFromReference) {
  const std::vector<P2> p = {{0, 0}, {4, 0}, {4, 3}, {10, 3}};
  const std::vector<double> z = {1.0, 0.9, 1.2, 0.5};
  const std::vector<std::pair<int, int>> e = {{0, 1}, {1, 2}, {2, 3}};
  const int low = LowestPoint(z);
  d8test::Check(low == 3, "pipe slope: the lowest point is the last node");
  const auto n = ApplySlopeFromReference(p, z, e, low, 0.02);
  d8test::Check(Near(n[3], 0.5) && Near(n[2], 0.62, 1e-12) && Near(n[1], 0.68, 1e-12) && Near(n[0], 0.76, 1e-12), "pipe slope: elevations rise 2 % away from the fixed lowest point");
  const std::vector<P2> p2 = {{0, 0}, {4, 0}, {4, 3}, {10, 3}, {4, -2}};
  const std::vector<double> z2 = {1.0, 0.9, 1.2, 0.5, 0.3};
  const auto t = ApplySlopeFromReference(p2, z2, {{0, 1}, {1, 2}, {2, 3}, {1, 4}}, 0, 0.02);
  d8test::Check(Near(t[0], 1.0) && Near(t[1], 1.08, 1e-12) && Near(t[2], 1.14, 1e-12) && Near(t[3], 1.26, 1e-12) && Near(t[4], 1.12, 1e-12), "pipe slope: a branch continues from its junction (1.08 + 2 x 0.02)");
  const auto nn = ApplySlopeFromReference(p, z, e, 1, -0.01);
  d8test::Check(Near(nn[1], 0.9) && Near(nn[0], 0.86, 1e-12) && Near(nn[2], 0.87, 1e-12) && Near(nn[3], 0.81, 1e-12), "pipe slope: a negative slope falls away from the reference");
  const auto bad = ApplySlopeFromReference(p, z, e, 9, 0.02);
  d8test::Check(bad == z, "pipe slope: an invalid reference leaves the elevations");
}

// D8-ARCHICAD-MEP-0030: "Set route segment slope during input in percent, degrees or rise over run, keep the slope when the elevation
// changes ..." (inheritance by branches and the plan/3D/section feedback are not tested)
// 2 % = 0.02 = rise 1 over run 50 = atan(0.02) = 1.14576 degrees; 45 degrees = 1 (100 %); 10 degrees = tan 10 = 0.176327 (17.6327 %). A 12 m (plan) segment
// falling at 0.02 from 0.5: the far end is 0.26; with the far end fixed at 0.26 the near end must be 0.5.
D8_EXTRA_TEST(Rw4RouteSlopeUnits) {
  d8test::Check(Near(SlopeFromPercent(2.0), 0.02) && Near(SlopeFromRiseRun(1, 50), 0.02) && Near(SlopeToDegrees(0.02), 1.1457628381751035, 1e-12) && Near(SlopeToPercent(0.02), 2.0), "route slope: 2 % = 1:50 = 1.14576 degrees");
  d8test::Check(Near(SlopeFromDegrees(45.0), 1.0, 1e-12) && Near(SlopeFromDegrees(10.0), 0.17632698070846498, 1e-12) && Near(SlopeToPercent(SlopeFromDegrees(10.0)), 17.632698070846498, 1e-9), "route slope: 45 degrees is 100 %, 10 degrees is 17.6327 %");
  d8test::Check(Near(SlopeToDegrees(SlopeFromDegrees(7.5)), 7.5, 1e-12) && Near(SlopeFromPercent(SlopeToPercent(0.037)), 0.037, 1e-15), "route slope: conversions round-trip");
  d8test::Check(Near(EndElevationKeepingSlope(0.5, -0.02, 12.0, true), 0.26, 1e-12) && Near(EndElevationKeepingSlope(0.26, -0.02, 12.0, false), 0.5, 1e-12), "route slope: the far end follows the slope when the near end is fixed, and the other way round");
}

// D8-REVIT-SYS-0010 (static regain): "Size ducts by equal friction, velocity, static regain or fixed constraints."
// Static regain after a branch: R rho/2 (V1^2 - V2^2) = friction loss of the next section. V1 = 10 m/s, rho = 1.2, R = 0.75, friction loss 15 Pa:
// V2^2 = 100 - 2 x 15/(0.75 x 1.2) = 66.667, V2 = 8.164966 m/s, and substituting back gives a regain of 15 Pa (0.75 x 0.6 x 33.333). A
// 0.4 m3/s flow at that velocity needs D = sqrt(4 x 0.4/(pi V2)) = 0.2497514 m. A friction loss the regain cannot pay (50 Pa) has no real velocity; R
// outside (0, 1] is refused.
D8_EXTRA_TEST(Rw4StaticRegainDuctSizing) {
  const double v2 = StaticRegainDownstreamVelocity(10.0, 1.2, 0.75, 15.0);
  d8test::Check(Near(v2, 8.164965809277259, 1e-12) && Near(StaticRegain(10.0, v2, 1.2, 0.75), 15.0, 1e-9), "static regain: V2 = 8.16497 m/s and the regain equals the 15 Pa friction loss");
  d8test::Check(Near(dino8::kernel::bim3::DiameterForVelocity(0.4, v2), 0.2497513645582224, 1e-9), "static regain: 0.4 m3/s at V2 needs 0.24975 m");
  d8test::Check(StaticRegainDownstreamVelocity(10.0, 1.2, 0.75, 50.0) < 0 && StaticRegainDownstreamVelocity(10.0, 1.2, 0.0, 15.0) < 0 && StaticRegainDownstreamVelocity(10.0, 1.2, 1.2, 15.0) < 0, "static regain: no real velocity for 50 Pa, R = 0 or R = 1.2");
  d8test::Check(Near(StaticRegainDownstreamVelocity(10.0, 1.2, 1.0, 0.0), 10.0, 1e-12) && StaticRegainDownstreamVelocity(10.0, 1.2, 0.9, 5.0) > StaticRegainDownstreamVelocity(10.0, 1.2, 0.5, 5.0), "static regain: no friction keeps the velocity; a better regain factor allows a higher V2");
}

} // namespace
