// Composite wall skin quantities and layer thermal data (include/dino8/kernel/arch_calc.h). Expected values are hand-derived; the
// derivation (rational arithmetic outside the code under test) is in each comment.
#include <cmath>
#include <vector>

#include "dino8/kernel/arch_calc.h"
#include "test_registry.h"

namespace {
using namespace dino8::kernel;
bool Near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
}  // namespace

// D8-ARCHICAD-ATTRIB-0013: "List each skin of a composite or each fill of a profile as a component row with its building material,
// thickness and net or gross volume, so material quantities can be reported per skin and per building material" (Attributes).
// Wall 6 long, 2.8 high, with a 0.9 x 2.1 door and a 1.2 x 1.2 window: gross face 16.8, openings 1.89 + 1.44 = 3.33, net face 13.47.
// Skins brick 0.20, insulation 0.05, plaster 0.05 (total 0.30): gross volumes 3.36, 0.84, 0.84 (sum 5.04), net volumes
// 13.47 * 0.20 = 2.694, 13.47 * 0.05 = 0.6735 twice (sum 4.041). A second wall of the same material adds to a per-material sum.
D8_EXTRA_TEST(ArchWallSkinComponentRows) {
  std::vector<WallSkin> skins = {{"Brick", 0.20}, {"Insulation", 0.05}, {"Plaster", 0.05}};
  std::vector<WallOpening> ops = {{1.0, 0.9, 0.0, 2.1}, {3.0, 1.2, 0.9, 1.2}};
  WallSkinQuantities q = ComputeWallSkinQuantities(6.0, 2.8, 2.8, skins, ops);
  d8test::Check(q.ok && q.rows.size() == 3 && Near(q.total_thickness, 0.30, 1e-15), "three skin rows, total thickness 0.30");
  d8test::Check(q.rows[0].material == "Brick" && Near(q.rows[0].thickness, 0.20, 1e-15) && Near(q.rows[0].gross_volume, 3.36, 1e-12) && Near(q.rows[0].net_volume, 2.694, 1e-12), "brick skin: gross 3.36, net 2.694");
  d8test::Check(q.rows[1].material == "Insulation" && Near(q.rows[1].gross_volume, 0.84, 1e-12) && Near(q.rows[1].net_volume, 0.6735, 1e-12), "insulation skin: gross 0.84, net 0.6735");
  d8test::Check(q.rows[2].material == "Plaster" && Near(q.rows[2].gross_volume, 0.84, 1e-12) && Near(q.rows[2].net_volume, 0.6735, 1e-12), "plaster skin: gross 0.84, net 0.6735");
  d8test::Check(Near(q.gross_volume, 5.04, 1e-12) && Near(q.net_volume, 4.041, 1e-12), "skin sums: gross 5.04, net 4.041");
  WallQuantitiesResult whole = ComputeWallQuantities(6.0, 2.8, 2.8, 0.30, ops);
  d8test::Check(Near(q.gross_volume, whole.gross_volume, 1e-12) && Near(q.net_volume, whole.net_volume, 1e-12), "the skin rows add up to the whole-wall quantities of ComputeWallQuantities");
  // Sloped top 2 -> 4 over 8 (net face 20.5 with the openings of the sloped-wall case), one 0.2 skin: net 4.1, gross 4.8.
  WallSkinQuantities s = ComputeWallSkinQuantities(8.0, 2.0, 4.0, {{"Concrete", 0.2}}, {{2.0, 2.0, 0.5, 1.0}, {6.0, 2.0, 3.0, 2.0}});
  d8test::Check(s.rows.size() == 1 && Near(s.rows[0].gross_volume, 4.8, 1e-12) && Near(s.rows[0].net_volume, 4.1, 1e-12), "sloped wall, one skin of 0.2: gross 4.8, net 4.1");
  WallSkinQuantities overlap = ComputeWallSkinQuantities(6.0, 2.8, 2.8, skins, {{1.0, 2.0, 0.0, 2.0}, {2.0, 2.0, 0.0, 2.0}});
  d8test::Check(!overlap.ok, "overlapping openings are flagged, as for the whole wall");
  WallSkinQuantities empty = ComputeWallSkinQuantities(6.0, 2.8, 2.8, {}, ops);
  d8test::Check(empty.rows.empty() && empty.gross_volume == 0.0, "a wall with no skins lists no rows");
}

// D8-ARCHICAD-TOOLBOX-0104: "Store conductivity, density and heat capacity values on building materials so composite skins can
// supply layer data to a U-value calculation for energy evaluation" (Building Materials).
// Layers: brick 0.10 m (lambda 0.77, rho 1800, c 900), insulation 0.10 (0.04, 30, 1400), concrete 0.20 (1.7, 2400, 1000).
//   R layers = 0.10 / 0.77 + 0.10 / 0.04 + 0.20 / 1.7 = 0.129870 + 2.5 + 0.117647 = 2.747517; with R_si 0.13 and R_se 0.04: 2.917517,
//   U = 1 / 2.917517 = 0.342757 W/m^2K. Areal heat capacity = 1800 * 900 * 0.1 + 30 * 1400 * 0.1 + 2400 * 1000 * 0.2 = 162000 + 4200 +
//   480000 = 646200 J/m^2K; mass per area = 180 + 3 + 480 = 663 kg/m^2. Doubling the insulation to 0.20 m adds 2.5: R_total 5.417517,
//   U 0.184586. A single 0.3 m homogeneous layer of lambda 0.5 with R_si = R_se = 0 gives U = 0.5 / 0.3 = 1.666667.
D8_EXTRA_TEST(ArchThermalUValue) {
  std::vector<ThermalLayer> w = {{0.10, 0.77, 1800, 900}, {0.10, 0.04, 30, 1400}, {0.20, 1.7, 2400, 1000}};
  ThermalResult t = ComputeThermal(w);
  d8test::Check(t.valid && Near(t.r_layers, 2.747517188693659, 1e-12) && Near(t.r_total, 2.917517188693659, 1e-12), "R of the layers 2.747517, total with the surface resistances 2.917517");
  d8test::Check(Near(t.u_value, 0.34275719227133594, 1e-12), "U = 0.342757 W/m^2K");
  d8test::Check(Near(t.areal_heat_capacity, 646200.0, 1e-6) && Near(t.mass_per_area, 663.0, 1e-9), "areal heat capacity 646200 J/m^2K, mass 663 kg/m^2");
  w[1].thickness = 0.20;
  ThermalResult t2 = ComputeThermal(w);
  d8test::Check(Near(t2.r_total, 5.417517188693659, 1e-12) && Near(t2.u_value, 0.18458640096001852, 1e-12), "doubling the insulation: R 5.417517, U 0.184586");
  ThermalResult h = ComputeThermal({{0.30, 0.5, 800, 1000}}, 0.0, 0.0);
  d8test::Check(h.valid && Near(h.u_value, 0.5 / 0.3, 1e-12), "homogeneous 0.3 m layer, no surface resistance: U = lambda / d = 1.666667");
  d8test::Check(!ComputeThermal({{0.1, 0.0, 1, 1}}).valid && !ComputeThermal({{-0.1, 0.5, 1, 1}}).valid && !ComputeThermal({}).valid, "a layer without conductivity, a negative thickness or no layers is invalid");
  ThermalResult order = ComputeThermal({w[2], w[0], w[1]});
  d8test::Check(Near(order.u_value, t2.u_value, 1e-12), "the layer order does not change the steady-state U-value");
  ThermalResult m = ComputeThermal(w, 0.17, 0.04);
  d8test::Check(Near(m.r_total, t2.r_layers + 0.21, 1e-12), "the surface resistances are inputs (0.17 / 0.04)");
}
