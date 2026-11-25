// Carbon accounting for early-stage design: embodied carbon as quantity x emission factor by life-cycle stage, a factor database with
// regions and user entries, operational carbon from energy and grid factors per region and year, whole-life totals per floor area,
// breakdowns with hot spots, and scenario comparison over time against a target.
//
// Header-only, standard library only. Units: kgCO2e; energy in kWh; areas in square metres. Stage names are free strings; the usual
// module labels (A1A3, A4, A5, B1 .. B7, C1 .. C4, D) are only a convention, the code sums whatever stages a factor carries.
#pragma once

#include <algorithm>
#include <cmath>
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

// Comma-separated report with a header row and one row per group, then the total.
inline std::string CarbonCsv(const std::string& groupTitle, const std::map<std::string, double>& group) {
  std::ostringstream os;
  os << groupTitle << ",kgCO2e\n";
  double total = 0;
  for (const auto& kv : group) {
    os << kv.first << "," << kv.second << "\n";
    total += kv.second;
  }
  os << "total," << total << "\n";
  return os.str();
}

}  // namespace forma
}  // namespace kernel
}  // namespace dino8
