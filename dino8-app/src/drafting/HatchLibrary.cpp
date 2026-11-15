#include "drafting/HatchLibrary.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dino8::app::drafting {

namespace {

kernel::NurbsCurve MakeSegment(kernel::Point3d a, kernel::Point3d b) {
  ON_Polyline pl;
  pl.Append(a);
  pl.Append(b);
  ON_PolylineCurve pc(pl);
  ON_NurbsCurve nc;
  pc.GetNurbForm(nc);
  kernel::NurbsCurve out;
  out.raw() = nc;
  return out;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> SplitCommas(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ',') { out.push_back(Trim(cur)); cur.clear(); }
    else cur += c;
  }
  out.push_back(Trim(cur));
  return out;
}

// Built-in copy of the library, used when no hatchpatterns.pat is found on
// disk (a packaged build, or the file being edited away).
const char* const kBuiltinPat = R"PAT(
*SOLID, Solid fill
45, 0,0, 0,1
*ANSI31, ANSI Iron, Brick, Stone masonry
45, 0,0, 0,.125
*ANSI32, ANSI Steel
45, 0,0, 0,.375
45, .176776695,0, 0,.375
*ANSI33, ANSI Bronze, Brass, Copper
45, 0,0, 0,.25
45, .176776695,0, 0,.25, .125,-.0625
*ANSI34, ANSI Plastic, Rubber
45, 0,0, 0,.75
45, .176776695,0, 0,.75
45, .353553391,0, 0,.75
45, .530330086,0, 0,.75
*ANSI35, ANSI Fire brick, Refractory material
45, 0,0, 0,.25
45, .176776695,0, 0,.25, .3125,-.0625,0,-.0625
*ANSI36, ANSI Marble, Slate, Glass
45, 0,0, .21875,.125, .3125,-.0625,0,-.0625
*ANSI37, ANSI Lead, Zinc, Magnesium, Sound/Heat/Elec Insulation
45, 0,0, 0,.125
135, 0,0, 0,.125
*ANSI38, ANSI Aluminum
45, 0,0, 0,.125
135, 0,0, .25,.125, .3125,-.1875
*BRICK, Brick or masonry-type surface
0, 0,0, 0,.25
90, 0,0, 0,.5, .25,-.25
90, .25,0, 0,.5, -.25,.25
*HONEYCOMB, Honeycomb pattern
0, 0,0, .1875,.108253175, .125,-.25
120, 0,0, .1875,.108253175, .125,-.25
60, 0,0, .1875,.108253175, -.25,.125
*DOTS, A series of dots
0, 0,0, .03125,.0625, 0,-.0625
*CROSS, A series of crosses
0, 0,0, .25,.25, .125,-.375
90, .0625,-.0625, .25,.25, .125,-.375
*GRASS, Grass area
90, 0,0, .707106781,.707106781, .1875,-1.226713563
45, 0,0, 0,1, .1875,-.8125
135, 0,0, 0,1, .1875,-.8125
*GRAVEL, Cobble / gravel pattern
228.0127875, 0,.75, 0,1, .1,-.9
184.3987054, .25,.75, 0,1, .2,-.8
120, 0,.5, 0,1, .12,-.88
60, .5,.5, 0,1, .15,-.85
0, .25,.25, 0,1, .3,-.7
315, .75,.35, 0,1, .18,-.82
*NET, Horizontal / vertical grid
0, 0,0, 0,.125
90, 0,0, 0,.125
)PAT";

}  // namespace

bool HatchPattern::IsSolid() const {
  std::string u = name;
  for (char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return u == "SOLID";
}

int ParsePatText(const std::string& text, std::vector<HatchPattern>& out, std::string* error) {
  std::istringstream ss(text);
  std::string line;
  HatchPattern* cur = nullptr;
  int count = 0;
  int lineno = 0;
  while (std::getline(ss, line)) {
    ++lineno;
    std::string t = Trim(line);
    if (t.empty() || t[0] == ';') continue;
    if (t[0] == '*') {
      const std::string body = t.substr(1);
      const size_t comma = body.find(',');
      HatchPattern p;
      p.name = Trim(comma == std::string::npos ? body : body.substr(0, comma));
      p.description = comma == std::string::npos ? "" : Trim(body.substr(comma + 1));
      // Replace an existing pattern of the same name (later files override).
      auto it = std::find_if(out.begin(), out.end(), [&](const HatchPattern& e) {
        std::string a = e.name, b = p.name;
        for (char& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (char& c : b) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return a == b;
      });
      if (it != out.end()) { *it = p; cur = &*it; }
      else { out.push_back(p); cur = &out.back(); }
      ++count;
      continue;
    }
    if (!cur) {
      if (error) *error = "line " + std::to_string(lineno) + ": data before a *NAME header";
      continue;
    }
    std::vector<std::string> fields = SplitCommas(t);
    if (fields.size() < 5) {
      if (error) *error = "line " + std::to_string(lineno) + ": expected angle,x,y,dx,dy[,dashes...]";
      continue;
    }
    HatchFamily fam;
    fam.angle = std::atof(fields[0].c_str());
    fam.x0 = std::atof(fields[1].c_str());
    fam.y0 = std::atof(fields[2].c_str());
    fam.dx = std::atof(fields[3].c_str());
    fam.dy = std::atof(fields[4].c_str());
    for (size_t i = 5; i < fields.size(); ++i) if (!fields[i].empty()) fam.dashes.push_back(std::atof(fields[i].c_str()));
    cur->families.push_back(fam);
  }
  return count;
}

HatchLibrary& HatchLibrary::Instance() {
  static HatchLibrary lib;
  return lib;
}

void HatchLibrary::Load(const std::vector<std::string>& data_dirs) {
  data_dirs_ = data_dirs;
  Reload();
}

void HatchLibrary::Reload() {
  patterns_.clear();
  source_dir_.clear();
  std::string err;
  ParsePatText(kBuiltinPat, patterns_, &err);  // baseline, overridable below
  for (const std::string& dir : data_dirs_) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    if (source_dir_.empty()) source_dir_ = dir;
    // Deterministic order: hatchpatterns.pat first, then any other *.pat alphabetically.
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      if (entry.path().extension() == ".pat") files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
      const bool ah = std::filesystem::path(a).filename() == "hatchpatterns.pat";
      const bool bh = std::filesystem::path(b).filename() == "hatchpatterns.pat";
      if (ah != bh) return ah;
      return a < b;
    });
    for (const std::string& f : files) ImportFile(f);
  }
  loaded_ = true;
}

int HatchLibrary::ImportFile(const std::string& path, std::string* error) {
  std::ifstream f(path);
  if (!f) { if (error) *error = "cannot open " + path; return 0; }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ParsePatText(ss.str(), patterns_, error);
}

void HatchLibrary::EnsureLoaded() {
  if (!loaded_) Reload();
}

const std::vector<HatchPattern>& HatchLibrary::Patterns() {
  EnsureLoaded();
  return patterns_;
}

const HatchPattern* HatchLibrary::Find(const std::string& name) {
  EnsureLoaded();
  std::string u = name;
  for (char& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (const HatchPattern& p : patterns_) {
    std::string pn = p.name;
    for (char& c : pn) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (pn == u) return &p;
  }
  return patterns_.empty() ? nullptr : &patterns_.front();
}

std::vector<std::string> HatchLibrary::Names() {
  EnsureLoaded();
  std::vector<std::string> out;
  for (const HatchPattern& p : patterns_) out.push_back(p.name);
  return out;
}

namespace {

// Clips one infinite family of parallel lines (in the pattern's own,
// unscaled/unrotated space, then mapped onto `plane`) to `loops` (even-odd),
// applying dashes if given. Appends segments (as plane-space (s,t) pairs) to
// `segs2d` when `out3d` is null (thumbnail path), else appends 3D curves.
void ClipFamily(const HatchFamily& fam, const std::vector<Loop>& loops, const ON_Plane& plane, double scale,
                double rotation_deg, kernel::Point3d base, std::vector<kernel::NurbsCurve>* out3d,
                std::vector<double>* segs2d, size_t max_segments, bool* truncated) {
  const double angle = (fam.angle + rotation_deg) * ON_PI / 180.0;
  const kernel::Vector3d dir = plane.xaxis * std::cos(angle) + plane.yaxis * std::sin(angle);
  const kernel::Vector3d perp = ON_CrossProduct(plane.zaxis, dir);
  const kernel::Point3d origin = plane.ClosestPointTo(base) + plane.xaxis * (fam.x0 * scale) + plane.yaxis * (fam.y0 * scale);
  const double spacing = std::fabs(fam.dy) * scale;
  if (spacing <= 1e-12 && !segs2d) return;  // degenerate family (thumbnail still shows the one line)

  // Project every loop into (s, t) coordinates along dir / perp.
  std::vector<std::vector<std::pair<double, double>>> polys2d;
  double tmin = 1e300, tmax = -1e300;
  for (const Loop& loop : loops) {
    std::vector<std::pair<double, double>> p2;
    for (const kernel::Point3d& p : loop) {
      const kernel::Vector3d d = p - origin;
      const double s = ON_DotProduct(d, dir), tt = ON_DotProduct(d, perp);
      p2.push_back({s, tt});
      tmin = std::min(tmin, tt);
      tmax = std::max(tmax, tt);
    }
    polys2d.push_back(std::move(p2));
  }
  if (polys2d.empty() || tmax < tmin) return;

  const double step = spacing > 1e-9 ? spacing : (tmax - tmin > 0 ? tmax - tmin : 1.0);
  const double shift_per_line = fam.dx * scale;
  if ((tmax - tmin) / step > 4'000'000) { if (truncated) *truncated = true; return; }  // absurd density guard

  int line_index = static_cast<int>(std::floor((tmin - 1e-9) / step)) - 1;
  for (double t = line_index * step; t <= tmax + step; t += step, ++line_index) {
    if (t < tmin - step || t > tmax + step) continue;
    const double along_shift = shift_per_line * line_index;
    // Even-odd crossings with every loop at this t.
    std::vector<double> xs;
    for (const auto& p2 : polys2d) {
      for (size_t i = 0; i < p2.size(); ++i) {
        const auto& p = p2[i];
        const auto& q = p2[(i + 1) % p2.size()];
        if ((p.second > t) != (q.second > t)) {
          const double s = p.first + (q.first - p.first) * (t - p.second) / (q.second - p.second);
          xs.push_back(s);
        }
      }
    }
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2) {
      double s0 = xs[i], s1 = xs[i + 1];
      if (s1 - s0 < 1e-9) continue;
      // Apply the dash pattern along the run, phased by `along_shift` so
      // dashes line up between neighbouring lines the way AutoCAD's do.
      std::vector<std::pair<double, double>> runs;
      if (fam.dashes.empty()) {
        runs.push_back({s0, s1});
      } else {
        double period = 0;
        for (double d : fam.dashes) period += std::fabs(d) * scale;
        if (period <= 1e-9) { runs.push_back({s0, s1}); }
        else {
          double phase = std::fmod(along_shift, period);
          if (phase < 0) phase += period;
          double pos = s0 - phase;
          size_t di = 0;
          // Walk back to align dash index 0 at `pos`.
          while (pos < s0) {
            const double len = std::fabs(fam.dashes[di % fam.dashes.size()]) * scale;
            const bool draw = fam.dashes[di % fam.dashes.size()] > 0;
            const double a = std::max(pos, s0), b = std::min(pos + len, s1);
            if (draw && b > a + 1e-9) runs.push_back({a, b});
            pos += len;
            ++di;
            if (di > 200000) break;
          }
          while (pos < s1 && di < 400000) {
            const double len = std::fabs(fam.dashes[di % fam.dashes.size()]) * scale;
            const bool draw = fam.dashes[di % fam.dashes.size()] > 0;
            const double a = std::max(pos, s0), b = std::min(pos + len, s1);
            if (draw && b > a + 1e-9) runs.push_back({a, b});
            pos += len;
            ++di;
          }
        }
      }
      for (auto [a, b] : runs) {
        if (out3d) {
          if (out3d->size() >= max_segments) { if (truncated) *truncated = true; return; }
          out3d->push_back(MakeSegment(origin + dir * a + perp * t, origin + dir * b + perp * t));
        } else if (segs2d) {
          segs2d->push_back(a); segs2d->push_back(t); segs2d->push_back(b); segs2d->push_back(t);
        }
      }
    }
  }
}

}  // namespace

std::vector<kernel::NurbsCurve> HatchPatternCurves(const HatchPattern& pattern, const std::vector<Loop>& loops,
                                                   const ON_Plane& plane, double scale, double rotation,
                                                   kernel::Point3d base, size_t max_segments, bool* truncated) {
  std::vector<kernel::NurbsCurve> out;
  if (truncated) *truncated = false;
  if (scale <= 0) scale = 1;
  for (const HatchFamily& fam : pattern.families) {
    ClipFamily(fam, loops, plane, scale, rotation, base, &out, nullptr, max_segments, truncated);
    if (out.size() >= max_segments) break;
  }
  return out;
}

std::vector<double> HatchPatternPreview(const HatchPattern& pattern, double size, double scale, double rotation,
                                        size_t max_segments) {
  const ON_Plane plane(ON_origin, ON_xaxis, ON_yaxis);
  Loop square = {kernel::Point3d(0, 0, 0), kernel::Point3d(size, 0, 0), kernel::Point3d(size, size, 0),
                 kernel::Point3d(0, size, 0)};
  std::vector<double> segs;
  for (const HatchFamily& fam : pattern.families) {
    ClipFamily(fam, {square}, plane, scale, rotation, kernel::Point3d(0, 0, 0), nullptr, &segs, max_segments, nullptr);
    if (segs.size() / 4 >= max_segments) break;
  }
  return segs;
}

}  // namespace dino8::app::drafting
