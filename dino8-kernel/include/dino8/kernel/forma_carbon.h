// Carbon accounting for early-stage design: embodied carbon as quantity x emission factor by life-cycle stage, a factor database with
// regions and user entries, operational carbon from energy and grid factors per region and year, whole-life totals per floor area,
// breakdowns with hot spots, and scenario comparison over time against a target.
//
// Header-only, standard library only. Units: kgCO2e; energy in kWh; areas in square metres. Stage names are free strings; the usual
// module labels (A1A3, A4, A5, B1 .. B7, C1 .. C4, D) are only a convention, the code sums whatever stages a factor carries.
#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace dino8 {
namespace kernel {
namespace forma {

struct EmissionFactor {
  std::string material;
  std::string region = "global";
  std::string unit;                           // the unit the quantities of this material are given in (m3, kg, m2 ...)
  std::map<std::string, double> perUnit;      // stage -> kgCO2e per unit (negative values allowed, e.g. module D credits)
  bool user = false;
};

class FactorDb {
 public:
  void Add(const EmissionFactor& f) { rows_.push_back(f); }
  // Lookup order: a user entry for the region, a user entry for "global", a built-in entry for the region, a built-in entry for "global".
  const EmissionFactor* Find(const std::string& material, const std::string& region) const {
    const EmissionFactor* hit = nullptr;
    int bestRank = 99;
    for (const EmissionFactor& f : rows_) {
      if (f.material != material) continue;
      int rank = -1;
      if (f.user && f.region == region) rank = 0;
      else if (f.user && f.region == "global") rank = 1;
      else if (!f.user && f.region == region) rank = 2;
      else if (!f.user && f.region == "global") rank = 3;
      if (rank >= 0 && rank < bestRank) {
        bestRank = rank;
        hit = &f;
      }
    }
    return hit;
  }
  std::set<std::string> Regions() const {
    std::set<std::string> r;
    for (const EmissionFactor& f : rows_) r.insert(f.region);
    return r;
  }

 private:
  std::vector<EmissionFactor> rows_;
};

struct CarbonItem {
  std::string element, system, material;
  double quantity = 0;   // in the unit of the material's factor
};

struct CarbonBreakdown {
  double total = 0;
  std::map<std::string, double> byStage, byMaterial, bySystem, byElement;
  std::vector<std::string> missing;   // materials without a factor in the region (they add nothing)
};

// Embodied carbon of the items for `region`. `stages` limits the sum to those stages (empty = every stage the factor carries).
inline CarbonBreakdown EmbodiedCarbon(const FactorDb& db, const std::string& region, const std::vector<CarbonItem>& items,
                                      const std::set<std::string>& stages = {}) {
  CarbonBreakdown r;
  std::set<std::string> miss;
  for (const CarbonItem& it : items) {
    const EmissionFactor* f = db.Find(it.material, region);
    if (!f) {
      miss.insert(it.material);
      continue;
    }
    double sum = 0;
    for (const auto& st : f->perUnit) {
      if (!stages.empty() && !stages.count(st.first)) continue;
      const double c = it.quantity * st.second;
      r.byStage[st.first] += c;
      sum += c;
    }
    r.total += sum;
    r.byMaterial[it.material] += sum;
    r.bySystem[it.system] += sum;
    r.byElement[it.element] += sum;
  }
  r.missing.assign(miss.begin(), miss.end());
  return r;
}

// Groups whose share of the total is at least `share`, largest first.
inline std::vector<std::pair<std::string, double>> HotSpots(const std::map<std::string, double>& group, double share) {
  double total = 0;
  for (const auto& kv : group) total += kv.second;
  std::vector<std::pair<std::string, double>> out;
  if (total <= 0) return out;
  for (const auto& kv : group)
    if (kv.second / total >= share - 1e-12) out.push_back(kv);
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });
  return out;
}

// Grid carbon factors (kgCO2e per kWh) per region: points (year, factor); between points linear, outside the held at the end values.
class GridFactors {
 public:
  void SetRegion(const std::string& region, std::vector<std::pair<int, double>> points) {
    std::sort(points.begin(), points.end());
    table_[region] = points;
  }
  bool Has(const std::string& region) const { return table_.count(region) > 0; }
  double Factor(const std::string& region, int year) const {
    const auto it = table_.find(region);
    if (it == table_.end() || it->second.empty()) return 0;
    const auto& p = it->second;
    if (year <= p.front().first) return p.front().second;
    if (year >= p.back().first) return p.back().second;
    for (size_t i = 1; i < p.size(); ++i)
      if (year <= p[i].first) {
        const double t = static_cast<double>(year - p[i - 1].first) / (p[i].first - p[i - 1].first);
        return p[i - 1].second + t * (p[i].second - p[i - 1].second);
      }
    return p.back().second;
  }

 private:
  std::map<std::string, std::vector<std::pair<int, double>>> table_;
};

// Operational carbon of `kwhPerYear` over `years` years starting at `startYear` (each year at its own grid factor).
inline double OperationalCarbon(const GridFactors& g, const std::string& region, double kwhPerYear, int startYear, int years) {
  double s = 0;
  for (int y = 0; y < years; ++y) s += kwhPerYear * g.Factor(region, startYear + y);
  return s;
}

struct WholeLife {
  double embodied = 0, operational = 0, total = 0;
  double embodiedPerM2 = 0, operationalPerM2 = 0, totalPerM2 = 0;
};
inline WholeLife WholeLifeCarbon(double embodied, double operational, double floorArea) {
  WholeLife w;
  w.embodied = embodied;
  w.operational = operational;
  w.total = embodied + operational;
  if (floorArea > 0) {
    w.embodiedPerM2 = embodied / floorArea;
    w.operationalPerM2 = operational / floorArea;
    w.totalPerM2 = w.total / floorArea;
  }
  return w;
}

// Scenario over time: cumulative carbon after t years = embodied + sum of the yearly operational values of the first t years.
struct CarbonScenario {
  std::string name;
  double embodied = 0;
  std::vector<double> operationalPerYear;   // year 1, 2, ...
  double Cumulative(int years) const {
    double s = embodied;
    for (int y = 0; y < years && y < static_cast<int>(operationalPerYear.size()); ++y) s += operationalPerYear[y];
    return s;
  }
};
// First year at which scenario a's cumulative carbon is no longer below b's (-1 when never within the horizon).
inline int CrossoverYear(const CarbonScenario& a, const CarbonScenario& b, int horizon) {
  for (int t = 0; t <= horizon; ++t)
    if (a.Cumulative(t) >= b.Cumulative(t)) return t;
  return -1;
}
struct TargetGap {
  double value = 0, target = 0, gap = 0;   // gap = value - target; positive = over the target
  bool meets = false;
};
inline TargetGap CompareToTarget(double value, double target) { return {value, target, value - target, value <= target + 1e-12}; }

// A double as the shortest plain decimal text that reads back to the same double: no exponent, no digit grouping, a point as the decimal
// separator whatever the global locale is (the streams are imbued with the classic locale; no C library formatting is used). Non-finite
// values are written as nan, inf and -inf.
inline std::string ShortestDecimal(double v) {
  if (std::isnan(v)) return "nan";
  if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
  if (v == 0) return "0";   // also negative zero
  std::string last;
  for (int decimals = 0; decimals <= 340; ++decimals) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(decimals) << v;
    last = o.str();
    std::istringstream in(last);
    in.imbue(std::locale::classic());
    double back = 0;
    in >> back;
    if (!in.fail() && back == v) break;
  }
  return last;
}

// A CSV field per RFC 4180: quoted when it holds a comma, a double quote or a line break, quotes doubled.
inline std::string CsvField(const std::string& f) {
  if (f.find_first_of(",\"\r\n") == std::string::npos) return f;
  std::string q = "\"";
  for (char c : f) {
    if (c == '"') q += '"';
    q += c;
  }
  return q + "\"";
}

// Comma-separated report with a header row and one row per group, then the total. Numbers are the shortest decimal text that reads back
// exactly (so a re-import keeps the breakdown-sums-to-total property to the last bit), independent of the host's locale.
inline std::string CarbonCsv(const std::string& groupTitle, const std::map<std::string, double>& group) {
  std::string out = CsvField(groupTitle) + ",kgCO2e\n";
  double total = 0;
  for (const auto& kv : group) {
    out += CsvField(kv.first) + "," + ShortestDecimal(kv.second) + "\n";
    total += kv.second;
  }
  out += "total," + ShortestDecimal(total) + "\n";
  return out;
}

// ------------------------------------------------------------------------------------------------------------------------------
// Regions: embodied factors, grid carbon factors and climate per region, added over time
// ------------------------------------------------------------------------------------------------------------------------------
struct RegionPack {
  std::string code;                                   // region code, for example "UK"
  std::vector<EmissionFactor> embodied;               // local embodied-carbon factors (their region field is overwritten with `code`)
  std::vector<std::pair<int, double>> gridPerYear;    // grid carbon factor (kgCO2e per kWh) at table years
  std::vector<double> dailyMeanTempC;                 // climate: the daily mean outdoor temperature of a typical year (365 or 366 values)
};

// Schema check of a pack. Every error is one line of text; an empty list means the pack is valid.
inline std::vector<std::string> ValidateRegionPack(const RegionPack& p) {
  std::vector<std::string> e;
  if (p.code.empty()) e.push_back("code: empty");
  if (p.embodied.empty()) e.push_back("embodied: no factors");
  for (size_t i = 0; i < p.embodied.size(); ++i) {
    const EmissionFactor& f = p.embodied[i];
    if (f.material.empty()) e.push_back("embodied[" + std::to_string(i) + "]: empty material");
    if (f.unit.empty()) e.push_back("embodied[" + std::to_string(i) + "]: empty unit");
    if (f.perUnit.empty()) e.push_back("embodied[" + std::to_string(i) + "]: no stages");
    for (const auto& st : f.perUnit)
      if (!std::isfinite(st.second)) e.push_back("embodied[" + std::to_string(i) + "]: stage " + st.first + " is not a finite number");
  }
  if (p.gridPerYear.empty()) e.push_back("grid: no table");
  std::set<int> years;
  for (size_t i = 0; i < p.gridPerYear.size(); ++i) {
    if (!years.insert(p.gridPerYear[i].first).second) e.push_back("grid: year " + std::to_string(p.gridPerYear[i].first) + " twice");
    if (!(p.gridPerYear[i].second >= 0) || !std::isfinite(p.gridPerYear[i].second)) e.push_back("grid: year " + std::to_string(p.gridPerYear[i].first) + " has a negative or non-finite factor");
  }
  if (p.dailyMeanTempC.size() != 365 && p.dailyMeanTempC.size() != 366) e.push_back("climate: " + std::to_string(p.dailyMeanTempC.size()) + " daily values, need 365 or 366");
  for (size_t i = 0; i < p.dailyMeanTempC.size(); ++i)
    if (!(p.dailyMeanTempC[i] >= -90.0 && p.dailyMeanTempC[i] <= 60.0)) {
      e.push_back("climate: day " + std::to_string(i + 1) + " outside -90 .. 60 C");
      break;
    }
  return e;
}

// Degree days of a daily mean temperature series: heating counts base - T where T is below the base, cooling T - base above it.
inline double DegreeDays(const std::vector<double>& dailyMeanC, double baseC, bool heating) {
  double s = 0;
  for (double t : dailyMeanC) s += heating ? std::max(0.0, baseC - t) : std::max(0.0, t - baseC);
  return s;
}

class RegionLibrary {
 public:
  // Adds a valid pack and returns an empty list; an invalid pack, or a code that is already present, is refused (nothing changes) and the
  // reasons are returned.
  std::vector<std::string> Add(const RegionPack& pack) {
    std::vector<std::string> err = ValidateRegionPack(pack);
    if (!pack.code.empty() && Has(pack.code)) err.push_back("code: region " + pack.code + " already present");
    if (!err.empty()) return err;
    for (EmissionFactor f : pack.embodied) {
      f.region = pack.code;
      factors_.Add(f);
    }
    grid_.SetRegion(pack.code, pack.gridPerYear);
    climate_[pack.code] = pack.dailyMeanTempC;
    order_.push_back(pack.code);
    return err;
  }
  bool Has(const std::string& region) const { return climate_.count(region) > 0; }
  const std::vector<std::string>& Regions() const { return order_; }
  const FactorDb& Factors() const { return factors_; }
  const GridFactors& Grid() const { return grid_; }
  double DegreeDaysOf(const std::string& region, double baseC, bool heating) const {
    const auto it = climate_.find(region);
    return it == climate_.end() ? 0.0 : DegreeDays(it->second, baseC, heating);
  }

 private:
  FactorDb factors_;
  GridFactors grid_;
  std::map<std::string, std::vector<double>> climate_;
  std::vector<std::string> order_;
};

struct RegionalCarbonResult {
  bool ok = false;
  double embodied = 0, operational = 0, total = 0;
  std::vector<std::string> missing;
};
// Embodied carbon with the region's own factors plus operational carbon with its grid table.
inline RegionalCarbonResult RegionalCarbon(const RegionLibrary& lib, const std::string& region, const std::vector<CarbonItem>& items, double kwhPerYear,
                                           int startYear, int years) {
  RegionalCarbonResult r;
  if (!lib.Has(region)) return r;
  const CarbonBreakdown b = EmbodiedCarbon(lib.Factors(), region, items);
  r.ok = true;
  r.embodied = b.total;
  r.missing = b.missing;
  r.operational = OperationalCarbon(lib.Grid(), region, kwhPerYear, startYear, years);
  r.total = r.embodied + r.operational;
  return r;
}

}  // namespace forma
}  // namespace kernel
}  // namespace dino8
