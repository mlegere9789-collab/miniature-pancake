// NurbsCurve::SaveIges/LoadIges - a minimal, single-curve, kernel-level
// IGES 5.3 ASCII reader/writer (Type 126, Rational B-Spline Curve).
//
// This is a Document-free port of the exact field layout
// dino8-app/src/io/FileIgesStep.cpp's own ExportIges/ImportIges already use
// at the app level (WriteIges126/EuclideanCV/FullKnots for writing; the
// IgesRawEntity/SplitIgesParams/PNum file-level parser plus
// BuildIgesCurve's own case 126 branch for reading) - the same proven
// encoding, not a fresh derivation, closing this kernel's own
// "App-independent (kernel library) STEP/IGES/PLY/DXF exchange API" gap's
// IGES half (PARITY_MAP.md, kernel: Kernel-level data exchange) the same
// way SaveDxf()/LoadDxf() (mesh.cpp) already closed the DXF half.
#include "dino8/kernel/curve.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace dino8::kernel {

namespace {

std::string IgesNum(double v) {
  if (!std::isfinite(v)) v = 0.0;
  if (std::fabs(v) < 1e-13) v = 0.0;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.9g", v);
  std::string s = buf;
  // An IGES real literal needs a decimal point (a bare "10" is read as an
  // integer by a strict parser) - same correction this codebase's own
  // STEP/IFC writer (FileIgesStep.cpp's Num()) already makes for the
  // identical reason.
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) s += ".";
  return s;
}

std::string IgesTrim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string HString(const std::string& s) { return std::to_string(s.size()) + "H" + s; }

// Full (clamped) knot vector: OpenNURBS drops the superfluous first and
// last knots that a fully-clamped B-spline's own standard knot vector
// repeats once more - IGES (and STEP) need the full form. Identical to
// FileIgesStep.cpp's own FullKnots(const ON_NurbsCurve&).
std::vector<double> IgesFullKnots(const ON_NurbsCurve& c) {
  std::vector<double> k;
  const int n = c.KnotCount();
  if (n <= 0) return k;
  k.push_back(c.Knot(0));
  for (int i = 0; i < n; ++i) k.push_back(c.Knot(i));
  k.push_back(c.Knot(n - 1));
  return k;
}

// Euclidean (not homogeneous) control point and its own real weight -
// identical to FileIgesStep.cpp's own EuclideanCV(const ON_NurbsCurve&, ...).
ON_3dPoint IgesEuclideanCV(const ON_NurbsCurve& c, int i, double& w) {
  ON_4dPoint h;
  c.GetCV(i, h);
  w = c.IsRational() ? h.w : 1.0;
  if (c.IsRational() && std::fabs(h.w) > 1e-300) return ON_3dPoint(h.x / h.w, h.y / h.w, h.z / h.w);
  return ON_3dPoint(h.x, h.y, h.z);
}

// Splits free-format parameter text (already terminated by the record
// delimiter) into lines of at most `width` characters, never breaking a
// Hollerith (nHtext) string mid-way - identical in shape to
// FileIgesStep.cpp's own IgesWriter::SplitParams, duplicated here (not
// shared) since that one lives in dino8-app, not this kernel.
std::vector<std::string> IgesSplitParamsForWrite(const std::string& text, size_t width = 64) {
  std::vector<std::string> lines;
  std::string cur;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = i;
    if (std::isdigit(static_cast<unsigned char>(text[i]))) {
      size_t k = i;
      while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) ++k;
      if (k < text.size() && text[k] == 'H') {
        const size_t n = static_cast<size_t>(std::atol(text.substr(i, k - i).c_str()));
        j = std::min(text.size(), k + 1 + n);
      }
    }
    while (j < text.size() && text[j] != ',' && text[j] != ';') ++j;
    if (j < text.size()) ++j;
    std::string tok = text.substr(i, j - i);
    i = j;
    while (tok.size() > width) {
      if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
      lines.push_back(tok.substr(0, width));
      tok = tok.substr(width);
    }
    if (cur.size() + tok.size() > width) { lines.push_back(cur); cur.clear(); }
    cur += tok;
  }
  if (!cur.empty() || lines.empty()) lines.push_back(cur);
  return lines;
}

// Writes `text` (already a free-format Parameter Data record, no DE
// back-pointer) as however many 80-column Parameter Data card images it
// takes, each one's own columns 66-72/74-80 carrying the fixed DE=1/
// sequence-number back-pointers a single-curve file always has, and
// returns how many P lines were written (for the Terminate section's own
// count below).
int WriteIgesParamLines(std::ostream& os, const std::string& text) {
  const std::vector<std::string> lines = IgesSplitParamsForWrite(text);
  for (size_t i = 0; i < lines.size(); ++i) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%-64s%8dP%7d", lines[i].c_str(), 1, static_cast<int>(i) + 1);
    os << buf << "\n";
  }
  return static_cast<int>(lines.size());
}

}  // namespace

Result NurbsCurve::SaveIges(const std::string& path) const {
  if (!curve_.IsValid()) return Result::Failed;
  const int K = curve_.CVCount() - 1;
  const int M = curve_.Degree();
  if (K < 0 || M < 1) return Result::Failed;

  ON_Plane plane;
  const bool planar = curve_.IsPlanar(&plane, 1e-6);
  std::string p = std::to_string(K) + "," + std::to_string(M) + ",";
  p += std::string(planar ? "1" : "0") + "," + (curve_.IsClosed() ? "1" : "0") + "," + (curve_.IsRational() ? "0" : "1") + ",0,";
  for (double k : IgesFullKnots(curve_)) p += IgesNum(k) + ",";
  std::vector<double> weights;
  std::vector<ON_3dPoint> pts;
  weights.reserve(static_cast<size_t>(K) + 1);
  pts.reserve(static_cast<size_t>(K) + 1);
  for (int i = 0; i <= K; ++i) {
    double wgt;
    pts.push_back(IgesEuclideanCV(curve_, i, wgt));
    weights.push_back(wgt);
  }
  for (double wgt : weights) p += IgesNum(wgt) + ",";
  for (const ON_3dPoint& q : pts) p += IgesNum(q.x) + "," + IgesNum(q.y) + "," + IgesNum(q.z) + ",";
  p += IgesNum(curve_.Domain().Min()) + "," + IgesNum(curve_.Domain().Max()) + ",";
  if (planar) p += IgesNum(plane.zaxis.x) + "," + IgesNum(plane.zaxis.y) + "," + IgesNum(plane.zaxis.z);
  else p += "0,0,0";
  const std::string record = "126," + p + ";";

  std::ofstream out(path);
  if (!out) return Result::Failed;

  char buf[128];
  std::snprintf(buf, sizeof(buf), "%-72sS%7d", "Dino 8 kernel IGES export", 1);
  out << buf << "\n";

  std::time_t now = std::time(nullptr);
  char date[32];
  std::strftime(date, sizeof(date), "%Y%m%d.%H%M%S", std::localtime(&now));
  // Global section: the 24 parameters every real IGES reader expects, with
  // the same field values FileIgesStep.cpp's own IgesWriter::Write uses for
  // everything this Document-free function has no caller-supplied
  // equivalent for (product/file/author/organization identifiers) -
  // millimetres (units_flag 2) as a fixed, honest default since a bare
  // NurbsCurve carries no unit-system field of its own to read one from.
  std::vector<std::string> g = {"1H,",       "1H;",        HString("curve"),  HString(path),
                                HString("Dino 8"), HString("Dino 8 kernel"),
                                "32",        "38",         "6",               "308",
                                "15",        HString("curve"), "1.0",         "2",
                                HString("MM"), "1",          "0.01",            HString(date),
                                IgesNum(1e-6), IgesNum(1e6), HString("Dino 8"), HString(""),
                                "11",        "0",          HString(date),     ""};
  std::string gtext;
  for (size_t i = 0; i < g.size(); ++i) gtext += g[i] + (i + 1 < g.size() ? "," : ";");
  const std::vector<std::string> glines = IgesSplitParamsForWrite(gtext, 72);
  for (size_t i = 0; i < glines.size(); ++i) {
    std::snprintf(buf, sizeof(buf), "%-72sG%7d", glines[i].c_str(), static_cast<int>(i) + 1);
    out << buf << "\n";
  }

  // Directory Entry: exactly one entity (Type 126), two 80-column lines -
  // the same field layout FileIgesStep.cpp's own IgesWriter::Add writes
  // (type, DE1's param-data pointer, structure, line-font, level, view,
  // xform, label-display-assoc, status; DE2's type repeat, line weight,
  // color, param-line count, form, two reserved fields, label, reserved).
  char d1[96], d2[96];
  std::snprintf(d1, sizeof(d1), "%8d%8d%8d%8d%8d%8d%8d%8d%8sD%7d", 126, 1, 0, 0, 0, 0, 0, 0, "00000000", 1);
  out << d1 << "\n";
  // Parameter line count is filled in once the P section below is written
  // (IGES allows writing D before P since the P-line count/DE number are
  // both already known from the free-format text split) - compute first.
  const std::vector<std::string> plines_preview = IgesSplitParamsForWrite(record);
  std::snprintf(d2, sizeof(d2), "%8d%8d%8d%8d%8d%8s%8s%-8s%8dD%7d", 126, 0, 0, static_cast<int>(plines_preview.size()), 0, "", "", "", 0, 2);
  out << d2 << "\n";

  const int p_count = WriteIgesParamLines(out, record);

  std::snprintf(buf, sizeof(buf), "S%7dG%7dD%7dP%7d%40sT%7d", 1, static_cast<int>(glines.size()), 2, p_count, "", 1);
  out << buf << "\n";

  return out.good() ? Result::Ok : Result::Failed;
}

namespace {

// Builds an OpenNURBS curve from a full knot vector (cv_count + order
// knots), Euclidean control points and weights (empty = non-rational) -
// identical to FileIgesStep.cpp's own MakeNurbsCurve.
bool IgesMakeNurbsCurve(int degree, const std::vector<ON_3dPoint>& cvs, const std::vector<double>& weights,
                        const std::vector<double>& full_knots, ON_NurbsCurve& out) {
  const int cv_count = static_cast<int>(cvs.size());
  if (degree < 1 || cv_count < degree + 1) return false;
  if (static_cast<int>(full_knots.size()) != cv_count + degree + 1) return false;
  const bool rational = !weights.empty();
  if (!out.Create(3, rational, degree + 1, cv_count)) return false;
  for (int i = 0; i < out.KnotCount(); ++i) out.SetKnot(i, full_knots[static_cast<size_t>(i + 1)]);
  for (int i = 0; i < cv_count; ++i) {
    if (rational) {
      const double w = weights[static_cast<size_t>(i)];
      out.SetCV(i, ON_4dPoint(cvs[static_cast<size_t>(i)].x * w, cvs[static_cast<size_t>(i)].y * w, cvs[static_cast<size_t>(i)].z * w, w));
    } else {
      out.SetCV(i, cvs[static_cast<size_t>(i)]);
    }
  }
  return out.IsValid();
}

// Splits IGES free-format parameter text on the given delimiters, honouring
// Hollerith strings (nHtext), which may themselves contain delimiter
// characters - identical to FileIgesStep.cpp's own SplitIgesParams.
std::vector<std::string> IgesSplitParamsForRead(const std::string& text, char pd, char rd) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= text.size()) {
    size_t j = i;
    if (j < text.size() && std::isdigit(static_cast<unsigned char>(text[j]))) {
      size_t k = j;
      while (k < text.size() && std::isdigit(static_cast<unsigned char>(text[k]))) ++k;
      if (k < text.size() && text[k] == 'H') {
        const size_t n = static_cast<size_t>(std::atol(text.substr(j, k - j).c_str()));
        j = std::min(text.size(), k + 1 + n);
        out.push_back(text.substr(i, j - i));
        if (j < text.size() && (text[j] == pd || text[j] == rd)) ++j;
        i = j;
        if (j >= text.size()) break;
        continue;
      }
    }
    while (j < text.size() && text[j] != pd && text[j] != rd) ++j;
    out.push_back(text.substr(i, j - i));
    if (j >= text.size()) break;
    if (text[j] == rd) break;
    i = j + 1;
  }
  return out;
}

double IgesPNum(const std::vector<std::string>& p, size_t i) {
  if (i >= p.size() || p[i].empty()) return 0.0;
  std::string s = p[i];
  for (char& c : s) if (c == 'D' || c == 'd') c = 'E';
  return std::atof(s.c_str());
}
int IgesPInt(const std::vector<std::string>& p, size_t i) {
  if (i >= p.size() || p[i].empty()) return 0;
  return std::atoi(p[i].c_str());
}

}  // namespace

Result NurbsCurve::LoadIges(const std::string& path, NurbsCurve& out) {
  std::ifstream is(path, std::ios::binary);
  if (!is) return Result::Failed;

  std::string line;
  std::vector<std::string> glines, dlines, plines;
  bool any_line = false;
  while (std::getline(is, line)) {
    any_line = true;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 73) line.resize(73, ' ');
    const char sect = line[72];
    if (sect == 'G') glines.push_back(line.substr(0, 72));
    else if (sect == 'D') dlines.push_back(line.substr(0, 72));
    // A Parameter Data line's columns 65-72 carry the entity's DE
    // back-pointer, not data - dropped the same way FileIgesStep.cpp's own
    // ImportIges front-end drops it, so it can't splice onto the next
    // continuation line's own leading token.
    else if (sect == 'P') plines.push_back(line.substr(0, 64));
    else if (sect == 'T') break;
  }
  if (!any_line || dlines.empty()) return Result::Failed;

  char pd = ',', rd = ';';
  {
    std::string g;
    for (const std::string& l : glines) g += l;
    if (g.rfind("1H", 0) == 0 && g.size() > 2) {
      pd = g[2];
      size_t i = 3;
      if (i < g.size() && g[i] == pd) ++i;
      if (i < g.size() && g.compare(i, 2, "1H") == 0 && g.size() > i + 2) rd = g[i + 2];
    }
  }

  // Directory entries: two fixed-width 72-usable-column lines each.
  for (size_t i = 0; i + 1 < dlines.size(); i += 2) {
    auto field = [&](const std::string& l, int col) { return IgesTrim(l.substr(static_cast<size_t>(col) * 8, 8)); };
    const int type = std::atoi(field(dlines[i], 0).c_str());
    if (type != 126) continue;  // this reader's own disclosed scope: the first Type 126 entity only
    const int pd_ptr = std::atoi(field(dlines[i], 1).c_str());
    const int line_count = std::max(1, std::atoi(field(dlines[i + 1], 3).c_str()));
    if (pd_ptr < 1) return Result::Failed;
    std::string text;
    for (int k = 0; k < line_count && pd_ptr - 1 + k < static_cast<int>(plines.size()); ++k) {
      text += plines[static_cast<size_t>(pd_ptr - 1 + k)];
    }
    const size_t comma = text.find(pd);
    if (comma == std::string::npos) return Result::Failed;
    const std::vector<std::string> p = IgesSplitParamsForRead(text.substr(comma + 1), pd, rd);

    const int K = IgesPInt(p, 0), M = IgesPInt(p, 1);
    if (K < 0 || K > 100000 || M < 1 || M > 1000) return Result::Failed;
    const int n = K + 1, degree = M;
    size_t idx = 6;
    std::vector<double> knots(static_cast<size_t>(n + degree + 1));
    for (auto& k : knots) k = IgesPNum(p, idx++);
    std::vector<double> weights(static_cast<size_t>(n));
    for (auto& wv : weights) wv = IgesPNum(p, idx++);
    const bool all_one = std::all_of(weights.begin(), weights.end(), [](double w) { return std::fabs(w - 1.0) < 1e-9; });
    std::vector<ON_3dPoint> cvs(static_cast<size_t>(n));
    for (auto& q : cvs) {
      q.x = IgesPNum(p, idx++);
      q.y = IgesPNum(p, idx++);
      q.z = IgesPNum(p, idx++);
    }
    ON_NurbsCurve nc;
    if (!IgesMakeNurbsCurve(degree, cvs, all_one ? std::vector<double>() : weights, knots, nc)) return Result::Failed;
    out.raw() = nc;
    return Result::Ok;
  }
  return Result::Failed;  // no Type 126 entity found
}

}  // namespace dino8::kernel
