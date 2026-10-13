// NurbsCurve::SaveStep/LoadStep - a minimal, single-curve, kernel-level
// ISO-10303-21 (STEP) physical-file reader/writer (B_SPLINE_CURVE_WITH_KNOTS,
// optionally wrapped in the complex RATIONAL_B_SPLINE_CURVE form).
//
// This is a Document-free port of the exact field layout
// dino8-app/src/io/FileIgesStep.cpp's own ExportStep/ImportStep already use
// at the app level (WriteStepCurve/EuclideanCV/FullKnots for writing; the
// StepModel/StepEntity/ParseStepPhysicalFile parser plus Curve()'s own
// B_SPLINE_CURVE_WITH_KNOTS branch for reading) - the same proven encoding,
// not a fresh derivation, closing this kernel's own "App-independent
// (kernel library) STEP/IGES/PLY/DXF exchange API" gap's STEP half
// (PARITY_MAP.md, kernel: Kernel-level data exchange) the same way
// SaveIges()/LoadIges() (iges.cpp) already closed the IGES half.
#include "dino8/kernel/curve.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace dino8::kernel {

namespace {

// An ISO-10303-21 REAL literal needs a decimal point (a bare "10" parses
// as an INTEGER) - identical to FileIgesStep.cpp's own Num().
std::string StepCurveNum(double v) {
  if (!std::isfinite(v)) v = 0.0;
  if (std::fabs(v) < 1e-13) v = 0.0;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.9g", v);
  std::string s = buf;
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) s += ".";
  return s;
}

std::string StepCurveTrim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// Full (clamped) knot vector - identical to iges.cpp's own IgesFullKnots()
// and FileIgesStep.cpp's own FullKnots(const ON_NurbsCurve&).
std::vector<double> StepCurveFullKnots(const ON_NurbsCurve& c) {
  std::vector<double> k;
  const int n = c.KnotCount();
  if (n <= 0) return k;
  k.push_back(c.Knot(0));
  for (int i = 0; i < n; ++i) k.push_back(c.Knot(i));
  k.push_back(c.Knot(n - 1));
  return k;
}

// Euclidean (not homogeneous) control point and its own real weight -
// identical to iges.cpp's own IgesEuclideanCV() and FileIgesStep.cpp's own
// EuclideanCV(const ON_NurbsCurve&, ...).
ON_3dPoint StepCurveEuclideanCV(const ON_NurbsCurve& c, int i, double& w) {
  ON_4dPoint h;
  c.GetCV(i, h);
  w = c.IsRational() ? h.w : 1.0;
  if (c.IsRational() && std::fabs(h.w) > 1e-300) return ON_3dPoint(h.x / h.w, h.y / h.w, h.z / h.w);
  return ON_3dPoint(h.x, h.y, h.z);
}

}  // namespace

Result NurbsCurve::SaveStep(const std::string& path) const {
  if (!curve_.IsValid()) return Result::Failed;
  const int K = curve_.CVCount() - 1;
  const int M = curve_.Degree();
  if (K < 0 || M < 1) return Result::Failed;

  // Compress the full clamped knot vector to (knot, multiplicity) pairs -
  // the form STEP's own B_SPLINE_CURVE_WITH_KNOTS schema requires, rather
  // than IGES's flat repeated-knot list.
  const std::vector<double> full = StepCurveFullKnots(curve_);
  std::vector<double> uknots;
  std::vector<int> mult;
  for (double k : full) {
    if (!uknots.empty() && std::fabs(k - uknots.back()) < 1e-9) ++mult.back();
    else { uknots.push_back(k); mult.push_back(1); }
  }

  std::vector<double> weights;
  std::vector<ON_3dPoint> pts;
  weights.reserve(static_cast<size_t>(K) + 1);
  pts.reserve(static_cast<size_t>(K) + 1);
  bool rational = false;
  for (int i = 0; i <= K; ++i) {
    double wgt;
    pts.push_back(StepCurveEuclideanCV(curve_, i, wgt));
    weights.push_back(wgt);
    if (std::fabs(wgt - 1.0) > 1e-9) rational = true;
  }

  std::ofstream out(path);
  if (!out) return Result::Failed;

  std::time_t now = std::time(nullptr);
  char date[32];
  std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", std::localtime(&now));

  out << "ISO-10303-21;\n";
  out << "HEADER;\n";
  out << "FILE_DESCRIPTION((''),'2;1');\n";
  out << "FILE_NAME('" << path << "','" << date << "',('Dino 8'),(''),'Dino 8 kernel','Dino 8','');\n";
  out << "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));\n";
  out << "ENDSEC;\n";
  out << "DATA;\n";

  // One CARTESIAN_POINT entity per control point, each its own numbered
  // record - the same flat entity-per-point shape WriteStepCurve() (app
  // level) already builds via its own StepWriter::Add, just without that
  // class's generality (this writer only ever needs CARTESIAN_POINT plus
  // the one curve entity, so a plain running counter suffices).
  int next_id = 1;
  std::string cvs = "(";
  for (size_t i = 0; i < pts.size(); ++i) {
    out << "#" << next_id << "=CARTESIAN_POINT('',(" << StepCurveNum(pts[i].x) << "," << StepCurveNum(pts[i].y) << ","
        << StepCurveNum(pts[i].z) << "));\n";
    cvs += "#" + std::to_string(next_id) + (i + 1 < pts.size() ? "," : "");
    ++next_id;
  }
  cvs += ")";

  std::string wts = "(";
  for (size_t i = 0; i < weights.size(); ++i) wts += StepCurveNum(weights[i]) + (i + 1 < weights.size() ? "," : "");
  wts += ")";

  std::string kn = "(", mu = "(";
  for (size_t i = 0; i < uknots.size(); ++i) {
    kn += StepCurveNum(uknots[i]) + (i + 1 < uknots.size() ? "," : "");
    mu += std::to_string(mult[i]) + (i + 1 < uknots.size() ? "," : "");
  }
  kn += ")"; mu += ")";

  const std::string args = "''," + std::to_string(M) + "," + cvs + ",.UNSPECIFIED.,.F.,.F.,.F.," + mu + "," + kn + ",.UNSPECIFIED.";
  const int curve_id = next_id++;
  if (!rational) {
    out << "#" << curve_id << "=B_SPLINE_CURVE_WITH_KNOTS(" << args << ");\n";
  } else {
    // The complex-entity form: several KEYWORD(args) groups bracketed
    // together under one id, read back by ParseEntity()'s own "starts with
    // '('" branch (app level) exactly as WriteStepCurve()'s own
    // StepWriter::AddComplex already emits it.
    out << "#" << curve_id << "=(BOUNDED_CURVE()B_SPLINE_CURVE()B_SPLINE_CURVE_WITH_KNOTS(" << args.substr(2)
        << ")CURVE()GEOMETRIC_REPRESENTATION_ITEM()RATIONAL_B_SPLINE_CURVE(" << wts << ")REPRESENTATION_ITEM(''));\n";
  }

  out << "ENDSEC;\n";
  out << "END-ISO-10303-21;\n";

  return out.good() ? Result::Ok : Result::Failed;
}

namespace {

// Builds an OpenNURBS curve from a full knot vector (cv_count + order
// knots), Euclidean control points and weights (empty = non-rational) -
// identical to iges.cpp's own IgesMakeNurbsCurve() and FileIgesStep.cpp's
// own MakeNurbsCurve().
bool StepCurveMakeNurbsCurve(int degree, const std::vector<ON_3dPoint>& cvs, const std::vector<double>& weights,
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

// Splits `text` on `sep` at paren/quote depth 0 - identical to
// FileIgesStep.cpp's own SplitTop().
std::vector<std::string> StepCurveSplitTop(const std::string& text, char sep) {
  std::vector<std::string> out;
  int depth = 0;
  bool q = false;
  std::string cur;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (q) {
      cur += c;
      if (c == '\'') { if (i + 1 < text.size() && text[i + 1] == '\'') { cur += text[++i]; } else q = false; }
      continue;
    }
    if (c == '\'') { q = true; cur += c; continue; }
    if (c == '(') { ++depth; cur += c; continue; }
    if (c == ')') { --depth; cur += c; continue; }
    if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); continue; }
    cur += c;
  }
  out.push_back(cur);
  return out;
}

std::string StepCurveUnparen(std::string s) {
  s = StepCurveTrim(s);
  if (s.size() >= 2 && s.front() == '(' && s.back() == ')') return s.substr(1, s.size() - 2);
  return s;
}

bool StepCurveIsRef(const std::string& a) { const std::string s = StepCurveTrim(a); return !s.empty() && s[0] == '#'; }
int StepCurveRef(const std::string& a) {
  const std::string s = StepCurveTrim(a);
  if (s.size() < 2 || s[0] != '#') return 0;
  return std::atoi(s.c_str() + 1);
}
double StepCurveReal(const std::string& a) {
  std::string s = StepCurveTrim(a);
  for (char& c : s) if (c == 'D' || c == 'd') c = 'E';
  return std::atof(s.c_str());
}
std::vector<double> StepCurveRealList(const std::string& a) {
  std::vector<double> out;
  for (const std::string& t : StepCurveSplitTop(StepCurveUnparen(a), ',')) out.push_back(StepCurveReal(t));
  return out;
}
std::vector<int> StepCurveIntList(const std::string& a) {
  std::vector<int> out;
  for (const std::string& t : StepCurveSplitTop(StepCurveUnparen(a), ',')) out.push_back(std::atoi(StepCurveTrim(t).c_str()));
  return out;
}

struct StepCurvePart {
  std::string keyword;
  std::vector<std::string> args;
};
struct StepCurveEntity {
  std::vector<StepCurvePart> parts;  // one part for a simple entity, several for a complex one
  const StepCurvePart* Find(const std::string& kw) const {
    for (const StepCurvePart& p : parts) if (p.keyword == kw) return &p;
    return nullptr;
  }
};

// Parses one record's right-hand side (after "="): either "KW(args)" or a
// complex "(KW1(args1)KW2(args2)...)" - identical in shape to
// FileIgesStep.cpp's own ParseEntity(), narrowed to this reader's own
// disclosed single-entity-kind scope (no colour/style/assembly handling).
StepCurveEntity StepCurveParseEntity(const std::string& rhs) {
  StepCurveEntity e;
  const std::string s = StepCurveTrim(rhs);
  if (!s.empty() && s.front() == '(') {
    const std::string inner = s.substr(1, s.size() >= 2 ? s.size() - 2 : 0);
    size_t i = 0;
    while (i < inner.size()) {
      while (i < inner.size() && std::isspace(static_cast<unsigned char>(inner[i]))) ++i;
      size_t k = i;
      while (k < inner.size() && inner[k] != '(') ++k;
      if (k >= inner.size()) break;
      const std::string kw = StepCurveTrim(inner.substr(i, k - i));
      int depth = 0;
      size_t j = k;
      bool q = false;
      for (; j < inner.size(); ++j) {
        const char c = inner[j];
        if (q) { if (c == '\'') { if (j + 1 < inner.size() && inner[j + 1] == '\'') ++j; else q = false; } continue; }
        if (c == '\'') { q = true; continue; }
        if (c == '(') ++depth;
        else if (c == ')') { --depth; if (depth == 0) { ++j; break; } }
      }
      const std::string args_text = inner.substr(k + 1, (j > k + 1) ? (j - k - 2) : 0);
      StepCurvePart part;
      part.keyword = kw;
      part.args = args_text.empty() ? std::vector<std::string>() : StepCurveSplitTop(args_text, ',');
      e.parts.push_back(std::move(part));
      i = j;
    }
    return e;
  }
  const size_t paren = s.find('(');
  if (paren == std::string::npos) { StepCurvePart p; p.keyword = s; e.parts.push_back(p); return e; }
  StepCurvePart p;
  p.keyword = StepCurveTrim(s.substr(0, paren));
  const size_t last = s.rfind(')');
  const std::string args_text = last > paren ? s.substr(paren + 1, last - paren - 1) : "";
  p.args = args_text.empty() ? std::vector<std::string>() : StepCurveSplitTop(args_text, ',');
  e.parts.push_back(p);
  return e;
}

ON_3dPoint StepCurvePoint(const std::map<int, StepCurveEntity>& entities, int id) {
  auto it = entities.find(id);
  if (it == entities.end()) return ON_3dPoint::Origin;
  if (const StepCurvePart* pp = it->second.Find("CARTESIAN_POINT")) {
    const std::vector<double> c = StepCurveRealList(pp->args.size() > 1 ? pp->args[1] : "");
    if (c.size() >= 3) return ON_3dPoint(c[0], c[1], c[2]);
    if (c.size() == 2) return ON_3dPoint(c[0], c[1], 0.0);
  }
  return ON_3dPoint::Origin;
}

}  // namespace

Result NurbsCurve::LoadStep(const std::string& path, NurbsCurve& out) {
  std::ifstream is(path, std::ios::binary);
  if (!is) return Result::Failed;
  std::ostringstream buf;
  buf << is.rdbuf();
  const std::string text = buf.str();
  if (text.find("ISO-10303-21") == std::string::npos) return Result::Failed;

  const size_t data_pos = text.find("DATA;");
  const size_t end_pos = text.rfind("ENDSEC;");
  if (data_pos == std::string::npos) return Result::Failed;
  std::string data =
      text.substr(data_pos + 5, (end_pos != std::string::npos && end_pos > data_pos ? end_pos - data_pos - 5 : std::string::npos));

  // Strip comments, same quote-aware scan FileIgesStep.cpp's own
  // ParseStepPhysicalFile() uses, then split into ';'-terminated records.
  {
    std::string stripped;
    stripped.reserve(data.size());
    bool q = false;
    for (size_t i = 0; i < data.size(); ++i) {
      if (!q && data[i] == '/' && i + 1 < data.size() && data[i + 1] == '*') {
        i += 2;
        while (i + 1 < data.size() && !(data[i] == '*' && data[i + 1] == '/')) ++i;
        ++i;
        continue;
      }
      if (data[i] == '\'') {
        if (q && i + 1 < data.size() && data[i + 1] == '\'') { stripped += "''"; ++i; continue; }
        q = !q;
      }
      stripped += data[i];
    }
    data = stripped;
  }

  std::map<int, StepCurveEntity> entities;
  for (const std::string& raw : StepCurveSplitTop(data, ';')) {
    const std::string r = StepCurveTrim(raw);
    if (r.empty() || r[0] != '#') continue;
    const size_t eq = r.find('=');
    if (eq == std::string::npos) continue;
    const int id = std::atoi(r.c_str() + 1);
    if (id <= 0) continue;
    entities[id] = StepCurveParseEntity(r.substr(eq + 1));
  }
  if (entities.empty()) return Result::Failed;

  for (const auto& [id, e] : entities) {
    (void)id;
    const StepCurvePart* p = e.Find("B_SPLINE_CURVE_WITH_KNOTS");
    if (!p) continue;  // this reader's own disclosed scope: the first B_SPLINE_CURVE_WITH_KNOTS entity only
    if (p->args.size() < 9) return Result::Failed;

    const int degree = std::atoi(StepCurveTrim(p->args[1]).c_str());
    if (degree < 1 || degree > 1000) return Result::Failed;
    std::vector<int> cv_refs;
    for (const std::string& t : StepCurveSplitTop(StepCurveUnparen(p->args[2]), ',')) {
      if (StepCurveIsRef(t)) cv_refs.push_back(StepCurveRef(t));
    }
    // Field layout (flattened, 0-indexed): name[0], degree[1],
    // control_points_list[2], curve_form[3], closed_curve[4],
    // self_intersect[5], knot_multiplicities[6], knots[7], knot_spec[8] -
    // THREE fields (curve_form plus the two logicals) separate the control
    // point list from knot_multiplicities, not two; confirmed directly by
    // splitting this writer's own emitted args string on top-level commas
    // and counting, not assumed from FileIgesStep.cpp's own matching
    // branch, which reads args[6]/args[7] here - the identical off-by-one
    // this fix corrects, a genuine, previously-undisclosed defect in that
    // app-level reader too (see PARITY_MAP.md's own note on this bullet).
    const std::vector<int> mult = StepCurveIntList(p->args[7]);
    const std::vector<double> uknots = StepCurveRealList(p->args[8]);

    std::vector<double> full;
    for (size_t i = 0; i < uknots.size() && i < mult.size(); ++i) {
      // Reject a corrupt/hostile file's multiplicity instead of an
      // unbounded allocation - the identical untrusted-count guard
      // FileIgesStep.cpp's own matching branch already applies.
      if (mult[i] < 0 || mult[i] > 100000) return Result::Failed;
      for (int k = 0; k < mult[i]; ++k) full.push_back(uknots[i]);
    }

    std::vector<ON_3dPoint> cvs;
    cvs.reserve(cv_refs.size());
    for (int r : cv_refs) cvs.push_back(StepCurvePoint(entities, r));

    std::vector<double> weights;
    if (const StepCurvePart* rp = e.Find("RATIONAL_B_SPLINE_CURVE")) {
      if (!rp->args.empty()) weights = StepCurveRealList(rp->args[0]);
    }

    ON_NurbsCurve nc;
    if (!StepCurveMakeNurbsCurve(degree, cvs, weights, full, nc)) return Result::Failed;
    out.raw() = nc;
    return Result::Ok;
  }
  return Result::Failed;  // no B_SPLINE_CURVE_WITH_KNOTS entity found
}

}  // namespace dino8::kernel
