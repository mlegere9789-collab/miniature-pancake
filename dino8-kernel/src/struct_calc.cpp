#include "dino8/kernel/struct_calc.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace dino8::kernel {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Fill the derived fields from area, ybar, depth, width, ix, iy.
SectionProps Finish(SectionProps s) {
  s.valid = s.area > 0 && s.depth > 0 && s.width > 0;
  if (!s.valid) return s;
  s.zx_top = s.ix / (s.depth - s.ybar);
  s.zx_bottom = s.ix / s.ybar;
  s.zx = std::min(s.zx_top, s.zx_bottom);
  s.zy = s.iy / (s.width / 2.0);
  s.rx = std::sqrt(s.ix / s.area);
  s.ry = std::sqrt(s.iy / s.area);
  return s;
}
}  // namespace

SectionProps RectSection(double b, double h) {
  SectionProps s;
  if (!(b > 0 && h > 0)) return s;
  s.area = b * h;
  s.ybar = h / 2.0;
  s.depth = h;
  s.width = b;
  s.ix = b * h * h * h / 12.0;
  s.iy = h * b * b * b / 12.0;
  s.plastic_zx = b * h * h / 4.0;
  s = Finish(s);
  // St Venant torsion of a thin rectangle is not a closed form: left at 0.
  return s;
}

SectionProps CircleSection(double d) {
  SectionProps s;
  if (!(d > 0)) return s;
  s.area = kPi * d * d / 4.0;
  s.ybar = d / 2.0;
  s.depth = d;
  s.width = d;
  s.ix = s.iy = kPi * d * d * d * d / 64.0;
  s.plastic_zx = d * d * d / 6.0;
  s.j = kPi * d * d * d * d / 32.0;
  return Finish(s);
}

SectionProps HollowCircleSection(double d_outer, double t) {
  SectionProps s;
  if (!(d_outer > 0 && t > 0 && 2.0 * t < d_outer)) return s;
  const double di = d_outer - 2.0 * t;
  s.area = kPi * (d_outer * d_outer - di * di) / 4.0;
  s.ybar = d_outer / 2.0;
  s.depth = d_outer;
  s.width = d_outer;
  s.ix = s.iy = kPi * (std::pow(d_outer, 4) - std::pow(di, 4)) / 64.0;
  s.plastic_zx = (std::pow(d_outer, 3) - std::pow(di, 3)) / 6.0;
  s.j = 2.0 * s.ix;
  return Finish(s);
}

SectionProps BoxSection(double b, double h, double t) {
  SectionProps s;
  if (!(b > 0 && h > 0 && t > 0 && 2.0 * t < b && 2.0 * t < h)) return s;
  const double bi = b - 2.0 * t, hi = h - 2.0 * t;
  s.area = b * h - bi * hi;
  s.ybar = h / 2.0;
  s.depth = h;
  s.width = b;
  s.ix = (b * h * h * h - bi * hi * hi * hi) / 12.0;
  s.iy = (h * b * b * b - hi * bi * bi * bi) / 12.0;
  s.plastic_zx = (b * h * h - bi * hi * hi) / 4.0;
  return Finish(s);
}

SectionProps ISection(double bf, double tf, double tw, double h) {
  SectionProps s;
  if (!(bf > 0 && tf > 0 && tw > 0 && tw <= bf && 2.0 * tf < h)) return s;
  const double hw = h - 2.0 * tf;
  s.area = 2.0 * bf * tf + hw * tw;
  s.ybar = h / 2.0;
  s.depth = h;
  s.width = bf;
  s.ix = (bf * h * h * h - (bf - tw) * hw * hw * hw) / 12.0;
  s.iy = (2.0 * tf * bf * bf * bf + hw * tw * tw * tw) / 12.0;
  s.plastic_zx = bf * tf * (h - tf) + tw * hw * hw / 4.0;
  return Finish(s);
}

SectionProps TSection(double bf, double tf, double tw, double h) {
  SectionProps s;
  if (!(bf > 0 && tf > 0 && tw > 0 && tw <= bf && tf < h)) return s;
  const double hw = h - tf;
  const double af = bf * tf, aw = tw * hw;
  const double yf = h - tf / 2.0, yw = hw / 2.0;
  s.area = af + aw;
  s.ybar = (af * yf + aw * yw) / s.area;
  s.depth = h;
  s.width = bf;
  s.ix = bf * tf * tf * tf / 12.0 + af * (yf - s.ybar) * (yf - s.ybar) + tw * hw * hw * hw / 12.0 + aw * (yw - s.ybar) * (yw - s.ybar);
  s.iy = (tf * bf * bf * bf + hw * tw * tw * tw) / 12.0;
  return Finish(s);
}

double MemberMass(double section_area, double length, double density) {
  if (!(section_area > 0) || !(density > 0) || !(length > 0)) return 0;
  return section_area * length * density;
}

FramingQuantities ComputeFraming(const FramingMember& m) {
  FramingQuantities q;
  q.cut_length = std::max(0.0, m.length + m.start_extension + m.end_extension - m.start_cutback - m.end_cutback);
  q.volume = std::max(0.0, m.section_area) * q.cut_length;
  return q;
}

double RebarMassPerLength(double diameter, double density) {
  if (!(diameter > 0) || !(density > 0)) return 0;
  return kPi * diameter * diameter / 4.0 * density;
}
double RebarMass(double total_length, double mass_per_length) { return std::max(0.0, total_length) * std::max(0.0, mass_per_length); }
double RebarSetMass(int count, double bar_length, double mass_per_length) { return std::max(0, count) * RebarMass(bar_length, mass_per_length); }

double LapLength(double bar_diameter, double lap_multiplier, double manual_override) {
  if (manual_override > 0) return manual_override;
  return std::max(0.0, bar_diameter) * std::max(0.0, lap_multiplier);
}
double StaggerLength(double lap_length, double stagger_multiplier) { return std::max(0.0, lap_length) * std::max(0.0, stagger_multiplier); }

double RoundLength(double value, RoundMethod method, double increment) {
  if (method == RoundMethod::None || !(increment > 0)) return value;
  const double q = value / increment;
  const double tol = 1e-9;  // a value that is a multiple of the increment up to rounding noise stays where it is
  double n = q;
  switch (method) {
    case RoundMethod::Nearest: n = std::floor(q + 0.5 + tol); break;
    case RoundMethod::Up: n = std::ceil(q - tol); break;
    case RoundMethod::Down: n = std::floor(q + tol); break;
    case RoundMethod::None: break;
  }
  return n * increment;
}

BarLayout ComputeBarLayout(const BarLayoutInput& in) {
  BarLayout r;
  const double tol = 1e-9;
  switch (in.rule) {
    case BarLayoutRule::Single:
      r.ok = true; r.count = 1; return r;
    case BarLayoutRule::FixedNumber:
      if (in.number < 1 || in.array_length < 0) return r;
      r.count = in.number;
      r.spacing = in.number > 1 ? in.array_length / (in.number - 1) : 0.0;
      break;
    case BarLayoutRule::MaximumSpacing:
      if (!(in.spacing > 0) || in.array_length < 0) return r;
      r.count = static_cast<int>(std::ceil(in.array_length / in.spacing - tol)) + 1;
      r.spacing = r.count > 1 ? in.array_length / (r.count - 1) : 0.0;
      break;
    case BarLayoutRule::NumberWithSpacing:
      if (in.number < 1 || !(in.spacing > 0)) return r;
      r.count = in.number;
      r.spacing = in.spacing;
      break;
    case BarLayoutRule::MinimumClearSpacing: {
      const double pitch = in.bar_diameter + in.min_clear;
      if (!(pitch > 0) || in.array_length < 0) return r;
      r.count = static_cast<int>(std::floor(in.array_length / pitch + tol)) + 1;
      r.spacing = r.count > 1 ? in.array_length / (r.count - 1) : 0.0;
      break;
    }
  }
  r.span = (r.count - 1) * r.spacing;
  r.ok = true;
  return r;
}

TakeoffResult TakeOffByLevelMaterial(const std::vector<TakeoffItem>& items) {
  TakeoffResult out;
  std::map<std::pair<std::string, std::string>, TakeoffRow> rows;
  std::map<std::string, TakeoffRow> levels, mats;
  for (const TakeoffItem& it : items) {
    TakeoffRow& r = rows[{it.level, it.material}];
    r.level = it.level; r.material = it.material; r.count += 1; r.volume += it.volume;
    TakeoffRow& l = levels[it.level];
    l.level = it.level; l.count += 1; l.volume += it.volume;
    TakeoffRow& m = mats[it.material];
    m.material = it.material; m.count += 1; m.volume += it.volume;
    out.total_volume += it.volume;
    out.total_count += 1;
  }
  for (auto& kv : rows) out.rows.push_back(kv.second);
  for (auto& kv : levels) out.level_totals.push_back(kv.second);
  for (auto& kv : mats) out.material_totals.push_back(kv.second);
  return out;
}

ZoneLoadsResult GenerateZoneLiveLoads(const std::vector<ZoneLoadInput>& zones, const std::vector<LoadCategory>& mapping) {
  ZoneLoadsResult out;
  for (const ZoneLoadInput& z : zones) {
    ZoneLoad zl;
    for (const LoadCategory& c : mapping) {
      if (c.classification == z.classification) {
        zl.mapped = true; zl.category = c.category; zl.unit_value = c.unit_value; zl.load = std::max(0.0, z.area) * c.unit_value;
        break;
      }
    }
    if (!zl.mapped) ++out.unmapped;
    out.total_load += zl.load;
    out.zones.push_back(zl);
  }
  return out;
}

}  // namespace dino8::kernel
